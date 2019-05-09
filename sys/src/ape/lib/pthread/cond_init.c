#include <pthread.h>
#include <string.h>

int
pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *)
{
	memset(cond, 0, sizeof(*cond));
	cond->r.l = &cond->l;
	return 0;
}
