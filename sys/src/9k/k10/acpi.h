typedef	struct	Fadt	Fadt;
typedef	struct	Gas	Gas;
typedef	struct	Tbl	Tbl;

typedef	struct	Acpicfg	Acpicfg;

/*
 * Header for ACPI description tables
 */
struct Tbl {
	uchar	sig[4];			/* e.g. "FACP" */
	uchar	len[4];
	uchar	rev;
	uchar	csum;
	uchar	oemid[6];
	uchar	oemtblid[8];
	uchar	oemrev[4];
	uchar	creatorid[4];
	uchar	creatorrev[4];
	uchar	data[];
};

/*
 * Generic address structure. 
 */
struct Gas
{
	uchar	spc;	/* address space id */
	uchar	len;	/* register size in bits */
	uchar	off;	/* bit offset */
	uchar	accsz;	/* 1: byte; 2: word; 3: dword; 4: qword */
	u64int	addr;	/* address (or acpi encoded tbdf + reg) */
};

/*
 * Fixed ACPI description table.
 */
struct Fadt {
	int	rev;

	u32int	facs;
	u32int	dsdt;
	uchar	pmprofile;
	u16int	sciint;
	u32int	smicmd;
	uchar	acpienable;
	uchar	acpidisable;
	uchar	s4biosreq;
	uchar	pstatecnt;
	u32int	pm1aevtblk;
	u32int	pm1bevtblk;
	u32int	pm1acntblk;
	u32int	pm1bcntblk;
	u32int	pm2cntblk;
	u32int	pmtmrblk;
	u32int	gpe0blk;
	u32int	gpe1blk;
	uchar	pm1evtlen;
	uchar	pm1cntlen;
	uchar	pm2cntlen;
	uchar	pmtmrlen;
	uchar	gpe0blklen;
	uchar	gpe1blklen;
	uchar	gp1base;
	uchar	cstcnt;
	u16int	plvl2lat;
	u16int	plvl3lat;
	u16int	flushsz;
	u16int	flushstride;
	uchar	dutyoff;
	uchar	dutywidth;
	uchar	dayalrm;
	uchar	monalrm;
	uchar	century;
	u16int	iapcbootarch;
	u32int	flags;
	Gas	resetreg;
	uchar	resetval;
	u64int	xfacs;
	u64int	xdsdt;
	Gas	xpm1aevtblk;
	Gas	xpm1bevtblk;
	Gas	xpm1acntblk;
	Gas	xpm1bcntblk;
	Gas	xpm2cntblk;
	Gas	xpmtmrblk;
	Gas	xgpe0blk;
	Gas	xgpe1blk;
};
#pragma	varargck	type	"G"	Gas*

struct Acpicfg {
	uint	sval[6][2];		/* p1a.ctl, p1b.ctl */
};

Tbl*	acpigettbl(void*);

extern	Fadt	fadt;
extern	Acpicfg	acpicfg;

extern void hpetinit(uint, uint, uintmem, int);

enum{
	MemHotPlug=	1<<1,
	MemNonVolatile=	1<<2,
};
extern void memaffinity(u64int, u64int, u32int, int);

/*
 * ACPI 4.0 E820 AddressRange types (table 14-1)
 */
enum {
	AddrsNone		= 0,
	AddrsMemory	= 1,
	AddrsReserved	= 2,
	AddrsACPI	= 3,
	AddrsNVS	= 4,
	AddrsUnusable = 5,
	AddrsDisabled = 6,

	AddrsDEV		= 9,	/* our internal code */

	AddrsNVDIMM	= 0x5a,	/* Viking NVDIMM */

	/* extended attribute flags, not currently used */
	AddrsNonVolatile = 1<<1,
	AddrsSlowAccess = 1<<2,
	AddrsErrorLog = 1<<3,
};
