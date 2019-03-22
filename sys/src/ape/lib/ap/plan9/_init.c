#include <limits.h>
#include <stddef.h>
#include <string.h>

extern char *argv0;

static char name[_POSIX_PATH_MAX];

void
_init(int argc, char **argv)
{
	const char *p;

	if(argc == 0)
		return;
	p = strrchr(argv[0], '/');
	if(p == NULL)
		p = argv[0];
	else
		p++;
	strncpy(name, p, sizeof(name)-1);
	name[sizeof(name)-1] = '\0';
	argv0 = name;
}
