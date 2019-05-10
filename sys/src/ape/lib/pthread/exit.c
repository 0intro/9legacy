#include <pthread.h>
#include <assert.h>
#include "lib.h"

extern void	_EXITS(char *);

void
pthread_exit(void *retval)
{
	Thread *priv;
	pthread_t pid;

	pid = pthread_self();
	priv = _pthreadget(pid);
	assert(priv != NULL);
	lock(&priv->l);
	priv->exited = 1;
	priv->ret = retval;
	unlock(&priv->l);
	_EXITS(0);
}
