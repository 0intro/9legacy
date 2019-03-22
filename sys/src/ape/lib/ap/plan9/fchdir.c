#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

extern int _FD2PATH(int fd, char *buf, int nbuf);
extern int _CHDIR(char *dirname);

int
fchdir(int fd)
{
	char buf[_POSIX_PATH_MAX];
	struct stat s;

	if(fstat(fd, &s) < 0)
		return -1;
	if(!S_ISDIR(s.st_mode)){
		errno = ENOTDIR;
		return -1;
	}
	if(_FD2PATH(fd, buf, sizeof buf) < 0)
		return -1;
	return _CHDIR(buf);
}
