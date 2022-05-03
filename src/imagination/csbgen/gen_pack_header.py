# encoding=utf-8

# Copyright © 2022 Imagination Technologies Ltd.

# based on anv driver gen_pack_header.py which is:
# Copyright © 2016 Intel Corporation

# based on v3dv driver gen_pack_header.py which is:
# Copyright (C) 2016 Broadcom

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import argparse
import ast
import xml.parsers.expat
import re
import sys
import copy
import os
import textwrap

license = """/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */"""

pack_header = """%(license)s

/* Enums, structures and pack functions for %(platform)s.
 *
 * This file has been generated, do not hand edit.
 */

#ifndef %(guard)s
#define %(guard)s

#include "csbgen/pvr_packet_helpers.h"

"""

def safe_name(name):
    if not name[0].isalpha():
        name = '_' + name

    return name

def num_from_str(num_str):
    if num_str.lower().startswith('0x'):
        return int(num_str, base=16)

    if num_str.startswith('0') and len(num_str) > 1:
        raise ValueError('Octal numbers not allowed')

    return int(num_str)

class Node:
    def __init__(self, parent, name, name_is_safe = False):
        self.parent = parent
        if name_is_safe:
            self.name = name
        else:
            self.name = safe_name(name)

    @property
    def full_name(self):
        if self.name[0] == '_':
            return self.parent.prefix + self.name.upper()

        return self.parent.prefix + "_" + self.name.upper()

    @property
    def prefix(self):
        return self.parent.prefix

class Csbgen(Node):
    def __init__(self, name, prefix, filename):
        super().__init__(None, name.upper())
        self.prefix_field = safe_name(prefix.upper())
        self.filename = filename

        self._defines = []
        self._enums = {}
        self._structs = {}

    @property
    def full_name(self):
        return self.name + "_" + self.prefix_field

    @property
    def prefix(self):
        return self.full_name

    def add(self, element):
        if isinstance(element, Enum):
            if element.name in self._enums:
                raise RuntimeError('Enum redefined. Enum: %s' % element.name)

            self._enums[element.name] = element
        elif isinstance(element, Struct):
            if element.name in self._structs:
                raise RuntimeError('Struct redefined. Struct: %s' % element.name)

            self._structs[element.name] = element
        elif isinstance(element, Define):
            define_names = map(lambda d: d.full_name, self._defines)
            if element.full_name in define_names:
                raise RuntimeError('Define redefined. Define: %s' % element.full_name)

            self._defines.append(element)
        else:
            raise RuntimeError('Element "%s" cannot be nested in csbgen.' %
                    type(element).__name__)

    def _gen_guard(self):
        return os.path.basename(self.filename).replace('.xml', '_h').upper()

    def emit(self):
        print(pack_header % {'license': license,
                             'platform': self.name,
                             'guard': self._gen_guard()})

        for define in self._defines:
            define.emit(self)

        print()

        for enum in self._enums.values():
            enum.emit(self)

        for struct in self._structs.values():
            struct.emit(self)

        print('#endif /* %s */' % self._gen_guard())

    def is_known_struct(self, struct_name):
        return struct_name in self._structs.keys()

    def is_known_enum(self, enum_name):
        return enum_name in self._enums.keys()

    def get_enum(self, enum_name):
        return self._enums[enum_name]

class Enum(Node):
    def __init__(self, parent, name):
        super().__init__(parent, name)

        self._values = {}

        self.parent.add(self)

    # We override prefix so that the values will contain the enum's name too.
    @property
    def prefix(self):
        return self.full_name

    def get_value(self, value_name):
        return self._values[value_name]

    def add(self, element):
        if not isinstance(element, Value):
            raise RuntimeError('Element cannot be nested in enum. ' +
                    'Element Type: %s, Enum: %s' %
                    (type(element).__name__, self.full_name))

        if element.name in self._values:
            raise RuntimeError('Value is being redefined. Value: "%s"' % element.name)

        self._values[element.name] = element

    def emit(self, root):
        # This check is invalid if tags other than Value can be nested within an enum.
        if not self._values.values():
            raise RuntimeError('Enum definition is empty. Enum: "%s"' % self.full_name)

        print('enum %s {' % self.full_name)
        for value in self._values.values():
            value.emit()
        print('};\n')

class Value(Node):
    def __init__(self, parent, name, value):
        super().__init__(parent, name)

        self.value = int(value)

        self.parent.add(self)

    def emit(self):
            print('    %-36s = %6d,' % (self.full_name, self.value))

class Struct(Node):
    def __init__(self, parent, name, length):
        super().__init__(parent, name)

        self.length = int(length)
        self.size = self.length * 32

        if self.length <= 0:
            raise ValueError('Struct length must be greater than 0. ' +
                    'Struct: "%s".' % self.full_name)

        self._children = {}

        self.parent.add(self)

    @property
    def fields(self):
        # TODO: Should we cache? See TODO in equivalent Condition getter.

        fields = []
        for child in self._children.values():
            if isinstance(child, Condition):
                fields += child.fields
            else:
                fields.append(child)

        return fields

    @property
    def prefix(self):
        return self.full_name

    def add(self, element):
        # We don't support conditions and field having the same name.
        if isinstance(element, Field):
            if element.name in self._children.keys():
                raise ValueError('Field is being redefined. ' +
                        'Field: "%s", Struct: "%s"' %
                        (element.name, self.full_name))

            self._children[element.name] = element

        elif isinstance(element, Condition):
            # We only save ifs, and ignore the rest. The rest will be linked to
            # the if condition so we just need to call emit() on the if and the
            # rest will also be emitted.
            if element.type == 'if':
                self._children[element.name] = element
            else:
                if element.name not in self._children.keys():
                    raise RuntimeError('Unknown condition: "%s"' % element.name)

        else:
            raise RuntimeError('Element cannot be nested in struct. ' +
                    'Element Type: %s, Struct: %s' %
                    (type(element).__name__, self.full_name))

    def _emit_header(self, root):
        fields = filter(lambda f: hasattr(f, 'default'), self.fields)

        default_fields = []
        for field in fields:
            if field.is_builtin_type:
                default_fields.append("    .%-35s = %6d" %
                                      (field.name, field.default))
            else:
                if not root.is_known_enum(field.type):
                    # Default values should not apply to structures
                    raise RuntimeError('Unknown type. Field: "%s" Type: "%s"' %
                            (field.name, field.type))

                enum = root.get_enum(field.type)

                try:
                    value = enum.get_value(field.default)
                except KeyError:
                    raise ValueError('Unknown enum value. ' +
                            'Value: "%s", Enum: "%s", Field: "%s"' %
                            (field.default, enum.full_name, field.name))

                default_fields.append("    .%-35s = %s" %
                        (field.name, value.full_name))

        print('#define %-40s\\' % (self.full_name + '_header'))
        print(",  \\\n".join(default_fields))
        print('')

    def _emit_helper_macros(self, root):
        fields_with_defines = filter(lambda f: f.defines, self.fields)

        for field in fields_with_defines:
            print("/* Helper macros for %s */" % (field.name))

            for define in field.defines:
                define.emit(root)

            print()

    def _emit_pack_function(self, root):
        print(textwrap.dedent("""\
            static inline __attribute__((always_inline)) void
            %s_pack(__attribute__((unused)) void * restrict dst,
                  %s__attribute__((unused)) const struct %s * restrict values)
            {""") % (self.full_name, ' ' * len(self.full_name), self.full_name))

        group = Group(0, 1, self.size, self.fields)
        (dwords, length) = group.collect_dwords_and_length()
        if length:
            # Cast dst to make header C++ friendly
            print("    uint32_t * restrict dw = (uint32_t * restrict) dst;")

        group.emit_pack_function(root, dwords, length)

        print("}\n")


    def emit(self, root):
        print('#define %-33s %6d' % (self.full_name + "_length", self.length))

        self._emit_header(root)

        self._emit_helper_macros(root)

        print("struct %s {" % self.full_name)
        for child in self._children.values():
                child.emit(root)
        print("};\n")

        self._emit_pack_function(root)

class Field(Node):
    def __init__(self, parent, name, start, end, type, default=None, shift=None):
        super().__init__(parent, name)

        self.start = int(start)
        self.end = int(end)
        self.type = type

        self._defines = {}

        self.parent.add(self)

        if self.start > self.end:
            raise ValueError('Start cannot be after end. ' +
                    'Start: %d, End: %d, Field: "%s"' %
                    (self.start, self.end, self.name))

        if self.type == 'bool' and self.end != self.start:
            raise ValueError('Bool field can only be 1 bit long. ' +
                    'Field "%s"' % self.name)

        if default is not None:
            if not self.is_builtin_type:
                # Assuming it's an enum type.
                self.default = safe_name(default)
            else:
                self.default = num_from_str(default)

        if shift is not None:
            if self.type != 'address':
                raise RuntimeError('Only address fields can have a shift ' +
                        'attribute. Field: "%s"' % self.name)

            self.shift = int(shift)

            Define(self, "ALIGNMENT", 2 ** self.shift)
        else:
            if self.type == 'address':
                raise RuntimeError('Field of address type ' +
                        'requires a shift attribute. Field "%s"' %
                        self.name)

    @property
    def defines(self):
        return self._defines.values()

    # We override prefix so that the defines will contain the field's name too.
    @property
    def prefix(self):
        return self.full_name

    @property
    def is_builtin_type(self):
        builtins = {'address', 'bool', 'float', 'mbo', 'offset', 'int', 'uint'}
        return self.type in builtins

    def _get_c_type(self, root):
        if self.type == 'address':
            return '__pvr_address_type'
        elif self.type == 'bool':
            return 'bool'
        elif self.type == 'float':
            return 'float'
        elif self.type == 'offset':
            return 'uint64_t'
        elif self.type == 'int':
            return 'int32_t'
        elif self.type == 'uint':
            if self.end - self.start < 32:
                return 'uint32_t'
            elif self.end - self.self < 64:
                return 'uint64_t'

            raise RuntimeError('No known C type found to hold %d bit sized value. ' +
                    'Field: "%s"' %
                    (self.end - self.start, self.name))
        elif root.is_known_struct(self.type):
            return 'struct ' + self.type
        elif root.is_known_enum(self.type):
            return 'enum ' + root.get_enum(self.type).full_name
        raise RuntimeError('Unknown type. Type: "%s", Field: "%s"' %
                (self.type, self.name))

    def add(self, element):
        if self.type == 'mbo':
            raise RuntimeError('No element can be nested in an mbo field. ' +
                    'Element Type: %s, Field: %s' %
                    (type(element).__name__, self.name))

        if isinstance(element, Define):
            if element.name in self._defines:
                raise RuntimeError('Duplicate define. Define: "%s"' %
                        element.name)

            self._defines[element.name] = element
        else:
            raise RuntimeError('Element cannot be nested in a field. ' +
                    'Element Type: %s, Field: %s' %
                    (type(element).__name__, self.name))

    def emit(self, root):
        if self.type == 'mbo':
            return

        print("    %-36s %s;" % (self._get_c_type(root), self.name))

class Define(Node):
    def __init__(self, parent, name, value):
        super().__init__(parent, name)

        self.value = value

        self.parent.add(self)

    def emit(self, root):
        print("#define %-40s %d" % (self.full_name, self.value))

class Condition(Node):
    def __init__(self, parent, name, type):
        super().__init__(parent, name, name_is_safe = True)

        self.type = type
        if not Condition._is_valid_type(self.type):
            raise RuntimeError('Unknown type: "%s"' % self.name)

        self._children = {}

        # This is the link to the next branch for the if statement so either
        # elif, else, or endif. They themselves will also have a link to the
        # next branch up until endif which terminates the chain.
        self._child_branch = None

        self.parent.add(self)

    @property
    def fields(self):
        # TODO: Should we use some kind of state to indicate the all of the
        # child nodes have been added and then cache the fields in here on the
        # first call so that we don't have to traverse them again per each call?
        # The state could be changed wither when we reach the endif and pop from
        # the context, or when we start emitting.

        fields = []

        for child in self._children.values():
            if isinstance(child, Condition):
                fields += child.fields
            else:
                fields.append(child)

        if self._child_branch is not None:
            fields += self._child_branch.fields

        return fields

    def _is_valid_type(type):
        types = {'if', 'elif', 'else', 'endif'}
        return type in types

    def _is_compatible_child_branch(self, branch):
        types = ['if', 'elif', 'else', 'endif']
        idx = types.index(self.type)
        return (branch.type in types[idx + 1:] or
                self.type == 'elif' and branch.type == 'elif')

    def _add_branch(self, branch):
        if branch.type == 'elif' and branch.name == self.name:
                raise RuntimeError('Elif branch cannot have same check as previous branch. ' +
                        'Check: "%s"' % (branch.name))

        if not self._is_compatible_child_branch(branch):
            raise RuntimeError('Invalid branch. Check: "%s", Type: "%s"' %
                    (branch.name, branch.type))

        self._child_branch = branch

    # Returns the name of the if condition. This is used for elif branches since
    # they have a different name than the if condition thus we have to traverse
    # the chain of branches.
    # This is used to discriminate nested if conditions from branches since
    # branches like 'endif' and 'else' will have the same name as the 'if' (the
    # elif is an exception) while nested conditions will have different names.
    #
    # TODO: Redo this to improve speed? Would caching this be helpful? We could
    # just save the name of the if instead of having to walk towards it whenever
    # a new condition is being added.
    def _top_branch_name(self):
        if self.type == 'if':
            return self.name

        return self.parent._top_branch_name()

    def add(self, element):
        if isinstance(element, Field):
            if element.name in self._children.keys():
                raise ValueError('Duplicate field. Field: "%s"' % element.name)

            self._children[element.name] = element
        elif isinstance(element, Condition):
            if element.type == 'elif' or self._top_branch_name() == element.name:
                self._add_branch(element)
            else:
                if element.type != 'if':
                    raise RuntimeError('Branch of an unopened if condition. ' +
                        'Check: "%s", Type: "%s".' % (element.name, element.type))

                # This is a nested condition and we made sure that the name
                # doesn't match _top_branch_name() so we can recognize the else
                # and endif.
                # We recognized the elif by its type however its name differs
                # from the if condition thus when we add an if condition with
                # the same name as the elif nested in it, the _top_branch_name()
                # check doesn't hold true as the name matched the elif and not
                # the if statement which the elif was a branch of, thus the
                # nested if condition is not recognized as an invalid branch of
                # the outer if statement.
                #   Sample:
                #   <condition type="if" check="ROGUEXE"/>
                #       <condition type="elif" check="COMPUTE"/>
                #           <condition type="if" check="COMPUTE"/>
                #           <condition type="endif" check="COMPUTE"/>
                #       <condition type="endif" check="COMPUTE"/>
                #   <condition type="endif" check="ROGUEXE"/>
                #
                # We fix this by checking the if condition name against its
                # parent.
                if element.name == self.name:
                    raise RuntimeError('Invalid if condition. Check: "%s"' %
                            element.name)

                self._children[element.name] = element
        else:
            raise RuntimeError('Element cannot be nested in a condition. ' +
                    'Element Type: %s, Check: %s' %
                    (type(element).__name__, self.name))

    def emit(self, root):
        if self.type == "if":
            print("/* if %s is supported use: */" % (self.name))
        elif self.type == "elif":
            print("/* else if %s is supported use: */" % (self.name))
        elif self.type == "else":
            print("/* else %s is not-supported use: */" % (self.name))
        elif self.type == "endif":
            print("/* endif %s */" % (self.name))
            return
        else:
            raise RuntimeError('Unknown condition type. Implementation error.')

        for child in self._children.values():
            child.emit(root)

        self._child_branch.emit(root)

class Group(object):
    def __init__(self, start, count, size, fields):
        self.start = start
        self.count = count
        self.size = size
        self.fields = fields

    class DWord:
        def __init__(self):
            self.size = 32
            self.fields = []
            self.addresses = []

    def collect_dwords(self, dwords, start, dim):
        for field in self.fields:
            index = (start + field.start) // 32
            if index not in dwords:
                dwords[index] = self.DWord()

            clone = copy.copy(field)
            clone.start = clone.start + start
            clone.end = clone.end + start
            clone.dim = dim
            dwords[index].fields.append(clone)

            if field.type == "address":
                # assert dwords[index].address == None
                dwords[index].addresses.append(clone)

            # Coalesce all the dwords covered by this field. The two cases we
            # handle are where multiple fields are in a 64 bit word (typically
            # and address and a few bits) or where a single struct field
            # completely covers multiple dwords.
            while index < (start + field.end) // 32:
                if index + 1 in dwords and \
                   not dwords[index] == dwords[index + 1]:
                    dwords[index].fields.extend(dwords[index + 1].fields)
                    dwords[index].addresses.extend(dwords[index + 1].addresses)
                dwords[index].size = 64
                dwords[index + 1] = dwords[index]
                index = index + 1

    def collect_dwords_and_length(self):
        dwords = {}
        self.collect_dwords(dwords, 0, "")

        # Determine number of dwords in this group. If we have a size, use
        # that, since that'll account for MBZ dwords at the end of a group
        # (like dword 8 on BDW+ 3DSTATE_HS). Otherwise, use the largest dword
        # index we've seen plus one.
        if self.size > 0:
            length = self.size // 32
        elif dwords:
            length = max(dwords.keys()) + 1
        else:
            length = 0

        return (dwords, length)

    def emit_pack_function(self, root, dwords, length):
        for index in range(length):
            # Handle MBZ dwords
            if index not in dwords:
                print("")
                print("    dw[%d] = 0;" % index)
                continue

            # For 64 bit dwords, we aliased the two dword entries in the dword
            # dict it occupies. Now that we're emitting the pack function,
            # skip the duplicate entries.
            dw = dwords[index]
            if index > 0 and index - 1 in dwords and dw == dwords[index - 1]:
                continue

            # Special case: only one field and it's a struct at the beginning
            # of the dword. In this case we pack directly into the
            # destination. This is the only way we handle embedded structs
            # larger than 32 bits.
            if len(dw.fields) == 1:
                field = dw.fields[0]
                name = field.name + field.dim
                if root.is_known_struct(field.type) and field.start % 32 == 0:
                    print("")
                    print("    %s_pack(data, &dw[%d], &values->%s);" %
                          (self.parser.gen_prefix(safe_name(field.type)),
                           index, name))
                    continue

            # Pack any fields of struct type first so we have integer values
            # to the dword for those fields.
            field_index = 0
            for field in dw.fields:
                if isinstance(field, Field) and root.is_known_struct(field.type):
                    name = field.name + field.dim
                    print("")
                    print("    uint32_t v%d_%d;" % (index, field_index))
                    print("    %s_pack(data, &v%d_%d, &values->%s);" %
                          (self.parser.gen_prefix(safe_name(field.type)),
                           index, field_index, name))
                    field_index = field_index + 1

            print("")
            dword_start = index * 32
            address_count = len(dw.addresses);

            if dw.size == 32 and not dw.addresses:
                v = None
                print("    dw[%d] =" % index)
            elif len(dw.fields) > address_count:
                v = "v%d" % index
                print("    const uint%d_t %s =" % (dw.size, v))
            else:
                v = "0"

            field_index = 0
            non_address_fields = []
            for field in dw.fields:
                if field.type != "mbo":
                    name = field.name + field.dim

                if field.type == "mbo":
                    non_address_fields.append("__pvr_mbo(%d, %d)" %
                                              (field.start - dword_start,
                                               field.end - dword_start))
                elif field.type == "address":
                    pass
                elif field.type == "uint":
                    non_address_fields.append("__pvr_uint(values->%s, %d, %d)" %
                                              (name, field.start - dword_start,
                                               field.end - dword_start))
                elif root.is_known_enum(field.type):
                    non_address_fields.append("__pvr_uint(values->%s, %d, %d)" %
                                              (name, field.start - dword_start,
                                               field.end - dword_start))
                elif field.type == "int":
                    non_address_fields.append("__pvr_sint(values->%s, %d, %d)" %
                                              (name, field.start - dword_start,
                                               field.end - dword_start))
                elif field.type == "bool":
                    non_address_fields.append("__pvr_uint(values->%s, %d, %d)" %
                                              (name, field.start - dword_start,
                                               field.end - dword_start))
                elif field.type == "float":
                    non_address_fields.append("__pvr_float(values->%s)" % name)
                elif field.type == "offset":
                    non_address_fields.append(
                        "__pvr_offset(values->%s,"" %d, %d)" %
                        (name, field.start - dword_start,
                         field.end - dword_start))
                elif field.is_struct_type():
                    non_address_fields.append("__pvr_uint(v%d_%d, %d, %d)" %
                                              (index, field_index,
                                               field.start - dword_start,
                                               field.end - dword_start))
                    field_index = field_index + 1
                else:
                    non_address_fields.append("/* unhandled field %s,"
                                              " type %s */\n" %
                                              (name, field.type))

            if non_address_fields:
                print(" |\n".join("      " + f for f in non_address_fields) +
                      ";")

            if dw.size == 32:
                for i in range(address_count):
                    print("    dw[%d] = __pvr_address("
                          "values->%s, %d, %d, %d) | %s;" %
                          (index, dw.addresses[i].name + field.dim,
                           dw.addresses[i].shift, dw.addresses[i].start - dword_start,
                           dw.addresses[i].end - dword_start, v))
                continue

            v_accumulated_addr = ""
            for i in range(address_count):
                v_address = "v%d_address" % i
                v_accumulated_addr += "v%d_address" % i
                print("    const uint64_t %s =\n     "
                      " __pvr_address(values->%s, %d, %d, %d);" %
                      (v_address, dw.addresses[i].name + field.dim, dw.addresses[i].shift,
                       dw.addresses[i].start - dword_start,
                       dw.addresses[i].end - dword_start))
                if i < (address_count - 1):
                    v_accumulated_addr += " |\n            "

            if dw.addresses:
                if len(dw.fields) > address_count:
                    print("    dw[%d] = %s | %s;" % (index, v_accumulated_addr, v))
                    print("    dw[%d] = (%s >> 32) | (%s >> 32);" %
                          (index + 1, v_accumulated_addr, v))
                    continue
                else:
                    v = v_accumulated_addr

            print("    dw[%d] = %s;" % (index, v))
            print("    dw[%d] = %s >> 32;" % (index + 1, v))

class Parser(object):
    def __init__(self):
        self.parser = xml.parsers.expat.ParserCreate()
        self.parser.StartElementHandler = self.start_element
        self.parser.EndElementHandler = self.end_element

        self.context = []

    def start_element(self, name, attrs):
        if not name == "csbgen":
            parent = self.context[-1]

        if name == "csbgen":
            if self.context:
                raise RuntimeError('Can only have 1 csbgen block and it has ' +
                        'to contain all of the other elements.')

            csbgen = Csbgen(attrs["name"], attrs["prefix"], self.filename)
            self.context.append(csbgen)

        elif name == "struct":
            struct = Struct(parent , attrs["name"], attrs["length"])
            self.context.append(struct)

        elif name == "field":
            default = None
            if "default" in attrs.keys():
                default = attrs["default"]

            shift = None
            if "shift" in attrs.keys():
                shift = attrs["shift"]

            field = Field(parent,
                    name = attrs["name"],
                    start = int(attrs["start"]),
                    end = int(attrs["end"]),
                    type = attrs["type"],
                    default = default,
                    shift = shift)
            self.context.append(field)

        elif name == "enum":
            enum = Enum(parent, attrs["name"])
            self.context.append(enum)

        elif name == "value":
            value = Value(parent, attrs["name"], ast.literal_eval(attrs["value"]))
            self.context.append(value)

        elif name == "define":
            define = Define(parent, attrs["name"], ast.literal_eval(attrs["value"]))
            self.context.append(define)

        elif name == "condition":
            condition = Condition(parent, name=attrs["check"], type=attrs["type"])

            # Starting with the if statement we push it in the context. For each
            # branch following (elif, and else) we assign the top of stack as
            # its parent, pop() and push the new condition. So per branch we end
            # up having [..., struct, condition]. We don't push an endif since
            # it's not supposed to have any children and it's supposed to close
            # the whole if statement.

            if condition.type != 'if':
                # Remove the parent condition from the context. We were peeking
                # before, now we pop().
                self.context.pop()

            if condition.type == 'endif':
                if not isinstance(parent, Condition):
                    raise RuntimeError('Cannot close unopened or already ' +
                            'closed condition. Condition: "%s"' % condition.name)
            else:
                self.context.append(condition)

        else:
            raise RuntimeError('Unknown tag: "%s"' % name)

    def end_element(self, name):
        if name == 'condition':
            element = self.context[-1]
            if not isinstance(element, Condition) and not isinstance(element, Struct):
                raise RuntimeError("Expected condition or struct tag to be closed.")

            return

        element = self.context.pop()

        if name == "struct":
            if not isinstance(element, Struct):
                raise RuntimeError("Expected struct tag to be closed.")
        elif name == "field":
            if not isinstance(element, Field):
                raise RuntimeError("Expected field tag to be closed.")
        elif name == "enum":
            if not isinstance(element, Enum):
                raise RuntimeError("Expected enum tag to be closed.")
        elif name == "value":
            if not isinstance(element, Value):
                raise RuntimeError("Expected value tag to be closed.")
        elif name == "define":
            if not isinstance(element, Define):
                raise RuntimeError("Expected define tag to be closed.")
        elif name == "csbgen":
            if not isinstance(element, Csbgen):
                raise RuntimeError("""Expected csbgen tag to be closed.
                Some tags may have not been closed""")

            element.emit()
        else:
            raise RuntimeError('Unknown closing element: "%s"' % name)

    def parse(self, filename):
        file = open(filename, "rb")
        self.filename = filename
        self.parser.ParseFile(file)
        file.close()

if len(sys.argv) < 2:
    print("No input xml file specified")
    sys.exit(1)

input_file = sys.argv[1]

p = Parser()
p.parse(input_file)
