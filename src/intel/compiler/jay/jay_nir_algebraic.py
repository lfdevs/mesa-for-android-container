# Copyright 2024 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys
from math import pi

a = 'a'
b = 'b'
c = 'c'

lower_fsign = [
    (('fsign', a), ('bcsel', ('!flt', 0, a), +1.0,
                    ('bcsel', ('!flt', a, 0), -1.0, 0.0))),
    (('fceil', a), ('fneg', ('ffloor', ('fneg', a)))),

    # inot is free on and/or/xor sources but not dests. Apply De Morgan's.
    (('inot', ('iand(is_used_once)', ('inot', a), b)), ('ior', a, ('inot', b))),
    (('inot', ('ior(is_used_once)', ('inot', a), b)), ('iand', a, ('inot', b))),
    (('inot', ('ixor(is_used_once)', ('inot', a), b)), ('ixor', a, b)),
    (('inot', ('iand(is_used_once)', a, b)), ('ior', ('inot', a), ('inot', b))),
    (('inot', ('ior(is_used_once)', a, b)), ('iand', ('inot', a), ('inot', b))),
    (('inot', ('ixor(is_used_once)', a, b)), ('ixor', ('inot', a), b)),

    # Remove the zeroing. Down-conversion is free but extracts are not.
    (('u2f32', ('extract_u8', a, 0)), ('u2f32', ('u2u8', a))),
    (('u2f32', ('extract_u16', a, 0)), ('u2f32', ('u2u16', a))),
    (('i2f32', ('extract_i8', a, 0)), ('i2f32', ('i2i8', a))),
    (('i2f32', ('extract_i16', a, 0)), ('i2f32', ('i2i16', a))),

    (('pack_half_2x16_split', a, b),
     ('pack_32_2x16_split', ('f2f16', a), ('f2f16', b))),

    # Allows us to use more modifiers
    (('bcsel', a, ('iadd(is_used_once)', b, c), b),
     ('iadd', ('bcsel', a, c, 0), b)),
]


lower_bool = [
    # Try to use conditional modifiers more
    (('ieq', ('iand(is_used_once)', a, b), b),
     ('ieq', ('iand', ('inot', a), b), 0)),
    (('ine', ('iand(is_used_once)', a, b), b),
     ('ine', ('iand', ('inot', a), b), 0)),
]

for T, sizes, one in [('f', [16, 32], 1.0),
                      ('i', [8, 16, 32], 1),
                      ('b', [8, 16, 32], -1)]:
    for sz in sizes:
        if T in ['f', 'i']:
            lower_bool.extend([
                ((f'{T}neg', (f'b2{T}{sz}', ('inot', 'a@1'))),
                 ('bcsel', a, 0, -one)),
                ((f'{T}neg', (f'b2{T}{sz}', 'a@1')), ('bcsel', a, -one, 0)),
            ])

        lower_bool.extend([
            ((f'b2{T}{sz}', ('inot', 'a@1')), ('bcsel', a, 0, one)),
            ((f'b2{T}{sz}', 'a@1'), ('bcsel', a, one, 0)),
        ])

lower_bool.extend([
    ((f'b2i64', 'a@1'), ('pack_64_2x32_split', ('bcsel', a, 1, 0), 0)),
])

opt_sel_zero = [
    (('bcsel@32', a, 0, 1), ('iadd', ('bcsel', a, 0xffffffff, 0), 1)),
    (('bcsel@32', a, 1, 0), ('ineg', ('bcsel', a, 0xffffffff, 0))),
]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('output')
    args = parser.parse_args()

    sys.path.insert(0, args.import_path)
    import nir_algebraic  # pylint: disable=import-error

    with open(args.output, 'w', encoding='utf-8') as f:
        f.write('#include "jay_private.h"')

        f.write(nir_algebraic.AlgebraicPass(
            "jay_nir_lower_fsign", lower_fsign).render())
        f.write(nir_algebraic.AlgebraicPass(
            "jay_nir_lower_bool", lower_bool).render())
        f.write(nir_algebraic.AlgebraicPass(
            "jay_nir_opt_sel_zero", opt_sel_zero).render())


if __name__ == '__main__':
    main()
