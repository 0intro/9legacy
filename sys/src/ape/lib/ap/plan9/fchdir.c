#include <limits.h>

extern int _FD2PATH(int fd, char *buf, int nbuf);
extern int _CHDIR(char *dirname);

int
fchdir(int fd)
{
	char buf[PATH_MAX];

	if(_FD2PATH(fd, buf, sizeof buf) < 0)
		return -1;
	return _CHDIR(buf);
}
