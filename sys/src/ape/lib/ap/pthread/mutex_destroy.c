#include <pthread.h>

int
pthread_mutex_destroy(pthread_mutex_t *)
{
	/* TODO: should we check mutex is busy? */
	return 0;
}
