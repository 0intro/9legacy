#include "lib.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "sys9.h"

int
getpgid(pid_t pid)
{
	int n, f;
	char buf[50], fname[30];

	if(pid == 0)
		pid = getpid();
	sprintf(fname, "/proc/%d/noteid", pid);
	f = open(fname, 0);
	if(f < 0) {
		errno = ESRCH;
		return -1;
	}
	n = read(f, buf, sizeof(buf));
	if(n < 0)
		_syserrno();
	else{
		buf[n] = '\0';
		n = atoi(buf);
	}
	close(f);
	return n;
}
