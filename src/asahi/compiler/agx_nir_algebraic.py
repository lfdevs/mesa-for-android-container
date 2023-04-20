# Copyright 2022 Alyssa Rosenzweig
# Copyright 2021 Collabora, Ltd.
# Copyright 2016 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys
import math

a = 'a'
b = 'b'
c = 'c'

lower_sm5_shift = []

# Our shifts differ from SM5 for the upper bits. Mask to match the NIR
# behaviour. Because this happens as a late lowering, NIR won't optimize the
# masking back out (that happens in the main nir_opt_algebraic).
for s in [8, 16, 32, 64]:
    for shift in ["ishl", "ishr", "ushr"]:
        lower_sm5_shift += [((shift, f'a@{s}', b),
                             (shift, a, ('iand', b, s - 1)))]

lower_pack = [
    (('pack_half_2x16_split', a, b),
     ('pack_32_2x16_split', ('f2f16', a), ('f2f16', b))),

    (('unpack_half_2x16_split_x', a), ('f2f32', ('unpack_32_2x16_split_x', a))),
    (('unpack_half_2x16_split_y', a), ('f2f32', ('unpack_32_2x16_split_y', a))),

    (('extract_u16', 'a@32', 0), ('u2u32', ('unpack_32_2x16_split_x', a))),
    (('extract_u16', 'a@32', 1), ('u2u32', ('unpack_32_2x16_split_y', a))),
    (('extract_i16', 'a@32', 0), ('i2i32', ('unpack_32_2x16_split_x', a))),
    (('extract_i16', 'a@32', 1), ('i2i32', ('unpack_32_2x16_split_y', a))),

    # For optimizing extract->convert sequences for unpack/pack norm
    (('u2f32', ('u2u32', a)), ('u2f32', a)),
    (('i2f32', ('i2i32', a)), ('i2f32', a)),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()

def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "agx_nir.h"')

    print(nir_algebraic.AlgebraicPass("agx_nir_lower_algebraic_late",
                                      lower_sm5_shift + lower_pack).render())


if __name__ == '__main__':
    main()
