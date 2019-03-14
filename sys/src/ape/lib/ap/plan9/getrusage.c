#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>

int
getrusage(int who, struct rusage *usage)
{
	/* dummy implementation */
	memset(usage, 0, sizeof *usage);
	return 0;
}
