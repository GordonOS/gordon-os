#!/usr/bin/env python3
"""dis6502-linear.py -- linear disassembly through py65, to cross-check dis6502.py.

Requires py65 (in the local .venv); dis6502.py needs nothing. This walks every byte
linearly, so it covers code the recursive walk never reaches (code entered through a
jump table) at the cost of decoding data as code.

dis6502.py follows control flow (so its hardware list is trustworthy but
partial); this walks every byte linearly, which gives full coverage at the cost
of decoding data as code. Running both over the same range is the check on
dis6502.py's hand-written opcode table.

Usage:
    python tools/dis6502-linear.py FILE [start_hex] [end_hex]
"""

import collections
import sys

from py65.devices.mpu6502 import MPU
from py65.disassembler import Disassembler, AddressParser


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    b = open(sys.argv[1], 'rb').read()
    load = b[0] | (b[1] << 8)
    start = int(sys.argv[2], 16) if len(sys.argv) > 2 else load
    end = int(sys.argv[3], 16) if len(sys.argv) > 3 else load + len(b) - 3

    mem = collections.defaultdict(int)
    for i, byte in enumerate(b[2:]):
        mem[load + i] = byte

    mpu = MPU(mem)
    dis = Disassembler(mpu, AddressParser())
    pc = start
    while pc <= end:
        mpu.pc = pc
        result = dis.instruction_at(pc)
        length = result[0]
        text = result[1]
        raw = ' '.join(f"{mem[pc + k]:02X}" for k in range(length))
        print(f"    ${pc:04X}  {raw:<9} {text}")
        pc += length
    return 0


if __name__ == '__main__':
    sys.exit(main())
