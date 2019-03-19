#include <stdlib.h>
#include <string.h>

char *
basename(char *path)
{
	int n;
	char *p;

	if(path == NULL || path[0] == '\0')
		return ".";
	n = strlen(path);
	if(n == 2 && path[0] == '/' && path[1] == '\0')
		return "/";
	if(path[n-1] == '/')
		path[n-1] = '\0';
	p = strrchr(path, '/');
	if(p == NULL)
		return path;
	return p+1;
}
