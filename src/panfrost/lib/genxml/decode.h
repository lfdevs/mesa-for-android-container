/*
 * Copyright (C) 2017-2019 Lyude Paul
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __PAN_DECODE_H__
#define __PAN_DECODE_H__

#include "genxml/gen_macros.h"
#include "util/rb_tree.h"

#include "wrap.h"

extern FILE *pandecode_dump_stream;
extern unsigned pandecode_indent;

void pandecode_dump_file_open(void);

struct pandecode_mapped_memory {
   struct rb_node node;
   size_t length;
   void *addr;
   uint64_t gpu_va;
   bool ro;
   char name[32];
};

char *pointer_as_memory_reference(uint64_t ptr);

struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing(uint64_t addr);

void pandecode_map_read_write(void);

void pandecode_dump_mappings(void);

static inline void *
__pandecode_fetch_gpu_mem(uint64_t gpu_va, size_t size, int line,
                          const char *filename)
{
   const struct pandecode_mapped_memory *mem =
      pandecode_find_mapped_gpu_mem_containing(gpu_va);

   if (!mem) {
      fprintf(stderr, "Access to unknown memory %" PRIx64 " in %s:%d\n", gpu_va,
              filename, line);
      assert(0);
   }

   assert(size + (gpu_va - mem->gpu_va) <= mem->length);

   return mem->addr + gpu_va - mem->gpu_va;
}

#define pandecode_fetch_gpu_mem(gpu_va, size)                                  \
   __pandecode_fetch_gpu_mem(gpu_va, size, __LINE__, __FILE__)

/* Returns a validated pointer to mapped GPU memory with the given pointer type,
 * size automatically determined from the pointer type
 */
#define PANDECODE_PTR(gpu_va, type)                                            \
   ((type *)(__pandecode_fetch_gpu_mem(gpu_va, sizeof(type), __LINE__,         \
                                       __FILE__)))

/* Usage: <variable type> PANDECODE_PTR_VAR(name, gpu_va) */
#define PANDECODE_PTR_VAR(name, gpu_va)                                        \
   name = __pandecode_fetch_gpu_mem(gpu_va, sizeof(*name), __LINE__, __FILE__)

void pandecode_validate_buffer(mali_ptr addr, size_t sz);

/* Forward declare for all supported gens to permit thunking */
void pandecode_jc_v4(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v5(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v6(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v7(mali_ptr jc_gpu_va, unsigned gpu_id);
void pandecode_jc_v9(mali_ptr jc_gpu_va, unsigned gpu_id);

void pandecode_abort_on_fault_v4(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v5(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v6(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v7(mali_ptr jc_gpu_va);
void pandecode_abort_on_fault_v9(mali_ptr jc_gpu_va);

void pandecode_cs_v10(mali_ptr queue, uint32_t size, unsigned gpu_id,
                      uint32_t *regs);

static inline void
pan_hexdump(FILE *fp, const uint8_t *hex, size_t cnt, bool with_strings)
{
   for (unsigned i = 0; i < cnt; ++i) {
      if ((i & 0xF) == 0)
         fprintf(fp, "%06X  ", i);

      uint8_t v = hex[i];

      if (v == 0 && (i & 0xF) == 0) {
         /* Check if we're starting an aligned run of zeroes */
         unsigned zero_count = 0;

         for (unsigned j = i; j < cnt; ++j) {
            if (hex[j] == 0)
               zero_count++;
            else
               break;
         }

         if (zero_count >= 32) {
            fprintf(fp, "*\n");
            i += (zero_count & ~0xF) - 1;
            continue;
         }
      }

      fprintf(fp, "%02X ", hex[i]);
      if ((i & 0xF) == 0xF && with_strings) {
         fprintf(fp, " | ");
         for (unsigned j = i & ~0xF; j <= i; ++j) {
            uint8_t c = hex[j];
            fputc((c < 32 || c > 128) ? '.' : c, fp);
         }
      }

      if ((i & 0xF) == 0xF)
         fprintf(fp, "\n");
   }

   fprintf(fp, "\n");
}

/* Logging infrastructure */
static void
pandecode_make_indent(void)
{
   for (unsigned i = 0; i < pandecode_indent; ++i)
      fprintf(pandecode_dump_stream, "  ");
}

static void PRINTFLIKE(1, 2) pandecode_log(const char *format, ...)
{
   va_list ap;

   pandecode_make_indent();
   va_start(ap, format);
   vfprintf(pandecode_dump_stream, format, ap);
   va_end(ap);
}

static void
pandecode_log_cont(const char *format, ...)
{
   va_list ap;

   va_start(ap, format);
   vfprintf(pandecode_dump_stream, format, ap);
   va_end(ap);
}

/* Convenience methods */
#define DUMP_UNPACKED(T, var, ...)                                             \
   {                                                                           \
      pandecode_log(__VA_ARGS__);                                              \
      pan_print(pandecode_dump_stream, T, var, (pandecode_indent + 1) * 2);    \
   }

#define DUMP_CL(T, cl, ...)                                                    \
   {                                                                           \
      pan_unpack(cl, T, temp);                                                 \
      DUMP_UNPACKED(T, temp, __VA_ARGS__);                                     \
   }

#define DUMP_SECTION(A, S, cl, ...)                                            \
   {                                                                           \
      pan_section_unpack(cl, A, S, temp);                                      \
      pandecode_log(__VA_ARGS__);                                              \
      pan_section_print(pandecode_dump_stream, A, S, temp,                     \
                        (pandecode_indent + 1) * 2);                           \
   }

#define MAP_ADDR(T, addr, cl)                                                  \
   const uint8_t *cl = pandecode_fetch_gpu_mem(addr, pan_size(T));

#define DUMP_ADDR(T, addr, ...)                                                \
   {                                                                           \
      MAP_ADDR(T, addr, cl)                                                    \
      DUMP_CL(T, cl, __VA_ARGS__);                                             \
   }

void pandecode_shader_disassemble(mali_ptr shader_ptr, unsigned gpu_id);

#ifdef PAN_ARCH

/* Information about the framebuffer passed back for additional analysis */
struct pandecode_fbd {
   unsigned rt_count;
   bool has_extra;
};

struct pandecode_fbd GENX(pandecode_fbd)(uint64_t gpu_va, bool is_fragment,
                                         unsigned gpu_id);

#if PAN_ARCH >= 9
void GENX(pandecode_dcd)(const struct MALI_DRAW *p, unsigned unused,
                         unsigned gpu_id);
#else
void GENX(pandecode_dcd)(const struct MALI_DRAW *p, enum mali_job_type job_type,
                         unsigned gpu_id);
#endif

#if PAN_ARCH <= 5
void GENX(pandecode_texture)(mali_ptr u, unsigned tex);
#else
void GENX(pandecode_texture)(const void *cl, unsigned tex);
#endif

#if PAN_ARCH >= 5
mali_ptr GENX(pandecode_blend)(void *descs, int rt_no, mali_ptr frag_shader);
#endif

#if PAN_ARCH >= 6
void GENX(pandecode_tiler)(mali_ptr gpu_va, unsigned gpu_id);
#endif

#if PAN_ARCH >= 9
void GENX(pandecode_shader_environment)(const struct MALI_SHADER_ENVIRONMENT *p,
                                        unsigned gpu_id);

void GENX(pandecode_resource_tables)(mali_ptr addr, const char *label);

void GENX(pandecode_fau)(mali_ptr addr, unsigned count, const char *name);

mali_ptr GENX(pandecode_shader)(mali_ptr addr, const char *label,
                                unsigned gpu_id);

void GENX(pandecode_blend_descs)(mali_ptr blend, unsigned count,
                                 mali_ptr frag_shader, unsigned gpu_id);

void GENX(pandecode_depth_stencil)(mali_ptr addr);
#endif

#endif

#endif /* __MMAP_TRACE_H__ */
