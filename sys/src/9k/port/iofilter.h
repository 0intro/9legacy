
enum {
	Niosamples	= 32,
	Lsum		= 0,
	Lmax,
	Lavg,
	Lsz,
};

typedef struct Iofilter Iofilter;
struct Iofilter {
	Lock;
	ulong nsamples;			/* total samples taken */
	struct {
		ulong b;
		ulong lat[Lsz];		/* latency min, max, avg for bytes in b */
	} samples[Niosamples];

	ulong bytes;
	ulong lmin;
	ulong lmax;
	vlong lsum;
	ulong nlat;
};

#pragma	varargck	type	"Z"	Iofilter*

void		incfilter(Iofilter *, ulong, ulong);
void		delfilter(Iofilter *);
int		addfilter(Iofilter *);
void		zfilter(Iofilter *);
int		filtersum(Iofilter*, uvlong*, vlong*, int);
int		filterfmt(Fmt *);
