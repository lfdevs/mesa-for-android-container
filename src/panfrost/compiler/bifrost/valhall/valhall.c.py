#encoding=utf-8

# Copyright (C) 2021 Collabora, Ltd.
# SPDX-License-Identifier: MIT

import argparse
import sys
from valhall import valhall_parse_isa
from mako.template import Template
from mako import exceptions

parser = argparse.ArgumentParser()
parser.add_argument('--xml', required=True, help='Input ISA XML file')

args = parser.parse_args()

(instructions, immediates, enums, typesize, safe_name) = valhall_parse_isa(args.xml)

SKIP = set([
        # Extra conversions
        "S8_TO_S16",
        "S8_TO_F16",
        "U8_TO_U16",
        "U8_TO_F16",

        # Saturating multiplies
        "IMUL.s32",
        "IMUL.v2s16",
        "IMUL.v4s8",

        # 64-bit support
        "IADD.u64",
        "IADD.s64",
        "ISUB.u64",
        "ISUB.s64",
        "IMULD.u64",
        "IMULD.u64",
        "LSHIFT_AND.i64",
        "RSHIFT_AND.i64",
        "LSHIFT_OR.i64",
        "RSHIFT_OR.i64",
        "LSHIFT_XOR.i64",
        "RSHIFT_XOR.i64",

        # VAR_TEX
        "VAR_TEX_SINGLE",
        "VAR_TEX_GATHER",
        "VAR_TEX_GRADIENT",
        "VAR_TEX_DUAL",
        "VAR_TEX_BUF_SINGLE",
        "VAR_TEX_BUF_GATHER",
        "VAR_TEX_BUF_GRADIENT",
        "VAR_TEX_BUF_DUAL",

        # Special cased
        "FMA_RSCALE_N.f32",
        "FMA_RSCALE_LEFT.f32",
        "FMA_RSCALE_SCALE16.f32",

        # Deprecated instruction
        "NOT_OLD.i32",
        "NOT_OLD.i64",

        # TODO
        "FATAN_ASSIST.f32",
        "SEG_ADD.u64",
        "TEX_DUAL",
    ])

template = """
#include "valhall.h"
#include "bi_opcodes.h"

const uint32_t valhall_immediates[32] = {
% for imm in immediates:
    ${hex(imm)},
% endfor
};

<%
def ibool(x):
    return '1' if x else '0'

def hasmod(x, mod):
    return ibool(any([x.name == mod for x in op.modifiers]))

%>
const struct va_opcode_info
valhall_opcodes[BI_NUM_OPCODES] = {
% for op in instructions:
% if op.name not in skip:
<%
    name = op.name
    if name == 'BRANCHZ':
        name = 'BRANCHZ.i16'

    sr_control = 0

    if len(op.staging) > 0:
        sr_control = op.staging[0].encoded_flags >> 6
%>
    [BI_OPCODE_${name.replace('.', '_').upper()}] = {
        .exact = ${hex(exact(op))}ULL,
        .srcs = {
% for src in ([sr for sr in op.staging if sr.read] + op.srcs):
            {
                .absneg = ${ibool(src.absneg)},
                .swizzle = ${ibool(src.swizzle)},
                .notted = ${ibool(src.notted)},
                .widen = ${ibool(src.widen)},
                .lanes = ${ibool(src.lanes)},
                .halfswizzle = ${ibool(src.halfswizzle)},
                .lane = ${ibool(src.lane)},
                .combine = ${ibool(src.combine)},
% if src.size in [8, 16, 32, 64]:
                .size = VA_SIZE_${src.size},
% endif
            },
% endfor
        },
        .type_size = ${typesize(op.name)},
        .has_dest = ${ibool(len(op.dests) > 0)},
        .is_signed = ${ibool(op.is_signed)},
        .unit = VA_UNIT_${op.unit},
        .nr_srcs = ${len(op.srcs)},
        .nr_staging_srcs = ${sum([sr.read for sr in op.staging])},
        .nr_staging_dests = ${sum([sr.write for sr in op.staging])},
        .clamp = ${hasmod(x, 'clamp')},
        .saturate = ${hasmod(x, 'saturate')},
        .rhadd = ${hasmod(x, 'rhadd')},
        .round_mode = ${hasmod(x, 'round_mode')},
        .condition = ${hasmod(x, 'condition')},
        .result_type = ${hasmod(x, 'result_type')},
        .vecsize = ${hasmod(x, 'vector_size')},
        .register_format = ${hasmod(x, 'register_format')},
        .slot = ${hasmod(x, 'slot')},
        .sr_count = ${hasmod(x, 'staging_register_count')},
        .sr_write_count = ${hasmod(x, 'staging_register_write_count')},
        .sr_control = ${sr_control},
    },
% endif
% endfor
};
"""

# Exact value to be ORed in to every opcode
def exact_op(op):
    exact_op = 0
    for subcode in op.opcode:
        exact_op |= (subcode.value << subcode.start)
    return exact_op

try:
    print(Template(template).render(immediates = immediates, instructions = instructions, skip = SKIP, exact = exact_op, typesize = typesize))
except:
    print(exceptions.text_error_template().render())
