#include <pthread.h>
#include <string.h>
#include "lib.h"

static Lock privlock;
static Thread privileges[PTHREAD_THREADS_MAX];

Thread *
_pthreadalloc(void)
{
	Thread *p, *ep;

	ep = privileges+nelem(privileges);
	lock(&privlock);
	for(p = privileges; p < ep; p++){
		if(!p->inuse){
			memset(p, 0, sizeof(*p));
			p->inuse = 1;
			unlock(&privlock);
			return p;
		}
	}
	unlock(&privlock);
	return NULL;
}

void
_pthreadsetpid(Thread *priv, pthread_t pid)
{
	lock(&privlock);
	priv->pid = pid;
	unlock(&privlock);
}

Thread *
_pthreadget(pthread_t pid)
{
	Thread *p, *ep;

	ep = privileges+nelem(privileges);
	lock(&privlock);
	for(p = privileges; p < ep; p++)
		if(p->inuse && p->pid == pid){
			unlock(&privlock);
			return p;
		}
	unlock(&privlock);
	return NULL;
}

void
_pthreadfree(Thread *priv)
{
	lock(&privlock);
	priv->inuse = 0;
	unlock(&privlock);
}
