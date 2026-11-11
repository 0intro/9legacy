/*
 * generate a list of files and their metadata
 * using a given proto file.
 */
#include "all.h"

int changesonly;
char *uid;
Db *db;
Biobuf blog;
ulong now;
int n;
char **x;
int nx;
int justlog;
char *root=".";
char **match;
int nmatch;

int
ismatch(char *s)
{
	int i, len;

	if(nmatch == 0)
		return 1;
	for(i=0; i<nmatch; i++){
		if(strcmp(s, match[i]) == 0)
			return 1;
		len = strlen(match[i]);
		if(strncmp(s, match[i], len) == 0 && s[len]=='/')
			return 1;
	}
	return 0;
}

void
xlog(int c, char *name, Dir *d, char *hash)
{
	char *dname;

	dname = d->name;
	if(strcmp(dname, name) == 0)
		dname = "-";
	if(!justlog)
		Bprint(&blog, "%lud %d ", now, n++);
	Bprint(&blog, "%c %q %q %luo %q %q %lud %lld %s\n",
		c, name, dname, d->mode, uid ? uid : d->uid, d->gid, d->mtime, d->length, hash);
}

void
walk(char *new, char *old, Dir *xd, void*)
{
	int i, change, len;
	Dir od, d;
	char *path, *h, hbuf[2*SHA1dlen+1];

	path = old;			/* full on-disk path, before unroot */
	new = unroot(new, "/");
	old = unroot(old, root);

	if(!ismatch(new))
		return;
	for(i=0; i<nx; i++){
		if(strcmp(new, x[i]) == 0)
			return;
		len = strlen(x[i]);
		if(strncmp(new, x[i], len)==0 && new[len]=='/')
			return;
	}

	d = *xd;
	d.name = old;
	memset(&od, 0, sizeof od);
	change = 0;
	h = "-";
	if(markdb(db, new, &od) < 0){
		if(!changesonly){
			if((d.mode&DMDIR)==0 && hashfile(path, hbuf)!=nil)
				h = hbuf;
			xlog('a', new, &d, h);
			change = 1;
		}
	}else{
		h = dbhash(db, new);	/* keep the stored hash unless content changed */
		if((d.mode&DMDIR)==0 && (od.mtime!=d.mtime || od.length!=d.length)){
			if(hashfile(path, hbuf) == nil){
				h = "-";	/* can't hash: fall back to mtime|length */
				xlog('c', new, &d, h);
				change = 1;
			}else if(strcmp(hbuf, h) == 0){
				h = hbuf;	/* churn only: refresh the db, no log entry */
				change = 1;
			}else{
				h = hbuf;
				xlog('c', new, &d, h);
				change = 1;
			}
		}
		if((!uid&&strcmp(od.uid,d.uid)!=0)
		|| strcmp(od.gid,d.gid)!=0
		|| od.mode!=d.mode){
			xlog('m', new, &d, "-");
			change = 1;
		}
	}
	if(!justlog && change){
		if(uid)
			d.uid = uid;
		d.muid = "mark";	/* mark bit */
		insertdbh(db, new, &d, h);
	}
}

void
warn(char *msg, void*)
{
	char *p;

	fprint(2, "warning: %s\n", msg);

	/* find the %r in "can't open foo: %r" */
	p = strstr(msg, ": ");
	if(p)
		p += 2;

	/*
	 * if the error is about a remote server failing,
	 * then there's no point in continuing to look
	 * for changes -- we'll think everything got deleted!
	 *
	 * actual errors i see are:
	 *	"i/o on hungup channel" for a local hangup
	 *	"i/o on hungup channel" for a timeout (yank the network wire)
	 *	"'/n/sources/plan9' Hangup" for a remote hangup
	 * the rest is paranoia.
	 */
	if(p){
		if(cistrstr(p, "hungup") || cistrstr(p, "Hangup")
		|| cistrstr(p, "rpc error")
		|| cistrstr(p, "shut down")
		|| cistrstr(p, "i/o")
		|| cistrstr(p, "connection"))
			sysfatal("suspected network or i/o error - bailing out");
	}
}

void
usage(void)
{
	fprint(2, "usage: replica/updatedb [-c] [-p proto] [-r root] [-t now n] [-u uid] [-x path]... db [paths]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *proto;
	Avlwalk *w;
	Dir d;
	Entry *e;

	quotefmtinstall();
	proto = "/sys/lib/sysconfig/proto/allproto";
	now = time(0);
	Binit(&blog, 1, OWRITE);
	ARGBEGIN{
	case 'c':
		changesonly = 1;
		break;
	case 'l':
		justlog = 1;
		break;
	case 'p':
		proto = EARGF(usage());
		break;
	case 'r':
		root = EARGF(usage());
		break;
	case 't':
		now = strtoul(EARGF(usage()), 0, 0);
		n = atoi(EARGF(usage()));
		break;
	case 'u':
		uid = EARGF(usage());
		break;
	case 'x':
		if(nx%16 == 0)
			x = erealloc(x, (nx+16)*sizeof(x[0]));
		x[nx++] = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc <1)
		usage();

	match = argv+1;
	nmatch = argc-1;

	db = opendb(argv[0]);
	if(rdproto(proto, root, walk, warn, nil) < 0)
		sysfatal("rdproto: %r");

	if(!changesonly){
		w = avlwalk(db->avl);
		while(e = (Entry*)avlprev(w)){
			if(!ismatch(e->name))
				continue;
			if(!e->d.mark){		/* not visited during walk */
				memset(&d, 0, sizeof d);
				d.name = e->d.name;
				d.uid = e->d.uid;
				d.gid = e->d.gid;
				d.mtime = e->d.mtime;
				d.mode = e->d.mode;
				xlog('d', e->name, &d, "-");
				if(!justlog)
					removedb(db, e->name);
			}
		}
	}

	if(Bterm(&blog) < 0)
		sysfatal("writing output: %r");

	exits(nil);
}

