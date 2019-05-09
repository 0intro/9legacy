#include <pthread.h>
#include <stdlib.h>
#include "lib.h"

int
pthread_key_delete(pthread_key_t key)
{
	free(key.arenas);
	return 0;
}
