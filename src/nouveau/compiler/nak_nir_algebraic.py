# Copyright (C) 2023 Red Hat, Inc.
# Copyright (C) 2021 Collabora, Ltd.
# Copyright (C) 2016 Intel Corporation
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

import argparse
import sys

a = 'a'
b = 'b'
c = 'c'
s = 's'

# common conditions to improve readability
volta = 'nak->sm >= 70 && nak->sm < 73'
fp16_round_is_rtz = 'nir_is_rounding_mode_rtz(info->float_controls_execution_mode, 16)'

algebraic_lowering = [
    # Volta doesn't have `IMNMX`
    (('imin', 'a', 'b'), ('bcsel', ('ilt', a, b), a, b), volta),
    (('imax', 'a', 'b'), ('bcsel', ('ilt', a, b), b, a), volta),
    (('umin', 'a', 'b'), ('bcsel', ('ult', a, b), a, b), volta),
    (('umax', 'a', 'b'), ('bcsel', ('ult', a, b), b, a), volta),
    (('iadd', 'a@64', ('ineg', 'b@64')), ('isub', a, b)),

    (('iadd', ('iadd(is_only_used_by_iadd)', 'a(is_not_const)', '#b'), 'c(is_not_const)'),
        ('iadd3', a, b, c), 'options->has_iadd3'),
    (('iadd', ('iadd(is_only_used_by_iadd)', 'a(is_not_const)', 'b(is_not_const)'), '#c'),
        ('iadd3', a, b, c), 'options->has_iadd3'),

    (('iadd', ('ineg(is_only_used_by_iadd)', ('iadd(is_used_once)', 'a(is_not_const)', '#b')), 'c(is_not_const)'),
        ('iadd3', ('ineg', a), ('ineg', b), c), 'options->has_iadd3'),
    (('iadd', ('ineg(is_only_used_by_iadd)', ('iadd(is_used_once)', 'a(is_not_const)', 'b(is_not_const)')), '#c'),
        ('iadd3', ('ineg', a), c, ('ineg', b)), 'options->has_iadd3'),

    (('iadd', 'a@32', ('ishl', 'b@32', '#s@32')),
        ('lea_nv', a, b, s), 'nak->sm >= 70'),
    (('iadd', 'a@64', ('ishl', 'b@64', '#s@32')),
        ('lea_nv', a, b, s), 'nak->sm >= 70'),
]

for f2f16 in ['f2f16', 'f2f16_rtz', 'f2f16_rtne']:
    algebraic_lowering += [
        (('vec2', (f2f16 + '(is_used_once)', 'a@32'), (f2f16 + '(is_used_once)', 'b@32')), (f2f16, ('vec2', a, b)), 'nak->sm >= 86')
    ]

# If we find mufu surrounded by bit_size conversions, just do the op in the
# original bit_size.
# MUFU.F16 internally appears to operate with the same precision as F32 does
# with the result being rounded towards zero to F16. EXP2 and RCP seem to be
# off by one around Inf, so it's only safe if we can ignore inf for those.
#
# This was verified with the `hw_tests::test_op_mufu_f16_down`.

# mufu.f16 for those is identical to mufu.f32 with rtz rounding except for results around infinity
for op in ['fexp2', 'frcp']:
    algebraic_lowering += [
        (('f2f16_rtz(ninf)', (op + '(is_used_once)', ('f2f32', 'a@16'))), (op, a), 'nak->sm >= 73'),
        (('f2f16(ninf)', (op + '(is_used_once)', ('f2f32', 'a@16'))),
            (op, a),
            'nak->sm >= 73 && ' + fp16_round_is_rtz),
    ]

# mufu.f16 for those is identical to mufu.f32 with rtz rounding
for op in ['fcos_normalized_2_pi', 'flog2', 'frsq', 'fsin_normalized_2_pi', 'fsqrt']:
    algebraic_lowering += [
        (('f2f16_rtz', (op + '(is_used_once)', ('f2f32', 'a@16'))), (op, a), 'nak->sm >= 73'),
        (('f2f16', (op + '(is_used_once)', ('f2f32', 'a@16'))),
            (op, a),
            'nak->sm >= 73 && ' + fp16_round_is_rtz),
    ]

# If contract is on we can always remove the conversions
for op in ['fcos_normalized_2_pi', 'fexp2', 'flog2', 'frcp', 'frsq', 'fsin_normalized_2_pi', 'fsqrt']:
    for f2f16 in ['f2f16_rtz', 'f2f16_rtne', 'f2f16']:
        algebraic_lowering += [
            ((f2f16 + '(contract)', (op + '(is_used_once)', ('f2f32', 'a@16'))),
                (op, a), 'nak->sm >= 73'),
            (('f2f32(contract)', (op + '(is_used_once)', (f2f16, 'a@32'))), (op, a)),
    ]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True, help='Output file.')
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)

    import nir_algebraic  # pylint: disable=import-error

    try:
        with open(args.out, 'w', encoding='utf-8') as f:
            f.write('#include "nak_private.h"')
            f.write(nir_algebraic.AlgebraicPass(
                "nak_nir_lower_algebraic_late",
                algebraic_lowering,
                [
                    ("const struct nak_compiler *", "nak"),
                ]).render())
    except Exception:
        sys.exit(1)

if __name__ == '__main__':
    main()
