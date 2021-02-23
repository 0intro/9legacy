#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define nil	((void*)0)
#define nelem(a)	(sizeof(a)/sizeof((a)[0]))

char *gaitab[] = {
[-EAI_BADFLAGS]	"bad flags",
[-EAI_NONAME]	"authoratitive negative response",
[-EAI_AGAIN]		"temporary lookup failure",
[-EAI_FAIL]		"name resolution failure",
[-EAI_FAMILY]		"family not supported",
[-EAI_SOCKTYPE]	"ai_socktype not supported",
[-EAI_SERVICE]	"srvname unsupported",
[-EAI_MEMORY]	"no memory",
[-EAI_SYSTEM]		"see errno",
[-EAI_OVERFLOW]	"overflow",
};

const char*
gai_strerror(int error)
{
	unsigned int e;

	e = -error;
	if(e <= nelem(gaitab) && gaitab[e] != nil)
		return gaitab[e];
	return "bogus gai_strerror argument";
}

