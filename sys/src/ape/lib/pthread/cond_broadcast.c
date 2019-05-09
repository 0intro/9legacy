#include <pthread.h>

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
	rwakeupall(&cond->r);
	return 0;
}
