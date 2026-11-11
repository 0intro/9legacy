#include <u.h>
#include <libc.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>
#include "rsa2any.h"

char*
encurl64(void *in, int n)
{
	int lim;
	char *out, *p;

	lim = 4*((n+2)/3) + 1;
	if((out = malloc(lim)) == nil)
		sysfatal("malloc: %r");
	enc64(out, lim, in, n);
	for(p = out; *p != 0; p++){
		if(*p == '+')
			*p = '-';
		else if(*p == '/')
			*p = '_';
		else if(*p == '='){
			*p = 0;
			break;
		}
	}
	return out;
}

void
usage(void)
{
	fprint(2, "usage: auth/rsa2pub [file]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	uchar nbuf[8192], ebuf[512];
	char *nstr, *estr, *s;
	RSApriv *k;
	int nlen, elen;

	fmtinstall('[', encodefmt);
	quotefmtinstall();

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();

	if((k = getkey(argc, argv, 0, nil)) == nil)
		sysfatal("%r");

	nlen = (mpsignif(k->pub.n)+7)/8;
	if(nlen >= sizeof(nbuf))
		sysfatal("key too big");
	mptobe(k->pub.n, nbuf, nlen, nil);
	nstr = encurl64(nbuf, nlen);

	elen = (mpsignif(k->pub.ek)+7)/8;
	if(elen >= sizeof(ebuf))
		sysfatal("key too big");
	mptobe(k->pub.ek, ebuf, elen, nil);
	estr = encurl64(ebuf, elen);

	s = smprint(
		"{"
		"\"kty\": \"RSA\","
		"\"n\": \"%s\","
		"\"e\": \"%s\""
		"}\n",
		nstr, estr);
	if(s == nil)
		sysfatal("smprint: %r");
	if(write(1, s, strlen(s))  != strlen(s))
		sysfatal("write: %r");
	exits(nil);
}
