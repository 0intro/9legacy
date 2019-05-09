#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "lib.h"

extern int	_RFORK(int);
extern int	_RENDEZVOUS(unsigned long, unsigned long);

int
pthread_create(pthread_t *t, pthread_attr_t *attr, void *(*f)(void*), void *arg)
{
	void *p;
	int pid;
	Thread *priv;
	unsigned long tag;

	if(attr != NULL)
		return EINVAL;
	priv = _pthreadalloc();
	if(priv == NULL)
		return EAGAIN;
	tag = (unsigned long)priv;
	pid = _RFORK(RFFDG|RFPROC|RFMEM);
	switch(pid){
	case -1:
		_syserrno();
		unlock(&priv->l);
		_pthreadfree(priv);
		return errno;
	case 0:
		_RENDEZVOUS(tag, 0);
		p = f(arg);
		pthread_exit(p);
		abort(); /* can't reach here */
	default:
		_pthreadsetpid(priv, pid);
		_RENDEZVOUS(tag, 0);
		*t = pid;
	}
	return 0;
}
