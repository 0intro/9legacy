#include <pthread.h>

int
pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind)
{
	*attr = kind;
	return 0;
}
