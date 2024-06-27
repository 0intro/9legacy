#include "os.h"
#include <mp.h>
#include "dat.h"

// convert an mpint into a big endian byte array (most significant byte first; left adjusted)
//   return number of bytes converted
//   if p == nil, allocate and result array
int
mptobe(mpint *b, uchar *p, uint n, uchar **pp)
{
	int m;

	m = (mpsignif(b)+7)/8;
	if(m == 0)
		m++;
	if(p == nil){
		n = m;
		p = malloc(n);
		if(p == nil)
			sysfatal("mptobe: %r");
		setmalloctag(p, getcallerpc(&b));
	} else {
		if(n < m)
			return -1;
		if(n > m)
			memset(p+m, 0, n-m);
	}
	if(pp != nil)
		*pp = p;
	mptober(b, p, m);
	return m;
}
