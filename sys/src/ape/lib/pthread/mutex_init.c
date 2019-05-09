#include <pthread.h>
#include <string.h>

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	memset(mutex, 0, sizeof(*mutex));
	if(attr)
		mutex->attr = *attr;
	return 0;
}
