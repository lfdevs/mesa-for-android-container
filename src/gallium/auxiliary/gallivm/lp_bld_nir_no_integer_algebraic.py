#
# Copyright 2026 Pavel Ondračka <pavel.ondracka@gmail.com>
#
# SPDX-License-Identifier: MIT

import argparse
import sys

a, b, c = 'a', 'b', 'c'

no_integers = [
   # lp_nir_lower_if_float_cond turns f32 if conditions back into booleans by
   # comparing them with zero. Clean up the common forms before lowerings.
   (('fneu', ('seq', 'a@32', 'b@32'), 0.0), ('feq', a, b)),
   (('fneu', ('sne', 'a@32', 'b@32'), 0.0), ('fneu', a, b)),
   (('fneu', ('slt', 'a@32', 'b@32'), 0.0), ('flt', a, b)),
   (('fneu', ('sge', 'a@32', 'b@32'), 0.0), ('fge', a, b)),
   (('fneu', ('bcsel', a, 1.0, 0.0), 0.0), a),
   (('fneu', ('fcsel', a, 1.0, 0.0), 0.0), ('fneu', a, 0.0)),
   (('fneu', ('fcsel_gt', a, 1.0, 0.0), 0.0), ('flt', 0.0, a)),
   (('fneu', ('fcsel_ge', a, 1.0, 0.0), 0.0), ('fge', a, 0.0)),

   # Lower the remaining unsupported float opcodes back to bools.
   (('seq', 'a@32', 'b@32'), ('bcsel', ('feq', a, b), 1.0, 0.0)),
   (('sne', 'a@32', 'b@32'), ('bcsel', ('fneu', a, b), 1.0, 0.0)),
   (('slt', 'a@32', 'b@32'), ('bcsel', ('flt', a, b), 1.0, 0.0)),
   (('sge', 'a@32', 'b@32'), ('bcsel', ('fge', a, b), 1.0, 0.0)),
   (('fcsel', 'a@32', b, c), ('bcsel', ('fneu', a, 0.0), b, c)),
   (('fcsel_gt', 'a@32', b, c), ('bcsel', ('flt', 0.0, a), b, c)),
   (('fcsel_ge', 'a@32', b, c), ('bcsel', ('fge', a, 0.0), b, c)),
]

parser = argparse.ArgumentParser()
parser.add_argument('-p', '--import-path', required=True)
args = parser.parse_args()
sys.path.insert(0, args.import_path)

import nir_algebraic

p = nir_algebraic.AlgebraicPass("lp_nir_no_integer_lowering", no_integers)
print('#include "gallivm/lp_bld_nir.h"')
print(p.render())
