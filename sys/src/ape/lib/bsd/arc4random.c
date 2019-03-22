#include <u.h>
#include <sys/types.h>
#include <errno.h>
#include <libsec.h>

void
arc4random_buf(void *buf, size_t nbytes)
{
	genrandom(buf, nbytes);
}

unsigned int
arc4random(void)
{
	unsigned int v;

	arc4random_buf(&v, sizeof v);
	return v;
}

int
getentropy(void *buf, size_t len)
{
	if (len > 256) {
		errno = EIO;
		return -1;
	}
	genrandom(buf, len);
	return 0;
}
