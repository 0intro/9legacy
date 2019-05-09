#define _RESEARCH_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <libv.h>
#include "lib.h"

typedef
struct Waitmsg
{
	int	pid;	/* of loved one */
	ulong	time[3];	/* of loved one & descendants */
	char	*msg;
} Waitmsg;

extern int	_AWAIT(char*, int);

static Waitmsg*
_wait(void)
{
	int n, l;
	char buf[512], *fld[5];
	Waitmsg *w;

	n = _AWAIT(buf, sizeof buf-1);
	if(n < 0){
		_syserrno();
		return nil;
	}
	buf[n] = '\0';
	if(getfields(buf, fld, nelem(fld)) != nelem(fld)){
		errno = ENOBUFS;
		return nil;
	}
	l = strlen(fld[4])+1;
	w = malloc(sizeof(Waitmsg)+l);
	if(w == nil)
		return nil;
	w->pid = atoi(fld[0]);
	w->time[0] = atoi(fld[1]);
	w->time[1] = atoi(fld[2]);
	w->time[2] = atoi(fld[3]);
	w->msg = (char*)&w[1];
	memmove(w->msg, fld[4], l);
	return w;
}

static void
emitexits(void **ret, Thread *priv)
{
	if(ret == NULL){
		return;
	}
	*ret = priv->ret;
}

int
pthread_join(pthread_t t, void **ret)
{
	static Lock l;
	Thread *priv;
	int pid;
	Waitmsg *msg;

	lock(&l);
	priv = _pthreadget(t);
	if(priv == NULL){
		unlock(&l);
		return EINVAL;
	}
	lock(&priv->l);
	if(priv->exited){
		emitexits(ret, priv);
		unlock(&priv->l);
		_pthreadfree(priv);
		unlock(&l);
		return 0;
	}
	unlock(&priv->l);

	while((msg=_wait()) != NULL){
		pid = msg->pid;
		free(msg);
		if(pid == t)
			break;
	}
	lock(&priv->l);
	if(priv->exited){
		emitexits(ret, priv);
		unlock(&priv->l);
		_pthreadfree(priv);
		unlock(&l);
		return 0;
	}
	unlock(&priv->l);
	unlock(&l);
	return ESRCH;
}
