/*
 * Copyright 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "genxml/gen_macros.h"
#include "lib/pan_encoder.h"
#include "draw_helper.h"

#if (PAN_ARCH == 6 || PAN_ARCH == 7)
struct panlib_draw_info {
   struct {
      uint32_t size;
      uint32_t offset;
   } index;

   struct {
      int32_t raw_offset;
      int32_t base;
      uint32_t count;
   } vertex;

   struct {
      int32_t base;
      uint32_t count;
   } instance;

   struct {
      global struct mali_attribute_buffer_packed *descs;
      global struct libpan_draw_helper_varying_buf_info *info;
   } varying_bufs;

   struct {
      global struct mali_attribute_buffer_packed *descs;
      global struct libpan_draw_helper_attrib_buf_info *infos;
      uint32_t valid;
   } attrib_bufs;

   struct {
      global struct mali_attribute_packed *descs;
      global struct libpan_draw_helper_attrib_info *infos;
      uint32_t valid;
   } attribs;

   global uint8_t *idvs_job;
   global uint8_t *vertex_job;
   global uint8_t *tiler_job;

   uint32_t vertex_range;
   uint32_t padded_vertex_count;
   uint8_t primitive_vertex_count;
   global uint8_t *indices;

   uint64_t position;
   uint64_t psiz;
};

static void
panlib_patch_draw_vertex_dcd(struct panlib_draw_info *draw)
{
   global struct mali_invocation_packed *vertex_invocation;
   global struct mali_draw_packed *vertex_dcd;

   if (draw->idvs_job != NULL) {
      vertex_invocation =
         (global struct mali_invocation_packed *)(draw->idvs_job +
                                                  pan_section_offset(
                                                     INDEXED_VERTEX_JOB,
                                                     INVOCATION));
      vertex_dcd = (global struct mali_draw_packed *)(draw->idvs_job +
                                                      pan_section_offset(
                                                         INDEXED_VERTEX_JOB,
                                                         VERTEX_DRAW));
   } else {
      vertex_invocation =
         (global struct mali_invocation_packed *)(draw->vertex_job +
                                                  pan_section_offset(
                                                     COMPUTE_JOB, INVOCATION));
      vertex_dcd = (global struct mali_draw_packed *)(draw->vertex_job +
                                                      pan_section_offset(
                                                         COMPUTE_JOB, DRAW));
   }

   /* Patch the number of invocations on the vertex job */
   pan_pack_work_groups_compute(vertex_invocation, 1, draw->vertex_range,
                                draw->instance.count, 1, 1, 1, true, false);

   /* Patch the draw descriptor */
   pan_unpack(vertex_dcd, DRAW, unpacked_vertex_dcd)
      ;
   pan_pack(vertex_dcd, DRAW, cfg) {
      memcpy(&cfg, &unpacked_vertex_dcd, sizeof(cfg));
      cfg.offset_start = draw->vertex.raw_offset;
      cfg.instance_size =
         draw->instance.count > 1 ? draw->padded_vertex_count : 1;
   }
}

static void
panlib_patch_draw_tiler_dcd(struct panlib_draw_info *draw)
{
   /* No tiler job, nothing to do here */
   if (draw->idvs_job == NULL && draw->tiler_job == NULL)
      return;

   global struct mali_draw_packed *tiler_dcd;
   global struct mali_primitive_packed *tiler_primitive;
   global struct mali_primitive_size_packed *tiler_primsz;

   if (draw->idvs_job != NULL) {
      tiler_dcd = (global struct mali_draw_packed *)(draw->idvs_job +
                                                     pan_section_offset(
                                                        INDEXED_VERTEX_JOB,
                                                        FRAGMENT_DRAW));
      tiler_primitive =
         (global struct mali_primitive_packed *)(draw->idvs_job +
                                                 pan_section_offset(
                                                    INDEXED_VERTEX_JOB,
                                                    PRIMITIVE));
      tiler_primsz =
         (global struct mali_primitive_size_packed *)(draw->idvs_job +
                                                      pan_section_offset(
                                                         INDEXED_VERTEX_JOB,
                                                         PRIMITIVE_SIZE));
   } else {
      global struct mali_invocation_packed *vertex_invocation =
         (global struct mali_invocation_packed *)(draw->vertex_job +
                                                  pan_section_offset(
                                                     COMPUTE_JOB, INVOCATION));
      global struct mali_invocation_packed *tiler_invocation =
         (global struct mali_invocation_packed *)(draw->tiler_job +
                                                  pan_section_offset(
                                                     TILER_JOB, INVOCATION));
      /* Patch the tiler job invocation if present (identical to the vertex
       * invocations) */
      memcpy(tiler_invocation, vertex_invocation, pan_size(INVOCATION));

      tiler_dcd = (global struct mali_draw_packed *)(draw->tiler_job +
                                                     pan_section_offset(
                                                        TILER_JOB, DRAW));
      tiler_primitive =
         (global struct mali_primitive_packed *)(draw->tiler_job +
                                                 pan_section_offset(TILER_JOB,
                                                                    PRIMITIVE));
      tiler_primsz =
         (global struct mali_primitive_size_packed *)(draw->tiler_job +
                                                      pan_section_offset(
                                                         TILER_JOB,
                                                         PRIMITIVE_SIZE));
   }

   /* Patch the attribute start offset */
   pan_unpack(tiler_dcd, DRAW, unpacked_tiler_dcd)
      ;
   pan_pack(tiler_dcd, DRAW, cfg) {
      memcpy(&cfg, &unpacked_tiler_dcd, sizeof(cfg));
      cfg.position = draw->position;
      cfg.offset_start = draw->vertex.raw_offset;
      cfg.instance_size =
         draw->instance.count > 1 ? draw->padded_vertex_count : 1;
      uint32_t primitives_per_instance =
         DIV_ROUND_UP(draw->padded_vertex_count, draw->primitive_vertex_count);
      cfg.instance_primitive_size =
         pan_padded_vertex_count(primitives_per_instance);
   }

   /* Patch the primitive vertex offset to take offset_start into account in
    * case of indexed draw */
   pan_unpack(tiler_primitive, PRIMITIVE, unpacked_tiler_primitive)
      ;

   pan_pack(tiler_primitive, PRIMITIVE, cfg) {
      memcpy(&cfg, &unpacked_tiler_primitive, sizeof(cfg));
      cfg.index_count = draw->vertex.count;

      if (draw->index.size) {
         cfg.indices =
            (uint64_t)draw->indices + draw->index.offset * draw->index.size;
         cfg.base_vertex_offset =
            (int64_t)draw->vertex.base - draw->vertex.raw_offset;
      }
   }

   /* In case we have a point size, we need to patch the descriptor there */
   if (unpacked_tiler_primitive.point_size_array_format !=
       MALI_POINT_SIZE_ARRAY_FORMAT_NONE) {
      pan_pack(tiler_primsz, PRIMITIVE_SIZE, cfg) {
         cfg.size_array = draw->psiz;
      }
   }
}

static void
panlib_patch_varying_bufs(struct panlib_draw_info *draw)
{
   uint32_t vertex_count = draw->padded_vertex_count * draw->instance.count;

   for (uint32_t i = 0; i < PANLIB_VARY_BUF_MAX; i++) {
      global struct libpan_draw_helper_varying_buf_info *info =
         &draw->varying_bufs.info[0];

      global struct mali_attribute_buffer_packed *desc =
         &draw->varying_bufs.descs[i];
      pan_unpack(desc, ATTRIBUTE_BUFFER, unpacked_desc)
         ;

      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         memcpy(&cfg, &unpacked_desc, sizeof(cfg));
         cfg.size = vertex_count * cfg.stride;

         /* Ensure we are aligned properly */
         uint32_t offset_add = ALIGN_POT(cfg.size, 64);

         /* Allocate some space for ourself */
         uint32_t current_offset = atomic_fetch_add_explicit(
            &info->offset, offset_add, memory_order_relaxed);

         /* Sanity check that we don't overrun the buffer */
         assert(current_offset + cfg.size <= info->size);

         cfg.pointer = info->address + current_offset;

         if (i == PANLIB_VARY_BUF_POSITION)
            draw->position = cfg.pointer;
         else if (i == PANLIB_VARY_BUF_PSIZ)
            draw->psiz = cfg.pointer;
      }
   }
}

static void
panlib_patch_attrib_buf(struct panlib_draw_info *draw,
                        global struct mali_attribute_buffer_packed *desc,
                        struct libpan_draw_helper_attrib_buf_info info)
{
   uint32_t divisor = draw->padded_vertex_count * info.divisor;
   global struct mali_attribute_buffer_continuation_npot_packed *desc_ext =
      (global struct mali_attribute_buffer_continuation_npot_packed *)&desc[1];

   /* We are effectively recreating the attribute buffer descriptors. The only
    * trustworthy infos in the descriptor are the pointer and size */
   pan_unpack(desc, ATTRIBUTE_BUFFER, unpacked_desc)
      ;

   if (draw->instance.count <= 1) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = info.per_instance ? 0 : info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
      }
   } else if (!info.per_instance) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
         cfg.divisor = draw->padded_vertex_count;
         cfg.stride = info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
      }
   } else if (!divisor) {
      /* instance_divisor == 0 means all instances share the same value.
       * Make it a 1D array with a zero stride.
       */
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = 0;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
      }
   } else if (util_is_power_of_two_or_zero(divisor)) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.stride = info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
         cfg.divisor_r = __builtin_ctz(divisor);
      }
   } else {
      uint32_t divisor_r = 0, divisor_e = 0;
      uint32_t divisor_d =
         pan_compute_npot_divisor(divisor, &divisor_r, &divisor_e);
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.stride = info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
         cfg.divisor_r = divisor_r;
         cfg.divisor_e = divisor_e;
      }

      pan_pack(desc_ext, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_CONTINUATION;
         cfg.divisor_numerator = divisor_d;
         cfg.divisor = info.divisor;
      }

      return;
   }

   /* If the buffer extension wasn't used, memset(0) */
   for (uint32_t i = 0; i < pan_size(ATTRIBUTE_BUFFER) / 4; i++)
      desc_ext->opaque[i] = 0;
}

static void
panlib_patch_attrib(struct panlib_draw_info *draw,
                    global struct mali_attribute_packed *desc,
                    struct libpan_draw_helper_attrib_info info)
{
   /* We are effectively recreating the attribute descriptors. The only
    * trustworthy infos in the descriptor are the buffer index and format */
   pan_unpack(desc, ATTRIBUTE, unpacked_desc)
      ;

   pan_pack(desc, ATTRIBUTE, cfg) {
      cfg.buffer_index = unpacked_desc.buffer_index;
      cfg.offset = info.base_offset + draw->instance.base * info.stride;
      cfg.offset_enable = true;
      cfg.format = unpacked_desc.format;
   }
}

static void
panlib_patch_job_type_header(global struct mali_job_header_packed *desc,
                             enum mali_job_type job_type)
{
   if (desc == NULL)
      return;

   pan_unpack(desc, JOB_HEADER, unpacked_desc)
      ;

   pan_pack(desc, JOB_HEADER, cfg) {
      memcpy(&cfg, &unpacked_desc, sizeof(cfg));
      cfg.type = job_type;
   }
}

static void
panlib_patch_draw(struct panlib_draw_info *draw)
{
   /* First of all, we need to ensure each jobs have the proper header */
   bool is_null_job = draw->vertex_range == 0 || draw->instance.count == 0;

   if (draw->idvs_job != NULL) {
      panlib_patch_job_type_header(
         (global struct mali_job_header_packed *)draw->idvs_job,
         is_null_job ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_INDEXED_VERTEX);
   } else {
      panlib_patch_job_type_header(
         (global struct mali_job_header_packed *)draw->vertex_job,
         is_null_job ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_VERTEX);
      panlib_patch_job_type_header(
         (global struct mali_job_header_packed *)draw->tiler_job,
         is_null_job ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_TILER);
   }

   /* In case there is nothing to draw, let's just bail out. */
   if (is_null_job)
      return;

   /* Next we patch varying buffer descriptors and collect position and point
    * size varying addresses */
   panlib_patch_varying_bufs(draw);

   /* Patch the vertex draw and invocation descriptors */
   panlib_patch_draw_vertex_dcd(draw);

   /* Patch the tiler descriptors if present */
   panlib_patch_draw_tiler_dcd(draw);

   /* Finally, patch attribute descriptors */
   uint32_t num_vbs = util_last_bit(draw->attrib_bufs.valid);
   for (uint32_t i = 0; i < num_vbs; i++) {
      if (draw->attrib_bufs.valid & BITFIELD_BIT(i))
         panlib_patch_attrib_buf(draw, &draw->attrib_bufs.descs[i * 2],
                                 draw->attrib_bufs.infos[i]);
   }

   uint32_t num_vs_attribs = util_last_bit(draw->attribs.valid);
   for (uint32_t i = 0; i < num_vs_attribs; i++) {
      if (draw->attribs.valid & BITFIELD_BIT(i))
         panlib_patch_attrib(draw, &draw->attribs.descs[i],
                             draw->attribs.infos[i]);
   }
}

static uint32_t
padded_vertex_count(uint32_t vertex_count, uint32_t instance_count, bool idvs)
{
   if (instance_count == 1)
      return vertex_count;

   /* Index-Driven Vertex Shading requires different instances to
    * have different cache lines for position results. Each vertex
    * position is 16 bytes and the Mali cache line is 64 bytes, so
    * the instance count must be aligned to 4 vertices.
    */
   if (idvs)
      vertex_count = ALIGN_POT(vertex_count, 4);

   return pan_padded_vertex_count(vertex_count);
}

/* Argument ordering choosen to avoid any padding */

KERNEL(1)
panlib_draw_indirect_helper(
   global VkDrawIndirectCommand *cmd,
   global struct mali_attribute_buffer_packed *varying_bufs_descs,
   global struct libpan_draw_helper_varying_buf_info *varying_bufs_info,
   global struct mali_attribute_buffer_packed *attrib_bufs_descs,
   global struct libpan_draw_helper_attrib_buf_info *attrib_bufs_infos,
   uint32_t attrib_bufs_valid, uint32_t attribs_valid,
   global struct mali_attribute_packed *attribs_descs,
   global struct libpan_draw_helper_attrib_info *attribs_infos,
   global uint32_t *first_vertex_sysval, global uint32_t *first_instance_sysval,
   global uint32_t *raw_vertex_offset_sysval, global uint8_t *idvs_job,
   global uint8_t *vertex_job, global uint8_t *tiler_job,
   uint32_t primitive_vertex_count)
{
   const uint32_t vertex_count = cmd->vertexCount;
   const uint32_t instance_count = cmd->instanceCount;
   const uint32_t first_vertex = cmd->firstVertex;
   const uint32_t first_instance = cmd->firstInstance;

   struct panlib_draw_info draw = {
      .idvs_job = idvs_job,
      .vertex_job = vertex_job,
      .tiler_job = tiler_job,

      .vertex.base = first_vertex,
      .vertex.raw_offset = first_vertex,
      .vertex.count = vertex_count,
      .instance.base = first_instance,
      .instance.count = instance_count,

      .varying_bufs.descs = varying_bufs_descs,
      .varying_bufs.info = varying_bufs_info,

      .attrib_bufs.descs = attrib_bufs_descs,
      .attrib_bufs.infos = attrib_bufs_infos,
      .attrib_bufs.valid = attrib_bufs_valid,

      .attribs.descs = attribs_descs,
      .attribs.infos = attribs_infos,
      .attribs.valid = attribs_valid,

      .vertex_range = vertex_count,
      .padded_vertex_count =
         padded_vertex_count(vertex_count, instance_count, idvs_job != NULL),
      .primitive_vertex_count = primitive_vertex_count,
   };

   panlib_patch_draw(&draw);

   *first_vertex_sysval = draw.vertex.base;
   *first_instance_sysval = draw.instance.base;
   *raw_vertex_offset_sysval = draw.vertex.raw_offset;
}

KERNEL(1)
panlib_draw_indexed_indirect_helper(
   global VkDrawIndexedIndirectCommand *cmd, global uint8_t *index_buffer_ptr,
   global struct libpan_draw_helper_index_min_max_result *index_min_max_res,
   uint32_t index_size, uint32_t primitive_vertex_count,
   uint32_t attrib_bufs_valid, uint32_t attribs_valid,
   global struct mali_attribute_buffer_packed *varying_bufs_descs,
   global struct libpan_draw_helper_varying_buf_info *varying_bufs_info,
   global struct mali_attribute_buffer_packed *attrib_bufs_descs,
   global struct libpan_draw_helper_attrib_buf_info *attrib_bufs_infos,
   global struct mali_attribute_packed *attribs_descs,
   global struct libpan_draw_helper_attrib_info *attribs_infos,
   global uint32_t *first_vertex_sysval, global uint32_t *first_instance_sysval,
   global uint32_t *raw_vertex_offset_sysval, global uint8_t *idvs_job,
   global uint8_t *vertex_job, global uint8_t *tiler_job)
{
   const uint32_t index_count = cmd->indexCount;
   const uint32_t first_index = cmd->firstIndex;
   const uint32_t first_instance = cmd->firstInstance;
   const uint32_t instance_count = cmd->instanceCount;
   const int32_t vertex_offset = cmd->vertexOffset;
   const uint32_t min_vertex = index_min_max_res->min;
   const uint32_t max_vertex = index_min_max_res->max;
   const uint32_t vertex_range = max_vertex - min_vertex + 1;

   struct panlib_draw_info draw = {
      .idvs_job = idvs_job,
      .vertex_job = vertex_job,
      .tiler_job = tiler_job,

      .index.size = index_size,
      .index.offset = first_index,
      .vertex.base = vertex_offset,
      .vertex.raw_offset = min_vertex + vertex_offset,
      .vertex.count = index_count,
      .instance.base = first_instance,
      .instance.count = instance_count,

      .varying_bufs.descs = varying_bufs_descs,
      .varying_bufs.info = varying_bufs_info,

      .attrib_bufs.descs = attrib_bufs_descs,
      .attrib_bufs.infos = attrib_bufs_infos,
      .attrib_bufs.valid = attrib_bufs_valid,

      .attribs.descs = attribs_descs,
      .attribs.infos = attribs_infos,
      .attribs.valid = attribs_valid,

      .vertex_range = vertex_range,
      .padded_vertex_count =
         padded_vertex_count(vertex_range, instance_count, idvs_job != NULL),
      .primitive_vertex_count = primitive_vertex_count,
      .indices = index_buffer_ptr,
   };

   panlib_patch_draw(&draw);

   *first_vertex_sysval = draw.vertex.base;
   *first_instance_sysval = draw.instance.base;
   *raw_vertex_offset_sysval = draw.vertex.raw_offset;
}

KERNEL(64)
panlib_draw_index_minmax_search_helper(global uint8_t *index_buffer_ptr,
                                       global VkDrawIndexedIndirectCommand *cmd,
                                       global atomic_uint *min_ptr,
                                       global atomic_uint *max_ptr,
                                       uint32_t index_bytes_log2__3,
                                       uint8_t primitive_restart__2)
{
   /* Max count of values to process per thread */
   const uint32_t max_count_per_thread = 1024;

   const uint32_t index_bit_size = (1 << index_bytes_log2__3) * 8;
   const uint32_t start = cmd->firstIndex;
   const uint32_t index_count = cmd->indexCount;

   uint32_t base_idx = cl_global_id.x * max_count_per_thread;

   /* If the thread is out of range, bail out */
   if (base_idx >= index_count)
      return;

   /* Compute expected max iteration to do in this thread */
   uint32_t count = MIN2(max_count_per_thread, index_count - base_idx);

   /* Sanity check so nothing weird will happen */
   assert(base_idx + count <= index_count);

   uint32_t local_min = ((uint64_t)1 << index_bit_size) - 1;
   uint32_t local_max = 0;

   switch (index_bit_size) {
#define MINMAX_SEARCH_CASE(sz)                                                 \
   case sz: {                                                                  \
      global uint##sz##_t *indices = (global uint##sz##_t *)index_buffer_ptr;  \
      for (uint32_t i = 0; i < count; i++) {                                   \
         uint32_t val = (uint32_t)indices[start + base_idx + i];               \
         if (primitive_restart__2 && val == UINT##sz##_MAX)                    \
            continue;                                                          \
         local_min = min(local_min, val);                                      \
         local_max = max(local_max, val);                                      \
      }                                                                        \
      break;                                                                   \
   }
      MINMAX_SEARCH_CASE(32)
      MINMAX_SEARCH_CASE(16)
      MINMAX_SEARCH_CASE(8)
#undef MINMAX_SEARCH_CASE
   default:
      assert(0 && "Invalid index size");
   }

   atomic_fetch_min(min_ptr, local_min);
   atomic_fetch_max(max_ptr, local_max);
}

#endif
