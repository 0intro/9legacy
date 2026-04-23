typedef struct Tos32 Tos32;
typedef struct Plink Plink;

struct Tos32 {
	struct			/* Per process profiling */
	{
		ulong	pp;	/* known to be 0(ptr) */
		ulong	next;	/* known to be 4(ptr) */
		ulong	last;
		ulong	first;
		ulong	pid;
		ulong	what;
	} prof;
	uvlong	cyclefreq;	/* cycle clock frequency if there is one, 0 otherwise */
	vlong	kcycles;	/* cycles spent in kernel */
	vlong	pcycles;	/* cycles spent in process (kernel + user) */
	ulong	pid;		/* might as well put the pid here */
	ulong	clock;
	/* scratch space for kernel use (e.g., mips fp delay-slot execution) */
	ulong	kscr[4];
	/* top of stack is here */
};
