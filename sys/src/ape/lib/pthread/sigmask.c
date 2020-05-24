#include <signal.h>

int
pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
	return sigprocmask(how, set, oldset);
}
