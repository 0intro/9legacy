#include <string.h>

char*
strchr(const char *s, int c)
{
	char c1;

	if(c == 0) {
		while(*s++)
			;
		return (char *)s-1;
	}

	while(c1 = *s++)
		if(c1 == c)
			return (char *)s-1;
	return 0;
}
