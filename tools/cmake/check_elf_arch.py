#!/usr/bin/env python3
"""Validate ELF Machine matches MAIX_ARCH (riscv64 / arm64)."""
import os
import struct
import sys

# ELF e_machine values
EM_RISCV = 243
EM_AARCH64 = 183

ARCH_TO_EM = {
    "riscv64": EM_RISCV,
    "arm64": EM_AARCH64,
}

EM_TO_NAME = {
    EM_RISCV: "RISC-V",
    EM_AARCH64: "AArch64",
}


def read_elf_machine(path):
    with open(path, "rb") as f:
        magic = f.read(4)
        if magic != b"\x7fELF":
            return None
        ei_class = f.read(1)  # 1=32, 2=64
        f.read(1)  # data
        f.read(1)  # version
        f.read(9)  # padding
        # e_type (2) + e_machine (2)
        f.read(2)
        em = struct.unpack("<H", f.read(2))[0]
        return em


def main():
    if len(sys.argv) < 3:
        print("Usage: check_elf_arch.py <riscv64|arm64> <elf> [elf...]")
        return 2
    arch = sys.argv[1]
    expect = ARCH_TO_EM.get(arch)
    if expect is None:
        print("-- skip ELF check for arch={!r}".format(arch))
        return 0
    failed = 0
    for path in sys.argv[2:]:
        if not os.path.isfile(path):
            print("-- skip missing {}".format(path))
            continue
        em = read_elf_machine(path)
        if em is None:
            print("-- not ELF, skip {}".format(path))
            continue
        if em != expect:
            print("[ERROR] ELF arch mismatch: {} Machine={} ({}) expected {} ({})".format(
                path, em, EM_TO_NAME.get(em, "?"), expect, EM_TO_NAME.get(expect, arch)))
            failed = 1
        else:
            print("-- ELF arch OK: {} ({})".format(path, EM_TO_NAME.get(em, arch)))
    return failed


if __name__ == "__main__":
    sys.exit(main())
