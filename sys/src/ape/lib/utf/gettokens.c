#include <u.h>
#include <utf.h>

static char*
etoken(char *t, char *sep)
{
	int quoting;

	/* move to end of next token */
	quoting = 0;
	while(*t!='\0' && (quoting || utfrune(sep, *t)==nil)){
		if(*t != '\''){
			t++;
			continue;
		}
		/* *t is a quote */
		if(!quoting){
			quoting = 1;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if(t[1] != '\''){
			/* end of quoted section; absorb closing quote */
			t++;
			quoting = 0;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t += 2;
	}
	return t;
}

int
gettokens(char *s, char **args, int maxargs, char *sep)
{
	int nargs;

	for(nargs=0; nargs<maxargs; nargs++){
		while(*s!='\0' && utfrune(sep, *s)!=nil)
			*s++ = '\0';
		if(*s == '\0')
			break;
		args[nargs] = s;
		s = etoken(s, sep);
	}

	return nargs;
}
