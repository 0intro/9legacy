#include <pthread.h>

int
pthread_cond_destroy(pthread_cond_t *)
{
	/* TODO: should we check cond is busy? */
	return 0;
}
