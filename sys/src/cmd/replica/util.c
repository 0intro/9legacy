#include "all.h"

void*
erealloc(void *a, int n)
{
	a = realloc(a, n);
	if(a==nil)
		sysfatal("realloc: %r");
	return a;
}

char*
estrdup(char *s)
{
	s = strdup(s);
	if(s == nil)
		sysfatal("strdup: %r");
	return s;
}

void*
emalloc(int n)
{
	void *a;

	a = mallocz(n, 1);
	if(a == nil)
		sysfatal("malloc: %r");
	return a;
}

/*
 *	Custom allocators to avoid malloc overheads on small objects.
 * 	We never free these.  (See below.)
 */
typedef struct Stringtab	Stringtab;
struct Stringtab {
	Stringtab *link;
	char *str;
};
static Stringtab*
taballoc(void)
{
	static Stringtab *t;
	static uint nt;

	if(nt == 0){
		t = malloc(64*sizeof(Stringtab));
		if(t == 0)
			sysfatal("out of memory");
		nt = 64;
	}
	nt--;
	return t++;
}

static char*
xstrdup(char *s)
{
	char *r;
	int len;
	static char *t;
	static int nt;

	len = strlen(s)+1;
	if(len >= 8192)
		sysfatal("strdup big string");

	if(nt < len){
		t = malloc(8192);
		if(t == 0)
			sysfatal("out of memory");
		nt = 8192;
	}
	r = t;
	t += len;
	nt -= len;
	strcpy(r, s);
	return r;
}

/*
 *	Return a uniquely allocated copy of a string.
 *	Don't free these -- they stay in the table for the 
 *	next caller who wants that particular string.
 *	String comparison can be done with pointer comparison 
 *	if you know both strings are atoms.
 */
static Stringtab *stab[1024];

static uint
hash(char *s)
{
	uint h;
	uchar *p;

	h = 0;
	for(p=(uchar*)s; *p; p++)
		h = h*37 + *p;
	return h;
}

char*
atom(char *str)
{
	uint h;
	Stringtab *tab;
	
	h = hash(str) % nelem(stab);
	for(tab=stab[h]; tab; tab=tab->link)
		if(strcmp(str, tab->str) == 0)
			return tab->str;

	tab = taballoc();
	tab->str = xstrdup(str);
	tab->link = stab[h];
	stab[h] = tab;
	return tab->str;
}

char*
unroot(char *path, char *root)
{
	int len;
	char *s;

	len = strlen(root);
	while(len >= 1 && root[len-1]=='/')
		len--;
	if(strncmp(path, root, len)==0 && (path[len]=='/' || path[len]=='\0')){
		s = path+len;
		while(*s == '/')
			s++;
		return s;
	}
	return path;
}

/*
 * SHA1 an open file by pread (so the caller's seek position is left
 * alone) into buf as lowercase hex. buf must hold 2*SHA1dlen+1 bytes.
 * Returns buf, or nil if the file cannot be read.
 */
char*
hashfd(int fd, char *buf)
{
	static char hex[] = "0123456789abcdef";
	uchar digest[SHA1dlen], data[8192];
	DigestState *s;
	vlong o;
	int n;

	s = nil;
	o = 0;
	while((n = pread(fd, data, sizeof data, o)) > 0){
		s = sha1(data, n, nil, s);
		o += n;
	}
	if(n < 0)
		return nil;
	sha1(nil, 0, digest, s);
	for(n = 0; n < SHA1dlen; n++){
		buf[2*n] = hex[digest[n]>>4];
		buf[2*n+1] = hex[digest[n]&0xf];
	}
	buf[2*SHA1dlen] = 0;
	return buf;
}

/*
 * SHA1 the file at path. Returns buf, or nil if it cannot be read,
 * in which case the caller uses "-".
 */
char*
hashfile(char *path, char *buf)
{
	int fd;
	char *r;

	if((fd = open(path, OREAD)) < 0)
		return nil;
	r = hashfd(fd, buf);
	close(fd);
	return r;
}
