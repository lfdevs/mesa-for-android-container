# Copyright 2025 Valve Corporation
# SPDX-License-Identifier: MIT

from mako.template import Template
import xml.etree.ElementTree as ET
import sys
import textwrap

schema_filename, xml_filename = sys.argv[1:]

try:
    from lxml import etree
    import rnc2rng

    rng = rnc2rng.dumps(rnc2rng.load(schema_filename))
    schema = etree.RelaxNG(etree.fromstring(rng.encode()))
    valid = schema.validate(etree.parse(xml_filename))
    if not valid:
        print(schema.error_log, file=sys.stderr)
        sys.exit(1)
except ImportError:
    # meson/ninja only displays stderr if the script fails, so this warning is
    # only visible when someone changes the XML in a way that causes the below
    # code to blow up. Ideally we'd add build dependencies but that might not be
    # desirable for exotic platforms... This seems reasonable as a compromise.
    print("", file=sys.stderr)
    print("****", file=sys.stderr)
    print("lxml or rnc2rng missing, skipping validation", file=sys.stderr)
    print("If this script fails, install for diagnostics", file=sys.stderr)
    print("****", file=sys.stderr)
    print("", file=sys.stderr)

# XXX: cribbed from genxml
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
        '?': '',
    }

    for i, j in substitutions.items():
        name = name.replace(i, j)

    return name

def safe_name(name):
    name = to_alphanum(name)
    if not name[0].isalpha():
        name = '_' + name

    return name

TYPE_MAP = {
    'i8': ('int8_t', 'i64', '" PRId8 "'),
    'u8': ('uint8_t', 'u64', '" PRIu8 "'),
    'i16': ('int16_t', 'i64', '" PRId16 "'),
    'u16': ('uint16_t', 'u64', '" PRIu16 "'),
    'i32': ('int32_t', 'i64', '" PRId32 "'),
    'u32': ('uint32_t', 'u64', '" PRIu32 "'),
    'i64': ('int64_t', 'i64', '" PRId64 "'),
    'u64': ('uint64_t', 'u64', '" PRIu64 "'),
    'float': ('float', 'f64', 'f'),
    'bool': ('bool', 'bool', 'u')
}

class Stat:
    def __init__(self, el):
        type_ = el.attrib.get('type', 'u32')

        self.name = el.attrib['name']
        self.display = el.attrib.get('display', self.name)
        self.hidden = el.attrib.get('hidden', False)
        self.description = textwrap.dedent(el.text or '').replace('\n', ' ').strip()
        self.count = int(el.attrib.get('count', 1))
        self.c_name = safe_name(self.display).lower()
        self.c_type, self.vk_type, format_specifier = TYPE_MAP[type_]
        self.format_strings = [f'%{format_specifier} {self.display.lower()}']
        self.format_args = [f'stats->{self.c_name}']

        if self.count > 1:
            self.format_strings = [self.format_strings[0].replace('#', str(i)) for i in range(self.count)]
            self.format_args = [f'{self.format_args[0]}[{i}]' for i in range(self.count)]

class ISA:
    def __init__(self, el):
        self.name = el.attrib['name']
        self.stats = [Stat(stat) for stat in el]
        self.c_name = safe_name(self.name).lower()
        self.c_struct_name = f"struct {self.c_name}_stats"

        # Derive a the format string to print statistics in GL (report.py)
        # format. report.py has a weird special case for spills/fills, which we
        # need to fix up here.
        external_stats = [stat for stat in self.stats if not stat.hidden]
        fmt = ', '.join([x for stat in external_stats for x in stat.format_strings])
        self.format_string = fmt.replace('%" PRIu32 " spills, %" PRIu32 " fills', '%" PRIu32 ":%" PRIu32 " spills:fills')
        self.format_args = ', '.join([x for stat in external_stats for x in stat.format_args])

class Family:
    def __init__(self, el):
        self.name = el.attrib['name']
        self.isas = [ISA(isa) for isa in el]
        self.c_name = safe_name(self.name).lower()
        self.c_enum_name = f'enum {self.c_name}_stat_isa'
        self.c_struct_name = f'struct {self.c_name}_stats'

    def isa_tag(self, isa):
        return f'{self.c_name.upper()}_STAT_{isa.name.upper()}'

def parse_file(root):
    isas = []
    families = []

    for el in root:
        if el.tag == 'isa':
            isas.append(ISA(el))
        elif el.tag == 'family':
            family = Family(el)
            families.append(family)
            isas += family.isas

    return (families, isas)

tree = ET.parse(xml_filename)
root = tree.getroot()
families, isas = parse_file(root)

template = Template("""\
#ifndef __SHADER_STATS_H
#define __SHADER_STATS_H
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "util/u_debug.h"

% for isa in isas:
${isa.c_struct_name} {
% for stat in isa.stats:
% if stat.count > 1:
   ${stat.c_type} ${stat.c_name}[${stat.count}];
% else:
   ${stat.c_type} ${stat.c_name};
% endif
% endfor
};

static inline int
${isa.c_name}_stats_fprintf(FILE *fp, const char *prefix, const ${isa.c_struct_name} *stats)
{
   return fprintf(fp, "%s shader: ${isa.format_string}\\n", prefix, ${isa.format_args});
}

static inline void
${isa.c_name}_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const ${isa.c_struct_name} *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: ${isa.format_string}", prefix, ${isa.format_args});
}

#define vk_add_${isa.c_name}_stats(out, stats) do { ${'\\\\'}
% for stat in isa.stats:
% if not stat.hidden:
% for i in range(stat.count):
% if stat.count > 1:
   vk_add_exec_statistic_${stat.vk_type}(out, "${stat.name.replace('#', str(i))}", "${stat.description.replace('#', str(i))}", (stats)->${stat.c_name}[${i}]); ${'\\\\'}
% else:
   vk_add_exec_statistic_${stat.vk_type}(out, "${stat.name}", "${stat.description}", (stats)->${stat.c_name}); ${'\\\\'}
% endif
% endfor
% endif
% endfor
} while(0)

static inline void
${isa.c_name}_stats_serialize(uint8_t *dest, const ${isa.c_struct_name} *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
% for stat in isa.stats:
% for i in range(stat.count):
% if stat.count > 1:
    memcpy(dest + offsetof(${isa.c_struct_name}, ${stat.c_name}) + ${i} * sizeof(${stat.c_type}), &stats->${stat.c_name}[${i}], sizeof(${stat.c_type}));
% else:
    memcpy(dest + offsetof(${isa.c_struct_name}, ${stat.c_name}), &stats->${stat.c_name}, sizeof(${stat.c_type}));
% endif
% endfor
% endfor
}

%endfor

% for family in families:
${family.c_enum_name} {
% for isa in family.isas:
   ${family.isa_tag(isa)},
% endfor
};

${family.c_struct_name} {
   ${family.c_enum_name} isa;
   union {
% for isa in family.isas:
      ${isa.c_struct_name} ${isa.name.lower()};
% endfor
   };
};

#define vk_add_${family.c_name}_stats(out, stats) do { ${'\\\\'}
% for isa in family.isas:
    if ((stats)->isa == ${family.isa_tag(isa)}) ${'\\\\'}
       vk_add_${isa.c_name}_stats(out, &(stats)->${isa.name.lower()}); ${'\\\\'}
% endfor
} while(0)

static inline void
${family.c_name}_stats_fprintf(FILE *fp, const char *prefix, const ${family.c_struct_name} *stats)
{
% for isa in family.isas:
    if (stats->isa == ${family.isa_tag(isa)})
       ${isa.c_name}_stats_fprintf(fp, prefix, &stats->${isa.name.lower()});
% endfor
}

static inline void
${family.c_name}_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const ${family.c_struct_name} *stats)
{
% for isa in family.isas:
    if (stats->isa == ${family.isa_tag(isa)})
       ${isa.c_name}_stats_util_debug(debug, prefix, &stats->${isa.name.lower()});
% endfor
}

static inline void
${family.c_name}_stats_serialize(uint8_t *dest, const ${family.c_struct_name} *stats)
{
% for isa in family.isas:
    if (stats->isa == ${family.isa_tag(isa)})
       ${isa.c_name}_stats_serialize(dest, &stats->${isa.name.lower()});
% endfor
}

% endfor

#endif
""")

print(template.render(isas=isas, families=families))
