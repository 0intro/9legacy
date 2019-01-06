#include <u.h>
#include <sys/types.h>
#include <libsec.h>

unsigned int
arc4random(void)
{
	unsigned int v;

	arc4random_buf(&v, sizeof v);
	return v;
}

void
arc4random_buf(void *buf, size_t nbytes)
{
	genrandom(buf, nbytes);
}
