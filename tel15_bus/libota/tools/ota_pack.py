#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 ELF 打成设备可校验的 OTA 包（尾部 48 字节），网页仍用命令 26 上传本文件即可。

布局: [ELF][magic8][product_id u32LE][bin_size u32LE][sha256 32]

用法:
  python ota_pack.py tel_15_qudao
  python ota_pack.py tel_15_qudao -o tel_15_qudao.ota

可选检查固件内是否含 TEL15_OTA_ID=T15Q（-c）。
"""
from __future__ import print_function

import argparse
import binascii
import hashlib
import struct
import sys

MAGIC = b"DPLRPKG1"
PRODUCT_ID = 0x54313551  # 'T15Q'
PRODUCT_TAG = b"TEL15_OTA_ID=T15Q"


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

    # binascii：兼容 Python < 3.5（无 bytes.hex）以及 python2
    sha_hex = binascii.hexlify(digest).decode("ascii")
    print("ok: %s" % out_path)
    print("  elf_size=%u  pkg_size=%u  sha256=%s" % (
        len(data), len(data) + 48, sha_hex))


def main():
    ap = argparse.ArgumentParser(description="Pack TEL-15 OTA image with product id + SHA256 trailer")
    ap.add_argument("elf", help="input ELF (e.g. tel_15_qudao)")
    ap.add_argument("-o", "--output", help="output path (default: <elf>.ota)")
    ap.add_argument("-c", "--check-tag", action="store_true",
                    help="require TEL15_OTA_ID=T15Q inside ELF")
    args = ap.parse_args()
    out = args.output or (args.elf + ".ota")
    pack(args.elf, out, args.check_tag)


if __name__ == "__main__":
    main()
