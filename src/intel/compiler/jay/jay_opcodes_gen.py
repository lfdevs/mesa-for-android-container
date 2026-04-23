# Copyright 2026 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

from mako import exceptions
from mako.template import Template

from jay_opcodes import OPCODES

HEADER_TEMPLATE = """/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include "util/macros.h"

enum PACKED jay_opcode {
% for opcode in opcodes:
   JAY_OPCODE_${opcode.upper()},
% endfor
   JAY_NUM_OPCODES
};
static_assert(sizeof(enum jay_opcode) == 1);

struct jay_opcode_info {
   const char *name;
   unsigned num_srcs;

   /** Bitfield of sources which support negation/abs */
   uint8_t src_mods;

   /** Which modifiers are broadly supported by the opcode. Note there may be
     * further restrictions (e.g. based on types) not encoded here.
     */
   bool sat;
   bool cmod;

   /** Whether the operation has side effects not expressed in the SSA IR */
   bool side_effects;

   /** op(a, b, c, ...) = op(b, a, c, ...) */
   bool _2src_commutative;
};

extern const struct jay_opcode_info jay_opcode_infos[JAY_NUM_OPCODES];
"""

CODE_TEMPLATE = """/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "jay_opcodes.h"

const struct jay_opcode_info jay_opcode_infos[JAY_NUM_OPCODES] = {
% for opcode, op in opcodes.items():
   [JAY_OPCODE_${opcode.upper()}] = {
      .name = "${opcode}",
      .num_srcs = ${op.num_srcs},
      .src_mods = ${bin(op.negate)},
% for mod in ["sat", "cmod", "side_effects", "_2src_commutative"]:
% if getattr(op, mod):
      .${mod} = true,
% endif
% endfor
   },
% endfor
};
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--code', action='store', default=None)
    parser.add_argument('--header', action='store', default=None)
    args = parser.parse_args()

    if not (args.header or args.code):
        parser.error('At least one of --code or --header is required')

    try:
        if args.code is not None:
            with open(args.code, 'w', encoding='utf-8') as f:
                f.write(Template(CODE_TEMPLATE).render(opcodes=OPCODES))
        if args.header is not None:
            with open(args.header, 'w', encoding='utf-8') as f:
                f.write(Template(HEADER_TEMPLATE).render(opcodes=OPCODES))
    except Exception:
        print(exceptions.text_error_template().render())
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
