#include <math.h>
#include <errno.h>
#define _RESEARCH_SOURCE
#include <float.h>

#define SIGN	(1<<31)

double
copysign(double x, double y)
{
	FPdbleword a, b;

	a.x = x;
	b.x = y;
	a.hi &= ~SIGN;
	a.hi |= b.hi & SIGN;
	return a.x;
}
