# PS2 Master Disc Boot Patcher

[![Build](https://github.com/bucanero/ps2-master-patcher/actions/workflows/build.yml/badge.svg)](https://github.com/bucanero/ps2-master-patcher/actions/workflows/build.yml)

A command line tool that adds the PlayStation 2 **boot sectors** to a disc image:
the encrypted PS2 logo and the **master disc** sectors. A PS2 disc image made
with a generic tool (`mkisofs`, `xorriso`, ImgBurn...) leaves these sectors
empty; this tool fills them in, so the image is laid out like a PS2 master
disc.

Typical use: making a bootable disc for PS2 homebrew. For example,
[Apollo Save Tool PS2](https://github.com/bucanero/apollo-ps2) builds its disc
image with it.

## What it does

The first 16 sectors of a PS2 disc (the "boot sectors", before the ISO9660
volume descriptor at sector 16) hold:

| Sectors | Contents | What the patcher does |
|---------|----------|-----------------------|
| 0 - 11  | PS2 logo, encrypted with a key derived from the disc ID | If they're empty, writes the NTSC or PAL logo, encrypted for the disc ID. If there's already a logo, keeps it and checks it decrypts with the disc ID. |
| 12 - 13 | empty | left alone |
| 14 - 15 | master disc sector, written twice | writes it (the old contents are saved to a backup file first) |

The master disc sector holds the disc ID, region, disc type (CD or DVD), the
image size (DVD), three "magic numbers" derived from the disc ID, and a
`CDVDGEN` version string. The format is documented in
[ps2boot.txt](ps2boot.txt) (by loser).

> [!NOTE]
> ps2boot.txt places the master disc sectors at 12 - 13. The patcher writes
> them at 14 - 15.

### The disc ID

Everything is derived from the disc ID, which the patcher takes from the
`SYSTEM.CNF` boot line in the image:

```
BOOT2 = cdrom0:\SLUS_123.45;1
VER = 1.00
VMODE = NTSC
```

- The boot file name must be a disc ID: **4 capital letters, an underscore and
  5 digits as `NNN.NN`**. `SLUS_123.45` is disc `SLUS-12345`. A name like
  `cdrom0:\MYAPP.ELF;1` won't work: rename the ELF on the disc instead.
- `VMODE = PAL` selects the PAL logo; anything else uses the NTSC logo.

### Image types

| Image | Detected by | Notes |
|-------|-------------|-------|
| DVD `.iso` | size is a multiple of 2048 bytes | disc type DVD; the sector count is written to the master disc sector |
| CD `.bin`  | size is a multiple of 2352 bytes (raw sectors) | disc type CD; only Mode 2 Form 1 sectors are supported. The EDC/ECC of every sector it changes is recomputed. |

A 2048-byte `.iso` is always patched as a DVD, whatever its size: to burn a
CD-R, use a raw `.bin`/`.cue` image.

## Usage

```
ps2-master-patcher <image.iso | image.bin> [region]
```

- `region`: `J` (Japan), `U` (USA), `E` (Europe) or `W` (World). Optional,
  the default is USA. Japan uses its own sector layout (see ps2boot.txt).

> [!IMPORTANT]
> The image is **modified in place**. The two sectors it replaces (14 - 15)
> are saved to `DVD_SECTORS.BIN` or `CD_SECTORS.BIN` in the current
> directory; keep a copy of the image if you need the original.

Example, building a disc image with `xorriso` and patching it:

```sh
mkdir disc
cp myapp.elf disc/SLUS_123.45
printf 'BOOT2 = cdrom0:\\SLUS_123.45;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > disc/SYSTEM.CNF
xorriso -as mkisofs -iso-level 3 -l -V MYAPP -o myapp.iso disc

ps2-master-patcher myapp.iso W
```

```
	PlayStation 2 Master Disc Boot Patcher by Bucanero

[i] Forcing World region
[i] Reading 'myapp.iso'...
    + Image size: 677888 bytes
    + Detected DVD-ROM Image
[i] Searching for Disc ID in the image...
    + Found SYSTEM.CNF data at offset 0x5A000
    + Detected Disc ID: SLUS-12345 (NTSC)
[!] Disc image has an empty boot sector.
    + Adding Encrypted PS2 logo (NTSC) to boot sector...
[i] Encrypted PS2 logo matches SLUS-12345
[i] Backing up disc sectors...
    + DVD_SECTORS.BIN saved OK!
[i] Writing master disc sectors...
    + Master disc sectors written to 'myapp.iso'
```

The exit code is 0 on success and non-zero if the image couldn't be patched
(not a CD/DVD image, no disc ID found, write error).

## Download

Builds for Linux (x64), macOS (Apple Silicon and Intel) and Windows (x64 and
32-bit x86) are attached to
each [release](https://github.com/bucanero/ps2-master-patcher/releases), and to
every [CI run](https://github.com/bucanero/ps2-master-patcher/actions/workflows/build.yml)
as artifacts.

## Building

Any C99 compiler; no dependencies.

```sh
make
```

- macOS universal binary: `make CFLAGS="-Wall -Wextra -O2 -arch arm64 -arch x86_64"`
- Windows (MinGW-w64, or MSYS2): `make CC=x86_64-w64-mingw32-gcc TARGET_PS2MDBP=ps2-master-patcher.exe`
  (32-bit: `CC=i686-w64-mingw32-gcc`)
- Install to `/usr/local/bin`: `make install`

## Credits

- Boot sector and master disc format, logo encryption and magic numbers:
  **loser**, [ps2boot.txt](ps2boot.txt) (2002), via
  [mlafeldt/ps2logo](https://github.com/mlafeldt/ps2logo).
- CD-ROM EDC/ECC regeneration: based on
  [PSXtract](https://github.com/xdotnano/PSXtract) by Daniel Huguenin
  (`cdrom.h` by Hykem).
- LZARI decompression (packed logos): Haruhiko Okumura, memory-to-memory
  version by Luigi Auriemma.

## Tests

`tests/smoke_test.py` builds small fake DVD and CD images, patches them and
checks the result independently (logo, magic numbers, master disc sector, CD
EDC). It also patches a sparse DVD image larger than 2 GB, to check file
offsets past 2 GB work (on Windows, and in 32-bit builds). CI runs it on every
platform.

```sh
python3 tests/smoke_test.py ./ps2-master-patcher
```

## License

[GPL-3.0](LICENSE)
