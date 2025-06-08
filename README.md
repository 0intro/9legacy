9legacy
=======

[9legacy](http://9legacy.org/) is a continuation of [Plan 9 from Bell Labs](https://9p.io/plan9).

# Usage

## Run Plan 9 on QEMU

1. Build empty directory hierarchy

Empty directories can't be committed to a Git repository.

The following script will build the empty directory hierachy, identically
to the content of the original Plan 9 CD image.

Run:

```
./boot/mkdirs
```

2. Install binaries

This step requires the [9660srv](https://github.com/9fans/plan9port/tree/master/src/cmd/9660srv)
command from [plan9port](https://github.com/9fans/plan9port),
so the CD image will be mounted using the Plan 9 ISO 9660 extension.

The following script will download the last 9legacy CD image and extract
the 386 and amd64 binaries to the root of the Git repository.

Run:

```
./boot/getbin
```

3. Run QEMU

The following script will build u9fs from [sys/src/cmd/unix/u9fs](https://github.com/0intro/9legacy/tree/main/sys/src/cmd/unix/u9fs),
if needed, then run QEMU.

Run the following script to build u9fs and run QEMU:

```
./boot/qemu
```

## Rebuild boot/pxeboot.raw

This Git repository already includes a prebuilt `boot/pxeboot.raw` boot loader.

This step is necessary only if you want to rebuild `boot/pxeboot.raw`, for example,
after making changes to the boot loader.

1. Reduce the size of 9bootpbs

Apply the following patch to reduce the size of 9bootpbs:

```
patch -p1 boot/9-pcboot-boot.diff
```

2. Rebuild 9bootpbs

On Plan 9, run:

```
mkbootpbs
```

3. Rebuild pxeboot.raw

On Plan 9, run:

```
mkpxeboot
```

# Thanks

We thank Russ Cox, who made the scripts to easily boot a Plan 9 file hierarchy on QEMU.
