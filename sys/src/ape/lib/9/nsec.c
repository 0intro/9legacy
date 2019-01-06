#include <lib9.h>

extern vlong _NSEC(void);

vlong
nsec(void)
{
	return _NSEC();
}
