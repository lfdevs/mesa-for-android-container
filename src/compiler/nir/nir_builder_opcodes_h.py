template = """\
/* Copyright (C) 2015 Broadcom
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

#ifndef _NIR_BUILDER_OPCODES_
#define _NIR_BUILDER_OPCODES_

#include "util/u_func_arg_order.h"

<%
def src_decl_list(num_srcs):
   return ', '.join('nir_def *src' + str(i) for i in range(num_srcs))

def src_list(num_srcs):
   return ', '.join('src' + str(i) for i in range(num_srcs))

def needs_ltr_args(num_srcs):
   return num_srcs >= 2

def alu_helper_name(name, num_srcs):
   return f'_nir_{name}' if num_srcs >= 2 else f'nir_{name}'

def needs_num_components(opcode):
   return "replicated" in opcode.name

def intrinsic_prefix(name):
   if name in build_prefixed_intrinsics:
      return 'nir_build'
   else:
      return 'nir'
%>

% for name, opcode in sorted(opcodes.items()):
% if not needs_num_components(opcode):
static inline nir_def *
${alu_helper_name(name, opcode.num_inputs)}(nir_builder *build, ${src_decl_list(opcode.num_inputs)})
{
% if opcode.is_conversion and \
     type_base_type(opcode.output_type) == opcode.input_types[0]:
   if (src0->bit_size == ${type_size(opcode.output_type)})
      return src0;
% if opcode.input_types[0] == "int":
   if (src0->bit_size > ${type_size(opcode.output_type)})
      return nir_build_alu1(build, nir_op_u2u${type_size(opcode.output_type)}, src0);
%endif
%endif
% if opcode.num_inputs <= 4:
   return nir_build_alu${opcode.num_inputs}(build, nir_op_${name}, ${src_list(opcode.num_inputs)});
% else:
   nir_def *srcs[${opcode.num_inputs}] = {${src_list(opcode.num_inputs)}};
   return nir_build_alu_src_arr(build, nir_op_${name}, srcs);
% endif
}
% if needs_ltr_args(opcode.num_inputs):
#define nir_${name}(...) MESA_CALL_WITH_LTR_ARGS(_nir_${name}, __VA_ARGS__)
% endif

% endif
% endfor

% for name, opcode in sorted(INTR_OPCODES.items()):
% if opcode.indices:
struct _nir_${name}_indices {
   int _; /* exists to avoid empty initializers */
% for index in opcode.indices:
   ${index.c_data_type} ${index.name};
% endfor
};
% endif
% endfor

<%
def intrinsic_decl_list(opcode):
    need_components = opcode.dest_components == 0 and \
                      0 not in opcode.src_components

    res = ''
    if (opcode.has_dest or opcode.num_srcs) and need_components:
        res += ', unsigned num_components'
    if opcode.has_dest and len(opcode.bit_sizes) != 1 and opcode.bit_size_src == -1:
        res += ', unsigned bit_size'
    for i in range(opcode.num_srcs):
        res += ', nir_def *src' + str(i)
    if opcode.indices:
        res += ', struct _nir_' + opcode.name + '_indices indices'
    return res

def intrinsic_macro_list(opcode):
    need_components = opcode.dest_components == 0 and \
                      0 not in opcode.src_components

    res = ''
    if (opcode.has_dest or opcode.num_srcs) and need_components:
        res += ', num_components'
    if opcode.has_dest and len(opcode.bit_sizes) != 1 and opcode.bit_size_src == -1:
        res += ', bit_size'
    for i in range(opcode.num_srcs):
        res += ', src' + str(i)
    return res

def get_intrinsic_bitsize(opcode):
    if len(opcode.bit_sizes) == 1:
        return str(opcode.bit_sizes[0])
    elif opcode.bit_size_src != -1:
        return 'src' + str(opcode.bit_size_src) + '->bit_size'
    else:
        return 'bit_size'

def intrinsic_alias(name, opcode):
    public_name = intrinsic_prefix(name) + '_' + name if opcode.indices else 'nir_' + name
    args = 'build' + intrinsic_macro_list(opcode)

    if needs_ltr_args(opcode.num_srcs):
        if opcode.indices:
            return f'''#ifdef __cplusplus
#define {public_name}({args}, ...) \\\\
MESA_CALL_WITH_LTR_ARGS(_nir_build_{name}, {args}, (_nir_{name}_indices{{0, __VA_ARGS__}}))
#else
#define {public_name}({args}, ...) \\\\
MESA_CALL_WITH_LTR_ARGS(_nir_build_{name}, {args}, ((struct _nir_{name}_indices){{0, __VA_ARGS__}}))
#endif'''
        else:
            return f'#define {public_name}(...) MESA_CALL_WITH_LTR_ARGS(_nir_build_{name}, __VA_ARGS__)'
    elif opcode.indices:
        return f'''#ifdef __cplusplus
#define {public_name}({args}, ...) \\\\
_nir_build_{name}({args}, _nir_{name}_indices{{0, __VA_ARGS__}})
#else
#define {public_name}({args}, ...) \\\\
_nir_build_{name}({args}, (struct _nir_{name}_indices){{0, __VA_ARGS__}})
#endif'''
    else:
        return f'#define {public_name} _nir_build_{name}'
%>

% for name, opcode in sorted(INTR_OPCODES.items()):
% if opcode.has_dest:
static inline nir_def *
% else:
static inline nir_intrinsic_instr *
% endif
_nir_build_${name}(nir_builder *build${intrinsic_decl_list(opcode)})
{
   nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(
      build->shader, nir_intrinsic_${name});

   % if 0 in opcode.src_components:
   intrin->num_components = src${opcode.src_components.index(0)}->num_components;
   % elif opcode.dest_components == 0:
   intrin->num_components = (uint8_t)num_components;
   % endif
   % if opcode.has_dest:
      % if opcode.dest_components == 0:
      nir_def_init(&intrin->instr, &intrin->def, intrin->num_components, ${get_intrinsic_bitsize(opcode)});
      % else:
      nir_def_init(&intrin->instr, &intrin->def, ${opcode.dest_components}, ${get_intrinsic_bitsize(opcode)});
      % endif
   % endif
   % for i in range(opcode.num_srcs):
   intrin->src[${i}] = nir_src_for_ssa(src${i});
   % endfor
   % if WRITE_MASK in opcode.indices and 0 in opcode.src_components:
   if (!indices.write_mask)
      indices.write_mask = BITFIELD_MASK(intrin->num_components);
   % endif
   % if ALIGN_MUL in opcode.indices and 0 in opcode.src_components:
   if (!indices.align_mul)
      indices.align_mul = src${opcode.src_components.index(0)}->bit_size / 8u;
   % elif ALIGN_MUL in opcode.indices and opcode.dest_components == 0:
   if (!indices.align_mul)
      indices.align_mul = intrin->def.bit_size / 8u;
   % endif
   % if IO_SEMANTICS in opcode.indices:
   if (!indices.io_semantics.num_slots)
      indices.io_semantics.num_slots = 1;
   % endif
   % if IO_SEMANTICS in opcode.indices and DEST_TYPE in opcode.indices and opcode.has_dest:
   if (!indices.dest_type)
      indices.dest_type = (nir_alu_type)(nir_type_float | bit_size);
   % endif
   % if IO_SEMANTICS in opcode.indices and SRC_TYPE in opcode.indices and opcode.num_srcs > 0:
   if (!indices.src_type)
      indices.src_type = (nir_alu_type)(nir_type_float | src0->bit_size);
   % endif
   % if FP_MATH_CTRL in opcode.indices:
   if (!indices.fp_math_ctrl)
      indices.fp_math_ctrl = build->fp_math_ctrl;
   % endif
   % for index in opcode.indices:
   nir_intrinsic_set_${index.name}(intrin, indices.${index.name});
   % endfor

   nir_builder_instr_insert(build, &intrin->instr);
   % if opcode.has_dest:
   return &intrin->def;
   % else:
   return intrin;
   % endif
}
% endfor

% for name, opcode in sorted(INTR_OPCODES.items()):
${intrinsic_alias(name, opcode)}
% if name in build_prefixed_intrinsics:
#define nir_${name} nir_build_${name}
% endif
% endfor

% for name in ['flt', 'fge', 'feq', 'fneu']:
static inline nir_def *
nir_${name}_imm(nir_builder *build, nir_def *src1, double src2)
{
   return nir_${name}(build, src1, nir_imm_floatN_t(build, src2, src1->bit_size));
}
% endfor

% for name in ['ilt', 'ige', 'ieq', 'ine', 'ult', 'uge']:
static inline nir_def *
nir_${name}_imm(nir_builder *build, nir_def *src1, uint64_t src2)
{
   return nir_${name}(build, src1, nir_imm_intN_t(build, src2, src1->bit_size));
}
% endfor

% for prefix in ['i', 'u']:
static inline nir_def *
nir_${prefix}gt_imm(nir_builder *build, nir_def *src1, uint64_t src2)
{
   return nir_${prefix}lt(build, nir_imm_intN_t(build, src2, src1->bit_size), src1);
}

static inline nir_def *
nir_${prefix}le_imm(nir_builder *build, nir_def *src1, uint64_t src2)
{
   return nir_${prefix}ge(build, nir_imm_intN_t(build, src2, src1->bit_size), src1);
}
% endfor

#endif /* _NIR_BUILDER_OPCODES_ */"""

from nir_opcodes import opcodes, type_size, type_base_type
from nir_intrinsics import INTR_OPCODES, WRITE_MASK, ALIGN_MUL, IO_SEMANTICS, DEST_TYPE, SRC_TYPE
from mako.template import Template

# List of intrinsics that also need a nir_build_ prefixed factory macro.
build_prefixed_intrinsics = [
   "load_deref",
   "store_deref",
   "copy_deref",
   "memcpy_deref",

   "load_param",

   "load_global",
   "load_global_constant",
   "store_global",

   "load_reg",
   "store_reg",

   "deref_mode_is",
]

print(Template(template).render(opcodes=opcodes,
                                type_size=type_size,
                                type_base_type=type_base_type,
                                INTR_OPCODES=INTR_OPCODES,
                                WRITE_MASK=WRITE_MASK,
                                ALIGN_MUL=ALIGN_MUL,
                                IO_SEMANTICS=IO_SEMANTICS,
                                DEST_TYPE=DEST_TYPE,
                                SRC_TYPE=SRC_TYPE,
                                build_prefixed_intrinsics=build_prefixed_intrinsics))
