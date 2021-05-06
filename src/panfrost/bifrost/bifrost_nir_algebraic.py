#
# Copyright (C) 2020 Collabora, Ltd.
# Copyright (C) 2018 Alyssa Rosenzweig
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
import math

a = 'a'
b = 'b'
c = 'c'

algebraic_late = [
    # ineg must be lowered late, but only for integers; floats will try to
    # have modifiers attached... hence why this has to be here rather than
    # a more standard lower_negate approach

    (('ineg', a), ('isub', 0, a)),
]

for isz in ('8', '16', '32'):
        for osz in ('16', '32', '64'):
                algebraic_late += [(('b2f' + osz, 'a@' + isz), ('b' + isz + 'csel', a, 1.0, 0.0))]

# There's no native integer min/max instruction, lower those to cmp+bcsel
for sz in ('8', '16', '32'):
    for t in ('i', 'u'):
        algebraic_late += [
            ((t + 'min', 'a@' + sz, 'b@' + sz), ('b' + sz + 'csel', (t + 'lt' + sz, a, b), a, b)),
            ((t + 'max', 'a@' + sz, 'b@' + sz), ('b' + sz + 'csel', (t + 'lt' + sz, b, a), a, b))
        ]

# Bifrost doesn't have fp16 for a lot of special ops
SPECIAL = ['fexp2', 'flog2', 'fsin', 'fcos']

for op in SPECIAL:
        algebraic_late += [((op + '@16', a), ('f2f16', (op, ('f2f32', a))))]

algebraic_late += [(('f2b32', a), ('fneu32', a, 0.0)),
             (('i2b32', a), ('ine32', a, 0)),
             (('b2i32', a), ('iand', 'a@32', 1))]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    run()


def run():
    import nir_algebraic  # pylint: disable=import-error

    print('#include "bifrost_nir.h"')

    print(nir_algebraic.AlgebraicPass("bifrost_nir_lower_algebraic_late",
                                      algebraic_late).render())

if __name__ == '__main__':
    main()
