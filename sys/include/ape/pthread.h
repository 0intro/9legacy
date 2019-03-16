#ifndef __PTHREAD_H
#define __PTHREAD_H
#pragma lib "/$M/lib/ape/libap.a"

#include <sys/types.h>
#include <unistd.h>
#include <lock.h>

struct pthread_once {
	Lock l;
	int once;
};
typedef struct pthread_once pthread_once_t;

typedef pid_t pthread_t;
typedef Lock pthread_mutex_t;

#define PTHREAD_ONCE_INIT { 0 }
#define PTHREAD_MUTEX_INITIALIZER { 0 }

#ifdef __cplusplus
extern "C" {
#endif

extern int pthread_atfork(void (*)(void), void (*)(void), void (*)(void));
extern int pthread_once(pthread_once_t *once_control, void (*)(void));
extern pthread_t pthread_self(void);
extern int pthread_equal(pthread_t, pthread_t);
extern int pthread_mutex_lock(pthread_mutex_t*);
extern int pthread_mutex_unlock(pthread_mutex_t*);
extern int pthread_mutex_trylock(pthread_mutex_t*);

#ifdef __cplusplus
}
#endif

#endif
