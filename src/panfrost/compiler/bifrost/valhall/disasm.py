#encoding=utf-8

# Copyright (C) 2021 Collabora, Ltd.
# Copyright (C) 2026 Arm Ltd.
# SPDX-License-Identifier: MIT

import argparse
import sys
from valhall import valhall_parse_isa
from mako.template import Template
from mako import exceptions

parser = argparse.ArgumentParser()
parser.add_argument('--xml', required=True, help='Input ISA XML file')

args = parser.parse_args()

(instructions, immediates, enums, typesize, safe_name) = valhall_parse_isa(args.xml)

template = """
#include "disassemble.h"

#define BIT(b)         (1ull << (b))
#define MASK(count)    ((1ull << (count)) - 1)
#define SEXT(b, count) ((b ^ BIT(count - 1)) - BIT(count - 1))
#define UNUSED         __attribute__((unused))

#define VA_SRC_UNIFORM_TYPE 0x2
#define VA_SRC_IMM_TYPE     0x3

% for name, en in ENUMS.items():
UNUSED static const char *valhall_${name}[] = {
% for v in en.values:
   "${"" if v.default else "." + v.value}",
% endfor
};

% endfor
static const uint32_t va_immediates[32] = {
% for imm in IMMEDIATES:
   ${hex(imm)},
% endfor
};

static inline void
va_print_src(FILE *fp, unsigned type, unsigned value, unsigned size, unsigned fau_page)
{
   if (type == VA_SRC_IMM_TYPE) {
      if (value >= 32) {
         if (fau_page == 0)
            fputs(valhall_fau_special_page_0[(value - 0x20) >> 1] + 1, fp);
         else if (fau_page == 1)
            fputs(valhall_fau_special_page_1[(value - 0x20) >> 1] + 1, fp);
         else if (fau_page == 3)
            fputs(valhall_fau_special_page_3[(value - 0x20) >> 1] + 1, fp);
         else
            fprintf(fp, "reserved_page2");

         fprintf(fp, ".w%u", value & 1);
      } else {
         fprintf(fp, "0x%X", va_immediates[value]);
      }
   } else if (type == VA_SRC_UNIFORM_TYPE) {
      fprintf(fp, "u%u", value >> 1 | (fau_page << 5));
      if (size <= 32)
         fprintf(fp, ".w%u", value & 1);
   } else {
      bool discard = (type & 1);
      char *dmark = discard ? "^" : "";
      if (size > 32)
         fprintf(fp, "[r%u%s:r%u%s]", value, dmark, value + 1, dmark);
      else
         fprintf(fp, "r%u%s", value, dmark);
   }
}

static inline void
va_print_float_src(FILE *fp, unsigned type, unsigned value, unsigned size, unsigned fau_page, bool neg, bool abs)
{
   if (type == VA_SRC_IMM_TYPE) {
      assert(value < 32 && "overflow in LUT");
      fprintf(fp, "0x%X", va_immediates[value]);
   } else {
      va_print_src(fp, type, value, size, fau_page);
   }

   if (neg)
      fprintf(fp, ".neg");

   if (abs)
      fprintf(fp, ".abs");
}

static inline void
va_print_dest(FILE *fp, unsigned mask, unsigned value, unsigned size)
{
   if (size > 32)
      fprintf(fp, "[r%u:r%u]", value, value + 1);
   else
      fprintf(fp, "r%u", value);

   if (mask != 0x3)
      fprintf(fp, ".h%u", (mask == 1) ? 0 : 1);
}

<%def name="print_instr(op)">
<% no_comma = True %>
      fputs("${op.name}", fp);
% for mod in op.modifiers:
% if mod.name not in ["staging_register_count", "staging_register_write_count"]:
% if mod.is_enum:
      fputs(valhall_${safe_name(mod.enum)}[(instr >> ${mod.start}) & ${hex((1 << mod.size) - 1)}], fp);
% else:
      if (instr & BIT(${mod.start})) fputs(".${mod.name}", fp);
% endif
% endif
% endfor
      fprintf(fp, "%s ", valhall_flow[(instr >> ${op.offset['flow']}) & ${hex(op.mask['flow'])}]);
% for i, dest in enumerate(op.dests):
<% no_comma = False %>
      va_print_dest(fp, (instr >> ${dest.offset['mode']}) & ${hex(dest.mask['mode'])}, (instr >> ${dest.offset['value']}) & ${hex(dest.mask['value'])}, ${dest.size});
% endfor
% for index, sr in enumerate(op.staging):
% if not no_comma:
      fputs(", ", fp);
% endif
<%
   no_comma = False

   if sr.count != 0:
      sr_count = sr.count;
   else:
      for mod in op.modifiers:
         if mod.name == "staging_register_write_count" and sr.write:
            sr_count = f"(((instr >> {mod.start}) & {hex((1 << mod.size) - 1)}) + 1)";
         elif mod.name == "staging_register_count":
            sr_count = f"((instr >> {mod.start}) & {hex((1 << mod.size) - 1)})";
%>
//    assert(((instr >> ${sr.start}) & 0xC0) == ${sr.encoded_flags});
      fprintf(fp, "@");
      for (unsigned i = 0; i < ${sr_count}; ++i) {
         fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                 (uint32_t) (((instr >> ${sr.offset['value']}) & ${hex(sr.mask['value'])}) + i));
      }
% endfor
% for i, src in enumerate(op.srcs):
% if not no_comma:
      fputs(", ", fp);
% endif
<% no_comma = False %>
% if src.absneg:
      va_print_float_src(fp, (instr >> ${src.offset['mode']}) & ${hex(src.mask['mode'])}, (instr >> ${src.offset['value']}) & ${hex(src.mask['value'])},
                         ${src.size}, (instr >> ${op.offset['fau_page']}) & ${hex(op.mask['fau_page'])},
                         instr & BIT(${src.offset['neg']}),
                         instr & BIT(${src.offset['abs']}));
% elif src.is_float:
      va_print_float_src(fp, (instr >> ${src.offset['mode']}) & ${src.mask['mode']}, (instr >> ${src.offset['value']}) & ${hex(src.mask['value'])},
                         ${src.size}, (instr >> ${op.offset['fau_page']}) & ${hex(op.mask['fau_page'])}, false, false);
% else:
      va_print_src(fp, (instr >> ${src.offset['mode']}) & ${src.mask['mode']}, (instr >> ${src.offset['value']}) & ${hex(src.mask['value'])},
                   ${src.size}, (instr >> ${op.offset['fau_page']}) & ${hex(op.mask['fau_page'])});
% endif
% if src.swizzle:
% if src.size == 32:
      fputs(valhall_widen[(instr >> ${src.offset['swizzle']}) & ${hex(src.mask['swizzle'])}], fp);
% else:
      fputs(valhall_swizzles_16_bit[(instr >> ${src.offset['swizzle']}) & ${hex(src.mask['swizzle'])}], fp);
% endif
% endif
% if src.lanes:
      fputs(valhall_lanes_8_bit[(instr >> ${src.offset['widen']}) & ${hex(src.mask['widen'])}], fp);
% elif src.halfswizzle:
      fputs(valhall_half_swizzles_8_bit[(instr >> ${src.offset['widen']}) & ${hex(src.mask['widen'])}], fp);
% elif src.widen:
      fputs(valhall_swizzles_${src.size}_bit[(instr >> ${src.offset['widen']}) & ${hex(src.mask['widen'])}], fp);
% elif src.combine:
      fputs(valhall_combine[(instr >> ${src.offset['combine']}) & ${hex(src.mask['combine'])}], fp);
% endif
% if src.lane:
      fputs(valhall_lane_${src.size}_bit[(instr >> ${src.offset['lane']}) & ${hex(src.mask['lane'])}], fp);
% endif
% if 'not' in src.offset:
      if (instr & BIT(${src.offset['not']})) fputs(".not", fp);
% endif
% endfor
% for imm in op.immediates:
<%
   prefix = "#" if imm.name == "constant" else imm.name + ":"
   fmt = "%d" if imm.signed else "0x%X"
%>
      fprintf(fp, ", ${prefix}${fmt}", (uint32_t) ${"SEXT(" if imm.signed else ""} ((instr >> ${imm.start}) & MASK(${imm.size})) ${f", {imm.size})" if imm.signed else ""});
% endfor
</%def>

<%def name="recurse_subcodes(op_bucket)">
%if op_bucket.instr:
${print_instr(op_bucket.instr)}
%else:
   opcode = (instr >> ${op_bucket.start}) & ${hex(op_bucket.mask)};
   switch (opcode) {
%for op in op_bucket.children:
   case ${hex(op)}:
   {
${recurse_subcodes(op_bucket.children[op])}
      break;
   }
%endfor
   }
%endif
</%def>


void
va_disasm_instr(FILE *fp, uint64_t instr)
{
   unsigned opcode;

${recurse_subcodes(OPCODES)}
}

static bool is_branch(uint64_t instr)
{
<% (exact, mask) = OPCODES.get_exact_mask("BRANCHZ") %>
   if ((instr & ${hex(mask)}) == ${hex(exact)})
      return true;
<% (exact, mask) = OPCODES.get_exact_mask("BRANCHZI") %>
   if ((instr & ${hex(mask)}) == ${hex(exact)})
      return true;
   return false;
}

void
disassemble_valhall(FILE *fp, const void *code, size_t size, bool verbose)
{
   assert((size & 7) == 0);

   const uint64_t *words = (const uint64_t *)code;

   /* Segment into 8-byte instructions */
   for (unsigned i = 0; i < (size / 8); ++i) {
      uint64_t instr = words[i];

      if (instr == 0) {
         fprintf(fp, "\\n");
         return;
      }

      if (verbose) {
         /* Print byte pattern */
         for (unsigned j = 0; j < 8; ++j)
            fprintf(fp, "%02x ", (uint8_t)(instr >> (j * 8)));

         fprintf(fp, "   ");
      } else {
         /* Print whitespace */
         fprintf(fp, "   ");
      }

      va_disasm_instr(fp, instr);
      fprintf(fp, "\\n");

      /* Separate blocks visually by inserting whitespace after branches */
      if (is_branch(instr))
         fprintf(fp, "\\n");
   }

   fprintf(fp, "\\n");
}
"""

class OpBucket:
   def __init__(self):
      self.start = None
      self.mask = None
      self.instr = None
      self.children = {}

   def insert(self, subcodes, ins):
      if len(subcodes) == 0:
         self.instr = ins
      else:
         sc = subcodes[0]
         assert(self.start is None or self.start == sc.start)
         assert(self.mask is None or self.mask == sc.mask)
         self.start = sc.start
         self.mask = sc.mask
         if sc.value not in self.children:
            self.children[sc.value] = OpBucket()
         self.children[sc.value].insert(subcodes[1:], ins)

   def get_exact_mask(self, op_name, exact = 0, mask = 0):
      if self.instr:
         if self.instr.name == op_name:
            return (exact, mask)
         else:
            return ()
      else:
         for op in self.children:
            exact_mask = self.children[op].get_exact_mask(op_name,
                                               exact | (op << self.start),
                                               mask | (self.mask << self.start))
            if exact_mask:
               return exact_mask
         return ()

# Build opcode hierarchy:
OPCODES = OpBucket()
for ins in instructions:
   OPCODES.insert(ins.opcode, ins)

try:
   print(Template(template).render(OPCODES = OPCODES, IMMEDIATES = immediates, ENUMS = enums, typesize = typesize, safe_name = safe_name))
except:
   print(exceptions.text_error_template().render())
