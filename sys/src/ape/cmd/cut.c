#include <u.h>
#include <libc.h>
#include <bio.h>

void	usage(void);
int	parselist(char *, long **);
int	indexl(long *, long, int);
int	cmpl(const void *, const void *);
void	selectbytes(Biobuf*, int);
void	selectrunes(Biobuf*, int);
void	selectfields(Biobuf*, int);

enum {
	Listmax = 1<<10,
};

long *cols;
int ncol;
char *delims;

void
main(int argc, char **argv)
{
	Biobuf fout;
	char *list;
	int i, fd;
	void (*filter)(Biobuf*, int);

	delims = "\t";
	list = nil;
	filter = nil;
	ARGBEGIN {
	case 'b':
		list = EARGF(usage());
		filter = selectbytes;
		break;
	case 'c':
		list = EARGF(usage());
		filter = selectrunes;
		break;
	case 'd':
		delims = EARGF(usage());
		if(strlen(delims) != 1){
			fprint(2, "%s: bad delimiter\n", argv0);
			exits("usage");
		}
		break;
	case 'f':
		list = EARGF(usage());
		filter = selectfields;
		break;
	default:
		usage();
	} ARGEND
	if(list == nil)
		usage();

	ncol = parselist(list, &cols);
	if(ncol < 0)
		sysfatal("list '%s': %r", list);
	Binit(&fout, 1, OWRITE);
	if(argc == 0)
		filter(&fout, 0);
	else
		for(i = 0; i < argc; i++){
			fd = open(argv[i], OREAD);
			if(fd < 0)
				sysfatal("open: %r");
			filter(&fout, fd);
			close(fd);
		}
	exits(nil);
}

void
usage(void)
{
	fprint(2, "usage: %s {-[b|c|f] list} [-d delim] [file ...]\n", argv0);
	exits("usage");
}

int
parselist(char *s, long **a)
{
	int n, u;
	long v, r, d;

	n = 1;
	u = 0;
	*a = malloc(sizeof(**a) * n);
	if(*a == nil)
		sysfatal("malloc: %r");
	while(*s){
		if(*s == '-')
			v = 0;
		else{
			v = strtol(s, &s, 10);
			if(n <= 0){
				werrstr("value may not include zero");
				return -1;
			}
			v--;
		}
		if(*s != '-')
			r = v+1;
		else{
			r = strtol(s+1, &s, 10);
			if(r < 0){
				werrstr("illegal list value");
				return -1;
			}
			if(r == 0)
				r = Listmax;
		}
		d = r-v;
		if(d < 0)
			d = 0;
		if(u+d >= n){
			n = n*2 + d;
			*a = realloc(*a, sizeof(**a)*n);
			if(*a == nil)
				sysfatal("realloc: %r");
		}
		while(r-- > v)
			if(indexl(*a, r, u) < 0)
				(*a)[u++] = r;
		if(*s == ' ' || *s == ',')
			s++;
	}
	qsort(*a, u, sizeof **a, cmpl);
	return u;
}

int
indexl(long *a, long v, int n)
{
	int i;

	for(i = 0; i < n; i++)
		if(a[i] == v)
			return i;
	return -1;
}

int
cmpl(const void *a, const void *b)
{
	long n1, n2;

	n1 = *(long *)a;
	n2 = *(long *)b;
	if(n1 < n2)
		return -1;
	else if(n1 == n2)
		return 0;
	else
		return 1;
}

void
selectbytes(Biobuf *w, int fd)
{
	Biobuf b;
	char *s;
	long *p, *e;

	Binit(&b, fd, OREAD);
	while(s = Brdline(&b, '\n')){
		s[Blinelen(&b)-1] = '\0';
		e = cols + ncol;
		for(p = cols; p < e; p++){
			if(*p >= strlen(s))
				break;
			Bputc(w, s[*p]);
		}
		Bputc(w, '\n');
		Bflush(w);
	}
}

void
selectrunes(Biobuf *w, int fd)
{
	Biobuf b;
	char *s;
	Rune *r, *rp;
	long *p, *e;

	Binit(&b, fd, OREAD);
	while(s = Brdline(&b, '\n')){
		s[Blinelen(&b)-1] = '\0';
		r = malloc(strlen(s) * sizeof *r);
		if(r == nil)
			sysfatal("malloc: %r");
		rp = r;
		while(*s)
			s += chartorune(rp++, s);
		e = cols + ncol;
		for(p = cols; p < e; p++){
			if(*p >= runestrlen(r))
				break;
			Bputrune(w, r[*p]);
		}
		free(r);
		Bputc(w, '\n');
		Bflush(w);
	}
}

void
selectfields(Biobuf *w, int fd)
{
	Biobuf b;
	char *s, *a[Listmax];
	int na;
	long *p, *e;

	Binit(&b, fd, OREAD);
	while(s = Brdline(&b, '\n')){
		s[Blinelen(&b)-1] = '\0';
		na = getfields(s, a, nelem(a), 0, delims);
		e = cols + ncol;
		for(p = cols; p < e; p++){
			if(*p >= na)
				break;
			if(p > cols) /* p is not first field */
				Bprint(w, "%s", delims);
			Bprint(w, "%s", a[*p]);
		}
		Bputc(w, '\n');
		Bflush(w);
	}
}
