#ifndef __PTHREAD_H
#define __PTHREAD_H
#pragma lib "/$M/lib/ape/libap.a"

#include <sys/types.h>
#include <unistd.h>
#include <lock.h>

typedef struct pthread_once pthread_once_t;
typedef pid_t pthread_t;
typedef int pthread_attr_t;
typedef struct pthread_mutex pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef struct pthread_cond pthread_cond_t;
typedef int pthread_condattr_t;
typedef struct pthread_key pthread_key_t;

enum {
	PTHREAD_THREADS_MAX = 1000
};

struct pthread_once {
	Lock l;
	int once;
};
struct pthread_mutex {
	Lock l;
	pthread_mutexattr_t attr;
};
struct pthread_cond {
	QLock l;

	struct {
		QLock	*l;
		QLp	*head;
		QLp	*tail;
	} r;
};
struct pthread_key {
	void **p;
	void (*destroy)(void*);
};

#define PTHREAD_ONCE_INIT	{ 0 }
#define PTHREAD_MUTEX_INITIALIZER	{ 0 }
#define PTHREAD_MUTEX_DEFAULT	0
#define PTHREAD_MUTEX_NORAML	1
#define PTHREAD_MUTEX_RECURSIVE	2
#define PTHREAD_COND_INITIALIZER	{ 0 }

#ifdef __cplusplus
extern "C" {
#endif

extern int	pthread_atfork(void (*)(void), void (*)(void), void (*)(void));
extern int	pthread_once(pthread_once_t*, void (*)(void));
extern pthread_t	pthread_self(void);
extern int	pthread_equal(pthread_t, pthread_t);
extern int	pthread_create(pthread_t*, pthread_attr_t*, void *(*)(void*), void*);
extern int	pthread_exit(void*);
extern int	pthread_join(pthread_t, void**);

extern int	pthread_mutexattr_init(pthread_mutexattr_t*);
extern int	pthread_mutexattr_destroy(pthread_mutexattr_t*);
extern int	pthread_mutexattr_settype(pthread_mutexattr_t*, int);

extern int	pthread_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*);
extern int	pthread_mutex_lock(pthread_mutex_t*);
extern int	pthread_mutex_unlock(pthread_mutex_t*);
extern int	pthread_mutex_trylock(pthread_mutex_t*);
extern int	pthread_mutex_destroy(pthread_mutex_t*);

extern int	pthread_cond_init(pthread_cond_t*, pthread_condattr_t*);
extern int	pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*);
extern int	pthread_cond_signal(pthread_cond_t*);
extern int	pthread_cond_broadcast(pthread_cond_t*);
extern int	pthread_cond_destroy(pthread_cond_t*);

extern int	pthread_key_create(pthread_key_t*, void (*)(void*));
extern int	pthread_key_delete(pthread_key_t);
extern void	*pthread_getspecific(pthread_key_t);
extern int	pthread_setspecific(pthread_key_t, const void*);

#ifdef __cplusplus
}
#endif

#endif
