#include "lib.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "sys9.h"
#include "dir.h"

/*
 * Called before main to initialize environ.
 * Some plan9 environment variables
 * have 0 bytes in them (notably $path);
 * we change them to 1's (and execve changes back)
 *
 * Also, register the note handler.
 */

char **environ;
int _envsize; /* allocated size for environ; including NULL of the end */
int _envcnt;
int errno;
unsigned long _clock;

static void fdsetup(char *, char *);
static void sigsetup(char *, char *);

void
_envsetup(void)
{
	int dfd, fdinited, n, nd, m, i, j, f, nohandle, cnt;
	char *p;
	char **pp;
	char name[NAME_MAX+5];
	Dir *d9, *d9a;
	static char **emptyenvp = 0;

	environ = emptyenvp;		/* pessimism */
	nohandle = 0;
	fdinited = 0;
	cnt = 0;
	strcpy(name, "#e");
	dfd = _OPEN(name, 0);
	if(dfd < 0)
		return;
	name[2] = '/';
	nd = _dirreadall(dfd, &d9a);
	_CLOSE(dfd);
	pp = malloc((1+nd)*sizeof(char *));
	if(pp == 0)
		return;
	for(j=0; j<nd; j++){
		d9 = &d9a[j];
		n = strlen(d9->name);
		if(n >= sizeof name - 4)
			continue;	/* shouldn't be possible */
		m = d9->length;
		p = malloc(n+m+2);
		if(p == 0){
			free(d9a);
			free(pp);
			return;
		}
		memcpy(p, d9->name, n);
		p[n] = '=';
		strcpy(name+3, d9->name);
		f = _OPEN(name, O_RDONLY);
		if(f < 0 || _READ(f, p+n+1, m) != m)
			m = 0;
		_CLOSE(f);
		if(p[n+m] == 0)
			m--;
		for(i=0; i<m; i++)
			if(p[n+1+i] == 0)
				p[n+1+i] = 1;
		p[n+1+m] = 0;
		if(strcmp(d9->name, "_fdinfo") == 0) {
			_fdinit(p+n+1, p+n+1+m);
			fdinited = 1;
		} else if(strcmp(d9->name, "_sighdlr") == 0)
			sigsetup(p+n+1, p+n+1+m);
		else if(strcmp(d9->name, "nohandle") == 0)
			nohandle = 1;
		pp[cnt++] = p;
	}
	free(d9a);
	if(!fdinited)
		_fdinit(0, 0);
	pp[cnt] = 0;
	environ = pp;
	_envsize = nd+1;
	_envcnt = cnt;
	if(!nohandle)
		_NOTIFY(_notehandler);
}

static void
sigsetup(char *s, char *se)
{
	int sig;
	char *e;

	while(s < se){
		sig = strtoul(s, &e, 10);
		if(s == e)
			break;
		s = e;
		if(sig <= MAXSIG)
			_sighdlr[sig] = SIG_IGN;
	}
}
