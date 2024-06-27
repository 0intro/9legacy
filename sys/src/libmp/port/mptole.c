#include "os.h"
#include <mp.h>
#include "dat.h"

// convert an mpint into a little endian byte array (least significant byte first)

//   return number of bytes converted
//   if p == nil, allocate and result array
int
mptole(mpint *b, uchar *p, uint n, uchar **pp)
{
	int m;

	m = (mpsignif(b)+7)/8;
	if(m == 0)
		m++;
	if(p == nil){
		n = m;
		p = malloc(n);
		if(p == nil)
			sysfatal("mptole: %r");
		setmalloctag(p, getcallerpc(&b));
	} else if(n < m)
		return -1;
	if(pp != nil)
		*pp = p;
	mptolel(b, p, n);
	return m;
}
