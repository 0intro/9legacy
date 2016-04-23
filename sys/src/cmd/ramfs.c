#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>

/*
 * Rather than reading /adm/users, which is a lot of work for
 * a toy program, we assume all groups have the form
 *	NNN:user:user:
 * meaning that each user is the leader of his own group.
 */

enum
{
	OPERM	= 0x3,		/* mask of all permission types in open mode */
	Nram	= 512,
	Maxsize	= 768*1024*1024,
	Maxfdata	= 6*8192,
};

typedef struct Fid Fid;
typedef struct Ram Ram;

struct Fid
{
	char	busy;
	char	open;
	char	rclose;
	int	fid;
	Fid	*next;
	char	*user;
	int	rami;
};

struct Ram
{
	char	busy;
	char	open;
	int	parent;		/* index in Ram array */
	Qid	qid;
	long	perm;
	char	*name;
	ulong	atime;
	ulong	mtime;
	char	*user;
	char	*group;
	char	*muid;
	char	*data;
	uvlong	ndata;
};

enum
{
	Pexec =		1,
	Pwrite = 	2,
	Pread = 	4,
	Pother = 	1,
	Pgroup = 	8,
	Powner =	64,
};

ulong	path;		/* incremented for each new file */
Fid	*fids;
Ram	*ram;
int	nram;
int	aram;
int	mfd[2];
char	*user;
uchar	mdata[IOHDRSZ+Maxfdata];
uchar	rdata[Maxfdata];	/* buffer for data in reply */
uchar statbuf[STATMAX];
Fcall thdr;
Fcall	rhdr;
int	messagesize = sizeof mdata;

Fid *	newfid(int);
uint	ramstat(Ram*, uchar*, uint);
void	error(char*);
void	io(void);
int	moreram(void);
char	*estrdup(char*);
void	usage(void);
int	perm(Fid*, Ram*, int);

char	*rflush(Fid*), *rversion(Fid*), *rauth(Fid*),
	*rattach(Fid*), *rwalk(Fid*),
	*ropen(Fid*), *rcreate(Fid*),
	*rread(Fid*), *rwrite(Fid*), *rclunk(Fid*),
	*rremove(Fid*), *rstat(Fid*), *rwstat(Fid*);

int needfid[] = {
	[Tversion] 0,
	[Tflush] 0,
	[Tauth] 0,
	[Tattach] 0,
	[Twalk] 1,
	[Topen] 1,
	[Tcreate] 1,
	[Tread] 1,
	[Twrite] 1,
	[Tclunk] 1,
	[Tremove] 1,
	[Tstat] 1,
	[Twstat] 1,
};

char 	*(*fcalls[])(Fid*) = {
	[Tversion]	rversion,
	[Tflush]	rflush,
	[Tauth]	rauth,
	[Tattach]	rattach,
	[Twalk]		rwalk,
	[Topen]		ropen,
	[Tcreate]	rcreate,
	[Tread]		rread,
	[Twrite]	rwrite,
	[Tclunk]	rclunk,
	[Tremove]	rremove,
	[Tstat]		rstat,
	[Twstat]	rwstat,
};

char	Eperm[] =	"permission denied";
char	Enotdir[] =	"not a directory";
char	Enoauth[] =	"ramfs: authentication not required";
char	Enotexist[] =	"file does not exist";
char	Einuse[] =	"file in use";
char	Eexist[] =	"file exists";
char	Eisdir[] =	"file is a directory";
char	Enotowner[] =	"not owner";
char	Eisopen[] = 	"file already open for I/O";
char	Excl[] = 	"exclusive use file already open";
char	Ename[] = 	"illegal name";
char	Eversion[] =	"unknown 9P version";
char	Enotempty[] =	"directory not empty";
char	Enomem[] =	"out of memory";
char	Ememlimit[] =	"memory limited";

int debug;
int private;

static uvlong	memlim = Maxsize;
static uvlong	memsize;
static int		oomlatch;

void
notifyf(void *a, char *s)
{
	USED(a);
	if(strncmp(s, "interrupt", 9) == 0)
		noted(NCONT);
	noted(NDFLT);
}

static	char	power[]	= "kmgtpezy";

uvlong
atosize(char *s)
{
	char *p, *q;
	int c, n;
	Rune r, op;
	uvlong v, t;

	t = 0;
	op = 0;
	for(p = s;;){
		q = p;
		v = strtoll(p, &p, 0);
		if(q == p)
			sysfatal("unknown size");
		n = chartorune(&r, p);
		if(r >= L'A' && r <= L'Z')
			r += 0x20;
	//	c = op == 0? 2: 0;	too magical
		c = 0;
		if(r != 0){
			if(r < Runeself && (q = strchr(power, r)))
				c = q - power + 1;
			p += n;
		}
		if(*p == 'B' || *p == 'b')
			p++;
		while(c--)
			v *= 1024;
		if(op == '+' || op == 0)
			t += v;
		if(op == '*')
			t *= v;
		if(*p == 0)
			return t;
		op = *p++;
		if(op != '+' && op != '*')
			sysfatal("unknown size");
	}
}

void
main(int argc, char *argv[])
{
	Ram *r;
	char *defmnt, *service;
	int p[2];
	int fd;
	int stdio = 0;

	service = "ramfs";
	defmnt = "/tmp";
	ARGBEGIN{
	case 'i':
		defmnt = 0;
		stdio = 1;
		mfd[0] = 0;
		mfd[1] = 1;
		break;
	case 'm':
		defmnt = EARGF(usage());
		break;
	case 'l':
		memlim = atosize(EARGF(usage()));
		break;
	case 'p':
		private++;
		break;
	case 's':
		defmnt = 0;
		break;
	case 'u':
		memlim = ~0;		/* unlimited memory consumption */
		break;
	case 'D':
		debug = 1;
		break;
	case 'S':
		defmnt = 0;
		service = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(pipe(p) < 0)
		error("pipe failed");
	if(!stdio){
		mfd[0] = p[0];
		mfd[1] = p[0];
		if(defmnt == 0){
			char buf[64];
			snprint(buf, sizeof buf, "#s/%s", service);
			fd = create(buf, OWRITE|ORCLOSE, 0666);
			if(fd < 0)
				error("create failed");
			snprint(buf, sizeof buf, "%d", p[1]);
			if(write(fd, buf, strlen(buf)) < 0)
				error("writing service file");
		}
	}

	aram = Nram;
	if(moreram() == -1)
		sysfatal(Enomem);

	user = getuser();
	notify(notifyf);
	nram = 1;
	r = &ram[0];
	r->busy = 1;
	r->data = 0;
	r->ndata = 0;
	r->perm = DMDIR | 0775;
	r->qid.type = QTDIR;
	r->qid.path = 0LL;
	r->qid.vers = 0;
	r->parent = 0;
	r->user = user;
	r->group = user;
	r->muid = user;
	r->atime = time(0);
	r->mtime = r->atime;
	r->name = estrdup(".");

	if(debug) {
		fmtinstall('F', fcallfmt);
		fmtinstall('M', dirmodefmt);
	}
	switch(rfork(RFFDG|RFPROC|RFNAMEG|RFNOTEG)){
	case -1:
		error("fork");
	case 0:
		close(p[1]);
		io();
		break;
	default:
		close(p[0]);	/* don't deadlock if child fails */
		if(defmnt && mount(p[1], -1, defmnt, MREPL|MCREATE, "") < 0)
			error("mount failed");
	}
	exits(0);
}

char*
rversion(Fid*)
{
	Fid *f;

	for(f = fids; f; f = f->next)
		if(f->busy)
			rclunk(f);
	if(thdr.msize > sizeof mdata)
		rhdr.msize = sizeof mdata;
	else
		rhdr.msize = thdr.msize;
	messagesize = rhdr.msize;
	if(strncmp(thdr.version, "9P2000", 6) != 0)
		return Eversion;
	rhdr.version = "9P2000";
	return 0;
}

char*
rauth(Fid*)
{
	return "ramfs: no authentication required";
}

char*
rflush(Fid *f)
{
	USED(f);
	return 0;
}

char*
rattach(Fid *f)
{
	/* no authentication! */
	f->busy = 1;
	f->rclose = 0;
	f->rami = 0;
	rhdr.qid = ram[0].qid;
	if(thdr.uname[0])
		f->user = estrdup(thdr.uname);
	else
		f->user = "none";
	if(strcmp(user, "none") == 0)
		user = f->user;
	return 0;
}

char*
clone(Fid *f, Fid **nf)
{
	if(f->open)
		return Eisopen;
	if(ram[f->rami].busy == 0)
		return Enotexist;
	*nf = newfid(thdr.newfid);
	(*nf)->busy = 1;
	(*nf)->open = 0;
	(*nf)->rclose = 0;
	(*nf)->rami = f->rami;
	(*nf)->user = f->user;	/* no ref count; the leakage is minor */
	return 0;
}

char*
rwalk(Fid *f)
{
	Ram *r, *fram;
	char *name;
	Ram *parent;
	Fid *nf;
	char *err;
	ulong t;
	int i;

	err = nil;
	nf = nil;
	rhdr.nwqid = 0;
	if(thdr.newfid != thdr.fid){
		err = clone(f, &nf);
		if(err)
			return err;
		f = nf;	/* walk the new fid */
	}
	fram = ram + f->rami;
	if(thdr.nwname > 0){
		t = time(0);
		for(i=0; i<thdr.nwname && i<MAXWELEM; i++){
			if((fram->qid.type & QTDIR) == 0){
				err = Enotdir;
 				break;
			}
			if(fram->busy == 0){
				err = Enotexist;
				break;
			}
			fram->atime = t;
			name = thdr.wname[i];
			if(strcmp(name, ".") == 0){
    Found:
				rhdr.nwqid++;
				rhdr.wqid[i] = fram->qid;
				continue;
			}
			parent = &ram[fram->parent];
			if(!perm(f, parent, Pexec)){
				err = Eperm;
				break;
			}
			if(strcmp(name, "..") == 0){
				fram = parent;
				goto Found;
			}
			for(r=ram; r < &ram[nram]; r++)
				if(r->busy && r->parent==fram-ram && strcmp(name, r->name)==0){
					fram = r;
					goto Found;
				}
			break;
		}
		if(i==0 && err == nil)
			err = Enotexist;
	}
	if(nf != nil && (err!=nil || rhdr.nwqid<thdr.nwname)){
		/* clunk the new fid, which is the one we walked */
		f->busy = 0;
		f->rami = -1;
	}
	if(rhdr.nwqid > 0)
		err = nil;	/* didn't get everything in 9P2000 right! */
	if(rhdr.nwqid == thdr.nwname)	/* update the fid after a successful walk */
		f->rami = fram - ram;
	return err;
}

char *
ropen(Fid *f)
{
	Ram *r;
	int mode, trunc;

	if(f->open)
		return Eisopen;
	r = ram + f->rami;
	if(r->busy == 0)
		return Enotexist;
	if(r->perm & DMEXCL)
		if(r->open)
			return Excl;
	mode = thdr.mode;
	if(r->qid.type & QTDIR){
		if(mode != OREAD)
			return Eperm;
		rhdr.qid = r->qid;
		return 0;
	}
	if(mode & ORCLOSE){
		/* can't remove root; must be able to write parent */
		if(r->qid.path==0 || !perm(f, &ram[r->parent], Pwrite))
			return Eperm;
		f->rclose = 1;
	}
	trunc = mode & OTRUNC;
	mode &= OPERM;
	if(mode==OWRITE || mode==ORDWR || trunc)
		if(!perm(f, r, Pwrite))
			return Eperm;
	if(mode==OREAD || mode==ORDWR)
		if(!perm(f, r, Pread))
			return Eperm;
	if(mode==OEXEC)
		if(!perm(f, r, Pexec))
			return Eperm;
	if(trunc && (r->perm&DMAPPEND)==0){
		memsize -= r->ndata;
		oomlatch = 0;
		free(r->data);
		r->ndata = 0;
		r->data = 0;
		r->qid.vers++;
	}
	rhdr.qid = r->qid;
	rhdr.iounit = messagesize-IOHDRSZ;
	f->open = 1;
	r->open++;
	return 0;
}

int
moreram(void)
{
	Ram *r;

	r = realloc(ram, 2*aram*sizeof ram[0]);
	if(r == nil)
		return -1;
	ram = r;
	memset(ram+aram, 0, aram*sizeof ram[0]);
	aram *= 2;
	return 0;
}

char *
rcreate(Fid *f)
{
	int i;
	Ram *r, *fram;
	char *name;
	long parent, prm;

	if(f->open)
		return Eisopen;
	fram = ram + f->rami;
	if(fram->busy == 0)
		return Enotexist;
	parent = fram - ram;
	if((fram->qid.type&QTDIR) == 0)
		return Enotdir;
	/* must be able to write parent */
	if(!perm(f, fram, Pwrite))
		return Eperm;
	prm = thdr.perm;
	name = thdr.name;
	if(strcmp(name, ".")==0 || strcmp(name, "..")==0)
		return Ename;
	for(r=ram; r<&ram[nram]; r++)
		if(r->busy && parent==r->parent)
		if(strcmp((char*)name, r->name)==0)
			return Einuse;
	if(oomlatch)	/* sanity check */
		return Ememlimit;
	for(r=ram; r->busy; r++)
		if(r == &ram[aram-1]){
			i = aram-1;
			if(moreram() == -1)
				return Enomem;
			r = ram+i;
			fram = ram + f->rami;
		}
	r->busy = 1;
	r->qid.path = ++path;
	r->qid.vers = 0;
	if(prm & DMDIR)
		r->qid.type |= QTDIR;
	r->parent = parent;
	free(r->name);
	r->name = estrdup(name);
	r->user = f->user;
	r->group = fram->group;
	r->muid = fram->muid;
	if(prm & DMDIR)
		prm = (prm&~0777) | (fram->perm&prm&0777);
	else
		prm = (prm&(~0777|0111)) | (fram->perm&prm&0666);
	r->perm = prm;
	r->ndata = 0;
	if(r-ram >= nram)
		nram = r - ram + 1;
	r->atime = time(0);
	r->mtime = r->atime;
	fram->mtime = r->atime;
	f->rami = r-ram;
	rhdr.qid = r->qid;
	rhdr.iounit = messagesize-IOHDRSZ;
	f->open = 1;
	if(thdr.mode & ORCLOSE)
		f->rclose = 1;
	r->open++;
	return 0;
}

char*
rread(Fid *f)
{
	Ram *r, *fram;
	uchar *buf;
	vlong off;
	int n, m, cnt;

	fram = ram + f->rami;
	if(fram->busy == 0)
		return Enotexist;
	n = 0;
	rhdr.count = 0;
	if(thdr.offset < 0)
		return "negative seek offset";
	off = thdr.offset;
	buf = rdata;
	cnt = thdr.count;
	if(cnt > messagesize)	/* shouldn't happen, anyway */
		cnt = messagesize;
	if(cnt < 0)
		return "negative read count";
	if(fram->qid.type & QTDIR){
		for(r=ram+1; off > 0; r++){
			if(r->busy && r->parent==fram-ram)
				off -= ramstat(r, statbuf, sizeof statbuf);
			if(r == &ram[nram-1])
				return 0;
		}
		for(; r<&ram[nram] && n < cnt; r++){
			if(!r->busy || r->parent!=fram-ram)
				continue;
			m = ramstat(r, buf+n, cnt-n);
			if(m == 0)
				break;
			n += m;
		}
		rhdr.data = (char*)rdata;
		rhdr.count = n;
		return 0;
	}
	r = fram;
	if(off >= r->ndata)
		return 0;
	r->atime = time(0);
	n = cnt;
	if(off+n > r->ndata)
		n = r->ndata - off;
	rhdr.data = r->data+off;
	rhdr.count = n;
	return 0;
}

char*
rwrite(Fid *f)
{
	int cnt, d;
	uvlong off;
	void *p;
	Ram *r;

	r = ram + f->rami;
	if(r->busy == 0)
		return Enotexist;
	if(thdr.offset < 0)
		return "negative seek offset";
	off = thdr.offset;
	if(r->perm & DMAPPEND)
		off = r->ndata;
	cnt = thdr.count;
	if(cnt < 0)
		return "negative write count";
	if(r->qid.type & QTDIR)
		return Eisdir;
	if(off+cnt > r->ndata){
		d = off+cnt-r->ndata;
		if(memsize+d >= memlim){
			oomlatch = 1;		/* correct ±Maxfdata */
			return "write too big";
		}
		p = realloc(r->data, off+cnt);
		if(off+cnt>0 && p == nil)
			return Enomem;
		r->data = p;
		memsize += d;
	}
	if(off > r->ndata)
		memset(r->data+r->ndata, 0, off-r->ndata);
	if(off+cnt > r->ndata)
		r->ndata = off+cnt;
	memmove(r->data+off, thdr.data, cnt);
	r->qid.vers++;
	r->mtime = time(0);
	rhdr.count = cnt;
	return 0;
}

static int
emptydir(Ram *dr)
{
	long didx = dr - ram;
	Ram *r;

	for(r=ram; r<&ram[nram]; r++)
		if(r->busy && didx==r->parent)
			return 0;
	return 1;
}

char *
realremove(Ram *r)
{
	if(r->qid.type & QTDIR && !emptydir(r))
		return Enotempty;
	if(r->data){
		memsize -= r->ndata;
		oomlatch = 0;
		free(r->data);
	}
	r->ndata = 0;
	r->data = 0;
	r->parent = 0;
	memset(&r->qid, 0, sizeof r->qid);
	free(r->name);
	r->name = nil;
	r->busy = 0;
	return nil;
}

char *
rclunk(Fid *f)
{
	char *e = nil;
	Ram *r;

	r = ram + f->rami;
	if(f->open)
		r->open--;
	if(f->rclose)
		e = realremove(r);
	f->busy = 0;
	f->open = 0;
	f->rami = -1;
	return e;
}

char *
rremove(Fid *f)
{
	Ram *r;

	r = ram + f->rami;
	if(f->open)
		r->open--;
	f->busy = 0;
	f->open = 0;
	f->rami = -1;
	if(r->qid.path == 0 || !perm(f, &ram[r->parent], Pwrite))
		return Eperm;
	ram[r->parent].mtime = time(0);
	return realremove(r);
}

char *
rstat(Fid *f)
{
	Ram *r;

	r = ram + f->rami;
	if(r->busy == 0)
		return Enotexist;
	rhdr.nstat = ramstat(r, statbuf, sizeof statbuf);
	rhdr.stat = statbuf;
	return 0;
}

char *
rwstat(Fid *f)
{
	void *p;
	Ram *r, *s;
	Dir dir;

	r = ram + f->rami;
	if(r->busy == 0)
		return Enotexist;
	convM2D(thdr.stat, thdr.nstat, &dir, (char*)statbuf);

	/*
	 * To change length, must have write permission on file.
	 */
	if(dir.length!=~0 && dir.length!=r->ndata){
	 	if(!perm(f, r, Pwrite))
			return Eperm;
		if(r->ndata < dir.length)
		if(oomlatch || memsize+dir.length-r->ndata >= memlim)
			return Ememlimit;
	}

	/*
	 * to change mtime, ditto
	 */
	if(dir.mtime!=~0 && dir.mtime!=r->mtime){
	 	if(!perm(f, r, Pwrite))
			return Eperm;		/* Ewstato */
	}

	/*
	 * To change name, must have write permission in parent
	 * and name must be unique.
	 */
	if(dir.name[0]!='\0' && strcmp(dir.name, r->name)!=0){
	 	if(!perm(f, &ram[r->parent], Pwrite))
			return Eperm;
		for(s=ram; s<&ram[nram]; s++)
			if(s->busy && s->parent==r->parent)
			if(strcmp(dir.name, s->name)==0)
				return Eexist;
	}

	/*
	 * To change mode, must be owner or group leader.
	 * Because of lack of users file, leader=>group itself.
	 */
	if(dir.mode!=~0 && r->perm!=dir.mode){
		if(strcmp(f->user, r->user) != 0)
		if(strcmp(f->user, r->group) != 0)
			return Enotowner;
	}

	/*
	 * To change group, must be owner and member of new group,
	 * or leader of current group and leader of new group.
	 * Second case cannot happen, but we check anyway.
	 */
	if(dir.gid[0]!='\0' && strcmp(r->group, dir.gid)!=0){
		if(strcmp(f->user, r->user) == 0)
	//	if(strcmp(f->user, dir.gid) == 0)
			goto ok;
		if(strcmp(f->user, r->group) == 0)
		if(strcmp(f->user, dir.gid) == 0)
			goto ok;
		return Enotowner;
		ok:;
	}

	/* all ok; do it */
	if(dir.mode != ~0){
		dir.mode &= ~DMDIR;	/* cannot change dir bit */
		dir.mode |= r->perm&DMDIR;
		r->perm = dir.mode;
	}
	if(dir.name[0] != '\0'){
		free(r->name);
		r->name = estrdup(dir.name);
	}
	if(dir.gid[0] != '\0')
		r->group = estrdup(dir.gid);
	if(dir.length!=~0 && dir.length!=r->ndata){
		p = realloc(r->data, dir.length);
		if(dir.length > 0 && p == nil)
			return Enomem;
		r->data = p;
		memsize -= r->ndata;
		oomlatch = 0;
		memsize += dir.length;
		if(r->ndata < dir.length)
			memset(r->data+r->ndata, 0, dir.length-r->ndata);
		r->ndata = dir.length;
	}
	if(dir.mtime!=~0)
		r->mtime = dir.mtime;

	ram[r->parent].mtime = time(0);
	return 0;
}

uint
ramstat(Ram *r, uchar *buf, uint nbuf)
{
	int n;
	Dir dir;

	dir.name = r->name;
	dir.qid = r->qid;
	dir.mode = r->perm;
	dir.length = r->ndata;
	dir.uid = r->user;
	dir.gid = r->group;
	dir.muid = r->muid;
	dir.atime = r->atime;
	dir.mtime = r->mtime;
	n = convD2M(&dir, buf, nbuf);
	if(n > 2)
		return n;
	return 0;
}

Fid *
newfid(int fid)
{
	Fid *f, *ff;

	ff = 0;
	for(f = fids; f; f = f->next)
		if(f->fid == fid)
			return f;
		else if(!ff && !f->busy)
			ff = f;
	if(ff){
		ff->fid = fid;
		return ff;
	}
	f = malloc(sizeof *f);
	if(f == nil)
		return nil;
	memset(f, 0, sizeof *f);
	f->rami = -1;
	f->fid = fid;
	f->next = fids;
	fids = f;
	return f;
}

void
io(void)
{
	char *err, buf[40];
	int n, pid, ctl;
	Fid *fid;

	pid = getpid();
	if(private){
		snprint(buf, sizeof buf, "/proc/%d/ctl", pid);
		ctl = open(buf, OWRITE);
		if(ctl < 0){
			fprint(2, "can't protect ramfs\n");
		}else{
			fprint(ctl, "noswap\n");
			fprint(ctl, "private\n");
			close(ctl);
		}
	}

	for(;;){
		/*
		 * reading from a pipe or a network device
		 * will give an error after a few eof reads.
		 * however, we cannot tell the difference
		 * between a zero-length read and an interrupt
		 * on the processes writing to us,
		 * so we wait for the error.
		 */
		n = read9pmsg(mfd[0], mdata, messagesize);
		if(n < 0){
			rerrstr(buf, sizeof buf);
			if(buf[0]=='\0' || strstr(buf, "hungup"))
				exits("");
			error("mount read");
		}
		if(n == 0)
			continue;
		if(convM2S(mdata, n, &thdr) == 0)
			continue;

		if(debug)
			fprint(2, "ramfs %d:<-%F\n", pid, &thdr);

		if(thdr.type<0 || thdr.type>=nelem(fcalls) || !fcalls[thdr.type])
			err = "bad fcall type";
		else if((fid=newfid(thdr.fid))==nil)
			err = Ememlimit;
		else if(fid->rami == -1 && needfid[thdr.type])
			err = "fid not in use";
		else
			err = (*fcalls[thdr.type])(fid);
		if(err){
			rhdr.type = Rerror;
			rhdr.ename = err;
		}else{
			rhdr.type = thdr.type + 1;
			rhdr.fid = thdr.fid;
		}
		rhdr.tag = thdr.tag;
		if(debug)
			fprint(2, "ramfs %d:->%F\n", pid, &rhdr);/**/
		n = convS2M(&rhdr, mdata, messagesize);
		if(n == 0)
			error("convS2M error on write");
		if(write(mfd[1], mdata, n) != n)
			error("mount write");
	}
}

int
perm(Fid *f, Ram *r, int p)
{
	if((p*Pother) & r->perm)
		return 1;
	if(strcmp(f->user, r->group)==0 && ((p*Pgroup) & r->perm))
		return 1;
	if(strcmp(f->user, r->user)==0 && ((p*Powner) & r->perm))
		return 1;
	return 0;
}

void
error(char *s)
{
	fprint(2, "%s: %s: %r\n", argv0, s);
	exits(s);
}

char *
estrdup(char *q)
{
	char *p;
	int n;

	n = strlen(q)+1;
	p = malloc(n);
	if(!p)
		error("out of memory");
	memmove(p, q, n);
	return p;
}

void
usage(void)
{
	fprint(2, "usage: %s [-Dipsu] [-l memlimit] [-m mntpt] [-S srvname]\n", argv0);
	exits("usage");
}
