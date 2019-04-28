#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "lib.h"

int
pthread_key_create(pthread_key_t *key, void (*destr_func)(void*))
{
	if(destr_func)
		return EINVAL; /* don't implement yet */
	memset(key, 0, sizeof(*key));
	key->destroy = destr_func;
	key->n = 32;
	key->arenas = malloc(sizeof(*key->arenas)*key->n);
	if(key->arenas == NULL)
		return ENOMEM;
	memset(key->arenas, 0, sizeof(*key->arenas)*key->n);
	return 0;
}
