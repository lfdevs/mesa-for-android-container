# Copyright © 2024 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys
from math import pi

a = 'a'
b = 'b'

lower_fsign = [
    # The true branch of the fcsel elides ±1*X, so the pattern must be
    # conditioned for that using is_only_used_as_float (see
    # nir_opt_algebraic.py). The false branch elides 0*x, so the pattern must
    # also be conditioned for that using either nsz,nnan or nsz with
    # is_finite.
    #
    # NOTE: fcsel opcodes are currently limited to float32 in NIR.
    (('fmul@32(is_only_used_as_float)', ('fsign(is_used_once)', 'a(is_not_negative)'), b), ('fcsel_gt',          a ,          b , ('fmul', b, 0.0       ))),
    (('~fmul@32',                       ('fsign(is_used_once)', 'a(is_not_negative)'), b), ('fcsel_gt',          a ,          b ,             0.0        )),
    (('fmul@32(is_only_used_as_float)', ('fsign(is_used_once)', 'a(is_not_positive)'), b), ('fcsel_gt', ('fneg', a), ('fneg', b), ('fmul', b, 0x80000000))),
    (('~fmul@32',                       ('fsign(is_used_once)', 'a(is_not_positive)'), b), ('fcsel_gt', ('fneg', a), ('fneg', b),             0x80000000 )),

    (('fmul@16(is_only_used_as_float)', ('fsign(is_used_once)', 'a(is_not_negative)'), b), ('bcsel', ('!flt', 0,          a ),          b , ('fmul', b, 0.0   ))),
    (('~fmul@16',                       ('fsign(is_used_once)', 'a(is_not_negative)'), b), ('bcsel', ('!flt', 0,          a ),          b ,             0.0    )),
    (('fmul@16(is_only_used_as_float)', ('fsign(is_used_once)', 'a(is_not_positive)'), b), ('bcsel', ('!flt', 0, ('fneg', a)), ('fneg', b), ('fmul', b, 0x8000))),
    (('~fmul@16',                       ('fsign(is_used_once)', 'a(is_not_positive)'), b), ('bcsel', ('!flt', 0, ('fneg', a)), ('fneg', b),             0x8000 )),

    (('fmul@32(is_only_used_as_float,nsz)',      ('fsign(is_used_once)', a), 'b(is_finite)'), ('fcsel_gt', a, b, ('fcsel_gt', ('fneg', a), ('fneg', b), 0.0))),
    (('fmul@32(is_only_used_as_float,nsz,nnan)', ('fsign(is_used_once)', a),  b            ), ('fcsel_gt', a, b, ('fcsel_gt', ('fneg', a), ('fneg', b), 0.0))),
    (('~fmul@32',                                ('fsign(is_used_once)', a),  b            ), ('fcsel_gt', a, b, ('fcsel_gt', ('fneg', a), ('fneg', b), 0.0))),

    # This is 99.99% strictly correct for OpenCL. It will provide correctly
    # signed zero for ±0 inputs, and it will provide zero for NaN inputs. The
    # only slight deviation is that it can provide -0 for some NaN inputs.
    (('fsign@32', a), ('fcsel_gt',          ('fabs', a) , ('ior', ('iand', a, 0x80000000), 0x3f800000), ('iand', a, 0x80000000))),
    (('fsign@16', a), ('bcsel', ('!flt', 0, ('fabs', a)), ('ior', ('iand', a, 0x8000    ), 0x3c00    ), ('iand', a, 0x8000    ))),

    # The only effect a*0.0 should have is when 'a' is infinity, -0.0 or NaN
    (('fmul(nsz,nnan)', 'a', 0.0), 0.0),
    (('fmul(nsz)', 'a(is_finite)', 0.0), 0.0),
    (('fmul(nsz,nnan)', 'a@32', 0x80000000), 0.0),
    (('fmul(nsz,nnan)', 'a@16', 0x8000    ), 0.0),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "brw_nir.h"')

    print(nir_algebraic.AlgebraicPass("brw_nir_lower_fsign", lower_fsign).render())


if __name__ == '__main__':
    main()
