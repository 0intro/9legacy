#include <u.h>
#include <libc.h>
#include <ip.h>

#include "boot.h"

static	char	fsaddr[128];
static	char	mpoint[32];

static void getndbvar(char *name, char *dflt, char *prompt, char *addr, int n);

void
configip(int bargc, char **bargv, int needfs)
{
	Waitmsg *w;
	int argc, pid;
	char **arg, **argv, *p;

	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);
	fmtinstall('E', eipfmt);

	arg = malloc((bargc+1) * sizeof(char*));
	if(arg == nil)
		fatal("malloc");
	memmove(arg, bargv, bargc * sizeof(char*));
	arg[bargc] = 0;

	argc = bargc;
	argv = arg;
	strcpy(mpoint, "/net");
	ARGBEGIN {
	case 'x':
		p = ARGF();
		if(p != nil)
			snprint(mpoint, sizeof(mpoint), "/net%s", p);
		break;
	case 'g':
	case 'b':
	case 'h':
	case 'm':
		p = ARGF();
		USED(p);
		break;
	} ARGEND;

	/* bind in an ip interface or two */
	dprint("bind #I...");
	if(bind("#I", mpoint, MAFTER) < 0)
		fatal("bind #I");
	dprint("bind #l0...");
	if(access("#l0", 0) == 0 && bind("#l0", mpoint, MAFTER) < 0)
		warning("bind #l0");
	dprint("bind #l1...");
	if(access("#l1", 0) == 0 && bind("#l1", mpoint, MAFTER) < 0)
		warning("bind #l1");
	dprint("bind #l2...");
	if(access("#l2", 0) == 0 && bind("#l2", mpoint, MAFTER) < 0)
		warning("bind #l2");
	dprint("bind #l3...");
	if(access("#l3", 0) == 0 && bind("#l3", mpoint, MAFTER) < 0)
		warning("bind #l3");
	werrstr("");

	/* let ipconfig configure the first ip interface */
	switch(pid = fork()){
	case -1:
		fatal("fork configuring ip: %r");
	case 0:
		dprint("starting ipconfig...");
		exec("/boot/ipconfig", arg);
		fatal("execing /boot/ipconfig: %r");
	default:
		break;
	}

	/* wait for ipconfig to finish */
	dprint("waiting for dhcp...");
	for(;;){
		w = wait();
		if(w != nil && w->pid == pid){
			if(w->msg[0] != 0)
				fatal(w->msg);
			free(w);
			break;
		} else if(w == nil)
			fatal("configuring ip");
		free(w);
	}
	dprint("\n");

	if(needfs) {  /* if we didn't get a file and auth server, query user */
		char buf[128];

		getndbvar("fs", "564", "filesystem address", fsaddr, sizeof fsaddr);
		getndbvar("auth", "567", "authentication server address", buf, sizeof buf);
		authaddr = strdup(buf);
	}
}

void
configtcp(Method*)
{
	dprint("configip...");
	configip(bargc, bargv, 1);
}

int
connecttcp(void)
{
	int fd;

	dprint("dial %s...", fsaddr);
	fd = dial(fsaddr, 0, 0, 0);
	if (fd < 0)
		werrstr("dial %s: %r", fsaddr);
	return fd;
}

static char*
dialstring(char *s, char *dflt)
{
	static char addr[128];
	uchar ip[IPaddrlen];
	char *p;

	p = strchr(s, '!');
	if(p == nil){
		if(parseip(ip, s) == -1)
			return nil;
		snprint(addr, sizeof addr, "tcp!%s!%s", s, dflt);
	}else if(parseip(ip, s) != -1)
		snprint(addr, sizeof addr, "tcp!%s", s);
	else{
		if(parseip(ip, p+1) == -1)
			return nil;
		if(strchr(p+1, '!') == nil)
			snprint(addr, sizeof addr, "%s!%s", s, dflt);
		else
			snprint(addr, sizeof addr, "%s", s);
	}
	return addr;
}

static int
netenv(char *attr, char *buf, int n)
{
	int fd;
	char path[40];

	snprint(path, sizeof path, "#e/%s", attr);
	fd = open(path, OREAD);
	if(fd < 0)
		return 0;
	n = read(fd, buf, n-1);
	close(fd);
	if(n <= 0)
		return 0;
	buf[n] = 0;
	return 1;
}

static int
netndb(char *attr, char *val, int len)
{
	int fd, n, c;
	char buf[1024];
	char *p, *e;

	snprint(buf, sizeof(buf), "%s/ndb", mpoint);
	fd = open(buf, OREAD);
	if(fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if(n <= 0)
		return 0;
	buf[n] = 0;
	n = strlen(attr);
	for(p = buf; ; p++){
		p = strstr(p, attr);
		if(p == nil)
			break;
		c = *(p-1);
		if(*(p + n) == '=' && (p == buf || c == '\n' || c == ' ' || c == '\t')){
			p += n+1;
			for(e = p; *e != 0 && *e != '\n' && *e != ' ' && *e != '\t'; e++)
				;
			*e = 0;
			strecpy(val, val+len, p);
			return 1;
		}
	}
	return 0;
}

static void
getndbvar(char *name, char *dflt, char *prompt, char *addr, int n)
{
	char buf[128], *a;

	if(!netndb(name, buf, sizeof buf) && !netenv(name, buf, sizeof buf))
		buf[0] = 0;
	for(;;){
		if(buf[0] != 0){
			a = dialstring(buf, dflt);
			if(a != nil){
				strecpy(addr, addr+n, a);
				return;
			}
			fprint(2, "configip: can't parse %s address %s\n", name, buf);
		}
		buf[0] = 0;
		outin(prompt, buf, sizeof buf);
	}
}
