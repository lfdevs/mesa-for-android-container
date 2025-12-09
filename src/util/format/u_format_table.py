CopyRight = '''
/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
'''


import sys, os

from u_format_parse import *
import u_format_pack


def layout_map(layout):
    return 'UTIL_FORMAT_LAYOUT_' + str(layout).upper()


def colorspace_map(colorspace):
    return 'UTIL_FORMAT_COLORSPACE_' + str(colorspace).upper()

colorspace_channels_map = {
    'RGB': ['r', 'g', 'b', 'a'],
    'SRGB': ['sr', 'sg', 'sb', 'a'],
    'ZS': ['z', 's'],
    'YUV': ['y', 'u', 'v'],
}


type_map = {
    VOID:     "UTIL_FORMAT_TYPE_VOID",
    UNSIGNED: "UTIL_FORMAT_TYPE_UNSIGNED",
    SIGNED:   "UTIL_FORMAT_TYPE_SIGNED",
    FIXED:    "UTIL_FORMAT_TYPE_FIXED",
    FLOAT:    "UTIL_FORMAT_TYPE_FLOAT",
}


def bool_map(value):
    if value:
        return "true"
    else:
        return "false"


swizzle_map = {
    SWIZZLE_X:    "PIPE_SWIZZLE_X",
    SWIZZLE_Y:    "PIPE_SWIZZLE_Y",
    SWIZZLE_Z:    "PIPE_SWIZZLE_Z",
    SWIZZLE_W:    "PIPE_SWIZZLE_W",
    SWIZZLE_0:    "PIPE_SWIZZLE_0",
    SWIZZLE_1:    "PIPE_SWIZZLE_1",
    SWIZZLE_NONE: "PIPE_SWIZZLE_NONE",
}

def has_access(format):
    # We don't generate code for YUV formats, and many of the new ones lack
    # pack/unpack functions for softpipe/llvmpipe.
    noaccess_formats = [
        'r1_unorm',
        'y8_v8_u8_420_unorm',
        'y8_v8_u8_422_unorm',
        'y8_u8_v8_420_unorm',
        'y8_u8v8_420_unorm',
        'y8_u8v8_422_unorm',
        'y8_u8v8_444_unorm',
        'y8_v8u8_420_unorm',
        'y8_v8u8_422_unorm',
        'y8_v8u8_444_unorm',
        'y10_u10v10_420_unorm',
        'y10_u10v10_422_unorm',
        'x6y10_x6u10x6v10_420_unorm',
        'x4y12_x4u12x4v12_420_unorm',
        'y16_u16v16_420_unorm',
        'y10y10y10x2_u10v10u10x2v10u10v10x2_420_unorm',
        'x6y10x6u10x6y10x6v10_422_unorm',
        'x4y12x4u12x4y12x4v12_422_unorm',
        'y16u16y16v16_422_unorm',
        'u10y10v10a2_444_unorm',
        'x4u12x4y12x4v12x4a12_444_unorm',
        'u16y16v16a16_444_unorm',
        'x8y8u8v8_444_unorm',
        'a8y8u8v8_444_unorm',
        'r8g8_r8b8_422_unorm',
        'r8b8_r8g8_422_unorm',
        'g8r8_b8r8_422_unorm',
        'b8r8_g8r8_422_unorm',
        'g8r8_g8b8_422_unorm',
        'g8b8_g8r8_422_unorm',
        'b8g8_r8g8_422_unorm',
        'r16g16_r16b16_422_unorm',
        'x6r10x6g10_x6r10x6b10_422_unorm',
        'x6g10_x6b10x6r10_420_unorm',
        'x4g12_x4b12x4r12_420_unorm',
        'y8_400_unorm',
        'y8_u8_v8_422_unorm',
        'y8_u8_v8_444_unorm',
        'y8_u8_v8_440_unorm',
        'y10x6_u10x6_v10x6_420_unorm',
        'y12x4_u12x4_v12x4_420_unorm',
        'y16_u16_v16_420_unorm',
        'y10x6_u10x6_v10x6_422_unorm',
        'y12x4_u12x4_v12x4_422_unorm',
        'y16_u16_v16_422_unorm',
        'y10x6_u10x6_v10x6_444_unorm',
        'y12x4_u12x4_v12x4_444_unorm',
        'y16_u16_v16_444_unorm',
        'y16_u16v16_422_unorm',
        'r8_g8b8_420_unorm',
        'r8_b8g8_420_unorm',
        'g8_b8r8_420_unorm',
        'r8_g8b8_422_unorm',
        'r8_b8g8_422_unorm',
        'g8_b8r8_422_unorm',
        'r10_g10b10_420_unorm',
        'r10_g10b10_422_unorm',
        'r8_g8_b8_420_unorm',
        'r8_b8_g8_420_unorm',
        'g8_b8_r8_420_unorm',
        'r8_g8_b8_444_unorm',
        'r8g8b8_420_unorm_packed',
        'r10g10b10_420_unorm_packed',
        'y8_unorm',
        'y8u8v8_420_unorm_packed',
        'y10u10v10_420_unorm_packed',
        'g8_b8r8_444_unorm',
        'x6g10_x6b10x6r10_444_unorm',
        'x4g12_x4b12x4r12_444_unorm',
        'y16_u16v16_444_unorm',
    ]
    if format.short_name() in noaccess_formats:
        return False
    if format.layout in ('astc', 'atc'):
        return False
    if format.layout == 'etc' and format.short_name() != 'etc1_rgb8':
        return False
    return True

def write_format_table_header(file):
    print('/* This file is autogenerated by u_format_table.py from u_format.yaml. Do not edit directly. */', file=file)
    print(file=file)
    # This will print the copyright message on the top of this file
    print(CopyRight.strip(), file=file)
    print(file=file)

def write_format_enum_section(formats, type_name=None, max_formats=None):
    assert(max_formats == None or len(formats) <= max_formats)
    for idx, f in enumerate(formats):
        # Vertex formats must be first and must be <= 255.
        if idx == 0 and type_name != None:
            print('   PIPE_FORMAT_%s_START,' % (type_name), file=sys.stdout3)
            print('   %s = PIPE_FORMAT_%s_START,' % (f, type_name), file=sys.stdout3)
        elif idx == len(formats) - 1 and type_name != None:
            print('   %s,' % (f), file=sys.stdout3)
            print('   PIPE_FORMAT_%s_END = %s,' % (type_name, f), file=sys.stdout3)
        else:
            print('   %s,' % (f), file=sys.stdout3)

def collect_vertex_formats(formats):
    vertex_formats = []
    all_comp_types = [
        'UINT', 'SINT', 'FLOAT', 'UNORM', 'SNORM',
        'USCALED', 'SSCALED', 'FIXED'
    ]

    # RxGxBxAx_<type> first, then we'll deal with custom formats
    for bitsz in [64, 32, 16, 8]:
        for comp_type in all_comp_types:
            comp_suffix = ''
            for comp_name in ['R', 'G', 'B', 'A']:
                comp_suffix += comp_name + str(bitsz)
                format_name = 'PIPE_FORMAT_' + comp_suffix + '_' + comp_type
                f = formats.pop(format_name, None)
                if f != None:
                    vertex_formats.append(f.name)

    other_comp_suffixes = {
        # [A8]B8G8R8 variants
        'B8G8R8': all_comp_types,
        'B8G8R8A8': all_comp_types,
        'A8B8G8R8': ['USCALED', 'SSCALED'],
        # A8R8G8B8
        'A8R8G8B8': ['UNORM'],
        # R10G10B10A2/B10G10R10A2 variants
        'R10G10B10A2': all_comp_types,
        'B10G10R10A2': all_comp_types,
        # R11G11B10 variants
        'R11G11B10': ['FLOAT'],
    }
    for comp_suffix, comp_types in other_comp_suffixes.items():
        for comp_type in comp_types:
            format_name = 'PIPE_FORMAT_' + comp_suffix + '_' + comp_type
            f = formats.pop(format_name, None)
            if f != None:
                    vertex_formats.append(f.name)

    return vertex_formats

def collect_yuv_formats(formats):
    yuv_formats = []
    for name, f in formats.items():
        if f.colorspace == 'YUV':
            yuv_formats.append(f.name)
    for f in yuv_formats:
        formats.pop(f)
    return yuv_formats

def collect_compr_formats(formats):
    compr_formats = []
    for compr_type in ['s3tc', 'rgtc', 'etc', 'bptc', 'astc', 'atc', 'fxt1']:
        for cs in ['RGB', 'SRGB']:
            for name, f in formats.items():
                if f.layout == compr_type and f.colorspace == cs:
                    compr_formats.append(f.name)
    for f in compr_formats:
        formats.pop(f)
    return compr_formats

def collect_zs_formats(formats):
    zs_formats = []
    for name, f in formats.items():
        if f.colorspace == 'ZS':
            zs_formats.append(f.name)
    for f in zs_formats:
        formats.pop(f)
    return zs_formats

def write_format_enum(formats):
    remaining_formats = {}
    for f in formats:
        if f.name != 'PIPE_FORMAT_NONE':
            remaining_formats[f.name] = f

    vertex_formats = collect_vertex_formats(remaining_formats)
    yuv_formats = collect_yuv_formats(remaining_formats)
    compr_formats = collect_compr_formats(remaining_formats)
    zs_formats = collect_zs_formats(remaining_formats)

    print('/**', file=sys.stdout3)
    print(' * Formats for textures, surfaces and vertex data', file=sys.stdout3)
    print(' */', file=sys.stdout3)
    print("enum pipe_format {", file=sys.stdout3)
    print('   PIPE_FORMAT_NONE,', file=sys.stdout3)

    print(file=sys.stdout3)
    print('   /* Vertex formats must be first and must be <= 255 */', file=sys.stdout3)
    write_format_enum_section(vertex_formats, "VERTEX", max_formats=255)

    print(file=sys.stdout3)
    print('   /* YUV formats. */', file=sys.stdout3)
    write_format_enum_section(yuv_formats, "YUV")

    print(file=sys.stdout3)
    print('   /* Compressed formats. */', file=sys.stdout3)
    write_format_enum_section(compr_formats, "COMPR")

    print(file=sys.stdout3)
    print('   /* Depth/stencil formats. */', file=sys.stdout3)
    write_format_enum_section(zs_formats, "ZS")

    print(file=sys.stdout3)
    print('   /* Other formats. */', file=sys.stdout3)
    write_format_enum_section(remaining_formats)

    print('   PIPE_FORMAT_COUNT,', file=sys.stdout3)
    print('};', file=sys.stdout3)

def write_format_aliases(formats):
    for f in formats:
        if f.colorspace and f.alias != None:
            print("#define %s %s" % (f.alias, f.name), file=sys.stdout3)

    print(file=sys.stdout3)
    print("#if UTIL_ARCH_LITTLE_ENDIAN", file=sys.stdout3)
    for f in formats:
        if f.le_alias:
            print("#define %s %s" % (f.le_alias, f.name), file=sys.stdout3)
    print("#elif UTIL_ARCH_BIG_ENDIAN", file=sys.stdout3)
    for f in formats:
        if f.be_alias:
            print("#define %s %s" % (f.be_alias, f.name), file=sys.stdout3)
    print("#endif", file=sys.stdout3)

CHROMA_SUBSAMP = ['400', '420', '422', '444', '440']

def write_to_chroma_format(formats):
    print('enum pipe_video_chroma_format {', file=sys.stdout3)
    for subsamp in CHROMA_SUBSAMP:
        print('   PIPE_VIDEO_CHROMA_FORMAT_%s,' % subsamp, file=sys.stdout3)
    print('   PIPE_VIDEO_CHROMA_FORMAT_NONE,', file=sys.stdout3)
    print('};', file=sys.stdout3)
    print(file=sys.stdout3)
    print('static inline enum pipe_video_chroma_format', file=sys.stdout3)
    print('pipe_format_to_chroma_format(enum pipe_format format)', file=sys.stdout3)
    print('{', file=sys.stdout3)
    print('   switch(format) {', file=sys.stdout3)
    for subsamp in CHROMA_SUBSAMP:
        format_count = 0
        for f in formats:
            if f.colorspace == 'YUV':
                yuv_split_name = f.name.split('_')
                if yuv_split_name[-2] == subsamp:
                    print('   case %s:' % f.name, file=sys.stdout3)
                    format_count += 1
        if format_count > 0:
            print('      return PIPE_VIDEO_CHROMA_FORMAT_%s;' % subsamp, file=sys.stdout3)
    print('   default:', file=sys.stdout3)
    print('      return PIPE_VIDEO_CHROMA_FORMAT_NONE;', file=sys.stdout3)
    print('   }', file=sys.stdout3)
    print('}', file=sys.stdout3)

def chroma_horizontal_subsample_factor(subsample_name):
    assert(subsample_name in CHROMA_SUBSAMP)
    if subsample_name in ['420', '422']:
        return 2
    return 1

def chroma_vertical_subsample_factor(subsample_name):
    assert(subsample_name in CHROMA_SUBSAMP)
    if subsample_name in ['420', '440']:
        return 2
    return 1

def write_get_plane_width_height(formats):
    print('static inline unsigned', file=sys.stdout3)
    print('util_format_get_plane_width(enum pipe_format format, unsigned plane,', file=sys.stdout3)
    print('                            unsigned width)', file=sys.stdout3)
    print('{', file=sys.stdout3)
    print('   if (!plane)', file=sys.stdout3)
    print('      return width;', file=sys.stdout3)
    print(file=sys.stdout3)
    print('   switch (format) {', file=sys.stdout3)
    for factor in [2]:
        format_count = 0
        for f in formats:
            if f.layout in ['planar2', 'planar3']:
                split_name = f.name.split('_')
                subsamp = split_name[-2]
                if subsamp not in CHROMA_SUBSAMP:
                    continue
                if chroma_horizontal_subsample_factor(subsamp) != factor:
                    continue
                print('   case %s:' % f.name, file=sys.stdout3)
                format_count += 1
        if format_count > 0:
            print('      return DIV_ROUND_UP(width, %d);' % factor, file=sys.stdout3)
    print('   default:', file=sys.stdout3)
    print('      return width;', file=sys.stdout3)
    print('   }', file=sys.stdout3)
    print('}', file=sys.stdout3)
    print(file=sys.stdout3)
    print('static inline unsigned', file=sys.stdout3)
    print('util_format_get_plane_height(enum pipe_format format, unsigned plane,', file=sys.stdout3)
    print('                             unsigned height)', file=sys.stdout3)
    print('{', file=sys.stdout3)
    print('   if (!plane)', file=sys.stdout3)
    print('      return height;', file=sys.stdout3)
    print(file=sys.stdout3)
    print('   switch (format) {', file=sys.stdout3)
    for factor in [2]:
        format_count = 0
        for f in formats:
            if f.layout in ['planar2', 'planar3']:
                split_name = f.name.split('_')
                subsamp = split_name[-2]
                if subsamp not in CHROMA_SUBSAMP:
                    continue
                if chroma_vertical_subsample_factor(subsamp) != factor:
                    continue
                print('   case %s:' % f.name, file=sys.stdout3)
                format_count += 1
        if format_count > 0:
            print('      return DIV_ROUND_UP(height, %d);' % factor, file=sys.stdout3)
    print('   default:', file=sys.stdout3)
    print('      return height;', file=sys.stdout3)
    print('   }', file=sys.stdout3)
    print('}', file=sys.stdout3)

def write_get_plane_format(formats):
    to_rgb_plane_format = {
        'Y8': 'R8',
        'U8': 'R8',
        'V8': 'R8',
        'X6Y10': 'X6R10',
        'X6U10': 'X6R10',
        'X6V10': 'X6R10',
        'Y10X6': 'R16',
        'U10X6': 'R16',
        'V10X6': 'R16',
        'X6U10X6V10': 'X6R10X6G10',
        'X6V10X6U10': 'X6R10X6G10',
        'X4Y12': 'X4R12',
        'X4U12': 'X4R12',
        'X4V12': 'X4R12',
        'Y12X4': 'R16',
        'U12X4': 'R16',
        'V12X4': 'R16',
        'X4U12X4V12': 'X4R12X4G12',
        'X4V12X4U12': 'X4R12X4G12',
        'Y16': 'R16',
        'U16': 'R16',
        'V16': 'R16',
        'U16V16': 'R16G16',
        'U8V8': 'RG88',
        'V8U8': 'GR88',
        'R8': 'R8',
        'G8': 'R8',
        'B8': 'R8',
        'G8B8': 'RG88',
        'B8G8': 'GR88',
        'B8R8': 'GR88',
        'X6G10': 'X6R10',
        'X6B10X6R10': 'X6R10X6G10',
        'X4G12': 'X4R12',
        'X4B12X4R12': 'X4R12X4G12',
    }

    # On some YUV formats, we don't want RGB lowering
    no_rgb_lowering = ['Y8_UNORM']

    print('static inline enum pipe_format', file=sys.stdout3)
    print('util_format_get_plane_format(enum pipe_format format, unsigned plane)', file=sys.stdout3)
    print('{', file=sys.stdout3)
    print('   switch (format) {', file=sys.stdout3)
    unhandled_formats = []
    for f in formats:
        if f.layout not in ['planar2', 'planar3'] and f.colorspace != 'YUV':
            continue

        if f.short_name().upper() in no_rgb_lowering:
            unhandled_formats += [f.name]
            continue

        nplanes = int(f.layout[-1]) if f.layout.startswith('planar') else 1
        planes = f.short_name().upper().split('_')
        if planes[-1] == 'PACKED':
            planes.pop(-1)

        assert(planes[-1] == 'UNORM')
        planes.pop(-1)
        if planes[-1] in CHROMA_SUBSAMP:
            planes.pop(-1)
        rgb_formats = [None, None, None]
        no_match = False
        same_format_on_all_planes = True
        for i in range(nplanes):
            if planes[i] not in to_rgb_plane_format:
                no_match = True
                break
            rgb_formats[i] = to_rgb_plane_format[planes[i]]
            if i > 0 and rgb_formats[i] != rgb_formats[i - 1]:
                same_format_on_all_planes = False

        if no_match:
            unhandled_formats += [f.name]
            continue

        print('   case %s:' % f.name, file=sys.stdout3)
        if same_format_on_all_planes:
            print('      return PIPE_FORMAT_%s_UNORM;' % rgb_formats[0], file=sys.stdout3)
        else:
            if nplanes == 3:
                assert(rgb_formats[1] == rgb_formats[2])
            print('      return !plane ? PIPE_FORMAT_%s_UNORM : PIPE_FORMAT_%s_UNORM;' % (rgb_formats[0], rgb_formats[1]), file=sys.stdout3)

    print(file=sys.stdout3)
    if len(unhandled_formats) > 0:
        print('   /* No matching per-plane RGB format, we return the original format in that case. */', file=sys.stdout3)
    for f in unhandled_formats:
        print('   case %s:' % f, file=sys.stdout3)
    print('   default:', file=sys.stdout3)
    print('      return format;', file=sys.stdout3)
    print('   }', file=sys.stdout3)
    print('}', file=sys.stdout3)
    print(file=sys.stdout3)

def write_type_conv_helper(formats, in_type, out_type):
    print('static inline enum pipe_format', file=sys.stdout3)
    print('util_format_%s_to_%s(enum pipe_format format)' % (in_type, out_type), file=sys.stdout3)
    print('{', file=sys.stdout3)
    print('   switch (format) {', file=sys.stdout3)
    for out_fmt in formats:
        if out_fmt.colorspace != 'RGB':
            continue
        if out_type not in out_fmt.short_name():
            continue

        match = []
        if in_type != 'any':
            match += [out_fmt.short_name().replace(out_type, in_type)]
        else:
            for fmt_type in ['unorm', 'snorm', 'uint', 'sint', 'float']:
                if fmt_type != in_type:
                    match += [out_fmt.short_name().replace(out_type, fmt_type)]

        found_match = False
        for in_fmt in formats:
            if in_fmt.short_name() in match:
                print('   case %s:' % in_fmt.name, file=sys.stdout3)
                found_match = True

        if found_match:
            print('      return %s;' % out_fmt.name, file=sys.stdout3)
    print('   default:', file=sys.stdout3)
    print('      return format;', file=sys.stdout3)
    print('   }', file=sys.stdout3)
    print('}', file=sys.stdout3)

def write_type_conv_helpers(formats):
    out_types = ['unorm', 'snorm', 'uint', 'sint', 'float']
    in_types = out_types + ['any']
    for in_type in in_types:
        for out_type in out_types:
            if in_type != out_type:
                write_type_conv_helper(formats, in_type, out_type)
                print(file=sys.stdout3)

def write_format_inline_helpers(formats):
    write_to_chroma_format(formats)
    print(file=sys.stdout3)
    write_get_plane_format(formats)
    print(file=sys.stdout3)
    write_get_plane_width_height(formats)
    print(file=sys.stdout3)
    write_type_conv_helpers(formats)

def write_format_table(formats):
    write_format_table_header(sys.stdout)
    print('#include "util/format/u_format.h"')
    print('#include "u_format_bptc.h"')
    print('#include "u_format_fxt1.h"')
    print('#include "u_format_s3tc.h"')
    print('#include "u_format_rgtc.h"')
    print('#include "u_format_latc.h"')
    print('#include "u_format_etc.h"')
    print()

    write_format_table_header(sys.stdout2)

    print('#ifdef __cplusplus', file=sys.stdout2)
    print('extern "C" {', file=sys.stdout2)
    print('#endif', file=sys.stdout2)
    print('#include "util/format/u_format.h"', file=sys.stdout2)
    print(file=sys.stdout2)

    u_format_pack.generate(formats)

    print('#ifdef __cplusplus', file=sys.stdout2)
    print('} /* extern "C" */', file=sys.stdout2)
    print('#endif', file=sys.stdout2)


    write_format_table_header(sys.stdout3)

    print('#include "util/macros.h"', file=sys.stdout3)
    print('#include "util/u_endian.h"', file=sys.stdout3)
    print(file=sys.stdout3)

    print('#ifdef __cplusplus', file=sys.stdout3)
    print('extern "C" {', file=sys.stdout3)
    print('#endif', file=sys.stdout3)
    print(file=sys.stdout3)

    write_format_enum(formats)
    print(file=sys.stdout3)
    write_format_aliases(formats)
    print(file=sys.stdout3)
    write_format_inline_helpers(formats)
    print(file=sys.stdout3)

    print('#ifdef __cplusplus', file=sys.stdout3)
    print('} /* extern "C" */', file=sys.stdout3)
    print('#endif', file=sys.stdout3)

    def do_channel_array(channels, swizzles):
        print("   {")
        for i in range(4):
            channel = channels[i]
            if i < 3:
                sep = ","
            else:
                sep = ""
            if channel.size:
                print("      {%s, %s, %s, %u, %u}%s\t/* %s = %s */" % (type_map[channel.type], bool_map(channel.norm), bool_map(channel.pure), channel.size, channel.shift, sep, "xyzw"[i], channel.name))
            else:
                print("      {0, 0, 0, 0, 0}%s" % (sep,))
        print("   },")

    def do_swizzle_array(channels, swizzles):
        print("   {")
        for i in range(4):
            swizzle = swizzles[i]
            if i < 3:
                sep = ","
            else:
                sep = ""
            try:
                comment = colorspace_channels_map[format.colorspace][i]
            except (KeyError, IndexError):
                comment = 'ignored'
            print("      %s%s\t/* %s */" % (swizzle_map[swizzle], sep, comment))
        print("   },")

    def generate_table_getter(type):
        suffix = ""
        if type == "unpack_":
            suffix = "_generic"
        print("ATTRIBUTE_RETURNS_NONNULL const struct util_format_%sdescription *" % type)
        print("util_format_%sdescription%s(enum pipe_format format)" % (type, suffix))
        print("{")
        print("   assert(format < PIPE_FORMAT_COUNT);")
        print("   return &util_format_%sdescriptions[format];" % (type))
        print("}")
        print()

    def generate_function_getter(func):
        print("util_format_%s_func_ptr" % func)
        print("util_format_%s_func(enum pipe_format format)" % (func))
        print("{")
        print("   assert(format < PIPE_FORMAT_COUNT);")
        print("   return util_format_%s_table[format];" % (func))
        print("}")
        print()

    print('static const struct util_format_description')
    print('util_format_descriptions[PIPE_FORMAT_COUNT] = {')
    for format in formats:
        sn = format.short_name()

        print("   [%s] = {" % (format.name,))
        print("      .format = %s," % (format.name,))
        print("      .name = \"%s\"," % (format.name,))
        print("      .short_name = \"%s\"," % (sn,))
        print("      .block = {%u, %u, %u, %u},\t/* block */" % (format.block_width, format.block_height, format.block_depth, format.block_size()))
        print("      .layout = %s," % (layout_map(format.layout),))
        print("      .nr_channels = %u,\t/* nr_channels */" % (format.nr_channels(),))
        print("      .is_array = %s,\t/* is_array */" % (bool_map(format.is_array()),))
        print("      .is_bitmask = %s,\t/* is_bitmask */" % (bool_map(format.is_bitmask()),))
        print("      .is_mixed = %s,\t/* is_mixed */" % (bool_map(format.is_mixed()),))
        print("      .is_unorm = %s,\t/* is_unorm */" % (bool_map(format.is_unorm()),))
        print("      .is_snorm = %s,\t/* is_snorm */" % (bool_map(format.is_snorm()),))
        u_format_pack.print_channels(format, do_channel_array)
        u_format_pack.print_channels(format, do_swizzle_array)
        print("      .colorspace = %s," % (colorspace_map(format.colorspace),))
        if format.srgb_equivalent:
            print("      .srgb_equivalent = %s,\t/* srgb_equivalent */" % format.srgb_equivalent.name)
        elif format.linear_equivalent:
            print("      .linear_equivalent = %s,\t/* linear_equivalent */" % format.linear_equivalent.name)
        else:
            print("      .srgb_equivalent = PIPE_FORMAT_NONE,\t/* srgb_equivalent */")
        print("   },")
        print()
    print("};")
    print()
    generate_table_getter("")

    print('static const struct util_format_pack_description')
    print('util_format_pack_descriptions[PIPE_FORMAT_COUNT] = {')
    for format in formats:
        sn = format.short_name()

        if not has_access(format):
            print("   [%s] = { 0 }," % (format.name,))
            continue

        print("   [%s] = {" % (format.name,))
        if format.colorspace != ZS and not format.is_pure_color():
            print("      .pack_rgba_8unorm = &util_format_%s_pack_rgba_8unorm," % sn)
            print("      .pack_rgba_float = &util_format_%s_pack_rgba_float," % sn)

        if format.has_depth():
            print("      .pack_z_32unorm = &util_format_%s_pack_z_32unorm," % sn)
            print("      .pack_z_float = &util_format_%s_pack_z_float," % sn)

        if format.has_stencil():
            print("      .pack_s_8uint = &util_format_%s_pack_s_8uint," % sn)

        if format.is_pure_unsigned() or format.is_pure_signed():
            print("      .pack_rgba_uint = &util_format_%s_pack_unsigned," % sn)
            print("      .pack_rgba_sint = &util_format_%s_pack_signed," % sn)
        print("   },")
        print()
    print("};")
    print()
    generate_table_getter("pack_")
    print('static const struct util_format_unpack_description')
    print('util_format_unpack_descriptions[PIPE_FORMAT_COUNT] = {')
    for format in formats:
        sn = format.short_name()

        if not has_access(format):
            print("   [%s] = { 0 }," % (format.name,))
            continue

        print("   [%s] = {" % (format.name,))

        if format.colorspace != ZS and not format.is_pure_color():
            if format.layout == 's3tc' or format.layout == 'rgtc':
                print("      .fetch_rgba_8unorm = &util_format_%s_fetch_rgba_8unorm," % sn)
            if format.block_width > 1:
                print(
                    "      .unpack_rgba_8unorm_rect = &util_format_%s_unpack_rgba_8unorm," % sn)
                print(
                    "      .unpack_rgba_rect = &util_format_%s_unpack_rgba_float," % sn)
            else:
                print(
                    "      .unpack_rgba_8unorm = &util_format_%s_unpack_rgba_8unorm," % sn)
                print("      .unpack_rgba = &util_format_%s_unpack_rgba_float," % sn)

        if format.has_depth():
            print("      .unpack_z_32unorm = &util_format_%s_unpack_z_32unorm," % sn)
            print("      .unpack_z_float = &util_format_%s_unpack_z_float," % sn)

        if format.has_stencil():
            print("      .unpack_s_8uint = &util_format_%s_unpack_s_8uint," % sn)

        if format.is_pure_unsigned():
            print("      .unpack_rgba = &util_format_%s_unpack_unsigned," % sn)
        elif format.is_pure_signed():
            print("      .unpack_rgba = &util_format_%s_unpack_signed," % sn)
        print("   },")
    print("};")
    print()

    generate_table_getter("unpack_")

    print('static const util_format_fetch_rgba_func_ptr util_format_fetch_rgba_table[PIPE_FORMAT_COUNT] = {')
    for format in formats:
        sn = format.short_name()

        if format.colorspace != ZS and has_access(format):
            print("  [%s] = &util_format_%s_fetch_rgba," % (format.name, sn))
        else:
            print("  [%s] = NULL," % format.name)

    print("};")
    print()

    generate_function_getter("fetch_rgba")

def main():
    formats = {}

    sys.stdout2 = open(os.devnull, "w")
    sys.stdout3 = open(os.devnull, "w")

    for arg in sys.argv[1:]:
        if arg == '--header':
            sys.stdout2 = sys.stdout
            sys.stdout = open(os.devnull, "w")
            sys.stdout3 = sys.stdout
            continue
        elif arg == '--enums':
            sys.stdout3 = sys.stdout
            sys.stdout = open(os.devnull, "w")
            sys.stdout2 = sys.stdout
            continue

        to_add = parse(arg)
        duplicates = [x.name for x in to_add if x.name in formats]
        if len(duplicates):
            raise RuntimeError(f"Duplicate format entries {', '.join(duplicates)}")
        formats.update({ x.name: x for x in to_add })

    write_format_table(formats.values())

if __name__ == '__main__':
    main()
