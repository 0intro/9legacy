/*
 * Intel 8256[367], 8257[1-9], 8258[03], i21[01], i350
 *	Gigabit Ethernet PCI-Express Controllers
 * Coraid EtherDrive® hba
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

/*
 * note: the 82575, 82576 and 82580 are operated using registers aliased
 * to the 82563-style architecture.  many features seen in the 82598
 * are also seen in the 82575 part.
 */

enum {
	/* General */

	Ctrl		= 0x0000,	/* Device Control */
	Status		= 0x0008,	/* Device Status */
	Eec		= 0x0010,	/* EEPROM/Flash Control/Data */
	Eerd		= 0x0014,	/* EEPROM Read */
	Ctrlext		= 0x0018,	/* Extended Device Control */
	Fla		= 0x001c,	/* Flash Access */
	Mdic		= 0x0020,	/* MDI Control */
	Fcal		= 0x0028,	/* Flow Control Address Low */
	Fcah		= 0x002C,	/* Flow Control Address High */
	Fct		= 0x0030,	/* Flow Control Type */
	Kumctrlsta	= 0x0034,	/* Kumeran Control and Status Register */
	Connsw		= 0x0034,	/* copper / fiber switch control; 82575/82576 */
	Vet		= 0x0038,	/* VLAN EtherType */
	Fcttv		= 0x0170,	/* Flow Control Transmit Timer Value */
	Txcw		= 0x0178,	/* Transmit Configuration Word */
	Rxcw		= 0x0180,	/* Receive Configuration Word */
	Ledctl		= 0x0E00,	/* LED control */
	Pba		= 0x1000,	/* Packet Buffer Allocation */
	Pbs		= 0x1008,	/* Packet Buffer Size */

	/* Interrupt */

	Icr		= 0x00C0,	/* Interrupt Cause Read */
	Itr		= 0x00c4,	/* Interrupt Throttling Rate */
	Ics		= 0x00C8,	/* Interrupt Cause Set */
	Ims		= 0x00D0,	/* Interrupt Mask Set/Read */
	Imc		= 0x00D8,	/* Interrupt mask Clear */
	Iam		= 0x00E0,	/* Interrupt acknowledge Auto Mask */
	Eitr		= 0x1680,	/* Extended itr; 82575/6 80 only */

	/* Receive */

	Rctl		= 0x0100,	/* Control */
	Ert		= 0x2008,	/* Early Receive Threshold (573[EVL], 82578 only) */
	Fcrtl		= 0x2160,	/* Flow Control RX Threshold Low */
	Fcrth		= 0x2168,	/* Flow Control Rx Threshold High */
	Psrctl		= 0x2170,	/* Packet Split Receive Control */
	Drxmxod		= 0x2540,	/* dma max outstanding bytes (82575) */
	Rdbal		= 0x2800,	/* Rdesc Base Address Low Queue 0 */
	Rdbah		= 0x2804,	/* Rdesc Base Address High Queue 0 */
	Rdlen		= 0x2808,	/* Descriptor Length Queue 0 */
	Srrctl		= 0x280c,	/* split and replication rx control (82575) */
	Rdh		= 0x2810,	/* Descriptor Head Queue 0 */
	Rdt		= 0x2818,	/* Descriptor Tail Queue 0 */
	Rdtr		= 0x2820,	/* Descriptor Timer Ring */
	Rxdctl		= 0x2828,	/* Descriptor Control */
	Radv		= 0x282C,	/* Interrupt Absolute Delay Timer */
	Rsrpd		= 0x2c00,	/* Small Packet Detect */
	Raid		= 0x2c08,	/* ACK interrupt delay */
	Cpuvec		= 0x2c10,	/* CPU Vector */
	Rxcsum		= 0x5000,	/* Checksum Control */
	Rmpl		= 0x5004,	/* rx maximum packet length (82575) */
	Rfctl		= 0x5008,	/* Filter Control */
	Mta		= 0x5200,	/* Multicast Table Array */
	Ral		= 0x5400,	/* Receive Address Low */
	Rah		= 0x5404,	/* Receive Address High */
	Vfta		= 0x5600,	/* VLAN Filter Table Array */
	Mrqc		= 0x5818,	/* Multiple Receive Queues Command */

	/* Transmit */

	Tctl		= 0x0400,	/* Transmit Control */
	Tipg		= 0x0410,	/* Transmit IPG */
	Tkabgtxd	= 0x3004,	/* glci afe band gap transmit ref data, or something */
	Tdbal		= 0x3800,	/* Tdesc Base Address Low */
	Tdbah		= 0x3804,	/* Tdesc Base Address High */
	Tdlen		= 0x3808,	/* Descriptor Length */
	Tdh		= 0x3810,	/* Descriptor Head */
	Tdt		= 0x3818,	/* Descriptor Tail */
	Tidv		= 0x3820,	/* Interrupt Delay Value */
	Txdctl		= 0x3828,	/* Descriptor Control */
	Tadv		= 0x382C,	/* Interrupt Absolute Delay Timer */
	Tarc0		= 0x3840,	/* Arbitration Counter Queue 0 */

	/* Statistics */

	Statistics	= 0x4000,	/* Start of Statistics Area */
	Gorcl		= 0x88/4,	/* Good Octets Received Count */
	Gotcl		= 0x90/4,	/* Good Octets Transmitted Count */
	Torl		= 0xC0/4,	/* Total Octets Received */
	Totl		= 0xC8/4,	/* Total Octets Transmitted */
	Nstatistics	= 0x124/4,
};

enum {					/* Ctrl */
	Lrst		= 1<<3,		/* link reset */
	Slu		= 1<<6,		/* Set Link Up */
	Devrst		= 1<<26,	/* Device Reset */
	Rfce		= 1<<27,	/* Receive Flow Control Enable */
	Tfce		= 1<<28,	/* Transmit Flow Control Enable */
	Phyrst		= 1<<31,	/* Phy Reset */
};

enum {					/* Status */
	Lu		= 1<<1,		/* Link Up */
	Lanid		= 3<<2,		/* mask for Lan ID. */
	Txoff		= 1<<4,		/* Transmission Paused */
	Tbimode		= 1<<5,		/* TBI Mode Indication */
	Phyra		= 1<<10,	/* PHY Reset Asserted */
	GIOme		= 1<<19,	/* GIO Master Enable Status */
};

enum {
	/* Eec */
	Nvpres		= 1<<8,		/* nvram present */
	Autord		= 1<<9,		/* autoread complete */
	Sec1val		= 1<<22,		/* sector 1 valid (!sec0) */
};

enum {					/* Eerd */
	EEstart		= 1<<0,		/* Start Read */
	EEdone		= 1<<1,		/* Read done */
};

enum {					/* Ctrlext */
	Eerst		= 1<<13,	/* EEPROM Reset */
	Linkmode	= 3<<22,	/* linkmode */
	Internalphy	= 0<<22,	/* " internal phy (copper) */
	Sgmii		= 2<<22,	/* " sgmii */
	Serdes		= 3<<22,	/* " serdes */
};

enum {
	/* Connsw */
	Enrgirq		= 1<<2,	/* interrupt on power detect (enrgsrc) */
};

enum {					/* EEPROM content offsets */
	Ea		= 0x00,		/* Ethernet Address */
};

enum {					/* Mdic */
	MDIdMASK	= 0x0000FFFF,	/* Data */
	MDIdSHIFT	= 0,
	MDIrMASK	= 0x001F0000,	/* PHY Register Address */
	MDIrSHIFT	= 16,
	MDIpMASK	= 0x03E00000,	/* PHY Address */
	MDIpSHIFT	= 21,
	MDIwop		= 0x04000000,	/* Write Operation */
	MDIrop		= 0x08000000,	/* Read Operation */
	MDIready	= 0x10000000,	/* End of Transaction */
	MDIie		= 0x20000000,	/* Interrupt Enable */
	MDIe		= 0x40000000,	/* Error */
};

enum {					/* phy interface */
	Phyctl		= 0,		/* phy ctl register */
	Phyisr		= 19,		/* 82563 phy interrupt status register */
	Phylhr		= 19,		/* 8257[12] link health register */
	Physsr		= 17,		/* phy secondary status register */
	Phyprst		= 193<<8 | 17,	/* 8256[34] phy port reset */
	Phyier		= 18,		/* 82573 phy interrupt enable register */
	Phypage		= 22,		/* 8256[34] page register */
	Phystat		= 26,		/* 82580 phy status */
	Phyapage	= 29,
	Phy79page	= 31,		/* 82579 phy page register (all pages) */

	Rtlink		= 1<<10,	/* realtime link status */
	Phyan		= 1<<11,	/* phy has autonegotiated */

	/* Phyctl bits */
	Ran		= 1<<9,	/* restart auto negotiation */
	Ean		= 1<<12,	/* enable auto negotiation */

	/* Phyprst bits */
	Prst		= 1<<0,	/* reset the port */

	/* 82573 Phyier bits */
	Lscie		= 1<<10,	/* link status changed ie */
	Ancie		= 1<<11,	/* auto negotiation complete ie */
	Spdie		= 1<<14,	/* speed changed ie */
	Panie		= 1<<15,	/* phy auto negotiation error ie */

	/* Phylhr/Phyisr bits */
	Anf		= 1<<6,	/* lhr: auto negotiation fault */
	Ane		= 1<<15,	/* isr: auto negotiation error */

	/* 82580 Phystat bits */
	Ans	= 1<<14 | 1<<15,	/* 82580 autoneg. status */
	Link	= 1<<6,		/* 82580 Link */

	/* Rxcw builtin serdes */
	Anc		= 1<<31,
	Rxsynch		= 1<<30,
	Rxcfg		= 1<<29,
	Rxcfgch		= 1<<28,
	Rxcfgbad	= 1<<27,
	Rxnc		= 1<<26,

	/* Txcw */
	Txane		= 1<<31,
	Txcfg		= 1<<30,
};

enum {					/* fiber (pcs) interface */
	Pcsctl	= 0x4208,		/* pcs control */
	Pcsstat	= 0x420c,		/* pcs status */

	/* Pcsctl bits */
	Pan	= 1<<16,		/* autonegotiate */
	Prestart	= 1<<17,		/* restart an (self clearing) */

	/* Pcsstat bits */
	Linkok	= 1<<0,		/* link is okay */
	Andone	= 1<<16,		/* an phase is done see below for success */
	Anbad	= 1<<19 | 1<<20,	/* Anerror | Anremfault */
};

enum {					/* Icr, Ics, Ims, Imc */
	Txdw		= 0x00000001,	/* Transmit Descriptor Written Back */
	Txqe		= 0x00000002,	/* Transmit Queue Empty */
	Lsc		= 0x00000004,	/* Link Status Change */
	Rxseq		= 0x00000008,	/* Receive Sequence Error */
	Rxdmt0		= 0x00000010,	/* Rdesc Minimum Threshold Reached */
	Rxo		= 0x00000040,	/* Receiver Overrun */
	Rxt0		= 0x00000080,	/* Receiver Timer Interrupt; !82575/6/80 only */
	Rxdw		= 0x00000080,	/* Rdesc write back; 82575/6/80 only */
	Mdac		= 0x00000200,	/* MDIO Access Completed */
	Rxcfgset		= 0x00000400,	/* Receiving /C/ ordered sets */
	Ack		= 0x00020000,	/* Receive ACK frame */
	Omed		= 1<<20,	/* media change; pcs interface */
};

enum {					/* Txcw */
	TxcwFd		= 0x00000020,	/* Full Duplex */
	TxcwHd		= 0x00000040,	/* Half Duplex */
	TxcwPauseMASK	= 0x00000180,	/* Pause */
	TxcwPauseSHIFT	= 7,
	TxcwPs		= 1<<TxcwPauseSHIFT,	/* Pause Supported */
	TxcwAs		= 2<<TxcwPauseSHIFT,	/* Asymmetric FC desired */
	TxcwRfiMASK	= 0x00003000,	/* Remote Fault Indication */
	TxcwRfiSHIFT	= 12,
	TxcwNpr		= 0x00008000,	/* Next Page Request */
	TxcwConfig	= 0x40000000,	/* Transmit COnfig Control */
	TxcwAne		= 0x80000000,	/* Auto-Negotiation Enable */
};

enum {					/* Rctl */
	Rrst		= 0x00000001,	/* Receiver Software Reset */
	Ren		= 0x00000002,	/* Receiver Enable */
	Sbp		= 0x00000004,	/* Store Bad Packets */
	Upe		= 0x00000008,	/* Unicast Promiscuous Enable */
	Mpe		= 0x00000010,	/* Multicast Promiscuous Enable */
	Lpe		= 0x00000020,	/* Long Packet Reception Enable */
	RdtmsMASK	= 0x00000300,	/* Rdesc Minimum Threshold Size */
	RdtmsHALF	= 0x00000000,	/* Threshold is 1/2 Rdlen */
	RdtmsQUARTER	= 0x00000100,	/* Threshold is 1/4 Rdlen */
	RdtmsEIGHTH	= 0x00000200,	/* Threshold is 1/8 Rdlen */
	MoMASK		= 0x00003000,	/* Multicast Offset */
	Bam		= 0x00008000,	/* Broadcast Accept Mode */
	BsizeMASK	= 0x00030000,	/* Receive Buffer Size */
	Bsize16384	= 0x00010000,	/* Bsex = 1 */
	Bsize8192	= 0x00020000, 	/* Bsex = 1 */
	Bsize2048	= 0x00000000,
	Bsize1024	= 0x00010000,
	Bsize512	= 0x00020000,
	Bsize256	= 0x00030000,
	BsizeFlex	= 0x08000000,	/* Flexable Bsize in 1kb increments */
	Vfe		= 0x00040000,	/* VLAN Filter Enable */
	Cfien		= 0x00080000,	/* Canonical Form Indicator Enable */
	Cfi		= 0x00100000,	/* Canonical Form Indicator value */
	Dpf		= 0x00400000,	/* Discard Pause Frames */
	Pmcf		= 0x00800000,	/* Pass MAC Control Frames */
	Bsex		= 0x02000000,	/* Buffer Size Extension */
	Secrc		= 0x04000000,	/* Strip CRC from incoming packet */
};

enum {					/* Srrctl */
	Dropen		= 1<<31,
};

enum {					/* Tctl */
	Trst		= 0x00000001,	/* Transmitter Software Reset */
	Ten		= 0x00000002,	/* Transmit Enable */
	Psp		= 0x00000008,	/* Pad Short Packets */
	Mulr		= 0x10000000,	/* Allow multiple concurrent requests */
	CtMASK		= 0x00000FF0,	/* Collision Threshold */
	CtSHIFT		= 4,
	ColdMASK	= 0x003FF000,	/* Collision Distance */
	ColdSHIFT	= 12,
	Swxoff		= 0x00400000,	/* Sofware XOFF Transmission */
	Pbe		= 0x00800000,	/* Packet Burst Enable */
	Rtlc		= 0x01000000,	/* Re-transmit on Late Collision */
	Nrtu		= 0x02000000,	/* No Re-transmit on Underrrun */
};

enum {					/* [RT]xdctl */
	PthreshMASK	= 0x0000003F,	/* Prefetch Threshold */
	PthreshSHIFT	= 0,
	HthreshMASK	= 0x00003F00,	/* Host Threshold */
	HthreshSHIFT	= 8,
	WthreshMASK	= 0x003F0000,	/* Writeback Threshold */
	WthreshSHIFT	= 16,
	Gran		= 0x01000000,	/* Granularity; not 82575 */
	Enable		= 0x02000000,
};

enum {					/* Rxcsum */
	Ipofl		= 0x0100,	/* IP Checksum Off-load Enable */
	Tuofl		= 0x0200,	/* TCP/UDP Checksum Off-load Enable */
};

typedef struct Rd {			/* Receive Descriptor */
	u32int	addr[2];
	u16int	length;
	u16int	checksum;
	uchar	status;
	uchar	errors;
	u16int	special;
} Rd;

enum {					/* Rd status */
	Rdd		= 0x01,		/* Descriptor Done */
	Reop		= 0x02,		/* End of Packet */
	Ixsm		= 0x04,		/* Ignore Checksum Indication */
	Vp		= 0x08,		/* Packet is 802.1Q (matched VET) */
	Tcpcs		= 0x20,		/* TCP Checksum Calculated on Packet */
	Ipcs		= 0x40,		/* IP Checksum Calculated on Packet */
	Pif		= 0x80,		/* Passed in-exact filter */
};

enum {					/* Rd errors */
	Ce		= 0x01,		/* CRC Error or Alignment Error */
	Se		= 0x02,		/* Symbol Error */
	Seq		= 0x04,		/* Sequence Error */
	Cxe		= 0x10,		/* Carrier Extension Error */
	Tcpe		= 0x20,		/* TCP/UDP Checksum Error */
	Ipe		= 0x40,		/* IP Checksum Error */
	Rxe		= 0x80,		/* RX Data Error */
};

typedef struct {			/* Transmit Descriptor */
	u32int	addr[2];		/* Data */
	u32int	control;
	u32int	status;
} Td;

enum {					/* Tdesc control */
	LenMASK		= 0x000FFFFF,	/* Data/Packet Length Field */
	LenSHIFT	= 0,
	DtypeCD		= 0x00000000,	/* Data Type 'Context Descriptor' */
	DtypeDD		= 0x00100000,	/* Data Type 'Data Descriptor' */
	PtypeTCP	= 0x01000000,	/* TCP/UDP Packet Type (CD) */
	Teop		= 0x01000000,	/* End of Packet (DD) */
	PtypeIP		= 0x02000000,	/* IP Packet Type (CD) */
	Ifcs		= 0x02000000,	/* Insert FCS (DD) */
	Tse		= 0x04000000,	/* TCP Segmentation Enable */
	Rs		= 0x08000000,	/* Report Status */
	Rps		= 0x10000000,	/* Report Status Sent */
	Dext		= 0x20000000,	/* Descriptor Extension */
	Vle		= 0x40000000,	/* VLAN Packet Enable */
	Ide		= 0x80000000,	/* Interrupt Delay Enable */
};

enum {					/* Tdesc status */
	Tdd		= 0x0001,	/* Descriptor Done */
	Ec		= 0x0002,	/* Excess Collisions */
	Lc		= 0x0004,	/* Late Collision */
	Tu		= 0x0008,	/* Transmit Underrun */
	CssMASK		= 0xFF00,	/* Checksum Start Field */
	CssSHIFT	= 8,
};

typedef struct {
	u16int	*reg;
	u32int	*reg32;
	uint	base;
	uint	lim;
} Flash;

enum {
	/* 16 and 32-bit flash registers for ich flash parts */
	Bfpr	= 0x00/4,		/* flash base 0:12; lim 16:28 */
	Fsts	= 0x04/2,		/* flash status; Hsfsts */
	Fctl	= 0x06/2,		/* flash control; Hsfctl */
	Faddr	= 0x08/4,		/* flash address to r/w */
	Fdata	= 0x10/4,		/* data @ address */

	/* status register */
	Fdone	= 1<<0,			/* flash cycle done */
	Fcerr	= 1<<1,			/* cycle error; write 1 to clear */
	Ael	= 1<<2,			/* direct access error log; 1 to clear */
	Scip	= 1<<5,			/* spi cycle in progress */
	Fvalid	= 1<<14,		/* flash descriptor valid */

	/* control register */
	Fgo	= 1<<0,			/* start cycle */
	Flcycle	= 1<<1,			/* two bits: r=0; w=2 */
	Fdbc	= 1<<8,			/* bytes to read; 5 bits */
};

enum {
	Nrd		= 256,		/* power of two */
	Ntd		= 256,		/* power of two */
	Nrb		= 3*512,		/* private receive buffers per Ctlr */
	Rbalign		= 16,		/* rx buffer alignment */
	Npool		= 10,
};

enum {
	i82563,
	i82566,
	i82567,
	i82567m,
	i82571,
	i82572,
	i82573,
	i82574,
	i82575,
	i82576,
	i82577,
	i82577m,	
	i82578,
	i82578m,
	i82579,
	i82580,
	i82583,
	i210,
	i217,
	i350,
	Nctlrtype,
};

enum {
	Fload	= 1<<0,
	Fert	= 1<<1,
	F75	= 1<<2,
	Fpba	= 1<<3,
	Fflashea	= 1<<4,
	F79phy	= 1<<5,
	Fnofct	= 1<<6,
};

typedef struct Ctlrtype Ctlrtype;
struct Ctlrtype {
	int	type;
	int	mtu;
	int	phyno;
	char	*name;
	int	flag;
};

static Ctlrtype cttab[Nctlrtype] = {
	i82563,		9014,	1,	"i82563",		Fpba,
	i82566,		1514,	1,	"i82566",		Fload,
	i82567,		9234,	1,	"i82567",		Fload,
	i82567m,		1514,	1,	"i82567m",	0,
	i82571,		9234,	1,	"i82571",		Fpba,
	i82572,		9234,	1,	"i82572",		Fpba,
	i82573,		8192,	1,	"i82573",		Fert,		/* terrible perf above 8k */
	i82574,		9018,	1,	"i82574",		0,
	i82575,		9728,	1,	"i82575",		F75|Fflashea,
	i82576,		9728,	1,	"i82576",		F75,
	i82577,		4096,	2,	"i82577",		Fload|Fert,
	i82577m,		1514,	2,	"i82577",		Fload|Fert,
	i82578,		4096,	2,	"i82578",		Fload|Fert,
	i82578m,		1514,	2,	"i82578",		Fload|Fert,
	i82579,		9018,	2,	"i82579",		Fload|Fert|F79phy|Fnofct,
	i82580,		9728,	1,	"i82580",		F75|F79phy,
	i82583,		1514,	1,	"i82583",		0,
	i210,		9728,	1,	"i210",		F75|F79phy|Fnofct|Fert,
	i217,		9728,	1,	"i217",		F79phy|Fnofct|Fload|Fert,
	i350,		9728,	1,	"i350",		F75|F79phy|Fnofct,
};

typedef void (*Freefn)(Block*);

typedef struct Ctlr Ctlr;
struct Ctlr {
	uintmem	port;
	Pcidev	*pcidev;
	Ctlr	*next;
	int	active;
	int	type;
	int	pool;
	u16int	eeprom[0x40];

	QLock	alock;			/* attach */
	void	*alloc;			/* receive/transmit descriptors */
	int	nrd;
	int	ntd;
	uint	rbsz;

	u32int	*nic;
	Lock	imlock;
	int	im;			/* interrupt mask */

	Rendez	lrendez;
	int	lim;

	QLock	slock;
	u32int	statistics[Nstatistics];
	uint	lsleep;
	uint	lintr;
	uint	rsleep;
	uint	rintr;
	uint	txdw;
	uint	tintr;
	uint	ixsm;
	uint	ipcs;
	uint	tcpcs;
	uint	speeds[4];
	uint	phyerrata;

	uchar	ra[Eaddrlen];		/* receive address */
	u32int	mta[128];		/* multicast table array */

	Rendez	rrendez;
	int	rim;
	int	rdfree;
	Rd	*rdba;			/* receive descriptor base address */
	Block	**rb;			/* receive buffers */
	uint	rdh;			/* receive descriptor head */
	uint	rdt;			/* receive descriptor tail */
	int	rdtr;			/* receive delay timer ring value */
	int	radv;			/* receive interrupt absolute delay timer */

	Rendez	trendez;
	QLock	tlock;
	int	tbusy;
	Td	*tdba;			/* transmit descriptor base address */
	Block	**tb;			/* transmit buffers */
	int	tdh;			/* transmit descriptor head */
	int	tdt;			/* transmit descriptor tail */

	int	fcrtl;
	int	fcrth;

	u32int	pba;			/* packet buffer allocation */
};

typedef struct Rbpool Rbpool;
struct Rbpool {
	union {
		struct {
			Lock;
			Block	*b;
			uint	nstarve;
			uint	nwakey;
			uint	starve;
			Rendez;
		};
		uchar pad[64];		/* cacheline */
	};

	Block	*x;
	uint	nfast;
	uint	nslow;
};

#define csr32r(c, r)	(*((c)->nic+((r)/4)))
#define csr32w(c, r, v)	(*((c)->nic+((r)/4)) = (v))

static	Ctlr	*i82563ctlr;
static	Rbpool	rbtab[Npool];

static char *statistics[Nstatistics] = {
	"CRC Error",
	"Alignment Error",
	"Symbol Error",
	"RX Error",
	"Missed Packets",
	"Single Collision",
	"Excessive Collisions",
	"Multiple Collision",
	"Late Collisions",
	nil,
	"Collision",
	"Transmit Underrun",
	"Defer",
	"Transmit - No CRS",
	"Sequence Error",
	"Carrier Extension Error",
	"Receive Error Length",
	nil,
	"XON Received",
	"XON Transmitted",
	"XOFF Received",
	"XOFF Transmitted",
	"FC Received Unsupported",
	"Packets Received (64 Bytes)",
	"Packets Received (65-127 Bytes)",
	"Packets Received (128-255 Bytes)",
	"Packets Received (256-511 Bytes)",
	"Packets Received (512-1023 Bytes)",
	"Packets Received (1024-mtu Bytes)",
	"Good Packets Received",
	"Broadcast Packets Received",
	"Multicast Packets Received",
	"Good Packets Transmitted",
	nil,
	"Good Octets Received",
	nil,
	"Good Octets Transmitted",
	nil,
	nil,
	nil,
	"Receive No Buffers",
	"Receive Undersize",
	"Receive Fragment",
	"Receive Oversize",
	"Receive Jabber",
	"Management Packets Rx",
	"Management Packets Drop",
	"Management Packets Tx",
	"Total Octets Received",
	nil,
	"Total Octets Transmitted",
	nil,
	"Total Packets Received",
	"Total Packets Transmitted",
	"Packets Transmitted (64 Bytes)",
	"Packets Transmitted (65-127 Bytes)",
	"Packets Transmitted (128-255 Bytes)",
	"Packets Transmitted (256-511 Bytes)",
	"Packets Transmitted (512-1023 Bytes)",
	"Packets Transmitted (1024-mtu Bytes)",
	"Multicast Packets Transmitted",
	"Broadcast Packets Transmitted",
	"TCP Segmentation Context Transmitted",
	"TCP Segmentation Context Fail",
	"Interrupt Assertion",
	"Interrupt Rx Pkt Timer",
	"Interrupt Rx Abs Timer",
	"Interrupt Tx Pkt Timer",
	"Interrupt Tx Abs Timer",
	"Interrupt Tx Queue Empty",
	"Interrupt Tx Desc Low",
	"Interrupt Rx Min",
	"Interrupt Rx Overrun",
};

static char*
cname(Ctlr *c)
{
	return cttab[c->type].name;
}

static long
i82563ifstat(Ether *edev, void *a, long n, usize offset)
{
	char *s, *p, *e, *stat;
	int i, r;
	uvlong tuvl, ruvl;
	Ctlr *ctlr;
	Rbpool *b;

	ctlr = edev->ctlr;
	qlock(&ctlr->slock);
	p = s = malloc(READSTR);
	e = p + READSTR;

	for(i = 0; i < Nstatistics; i++){
		r = csr32r(ctlr, Statistics + i*4);
		if((stat = statistics[i]) == nil)
			continue;
		switch(i){
		case Gorcl:
		case Gotcl:
		case Torl:
		case Totl:
			ruvl = r;
			ruvl += (uvlong)csr32r(ctlr, Statistics+(i+1)*4) << 32;
			tuvl = ruvl;
			tuvl += ctlr->statistics[i];
			tuvl += (uvlong)ctlr->statistics[i+1] << 32;
			if(tuvl == 0)
				continue;
			ctlr->statistics[i] = tuvl;
			ctlr->statistics[i+1] = tuvl >> 32;
			p = seprint(p, e, "%s: %llud %llud\n", stat, tuvl, ruvl);
			i++;
			break;

		default:
			ctlr->statistics[i] += r;
			if(ctlr->statistics[i] == 0)
				continue;
			p = seprint(p, e, "%s: %ud %ud\n", stat,
				ctlr->statistics[i], r);
			break;
		}
	}

	p = seprint(p, e, "lintr: %ud %ud\n", ctlr->lintr, ctlr->lsleep);
	p = seprint(p, e, "rintr: %ud %ud\n", ctlr->rintr, ctlr->rsleep);
	p = seprint(p, e, "tintr: %ud %ud\n", ctlr->tintr, ctlr->txdw);
	p = seprint(p, e, "ixcs: %ud %ud %ud\n", ctlr->ixsm, ctlr->ipcs, ctlr->tcpcs);
	p = seprint(p, e, "rdtr: %ud\n", ctlr->rdtr);
	p = seprint(p, e, "radv: %ud\n", ctlr->radv);
	p = seprint(p, e, "ctrl: %.8ux\n", csr32r(ctlr, Ctrl));
	p = seprint(p, e, "ctrlext: %.8ux\n", csr32r(ctlr, Ctrlext));
	p = seprint(p, e, "status: %.8ux\n", csr32r(ctlr, Status));
	p = seprint(p, e, "txcw: %.8ux\n", csr32r(ctlr, Txcw));
	p = seprint(p, e, "txdctl: %.8ux\n", csr32r(ctlr, Txdctl));
	p = seprint(p, e, "pba: %.8ux\n", ctlr->pba);

	b = rbtab + ctlr->pool;
	p = seprint(p, e, "pool: fast %ud slow %ud nstarve %ud nwakey %ud starve %ud\n",
		b->nfast, b->nslow, b->nstarve, b->nwakey, b->starve);
	p = seprint(p, e, "speeds: 10:%ud 100:%ud 1000:%ud ?:%ud\n",
		ctlr->speeds[0], ctlr->speeds[1], ctlr->speeds[2], ctlr->speeds[3]);
	p = seprint(p, e, "type: %s\n", cname(ctlr));

	USED(p);
	n = readstr(offset, a, n, s);
	free(s);
	qunlock(&ctlr->slock);

	return n;
}

static void
i82563promiscuous(void *arg, int on)
{
	int rctl;
	Ctlr *ctlr;
	Ether *edev;

	edev = arg;
	ctlr = edev->ctlr;

	rctl = csr32r(ctlr, Rctl);
	rctl &= ~MoMASK;
	if(on)
		rctl |= Upe|Mpe;
	else
		rctl &= ~(Upe|Mpe);
	csr32w(ctlr, Rctl, rctl);
}

static void
i82563multicast(void *arg, uchar *addr, int on)
{
	int bit, x;
	Ctlr *ctlr;
	Ether *edev;

	edev = arg;
	ctlr = edev->ctlr;

	x = addr[5]>>1;
	if(ctlr->type == i82566)
		x &= 31;
	if(ctlr->type == i210 || ctlr->type == i217)
		x &= 15;
	bit = ((addr[5] & 1)<<4)|(addr[4]>>4);
	/*
	 * multiple ether addresses can hash to the same filter bit,
	 * so it's never safe to clear a filter bit.
	 * if we want to clear filter bits, we need to keep track of
	 * all the multicast addresses in use, clear all the filter bits,
	 * then set the ones corresponding to in-use addresses.
	 */
	if(on)
		ctlr->mta[x] |= 1<<bit;
//	else
//		ctlr->mta[x] &= ~(1<<bit);

	csr32w(ctlr, Mta+x*4, ctlr->mta[x]);
}

static int
icansleep(void *v)
{
	Rbpool *p;
	int r;

	p = v;
	ilock(p);
	r = p->starve == 0;
	iunlock(p);

	return r;
}

static Block*
i82563rballoc(Rbpool *p)
{
	Block *b;

	for(;;){
		if((b = p->x) != nil){
			p->nfast++;
			p->x = b->next;
			b->next = nil;
			return b;
		}

		ilock(p);
		b = p->b;
		p->b = nil;
		if(b == nil){
			p->nstarve++;
			iunlock(p);
			return nil;
		}
		p->nslow++;
		iunlock(p);
		p->x = b;
	}
}

static void
rbfree(Block *b, int t)
{
	Rbpool *p;

	p = rbtab + t;
	b->rp = b->wp = (uchar*)ROUNDUP((uintptr)b->base, Rbalign);
	b->flag &= ~(Bipck | Budpck | Btcpck | Bpktck);

	ilock(p);
	b->next = p->b;
	p->b = b;
	if(p->starve){
		if(0)
			iprint("wakey %d; %d %d\n", t, p->nstarve, p->nwakey);
		p->nwakey++;
		p->starve = 0;
		iunlock(p);
		wakeup(p);
	}else
		iunlock(p);
}

static void
rbfree0(Block *b)
{
	rbfree(b, 0);
}

static void
rbfree1(Block *b)
{
	rbfree(b, 1);
}

static void
rbfree2(Block *b)
{
	rbfree(b, 2);
}

static void
rbfree3(Block *b)
{
	rbfree(b, 3);
}

static void
rbfree4(Block *b)
{
	rbfree(b, 4);
}

static void
rbfree5(Block *b)
{
	rbfree(b, 5);
}

static void
rbfree6(Block *b)
{
	rbfree(b, 6);
}

static void
rbfree7(Block *b)
{
	rbfree(b, 7);
}

static void
rbfree8(Block *b)
{
	rbfree(b, 8);
}

static void
rbfree9(Block *b)
{
	rbfree(b, 9);
}

static Freefn freetab[Npool] = {
	rbfree0,
	rbfree1,
	rbfree2,
	rbfree3,
	rbfree4,
	rbfree5,
	rbfree6,
	rbfree7,
	rbfree8,
	rbfree9,
};

static int
newpool(void)
{
	static int seq;

	if(seq == nelem(freetab))
		return -1;
	if(freetab[seq] == nil){
		print("82563: bad freetab\n");
		return -1;
	}
	return seq++;
}

static void
i82563im(Ctlr *ctlr, int im)
{
	ilock(&ctlr->imlock);
	ctlr->im |= im;
	csr32w(ctlr, Ims, ctlr->im);
	iunlock(&ctlr->imlock);
}

static void
i82563txinit(Ctlr *ctlr)
{
	int i;
	u32int r;
	Block *b;

	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Tctl, 0x0F<<CtSHIFT | Psp);
	else
		csr32w(ctlr, Tctl, 0x0F<<CtSHIFT | Psp | 66<<ColdSHIFT | Mulr);
	csr32w(ctlr, Tipg, 6<<20 | 8<<10 | 8);		/* yb sez: 0x702008 */
	csr32w(ctlr, Tdbal, PCIWADDRL(ctlr->tdba));
	csr32w(ctlr, Tdbah, PCIWADDRH(ctlr->tdba));
	csr32w(ctlr, Tdlen, ctlr->ntd * sizeof(Td));
	ctlr->tdh = PREV(0, ctlr->ntd);
	csr32w(ctlr, Tdh, 0);
	ctlr->tdt = 0;
	csr32w(ctlr, Tdt, 0);
	for(i = 0; i < ctlr->ntd; i++){
		if((b = ctlr->tb[i]) != nil){
			ctlr->tb[i] = nil;
			freeb(b);
		}
		memset(&ctlr->tdba[i], 0, sizeof(Td));
	}
	csr32w(ctlr, Tidv, 128);
	csr32w(ctlr, Tadv, 64);
	csr32w(ctlr, Tctl, csr32r(ctlr, Tctl) | Ten);
	r = csr32r(ctlr, Txdctl) & ~WthreshMASK;
	r |= 4<<WthreshSHIFT | 4<<PthreshSHIFT;
	if(cttab[ctlr->type].flag & F75)
		r |= Enable;
	csr32w(ctlr, Txdctl, r);
}

#define Next(x, m)	(((x)+1) & (m))

static int
i82563cleanup(Ether *e)
{
	Block *b;
	Ctlr *c;
	int tdh, m, n;

	c = e->ctlr;
	tdh = c->tdh;
	m = c->ntd-1;
	while(c->tdba[n = Next(tdh, m)].status & Tdd){
		tdh = n;
		if((b = c->tb[tdh]) != nil){
			c->tb[tdh] = nil;
			freeb(b);
		}else
			iprint("#l%d: %s tx underrun! %d\n", e->ctlrno, cname(c), n);
		c->tdba[tdh].status = 0;
	}

	return c->tdh = tdh;
}

static int
notrim(void *v)
{
	Ctlr *c;

	c = v;
	return (c->im & Txdw) == 0;
}

static void
i82563tproc(void *v)
{
	Td *td;
	Block *bp;
	Ether *edev;
	Ctlr *ctlr;
	int tdh, tdt, m;

	edev = v;
	ctlr = edev->ctlr;
	tdt = ctlr->tdt;
	m = ctlr->ntd-1;

	i82563txinit(ctlr);

	for(;;){
		tdh = i82563cleanup(edev);

		if(Next(tdt, m) == tdh){
			ctlr->txdw++;
			i82563im(ctlr, Txdw);
			sleep(&ctlr->trendez, notrim, ctlr);
			continue;
		}
		bp = qbread(edev->oq, 100000);
		td = &ctlr->tdba[tdt];
		td->addr[0] = PCIWADDRL(bp->rp);
		td->addr[1] = PCIWADDRH(bp->rp);
		td->control = Ide|Rs|Ifcs|Teop|BLEN(bp);
		ctlr->tb[tdt] = bp;
		tdt = Next(tdt, m);
		sfence();
		csr32w(ctlr, Tdt, tdt);
	}
}

static int
i82563replenish(Ctlr *ctlr, int maysleep)
{
	uint rdt, m, i;
	Block *bp;
	Rbpool *p;
	Rd *rd;

	rdt = ctlr->rdt;
	m = ctlr->nrd-1;
	p = rbtab + ctlr->pool;
	i = 0;
	for(; Next(rdt, m) != ctlr->rdh; rdt = Next(rdt, m)){
		rd = &ctlr->rdba[rdt];
		if(ctlr->rb[rdt] != nil){
			iprint("%s: tx overrun\n", cname(ctlr));
			break;
		}
	redux:
		bp = i82563rballoc(p);
		if(bp == nil){
			if(rdt - ctlr->rdh >= 16)
				break;
			print("%s: pool %d: no rx buffers\n", cname(ctlr), ctlr->pool);
			if(maysleep == 0)
				return -1;
			ilock(p);
			p->starve = 1;
			iunlock(p);
			sleep(p, icansleep, p);
			goto redux;
		}
		i++;
		ctlr->rb[rdt] = bp;
		rd->addr[0] = PCIWADDRL(bp->rp);
		rd->addr[1] = PCIWADDRH(bp->rp);
		rd->status = 0;
		ctlr->rdfree++;
	}
	if(i != 0){
		ctlr->rdt = rdt;
		sfence();
		csr32w(ctlr, Rdt, rdt);
	}
	return 0;
}

static void
i82563rxinit(Ctlr *ctlr)
{
	int i;
	Block *bp;

	if(ctlr->rbsz <= 2048)
		csr32w(ctlr, Rctl, Dpf|Bsize2048|Bam|RdtmsHALF);
	else{
		i = ctlr->rbsz / 1024;
		if(ctlr->rbsz % 1024)
			i++;
		if(cttab[ctlr->type].flag & F75){
			csr32w(ctlr, Rctl, Lpe|Dpf|Bsize2048|Bam|RdtmsHALF|Secrc);
			if(ctlr->type != i82575)
				i |= (ctlr->nrd/2>>4)<<20;		/* RdmsHalf */
			csr32w(ctlr, Srrctl, i | Dropen);
			csr32w(ctlr, Rmpl, ctlr->rbsz);
//			csr32w(ctlr, Drxmxod, 0x7ff);
		}else
			csr32w(ctlr, Rctl, Lpe|Dpf|BsizeFlex*i|Bam|RdtmsHALF|Secrc);
	}

	if(cttab[ctlr->type].flag & Fert)
		csr32w(ctlr, Ert, 1024/8);

	if(ctlr->type == i82566)
		csr32w(ctlr, Pbs, 16);

	csr32w(ctlr, Rdbal, PCIWADDRL(ctlr->rdba));
	csr32w(ctlr, Rdbah, PCIWADDRH(ctlr->rdba));
	csr32w(ctlr, Rdlen, ctlr->nrd * sizeof(Rd));
	ctlr->rdh = 0;
	csr32w(ctlr, Rdh, 0);
	ctlr->rdt = 0;
	csr32w(ctlr, Rdt, 0);
	ctlr->rdtr = 0; //25;
	ctlr->radv = 0; //500;
	csr32w(ctlr, Rdtr, ctlr->rdtr);
	csr32w(ctlr, Radv, ctlr->radv);

	for(i = 0; i < ctlr->nrd; i++)
		if((bp = ctlr->rb[i]) != nil){
			ctlr->rb[i] = nil;
			freeb(bp);
		}
	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Rxdctl, 1<<WthreshSHIFT | 8<<PthreshSHIFT | 1<<HthreshSHIFT | Enable);
	else
		csr32w(ctlr, Rxdctl, 2<<WthreshSHIFT | 2<<PthreshSHIFT);

	/*
	 * Enable checksum offload.
	 */
	csr32w(ctlr, Rxcsum, Tuofl | Ipofl | ETHERHDRSIZE);
}

static int
i82563rim(void *v)
{
	return ((Ctlr*)v)->rim != 0;
}

static void
i82563rproc(void *arg)
{
	uint m, rdh, rim, im;
	Block *bp;
	Ctlr *ctlr;
	Ether *edev;
	Rd *rd;

	edev = arg;
	ctlr = edev->ctlr;

	i82563rxinit(ctlr);
	csr32w(ctlr, Rctl, csr32r(ctlr, Rctl) | Ren);
	if(cttab[ctlr->type].flag & F75){
		csr32w(ctlr, Rxdctl, csr32r(ctlr, Rxdctl) | Enable);
		im = Rxt0|Rxo|Rxdmt0|Rxseq|Ack;
	}else
		im = Rxt0|Rxo|Rxdmt0|Rxseq|Ack;
	m = ctlr->nrd-1;

	for(;;){
		i82563im(ctlr, im);
		ctlr->rsleep++;
		i82563replenish(ctlr, 1);
		sleep(&ctlr->rrendez, i82563rim, ctlr);

		rdh = ctlr->rdh;
		for(;;){
			rd = &ctlr->rdba[rdh];
			rim = ctlr->rim;
			ctlr->rim = 0;
			if(!(rd->status & Rdd))
				break;

			/*
			 * Accept eop packets with no errors.
			 * With no errors and the Ixsm bit set,
			 * the descriptor status Tpcs and Ipcs bits give
			 * an indication of whether the checksums were
			 * calculated and valid.
			 */
			bp = ctlr->rb[rdh];
			if((rd->status & Reop) && rd->errors == 0){
				bp->wp += rd->length;
				bp->lim = bp->wp;	/* lie like a dog.  avoid packblock. */
				if(!(rd->status & Ixsm)){
					ctlr->ixsm++;
					if(rd->status & Ipcs){
						/*
						 * IP checksum calculated
						 * (and valid as errors == 0).
						 */
						ctlr->ipcs++;
						bp->flag |= Bipck;
					}
					if(rd->status & Tcpcs){
						/*
						 * TCP/UDP checksum calculated
						 * (and valid as errors == 0).
						 */
						ctlr->tcpcs++;
						bp->flag |= Btcpck|Budpck;
					}
					bp->checksum = rd->checksum;
					bp->flag |= Bpktck;
				}
				etheriq(edev, bp, 1);
			} else
				freeb(bp);
			ctlr->rb[rdh] = nil;
			rd->status = 0;
			ctlr->rdfree--;
			ctlr->rdh = rdh = Next(rdh, m);
			if(ctlr->nrd-ctlr->rdfree >= 32 || (rim & Rxdmt0))
				if(i82563replenish(ctlr, 0) == -1)
					break;
		}
	}
}

static int
i82563lim(void *v)
{
	return ((Ctlr*)v)->lim != 0;
}

static int speedtab[] = {
	10, 100, 1000, 0
};

static uint phywrite0(Ctlr*, int, int, ushort);

static uint
setpage(Ctlr *c, uint phyno, uint p, uint r)
{
	uint pr;

	switch(c->type){
	case i82563:
		if(r >= 16 && r <= 28 && r != 22)
			pr = Phypage;
		else if(r == 30 || r == 31)
			pr = Phyapage;
		else
			return 0;
		return phywrite0(c, phyno, pr, p);
	case i82576:
	case i82577:
	case i82578:
		return phywrite0(c, phyno, Phy79page, p);		/* unverified */
	case i82579:
		return phywrite0(c, phyno, Phy79page, p<<5);
	default:
		if(p == 0)
			return 0;
		return ~0;
	}
}

static uint
phyread0(Ctlr *c, int phyno, int reg)
{
	uint phy, i;

	csr32w(c, Mdic, MDIrop | phyno<<MDIpSHIFT | reg<<MDIrSHIFT);
	phy = 0;
	for(i = 0; i < 64; i++){
		phy = csr32r(c, Mdic);
		if(phy & (MDIe|MDIready))
			break;
		microdelay(1);
	}
	if((phy & (MDIe|MDIready)) != MDIready){
		print("%s: phy %d wedged %.8ux\n", cttab[c->type].name, phyno, phy);
		return ~0;
	}
	return phy & 0xffff;
}

static uint
phyread(Ctlr *c, uint phyno, uint reg)
{
	if(setpage(c, phyno, reg>>8, reg & 0xff) == ~0){
		print("%s: phyread: bad phy page %d\n", cname(c), reg>>8);
		return ~0;
	}
	return phyread0(c, phyno, reg & 0xff);
}

static uint
phywrite0(Ctlr *c, int phyno, int reg, ushort val)
{
	uint phy, i;

	csr32w(c, Mdic, MDIwop | phyno<<MDIpSHIFT | reg<<MDIrSHIFT | val);
	phy = 0;
	for(i = 0; i < 64; i++){
		phy = csr32r(c, Mdic);
		if(phy & (MDIe|MDIready))
			break;
		microdelay(1);
	}
	if((phy & (MDIe|MDIready)) != MDIready)
		return ~0;
	return 0;
}

static uint
phywrite(Ctlr *c, uint phyno, uint reg, ushort v)
{
	if(setpage(c, phyno, reg>>8, reg & 0xff) == ~0)
		panic("%s: bad phy reg %.4ux", cname(c), reg);
	return phywrite0(c, phyno, reg & 0xff, v);
}

static void
phyerrata(Ether *e, Ctlr *c, uint phyno)
{
	if(e->mbps == 0)
		if(c->phyerrata == 0){
			c->phyerrata++;
			phywrite(c, phyno, Phyprst, Prst);	/* try a port reset */
			print("ether%d: %s: phy port reset\n", e->ctlrno, cname(c));
		}
	else
		c->phyerrata = 0;
}

static void
phyl79proc(void *v)
{
	uint a, i, r, phy, phyno;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	phyno = cttab[c->type].phyno;
	for(;;){
		phy = phyread(c, phyno, Phystat);
		if(phy == ~0){
			phy = 0;
			i = 3;
			goto next;
		}
		i = (phy>>8) & 3;
		a = phy & Ans;
		if(a){
			r = phyread(c, phyno, Phyctl);
			phywrite(c, phyno, Phyctl, r | Ran | Ean);
		}
next:
		e->link = i != 3 && (phy & Link) != 0;
		if(e->link == 0)
			i = 3;
		c->speeds[i]++;
		e->mbps = speedtab[i];
		c->lim = 0;
		i82563im(c, Lsc);
		c->lsleep++;
		sleep(&c->lrendez, i82563lim, c);
	}
}

static void
phylproc(void *v)
{
	uint a, i, phy, phyno;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;
	phyno = cttab[c->type].phyno;

	if(c->type == i82573 && (phy = phyread(c, 1, Phyier)) != ~0)
		phywrite(c, phyno, Phyier, phy | Lscie | Ancie | Spdie | Panie);
	for(;;){
		phy = phyread(c, phyno, Physsr);
		if(phy == ~0){
			phy = 0;
			i = 3;
			goto next;
		}
		i = (phy>>14) & 3;
		switch(c->type){
		default:
			a = 0;
			break;
		case i82563:
		case i82578:
		case i82578m:
		case i82583:
			a = phyread(c, phyno, Phyisr) & Ane;
			break;
		case i82571:
		case i82572:
		case i82575:
		case i82576:
			a = phyread(c, phyno, Phylhr) & Anf;
			i = (i-1) & 3;
			break;
		}
		if(a)
			phywrite(c, phyno, Phyctl, phyread(c, phyno, Phyctl) | Ran | Ean);
next:
		e->link = (phy & Rtlink) != 0;
		if(e->link == 0)
			i = 3;
		c->speeds[i]++;
		e->mbps = speedtab[i];
		if(c->type == i82563)
			phyerrata(e, c, phyno);
		c->lim = 0;
		i82563im(c, Lsc);
		c->lsleep++;
		sleep(&c->lrendez, i82563lim, c);
	}
}

static void
pcslproc(void *v)
{
	uint i, phy;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	if(c->type == i82575 || c->type == i82576)
		csr32w(c, Connsw, Enrgirq);
	for(;;){
		phy = csr32r(c, Pcsstat);
		e->link = phy & Linkok;
		i = 3;
		if(e->link)
			i = (phy & 6) >> 1;
		else if(phy & Anbad)
			csr32w(c, Pcsctl, csr32r(c, Pcsctl) | Pan | Prestart);
		c->speeds[i]++;
		e->mbps = speedtab[i];
		c->lim = 0;
		i82563im(c, Lsc | Omed);
		c->lsleep++;
		sleep(&c->lrendez, i82563lim, c);
	}
}

static void
serdeslproc(void *v)
{
	uint i, tx, rx;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	for(;;){
		rx = csr32r(c, Rxcw);
		tx = csr32r(c, Txcw);
		USED(tx);
		e->link = (rx & 1<<31) != 0;
//		e->link = (csr32r(c, Status) & Lu) != 0;
		i = 3;
		if(e->link)
			i = 2;
		c->speeds[i]++;
		e->mbps = speedtab[i];
		c->lim = 0;
		i82563im(c, Lsc);
		c->lsleep++;
		sleep(&c->lrendez, i82563lim, c);
	}
}

static void
i82563attach(Ether *edev)
{
	char name[KNAMELEN];
	int i;
	Block *bp;
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	qlock(&ctlr->alock);
	if(ctlr->alloc != nil){
		qunlock(&ctlr->alock);
		return;
	}

	ctlr->nrd = Nrd;
	ctlr->ntd = Ntd;
	ctlr->alloc = malloc(ctlr->nrd*sizeof(Rd)+ctlr->ntd*sizeof(Td) + 255);
	if(ctlr->alloc == nil){
		qunlock(&ctlr->alock);
		error(Enomem);
	}
	ctlr->rdba = (Rd*)ROUNDUP((uintptr)ctlr->alloc, 256);
	ctlr->tdba = (Td*)(ctlr->rdba + ctlr->nrd);

	ctlr->rb = malloc(ctlr->nrd * sizeof(Block*));
	ctlr->tb = malloc(ctlr->ntd * sizeof(Block*));

	if(waserror()){
		while(bp = i82563rballoc(rbtab + ctlr->pool)){
			bp->free = nil;
			freeb(bp);
		}
		free(ctlr->tb);
		ctlr->tb = nil;
		free(ctlr->rb);
		ctlr->rb = nil;
		free(ctlr->alloc);
		ctlr->alloc = nil;
		qunlock(&ctlr->alock);
		nexterror();
	}

	for(i = 0; i < Nrb; i++){
		bp = allocb(ctlr->rbsz + Rbalign);
		bp->free = freetab[ctlr->pool];
		freeb(bp);
	}

	snprint(name, sizeof name, "#l%dl", edev->ctlrno);
	if(csr32r(ctlr, Status) & Tbimode)
		kproc(name, serdeslproc, edev);		/* mac based serdes */
	else if((csr32r(ctlr, Ctrlext) & Linkmode) == Serdes)
		kproc(name, pcslproc, edev);		/* phy based serdes */
	else if(cttab[ctlr->type].flag & F79phy)
		kproc(name, phyl79proc, edev);
	else
		kproc(name, phylproc, edev);

	snprint(name, sizeof name, "#l%dr", edev->ctlrno);
	kproc(name, i82563rproc, edev);

	snprint(name, sizeof name, "#l%dt", edev->ctlrno);
	kproc(name, i82563tproc, edev);

	qunlock(&ctlr->alock);
	poperror();
}

static void
i82563interrupt(Ureg*, void *arg)
{
	Ctlr *ctlr;
	Ether *edev;
	u32int icr, im;

	edev = arg;
	ctlr = edev->ctlr;

	ilock(&ctlr->imlock);
	csr32w(ctlr, Imc, ~0);
	im = ctlr->im;

	while(icr = csr32r(ctlr, Icr) & ctlr->im){
		if(icr & (Lsc | Omed)){
			im &= ~(Lsc | Omed);
			ctlr->lim = icr & (Lsc | Omed);
			wakeup(&ctlr->lrendez);
			ctlr->lintr++;
		}
		if(icr & (Rxt0|Rxo|Rxdmt0|Rxseq|Ack)){
			ctlr->rim = icr & (Rxt0|Rxo|Rxdmt0|Rxseq|Ack);
			im &= ~(Rxt0|Rxo|Rxdmt0|Rxseq|Ack);
			wakeup(&ctlr->rrendez);
			ctlr->rintr++;
		}
		if(icr & Txdw){
			im &= ~Txdw;
			ctlr->tintr++;
			wakeup(&ctlr->trendez);
		}
	}

	ctlr->im = im;
	csr32w(ctlr, Ims, im);
	iunlock(&ctlr->imlock);
}

static int
i82563detach(Ctlr *ctlr)
{
	int r, timeo;

	/* balance rx/tx packet buffer; survives reset */
	if(ctlr->rbsz > 8192 && cttab[ctlr->type].flag & Fpba){
		ctlr->pba = csr32r(ctlr, Pba);
		r = ctlr->pba >> 16;
		r += ctlr->pba & 0xffff;
		r >>= 1;
		csr32w(ctlr, Pba, r);
	}else if(ctlr->type == i82573 && ctlr->rbsz > 1514)
		csr32w(ctlr, Pba, 14);
	ctlr->pba = csr32r(ctlr, Pba);

	/*
	 * Perform a device reset to get the chip back to the
	 * power-on state, followed by an EEPROM reset to read
	 * the defaults for some internal registers.
	 */
	csr32w(ctlr, Imc, ~0);
	csr32w(ctlr, Rctl, 0);
	csr32w(ctlr, Tctl, csr32r(ctlr, Tctl) & ~Ten);

	delay(10);

	r = csr32r(ctlr, Ctrl);
	if(ctlr->type == i82566 || ctlr->type == i82579)
		r |= Phyrst;
	csr32w(ctlr, Ctrl, Devrst | r);
	delay(1);
	for(timeo = 0;; timeo++){
		if((csr32r(ctlr, Ctrl) & (Devrst|Phyrst)) == 0)
			break;
		if(timeo >= 1000)
			return -1;
		delay(1);
	}

	r = csr32r(ctlr, Ctrl);
	csr32w(ctlr, Ctrl, Slu|r);

	r = csr32r(ctlr, Ctrlext);
	csr32w(ctlr, Ctrlext, r|Eerst);
	delay(1);
	for(timeo = 0; timeo < 1000; timeo++){
		if(!(csr32r(ctlr, Ctrlext) & Eerst))
			break;
		delay(1);
	}
	if(csr32r(ctlr, Ctrlext) & Eerst)
		return -1;

	csr32w(ctlr, Imc, ~0);
	delay(1);
	for(timeo = 0; timeo < 1000; timeo++){
		if((csr32r(ctlr, Icr) & ~Rxcfg) == 0)
			break;
		delay(1);
	}
	if(csr32r(ctlr, Icr) & ~Rxcfg)
		return -1;

	return 0;
}

static void
i82563shutdown(Ether *edev)
{
	i82563detach(edev->ctlr);
}

static ushort
eeread(Ctlr *ctlr, int adr)
{
	csr32w(ctlr, Eerd, EEstart | adr << 2);
	while ((csr32r(ctlr, Eerd) & EEdone) == 0)
		;
	return csr32r(ctlr, Eerd) >> 16;
}

static int
eeload(Ctlr *ctlr)
{
	u16int sum;
	int data, adr;

	sum = 0;
	for (adr = 0; adr < 0x40; adr++) {
		data = eeread(ctlr, adr);
		ctlr->eeprom[adr] = data;
		sum += data;
	}
	return sum;
}

static int
fcycle(Ctlr*, Flash *f)
{
	u16int s, i;

	s = f->reg[Fsts];
	if((s&Fvalid) == 0)
		return -1;
	f->reg[Fsts] |= Fcerr | Ael;
	for(i = 0; i < 10; i++){
		if((s&Scip) == 0)
			return 0;
		delay(1);
		s = f->reg[Fsts];
	}
	return -1;
}

static int
fread(Ctlr *c, Flash *f, int ladr)
{
	u16int s;

	delay(1);
	if(fcycle(c, f) == -1)
		return -1;
	f->reg[Fsts] |= Fdone;
	f->reg32[Faddr] = ladr;

	/* setup flash control register */
	s = f->reg[Fctl] & ~0x3ff;
	f->reg[Fctl] = s | 1<<8 | Fgo;	/* 2 byte read */

	while((f->reg[Fsts] & Fdone) == 0)
		;
	if(f->reg[Fsts] & (Fcerr|Ael))
		return -1;
	return f->reg32[Fdata] & 0xffff;
}

static int
fload(Ctlr *c)
{
	uint data,  r, adr;
	u16int sum;
	uintmem io;
	Flash f;

	io = c->pcidev->mem[1].bar & ~(uintmem)0xf;
	f.reg = vmap(io, c->pcidev->mem[1].size);
	if(f.reg == nil)
		return -1;
	f.reg32 = (u32int*)f.reg;
	f.base = f.reg32[Bfpr] & 0x1fff;
	f.lim = f.reg32[Bfpr]>>16 & 0x1fff;
	if(csr32r(c, Eec) & Sec1val)
		f.base += f.lim+1 - f.base >> 1;
	r = f.base << 12;
	sum = 0;
	for(adr = 0; adr < 0x40; adr++) {
		data = fread(c, &f, r + adr*2);
		if(data == -1)
			return -1;
		c->eeprom[adr] = data;
		sum += data;
	}
	vunmap(f.reg, c->pcidev->mem[1].size);
	return sum;
}

static void
defaultea(Ctlr *ctlr, uchar *ra)
{
	uint i, r;
	uvlong u;
	static uchar nilea[Eaddrlen];

	if(memcmp(ra, nilea, Eaddrlen) != 0)
		return;
	if(cttab[ctlr->type].flag & Fflashea){
		/* intel mb bug */
		u = (uvlong)csr32r(ctlr, Rah)<<32u | (uint)csr32r(ctlr, Ral);
		for(i = 0; i < Eaddrlen; i++)
			ra[i] = u >> 8*i;
	}
	if(memcmp(ra, nilea, Eaddrlen) != 0)
		return;
	for(i = 0; i < Eaddrlen/2; i++){
		ra[2*i] = ctlr->eeprom[Ea+i];
		ra[2*i+1] = ctlr->eeprom[Ea+i] >> 8;
	}
	r = (csr32r(ctlr, Status) & Lanid) >> 2;
	ra[5] += r;				/* ea ctlr[n] = ea ctlr[0]+n */
}

static int
i82563reset(Ctlr *ctlr)
{
	uchar *ra;
	int i, r;

	if(i82563detach(ctlr))
		return -1;
	if(cttab[ctlr->type].flag & Fload)
		r = fload(ctlr);
	else
		r = eeload(ctlr);
	if(r != 0 && r != 0xbaba){
		print("%s: bad eeprom checksum - %#.4ux\n",
			cname(ctlr), r);
		return -1;
	}

	ra = ctlr->ra;
	defaultea(ctlr, ra);
	csr32w(ctlr, Ral, ra[3]<<24 | ra[2]<<16 | ra[1]<<8 | ra[0]);
	csr32w(ctlr, Rah, 1<<31 | ra[5]<<8 | ra[4]);
	for(i = 1; i < 16; i++){
		csr32w(ctlr, Ral+i*8, 0);
		csr32w(ctlr, Rah+i*8, 0);
	}
	memset(ctlr->mta, 0, sizeof(ctlr->mta));
	for(i = 0; i < 128; i++)
		csr32w(ctlr, Mta + i*4, 0);
	csr32w(ctlr, Fcal, 0x00C28001);
	csr32w(ctlr, Fcah, 0x0100);
	if((cttab[ctlr->type].flag & Fnofct) == 0)
		csr32w(ctlr, Fct, 0x8808);
	csr32w(ctlr, Fcttv, 0x0100);
	csr32w(ctlr, Fcrtl, ctlr->fcrtl);
	csr32w(ctlr, Fcrth, ctlr->fcrth);
	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Eitr, 128<<2);		/* 128 ¼ microsecond intervals */
	return 0;
}

enum {
	CMrdtr,
	CMradv,
	CMpause,
	CMan,
};

static Cmdtab i82563ctlmsg[] = {
	CMrdtr,	"rdtr",	2,
	CMradv,	"radv",	2,
	CMpause, "pause", 1,
	CMan,	"an",	1,
};

static long
i82563ctl(Ether *edev, void *buf, long n)
{
	char *p;
	u32int v;
	Ctlr *ctlr;
	Cmdbuf *cb;
	Cmdtab *ct;

	if((ctlr = edev->ctlr) == nil)
		error(Enonexist);

	cb = parsecmd(buf, n);
	if(waserror()){
		free(cb);
		nexterror();
	}

	ct = lookupcmd(cb, i82563ctlmsg, nelem(i82563ctlmsg));
	switch(ct->index){
	case CMrdtr:
		v = strtoul(cb->f[1], &p, 0);
		if(*p || v > 0xffff)
			error(Ebadarg);
		ctlr->rdtr = v;
		csr32w(ctlr, Rdtr, v);
		break;
	case CMradv:
		v = strtoul(cb->f[1], &p, 0);
		if(*p || v > 0xffff)
			error(Ebadarg);
		ctlr->radv = v;
		csr32w(ctlr, Radv, v);
		break;
	case CMpause:
		csr32w(ctlr, Ctrl, csr32r(ctlr, Ctrl) ^ (Rfce | Tfce));
		break;
	case CMan:
		csr32w(ctlr, Ctrl, csr32r(ctlr, Ctrl) | Lrst | Phyrst);
		break;
	}
	free(cb);
	poperror();

	return n;
}

static int
didtype(int d)
{
	switch(d){
	case 0x1096:
	case 0x10ba:		/* “gilgal” */
	case 0x1098:		/* serdes; not seen */
	case 0x10bb:		/* serdes */
		return i82563;
	case 0x1049:		/* mm */
	case 0x104a:		/* dm */
	case 0x104b:		/* dc */
	case 0x104d:		/* v “ninevah” */
	case 0x10bd:		/* dm-2 */
	case 0x294c:		/* ich 9 */
		return i82566;
	case 0x10de:		/* lm ich10d */
	case 0x10df:		/* lf ich10 */
	case 0x10e5:		/* lm ich9 */
	case 0x10f5:		/* lm ich9m; “boazman” */
		return i82567;
	case 0x10bf:		/* lf ich9m */
	case 0x10cb:		/* v ich9m */
	case 0x10cd:		/* lf ich10 */
	case 0x10ce:		/* v ich10 */
	case 0x10cc:		/* lm ich10 */
		return i82567m;
	case 0x105e:		/* eb */
	case 0x105f:		/* eb */
	case 0x1060:		/* eb */
	case 0x10a4:		/* eb */
	case 0x10a5:		/* eb  fiber */
	case 0x10bc:		/* eb */
	case 0x10d9:		/* eb serdes */
	case 0x10da:		/* eb serdes “ophir” */
		return i82571;
	case 0x107d:		/* eb copper */
	case 0x107e:		/* ei fiber */
	case 0x107f:		/* ei */
	case 0x10b9:		/* ei “rimon” */
		return i82572;
	case 0x108b:		/*  e “vidalia” */
	case 0x108c:		/*  e (iamt) */
	case 0x109a:		/*  l “tekoa” */
		return i82573;
	case 0x10d3:		/* l or it; “hartwell” */
		return i82574;
	case 0x10a7:
	case 0x10a9:		/* fiber/serdes */
		return i82575;
	case 0x10c9:		/* copper */
	case 0x10e6:		/* fiber */
	case 0x10e7:		/* serdes; “kawela” */
	case 0x150d:		/* backplane */
		return i82576;
	case 0x10ea:		/* lc “calpella”; aka pch lan */
		return i82577;
	case 0x10eb:		/* lm “calpella” */
		return i82577m;
	case 0x10ef:		/* dc “piketon” */
		return i82578;
	case 0x1502:		/* lm */
	case 0x1503:		/* v “lewisville” */
		return i82579;
	case 0x10f0:		/* dm “king's creek” */
		return i82578m;
	case 0x150e:		/* “barton hills” */
	case 0x150f:		/* fiber */
	case 0x1510:		/* backplane */
	case 0x1511:		/* sfp */
	case 0x1516:		
		return i82580;
	case 0x1506:		/* v */
		return i82583;
	case 0x1533:		/* i210-t1 */
	case 0x1534:
	case 0x1536:		/* fiber */
	case 0x1537:		/* backplane */
	case 0x1538:
	case 0x1539:		/* i211 */
		return i210;
	case 0x153a:		/* i217-lm */
	case 0x153b:		/* i217-v */
	case 0x15a0:		/* i218-lm */
	case 0x15a1:		/* i218-v */
	case 0x15a2:		/* i218-lm */
	case 0x15a3:		/* i218-v */
		return i217;
	case 0x151f:		/* “powerville” eeprom-less */
	case 0x1521:		/* copper */
	case 0x1522:		/* fiber */
	case 0x1523:		/* serdes */
	case 0x1524:		/* sgmii */
		return i350;
	}
	return -1;
}

static void
hbafixup(Pcidev *p)
{
	uint i;

	i = pcicfgr32(p, PciSVID);
	if((i & 0xffff) == 0x1b52 && p->did == 1)
		p->did = i>>16;
}

static void
i82563pci(void)
{
	int type;
	Ctlr *c, **cc;
	Pcidev *p;

	cc = &i82563ctlr;
	for(p = nil; p = pcimatch(p, 0x8086, 0);){
		hbafixup(p);
		if((type = didtype(p->did)) == -1)
			continue;
		c = malloc(sizeof *c);
		c->type = type;
		c->pcidev = p;
		c->rbsz = cttab[type].mtu;
		c->port = p->mem[0].bar & ~(uintmem)0xf;
		*cc = c;
		cc = &c->next;
	}
}

static int
setup(Ctlr *ctlr)
{
	Pcidev *p;

	if((ctlr->pool = newpool()) == -1){
		print("%s: no pool\n", cname(ctlr));
		return -1;
	}
	p = ctlr->pcidev;
	ctlr->nic = vmap(ctlr->port, p->mem[0].size);
	if(ctlr->nic == nil){
		print("%s: can't map %#P\n", cname(ctlr), ctlr->port);
		return -1;
	}
	if(i82563reset(ctlr)){
		vunmap(ctlr->nic, p->mem[0].size);
		return -1;
	}
	pcisetbme(ctlr->pcidev);
	return 0;
}

static int
pnp(Ether *edev, int type)
{
	Ctlr *ctlr;
	static int done;

	if(!done) {
		i82563pci();
		done = 1;
	}

	/*
	 * Any adapter matches if no edev->port is supplied,
	 * otherwise the ports must match.
	 */
	for(ctlr = i82563ctlr; ; ctlr = ctlr->next){
		if(ctlr == nil)
			return -1;
		if(ctlr->active)
			continue;
		if(type != -1 && ctlr->type != type)
			continue;
		if(ethercfgmatch(edev, ctlr->pcidev, ctlr->port) == 0){
			ctlr->active = 1;
			memmove(ctlr->ra, edev->ea, Eaddrlen);
			if(setup(ctlr) == 0)
				break;
		}
	}

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pcidev->intl;
	edev->tbdf = ctlr->pcidev->tbdf;
	edev->mbps = 1000;
	edev->maxmtu = ctlr->rbsz;
	memmove(edev->ea, ctlr->ra, Eaddrlen);

	/*
	 * Linkage to the generic ethernet driver.
	 */
	edev->attach = i82563attach;
	edev->interrupt = i82563interrupt;
	edev->ifstat = i82563ifstat;
	edev->ctl = i82563ctl;

	edev->arg = edev;
	edev->promiscuous = i82563promiscuous;
	edev->shutdown = i82563shutdown;
	edev->multicast = i82563multicast;

	return 0;
}

static int
anypnp(Ether *e)
{
	return pnp(e, -1);
}

void
ether82563link(void)
{
	addethercard("i82563", anypnp);
}
