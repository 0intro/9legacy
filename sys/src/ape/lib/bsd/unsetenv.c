#include <unistd.h>
#include <string.h>
#include <errno.h>

extern char **environ;
extern int _envcnt;

int
unsetenv(const char *name)
{
	char **p, **q;
	char *s1, *s2;

	if(name == NULL || *name == '\0' || strchr(name, '=')){
		errno = EINVAL;
		return -1;
	}
	for(p = environ; *p; p++){
		for(s1 = name, s2 = *p; *s1 == *s2; s1++, s2++)
			continue;
		if(*s1 == '\0' && *s2 == '='){
			/* don't free old values */
			for(q = p; *q; q++)
				*q = *(q+1);
			_envcnt--;
			/* fallthrough; remove all values that has same name if exists */
		}
	}
	return 0;
}
