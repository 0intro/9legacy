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
_pthreadnew(pthread_t pid)
{
	Thread *p, *ep, *freep = NULL;

	ep = privileges+nelem(privileges);
	lock(&privlock);
	for(p = privileges; p < ep; p++){
		if(p->inuse && p->pid == pid){
			unlock(&privlock);
			return p;
		}
		if(freep == NULL && !p->inuse)
			freep = p;
	}
	if(freep == NULL){
		unlock(&privlock);
		return NULL;
	}
	memset(freep, 0, sizeof(*freep));
	freep->inuse = 1;
	freep->pid = pid;
	unlock(&privlock);
	return freep;
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
