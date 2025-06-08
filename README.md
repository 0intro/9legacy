9legacy
=======

[![CI](https://github.com/0intro/9legacy/actions/workflows/ci.yml/badge.svg)](https://github.com/0intro/9legacy/actions/workflows/ci.yml)

[9legacy](http://9legacy.org/) is a continuation of [Plan 9 from Bell Labs](https://9p.io/plan9).

# 1. Usage

`boot/qemu` runs Plan 9 under QEMU with this tree as the guest's root file
system. An edit on the host is an edit in the running system. By default it
serves the tree over a virtio-9p device and boots an ELF kernel with multiboot.
`-9fat` boots the virtio-9p root from a 9fat menu disk instead. `-u9fs` serves
the tree with u9fs over 9P-over-TCP and boots `boot/pxeboot.raw` over tftp
through the PXE loader. That last path is slower, but doesn't depend on the
multiboot ELF or the virtio-9p driver.

Quit QEMU with `Ctrl-a x`.

## 1.1. Prepare the tree

Do this once before the first boot.

1. Build the empty directories

Git won't store an empty directory. `boot/mkdirs` creates the ones Plan 9
expects, matching the original CD image.

```
./boot/mkdirs
```

2. Install the binaries

`boot/getbin` downloads the latest 9legacy CD image and copies the 386 and
amd64 binaries, libraries and kernels into the tree. It reads the image with
`9660srv` and `9p` from [plan9port](https://github.com/9fans/plan9port), which
must be installed.

```
./boot/getbin
```

## 1.2. virtio-9p mode

With no arguments `boot/qemu` boots the 386 terminal kernel over virtio-9p, on
the serial console, with four CPUs and QEMU's slirp networking.

```
./boot/qemu                 # 386 terminal (default)
./boot/qemu -amd64          # amd64 terminal
./boot/qemu -cpu            # 386 cpu server
./boot/qemu -amd64 -cpu     # amd64 cpu server
./boot/qemu -smp 8          # eight CPUs
```

### 1.2.1. Graphics

`-vga` opens a window and runs rio at 1920x1080. Change the resolution with
`vgasize` at the top of `boot/qemu`.

```
./boot/qemu -vga
./boot/qemu -amd64 -vga
```

### 1.2.2. Boot from the 9fat disk

`boot/qemu -9fat` boots a small 9fat disk that holds every kernel behind a boot
menu. Build the disk once inside a guest with `mk9fat`, then pick a kernel at
the `Selection:` prompt.

```
mk9fat                      # in the guest, writes boot/9fat.raw
./boot/qemu -9fat
```

## 1.3. u9fs mode

`boot/qemu -u9fs` is the original mode. It builds u9fs from
[sys/src/cmd/unix/u9fs](https://github.com/0intro/9legacy/tree/main/sys/src/cmd/unix/u9fs),
boots `boot/pxeboot.raw` over tftp, and serves the tree with u9fs over
9P-over-TCP. It boots the stock kernel through the PXE loader.

```
./boot/qemu -u9fs
```

## 1.4. Rebuild the boot images

`boot/pxeboot.raw`, the PXE loader for `-u9fs`, is committed. `boot/9fat.raw`,
the menu disk for `-9fat`, is built on demand. `boot/regen` rebuilds both from
the host. It boots a guest and runs `mkbootpbs`, `mkpxeboot` and `mk9fat`.

```
./boot/regen
```

To rebuild `boot/pxeboot.raw` by hand, shrink 9bootpbs and build the loader in a
guest. The boot ramdisk shadows `/boot`, so the diff is read from `/root/boot`:

```
cd / && ape/patch -p1 < /root/boot/9-pcboot-boot.diff
mkbootpbs
mkpxeboot   # writes boot/pxeboot.raw
```

Test the new loader with `./boot/qemu -u9fs`.

To rebuild `boot/9fat.raw` by hand, run `mk9fat` in a guest. It lays 9load,
every installed kernel and a plan9.ini boot menu onto the disk, with the root
over virtio-9p.

```
mk9fat
```

# 2. Test

`boot/test` drives a guest through a CI task and reports on the serial console.

```
boot/test build           # boot 386, build every architecture
boot/test smoke -386      # boot 386, compile and run the smoke test
boot/test smoke -amd64    # boot amd64, compile and run the smoke test
```

The smoke test is `usr/glenda/src/smoke/smoke.c`. It checks the compiler and
library, then stresses the allocator, processes, pipes, the file system and the
network. GitHub Actions runs all three on every push and pull request. See
`.github/workflows/ci.yml`.

# 3. Thanks

We thank Russ Cox, who made the original scripts to easily boot a Plan 9 file
hierarchy on QEMU.
