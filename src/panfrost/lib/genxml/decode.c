/*
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * Copyright (C) 2017-2019 Connor Abbott
 * Copyright (C) 2019 Collabora, Ltd.
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
 */

#include "decode.h"
#include <ctype.h>
#include <errno.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <genxml/gen_macros.h>
#include <sys/mman.h>

#include "compiler/bifrost/disassemble.h"
#include "compiler/valhall/disassemble.h"
#include "midgard/disassemble.h"
#include "util/set.h"

#if PAN_ARCH <= 5
/* Midgard's tiler descriptor is embedded within the
 * larger FBD */

static void
pandecode_midgard_tiler_descriptor(const struct mali_tiler_context_packed *tp,
                                   const struct mali_tiler_weights_packed *wp)
{
   pan_unpack(tp, TILER_CONTEXT, t);
   DUMP_UNPACKED(TILER_CONTEXT, t, "Tiler:\n");

   /* We've never seen weights used in practice, but they exist */
   pan_unpack(wp, TILER_WEIGHTS, w);
   bool nonzero_weights = false;

   nonzero_weights |= w.weight0 != 0x0;
   nonzero_weights |= w.weight1 != 0x0;
   nonzero_weights |= w.weight2 != 0x0;
   nonzero_weights |= w.weight3 != 0x0;
   nonzero_weights |= w.weight4 != 0x0;
   nonzero_weights |= w.weight5 != 0x0;
   nonzero_weights |= w.weight6 != 0x0;
   nonzero_weights |= w.weight7 != 0x0;

   if (nonzero_weights)
      DUMP_UNPACKED(TILER_WEIGHTS, w, "Tiler Weights:\n");
}
#endif

#if PAN_ARCH >= 5
static void
pandecode_render_target(uint64_t gpu_va, unsigned gpu_id,
                        const struct MALI_FRAMEBUFFER_PARAMETERS *fb)
{
   pandecode_log("Color Render Targets @%" PRIx64 ":\n", gpu_va);
   pandecode_indent++;

   for (int i = 0; i < (fb->render_target_count); i++) {
      mali_ptr rt_va = gpu_va + i * pan_size(RENDER_TARGET);
      const struct mali_render_target_packed *PANDECODE_PTR_VAR(
         rtp, (mali_ptr)rt_va);
      DUMP_CL(RENDER_TARGET, rtp, "Color Render Target %d:\n", i);
   }

   pandecode_indent--;
   pandecode_log("\n");
}
#endif

#if PAN_ARCH >= 6
static void
pandecode_sample_locations(const void *fb)
{
   pan_section_unpack(fb, FRAMEBUFFER, PARAMETERS, params);

   const u16 *PANDECODE_PTR_VAR(samples, params.sample_locations);

   pandecode_log("Sample locations @%" PRIx64 ":\n", params.sample_locations);
   for (int i = 0; i < 33; i++) {
      pandecode_log("  (%d, %d),\n", samples[2 * i] - 128,
                    samples[2 * i + 1] - 128);
   }
}
#endif

struct pandecode_fbd
GENX(pandecode_fbd)(uint64_t gpu_va, bool is_fragment, unsigned gpu_id)
{
   const void *PANDECODE_PTR_VAR(fb, (mali_ptr)gpu_va);
   pan_section_unpack(fb, FRAMEBUFFER, PARAMETERS, params);
   DUMP_UNPACKED(FRAMEBUFFER_PARAMETERS, params, "Parameters:\n");

#if PAN_ARCH >= 6
   pandecode_sample_locations(fb);

   unsigned dcd_size = pan_size(DRAW);
   unsigned job_type_param = 0;

#if PAN_ARCH <= 9
   job_type_param = MALI_JOB_TYPE_FRAGMENT;
#endif

   if (params.pre_frame_0 != MALI_PRE_POST_FRAME_SHADER_MODE_NEVER) {
      const void *PANDECODE_PTR_VAR(dcd,
                                    params.frame_shader_dcds + (0 * dcd_size));
      pan_unpack(dcd, DRAW, draw);
      pandecode_log("Pre frame 0 @%" PRIx64 " (mode=%d):\n",
                    params.frame_shader_dcds, params.pre_frame_0);
      GENX(pandecode_dcd)(&draw, job_type_param, gpu_id);
   }

   if (params.pre_frame_1 != MALI_PRE_POST_FRAME_SHADER_MODE_NEVER) {
      const void *PANDECODE_PTR_VAR(dcd,
                                    params.frame_shader_dcds + (1 * dcd_size));
      pan_unpack(dcd, DRAW, draw);
      pandecode_log("Pre frame 1 @%" PRIx64 ":\n",
                    params.frame_shader_dcds + (1 * dcd_size));
      GENX(pandecode_dcd)(&draw, job_type_param, gpu_id);
   }

   if (params.post_frame != MALI_PRE_POST_FRAME_SHADER_MODE_NEVER) {
      const void *PANDECODE_PTR_VAR(dcd,
                                    params.frame_shader_dcds + (2 * dcd_size));
      pan_unpack(dcd, DRAW, draw);
      pandecode_log("Post frame:\n");
      GENX(pandecode_dcd)(&draw, job_type_param, gpu_id);
   }
#else
   DUMP_SECTION(FRAMEBUFFER, LOCAL_STORAGE, fb, "Local Storage:\n");

   const void *t = pan_section_ptr(fb, FRAMEBUFFER, TILER);
   const void *w = pan_section_ptr(fb, FRAMEBUFFER, TILER_WEIGHTS);
   pandecode_midgard_tiler_descriptor(t, w);
#endif

   pandecode_log("Framebuffer @%" PRIx64 ":\n", gpu_va);
   pandecode_indent++;

   DUMP_UNPACKED(FRAMEBUFFER_PARAMETERS, params, "Parameters:\n");
#if PAN_ARCH >= 6
   if (params.tiler)
      GENX(pandecode_tiler)(params.tiler, gpu_id);
#endif

   pandecode_indent--;
   pandecode_log("\n");

#if PAN_ARCH >= 5
   gpu_va += pan_size(FRAMEBUFFER);

   if (params.has_zs_crc_extension) {
      const struct mali_zs_crc_extension_packed *PANDECODE_PTR_VAR(
         zs_crc, (mali_ptr)gpu_va);
      DUMP_CL(ZS_CRC_EXTENSION, zs_crc, "ZS CRC Extension:\n");
      pandecode_log("\n");

      gpu_va += pan_size(ZS_CRC_EXTENSION);
   }

   if (is_fragment)
      pandecode_render_target(gpu_va, gpu_id, &params);

   return (struct pandecode_fbd){
      .rt_count = params.render_target_count,
      .has_extra = params.has_zs_crc_extension,
   };
#else
   /* Dummy unpack of the padding section to make sure all words are 0.
    * No need to call print here since the section is supposed to be empty.
    */
   pan_section_unpack(fb, FRAMEBUFFER, PADDING_1, padding1);
   pan_section_unpack(fb, FRAMEBUFFER, PADDING_2, padding2);

   return (struct pandecode_fbd){
      .rt_count = 1,
   };
#endif
}

#if PAN_ARCH >= 5
mali_ptr
GENX(pandecode_blend)(void *descs, int rt_no, mali_ptr frag_shader)
{
   pan_unpack(descs + (rt_no * pan_size(BLEND)), BLEND, b);
   DUMP_UNPACKED(BLEND, b, "Blend RT %d:\n", rt_no);
#if PAN_ARCH >= 6
   if (b.internal.mode != MALI_BLEND_MODE_SHADER)
      return 0;

   return (frag_shader & 0xFFFFFFFF00000000ULL) | b.internal.shader.pc;
#else
   return b.blend_shader ? (b.shader_pc & ~0xf) : 0;
#endif
}
#endif

#if PAN_ARCH <= 7
static void
pandecode_texture_payload(mali_ptr payload, enum mali_texture_dimension dim,
                          enum mali_texture_layout layout, bool manual_stride,
                          uint8_t levels, uint16_t nr_samples,
                          uint16_t array_size)
{
   pandecode_log(".payload = {\n");
   pandecode_indent++;

   /* A bunch of bitmap pointers follow.
    * We work out the correct number,
    * based on the mipmap/cubemap
    * properties, but dump extra
    * possibilities to futureproof */

   int bitmap_count = levels;

   /* Miptree for each face */
   if (dim == MALI_TEXTURE_DIMENSION_CUBE)
      bitmap_count *= 6;

   /* Array of layers */
   bitmap_count *= nr_samples;

   /* Array of textures */
   bitmap_count *= array_size;

   /* Stride for each element */
   if (manual_stride)
      bitmap_count *= 2;

   mali_ptr *pointers_and_strides =
      pandecode_fetch_gpu_mem(payload, sizeof(mali_ptr) * bitmap_count);
   for (int i = 0; i < bitmap_count; ++i) {
      /* How we dump depends if this is a stride or a pointer */

      if (manual_stride && (i & 1)) {
         /* signed 32-bit snuck in as a 64-bit pointer */
         uint64_t stride_set = pointers_and_strides[i];
         int32_t row_stride = stride_set;
         int32_t surface_stride = stride_set >> 32;
         pandecode_log(
            "(mali_ptr) %d /* surface stride */ %d /* row stride */, \n",
            surface_stride, row_stride);
      } else {
         char *a = pointer_as_memory_reference(pointers_and_strides[i]);
         pandecode_log("%s, \n", a);
         free(a);
      }
   }

   pandecode_indent--;
   pandecode_log("},\n");
}
#endif

#if PAN_ARCH <= 5
void
GENX(pandecode_texture)(mali_ptr u, unsigned tex)
{
   const uint8_t *cl = pandecode_fetch_gpu_mem(u, pan_size(TEXTURE));

   pan_unpack(cl, TEXTURE, temp);
   DUMP_UNPACKED(TEXTURE, temp, "Texture:\n")

   pandecode_indent++;
   unsigned nr_samples =
      temp.dimension == MALI_TEXTURE_DIMENSION_3D ? 1 : temp.sample_count;
   pandecode_texture_payload(u + pan_size(TEXTURE), temp.dimension,
                             temp.texel_ordering, temp.manual_stride,
                             temp.levels, nr_samples, temp.array_size);
   pandecode_indent--;
}
#else
void
GENX(pandecode_texture)(const void *cl, unsigned tex)
{
   pan_unpack(cl, TEXTURE, temp);
   DUMP_UNPACKED(TEXTURE, temp, "Texture:\n")

   pandecode_indent++;

#if PAN_ARCH >= 9
   int plane_count = temp.levels * temp.array_size;

   /* Miptree for each face */
   if (temp.dimension == MALI_TEXTURE_DIMENSION_CUBE)
      plane_count *= 6;

   for (unsigned i = 0; i < plane_count; ++i)
      DUMP_ADDR(PLANE, temp.surfaces + i * pan_size(PLANE), "Plane %u:\n", i);
#else
   unsigned nr_samples =
      temp.dimension == MALI_TEXTURE_DIMENSION_3D ? 1 : temp.sample_count;

   pandecode_texture_payload(temp.surfaces, temp.dimension, temp.texel_ordering,
                             true, temp.levels, nr_samples, temp.array_size);
#endif
   pandecode_indent--;
}
#endif

#if PAN_ARCH >= 6
void
GENX(pandecode_tiler)(mali_ptr gpu_va, unsigned gpu_id)
{
   pan_unpack(PANDECODE_PTR(gpu_va, void), TILER_CONTEXT, t);

   if (t.heap) {
      pan_unpack(PANDECODE_PTR(t.heap, void), TILER_HEAP, h);
      DUMP_UNPACKED(TILER_HEAP, h, "Tiler Heap:\n");
   }

   DUMP_UNPACKED(TILER_CONTEXT, t, "Tiler Context @%" PRIx64 ":\n", gpu_va);
}
#endif

#if PAN_ARCH >= 9
void
GENX(pandecode_fau)(mali_ptr addr, unsigned count, const char *name)
{
   if (count == 0)
      return;

   const uint32_t *PANDECODE_PTR_VAR(raw, addr);

   pandecode_validate_buffer(addr, count * 8);

   fprintf(pandecode_dump_stream, "%s @%" PRIx64 ":\n", name, addr);
   for (unsigned i = 0; i < count; ++i) {
      fprintf(pandecode_dump_stream, "  %08X %08X\n", raw[2 * i],
              raw[2 * i + 1]);
   }
   fprintf(pandecode_dump_stream, "\n");
}

mali_ptr
GENX(pandecode_shader)(mali_ptr addr, const char *label, unsigned gpu_id)
{
   MAP_ADDR(SHADER_PROGRAM, addr, cl);
   pan_unpack(cl, SHADER_PROGRAM, desc);

   assert(desc.type == 8);

   DUMP_UNPACKED(SHADER_PROGRAM, desc, "%s Shader @%" PRIx64 ":\n", label,
                 addr);
   pandecode_shader_disassemble(desc.binary, gpu_id);
   return desc.binary;
}

static void
pandecode_resources(mali_ptr addr, unsigned size)
{
   const uint8_t *cl = pandecode_fetch_gpu_mem(addr, size);
   assert((size % 0x20) == 0);

   for (unsigned i = 0; i < size; i += 0x20) {
      unsigned type = (cl[i] & 0xF);

      switch (type) {
      case MALI_DESCRIPTOR_TYPE_SAMPLER:
         DUMP_CL(SAMPLER, cl + i, "Sampler @%" PRIx64 ":\n", addr + i);
         break;
      case MALI_DESCRIPTOR_TYPE_TEXTURE:
         pandecode_log("Texture @%" PRIx64 "\n", addr + i);
         GENX(pandecode_texture)(cl + i, i);
         break;
      case MALI_DESCRIPTOR_TYPE_ATTRIBUTE:
         DUMP_CL(ATTRIBUTE, cl + i, "Attribute @%" PRIx64 ":\n", addr + i);
         break;
      case MALI_DESCRIPTOR_TYPE_BUFFER:
         DUMP_CL(BUFFER, cl + i, "Buffer @%" PRIx64 ":\n", addr + i);
         break;
      default:
         fprintf(pandecode_dump_stream, "Unknown descriptor type %X\n", type);
         break;
      }
   }
}

void
GENX(pandecode_resource_tables)(mali_ptr addr, const char *label)
{
   unsigned count = addr & 0x3F;
   addr = addr & ~0x3F;

   const uint8_t *cl =
      pandecode_fetch_gpu_mem(addr, MALI_RESOURCE_LENGTH * count);

   for (unsigned i = 0; i < count; ++i) {
      pan_unpack(cl + i * MALI_RESOURCE_LENGTH, RESOURCE, entry);
      DUMP_UNPACKED(RESOURCE, entry, "Entry %u @%" PRIx64 ":\n", i,
                    addr + i * MALI_RESOURCE_LENGTH);

      pandecode_indent += 2;
      if (entry.address)
         pandecode_resources(entry.address, entry.size);
      pandecode_indent -= 2;
   }
}

void
GENX(pandecode_depth_stencil)(mali_ptr addr)
{
   MAP_ADDR(DEPTH_STENCIL, addr, cl);
   pan_unpack(cl, DEPTH_STENCIL, desc);
   DUMP_UNPACKED(DEPTH_STENCIL, desc, "Depth/stencil");
}

void
GENX(pandecode_shader_environment)(const struct MALI_SHADER_ENVIRONMENT *p,
                                   unsigned gpu_id)
{
   if (p->shader)
      GENX(pandecode_shader)(p->shader, "Shader", gpu_id);

   if (p->resources)
      GENX(pandecode_resource_tables)(p->resources, "Resources");

   if (p->thread_storage)
      DUMP_ADDR(LOCAL_STORAGE, p->thread_storage, "Local Storage:\n");

   if (p->fau)
      GENX(pandecode_fau)(p->fau, p->fau_count, "FAU");
}

void
GENX(pandecode_blend_descs)(mali_ptr blend, unsigned count,
                            mali_ptr frag_shader, unsigned gpu_id)
{
   for (unsigned i = 0; i < count; ++i) {
      struct mali_blend_packed *PANDECODE_PTR_VAR(blend_descs, blend);

      mali_ptr blend_shader =
         GENX(pandecode_blend)(blend_descs, i, frag_shader);
      if (blend_shader) {
         fprintf(pandecode_dump_stream, "Blend shader %u @%" PRIx64 "", i,
                 blend_shader);
         pandecode_shader_disassemble(blend_shader, gpu_id);
      }
   }
}

void
GENX(pandecode_dcd)(const struct MALI_DRAW *p, unsigned unused, unsigned gpu_id)
{
   mali_ptr frag_shader = 0;

   GENX(pandecode_depth_stencil)(p->depth_stencil);
   GENX(pandecode_blend_descs)(p->blend, p->blend_count, frag_shader, gpu_id);
   GENX(pandecode_shader_environment)(&p->shader, gpu_id);
   DUMP_UNPACKED(DRAW, *p, "Draw:\n");
}
#endif
