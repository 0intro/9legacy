/*
 * byte serialization
 */
#include <u.h>
#include <libc.h>

/* convert bytes to integers, by byte order and integer length */

uint
legeth(void *vp)
{
	uchar *p;

	p = vp;
	return p[1]<<8 | p[0];
}

uint
begeth(void *vp)
{
	uchar *p;

	p = vp;
	return p[0]<<8 | p[1];
}

uint
legetl(void *vp)
{
	uchar *p;

	p = vp;
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

uint
begetl(void *vp)
{
	uchar *p;

	p = vp;
	return p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];
}

uvlong
legetvl(void *vp)
{
	return (uvlong)legetl((uchar *)vp+4) << 32 | legetl(vp);
}

uvlong
begetvl(void *vp)
{
	return (uvlong)begetl(vp) << 32 | begetl((uchar *)vp+4);
}


/* convert integers to bytes, by byte order and integer length */

void *
leputh(void *vp, ushort l)
{
	uchar *p;

	p = vp;
	*p++ = l; l >>= 8;
	*p++ = l;
	return p;
}

void *
beputh(void *vp, ushort l)
{
	uchar *p, *ep;

	p = vp;
	p += sizeof l;
	ep = p;
	*--p = l; l >>= 8;
	*--p = l;
	return ep;
}

void *
leputl(void *vp, ulong l)
{
	uchar *p;

	p = vp;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l;
	return p;
}

void *
beputl(void *vp, ulong l)
{
	uchar *p, *ep;

	p = vp;
	p += sizeof l;
	ep = p;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l;
	return ep;
}

void *
leputvl(void *vp, uvlong l)
{
	uchar *p;

	p = vp;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l; l >>= 8;
	*p++ = l;
	return p;
}

void *
beputvl(void *vp, uvlong l)
{
	uchar *p, *ep;

	p = vp;
	p += sizeof l;
	ep = p;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l; l >>= 8;
	*--p = l;
	return ep;
}
