#ifndef _SUSV2_SOURCE
#error "inttypes.h is SUSV2"
#endif

#ifndef _INTTYPES_H_
#define _INTTYPES_H_ 1

#include "_apetypes.h"

#ifdef _BITS64
typedef long long _intptr_t;
typedef unsigned long long _uintptr_t;
#else
typedef int _intptr_t;
typedef unsigned int _uintptr_t;
#endif

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef _intptr_t intptr_t;
typedef _uintptr_t uintptr_t;

typedef unsigned char u8int;
typedef unsigned short u16int;
#ifndef _U32INT_
#define _U32INT_
typedef unsigned int u32int;
#endif
#ifndef _U64INT_
#define _U64INT_
typedef unsigned long long u64int;
#endif
typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef unsigned long long u_int64_t;

#endif
