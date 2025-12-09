# Copyright © 2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

TEMPLATE = """
#include "bi_swizzles.h"

uint32_t bi_op_swizzles[BI_NUM_OPCODES][BI_MAX_SRCS] = {
% for opcode, src_swizzles in op_swizzles.items():
    [BI_OPCODE_${opcode.replace('.', '_').upper()}] = {
% for src in src_swizzles:
% for swizzle in src:
        (1 << BI_SWIZZLE_${swizzle.upper()}) |
% endfor
        0,
% endfor
    },
% endfor
};
"""

import sys
from bifrost_isa import *
from mako.template import Template

instructions = {}
for arg in sys.argv[1:]:
    new_instructions = parse_instructions(arg, include_pseudo = True)
    instructions.update(new_instructions)

ir_instructions = partition_mnemonics(instructions)

op_swizzles = {}

for name, op in ir_instructions.items():
    src_swizzles = [None] * op['srcs']
    SWIZZLE_MODS = ["lane", "lanes", "replicate", "swz", "widen", "swap"]

    for mod, opts in op['modifiers'].items():
        raw, arg = (mod[0:-1], int(mod[-1])) if mod[-1] in "0123" else (mod, 0)
        if raw in SWIZZLE_MODS:
            assert(src_swizzles[arg] is None)
            src_swizzles[arg] = set(opts)

            src_swizzles[arg].discard('reserved')
            if 'none' in src_swizzles[arg]:
                src_swizzles[arg].remove('none')
                src_swizzles[arg].add('h01')

    # Anything that doesn't have swizzle mods only supports identity
    src_swizzles = [swizzle or ['h01'] for swizzle in src_swizzles]

    op_swizzles[name] = src_swizzles

print(Template(COPYRIGHT + TEMPLATE).render(op_swizzles = op_swizzles))
