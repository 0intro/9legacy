/*
 * virtio 1.0 9P transport (#9): mount a host directory exported by
 * qemu's -device virtio-9p-pci / -fsdev local directly over a virtqueue,
 * with no network in the path.
 *
 *	bind -a '#9' /dev
 *	mount -c '#9/0' /n/host
 *
 * The modern-virtio plumbing (pci capability dance, virtqueue rings,
 * notify/interrupt) is the same as sdvirtio10.c, from which the Vconfig/
 * Vring/Vdesc/Vused/Vqueue structs and mkvqueue/virtiocap/virtiomapregs
 * are taken. A virtio-9p device has one request queue: each descriptor
 * chain is one 9P RPC: a device-readable buffer holding the marshalled
 * T-message followed by a device-writable buffer the device fills with
 * the R-message.
 *
 * devmnt drives this chan like a tcp connection: it write()s a whole
 * T-message and reads the reply back as a byte stream reassembled by the
 * size[4] prefix (devmnt.c mountio/doread, and read() directly for the
 * version handshake in mntversion). So write() posts one chain and
 * read() serves one complete reply at a time; the interrupt harvests
 * completions onto a FIFO.
 *
 * qemu's 9pfs speaks only 9P2000.u, Plan 9 speaks plain 9P2000, so a
 * small in-kernel shim bridges the two. On the way out it rewrites
 * Tversion ("9P2000"->"9P2000.u"), Tattach (+n_uname), Tcreate
 * (+extension) and the Twstat stat; on the way back Rversion, Rerror
 * (map the errno and drop the trailing errno[4]), and the stat entries
 * in Rstat and directory reads.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

typedef struct Vconfig Vconfig;
typedef struct Vring Vring;
typedef struct Vdesc Vdesc;
typedef struct Vused Vused;
typedef struct Vqueue Vqueue;
typedef struct V9cfg V9cfg;
typedef struct Slot Slot;
typedef struct Fid Fid;
typedef struct Ctlr Ctlr;

enum {
	Typ9p	= 9,		/* virtio device type for 9P */

	/* status */
	Acknowledge	= 1,
	Driver		= 2,
	DriverOk	= 4,
	FeaturesOk	= 8,
	Failed		= 0x80,

	/* descriptor flags */
	Next		= 1,
	Write		= 2,
	Indirect	= 4,

	VringSize	= 4,

	Fmounttag	= 1,	/* 9P device feature bit 0: config carries a mount tag */

	Msize		= IOHDRSZ + 16*1024,	/* matches MAXRPC in devmnt.c */
	Maxslot		= 64,	/* cap on outstanding RPCs per device */

	Fidhash		= 257,	/* buckets in the directory-fid set */
};

struct Vconfig {		/* common configuration (cap type 1) */
	u32int	devfeatsel;
	u32int	devfeat;
	u32int	drvfeatsel;
	u32int	drvfeat;

	u16int	msixcfg;
	u16int	nqueues;

	u8int	status;
	u8int	cfggen;
	u16int	queuesel;

	u16int	queuesize;
	u16int	queuemsixvect;

	u16int	queueenable;
	u16int	queuenotifyoff;

	u64int	queuedesc;
	u64int	queueavail;
	u64int	queueused;
};

struct V9cfg {			/* device configuration (cap type 4) */
	u16int	taglen;
	/* uchar tag[taglen] follows */
};

struct Vring {
	u16int	flags;
	u16int	idx;
};

struct Vdesc {
	u64int	addr;
	u32int	len;
	u16int	flags;
	u16int	next;
};

struct Vused {
	u32int	id;
	u32int	len;
};

struct Vqueue {
	Lock;

	int	idx;
	int	size;

	int	free;		/* descriptor free list head */
	int	nfree;

	void	*notify;

	Vdesc	*desc;

	Vring	*avail;
	u16int	*availent;
	u16int	*availevent;

	Vring	*used;
	Vused	*usedent;
	u16int	*usedevent;
	u16int	lastused;

	void	*rock[];		/* head index -> Slot* in flight */
};

struct Slot {
	uchar	*out;		/* device-readable: T-message after shim */
	uchar	*in;		/* device-writable: R-message */
	ulong	inlen;		/* bytes written by device (then shimmed) */
	ulong	rp;		/* read-serve offset within in */
	/* the request's T-type and fids, for matching up the reply */
	int	op;
	u32int	fid;		/* fid of open/create/read/clunk/remove */
	Slot	*next;		/* free list / done FIFO link */
};

struct Fid {			/* a fid known to be a directory */
	u32int	fid;
	vlong	soff;		/* qemu-side (9P2000.u) directory read offset */
	Fid	*next;
};

struct Ctlr {
	Pcidev	*pci;
	Vconfig	*cfg;
	u8int	*isr;
	u8int	*notify;
	u32int	notifyoffmult;
	ulong	feat[2];

	Vqueue	*vq;		/* the single request queue */
	char	tag[64];	/* mount tag, for identification */

	Slot	*slot;		/* slot array */
	int	nslot;
	Slot	*sfree;		/* free slots */
	Slot	*donehd;	/* completed replies, oldest first */
	Slot	*donetl;
	Slot	*cur;		/* reply being served by read (reader-private) */

	Rendez	rwait;		/* read waits here for a completed reply */
	Rendez	wwait;		/* write waits here for a free slot */

	QLock	fidlk;		/* guards the directory-fid set and its offsets */
	Fid	*fids[Fidhash];	/* fids known to be directories */

	int	started;	/* DriverOk + interrupt enabled */
	int	inuse;		/* one mount at a time */

	Ctlr	*next;
};

static Ctlr	*ctlrs;
static int	probed;

static void	v9interrupt(Ureg*, void*);
static void	clearfids(Ctlr*);

static Vqueue*
mkvqueue(int size)
{
	Vqueue *q;
	uchar *p;
	int i;

	q = malloc(sizeof(*q) + sizeof(void*)*size);
	p = mallocalign(
		PGROUND(sizeof(Vdesc)*size +
			VringSize +
			sizeof(u16int)*size +
			sizeof(u16int)) +
		PGROUND(VringSize +
			sizeof(Vused)*size +
			sizeof(u16int)),
		BY2PG, 0, 0);
	if(p == nil || q == nil){
		print("virtio9p: no memory for Vqueue\n");
		free(p);
		free(q);
		return nil;
	}

	q->desc = (void*)p;
	p += sizeof(Vdesc)*size;
	q->avail = (void*)p;
	p += VringSize;
	q->availent = (void*)p;
	p += sizeof(u16int)*size;
	q->availevent = (void*)p;
	p += sizeof(u16int);

	p = (uchar*)PGROUND((uintptr)p);
	q->used = (void*)p;
	p += VringSize;
	q->usedent = (void*)p;
	p += sizeof(Vused)*size;
	q->usedevent = (void*)p;

	q->free = -1;
	q->nfree = q->size = size;
	for(i=0; i<size; i++){
		q->desc[i].next = q->free;
		q->free = i;
	}

	return q;
}

static int
matchvirtiocfgcap(Pcidev *p, int cap, int off, int typ)
{
	int bar;

	if(cap != 9 || pcicfgr8(p, off+3) != typ)
		return 1;

	bar = pcicfgr8(p, off+4);
	if(bar < 0 || bar >= nelem(p->mem)
	|| p->mem[bar].size == 0
	|| (p->mem[bar].bar & 3) != 0)
		return 1;

	return 0;
}

static int
virtiocap(Pcidev *p, int typ)
{
	return pcienumcaps(p, matchvirtiocfgcap, typ);
}

static void*
virtiomapregs(Pcidev *p, int cap, int size)
{
	int bar, len;
	uvlong addr, base;

	if(cap < 0)
		return nil;
	bar = pcicfgr8(p, cap+4) % nelem(p->mem);
	addr = pcicfgr32(p, cap+8);
	len = pcicfgr32(p, cap+12);
	if(size <= 0)
		size = len;
	else if(len < size)
		return nil;
	if(addr+len > p->mem[bar].size)
		return nil;
	base = p->mem[bar].bar & ~0xFULL;
	if((p->mem[bar].bar & 6) == 4 && bar+1 < nelem(p->mem))
		base |= (uvlong)p->mem[bar+1].bar << 32;
	if(base >> 32){
		/*
		 * qemu with more than 3GB of memory places this modern 64-bit
		 * BAR above 4GB, where the 32-bit kernel cannot map it. Move it
		 * into the low device window from upaalloc and rewrite the BAR.
		 */
		ulong pa;

		pa = upaalloc(p->mem[bar].size, p->mem[bar].size);
		if(pa == 0)
			return nil;
		pcicfgw32(p, PciBAR0 + bar*4, pa);
		pcicfgw32(p, PciBAR0 + (bar+1)*4, 0);
		p->mem[bar].bar = pa | (p->mem[bar].bar & 0xF);
		p->mem[bar+1].bar = 0;
		base = pa;
	}
	addr += base;
	return vmap(addr, size);
}

static void
readtag(Ctlr *c)
{
	V9cfg *dc;
	int n;

	c->tag[0] = 0;
	if((c->feat[0] & Fmounttag) == 0)
		return;
	dc = virtiomapregs(c->pci, virtiocap(c->pci, 4), sizeof(V9cfg));
	if(dc == nil)
		return;
	n = dc->taglen;
	if(n >= sizeof(c->tag))
		n = sizeof(c->tag)-1;
	memmove(c->tag, (uchar*)dc + sizeof(dc->taglen), n);
	c->tag[n] = 0;
}

static void
v9probe(void)
{
	Ctlr *c, **tail;
	Vconfig *cfg;
	Pcidev *p;
	Vqueue *q;
	Slot *s;
	int cap, n, i, ndev, rno;

	if(probed)
		return;
	probed = 1;

	ndev = 0;
	tail = &ctlrs;
	for(p = nil; p = pcimatch(p, 0x1AF4, 0x1040+Typ9p);){
		if(p->rid == 0)		/* legacy/transitional; we want modern */
			continue;
		/*
		 * qemu gives the virtio-9p device PCI class 0x00, for which
		 * pcilscan (pci.c) does not read the base address registers,
		 * leaving p->mem[] empty. The virtio config capabilities all
		 * live in those BARs, so read and size them here as pcilscan
		 * would for a recognised class.
		 */
		rno = PciBAR0 - 4;
		for(i = 0; i < nelem(p->mem); i++){
			rno += 4;
			p->mem[i].bar = pcicfgr32(p, rno);
			p->mem[i].size = pcibarsize(p, rno);
		}
		if((cap = virtiocap(p, 1)) < 0)
			continue;
		cfg = virtiomapregs(p, cap, sizeof(Vconfig));
		if(cfg == nil)
			continue;
		if((c = malloc(sizeof(*c))) == nil){
			print("virtio9p: no memory for Ctlr\n");
			break;
		}
		c->pci = p;
		c->cfg = cfg;

		c->isr = virtiomapregs(p, virtiocap(p, 3), 0);
		if(c->isr == nil){
Baddev:
			free(c);
			continue;
		}
		cap = virtiocap(p, 2);
		c->notify = virtiomapregs(p, cap, 0);
		if(c->notify == nil)
			goto Baddev;
		c->notifyoffmult = pcicfgr32(p, cap+16);

		/* reset, then acknowledge */
		cfg->status = 0;
		while(cfg->status != 0)
			delay(1);
		cfg->status = Acknowledge|Driver;

		/* negotiate features: VERSION_1 (bit 32) and the mount tag */
		cfg->devfeatsel = 1;
		c->feat[1] = cfg->devfeat;
		cfg->devfeatsel = 0;
		c->feat[0] = cfg->devfeat;
		cfg->drvfeatsel = 1;
		cfg->drvfeat = c->feat[1] & 1;			/* VERSION_1 */
		cfg->drvfeatsel = 0;
		cfg->drvfeat = c->feat[0] & Fmounttag;
		cfg->status |= FeaturesOk;
		if((cfg->status & FeaturesOk) == 0){
			print("virtio9p: device rejected features\n");
			goto Baddev;
		}

		/* set up the single request queue (index 0) */
		cfg->queuesel = 0;
		n = cfg->queuesize;
		if(n == 0 || (n & (n-1)) != 0){
			print("virtio9p: bad queue size %d\n", n);
			goto Baddev;
		}
		if((q = mkvqueue(n)) == nil)
			goto Baddev;
		q->idx = 0;
		q->notify = c->notify + c->notifyoffmult * cfg->queuenotifyoff;
		c->vq = q;
		coherence();
		cfg->queuedesc = PADDR(q->desc);
		cfg->queueavail = PADDR(q->avail);
		cfg->queueused = PADDR(q->used);

		readtag(c);

		/* a pool of request slots, each with a T- and R-buffer */
		c->nslot = n/2;
		if(c->nslot > Maxslot)
			c->nslot = Maxslot;
		c->slot = malloc(sizeof(Slot) * c->nslot);
		if(c->slot == nil)
			goto Baddev;
		c->sfree = nil;
		for(i=0; i<c->nslot; i++){
			s = &c->slot[i];
			s->out = malloc(Msize + 64);	/* +shim growth slack */
			s->in = malloc(Msize);
			if(s->out == nil || s->in == nil){
				print("virtio9p: no memory for slots\n");
				goto Baddev;
			}
			s->next = c->sfree;
			c->sfree = s;
		}

		*tail = c;
		tail = &c->next;
		print("#9/%d: virtio9p tag \"%s\" nslot %d msize %d\n",
			ndev++, c->tag, c->nslot, Msize);
	}
}

static void
v9start(Ctlr *c)
{
	if(c->started)
		return;
	pcisetbme(c->pci);
	intrenable(c->pci->intl, v9interrupt, c, c->pci->tbdf, "virtio9p");
	coherence();
	c->cfg->queuesel = 0;
	c->cfg->queueenable = 1;
	c->cfg->status |= DriverOk;
	c->started = 1;
}

/* harvest completed chains: recycle descriptors, queue replies, wake readers */
static void
vqharvest(Ctlr *c)
{
	Vqueue *q;
	Slot *s;
	int id, free, m, ix;

	q = c->vq;
	m = q->size - 1;
	ilock(q);
	while((q->lastused ^ q->used->idx) & 0xFFFF){
		ix = q->lastused & m;
		id = q->usedent[ix].id;
		s = q->rock[id];
		q->rock[id] = nil;
		if(s != nil)
			s->inlen = q->usedent[ix].len;
		q->lastused++;
		do {
			free = id;
			id = q->desc[free].next;
			q->desc[free].next = q->free;
			q->free = free;
			q->nfree++;
		} while(q->desc[free].flags & Next);
		if(s != nil){
			s->next = nil;
			if(c->donetl != nil)
				c->donetl->next = s;
			else
				c->donehd = s;
			c->donetl = s;
		}
	}
	iunlock(q);
	wakeup(&c->rwait);
}

static void
v9interrupt(Ureg*, void *arg)
{
	Ctlr *c = arg;

	if(c->isr[0] & 1)
		vqharvest(c);
}

static int
havereply(void *arg)
{
	return ((Ctlr*)arg)->donehd != nil;
}

static int
haveslot(void *arg)
{
	return ((Ctlr*)arg)->sfree != nil;
}

/* take a free slot, blocking until one is available */
static Slot*
getslot(Ctlr *c)
{
	Vqueue *q;
	Slot *s;

	q = c->vq;
	ilock(q);
	while(c->sfree == nil){
		iunlock(q);
		while(waserror())
			;
		tsleep(&c->wwait, haveslot, c, 1000);
		poperror();
		vqharvest(c);
		ilock(q);
	}
	s = c->sfree;
	c->sfree = s->next;
	s->next = nil;
	iunlock(q);
	return s;
}

static void
putslot(Ctlr *c, Slot *s)
{
	Vqueue *q;

	q = c->vq;
	ilock(q);
	s->next = c->sfree;
	c->sfree = s;
	iunlock(q);
	wakeup(&c->wwait);
}

/* dequeue the oldest completed reply, blocking until one arrives */
static Slot*
getreply(Ctlr *c)
{
	Vqueue *q;
	Slot *s;

	q = c->vq;
	ilock(q);
	while(c->donehd == nil){
		iunlock(q);
		while(waserror())
			;
		tsleep(&c->rwait, havereply, c, 1000);
		poperror();
		if(c->donehd == nil)
			vqharvest(c);		/* in case the irq was missed */
		ilock(q);
	}
	s = c->donehd;
	c->donehd = s->next;
	if(c->donehd == nil)
		c->donetl = nil;
	iunlock(q);
	s->next = nil;
	return s;
}

/* nslot <= size/2, so a free slot guarantees >= 2 free descriptors */
static void
submit(Ctlr *c, Slot *s, long outlen)
{
	Vqueue *q;
	Vdesc *d;
	int head, free;

	q = c->vq;
	ilock(q);
	head = free = q->free;
	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(s->out);
	d->len = outlen;
	d->flags = Next;
	d = &q->desc[free]; free = d->next;
	d->addr = PADDR(s->in);
	d->len = Msize;
	d->flags = Write;
	q->free = free;
	q->nfree -= 2;
	q->rock[head] = s;
	q->availent[q->avail->idx & (q->size-1)] = head;
	coherence();
	q->avail->idx++;
	iunlock(q);
	if((q->used->flags & 1) == 0)
		*((u16int*)q->notify) = q->idx;
}

/* drop any queued/in-progress replies back to the free list */
static void
v9flush(Ctlr *c)
{
	Vqueue *q;
	Slot *s, *nx;

	q = c->vq;
	ilock(q);
	if(c->cur != nil){
		c->cur->next = c->sfree;
		c->sfree = c->cur;
		c->cur = nil;
	}
	for(s = c->donehd; s != nil; s = nx){
		nx = s->next;
		s->next = c->sfree;
		c->sfree = s;
	}
	c->donehd = c->donetl = nil;
	iunlock(q);
	clearfids(c);
}

/*
 * The directory-fid set: only fids that are directories are stored
 * (presence => directory). It is touched only from the single mount
 * reader (v9read) and at open/close, so it needs no lock.
 */
static int
isdirfid(Ctlr *c, u32int fid)
{
	Fid *f;
	int r = 0;

	qlock(&c->fidlk);
	for(f = c->fids[fid % Fidhash]; f != nil; f = f->next)
		if(f->fid == fid){
			r = 1;
			break;
		}
	qunlock(&c->fidlk);
	return r;
}

static void
setdirfid(Ctlr *c, u32int fid, int isdir)
{
	Fid **pp, *f;

	qlock(&c->fidlk);
	for(pp = &c->fids[fid % Fidhash]; (f = *pp) != nil; pp = &f->next)
		if(f->fid == fid){
			if(isdir)
				f->soff = 0;	/* (re)established: read from the start */
			else {			/* no longer a directory */
				*pp = f->next;
				free(f);
			}
			qunlock(&c->fidlk);
			return;
		}
	if(isdir && (f = malloc(sizeof(Fid))) != nil){
		f->fid = fid;
		f->soff = 0;
		f->next = c->fids[fid % Fidhash];
		c->fids[fid % Fidhash] = f;
	}
	qunlock(&c->fidlk);
}

/*
 * Directory-read offset translation. The client counts cumulative 9P2000
 * bytes, but qemu counts 9P2000.u bytes (its entries are larger), so the two
 * offsets diverge across a multi-read directory. We track qemu's offset per
 * directory fid: dirsoffget rewrites an outgoing Tread's offset to it (and
 * rewinds on a client offset of 0), dirsoffadv advances it by the bytes qemu
 * actually returned. Returns 1 (and sets *soff) iff fid is a directory.
 */
static int
dirsoffget(Ctlr *c, u32int fid, vlong coff, vlong *soff)
{
	Fid *f;
	int r = 0;

	qlock(&c->fidlk);
	for(f = c->fids[fid % Fidhash]; f != nil; f = f->next)
		if(f->fid == fid){
			if(coff == 0)
				f->soff = 0;
			*soff = f->soff;
			r = 1;
			break;
		}
	qunlock(&c->fidlk);
	return r;
}

static void
dirsoffadv(Ctlr *c, u32int fid, long n)
{
	Fid *f;

	qlock(&c->fidlk);
	for(f = c->fids[fid % Fidhash]; f != nil; f = f->next)
		if(f->fid == fid){
			f->soff += n;
			break;
		}
	qunlock(&c->fidlk);
}

static void
clearfids(Ctlr *c)
{
	Fid *f, *nx;
	int i;

	qlock(&c->fidlk);
	for(i = 0; i < Fidhash; i++){
		for(f = c->fids[i]; f != nil; f = nx){
			nx = f->next;
			free(f);
		}
		c->fids[i] = nil;
	}
	qunlock(&c->fidlk);
}

/*
 * 9P2000 and 9P2000.u stat entries differ only in a trailing
 * extension[s] n_uid[4] n_gid[4] n_muid[4] that .u appends after muid[s].
 * statbase returns the length of the common (9P2000) part: type[2]
 * through muid[s], given a pointer just past the leading size[2].
 */
static int
statbase(uchar *p, uchar *e)
{
	int i, ns;
	uchar *q;

	q = p + (BIT16SZ+BIT32SZ+QIDSZ+3*BIT32SZ+BIT64SZ);	/* .. length[8] */
	for(i = 0; i < 4; i++){					/* name uid gid muid */
		if(q+BIT16SZ > e)
			return -1;
		ns = GBIT16(q);
		q += BIT16SZ + ns;
	}
	if(q > e)
		return -1;
	return q - p;
}

/* .u stat entry at in -> 9P2000 entry at out; returns the new entry length */
static int
ustat2p9(uchar *in, uchar *e, uchar *out)
{
	int base;
	uchar *st;

	if((base = statbase(in+BIT16SZ, e)) < 0)
		return -1;
	/*
	 * Plan 9 directories report length 0; qemu reports the host dir size.
	 * disk/mkfs writes a directory's length as its archive entry's data size
	 * but emits no data for it, so a nonzero length desyncs the archive
	 * ("corrupt archive"). Normalize on the pristine .u source before the
	 * copy (st-relative offsets: type[2] dev[4] qid mode[4] atime mtime len[8]).
	 */
	st = in + BIT16SZ;
	if(st[BIT16SZ+BIT32SZ+QIDSZ + BIT32SZ-1] & 0x80){	/* DMDIR = top bit of mode[4] */
		uchar *lp = st + BIT16SZ+BIT32SZ+QIDSZ+3*BIT32SZ;
		lp[0]=lp[1]=lp[2]=lp[3]=lp[4]=lp[5]=lp[6]=lp[7]=0;	/* length = 0 */
	}
	PBIT16(out, base);
	memmove(out+BIT16SZ, in+BIT16SZ, base);
	return BIT16SZ + base;
}

/* 9P2000 stat entry at in -> .u entry at out; returns the new entry length */
static int
p9stat2u(uchar *in, uchar *e, uchar *out)
{
	int base;
	uchar *q;

	if((base = statbase(in+BIT16SZ, e)) < 0)
		return -1;
	PBIT16(out, base + BIT16SZ + 3*BIT32SZ);
	memmove(out+BIT16SZ, in+BIT16SZ, base);
	q = out + BIT16SZ + base;
	PBIT16(q, 0); q += BIT16SZ;		/* extension = "" */
	PBIT32(q, ~0U); q += BIT32SZ;		/* n_uid */
	PBIT32(q, ~0U); q += BIT32SZ;		/* n_gid */
	PBIT32(q, ~0U);				/* n_muid */
	return BIT16SZ + base + BIT16SZ + 3*BIT32SZ;
}

/* rebuild a T/Rversion message in o with version string v; returns its size */
static long
fixversion(uchar *m, uchar *o, char *v)
{
	long vl, sz;

	vl = strlen(v);
	sz = BIT32SZ+BIT8SZ+BIT16SZ + BIT32SZ + BIT16SZ + vl;
	if(o != m)
		memmove(o, m, BIT32SZ+BIT8SZ+BIT16SZ + BIT32SZ);
	PBIT16(o + BIT32SZ+BIT8SZ+BIT16SZ + BIT32SZ, vl);
	memmove(o + BIT32SZ+BIT8SZ+BIT16SZ + BIT32SZ + BIT16SZ, v, vl);
	PBIT32(o, sz);
	return sz;
}

/* rewrite an outgoing T-message into o; returns the new length */
static long
tshim(uchar *m, long n, uchar *o)
{
	int so, elen;

	switch(m[BIT32SZ]){
	case Tversion:
		return fixversion(m, o, "9P2000.u");
	case Tauth:
	case Tattach:
		memmove(o, m, n);
		PBIT32(o+n, ~0U);		/* n_uname = NONUNAME (both append it) */
		PBIT32(o, n + BIT32SZ);
		return n + BIT32SZ;
	case Tcreate:
		memmove(o, m, n);
		PBIT16(o+n, 0);			/* extension = "" */
		PBIT32(o, n + BIT16SZ);
		return n + BIT16SZ;
	case Twstat:
		so = BIT32SZ+BIT8SZ+BIT16SZ+BIT32SZ;	/* offset of nstat[2] */
		elen = p9stat2u(m+so+BIT16SZ, m+n, o+so+BIT16SZ);
		if(elen < 0)
			break;
		memmove(o, m, so+BIT16SZ);
		PBIT16(o+so, elen);
		PBIT32(o, so+BIT16SZ+elen);
		return so+BIT16SZ+elen;
	}
	memmove(o, m, n);
	return n;
}

/* Rstat: convert the single .u stat back to 9P2000, in place */
static void
rstatfix(Slot *s)
{
	uchar *m;
	int so, elen;

	m = s->in;
	so = BIT32SZ+BIT8SZ+BIT16SZ;		/* offset of nstat[2] */
	elen = ustat2p9(m+so+BIT16SZ, m+s->inlen, m+so+BIT16SZ);
	if(elen < 0)
		return;
	PBIT16(m+so, elen);
	s->inlen = so + BIT16SZ + elen;
	PBIT32(m, s->inlen);
}

/*
 * Rread of a directory: convert each .u stat entry to 9P2000, in place, and
 * drop the "." and ".." entries. qemu returns them (Unix readdir semantics)
 * but Plan 9 directories never contain them, and software assumes so: a
 * recursive mk(1) that builds its subdir list from `ls` would `cd .; mk` into
 * an infinite descent (observed: 2000 nested procs -> "no procs" panic).
 */
static int
isdotname(uchar *rp, uchar *de)
{
	int nl;
	char *nm;
	uchar *np;

	np = rp + BIT16SZ + (BIT16SZ+BIT32SZ+QIDSZ+3*BIT32SZ+BIT64SZ);	/* -> name[s] */
	if(np + BIT16SZ > de)
		return 0;
	nl = GBIT16(np);
	nm = (char*)np + BIT16SZ;
	if(np + BIT16SZ + nl > de)
		return 0;
	return (nl == 1 && nm[0] == '.')
	    || (nl == 2 && nm[0] == '.' && nm[1] == '.');
}

static void
rreaddir(Slot *s)
{
	uchar *m, *rp, *wp, *de;
	int co, count, ulen, elen;

	m = s->in;
	co = BIT32SZ+BIT8SZ+BIT16SZ;		/* offset of count[4] */
	count = GBIT32(m+co);
	rp = wp = m + co + BIT32SZ;
	de = rp + count;
	while(rp + BIT16SZ <= de){
		ulen = BIT16SZ + GBIT16(rp);
		if(rp + ulen > de)
			break;
		if(isdotname(rp, de)){		/* drop . and .. */
			rp += ulen;
			continue;
		}
		if((elen = ustat2p9(rp, de, wp)) < 0)
			break;
		wp += elen;
		rp += ulen;
	}
	count = wp - (m + co + BIT32SZ);
	PBIT32(m+co, count);
	s->inlen = co + BIT32SZ + count;
	PBIT32(m, s->inlen);
}

/*
 * qemu's 9P2000.u returns Unix (strerror) error strings plus a numeric errno.
 * Plan 9 programs match on the string; ape's _syserrno table is phrased the
 * Plan 9 way, so a Unix string like "No such file or directory" matches
 * nothing and ape defaults it to EINVAL. That is fatal in practice: patch(1)
 * treats a missing parent directory (ENOENT) as a hard error instead of
 * creating it, aborting a whole diff. Map the numeric errno (Linux values,
 * what qemu sends) back to the canonical Plan 9 string the system expects.
 */
static char*
uerrstr(int err)
{
	switch(err){
	case 1:  return "permission denied";		/* EPERM */
	case 2:  return "does not exist";		/* ENOENT */
	case 5:  return "i/o error";			/* EIO */
	case 9:  return "fd out of range or not open";	/* EBADF */
	case 12: return "no free memory";		/* ENOMEM */
	case 13: return "permission denied";		/* EACCES */
	case 16: return "device or object already in use"; /* EBUSY */
	case 17: return "create: file exists";	/* EEXIST */
	case 20: return "not a directory";		/* ENOTDIR */
	case 21: return "file is a directory";		/* EISDIR */
	case 22: return "bad arg in system call";	/* EINVAL */
	case 28: return "file system full";		/* ENOSPC */
	case 30: return "file system read only";	/* EROFS */
	case 39: return "directory not empty";		/* ENOTEMPTY */
	}
	return nil;
}

/*
 * Fix up a completed reply in place and maintain the directory-fid set.
 * s->op and s->fid/newfid/nwname were recorded when the matching T went out.
 */
static void
rfixup(Ctlr *cl, Slot *s)
{
	uchar *m;
	long n;

	m = s->in;
	n = s->inlen;
	if(n < BIT32SZ+BIT8SZ+BIT16SZ)
		return;
	switch(m[BIT32SZ]){
	case Rversion:
		s->inlen = fixversion(m, m, "9P2000");
		return;
	case Rerror: {
		int ns, err;
		char *ps;

		/* .u format: ename[s] then errno[4]. Rewrite the ename from the
		 * numeric errno to a Plan 9 string the system recognises; if the
		 * errno is unknown, keep qemu's ename and just drop the trailing
		 * errno[4] to leave a valid 9P2000 Rerror. */
		ns = GBIT16(m + BIT32SZ+BIT8SZ+BIT16SZ);
		if(n >= BIT32SZ+BIT8SZ+BIT16SZ + BIT16SZ + ns + BIT32SZ){
			err = GBIT32(m + BIT32SZ+BIT8SZ+BIT16SZ + BIT16SZ + ns);
			if((ps = uerrstr(err)) != nil){
				ns = strlen(ps);
				memmove(m + BIT32SZ+BIT8SZ+BIT16SZ + BIT16SZ, ps, ns);
				PBIT16(m + BIT32SZ+BIT8SZ+BIT16SZ, ns);
			}
			s->inlen = BIT32SZ+BIT8SZ+BIT16SZ + BIT16SZ + ns;
			PBIT32(m, s->inlen);
		}
		return;
	}
	case Ropen:		/* a fid must be opened before it can be read */
	case Rcreate:
		setdirfid(cl, s->fid, GBIT8(m + BIT32SZ+BIT8SZ+BIT16SZ) & QTDIR);
		return;
	case Rclunk:
	case Rremove:
		setdirfid(cl, s->fid, 0);
		return;
	case Rstat:
		rstatfix(s);
		return;
	case Rread:
		if(isdirfid(cl, s->fid)){
			long sc = GBIT32(m+BIT32SZ+BIT8SZ+BIT16SZ);	/* qemu bytes */
			rreaddir(s);
			dirsoffadv(cl, s->fid, sc);
		}
		return;
	}
}

static Ctlr*
ctlrindex(int i)
{
	Ctlr *c;

	for(c = ctlrs; c != nil && i > 0; c = c->next, i--)
		;
	return c;
}

static int
v9gen(Chan *c, char*, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	char buf[16];

	if(s == DEVDOTDOT){
		mkqid(&q, 0, 0, QTDIR);
		devdir(c, q, "#9", 0, eve, DMDIR|0555, dp);
		return 1;
	}
	if(ctlrindex(s) == nil)
		return -1;
	snprint(buf, sizeof buf, "%d", s);
	mkqid(&q, s+1, 0, QTFILE);
	devdir(c, q, buf, 0, eve, 0660, dp);
	return 1;
}

static void
v9reset(void)
{
	v9probe();
}

static Chan*
v9attach(char *spec)
{
	return devattach('9', spec);
}

static Walkqid*
v9walk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, v9gen);
}

static int
v9stat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, v9gen);
}

static Chan*
v9open(Chan *c, int omode)
{
	Ctlr *cl;
	Vqueue *q;

	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Eperm);
		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	cl = ctlrindex(c->qid.path - 1);
	if(cl == nil)
		error(Enonexist);

	q = cl->vq;
	ilock(q);
	if(cl->inuse){
		iunlock(q);
		error(Einuse);
	}
	cl->inuse = 1;
	iunlock(q);
	if(waserror()){
		ilock(q);
		cl->inuse = 0;
		iunlock(q);
		nexterror();
	}
	v9start(cl);
	v9flush(cl);		/* start the conversation clean */
	poperror();

	c->aux = cl;
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
v9close(Chan *c)
{
	Ctlr *cl;
	Vqueue *q;

	if((c->flag & COPEN) == 0 || (c->qid.type & QTDIR))
		return;
	cl = c->aux;
	if(cl == nil)
		return;
	v9flush(cl);
	q = cl->vq;
	ilock(q);
	cl->inuse = 0;
	iunlock(q);
}

static long
v9read(Chan *c, void *a, long n, vlong)
{
	Ctlr *cl;
	Slot *s;
	long k;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, nil, 0, v9gen);
	if(n <= 0)
		return 0;

	cl = c->aux;
	s = cl->cur;
	if(s == nil){
		s = getreply(cl);
		rfixup(cl, s);
		s->rp = 0;
		cl->cur = s;
	}
	k = s->inlen - s->rp;
	if(k > n)
		k = n;
	memmove(a, s->in + s->rp, k);
	s->rp += k;
	if(s->rp >= s->inlen){
		cl->cur = nil;
		putslot(cl, s);
	}
	return k;
}

static long
v9write(Chan *c, void *a, long n, vlong)
{
	Ctlr *cl;
	Slot *s;
	uchar *m;
	long outlen;

	if(c->qid.type & QTDIR)
		error(Eperm);
	if(n < BIT32SZ+BIT8SZ+BIT16SZ)
		error("invalid 9P message");

	cl = c->aux;
	m = a;
	s = getslot(cl);

	/* record the fid so rfixup can match the reply (reads need an open) */
	s->op = m[BIT32SZ];
	switch(s->op){
	case Topen:
	case Tcreate:
	case Tread:
	case Tclunk:
	case Tremove:
		s->fid = GBIT32(m + BIT32SZ+BIT8SZ+BIT16SZ);
		break;
	}

	outlen = tshim(m, n, s->out);

	/* a directory read needs its offset mapped into qemu's 9P2000.u space.
	 * the braces matter: PBIT64 is a multi-statement macro. */
	if(s->op == Tread){
		vlong soff;
		if(dirsoffget(cl, s->fid, GBIT64(m+BIT32SZ+BIT8SZ+BIT16SZ+BIT32SZ), &soff)){
			PBIT64(s->out+BIT32SZ+BIT8SZ+BIT16SZ+BIT32SZ, soff);
		}
	}

	submit(cl, s, outlen);
	return n;
}

Dev virtio9pdevtab = {
	'9',
	"virtio9p",

	v9reset,
	devinit,
	devshutdown,
	v9attach,
	v9walk,
	v9stat,
	v9open,
	devcreate,
	v9close,
	v9read,
	devbread,
	v9write,
	devbwrite,
	devremove,
	devwstat,
};
