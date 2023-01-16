#!/usr/bin/env python3
#
# Copyright © 2020 Google, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

from mako.template import Template
from isa import ISA
import argparse
import os
import sys

template = """\
/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "${header}"

/*
 * enum tables, these don't have any link back to other tables so just
 * dump them up front before the bitset tables
 */

%for name, enum in isa.enums.items():
static const struct isa_enum ${enum.get_c_name()} = {
    .num_values = ${len(enum.values)},
    .values = {
%   for val, display in enum.values.items():
        { .val = ${val}, .display = "${display}" },
%   endfor
    },
};
%endfor

/*
 * generated expression functions, can be linked from bitset tables, so
 * also dump them up front
 */

%for name, expr in isa.expressions.items():
static uint64_t
${expr.get_c_name()}(struct decode_scope *scope)
{
%   for fieldname in sorted(expr.fieldnames):
    int64_t ${fieldname} = isa_decode_field(scope, "${fieldname}");
%   endfor
    return ${expr.expr};
}
%endfor

/*
 * Forward-declarations (so we don't have to figure out which order to
 * emit various tables when they have pointers to each other)
 */

%for name, bitset in isa.all_bitsets():
static const struct isa_bitset bitset_${bitset.get_c_name()}_gen_${bitset.gen_min};
%endfor

%for root_name, root in isa.roots.items():
const struct isa_bitset *${root.get_c_name()}[];
%endfor

/*
 * bitset tables:
 */

%for name, bitset in isa.all_bitsets():
%   for case in bitset.cases:
%      for field_name, field in case.fields.items():
%         if field.get_c_typename() == 'TYPE_BITSET':
%            if len(field.params) > 0:
static const struct isa_field_params ${case.get_c_name()}_gen_${bitset.gen_min}_${field.get_c_name()} = {
       .num_params = ${len(field.params)},
       .params = {
%               for param in field.params:
           { .name= "${param[0]}",  .as = "${param[1]}" },
%               endfor

       },
};
%            endif
%         endif
%      endfor
static const struct isa_case ${case.get_c_name()}_gen_${bitset.gen_min} = {
%   if case.expr is not None:
       .expr     = &${isa.expressions[case.expr].get_c_name()},
%   endif
%   if case.display is not None:
       .display  = "${case.display}",
%   endif
       .num_fields = ${len(case.fields)},
       .fields   = {
%   for field_name, field in case.fields.items():
          { .name = "${field_name}", .low = ${field.low}, .high = ${field.high},
%      if field.expr is not None:
            .expr = &${isa.expressions[field.expr].get_c_name()},
%      endif
%      if field.display is not None:
            .display = "${field.display}",
%      endif
            .type = ${field.get_c_typename()},
%      if field.get_c_typename() == 'TYPE_BITSET':
            .bitsets = ${isa.roots[field.type].get_c_name()},
%         if len(field.params) > 0:
            .params = &${case.get_c_name()}_gen_${bitset.gen_min}_${field.get_c_name()},
%         endif
%      endif
%      if field.get_c_typename() == 'TYPE_ENUM':
            .enums = &${isa.enums[field.type].get_c_name()},
%      endif
%      if field.get_c_typename() == 'TYPE_ASSERT':
            .val.bitset = { ${', '.join(isa.split_bits(field.val, 32))} },
%      endif
          },
%   endfor
       },
};
%   endfor
static const struct isa_bitset bitset_${bitset.get_c_name()}_gen_${bitset.gen_min} = {
<% pattern = bitset.get_pattern() %>
%   if bitset.extends is not None:
       .parent   = &bitset_${isa.bitsets[bitset.extends].get_c_name()}_gen_${isa.bitsets[bitset.extends].gen_min},
%   endif
       .name     = "${name}",
       .gen      = {
           .min  = ${bitset.get_gen_min()},
           .max  = ${bitset.get_gen_max()},
       },
       .match.bitset    = { ${', '.join(isa.split_bits(pattern.match, 32))} },
       .dontcare.bitset = { ${', '.join(isa.split_bits(pattern.dontcare, 32))} },
       .mask.bitset     = { ${', '.join(isa.split_bits(pattern.mask, 32))} },
       .num_cases = ${len(bitset.cases)},
       .cases    = {
%   for case in bitset.cases:
            &${case.get_c_name()}_gen_${bitset.gen_min},
%   endfor
       },
};
%endfor

/*
 * bitset hierarchy root tables (where decoding starts from):
 */

%for root_name, root in isa.roots.items():
const struct isa_bitset *${root.get_c_name()}[] = {
%   for leaf_name, leafs in isa.leafs.items():
%      for leaf in leafs:
%         if leaf.get_root() == root:
             &bitset_${leaf.get_c_name()}_gen_${leaf.gen_min},
%         endif
%      endfor
%   endfor
    (void *)0
};
%endfor

#include "isaspec_decode_impl.c"

"""

header = """\
/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _${guard}_
#define _${guard}_

#include <stdint.h>
#include <util/bitset.h>

#define BITMASK_WORDS BITSET_WORDS(${isa.bitsize})

typedef struct {
    BITSET_WORD bitset[BITMASK_WORDS];
} bitmask_t;


#define BITSET_FORMAT ${isa.format()}
#define BITSET_VALUE(v) ${isa.value()}

static inline void
next_instruction(bitmask_t *instr, BITSET_WORD *start)
{
    %for i in range(0, int(isa.bitsize / 32)):
    instr->bitset[${i}] = *(start + ${i});
    %endfor
}

static inline uint64_t
bitmask_to_uint64_t(bitmask_t mask)
{
%   if isa.bitsize <= 32:
    return mask.bitset[0];
%   else:
    return ((uint64_t)mask.bitset[1] << 32) | mask.bitset[0];
%   endif
}

static inline bitmask_t
uint64_t_to_bitmask(uint64_t val)
{
    bitmask_t mask = {
        .bitset[0] = val & 0xffffffff,
%   if isa.bitsize > 32:
        .bitset[1] = (val >> 32) & 0xffffffff,
%   endif
    };

    return mask;
}

#include "isaspec_decode_decl.h"

#endif /* _${guard}_ */

"""

def guard(p):
    return os.path.basename(p).upper().replace("-", "_").replace(".", "_")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--xml', required=True, help='isaspec XML file.')
    parser.add_argument('--out-c', required=True, help='Output C file.')
    parser.add_argument('--out-h', required=True, help='Output H file.')
    args = parser.parse_args()

    isa = ISA(args.xml)

    try:
        with open(args.out_c, 'w') as f:
            out_h_basename = os.path.basename(args.out_h)
            f.write(Template(template).render(isa=isa, header=out_h_basename))

        with open(args.out_h, 'w') as f:
            f.write(Template(header).render(isa=isa, guard=guard(args.out_h)))

    except Exception:
        # In the event there's an error, this imports some helpers from mako
        # to print a useful stack trace and prints it, then exits with
        # status 1, if python is run with debug; otherwise it just raises
        # the exception
        import sys
        from mako import exceptions
        print(exceptions.text_error_template().render(), file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
