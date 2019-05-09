#include <pthread.h>

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
