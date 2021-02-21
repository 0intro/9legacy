#ifndef __STDDEF_H
#define __STDDEF_H

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif
#ifndef offsetof
#define offsetof(ty,mem) ((size_t) &(((ty *)0)->mem))
#endif

#include "_apetypes.h"

#ifdef _BITS64
typedef long long ptrdiff_t;
#else
typedef long ptrdiff_t;
#endif
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned long size_t;	/* even on 64-bit systems; see read(2) */
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
typedef unsigned long wchar_t;
#endif

#endif /* __STDDEF_H */
