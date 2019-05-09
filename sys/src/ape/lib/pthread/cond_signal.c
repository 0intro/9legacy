#include <pthread.h>

int
pthread_cond_signal(pthread_cond_t *cond)
{
	rwakeup(&cond->r);
	return 0;
}
