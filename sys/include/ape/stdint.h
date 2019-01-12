#ifndef __STDINT_H
#define __STDINT_H

#include <inttypes.h>
#include <limits.h>

#define SIZE_MAX	ULONG_MAX
#define UINT8_MAX	0xff
#define UINT16_MAX	0xffff
#define UINT32_MAX	0xffffffffL
#define UINT64_MAX	0xffffffffffffffffLL
#define INT8_MAX	0x7f
#define INT16_MAX	0x7fff
#define INT32_MAX	0x7fffffffL
#define INT64_MAX	0x7fffffffffffffffLL

#endif
