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

header = r"""
/****************************************************************************
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

#pragma once

"""

"""
"""
def gen_file_header(filename):
    global header
    headerStr = header % filename
    return headerStr.splitlines()

"""
"""
def gen_llvm_type(type, name, postfix_name, is_pointer, is_pointer_pointer, is_array, is_array_array, array_count, array_count1, is_llvm_struct, is_llvm_enum, is_llvm_pfn, output_file):

    llvm_type = ''

    if is_llvm_struct:
        if is_pointer or is_pointer_pointer:
            llvm_type = 'Type::getInt32Ty(ctx)'
        else:
            llvm_type = 'ArrayType::get(Type::getInt8Ty(ctx), sizeof(%s))' % type
    elif is_llvm_enum:
        llvm_type = 'Type::getInt32Ty(ctx)'
    elif is_llvm_pfn:
        llvm_type = 'PointerType::get(Type::getInt8Ty(ctx), 0)'
    else:
        if type == "BYTE" or type == "char" or type == "uint8_t" or type == "int8_t" or type == 'bool':
            llvm_type = 'Type::getInt8Ty(ctx)'
        elif type == 'UINT64' or type == 'INT64' or type == 'uint64_t' or type == 'int64_t':
            llvm_type = 'Type::getInt64Ty(ctx)'
        elif type == 'UINT16' or type == 'int16_t' or type == 'uint16_t':
            llvm_type = 'Type::getInt16Ty(ctx)'
        elif type == 'UINT' or type == 'INT' or type == 'int' or type == 'BOOL' or type == 'uint32_t' or type == 'int32_t':
            llvm_type = 'Type::getInt32Ty(ctx)'
        elif type == 'float' or type == 'FLOAT':
            llvm_type = 'Type::getFloatTy(ctx)'
        elif type == 'double' or type == 'DOUBLE':
            llvm_type = 'Type::getDoubleTy(ctx)'
        elif type == 'void' or type == 'VOID':
            llvm_type = 'Type::getInt32Ty(ctx)'
        elif type == 'HANDLE':
            llvm_type = 'PointerType::get(Type::getInt32Ty(ctx), 0)'
        elif type == 'simdscalar':
            llvm_type = 'VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth)'
        elif type == 'simdscalari':
            llvm_type = 'VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth)'
        elif type == 'simdvector':
            llvm_type = 'ArrayType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 4)'
        else:
            llvm_type = 'Gen_%s%s(pJitMgr)' % (type, postfix_name)

    if is_pointer:
        llvm_type = 'PointerType::get(%s, 0)' % llvm_type

    if is_pointer_pointer:
        llvm_type = 'PointerType::get(%s, 0)' % llvm_type

    if is_array_array:
        llvm_type = 'ArrayType::get(ArrayType::get(%s, %s), %s)' % (llvm_type, array_count1, array_count)
    elif is_array:
        llvm_type = 'ArrayType::get(%s, %s)' % (llvm_type, array_count)

    return ['    members.push_back( %s );    // %s' % (llvm_type, name)]

"""
"""
def gen_llvm_types(input_file, output_file):

    output_lines = gen_file_header(os.path.basename(output_file.name))

    lines = input_file.readlines()

    postfix_name = ""

    for idx in range(len(lines)):
        line = lines[idx].rstrip()

        if "gen_llvm_types FINI" in line:
            break

        match = re.match(r"(\s*)struct(\s*)(\w+)", line)
        if match:
            llvm_args = []

             # Detect start of structure
            is_fwd_decl = re.search(r";", line)

            if not is_fwd_decl:

                # Extract the command name
                struct_name = match.group(3).strip()

                output_lines += [
                    '//////////////////////////////////////////////////////////////////////////',
                    '/// Generate LLVM type information for %s' % struct_name,
                    'INLINE static StructType *Gen_%s%s(JitManager* pJitMgr)' % (struct_name, postfix_name),
                    '{',
                    '    LLVMContext& ctx = pJitMgr->mContext;',
                    '    std::vector<Type*> members;',
                    '',
                ]

                end_of_struct = False

                while not end_of_struct and idx < len(lines)-1:
                    idx += 1
                    line = lines[idx].rstrip()

                    is_llvm_typedef = re.search(r"@llvm_typedef", line)
                    if is_llvm_typedef is not None:
                        is_llvm_typedef = True
                    else:
                        is_llvm_typedef = False

                    ###########################################
                    # Is field a llvm struct? Tells script to treat type as array of bytes that is size of structure.
                    is_llvm_struct = re.search(r"@llvm_struct", line)

                    if is_llvm_struct is not None:
                        is_llvm_struct = True
                    else:
                        is_llvm_struct = False

                    ###########################################
                    # Is field a llvm enum? Tells script to treat type as an enum and replaced with uint32 type.
                    is_llvm_enum = re.search(r"@llvm_enum", line)

                    if is_llvm_enum is not None:
                        is_llvm_enum = True
                    else:
                        is_llvm_enum = False

                    ###########################################
                    # Is field a llvm function pointer? Tells script to treat type as an enum and replaced with uint32 type.
                    is_llvm_pfn = re.search(r"@llvm_pfn", line)

                    if is_llvm_pfn is not None:
                        is_llvm_pfn = True
                    else:
                        is_llvm_pfn = False

                    ###########################################
                    # Is field const?
                    is_const = re.search(r"\s+const\s+", line)

                    if is_const is not None:
                        is_const = True
                    else:
                        is_const = False

                    ###########################################
                    # Is field a pointer?
                    is_pointer_pointer = re.search("\*\*", line)

                    if is_pointer_pointer is not None:
                        is_pointer_pointer = True
                    else:
                        is_pointer_pointer = False

                    ###########################################
                    # Is field a pointer?
                    is_pointer = re.search("\*", line)

                    if is_pointer is not None:
                        is_pointer = True
                    else:
                        is_pointer = False

                    ###########################################
                    # Is field an array of arrays?
                    # TODO: Can add this to a list.
                    is_array_array = re.search("\[(\w*)\]\[(\w*)\]", line)
                    array_count = '0'
                    array_count1 = '0'

                    if is_array_array is not None:
                        array_count = is_array_array.group(1)
                        array_count1 = is_array_array.group(2)
                        is_array_array = True
                    else:
                        is_array_array = False

                    ###########################################
                    # Is field an array?
                    is_array = re.search("\[(\w*)\]", line)

                    if is_array is not None:
                        array_count = is_array.group(1)
                        is_array = True
                    else:
                        is_array = False

                    is_scoped = re.search("::", line)

                    if is_scoped is not None:
                        is_scoped = True
                    else:
                        is_scoped = False

                    type = None
                    name = None
                    if is_const and is_pointer:

                        if is_scoped:
                            field_match = re.match(r"(\s*)(\w+\<*\w*\>*)(\s+)(\w+::)(\w+)(\s*\**\s*)(\w+)", line)

                            type = "%s%s" % (field_match.group(4), field_match.group(5))
                            name = field_match.group(7)
                        else:
                            field_match = re.match(r"(\s*)(\w+\<*\w*\>*)(\s+)(\w+)(\s*\**\s*)(\w+)", line)

                            type = field_match.group(4)
                            name = field_match.group(6)

                    elif is_pointer:
                        field_match = re.match(r"(\s*)(\s+)(\w+\<*\w*\>*)(\s*\**\s*)(\w+)", line)

                        if field_match:
                            type = field_match.group(3)
                            name = field_match.group(5)
                    elif is_const:
                        field_match = re.match(r"(\s*)(\w+\<*\w*\>*)(\s+)(\w+)(\s*)(\w+)", line)

                        if field_match:
                            type = field_match.group(4)
                            name = field_match.group(6)
                    else:
                        if is_scoped:
                            field_match = re.match(r"\s*(\w+\<*\w*\>*)\s*::\s*(\w+\<*\w*\>*)\s+(\w+)", line)

                            if field_match:
                                type = field_match.group(1) + '::' + field_match.group(2)
                                name = field_match.group(3)
                        else:
                            field_match = re.match(r"(\s*)(\w+\<*\w*\>*)(\s+)(\w+)", line)

                            if field_match:
                                type = field_match.group(2)
                                name = field_match.group(4)

                    if is_llvm_typedef is False:
                        if type is not None:
                            output_lines += gen_llvm_type(type, name, postfix_name, is_pointer, is_pointer_pointer, is_array, is_array_array, array_count, array_count1, is_llvm_struct, is_llvm_enum, is_llvm_pfn, output_file)
                            llvm_args.append(name)

                    # Detect end of structure
                    end_of_struct = re.match(r"(\s*)};", line)

                    if (end_of_struct):
                        output_lines += [
                            '',
                            '    return StructType::get(ctx, members, false);',
                            '}',
                            '',
                        ]

                        for i in range(len(llvm_args)):
                            output_lines.append('static const uint32_t %s%s_%s = %s;' % (struct_name, postfix_name, llvm_args[i], i))

                        output_lines.append('')

    output_file.write('\n'.join(output_lines) + '\n')

"""
    Function which is invoked when this script is started from a command line.
    Will present and consume a set of arguments which will tell this script how
    to behave
"""
def main():

    # Parse args...
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", "-i", type=argparse.FileType('r'),
            help="Path to input file containing structs", required=True)
    parser.add_argument("--output", "-o", type=argparse.FileType('w'),
            help="Path to output file", required=True)
    parser.add_argument("--scalar", "-scalar", help="Generates scalar files with all enums", action="store_true", default=False)
    args = parser.parse_args()

    gen_llvm_types(args.input, args.output)

if __name__ == '__main__':
    main()
# END OF FILE
