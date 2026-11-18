#!/usr/bin/env python3
"""
Smoke test for ps2-master-patcher: builds small fake DVD (.iso) and CD (.bin)
images with a SYSTEM.CNF sector, runs the patcher on them and checks the
result independently, following ps2boot.txt (logo encryption, magic numbers,
master disc sector) and the CD-ROM EDC.

usage: smoke_test.py <path to ps2-master-patcher binary>
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

PATCHER = os.path.abspath(sys.argv[1])
LOGO_CRC = {'NTSC': 0x9F1AEE24, 'PAL': 0x87B50222}
REGIONS = {None: 0x02, 'U': 0x02, 'E': 0x04, 'W': 0x07, 'J': 0x01}
failures = 0


def check(cond, msg):
    global failures
    print(('  ok   ' if cond else '  FAIL ') + msg)
    if not cond:
        failures += 1


def magic_numbers(letters, number):
    L = (ord(letters[3]) << 0) | (ord(letters[2]) << 7) | (ord(letters[1]) << 14) | (ord(letters[0]) << 21)
    m1 = (((number & 0x1F) << 3) | ((0x0FFFFFFF & L) >> 25)) & 0xFF
    m2 = ((number >> 10) | ((0x0FFFFFFF & L) << 7)) & 0xFFFFFFFF
    m3 = (((number & 0x3E0) >> 2) | 0x04) & 0xFF
    return m1, m2, m3


def system_cnf(boot, vmode):
    return ('BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = %s\r\n' % (boot, vmode)).encode()


# CD-ROM EDC: CRC-32 with the reflected polynomial 0xD8018001
EDC_TABLE = []
for i in range(256):
    e = i
    for _ in range(8):
        e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
    EDC_TABLE.append(e)


def edc(data):
    e = 0
    for b in data:
        e = (e >> 8) ^ EDC_TABLE[(e ^ b) & 0xFF]
    return e


def bcd(n):
    return ((n // 10) << 4) | (n % 10)


def cd_sector(lba, user):
    """Raw Mode 2 Form 1 sector (2352 bytes); EDC/ECC left zero."""
    lba += 150
    header = bytes([bcd(lba // 4500), bcd((lba // 75) % 60), bcd(lba % 75), 2])
    sync = b'\x00' + b'\xff' * 10 + b'\x00'
    sub = b'\x00\x00\x08\x00' * 2
    return sync + header + sub + user.ljust(2048, b'\0') + b'\0' * (4 + 276)


def run_patcher(image, region):
    args = [PATCHER, image] + ([region] if region else [])
    p = subprocess.run(args, cwd=os.path.dirname(image), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def check_boot_sectors(boot, letters, number, vmode, region, disc_type, image_sectors):
    m1, m2, m3 = magic_numbers(letters, number)
    raw = boot[:12 * 2048]
    logo = bytes((((b ^ m1) >> 5) | ((b ^ m1) << 3)) & 0xFF for b in raw)
    check(zlib.crc32(logo) == LOGO_CRC[vmode], 'sectors 0-11: %s logo encrypted for %s-%05d' % (vmode, letters, number))
    check(not any(boot[12 * 2048:14 * 2048]), 'sectors 12-13: empty')
    s = boot[14 * 2048:15 * 2048]
    check(s == boot[15 * 2048:16 * 2048], 'sectors 14-15: two identical copies')
    check(s[0:32] == ('%s-%05d' % (letters, number)).ljust(32).encode(), 'disc name')
    check(s[104:128] == b'PlayStation Master Disc ', '"PlayStation Master Disc "')
    check(s[129] == REGIONS[region], 'region %02X' % s[129])
    check(s[131] == disc_type, 'disc type %d' % disc_type)
    if disc_type == 2:
        count = struct.unpack_from('<I', s, 134)[0]
        check(count == ((image_sectors + 15) // 16) * 16 - 1, 'DVD sector count %d' % count)
    else:
        check(s[132:256] == b' ' * 124, 'CD filler')
    check(struct.unpack_from('<IB', s, 265) == (m2, m1), 'magic numbers 1 and 2')
    if region != 'J':
        check(s[301] == m3, 'magic number 3')
    check(s[816:824] == b'CDVDGEN ', 'CDVDGEN version')


def test_dvd(tmp, letters, number, vmode, region):
    name = '%s_%03d.%02d' % (letters, number // 100, number % 100)
    print('DVD %s %s region %s:' % (name, vmode, region or 'default'))
    image = os.path.join(tmp, 'test_dvd.iso')
    sectors = 40
    with open(image, 'wb') as f:
        f.write(b'\0' * 2048 * 20)
        f.write(system_cnf(name, vmode).ljust(2048, b'\0'))
        f.write(b'\0' * 2048 * (sectors - 21))
    code, out = run_patcher(image, region)
    check(code == 0, 'exit code 0')
    check(os.path.exists(os.path.join(tmp, 'DVD_SECTORS.BIN')), 'DVD_SECTORS.BIN backup written')
    with open(image, 'rb') as f:
        data = f.read()
    check(len(data) == sectors * 2048, 'image size unchanged')
    check_boot_sectors(data[:16 * 2048], letters, number, vmode, region, 2, sectors)
    check(data[20 * 2048:21 * 2048] == system_cnf(name, vmode).ljust(2048, b'\0'), 'rest of the image untouched')
    if failures:
        print(out)


def test_cd(tmp, letters, number, vmode, region):
    name = '%s_%03d.%02d' % (letters, number // 100, number % 100)
    print('CD %s %s region %s:' % (name, vmode, region or 'default'))
    image = os.path.join(tmp, 'test_cd.bin')
    sectors = 40
    with open(image, 'wb') as f:
        for lba in range(sectors):
            f.write(cd_sector(lba, system_cnf(name, vmode) if lba == 20 else b''))
    code, out = run_patcher(image, region)
    check(code == 0, 'exit code 0')
    check(os.path.exists(os.path.join(tmp, 'CD_SECTORS.BIN')), 'CD_SECTORS.BIN backup written')
    with open(image, 'rb') as f:
        data = f.read()
    check(len(data) == sectors * 2352, 'image size unchanged')
    sec = [data[i * 2352:(i + 1) * 2352] for i in range(sectors)]
    check_boot_sectors(b''.join(s[24:24 + 2048] for s in sec[:16]), letters, number, vmode, region, 1, sectors)
    bad = [i for i in list(range(12)) + [14, 15] if struct.unpack_from('<I', sec[i], 2072)[0] != edc(sec[i][16:2072])]
    check(not bad, 'EDC of the patched sectors' + (' (wrong: %s)' % bad if bad else ''))
    hdr = [i for i in range(16) if sec[i][:16] != cd_sector(i, b'')[:16]]
    check(not hdr, 'sync and headers kept')
    if failures:
        print(out)


def test_large_dvd(tmp):
    # past 2 GB: offsets don't fit a 32-bit long (Windows, 32-bit systems)
    sectors = (1 << 20) + 1024          # 2 GiB + 2 MiB
    cnf_lba = (1 << 20) + 512           # SYSTEM.CNF past the 2 GiB mark
    print('DVD larger than 2 GB (%d bytes, SYSTEM.CNF at 0x%X):' % (sectors * 2048, cnf_lba * 2048))
    image = os.path.join(tmp, 'large.iso')
    cnf = system_cnf('SLUS_123.45', 'NTSC').ljust(2048, b'\0')
    with open(image, 'wb') as f:        # sparse where the filesystem allows it
        f.truncate(sectors * 2048)
        f.seek(cnf_lba * 2048)
        f.write(cnf)
    code, out = run_patcher(image, None)
    check(code == 0, 'exit code 0')
    check('Image size: %d bytes' % (sectors * 2048) in out, 'image size reported as %d' % (sectors * 2048))
    check('offset 0x%X' % (cnf_lba * 2048) in out, 'SYSTEM.CNF found at 0x%X' % (cnf_lba * 2048))
    with open(image, 'rb') as f:
        boot = f.read(16 * 2048)
        f.seek(cnf_lba * 2048)
        check(f.read(2048) == cnf, 'SYSTEM.CNF sector untouched')
    check(os.path.getsize(image) == sectors * 2048, 'image size unchanged')
    check_boot_sectors(boot, 'SLUS', 12345, 'NTSC', None, 2, sectors)
    if failures:
        print(out)


def test_version():
    # EXPECTED_VERSION: what a release build must report (CI sets it from the tag)
    expected = os.environ.get('EXPECTED_VERSION') or None
    print('--version%s:' % (' (expecting %s)' % expected if expected else ''))
    for flag in ('--version', '-v'):
        p = subprocess.run([PATCHER, flag], capture_output=True, text=True)
        m = re.fullmatch(r'ps2-master-patcher (\S+)\s*', p.stdout)
        check(p.returncode == 0 and m is not None, '%s prints "ps2-master-patcher <version>" (%r)' % (flag, p.stdout.strip()))
    version = m.group(1) if m else None
    if expected:
        check(version == expected, 'version is %s' % expected)
    p = subprocess.run([PATCHER], capture_output=True, text=True)
    check('Boot Patcher v%s ' % version in p.stdout, 'banner shows the same version')


def test_no_disc_id(tmp):
    print('image without a disc ID:')
    image = os.path.join(tmp, 'no_id.iso')
    with open(image, 'wb') as f:
        f.write(system_cnf('MYAPP.ELF', 'NTSC').rjust(2048 * 40, b'\0'))
    code, out = run_patcher(image, None)
    check(code != 0 and 'Could not detect Disc ID' in out, 'fails with "Could not detect Disc ID"')


def main():
    test_version()
    for case in [(test_dvd, 'SLUS', 12345, 'NTSC', None),
                 (test_dvd, 'SLES', 54321, 'PAL', 'E'),
                 (test_dvd, 'APOL', 99999, 'NTSC', 'W'),
                 (test_dvd, 'SLPM', 65432, 'NTSC', 'J'),
                 (test_cd, 'SLUS', 12345, 'NTSC', 'U'),
                 (test_cd, 'SCES', 50003, 'PAL', 'W')]:
        with tempfile.TemporaryDirectory() as tmp:
            case[0](tmp, *case[1:])
    with tempfile.TemporaryDirectory() as tmp:
        test_large_dvd(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_no_disc_id(tmp)

    print('%d failure(s)' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
