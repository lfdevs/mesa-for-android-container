/*
 * Copyright © 2007-2017 Intel Corporation
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
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <assert.h>
#include <getopt.h>
#include <zlib.h>

#include "common/gen_decoder.h"
#include "util/macros.h"
#include "gen_disasm.h"

#define CSI "\e["
#define BLUE_HEADER  CSI "0;44m"
#define GREEN_HEADER CSI "1;42m"
#define NORMAL       CSI "0m"

/* options */

static bool option_full_decode = true;
static bool option_print_offsets = true;
static enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } option_color;
static char *xml_path = NULL;

static uint32_t
print_head(unsigned int reg)
{
   printf("    head = 0x%08x, wraps = %d\n", reg & (0x7ffff<<2), reg >> 21);
   return reg & (0x7ffff<<2);
}

static void
print_register(struct gen_spec *spec, const char *name, uint32_t reg)
{
   struct gen_group *reg_spec = gen_spec_find_register_by_name(spec, name);

   if (reg_spec)
      gen_print_group(stdout, reg_spec, 0, &reg, option_color == COLOR_ALWAYS);
}

struct ring_register_mapping {
   const char *ring_name;
   const char *register_name;
};

static const struct ring_register_mapping acthd_registers[] = {
   { "blt", "BCS_ACTHD_UDW" },
   { "bsd", "VCS_ACTHD_UDW" },
   { "bsd2", "VCS2_ACTHD_UDW" },
   { "render", "ACTHD_UDW" },
   { "vebox", "VECS_ACTHD_UDW" },
};

static const struct ring_register_mapping ctl_registers[] = {
   { "blt", "BCS_RING_BUFFER_CTL" },
   { "bsd", "VCS_RING_BUFFER_CTL" },
   { "bsd2", "VCS2_RING_BUFFER_CTL" },
   { "render", "RCS_RING_BUFFER_CTL" },
   { "vebox", "VECS_RING_BUFFER_CTL" },
};

static const struct ring_register_mapping fault_registers[] = {
   { "blt", "BCS_FAULT_REG" },
   { "bsd", "VCS_FAULT_REG" },
   { "render", "RCS_FAULT_REG" },
   { "vebox", "VECS_FAULT_REG" },
};

static const char *
register_name_from_ring(const struct ring_register_mapping *mapping,
                        unsigned nb_mapping,
                        const char *ring_name)
{
   for (unsigned i = 0; i < nb_mapping; i++) {
      if (strcmp(mapping[i].ring_name, ring_name) == 0)
         return mapping[i].register_name;
   }
   return NULL;
}

static const char *
instdone_register_for_ring(const struct gen_device_info *devinfo,
                           const char *ring_name)
{
   if (strcmp(ring_name, "blt") == 0)
      return "BCS_INSTDONE";
   else if (strcmp(ring_name, "vebox") == 0)
      return "VECS_INSTDONE";
   else if (strcmp(ring_name, "bsd") == 0)
      return "VCS_INSTDONE";
   else if (strcmp(ring_name, "render") == 0) {
      if (devinfo->gen == 6)
         return "INSTDONE_2";
      return "INSTDONE_1";
   }

   return NULL;
}

static void
print_pgtbl_err(unsigned int reg, struct gen_device_info *devinfo)
{
   if (reg & (1 << 26))
      printf("    Invalid Sampler Cache GTT entry\n");
   if (reg & (1 << 24))
      printf("    Invalid Render Cache GTT entry\n");
   if (reg & (1 << 23))
      printf("    Invalid Instruction/State Cache GTT entry\n");
   if (reg & (1 << 22))
      printf("    There is no ROC, this cannot occur!\n");
   if (reg & (1 << 21))
      printf("    Invalid GTT entry during Vertex Fetch\n");
   if (reg & (1 << 20))
      printf("    Invalid GTT entry during Command Fetch\n");
   if (reg & (1 << 19))
      printf("    Invalid GTT entry during CS\n");
   if (reg & (1 << 18))
      printf("    Invalid GTT entry during Cursor Fetch\n");
   if (reg & (1 << 17))
      printf("    Invalid GTT entry during Overlay Fetch\n");
   if (reg & (1 << 8))
      printf("    Invalid GTT entry during Display B Fetch\n");
   if (reg & (1 << 4))
      printf("    Invalid GTT entry during Display A Fetch\n");
   if (reg & (1 << 1))
      printf("    Valid PTE references illegal memory\n");
   if (reg & (1 << 0))
      printf("    Invalid GTT entry during fetch for host\n");
}

static void
print_snb_fence(struct gen_device_info *devinfo, uint64_t fence)
{
   printf("    %svalid, %c-tiled, pitch: %i, start: 0x%08x, size: %u\n",
          fence & 1 ? "" : "in",
          fence & (1<<1) ? 'y' : 'x',
          (int)(((fence>>32)&0xfff)+1)*128,
          (uint32_t)fence & 0xfffff000,
          (uint32_t)(((fence>>32)&0xfffff000) - (fence&0xfffff000) + 4096));
}

static void
print_i965_fence(struct gen_device_info *devinfo, uint64_t fence)
{
   printf("    %svalid, %c-tiled, pitch: %i, start: 0x%08x, size: %u\n",
          fence & 1 ? "" : "in",
          fence & (1<<1) ? 'y' : 'x',
          (int)(((fence>>2)&0x1ff)+1)*128,
          (uint32_t)fence & 0xfffff000,
          (uint32_t)(((fence>>32)&0xfffff000) - (fence&0xfffff000) + 4096));
}

static void
print_fence(struct gen_device_info *devinfo, uint64_t fence)
{
   if (devinfo->gen == 6 || devinfo->gen == 7) {
      return print_snb_fence(devinfo, fence);
   } else if (devinfo->gen == 4 || devinfo->gen == 5) {
      return print_i965_fence(devinfo, fence);
   }
}

static void
print_fault_data(struct gen_device_info *devinfo, uint32_t data1, uint32_t data0)
{
   uint64_t address;

   if (devinfo->gen < 8)
      return;

   address = ((uint64_t)(data0) << 12) | ((uint64_t)data1 & 0xf) << 44;
   printf("    Address 0x%016" PRIx64 " %s\n", address,
          data1 & (1 << 4) ? "GGTT" : "PPGTT");
}

#define MAX_RINGS 10 /* I really hope this never... */

#define CSI "\e["
#define NORMAL       CSI "0m"

struct program {
   const char *type;
   const char *command;
   uint64_t command_offset;
   uint64_t instruction_base_address;
   uint64_t ksp;
};

#define MAX_NUM_PROGRAMS 4096
static struct program programs[MAX_NUM_PROGRAMS];
static int num_programs = 0;

static void decode(struct gen_spec *spec,
                   const char *buffer_name,
                   const char *ring_name,
                   uint64_t gtt_offset,
                   uint32_t *data,
                   int *count)
{
   uint32_t *p, *end = (data + *count);
   int length;
   struct gen_group *inst;
   uint64_t current_instruction_base_address = 0;

   for (p = data; p < end; p += length) {
      const char *color = option_full_decode ? BLUE_HEADER : NORMAL,
         *reset_color = NORMAL;
      uint64_t offset = gtt_offset + 4 * (p - data);

      inst = gen_spec_find_instruction(spec, p);
      length = gen_group_get_length(inst, p);
      assert(inst == NULL || length > 0);
      length = MAX2(1, length);
      if (inst == NULL) {
         printf("unknown instruction %08x\n", p[0]);
         continue;
      }
      if (option_color == COLOR_NEVER) {
         color = "";
         reset_color = "";
      }

      printf("%s0x%08"PRIx64":  0x%08x:  %-80s%s\n",
             color, offset, p[0], gen_group_get_name(inst), reset_color);

      gen_print_group(stdout, inst, offset, p,
                      option_color == COLOR_ALWAYS);

      if (strcmp(inst->name, "MI_BATCH_BUFFER_END") == 0)
         break;

      if (strcmp(inst->name, "STATE_BASE_ADDRESS") == 0) {
         struct gen_field_iterator iter;
         gen_field_iterator_init(&iter, inst, p, false);

         while (gen_field_iterator_next(&iter)) {
            if (strcmp(iter.name, "Instruction Base Address") == 0) {
               current_instruction_base_address = strtol(iter.value, NULL, 16);
            }
         }
      } else if (strcmp(inst->name,   "WM_STATE") == 0 ||
                 strcmp(inst->name, "3DSTATE_PS") == 0 ||
                 strcmp(inst->name, "3DSTATE_WM") == 0) {
         struct gen_field_iterator iter;
         gen_field_iterator_init(&iter, inst, p, false);
         uint64_t ksp[3] = {0, 0, 0};
         bool enabled[3] = {false, false, false};

         while (gen_field_iterator_next(&iter)) {
            if (strncmp(iter.name, "Kernel Start Pointer ",
                        strlen("Kernel Start Pointer ")) == 0) {
               int idx = iter.name[strlen("Kernel Start Pointer ")] - '0';
               ksp[idx] = strtol(iter.value, NULL, 16);
            } else if (strcmp(iter.name, "8 Pixel Dispatch Enable") == 0) {
               enabled[0] = strcmp(iter.value, "true") == 0;
            } else if (strcmp(iter.name, "16 Pixel Dispatch Enable") == 0) {
               enabled[1] = strcmp(iter.value, "true") == 0;
            } else if (strcmp(iter.name, "32 Pixel Dispatch Enable") == 0) {
               enabled[2] = strcmp(iter.value, "true") == 0;
            }
         }

         /* FINISHME: Broken for multi-program WM_STATE,
          * which Mesa does not use
          */
         if (enabled[0] + enabled[1] + enabled[2] == 1) {
            const char *type = enabled[0] ? "SIMD8 fragment shader" :
                               enabled[1] ? "SIMD16 fragment shader" :
                               enabled[2] ? "SIMD32 fragment shader" : NULL;

            programs[num_programs++] = (struct program) {
               .type = type,
               .command = inst->name,
               .command_offset = offset,
               .instruction_base_address = current_instruction_base_address,
               .ksp = ksp[0],
            };
         } else {
            if (enabled[0]) /* SIMD8 */ {
               programs[num_programs++] = (struct program) {
                  .type = "SIMD8 fragment shader",
                  .command = inst->name,
                  .command_offset = offset,
                  .instruction_base_address = current_instruction_base_address,
                  .ksp = ksp[0], /* SIMD8 shader is specified by ksp[0] */
               };
            }
            if (enabled[1]) /* SIMD16 */ {
               programs[num_programs++] = (struct program) {
                  .type = "SIMD16 fragment shader",
                  .command = inst->name,
                  .command_offset = offset,
                  .instruction_base_address = current_instruction_base_address,
                  .ksp = ksp[2], /* SIMD16 shader is specified by ksp[2] */
               };
            }
            if (enabled[2]) /* SIMD32 */ {
               programs[num_programs++] = (struct program) {
                  .type = "SIMD32 fragment shader",
                  .command = inst->name,
                  .command_offset = offset,
                  .instruction_base_address = current_instruction_base_address,
                  .ksp = ksp[1], /* SIMD32 shader is specified by ksp[1] */
               };
            }
         }
      } else if (strcmp(inst->name,   "VS_STATE") == 0 ||
                 strcmp(inst->name,   "GS_STATE") == 0 ||
                 strcmp(inst->name,   "SF_STATE") == 0 ||
                 strcmp(inst->name, "CLIP_STATE") == 0 ||
                 strcmp(inst->name, "3DSTATE_DS") == 0 ||
                 strcmp(inst->name, "3DSTATE_HS") == 0 ||
                 strcmp(inst->name, "3DSTATE_GS") == 0 ||
                 strcmp(inst->name, "3DSTATE_VS") == 0) {
         struct gen_field_iterator iter;
         gen_field_iterator_init(&iter, inst, p, false);
         uint64_t ksp = 0;
         bool is_simd8 = false; /* vertex shaders on Gen8+ only */
         bool is_enabled = true;

         while (gen_field_iterator_next(&iter)) {
            if (strcmp(iter.name, "Kernel Start Pointer") == 0) {
               ksp = strtol(iter.value, NULL, 16);
            } else if (strcmp(iter.name, "SIMD8 Dispatch Enable") == 0) {
               is_simd8 = strcmp(iter.value, "true") == 0;
            } else if (strcmp(iter.name, "Dispatch Enable") == 0) {
               is_simd8 = strcmp(iter.value, "SIMD8") == 0;
            } else if (strcmp(iter.name, "Enable") == 0) {
               is_enabled = strcmp(iter.value, "true") == 0;
            }
         }

         const char *type =
            strcmp(inst->name,   "VS_STATE") == 0 ? "vertex shader" :
            strcmp(inst->name,   "GS_STATE") == 0 ? "geometry shader" :
            strcmp(inst->name,   "SF_STATE") == 0 ? "strips and fans shader" :
            strcmp(inst->name, "CLIP_STATE") == 0 ? "clip shader" :
            strcmp(inst->name, "3DSTATE_DS") == 0 ? "tessellation control shader" :
            strcmp(inst->name, "3DSTATE_HS") == 0 ? "tessellation evaluation shader" :
            strcmp(inst->name, "3DSTATE_VS") == 0 ? (is_simd8 ? "SIMD8 vertex shader" : "vec4 vertex shader") :
            strcmp(inst->name, "3DSTATE_GS") == 0 ? (is_simd8 ? "SIMD8 geometry shader" : "vec4 geometry shader") :
            NULL;

         if (is_enabled) {
            programs[num_programs++] = (struct program) {
               .type = type,
               .command = inst->name,
               .command_offset = offset,
               .instruction_base_address = current_instruction_base_address,
               .ksp = ksp,
            };
         }
      }

      assert(num_programs < MAX_NUM_PROGRAMS);
   }
}

static int zlib_inflate(uint32_t **ptr, int len)
{
   struct z_stream_s zstream;
   void *out;
   const uint32_t out_size = 128*4096;  /* approximate obj size */

   memset(&zstream, 0, sizeof(zstream));

   zstream.next_in = (unsigned char *)*ptr;
   zstream.avail_in = 4*len;

   if (inflateInit(&zstream) != Z_OK)
      return 0;

   out = malloc(out_size);
   zstream.next_out = out;
   zstream.avail_out = out_size;

   do {
      switch (inflate(&zstream, Z_SYNC_FLUSH)) {
      case Z_STREAM_END:
         goto end;
      case Z_OK:
         break;
      default:
         inflateEnd(&zstream);
         return 0;
      }

      if (zstream.avail_out)
         break;

      out = realloc(out, 2*zstream.total_out);
      if (out == NULL) {
         inflateEnd(&zstream);
         return 0;
      }

      zstream.next_out = (unsigned char *)out + zstream.total_out;
      zstream.avail_out = zstream.total_out;
   } while (1);
 end:
   inflateEnd(&zstream);
   free(*ptr);
   *ptr = out;
   return zstream.total_out / 4;
}

static int ascii85_decode(const char *in, uint32_t **out, bool inflate)
{
   int len = 0, size = 1024;

   *out = realloc(*out, sizeof(uint32_t)*size);
   if (*out == NULL)
      return 0;

   while (*in >= '!' && *in <= 'z') {
      uint32_t v = 0;

      if (len == size) {
         size *= 2;
         *out = realloc(*out, sizeof(uint32_t)*size);
         if (*out == NULL)
            return 0;
      }

      if (*in == 'z') {
         in++;
      } else {
         v += in[0] - 33; v *= 85;
         v += in[1] - 33; v *= 85;
         v += in[2] - 33; v *= 85;
         v += in[3] - 33; v *= 85;
         v += in[4] - 33;
         in += 5;
      }
      (*out)[len++] = v;
   }

   if (!inflate)
      return len;

   return zlib_inflate(out, len);
}

static void
read_data_file(FILE *file)
{
   struct gen_spec *spec = NULL;
   uint32_t *data = NULL;
   long long unsigned fence;
   int data_size = 0, count = 0, line_number = 0, matched;
   char *line = NULL;
   size_t line_size;
   uint32_t offset, value;
   uint64_t gtt_offset = 0, new_gtt_offset;
   const char *buffer_name = "batch buffer";
   char *ring_name = NULL;
   struct gen_device_info devinfo;
   struct gen_disasm *disasm = NULL;

   while (getline(&line, &line_size, file) > 0) {
      char *new_ring_name = NULL;
      char *dashes;
      line_number++;

      if (sscanf(line, "%m[^ ] command stream\n", &new_ring_name) > 0) {
         free(ring_name);
         ring_name = new_ring_name;
      }

      dashes = strstr(line, "---");
      if (dashes) {
         uint32_t lo, hi;
         char *new_ring_name = malloc(dashes - line);
         strncpy(new_ring_name, line, dashes - line);
         new_ring_name[dashes - line - 1] = '\0';

         printf("%s", line);

         matched = sscanf(dashes, "--- gtt_offset = 0x%08x %08x\n",
                          &hi, &lo);
         if (matched > 0) {
            new_gtt_offset = hi;
            if (matched == 2) {
               new_gtt_offset <<= 32;
               new_gtt_offset |= lo;
            }

            decode(spec,
                   buffer_name, ring_name,
                   gtt_offset, data, &count);
            gtt_offset = new_gtt_offset;
            free(ring_name);
            ring_name = new_ring_name;
            buffer_name = "batch buffer";
            continue;
         }

         matched = sscanf(dashes, "--- ringbuffer = 0x%08x %08x\n",
                          &hi, &lo);
         if (matched > 0) {
            new_gtt_offset = hi;
            if (matched == 2) {
               new_gtt_offset <<= 32;
               new_gtt_offset |= lo;
            }

            decode(spec,
                   buffer_name, ring_name,
                   gtt_offset, data, &count);
            gtt_offset = new_gtt_offset;
            free(ring_name);
            ring_name = new_ring_name;
            buffer_name = "ring buffer";
            continue;
         }

         matched = sscanf(dashes, "--- HW Context = 0x%08x %08x\n",
                          &hi, &lo);
         if (matched > 0) {
            new_gtt_offset = hi;
            if (matched == 2) {
               new_gtt_offset <<= 32;
               new_gtt_offset |= lo;
            }

            decode(spec,
                   buffer_name, ring_name,
                   gtt_offset, data, &count);
            gtt_offset = new_gtt_offset;
            free(ring_name);
            ring_name = new_ring_name;
            buffer_name = "HW Context";
            continue;
         }

         matched = sscanf(dashes, "--- user = 0x%08x %08x\n",
                          &hi, &lo);
         if (matched > 0) {
            new_gtt_offset = hi;
            if (matched == 2) {
               new_gtt_offset <<= 32;
               new_gtt_offset |= lo;
            }

            gtt_offset = new_gtt_offset;
            free(ring_name);
            ring_name = new_ring_name;
            buffer_name = "user";
            continue;
         }
      }

      if (line[0] == ':' || line[0] == '~') {
         count = ascii85_decode(line+1, &data, line[0] == ':');
         if (count == 0) {
            fprintf(stderr, "ASCII85 decode failed.\n");
            exit(EXIT_FAILURE);
         }

         if (strcmp(buffer_name, "user") == 0) {
            printf("Disassembly of programs in instruction buffer at "
                   "0x%08"PRIx64":\n", gtt_offset);
            for (int i = 0; i < num_programs; i++) {
               if (programs[i].instruction_base_address == gtt_offset) {
                    printf("\n%s (specified by %s at batch offset "
                           "0x%08"PRIx64") at offset 0x%08"PRIx64"\n",
                           programs[i].type,
                           programs[i].command,
                           programs[i].command_offset,
                           programs[i].ksp);
                    gen_disasm_disassemble(disasm, data, programs[i].ksp,
                                           stdout);
               }
            }
         } else {
            decode(spec,
                   buffer_name, ring_name,
                   gtt_offset, data, &count);
         }
         continue;
      }

      matched = sscanf(line, "%08x : %08x", &offset, &value);
      if (matched != 2) {
         uint32_t reg, reg2;

         /* display reg section is after the ringbuffers, don't mix them */
         decode(spec,
                buffer_name, ring_name,
                gtt_offset, data, &count);

         printf("%s", line);

         matched = sscanf(line, "PCI ID: 0x%04x\n", &reg);
         if (matched == 0)
            matched = sscanf(line, " PCI ID: 0x%04x\n", &reg);
         if (matched == 0) {
            const char *pci_id_start = strstr(line, "PCI ID");
            if (pci_id_start)
               matched = sscanf(pci_id_start, "PCI ID: 0x%04x\n", &reg);
         }
         if (matched == 1) {
            if (!gen_get_device_info(reg, &devinfo)) {
               printf("Unable to identify devid=%x\n", reg);
               exit(EXIT_FAILURE);
            }

            disasm = gen_disasm_create(reg);

            printf("Detected GEN%i chipset\n", devinfo.gen);

            if (xml_path == NULL)
               spec = gen_spec_load(&devinfo);
            else
               spec = gen_spec_load_from_path(&devinfo, xml_path);
         }

         matched = sscanf(line, "  CTL: 0x%08x\n", &reg);
         if (matched == 1) {
            print_register(spec,
                           register_name_from_ring(ctl_registers,
                                                   ARRAY_SIZE(ctl_registers),
                                                   ring_name), reg);
         }

         matched = sscanf(line, "  HEAD: 0x%08x\n", &reg);
         if (matched == 1)
            print_head(reg);

         matched = sscanf(line, "  ACTHD: 0x%08x\n", &reg);
         if (matched == 1) {
            print_register(spec,
                           register_name_from_ring(acthd_registers,
                                                   ARRAY_SIZE(acthd_registers),
                                                   ring_name), reg);
         }

         matched = sscanf(line, "  PGTBL_ER: 0x%08x\n", &reg);
         if (matched == 1 && reg)
            print_pgtbl_err(reg, &devinfo);

         matched = sscanf(line, "  ERROR: 0x%08x\n", &reg);
         if (matched == 1 && reg) {
            print_register(spec, "GFX_ARB_ERROR_RPT", reg);
         }

         matched = sscanf(line, "  INSTDONE: 0x%08x\n", &reg);
         if (matched == 1) {
            const char *reg_name =
               instdone_register_for_ring(&devinfo, ring_name);
            if (reg_name)
               print_register(spec, reg_name, reg);
         }

         matched = sscanf(line, "  INSTDONE1: 0x%08x\n", &reg);
         if (matched == 1)
            print_register(spec, "INSTDONE_1", reg);

         matched = sscanf(line, "  fence[%i] = %Lx\n", &reg, &fence);
         if (matched == 2)
            print_fence(&devinfo, fence);

         matched = sscanf(line, "  FAULT_REG: 0x%08x\n", &reg);
         if (matched == 1 && reg) {
            const char *reg_name =
               register_name_from_ring(fault_registers,
                                       ARRAY_SIZE(fault_registers),
                                       ring_name);
            if (reg_name == NULL)
               reg_name = "FAULT_REG";
            print_register(spec, reg_name, reg);
         }

         matched = sscanf(line, "  FAULT_TLB_DATA: 0x%08x 0x%08x\n", &reg, &reg2);
         if (matched == 2)
            print_fault_data(&devinfo, reg, reg2);

         continue;
      }

      count++;

      if (count > data_size) {
         data_size = data_size ? data_size * 2 : 1024;
         data = realloc(data, data_size * sizeof (uint32_t));
         if (data == NULL) {
            fprintf(stderr, "Out of memory.\n");
            exit(EXIT_FAILURE);
         }
      }

      data[count-1] = value;
   }

   decode(spec,
          buffer_name, ring_name,
          gtt_offset, data, &count);

   gen_disasm_destroy(disasm);
   free(data);
   free(line);
   free(ring_name);
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
           "Parse an Intel GPU i915_error_state.\n"
           "With no FILE, debugfs-dri-directory is probed for in /debug and \n"
           "/sys/kernel/debug.  Otherwise, it may be specified. If a file is given,\n"
           "it is parsed as an GPU dump in the format of /debug/dri/0/i915_error_state.\n\n"
           "      --help          display this help and exit\n"
           "      --headers       decode only command headers\n"
           "      --color[=WHEN]  colorize the output; WHEN can be 'auto' (default\n"
           "                        if omitted), 'always', or 'never'\n"
           "      --no-pager      don't launch pager\n"
           "      --no-offsets    don't print instruction offsets\n"
           "      --xml=DIR       load hardware xml description from directory DIR\n",
           progname);
}

int
main(int argc, char *argv[])
{
   FILE *file;
   const char *path;
   struct stat st;
   int c, i, error;
   bool help = false, pager = true;
   const struct option aubinator_opts[] = {
      { "help",       no_argument,       (int *) &help,                 true },
      { "no-pager",   no_argument,       (int *) &pager,                false },
      { "no-offsets", no_argument,       (int *) &option_print_offsets, false },
      { "headers",    no_argument,       (int *) &option_full_decode,   false },
      { "color",      required_argument, NULL,                          'c' },
      { "xml",        required_argument, NULL,                          'x' },
      { NULL,         0,                 NULL,                          0 }
   };

   i = 0;
   while ((c = getopt_long(argc, argv, "", aubinator_opts, &i)) != -1) {
      switch (c) {
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
      exit(EXIT_SUCCESS);
   }

   if (optind >= argc) {
      if (isatty(0)) {
         path = "/sys/class/drm/card0/error";
         error = stat(path, &st);
         if (error != 0) {
            path = "/debug/dri";
            error = stat(path, &st);
         }
         if (error != 0) {
            path = "/sys/kernel/debug/dri";
            error = stat(path, &st);
         }
         if (error != 0) {
            errx(1,
                 "Couldn't find i915 debugfs directory.\n\n"
                 "Is debugfs mounted? You might try mounting it with a command such as:\n\n"
                 "\tsudo mount -t debugfs debugfs /sys/kernel/debug\n");
         }
      } else {
         read_data_file(stdin);
         exit(EXIT_SUCCESS);
      }
   } else {
      path = argv[optind];
      error = stat(path, &st);
      if (error != 0) {
         fprintf(stderr, "Error opening %s: %s\n",
                 path, strerror(errno));
         exit(EXIT_FAILURE);
      }
   }

   if (option_color == COLOR_AUTO)
      option_color = isatty(1) ? COLOR_ALWAYS : COLOR_NEVER;

   if (isatty(1) && pager)
      setup_pager();

   if (S_ISDIR(st.st_mode)) {
      int ret;
      char *filename;

      ret = asprintf(&filename, "%s/i915_error_state", path);
      assert(ret > 0);
      file = fopen(filename, "r");
      if (!file) {
         int minor;
         free(filename);
         for (minor = 0; minor < 64; minor++) {
            ret = asprintf(&filename, "%s/%d/i915_error_state", path, minor);
            assert(ret > 0);

            file = fopen(filename, "r");
            if (file)
               break;

            free(filename);
         }
      }
      if (!file) {
         fprintf(stderr, "Failed to find i915_error_state beneath %s\n",
                 path);
         return EXIT_FAILURE;
      }
   } else {
      file = fopen(path, "r");
      if (!file) {
         fprintf(stderr, "Failed to open %s: %s\n",
                 path, strerror(errno));
         return EXIT_FAILURE;
      }
   }

   read_data_file(file);
   fclose(file);

   /* close the stdout which is opened to write the output */
   fflush(stdout);
   close(1);
   wait(NULL);

   if (xml_path)
      free(xml_path);

   return EXIT_SUCCESS;
}

/* vim: set ts=8 sw=8 tw=0 cino=:0,(0 noet :*/
