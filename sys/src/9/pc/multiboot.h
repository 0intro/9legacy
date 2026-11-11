/*
 * Multiboot information left by a multiboot loader (qemu -kernel, GRUB).
 * See https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 */
typedef struct Mbi Mbi;
struct Mbi {				/* multiboot boot information */
	uint	flags;
	uint	memlower;
	uint	memupper;
	uint	bootdevice;
	uint	cmdline;
	uint	modscount;
	uint	modsaddr;
	uint	syms[4];
	uint	mmaplength;
	uint	mmapaddr;
	uint	driveslength;
	uint	drivesaddr;
	uint	configtable;
	uint	bootloadername;
	uint	apmtable;
	uint	vbe[6];
};

enum {					/* flags */
	Fmem		= 0x00000001,	/* mem* valid */
	Fbootdevice	= 0x00000002,	/* bootdevice valid */
	Fcmdline	= 0x00000004,	/* cmdline valid */
	Fmods		= 0x00000008,	/* mod* valid */
	Fsyms		= 0x00000010,	/* syms[] has a.out info */
	Felf		= 0x00000020,	/* syms[] has ELF info */
	Fmmap		= 0x00000040,	/* mmap* valid */
	Fdrives		= 0x00000080,	/* drives* valid */
	Fconfigtable	= 0x00000100,	/* configtable* valid */
	Fbootloadername	= 0x00000200,	/* bootloadername* valid */
	Fapmtable	= 0x00000400,	/* apmtable* valid */
	Fvbe		= 0x00000800,	/* vbe[] valid */
};

typedef struct Mod Mod;
struct Mod {
	uint	modstart;
	uint	modend;
	uint	string;
	uint	reserved;
};

/* MMap is also the format of the E820 map, so fixed by BIOS */
typedef struct MMap MMap;
struct MMap {
	uint	size;
	uvlong	base;
	uvlong	len;
	uint	type;
};

enum {
	Nmmap	= 32,		/* multiboot mmap entries we keep */
};

extern Mbi *multibootheader;	/* loader's Mbi addr captured in l.s, nil if not multibooted */
extern MMap mbmmap[Nmmap];	/* snapshot of the loader's memory map */
extern int nmbmmap;

void	multibootinit(void);	/* snapshot the multiboot tables while low memory is intact */
int	multibootmmap(void);	/* feed the snapshot memory map into map(), -1 if none */
