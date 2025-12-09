# Copyright © 2025 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

import argparse
import sys
import math

a = 'a'
b = 'b'

lower_algebraic = []
lower_algebraic_late = []

def lowered_fround_even(src):
   abs_src = ('fabs', src)
   ffloor_temp = ('ffloor', abs_src)
   ffract_temp = ('ffract', abs_src)

   ceil_temp = ('fadd', ffloor_temp, 1.0)
   even_temp = ('fmul', ffloor_temp, 0.5)

   even_ffract_temp = ('ffract', even_temp)

   ishalf_temp = ('feq', ffract_temp, 0.5)
   ffract_temp = ('bcsel', ishalf_temp, even_ffract_temp, ffract_temp)

   lesshalf_temp = ('flt', ffract_temp, 0.5)
   result_temp = ('bcsel', lesshalf_temp, ffloor_temp, ceil_temp)

   res = ('fcopysign_pco', result_temp, src)
   return res

lower_algebraic.append((('fround_even', a), lowered_fround_even(a)))

lower_insert_extract = [
   (('insert_u8', 'a@32', b), ('bitfield_insert', 0, a, ('imul', b, 8), 8)),
   (('insert_u16', 'a@32', b), ('bitfield_insert', 0, a, ('imul', b, 16), 16)),

   (('extract_u8', 'a@32', b), ('ubitfield_extract', a, ('imul', b, 8), 8)),
   (('extract_i8', 'a@32', b), ('ibitfield_extract', a, ('imul', b, 8), 8)),

   (('extract_u16', 'a@32', b), ('ubitfield_extract', a, ('imul', b, 16), 16)),
   (('extract_i16', 'a@32', b), ('ibitfield_extract', a, ('imul', b, 16), 16)),
]

lower_algebraic_late.extend(lower_insert_extract)

lower_b2b = [
   (('b2b32', a), ('ineg', ('b2i32', a))),
   (('b2b1', a), ('ine', a, 0)),
]

lower_algebraic.extend(lower_b2b)

lower_scmp = [
   # Float comparisons + bool conversions.
   (('b2f32', ('flt', a, b)), ('slt', a, 'b@32'), '!options->lower_scmp'),
   (('b2f32', ('fge', a, b)), ('sge', a, 'b@32'), '!options->lower_scmp'),
   (('b2f32', ('feq', a, b)), ('seq', a, 'b@32'), '!options->lower_scmp'),
   (('b2f32', ('fneu', a, b)), ('sne', a, 'b@32'), '!options->lower_scmp'),

   # Float comparisons + bool conversions via bcsel.
   (('bcsel@32', ('flt', a, b), 1.0, 0.0), ('slt', a, 'b@32'), '!options->lower_scmp'),
   (('bcsel@32', ('fge', a, b), 1.0, 0.0), ('sge', a, 'b@32'), '!options->lower_scmp'),
   (('bcsel@32', ('feq', a, b), 1.0, 0.0), ('seq', a, 'b@32'), '!options->lower_scmp'),
   (('bcsel@32', ('fneu', a, b), 1.0, 0.0), ('sne', a, 'b@32'), '!options->lower_scmp'),
]

lower_algebraic_late.extend(lower_scmp)

# TODO: core-specific info.
params=[]

def run():
    import nir_algebraic  # pylint: disable=import-error
    print('#include "pco_internal.h"')
    print(nir_algebraic.AlgebraicPass('pco_nir_lower_algebraic', lower_algebraic, params=params).render())
    print(nir_algebraic.AlgebraicPass('pco_nir_lower_algebraic_late', lower_algebraic_late, params=params).render())

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()

if __name__ == '__main__':
    main()
