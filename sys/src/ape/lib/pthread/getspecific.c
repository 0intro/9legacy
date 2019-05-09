#include <pthread.h>
#include "lib.h"

void *
pthread_getspecific(pthread_key_t key)
{
	int i;
	pthread_t pid;
	const void *p;

	pid = pthread_self();
	lock(&key.l);
	for(i = 0; i < key.n; i++)
		if(key.arenas[i].pid == pid){
			p = key.arenas[i].p;
			unlock(&key.l);
			return (void *)p;
		}
	unlock(&key.l);
	return NULL;
}
