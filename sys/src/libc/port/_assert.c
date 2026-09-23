#include <u.h>
#include <libc.h>

void (*__assert)(char*);

void
_assert(char *s)
{
	if(__assert)
		(*__assert)(s);
	fprint(2, "assert failed, called from %#p: %s\n", getcallerpc(&s), s);
	abort();
}
