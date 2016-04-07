#include <u.h>
#include <libc.h>
#include <ip.h>

#include "boot.h"

static	char*	fsaddr;
static	char	mpoint[32];

static int isvalidip(uchar*);
static void netndb(char*, uchar*);
static void netenv(char*, uchar*);
static char* queryaddr(char*, char*, char*, char*);


void
configip(int bargc, char **bargv, char *fsproto)
{
	Waitmsg *w;
	int argc, pid;
	char **arg, **argv, buf[32], buf1[32], *p;

	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);
	fmtinstall('E', eipfmt);

	arg = malloc((bargc+1) * sizeof(char*));
	if(arg == nil)
		fatal("malloc");
	memmove(arg, bargv, bargc * sizeof(char*));
	arg[bargc] = 0;

print("ipconfig...");
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

	/* bind in an ip interface */
	if(bind("#I", mpoint, MAFTER) < 0)
		fatal("bind #I\n");
	if(access("#l0", 0) == 0 && bind("#l0", mpoint, MAFTER) < 0)
		print("bind #l0: %r\n");
	if(access("#l1", 0) == 0 && bind("#l1", mpoint, MAFTER) < 0)
		print("bind #l1: %r\n");
	if(access("#l2", 0) == 0 && bind("#l2", mpoint, MAFTER) < 0)
		print("bind #l2: %r\n");
	if(access("#l3", 0) == 0 && bind("#l3", mpoint, MAFTER) < 0)
		print("bind #l3: %r\n");
	bind("#©", "/dev", MBEFORE);
	writefile("/dev/cecctl", "cecon #l0/ether0", 16);
	bind("#æ", "/dev", MAFTER);
	werrstr("");

	/* let ipconfig configure the ip interface */
	switch(pid = fork()){
	case -1:
		fatal("fork configuring ip");
	case 0:
		exec("/boot/ipconfig", arg);
		fatal("execing /ipconfig");
	default:
		break;
	}

	/* wait for ipconfig to finish */
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

	readfile("#c/sysname", buf, sizeof buf);
	if(buf[0] != 0){
		snprint(buf1, sizeof buf1, "name %s\n", buf);
		writefile("#©/cecctl", buf1, sizeof buf1);
	}

	if(fsproto == nil)
		return;

	/* if we didn't get a file and auth server, query user */
	fsaddr = queryaddr("fs", fsproto, strcmp(fsproto,"tcp")==0? "564": "17008", "filesystem IP address");
	authaddr = queryaddr("auth", "tcp", "567", "authentication server IP address");
}

static char*
queryaddr(char *key, char *proto, char *svc, char *what)
{
	uchar ipa[IPaddrlen];
	char buf[64], *p;

	netndb(key, ipa);
	if(!isvalidip(ipa))
		netenv(key, ipa);
	while(!isvalidip(ipa)){
		buf[0] = 0;
		outin(what, buf, sizeof(buf));
		p = strchr(buf, '!');
		if(p != nil)
			return strdup(netmkaddr(buf, proto, svc));
		if(parseip(ipa, buf) == -1)
			fprint(2, "configip: can't parse %s %s\n", what, buf);
	}
	return smprint("%s!%I!%s", proto, ipa, svc);
}

void
configtcp(Method*)
{
	configip(bargc, bargv, "tcp");
}

int
connecttcp(void)
{
	int fd;

	fd = dial(fsaddr, 0, 0, 0);
	if (fd < 0)
		werrstr("dial %s: %r", fsaddr);
	return fd;
}

void
configil(Method*)
{
	configip(bargc, bargv, "il");
}

int
connectil(void)
{
	int fd;

	fd = dial(fsaddr, 0, 0, 0);
	if(fd < 0)
		werrstr("dial %s: %r", fsaddr);
	return fd;
}

static int
isvalidip(uchar *ip)
{
	if(ipcmp(ip, IPnoaddr) == 0)
		return 0;
	if(ipcmp(ip, v4prefix) == 0)
		return 0;
	return 1;
}

static void
netenv(char *attr, uchar *ip)
{
	int fd, n;
	char buf[128];

	ipmove(ip, IPnoaddr);
	snprint(buf, sizeof(buf), "#e/%s", attr);
	fd = open(buf, OREAD);
	if(fd < 0)
		return;

	n = read(fd, buf, sizeof(buf)-1);
	if(n <= 0)
		return;
	buf[n] = 0;
	if (parseip(ip, buf) == -1)
		fprint(2, "netenv: can't parse ip %s\n", buf);
}

static void
netndb(char *attr, uchar *ip)
{
	int fd, n, c;
	char buf[1024];
	char *p;

	ipmove(ip, IPnoaddr);
	snprint(buf, sizeof(buf), "%s/ndb", mpoint);
	fd = open(buf, OREAD);
	if(fd < 0)
		return;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if(n <= 0)
		return;
	buf[n] = 0;
	n = strlen(attr);
	for(p = buf; ; p++){
		p = strstr(p, attr);
		if(p == nil)
			break;
		c = *(p-1);
		if(*(p + n) == '=' && (p == buf || c == '\n' || c == ' ' || c == '\t')){
			p += n+1;
			if (parseip(ip, p) == -1)
				fprint(2, "netndb: can't parse ip %s\n", p);
			return;
		}
	}
}
