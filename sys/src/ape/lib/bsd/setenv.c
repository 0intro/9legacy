#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int
setenv(const char *name, const char *value, int overwrite)
{
	int n, m;
	char *s;

	if(name == NULL || *name == '\0' || strchr(name, '=')){
		errno = EINVAL;
		return -1;
	}
	if(!overwrite && getenv(name))
		return 0;
	n = strlen(name);
	m = strlen(value);
	s = malloc(n+m+2);
	memmove(s, name, n);
	s[n] = '=';
	memmove(s+n+1, value, m);
	s[n+m+1] = '\0';
	return putenv(s);
}
