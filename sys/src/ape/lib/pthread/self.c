#include <pthread.h>
#include <unistd.h>

pthread_t
pthread_self(void)
{
	return getpid();
}
