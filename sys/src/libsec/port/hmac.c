#include "os.h"
#include <libsec.h>

/* rfc2104 */
DigestState*
hmac_x(uchar *p, ulong len, uchar *key, ulong klen, uchar *digest, DigestState *s,
	DigestState*(*x)(uchar*, ulong, uchar*, DigestState*), int xlen)
{
	int i, blksz;
	uchar pad[Maxblksz+1], innerdigest[256], hkey[Maxdlen], *k;

	assert(xlen <= Maxdlen);

	/* MD4, MD5, SHA1 to SHA2_256 block sizes are identical */
	blksz = SHA1blksz;
	if(xlen > SHA2_256dlen)
		blksz = SHA2_384blksz; /* SHA2_384 and SHA2_512 */

	if(xlen > sizeof(innerdigest))
		return nil;
	if(klen <= blksz) 
		k = key;
	else {
		x(key, klen, hkey, nil);
		k = hkey;
		klen = xlen;
	}

	/* first time through */
	if(s == nil || s->seeded == 0){
		memset(pad, 0x36, blksz);
		pad[blksz] = 0;
		for(i = 0; i < klen; i++)
			pad[i] ^= k[i];
		s = (*x)(pad, blksz, nil, s);
		if(s == nil)
			return nil;
	}

	s = (*x)(p, len, nil, s);
	if(digest == nil)
		return s;

	/* last time through */
	memset(pad, 0x5c, blksz);
	pad[blksz] = 0;
	for(i = 0; i < klen; i++)
		pad[i] ^= k[i];
	(*x)(nil, 0, innerdigest, s);
	s = (*x)(pad, blksz, nil, nil);
	(*x)(innerdigest, xlen, digest, s);
	return nil;
}
