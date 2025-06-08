9legacy
=======

[9legacy](http://9legacy.org/) is a continuation of [Plan 9 from Bell Labs](https://9p.io/plan9).

# Usage

## Run Plan 9 on QEMU

1. Build empty directory hierarchy

Build empty directories, which couldn't be committed to the Git repository:

```
./boot/mkdirs
```

2. Install binaries

This step requires the [9660srv](https://github.com/9fans/plan9port/tree/master/src/cmd/9660srv) command from [plan9port](https://github.com/9fans/plan9port).

Install 386 and amd64 binaries from latest 9legacy CD image:

```
./boot/getbin
```

3. Run QEMU

Run the following script to build u9fs and run QEMU:

```
./boot/qemu
```

## Rebuild boot/pxeboot.raw

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
