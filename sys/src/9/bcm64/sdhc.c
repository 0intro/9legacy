/*
 * bcm2711-2712 sd host controller
 *
 * Copyright © 2012,2019 Richard Miller <r.miller@acm.org>
 *
 * adapted from emmc.c
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

extern SDio *sdcardlink, *sdwifilink;
#define EMMC1REGS	(VIRTIO+0x00fff000)
#define EMMC2REGS	(VIRTIO+0x01100000)

#define okay(n)

enum {
	Extfreq		= 200*Mhz,	/* guess external clock frequency */
	Initfreq	= 400000,	/* initialisation frequency for MMC */
	SDfreq		= 25*Mhz,	/* standard SD frequency */
	SDfreqhs	= 50*Mhz,	/* high speed frequency */
	DTO		= 14,		/* data timeout exponent (guesswork) */

	GoIdle		= 0,		/* mmc/sdio go idle state */
	MMCSelect	= 7,		/* mmc/sd card select command */
	Setbuswidth	= 6,		/* mmc/sd set bus width command */
	Switchfunc	= 6,		/* mmc/sd switch function command */
	Voltageswitch = 11,		/* md/sdio switch to 1.8V */
	IORWdirect = 52,		/* sdio read/write direct command */
	IORWextended = 53,		/* sdio read/write extended command */
	Appcmd = 55,			/* mmc/sd application command prefix */
};

enum {
	/* Controller registers */
	SDMAaddr		= 0x00>>2,
	Blksizecnt		= 0x04>>2,
	Arg1			= 0x08>>2,
	Cmdtm			= 0x0c>>2,
	Resp0			= 0x10>>2,
	Resp1			= 0x14>>2,
	Resp2			= 0x18>>2,
	Resp3			= 0x1c>>2,
	Data			= 0x20>>2,
	Status			= 0x24>>2,
	Control0		= 0x28>>2,
	Control1		= 0x2c>>2,
	Interrupt		= 0x30>>2,
	Irptmask		= 0x34>>2,
	Irpten			= 0x38>>2,
	Control2		= 0x3c>>2,
	Capability		= 0x40>>2,
	Forceirpt		= 0x50>>2,
	Dmadesc			= 0x58>>2,
	Boottimeout		= 0x70>>2,
	Dbgsel			= 0x74>>2,
	Exrdfifocfg		= 0x80>>2,
	Exrdfifoen		= 0x84>>2,
	Tunestep		= 0x88>>2,
	Tunestepsstd		= 0x8c>>2,
	Tunestepsddr		= 0x90>>2,
	Spiintspt		= 0xf0>>2,
	Slotisrver		= 0xfc>>2,

	/* Control0 */
	Busvoltage		= 7<<9,
		V1_8		= 5<<9,
		V3_0		= 6<<9,
		V3_3		= 7<<9,
	Buspower		= 1<<8,
	Dwidth8			= 1<<5,
	Dmaselect		= 3<<3,
		DmaSDMA		= 0<<3,
		DmaADMA1	= 1<<3,
		DmaADMA2	= 2<<3,
	Hispeed			= 1<<2,
	Dwidth4			= 1<<1,
	Dwidth1			= 0<<1,
	LED			= 1<<0,

	/* Control1 */
	Srstdata		= 1<<26,	/* reset data circuit */
	Srstcmd			= 1<<25,	/* reset command circuit */
	Srsthc			= 1<<24,	/* reset complete host controller */
	Datatoshift		= 16,		/* data timeout unit exponent */
	Datatomask		= 0xF0000,
	Clkfreq8shift		= 8,		/* SD clock base divider LSBs */
	Clkfreq8mask		= 0xFF00,
	Clkfreqms2shift		= 6,		/* SD clock base divider MSBs */
	Clkfreqms2mask		= 0xC0,
	Clkgendiv		= 0<<5,		/* SD clock divided */
	Clkgenprog		= 1<<5,		/* SD clock programmable */
	Clken			= 1<<2,		/* SD clock enable */
	Clkstable		= 1<<1,	
	Clkintlen		= 1<<0,		/* enable internal EMMC clocks */

	/* Cmdtm */
	Indexshift		= 24,
	Suspend			= 1<<22,
	Resume			= 2<<22,
	Abort			= 3<<22,
	Isdata			= 1<<21,
	Ixchken			= 1<<20,
	Crcchken		= 1<<19,
	Respmask		= 3<<16,
	Respnone		= 0<<16,
	Resp136			= 1<<16,
	Resp48			= 2<<16,
	Resp48busy		= 3<<16,
	Multiblock		= 1<<5,
	Host2card		= 0<<4,
	Card2host		= 1<<4,
	Autocmd12		= 1<<2,
	Autocmd23		= 2<<2,
	Blkcnten		= 1<<1,
	Dmaen			= 1<<0,

	/* Interrupt */
	Admaerr		= 1<<25,
	Acmderr		= 1<<24,
	Denderr		= 1<<22,
	Dcrcerr		= 1<<21,
	Dtoerr		= 1<<20,
	Cbaderr		= 1<<19,
	Cenderr		= 1<<18,
	Ccrcerr		= 1<<17,
	Ctoerr		= 1<<16,
	Err		= 1<<15,
	Cardintr	= 1<<8,
	Cardinsert	= 1<<6,		/* not in Broadcom datasheet */
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Dmaintr		= 1<<3,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

	/* Status */
	Bufread		= 1<<11,	/* not in Broadcom datasheet */
	Bufwrite	= 1<<10,	/* not in Broadcom datasheet */
	Readtrans	= 1<<9,
	Writetrans	= 1<<8,
	Datactive	= 1<<2,
	Datinhibit	= 1<<1,
	Cmdinhibit	= 1<<0,
};

static int cmdinfo[64] = {
[0]  Ixchken,
[2]  Resp136,
[3]  Resp48 | Ixchken | Crcchken,
[5]  Resp48,
[6]  Resp48 | Ixchken | Crcchken,
[7]  Resp48busy | Ixchken | Crcchken,
[8]  Resp48 | Ixchken | Crcchken,
[9]  Resp136,
[11] Resp48 | Ixchken | Crcchken,
[12] Resp48busy | Ixchken | Crcchken,
[13] Resp48 | Ixchken | Crcchken,
[16] Resp48,
[17] Resp48 | Isdata | Card2host | Ixchken | Crcchken,
[18] Resp48 | Isdata | Card2host | Multiblock | Blkcnten | Ixchken | Crcchken,
[24] Resp48 | Isdata | Host2card | Ixchken | Crcchken,
[25] Resp48 | Isdata | Host2card | Multiblock | Blkcnten | Ixchken | Crcchken,
[41] Resp48,
[52] Resp48 | Ixchken | Crcchken,
[53] Resp48	| Ixchken | Crcchken | Isdata,
[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Adma Adma;
typedef struct Ctlr Ctlr;

/*
 * ADMA2 descriptor
 *	See SD Host Controller Simplified Specification Version 2.00
 */

struct Adma {
	u32int	desc;
	u32int	addr;
};

enum {
	/* desc fields */
	Valid		= 1<<0,
	End			= 1<<1,
	Int			= 1<<2,
	Nop			= 0<<4,
	Tran		= 2<<4,
	Link		= 3<<4,
	OLength		= 16,
	/* maximum value for Length field */
	Maxdma		= ((1<<16) - 4),
};

struct Ctlr {
	u32int	*regs;
	int	irq;
	Rendez	r;
	Rendez	cardr;
	int	fastclock;
	ulong	extclk;
	int	appcmd;
	Adma	*dma;
};

static Ctlr emmc1 = {
	(u32int*)EMMC1REGS,
	IRQmmc,
};
static Ctlr emmc2 = {
	(u32int*)EMMC2REGS,
	IRQsdhci,
};

static void mmcinterrupt(Ureg*, void*);

#define WR(reg, val) { if(0)print("WR %2.2ux %ux\n", reg<<2, val); r[reg] = val; }

static uint
clkdiv(uint d)
{
	uint v;

	assert(d < 1<<10);
	v = (d << Clkfreq8shift) & Clkfreq8mask;
	v |= ((d >> 8) << Clkfreqms2shift) & Clkfreqms2mask;
	return v;
}

static Adma*
dmaalloc(void *addr, int len)
{
	int n;
	uintptr a;
	Adma *adma, *p;

	a = (uintptr)addr;
	n = HOWMANY(len, Maxdma);
	adma = sdmalloc(n * sizeof(Adma));
	for(p = adma; len > 0; p++){
		p->desc = Valid | Tran;
		if(n == 1)
			p->desc |= len<<OLength | End | Int;
		else
			p->desc |= Maxdma<<OLength;
		p->addr = (u32int)dmaaddr((void*)a);
		a += Maxdma;
		len -= Maxdma;
		n--;
	}
	cachedwbse(adma, (char*)p - (char*)adma);
	return adma;
}

static void
emmcclk(Ctlr *ctl, uint freq)
{
	u32int *r;
	uint div;
	int i;

	r = ctl->regs;
	div = ctl->extclk / (freq<<1);
	if(ctl->extclk / (div<<1) > freq)
		div++;
	WR(Control1, clkdiv(div) |
		DTO<<Datatoshift | Clkgendiv | Clkintlen);
	for(i = 0; i < 1000; i++){
		delay(1);
		if(r[Control1] & Clkstable)
			break;
	}
	if(i == 1000)
		print("sdhc: can't set clock to %ud\n", freq);
	else{
		WR(Control1, r[Control1] | Clken);
		delay(1);
	}
}

static int
datadone(void *a)
{
	int i;

	u32int *r = a;
	i = r[Interrupt];
	return i & (Datadone|Err);
}

static int
cardintready(void *a)
{
	int i;

	u32int *r = a;
	i = r[Interrupt];
	return i & Cardintr;
}

static int
sdhcinit(Ctlr *ctl)
{
	u32int *r;
	ulong clk;

	clk = soc.emmc2freq;
	if(clk == 0){
		clk = Extfreq;
		print("sdhc: assuming external clock %lud Mhz\n", clk/1000000);
	}else
		print("sdhc: external clock %lud Mhz\n", clk/1000000);
	ctl->extclk = clk;
	r = ctl->regs;
	if(0)print("sdhc init: control %8.8ux %8.8ux %8.8ux\n",
		r[Control0], r[Control1], r[Control2]);
	WR(Control1, Srsthc);
	delay(10);
	while(r[Control1] & Srsthc)
		;
	WR(Control1, Srstdata);
	delay(10);
	WR(Control1, 0);
	return 0;
}

static int
sdhcinquiry(Ctlr *ctl, char *inquiry, int inqlen)
{
	u32int *r;
	uint ver;

	r = ctl->regs;
	ver = r[Slotisrver] >> 16;
	return snprint(inquiry, inqlen,
		"BCM SD Host Controller %2.2x Version %2.2x",
		ver&0xFF, ver>>8);
}

static void
sdhcenable(Ctlr *ctl)
{
	u32int *r;

	r = ctl->regs;
	WR(Control0, 0);
	delay(1);
	WR(Control0, V3_3 | Buspower | Dwidth1 | DmaADMA2);
	WR(Control1, 0);
	delay(1);
	emmcclk(ctl, Initfreq);
	WR(Irpten, 0);
	WR(Irptmask, ~Dmaintr);
	WR(Interrupt, ~0);
	intrenable(ctl->irq, mmcinterrupt, ctl, 0, "sdhci");
}

int
sdwificardintr(int wait)
{
	u32int *r;
	int i;
	Ctlr *ctl;

	ctl = &emmc2;
	r = ctl->regs;
	while(((i = r[Interrupt]) & Cardintr) == 0){
		if(!wait)
			return 0;
		WR(Irptmask, r[Irptmask] | Cardintr);
		WR(Irpten, r[Irpten] | Cardintr);
		sleep(&ctl->cardr, cardintready, r);
	}
	WR(Irptmask, r[Irptmask] & ~Cardintr);
	return i;
}

static int
sdhccmd(Ctlr *ctl, u32int cmd, u32int arg, u32int *resp)
{
	u32int *r;
	u32int c;
	int i;
	ulong now;

	r = ctl->regs;
	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	c = (cmd << Indexshift) | cmdinfo[cmd];
	/*
	 * CMD6 may be Setbuswidth or Switchfunc depending on Appcmd prefix
	 */
	if(cmd == Switchfunc && !ctl->appcmd)
		c |= Isdata|Card2host;
	if(c & Isdata)
		c |= Dmaen;
	if(cmd == IORWextended){
		if(arg & (1<<31))
			c |= Host2card;
		else
			c |= Card2host;
		if((r[Blksizecnt]&0xFFFF0000) != 0x10000)
			c |= Multiblock | Blkcnten;
	}
	/*
	 * GoIdle indicates new card insertion: reset bus width & speed
	 */
	if(cmd == GoIdle){
		WR(Control0, r[Control0] & ~(Dwidth4|Hispeed));
		emmcclk(ctl, Initfreq);
	}
	if(r[Status] & Cmdinhibit){
		print("sdhc: need to reset Cmdinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstcmd);
		while(r[Control1] & Srstcmd)
			;
		while(r[Status] & Cmdinhibit)
			;
	}
	if((r[Status] & Datinhibit) &&
	   ((c & Isdata) || (c & Respmask) == Resp48busy)){
		print("sdhc: need to reset Datinhibit intr %ux stat %ux\n",
			r[Interrupt], r[Status]);
		WR(Control1, r[Control1] | Srstdata);
		while(r[Control1] & Srstdata)
			;
		while(r[Status] & Datinhibit)
			;
	}
	WR(Arg1, arg);
	if((i = (r[Interrupt] & ~Cardintr)) != 0){
		if(i != Cardinsert)
			print("sdhc: before command, intr was %ux\n", i);
		WR(Interrupt, i);
	}
	WR(Cmdtm, c);
	now = m->ticks;
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0)
		if(m->ticks-now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((i&~(Err|Cardintr)) != Ctoerr)
			print("sdhc: cmd %ux arg %ux error intr %ux stat %ux\n", c, arg, i, r[Status]);
		WR(Interrupt, i);
		if(r[Status]&Cmdinhibit){
			WR(Control1, r[Control1]|Srstcmd);
			while(r[Control1]&Srstcmd)
				;
		}
		error(Eio);
	}
	WR(Interrupt, i & ~(Datadone|Readrdy|Writerdy));
	switch(c & Respmask){
	case Resp136:
		resp[0] = r[Resp0]<<8;
		resp[1] = r[Resp0]>>24 | r[Resp1]<<8;
		resp[2] = r[Resp1]>>24 | r[Resp2]<<8;
		resp[3] = r[Resp2]>>24 | r[Resp3]<<8;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp0];
		break;
	case Respnone:
		resp[0] = 0;
		break;
	}
	if((c & Respmask) == Resp48busy){
		WR(Irpten, r[Irpten]|Datadone|Err);
		tsleep(&ctl->r, datadone, r, 3000);
		i = r[Interrupt];
		if((i & Datadone) == 0)
			print("sdhc: no Datadone after CMD%d\n", cmd);
		if(i & Err)
			print("sdhc: CMD%d error interrupt %ux\n",
				cmd, r[Interrupt]);
		WR(Interrupt, i);
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == MMCSelect){
		delay(1);
		emmcclk(ctl, SDfreq);
		delay(1);
		ctl->fastclock = 1;
	}
	if(cmd == Setbuswidth){
		if(ctl->appcmd){
			/*
			 * If card bus width changes, change host bus width
			 */
			switch(arg){
			case 0:
				WR(Control0, r[Control0] & ~Dwidth4);
				break;
			case 2:
				WR(Control0, r[Control0] | Dwidth4);
				break;
			}
		}else{
			/*
			 * If card switched into high speed mode, increase clock speed
			 */
			if((arg&0x8000000F) == 0x80000001){
				delay(1);
				emmcclk(ctl, SDfreqhs);
				delay(1);
			}
		}
	}else if(cmd == IORWdirect && (arg & ~0xFF) == (1<<31|0<<28|7<<9)){
		switch(arg & 0x3){
		case 0:
			WR(Control0, r[Control0] & ~Dwidth4);
			break;
		case 2:
			WR(Control0, r[Control0] | Dwidth4);
			//WR(Control0, r[Control0] | Hispeed);
			break;
		}
	}
	ctl->appcmd = (cmd == Appcmd);
	return 0;
}

static void
sdhciosetup(Ctlr *ctl, int write, void *buf, int bsize, int bcount)
{
	u32int *r;
	int len;

	r = ctl->regs;
	len = bsize * bcount;
	assert(((uintptr)buf&3) == 0);
	assert((len&3) == 0);
	assert(bsize <= 2048);
	WR(Blksizecnt, bcount<<16 | bsize);
	if(ctl->dma)
		sdfree(ctl->dma);
	ctl->dma = dmaalloc(buf, len);
	if(write)
		cachedwbse(buf, len);
	else
		cachedwbinvse(buf, len);
	WR(Dmadesc, (u32int)dmaaddr(ctl->dma));
	okay(1);
}

static void
sdhcio(Ctlr *ctl, int write, uchar *buf, int len)
{
	u32int *r;
	int i;

	r = ctl->regs;
	if(waserror()){
		okay(0);
		nexterror();
	}
	WR(Irpten, r[Irpten] | Datadone|Err);
	tsleep(&ctl->r, datadone, r, 3000);
	WR(Irpten, r[Irpten] & ~(Datadone|Err));
	i = r[Interrupt];
	if((i & (Datadone|Err)) != Datadone){
		print("sdhc: %s error intr %ux stat %ux\n",
			write? "write" : "read", i, r[Status]);
		if(r[Status] & Datinhibit)
			WR(Control1, r[Control1] | Srstdata);
			while(r[Control1] & Srstdata)
				;
			while(r[Status] & Datinhibit)
				;
		WR(Interrupt, i);
		error(Eio);
	}
	WR(Interrupt, i);
	if(!write)
		cachedinvse(buf, len);
	poperror();
	okay(0);
}

static void
mmcinterrupt(Ureg*, void *a)
{
	Ctlr *ctl;
	u32int *r;
	int i;

	ctl = a;
	r = ctl->regs;
	i = r[Interrupt];
	if(i&(Datadone|Err))
		wakeup(&ctl->r);
	if(i&Cardintr)
		wakeup(&ctl->cardr);
	WR(Irpten, r[Irpten] & ~i);
}

static int
emmcinit1(void)
{
	return sdhcinit(&emmc1);
}
static void
emmcenable1(void)
{
	sdhcenable(&emmc1);
}
static int
emmcinquiry1(char *inquiry, int inqlen)
{
	return sdhcinquiry(&emmc1, inquiry, inqlen);
}
static int
emmccmd1(u32int cmd, u32int arg, u32int *resp)
{
	return sdhccmd(&emmc1, cmd, arg, resp);
}
static void
emmciosetup1(int write, void *buf, int bsize, int bcount)
{
	sdhciosetup(&emmc1, write, buf, bsize, bcount);
}
static void
emmcio1(int write, uchar *buf, int len)
{
	sdhcio(&emmc1, write, buf, len);
}

static int
emmcinit2(void)
{
	return sdhcinit(&emmc2);
}
static void
emmcenable2(void)
{
	sdhcenable(&emmc2);
}
static int
emmcinquiry2(char *inquiry, int inqlen)
{
	return sdhcinquiry(&emmc2, inquiry, inqlen);
}
static int
emmccmd2(u32int cmd, u32int arg, u32int *resp)
{
	return sdhccmd(&emmc2, cmd, arg, resp);
}
static void
emmciosetup2(int write, void *buf, int bsize, int bcount)
{
	sdhciosetup(&emmc2, write, buf, bsize, bcount);
}
static void
emmcio2(int write, uchar *buf, int len)
{
	sdhcio(&emmc2, write, buf, len);
}

SDio sdiohci[2] = {
	{ "sdio1", emmcinit1, emmcenable1, emmcinquiry1, emmccmd1, emmciosetup1, emmcio1, },
	{ "sdio2", emmcinit2, emmcenable2, emmcinquiry2, emmccmd2, emmciosetup2, emmcio2, },
};

void
sdhclink(void)
{
	sdcardlink = &sdiohci[0];
	sdwifilink = &sdiohci[1];
}
