#include <pthread.h>

int
pthread_cond_signal(pthread_cond_t *cond)
{
	qlock(cond->r.l);
	rwakeup(&cond->r);
	qunlock(cond->r.l);
	return 0;
}
