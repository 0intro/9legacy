#include <pthread.h>

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
	qlock(cond->r.l);
	rwakeupall(&cond->r);
	qunlock(cond->r.l);
	return 0;
}
