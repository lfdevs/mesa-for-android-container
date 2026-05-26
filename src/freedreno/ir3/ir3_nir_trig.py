#
# Copyright © 2016 Intel Corporation
#
# SPDX-License-Identifier: MIT

import argparse
import sys

trig_workarounds = [
   # !fmad as reassociation could make this not precise enough.
   (('fsin', 'x@32'), ('fsin', ('!fmad', 6.2831853, ('ffract', ('!fmad', 0.15915494, 'x', 0.5)), -3.14159265))),
   (('fcos', 'x@32'), ('fcos', ('!fmad', 6.2831853, ('ffract', ('!fmad', 0.15915494, 'x', 0.5)), -3.14159265))),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "ir3_nir.h"')
    print(nir_algebraic.AlgebraicPass("ir3_nir_apply_trig_workarounds",
                                      trig_workarounds).render())


if __name__ == '__main__':
    main()
