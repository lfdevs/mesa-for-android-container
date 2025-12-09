#! /usr/bin/env python3

# script to parse nvidia CL headers and generate inlines to be used in pushbuffer encoding.
# probably needs python3.9

import argparse
import os.path
import sys
import re
import subprocess

from mako.template import Template

import util


METHOD_ARRAY_SIZES = {
    'BIND_GROUP_CONSTANT_BUFFER'                            : 16,
    'CALL_MME_DATA'                                         : 256,
    'CALL_MME_MACRO'                                        : 256,
    'LOAD_CONSTANT_BUFFER'                                  : 16,
    'LOAD_INLINE_QMD_DATA'                                  : 64,
    'SET_ANTI_ALIAS_SAMPLE_POSITIONS'                       : 4,
    'SET_BLEND'                                             : 8,
    'SET_BLEND_PER_TARGET_*'                                : 8,
    'SET_COLOR_TARGET_*'                                    : 8,
    'SET_COLOR_COMPRESSION'                                 : 8,
    'SET_COLOR_CLEAR_VALUE'                                 : 4,
    'SET_CT_WRITE'                                          : 8,
    # For compute, this is only 8:
    'SET_MME_SHADOW_SCRATCH'                                : 256,
    'SET_MULTI_VIEW_RENDER_TARGET_ARRAY_INDEX_OFFSET'       : 4,
    'SET_PIPELINE_*'                                        : 6,
    'SET_ROOT_TABLE_VISIBILITY'                             : 8,
    'SET_SCG_COMPUTE_SCHEDULING_PARAMETERS'                 : 16,
    'SET_SCG_GRAPHICS_SCHEDULING_PARAMETERS'                : 16,
    'SET_SCISSOR_*'                                         : 16,
    'SET_SHADER_PERFORMANCE_SNAPSHOT_COUNTER_VALUE*'        : 8,
    'SET_SHADER_PERFORMANCE_COUNTER_VALUE*'                 : 8,
    'SET_SHADER_PERFORMANCE_COUNTER_EVENT'                  : 8,
    'SET_SHADER_PERFORMANCE_COUNTER_CONTROL_A'              : 8,
    'SET_SHADER_PERFORMANCE_COUNTER_CONTROL_B'              : 8,
    'SET_SHADING_RATE_INDEX_SURFACE_*'                      : 1,
    'SET_SPARE_MULTI_VIEW_RENDER_TARGET_ARRAY_INDEX_OFFSET' : 4,
    'SET_STREAM_OUT_BUFFER_*'                               : 4,
    'SET_STREAM_OUT_CONTROL_*'                              : 4,
    'SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER'                  : 13,
    'SET_VARIABLE_PIXEL_RATE_SHADING_CONTROL*'              : 16,
    'SET_VARIABLE_PIXEL_RATE_SHADING_INDEX_TO_RATE*'        : 16,
    'SET_VIEWPORT_*'                                        : 16,
    'SET_VERTEX_ATTRIBUTE_*'                                : 16,
    'SET_VERTEX_STREAM_*'                                   : 16,
    'SET_WINDOW_CLIP_*'                                     : 8,
    'SET_CLIP_ID_EXTENT_*'                                  : 4,
}

METHOD_IS_FLOAT = [
    'SET_BLEND_CONST_*',
    'SET_DEPTH_BIAS',
    'SET_SLOPE_SCALE_DEPTH_BIAS',
    'SET_DEPTH_BIAS_CLAMP',
    'SET_DEPTH_BOUNDS_M*',
    'SET_LINE_WIDTH_FLOAT',
    'SET_ALIASED_LINE_WIDTH_FLOAT',
    'SET_VIEWPORT_SCALE_*',
    'SET_VIEWPORT_OFFSET_*',
    'SET_VIEWPORT_CLIP_MIN_Z',
    'SET_VIEWPORT_CLIP_MAX_Z',
    'SET_Z_CLEAR_VALUE',
    'SET_POINT_SIZE',
]

# generator chokes on these in later files
SKIP_FIELD = [
    'CURRENT_SCG_TYPE'
]

TEMPLATE_H = Template("""\
/* parsed class ${nvcl} */

#include "nvtypes.h"
#include "${clheader}"

#include <assert.h>
#include <stdio.h>
#include "util/u_math.h"

%for mthd in methods:
struct nv_${nvcl.lower()}_${mthd.name} {
  %for field in mthd.fields:
    uint32_t ${field.name.lower()};
  %endfor
};

static inline void
__${nvcl}_${mthd.name}(uint32_t *val_out, struct nv_${nvcl.lower()}_${mthd.name} st)
{
    uint32_t val = 0;
  %for field in mthd.fields:
    <% field_width = field.end - field.start + 1 %>
    %if field_width == 32:
    val |= st.${field.name.lower()};
    %else:
    assert(st.${field.name.lower()} < (1ULL << ${field_width}));
    val |= st.${field.name.lower()} << ${field.start};
    %endif
  %endfor
    *val_out = val;
}

#define V_${nvcl}_${mthd.name}(val, args...) { ${bs}
  %for field in mthd.fields:
    %for d in field.defs:
    UNUSED uint32_t ${field.name}_${d} = ${nvcl}_${mthd.name}_${field.name}_${d}; ${bs}
    %endfor
  %endfor
  %if len(mthd.fields) > 1:
    struct nv_${nvcl.lower()}_${mthd.name} __data = args; ${bs}
  %else:
<% field_name = mthd.fields[0].name.lower() %>\
    struct nv_${nvcl.lower()}_${mthd.name} __data = { .${field_name} = (args) }; ${bs}
  %endif
    __${nvcl}_${mthd.name}(&val, __data); ${bs}
}

%if mthd.is_array:
#define VA_${nvcl}_${mthd.name}(i) V_${nvcl}_${mthd.name}
%else:
#define VA_${nvcl}_${mthd.name} V_${nvcl}_${mthd.name}
%endif

%if mthd.is_array:
#define P_${nvcl}_${mthd.name}(push, idx, args...) do { ${bs}
%else:
#define P_${nvcl}_${mthd.name}(push, args...) do { ${bs}
%endif
  %for field in mthd.fields:
    %for d in field.defs:
    UNUSED uint32_t ${field.name}_${d} = ${nvcl}_${mthd.name}_${field.name}_${d}; ${bs}
    %endfor
  %endfor
    uint32_t nvk_p_ret; ${bs}
    V_${nvcl}_${mthd.name}(nvk_p_ret, args); ${bs}
    %if mthd.is_array:
    nv_push_val(push, ${nvcl}_${mthd.name}(idx), nvk_p_ret); ${bs}
    %else:
    nv_push_val(push, ${nvcl}_${mthd.name}, nvk_p_ret); ${bs}
    %endif
} while(0)

%endfor

const char *P_PARSE_${nvcl}_MTHD(uint16_t idx) ATTRIBUTE_COLD;
void P_DUMP_${nvcl}_MTHD_DATA(FILE *fp, uint16_t idx, uint32_t data,
                              const char *prefix) ATTRIBUTE_COLD;
""")

TEMPLATE_C = Template("""\
#include "${header}"

#include <stdio.h>

<%def name="cases(mthd)">
  %if mthd.is_array:
    %for i in range(mthd.array_size):
    case ${nvcl}_${mthd.name}(${i}):
    %endfor
  % else:
    case ${nvcl}_${mthd.name}:
  %endif
</%def>

<%def name="prev_cases()">
  %if any_method_removed:
    %for mthd in methods:
      %if prev_methods.get(mthd.name, None) == mthd:
        ${cases(mthd)}
      %endif
    %endfor
  %else:
    default:
  %endif
</%def>

%if prev_nvcl:
const char *P_PARSE_${prev_nvcl}_MTHD(uint16_t idx) ATTRIBUTE_COLD;
void P_DUMP_${prev_nvcl}_MTHD_DATA(FILE *fp, uint16_t idx, uint32_t data,
                                   const char *prefix) ATTRIBUTE_COLD;
%endif

const char*
P_PARSE_${nvcl}_MTHD(uint16_t idx)
{
    switch (idx) {

%if prev_nvcl:
  ${prev_cases()}
    return P_PARSE_${prev_nvcl}_MTHD(idx);
%endif

%for mthd in methods:
  %if mthd.is_array and mthd.array_size == 0:
    <% continue %>
  %endif
  %if prev_methods.get(mthd.name, None) == mthd:
    <% continue %>
  %endif
  %if mthd.is_array:
    %for i in range(mthd.array_size):
    case ${nvcl}_${mthd.name}(${i}):
        return "${nvcl}_${mthd.name}(${i})";
    %endfor
  % else:
    case ${nvcl}_${mthd.name}:
        return "${nvcl}_${mthd.name}";
  %endif
%endfor

%if not prev_nvcl or any_method_removed:
    default:
        return "unknown method";
%endif
    }
}

void
P_DUMP_${nvcl}_MTHD_DATA(FILE *fp, uint16_t idx, uint32_t data,
                         const char *prefix)
{
    UNUSED uint32_t parsed;
    switch (idx) {

%if prev_nvcl:
  ${prev_cases()}
    P_DUMP_${prev_nvcl}_MTHD_DATA(fp, idx, data, prefix);
    break;
%endif

%for mthd in methods:
  %if mthd.is_array and mthd.array_size == 0:
    <% continue %>
  %endif
  %if prev_methods.get(mthd.name, None) == mthd:
    <% continue %>
  %endif
  ${cases(mthd)}
  %for field in mthd.fields:
    <% field_width = field.end - field.start + 1 %>
    %if field_width == 32:
        parsed = data;
    %else:
        parsed = (data >> ${field.start}) & ((1u << ${field_width}) - 1);
    %endif
        fprintf(fp, "%s.${field.name} = ", prefix);
    %if len(field.defs):
        switch (parsed) {
      %for d in field.unique_defs:
        case ${nvcl}_${mthd.name}_${field.name}_${d}:
            fprintf(fp, "${d}${bs}n");
            break;
      %endfor
        default:
            fprintf(fp, "0x%x${bs}n", parsed);
            break;
        }
    %else:
      %if mthd.is_float:
        fprintf(fp, "%ff (0x%x)${bs}n", uif(parsed), parsed);
      %else:
        fprintf(fp, "(0x%x)${bs}n", parsed);
      %endif
    %endif
  %endfor
        break;
%endfor
%if not prev_nvcl or any_method_removed:
    default:
        fprintf(fp, "%s.VALUE = 0x%x${bs}n", prefix, data);
        break;
%endif
    }
}
""")

TEMPLATE_RS = Template("""\
// parsed class ${nvcl}

% if version is not None:
pub const ${version[0]}: u16 = ${version[1]};
% endif
""")

TEMPLATE_RS_MTHD = Template("""\
use crate::Mthd;
use crate::ArrayMthd;

%if prev_mod is not None:
use crate::classes::${prev_mod}::mthd as ${prev_mod};
%endif

// parsed class ${nvcl}

## Write out the methods in Rust
%for mthd in methods:

## If this method is the same as the one in the previous class header, just
## pub use everything instead of re-generating it.  This significantly
## reduces the number of unique Rust types and trait implementations.
%if prev_methods.get(mthd.name, None) == mthd:
    %for field in mthd.fields:
        %if field.is_rs_enum:
pub use ${prev_mod}::${field.rs_type(mthd)};
        %endif
    %endfor
pub use ${prev_mod}::${to_camel(mthd.name)};
    <% continue %>
%endif

## If there are a range of values for a field, we define an enum.
%for field in mthd.fields:
    %if field.is_rs_enum:
#[repr(u16)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum ${field.rs_type(mthd)} {
    %for def_name, def_value in field.unique_defs.items():
    ${to_camel(def_name)} = ${def_value.lower()},
    %endfor
}
    %endif
%endfor

## We also define a struct with the fields for the mthd.
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct ${to_camel(mthd.name)} {
  %for field in mthd.fields:
    pub ${field.rs_name}: ${field.rs_type(mthd)},
  %endfor
}

## Notice that the "to_bits" implementation is identical, so the first brace is
## not closed.
% if not mthd.is_array:
## This trait lays out how the conversion to u32 happens
impl Mthd for ${to_camel(mthd.name)} {
    const ADDR: u16 = ${mthd.addr_expr(None)};
    const CLASS: u16 = ${version[1].lower() if version is not None else nvcl.lower().replace("nv", "0x")};

%else:
impl ArrayMthd for ${to_camel(mthd.name)} {
    const CLASS: u16 = ${version[1].lower() if version is not None else nvcl.lower().replace("nv", "0x")};

    fn addr(i: usize) -> u16 {
        (${mthd.addr_expr('i')}).try_into().unwrap()
    }
%endif

    #[inline]
    fn to_bits(self) -> u32 {
        let mut val = 0;
        %for field in mthd.fields:
            <% field_width = field.end - field.start + 1 %>
            %if field.rs_type(mthd) == "u32":
        let field_u32 = self.${field.rs_name};
            %else:
        let field_u32 = self.${field.rs_name} as u32;
            %endif
            %if field_width == 32:
            <% assert field_width == 32 %>
            %else:
        assert!(field_u32 < (1 << ${field_width}));
            %endif
            %if field.start == 0:
        val |= field_u32;
            %else:
        val |= field_u32 << ${field.start};
            %endif
        %endfor

        val
    }
## Close the first brace.
}
%endfor
""")

## A mere convenience to convert snake_case to CamelCase. Numbers are prefixed
## with "_".
def to_camel(snake_str):
    result = ''.join(word.title() for word in snake_str.split('_'))
    return result if not result[0].isdigit() else '_' + result

def strip_parens(s):
    s = s.strip()
    while s.startswith('(') and s.endswith(')'):
        s = s[1:-1].strip()
    return s

def glob_match(glob, name):
    if glob.endswith('*'):
        return name.startswith(glob[:-1])
    else:
        assert '*' not in glob
        return name == glob

class Field(object):
    def __init__(self, name, start, end):
        self.name = name
        self.start = int(start)
        self.end = int(end)
        self.defs = {}
        self._values = set()
        self.unique_defs = {}

    def __eq__(self, other):
        if not isinstance(other, Field):
            return False

        return self.name == other.name and \
               self.start == other.start and \
               self.end == other.end and \
               self.defs == other.defs

    def add_def(self, name, value):
        assert name not in self.defs
        self.defs[name] = value

        if value not in self._values:
            self._values.add(value)
            self.unique_defs[name] = value

    @property
    def is_bool(self):
        if len(self.defs) != 2:
            return False

        for d in self.defs:
            if not d.lower() in ['true', 'false']:
                return False

        return True

    @property
    def is_rs_enum(self):
        return not self.is_bool and len(self.defs) > 0

    def rs_type(self, mthd):
        if self.is_bool:
            return "bool"
        elif self.name == 'V' and len(self.defs) > 0:
            return to_camel(mthd.name) + 'V'
        elif len(self.defs) > 0:
            assert(self.name != "")
            return to_camel(mthd.name) + to_camel(self.name)
        elif mthd.is_float:
            return "f32"
        else:
            return "u32"

    @property
    def rs_name(self):
        name = self.name.lower()

        # Fix up some Rust keywords
        if name == 'type':
            return 'type_'
        elif name == 'override':
            return 'override_'
        elif name[0].isdigit():
            return '_' + name
        else:
            return re.sub(r'_+', '_', name)

class Method(object):
    def __init__(self, name, addr, is_array=False):
        self.name = name
        self.addr = addr
        self.is_array = is_array
        self.fields = []

    def __eq__(self, other):
        if not isinstance(other, Method):
            return False

        return self.name == other.name and \
               self.addr == other.addr and \
               self.is_array == other.is_array and \
               self.fields == other.fields

    def addr_expr(self, idx_var):
        expr = self.addr
        if self.is_array:
            # We don't support multiple-indexing but the headers aren't
            # consistent in choosing i vs. j.
            assert idx_var is not None
            assert not ('i' in self.addr and 'j' in self.addr)
            expr = re.sub(r'[ij]', idx_var, expr)

            # Rust doesn't like extra parens
            expr = expr.replace('(' + idx_var + ')', idx_var)
        else:
            assert not 'i' in self.addr and not 'j' in self.addr

        # Rust also doesn't like extra parens
        expr = strip_parens(expr)

        return expr

    @property
    def array_size(self):
        for (glob, value) in METHOD_ARRAY_SIZES.items():
            if glob_match(glob, self.name):
                return value
        return 0

    @property
    def is_float(self):
        for glob in METHOD_IS_FLOAT:
            if glob_match(glob, self.name):
                assert len(self.fields) == 1
                return True
        return False

def parse_header(nvcl, f):
    # Simple state machine
    # state 0 looking for a new method define
    # state 1 looking for new fields in a method
    # state 2 looking for enums for a fields in a method
    # blank lines reset the state machine to 0

    version = None
    state = 0
    methods = {}
    curmthd = None

    for line in f:
        if line.strip() == "":
            state = 0
            if curmthd is not None and len(curmthd.fields) == 0:
                del methods[curmthd.name]
            curmthd = None
            continue

        if line.startswith("#define"):
            list = [strip_parens(e) for e in line.split()]
            if "_cl_" in list[1]:
                continue

            if (not list[1].startswith(nvcl)) or list[1].endswith('VIDEO_DECODER'):
                if len(list) > 2 and list[2].startswith("0x"):
                    assert version is None
                    version = (list[1], list[2])
                continue

            if list[1].endswith("TYPEDEF"):
                continue

            if state == 2:
                teststr = nvcl + "_" + curmthd.name + "_" + curfield.name + "_"
                if ":" in list[2]:
                    state = 1
                elif teststr in list[1]:
                    if not SKIP_FIELD[0] in list[1]:
                        curfield.add_def(list[1].removeprefix(teststr), list[2])
                else:
                    state = 1

            if state == 1:
                teststr = nvcl + "_" + curmthd.name + "_"
                if teststr in list[1]:
                    if ("0x" in list[2]):
                        state = 1
                    else:
                        field = list[1].removeprefix(teststr)
                        bitfield = list[2].split(":")
                        f = Field(field, bitfield[1], bitfield[0])
                        curmthd.fields.append(f)
                        curfield = f
                        state = 2
                else:
                    if len(curmthd.fields) == 0:
                        del methods[curmthd.name]
                    curmthd = None
                    state = 0

            if state == 0:
                if curmthd is not None and len(curmthd.fields) == 0:
                    del methods[curmthd.name]

                teststr = nvcl + "_"
                is_array = 0
                if (':' in list[2]):
                    continue
                name = list[1].removeprefix(teststr)
                if name.endswith("(i)"):
                    is_array = 1
                    name = name.removesuffix("(i)")
                if name.endswith("(j)"):
                    is_array = 1
                    name = name.removesuffix("(j)")

                curmthd = Method(name, list[2], is_array)
                methods[name] = curmthd
                state = 1

    return (version, methods)

def nvcl_for_filename(name):
    name = name.removeprefix("cl")
    name = name.removesuffix(".h")
    return "NV" + name.upper()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-h', required=False, help='Output C header.')
    parser.add_argument('--out-c', required=False, help='Output C file.')
    parser.add_argument('--out-rs', required=False, help='Output Rust file.')
    parser.add_argument('--out-rs-mthd', required=False,
                        help='Output Rust file for methods.')
    parser.add_argument('--in-h',
                        help='Input class header file.',
                        required=True)
    parser.add_argument('--prev-in-h',
                        help='Previous input class header file.',
                        required=False)
    args = parser.parse_args()

    clheader = os.path.basename(args.in_h)
    nvcl = nvcl_for_filename(clheader)

    with open(args.in_h, 'r', encoding='utf-8') as f:
        (version, methods) = parse_header(nvcl, f)

    prev_mod = None
    prev_methods = {}
    prev_nvcl = None
    if args.prev_in_h is not None:
        prev_clheader = os.path.basename(args.prev_in_h)
        prev_nvcl = nvcl_for_filename(prev_clheader)
        prev_mod = prev_clheader.removesuffix(".h")
        with open(args.prev_in_h, 'r', encoding='utf-8') as f:
            (prev_version, prev_methods) = parse_header(prev_nvcl, f)

    any_method_removed = any(x not in methods for x in prev_methods)

    environment = {
        'clheader': clheader,
        'nvcl': nvcl,
        'version': version,
        'methods': list(methods.values()),
        'prev_mod': prev_mod,
        'prev_methods': prev_methods,
        'prev_nvcl': prev_nvcl,
        'any_method_removed': any_method_removed,
        'to_camel': to_camel,
        'bs': '\\'
    }

    if args.out_h is not None:
        environment['header'] = os.path.basename(args.out_h)
        util.write_template(args.out_h, TEMPLATE_H, environment)
    if args.out_c is not None:
        util.write_template(args.out_c, TEMPLATE_C, environment)
    if args.out_rs is not None:
        util.write_template_rs(args.out_rs, TEMPLATE_RS, environment)
    if args.out_rs_mthd is not None:
        util.write_template_rs(args.out_rs_mthd, TEMPLATE_RS_MTHD, environment)


if __name__ == '__main__':
    main()
