#include "lib.h"
#include <errno.h>
#include <unistd.h>
#include "sys9.h"
#include <pthread.h>

enum {
	NHANDLERS = 100
};

static void (*preparehdlr[NHANDLERS])(void);
static void (*parenthdlr[NHANDLERS])(void);
static void (*childhdlr[NHANDLERS])(void);
static int nprepare;
static int nparent;
static int nchild;

int
pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
	if(prepare != NULL){
		if(nprepare >= NHANDLERS)
			return ENOMEM;
		preparehdlr[nprepare++] = prepare;
	}
	if(parent != NULL){
		if(nparent >= NHANDLERS)
			return ENOMEM;
		parenthdlr[nparent++] = parent;
	}
	if(child != NULL){
		if(nchild >= NHANDLERS)
			return ENOMEM;
		childhdlr[nchild++] = child;
	}
	return 0;
}

pid_t
fork(void)
{
	int n, i;

	for(i = nprepare-1; i >= 0; i--)
		preparehdlr[i]();
	n = _RFORK(RFENVG|RFFDG|RFPROC);
	if(n < 0)
		_syserrno();
	if(n == 0) {
		_detachbuf();
		_sessleader = 0;
		for(i = 0; i < nchild; i++)
			childhdlr[i]();
		return 0;
	}
	for(i = 0; i < nparent; i++)
		parenthdlr[i]();
	return n;
}
