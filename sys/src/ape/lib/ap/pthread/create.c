#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "lib.h"

extern int	_RFORK(int);
extern void	_syserrno(void);

int
pthread_create(pthread_t *t, pthread_attr_t *attr, void *(*f)(void*), void *arg)
{
	void *p;
	int pid;
	Thread *priv;

	if(attr != NULL)
		return EINVAL;
	priv = _pthreadalloc();
	if(priv == NULL)
		return EAGAIN;
	lock(&priv->l);
	pid = _RFORK(RFFDG|RFPROC|RFMEM);
	if(pid < 0){
		_syserrno();
		unlock(&priv->l);
		_pthreadfree(priv);
		return errno;
	}
	if(pid == 0){
		/* should wait for unlock by parent */
		lock(&priv->l);
		unlock(&priv->l);

		p = f(arg);
		pthread_exit(p);
		abort(); /* can't reach here */
	}
	_pthreadsetpid(priv, pid);
	*t = pid;
	unlock(&priv->l);
	return 0;
}
