#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 ELF 打成设备可校验的 OTA 包（尾部 48 字节），网页仍用 102 上传本文件即可。

布局: [ELF][magic8][product_id u32LE][bin_size u32LE][sha256 32]

用法:
  python ota_pack.py DOPPLER.1.0
  python ota_pack.py DOPPLER.1.0 -o DOPPLER.1.0.ota

可选检查固件内是否含 DOPPLER_OTA_ID=DPLR（-c）。
"""
from __future__ import print_function

import argparse
import binascii
import hashlib
import os
import struct
import sys

MAGIC = b"DPLRPKG1"
PRODUCT_ID = 0x44504C52  # 'DPLR'
PRODUCT_TAG = b"DOPPLER_OTA_ID=DPLR"


def pack(elf_path, out_path, check_tag):
    with open(elf_path, "rb") as f:
        data = f.read()
    if len(data) < 4 or data[0:4] != b"\x7fELF":
        raise SystemExit("not an ELF: %s" % elf_path)
    if check_tag and PRODUCT_TAG not in data:
        raise SystemExit("missing product tag %r in ELF (rebuild with OtaUpgrade.c)" % PRODUCT_TAG.decode())

    digest = hashlib.sha256(data).digest()
    trailer = MAGIC + struct.pack("<II", PRODUCT_ID, len(data)) + digest
    assert len(trailer) == 48

    with open(out_path, "wb") as f:
        f.write(data)
        f.write(trailer)

    # binascii：兼容 Python < 3.5（无 bytes.hex）
    sha_hex = binascii.hexlify(digest).decode("ascii")
    print("ok: %s" % out_path)
    print("  elf_size=%u  pkg_size=%u  sha256=%s" % (
        len(data), len(data) + 48, sha_hex))


def main():
    ap = argparse.ArgumentParser(description="Pack Doppler OTA image with product id + SHA256 trailer")
    ap.add_argument("elf", help="input ELF (e.g. DOPPLER.1.0)")
    ap.add_argument("-o", "--output", help="output path (default: <elf>.ota)")
    ap.add_argument("-c", "--check-tag", action="store_true",
                    help="require DOPPLER_OTA_ID=DPLR inside ELF")
    args = ap.parse_args()
    out = args.output or (args.elf + ".ota")
    pack(args.elf, out, args.check_tag)


if __name__ == "__main__":
    main()
