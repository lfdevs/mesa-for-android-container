# encoding=utf-8
# Copyright © 2019 Google

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from __future__ import print_function
import sys
import subprocess
import tempfile
import re
from collections import namedtuple


Test = namedtuple("Test", "name source match_re")


TESTS = [
    Test("simple division",
         """
         uniform mediump float a, b;

         void main()
         {
                 gl_FragColor.rgba = vec4(a / b);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("dot",
         """
         uniform mediump vec2 a, b;

         void main()
         {
                 gl_FragColor.rgba = vec4(dot(a, b));
         }
         """,
         r'\(expression +float16_t +dot\b'),
    Test("array with const index",
         """
         precision mediump float;

         uniform float in_simple[2];

         void main()
         {
                 gl_FragColor = vec4(in_simple[0] / in_simple[1]);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("array with uniform index",
         """
         precision mediump float;

         uniform float in_simple[2];
         uniform int i0, i1;

         void main()
         {
                 gl_FragColor = vec4(in_simple[i0] / in_simple[i1]);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("array-of-array with const index",
         """
         #version 310 es
         precision mediump float;

         uniform float in_aoa[2][2];

         layout(location = 0) out float out_color;

         void main()
         {
                 out_color = in_aoa[0][0] / in_aoa[1][1];
         }
         """,
         r'\(expression +float16_t +/'),
    Test("array-of-array with uniform index",
         """
         #version 310 es
         precision mediump float;

         uniform float in_aoa[2][2];
         uniform int i0, i1;

         layout(location = 0) out float out_color;

         void main()
         {
                 out_color = in_aoa[i0][i0] / in_aoa[i1][i1];
         }
         """,
         r'\(expression +float16_t +/'),
    Test("array index",
         """
         uniform mediump float a, b;
         uniform mediump float values[2];

         void main()
         {
                 gl_FragColor.rgba = vec4(values[int(a / b)]);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("function",
         """
         precision mediump float;

         uniform float a, b;

         mediump float
         get_a()
         {
                 return a;
         }

         float
         get_b()
         {
                 return b;
         }

         void main()
         {
                 gl_FragColor = vec4(get_a() / get_b());
         }
         """,
         r'\(expression +float16_t +/'),
    Test("function mediump args",
         """
         precision mediump float;

         uniform float a, b;

         mediump float
         do_div(float x, float y)
         {
                 return x / y;
         }

         void main()
         {
                 gl_FragColor = vec4(do_div(a, b));
         }
         """,
         r'\(expression +float16_t +/'),
    Test("function highp args",
         """
         precision mediump float;

         uniform float a, b;

         mediump float
         do_div(highp float x, highp float y)
         {
                 return x / y;
         }

         void main()
         {
                 gl_FragColor = vec4(do_div(a, b));
         }
         """,
         r'\(expression +float +/'),
    Test("function inout different precision highp",
         """
         uniform mediump float a, b;

         void
         do_div(inout highp float x, highp float y)
         {
                 x = x / y;
         }

         void main()
         {
                 mediump float temp = a;
                 do_div(temp, b);
                 gl_FragColor = vec4(temp);
         }
         """,
         r'\(expression +float +/'),
    Test("function inout different precision mediump",
         """
         uniform highp float a, b;

         void
         do_div(inout mediump float x, mediump float y)
         {
                 x = x / y;
         }

         void main()
         {
                 highp float temp = a;
                 do_div(temp, b);
                 gl_FragColor = vec4(temp);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("if",
         """
         precision mediump float;

         uniform float a, b;

         void
         main()
         {
                 if (a / b < 0.31)
                         gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
                 else
                         gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("matrix",
         """
         precision mediump float;

         uniform vec2 a;
         uniform mat2 b;

         void main()
         {
             gl_FragColor = vec4(b * a, 0.0, 0.0);
         }
         """,
         r'\(expression +f16vec2 \*.*\bf16mat2\b'),
    Test("simple struct deref",
         """
         precision mediump float;

         struct simple {
                 float a, b;
         };

         uniform simple in_simple;

         void main()
         {
                 gl_FragColor = vec4(in_simple.a / in_simple.b);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("embedded struct deref",
         """
         precision mediump float;

         struct simple {
                 float a, b;
         };

         struct embedded {
                 simple a, b;
         };

         uniform embedded in_embedded;

         void main()
         {
                 gl_FragColor = vec4(in_embedded.a.a / in_embedded.b.b);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("arrayed struct deref",
         """
         precision mediump float;

         struct simple {
                 float a, b;
         };

         struct arrayed {
                 simple a[2];
         };

         uniform arrayed in_arrayed;

         void main()
         {
                 gl_FragColor = vec4(in_arrayed.a[0].a / in_arrayed.a[1].b);
         }
         """,
         r'\(expression +float16_t +/'),
    Test("mixed precision not lowered",
         """
         uniform mediump float a;
         uniform highp float b;

         void main()
         {
                 gl_FragColor = vec4(a / b);
         }
         """,
         r'\(expression +float +/'),
    Test("texture sample",
         """
         precision mediump float;

         uniform sampler2D tex;
         uniform vec2 coord;
         uniform float divisor;

         void main()
         {
                 gl_FragColor = texture2D(tex, coord) / divisor;
         }
         """,
         r'\(expression +f16vec4 +/'),
    Test("expression in lvalue",
         """
         uniform mediump float a, b;

         void main()
         {
                 gl_FragColor = vec4(1.0);
                 gl_FragColor[int(a / b)] = 0.5;
         }
         """,
         r'\(expression +float16_t +/'),
    Test("builtin with const arg",
         """
         uniform mediump float a;

         void main()
         {
                 gl_FragColor = vec4(min(a, 3.0));
         }
         """,
         r'\(expression +float16_t min'),
]


def compile_shader(standalone_compiler, source):
    with tempfile.NamedTemporaryFile(mode='wt', suffix='.frag') as source_file:
        print(source, file=source_file)
        source_file.flush()
        return subprocess.check_output([standalone_compiler,
                                        '--version', '300',
                                        '--lower-precision',
                                        '--dump-lir',
                                        source_file.name],
                                       universal_newlines=True)


def run_test(standalone_compiler, test):
    ir = compile_shader(standalone_compiler, test.source)

    if re.search(test.match_re, ir) is None:
        return False

    return True


def main():
    standalone_compiler = sys.argv[1]
    passed = 0

    for test in TESTS:
        print('Testing {} ... '.format(test.name), end='')

        result = run_test(standalone_compiler, test)

        if result:
            print('PASS')
            passed += 1
        else:
            print('FAIL')

    print('{}/{} tests returned correct results'.format(passed, len(TESTS)))
    sys.exit(0 if passed == len(TESTS) else 1)


if __name__ == '__main__':
    main()
