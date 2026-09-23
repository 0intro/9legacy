#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

/*
 * Boot the root file system over the virtio-9p transport (#9), as served
 * by qemu's -device virtio-9p-pci / -fsdev local. Selected with
 * nobootprompt=virtio9p (or bootargs=virtio9p) in plan9.ini. There is
 * nothing to configure (no network), so connect just opens the device;
 * boot.c then does the 9P version handshake and mounts it as the root.
 */

void
configvirtio9p(Method*)
{
}

int
connectvirtio9p(void)
{
	int fd;

	dprint("open #9/0...");
	fd = open("#9/0", ORDWR);
	if(fd < 0)
		werrstr("can't open #9/0: %r");
	return fd;
}
