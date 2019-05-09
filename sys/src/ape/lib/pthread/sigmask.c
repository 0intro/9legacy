#include <pthread.h>
#include <errno.h>
#include "lib.h"

int
pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
	Thread *priv;
	pthread_t pid;

	pid = pthread_self();
	priv = _pthreadnew(pid);
	if(priv == NULL)
		return ENOMEM;
	lock(&priv->l);
	if(oldset)
		*oldset = priv->sigset;
	priv->sigset = *set;
	unlock(&priv->l);
	return 0;
}
