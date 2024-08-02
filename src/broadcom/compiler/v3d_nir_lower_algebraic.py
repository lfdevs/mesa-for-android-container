# SPDX-License-Identifier: MIT

import argparse
import sys

a = 'a'

lower_alu = [
    (('f2i8', a), ('i2i8', ('f2i32', a))),
    (('f2i16', a), ('i2i16', ('f2i32', a))),

    (('f2u8', a), ('u2u8', ('f2u32', a))),
    (('f2u16', a), ('u2u16', ('f2u32', a))),

    (('i2f32', 'a@8'), ('i2f32', ('i2i32', a))),
    (('i2f32', 'a@16'), ('i2f32', ('i2i32', a))),

    (('u2f32', 'a@8'), ('u2f32', ('u2u32', a))),
    (('u2f32', 'a@16'), ('u2f32', ('u2u32', a))),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()

def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "v3d_compiler.h"')

    print(nir_algebraic.AlgebraicPass("v3d_nir_lower_algebraic",
                                      lower_alu).render())

if __name__ == '__main__':
    main()
