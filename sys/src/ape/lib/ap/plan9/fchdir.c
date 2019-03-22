#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include "lib.h"

extern int _FD2PATH(int fd, char *buf, int nbuf);
extern int _CHDIR(char *dirname);

int
fchdir(int fd)
{
	char buf[_POSIX_PATH_MAX];
	struct stat s;
	int n;

	if(fstat(fd, &s) < 0)
		return -1;
	if(!S_ISDIR(s.st_mode)){
		errno = ENOTDIR;
		return -1;
	}
	if(_FD2PATH(fd, buf, sizeof buf) < 0){
		_syserrno();
		return -1;
	}
	n = _CHDIR(buf);
	if(n < 0)
		_syserrno();
	return n;
}
