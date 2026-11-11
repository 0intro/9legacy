#include "lib.h"
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include "sys9.h"

char	end[];
static	char	*bloc = { end };
extern	int	_BRK_(void*);

int
brk(char *p)
{
	uintptr_t n;

	n = (uintptr_t)p + sizeof(uintptr_t) - 1;
	n &= ~((uintptr_t)sizeof(uintptr_t) - 1);
	if(_BRK_((void*)n) < 0){
		errno = ENOMEM;
		return -1;
	}
	bloc = (char *)n;
	return 0;
}

void *
sbrk(uintptr_t n)
{
	if ((intptr_t)n < 0)
		abort();
	n += sizeof(uintptr_t) - 1;
	n &= ~((uintptr_t)sizeof(uintptr_t) - 1);
	if(_BRK_((void *)(bloc+n)) < 0){
		errno = ENOMEM;
		return (void *)-1;
	}
	bloc += n;
	return (void *)(bloc-n);
}
