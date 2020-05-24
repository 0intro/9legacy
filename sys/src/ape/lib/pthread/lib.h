#define	nelem(a)	(sizeof(a)/sizeof((a)[0]))

/* rfork */
enum
{
	RFNAMEG		= (1<<0),
	RFENVG		= (1<<1),
	RFFDG		= (1<<2),
	RFNOTEG		= (1<<3),
	RFPROC		= (1<<4),
	RFMEM		= (1<<5),
	RFNOWAIT	= (1<<6),
	RFCNAMEG	= (1<<10),
	RFCENVG		= (1<<11),
	RFCFDG		= (1<<12),
	RFREND		= (1<<13),
	RFNOMNT		= (1<<14)
};

typedef struct Thread Thread;
struct Thread {
	int	inuse;
	pid_t	pid;

	Lock	l;
	int	exited;
	void	*ret;
	int	state;
};

#ifdef __cplusplus
extern "C" {
#endif

extern void	_syserrno(void);

extern Thread*	_pthreadalloc(void);
extern void	_pthreadsetpid(Thread*, pthread_t);
extern Thread*	_pthreadnew(pthread_t pid);
extern Thread*	_pthreadget(pthread_t);
extern void	_pthreadfree(Thread*);

#ifdef __cplusplus
}
#endif
