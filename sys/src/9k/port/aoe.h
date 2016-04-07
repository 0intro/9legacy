/*
 * Copyright © 2011 Coraid, Inc.
 * All rights reserved.
 */

typedef struct Aoehdr Aoehdr;
typedef struct Aoeata Aoeata;
typedef struct Aoeqc Aoeqc;
typedef struct Mdir Mdir;
typedef struct Aoemask Aoemask;
typedef struct Aoesrr Aoesrr;
typedef struct Aoekrr Aoekrr;
typedef struct Kresp Kresp;
typedef struct Kreg Kreg;
typedef struct Kset Kset;
typedef struct Kreplace Kreplace;

enum
{
	ACata, ACconfig, ACmask, ACresrel, ACkresrel,

	AQCread= 0, AQCtest, AQCprefix, AQCset, AQCfset, AQCtar,

	ETAOE= 0x88a2,
	Aoever= 1,

	AEcmd= 1, AEarg, AEdev, AEcfg, AEver, AEres,

	AFerr= 1<<2,
	AFrsp= 1<<3,

	AAFwrite= 1,
	AAFext= 1<<6,

	AKstat = 0, AKreg, AKset, AKreplace, AKreset,

	Aoesectsz = 512,
	Szaoeata	= 24+12,
	Szaoeqc	= 24+8,

	/* mask commands */
	Mread= 0,	
	Medit,

	/* mask directives */
	MDnop= 0,
	MDadd,
	MDdel,

	/* mask errors */
	MEunspec= 1,
	MEbaddir,
	MEfull,

	/* Keyed-RR Rflags */
	KRnopreempt = 1<<0,
};

struct Aoehdr
{
	uchar dst[6];
	uchar src[6];
	uchar type[2];
	uchar verflags;
	uchar error;
	uchar major[2];
	uchar minor;
	uchar cmd;
	uchar tag[4];
};

struct Aoeata
{
	Aoehdr;
	uchar aflags;
	uchar errfeat;
	uchar scnt;
	uchar cmdstat;
	uchar lba[6];
	uchar res[2];
};

struct Aoeqc
{
	Aoehdr;
	uchar bufcnt[2];
	uchar fwver[2];
	uchar scnt;
	uchar verccmd;
	uchar cslen[2];
};

// mask directive
struct Mdir {
	uchar res;
	uchar cmd;
	uchar mac[6];
};

struct Aoemask {
	Aoehdr;
	uchar rid;
	uchar cmd;
	uchar merror;
	uchar nmacs;
//	struct Mdir m[0];
};

struct Aoesrr {
	Aoehdr;
	uchar rcmd;
	uchar nmacs;
//	uchar mac[6][nmacs];
};

struct Aoekrr {
	Aoehdr; 
	uchar rcmd;
};

struct Kresp {
	Aoehdr;
	uchar rcmd;
	uchar rtype;
	uchar nkeys[2];
	uchar res[4];
	uchar gencnt[4];
	uchar owner[8];
	uchar keys[1];
};

struct Kreg {
	Aoehdr;
	uchar rcmd;
	uchar nmacs;
	uchar res[2];
	uchar key[8];
	uchar macs[1];
};

struct Kset {
	Aoehdr;
	uchar rcmd;
	uchar rtype;
	uchar res[2];
	uchar key[8];
};

struct Kreplace {
	Aoehdr;
	uchar rcmd;
	uchar rtype;
	uchar rflags;
	uchar res;
	uchar targkey[8];
	uchar replkey[8];
};
