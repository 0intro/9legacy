#include <pthread.h>
#include <errno.h>

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	pthread_t pid;

	pid = pthread_self();
	lock(&mutex->mu);
	if(mutex->pid != pid){
		unlock(&mutex->mu);
		return EPERM;
	}
	if(mutex->attr == PTHREAD_MUTEX_RECURSIVE){
		mutex->ref--;
		if(mutex->ref <= 0){
			mutex->pid = 0;
			mutex->ref = 0;
			qunlock(&mutex->l);
		}
		unlock(&mutex->mu);
		return 0;
	}
	mutex->ref--;
	qunlock(&mutex->l);
	unlock(&mutex->mu);
	return 0;
}
