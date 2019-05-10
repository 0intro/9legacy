#include <pthread.h>

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	pthread_mutex_unlock(mutex);
	qlock(cond->r.l);
	rsleep(&cond->r);
	qunlock(cond->r.l);
	pthread_mutex_lock(mutex);
	return 0;
}
