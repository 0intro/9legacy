#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

static struct
{
	QLock;
	Rendez	producer;
	Rendez	consumer;
	ulong	randomcount;
	uchar	buf[1024];
	uchar	*ep;
	uchar	*rp;
	uchar	*wp;
	uchar	next;
	uchar	wakeme;
	uchar	filled;
	ushort	bits;
	ulong	randn;
	int	target;
} rb;

static int
rbnotfull(void*)
{
	int i;

	i = rb.wp - rb.rp;
	if(i < 0)
		i += sizeof(rb.buf);
	return i < rb.target;
}

static int
rbnotempty(void*)
{
	return rb.wp != rb.rp;
}

static void
genrandom(void*)
{
	up->basepri = PriNormal;
	up->priority = up->basepri;

	for(;;){
		for(;;)
			if(++rb.randomcount > 100000)
				break;
		if(anyhigher())
			sched();
		if(rb.filled || !rbnotfull(0))
			sleep(&rb.producer, rbnotfull, 0);
	}
}

/*
 *  produce random bits in a circular buffer
 */
static void
randomclock(void)
{
	uchar *p;

	if(rb.randomcount == 0)
		return;

	if(!rbnotfull(0)) {
		rb.filled = 1;
		return;
	}

	rb.bits = (rb.bits<<2) ^ rb.randomcount;
	rb.randomcount = 0;

	rb.next++;
	if(rb.next != 8/2)
		return;
	rb.next = 0;

	*rb.wp ^= rb.bits;
	p = rb.wp+1;
	if(p == rb.ep)
		p = rb.buf;
	rb.wp = p;

	if(rb.wakeme){
		rb.wakeme = 0;
		wakeup(&rb.consumer);
	}
}

void
randominit(void)
{
	/* Frequency close but not equal to HZ */
	addclock0link(randomclock, 13);
	rb.target = 16;
	rb.ep = rb.buf + sizeof(rb.buf);
	rb.rp = rb.wp = rb.buf;
	kproc("genrandom", genrandom, 0);
}

/*
 *  consume random bytes from a circular buffer
 */
ulong
randomread(void *xp, ulong n)
{
	uchar *e, *p, *r;
	ulong x;
	int i;

	p = xp;

	qlock(&rb);
	if(waserror()){
		qunlock(&rb);
		nexterror();
	}

	for(e = p + n; p < e; ){
		if(rb.wp == rb.rp){
			rb.wakeme = 1;
			wakeup(&rb.producer);
			sleep(&rb.consumer, rbnotempty, 0);
			continue;
		}

		/*
		 *  beating clocks will be predictable if
		 *  they are synchronized.  Use a cheap pseudo-
		 *  random number generator to obscure any cycles.
		 */
		x = rb.randn*1103515245 ^ *rb.rp;
		*p++ = rb.randn = x;

		r = rb.rp + 1;
		if(r == rb.ep)
			r = rb.buf;
		rb.rp = r;
	}
	if(rb.filled && rb.wp == rb.rp){
		i = 2*rb.target;
		if(i > sizeof(rb.buf) - 1)
			i = sizeof(rb.buf) - 1;
		rb.target = i;
		rb.filled = 0;
	}
	poperror();
	qunlock(&rb);

	wakeup(&rb.producer);

	return n;
}
