# Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
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

# Python source
from __future__ import print_function
import os
import sys
import knob_defs
from mako.template import Template
from mako.exceptions import RichTraceback

def write_template_to_string(template_filename, **kwargs):
    try:
        template = Template(filename=template_filename)
        # Split + Join fixes line-endings for whatever platform you are using
        return '\n'.join(template.render(**kwargs).splitlines())
    except:
        traceback = RichTraceback()
        for (filename, lineno, function, line) in traceback.traceback:
            print("File %s, line %s, in %s" % (filename, lineno, function))
            print(line, "\n")
        print("%s: %s" % (str(traceback.error.__class__.__name__), traceback.error))

def write_template_to_file(template_filename, output_filename, **kwargs):
    with open(output_filename, "w") as outfile:
        print(write_template_to_string(template_filename, **kwargs), file=outfile)

def main(args=sys.argv[1:]):
    if len(args) != 1:
        print('Usage:', sys.argv[0], '<output_directory>', file=sys.stderr)
        return 1

    output_dir = args[0]
    if not os.path.isdir(output_dir):
        if os.path.exists(output_dir):
            print('ERROR: Invalid output directory:', output_dir, file=sys.stderr)
            return 1

        try:
            os.makedirs(output_dir)
        except:
            print('ERROR: Could not create output directory:', output_dir, file=sys.stderr)
            return 1

    # Output path exists, now just run the template
    template_file = os.sep.join([sys.path[0], 'templates', 'knobs.template'])
    output_file = os.sep.join([output_dir, 'gen_knobs.cpp'])
    output_header = os.sep.join([output_dir, 'gen_knobs.h'])

    for f in [output_header, output_file]:
        write_template_to_file(template_file, f,
                filename='gen_knobs',
                knobs=knob_defs.KNOBS,
                includes=['core/knobs_init.h', 'common/os.h', 'sstream', 'iomanip'],
                gen_header=True if f == output_header else False)

    return 0

if __name__ == '__main__':
    sys.exit(main())

