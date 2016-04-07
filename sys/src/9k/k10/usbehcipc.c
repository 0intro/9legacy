/*
 * PC-specific code for
 * USB Enhanced Host Controller Interface (EHCI) driver
 * High speed USB 2.0.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"
#include	"../port/portusbehci.h"
#include	"usbehci.h"

static Ctlr* ctlrs[Nhcis];
static int maxehci = Nhcis;

static int
ehciecap(Ctlr *ctlr, int cap)
{
	int i, off;

	off = (ctlr->capio->capparms >> Ceecpshift) & Ceecpmask;
	for(i=0; i<48; i++){
		if(off < 0x40 || (off & 3) != 0)
			break;
		if(pcicfgr8(ctlr->pcidev, off) == cap)
			return off;
		off = pcicfgr8(ctlr->pcidev, off+1);
	}
	return -1;
}

static void
getehci(Ctlr* ctlr)
{
	int i, off;

	off = ehciecap(ctlr, Clegacy);
	if(off == -1)
		return;
	if(pcicfgr8(ctlr->pcidev, off+CLbiossem) != 0){
		dprint("ehci %#p: bios active, taking over...\n", ctlr->capio);
		pcicfgw8(ctlr->pcidev, off+CLossem, 1);
		for(i = 0; i < 100; i++){
			if(pcicfgr8(ctlr->pcidev, off+CLbiossem) == 0)
				break;
			delay(10);
		}
		if(i == 100)
			print("ehci %#p: bios timed out\n", ctlr->capio);
	}
	pcicfgw32(ctlr->pcidev, off+CLcontrol, 0);	/* no SMIs */
}

static void
ehcireset(Ctlr *ctlr)
{
	Eopio *opio;
	int i;

	ilock(ctlr);
	dprint("ehci %#p reset\n", ctlr->capio);
	opio = ctlr->opio;

	/*
	 * reclaim from bios
	 */
	getehci(ctlr);

	/*
	 * route interrupts to other controllers until we're ready
	 */
	ehcirun(ctlr, 0);
	opio->config = 0;

	/* clear high 32 bits of address signals if it's 64 bits capable.
	 * This is probably not needed but it does not hurt and others do it.
	 */
	if((ctlr->capio->capparms & C64) != 0){
		dprint("ehci: 64 bits\n");
		opio->seg = 0;
		coherence();
	}

	if(ehcidebugcapio != ctlr->capio){
		opio->cmd |= Chcreset;	/* controller reset */
		coherence();
		for(i = 0; i < 100; i++){
			if((opio->cmd & Chcreset) == 0)
				break;
			delay(1);
		}
		if(i == 100)
			print("ehci %#p controller reset timed out\n", ctlr->capio);
	}

	/* requesting more interrupts per µframe may miss interrupts */
	opio->cmd &= ~Citcmask;
	opio->cmd |= 1 << Citcshift;		/* max of 1 intr. per 125 µs */
	coherence();
	switch(opio->cmd & Cflsmask){
	case Cfls1024:
		ctlr->nframes = 1024;
		break;
	case Cfls512:
		ctlr->nframes = 512;
		break;
	case Cfls256:
		ctlr->nframes = 256;
		break;
	default:
		panic("ehci: unknown fls %d", opio->cmd & Cflsmask);
	}
	dprint("ehci: %d frames\n", ctlr->nframes);
	iunlock(ctlr);
}

static void
setdebug(Hci*, int d)
{
	ehcidebug = d;
}

static void
shutdown(Hci *hp)
{
	int i;
	Ctlr *ctlr;
	Eopio *opio;

	ctlr = hp->aux;
	ilock(ctlr);
	opio = ctlr->opio;
	opio->cmd |= Chcreset;		/* controller reset */
	coherence();
	for(i = 0; i < 100; i++){
		if((opio->cmd & Chcreset) == 0)
			break;
		delay(1);
	}
	if(i >= 100)
		print("ehci %#p controller reset timed out\n", ctlr->capio);
	delay(100);
	ehcirun(ctlr, 0);
	opio->frbase = 0;
	iunlock(ctlr);
}

static int
checkdev(Pcidev *p)
{
	char *conf, *s, dev[32];

	conf = getconf("*badehci");
	if(conf == nil)
		return 0;
	snprint(dev, sizeof dev, "%.4ux/%.4ux", p->vid, p->did);

	s = strstr(conf, dev);
	if(s != nil && (s[9] == 0 || s[9] == ' '))
		return -1;
	return 0;
}
	
static void
scanpci(void)
{
	int i;
	uintmem io;
	Ctlr *ctlr;
	Pcidev *p;
	Ecapio *capio;
	static int already;

	if(already)
		return;
	already = 1;
	i = 0;
	for(p = nil; (p = pcimatch(p, 0, 0)) != nil; ) {
		/*
		 * Find EHCI controllers (Programming Interface = 0x20).
		 */
		if(p->ccrb != 0xc || p->ccru != 3 || p->ccrp != 0x20)
			continue;
		if(i == Nhcis){
			print("ehci: bug: more than %d controllers\n", Nhcis);
			continue;
		}
		if(checkdev(p) == -1){
			print("usbehci: ignore %.4ux/%.4ux\n", p->vid, p->did);
			continue;
		}
		io = p->mem[0].bar & ~(uintmem)0xf;
		if(io == 0){
			print("usbehci: %x %x: failed to map registers\n",
				p->vid, p->did);
			continue;
		}
		if(p->intl == 0xff || p->intl == 0) {
			print("usbehci: no irq assigned for port %#P\n", io);
			continue;
		}
		dprint("usbehci: %#x %#x: port %#P size %#x irq %d\n",
			p->vid, p->did, io, p->mem[0].size, p->intl);
		capio = vmap(io, p->mem[0].size);
		if(capio == nil){
			print("usbehci: can't vmap %#P\n", io);
			continue;
		}

		ctlr = malloc(sizeof(Ctlr));
		if (ctlr == nil)
			panic("usbehci: out of memory");
		ctlr->pcidev = p;
		ctlr->capio = capio;
		ctlr->opio = (Eopio*)((uintptr)capio + (capio->cap & 0xff));
		pcisetbme(p);
		pcisetpms(p, 0);

		/*
		 * currently, if we enable a second ehci controller on zt
		 * systems w x58m motherboard, we'll wedge solid after iunlock
		 * in init for the second one.
		 */
		if (i >= maxehci) {
			print("usbehci: ignoring controllers after first %d, "
				"at %#P\n", maxehci, io);
			pciclrbme(p);
			vunmap(capio, p->mem[0].size);
			free(ctlr);
			continue;
		}
		ctlrs[i++] = ctlr;
	}
}

static int
reset(Hci *hp)
{
	int i;
	char *s;
	Ctlr *ctlr;
	Ecapio *capio;
	Pcidev *p;
	static Lock resetlck;

	s = getconf("*maxehci");
	if(s != nil){
		i = strtoul(s, &s, 0);
		if(*s == 0)
			maxehci = i;
	}
	if(maxehci == 0 || getconf("*nousbehci"))
		return -1;

	ilock(&resetlck);
	scanpci();

	/*
	 * Any adapter matches if no hp->port is supplied,
	 * otherwise the ports must match.
	 */
	ctlr = nil;
	for(i = 0; i < Nhcis && ctlrs[i] != nil; i++){
		ctlr = ctlrs[i];
		if(ctlr->active == 0)
		if(hp->port == 0 || hp->port == (uintptr)ctlr->capio){
			ctlr->active = 1;
			break;
		}
	}
	iunlock(&resetlck);
	if(i >= Nhcis || ctlrs[i] == nil)
		return -1;

	p = ctlr->pcidev;
	hp->aux = ctlr;
	hp->port = (uintptr)ctlr->capio;
	hp->irq = p->intl;
	hp->tbdf = p->tbdf;

	capio = ctlr->capio;
	hp->nports = capio->parms & Cnports;

	ddprint("echi: %s, ncc %ud npcc %ud\n",
		capio->parms & 0x10000 ? "leds" : "no leds",
		(capio->parms >> 12) & 0xf, (capio->parms >> 8) & 0xf);
	ddprint("ehci: routing %s, %sport power ctl, %d ports\n",
		capio->parms & 0x40 ? "explicit" : "automatic",
		capio->parms & 0x10 ? "" : "no ", hp->nports);

	ehcireset(ctlr);
	ehcimeminit(ctlr);

	/*
	 * Linkage to the generic HCI driver.
	 */
	ehcilinkage(hp);
	hp->shutdown = shutdown;
	hp->debug = setdebug;
	return 0;
}

void
usbehcilink(void)
{
	addhcitype("ehci", reset);
}
