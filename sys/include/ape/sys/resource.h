#ifndef __RESOURCE_H__
#define __RESOURCE_H__

#ifndef _BSD_EXTENSION
    This header file is an extension to ANSI/POSIX
#endif

#include <sys/time.h>

#pragma lib "/$M/lib/ape/libap.a"

enum {
	RUSAGE_SELF = 0,
	RUSAGE_CHILDREN = -1
};

#define	RUSAGE_SELF		RUSAGE_SELF
#define	RUSAGE_CHILDREN		RUSAGE_CHILDREN

struct rusage {
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
	long	ru_maxrss;		/* max resident set size */
#define	ru_first	ru_ixrss
	long	ru_ixrss;		/* integral shared memory size */
	long	ru_idrss;		/* integral unshared data " */
	long	ru_isrss;		/* integral unshared stack " */
	long	ru_minflt;		/* page reclaims */
	long	ru_majflt;		/* page faults */
	long	ru_nswap;		/* swaps */
	long	ru_inblock;		/* block input operations */
	long	ru_oublock;		/* block output operations */
	long	ru_msgsnd;		/* messages sent */
	long	ru_msgrcv;		/* messages received */
	long	ru_nsignals;		/* signals received */
	long	ru_nvcsw;		/* voluntary context switches */
	long	ru_nivcsw;		/* involuntary " */
#define	ru_last		ru_nivcsw
};

#ifdef __cplusplus
extern "C" {
#endif

extern int getrusage(int, struct rusage *);

#ifdef __cplusplus
}
#endif

#endif /* !__RESOURCE_H__ */
