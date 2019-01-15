#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

int
pthread_once(pthread_once_t *once_control, void (*init_routine) (void))
{
	lock(&once_control->l);
	if(once_control->once == 0){
		init_routine();
		once_control->once++;
	}
	unlock(&once_control->l);
	return 0;
}

pthread_t
pthread_self(void)
{
	pthread_t t;

	t.pid = getpid();
	return t;
}

int
pthread_equal(pthread_t t1, pthread_t t2)
{
	return t1.pid == t2.pid;
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
	lock(mutex);
	return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	unlock(mutex);
	return 0;
}

int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	if(!canlock(mutex))
		return EBUSY;
	return 0;
}
