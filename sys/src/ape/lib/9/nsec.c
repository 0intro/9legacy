#include <u.h>
#include "libc.h"

static uvlong border = 0x0001020304050607ull;

static uvlong
getbe(uchar *t, int w)
{
	uint i;
	uvlong r;

	r = 0;
	for(i = 0; i < w; i++)
		r = r<<8 | t[i];
	return r;
}

vlong
nsec(void)
{
	uchar b[8];
	int fd;
	vlong v;

	fd = _OPEN("/dev/bintime", OREAD);
	if(fd != -1 && _PREAD(fd, b, 8, 0) == 8)
		v = getbe(b, 8);
	else
		v = 0;
	_CLOSE(fd);
	return v;
}
