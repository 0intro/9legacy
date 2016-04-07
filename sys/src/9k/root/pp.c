#include <u.h>
#include <libc.h>

#define ESTR		256

/* ping pong. You have to tell it if is ping or pong. you have to tell it the partner 
 * address. ping sends packet, waits in loop, replies with packet. 
 * pong is same except does not send an initial packet. always wire to core 1. 
 */
static void
error(char* fmt, ...)
{
	va_list v;
	char *e, estr[ESTR], *p;

	va_start(v, fmt);
	e = estr + ESTR;
	p = seprint(estr, e, "%s: ", argv0);
	p = vseprint(p, e, fmt, v);
	p = seprint(p, e, "\n");
	va_end(v);

	write(2, estr, p-estr);
}

static void
fatal(char* fmt, ...)
{
	va_list v;
	char *e, estr[ESTR], *p;

	va_start(v, fmt);
	e = estr + ESTR;
	p = seprint(estr, e, "%s: ", argv0);
	p = vseprint(p, e, fmt, v);
	p = seprint(p, e, "\n");
	va_end(v);

	write(2, estr, p - estr);
	exits("fatal");
}

static void
usage(void)
{
	char *e, estr[ESTR], *p;

	e = estr + ESTR;
	p = seprint(estr, e, "usage: %s"
		" [whatever]"
		"\n",
		argv0);
	write(2, estr, p-estr);
	exits("usage");
}

#define F(v, o, w)	(((v) & ((1<<(w))-1))<<(o))

enum {
	X		= 0,			/* dimension */
	Y		= 1,
	Z		= 2,
	N		= 3,

	Chunk		= 32,			/* granularity of FIFO */
	Pchunk		= 7,			/* Chunks in a packet */

	Quad		= 16,
};

/*
 * Packet header. The hardware requires an 8-byte header
 * of which the last two are reserved (they contain a sequence
 * number and a header checksum inserted by the hardware).
 * The hardware also requires the packet to be aligned on a
 * 128-bit boundary for loading into the HUMMER.
 */
typedef struct Tpkt Tpkt;
struct Tpkt {
	u8int	sk;				/* Skip Checksum Control */
	u8int	hint;				/* Hint|Dp|Pid0 */
	u8int	size;				/* Size|Pid1|Dm|Dy|VC */
	u8int	dst[N];				/* Destination Coordinates */
	u8int	_6_[2];				/* reserved */
	u8int	_8_[8];				/* protocol header */
	u32int	payload[];
};

/*
 * SKIP is a field in .sk giving the number of 2-bytes
 * to skip from the top of the packet before including
 * the packet bytes into the running checksum.
 * SIZE is a field in .size giving the size of the
 * packet in 32-byte 'chunks'.
 */
#define SKIP(n)		F(n, 1, 7)
#define SIZE(n)		F(n, 5, 3)

enum {
	Sk		= 0x01,			/* Skip Checksum */

	Pid0		= 0x01,			/* Destination Group FIFO MSb */
	Dp		= 0x02,			/* Multicast Deposit */
	Hzm		= 0x04,			/* Z- Hint */
	Hzp		= 0x08,			/* Z+ Hint */
	Hym		= 0x10,			/* Y- Hint */
	Hyp		= 0x20,			/* Y+ Hint */
	Hxm		= 0x40,			/* X- Hint */
	Hxp		= 0x80,			/* X+ Hint */

	Vcd0		= 0x00,			/* Dynamic 0 VC */
	Vcd1		= 0x01,			/* Dynamic 1 VC */
	Vcbn		= 0x02,			/* Deterministic Bubble VC */
	Vcbp		= 0x03,			/* Deterministic Priority VC */
	Dy		= 0x04,			/* Dynamic Routing */
	Dm		= 0x08,			/* DMA Mode */
	Pid1		= 0x10,			/* Destination Group FIFO LSb */
};

static int
torusparse(u8int d[3], char* item, char* buf)
{
	int n;
	char *p;

	if((p = strstr(buf, item)) == nil || (p != buf && *(p-1) != '\n'))
		return -1;
	n = strlen(item);
	if(strlen(p) < n+sizeof(": x 0 y 0 z 0"))
		return -1;
	p += n+sizeof(": x ")-1;
	if(strncmp(p-4, ": x ", 4) != 0)
		return -1;
	if((n = strtol(p, &p, 0)) > 255 || *p != ' ' || *(p+1) != 'y')
		return -1;
	d[0] = n;
	if((n = strtol(p+2, &p, 0)) > 255 || *p != ' ' || *(p+1) != 'z')
		return -1;
	d[1] = n;
	if((n = strtol(p+2, &p, 0)) > 255 || (*p != '\n' && *p != '\0'))
		return -1;
	d[2] = n;

	return 0;
}

void wire(int core, int pri)
{

	int me = getpid();
	char *name = smprint("/proc/%d/ctl", me);
	int procfd = open(name, ORDWR);
	char *cmd;
	int amt;
	assert (procfd > 0);
	print("Wired to %d\n", core);
	cmd = smprint("wired %d\n", core);
	amt = write(procfd, cmd, strlen(cmd));
	assert(amt >= strlen(cmd));

	if (pri) {
		print("Pri to %d\n", pri);
		cmd = smprint("fixedpri %d\n", pri);
		amt = write(procfd, cmd, strlen(cmd));
		assert(amt >= strlen(cmd));
	}	
}

void
send(int fd, void *tpkt, int length, u64int *x)
{
	u64int start, end;
	cycles(&start);
	int n = pwrite(fd, tpkt, length, 0);
	cycles(&end);
	*x = end - start;
	if(n < 0)
		fatal("write /dev/torus: %r\n", n);
	else if(n < length)
		fatal("write /dev/torus: short write %d\n", n);
}

void
recv(int fd, void *rpkt, int length, u64int *x)
{
	int n;
	u64int start, end;
	cycles(&start);
	n = pread(fd, rpkt, length, 0);
	cycles(&end);
	*x = end - start;
	if(n < length)
		fatal("read /dev/torus: %r\n", n);
}
static void
dumptpkt(Tpkt* tpkt, int hflag, int dflag)
{
	uchar *t;
	int i, j, n;
	char buf[512], *e, *p;

	n = ((tpkt->size>>5)+1) * Chunk;

	p = buf;
	e = buf + sizeof(buf);
	if(hflag){
		p = seprint(p, e, "Hw:");
		t = (uchar*)tpkt;
		for(i = 0; i < 8; i++)
			p = seprint(p, e, " %2.2ux", t[i]);
		p = seprint(p, e, "\n");

		p = seprint(p, e, "Sw:");
		t = (uchar*)tpkt->_8_;
		for(i = 0; i < 8; i++)
			p = seprint(p, e, " %#2.2ux", t[i]);
		print("%s\n", buf);

	}

	if(!dflag)
		return;

	n -= sizeof(Tpkt);
	for(i = 0; i < n; i += 16){
		p = seprint(buf, e, "%4.4ux:", i);
		for(j = 0; j < 16; j++)
			seprint(p, e, " %2.2ux", tpkt->payload[i+j]);
		print("%s\n", buf);
	}
}


void
main(int argc, char* argv[])
{
	Tpkt *tpkt, *rpkt;
	u8int d[N];
	char buf[512], *p;
	uvlong r, start, stop;
	u64int *xtimes, *rtimes;
	int count, fd, i, length, mhz, n, x, y, z;
	int tracefd;
	int rank;
	int pri = 19;
	count = 1;
	length = Pchunk*Chunk;
	mhz = 850;

	ARGBEGIN{
	default:
		usage();
		break;
	case 'l':
		p = EARGF(usage());
		if((n = strtol(argv[0], &p, 0)) <= 0 || p == argv[0] || *p != 0)
			usage();
		if(n % Chunk)
			usage();
		length = n;
		break;
	case 'm':
		p = EARGF(usage());
		if((n = strtol(argv[0], &p, 0)) <= 0 || p == argv[0] || *p != 0)
			usage();
		mhz = n;
		break;
	case 'n':
		p = EARGF(usage());
		if((n = strtol(argv[0], &p, 0)) <= 0 || p == argv[0] || *p != 0)
			usage();
		count = n;
		break;
	}ARGEND;

	if(argc != 4)
		usage();
	if((x = strtol(argv[0], &p, 0)) < 0 || *p != 0)
		fatal("x invalid: %s\n", argv[0]);
	if((y = strtol(argv[1], &p, 0)) < 0 || *p != 0)
		fatal("y invalid: %s\n", argv[1]);
	if((z = strtol(argv[2], &p, 0)) <= 0 || *p != 0)
		fatal("z invalid: %s\n", argv[2]);
	if((rank= strtol(argv[3], &p, 0)) < 0 || *p != 0)
		fatal("rank invalid: %s\n", argv[3]);
	z -= 1;

	if((fd = open("/dev/torusstatus", OREAD)) < 0)
		fatal("open /dev/torusstatus: %r\n");
	if((n = read(fd, buf, sizeof(buf))) < 0)
		fatal("read /dev/torusstatus: %r\n");
	close(fd);
	buf[n] = 0;

	if(torusparse(d, "size", buf) < 0)
		fatal("parse /dev/torusstatus: <%s>\n", buf);
	if(x >= d[X] || y >= d[Y] || z >= d[Z])
		fatal("destination out of range: %d.%d.%d >= %d.%d.%d",
			x, y, z, d[X], d[Y], d[Z]);

	if((tpkt = mallocalign(length, Chunk, 0, 0)) == nil)
		fatal("mallocalign tpkt\n");
	memset(tpkt, 0, length);

	if((rpkt = mallocalign(length, Chunk, 0, 0)) == nil)
		fatal("mallocalign rptk\n");
	memset(rpkt, 0, length);

	xtimes = malloc(sizeof(*xtimes)*count);
	if (xtimes == nil)
		fatal("malloc x\n");
	rtimes = malloc(sizeof(*xtimes)*count);
	if (rtimes == nil)
		fatal("malloc r\n");
	tpkt->sk = SKIP(4);
	tpkt->hint = 0;
	tpkt->size = SIZE(Pchunk-1)|Dy|Vcd0;
	tpkt->dst[X] = x;
	tpkt->dst[Y] = y;
	tpkt->dst[Z] = z;

	if((fd = open("/dev/torus", ORDWR)) < 0)
		fatal("open /dev/torus: %r\n");

	tracefd = open("/dev/tracectl", ORDWR); 
	if (tracefd < 0)
		print("Warning: no trace device, no traces\n");

	wire(1, pri);

	if (tracefd > 0)
		if (write(tracefd, "start", 6) < 6)
			print("Warning: could not start trace device\n");

	cycles(&start);
	if (! rank){
		tpkt->payload[0] = 1;
		send(fd, tpkt, length, &xtimes[0]);
	}

	for(i = 0; i < count; i++){
		recv(fd, rpkt, length, &rtimes[i]);
		if (rpkt->payload[0] != i + 1)
			print("SEQ: Got %d expect %d\n", rpkt->payload[0], i);
		tpkt->payload[0] = rpkt->payload[0];
		if (! rank)
			tpkt->payload[0] ++;
		send(fd, tpkt, length, &xtimes[i]);
	}

	cycles(&stop);

	/* we may chop some off but tough */
	if (tracefd > 0)
		if(write(tracefd, "stop", 5) < 5)
			print("Warning: could not stop trace device\n");
;
	
	close(fd);

	r = (count*length);
	r *= mhz;
	r /= stop - start;

	print("%d writes of %d in %llud cycles @ %dMHz = %llud MB/s\n",
		count, length, stop - start, mhz, r);
	print("xmit\n");
	for(i = 0; i < count; i++)
		print("%d %lld\n", i, xtimes[i]);
	print("recv\n");
	for(i = 0; i < count; i++)
		print("%d %lld\n", i, rtimes[i]);
	
	exits(0);
}
