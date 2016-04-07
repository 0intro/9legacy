/*
 * There are 2 flavours of APIC, Local APIC and IOAPIC,
 * which don't necessarily share one APIC ID space.
 * Each IOAPIC has a unique address, Local APICs are all
 * at the same address as they can only be accessed by
 * the local CPU.
 */
typedef struct IOapic IOapic;
typedef struct Lapic Lapic;

struct IOapic {
	int	useable;	/* en */

	Lock;	/* register access */
	u32int*	addr;	/* register base */
	uintmem	paddr;	/* register base */
	int	nrdt;	/* size of RDT */
	int	gsib;	/* global RDT index */
};

struct Lapic {
	int	useable;	/* en */
	int	machno;
	int	dom;
	u32int	lvt[10];
	int	nlvt;
	int	ver;	/* unused */

	vlong	hz;				/* APIC Timer frequency */
	vlong	max;
	vlong	min;
	vlong	div;
};

enum {
	Nbus		= 256,
	Napic		= 254,			/* xAPIC architectural limit */
	Nrdt		= 128,
};

/*
 * Common bits for
 *	IOAPIC Redirection Table Entry (RDT);
 *	APIC Local Vector Table Entry (LVT);
 *	APIC Interrupt Command Register (ICR).
 * [10:8] Message Type
 * [11] Destination Mode (RW)
 * [12] Delivery Status (RO)
 * [13] Interrupt Input Pin Polarity (RW)
 * [14] Remote IRR (RO)
 * [15] Trigger Mode (RW)
 * [16] Interrupt Mask
 */
enum {
	MTf		= 0x00000000,		/* Fixed */
	MTlp		= 0x00000100,		/* Lowest Priority */
	MTsmi		= 0x00000200,		/* SMI */
	MTrr		= 0x00000300,		/* Remote Read */
	MTnmi		= 0x00000400,		/* NMI */
	MTir		= 0x00000500,		/* INIT/RESET */
	MTsipi		= 0x00000600,		/* Startup IPI */
	MTei		= 0x00000700,		/* ExtINT */

	Pm		= 0x00000000,		/* Physical Mode */
	Lm		= 0x00000800,		/* Logical Mode */

	Ds		= 0x00001000,		/* Delivery Status */
	IPhigh		= 0x00000000,		/* IIPP High */
	IPlow		= 0x00002000,		/* IIPP Low */
	Rirr		= 0x00004000,		/* Remote IRR Status */
	TMedge		= 0x00000000,		/* Trigger Mode Edge */
	TMlevel		= 0x00008000,		/* Trigger Mode Level */
	Im		= 0x00010000,		/* Interrupt Mask */
};

#define l16get(p)	(((p)[1]<<8)|(p)[0])
#define	l32get(p)	(((u32int)l16get(p+2)<<16)|l16get(p))
#define	l64get(p)	(((u64int)l32get(p+4)<<32)|l32get(p))

void	apictimerenab(void);
int	gsitoapicid(int, uint*);

void	ioapicdump(void);
IOapic*	ioapicinit(int, int, uintmem);
void	ioapicintrinit(int, int, int, int, int, u32int);
IOapic*	ioapiclookup(uint);
void	ioapiconline(void);
void	iordtdump(void);

void	lapicdump(void);
int	lapiceoi(int);
void	lapicinit(int, uintmem, int);
void	lapicipi(int);
int	lapicisr(int);
Lapic*	lapiclookup(uint);
int	lapiconline(void);
void	lapicpri(int);
void	lapicsetdom(int, int);
void	lapicsipi(int, uintmem);

int	pcimsienable(Pcidev*, uvlong);
int	pcimsimask(Pcidev*, int);

/*
 * lapic.c
 */
extern int lapiceoi(int);
extern void lapicinit(int, uintptr, int);
extern int lapicisr(int);
extern int lapiconline(void);
extern void lapicsipi(int, uintptr);
extern void lapictimerdisable(void);
extern void lapictimerenable(void);
extern void lapictprput(int);

/*
 * ioapic.c
 */
extern IOapic* ioapicinit(int, int, uintmem);
extern void ioapicintrinit(int, int, int, int, int, u32int);
extern void ioapiconline(void);
extern int	ioapicintrenable(Vctl*);
extern int ioapicintrdisable(int);
