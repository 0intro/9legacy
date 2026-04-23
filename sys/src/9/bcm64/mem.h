/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */

/*
 * Sizes
 */
#define	BY2PG		(4*KiB)			/* bytes per page */
#define	PGSHIFT		12			/* log(BY2PG) */

#define	MAXMACH		4			/* max # cpus system can run */
#define	MACHSIZE	BY2PG
#define L1SIZE		(5 * BY2PG)

#define KSTKSIZE	(16*KiB)	/* was 8*KiB ... revisit? */
#define STACKALIGN(sp)	((sp) & ~15)		/* bug: assure with alloc */

/*
 * Magic registers
 */

#define	USER		26		/* R26 is up-> */
#define	MACH		27		/* R27 is m-> */

/*
 * Address spaces.
 * KTZERO is used by kprof and dumpstack (if any).
 *
 * KZERO is mapped to physical 0 (start of ram).
 *
 */

#define L2VA		0x100000000uLL		/* L2 page tables virtual addr */
#define	UCKZERO		0x140000000ull		/* kernel low RAM uncached */
#define	KSEG0		0x200000000ull		/* kernel segment */
#define	KSEGM		0xFFFFFFFE00000000ull	/* mask to check segment */
#define	KZERO		KSEG0			/* kernel address space */
#define	MACHADDR	(KZERO+0x80000)		/* Mach structure */
#define	L1		(KZERO+0x81000)		/* tt ptes: 5 pages 4KiB aligned */

#define	KTZERO		(KZERO+0x100000)	/* kernel text start */
#define VIRTIO		(KZERO+16LL*GiB)	/* i/o registers */
#define	FRAMEBUFFER	(VIRTIO+4LL*GiB)	/* video framebuffer */
#define	ARMLOCAL	VIRTIO			/* armv7/8 only */
#define	DESCRIPTORS	(UCKZERO+0xF0000)

#define	UZERO		0			/* user segment */
#define	UTZERO		(UZERO+0x10000)
#define	UTROUND(t)	ROUNDUP((t), 0x10000)
#define	UTZERO_COMPAT32	(UZERO+BY2PG)		/* user text start */
#define	UTROUND_COMPAT32(t)	ROUNDUP((t), BY2PG)
#define	USTKTOP		0xFFFF0000uLL		/* user segment end +1 */
#define	USTKSIZE	(16*1024*1024)		/* user stack size */
#define	TSTKTOP		(USTKTOP-USTKSIZE)	/* sysexec temporary stack */
#define	TSTKSIZ	 	256

/* address at which to copy and execute rebootcode */
#define	REBOOTADDR	(KZERO+0x1800)

/*
 * Legacy...
 */
#define BLOCKALIGN	64			/* only used in allocb.c */
#define KSTACK		KSTKSIZE

/*
 * Sizes
 */
#define BI2BY		8			/* bits per byte */
#define BY2SE		4
#define BY2WD		4
#define BY2V		8			/* only used in xalloc.c */

#define	PTEMAPMEM	(1024*1024)
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SEGMAPSIZE	4096
#define	SSEGMAPSIZE	16
#define	PPN(x)		((x) & (~(BY2PG-1) | PTEHIMEM))

/*
 * With a little work these move to port.
 */
#define	PTEVALID	(1<<0)
#define	PTERONLY	0
#define	PTEWRITE	(1<<1)
#define	PTEUNCACHED	(1<<2)
#define PTEKERNEL	(1<<3)
#define PTEHIMEM	(0xF<<8)

/*
 * Physical machine information from here on.
 *	PHYS addresses as seen from the arm cpu.
 *	BUS  addresses as seen from the videocore gpu.
 */
#define	PHYSDRAM	0
