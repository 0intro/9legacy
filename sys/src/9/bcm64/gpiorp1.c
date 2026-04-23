/*
 * Raspberry Pi 5 (RP1) GPIO support
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

/*

d0000	bank0 ctl	(pins 0-27)
  0	0 status
  4 0 ctl
  8 1 status
  c 1 ctl
  ...
d4000	bank1 ctl	(pins 28-33)
d8000	bank2 ctl	(pins 34-53)
e0000	bank0 rio
  0 out 1<<pin
  4 out_enable 1<<pin
  8 in	1<<pin
 1000	 xor
 2000	 set
 3000	 clr
e4000	bank1 rio
e8000	bank2 rio
f0000	bank0 pads
  4 0 pad
  8 1 pad
  ...
f4000	bank1 pads
f8000	bank2 pads

Bank1 pin 4
	ctl d4024
	pad f4014
	rio e4000 	1<<4

85 1A 10
*/

typedef struct Ctlregs Ctlregs;
typedef struct Rioregs Rioregs;
typedef struct Padregs Padregs;
typedef struct Gpioregs Gpioregs;

struct Ctlregs {
	struct {
		u32int	status;
		u32int	ctl;
	} reg[28];
};

struct Rioregs {
	u32int	out;
	u32int	out_enable;
	u32int	in;
};

enum {	/* register offsets for atomic actions */
	Xor	= 0x1000/4,
	Set = 0x2000/4,
	Clr	= 0x3000/4,
};


struct Padregs {
	u32int	voltage;
	u32int	pad[28];
};

enum {
	Outdisable	= 1<<7,
	Inenable	= 1<<6,
	Pullup	= 1<<3,
	Pulldown= 1<<2,
	Schmitt	= 1<<1,
};

struct Gpioregs {
	union {
		char	space[0x4000];
		Ctlregs	ctlregs;
	} ctlbank[4];
	union {
		char	space[0x4000];
		Rioregs	rioregs;
	} riobank[4];
	union {
		char	space[0x4000];
		Padregs	padregs;
	} padbank[4];
};

#define GPIOREGS	(VIRTIO+0x800d0000ull)

static uint
pinbank(uint *pin)
{
	uint b;

	if(*pin >= 34){
		b = 2;
		*pin -= 34;
	}else if(*pin >= 28){
		b = 1;
		*pin -= 28;
	}else
		b = 0;
	return b;
}
void
gpioselrp1(uint pin, int func)
{
	Gpioregs *gp;
	uint b;

	gp = (Gpioregs*)GPIOREGS;
	b = pinbank(&pin);
	u32int *p;
	p = &gp->ctlbank[b].ctlregs.reg[pin].ctl;
	if(0)print("\ngpiosel %#p %ux -> %ux\n", p, *p, (*p & ~0x1f) | func);
	*p = (*p & ~0x1f) | func;
}

static void
gpiopullrp1(uint pin, int func)
{
	Gpioregs *gp;
	uint b;
	u32int *p;

	gp = (Gpioregs*)GPIOREGS;
	b = pinbank(&pin);
	p = &gp->padbank[b].padregs.pad[pin];
	if(0)print("gpiopull %#p %ux -> %ux\n", p, *p, (*p & ~(Pullup|Pulldown)) | func);
	*p = (*p & ~(Pullup|Pulldown) | func);
}

void
gpiopulloffrp1(uint pin)
{
	gpiopullrp1(pin, 0);
}

void
gpiopulluprp1(uint pin)
{
	gpiopullrp1(pin, Pullup);
}

void
gpiopulldownrp1(uint pin)
{
	gpiopullrp1(pin, Pulldown);
}

void
gpiooutrp1(uint pin, int set)
{
	Gpioregs *gp;
	uint b;

	gp = (Gpioregs*)GPIOREGS;
	b = pinbank(&pin);
	gp->padbank[b].padregs.pad[pin] &= ~0x80;	/* clear output_disable */
	(&gp->riobank[b].rioregs.out_enable)[Set] = 1<<pin;
	if(set)
		(&gp->riobank[b].rioregs.out)[Set] = 1<<pin;
	else
		(&gp->riobank[b].rioregs.out)[Clr] = 1<<pin;
	if(0)print("gpioout %#p %ux\n", &gp->riobank[b].rioregs.out, gp->riobank[b].rioregs.out);

}

int
gpioinrp1(uint pin)
{
	Gpioregs *gp;
	uint b;

	gp = (Gpioregs*)GPIOREGS;
	b = pinbank(&pin);
	return (gp->riobank[b].rioregs.in & (1<<pin)) != 0;
}
