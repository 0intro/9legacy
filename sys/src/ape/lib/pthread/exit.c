#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include "lib.h"

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
	exit(0);
}
