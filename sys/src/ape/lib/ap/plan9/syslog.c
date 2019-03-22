#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

static struct {
	const char *ident;
	char hostname[128];
	int option;
	int facility;
	int fd;
	int consfd;
} flags = {
	.fd = -1,
	.consfd = -1,
};

void
openlog(const char *ident, int option, int facility)
{
	char buf[1024];
	int fd;

	closelog();
	if(gethostname(flags.hostname, sizeof(flags.hostname)) < 0)
		return;
	if(ident){
		snprintf(buf, sizeof(buf), "/sys/log/%s", ident);
		fd = open(buf, O_WRONLY);
		if(fd < 0)
			return;
		if(fcntl(fd, F_SETFD, FD_CLOEXEC) < 0){
			close(fd);
			return;
		}
		flags.fd = fd;
	}
	if(option&LOG_CONS){
		fd = open("/dev/cons", O_WRONLY);
		if(fd < 0){
			closelog();
			return;
		}
		if(fcntl(fd, F_SETFD, FD_CLOEXEC) < 0){
			closelog();
			return;
		}
		flags.consfd = fd;
	}
	flags.ident = ident;
	flags.option = option;
	flags.facility = facility;
}

void
syslog(int priority, const char *format, ...)
{
	char buf[128], *s, *p;
	time_t t;
	int n;
	va_list arg;

	/* syslog => Mar 10 01:45:50 $hostname $prog[$pid]: $msg */
	/* plan9  => $hostname Mar 10 01:45:50 $msg */

	USED(priority);

	/* TODO: lock */
	t = time(NULL);
	s = ctime(&t);
	p = buf + snprintf(buf, sizeof(buf)-1, "%s ", flags.hostname);
	strncpy(p, s+4, 15);
	p += 15;
	*p++ = ' ';
	va_start(arg, format);
	p += vsnprintf(p, buf+sizeof(buf)-p-1, format, arg);
	va_end(arg);
	*p++ = '\n';
	n = p - buf;
	if(flags.fd >= 0){
		lseek(flags.fd, 0, 2);
		write(flags.fd, buf, n);
	}
	if(flags.consfd >= 0)
		write(flags.consfd, buf, n);
}

void
closelog(void)
{
	flags.ident = NULL;
	flags.hostname[0] = '\0';
	flags.option = 0;
	flags.facility = 0;
	if(flags.fd >= 0)
		close(flags.fd);
	flags.fd = -1;
	if(flags.consfd >= 0)
		close(flags.consfd);
	flags.consfd = -1;
}
