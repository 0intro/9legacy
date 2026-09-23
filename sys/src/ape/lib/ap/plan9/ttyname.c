#include <unistd.h>
#include <stdio.h>

char *
ttyname(int fd)
{
	static char buf[16];

	snprintf(buf, sizeof buf, "/fd/%d", fd);
	return buf;
}
