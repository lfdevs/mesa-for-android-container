#!/usr/bin/env python3
#
# Copyright 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

"""
The last parameter determines which files is generated.

If the last parameter is 'packets_h':

    The header file with packet definitions is generated. The parameters must be specified
    in the following order and must contain 'gfx'. The gfx version must be the second last
    parameter. All other input files are only used to resolve definition conflicts.

    Parameters:
        cp_pm4_table_data_gfx$1.json
        pm4_it_opcodes_gfx$1.h
        ...
        cp_pm4_table_data_gfx$N.json
        pm4_it_opcodes_gfx$N.h
        gfx$VERSION (e.g. 'gfx11')
        packets_h

If the last parameter is 'print_h' or 'print_c':

    The packet parser is generated.

    Parameters:
        cp_pm4_table_data_gfx$N.json
        pm4_it_opcodes_gfx$N.h
        print_h OR print_c
"""

import sys, json, re

# The printer doesn't print certain variable-length packets, register-setting packets, and packets
# requiring custom printing code.
no_printer_support = {
    'NOP',
    'FENCE_WAIT_MULTI',
    'INDIRECT_BUFFER',
    'SET_CONFIG_REG',
    'SET_CONTEXT_REG',
    'SET_CONTEXT_REG_PAIRS',
    'SET_CONTEXT_REG_PAIRS_PACKED',
    'SET_SH_REG',
    'SET_SH_REG_INDEX',
    'SET_SH_REG_PAIRS',
    'SET_SH_REG_PAIRS_PACKED',
    'SET_SH_REG_PAIRS_PACKED_N',
    'SET_UCONFIG_REG',
    'SET_UCONFIG_REG_INDEX',
}

# Packet fields that should be printed as registers.
packet_field_register_map = {
    # (name, first_bit): (register, mask)
    ('COHER_CNTL', 0): ('R_0301F0_CP_COHER_CNTL', ~0),
    ('EVENT_TYPE', 0): ('R_028A90_VGT_EVENT_INITIATOR', 0x3F),
    ('GCR_CNTL', 0): ('R_587_GCR_CNTL', ~0),
    ('DISPATCH_INITIATOR', 0): ('R_00B800_COMPUTE_DISPATCH_INITIATOR', ~0),
    ('DRAW_INITIATOR', 0): ('R_0287F0_VGT_DRAW_INITIATOR', ~0),
}

# Packet fields that are addresses for invoking ac_ib_handle_address.
# The whole dword must be the whole address_hi field, and the whole previous dword must be
# the whole address_lo field.
address_field_map = {
    # address_hi field: (packed list, condition, count)
    # - If the packet list is not empty, ac_ib_handle_address is only called for these packets.
    # - If the condition is not empty, it determines whether the dwords contain an address.
    #   (if the condition is missing, the packet word must have only 1 variant)
    # - If the count is not empty, it must be the code that returns the byte count for ac_ib_handle_address.
    'ADDR_HI': ([], '', ''),
    'CONTROL_BUF_ADDR_HI': ([], '', ''),
    'COUNT_ADDR_HI': ([], '', ''),
    'DST_MEM_ADDR_HI': ([], ('G_371_DST_SEL(dw0) == V_371_MEMORY_SYNC_ACROSS_GRBM || ' +
                             'G_371_DST_SEL(dw0) == V_371_TC_L2 || ' +
                             'G_371_DST_SEL(dw0) == V_371_MEMORY'), ''),
    'INDEX_BASE_HI': ([], '', ''),
    'DST_ADDR_HI': (['DMA_DATA'],
                    ('G_501_DST_SEL(dw0) == V_501_DST_ADDR_USING_DAS || ' +
                     'G_501_DST_SEL(dw0) == V_501_DST_ADDR_USING_L2'),
                    'G_506_BYTE_COUNT(dw5)'),
    'SRC_ADDR_HI': (['DMA_DATA'],
                    ('G_501_SRC_SEL(dw0) == V_501_SRC_ADDR_USING_SAS || ' +
                     'G_501_SRC_SEL(dw0) == V_501_SRC_ADDR_USING_L2'),
                    'G_506_BYTE_COUNT(dw5)'),
    'ADDRESS_HI': (['EVENT_WRITE', 'SET_BASE'],
                   'opcode != PKT3_EVENT_WRITE || G_461_EVENT_TYPE(dw0) != V_028A90_PIXEL_PIPE_STAT_CONTROL',
                   ''),
}


engines_dict = {'pfp': 0, 'meg': 1, 'mec': 2}


def gfx_version_to_int(s):
    assert s[0:3] == 'gfx'
    return int(s[3:])


def engine_to_int(s):
    assert s in engines_dict
    return engines_dict[s]


def int_to_engine(i):
    # Swap keys and values
    rev_dict = {v: k for k, v in engines_dict.items()}
    assert i in rev_dict
    return rev_dict[i]


# Return (suffix, comment) that should be added to the definition by detecting content conflicts
# in list = [(gfx_version_int, engine_int, content), ...]. Other parameters determine which
# definition this is for.
def get_conflict_suffix(list, in_gfx_version_name, in_engine_name, in_content):
    assert len(list) > 0

    in_gfx_version = gfx_version_to_int(in_gfx_version_name)
    in_engine = engine_to_int(in_engine_name)

    # Are there any conflicts?
    any_conflict = False

    for _, _, content in list:
        _, _, first_content = list[0]
        if first_content != content:
            any_conflict = True
            break

    if not any_conflict:
        return ('', '')

    # Are there any conflicts between engines of the same gfx version?
    any_engine_conflict = False
    matching_engines = set()

    first = {}
    for gfx_version, engine, content in list:
        if content == in_content:
            matching_engines.add(engine)

        if gfx_version not in first:
            first[gfx_version] = content
        elif first[gfx_version] != content:
            any_engine_conflict = True

    # Are there any conflicts between gfx versions of the same engine?
    any_gfx_conflict = False
    first_matching_gfx_version = -1
    matching_gfx_versions = set()

    first = {}
    for gfx_version, engine, content in list:
        if content == in_content:
            matching_gfx_versions.add(gfx_version)
            if first_matching_gfx_version == -1 and gfx_version <= in_gfx_version:
                first_matching_gfx_version = gfx_version

        if engine not in first:
            first[engine] = content
        elif first[engine] != content:
            any_gfx_conflict = True

    # Assemble the name suffix and comment for the definition
    assert any_gfx_conflict or any_engine_conflict
    suffix = ((('_gfx%d' % first_matching_gfx_version) if any_gfx_conflict else '') +
              (('_%s' % in_engine_name) if any_engine_conflict else ''))

    comment = ' /* '
    if any_gfx_conflict:
        comment += ', '.join([('gfx%d' % x) for x in matching_gfx_versions])
    if any_gfx_conflict and any_engine_conflict:
        comment += ' | '
    if any_engine_conflict:
        comment += ', '.join([int_to_engine(x) for x in matching_engines])
    comment += ' */'

    return (suffix, comment)


# Return (suffix, comment) for the given packet.
def get_packet_conflict_suffix(gfx_opcodes, in_gfx_version_name, in_packet_name, in_opcode):
    # Gather an array of tuples (gfx_version_int, content) for the packet.
    list = []

    for gfx_version_name, opcodes in gfx_opcodes.items():
        if in_packet_name in opcodes:
            list += [(gfx_version_to_int(gfx_version_name), 0, opcodes[in_packet_name])]

    return get_conflict_suffix(list, in_gfx_version_name, int_to_engine(0), in_opcode)


# Return (suffix, comment) for the given packet field.
def get_field_conflict_suffix(gfx_versions, in_gfx_version_name, in_engine_name,
                              in_packet_name, in_word_index, in_word_variant_name,
                              in_field_name, in_field_bits):
    # Gather an array of tuples (gfx_version_int, engine_int, content) for the field.
    list = []
    for gfx_version_name, engines in gfx_versions.items():
        for engine_name, packets in engines.items():
            if in_packet_name in packets:
                packet = packets[in_packet_name]
                word_indices = packet['word']

                if in_word_index in word_indices:
                    word = word_indices[in_word_index]

                    if in_word_variant_name in word:
                        word_variant = word[in_word_variant_name]

                        if in_field_name in word_variant:
                            field = word_variant[in_field_name]
                            bits = field['bits']
                            assert bits != ''

                            list += [(gfx_version_to_int(gfx_version_name),
                                      engine_to_int(engine_name), bits)]

    return get_conflict_suffix(list, in_gfx_version_name, in_engine_name, in_field_bits)


# Return (suffix, comment) for the given field value.
# Disambiguation between gfx versions and engines is not implemented.
def get_value_conflict_suffix(gfx_versions, in_gfx_version_name, in_engine_name, in_packet_name, in_word_index,
                              in_word_variant_name, in_field_name, in_value_name, in_value_int):
    for gfx_version_name, engines in gfx_versions.items():
        for engine_name, packets in engines.items():
            if in_packet_name in packets:
                packet = packets[in_packet_name]
                enums = packet['enum']
                word_indices = packet['word']

                if in_word_index in word_indices:
                    word = word_indices[in_word_index]

                    if in_word_variant_name in word:
                        word_variant = word[in_word_variant_name]

                        # Conflicts between values of the same word are also possible.
                        for field_name, field in word_variant.items():
                            if field_name in enums:
                                enum = enums[field_name]

                                if in_value_name in enum:
                                    value = enum[in_value_name]
                                    value_int = value['value']

                                    if value['value'] != in_value_int:
                                        return ('_%s' % in_field_name, ' /* only %s */' % in_field_name)
    return ('', '')


def get_field_bits(field):
    bits_str = field['bits'] # Form: $last:$first or $first

    if ':' in bits_str:
        left, right = bits_str.split(':', 1)
        last_bit, first_bit = int(left), int(right)
    else:
        first_bit = int(bits_str)
        last_bit = first_bit

    return (first_bit, last_bit)


def print2(s1, s2):
    print(s1.ljust(80) + s2)


re_opcode = re.compile(r"^\s*IT_(?P<name>\w+)\s*=\s*(?P<hex>0x[\da-fA-F]+),*$")
re_gfx_number = re.compile(r"gfx(\d+)")


def print_packet_definitions():
    assert len(sys.argv) % 2 == 0 # argv = executable, N*2 input files, gfx$VERSION
    num_gfx_versions = (len(sys.argv) - 2) // 2
    assert num_gfx_versions > 0

    for i in range(1, len(sys.argv)):
        assert 'gfx' in sys.argv[i]

    gfx_version_param = sys.argv[-1]
    gfx_versions = {}
    gfx_opcodes = {}

    for i in range(num_gfx_versions):
        packet_filename = sys.argv[1 + i * 2]
        opcode_filename = sys.argv[1 + i * 2 + 1]

        assert (gfx_version_param in packet_filename) == (gfx_version_param in opcode_filename)

        file_gfx_version = 'gfx' + re_gfx_number.search(packet_filename).group(1)

        # Load the packet file
        engines = json.load(open(packet_filename, 'r', encoding='utf-8'))['pm4_packets']
        assert 'mes' not in engines, 'Removed "mes" from the json file.'
        gfx_versions[file_gfx_version] = engines

        # Load the opcode file
        opcode_file = open(opcode_filename, 'r', encoding='utf-8')
        opcodes = {}

        for line in opcode_file:
            match = re_opcode.match(line)
            if match:
                opcodes[match['name']] = int(match['hex'], 16)

        gfx_opcodes[file_gfx_version] = opcodes

    print(
"""/* This file is automatically generated. DO NOT EDIT.
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* Packets for each engine are defined separately.
 *
 * Engines:
 * - PFP = gfx queue engine 1
 * - MEG = gfx queue engine 2
 * - MEC = compute queue engine
 *
 * Packet field definition format: [SGC]_$OPCODE_$WORD_$FIELD
 *   Prefix meaning:
 *     S_... = set field
 *     G_... = get field
 *     C_... = clear field
 *   OPCODE = hexadecimal opcode number of the PKT3_* definition
 *   WORD = word index within the packet (0=header, 1=word1, 2=word2, ...)
 *   FIELD = the field within the word
 *
 *   E.g.: S_46_1_EVENT_TYPE
 *
 * If a definition contains GFX$version, it means the definition is only valid since that gfx version.
 * If a definition contains PFP, MEG, or MEC, it means the definition is only valid for those engines.
 * The comment after definitions, if present, lists gfx versions and engines where the definition is valid.
 */""")

    # Validate that the opcodes have defined packets
    for packet_name in gfx_opcodes[gfx_version_param].keys():
        if 'RESERVED' in packet_name:
            continue

        found = False
        for packets in gfx_versions[gfx_version_param].values():
            if packet_name in packets:
                found = True
                break

        if not found:
            print('/* NOTE: %s %s has opcode definition but no packet definition */' % (gfx_version_param, packet_name))

    # Validate that defined packets have opcodes
    for engine_name, packets in gfx_versions[gfx_version_param].items():
        for packet_name, packet in packets.items():
            if packet_name not in gfx_opcodes[gfx_version_param]:
                print('/* NOTE: %s %s has packet definition but no opcode definition */' % (gfx_version_param, packet_name))

    # Iterate over the dictionary.
    for engine_name, packets in gfx_versions[gfx_version_param].items():
        for packet_name, packet in packets.items():
            if packet_name not in gfx_opcodes[gfx_version_param]:
                continue

            opcode = gfx_opcodes[gfx_version_param][packet_name]
            opcode_suffix, opcode_comment = get_packet_conflict_suffix(gfx_opcodes, gfx_version_param, packet_name, opcode)

            print('')
            print2('#define PKT3_%s%s' % (packet_name, opcode_suffix.upper()),
                   '0x%X%s' % (opcode, opcode_comment))

            enums = packet['enum'] if 'enum' in packet else {}

            for word_index, word in packet['word'].items():
                if int(word_index) == 1:
                    continue # it's the packet header.

                for word_variant_name, word_variant in word.items():
                    if len(word.items()) > 1:
                        variant_letter = word_variant_name.upper()
                        variant_str = ' variant %s' % variant_letter
                    else:
                        variant_letter = ''
                        variant_str = ''

                    word_comment = ('%s PKT3_%s word %d%s' %
                                    (engine_name.upper(), packet_name, int(word_index) - 1, variant_str))

                    # If the number of fields is 1...
                    if len(word_variant) == 1:
                        field_name, field = next(iter(word_variant.items()))
                        first_bit, last_bit = get_field_bits(field)

                        if first_bit == 0 and last_bit == 31:
                            if field_name.startswith('reserved') or field_name.startswith('dummy'):
                                print('/* %s must be 0 */' % word_comment)
                            else:
                                print('/* %s is: %s (32 bits) */' % (word_comment, field_name.upper()))
                            continue

                    print('/* %s fields: */' % word_comment)

                    for field_name, field in word_variant.items():
                        if field_name.startswith('reserved'):
                            continue

                        field_suffix, field_comment = (
                            get_field_conflict_suffix(gfx_versions, gfx_version_param, engine_name, packet_name, word_index,
                                                      word_variant_name, field_name, field['bits']))
                        mangled_prefix = '%02X%d%s' % (opcode, int(word_index) - 1, variant_letter)
                        mangled_name = '%s_%s%s' % (mangled_prefix, field_name.upper(), field_suffix.upper())

                        first_bit, last_bit = get_field_bits(field)
                        num_bits = last_bit - first_bit + 1
                        bitmask = (1 << num_bits) - 1
                        clear_mask = (bitmask << first_bit) ^ 0xffffffff

                        assert num_bits < 32
                        encode_field = '(((unsigned)(x) & 0x%X) << %d)' % (bitmask, first_bit)
                        decode_field = '(((x) >> %d) & 0x%X)' % (first_bit, bitmask)
                        clear_field = '0x%08X' % clear_mask

                        print2('#define   S_%s(x)' % mangled_name, encode_field + field_comment)
                        print2('#define   G_%s(x)' % mangled_name, decode_field)
                        print2('#define   C_%s' % mangled_name, clear_field)

                        if field_name in enums:
                            for value_name, value in enums[field_name].items():
                                if value_name.startswith('reserved'):
                                    continue

                                value_int = value['value']
                                value_suffix, value_comment = (
                                    get_value_conflict_suffix(gfx_versions, gfx_version_param, engine_name, packet_name, word_index,
                                                              word_variant_name, field_name, value_name, value_int))

                                print2('#define     V_%s_%s%s' % (mangled_prefix, value_name.upper(), value_suffix.upper()),
                                       str(value_int) + value_comment)


def packet_has_engine_sel(packet_dict):
    return ('pfp' in packet_dict and 'meg' in packet_dict and
            'engine_sel' in packet_dict['pfp']['word']['2']['a'])


def print_enum_table(packet_name, packet_dict):
    # Gather a merged enum table from all engines.
    for engine_name, packet in packet_dict.items():
        if 'enum' not in packet:
            continue

        # Packets that have both PFP and MEG definitions and don't have ENGINE_SEL are parsed as PFP,
        # so ignore MEG enums.
        if engine_name == 'meg' and 'pfp' in packet_dict and not packet_has_engine_sel(packet_dict):
            continue;

        enums = packet['enum'] if 'enum' in packet else {}
        table = {}

        for field_name, values in enums.items():
            assert len(values) > 0

            if field_name not in table:
                table[field_name] = {}

            for value_name, value_item in values.items():
                value = value_item['value']

                if value_name.startswith('reserved'):
                    continue

                if value in table[field_name]:
                    if table[field_name][value] != value_name:
                        print('// Enum conflict: Packet %s field %s has value %d = %s, but the table already has %d = %s' %
                              (packet_name, field_name, value, value_name.upper(), value, table[field_name][value].upper()))
                else:
                    table[field_name][value] = value_name

        for field_name, values in table.items():
            print('')
            print('static const char *%s_%s_%s[] = {' % (engine_name, packet_name, field_name))

            for value, value_name in table[field_name].items():
                print(3 * ' ' + '[%d] = "%s",' % (value, value_name.upper()))

            print('};')


def print_packet(packet_name, packet_dict, engine_name, dword0_read):
    if engine_name not in packet_dict:
        print(9 * ' ' + 'fprintf(stderr, "amdgpu: packet %s is not supported by %s\\n");' %
              (packet_name, engine_name.upper()))
        print(9 * ' ' + 'assert(0 && "packet %s is not supported by %s");' % (packet_name, engine_name.upper()))
        return

    packet = packet_dict[engine_name]
    enums = packet['enum'] if 'enum' in packet else {}
    words = packet['word']
    seen_variable_length_word = False

    # Some packets need dwords to be loaded first if the byte count is after the address words.
    load_dwords_first = packet_name == 'DMA_DATA'

    if load_dwords_first:
        for word_index, word_variants in words.items():
            if int(word_index) == 1:
                continue # it's the packet header.

            if int(word_index) == 2 and dword0_read:
                continue

            # Don't load any variable-length fields here.
            has_variable_length_field = False
            for _, word_variant in word_variants.items():
                has_variable_length_field = has_variable_length_field or len([x for x in word_variant.keys() if '[]' in x]) > 0
            if has_variable_length_field:
                continue

            word_index_0based = int(word_index) - 2

            if len(word_variants) == 0:
                print(9 * ' ' + 'if (%d <= pkt_count_field) ac_ib_get(ib);' % word_index_0based)
            else:
                print(9 * ' ' + 'uint32_t dw%d = %d <= pkt_count_field ? ac_ib_get(ib) : 0;' % (word_index_0based, word_index_0based))

    # Print the dwords.
    for word_index, word_variants in words.items():
        if int(word_index) == 1:
            continue # it's the packet header.

        get_dword = (int(word_index) > 2 or not dword0_read) and not load_dwords_first
        word_index_0based = int(word_index) - 2
        dword_var = 'dw%d' % word_index_0based

        # Parse the dword.
        for word_variant_name, word_variant in word_variants.items():
            prefix = ('[%s]' % word_variant_name.upper()) if len(word_variants) > 1 else ''
            num_printed_fields = len([field_name for field_name in word_variant.keys()
                                     if not field_name.startswith('reserved') and not field_name.startswith('dummy')])

            # If any field (it should be exactly one field) contains [], it's a variable-length packet.
            num_var_length_fields = len([x for x in word_variant.keys() if '[]' in x])
            if num_var_length_fields > 0:
                assert num_var_length_fields == 1
                seen_variable_length_word = True

                if packet_name == 'WRITE_DATA':
                    assert word_index_0based == 3
                    print(9 * ' ' + 'for (unsigned i = 0; i < pkt_count_field - 3; i++)')
                    print(12 * ' ' + 'ac_print_data_dword(ib->f, ac_ib_get(ib), "data");')
                else:
                    assert False, 'unexpected variable-length packet: %s' % packet_name
                continue

            assert not seen_variable_length_word

            # Get the next dword if needed.
            if get_dword:
                if word_index_0based > 0:
                    print('')

                if len(word_variant) == 0:
                    print(9 * ' ' + 'ac_ib_get(ib);')
                else:
                    print(9 * ' ' + 'uint32_t %s = ac_ib_get(ib);' % dword_var)

                get_dword = False

            # Iterate over all fields.
            for field_name, field in word_variant.items():
                # Get field bits.
                first_bit, last_bit = get_field_bits(field)
                num_bits = last_bit - first_bit + 1
                bitmask = (1 << num_bits) - 1

                if field_name.startswith('reserved') or field_name.startswith('dummy'):
                    # If a word has multiple variants, a reserved field in one variant may be used by another variant,
                    # and we don't know which word variant is used, so ignore reserved fields.
                    if len(word_variants) == 1:
                        if num_bits == 32:
                            print(9 * ' ' + 'assert(!%s && "reserved packet fields should be 0 for %s, word %d");' %
                                  (dword_var, packet_name, word_index_0based))
                        else:
                            print(9 * ' ' + 'assert(!((%s >> %d) & 0x%x) && "reserved packet fields should be 0 for %s, word %d");' %
                                  (dword_var, first_bit, bitmask, packet_name, word_index_0based))
                    continue

                # Some address fields don't use low bits. Include them anyway.
                if num_printed_fields == 1 and first_bit + num_bits == 32 and first_bit <= 8:
                    num_bits = 32

                # Extract the field value if needed.
                if num_bits < 32:
                    field_var = '%s%s_%s' % (dword_var, '' if len(word_variants) == 1 else word_variant_name.upper(), field_name)
                    print(9 * ' ' + 'uint32_t %s = (%s >> %d) & 0x%x;' % (field_var, dword_var, first_bit, bitmask))
                else:
                    field_var = dword_var

                register_map_key = (field_name.upper(), first_bit)

                # Choose one of the methods of printing the field
                if field_name in enums:
                    # Print it as an enum value string
                    enum_array = '%s_%s_%s' % (engine_name, packet_name, field_name)
                    value_name_var = '%s_str' % field_var

                    print(9 * ' ' + 'const char *%s = %s < ARRAY_SIZE(%s) ?' % (value_name_var, field_var, enum_array));
                    print(9 * ' ' + '                    %s[%s] : NULL;' % (enum_array, field_var))
                    print(9 * ' ' + 'assert(%s && "invalid/reserved values shouldn\'t be present");' % value_name_var)
                    print(9 * ' ' + 'ac_print_string_value(ib->f, "%s%s", %s);' % (prefix, field_name.upper(), value_name_var))
                elif register_map_key in packet_field_register_map:
                    # Print it as a register
                    reg_name, mask = packet_field_register_map[register_map_key]
                    print(9 * ' ' + 'ac_dump_reg(ib->f, ib->gfx_level, ib->family, %s, %s, %s);' %
                          (reg_name, field_var, hex(mask) if mask >= 0 else '~0'))
                else:
                    # Print it as a regular value
                    print(9 * ' ' + 'ac_print_named_value(ib->f, "%s%s", %s, %d);' %
                          (prefix, field_name.upper(), field_var, num_bits))

                # If the field is an address, invoke ac_ib_handle_address.
                if field_name.upper() in address_field_map:
                    packet_list, addr_condition, count = address_field_map[field_name.upper()]
                    indent = 9

                    if len(packet_list) == 0 or packet_name in packet_list:
                        assert len(addr_condition) > 0 or len(word_variants) == 1

                        if len(addr_condition) > 0:
                            print(9 * ' ' + 'if (%s)' % addr_condition)
                            indent = 12

                        print(indent * ' ' + 'ac_ib_handle_address(ib, %s, %s, %s);' %
                              ('dw%d' % (word_index_0based - 1), dword_var, '0' if count == '' else count))

        # Stop printing if that was the last word of the packet.
        if word_index_0based < len(words) - 2:
            print(9 * ' ' + 'if (pkt_count_field == %d) break;' % word_index_0based)


def should_skip_packet(packet_name):
    # TODO: This packet conflicts with INDIRECT_BUFFER (same opcode number), but we may need to handle it somehow
    return packet_name == 'COND_INDIRECT_BUFFER'


def get_packet_dict(engines, packet_name):
    # Get a dictionary of the packet definition where the engine name is the top-level key.
    packet_dict = {}

    for engine_name, packets in engines.items():
        if packet_name in packets:
            packet_dict[engine_name] = packets[packet_name]

    return packet_dict


def print_packet_parser(is_header):
    gfx_version = 'gfx' + re_gfx_number.search(sys.argv[1]).group(1)

    # Load the packet file
    engines = json.load(open(sys.argv[1], 'r', encoding='utf-8'))['pm4_packets']

    # Load the opcode file
    opcode_file = open(sys.argv[2], 'r', encoding='utf-8')
    opcodes = {}

    for line in opcode_file:
        match = re_opcode.match(line)
        if match:
            opcodes[match['name']] = int(match['hex'], 16)

    print(
"""/* This file is automatically generated. DO NOT EDIT.
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */
""")

    if is_header:
        print('#ifndef AMD_CP_IB_PARSER_%s' % gfx_version.upper())
        print('#define AMD_CP_IB_PARSER_%s' % gfx_version.upper())
        print('')
        print('#include "ac_debug.h"')
    else:
        print('#include "amd_cp_print_packet_%s.h"' % gfx_version)
        print('#include "amd_cp_packets_%s.h"' % gfx_version)
        print('#include "amdgfxregs.h"')
        print('#include "util/macros.h"')

    # Generate enum-to-string tables.
    if not is_header:
        for packet_name, value in opcodes.items():
            if not should_skip_packet(packet_name) and packet_name not in no_printer_support:
                print_enum_table(packet_name, get_packet_dict(engines, packet_name))

    print('')
    print('/* Print the packet and use assertions to validate its content. */')
    print('void')
    print('amd_cp_print_packet_%s(struct ac_ib_parser *ib, unsigned opcode, unsigned pkt_count_field)%s'
          % (gfx_version, ';' if is_header else ''))

    if is_header:
        print('')
        print('#endif')
        return

    print('{')
    print(3 * ' ' + 'switch (opcode) {')

    # Generate packet parser cases.
    for packet_name, value in opcodes.items():
        skip_packet = should_skip_packet(packet_name)
        if skip_packet:
            print('#if 0')

        packet_dict = get_packet_dict(engines, packet_name)
        print(3 * ' ' + 'case 0x%X: { /* PKT3_%s */' % (value, packet_name))

        if packet_name in no_printer_support:
            print(6 * ' ' + 'UNREACHABLE("the caller should handle %s");' % packet_name)
        else:
            has_engine_sel = packet_has_engine_sel(packet_dict)

            if has_engine_sel:
                print(6 * ' ' + 'uint32_t dw0 = ac_ib_get(ib);')
                print('')

            print(6 * ' ' + 'if (ib->ip_type == AMD_IP_COMPUTE) {')

            if has_engine_sel:
                # Generate an expression that checks ENGINE_SEL
                engine_sel_infix = ('%02X1%s' %
                    (opcodes[packet_name], '' if len(packet_dict['pfp']['word']['2']) == 1 else 'A'))
                engine_sel_getter = 'G_%s_ENGINE_SEL' % engine_sel_infix

                if 'pfp' in packet_dict['pfp']['enum']['engine_sel']:
                    pfp_value_name = 'PFP'
                elif 'prefetch_parser' in packet_dict['pfp']['enum']['engine_sel']:
                    pfp_value_name = 'PREFETCH_PARSER'
                else:
                    assert False, 'ENGINE_SEL doesn''t contain PFP or PREFETCH_PARSER'

                pfp_value = 'V_%s_%s' % (engine_sel_infix, pfp_value_name)

                print_packet(packet_name, packet_dict, 'mec', True)

                # Parse both PFP and MEG packet variants.
                print(6 * ' ' + '} else if (%s(dw0) == %s) {' % (engine_sel_getter, pfp_value))
                print_packet(packet_name, packet_dict, 'pfp', True)
                print(6 * ' ' + '} else {')
                print_packet(packet_name, packet_dict, 'meg', True)
            else:
                print_packet(packet_name, packet_dict, 'mec', False)
                print(6 * ' ' + '} else {')
                print_packet(packet_name, packet_dict, 'pfp' if 'pfp' in packet_dict else 'meg', False)

            print(6 * ' ' + '}')
            print(6 * ' ' + 'break;')

        print(3 * ' ' + '}')
        if skip_packet:
            print('#endif')
        print('')

    print(3 * ' ' + 'default:')
    print(6 * ' ' + 'fprintf(stderr, "amdgpu: cannot decode packet 0x%x\\n", opcode);')
    print(6 * ' ' + 'break;')

    print(3 * ' ' + '}')
    print('}')


if __name__ == "__main__":
    last = sys.argv.pop()

    if last == 'packets_h':
        print_packet_definitions()
    elif last == 'print_c':
        print_packet_parser(False)
    elif last == 'print_h':
        print_packet_parser(True)
    else:
        assert False, 'the last parameter must be "header" or "parser"'
