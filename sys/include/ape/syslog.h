#ifndef __SYSLOG_H
#define __SYSLOG_H
#pragma lib "/$M/lib/ape/libap.a"

enum {
	/* facility */
	LOG_KERN	= 0<<3,
	LOG_USER	= 1<<3,
	LOG_DAEMON	= 3<<3,
	LOG_AUTH	= 4<<3,
	LOG_SYSLOG	= 5<<3,
	LOG_CRON	= 9<<3,
	LOG_AUTHPRIV= 10<<3,
	LOG_LOCAL0	= 16<<3,
	LOG_LOCAL1	= 17<<3,
	LOG_LOCAL2	= 18<<3,
	LOG_LOCAL3	= 19<<3,
	LOG_LOCAL4	= 20<<3,
	LOG_LOCAL5	= 21<<3,
	LOG_LOCAL6	= 22<<3,
	LOG_LOCAL7	= 23<<3,
};

enum {
	/* priority */
	LOG_EMERG,
	LOG_ALERT,
	LOG_CRIT,
	LOG_ERR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG,
};

enum {
	/* option */
	LOG_PID		= 0x01,
	LOG_CONS	= 0x02,
	LOG_ODELAY	= 0x04,
	LOG_NDELAY	= 0x08,
	LOG_NOWAIT	= 0x10,
};

#ifdef __cplusplus
extern "C" {
#endif

extern void openlog(const char *, int, int);
extern void syslog(int, const char *, ...);
extern void closelog(void);

#ifdef __cplusplus
}
#endif

#endif
