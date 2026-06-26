#include <u.h>
#include <libc.h>

static int atomlock;

static void
taslock(void)
{
	while(_tas(&atomlock))
		;
}

static void
tasunlock(void)
{
	atomlock = 0;
}

long
ainc(long *p)
{
	long v;

	taslock();
	v = (*p += 1);
	tasunlock();
	return v;
}

long
adec(long *p)
{
	long v;

	taslock();
	v = (*p -= 1);
	tasunlock();
	return v;
}

void
_xinc(long *p)
{
	taslock();
	*p += 1;
	tasunlock();
}

long
_xdec(long *p)
{
	long v;

	taslock();
	v = (*p -= 1);
	tasunlock();
	return v;
}

int
cas(int *p, int ov, int nv)
{
	int r;

	taslock();
	r = *p == ov;
	if(r)
		*p = nv;
	tasunlock();
	return r;
}

int
cas32(u32int *p, u32int ov, u32int nv)
{
	int r;

	taslock();
	r = *p == ov;
	if(r)
		*p = nv;
	tasunlock();
	return r;
}

int
casp(void **p, void *ov, void *nv)
{
	int r;

	taslock();
	r = *p == ov;
	if(r)
		*p = nv;
	tasunlock();
	return r;
}

int
casl(ulong *p, ulong ov, ulong nv)
{
	int r;

	taslock();
	r = *p == ov;
	if(r)
		*p = nv;
	tasunlock();
	return r;
}
