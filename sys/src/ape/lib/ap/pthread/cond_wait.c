#include <pthread.h>

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	pthread_mutex_unlock(mutex);
	rsleep(&cond->r);
	pthread_mutex_lock(mutex);
	return 0;
}
