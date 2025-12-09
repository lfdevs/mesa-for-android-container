#!/usr/bin/python3
#
# Copyright © 2024-2025 Tomeu Vizoso
#
# SPDX-License-Identifier: MIT

import sys
import os
import argparse
import struct
from gen_parser import Parser, Reg, Enum, mask, Error


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('--xml', type=str, required=True)
	parser.add_argument('--dump', type=str, required=True)

	args = parser.parse_args()

	p = Parser()

	try:
		p.parse("", args.xml)
	except Error as e:
		print(e, file=sys.stderr)
		exit(1)

	regs = {}
	for e in p.file:
		if isinstance(e, Reg):
			regs[e.offset] = e

	domains = {}
	for e in p.file:
		if isinstance(e, Enum):
			if e.name == "target":
				for name, val in e.values:
					domains[name] = val

	f = open(args.dump, mode='rb')
	for i in range(0, os.path.getsize(args.dump) // 8):
		cmd = f.read(8)
		(offset, value, target) = struct.unpack("<hIh", cmd)
		if offset in regs.keys():
			reg = regs[offset]

			if (target & 0xfffffffe) != domains[reg.domain]:
				print("WARNING: target 0x%x doesn't match register's domain 0x%x" % (target, domains[reg.domain]))

			print("EMIT(REG_%s, " % regs[offset].full_name.upper(), end="")
			first = True
			if value == 0 or len(reg.bitset.fields) == 1:
				print("0x%x" % value, end="")
			else:
				for field in reg.bitset.fields:
					if field.type == "boolean":
						if 1 << field.high & value:
							if not first:
								print(" | ", end="")
							print("%s_%s" % (reg.full_name.upper(), field.name.upper()), end="")
							first = False
					elif field.type == "uint":
						field_value = (value & mask(field.low, field.high)) >> field.low
						if field_value != 0:
							if not first:
								print(" | ", end="")
							print("%s_%s(%d)" % (reg.full_name.upper(), field.name.upper(), field_value), end="")
							first = False
			print(");")
		else:
			print("%x %x %x" % (target, offset, value))

if __name__ == '__main__':
	main()
