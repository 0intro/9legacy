/*
 * Cadence GEM gigabit ethernet for Raspberry Pi 5 RP1
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "etherif.h"
#include "../bcm/ethermii.h"

#define ETHERADDR	(VIRTIO+0x80100000ull)

enum {
	GpioGEM		= 32,	/* reset pin */

	Ringsize	= 256,	/* power of 2 */
	Rbsz		= 1600,	/* multiple of 64 */

	/* GEM registers */
	Ctl		= 0x00/4,
	Cfg		= 0x04/4,
	Sts		= 0x08/4,
	Dmacfg	= 0x10/4,
	Txsts	= 0x14/4,
	Rxdescq	= 0x18/4,
	Txdescq	= 0x1c/4,
	Rxsts	= 0x20/4,
	Intsts	= 0x24/4,
	Intena	= 0x28/4,
	Intdis	= 0x2c/4,
	Phymgmt	= 0x34/4,
	Revision= 0xfc/4,
	Hashbot	= 0x80/4,
	Hashtop	= 0x84/4,
	Specaddr1bot	= 0x88/4,
	Specaddr1top	= 0x8c/4,
	Txdescqhi = 0x4c8/4,
	Rxdescqhi = 0x4d4/4,

	/* Ctl bits */
	Txhalt	= 1<<10,
	Txstart	= 1<<9,
	Mden	= 1<<4,
	Txallow	= 1<<3,
	Rxallow	= 1<<2,

	/* Sts bits */
	Phyidle		= 1<<2,

	/* Intsts / Intena / Intdis bits */
	Txdoneint	= 1<<7,
	Rxrcvdint	= 1<<1,

	/* Cfg bits */
	Sgmii		= 1<<27,	/* enable gb mii, thus gb speed */
	Rxigncrc	= 1<<26,
	Rxckoffl	= 1<<24,	/* rx checksum offload: drop bad pkts */
	Mdcclkdivshft	= 18,
	Mdcclkdiv	= 7<<18,	/* default 2; 4 for Gb/s */
	Rxomitfcs	= 1<<17,	/* rx omit crc in memory */
	Pcsmand		= 1<<11,	/* not TBI */
	Gbmode		= 1<<10,	/* gigabit mode enable */
	Rxunihashfilt	= 1<<7,
	Rxmultihashfilt	= 1<<6,
	Rxnobcast	= 1<<5,		/* discard broadcasts */
	Caf		= 1<<4,		/* copy all frames (promiscuous mode) */
	Fd		= 1<<1,		/* full duplex */

	/* Dmacfg bits */
	Dmaaddr64	= 1 << 30,	/* use 64-bit dma addresses */
	Txextbd		= 1 << 29,	/* extended tx buffer descriptors */
	Rxextbd		= 1 << 28,	/* extended rx buffer descriptors */
	Rxbsshft	= 16,
	Rxbufsize	= MASK(8)<<Rxbsshft,	/* rx buffer size / 64 */
	Bepkt		= 1 << 7,	/* big-endian packet data swap; def 1 */
	Bemgmt		= 1 << 6,	/* big-endian mgmt desc access */
	/* ether0 has 4 (incr16) set, ether1 has 3 (incr8) set */
	Blen		= MASK(5),	/* dma burst len; def 4 (incr4) */
};

typedef struct Ctlr Ctlr;
typedef struct Ring Ring;
typedef struct Desc Desc;

struct Desc {
	u32int	addr;
	u32int	size_flags;
	u32int	addrhi;
	u32int	unused;
};

enum {
	/* receive descriptor addr bits */
	Rdlast	= 1<<1,
	Full	= 1<<0,
	/* receive descriptor size_flags bits */
	Rdeofr	= 1<<15,	/* buffer contains end of frame (packet) */
	Rdsofr	= 1<<14,	/* buffer contains start of frame (packet) */
	Rdbadfcs= 1<<13,	/* bad crc & ignore-fcs enabled */
	Rdpktsize= MASK(13),
	/* transmit descriptor size_flags bits */
	Tdlast	= 1<<30,
	Tdeofr	= 1<<15,
	Sent	= 1<<31,
};

struct Ring {
	Desc	*ring;
	uint	head;
	uint	tail;
};

struct Ctlr {
	Ether	*edev;
	Mii		mii;
	u32int	*regs;
	Ring	txq;
	Ring	rxq;
	QLock	alock;
	Lock	txlock;
	Rendez	rrendez;
};

static Ctlr *gemctlr;
static void gemrproc(void*);

/*
 * active-low pulse gpio pin 32 => reset phy
 */
static void
resetphy(void)
{
	gpioselrp1(GpioGEM, 5);
	gpiopulloffrp1(GpioGEM);
	gpiooutrp1(GpioGEM, 0);
	delay(10);
	gpiooutrp1(GpioGEM, 1);
	delay(10);
}

static int
mdiow(Mii *mii, int phy, int addr, int data)
{
	Ctlr *ctlr;
	uint *regs;

	ctlr = mii->ctlr;
	regs = ctlr->regs;
	regs[Ctl] |= Mden;

	regs[Phymgmt] = 1<<30 | 1<<28 | phy<<23 | addr<<18 | 2<<16 | data;
	while((regs[Sts]&Phyidle) == 0)
		;
	regs[Ctl] &= ~Mden;
	return 0;
}

static int
mdior(Mii *mii, int phy, int addr)
{
	Ctlr *ctlr;
	uint *regs;
	int data;

	ctlr = mii->ctlr;
	regs = ctlr->regs;
	regs[Ctl] |= Mden;

	regs[Phymgmt] = 1<<30 | 2<<28 | phy<<23 | addr<<18 | 2<<16;
	while((regs[Sts]&Phyidle) == 0)
		;
	data = regs[Phymgmt];
	regs[Ctl] &= ~Mden;
	return data & 0xFFFF;
}

Desc*
ringhead(Ring *r)
{
	if((r->head - r->tail) == Ringsize)
		return nil;
	return &r->ring[r->head & (Ringsize-1)];
}
	
Desc*
ringtail(Ring *r)
{
	if(r->head == r->tail)
		return nil;
	return &r->ring[r->tail & (Ringsize-1)];
}

static int
inputrxbufs(Ctlr *ctlr)
{
	Ring *r;
	Desc *rd;
	Block *bp;
	int n, len;

	r = &ctlr->rxq;
	n = 0;
	while((rd = ringtail(r)) != nil && (rd->addr & Full)){
		n++;
		bp = (Block*)KADDR(rd->unused);
		rd->addr &= (Rdlast|Full);
		coherence();
		r->tail++;
		if((rd->size_flags & (Rdeofr|Rdsofr|Rdbadfcs)) != (Rdeofr|Rdsofr)){
			iprint("ethergem: bad rx packet flags %ux\n", rd->size_flags);
			freeb(bp);
			continue;
		}
		len = rd->size_flags & Rdpktsize;
		if (len == ETHERMINTU)
			len += 4;	/* for 60-byte arps? */
		if (len > ETHERMAXTU)
			len = ETHERMAXTU;
		bp->wp = bp->rp + len;
		cachedinvse(bp->rp, len);
		etheriq(ctlr->edev, bp, 1);
	}
	return n;
}

static int
freetxbufs(Ctlr *ctlr)
{
	Ring *r;
	Desc *td;
	Block *bp;
	int n;

	r = &ctlr->txq;
	n = 0;
	while((td = ringtail(r)) != nil && (td->size_flags & Sent)){
		bp = (Block*)KADDR(td->unused);
		freeb(bp);
		td->addr = 0;
		coherence();
		r->tail++;
		n++;
	}
	return n;
}

static int
allocrxbufs(Ctlr *ctlr)
{
	Ring *r;
	Desc *rd;
	Block *bp;
	int n;

	r = &ctlr->rxq;
	n = 0;
	while((rd = ringhead(r)) != nil){
		if((rd->addr & ~Rdlast) != Full)
			print("gem: receive ring mangled\n");
		bp = allocb(Rbsz);
		if(bp == nil)
			break;
		cachedwbinvse(bp->rp, Rbsz);
		rd->addrhi = 0;
		rd->unused = PADDR(bp);
		coherence();
		rd->addr = (rd->addr & Rdlast) | PADDR(bp->rp);
		coherence();
		r->head++;
		n++;
	}
	return n;

}

static int
bufwork(void *a)
{
	Ctlr *ctlr;
	Desc *td, *rd;

	ctlr = a;
	if((td = ringtail(&ctlr->txq)) != nil && (td->size_flags & Sent))
		return 1;
	if((rd = ringhead(&ctlr->rxq)) != nil && (rd->addr & ~Rdlast) != Full)
		return 1;
	return 0;
}

void
gemdebug(void)
{
	Ctlr *ctlr;
	Ring *r;
	Desc *rd;
	u32int *regs;
	int i, h, t;

	ctlr = gemctlr;
	regs = ctlr->regs;
	print("ctl %ux intsts %ux txsts %ux rxsts %ux\n", regs[Ctl], regs[Intsts], regs[Txsts], regs[Rxsts]);
	r = &ctlr->rxq;
	h = r->head & (Ringsize-1);
	t = r->tail & (Ringsize-1);
	print("rxq %ud %ud:\n", r->head, r->tail);
	for(i = 0; i < Ringsize; i++){
		rd = &r->ring[i];
		print("%2.2d %ux %ux %ux %c%c\n", i, rd->addr, rd->unused, rd->size_flags, i == h? 'H' : ' ', i == t? 'T' : ' ');
	}
	r = &ctlr->txq;
	h = r->head & (Ringsize-1);
	t = r->tail & (Ringsize-1);
	print("txq %ud %ud:\n", r->head, r->tail);
	for(i = 0; i < Ringsize; i++){
		rd = &r->ring[i];
		print("%2.2d %ux %ux %ux %c%c\n", i, rd->addr, rd->unused, rd->size_flags, i == h? 'H' : ' ', i == t? 'T' : ' ');
	}
}

static long
gemifstat(Ether *edev, void *a, long n, ulong offset)
{
	Ctlr *ctlr;
	char *p;

	ctlr = edev->ctlr;
	USED(ctlr);
	p = "these stats intentionally left blank";
	n = readstr(offset, a, n, p);
	return n;
}

static void
gemshutdown(Ether *edev)
{
	u32int *regs;

	regs = ((Ctlr*)edev->ctlr)->regs;
	regs[Ctl] |= Txhalt;
	regs[Ctl] &= ~(Txallow | Rxallow);
	regs[Intdis] = ~0;
	regs[Intsts] = ~0;
}

static void
gemattach(Ether *edev)
{
	Ctlr *ctlr;
	u32int *regs;
	uchar *ea;
	int i;

	ctlr = edev->ctlr;
	regs = ctlr->regs;
	qlock(&ctlr->alock);
	if(ctlr->edev != nil){
		qunlock(&ctlr->alock);
		return;
	}
	/* configure ether address (must write bottom first) */
	ea = edev->ea;
	regs[Specaddr1bot] = ea[3]<<24 | ea[2]<<16 | ea[1]<<8 | ea[0];
	regs[Specaddr1top] = ea[5]<<8 | ea[4];
	/* set up buffer descriptor rings in uncached memory */
	ctlr->txq.ring = (Desc*)DESCRIPTORS;
	ctlr->rxq.ring = (Desc*)(DESCRIPTORS + Ringsize*sizeof(Desc));
	memset(ctlr->txq.ring, 0, 2*Ringsize*sizeof(Desc));
	for(i = 0; i < Ringsize; i++){
		ctlr->txq.ring[i].size_flags = Sent;
		ctlr->rxq.ring[i].size_flags = Rbsz;
		ctlr->rxq.ring[i].addr = Full;
	}
	ctlr->txq.ring[Ringsize-1].size_flags |= Tdlast;
	ctlr->rxq.ring[Ringsize-1].addr |= Rdlast;
	/* configure addresses of descriptor rings */
	regs[Txdescq] = (uintptr)ctlr->txq.ring - UCKZERO;
	regs[Txdescqhi] = 0;
	regs[Rxdescq] = (uintptr)ctlr->rxq.ring - UCKZERO;
	regs[Rxdescqhi] = 0;
	/* enable interrupts and transmit/receive */
	regs[Txsts] = ~0;
	regs[Rxsts] = ~0;
	regs[Intsts] = ~0;
	regs[Intena] = Txdoneint | Rxrcvdint;
	regs[Ctl] |= Txallow | Rxallow;
	regs[Ctl] |= Txstart;
	kproc("gemrproc", gemrproc, edev);
	ctlr->edev = edev;
	qunlock(&ctlr->alock);
}

static void
gemtransmit(Ether *edev)
{
	Ctlr *ctlr;
	Block *bp;
	Desc *td;
	Ring *r;
	uint *regs;
	int n;

	ctlr = edev->ctlr;
	regs = ctlr->regs;
	r = &ctlr->txq;
	n = 0;
	ilock(&ctlr->txlock);
	while((td = ringhead(r)) != nil){
		if((bp = qget(edev->oq)) == nil)
			break;
		cachedwbse(bp->rp, BLEN(bp));
		td->addr = PADDR(bp->rp);
		td->addrhi = 0;
		td->unused = PADDR(bp);
		coherence();
		td->size_flags = (td->size_flags & Tdlast) | Tdeofr | BLEN(bp);
		coherence();
		r->head++;
		n++;
	}
	//if(td == nil && qcanread(edev->oq))
	//	iprint("gemtransmit(after %d): queue full\n", n);
	if(n)
		regs[Ctl] |= Txstart;
	iunlock(&ctlr->txlock);
}

static int
gemreset(Ctlr *ctlr)
{
	u32int *regs;
	uint cfg;

	ctlr->regs = regs = (u32int*)ETHERADDR;
	if(((regs[Revision]>>16) & 0xFF) != 0x07){
		iprint("gem: interface not found at %#p, Revision reg = %#x\n", regs, regs[Revision]);
		return -1;
	}
	//iprint("gem: Revision %#x\n", regs[Revision]);
	ctlr->mii.ctlr = ctlr;
	ctlr->mii.mir = mdior;
	ctlr->mii.miw = mdiow;
	resetphy();
	if(mii(&ctlr->mii, ~0) == 0){
		iprint("gem: no phy found");
		return -1;
	}
	//iprint("gem: phy oui %#x\n", ctlr->mii.curphy->oui);
	cfg = regs[Cfg];
	cfg &= ~(Rxnobcast|Rxckoffl|Mdcclkdiv);
	cfg |= Pcsmand | Rxomitfcs | Fd | Gbmode | Sgmii |
		Rxunihashfilt | Rxmultihashfilt | 4<<Mdcclkdivshft;
	regs[Cfg] = cfg;
	cfg = regs[Dmacfg];
	cfg &= ~(Rxbufsize|Blen|Txextbd|Rxextbd);
	cfg |= HOWMANY(Rbsz, 64) << Rxbsshft | 4;	/* 4 = incr4 */
	cfg |= Dmaaddr64;
	regs[Dmacfg] = cfg;
	regs[Hashtop] = regs[Hashbot] = ~0;	/* accept all for now */
	return 0;
}

static void
geminterrupt(Ureg*, void *v)
{
	Ctlr *ctlr;
	Ether *edev;
	uint *regs;
	int i, sts;

	edev = v;
	ctlr = edev->ctlr;
	regs = ctlr->regs;
	sts = regs[Intsts];
	for(i = 0; ; i++){
		regs[Intsts] = sts;
		inputrxbufs(ctlr);
		sts = regs[Intsts];
		if(sts == 0)
			break;
		if(i == 100){
			iprint("gem: interrupt stuck sts=%ux\n", sts);
			break;
		}
	}
	if(bufwork(ctlr))
		wakeup(&ctlr->rrendez);
}

static void
gemrproc(void *a)
{
	Ether *edev;
	Ctlr *ctlr;

	edev = a;
	ctlr = edev->ctlr;
	for(;;){
		allocrxbufs(ctlr);
		if(freetxbufs(ctlr))
			gemtransmit(edev);
		tsleep(&ctlr->rrendez, bufwork, ctlr, 250);
	}
}

static int
gempnp(Ether* edev)
{
	Ctlr *ctlr;

	ctlr = malloc(sizeof(Ctlr));
	if(ctlr == nil || gemreset(ctlr)){
		free(ctlr);
		return -1;
	}
	edev->ctlr = ctlr;
	gemshutdown(edev);
	edev->port = (uintptr)ctlr->regs;
	edev->irq = IRQgem;
	edev->mbps = 1000;
	edev->maxmtu = ETHERMAXTU;
	edev->attach = gemattach;
	edev->shutdown = gemshutdown;
	edev->transmit = gemtransmit;
	edev->interrupt = geminterrupt;
	edev->ifstat = gemifstat;

	edev->arg = edev;
	gemctlr = ctlr;
	return 0;
}

void
ethergemlink(void)
{
	addethercard("gem", gempnp);
}
