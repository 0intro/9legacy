#include <pthread.h>
#include <errno.h>
#include "lib.h"

int
pthread_setspecific(pthread_key_t key, const void *p)
{
	int i;
	pthread_t pid;

	pid = pthread_self();
	lock(&key.l);
	/* exactly match */
	for(i = 0; i < key.n; i++)
		if(key.arenas[i].pid == pid){
			key.arenas[i].p = p;
			unlock(&key.l);
			return 0;
		}
	/* unused */
	for(i = 0; i < key.n; i++)
		if(key.arenas[i].pid == 0){
			key.arenas[i].pid = pid;
			key.arenas[i].p = p;
			unlock(&key.l);
			return 0;
		}
	unlock(&key.l);
	return ENOMEM;
}
