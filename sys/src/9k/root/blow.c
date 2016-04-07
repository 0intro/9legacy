#include <u.h>
#include <libc.h>

#define ESTR		256

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
	Pchunk		= 8,			/* Chunks in a packet */

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
	u8int	payload[];
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

void
main(int argc, char* argv[])
{
	Tpkt *tpkt;
	u8int d[N];
	char buf[512], *p;
	uvlong r, start, stop;
	int count, fd, i, length, mhz, n, x, y, z, oldstyle;
	int tracefd;
	int procs = 1;
	int rank = 0;
	int pri = 19;

	count = 1;
	oldstyle = 0;
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
		if(n <= Chunk)
			usage();
		if(oldstyle){
			if(n % Chunk)
				usage();
		}
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
	case 'p':
		p = EARGF(usage());
		if((n = strtol(argv[0], &p, 0)) <= 0 || n > 4 || p == argv[0] || *p != 0)
			usage();
		procs = n;
		break;
	}ARGEND;

	if(argc != 3)
		usage();
	if((x = strtol(argv[0], &p, 0)) < 0 || *p != 0)
		fatal("x invalid: %d\n", argv[0]);
	if((y = strtol(argv[1], &p, 0)) < 0 || *p != 0)
		fatal("y invalid: %d\n", argv[1]);
	if((z = strtol(argv[2], &p, 0)) <= 0 || *p != 0)
		fatal("z invalid: %d\n", argv[2]);
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

	/* fork at bottom of loop since we are proc 0 */
	for(i = 0; i < procs; i++) {
		int me = getpid();
		char *name = smprint("/proc/%d/ctl", me);
		int procfd = open(name, ORDWR);
		char *cmd;
		int amt;
		assert (procfd > 0);
		rank = i;
		//print("Wired to %d\n", (rank+1)%4);
		cmd = smprint("wired %d\n", (rank+1)%4);
		amt = write(procfd, cmd, strlen(cmd));
		assert(amt >= strlen(cmd));

		if (pri) {
			//print("Pri to %d\n", pri);
			cmd = smprint("fixedpri %d\n", pri);
			amt = write(procfd, cmd, strlen(cmd));
			assert(amt >= strlen(cmd));
		}

		if (i < procs-1) 
		if (fork())
			break;	
	}
	

	sleep(1000); /* sync up forked processes) */

	if (tracefd > 0 && rank == 0)
		if (write(tracefd, "start", 6) < 6)
			print("Warning: could not start trace device\n");

	cycles(&start);
	for(i = 0; i < count; i++){
		n = pwrite(fd, tpkt, length, 0);
		if(n < 0)
			fatal("write /dev/torus: %r\n", n);
		else if(n < length)
			fatal("write /dev/torus: short write %d\n", n);
	}
	cycles(&stop);

	/* we may chop some off but tough */
	if (tracefd > 0 && rank == 0)
		if(write(tracefd, "stop", 5) < 5)
			print("Warning: could not stop trace device\n");
;
	
	close(fd);

	r = (count*length);
	r *= mhz;
	r /= stop - start;

	print("%d writes of %d in %llud cycles @ %dMHz = %llud MB/s\n",
		count, length, stop - start, mhz, r);

	exits(0);
}
