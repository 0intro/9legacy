#include <u.h>
#include <libc.h>

/*
 * smoke exercises a freshly built system. It checks that the
 * compiler and library produce correct code, then stresses the
 * allocator, processes, pipes, the file system and the network.
 * Each check prints a line and any failure exits non-empty.
 */

enum
{
	Nblock	= 20000,		/* allocator blocks per pass */
	Nproc	= 16,			/* concurrent children */
	Work	= 2000000,		/* iterations of arithmetic per child */
	Nio	= 8*1024*1024,		/* bytes pushed through pipe and file */
	Chunk	= 8192,			/* i/o buffer size */
	Ndial	= 3,			/* network dial attempts */
};

char	*netaddr = "tcp!example.com!80";
char	*nethost = "example.com";
ulong	sink;

void
fail(char *fmt, ...)
{
	va_list a;
	char buf[ERRMAX];

	va_start(a, fmt);
	vseprint(buf, buf+sizeof buf, fmt, a);
	va_end(a);
	fprint(2, "smoke: %s\n", buf);
	exits("fail");
}

void
ok(char *what)
{
	print("smoke: %s ok\n", what);
}

/*
 * the compiler and library agree on integer, vlong and double code
 */
void
verify(void)
{
	vlong v;
	double d;
	char buf[32];

	v = 1;
	v <<= 40;
	if(v != 0x10000000000LL)
		fail("vlong shift: %lld", v);
	if(v/7 != 0x10000000000LL/7)
		fail("vlong divide");

	d = (double)v;
	if((vlong)d != v)
		fail("vlong<->double");
	d = 1.0/3.0;
	if(d*3.0 < 0.999999 || d*3.0 > 1.000001)
		fail("double arithmetic");

	snprint(buf, sizeof buf, "%lld", v);
	if(strtoll(buf, nil, 0) != v)
		fail("print and scan disagree: %s", buf);

	ok("verify");
}

/*
 * the allocator hands out distinct, writable, intact blocks
 */
void
memory(void)
{
	uchar **b;
	int *sz, i, j, c;

	b = malloc(Nblock*sizeof *b);
	sz = malloc(Nblock*sizeof *sz);
	if(b == nil || sz == nil)
		fail("malloc table: %r");
	srand(1);
	for(i = 0; i < Nblock; i++){
		sz[i] = 1 + nrand(1024);
		b[i] = malloc(sz[i]);
		if(b[i] == nil)
			fail("malloc %d: %r", sz[i]);
		memset(b[i], (i*7+1)&0xff, sz[i]);
	}
	for(i = 0; i < Nblock; i++){
		c = (i*7+1)&0xff;
		for(j = 0; j < sz[i]; j++)
			if(b[i][j] != c)
				fail("block %d byte %d corrupt", i, j);
		free(b[i]);
	}
	free(b);
	free(sz);
	ok("memory");
}

ulong
churn(ulong n)
{
	ulong i, s;

	s = 0;
	for(i = 1; i <= n; i++)
		s += (i*i) % 1000003;
	return s;
}

/*
 * fork stresses process creation, scheduling across cpus and wait
 */
void
processes(void)
{
	Waitmsg *w;
	int i;

	for(i = 0; i < Nproc; i++)
		switch(fork()){
		case -1:
			fail("fork: %r");
		case 0:
			sink = churn(Work);
			exits(nil);
		}
	for(i = 0; i < Nproc; i++){
		w = wait();
		if(w == nil)
			fail("wait: %r");
		if(w->msg[0] != 0)
			fail("child: %s", w->msg);
		free(w);
	}
	ok("processes");
}

/*
 * a child writes a known stream, the parent reads and checks every byte
 */
void
pipes(void)
{
	uchar *buf;
	int p[2], i, n;
	vlong off;

	if(pipe(p) < 0)
		fail("pipe: %r");
	switch(fork()){
	case -1:
		fail("fork: %r");
	case 0:
		close(p[0]);
		buf = malloc(Chunk);
		for(off = 0; off < Nio; off += n){
			for(i = 0; i < Chunk; i++)
				buf[i] = (off+i) & 0xff;
			n = Nio-off < Chunk ? Nio-off : Chunk;
			if(write(p[1], buf, n) != n)
				exits("write");
		}
		exits(nil);
	}
	close(p[1]);
	buf = malloc(Chunk);
	if(buf == nil)
		fail("malloc: %r");
	off = 0;
	while((n = read(p[0], buf, Chunk)) > 0){
		for(i = 0; i < n; i++)
			if(buf[i] != ((off+i) & 0xff))
				fail("pipe byte %lld corrupt", off+i);
		off += n;
	}
	if(off != Nio)
		fail("pipe short: %lld of %d", off, Nio);
	free(buf);
	ok("pipes");
}

/*
 * write a file on the root, read it back and compare, exercising 9p
 */
void
files(void)
{
	char *path = "/tmp/smoke.dat";
	uchar *buf;
	int fd, i, n;
	vlong off;

	buf = malloc(Chunk);
	if(buf == nil)
		fail("malloc: %r");
	fd = create(path, OWRITE, 0666);
	if(fd < 0)
		fail("create %s: %r", path);
	for(off = 0; off < Nio; off += n){
		for(i = 0; i < Chunk; i++)
			buf[i] = (off+i) & 0xff;
		n = Nio-off < Chunk ? Nio-off : Chunk;
		if(write(fd, buf, n) != n)
			fail("write %s: %r", path);
	}
	close(fd);

	fd = open(path, OREAD);
	if(fd < 0)
		fail("open %s: %r", path);
	off = 0;
	while((n = read(fd, buf, Chunk)) > 0){
		for(i = 0; i < n; i++)
			if(buf[i] != ((off+i) & 0xff))
				fail("file byte %lld corrupt", off+i);
		off += n;
	}
	if(off != Nio)
		fail("file short: %lld of %d", off, Nio);
	close(fd);
	remove(path);
	free(buf);
	ok("files");
}

/*
 * dial a server, send a request and confirm a sensible reply
 */
void
network(void)
{
	char buf[1024];
	int fd, i, n;

	fd = -1;
	for(i = 0; i < Ndial; i++){
		fd = dial(netaddr, nil, nil, nil);
		if(fd >= 0)
			break;
		sleep(1000);
	}
	if(fd < 0)
		fail("dial %s: %r", netaddr);
	if(fprint(fd, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n", nethost) < 0)
		fail("write %s: %r", netaddr);
	n = readn(fd, buf, sizeof buf - 1);
	if(n <= 0)
		fail("read %s: %r", netaddr);
	buf[n] = 0;
	if(strncmp(buf, "HTTP/", 5) != 0)
		fail("unexpected reply: %.16s", buf);
	close(fd);
	ok("network");
}

void
main(void)
{
	verify();
	memory();
	processes();
	pipes();
	files();
	network();
	print("smoke: all ok\n");
	exits(nil);
}
