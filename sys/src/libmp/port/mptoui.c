#include "os.h"
#include <mp.h>
#include "dat.h"

/*
 *  this code assumes that mpdigit is at least as
 *  big as an int.
 */

mpint*
uitomp(uint i, mpint *b)
{
	if(b == nil){
		b = mpnew(0);
		setmalloctag(b, getcallerpc(&i));
	}
	*b->p = i;
	b->top = 1;
	b->sign = 1;
	return mpnorm(b);
}

uint
mptoui(mpint *b)
{
	uint x;

	x = *b->p;
	if(b->sign < 0)
		x = 0;
	else if(b->top > 1 || (sizeof(mpdigit) > sizeof(uint) && x > MAXUINT))
		x =  MAXUINT;
	return x;
}
