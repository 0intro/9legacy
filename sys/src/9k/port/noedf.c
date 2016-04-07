#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

void
edfinit(Proc*)
{
}

char*
edfadmit(Proc*)
{
	return "edf scheduling not implemented";
}

int
edfready(Proc*)
{
	return 0;
}

void
edfrecord(Proc*)
{
}

void
edfrun(Proc*, int)
{
}

void
edfstop(Proc*)
{
}

void
edfyield(void)
{
	yield();
}
