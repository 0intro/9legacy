#if !defined(_RESEARCH_SOURCE) && !defined(_PLAN9_SOURCE)
   This header file is an extension of ANSI/POSIX
#endif

#ifndef __LOCK_H
#define __LOCK_H
#pragma lib "/$M/lib/ape/libap.a"

#include <u.h>

typedef struct
{
	long	key;
	long	sem;
} Lock;

typedef struct QLp QLp;
struct QLp
{
	int	inuse;
	QLp	*next;
	char	state;
};

typedef struct
{
	Lock	lock;
	int	locked;
	QLp	*head;
	QLp 	*tail;
} QLock;

#ifdef __cplusplus
extern "C" {
#endif

extern	void	lock(Lock*);
extern	void	unlock(Lock*);
extern	int	canlock(Lock*);
extern	void	qlock(QLock*);
extern	void	qunlock(QLock*);
extern	int	tas(int*);

#ifdef __cplusplus
}
#endif

#endif
