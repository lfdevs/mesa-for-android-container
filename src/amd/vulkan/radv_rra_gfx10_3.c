/*
 * Copyright © 2022 Friedrich Vock
 *
 * SPDX-License-Identifier: MIT
 */

/* GFX10_3-GFX11 specific code for RRA. */

#include "bvh/bvh.h"
#include "radv_rra.h"

#include "util/half_float.h"

struct rra_box32_node {
   uint32_t children[4];
   float coords[4][2][3];
   uint32_t reserved[4];
};

struct rra_box16_node {
   uint32_t children[4];
   float16_t coords[4][2][3];
};

/*
 * RRA files contain this struct in place of hardware
 * instance nodes. They're named "instance desc" internally.
 */
struct rra_instance_node {
   float wto_matrix[12];
   uint32_t custom_instance_id : 24;
   uint32_t mask : 8;
   uint32_t sbt_offset : 24;
   uint32_t instance_flags : 8;
   uint64_t blas_va : 54;
   uint64_t hw_instance_flags : 10;
   uint32_t instance_id;
   uint32_t unused1;
   uint32_t blas_metadata_size;
   uint32_t unused2;
   float otw_matrix[12];
};

static_assert(sizeof(struct rra_instance_node) == 128, "rra_instance_node does not match RRA spec!");

/*
 * Format RRA uses for aabb nodes
 */
struct rra_aabb_node {
   float aabb[2][3];
   uint32_t unused1[6];
   uint32_t geometry_id : 28;
   uint32_t flags : 4;
   uint32_t primitive_id;
   uint32_t unused[2];
};

static_assert(sizeof(struct rra_aabb_node) == 64, "rra_aabb_node does not match RRA spec!");

struct rra_triangle_node {
   float coords[3][3];
   uint32_t reserved[3];
   uint32_t geometry_id : 28;
   uint32_t flags : 4;
   uint32_t triangle_id;
   uint32_t reserved2;
   uint32_t id;
};

static_assert(sizeof(struct rra_triangle_node) == 64, "rra_triangle_node does not match RRA spec!");

static uint32_t
rra_parent_table_index_from_offset(uint32_t offset, uint32_t parent_table_size)
{
   uint32_t max_parent_table_index = parent_table_size / sizeof(uint32_t) - 1;
   return max_parent_table_index - (offset - RRA_ROOT_NODE_OFFSET) / 64;
}

static bool
is_internal_node(uint32_t type)
{
   return type == radv_bvh_node_box16 || type == radv_bvh_node_box32;
}

static const char *node_type_names[8] = {
   [radv_bvh_node_triangle + 0] = "triangle0",
   [radv_bvh_node_triangle + 1] = "triangle1",
   [radv_bvh_node_triangle + 2] = "triangle2",
   [radv_bvh_node_triangle + 3] = "triangle3",
   [radv_bvh_node_box16] = "box16",
   [radv_bvh_node_box32] = "box32",
   [radv_bvh_node_instance] = "instance",
   [radv_bvh_node_aabb] = "aabb",
};

bool
rra_validate_node_gfx10_3(struct hash_table_u64 *accel_struct_vas, uint8_t *data, void *node, uint32_t geometry_count,
                          uint32_t size, bool is_bottom_level, uint32_t depth)
{
   struct rra_validation_context ctx = {0};

   if (depth > 1024) {
      rra_validation_fail(&ctx, "depth > 1024");
      return true;
   }

   uint32_t cur_offset = (uint8_t *)node - data;
   snprintf(ctx.location, sizeof(ctx.location), "internal node (offset=%u)", cur_offset);

   /* The child ids are located at offset=0 for both box16 and box32 nodes. */
   uint32_t *children = node;
   for (uint32_t i = 0; i < 4; ++i) {
      if (children[i] == 0xFFFFFFFF)
         continue;

      uint32_t type = children[i] & 7;
      uint32_t offset = (children[i] & (~7u)) << 3;

      if (!is_internal_node(type) && is_bottom_level == (type == radv_bvh_node_instance))
         rra_validation_fail(&ctx,
                             is_bottom_level ? "%s node in BLAS (child index %u)" : "%s node in TLAS (child index %u)",
                             node_type_names[type], i);

      if (offset > size) {
         rra_validation_fail(&ctx, "Invalid child offset (child index %u)", i);
         continue;
      }

      struct rra_validation_context child_ctx = {0};
      snprintf(child_ctx.location, sizeof(child_ctx.location), "%s node (offset=%u)", node_type_names[type], offset);

      if (is_internal_node(type)) {
         ctx.failed |= rra_validate_node_gfx10_3(accel_struct_vas, data, data + offset, geometry_count, size,
                                                 is_bottom_level, depth + 1);
      } else if (type == radv_bvh_node_instance) {
         struct radv_bvh_instance_node *src = (struct radv_bvh_instance_node *)(data + offset);
         uint64_t blas_va = radv_node_to_addr(src->bvh_ptr) - src->bvh_offset;
         if (!_mesa_hash_table_u64_search(accel_struct_vas, blas_va))
            rra_validation_fail(&child_ctx, "Invalid instance node pointer 0x%llx (offset: 0x%x)",
                                (unsigned long long)src->bvh_ptr, src->bvh_offset);
      } else if (type == radv_bvh_node_aabb) {
         struct radv_bvh_aabb_node *src = (struct radv_bvh_aabb_node *)(data + offset);
         if ((src->geometry_id_and_flags & 0xFFFFFFF) >= geometry_count)
            rra_validation_fail(&ctx, "geometry_id >= geometry_count");
      } else {
         struct radv_bvh_triangle_node *src = (struct radv_bvh_triangle_node *)(data + offset);
         if ((src->geometry_id_and_flags & 0xFFFFFFF) >= geometry_count)
            rra_validation_fail(&ctx, "geometry_id >= geometry_count");
      }

      ctx.failed |= child_ctx.failed;
   }
   return ctx.failed;
}

static uint32_t
get_geometry_id(const void *node, uint32_t node_type)
{
   if (node_type == radv_bvh_node_triangle) {
      const struct radv_bvh_triangle_node *triangle = node;
      return triangle->geometry_id_and_flags & 0xFFFFFFF;
   }

   if (node_type == radv_bvh_node_aabb) {
      const struct radv_bvh_aabb_node *aabb = node;
      return aabb->geometry_id_and_flags & 0xFFFFFFF;
   }

   return 0;
}

void
rra_gather_bvh_info_gfx10_3(const uint8_t *bvh, uint32_t node_id, struct rra_bvh_info *dst)
{
   uint32_t node_type = node_id & 7;

   switch (node_type) {
   case radv_bvh_node_box16:
      dst->internal_nodes_size += sizeof(struct rra_box16_node);
      dst->box16_count++;
      break;
   case radv_bvh_node_box32:
      dst->internal_nodes_size += sizeof(struct rra_box32_node);
      dst->box32_count++;
      break;
   case radv_bvh_node_instance:
      dst->leaf_nodes_size += sizeof(struct rra_instance_node);
      break;
   case radv_bvh_node_triangle:
      dst->leaf_nodes_size += sizeof(struct rra_triangle_node);
      break;
   case radv_bvh_node_aabb:
      dst->leaf_nodes_size += sizeof(struct rra_aabb_node);
      break;
   default:
      break;
   }

   const void *node = bvh + ((node_id & (~7u)) << 3);
   if (is_internal_node(node_type)) {
      /* The child ids are located at offset=0 for both box16 and box32 nodes. */
      const uint32_t *children = node;
      for (uint32_t i = 0; i < 4; i++)
         if (children[i] != 0xffffffff)
            rra_gather_bvh_info_gfx10_3(bvh, children[i], dst);
   } else {
      dst->geometry_infos[get_geometry_id(node, node_type)].primitive_count++;
   }
}

static void
rra_transcode_triangle_node(struct rra_transcoding_context *ctx, const struct radv_bvh_triangle_node *src)
{
   struct rra_triangle_node *dst = (struct rra_triangle_node *)(ctx->dst + ctx->dst_leaf_offset);
   ctx->dst_leaf_offset += sizeof(struct rra_triangle_node);

   for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
         dst->coords[i][j] = src->coords[i][j];
   dst->triangle_id = src->triangle_id;
   dst->geometry_id = src->geometry_id_and_flags & 0xfffffff;
   dst->flags = src->geometry_id_and_flags >> 28;
   dst->id = src->id;
}

static void
rra_transcode_aabb_node(struct rra_transcoding_context *ctx, const struct radv_bvh_aabb_node *src, vk_aabb bounds)
{
   struct rra_aabb_node *dst = (struct rra_aabb_node *)(ctx->dst + ctx->dst_leaf_offset);
   ctx->dst_leaf_offset += sizeof(struct rra_aabb_node);

   dst->aabb[0][0] = bounds.min.x;
   dst->aabb[0][1] = bounds.min.y;
   dst->aabb[0][2] = bounds.min.z;
   dst->aabb[1][0] = bounds.max.x;
   dst->aabb[1][1] = bounds.max.y;
   dst->aabb[1][2] = bounds.max.z;

   dst->geometry_id = src->geometry_id_and_flags & 0xfffffff;
   dst->flags = src->geometry_id_and_flags >> 28;
   dst->primitive_id = src->primitive_id;
}

static void
rra_transcode_instance_node(struct rra_transcoding_context *ctx, const struct radv_bvh_instance_node *src)
{
   uint64_t blas_va = radv_node_to_addr(src->bvh_ptr) - src->bvh_offset;

   struct rra_instance_node *dst = (struct rra_instance_node *)(ctx->dst + ctx->dst_leaf_offset);
   ctx->dst_leaf_offset += sizeof(struct rra_instance_node);

   dst->custom_instance_id = src->custom_instance_and_mask & 0xffffff;
   dst->mask = src->custom_instance_and_mask >> 24;
   dst->sbt_offset = src->sbt_offset_and_flags & 0xffffff;
   dst->instance_flags = src->sbt_offset_and_flags >> 24;
   dst->blas_va = (blas_va + sizeof(struct rra_accel_struct_metadata)) >> 3;
   dst->instance_id = src->instance_id;
   dst->blas_metadata_size = sizeof(struct rra_accel_struct_metadata);

   memcpy(dst->wto_matrix, src->wto_matrix.values, sizeof(dst->wto_matrix));
   memcpy(dst->otw_matrix, src->otw_matrix.values, sizeof(dst->otw_matrix));

   uint64_t *addr = ralloc(ctx->used_blas, uint64_t);
   if (addr) {
      *addr = blas_va;
      _mesa_set_add(ctx->used_blas, addr);
   }
}

static void
rra_transcode_box16_node(struct rra_transcoding_context *ctx, const struct radv_bvh_box16_node *src)
{
   uint32_t dst_offset = ctx->dst_internal_offset;
   ctx->dst_internal_offset += sizeof(struct rra_box16_node);
   struct rra_box16_node *dst = (struct rra_box16_node *)(ctx->dst + dst_offset);

   memcpy(dst->coords, src->coords, sizeof(dst->coords));

   for (uint32_t i = 0; i < 4; ++i) {
      if (src->children[i] == 0xffffffff) {
         dst->children[i] = 0xffffffff;
         continue;
      }

      vk_aabb bounds = {
         .min =
            {
               _mesa_half_to_float(src->coords[i].min_x),
               _mesa_half_to_float(src->coords[i].min_y),
               _mesa_half_to_float(src->coords[i].min_z),
            },
         .max =
            {
               _mesa_half_to_float(src->coords[i].max_x),
               _mesa_half_to_float(src->coords[i].max_y),
               _mesa_half_to_float(src->coords[i].max_z),
            },
      };

      dst->children[i] =
         rra_transcode_node_gfx10_3(ctx, radv_bvh_node_box16 | (dst_offset >> 3), src->children[i], bounds);
   }
}

static void
rra_transcode_box32_node(struct rra_transcoding_context *ctx, const struct radv_bvh_box32_node *src)
{
   uint32_t dst_offset = ctx->dst_internal_offset;
   ctx->dst_internal_offset += sizeof(struct rra_box32_node);
   struct rra_box32_node *dst = (struct rra_box32_node *)(ctx->dst + dst_offset);

   memcpy(dst->coords, src->coords, sizeof(dst->coords));

   for (uint32_t i = 0; i < 4; ++i) {
      if (isnan(src->coords[i].min.x)) {
         dst->children[i] = 0xffffffff;
         continue;
      }

      dst->children[i] =
         rra_transcode_node_gfx10_3(ctx, radv_bvh_node_box32 | (dst_offset >> 3), src->children[i], src->coords[i]);
   }
}

uint32_t
rra_transcode_node_gfx10_3(struct rra_transcoding_context *ctx, uint32_t parent_id, uint32_t src_id, vk_aabb bounds)
{
   uint32_t node_type = src_id & 7;
   uint32_t src_offset = (src_id & (~7u)) << 3;

   uint32_t dst_offset;

   const void *src_child_node = ctx->src + src_offset;
   if (is_internal_node(node_type)) {
      dst_offset = ctx->dst_internal_offset;
      if (node_type == radv_bvh_node_box32)
         rra_transcode_box32_node(ctx, src_child_node);
      else
         rra_transcode_box16_node(ctx, src_child_node);
   } else {
      dst_offset = ctx->dst_leaf_offset;

      if (node_type == radv_bvh_node_triangle)
         rra_transcode_triangle_node(ctx, src_child_node);
      else if (node_type == radv_bvh_node_aabb)
         rra_transcode_aabb_node(ctx, src_child_node, bounds);
      else if (node_type == radv_bvh_node_instance)
         rra_transcode_instance_node(ctx, src_child_node);
   }

   uint32_t parent_id_index = rra_parent_table_index_from_offset(dst_offset, ctx->parent_id_table_size);
   ctx->parent_id_table[parent_id_index] = parent_id;

   uint32_t dst_id = node_type | (dst_offset >> 3);
   if (!is_internal_node(node_type))
      ctx->leaf_node_ids[ctx->leaf_indices[get_geometry_id(src_child_node, node_type)]++] = dst_id;

   return dst_id;
}

void
radv_gather_bvh_stats_gfx10_3(const uint8_t *bvh, uint32_t node_id, uint32_t depth, float p,
                              struct hash_table_u64 *blas_sah, struct radv_bvh_stats_gfx10_3 *stats)
{
   uint32_t node_type = node_id & 7;
   const void *node = bvh + ((node_id & (~7u)) << 3);

   stats->max_depth = MAX2(stats->max_depth, depth);

   switch (node_type) {
   case radv_bvh_node_box16: {
      stats->sah += 1.0 * p;
      stats->box16_node_count++;

      const struct radv_bvh_box16_node *box16 = node;
      for (uint32_t i = 0; i < 4; i++) {
         if (box16->children[i] != 0xffffffff) {
            float extent[3] = {
               _mesa_half_to_float(box16->coords[i].max_x) - _mesa_half_to_float(box16->coords[i].min_x),
               _mesa_half_to_float(box16->coords[i].max_y) - _mesa_half_to_float(box16->coords[i].min_y),
               _mesa_half_to_float(box16->coords[i].max_z) - _mesa_half_to_float(box16->coords[i].min_z),
            };
            float surface_area = 2 * (extent[0] * extent[1] + extent[0] * extent[2] + extent[1] * extent[2]);
            radv_gather_bvh_stats_gfx10_3(bvh, box16->children[i], depth + 1, surface_area, blas_sah, stats);
         }
      }

      break;
   }
   case radv_bvh_node_box32: {
      stats->sah += 1.5 * p;
      stats->box32_node_count++;

      const struct radv_bvh_box32_node *box32 = node;
      for (uint32_t i = 0; i < 4; i++) {
         if (box32->children[i] != 0xffffffff) {
            float extent[3] = {
               box32->coords[i].max.x - box32->coords[i].min.x,
               box32->coords[i].max.y - box32->coords[i].min.y,
               box32->coords[i].max.z - box32->coords[i].min.z,
            };
            float surface_area = 2 * (extent[0] * extent[1] + extent[0] * extent[2] + extent[1] * extent[2]);
            radv_gather_bvh_stats_gfx10_3(bvh, box32->children[i], depth + 1, surface_area, blas_sah, stats);
         }
      }

      break;
   }
   case radv_bvh_node_instance: {
      stats->sah += 2.0 * p;
      stats->instance_node_count++;

      const struct radv_bvh_instance_node *instance = node;
      uint64_t blas_va = radv_node_to_addr(instance->bvh_ptr) - instance->bvh_offset;
      float *sah = _mesa_hash_table_u64_search(blas_sah, blas_va);
      if (sah)
         stats->instance_sah += *sah * p;
      else
         fprintf(stderr, "radv: Could not find SAH for BLAS at address 0x%" PRIx64 "\n", blas_va);

      break;
   }
   case radv_bvh_node_triangle:
      stats->sah += 2.0 * p;
      stats->triangle_node_count++;
      break;
   case radv_bvh_node_aabb:
      stats->sah += 4.0 * p;
      stats->procedural_node_count++;
      break;
   default:
      break;
   }
}
