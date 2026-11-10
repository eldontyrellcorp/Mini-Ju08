#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# usage: fm1_app_area.py PACKAGE.fwsc APP.bin OUT.bin   (APP.bin: build/felucca.bin, to check the decrypted contents against)
# Extracts flash.bin from a Felucca .fwsc and checks it: UFW header and entry CRCs, the app area decrypts with the chip key
# to the firmware binary we built. Writes the app area [0x4000, 0x93000) to OUT. No device access.
import sys, struct
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fm1pkg_make as P
pkg, appbin, out = sys.argv[1:4]
raw = open(pkg, "rb").read()
BL, BLK, KEEP = 20, 0x30, 0x2F
logical = bytearray()
for i in range(BL): logical += raw[i*BLK:i*BLK+KEEP]
logical += raw[BL*BLK:]
hdr = bytearray(logical[:0x40]); P.enc(hdr, 0, 0x40)
total, n = struct.unpack_from("<IH", hdr, 4)
assert total == len(logical), (total, len(logical))
assert P.crc16(hdr[2:0x40]) == struct.unpack_from("<H", hdr, 0)[0], "header crc"
ents = bytearray(logical[0x40:0x40 + 0x50*n])
lst_crc = P.crc16(ents)
assert lst_crc == struct.unpack_from("<H", hdr, 2)[0], "entry list crc"
for k in range(n): P.enc(ents, k*0x50, 0x50)
files = {}
for k in range(n):
    e = ents[k*0x50:(k+1)*0x50]
    typ, idx, crc, _, off, size, _ = struct.unpack_from("<HHHHIII", e, 0)
    name = bytes(e[0x40:]).rstrip(b"\0").decode()
    d = bytes(logical[off:off+size])
    assert P.crc16(d) == crc, name + " crc"
    files[name] = d
    print(f"{name}: offset {off:#x}, {size} B, crc ok")
flash = files["flash.bin"]
assert len(flash) == P.FLASH_SIZE == 0x93000
app_area = bytearray(flash[0x4000:0x93000])
# decrypt a copy with the chip key and look for our firmware inside (app.bin is at 0x120 of the area)
plain = bytearray(app_area); P.sfc(plain, 0, len(plain), 0, P.KEY)
fw = open(appbin, "rb").read()
inner = bytes(plain[0x120:0x120+len(fw)])
print("decrypted app area holds the built firmware binary:", inner == fw, f"({len(fw)} B)")
assert inner == fw
open(out, "wb").write(bytes(app_area))
print(f"app area written: {len(app_area)} B (flash 0x4000..0x93000) -> {out}")
