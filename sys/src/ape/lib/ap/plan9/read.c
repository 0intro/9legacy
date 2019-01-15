#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include "lib.h"
#include "sys9.h"

#include <stdio.h>

ssize_t
pread(int d, void *buf, size_t nbytes, off_t offset)
{
	int n, noblock, isbuf;
	Fdinfo *f;

	if(d<0 || d>=OPEN_MAX || !(_fdinfo[d].flags&FD_ISOPEN)){
		errno = EBADF;
		return -1;
	}
	if(nbytes <= 0)
		return 0;
	if(buf == 0){
		errno = EFAULT;
		return -1;
	}
	f = &_fdinfo[d];
	noblock = f->oflags&O_NONBLOCK;
	isbuf = f->flags&(FD_BUFFERED|FD_BUFFEREDX);
	if(noblock || isbuf){
		if(f->flags&FD_BUFFEREDX) {
			errno = EIO;
			return -1;
		}
		if(!isbuf) {
			if(_startbuf(d) != 0) {
				errno = EIO;
				return -1;
			}
		}
		n = _readbuf(d, buf, nbytes, noblock);
	}else{
		n = _PREAD(d,  buf, nbytes, offset);
		if(n < 0)
			_syserrno();
	}
	return n;
}

ssize_t
read(int d, void *buf, size_t nbytes)
{
	return pread(d, buf, nbytes, -1LL);
}
