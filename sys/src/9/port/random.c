#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<libsec.h>


struct Rb
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
	ushort	bits;
	ulong	randn;
} rb;

static int
rbnotfull(void*)
{
	int i;

	i = rb.rp - rb.wp;
	return i != 1 && i != (1 - sizeof(rb.buf));
}

static int
rbnotempty(void*)
{
	return rb.wp != rb.rp;
}

static void
randomproc(void*)
{
	up->basepri = PriNormal;
	up->priority = up->basepri;

	for(;;){
		for(;;)
			if(++rb.randomcount > 100000)
				break;
		if(anyhigher())
			sched();
		if(!rbnotfull(0))
			sleep(&rb.producer, rbnotfull, 0);
	}
}

/*
 *  produce random bits in a circular buffer
 */
static void
randomclock(void)
{
	if(rb.randomcount == 0 || !rbnotfull(0))
		return;

	rb.bits = (rb.bits<<2) ^ rb.randomcount;
	rb.randomcount = 0;

	rb.next++;
	if(rb.next != 8/2)
		return;
	rb.next = 0;

	*rb.wp ^= rb.bits;
	if(rb.wp+1 == rb.ep)
		rb.wp = rb.buf;
	else
		rb.wp = rb.wp+1;

	if(rb.wakeme)
		wakeup(&rb.consumer);
}

void
randominit(void)
{
	/* Frequency close but not equal to HZ */
	addclock0link(randomclock, 13);
	rb.ep = rb.buf + sizeof(rb.buf);
	rb.rp = rb.wp = rb.buf;
	kproc("genrandom", randomproc, 0);
}

/*
 *  consume random bytes from a circular buffer
 */
static void
seedfromrb(void *xp, int n)
{
	uchar *e, *p;
	ulong x;

	p = xp;

	if(waserror()){
		qunlock(&rb);
		nexterror();
	}

	qlock(&rb);
	for(e = p + n; p < e; ){
		if(rb.wp == rb.rp){
			rb.wakeme = 1;
			wakeup(&rb.producer);
			sleep(&rb.consumer, rbnotempty, 0);
			rb.wakeme = 0;
			continue;
		}

		/*
		 *  beating clocks will be predictable if
		 *  they are synchronized.  Use a cheap pseudo-
		 *  random number generator to obscure any cycles.
		 */
		x = rb.randn*1103515245 ^ *rb.rp;
		*p++ = rb.randn = x;

		if(rb.rp+1 == rb.ep)
			rb.rp = rb.buf;
		else
			rb.rp = rb.rp+1;
	}
	qunlock(&rb);
	poperror();

	wakeup(&rb.producer);
}

ulong
randomread(void *p, ulong n)
{
	static QLock lk;
	static Chachastate cs;
	static int seeded;
	uchar seed[32+12];
	Chachastate c;

	if(n == 0)
		return 0;

	qlock(&lk);
	if(waserror()){
		qunlock(&lk);
		nexterror();
	}
	if(!seeded){
		seedfromrb(seed, sizeof seed);
		setupChachastate(&cs, seed, 32, seed+32, 12, 20);
		memset(seed, 0, sizeof seed);
		seeded = 1;
	}

	/* copy chacha state, rekey and increment iv */
	c = cs;
	chacha_encrypt((uchar*)&cs.input[4], 32, &c);
	if(++cs.input[13] == 0)
		if(++cs.input[14] == 0)
			++cs.input[15];
	poperror();
	qunlock(&lk);

	/* encrypt the buffer, can fault */
	chacha_encrypt((uchar*)p, n, &c);

	/* prevent state leakage */
	memset(&c, 0, sizeof c);

	return n;
}
