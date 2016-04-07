typedef struct SMBus SMBus;
typedef struct SMdev SMdev;

/* SMBus transactions */
enum
{
	SMBquick,		/* sends address only */

	/* write */
	SMBsend,		/* sends address and cmd */
	SMBbytewrite,		/* sends address and cmd and 1 byte */
	SMBwordwrite,		/* sends address and cmd and 2 bytes */

	/* read */
	SMBrecv,		/* sends address, recvs 1 byte */
	SMBbyteread,		/* sends address and cmd, recv's byte */
	SMBwordread,		/* sends address and cmd, recv's 2 bytes */

	/* read or write is a function of bit 0 in the slave addr */
//	SMBquick= 0,
	SMBbyte= 1,
	SMBbytedata,
	SMBworddata,
	SMBprocess,
	SMBblock,
	SMBi2cread,
	SMBblockprocess,
};

typedef struct Udid Udid;
struct Udid {
	uchar cap;
	uchar ver;
	uchar vid[2];
	uchar did[2];
	uchar ifc[2];
	uchar svid[2];
	uchar sdid[2];
	uchar vsid[4];
};

enum {
	STsmb,
	STi2c,
};

typedef struct Smbdev Smbdev;
struct Smbdev {		// smbus relies on this structure format.
	Udid udid;
	uchar addr;
	uchar type;
	ushort vid;
	ushort did;
};

typedef struct SMBus SMBus;
struct SMBus {
	QLock;		/* mutex */
	Rendez	r;	/* rendezvous point for completion interrupts */
	void	*arg;	/* implementation dependent */
	ulong	base;	/* port or memory base of smbus */
	int	busy;
	Smbdev* (*smbmatch)(Smbdev*, int, int);
	int	(*transact)(SMBus*, int, int, int, uchar*, int);
	void	(*enumerate)(SMBus*);
};


Smbdev*	smbmatch(Smbdev*, int, int);
Smbdev*	smbmatchaddr(int);
int	smbrdbyte(Smbdev*, int);
int smbwrbyte(Smbdev*, int, int);
SMBus*	smbus(void);
long	smbctl(char*, long);
void	smbreset(void);
