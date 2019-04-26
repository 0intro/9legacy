#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "lib.h"

int
pthread_once(pthread_once_t *once_control, void (*init_routine) (void))
{
	lock(&once_control->l);
	if(once_control->once == 0){
		init_routine();
		once_control->once++;
	}
	unlock(&once_control->l);
	return 0;
}

pthread_t
pthread_self(void)
{
	return getpid();
}

int
pthread_equal(pthread_t t1, pthread_t t2)
{
	return t1 == t2;
}

int
pthread_create(pthread_t *t, pthread_attr_t *attr, void *(*f)(void*), void *arg)
{
	void *p;
	int pid;

	if(attr != NULL)
		return EINVAL;
	/*
	 * TODO: should return EAGAIN,
	 * if number of threads reached PTHREAD_THREADS_MAX
	 */
	pid = rfork(RFFDG|RFPROC|RFMEM);
	if(pid < 0){
		_syserrno();
		return errno;
	}
	if(pid == 0){
		p = fn(arg);
		thread_exit(p);
		abort(); /* can't reach here */
	}
	*t = pid;
	return 0;
}

void
pthread_exit(void *retval)
{
	/* TODO: back retval to parent */
	exits(nil);
}

static void
emitexits(void **ret, char *s)
{
	if(ret == NULL)
		return;
	if(s == NULL || *s == '\0'){
		*ret = 0;
		return;
	}
	*ret = 1;
}

static Lock msglock;
static Waitmsg *msgbuf[PTHREAD_THREADS_MAX];

int
pthread_join(pthread_t t, void **ret)
{
	Waitmsg **p, **ep, *msg;

	ep = msgbuf+PTHREAD_THREADS_MAX;
	lock(&msglock);
	for(p = msgbuf; p < ep; p++){
		if(*p == NULL)
			continue;
		if((*p)->pid == t){
			emitexits(ret, (*p)->msg);
			free(*p);
			*p = NULL;
			unlock(&msglock);
			return 0;
		}
	}
	unlock(&msglock);

again:
	msg = wait();
	if(msg == NULL)
		return ESRCH;
	if(msg->pid == t){
		emitexits(ret, msg->msg);
		free(msg);
		return 0;
	}
	lock(&msglock);
	for(p = msgbuf; p < ep; p++)
		if(*p != NULL)
			continue;
		*p = msg;
		unlock(&msglock);
		goto again;
	}
	unlock(&msglock);
	return EAGAIN;
}
