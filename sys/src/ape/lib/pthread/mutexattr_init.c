#include <pthread.h>

int
pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	*attr = 0;
	return 0;
}
