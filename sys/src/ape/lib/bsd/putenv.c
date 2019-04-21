#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern char **environ;
extern int _envsize;
extern int _envcnt;

int
putenv(char *s)
{
	int n;
	char **p, *s1, *s2;
	char *value;
	char name[300];

	value = strchr(s, '=');
	if(value == NULL){
		errno = EINVAL;
		return -1;
	}
	n = value-s;
	if(n<=0 || n > sizeof(name)){
		errno = EINVAL;
		return -1;
	}
	strncpy(name, s, n);
	name[n] = 0;

	for(p = environ; *p; p++){
		for(s1 = name, s2 = *p; *s1 == *s2; s1++, s2++)
			continue;
		if(*s1 == '\0' && *s2 == '='){
			/* don't free old value */
			*p = s;
			return 0;
		}
	}
	if(_envcnt >= _envsize-1){
		n = _envsize*2+1;
		p = realloc(environ, n*sizeof(char *));
		if(p == NULL){
			errno = ENOMEM;
			return -1;
		}
		environ = p;
		_envsize = n;
	}
	environ[_envcnt++] = s;
	environ[_envcnt] = NULL;
	return 0;
}
