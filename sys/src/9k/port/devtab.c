/*
 * Stub.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

extern Dev* devtab[];

void
devtabreset(void)
{
	int i;
	Dev *d;

	for(i = 0; devtab[i] != nil; i++){
		d = devtab[i];
		if(d->readv == nil)
			d->readv = devreadv;
		if(d->writev == nil)
			d->writev = devwritev;
		d->reset();
	}
}

void
devtabinit(void)
{
	int i;

	for(i = 0; devtab[i] != nil; i++)
		devtab[i]->init();
}

void
devtabshutdown(void)
{
	int i;

	/*
	 * Shutdown in reverse order.
	 */
	for(i = 0; devtab[i] != nil; i++)
		;
	for(i--; i >= 0; i--)
		devtab[i]->shutdown();
}


Dev*
devtabget(int dc, int user)
{
	int i;

	for(i = 0; devtab[i] != nil; i++){
		if(devtab[i]->dc == dc)
			return devtab[i];
	}

	if(user == 0)
		panic("devtabget %C\n", dc);

	return nil;
}

Dev*
devbyname(char *name)
{
	int i;

	for(i = 0; devtab[i] != nil; i++)
		if(strcmp(devtab[i]->name, name) == 0)
			return devtab[i];
	return nil;
}

long
devtabread(Chan*, void* buf, long n, vlong off)
{
	int i;
	Dev *dev;
	char *alloc, *e, *p;

	alloc = malloc(READSTR);
	if(alloc == nil)
		error(Enomem);

	p = alloc;
	e = p + READSTR;
	for(i = 0; devtab[i] != nil; i++){
		dev = devtab[i];
		p = seprint(p, e, "#%C %s\n", dev->dc, dev->name);
	}

	if(waserror()){
		free(alloc);
		nexterror();
	}
	n = readstr(off, buf, n, alloc);
	free(alloc);
	poperror();

	return n;
}
