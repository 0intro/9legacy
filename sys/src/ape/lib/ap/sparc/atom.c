#include "../plan9/lib.h"
#include "../plan9/sys9.h"

extern int tas(int*);

static int atomlock;

long
ainc(long *p)
{
	long v;

	while(tas(&atomlock))
		;
	v = (*p += 1);
	atomlock = 0;
	return v;
}

long
adec(long *p)
{
	long v;

	while(tas(&atomlock))
		;
	v = (*p -= 1);
	atomlock = 0;
	return v;
}
