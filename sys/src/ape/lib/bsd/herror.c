/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* bsd extensions */
#include <netdb.h>
#include <sys/socket.h>
//#include <netinet/in.h>

#include "priv.h"

int h_errno;

static const char *hetab[] = {
[HOST_NOT_FOUND]	"authoritative answer host not found",
[TRY_AGAIN]		"non-authoritive host not found",
[NO_RECOVERY]	"non recoverable error",
[NO_DATA]		"valid name, no data record of requested type"
};

static const char*
getmsg(unsigned int e)
{
	const char *p;

	if(e > nelem(hetab) || (p = hetab[e]) == nil)
		p = "unknown error";
	return p;
}

void
herror(const char *s)
{
	fprintf(stderr, "%s: %s", s, getmsg(h_errno));
}


const char*
hstrerror(int err)
{
	return getmsg(err);
}
