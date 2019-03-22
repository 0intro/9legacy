#include <stdlib.h>
#include <string.h>

char *
dirname(char *path)
{
	int n;
	char *p;

	if(path == NULL || path[0] == '\0')
		return ".";
	n = strlen(path);
	if(n == 1 && path[0] == '/' && path[1] == '\0')
		return "/";
	while(path[n-1] == '/')
		path[--n] = '\0';
	p = strrchr(path, '/');
	if(p == NULL)
		return ".";
	while(*p == '/')
		*p-- = '\0';
	if(path[0] == '\0')
		return "/";
	return path;
}
