#include <pthread.h>

int
pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *)
{
	memset(cond, 0, sizeof(*cond));
	cond->r.l = &cond->l;
	return 0;
}

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	qlock(&cond->l);
	rsleep(&cond->r);
	qunlock(&cond->l);
	return 0;
}

int
pthread_cond_signal(pthread_cond_t *cond)
{
	qlock(&cond->l);
	rwakeup(&cond->r);
	qunlock(&cond->l);
	return 0;
}

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
	qlock(&cond->l);
	rwakeupall(&cond->r);
	qunlock(&cond->l);
	return 0;
}

int
pthread_cond_destroy(pthread_cond_t *)
{
	/* TODO: should we check cond is busy? */
	return 0;
}
