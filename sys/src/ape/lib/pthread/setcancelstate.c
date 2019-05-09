#include <pthread.h>
#include <errno.h>
#include "lib.h"

int
pthread_setcancelstate(int state, int *oldstate)
{
	Thread *priv;
	pthread_t pid;

	pid = pthread_self();
	priv = _pthreadnew(pid);
	if(priv == NULL)
		return ENOMEM;
	lock(&priv->l);
	if(oldstate)
		*oldstate = priv->state;
	priv->state = state;
	unlock(&priv->l);
	return 0;
}
