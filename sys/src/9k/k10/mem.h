/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024			/* Kibi 0x0000000000000400 */
#define MiB		1048576			/* Mebi 0x0000000000100000 */
#define GiB		1073741824		/* Gibi 000000000040000000 */
#define TiB		1099511627776ll		/* Tebi 0x0000010000000000 */
#define PiB		1125899906842624ll	/* Pebi 0x0004000000000000 */
#define EiB		1152921504606846976ll	/* Exbi 0x1000000000000000 */

/* aliases for the imported (Geoff) page allocator, which uses KB/MB/... */
#define KB		KiB
#define MB		MiB
#define GB		GiB
#define TB		TiB
#define PB		PiB
#define EB		EiB

#define HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))
#define ROUNDDN(x, y)	(((x)/(y))*(y))
#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define BY2V		8			/* bytes per double word */
#define BY2SE		8			/* bytes per stack element */
#define BLOCKALIGN	8

#define PGSZ		(4*KiB)			/* page size */
#define PGSHFT		12			/* log(PGSZ) */
#define PGROUND(s)	ROUNDUP((s), PGSZ)	/* for the imported page allocator */
#define PTSZ		(4*KiB)			/* page table page size */
#define PTSHFT		9			/*  */

#define MACHSZ		(4*KiB)			/* Mach+stack size */
#define MACHMAX		32			/* max. number of cpus */
#define MACHSTKSZ	(6*(4*KiB))		/* Mach stack size */

#define KSTACK		(16*1024)		/* Size of Proc kernel stack */
#define STACKALIGN(sp)	((sp) & ~(BY2SE-1))	/* bug: assure with alloc */

/*
 * Time
 */
#define HZ		(100)			/* clock frequency */
#define MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

/*
 *  Address spaces
 *
 *  Kernel gets loaded at 1*MiB+128*KiB;
 *  Memory from 0 to 1MiB is not used for other things;
 *  1*MiB to 1MiB+128KiB is used to hold the Sys and
 *  Mach0 datastructures.
 *
 *  User is at low addresses; kernel vm starts at KZERO;
 *  KSEG0 maps the first kernmem bytes, one to one (i.e. KZERO);
 *  KSEG1 maps the PML4 into itself;
 *  KSEG2 maps all remaining physical memory.
 */
#define UTZERO		(0+2*MiB)		/* first address in user text */
#define UTROUND(t)	ROUNDUP((t), 2*MiB)
#define ADDRSPCSZ	(1ull<<(48-1))		/* high bit selects user vs kernel */
#define USTKTOP		(ADDRSPCSZ-PGSZ)	/* 0x00007ffffffff000 */
#define USTKSIZE	(16*1024*1024)		/* size of user stack */
#define TSTKTOP		(USTKTOP-USTKSIZE)	/* end of new stack in sysexec */

#define KSEG0SIZE	(2ull*GB)		/* kernel direct-map window; l32p.s/mkfile know this */
#define VZERO		0ull			/* top of kernel address space + 1 */
#define KSEG0		(VZERO-KSEG0SIZE)	/* 2GB direct map (was 0xfffffffff0000000) */
#define KSEG1		(VZERO-TB)		/* 512GB - embedded PML4 */
#define KSEG2		(VZERO-ADDRSPCSZ)	/* KMAP - all remaining physical memory */

#define PMAPADDR	(VZERO-(2*MiB))		/* unused as of yet (KMAP?) */

#define KZERO		KSEG0
#define KTZERO		(KZERO+1*MiB+128*KiB)

/*
 *  virtual MMU
 */
#define PTEPERTAB	(256)
#define PTEMAPMEM	(PTEPERTAB*PGSZ)
#define SEGMAPSIZE	1984
#define SSEGMAPSIZE	16

/*
 * This is the interface between fixfault and mmuput.
 * Should be in port.
 */
#define PTEVALID	(1<<0)
#define PTEWRITE	(1<<1)
#define PTERONLY	(0<<1)
#define PTEUSER		(1<<2)
#define PTEUNCACHED	(1<<4)

#define getpgcolor(a)	0

/*
 * Hierarchical Page Tables.
 * For example, traditional IA-32 paging structures have 2 levels,
 * level 1 is the PD, and level 0 the PT pages; with IA-32e paging,
 * level 3 is the PML4(!), level 2 the PDP, level 1 the PD,
 * and level 0 the PT pages. The PTLX macro gives an index into the
 * page-table page at level 'l' for the virtual address 'v'.
 */
#define PTLX(v, l)	(((v)>>(((l)*PTSHFT)+PGSHFT)) & ((1<<PTSHFT)-1))
#define PGLSZ(l)	(1ull<<(((l)*PTSHFT)+PGSHFT))

/* this can go when the arguments to mmuput change */
#define PPN(x)		((x) & ~(PGSZ-1))		/* GAK */

#define	KVATOP	(KSEG0&KSEG1&KSEG2)
#define	iskaddr(a)	(((uintptr)(a)&KVATOP) == KVATOP)
