# encoding=utf-8
# Copyright © 2022 Intel Corporation
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

# Converts GLSL shader to SPIR-V library 

import argparse
import subprocess
import io
import os

class ShaderCompileError(RuntimeError):
    def __init__(self, *args):
        super(ShaderCompileError, self).__init__(*args)

def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', help="Name of input file.")
    parser.add_argument('output', help="Name of output file.")

    parser.add_argument("--create-entry",
                        dest="create_entry",
                        help="Create a new entry point and put to the end of a file.")

    parser.add_argument('--glsl-version',
                        dest="glsl_ver",
                        help="{100 | 110 | 120 | 130 | 140 | 150 | 300es | 310es | 320es | 330 | 400 | 410 | 420 | 430 | 440 | 450 | 460} set GLSL version, overrides #version in shader sources. Default is 460.")

    parser.add_argument("-Olib",
                        action='store_true',
                        help="Any optimizations are disabled and unused functions are saved.")

    parser.add_argument("--extra-flags",
                        dest="extra",
                        help="Pass additional flags to glslangValidator.")

    parser.add_argument("--vn",
                        dest="vn",
                        help="Variable name. Creates a C header file that contains a uint32_t array.")

    parser.add_argument("--stage",
                        default="vert",
                        help="Uses specified stage rather than parsing the file extension choices for <stage> are vert, tesc, tese, geom, frag, or comp.")
    args = parser.parse_args()
    return args


def create_include_guard(lines, filename):
    filename = filename.replace('.', '_')
    upper_name = filename.upper()

    guard_head = ["#ifndef {}\n".format(upper_name),
                  "#define {}\n".format(upper_name)]
    guard_tail = ["\n#endif // {}\n".format(upper_name)]

    # remove '#pragma once'
    for idx, l in enumerate(lines):
        if l.find('#pragma once') != -1:
            lines.pop(idx)
            break

    return guard_head + lines + guard_tail


def convert_to_static_variable(lines, varname):
    for idx, l in enumerate(lines):
        if l.find(varname) != -1:
            lines[idx] = "static " + lines[idx]
            return lines


def override_version(lines, glsl_version):
    for idx, l in enumerate(lines):
        if l.find('#version ') != -1:
            lines[idx] = "#version {}\n".format(glsl_version)
            return lines


def postprocess_file(args):
    with open(args.output, "r") as r:
        lines = r.readlines()

        # glslangValidator creates a variable without the static modifier.
        lines = convert_to_static_variable(lines, args.vn)

        # '#pragma once' is unstandardised.
        lines = create_include_guard(lines, os.path.basename(r.name))

        with open(args.output, "w") as w:
            w.writelines(lines)


def preprocess_file(args, origin_file):
    with io.open(origin_file.name + ".copy", "w") as copy_file:
        lines = origin_file.readlines()

        if args.create_entry is not None:
            lines.append("\nvoid {}()".format(args.create_entry) + "{}\n")

        if args.glsl_ver is not None:
            override_version(lines, args.glsl_ver)

        copy_file.writelines(lines)

        return copy_file.name


def process_file(args):
    with io.open(args.input, "r") as infile:
        copy_file = preprocess_file(args, infile)

        cmd_list = ["glslangValidator"]

        if args.Olib:
            cmd_list += ["--keep-uncalled"]

        if args.vn is not None:
            cmd_list += ["--variable-name", args.vn]

        if args.extra is not None:
            cmd_list.append(args.extra)

        if args.create_entry is not None:
            cmd_list += ["--entry-point", args.create_entry]

        cmd_list.append("-V")
        cmd_list += ["-o", args.output]
        cmd_list += ["-S", args.stage]

        cmd_list.append(copy_file)

        with subprocess.Popen(" ".join(cmd_list),
                              shell = True,
                              stdout = subprocess.PIPE,
                              stderr = subprocess.PIPE,
                              stdin = subprocess.PIPE) as proc:

            out, err = proc.communicate(timeout=30)

            if proc.returncode != 0:
                message = out.decode('utf-8') + '\n' + err.decode('utf-8')
                raise ShaderCompileError(message.strip())

            if args.vn is not None:
                postprocess_file(args)

        if args.create_entry is not None:
            os.remove(copy_file)


def main():
    args = get_args()
    process_file(args)


if __name__ == "__main__":
    main()
