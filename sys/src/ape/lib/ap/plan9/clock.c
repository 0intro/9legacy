#include <time.h>
#include <sys/times.h>
#include <lib9.h>
#include <errno.h>

#define NANO_IN_SEC 1000000000L

int
clock_gettime(clockid_t clock_id, struct timespec *tp)
{
	vlong ns;

	switch(clock_id){
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
		ns = nsec();
		tp->tv_sec = ns/NANO_IN_SEC;
		tp->tv_nsec = ns%NANO_IN_SEC;
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

int
clock_settime(clockid_t clock_id, struct timespec *tp)
{
	USED(clock_id);
	USED(tp);
	errno = EPERM;
	return -1;
}
