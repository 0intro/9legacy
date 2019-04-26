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

typedef
struct Waitmsg
{
	int	pid;		/* of loved one */
	ulong	time[3];	/* of loved one & descendants */
	char	*msg;
} Waitmsg;

typedef struct
{
	QLock	*l;
	QLp	*head;
	QLp	*tail;
} Rendez;

extern	void	rsleep(Rendez*);	/* unlocks r->l, sleeps, locks r->l again */
extern	int	rwakeup(Rendez*);
extern	int	rwakeupall(Rendez*);
extern	void**	privalloc(void);
extern	void	privfree(void**);
extern	Waitmsg	*wait(void);
