#
# Copyright 2025 Collabora Ltd.
# SPDX-License-Identifier: MIT
#

import argparse
import sys

lower_alu = [


   # this partially duplicates stuff from nir_opt_algebraic,
   # but without the 'is_used_once' because on r600 the sequence
   #   c = comp(a, b)
   #   d = inot(c)
   # requires two instruction groups whereas
   #   c = comp(a, b)
   #   d = comp_inv(a, b)
   # can be put into one instruction group, that is, if c is not used
   # we reduced the code by one instruction and potentially one instruction
   # group, if c is used, the we still may need one instruction group less.

   (('inot', ('flt', 'a(is_a_number)', 'b(is_a_number)')), ('fge', 'a', 'b')),
   (('inot', ('fge', 'a(is_a_number)', 'b(is_a_number)')), ('flt', 'a', 'b')),

   (('inot', ('fneu', 'a', 'b')), ('feq', 'a', 'b')),
   (('inot', ('feq', 'a', 'b')), ('fneu', 'a', 'b')),

   (('inot', ('ilt', 'a', 'b')), ('ige', 'a', 'b')),
   (('inot', ('ige', 'a', 'b')), ('ilt', 'a', 'b')),
   (('inot', ('ult', 'a', 'b')), ('uge', 'a', 'b')),
   (('inot', ('uge', 'a', 'b')), ('ult', 'a', 'b')),
   (('inot', ('ieq', 'a', 'b')), ('ine', 'a', 'b')),
   (('inot', ('ine', 'a', 'b')), ('ieq', 'a', 'b')),

   (('b2f32', ('fge', 'a@32', 'b@32')), ('sge', 'a', 'b')),
   (('b2f32', ('flt', 'a@32', 'b@32')), ('slt', 'a', 'b')),
   (('b2f32', ('feq', 'a@32', 'b@32')), ('seq', 'a', 'b')),
   (('b2f32', ('fneu', 'a@32', 'b@32')), ('sne', 'a', 'b')),

   (('flt', ('fadd', 'a', 'b'), 0.0), ('flt', 'a', ('fneg', 'b'))),
   (('flt', 0.0, ('fadd', 'a', 'b')), ('flt', ('fneg', 'b'), 'a')),

   (('slt', ('fadd', 'a', 'b'), 0.0), ('slt', 'a', ('fneg', 'b'))),
   (('slt', 0.0, ('fadd', 'a', 'b')), ('slt', ('fneg', 'b'), 'a')),

   (('sge', ('fadd', 'a', 'b'), 0.0), ('sge', 'a', ('fneg', 'b'))),
   (('sge', 0.0, ('fadd', 'a', 'b')), ('sge', ('fneg', 'b'), 'a')),

   (('seq', ('fadd', 'a', 'b'), 0.0), ('seq', 'a', ('fneg', 'b'))),
   (('sne', ('fadd', 'a', 'b'), 0.0), ('sne', 'a', ('fneg', 'b'))),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "nir_search_helpers.h"')
    print('#include "sfn/sfn_nir.h"')

    print(nir_algebraic.AlgebraicPass("r600_sfn_lower_alu",
                                      lower_alu).render())

if __name__ == '__main__':
    main()
