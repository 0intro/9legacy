#include <unistd.h>

int
getpagesize(void)
{
	return sysconf(_SC_PAGESIZE);
}
