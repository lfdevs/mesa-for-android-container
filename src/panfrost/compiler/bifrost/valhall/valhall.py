#encoding=utf-8

# Copyright (C) 2016 Intel Corporation
# Copyright (C) 2016 Broadcom
# Copyright (C) 2020 Collabora, Ltd.
# SPDX-License-Identifier: MIT

import os
import textwrap
import xml.etree.ElementTree as ET
import sys

# All instructions in the ISA
instructions = []

MODIFIERS = {}
enums = {}
immediates = []

def xmlbool(s):
    assert(s.lower() in ["false", "true"])
    return False if s.lower() == "false" else True

def bitmask(size):
    return (1 << size) - 1

class EnumValue:
    def __init__(self, value, default):
        self.value = value
        self.default = default

class Enum:
    def __init__(self, name, values):
        self.name = name
        self.values = values
        self.bare_values = [x.value for x in values]

        defaults = [x.value for x in values if x.default]
        if len(defaults) > 0:
            assert(len(defaults) == 1)
            self.default = defaults[0]

def build_enum(el):
    values = []

    for child in el:
        if child.tag == 'value':
            is_default = child.attrib.get('default', False)
            values.append(EnumValue(child.text, is_default))
        elif child.tag == 'reserved':
            values.append(EnumValue("reserved", False))

    return Enum(el.attrib['name'], values)

class Modifier:
    def __init__(self, name, start, size, implied = False, force_enum = None):
        self.name = name
        self.start = start
        self.size = size
        self.implied = implied
        self.is_enum = (force_enum is not None) or size > 1
        self.enum = force_enum or name

        if not self.is_enum:
            self.bare_values = ['', name]
            self.default = 0
        else:
            self.bare_values = [x.value for x in enums[self.enum].values]
            defaults = [x for x in enums[self.enum].values if x.default]
            assert(len(defaults) <= 1)

            if len(defaults) > 0:
                self.default = self.bare_values.index(defaults[0].value)
            else:
                self.default = None

def Flag(name, start):
    return Modifier(name, start, 1)

# Model a single instruction
class Source:
    def __init__(self, index, size, is_float = False, swizzle = False,
            halfswizzle = False, widen = False, lanes = False, combine = False, lane = None, absneg = False, notted = False, name = ""):
        self.is_float = is_float or absneg
        self.start = (index * 8)
        self.size = size
        self.absneg = absneg
        self.notted = notted
        self.swizzle = swizzle
        self.halfswizzle = halfswizzle
        self.widen = widen
        self.lanes = lanes
        self.lane = lane
        self.combine = combine
        self.name = name

        self.offset = {}
        self.mask = {}

        self.offset['mode'] = self.start + 6
        self.mask['mode'] = bitmask(2)
        self.offset['value'] = self.start
        self.mask['value'] = bitmask(6)

        if absneg:
            self.offset['neg'] = 32 + 2 + ((2 - index) * 2)
            self.offset['abs'] = 33 + 2 + ((2 - index) * 2)
            self.mask['neg'] = bitmask(1)
            self.mask['abs'] = bitmask(1)
        if notted:
            self.offset['not'] = 35
            self.mask['not'] = bitmask(1)
        if widen or lanes or halfswizzle:
            self.offset['widen'] = 26 if index == 1 else 36
            self.mask['widen'] = bitmask(4)
        if lane:
            self.offset['lane'] = self.lane
            self.mask['lane'] = bitmask(2) if size in (8, 32) else bitmask(1)
        if swizzle:
            assert(size in [16, 32])
            self.offset['swizzle'] = 24 + ((2 - index) * 2)
            self.mask['swizzle'] = bitmask(2)
        if combine:
            self.offset['combine'] = 37
            self.mask['combine'] = bitmask(3)

class Dest:
    def __init__(self, size, name = ""):
        self.name = name
        self.start = 40
        self.size = size
        self.offset = {}
        self.mask = {}

        self.offset['mode'] = self.start + 6
        self.mask['mode'] = bitmask(2)
        self.offset['value'] = self.start
        self.mask['value'] = bitmask(6)

class Staging:
    def __init__(self, read = False, write = False, count = 0, flags = 'true', name = ""):
        self.name = name
        self.read = read
        self.write = write
        self.count = count
        self.flags = (flags != 'false')
        self.start = 40
        if write and not self.flags:
            self.start = 16
        self.offset = {}
        self.mask = {}

        self.offset['value'] = self.start
        self.mask['value'] = bitmask(6)

        # For compatibility
        self.absneg = False
        self.swizzle = False
        self.notted = False
        self.widen = False
        self.lanes = False
        self.lane = False
        self.halfswizzle = False
        self.combine = False
        self.size = 32

        if not self.flags:
            self.encoded_flags = 0
        elif flags == 'rw':
            self.encoded_flags = 0xc0
        else:
            assert(flags == 'true')
            self.encoded_flags = (0x80 if write else 0) | (0x40 if read else 0)

class Immediate:
    def __init__(self, name, start, size, signed):
        self.name = name
        self.start = start
        self.size = size
        self.signed = signed

class Opcode:
    def __init__(self, value, start, mask):
        self.value = value
        self.start = start
        self.mask = mask

class Instruction:
    def __init__(self, name, opcode, srcs = [], dests = [], immediates = [], modifiers = [], staging = None, unit = None):
        self.name = name
        self.srcs = srcs
        self.dests = dests
        self.opcode = opcode
        self.immediates = immediates
        self.modifiers = modifiers
        self.staging = staging
        self.unit = unit
        self.is_signed = len(name.split(".")) > 1 and ('s' in name.split(".")[1])

        self.offset = {}
        self.mask = {}

        self.offset['flow'] = 59
        self.mask['flow'] = bitmask(4)
        self.offset['fau_page'] = 57
        self.mask['fau_page'] = bitmask(2)

        # Message-passing instruction <===> not ALU instruction
        self.message = unit not in ["FMA", "CVT", "SFU"]

        assert(len(dests) == 0 or not staging)

    def __str__(self):
        return self.name

# Build a single source from XML
def build_source(el, i, size):
    lane = el.get('lane', None)
    if lane == "true":
        lane = 38 if i == 0 else 36
    elif lane is not None:
        lane = int(lane)

    return Source(i, int(el.get('size', size)),
            absneg = el.get('absneg', False),
            is_float = el.get('float', False),
            swizzle = el.get('swizzle', False),
            halfswizzle = el.get('halfswizzle', False),
            widen = el.get('widen', False),
            lanes = el.get('lanes', False),
            combine = el.get('combine', False),
            lane = lane,
            notted = el.get('not', False),
            name = el.text or "")

def build_imm(el):
    return Immediate(el.attrib['name'], int(el.attrib['start']),
            int(el.attrib['size']), bool(el.attrib.get('signed', False)))

def build_staging(i, el):
    r = xmlbool(el.attrib.get('read', 'false'))
    w = xmlbool(el.attrib.get('write', 'false'))
    count = int(el.attrib.get('count', '0'))
    flags = el.attrib.get('flags', 'true')

    return Staging(r, w, count, flags, el.text or '')

def build_modifier(el):
    name = el.attrib['name']
    start = int(el.attrib['start'])
    size = int(el.attrib['size'])
    implied = xmlbool(el.get('implied', 'false'))

    return Modifier(name, start, size, implied)

def build_opcode(el, name):
    op_arr = []
    opcode = el.find(name)
    if opcode is None:
        return None

    for subcode in opcode:
        value = int(subcode.get('val'), base=0)
        start = int(subcode.get('start'))
        mask = int(subcode.get('mask'), base=0)
        assert((value & mask) == value)
        op_arr.append(Opcode(value, start, mask))

    return op_arr

# Build a single instruction from XML and group based overrides
def build_instr(el, overrides = {}):
    # Get overridables
    name = overrides.get('name') or el.attrib.get('name')
    opcode = overrides.get('opcode') or build_opcode(el, 'opcode')
    unit = overrides.get('unit') or el.attrib.get('unit')

    # Get explicit sources/dests
    tsize = typesize(name)
    sources = []
    i = 0

    for src in el.findall('src'):
        if (src.attrib.get('pseudo', False)):
            continue
        built = build_source(src, i, tsize)
        sources += [built]

        # 64-bit sources in a 32-bit (message) instruction count as two slots
        # Affects BLEND, ST_CVT
        if tsize != 64 and built.size == 64:
            i = i + 2
        else:
            i = i + 1

    dests = [Dest(int(tsize), dest.text or '') for dest in el.findall('dest')]

    # Get implicit ones
    sources = sources + ([Source(i, int(tsize)) for i in range(int(el.attrib.get('srcs', 0)))])
    dests = dests + ([Dest(int(tsize))] * int(el.attrib.get('dests', 0)))

    # Get staging registers
    staging = [build_staging(i, el) for i, el in enumerate(el.findall('sr'))]

    # Get immediates
    imms = [build_imm(imm) for imm in el.findall('imm')]

    modifiers = []
    for mod in el:
        if (mod.tag in MODIFIERS) and not (mod.attrib.get('pseudo', False)):
            modifiers.append(MODIFIERS[mod.tag])
        elif mod.tag =='va_mod':
            modifiers.append(build_modifier(mod))

    instr = Instruction(name, opcode, srcs = sources, dests = dests, immediates = imms, modifiers = modifiers, staging = staging, unit = unit)

    instructions.append(instr)

# Build all the instructions in a group by duplicating the group itself with
# overrides for each distinct instruction
def build_group(el):
    for ins in el.findall('ins'):
        build_instr(el, overrides = {
            'name': ins.attrib['name'],
            'opcode': build_opcode(ins, 'opcode'),
            'unit': ins.attrib.get('unit'),
        })

def to_alphanum(name):
    substitutions = {
        ' ': '_',
        '/': '_',
        '[': '',
        ']': '',
        '(': '',
        ')': '',
        '-': '_',
        ':': '',
        '.': '',
        ',': '',
        '=': '',
        '>': '',
        '#': '',
        '&': '',
        '*': '',
        '"': '',
        '+': '',
        '\'': '',
    }

    for i, j in substitutions.items():
        name = name.replace(i, j)

    return name

def safe_name(name):
    name = to_alphanum(name)
    if not name[0].isalpha():
        name = '_' + name

    return name.lower()

# Parses out the size part of an opcode name
def typesize(name):
    if name[-3:] == '128':
        return 128
    if name[-2:] == '48':
        return 48
    elif name[-1] == '8':
        return 8
    else:
        try:
            return int(name[-2:])
        except:
            return 32

# Parse the ISA
def valhall_parse_isa(xmlfile):
    global MODIFIERS
    global enums
    global immediates
    global root

    tree = ET.parse(xmlfile)
    root = tree.getroot()

    # All immediates in the ISA
    ilut = root.findall('lut')[0]
    assert(ilut.attrib['name'] == "Immediates")
    immediates = [int(imm.text, base=0) for imm in ilut.findall('constant')]

    for child in root.findall('enum'):
        enums[safe_name(child.attrib['name'])] = build_enum(child)

    MODIFIERS = {
        # Texture instructions share a common encoding
        "wide_indices": Flag("wide_indices", 8),
        "array_enable": Flag("array_enable", 10),
        "texel_offset": Flag("texel_offset", 11),
        "shadow": Flag("shadow", 12),
        "integer_coordinates": Flag("integer_coordinates", 13),
        "fetch_component": Modifier("fetch_component", 14, 2),
        "lod_mode": Modifier("lod_mode", 13, 3),
        "lod_bias_disable": Modifier("lod_mode", 13, 1),
        "lod_clamp_disable": Modifier("lod_mode", 14, 1),
        "write_mask": Modifier("write_mask", 22, 4),
        "register_type": Modifier("register_type", 26, 2),
        "dimension": Modifier("dimension", 28, 2),
        "skip": Flag("skip", 39),
        "register_width": Modifier("register_width", 46, 1, force_enum = "register_width"),
        "secondary_register_width": Modifier("secondary_register_width", 47, 1, force_enum = "register_width"),
        "vartex_register_width": Modifier("varying_texture_register_width", 24, 2),

        "atom_opc": Modifier("atomic_operation", 22, 4),
        "atom_opc_1": Modifier("atomic_operation_with_1", 22, 4),
        "inactive_result": Modifier("inactive_result", 22, 4),
        "memory_access": Modifier("memory_access", 24, 2),
        "regfmt": Modifier("register_format", 24, 3),
        "source_format": Modifier("source_format", 24, 4),
        "vecsize": Modifier("vector_size", 28, 2),

        "slot": Modifier("slot", 30, 3),
        "roundmode": Modifier("round_mode", 30, 2),
        "result_type": Modifier("result_type", 30, 2),
        "saturate": Flag("saturate", 30),
        "not_result": Flag("not_result", 30),

        "lane_op": Modifier("lane_operation", 32, 2),
        "cmp": Modifier("condition", 32, 3),
        "clamp": Modifier("clamp", 32, 2),
        "sr_count": Modifier("staging_register_count", 33, 3, implied = True),
        "sample_and_update": Modifier("sample_and_update_mode", 33, 3),
        "sr_write_count": Modifier("staging_register_write_count", 36, 3, implied = True),

        "conservative": Flag("conservative", 35),
        "subgroup": Modifier("subgroup_size", 36, 4),
        "update": Modifier("update_mode", 36, 2),
        "sample": Modifier("sample_mode", 38, 2),
    }

    for child in root:
        if child.tag == 'group':
            build_group(child)
        elif child.tag == 'ins':
            build_instr(child)

    instruction_dict = { ins.name: ins for ins in instructions }

    # Validate there are no duplicated instructions
    if len(instruction_dict) != len(instructions):
        import collections
        counts = collections.Counter([i.name for i in instructions])
        for c in counts:
            if counts[c] != 1:
                print(f'{c} appeared {counts[c]} times.')

    assert(len(instruction_dict) == len(instructions))

    return (instructions, immediates, enums, typesize, safe_name)
