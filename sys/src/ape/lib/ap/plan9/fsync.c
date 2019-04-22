#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

int
fsync(int)
{
	/* TODO: should fsync return an error? */
	return 0;
}
