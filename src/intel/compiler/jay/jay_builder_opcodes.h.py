# Copyright 2026 Intel Corporation
# SPDX-License-Identifier: MIT

from typing import TYPE_CHECKING
import argparse
import sys

from mako import exceptions
from mako.template import Template

from jay_opcodes import OPCODES

if TYPE_CHECKING:
    from jay_opcodes import Opcode


def infer_type(op: 'Opcode') -> bool:
    return op.has_dest and (set(op.types) <= set(["u1", "u32", "u64"]) or
                            op.name == 'mov')


def signature(op: 'Opcode', with_dest: bool = True, with_types: bool = False,
              mode: str = 'prototype', type_: str = 't', src: str = '{}') -> str:
    arr = [('jay_builder *', 'b')]

    if with_types and len(op.types) > 1 and not infer_type(op):
        arr += [('enum jay_type', type_)]

    if with_dest and op.has_dest:
        arr += [('jay_def', 'dst')]

    arr += [('jay_def', src.format(f'src{i}')) for i in range(op.num_srcs)]
    arr += [x for x in op.extra_struct if not x[1].startswith('pad')]

    return ', '.join([(t + ' ' if mode == 'prototype' else '') + v for t, v in arr])


TEMPLATE = """
/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "jay_private.h"

#ifndef NDEBUG
#define type_assert(op, ...) if (!(__VA_ARGS__)) { fprintf(stderr, "%s does not allow type: ", #op); jay_print_type(stderr, t); fprintf(stderr, "\\n"); } assert(__VA_ARGS__)
#else
#define type_assert(...)
#endif

% for op in opcodes.values():
<%
    OPCODE = op.name.upper()
    num_srcs = op.num_srcs
    has_dest = op.has_dest
    multi_type = len(op.types) > 1
    info_size = f'sizeof(jay_{op.name}_info)' if op.extra_struct else '0'
    operands = ["dst"] + [f"src{i}" for i in range(num_srcs)]
    if num_srcs > 0:
        uniform = " && " .join([f"jay_is_uniform(src{i})" for i in range(num_srcs)])
        reg_file = f"({uniform}) ? UGPR : GPR"
    else:
        reg_file = "GPR"
    if not op.types:
        continue
    # Ignore the lane index when determining the type of a shuffle
    infer_operands = operands[0:-1] if op.name == "shuffle" else operands
%>
static inline jay_inst *
_jay_${OPCODE}(${signature(op, with_types = True)})
{
% if infer_type(op):
   enum jay_type t = jay_num_values(dst) == 2 ? JAY_TYPE_U64 :
                     ${" && ".join([f"(jay_is_flag({x}) || jay_is_imm({x}))" for x in infer_operands])}
                     ? JAY_TYPE_U1 : JAY_TYPE_U32;
% elif multi_type:
   type_assert(${OPCODE}, 0
% for type in op.types:
    || t == JAY_TYPE_${type.upper()}
% endfor
   );

% else:
   enum jay_type t = JAY_TYPE_${op.types[0].upper()};

% endif
   jay_inst *inst = jay_alloc_inst(b, JAY_OPCODE_${OPCODE}, ${num_srcs}, ${info_size});
% for _, prop in op.extra_struct:
% if not prop.startswith('pad'):
   jay_set_${op.name}_${prop}(inst, ${prop});
% endif
% endfor

   inst->type = t;
% if op.has_dest:
   inst->dst = dst;
% endif
% for i in range(num_srcs):
   inst->src[${i}] = src${i};
% endfor

   jay_builder_insert(b, inst);
   return inst;
}

#define jay_${OPCODE}(${signature(op, with_types = True, mode = 'call')}) _jay_${OPCODE}(${signature(op, with_types = True, src = 'JAY_BUILD_SRC({})', mode='call')})

% for type in op.types:
static inline ${'jay_def' if op.has_dest else 'void'}
_jay_${OPCODE}_${type}(${signature(op, with_dest = False)})
{
% if op.has_dest:
   jay_def dst = jay_alloc_def(b, ${reg_file}, ${2 if '64' in type else 1});
%endif
   jay_${OPCODE}(${signature(op, with_types = True, type_ = 'JAY_TYPE_'+type.upper(), mode = 'call')});
% if op.has_dest:
   return dst;
% endif
}
#define jay_${OPCODE}_${type}(${signature(op, with_dest = False, mode =
'call')}) _jay_${OPCODE}_${type}(${signature(op, src='JAY_BUILD_SRC({})', mode = 'call', with_dest = False)})
% endfor

% endfor

#undef type_assert
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('output', action='store')
    args = parser.parse_args()

    ops = {op: v for (op, v) in OPCODES.items() if op not in {'cmp', 'send'}}

    try:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(Template(TEMPLATE).render(
                opcodes=ops,
                signature=signature,
                infer_type=infer_type))
    except Exception:
        print(exceptions.text_error_template().render())
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
