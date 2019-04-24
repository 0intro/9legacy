#include <pthread.h>

int
pthread_key_create(pthread_key_t *key, void (*destr_func)(void*))
{
	void **p;

	if(destr_func)
		return EINVAL; /* don't implement yet */
	memset(key, 0, sizeof(*key));
	p = privalloc();
	if(p == NULL)
		return ENOMEM;
	*p = NULL;
	key->p = p;
	return 0;
}

int
pthread_key_delete(pthread_key_t key)
{
	*key.p = NULL;
	privfree(key.p);
	return 0;
}

void *
pthread_getspecific(pthread_key_t key)
{
	return *key.p;
}

extern int
pthread_setspecific(pthread_key_t key, const void *p)
{
	*key.p = p;
	return 0;
}
