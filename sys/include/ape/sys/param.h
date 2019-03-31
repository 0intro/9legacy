#ifndef __PARAM_H__
#define __PARAM_H__

#ifndef _BSD_EXTENSION
    This header file is an extension to ANSI/POSIX
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NOFILES_MAX 100
#define MIN(a, b)	(((a)<(b)) ? (a) : (b))
#define MAX(a, b)	(((a)>(b)) ? (a) : (b))

#ifdef __cplusplus
}
#endif

#endif /* !__PARAM_H__ */
