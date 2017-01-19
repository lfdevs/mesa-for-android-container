/*
 * Copyright © 2016 Intel Corporation
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "util/macros.h"

#include "decoder.h"
#include "intel_aub.h"
#include "gen_disasm.h"

/* Below is the only command missing from intel_aub.h in libdrm
 * So, reuse intel_aub.h from libdrm and #define the
 * AUB_MI_BATCH_BUFFER_END as below
 */
#define AUB_MI_BATCH_BUFFER_END (0x0500 << 16)

#define CSI "\e["
#define BLUE_HEADER  CSI "0;44m"
#define GREEN_HEADER CSI "1;42m"
#define NORMAL       CSI "0m"

/* options */

static bool option_full_decode = true;
static bool option_print_offsets = true;
static enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } option_color;

/* state */

uint16_t pci_id = 0;
char *input_file = NULL, *xml_path = NULL;
struct gen_spec *spec;
struct gen_disasm *disasm;

uint64_t gtt_size, gtt_end;
void *gtt;
uint64_t general_state_base;
uint64_t surface_state_base;
uint64_t dynamic_state_base;
uint64_t instruction_base;
uint64_t instruction_bound;

static inline uint32_t
field(uint32_t value, int start, int end)
{
   uint32_t mask;

   mask = ~0U >> (31 - end + start);

   return (value >> start) & mask;
}

struct brw_instruction;

static inline int
valid_offset(uint32_t offset)
{
   return offset < gtt_end;
}

static void
print_dword_val(struct gen_field_iterator *iter, uint64_t offset,
                int *dword_num)
{
   struct gen_field *f;

   f = iter->group->fields[iter->i - 1];
   const int dword = f->start / 32;

   if (*dword_num != dword) {
      printf("0x%08"PRIx64":  0x%08x : Dword %d\n",
             offset + 4 * dword,  iter->p[dword], dword);
      *dword_num = dword;
   }
}

static char *
print_iterator_values(struct gen_field_iterator *iter, int *idx)
{
    char *token = NULL;
    if (strstr(iter->value, "struct") == NULL) {
       if (strlen(iter->description) > 0) {
          printf("    %s: %s (%s)\n",
                 iter->name, iter->value, iter->description);
       } else {
          printf("    %s: %s\n", iter->name, iter->value);
       }
    } else {
        token = strtok(iter->value, " ");
        if (token != NULL) {
            token = strtok(NULL, " ");
            *idx = atoi(strtok(NULL, ">"));
        } else {
            token = NULL;
        }
        printf("    %s:<struct %s>\n", iter->name, token);
    }
    return token;
}

static void
decode_structure(struct gen_spec *spec, struct gen_group *strct,
                 const uint32_t *p)
{
   struct gen_field_iterator iter;
   char *token = NULL;
   int idx = 0, dword_num = 0;
   uint64_t offset = 0;

   if (option_print_offsets)
      offset = (void *) p - gtt;
   else
      offset = 0;

   gen_field_iterator_init(&iter, strct, p,
                           option_color == COLOR_ALWAYS);
   while (gen_field_iterator_next(&iter)) {
      idx = 0;
      print_dword_val(&iter, offset, &dword_num);
      token = print_iterator_values(&iter, &idx);
      if (token != NULL) {
         struct gen_group *struct_val = gen_spec_find_struct(spec, token);
         decode_structure(spec, struct_val, &p[idx]);
         token = NULL;
      }
   }
}

static void
handle_struct_decode(struct gen_spec *spec, char *struct_name, uint32_t *p)
{
   if (struct_name == NULL)
      return;
   struct gen_group *struct_val = gen_spec_find_struct(spec, struct_name);
   decode_structure(spec, struct_val, p);
}

static void
dump_binding_table(struct gen_spec *spec, uint32_t offset)
{
   uint32_t *pointers, i;
   uint64_t start;
   struct gen_group *surface_state;

   surface_state = gen_spec_find_struct(spec, "RENDER_SURFACE_STATE");
   if (surface_state == NULL) {
      printf("did not find RENDER_SURFACE_STATE info\n");
      return;
   }

   start = surface_state_base + offset;
   pointers = gtt + start;
   for (i = 0; i < 16; i++) {
      if (pointers[i] == 0)
         continue;
      start = pointers[i] + surface_state_base;
      if (!valid_offset(start)) {
         printf("pointer %u: %08x <not valid>\n",
                i, pointers[i]);
         continue;
      } else {
         printf("pointer %u: %08x\n", i, pointers[i]);
      }

      decode_structure(spec, surface_state, gtt + start);
   }
}

static void
handle_3dstate_index_buffer(struct gen_spec *spec, uint32_t *p)
{
   void *start;
   uint32_t length, i, type, size;

   start = gtt + p[2];
   type = (p[1] >> 8) & 3;
   size = 1 << type;
   length = p[4] / size;
   if (length > 10)
      length = 10;

   printf("\t");

   for (i = 0; i < length; i++) {
      switch (type) {
      case 0:
         printf("%3d ", ((uint8_t *)start)[i]);
         break;
      case 1:
         printf("%3d ", ((uint16_t *)start)[i]);
         break;
      case 2:
         printf("%3d ", ((uint32_t *)start)[i]);
         break;
      }
   }
   if (length < p[4] / size)
      printf("...\n");
   else
      printf("\n");
}

static inline uint64_t
get_address(struct gen_spec *spec, uint32_t *p)
{
   /* Addresses are always guaranteed to be page-aligned and sometimes
    * hardware packets have extra stuff stuffed in the bottom 12 bits.
    */
   uint64_t addr = p[0] & ~0xfffu;

   if (gen_spec_get_gen(spec) >= gen_make_gen(8,0)) {
      /* On Broadwell and above, we have 48-bit addresses which consume two
       * dwords.  Some packets require that these get stored in a "canonical
       * form" which means that bit 47 is sign-extended through the upper
       * bits. In order to correctly handle those aub dumps, we need to mask
       * off the top 16 bits.
       */
      addr |= ((uint64_t)p[1] & 0xffff) << 32;
   }

   return addr;
}

static inline uint64_t
get_offset(uint32_t *p, uint32_t start, uint32_t end)
{
   assert(start <= end);
   assert(end < 64);

   uint64_t mask = (~0ull >> (64 - (end - start + 1))) << start;

   uint64_t offset = p[0];
   if (end >= 32)
      offset |= (uint64_t) p[1] << 32;

   return offset & mask;
}

static void
handle_state_base_address(struct gen_spec *spec, uint32_t *p)
{
   if (gen_spec_get_gen(spec) >= gen_make_gen(8,0)) {
      if (p[1] & 1)
         general_state_base = get_address(spec, &p[1]);
      if (p[4] & 1)
         surface_state_base = get_address(spec, &p[4]);
      if (p[6] & 1)
         dynamic_state_base = get_address(spec, &p[6]);
      if (p[10] & 1)
         instruction_base = get_address(spec, &p[10]);
      if (p[15] & 1)
         instruction_bound = p[15] & 0xfff;
   } else {
      if (p[2] & 1)
         surface_state_base = get_address(spec, &p[2]);
      if (p[3] & 1)
         dynamic_state_base = get_address(spec, &p[3]);
      if (p[5] & 1)
         instruction_base = get_address(spec, &p[5]);
      if (p[9] & 1)
         instruction_bound = get_address(spec, &p[9]);
   }
}

static void
dump_samplers(struct gen_spec *spec, uint32_t offset)
{
   uint32_t i;
   uint64_t start;
   struct gen_group *sampler_state;

   sampler_state = gen_spec_find_struct(spec, "SAMPLER_STATE");

   start = dynamic_state_base + offset;
   for (i = 0; i < 4; i++) {
      printf("sampler state %d\n", i);
      decode_structure(spec, sampler_state, gtt + start + i * 16);
   }
}

static void
handle_media_interface_descriptor_load(struct gen_spec *spec, uint32_t *p)
{
   int i, length = p[2] / 32;
   struct gen_group *descriptor_structure;
   uint32_t *descriptors;
   uint64_t start;
   struct brw_instruction *insns;

   descriptor_structure =
      gen_spec_find_struct(spec, "INTERFACE_DESCRIPTOR_DATA");
   if (descriptor_structure == NULL) {
      printf("did not find INTERFACE_DESCRIPTOR_DATA info\n");
      return;
   }

   start = dynamic_state_base + p[3];
   descriptors = gtt + start;
   for (i = 0; i < length; i++, descriptors += 8) {
      printf("descriptor %u: %08x\n", i, *descriptors);
      decode_structure(spec, descriptor_structure, descriptors);

      start = instruction_base + descriptors[0];
      if (!valid_offset(start)) {
         printf("kernel: %08"PRIx64" <not valid>\n", start);
         continue;
      } else {
         printf("kernel: %08"PRIx64"\n", start);
      }

      insns = (struct brw_instruction *) (gtt + start);
      gen_disasm_disassemble(disasm, insns, 0, stdout);

      dump_samplers(spec, descriptors[3] & ~0x1f);
      dump_binding_table(spec, descriptors[4] & ~0x1f);
   }
}

/* Heuristic to determine whether a uint32_t is probably actually a float
 * (http://stackoverflow.com/a/2953466)
 */

static bool
probably_float(uint32_t bits)
{
   int exp = ((bits & 0x7f800000U) >> 23) - 127;
   uint32_t mant = bits & 0x007fffff;

   /* +- 0.0 */
   if (exp == -127 && mant == 0)
      return true;

   /* +- 1 billionth to 1 billion */
   if (-30 <= exp && exp <= 30)
      return true;

   /* some value with only a few binary digits */
   if ((mant & 0x0000ffff) == 0)
      return true;

   return false;
}

static void
handle_3dstate_vertex_buffers(struct gen_spec *spec, uint32_t *p)
{
   uint32_t *end, *s, *dw, *dwend;
   uint64_t offset;
   int n, i, count, stride;

   end = (p[0] & 0xff) + p + 2;
   for (s = &p[1], n = 0; s < end; s += 4, n++) {
      if (gen_spec_get_gen(spec) >= gen_make_gen(8, 0)) {
         offset = *(uint64_t *) &s[1];
         dwend = gtt + offset + s[3];
      } else {
         offset = s[1];
         dwend = gtt + s[2] + 1;
      }

      stride = field(s[0], 0, 11);
      count = 0;
      printf("vertex buffer %d, size %d\n", n, s[3]);
      for (dw = gtt + offset, i = 0; dw < dwend && i < 256; dw++) {
         if (count == 0 && count % (8 * 4) == 0)
            printf("  ");

         if (probably_float(*dw))
            printf("  %8.2f", *(float *) dw);
         else
            printf("  0x%08x", *dw);

         i++;
         count += 4;

         if (count == stride) {
            printf("\n");
            count = 0;
         } else if (count % (8 * 4) == 0) {
            printf("\n");
         } else {
            printf(" ");
         }
      }
      if (count > 0 && count % (8 * 4) != 0)
         printf("\n");
   }
}

static void
handle_3dstate_vs(struct gen_spec *spec, uint32_t *p)
{
   uint64_t start;
   struct brw_instruction *insns;
   int vs_enable;

   if (gen_spec_get_gen(spec) >= gen_make_gen(8, 0)) {
      start = get_offset(&p[1], 6, 63);
      vs_enable = p[7] & 1;
   } else {
      start = get_offset(&p[1], 6, 31);
      vs_enable = p[5] & 1;
   }

   if (vs_enable) {
      printf("instruction_base %08"PRIx64", start %08"PRIx64"\n",
             instruction_base, start);

      insns = (struct brw_instruction *) (gtt + instruction_base + start);
      gen_disasm_disassemble(disasm, insns, 0, stdout);
   }
}

static void
handle_3dstate_hs(struct gen_spec *spec, uint32_t *p)
{
   uint64_t start;
   struct brw_instruction *insns;
   int hs_enable;

   if (gen_spec_get_gen(spec) >= gen_make_gen(8, 0)) {
      start = get_offset(&p[3], 6, 63);
   } else {
      start = get_offset(&p[3], 6, 31);
   }

   hs_enable = p[2] & 0x80000000;

   if (hs_enable) {
      printf("instruction_base %08"PRIx64", start %08"PRIx64"\n",
             instruction_base, start);

      insns = (struct brw_instruction *) (gtt + instruction_base + start);
      gen_disasm_disassemble(disasm, insns, 0, stdout);
   }
}

static void
handle_3dstate_constant(struct gen_spec *spec, uint32_t *p)
{
   int i, j, length;
   uint32_t *dw;
   float *f;

   for (i = 0; i < 4; i++) {
      length = (p[1 + i / 2] >> (i & 1) * 16) & 0xffff;
      f = (float *) (gtt + p[3 + i * 2] + dynamic_state_base);
      dw = (uint32_t *) f;
      for (j = 0; j < length * 8; j++) {
         if (probably_float(dw[j]))
            printf("  %04.3f", f[j]);
         else
            printf("  0x%08x", dw[j]);

         if ((j & 7) == 7)
            printf("\n");
      }
   }
}

static void
handle_3dstate_ps(struct gen_spec *spec, uint32_t *p)
{
   uint32_t mask = ~((1 << 6) - 1);
   uint64_t start;
   struct brw_instruction *insns;
   static const char unused[] = "unused";
   static const char *pixel_type[3] = {"8 pixel", "16 pixel", "32 pixel"};
   const char *k0, *k1, *k2;
   uint32_t k_mask, k1_offset, k2_offset;

   if (gen_spec_get_gen(spec) >= gen_make_gen(8, 0)) {
      k_mask = p[6] & 7;
      k1_offset = 8;
      k2_offset = 10;
   } else {
      k_mask = p[4] & 7;
      k1_offset = 6;
      k2_offset = 7;
   }

#define DISPATCH_8 1
#define DISPATCH_16 2
#define DISPATCH_32 4

   switch (k_mask) {
   case DISPATCH_8:
      k0 = pixel_type[0];
      k1 = unused;
      k2 = unused;
      break;
   case DISPATCH_16:
      k0 = pixel_type[1];
      k1 = unused;
      k2 = unused;
      break;
   case DISPATCH_8 | DISPATCH_16:
      k0 = pixel_type[0];
      k1 = unused;
      k2 = pixel_type[1];
      break;
   case DISPATCH_32:
      k0 = pixel_type[2];
      k1 = unused;
      k2 = unused;
      break;
   case DISPATCH_16 | DISPATCH_32:
      k0 = unused;
      k1 = pixel_type[2];
      k2 = pixel_type[1];
      break;
   case DISPATCH_8 | DISPATCH_16 | DISPATCH_32:
      k0 = pixel_type[0];
      k1 = pixel_type[2];
      k2 = pixel_type[1];
      break;
   default:
      k0 = unused;
      k1 = unused;
      k2 = unused;
      break;
   }

   start = instruction_base + (p[1] & mask);
   printf("  Kernel[0] %s\n", k0);
   if (k0 != unused) {
      insns = (struct brw_instruction *) (gtt + start);
      gen_disasm_disassemble(disasm, insns, 0, stdout);
   }

   start = instruction_base + (p[k1_offset] & mask);
   printf("  Kernel[1] %s\n", k1);
   if (k1 != unused) {
      insns = (struct brw_instruction *) (gtt + start);
      gen_disasm_disassemble(disasm, insns, 0, stdout);
   }

   start = instruction_base + (p[k2_offset] & mask);
   printf("  Kernel[2] %s\n", k2);
   if (k2 != unused) {
      insns = (struct brw_instruction *) (gtt + start);
      gen_disasm_disassemble(disasm, insns, 0, stdout);
   }
}

static void
handle_3dstate_binding_table_pointers(struct gen_spec *spec, uint32_t *p)
{
   dump_binding_table(spec, p[1]);
}

static void
handle_3dstate_sampler_state_pointers(struct gen_spec *spec, uint32_t *p)
{
   dump_samplers(spec, p[1]);
}

static void
handle_3dstate_viewport_state_pointers_cc(struct gen_spec *spec, uint32_t *p)
{
   uint64_t start;
   struct gen_group *cc_viewport;

   cc_viewport = gen_spec_find_struct(spec, "CC_VIEWPORT");

   start = dynamic_state_base + (p[1] & ~0x1fu);
   for (uint32_t i = 0; i < 4; i++) {
      printf("viewport %d\n", i);
      decode_structure(spec, cc_viewport, gtt + start + i * 8);
   }
}

static void
handle_3dstate_viewport_state_pointers_sf_clip(struct gen_spec *spec,
                                               uint32_t *p)
{
   uint64_t start;
   struct gen_group *sf_clip_viewport;

   sf_clip_viewport = gen_spec_find_struct(spec, "SF_CLIP_VIEWPORT");

   start = dynamic_state_base + (p[1] & ~0x3fu);
   for (uint32_t i = 0; i < 4; i++) {
      printf("viewport %d\n", i);
      decode_structure(spec, sf_clip_viewport, gtt + start + i * 64);
   }
}

static void
handle_3dstate_blend_state_pointers(struct gen_spec *spec, uint32_t *p)
{
   uint64_t start;
   struct gen_group *blend_state;

   blend_state = gen_spec_find_struct(spec, "BLEND_STATE");

   start = dynamic_state_base + (p[1] & ~0x3fu);
   decode_structure(spec, blend_state, gtt + start);
}

static void
handle_3dstate_cc_state_pointers(struct gen_spec *spec, uint32_t *p)
{
   uint64_t start;
   struct gen_group *cc_state;

   cc_state = gen_spec_find_struct(spec, "COLOR_CALC_STATE");

   start = dynamic_state_base + (p[1] & ~0x3fu);
   decode_structure(spec, cc_state, gtt + start);
}

static void
handle_3dstate_scissor_state_pointers(struct gen_spec *spec, uint32_t *p)
{
   uint64_t start;
   struct gen_group *scissor_rect;

   scissor_rect = gen_spec_find_struct(spec, "SCISSOR_RECT");

   start = dynamic_state_base + (p[1] & ~0x1fu);
   decode_structure(spec, scissor_rect, gtt + start);
}

static void
handle_load_register_imm(struct gen_spec *spec, uint32_t *p)
{
   struct gen_group *reg = gen_spec_find_register(spec, p[1]);

   if (reg != NULL) {
      printf("register %s (0x%x): 0x%x\n",
             reg->name, reg->register_offset, p[2]);
      decode_structure(spec, reg, &p[2]);
   }
}

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

#define STATE_BASE_ADDRESS                  0x61010000

#define MEDIA_INTERFACE_DESCRIPTOR_LOAD     0x70020000

#define _3DSTATE_INDEX_BUFFER               0x780a0000
#define _3DSTATE_VERTEX_BUFFERS             0x78080000

#define _3DSTATE_VS                         0x78100000
#define _3DSTATE_GS                         0x78110000
#define _3DSTATE_HS                         0x781b0000
#define _3DSTATE_DS                         0x781d0000

#define _3DSTATE_CONSTANT_VS                0x78150000
#define _3DSTATE_CONSTANT_GS                0x78160000
#define _3DSTATE_CONSTANT_PS                0x78170000
#define _3DSTATE_CONSTANT_HS                0x78190000
#define _3DSTATE_CONSTANT_DS                0x781A0000

#define _3DSTATE_PS                         0x78200000

#define _3DSTATE_BINDING_TABLE_POINTERS_VS  0x78260000
#define _3DSTATE_BINDING_TABLE_POINTERS_HS  0x78270000
#define _3DSTATE_BINDING_TABLE_POINTERS_DS  0x78280000
#define _3DSTATE_BINDING_TABLE_POINTERS_GS  0x78290000
#define _3DSTATE_BINDING_TABLE_POINTERS_PS  0x782a0000

#define _3DSTATE_SAMPLER_STATE_POINTERS_VS  0x782b0000
#define _3DSTATE_SAMPLER_STATE_POINTERS_GS  0x782e0000
#define _3DSTATE_SAMPLER_STATE_POINTERS_PS  0x782f0000

#define _3DSTATE_VIEWPORT_STATE_POINTERS_CC 0x78230000
#define _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP 0x78210000
#define _3DSTATE_BLEND_STATE_POINTERS       0x78240000
#define _3DSTATE_CC_STATE_POINTERS          0x780e0000
#define _3DSTATE_SCISSOR_STATE_POINTERS     0x780f0000

#define _MI_LOAD_REGISTER_IMM               0x11000000

struct custom_handler {
   uint32_t opcode;
   void (*handle)(struct gen_spec *spec, uint32_t *p);
} custom_handlers[] = {
   { STATE_BASE_ADDRESS, handle_state_base_address },
   { MEDIA_INTERFACE_DESCRIPTOR_LOAD, handle_media_interface_descriptor_load },
   { _3DSTATE_VERTEX_BUFFERS, handle_3dstate_vertex_buffers },
   { _3DSTATE_INDEX_BUFFER, handle_3dstate_index_buffer },
   { _3DSTATE_VS, handle_3dstate_vs },
   { _3DSTATE_GS, handle_3dstate_vs },
   { _3DSTATE_DS, handle_3dstate_vs },
   { _3DSTATE_HS, handle_3dstate_hs },
   { _3DSTATE_CONSTANT_VS, handle_3dstate_constant },
   { _3DSTATE_CONSTANT_GS, handle_3dstate_constant },
   { _3DSTATE_CONSTANT_PS, handle_3dstate_constant },
   { _3DSTATE_CONSTANT_HS, handle_3dstate_constant },
   { _3DSTATE_CONSTANT_DS, handle_3dstate_constant },
   { _3DSTATE_PS, handle_3dstate_ps },

   { _3DSTATE_BINDING_TABLE_POINTERS_VS, handle_3dstate_binding_table_pointers },
   { _3DSTATE_BINDING_TABLE_POINTERS_HS, handle_3dstate_binding_table_pointers },
   { _3DSTATE_BINDING_TABLE_POINTERS_DS, handle_3dstate_binding_table_pointers },
   { _3DSTATE_BINDING_TABLE_POINTERS_GS, handle_3dstate_binding_table_pointers },
   { _3DSTATE_BINDING_TABLE_POINTERS_PS, handle_3dstate_binding_table_pointers },

   { _3DSTATE_SAMPLER_STATE_POINTERS_VS, handle_3dstate_sampler_state_pointers },
   { _3DSTATE_SAMPLER_STATE_POINTERS_GS, handle_3dstate_sampler_state_pointers },
   { _3DSTATE_SAMPLER_STATE_POINTERS_PS, handle_3dstate_sampler_state_pointers },

   { _3DSTATE_VIEWPORT_STATE_POINTERS_CC, handle_3dstate_viewport_state_pointers_cc },
   { _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP, handle_3dstate_viewport_state_pointers_sf_clip },
   { _3DSTATE_BLEND_STATE_POINTERS, handle_3dstate_blend_state_pointers },
   { _3DSTATE_CC_STATE_POINTERS, handle_3dstate_cc_state_pointers },
   { _3DSTATE_SCISSOR_STATE_POINTERS, handle_3dstate_scissor_state_pointers },
   { _MI_LOAD_REGISTER_IMM, handle_load_register_imm }
};

static void
parse_commands(struct gen_spec *spec, uint32_t *cmds, int size, int engine)
{
   uint32_t *p, *end = cmds + size / 4;
   unsigned int length, i;
   struct gen_group *inst;

   for (p = cmds; p < end; p += length) {
      inst = gen_spec_find_instruction(spec, p);
      if (inst == NULL) {
         printf("unknown instruction %08x\n", p[0]);
         length = (p[0] & 0xff) + 2;
         continue;
      }
      length = gen_group_get_length(inst, p);

      const char *color, *reset_color = NORMAL;
      uint64_t offset;

      if (option_full_decode) {
         if ((p[0] & 0xffff0000) == AUB_MI_BATCH_BUFFER_START ||
             (p[0] & 0xffff0000) == AUB_MI_BATCH_BUFFER_END)
            color = GREEN_HEADER;
         else
            color = BLUE_HEADER;
      } else
         color = NORMAL;

      if (option_color == COLOR_NEVER) {
         color = "";
         reset_color = "";
      }

      if (option_print_offsets)
         offset = (void *) p - gtt;
      else
         offset = 0;

      printf("%s0x%08"PRIx64":  0x%08x:  %-80s%s\n",
             color, offset, p[0],
             gen_group_get_name(inst), reset_color);

      if (option_full_decode) {
         struct gen_field_iterator iter;
         char *token = NULL;
         int idx = 0, dword_num = 0;
         gen_field_iterator_init(&iter, inst, p,
                                 option_color == COLOR_ALWAYS);
         while (gen_field_iterator_next(&iter)) {
            idx = 0;
            print_dword_val(&iter, offset, &dword_num);
            if (dword_num > 0)
                token = print_iterator_values(&iter, &idx);
            if (token != NULL) {
                printf("0x%08"PRIx64":  0x%08x : Dword %d\n",
                       offset + 4 * idx, p[idx], idx);
                handle_struct_decode(spec,token, &p[idx]);
                token = NULL;
            }
         }

         for (i = 0; i < ARRAY_LENGTH(custom_handlers); i++) {
            if (gen_group_get_opcode(inst) ==
                custom_handlers[i].opcode)
               custom_handlers[i].handle(spec, p);
         }
      }

      if ((p[0] & 0xffff0000) == AUB_MI_BATCH_BUFFER_START) {
         uint64_t start = get_address(spec, &p[1]);

         if (p[0] & (1 << 22)) {
            /* MI_BATCH_BUFFER_START with "2nd Level Batch Buffer" set acts
             * like a subroutine call.  Commands that come afterwards get
             * processed once the 2nd level batch buffer returns with
             * MI_BATCH_BUFFER_END.
             */
            parse_commands(spec, gtt + start, gtt_end - start, engine);
         } else {
            /* MI_BATCH_BUFFER_START with "2nd Level Batch Buffer" unset acts
             * like a goto.  Nothing after it will ever get processed.  In
             * order to prevent the recursion from growing, we just reset the
             * loop and continue;
             */
            p = gtt + start;
            /* We don't know where secondaries end so use the GTT end */
            end = gtt + gtt_end;
            length = 0;
            continue;
         }
      } else if ((p[0] & 0xffff0000) == AUB_MI_BATCH_BUFFER_END) {
         break;
      }
   }
}

#define GEN_ENGINE_RENDER 1
#define GEN_ENGINE_BLITTER 2

static void
handle_trace_block(uint32_t *p)
{
   int operation = p[1] & AUB_TRACE_OPERATION_MASK;
   int type = p[1] & AUB_TRACE_TYPE_MASK;
   int address_space = p[1] & AUB_TRACE_ADDRESS_SPACE_MASK;
   uint64_t offset = p[3];
   uint32_t size = p[4];
   int header_length = p[0] & 0xffff;
   uint32_t *data = p + header_length + 2;
   int engine = GEN_ENGINE_RENDER;

   if (gen_spec_get_gen(spec) >= gen_make_gen(8,0))
      offset += (uint64_t) p[5] << 32;

   switch (operation) {
   case AUB_TRACE_OP_DATA_WRITE:
      if (address_space != AUB_TRACE_MEMTYPE_GTT)
         break;
      if (gtt_size < offset + size) {
         fprintf(stderr, "overflow gtt space: %s\n", strerror(errno));
         exit(EXIT_FAILURE);
      }
      memcpy((char *) gtt + offset, data, size);
      if (gtt_end < offset + size)
         gtt_end = offset + size;
      break;
   case AUB_TRACE_OP_COMMAND_WRITE:
      switch (type) {
      case AUB_TRACE_TYPE_RING_PRB0:
         engine = GEN_ENGINE_RENDER;
         break;
      case AUB_TRACE_TYPE_RING_PRB2:
         engine = GEN_ENGINE_BLITTER;
         break;
      default:
         printf("command write to unknown ring %d\n", type);
         break;
      }

      parse_commands(spec, data, size, engine);
      gtt_end = 0;
      break;
   }
}

static void
handle_trace_header(uint32_t *p)
{
   /* The intel_aubdump tool from IGT is kind enough to put a PCI-ID= tag in
    * the AUB header comment.  If the user hasn't specified a hardware
    * generation, try to use the one from the AUB file.
    */
   uint32_t *end = p + (p[0] & 0xffff) + 2;
   int aub_pci_id = 0;
   if (end > &p[12] && p[12] > 0)
      sscanf((char *)&p[13], "PCI-ID=%i", &aub_pci_id);

   if (pci_id == 0)
      pci_id = aub_pci_id;

   struct gen_device_info devinfo;
   if (!gen_get_device_info(pci_id, &devinfo)) {
      fprintf(stderr, "can't find device information: pci_id=0x%x\n", pci_id);
      exit(EXIT_FAILURE);
   }

   if (xml_path == NULL)
      spec = gen_spec_load(&devinfo);
   else
      spec = gen_spec_load_from_path(&devinfo, xml_path);
   disasm = gen_disasm_create(pci_id);

   if (spec == NULL || disasm == NULL)
      exit(EXIT_FAILURE);

   printf("%sAubinator: Intel AUB file decoder.%-80s%s\n",
          GREEN_HEADER, "", NORMAL);

   if (input_file)
      printf("File name:        %s\n", input_file);

   if (aub_pci_id)
      printf("PCI ID:           0x%x\n", aub_pci_id);

   char app_name[33];
   strncpy(app_name, (char *)&p[2], 32);
   app_name[32] = 0;
   printf("Application name: %s\n", app_name);

   printf("Decoding as:      %s\n", gen_get_device_name(pci_id));

   /* Throw in a new line before the first batch */
   printf("\n");
}

struct aub_file {
   FILE *stream;

   uint32_t *map, *end, *cursor;
   uint32_t *mem_end;
};

static struct aub_file *
aub_file_open(const char *filename)
{
   struct aub_file *file;
   struct stat sb;
   int fd;

   file = calloc(1, sizeof *file);
   fd = open(filename, O_RDONLY);
   if (fd == -1) {
      fprintf(stderr, "open %s failed: %s\n", filename, strerror(errno));
      exit(EXIT_FAILURE);
   }

   if (fstat(fd, &sb) == -1) {
      fprintf(stderr, "stat failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   file->map = mmap(NULL, sb.st_size,
                    PROT_READ, MAP_SHARED, fd, 0);
   if (file->map == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   file->cursor = file->map;
   file->end = file->map + sb.st_size / 4;

   return file;
}

static struct aub_file *
aub_file_stdin(void)
{
   struct aub_file *file;

   file = calloc(1, sizeof *file);
   file->stream = stdin;

   return file;
}

#define TYPE(dw)       (((dw) >> 29) & 7)
#define OPCODE(dw)     (((dw) >> 23) & 0x3f)
#define SUBOPCODE(dw)  (((dw) >> 16) & 0x7f)

#define MAKE_HEADER(type, opcode, subopcode) \
                   (((type) << 29) | ((opcode) << 23) | ((subopcode) << 16))

#define TYPE_AUB            0x7

/* Classic AUB opcodes */
#define OPCODE_AUB          0x01
#define SUBOPCODE_HEADER    0x05
#define SUBOPCODE_BLOCK     0x41
#define SUBOPCODE_BMP       0x1e

/* Newer version AUB opcode */
#define OPCODE_NEW_AUB      0x2e
#define SUBOPCODE_VERSION   0x00
#define SUBOPCODE_REG_WRITE 0x03
#define SUBOPCODE_MEM_POLL  0x05
#define SUBOPCODE_MEM_WRITE 0x06

#define MAKE_GEN(major, minor) ( ((major) << 8) | (minor) )

struct {
   const char *name;
   uint32_t gen;
} device_map[] = {
   { "bwr", MAKE_GEN(4, 0) },
   { "cln", MAKE_GEN(4, 0) },
   { "blc", MAKE_GEN(4, 0) },
   { "ctg", MAKE_GEN(4, 0) },
   { "el", MAKE_GEN(4, 0) },
   { "il", MAKE_GEN(4, 0) },
   { "sbr", MAKE_GEN(6, 0) },
   { "ivb", MAKE_GEN(7, 0) },
   { "lrb2", MAKE_GEN(0, 0) },
   { "hsw", MAKE_GEN(7, 5) },
   { "vlv", MAKE_GEN(7, 0) },
   { "bdw", MAKE_GEN(8, 0) },
   { "skl", MAKE_GEN(9, 0) },
   { "chv", MAKE_GEN(8, 0) },
   { "bxt", MAKE_GEN(9, 0) }
};

enum {
   AUB_ITEM_DECODE_OK,
   AUB_ITEM_DECODE_FAILED,
   AUB_ITEM_DECODE_NEED_MORE_DATA,
};

static int
aub_file_decode_batch(struct aub_file *file)
{
   uint32_t *p, h, device, data_type, *new_cursor;
   int header_length, bias;

   if (file->end - file->cursor < 1)
      return AUB_ITEM_DECODE_NEED_MORE_DATA;

   p = file->cursor;
   h = *p;
   header_length = h & 0xffff;

   switch (OPCODE(h)) {
   case OPCODE_AUB:
      bias = 2;
      break;
   case OPCODE_NEW_AUB:
      bias = 1;
      break;
   default:
      printf("unknown opcode %d at %td/%td\n",
             OPCODE(h), file->cursor - file->map,
             file->end - file->map);
      return AUB_ITEM_DECODE_FAILED;
   }

   new_cursor = p + header_length + bias;
   if ((h & 0xffff0000) == MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_BLOCK)) {
      if (file->end - file->cursor < 4)
         return AUB_ITEM_DECODE_NEED_MORE_DATA;
      new_cursor += p[4] / 4;
   }

   if (new_cursor > file->end)
      return AUB_ITEM_DECODE_NEED_MORE_DATA;

   switch (h & 0xffff0000) {
   case MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_HEADER):
      handle_trace_header(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_BLOCK):
      handle_trace_block(p);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_AUB, SUBOPCODE_BMP):
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_VERSION):
      printf("version block: dw1 %08x\n", p[1]);
      device = (p[1] >> 8) & 0xff;
      printf("  device %s\n", device_map[device].name);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_REG_WRITE):
      printf("register write block: (dwords %d)\n", h & 0xffff);
      printf("  reg 0x%x, data 0x%x\n", p[1], p[5]);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_MEM_WRITE):
      printf("memory write block (dwords %d):\n", h & 0xffff);
      printf("  address 0x%"PRIx64"\n", *(uint64_t *) &p[1]);
      data_type = (p[3] >> 20) & 0xff;
      if (data_type != 0)
         printf("  data type 0x%x\n", data_type);
      printf("  address space 0x%x\n", (p[3] >> 28) & 0xf);
      break;
   case MAKE_HEADER(TYPE_AUB, OPCODE_NEW_AUB, SUBOPCODE_MEM_POLL):
      printf("memory poll block (dwords %d):\n", h & 0xffff);
      break;
   default:
      printf("unknown block type=0x%x, opcode=0x%x, "
             "subopcode=0x%x (%08x)\n", TYPE(h), OPCODE(h), SUBOPCODE(h), h);
      break;
   }
   file->cursor = new_cursor;

   return AUB_ITEM_DECODE_OK;
}

static int
aub_file_more_stuff(struct aub_file *file)
{
   return file->cursor < file->end || (file->stream && !feof(file->stream));
}

#define AUB_READ_BUFFER_SIZE (4096)
#define MAX(a, b) ((a) < (b) ? (b) : (a))

static void
aub_file_data_grow(struct aub_file *file)
{
   size_t old_size = (file->mem_end - file->map) * 4;
   size_t new_size = MAX(old_size * 2, AUB_READ_BUFFER_SIZE);
   uint32_t *new_start = realloc(file->map, new_size);

   file->cursor = new_start + (file->cursor - file->map);
   file->end = new_start + (file->end - file->map);
   file->map = new_start;
   file->mem_end = file->map + (new_size / 4);
}

static bool
aub_file_data_load(struct aub_file *file)
{
   size_t r;

   if (file->stream == NULL)
      return false;

   /* First remove any consumed data */
   if (file->cursor > file->map) {
      memmove(file->map, file->cursor,
              (file->end - file->cursor) * 4);
      file->end -= file->cursor - file->map;
      file->cursor = file->map;
   }

   /* Then load some new data in */
   if ((file->mem_end - file->end) < (AUB_READ_BUFFER_SIZE / 4))
      aub_file_data_grow(file);

   r = fread(file->end, 1, (file->mem_end - file->end) * 4, file->stream);
   file->end += r / 4;

   return r != 0;
}

static void
setup_pager(void)
{
   int fds[2];
   pid_t pid;

   if (!isatty(1))
      return;

   if (pipe(fds) == -1)
      return;

   pid = fork();
   if (pid == -1)
      return;

   if (pid == 0) {
      close(fds[1]);
      dup2(fds[0], 0);
      execlp("less", "less", "-FRSi", NULL);
   }

   close(fds[0]);
   dup2(fds[1], 1);
   close(fds[1]);
}

static void
print_help(const char *progname, FILE *file)
{
   fprintf(file,
           "Usage: %s [OPTION]... [FILE]\n"
           "Decode aub file contents from either FILE or the standard input.\n\n"
           "A valid --gen option must be provided.\n\n"
           "      --help          display this help and exit\n"
           "      --gen=platform  decode for given platform (ivb, byt, hsw, bdw, chv, skl, kbl or bxt)\n"
           "      --headers       decode only command headers\n"
           "      --color[=WHEN]  colorize the output; WHEN can be 'auto' (default\n"
           "                        if omitted), 'always', or 'never'\n"
           "      --no-pager      don't launch pager\n"
           "      --no-offsets    don't print instruction offsets\n"
           "      --xml=DIR       load hardware xml description from directory DIR\n",
           progname);
}

int main(int argc, char *argv[])
{
   struct aub_file *file;
   int c, i;
   bool help = false, pager = true;
   const struct {
      const char *name;
      int pci_id;
   } gens[] = {
      { "ivb", 0x0166 }, /* Intel(R) Ivybridge Mobile GT2 */
      { "hsw", 0x0416 }, /* Intel(R) Haswell Mobile GT2 */
      { "byt", 0x0155 }, /* Intel(R) Bay Trail */
      { "bdw", 0x1616 }, /* Intel(R) HD Graphics 5500 (Broadwell GT2) */
      { "chv", 0x22B3 }, /* Intel(R) HD Graphics (Cherryview) */
      { "skl", 0x1912 }, /* Intel(R) HD Graphics 530 (Skylake GT2) */
      { "kbl", 0x591D }, /* Intel(R) Kabylake GT2 */
      { "bxt", 0x0A84 }  /* Intel(R) HD Graphics (Broxton) */
   };
   const struct option aubinator_opts[] = {
      { "help",       no_argument,       (int *) &help,                 true },
      { "no-pager",   no_argument,       (int *) &pager,                false },
      { "no-offsets", no_argument,       (int *) &option_print_offsets, false },
      { "gen",        required_argument, NULL,                          'g' },
      { "headers",    no_argument,       (int *) &option_full_decode,   false },
      { "color",      required_argument, NULL,                          'c' },
      { "xml",        required_argument, NULL,                          'x' },
      { NULL,         0,                 NULL,                          0 }
   };

   i = 0;
   while ((c = getopt_long(argc, argv, "", aubinator_opts, &i)) != -1) {
      switch (c) {
      case 'g':
         for (i = 0; i < ARRAY_SIZE(gens); i++) {
            if (!strcmp(optarg, gens[i].name)) {
               pci_id = gens[i].pci_id;
               break;
            }
         }
         if (i == ARRAY_SIZE(gens)) {
            fprintf(stderr, "can't parse gen: '%s', expected ivb, byt, hsw, "
                                   "bdw, chv, skl, kbl or bxt\n", optarg);
            exit(EXIT_FAILURE);
         }
         break;
      case 'c':
         if (optarg == NULL || strcmp(optarg, "always") == 0)
            option_color = COLOR_ALWAYS;
         else if (strcmp(optarg, "never") == 0)
            option_color = COLOR_NEVER;
         else if (strcmp(optarg, "auto") == 0)
            option_color = COLOR_AUTO;
         else {
            fprintf(stderr, "invalid value for --color: %s", optarg);
            exit(EXIT_FAILURE);
         }
         break;
      case 'x':
         xml_path = strdup(optarg);
         break;
      default:
         break;
      }
   }

   if (help || argc == 1) {
      print_help(argv[0], stderr);
      exit(0);
   }

   if (optind < argc)
      input_file = argv[optind];

   /* Do this before we redirect stdout to pager. */
   if (option_color == COLOR_AUTO)
      option_color = isatty(1) ? COLOR_ALWAYS : COLOR_NEVER;

   if (isatty(1) && pager)
      setup_pager();

   if (input_file == NULL)
      file = aub_file_stdin();
   else
      file = aub_file_open(input_file);

   /* mmap a terabyte for our gtt space. */
   gtt_size = 1ull << 40;
   gtt = mmap(NULL, gtt_size, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS |  MAP_NORESERVE, -1, 0);
   if (gtt == MAP_FAILED) {
      fprintf(stderr, "failed to alloc gtt space: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }

   while (aub_file_more_stuff(file)) {
      switch (aub_file_decode_batch(file)) {
      case AUB_ITEM_DECODE_OK:
         break;
      case AUB_ITEM_DECODE_NEED_MORE_DATA:
         if (!file->stream) {
            file->cursor = file->end;
            break;
         }
         if (aub_file_more_stuff(file) && !aub_file_data_load(file)) {
            fprintf(stderr, "failed to load data from stdin\n");
            exit(EXIT_FAILURE);
         }
         break;
      default:
         fprintf(stderr, "failed to parse aubdump data\n");
         exit(EXIT_FAILURE);
      }
   }


   fflush(stdout);
   /* close the stdout which is opened to write the output */
   close(1);
   free(xml_path);

   wait(NULL);

   return EXIT_SUCCESS;
}
