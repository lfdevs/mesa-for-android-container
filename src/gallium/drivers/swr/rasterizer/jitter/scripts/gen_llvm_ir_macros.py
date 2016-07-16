# Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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

#!deps/python32/python.exe

import os, sys, re
import argparse
import json as JSON
import operator

header = r"""/****************************************************************************
* Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
* 
* @file %s
* 
* @brief auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/

"""

"""
"""
def gen_file_header(filename):
    global header
    headerStr = header % filename
    return headerStr.splitlines()


inst_aliases = {
    'SHUFFLE_VECTOR': 'VSHUFFLE',
    'INSERT_ELEMENT': 'VINSERT',
    'EXTRACT_ELEMENT': 'VEXTRACT',
    'MEM_SET': 'MEMSET',
    'MEM_CPY': 'MEMCOPY',
    'MEM_MOVE': 'MEMMOVE',
    'L_SHR': 'LSHR',
    'A_SHR': 'ASHR',
    'BIT_CAST': 'BITCAST',
    'U_DIV': 'UDIV',
    'S_DIV': 'SDIV',
    'U_REM': 'UREM',
    'S_REM': 'SREM',
    'BIN_OP': 'BINOP',
}

intrinsics = [
        ["VGATHERPS", "x86_avx2_gather_d_ps_256", ["src", "pBase", "indices", "mask", "scale"]],
        ["VGATHERDD", "x86_avx2_gather_d_d_256", ["src", "pBase", "indices", "mask", "scale"]],
        ["VSQRTPS", "x86_avx_sqrt_ps_256", ["a"]],
        ["VRSQRTPS", "x86_avx_rsqrt_ps_256", ["a"]],
        ["VRCPPS", "x86_avx_rcp_ps_256", ["a"]],
        ["VMINPS", "x86_avx_min_ps_256", ["a", "b"]],
        ["VMAXPS", "x86_avx_max_ps_256", ["a", "b"]],
        ["VPMINSD", "x86_avx2_pmins_d", ["a", "b"]],
        ["VPMAXSD", "x86_avx2_pmaxs_d", ["a", "b"]],
        ["VROUND", "x86_avx_round_ps_256", ["a", "rounding"]],
        ["VCMPPS", "x86_avx_cmp_ps_256", ["a", "b", "cmpop"]],
        ["VBLENDVPS", "x86_avx_blendv_ps_256", ["a", "b", "mask"]],
        ["BEXTR_32", "x86_bmi_bextr_32", ["src", "control"]],
        ["VMASKLOADD", "x86_avx2_maskload_d_256", ["src", "mask"]],
        ["VMASKMOVPS", "x86_avx_maskload_ps_256", ["src", "mask"]],
        ["VPSHUFB", "x86_avx2_pshuf_b", ["a", "b"]],
        ["VPMOVSXBD", "x86_avx2_pmovsxbd", ["a"]],  # sign extend packed 8bit components
        ["VPMOVSXWD", "x86_avx2_pmovsxwd", ["a"]],  # sign extend packed 16bit components
        ["VPERMD", "x86_avx2_permd", ["idx", "a"]],
        ["VPERMPS", "x86_avx2_permps", ["idx", "a"]],
        ["VCVTPH2PS", "x86_vcvtph2ps_256", ["a"]],
        ["VCVTPS2PH", "x86_vcvtps2ph_256", ["a", "round"]],
        ["VHSUBPS", "x86_avx_hsub_ps_256", ["a", "b"]],
        ["VPTESTC", "x86_avx_ptestc_256", ["a", "b"]],
        ["VPTESTZ", "x86_avx_ptestz_256", ["a", "b"]],
        ["VFMADDPS", "x86_fma_vfmadd_ps_256", ["a", "b", "c"]],
        ["VCVTTPS2DQ", "x86_avx_cvtt_ps2dq_256", ["a"]],
        ["VMOVMSKPS", "x86_avx_movmsk_ps_256", ["a"]],
        ["INTERRUPT", "x86_int", ["a"]],
    ]

def convert_uppercamel(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).upper()

"""
    Given an input file (e.g. IRBuilder.h) generates function dictionary.
"""
def parse_ir_builder(input_file):

    functions = []

    lines = input_file.readlines()

    idx = 0
    while idx < len(lines) - 1:
        line = lines[idx].rstrip()
        idx += 1

        #match = re.search(r"\*Create", line)
        match = re.search(r"[\*\s]Create(\w*)\(", line)
        if match is not None:
            #print("Line: %s" % match.group(1))

            if re.search(r"^\s*Create", line) is not None:
                func_sig = lines[idx-2].rstrip() + line
            else:
                func_sig = line

            end_of_args = False
            while not end_of_args:
                end_paren = re.search(r"\)", line)
                if end_paren is not None:
                    end_of_args = True
                else:
                    line = lines[idx].rstrip()
                    func_sig += line
                    idx += 1

            delfunc = re.search(r"LLVM_DELETED_FUNCTION|= delete;", func_sig)

            if not delfunc:
                func = re.search(r"(.*?)\*[\n\s]*(Create\w*)\((.*?)\)", func_sig)
                if func is not None:

                    return_type = func.group(1).lstrip() + '*'
                    func_name = func.group(2)
                    arguments = func.group(3)

                    func_args = ''
                    func_args_nodefs = ''

                    num_args = arguments.count(',')

                    arg_names = []
                    num_args = 0
                    args = arguments.split(',')
                    for arg in args:
                        arg = arg.lstrip()
                        if arg:
                            if num_args > 0:
                                func_args += ', '
                                func_args_nodefs += ', '
                            func_args += arg
                            func_args_nodefs += arg.split(' =')[0]

                            split_args = arg.split('=')
                            arg_name = split_args[0].rsplit(None, 1)[-1]

                            #print("Before ArgName = %s" % arg_name)

                            reg_arg = re.search(r"[\&\*]*(\w*)", arg_name)
                            if reg_arg:
                                #print("Arg Name = %s" % reg_arg.group(1))
                                arg_names += [reg_arg.group(1)]

                            num_args += 1

                    ignore = False

                    # The following functions need to be ignored.
                    if func_name == 'CreateInsertNUWNSWBinOp':
                        ignore = True

                    if func_name == 'CreateMaskedIntrinsic':
                        ignore = True

                    # Convert CamelCase to CAMEL_CASE
                    func_mod = re.search(r"Create(\w*)", func_name)
                    if func_mod:
                        func_mod = func_mod.group(1)
                        func_mod = convert_uppercamel(func_mod)
                        if func_mod[0:2] == 'F_' or func_mod[0:2] == 'I_':
                            func_mod = func_mod[0] + func_mod[2:]

                    # Substitute alias based on CAMEL_CASE name.
                    func_alias = inst_aliases.get(func_mod)
                    if not func_alias:
                        func_alias = func_mod

                        if func_name == 'CreateCall' or func_name == 'CreateGEP':
                            arglist = re.search(r'ArrayRef', func_args)
                            if arglist:
                                func_alias = func_alias + 'A'

                    if not ignore:
                        functions.append({
                                "name": func_name,
                                "alias": func_alias,
                                "return": return_type,
                                "args": func_args,
                                "args_nodefs": func_args_nodefs,
                                "arg_names": arg_names
                            })

    return functions

"""
    Auto-generates macros for LLVM IR
"""
def generate_gen_h(functions, output_file):
    output_lines = gen_file_header(os.path.basename(output_file.name))

    output_lines += [
        '#pragma once',
        '',
        '//////////////////////////////////////////////////////////////////////////',
        '/// Auto-generated Builder IR declarations',
        '//////////////////////////////////////////////////////////////////////////',
    ]

    for func in functions:
        name = func['name']
        if func['alias']:
            name = func['alias']
        output_lines += [
            '%s%s(%s);' % (func['return'], name, func['args'])
        ]

    output_file.write('\n'.join(output_lines) + '\n')

"""
    Auto-generates macros for LLVM IR
"""
def generate_gen_cpp(functions, output_file):
    output_lines = gen_file_header(os.path.basename(output_file.name))

    output_lines += [
        '#include \"builder.h\"',
        ''
    ]

    for func in functions:
        name = func['name']
        if func['alias']:
            name = func['alias']

        args = func['arg_names']
        func_args = ''
        first_arg = True
        for arg in args:
            if not first_arg:
                func_args += ', '
            func_args += arg
            first_arg = False

        output_lines += [
            '//////////////////////////////////////////////////////////////////////////',
            '%sBuilder::%s(%s)' % (func['return'], name, func['args_nodefs']),
            '{',
            '   return IRB()->%s(%s);' % (func['name'], func_args),
            '}',
            '',
        ]

    output_file.write('\n'.join(output_lines) + '\n')

"""
    Auto-generates macros for LLVM IR
"""
def generate_x86_h(output_file):
    output_lines = gen_file_header(os.path.basename(output_file.name))

    output_lines += [
        '#pragma once',
        '',
        '//////////////////////////////////////////////////////////////////////////',
        '/// Auto-generated x86 intrinsics',
        '//////////////////////////////////////////////////////////////////////////',
    ]

    for inst in intrinsics:
        #print("Inst: %s, x86: %s numArgs: %d" % (inst[0], inst[1], len(inst[2])))

        args = ''
        first = True
        for arg in inst[2]:
            if not first:
                args += ', '
            args += ("Value* %s" % arg)
            first = False

        output_lines += [
            'Value *%s(%s);' % (inst[0], args)
        ]

    output_file.write('\n'.join(output_lines) + '\n')

"""
    Auto-generates macros for LLVM IR
"""
def generate_x86_cpp(output_file):
    output_lines = gen_file_header(os.path.basename(output_file.name))

    output_lines += [
        '#include \"builder.h\"',
        ''
    ]

    for inst in intrinsics:
        #print("Inst: %s, x86: %s numArgs: %d" % (inst[0], inst[1], len(inst[2])))

        args = ''
        pass_args = ''
        first = True
        for arg in inst[2]:
            if not first:
                args += ', '
                pass_args += ', '
            args += ("Value* %s" % arg)
            pass_args += arg
            first = False

        output_lines += [
            '//////////////////////////////////////////////////////////////////////////',
            'Value *Builder::%s(%s)' % (inst[0], args),
            '{',
            '    Function *func = Intrinsic::getDeclaration(JM()->mpCurrentModule, Intrinsic::%s);' % inst[1],
            '    return CALL(func, std::initializer_list<Value*>{%s});' % pass_args,
            '}',
            '',
        ]

    output_file.write('\n'.join(output_lines) + '\n')

"""
    Function which is invoked when this script is started from a command line.
    Will present and consume a set of arguments which will tell this script how
    to behave
"""
def main():

    # Parse args...
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", "-i", type=argparse.FileType('r'), help="Path to IRBuilder.h", required=False)
    parser.add_argument("--output", "-o", type=argparse.FileType('w'), help="Path to output file", required=True)
    parser.add_argument("--gen_h", "-gen_h", help="Generate builder_gen.h", action="store_true", default=False)
    parser.add_argument("--gen_cpp", "-gen_cpp", help="Generate builder_gen.cpp", action="store_true", default=False)
    parser.add_argument("--gen_x86_h", "-gen_x86_h", help="Generate x86 intrinsics. No input is needed.", action="store_true", default=False)
    parser.add_argument("--gen_x86_cpp", "-gen_x86_cpp", help="Generate x86 intrinsics. No input is needed.", action="store_true", default=False)
    args = parser.parse_args()

    if args.input:
        functions = parse_ir_builder(args.input)

        if args.gen_h:
            generate_gen_h(functions, args.output)

        if args.gen_cpp:
            generate_gen_cpp(functions, args.output)
    else:
        if args.gen_x86_h:
            generate_x86_h(args.output)

        if args.gen_x86_cpp:
            generate_x86_cpp(args.output)

        if args.gen_h:
            print("Need to specify --input for --gen_h!")

        if args.gen_cpp:
            print("Need to specify --input for --gen_cpp!")

if __name__ == '__main__':
    main()
# END OF FILE
