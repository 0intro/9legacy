#include <pthread.h>
#include <stdlib.h>
#include <errno.h>

int
pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	*attr = 0;
	return 0;
}

int
pthread_mutexattr_destroy(pthread_mutexattr_t*)
{
	return 0;
}

int
pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind)
{
	*attr = kind;
	return 0;
}

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	memset(mu, 0, sizeof(*mutex));
	mutex->attr = *attr;
	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
	lock(mutex->l);
	return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	unlock(mutex->l);
	return 0;
}

int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	if(!canlock(mutex->l))
		return EBUSY;
	return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	if(!canlock(mutex->l))
		return EBUSY;
	return 0;
}
