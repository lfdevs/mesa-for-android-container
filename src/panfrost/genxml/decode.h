/*
 * Copyright (C) 2017-2019 Lyude Paul
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_DECODE_H__
#define __PAN_DECODE_H__

#include "genxml/gen_macros.h"
#include "util/rb_tree.h"
#include "util/simple_mtx.h"
#include "util/u_dynarray.h"

#include "wrap.h"

struct pandecode_context {
   int id; /* only used for the filename */
   FILE *dump_stream;
   unsigned indent;
   struct rb_tree mmap_tree;
   struct util_dynarray ro_mappings;
   int dump_frame_count;
   simple_mtx_t lock;

   /* On CSF context, set to true if the root CS ring buffer
    * is managed in userspace. The blob does that, and mesa might use
    * usermode queues too at some point.
    */
   bool usermode_queue;
};

void pandecode_dump_file_open(struct pandecode_context *ctx);

struct pandecode_mapped_memory {
   struct rb_node node;
   size_t length;
   void *addr;
   uint64_t gpu_va;
   bool ro;
   char name[32];
};

char *pointer_as_memory_reference(struct pandecode_context *ctx, uint64_t ptr);

struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing(struct pandecode_context *ctx,
                                         uint64_t addr);

void pandecode_map_read_write(struct pandecode_context *ctx);

void pandecode_dump_mappings(struct pandecode_context *ctx);

static inline void *
__pandecode_fetch_gpu_mem(struct pandecode_context *ctx, uint64_t gpu_va,
                          size_t size, int line, const char *filename)
{
   const struct pandecode_mapped_memory *mem =
      pandecode_find_mapped_gpu_mem_containing(ctx, gpu_va);

   if (!mem) {
      fprintf(stderr, "Access to unknown memory %" PRIx64 " in %s:%d\n", gpu_va,
              filename, line);
      fflush(ctx->dump_stream);
      assert(0);
   }

   assert(size + (gpu_va - mem->gpu_va) <= mem->length);

   return mem->addr + gpu_va - mem->gpu_va;
}

#define pandecode_fetch_gpu_mem(ctx, gpu_va, size)                             \
   __pandecode_fetch_gpu_mem(ctx, gpu_va, size, __LINE__, __FILE__)

/* Returns a validated pointer to mapped GPU memory with the given pointer type,
 * size automatically determined from the pointer type
 */
#define PANDECODE_PTR(ctx, gpu_va, type)                                       \
   ((type *)(__pandecode_fetch_gpu_mem(ctx, gpu_va, sizeof(type), __LINE__,    \
                                       __FILE__)))

/* Usage: <variable type> PANDECODE_PTR_VAR(name, gpu_va) */
#define PANDECODE_PTR_VAR(ctx, name, gpu_va)                                   \
   name = __pandecode_fetch_gpu_mem(ctx, gpu_va, sizeof(*name), __LINE__,      \
                                    __FILE__)

void pandecode_validate_buffer(struct pandecode_context *ctx, uint64_t addr,
                               size_t sz);

/* Forward declare for all supported gens to permit thunking */
void pandecode_jc_v4(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                     uint64_t gpu_id);
void pandecode_jc_v5(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                     uint64_t gpu_id);
void pandecode_jc_v6(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                     uint64_t gpu_id);
void pandecode_jc_v7(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                     uint64_t gpu_id);
void pandecode_jc_v9(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                     uint64_t gpu_id);

void pandecode_abort_on_fault_v4(struct pandecode_context *ctx,
                                 uint64_t jc_gpu_va);
void pandecode_abort_on_fault_v5(struct pandecode_context *ctx,
                                 uint64_t jc_gpu_va);
void pandecode_abort_on_fault_v6(struct pandecode_context *ctx,
                                 uint64_t jc_gpu_va);
void pandecode_abort_on_fault_v7(struct pandecode_context *ctx,
                                 uint64_t jc_gpu_va);
void pandecode_abort_on_fault_v9(struct pandecode_context *ctx,
                                 uint64_t jc_gpu_va);

void pandecode_interpret_cs_v10(struct pandecode_context *ctx, uint64_t queue,
                                uint32_t size, uint64_t gpu_id, uint32_t *regs);
void pandecode_cs_binary_v10(struct pandecode_context *ctx, uint64_t bin,
                             uint32_t bin_size);
void pandecode_cs_trace_v10(struct pandecode_context *ctx, uint64_t trace,
                            uint32_t trace_size, uint64_t gpu_id);

void pandecode_interpret_cs_v12(struct pandecode_context *ctx, uint64_t queue,
                                uint32_t size, uint64_t gpu_id, uint32_t *regs);
void pandecode_cs_binary_v12(struct pandecode_context *ctx, uint64_t bin,
                             uint32_t bin_size);
void pandecode_cs_trace_v12(struct pandecode_context *ctx, uint64_t trace,
                            uint32_t trace_size, uint64_t gpu_id);

void pandecode_interpret_cs_v13(struct pandecode_context *ctx, uint64_t queue,
                                uint32_t size, uint64_t gpu_id, uint32_t *regs);
void pandecode_cs_binary_v13(struct pandecode_context *ctx, uint64_t bin,
                             uint32_t bin_size);
void pandecode_cs_trace_v13(struct pandecode_context *ctx, uint64_t trace,
                            uint32_t trace_size, uint64_t gpu_id);

/* Logging infrastructure */
static void
pandecode_make_indent(struct pandecode_context *ctx)
{
   for (unsigned i = 0; i < ctx->indent; ++i)
      fprintf(ctx->dump_stream, "  ");
}

static inline void PRINTFLIKE(2, 3)
   pandecode_log(struct pandecode_context *ctx, const char *format, ...)
{
   va_list ap;

   pandecode_make_indent(ctx);
   va_start(ap, format);
   vfprintf(ctx->dump_stream, format, ap);
   va_end(ap);
}

static inline void PRINTFLIKE(2, 3)
   pandecode_user_msg(struct pandecode_context *ctx, const char *format, ...)
{
   va_list ap;

   simple_mtx_lock(&ctx->lock);
   pandecode_dump_file_open(ctx);
   pandecode_make_indent(ctx);
   va_start(ap, format);
   vfprintf(ctx->dump_stream, format, ap);
   va_end(ap);
   simple_mtx_unlock(&ctx->lock);
}

static inline void
pandecode_log_cont(struct pandecode_context *ctx, const char *format, ...)
{
   va_list ap;

   va_start(ap, format);
   vfprintf(ctx->dump_stream, format, ap);
   va_end(ap);
}

/* Convenience methods */
#define DUMP_UNPACKED(ctx, T, var, ...)                                        \
   {                                                                           \
      pandecode_log(ctx, __VA_ARGS__);                                         \
      pan_print(ctx->dump_stream, T, var, (ctx->indent + 1) * 2);              \
   }

#define DUMP_CL(ctx, T, cl, ...)                                               \
   {                                                                           \
      pan_unpack((MALI_##T##_PACKED_T *)(cl), T, temp);                        \
      DUMP_UNPACKED(ctx, T, temp, __VA_ARGS__);                                \
   }

#define DUMP_SECTION(ctx, A, S, cl, ...)                                       \
   {                                                                           \
      pan_section_unpack(cl, A, S, temp);                                      \
      pandecode_log(ctx, __VA_ARGS__);                                         \
      pan_section_print(ctx->dump_stream, A, S, temp, (ctx->indent + 1) * 2);  \
   }

#define MAP_ADDR(ctx, T, addr, cl)                                             \
   const MALI_##T##_PACKED_T *cl =                                             \
      pandecode_fetch_gpu_mem(ctx, addr, pan_size(T));

#define DUMP_ADDR(ctx, T, addr, ...)                                           \
   {                                                                           \
      MAP_ADDR(ctx, T, addr, cl)                                               \
      DUMP_CL(ctx, T, cl, __VA_ARGS__);                                        \
   }

void pandecode_shader_disassemble(struct pandecode_context *ctx,
                                  uint64_t shader_ptr, uint64_t gpu_id);

#ifdef PAN_ARCH

/* Information about the framebuffer passed back for additional analysis */
struct pandecode_fbd {
   unsigned rt_count;
   bool has_extra;
};

struct pandecode_fbd GENX(pandecode_fbd)(struct pandecode_context *ctx,
                                         uint64_t gpu_va, bool is_fragment,
                                         uint64_t gpu_id);

#if PAN_ARCH >= 9
void GENX(pandecode_dcd)(struct pandecode_context *ctx,
                         const struct MALI_DRAW *p, unsigned unused,
                         uint64_t gpu_id);
#else
void GENX(pandecode_dcd)(struct pandecode_context *ctx,
                         const struct MALI_DRAW *p, enum mali_job_type job_type,
                         uint64_t gpu_id);
#endif

#if PAN_ARCH <= 5
void GENX(pandecode_texture)(struct pandecode_context *ctx, uint64_t u,
                             unsigned tex);
#else
void GENX(pandecode_texture)(struct pandecode_context *ctx,
                             const struct mali_texture_packed *cl,
                             unsigned tex);
#endif

#if PAN_ARCH >= 5
uint64_t GENX(pandecode_blend)(struct pandecode_context *ctx,
                               struct mali_blend_packed *descs, int rt_no,
                               uint64_t frag_shader);
#endif

#if PAN_ARCH >= 6
void GENX(pandecode_tiler)(struct pandecode_context *ctx, uint64_t gpu_va);
#endif

#if PAN_ARCH >= 9
#if PAN_ARCH < 12
void GENX(pandecode_shader_environment)(struct pandecode_context *ctx,
                                        const struct MALI_SHADER_ENVIRONMENT *p,
                                        uint64_t gpu_id);
#endif

void GENX(pandecode_resource_tables)(struct pandecode_context *ctx,
                                     uint64_t addr, const char *label);

void GENX(pandecode_fau)(struct pandecode_context *ctx, uint64_t addr,
                         unsigned count, const char *name);

uint64_t GENX(pandecode_shader)(struct pandecode_context *ctx, uint64_t addr,
                                const char *label, uint64_t gpu_id);

void GENX(pandecode_blend_descs)(struct pandecode_context *ctx, uint64_t blend,
                                 unsigned count, uint64_t frag_shader,
                                 uint64_t gpu_id);

void GENX(pandecode_depth_stencil)(struct pandecode_context *ctx,
                                   uint64_t addr);
#endif

#endif

#endif /* __MMAP_TRACE_H__ */
