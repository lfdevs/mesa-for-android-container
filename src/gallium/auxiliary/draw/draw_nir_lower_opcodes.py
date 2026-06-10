#
# Copyright 2026 Advanced Micro Devices, Inc.
#
# SPDX-License-Identifier: MIT

import argparse
import sys

a, b, c = 'a', 'b', 'c'

# This contains lowerings for opcodes that driver-specific optimizations
# may introduce but gallivm does not support.
lower = [
   (('fmulz', a, b),    ('bcsel', ('ior', ('feq', a, 0.0), ('feq', b, 0.0)), 0.0, ('fmul', a, b))),
   (('ffmaz', a, b, c), ('bcsel', ('ior', ('feq', a, 0.0), ('feq', b, 0.0)), c,   ('ffma_weak', a, b, c))),

   # draw shaders are GLSL shaders, which means we can just use ffma_weak
   (('ffma', a, b, c),  ('ffma_weak', a, b, c)),

   (('bitfield_select', a, b, c), ('ixor', c, ('iand', a, ('ixor', b, c)))),
   (('ubfe', a, b, c), ('ubitfield_extract', a, ('iand', b, 0x1f), ('iand', c, 0x1f))),
   (('ibfe', a, b, c), ('ibitfield_extract', a, ('iand', b, 0x1f), ('iand', c, 0x1f))),
   (('bfm', a, b), ('ishl', ('isub', ('ishl', 1, ('iand', a, 0x1f)), 1), ('iand', b, 0x1f))),
   # bfi(a, b, c) -> bitfield_select(a, ishl(b, find_lsb(a)), c)
   (('bfi', a, b, c), ('ixor', c, ('iand', a, ('ixor', ('ishl', b, ('find_lsb', a)), c)))),

   (('ufind_msb_rev', a), ('bcsel', ('ige', ('ufind_msb', a), 0), ('isub', 31, ('ufind_msb', a)), -1)),
   (('ifind_msb_rev', a), ('bcsel', ('ige', ('ifind_msb', a), 0), ('isub', 31, ('ifind_msb', a)), -1)),

   # uclz(a) -> umin(32, ufind_msb_rev(a))
   (('uclz', a), ('umin', 32, ('bcsel', ('ige', ('ufind_msb', a), 0), ('isub', 31, ('ufind_msb', a)), -1))),

   (('shfr', a, b, c),
    ('bcsel', ('ieq', ('iand', c, 0x1f), 0), b,
                      ('ior', ('ishl', a, ('iadd', 32, ('ineg', ('iand', c, 0x1f)))),
                              ('ushr', b, ('iand', c, 0x1f))))),
]

parser = argparse.ArgumentParser()
parser.add_argument('-p', '--import-path', required=True)
args = parser.parse_args()
sys.path.insert(0, args.import_path)

import nir_algebraic

p = nir_algebraic.AlgebraicPass("draw_nir_lower_opcodes", lower)
print('#include "draw/draw_nir.h"')
print(p.render())
