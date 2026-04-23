# Copyright 2026 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import sys

from mako import exceptions
from mako.template import Template

from jay_opcodes import OPCODES, ENUMS

TEMPLATE = """/* Do not include directly */
PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)

% for enum, (prefix, values) in enums.items():
% if enum.startswith('jay'):
enum PACKED ${enum} {
% for v in values:
   ${prefix}_${v.upper()},
% endfor
};
% endif
% endfor

% for name, op in opcodes:
typedef struct jay_${name}_info {
% for T, prop in op.extra_struct:
    ${T} ${prop};
% endfor
} jay_${name}_info;

% for prefix, _suffix in [('const ', '_const'), ('', '')]:
static inline ${prefix} struct jay_${name}_info *
jay_get_${name}_info${_suffix}(${prefix}jay_inst *I)
{
   assert(I->op == JAY_OPCODE_${name.upper()});
   return (${prefix}struct jay_${name}_info *) &I->src[I->num_srcs];
}

% endfor
% for T, prop in op.extra_struct:
% if not prop.startswith('pad'):
static inline ${T}
jay_${name}_${prop}(const jay_inst *I)
{
   return jay_get_${name}_info_const(I)->${prop};
}

static inline void
jay_set_${name}_${prop}(jay_inst *I, ${T} value)
{
   jay_get_${name}_info(I)->${prop} = value;
}

% endif
% endfor
% endfor

static inline unsigned
jay_inst_info_size(jay_inst *I)
{
   switch (I->op) {
% for name, op in opcodes:
   case JAY_OPCODE_${name.upper()}: return sizeof(struct jay_${name}_info);
% endfor
   default: return 0;
   }
}

#ifndef __cplusplus
static inline const char *
jay_print_inst_info(FILE *fp, const jay_inst *I, const char *sep)
{
   switch (I->op) {
% for name, op in opcodes:
   case JAY_OPCODE_${name.upper()}: {
% for T, prop in op.extra_struct:
% if not (prop.startswith('pad') or name == 'bfn' or T == 'enum jay_type'):
<%
   value = f"jay_{name}_{prop}(I)"
   spec = '0x%"PRIx64"' if T == 'uint64_t' else "%u"
%>
% if T.startswith('enum') and T[5:] in enums:
<%
    bare = T[5:]
    prefix, values = enums[bare]
%>
      const char *${bare}_str[] = {
% for v in values:
         [${prefix}_${v.upper()}] = "${v}",
% endfor
      };
      assert(${value} < ARRAY_SIZE(${bare}_str));
<%
      spec = "%s"
      value = f'{T[5:]}_str[{value}]'
%>
% endif
% if T == 'enum jay_rounding_mode':
      if (strcmp(${value}, "round")) {
         fprintf(fp, "%s%s", sep, ${value});
         sep = ", ";
      }
% elif T == 'bool':
      if (${value}) {
         fprintf(fp, "%s${prop}", sep);
         sep = ", ";
      }
% elif T.startswith('enum') or len(op.extra_struct) == 1:
      fprintf(fp, "%s${spec}", sep, ${value});
      sep = ", ";
% else:
      if (${value}) {
         fprintf(fp, "%s${prop}=${spec}", sep, ${value});
         sep = ", ";
      }
% endif
% endif
% endfor
      break;
   }
% endfor
   default: break;
   }

   return sep;
}
#endif

PRAGMA_DIAGNOSTIC_POP
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('output', action='store')
    args = parser.parse_args()

    try:
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(Template(TEMPLATE).render(
                opcodes=[(k, v) for k, v in OPCODES.items() if v.extra_struct],
                enums=ENUMS))
    except Exception:
        print(exceptions.text_error_template().render())
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
