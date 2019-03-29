#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

int
mkstemp(char *template)
{
	char *p;
	int fd;

	p = mktemp(template);
	if(strcmp(p, "") == 0){
		errno = EINVAL;
		return -1;
	}
	fd = open(p, O_RDWR|O_CREAT|O_EXCL, 0600);
	if(fd < 0)
		return -1;
	return fd;
}
