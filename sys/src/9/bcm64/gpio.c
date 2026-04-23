/*
 * Raspberry Pi (BCM2712) SoC GPIO support
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define GPIOREGS	(VIRTIO+0x7d508500)
#define	PINMUXREGS	(VIRTIO+0x7d504100)

/* PINMUX regs */
enum {
	Fsel0	= 0x00>>2,
		FuncMask= 0xF,
		Off	= 0x0,
		Pulldown= 0x1,
		Pullup	= 0x2,
};
/* GPIO regs */
enum {
	Data	= 0x04>>2,
	Iodir	= 0x08>>2,
};

void
gpiosel(uint pin, int alt)
{	
	u32int *gp, *fsel;
	int func, off;
	static uchar alt2func[8] = {
		0, 0, 5, 4, 0, 1, 2, 3
	};

	if(alt == Input || alt == Output){
		gp = (u32int*)GPIOREGS + 8*(pin/32);
		off = pin%32;
		if(alt == Input)
			gp[Iodir] |= 1<<off;
		else
			gp[Iodir] &= ~(1<<off);
	}
	func = alt2func[alt&0x7];
	gp = (u32int*)PINMUXREGS;
	if(!soc.dstepping){
		fsel = &gp[Fsel0 + pin/8];
		off = (pin % 8) * 4;
	}else{
		fsel = &gp[pin < 32 ? 2 : 3];	/* from circle OS; valid for pins [28..35] */
		off = ((pin-24) % 8) * 4;
	}
	*fsel = (*fsel & ~(FuncMask<<off)) | func<<off;
}

static void
gpiopull(uint pin, int func)
{
	u32int *gp, *reg;
	int shift;

	gp = (u32int*)PINMUXREGS;
	if(!soc.dstepping){
		pin += 112;		/* bcm2712: magic offset of pad */
		reg = &gp[pin/15];
		shift = (pin % 15) * 2;
	}else{
		reg = &gp[pin < 33? 5 : 6];	/* from circle OS; valid for pins [28..35] */
		shift = ((pin-18) % 15) * 2;
	}
	*reg = (func << shift) | (*reg & ~(3<<shift));
}

void
gpiopulloff(uint pin)
{
	gpiopull(pin, Off);
}

void
gpiopullup(uint pin)
{
	gpiopull(pin, Pullup);
}

void
gpiopulldown(uint pin)
{
	gpiopull(pin, Pulldown);
}

void
gpioout(uint pin, int set)
{
	u32int *gp;

	gp = (u32int*)GPIOREGS + 8*(pin/32);
	pin %= 32;
	if(set)
		gp[Data] |= 1<<pin;
	else
		gp[Data] &= ~(1<<pin);
}

int
gpioin(uint pin)
{
	u32int *gp;

	gp = (u32int*)GPIOREGS + 8*(pin/32);
	pin %= 32;
	return (gp[Data] & (1<<pin)) != 0;
}

