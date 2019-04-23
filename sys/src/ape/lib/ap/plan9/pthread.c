#include <pthread.h>
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
	return getpid();
}

int
pthread_equal(pthread_t t1, pthread_t t2)
{
	return t1 == t2;
}
