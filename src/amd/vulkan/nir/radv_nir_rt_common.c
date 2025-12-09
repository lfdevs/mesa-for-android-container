/*
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_nir_rt_common.h"
#include "bvh/bvh.h"
#include "nir_builder.h"
#include "radv_debug.h"

static nir_def *build_node_to_addr(struct radv_device *device, nir_builder *b, nir_def *node, bool skip_type_and);

bool
radv_use_bvh_stack_rtn(const struct radv_physical_device *pdevice)
{
   /* gfx12 requires using the bvh4 ds_bvh_stack_rtn differently - enable hw stack instrs on gfx12 only with bvh8 */
   return (pdevice->info.gfx_level == GFX11 || pdevice->info.gfx_level == GFX11_5 || radv_use_bvh8(pdevice)) &&
          !radv_emulate_rt(pdevice);
}

nir_def *
radv_build_bvh_stack_rtn_addr(nir_builder *b, const struct radv_physical_device *pdev, uint32_t workgroup_size,
                              uint32_t stack_base, uint32_t max_stack_entries)
{
   assert(stack_base % 4 == 0);

   nir_def *stack_idx = nir_load_local_invocation_index(b);
   /* RDNA3's ds_bvh_stack_rtn instruction uses a special encoding for the stack address.
    * Bits 0-17 encode the current stack index (set to 0 initially)
    * Bits 18-31 encodes the stack base in multiples of 4
    *
    * The hardware uses a stride of 128 bytes (32 entries) for the stack index so the upper 32 threads need a different
    * base offset with wave64.
    */
   if (workgroup_size > 32) {
      nir_def *wave32_thread_id = nir_iand_imm(b, stack_idx, 0x1f);
      nir_def *wave32_group_id = nir_ushr_imm(b, stack_idx, 5);
      uint32_t stack_entries_per_group = max_stack_entries * 32;
      nir_def *group_stack_base = nir_imul_imm(b, wave32_group_id, stack_entries_per_group);
      stack_idx = nir_iadd(b, wave32_thread_id, group_stack_base);
   }
   stack_idx = nir_iadd_imm(b, stack_idx, stack_base / 4);
   /* There are 4 bytes in each stack entry so no further arithmetic is needed. */
   if (pdev->info.gfx_level >= GFX12)
      stack_idx = nir_ishl_imm(b, stack_idx, 15);
   else
      stack_idx = nir_ishl_imm(b, stack_idx, 18);
   return stack_idx;
}

static void
nir_sort_hit_pair(nir_builder *b, nir_variable *var_distances, nir_variable *var_indices, uint32_t chan_1,
                  uint32_t chan_2)
{
   nir_def *ssa_distances = nir_load_var(b, var_distances);
   nir_def *ssa_indices = nir_load_var(b, var_indices);
   /* if (distances[chan_2] < distances[chan_1]) { */
   nir_push_if(b, nir_flt(b, nir_channel(b, ssa_distances, chan_2), nir_channel(b, ssa_distances, chan_1)));
   {
      /* swap(distances[chan_2], distances[chan_1]); */
      nir_def *new_distances[4] = {nir_undef(b, 1, 32), nir_undef(b, 1, 32), nir_undef(b, 1, 32), nir_undef(b, 1, 32)};
      nir_def *new_indices[4] = {nir_undef(b, 1, 32), nir_undef(b, 1, 32), nir_undef(b, 1, 32), nir_undef(b, 1, 32)};
      new_distances[chan_2] = nir_channel(b, ssa_distances, chan_1);
      new_distances[chan_1] = nir_channel(b, ssa_distances, chan_2);
      new_indices[chan_2] = nir_channel(b, ssa_indices, chan_1);
      new_indices[chan_1] = nir_channel(b, ssa_indices, chan_2);
      nir_store_var(b, var_distances, nir_vec(b, new_distances, 4), (1u << chan_1) | (1u << chan_2));
      nir_store_var(b, var_indices, nir_vec(b, new_indices, 4), (1u << chan_1) | (1u << chan_2));
   }
   /* } */
   nir_pop_if(b, NULL);
}

static nir_def *
intersect_ray_amd_software_box(struct radv_device *device, nir_builder *b, nir_def *bvh_node, nir_def *ray_tmax,
                               nir_def *origin, nir_def *dir, nir_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);
   const struct glsl_type *uvec4_type = glsl_vector_type(GLSL_TYPE_UINT, 4);

   bool old_exact = b->exact;
   b->exact = true;

   nir_def *node_addr = build_node_to_addr(device, b, bvh_node, false);

   /* vec4 distances = vec4(INF, INF, INF, INF); */
   nir_variable *distances = nir_variable_create(b->shader, nir_var_shader_temp, vec4_type, "distances");
   nir_store_var(b, distances, nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, INFINITY), 0xf);

   /* uvec4 child_indices = uvec4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff); */
   nir_variable *child_indices = nir_variable_create(b->shader, nir_var_shader_temp, uvec4_type, "child_indices");
   nir_store_var(b, child_indices, nir_imm_ivec4(b, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu), 0xf);

   /* Need to remove infinities here because otherwise we get nasty NaN propagation
    * if the direction has 0s in it. */
   /* inv_dir = clamp(inv_dir, -FLT_MAX, FLT_MAX); */
   inv_dir = nir_fclamp(b, inv_dir, nir_imm_float(b, -FLT_MAX), nir_imm_float(b, FLT_MAX));

   for (int i = 0; i < 4; i++) {
      const uint32_t child_offset = offsetof(struct radv_bvh_box32_node, children[i]);
      const uint32_t coord_offsets[2] = {
         offsetof(struct radv_bvh_box32_node, coords[i].min.x),
         offsetof(struct radv_bvh_box32_node, coords[i].max.x),
      };

      /* node->children[i] -> uint */
      nir_def *child_index = nir_load_global(b, 1, 32, nir_iadd_imm(b, node_addr, child_offset), .align_mul = 64,
                                             .align_offset = child_offset % 64);
      /* node->coords[i][0], node->coords[i][1] -> vec3 */
      nir_def *node_coords[2] = {
         nir_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[0]), .align_mul = 64,
                         .align_offset = coord_offsets[0] % 64),
         nir_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[1]), .align_mul = 64,
                         .align_offset = coord_offsets[1] % 64),
      };

      /* If x of the aabb min is NaN, then this is an inactive aabb.
       * We don't need to care about any other components being NaN as that is UB.
       * https://docs.vulkan.org/spec/latest/chapters/accelstructures.html#acceleration-structure-inactive-prims
       */
      nir_def *min_x = nir_channel(b, node_coords[0], 0);
      nir_def *min_x_is_not_nan = nir_inot(b, nir_fneu(b, min_x, min_x)); /* NaN != NaN -> true */

      /* vec3 bound0 = (node->coords[i][0] - origin) * inv_dir; */
      nir_def *bound0 = nir_fmul(b, nir_fsub(b, node_coords[0], origin), inv_dir);
      /* vec3 bound1 = (node->coords[i][1] - origin) * inv_dir; */
      nir_def *bound1 = nir_fmul(b, nir_fsub(b, node_coords[1], origin), inv_dir);

      /* float tmin = max(max(min(bound0.x, bound1.x), min(bound0.y, bound1.y)), min(bound0.z,
       * bound1.z)); */
      nir_def *tmin = nir_fmax(b,
                               nir_fmax(b, nir_fmin(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
                                        nir_fmin(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
                               nir_fmin(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      /* float tmax = min(min(max(bound0.x, bound1.x), max(bound0.y, bound1.y)), max(bound0.z,
       * bound1.z)); */
      nir_def *tmax = nir_fmin(b,
                               nir_fmin(b, nir_fmax(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
                                        nir_fmax(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
                               nir_fmax(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      /* if (!isnan(node->coords[i][0].x) && tmax >= max(0.0f, tmin) && tmin < ray_tmax) { */
      nir_push_if(b, nir_iand(b, min_x_is_not_nan,
                              nir_iand(b, nir_fge(b, tmax, nir_fmax(b, nir_imm_float(b, 0.0f), tmin)),
                                       nir_flt(b, tmin, ray_tmax))));
      {
         /* child_indices[i] = node->children[i]; */
         nir_def *new_child_indices[4] = {child_index, child_index, child_index, child_index};
         nir_store_var(b, child_indices, nir_vec(b, new_child_indices, 4), 1u << i);

         /* distances[i] = tmin; */
         nir_def *new_distances[4] = {tmin, tmin, tmin, tmin};
         nir_store_var(b, distances, nir_vec(b, new_distances, 4), 1u << i);
      }
      /* } */
      nir_pop_if(b, NULL);
   }

   /* Sort our distances with a sorting network. */
   nir_sort_hit_pair(b, distances, child_indices, 0, 1);
   nir_sort_hit_pair(b, distances, child_indices, 2, 3);
   nir_sort_hit_pair(b, distances, child_indices, 0, 2);
   nir_sort_hit_pair(b, distances, child_indices, 1, 3);
   nir_sort_hit_pair(b, distances, child_indices, 1, 2);

   b->exact = old_exact;
   return nir_load_var(b, child_indices);
}

static nir_def *
radv_build_intersect_edge(nir_builder *b, nir_def *v0_x, nir_def *v0_y, nir_def *v1_x, nir_def *v1_y)
{
   /* Test (1 0 0) direction: t = <v1-v0, (1 0 0)> */
   nir_def *t_x = nir_fsub(b, v1_x, v0_x);
   nir_def *test_y = nir_feq_imm(b, t_x, 0.0);
   /* Test (0 1 0) direction: t = <v1-v0, (0 1 0)> */
   nir_def *t_y = nir_fsub(b, v1_y, v0_y);

   return nir_bcsel(b, test_y, nir_flt_imm(b, t_y, 0.0), nir_flt_imm(b, t_x, 0.0));
}

static nir_def *
radv_build_intersect_vertex(nir_builder *b, nir_def *v0_x, nir_def *v1_x, nir_def *v2_x)
{
   /* Choose n=(1 0 0) to simplify the dot product. */
   nir_def *edge0 = nir_fsub(b, v1_x, v0_x);
   nir_def *edge1 = nir_fsub(b, v2_x, v0_x);
   return nir_iand(b, nir_fle_imm(b, edge0, 0.0), nir_fgt_imm(b, edge1, 0.0));
}

static nir_def *
intersect_ray_amd_software_tri(struct radv_device *device, nir_builder *b, nir_def *bvh_node, nir_def *ray_tmax,
                               nir_def *origin, nir_def *dir, nir_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);

   bool old_exact = b->exact;
   b->exact = true;

   nir_def *node_addr = build_node_to_addr(device, b, bvh_node, false);

   const uint32_t coord_offsets[3] = {
      offsetof(struct radv_bvh_triangle_node, coords[0]),
      offsetof(struct radv_bvh_triangle_node, coords[1]),
      offsetof(struct radv_bvh_triangle_node, coords[2]),
   };

   /* node->coords[0], node->coords[1], node->coords[2] -> vec3 */
   nir_def *node_coords[3] = {
      nir_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[0]), .align_mul = 64,
                      .align_offset = coord_offsets[0] % 64),
      nir_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[1]), .align_mul = 64,
                      .align_offset = coord_offsets[1] % 64),
      nir_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, coord_offsets[2]), .align_mul = 64,
                      .align_offset = coord_offsets[2] % 64),
   };

   nir_variable *result = nir_variable_create(b->shader, nir_var_shader_temp, vec4_type, "result");
   nir_store_var(b, result, nir_imm_vec4(b, INFINITY, 1.0f, 0.0f, 0.0f), 0xf);

   /* Based on watertight Ray/Triangle intersection from
    * http://jcgt.org/published/0002/01/05/paper.pdf */

   /* Calculate the dimension where the ray direction is largest */
   nir_def *abs_dir = nir_fabs(b, dir);

   nir_def *abs_dirs[3] = {
      nir_channel(b, abs_dir, 0),
      nir_channel(b, abs_dir, 1),
      nir_channel(b, abs_dir, 2),
   };
   /* Find index of greatest value of abs_dir and put that as kz. */
   nir_def *packed_k =
      nir_bcsel(b, nir_fge(b, abs_dirs[0], abs_dirs[1]),
                nir_bcsel(b, nir_fge(b, abs_dirs[0], abs_dirs[2]), nir_imm_int(b, (0 << 4) | (2 << 2) | (1 << 0)),
                          nir_imm_int(b, (2 << 4) | (1 << 2) | (0 << 0))),
                nir_bcsel(b, nir_fge(b, abs_dirs[1], abs_dirs[2]), nir_imm_int(b, (1 << 4) | (0 << 2) | (2 << 0)),
                          nir_imm_int(b, (2 << 4) | (1 << 2) | (0 << 0))));
   nir_def *kx = nir_iand_imm(b, packed_k, 0x3);
   nir_def *ky = nir_ubfe_imm(b, packed_k, 2, 2);
   nir_def *kz = nir_ishr_imm(b, packed_k, 4);
   nir_def *k_indices[3] = {kx, ky, kz};
   nir_def *k = nir_vec(b, k_indices, 3);

   /* Swap kx and ky dimensions to preserve winding order */
   unsigned swap_xy_swizzle[4] = {1, 0, 2, 3};
   k = nir_bcsel(b, nir_flt_imm(b, nir_vector_extract(b, dir, kz), 0.0f), nir_swizzle(b, k, swap_xy_swizzle, 3), k);

   kx = nir_channel(b, k, 0);
   ky = nir_channel(b, k, 1);
   kz = nir_channel(b, k, 2);

   /* Calculate shear constants */
   nir_def *sz = nir_vector_extract(b, inv_dir, kz);
   nir_def *sx = nir_fmul(b, nir_vector_extract(b, dir, kx), sz);
   nir_def *sy = nir_fmul(b, nir_vector_extract(b, dir, ky), sz);

   /* Calculate vertices relative to ray origin */
   nir_def *v_a = nir_fsub(b, node_coords[0], origin);
   nir_def *v_b = nir_fsub(b, node_coords[1], origin);
   nir_def *v_c = nir_fsub(b, node_coords[2], origin);

   /* Perform shear and scale */
   nir_def *ax = nir_fsub(b, nir_vector_extract(b, v_a, kx), nir_fmul(b, sx, nir_vector_extract(b, v_a, kz)));
   nir_def *ay = nir_fsub(b, nir_vector_extract(b, v_a, ky), nir_fmul(b, sy, nir_vector_extract(b, v_a, kz)));
   nir_def *bx = nir_fsub(b, nir_vector_extract(b, v_b, kx), nir_fmul(b, sx, nir_vector_extract(b, v_b, kz)));
   nir_def *by = nir_fsub(b, nir_vector_extract(b, v_b, ky), nir_fmul(b, sy, nir_vector_extract(b, v_b, kz)));
   nir_def *cx = nir_fsub(b, nir_vector_extract(b, v_c, kx), nir_fmul(b, sx, nir_vector_extract(b, v_c, kz)));
   nir_def *cy = nir_fsub(b, nir_vector_extract(b, v_c, ky), nir_fmul(b, sy, nir_vector_extract(b, v_c, kz)));

   nir_def *u = nir_fsub(b, nir_fmul(b, cx, by), nir_fmul(b, cy, bx));
   nir_def *v = nir_fsub(b, nir_fmul(b, ax, cy), nir_fmul(b, ay, cx));
   nir_def *w = nir_fsub(b, nir_fmul(b, bx, ay), nir_fmul(b, by, ax));

   /* Perform edge tests. */
   nir_def *cond_back =
      nir_ior(b, nir_ior(b, nir_flt_imm(b, u, 0.0f), nir_flt_imm(b, v, 0.0f)), nir_flt_imm(b, w, 0.0f));

   nir_def *cond_front =
      nir_ior(b, nir_ior(b, nir_fgt_imm(b, u, 0.0f), nir_fgt_imm(b, v, 0.0f)), nir_fgt_imm(b, w, 0.0f));

   nir_def *cond = nir_inot(b, nir_iand(b, cond_back, cond_front));

   /* When an edge is hit, we have to ensure that it is not hit twice in case it is shared.
    *
    * Vulkan 1.4.322, Section 40.1.1 Watertightness:
    *
    *    Any set of two triangles with two shared vertices that were specified in the same
    *    winding order in each triangle have a shared edge defined by those vertices.
    *
    * This means we can decide which triangle should intersect by comparing the shared edge
    * to two arbitrary directions because the shared edges are antiparallel. The triangle
    * vertices are transformed so the ray direction is (0 0 1). Therefore it makes sense to
    * choose (1 0 0) and (0 1 0) as reference directions.
    *
    * Hitting edges is extremely rare so an if should be worth.
    */
   nir_def *is_edge_a = nir_feq_imm(b, u, 0.0f);
   nir_def *is_edge_b = nir_feq_imm(b, v, 0.0f);
   nir_def *is_edge_c = nir_feq_imm(b, w, 0.0f);
   nir_def *cond_edge = nir_ior(b, is_edge_a, nir_ior(b, is_edge_b, is_edge_c));
   nir_def *intersect_edge = cond;
   nir_push_if(b, cond_edge);
   {
      nir_def *intersect_edge_a = nir_iand(b, is_edge_a, radv_build_intersect_edge(b, bx, by, cx, cy));
      nir_def *intersect_edge_b = nir_iand(b, is_edge_b, radv_build_intersect_edge(b, cx, cy, ax, ay));
      nir_def *intersect_edge_c = nir_iand(b, is_edge_c, radv_build_intersect_edge(b, ax, ay, bx, by));
      intersect_edge =
         nir_iand(b, intersect_edge, nir_ior(b, nir_ior(b, intersect_edge_a, intersect_edge_b), intersect_edge_c));

      /* For vertices, special handling is needed to avoid double hits. The spec defines
       * shared vertices as follows (Vulkan 1.4.322, Section 40.1.1 Watertightness):
       *
       *    Any set of two or more triangles where all triangles have one vertex with an
       *    identical position value, that vertex is a shared vertex.
       *
       * Since the no double hit/miss requirement of a shared vertex is only formulated for
       * closed fans
       *
       *    Implementations should not double-hit or miss when a ray intersects a shared edge,
       *    or a shared vertex of a closed fan.
       *
       * it is possible to choose an arbitrary direction n that defines which triangle in the
       * closed fan should intersect the shared vertex with the ray.
       *
       *    All edges that include the above vertex are shared edges.
       *
       * Implies that all triangles have the same winding order. It is therefore sufficiant
       * to choose the triangle where the other vertices are on both sides of a plane
       * perpendicular to n (relying on winding order to get one instead of two triangles
       * that meet said condition).
       */
      nir_def *is_vertex_a = nir_iand(b, is_edge_b, is_edge_c);
      nir_def *is_vertex_b = nir_iand(b, is_edge_a, is_edge_c);
      nir_def *is_vertex_c = nir_iand(b, is_edge_a, is_edge_b);
      nir_def *intersect_vertex_a = nir_iand(b, is_vertex_a, radv_build_intersect_vertex(b, ax, bx, cx));
      nir_def *intersect_vertex_b = nir_iand(b, is_vertex_b, radv_build_intersect_vertex(b, bx, cx, ax));
      nir_def *intersect_vertex_c = nir_iand(b, is_vertex_c, radv_build_intersect_vertex(b, cx, ax, bx));
      nir_def *is_vertex = nir_ior(b, nir_ior(b, is_vertex_a, is_vertex_b), is_vertex_c);
      nir_def *intersect_vertex = nir_ior(b, nir_ior(b, intersect_vertex_a, intersect_vertex_b), intersect_vertex_c);
      intersect_vertex = nir_ior(b, nir_inot(b, is_vertex), intersect_vertex);
      intersect_edge = nir_iand(b, intersect_edge, intersect_vertex);
   }
   nir_pop_if(b, NULL);
   cond = nir_if_phi(b, intersect_edge, cond);

   nir_push_if(b, cond);
   {
      nir_def *det = nir_fadd(b, u, nir_fadd(b, v, w));

      nir_def *az = nir_fmul(b, sz, nir_vector_extract(b, v_a, kz));
      nir_def *bz = nir_fmul(b, sz, nir_vector_extract(b, v_b, kz));
      nir_def *cz = nir_fmul(b, sz, nir_vector_extract(b, v_c, kz));

      nir_def *t = nir_fadd(b, nir_fadd(b, nir_fmul(b, u, az), nir_fmul(b, v, bz)), nir_fmul(b, w, cz));

      nir_def *t_signed = nir_fmul(b, nir_fsign(b, det), t);

      nir_def *det_cond_front = nir_inot(b, nir_flt_imm(b, t_signed, 0.0f));

      nir_push_if(b, det_cond_front);
      {
         nir_def *det_abs = nir_fabs(b, det);

         t = nir_fdiv(b, t, det_abs);
         v = nir_fdiv(b, v, det_abs);
         w = nir_fdiv(b, w, det_abs);

         nir_def *indices[4] = {t, nir_fsign(b, det), v, w};
         nir_store_var(b, result, nir_vec(b, indices, 4), 0xf);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);

   b->exact = old_exact;
   return nir_load_var(b, result);
}

nir_def *
build_addr_to_node(struct radv_device *device, nir_builder *b, nir_def *addr, nir_def *flags)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   const uint64_t bvh_size = 1ull << 42;
   nir_def *node = nir_ushr_imm(b, addr, 3);
   node = nir_iand_imm(b, node, (bvh_size - 1) << 3);

   if (radv_use_bvh8(pdev)) {
      /* The HW ray flags are the same bits as the API flags.
       * - SpvRayFlagsTerminateOnFirstHitKHRMask, SpvRayFlagsSkipClosestHitShaderKHRMask are handled in shader code.
       * - SpvRayFlagsSkipTrianglesKHRMask, SpvRayFlagsSkipAABBsKHRMask do not work.
       */
      flags = nir_iand_imm(b, flags,
                           SpvRayFlagsOpaqueKHRMask | SpvRayFlagsNoOpaqueKHRMask |
                              SpvRayFlagsCullBackFacingTrianglesKHRMask | SpvRayFlagsCullFrontFacingTrianglesKHRMask |
                              SpvRayFlagsCullOpaqueKHRMask | SpvRayFlagsCullNoOpaqueKHRMask);
      node = nir_ior(b, node, nir_ishl_imm(b, nir_u2u64(b, flags), 54));
   }

   return node;
}

static nir_def *
build_node_to_addr(struct radv_device *device, nir_builder *b, nir_def *node, bool skip_type_and)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_def *addr = skip_type_and ? node : nir_iand_imm(b, node, ~7ull);
   addr = nir_ishl_imm(b, addr, 3);
   /* Assumes everything is in the top half of address space, which is true in
    * GFX9+ for now. */
   return pdev->info.gfx_level >= GFX9 ? nir_ior_imm(b, addr, 0xffffull << 48) : addr;
}

nir_def *
nir_build_vec3_mat_mult(nir_builder *b, nir_def *vec, nir_def *matrix[], bool translation)
{
   nir_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         nir_def *v = nir_fmul(b, nir_channels(b, vec, 1 << j), nir_channels(b, matrix[i], 1 << j));
         result_components[i] = (translation || j) ? nir_fadd(b, result_components[i], v) : v;
      }
   }
   return nir_vec(b, result_components, 3);
}

nir_def *
radv_load_vertex_position(struct radv_device *device, nir_builder *b, nir_def *primitive_addr, uint32_t index)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (radv_use_bvh8(pdev)) {
      /* Assume that vertices are uncompressed. */
      uint32_t offset = ROUND_DOWN_TO(RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE / 8, 4) + index * 3 * sizeof(float);
      nir_def *data[4];
      for (uint32_t i = 0; i < ARRAY_SIZE(data); i++) {
         data[i] = nir_load_global(b, 1, 32, nir_iadd_imm(b, primitive_addr, offset));
         offset += 4;
      }

      uint32_t subdword_offset = RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE % 32;

      nir_def *vertices[3];
      for (uint32_t i = 0; i < ARRAY_SIZE(vertices); i++) {
         nir_def *lo = nir_ubitfield_extract_imm(b, data[i], subdword_offset, 32 - subdword_offset);
         nir_def *hi = nir_ubitfield_extract_imm(b, data[i + 1], 0, subdword_offset);
         vertices[i] = nir_ior(b, lo, nir_ishl_imm(b, hi, 32 - subdword_offset));
      }

      return nir_vec3(b, vertices[0], vertices[1], vertices[2]);
   }

   uint32_t offset = index * 3 * sizeof(float);
   return nir_load_global(b, 3, 32, nir_iadd_imm(b, primitive_addr, offset));
}

void
radv_load_wto_matrix(struct radv_device *device, nir_builder *b, nir_def *instance_addr, nir_def **out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   unsigned offset = offsetof(struct radv_bvh_instance_node, wto_matrix);
   if (radv_use_bvh8(pdev))
      offset = offsetof(struct radv_gfx12_instance_node, wto_matrix);

   for (unsigned i = 0; i < 3; ++i) {
      out[i] = nir_load_global(b, 4, 32, nir_iadd_imm(b, instance_addr, offset + i * 16), .align_mul = 64,
                               .align_offset = (offset + i * 16) % 64);
   }
}

void
radv_load_otw_matrix(struct radv_device *device, nir_builder *b, nir_def *instance_addr, nir_def **out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   unsigned offset = offsetof(struct radv_bvh_instance_node, otw_matrix);
   if (radv_use_bvh8(pdev))
      offset =
         sizeof(struct radv_gfx12_instance_node) + offsetof(struct radv_gfx12_instance_node_user_data, otw_matrix);

   for (unsigned i = 0; i < 3; ++i) {
      out[i] = nir_load_global(b, 4, 32, nir_iadd_imm(b, instance_addr, offset + i * 16), .align_mul = 64,
                               .align_offset = (offset + i * 16) % 64);
   }
}

nir_def *
radv_load_custom_instance(struct radv_device *device, nir_builder *b, nir_def *instance_addr)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (radv_use_bvh8(pdev)) {
      return nir_load_global(b, 1, 32,
                             nir_iadd_imm(b, instance_addr,
                                          sizeof(struct radv_gfx12_instance_node) +
                                             offsetof(struct radv_gfx12_instance_node_user_data, custom_instance)));
   }

   return nir_iand_imm(
      b,
      nir_load_global(
         b, 1, 32, nir_iadd_imm(b, instance_addr, offsetof(struct radv_bvh_instance_node, custom_instance_and_mask))),
      0xFFFFFF);
}

nir_def *
radv_load_instance_id(struct radv_device *device, nir_builder *b, nir_def *instance_addr)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (radv_use_bvh8(pdev)) {
      return nir_load_global(b, 1, 32,
                             nir_iadd_imm(b, instance_addr,
                                          sizeof(struct radv_gfx12_instance_node) +
                                             offsetof(struct radv_gfx12_instance_node_user_data, instance_index)));
   }

   return nir_load_global(b, 1, 32,
                          nir_iadd_imm(b, instance_addr, offsetof(struct radv_bvh_instance_node, instance_id)));
}

/* When a hit is opaque the any_hit shader is skipped for this hit and the hit
 * is assumed to be an actual hit. */
static nir_def *
hit_is_opaque(nir_builder *b, nir_def *sbt_offset_and_flags, const struct radv_ray_flags *ray_flags,
              nir_def *geometry_id_and_flags)
{
   nir_def *opaque = nir_uge_imm(b, nir_ior(b, geometry_id_and_flags, sbt_offset_and_flags),
                                 RADV_INSTANCE_FORCE_OPAQUE | RADV_INSTANCE_NO_FORCE_NOT_OPAQUE);
   opaque = nir_bcsel(b, ray_flags->force_opaque, nir_imm_true(b), opaque);
   opaque = nir_bcsel(b, ray_flags->force_not_opaque, nir_imm_false(b), opaque);
   return opaque;
}

static nir_def *
create_bvh_descriptor(nir_builder *b, const struct radv_physical_device *pdev, struct radv_ray_flags *ray_flags)
{
   /* We create a BVH descriptor that covers the entire memory range. That way we can always
    * use the same descriptor, which avoids divergence when different rays hit different
    * instances at the cost of having to use 64-bit node ids. */
   const uint64_t bvh_size = 1ull << 42;

   const uint32_t sort_triangles_first = radv_use_bvh8(pdev) ? BITFIELD_BIT(52 - 32) : 0;
   const uint32_t box_sort_enable = BITFIELD_BIT(63 - 32);
   const uint32_t triangle_return_mode = BITFIELD_BIT(120 - 96); /* Return IJ for triangles */

   uint32_t dword0 = 0;
   nir_def *dword1 = nir_imm_intN_t(b, sort_triangles_first | box_sort_enable, 32);
   uint32_t dword2 = (bvh_size - 1) & 0xFFFFFFFFu;
   uint32_t dword3 = ((bvh_size - 1) >> 32) | triangle_return_mode | (1u << 31);

   if (pdev->info.gfx_level >= GFX11) {
      /* Enable pointer flags on GFX11+ */
      dword3 |= BITFIELD_BIT(119 - 96);

      /* Instead of the default box sorting (closest point), use largest for terminate_on_first_hit rays and midpoint
       * for closest hit; this makes it more likely that the ray traversal will visit fewer nodes. */
      const uint32_t box_sort_largest = 1;
      const uint32_t box_sort_midpoint = 2;

      /* Only use largest/midpoint sorting when all invocations have the same ray flags, otherwise
       * fall back to the default closest point. */
      dword1 = nir_bcsel(b, nir_vote_any(b, 1, ray_flags->terminate_on_first_hit), dword1,
                         nir_imm_int(b, (box_sort_midpoint << 21) | sort_triangles_first | box_sort_enable));
      dword1 = nir_bcsel(b, nir_vote_all(b, 1, ray_flags->terminate_on_first_hit),
                         nir_imm_int(b, (box_sort_largest << 21) | sort_triangles_first | box_sort_enable), dword1);
   }

   if (radv_use_bvh8(pdev)) {
      /* compressed_format_en */
      dword3 |= BITFIELD_BIT(115 - 96);
      /* wide_sort_en */
      dword3 |= BITFIELD_BIT(117 - 96);
      /* instance_en */
      dword3 |= BITFIELD_BIT(118 - 96);
   }

   return nir_vec4(b, nir_imm_intN_t(b, dword0, 32), dword1, nir_imm_intN_t(b, dword2, 32),
                   nir_imm_intN_t(b, dword3, 32));
}

static void
insert_traversal_triangle_case(struct radv_device *device, nir_builder *b, const struct radv_ray_traversal_args *args,
                               const struct radv_ray_flags *ray_flags, nir_def *result, nir_def *bvh_node)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   if (!args->triangle_cb)
      return;

   struct radv_triangle_intersection intersection;
   intersection.t = nir_channel(b, result, 0);
   nir_def *div = nir_channel(b, result, 1);
   intersection.t = nir_fdiv(b, intersection.t, div);

   nir_def *tmax = nir_load_deref(b, args->vars.tmax);

   nir_push_if(b, nir_flt(b, intersection.t, tmax));
   {
      intersection.frontface = nir_fgt_imm(b, div, 0);
      nir_def *not_cull;
      if (pdev->info.gfx_level < GFX11 || radv_emulate_rt(pdev)) {
         nir_def *switch_ccw =
            nir_test_mask(b, nir_load_deref(b, args->vars.sbt_offset_and_flags), RADV_INSTANCE_TRIANGLE_FLIP_FACING);
         intersection.frontface = nir_ixor(b, intersection.frontface, switch_ccw);

         not_cull = ray_flags->no_skip_triangles;
         nir_def *not_facing_cull =
            nir_bcsel(b, intersection.frontface, ray_flags->no_cull_front, ray_flags->no_cull_back);

         not_cull = nir_iand(b, not_cull,
                             nir_ior(b, not_facing_cull,
                                     nir_test_mask(b, nir_load_deref(b, args->vars.sbt_offset_and_flags),
                                                   RADV_INSTANCE_TRIANGLE_FACING_CULL_DISABLE)));
      } else {
         not_cull = nir_imm_true(b);
      }

      nir_push_if(b, nir_iand(b,

                              nir_flt(b, args->tmin, intersection.t), not_cull));
      {
         intersection.base.node_addr = build_node_to_addr(device, b, bvh_node, false);
         nir_def *triangle_info = nir_load_global(
            b, 2, 32,
            nir_iadd_imm(b, intersection.base.node_addr, offsetof(struct radv_bvh_triangle_node, triangle_id)));
         intersection.base.primitive_id = nir_channel(b, triangle_info, 0);
         intersection.base.geometry_id_and_flags = nir_channel(b, triangle_info, 1);
         intersection.base.opaque = hit_is_opaque(b, nir_load_deref(b, args->vars.sbt_offset_and_flags), ray_flags,
                                                  intersection.base.geometry_id_and_flags);

         not_cull = nir_bcsel(b, intersection.base.opaque, ray_flags->no_cull_opaque, ray_flags->no_cull_no_opaque);
         nir_push_if(b, not_cull);
         {
            nir_def *divs[2] = {div, div};
            intersection.barycentrics = nir_fdiv(b, nir_channels(b, result, 0xc), nir_vec(b, divs, 2));

            args->triangle_cb(b, &intersection, args, ray_flags);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
insert_traversal_triangle_case_gfx12(struct radv_device *device, nir_builder *b,
                                     const struct radv_ray_traversal_args *args, const struct radv_ray_flags *ray_flags,
                                     nir_variable *intrinsic_result, nir_def *result, nir_def *global_bvh_node,
                                     nir_def *bvh_node)
{
   if (!args->triangle_cb)
      return;

   nir_def *t[2] = {
      nir_channel(b, result, 0),
      nir_channel(b, result, 4),
   };
   nir_def *triangle0_first = nir_flt(b, t[0], t[1]);

   nir_def *second_t = nir_bcsel(b, triangle0_first, t[1], t[0]);
   nir_def *second_iteration = nir_load_deref(b, args->vars.second_iteration);
   nir_def *revisit =
      nir_iand(b, nir_inot(b, second_iteration), nir_flt(b, second_t, nir_load_deref(b, args->vars.tmax)));
   nir_store_deref(b, args->vars.second_iteration, revisit, 0x1);

   if (args->use_bvh_stack_rtn) {
      nir_def *next_node = nir_bcsel(b, revisit, bvh_node, nir_imm_int(b, RADV_BVH_STACK_SKIP_0_TO_7));
      nir_def *comps[8];
      for (unsigned i = 0; i < 6; ++i)
         comps[i] = nir_channel(b, result, i);
      comps[6] = nir_imm_int(b, RADV_BVH_STACK_SKIP_0_TO_7);
      comps[7] = next_node;
      nir_store_var(b, intrinsic_result, nir_vec(b, comps, 8), 0xff);
   } else {
      nir_def *next_node = nir_bcsel(b, revisit, bvh_node, nir_imm_int(b, RADV_BVH_INVALID_NODE));
      nir_store_deref(b, args->vars.current_node, next_node, 0x1);
   }

   nir_def *triangle0 = nir_ixor(b, triangle0_first, second_iteration);

   struct radv_triangle_intersection intersection;
   intersection.t = nir_bcsel(b, triangle0, t[0], t[1]);

   nir_push_if(b, nir_iand(b, nir_flt(b, intersection.t, nir_load_deref(b, args->vars.tmax)),
                           nir_flt(b, args->tmin, intersection.t)));
   {
      nir_def *dword1 = nir_bcsel(b, triangle0, nir_channel(b, result, 1), nir_channel(b, result, 5));
      nir_def *dword2 = nir_bcsel(b, triangle0, nir_channel(b, result, 2), nir_channel(b, result, 6));
      nir_def *dword3 = nir_bcsel(b, triangle0, nir_channel(b, result, 3), nir_channel(b, result, 7));

      intersection.frontface = nir_inot(b, nir_test_mask(b, dword3, 1));
      intersection.base.node_addr = build_node_to_addr(device, b, global_bvh_node, false);
      intersection.base.primitive_id = nir_ishr_imm(b, dword3, 1);
      intersection.base.geometry_id_and_flags =
         nir_ishr_imm(b, nir_bcsel(b, triangle0, nir_channel(b, result, 8), nir_channel(b, result, 9)), 2);
      intersection.base.opaque = nir_inot(b, nir_test_mask(b, dword2, 1u << 31));
      intersection.barycentrics = nir_fabs(b, nir_vec2(b, dword1, dword2));

      nir_push_if(b, nir_bcsel(b, intersection.base.opaque, ray_flags->no_cull_opaque, ray_flags->no_cull_no_opaque));
      {
         args->triangle_cb(b, &intersection, args, ray_flags);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
insert_traversal_aabb_case(struct radv_device *device, nir_builder *b, const struct radv_ray_traversal_args *args,
                           const struct radv_ray_flags *ray_flags, nir_def *bvh_node)
{
   if (!args->aabb_cb)
      return;

   nir_push_if(b, ray_flags->no_skip_aabbs);
   {
      struct radv_leaf_intersection intersection;
      intersection.node_addr = build_node_to_addr(device, b, bvh_node, false);
      nir_def *triangle_info = nir_load_global(
         b, 2, 32, nir_iadd_imm(b, intersection.node_addr, offsetof(struct radv_bvh_aabb_node, primitive_id)));
      intersection.primitive_id = nir_channel(b, triangle_info, 0);
      intersection.geometry_id_and_flags = nir_channel(b, triangle_info, 1);
      intersection.opaque = hit_is_opaque(b, nir_load_deref(b, args->vars.sbt_offset_and_flags), ray_flags,
                                          intersection.geometry_id_and_flags);

      nir_push_if(b, nir_bcsel(b, intersection.opaque, ray_flags->no_cull_opaque, ray_flags->no_cull_no_opaque));
      {
         args->aabb_cb(b, &intersection, args);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
insert_traversal_aabb_case_gfx12(struct radv_device *device, nir_builder *b, const struct radv_ray_traversal_args *args,
                                 const struct radv_ray_flags *ray_flags, nir_def *result, nir_def *bvh_node)
{
   if (!args->aabb_cb)
      return;

   struct radv_leaf_intersection intersection;
   intersection.node_addr = build_node_to_addr(device, b, bvh_node, false);
   intersection.primitive_id = nir_ishr_imm(b, nir_channel(b, result, 3), 1);
   intersection.geometry_id_and_flags = nir_ishr_imm(b, nir_channel(b, result, 8), 2);
   intersection.opaque = nir_inot(b, nir_test_mask(b, nir_channel(b, result, 2), 1u << 31));

   nir_push_if(b, nir_bcsel(b, intersection.opaque, ray_flags->no_cull_opaque, ray_flags->no_cull_no_opaque));
   {
      args->aabb_cb(b, &intersection, args);
   }
   nir_pop_if(b, NULL);
}

static nir_def *
fetch_parent_node(struct radv_device *device, nir_builder *b, nir_def *bvh, nir_def *node)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_def *offset = nir_iadd_imm(b, nir_imul_imm(b, nir_udiv_imm(b, node, radv_use_bvh8(pdev) ? 16 : 8), 4), 4);
   return nir_load_global(b, 1, 32, nir_isub(b, bvh, nir_u2u64(b, offset)), .align_mul = 4);
}

static nir_def *
radv_test_flag(nir_builder *b, const struct radv_ray_traversal_args *args, uint32_t flag, bool set)
{
   nir_def *result;
   if (args->set_flags & flag)
      result = nir_imm_true(b);
   else if (args->unset_flags & flag)
      result = nir_imm_false(b);
   else
      result = nir_test_mask(b, args->flags, flag);

   return set ? result : nir_inot(b, result);
}

static nir_def *
build_bvh_base(nir_builder *b, const struct radv_physical_device *pdev, nir_def *base_addr, nir_def *ptr_flags,
               bool overwrite)
{
   if (pdev->info.gfx_level < GFX11 || radv_emulate_rt(pdev))
      return base_addr;

   nir_def *base_addr_vec = nir_unpack_64_2x32(b, base_addr);
   nir_def *addr_hi = nir_channel(b, base_addr_vec, 1);
   if (overwrite)
      addr_hi = nir_bitfield_insert(b, addr_hi, ptr_flags, nir_imm_int(b, 22), nir_imm_int(b, 10));
   else
      addr_hi = nir_ior(b, addr_hi, nir_ishl_imm(b, ptr_flags, 22));
   return nir_pack_64_2x32(b, nir_vector_insert_imm(b, base_addr_vec, addr_hi, 1));
}

static void
build_instance_exit(nir_builder *b, const struct radv_physical_device *pdev, const struct radv_ray_traversal_args *args,
                    nir_def *stack_instance_exit, nir_def *ptr_flags)
{
   nir_def *root_instance_exit = nir_iand(
      b, nir_ieq_imm(b, nir_load_deref(b, args->vars.current_node), RADV_BVH_INVALID_NODE),
      nir_ieq(b, nir_load_deref(b, args->vars.previous_node), nir_load_deref(b, args->vars.instance_bottom_node)));
   nir_if *instance_exit = nir_push_if(b, nir_ior(b, stack_instance_exit, root_instance_exit));
   instance_exit->control = nir_selection_control_dont_flatten;
   {
      if (radv_use_bvh8(pdev) && args->use_bvh_stack_rtn)
         nir_store_deref(b, args->vars.stack,
                         nir_ior_imm(b, nir_load_deref(b, args->vars.stack), RADV_BVH_STACK_FLAG_TLAS_POP), 0x1);
      else
         nir_store_deref(b, args->vars.top_stack, nir_imm_int(b, -1), 1);
      nir_store_deref(b, args->vars.previous_node, nir_load_deref(b, args->vars.instance_top_node), 1);
      nir_store_deref(b, args->vars.instance_bottom_node, nir_imm_int(b, RADV_BVH_NO_INSTANCE_ROOT), 1);

      nir_def *root_bvh_base =
         radv_use_bvh8(pdev) ? args->root_bvh_base : build_bvh_base(b, pdev, args->root_bvh_base, ptr_flags, true);

      nir_store_deref(b, args->vars.bvh_base, root_bvh_base, 0x1);
      nir_store_deref(b, args->vars.origin, args->origin, 7);
      nir_store_deref(b, args->vars.dir, args->dir, 7);
      nir_store_deref(b, args->vars.inv_dir, nir_frcp(b, args->dir), 7);
   }
   nir_pop_if(b, NULL);
}

nir_def *
radv_build_ray_traversal(struct radv_device *device, nir_builder *b, const struct radv_ray_traversal_args *args)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_variable *incomplete = nir_local_variable_create(b->impl, glsl_bool_type(), "incomplete");
   nir_store_var(b, incomplete, nir_imm_true(b), 0x1);
   nir_variable *intrinsic_result = nir_local_variable_create(b->impl, glsl_uvec4_type(), "intrinsic_result");
   nir_variable *last_visited_node = nir_local_variable_create(b->impl, glsl_uint_type(), "last_visited_node");

   struct radv_ray_flags ray_flags = {
      .force_opaque = radv_test_flag(b, args, SpvRayFlagsOpaqueKHRMask, true),
      .force_not_opaque = radv_test_flag(b, args, SpvRayFlagsNoOpaqueKHRMask, true),
      .terminate_on_first_hit = radv_test_flag(b, args, SpvRayFlagsTerminateOnFirstHitKHRMask, true),
      .no_cull_front = radv_test_flag(b, args, SpvRayFlagsCullFrontFacingTrianglesKHRMask, false),
      .no_cull_back = radv_test_flag(b, args, SpvRayFlagsCullBackFacingTrianglesKHRMask, false),
      .no_cull_opaque = radv_test_flag(b, args, SpvRayFlagsCullOpaqueKHRMask, false),
      .no_cull_no_opaque = radv_test_flag(b, args, SpvRayFlagsCullNoOpaqueKHRMask, false),
      .no_skip_triangles = radv_test_flag(b, args, SpvRayFlagsSkipTrianglesKHRMask, false),
      .no_skip_aabbs = radv_test_flag(b, args, SpvRayFlagsSkipAABBsKHRMask, false),
   };

   nir_def *ptr_flags =
      nir_iand_imm(b, args->flags, ~(SpvRayFlagsTerminateOnFirstHitKHRMask | SpvRayFlagsSkipClosestHitShaderKHRMask));

   nir_store_deref(b, args->vars.bvh_base,
                   build_bvh_base(b, pdev, nir_load_deref(b, args->vars.bvh_base), ptr_flags, true), 0x1);

   nir_def *desc = create_bvh_descriptor(b, pdev, &ray_flags);
   nir_def *vec3ones = nir_imm_vec3(b, 1.0, 1.0, 1.0);

   nir_push_loop(b);
   {
      /* When exiting instances via stack, current_node won't ever be invalid with ds_bvh_stack_rtn */
      if (args->use_bvh_stack_rtn) {
         /* Early-exit when the stack is empty and there are no more nodes to process. */
         nir_push_if(b, nir_ieq_imm(b, nir_load_deref(b, args->vars.current_node), RADV_BVH_STACK_TERMINAL_NODE));
         {
            nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
         build_instance_exit(b, pdev, args,
                             nir_ilt(b, nir_load_deref(b, args->vars.stack), nir_load_deref(b, args->vars.top_stack)),
                             ptr_flags);
      }

      nir_push_if(b, nir_ieq_imm(b, nir_load_deref(b, args->vars.current_node), RADV_BVH_INVALID_NODE));
      {
         /* Early exit if we never overflowed the stack, to avoid having to backtrack to
          * the root for no reason. */
         if (!args->use_bvh_stack_rtn) {
            nir_push_if(b, nir_ilt_imm(b, nir_load_deref(b, args->vars.stack), args->stack_base + args->stack_stride));
            {
               nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);
            build_instance_exit(
               b, pdev, args, nir_ige(b, nir_load_deref(b, args->vars.top_stack), nir_load_deref(b, args->vars.stack)),
               ptr_flags);
         }

         nir_def *overflow_cond =
            nir_ige(b, nir_load_deref(b, args->vars.stack_low_watermark), nir_load_deref(b, args->vars.stack));
         /* ds_bvh_stack_rtn returns 0xFFFFFFFF if and only if there was a stack overflow. */
         if (args->use_bvh_stack_rtn)
            overflow_cond = nir_imm_true(b);

         nir_push_if(b, overflow_cond);
         {
            /* Fix up the stack pointer if we overflowed. The HW will decrement the stack pointer by one in that case. */
            if (args->use_bvh_stack_rtn)
               nir_store_deref(b, args->vars.stack, nir_iadd_imm(b, nir_load_deref(b, args->vars.stack), 1), 0x1);
            nir_def *prev = nir_load_deref(b, args->vars.previous_node);
            nir_def *bvh_addr = build_node_to_addr(device, b, nir_load_deref(b, args->vars.bvh_base), true);

            nir_def *parent = fetch_parent_node(device, b, bvh_addr, prev);
            nir_push_if(b, nir_ieq_imm(b, parent, RADV_BVH_INVALID_NODE));
            {
               nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);
            nir_store_deref(b, args->vars.current_node, parent, 0x1);
         }
         nir_push_else(b, NULL);
         {
            if (!args->use_bvh_stack_rtn) {
               nir_store_deref(b, args->vars.stack,
                               nir_iadd_imm(b, nir_load_deref(b, args->vars.stack), -args->stack_stride), 1);

               nir_def *stack_ptr =
                  nir_umod_imm(b, nir_load_deref(b, args->vars.stack), args->stack_stride * args->stack_entries);
               nir_def *bvh_node = args->stack_load_cb(b, stack_ptr, args);
               nir_store_deref(b, args->vars.current_node, bvh_node, 0x1);
            }
            nir_store_deref(b, args->vars.previous_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         nir_store_deref(b, args->vars.previous_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
      }
      nir_pop_if(b, NULL);

      nir_def *bvh_node = nir_load_deref(b, args->vars.current_node);
      if (args->use_bvh_stack_rtn)
         nir_store_var(b, last_visited_node, nir_imm_int(b, RADV_BVH_STACK_TERMINAL_NODE), 0x1);
      else
         nir_store_deref(b, args->vars.current_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);

      nir_def *prev_node = nir_load_deref(b, args->vars.previous_node);
      nir_store_deref(b, args->vars.previous_node, bvh_node, 0x1);

      nir_def *global_bvh_node = nir_iadd(b, nir_load_deref(b, args->vars.bvh_base), nir_u2u64(b, bvh_node));

      bool has_result = false;
      if (pdev->info.has_image_bvh_intersect_ray && !radv_emulate_rt(pdev)) {
         nir_store_var(
            b, intrinsic_result,
            nir_bvh64_intersect_ray_amd(b, 32, desc, nir_unpack_64_2x32(b, global_bvh_node),
                                        nir_load_deref(b, args->vars.tmax), nir_load_deref(b, args->vars.origin),
                                        nir_load_deref(b, args->vars.dir), nir_load_deref(b, args->vars.inv_dir)),
            0xf);
         has_result = true;
      }

      nir_push_if(b, nir_test_mask(b, bvh_node, BITFIELD64_BIT(ffs(radv_bvh_node_box16) - 1)));
      {
         nir_push_if(b, nir_test_mask(b, bvh_node, BITFIELD64_BIT(ffs(radv_bvh_node_instance) - 1)));
         {
            nir_push_if(b, nir_test_mask(b, bvh_node, BITFIELD64_BIT(ffs(radv_bvh_node_aabb) - 1)));
            {
               insert_traversal_aabb_case(device, b, args, &ray_flags, global_bvh_node);
            }
            nir_push_else(b, NULL);
            {
               if (args->vars.iteration_instance_count) {
                  nir_def *iteration_instance_count = nir_load_deref(b, args->vars.iteration_instance_count);
                  iteration_instance_count = nir_iadd_imm(b, iteration_instance_count, 1 << 16);
                  nir_store_deref(b, args->vars.iteration_instance_count, iteration_instance_count, 0x1);
               }

               /* instance */
               nir_def *instance_node_addr = build_node_to_addr(device, b, global_bvh_node, false);
               nir_store_deref(b, args->vars.instance_addr, instance_node_addr, 1);

               nir_def *instance_data =
                  nir_load_global(b, 4, 32, instance_node_addr, .align_mul = 64, .align_offset = 0);

               nir_def *wto_matrix[3];
               radv_load_wto_matrix(device, b, instance_node_addr, wto_matrix);

               nir_store_deref(b, args->vars.sbt_offset_and_flags, nir_channel(b, instance_data, 3), 1);

               if (!args->ignore_cull_mask) {
                  nir_def *instance_and_mask = nir_channel(b, instance_data, 2);
                  nir_push_if(b, nir_ult(b, nir_iand(b, instance_and_mask, args->cull_mask), nir_imm_int(b, 1 << 24)));
                  {
                     if (!args->use_bvh_stack_rtn)
                        nir_jump(b, nir_jump_continue);
                  }
                  nir_push_else(b, NULL);
               }

               nir_store_deref(b, args->vars.top_stack, nir_load_deref(b, args->vars.stack), 1);

               /* If ray flags dictate a forced opaqueness/nonopaqueness, instance flags dictating the same are
                * meaningless.
                */
               uint32_t forced_opaqueness_mask = SpvRayFlagsOpaqueKHRMask | SpvRayFlagsNoOpaqueKHRMask;
               nir_def *instance_flag_mask =
                  nir_bcsel(b, nir_test_mask(b, ptr_flags, forced_opaqueness_mask),
                            nir_imm_int64(b, ~((uint64_t)forced_opaqueness_mask << 54ull)), nir_imm_int64(b, ~0ull));

               nir_def *instance_pointer = nir_pack_64_2x32(b, nir_trim_vector(b, instance_data, 2));
               instance_pointer = nir_iand(b, instance_pointer, instance_flag_mask);

               nir_store_deref(b, args->vars.bvh_base, build_bvh_base(b, pdev, instance_pointer, ptr_flags, false),
                               0x1);

               /* Push the instance root node onto the stack */
               if (args->use_bvh_stack_rtn) {
                  nir_store_var(b, last_visited_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
                  nir_store_var(b, intrinsic_result,
                                nir_imm_ivec4(b, RADV_BVH_ROOT_NODE, RADV_BVH_INVALID_NODE, RADV_BVH_INVALID_NODE,
                                              RADV_BVH_INVALID_NODE),
                                0xf);
               } else {
                  nir_store_deref(b, args->vars.current_node, nir_imm_int(b, RADV_BVH_ROOT_NODE), 0x1);
               }
               nir_store_deref(b, args->vars.instance_bottom_node, nir_imm_int(b, RADV_BVH_ROOT_NODE), 1);
               nir_store_deref(b, args->vars.instance_top_node, bvh_node, 1);

               /* Transform the ray into object space */
               nir_store_deref(b, args->vars.origin, nir_build_vec3_mat_mult(b, args->origin, wto_matrix, true), 7);
               nir_store_deref(b, args->vars.dir, nir_build_vec3_mat_mult(b, args->dir, wto_matrix, false), 7);
               nir_store_deref(b, args->vars.inv_dir, nir_fdiv(b, vec3ones, nir_load_deref(b, args->vars.dir)), 7);
               if (!args->ignore_cull_mask)
                  nir_pop_if(b, NULL);
            }
            nir_pop_if(b, NULL);
         }
         nir_push_else(b, NULL);
         {
            nir_def *result;
            if (has_result) {
               result = nir_load_var(b, intrinsic_result);
            } else {
               /* If we didn't run the intrinsic cause the hardware didn't support it,
                * emulate ray/box intersection here */
               result = intersect_ray_amd_software_box(
                  device, b, global_bvh_node, nir_load_deref(b, args->vars.tmax), nir_load_deref(b, args->vars.origin),
                  nir_load_deref(b, args->vars.dir), nir_load_deref(b, args->vars.inv_dir));
            }

            /* box */
            if (args->use_bvh_stack_rtn) {
               nir_store_var(b, last_visited_node, prev_node, 0x1);
            } else {
               nir_push_if(b, nir_ieq_imm(b, prev_node, RADV_BVH_INVALID_NODE));
               {
                  nir_def *new_nodes[4];
                  for (unsigned i = 0; i < 4; ++i)
                     new_nodes[i] = nir_channel(b, result, i);

                  for (unsigned i = 1; i < 4; ++i)
                     nir_push_if(b, nir_ine_imm(b, new_nodes[i], RADV_BVH_INVALID_NODE));

                  for (unsigned i = 4; i-- > 1;) {
                     nir_def *stack = nir_load_deref(b, args->vars.stack);
                     nir_def *stack_ptr = nir_umod_imm(b, stack, args->stack_entries * args->stack_stride);
                     args->stack_store_cb(b, stack_ptr, new_nodes[i], args);
                     nir_store_deref(b, args->vars.stack, nir_iadd_imm(b, stack, args->stack_stride), 1);

                     if (i == 1) {
                        nir_def *new_watermark = nir_iadd_imm(b, nir_load_deref(b, args->vars.stack),
                                                              -args->stack_entries * args->stack_stride);
                        new_watermark = nir_imax(b, nir_load_deref(b, args->vars.stack_low_watermark), new_watermark);
                        nir_store_deref(b, args->vars.stack_low_watermark, new_watermark, 0x1);
                     }

                     nir_pop_if(b, NULL);
                  }
                  nir_store_deref(b, args->vars.current_node, new_nodes[0], 0x1);
               }
               nir_push_else(b, NULL);
               {
                  nir_def *next = nir_imm_int(b, RADV_BVH_INVALID_NODE);
                  for (unsigned i = 0; i < 3; ++i) {
                     next = nir_bcsel(b, nir_ieq(b, prev_node, nir_channel(b, result, i)),
                                      nir_channel(b, result, i + 1), next);
                  }
                  nir_store_deref(b, args->vars.current_node, next, 0x1);
               }
               nir_pop_if(b, NULL);
            }
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         nir_def *result;
         if (has_result) {
            result = nir_load_var(b, intrinsic_result);
         } else {
            /* If we didn't run the intrinsic cause the hardware didn't support it,
             * emulate ray/tri intersection here */
            result = intersect_ray_amd_software_tri(
               device, b, global_bvh_node, nir_load_deref(b, args->vars.tmax), nir_load_deref(b, args->vars.origin),
               nir_load_deref(b, args->vars.dir), nir_load_deref(b, args->vars.inv_dir));
         }
         insert_traversal_triangle_case(device, b, args, &ray_flags, result, global_bvh_node);
      }
      nir_pop_if(b, NULL);

      if (args->vars.iteration_instance_count) {
         nir_def *iteration_instance_count = nir_load_deref(b, args->vars.iteration_instance_count);
         iteration_instance_count = nir_iadd_imm(b, iteration_instance_count, 1);
         nir_store_deref(b, args->vars.iteration_instance_count, iteration_instance_count, 0x1);
      }
      if (args->use_bvh_stack_rtn) {
         nir_def *stack_result =
            nir_bvh_stack_rtn_amd(b, 32, nir_load_deref(b, args->vars.stack), nir_load_var(b, last_visited_node),
                                  nir_load_var(b, intrinsic_result), .stack_size = args->stack_entries);
         nir_store_deref(b, args->vars.stack, nir_channel(b, stack_result, 0), 0x1);
         nir_store_deref(b, args->vars.current_node, nir_channel(b, stack_result, 1), 0x1);
      }

      if (args->vars.break_flag) {
         nir_push_if(b, nir_load_deref(b, args->vars.break_flag));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
      }
   }
   nir_pop_loop(b, NULL);

   return nir_load_var(b, incomplete);
}

nir_def *
radv_build_ray_traversal_gfx12(struct radv_device *device, nir_builder *b, const struct radv_ray_traversal_args *args)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   nir_variable *incomplete = nir_local_variable_create(b->impl, glsl_bool_type(), "incomplete");
   nir_store_var(b, incomplete, nir_imm_true(b), 0x1);
   nir_variable *intrinsic_result = nir_local_variable_create(b->impl, glsl_uvec_type(8), "intrinsic_result");
   nir_variable *last_visited_node = nir_local_variable_create(b->impl, glsl_uint_type(), "last_visited_node");

   struct radv_ray_flags ray_flags = {
      .force_opaque = radv_test_flag(b, args, SpvRayFlagsOpaqueKHRMask, true),
      .force_not_opaque = radv_test_flag(b, args, SpvRayFlagsNoOpaqueKHRMask, true),
      .terminate_on_first_hit = radv_test_flag(b, args, SpvRayFlagsTerminateOnFirstHitKHRMask, true),
      .no_cull_front = radv_test_flag(b, args, SpvRayFlagsCullFrontFacingTrianglesKHRMask, false),
      .no_cull_back = radv_test_flag(b, args, SpvRayFlagsCullBackFacingTrianglesKHRMask, false),
      .no_cull_opaque = radv_test_flag(b, args, SpvRayFlagsCullOpaqueKHRMask, false),
      .no_cull_no_opaque = radv_test_flag(b, args, SpvRayFlagsCullNoOpaqueKHRMask, false),
      .no_skip_triangles = radv_test_flag(b, args, SpvRayFlagsSkipTrianglesKHRMask, false),
      .no_skip_aabbs = radv_test_flag(b, args, SpvRayFlagsSkipAABBsKHRMask, false),
   };

   nir_def *desc = create_bvh_descriptor(b, pdev, &ray_flags);

   nir_push_loop(b);
   {
      /* When exiting instances via stack, current_node won't ever be invalid with ds_bvh_stack_rtn */
      if (args->use_bvh_stack_rtn) {
         /* Early-exit when the stack is empty and there are no more nodes to process. */
         nir_push_if(b, nir_ieq_imm(b, nir_load_deref(b, args->vars.current_node), RADV_BVH_STACK_TERMINAL_NODE));
         {
            nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
         build_instance_exit(b, pdev, args,
                             nir_test_mask(b, nir_load_deref(b, args->vars.stack), RADV_BVH_STACK_FLAG_TLAS_POP), NULL);
      }

      nir_push_if(b, nir_ieq_imm(b, nir_load_deref(b, args->vars.current_node), RADV_BVH_INVALID_NODE));
      {
         /* Early exit if we never overflowed the stack, to avoid having to backtrack to
          * the root for no reason. */
         if (!args->use_bvh_stack_rtn) {
            nir_push_if(b, nir_ilt_imm(b, nir_load_deref(b, args->vars.stack), args->stack_base + args->stack_stride));
            {
               nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);
            build_instance_exit(
               b, pdev, args, nir_ige(b, nir_load_deref(b, args->vars.top_stack), nir_load_deref(b, args->vars.stack)),
               NULL);
         }

         nir_def *overflow_cond =
            nir_ige(b, nir_load_deref(b, args->vars.stack_low_watermark), nir_load_deref(b, args->vars.stack));
         /* ds_bvh_stack_rtn returns 0xFFFFFFFF if and only if there was a stack overflow. */
         if (args->use_bvh_stack_rtn)
            overflow_cond = nir_imm_true(b);

         nir_push_if(b, overflow_cond);
         {
            nir_def *prev = nir_load_deref(b, args->vars.previous_node);

            nir_def *loaded_parent_id;
            nir_def *primitive_parent_id;
            nir_push_if(b, nir_test_mask(b, prev, BITFIELD64_BIT(ffs(radv_bvh_node_box16) - 1)));
            {
               nir_def *is_instance = nir_test_mask(b, prev, BITFIELD64_BIT(ffs(radv_bvh_node_instance) - 1));
               nir_def *field_offset = nir_bcsel(
                  b, is_instance,
                  nir_imm_int(
                     b, (int32_t)offsetof(struct radv_gfx12_instance_node, parent_id) - (radv_bvh_node_instance << 3)),
                  nir_imm_int(b,
                              (int32_t)offsetof(struct radv_gfx12_box_node, parent_id) - (radv_bvh_node_box32 << 3)));
               nir_def *offset = nir_iadd(b, nir_ishl_imm(b, prev, 3), field_offset);
               nir_def *bvh_addr = build_node_to_addr(device, b, nir_load_deref(b, args->vars.bvh_base), true);
               loaded_parent_id = nir_load_global(b, 1, 32, nir_iadd(b, bvh_addr, nir_u2u64(b, offset)));
            }
            nir_push_else(b, NULL);
            {
               primitive_parent_id = nir_load_deref(b, args->vars.parent_node);
            }
            nir_pop_if(b, NULL);
            nir_def *parent = nir_if_phi(b, loaded_parent_id, primitive_parent_id);

            nir_push_if(b, nir_ieq_imm(b, parent, RADV_BVH_INVALID_NODE));
            {
               nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);
            nir_store_deref(b, args->vars.current_node, parent, 0x1);
         }
         nir_push_else(b, NULL);
         {
            if (!args->use_bvh_stack_rtn) {
               nir_store_deref(b, args->vars.stack,
                               nir_iadd_imm(b, nir_load_deref(b, args->vars.stack), -args->stack_stride), 1);

               nir_def *stack_ptr =
                  nir_umod_imm(b, nir_load_deref(b, args->vars.stack), args->stack_stride * args->stack_entries);
               nir_def *bvh_node = args->stack_load_cb(b, stack_ptr, args);
               nir_store_deref(b, args->vars.current_node, bvh_node, 0x1);
            }
            nir_store_deref(b, args->vars.previous_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         nir_store_deref(b, args->vars.previous_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
      }
      nir_pop_if(b, NULL);

      nir_def *bvh_node = nir_load_deref(b, args->vars.current_node);

      nir_def *prev_node = nir_load_deref(b, args->vars.previous_node);
      nir_store_deref(b, args->vars.previous_node, bvh_node, 0x1);
      if (args->use_bvh_stack_rtn)
         nir_store_var(b, last_visited_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
      else
         nir_store_deref(b, args->vars.current_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);

      nir_def *global_bvh_node = nir_iadd(b, nir_load_deref(b, args->vars.bvh_base), nir_u2u64(b, bvh_node));

      nir_def *result =
         nir_bvh8_intersect_ray_amd(b, 32, desc, nir_unpack_64_2x32(b, nir_load_deref(b, args->vars.bvh_base)),
                                    nir_ishr_imm(b, args->cull_mask, 24), nir_load_deref(b, args->vars.tmax),
                                    nir_load_deref(b, args->vars.origin), nir_load_deref(b, args->vars.dir), bvh_node);
      nir_store_var(b, intrinsic_result, nir_channels(b, result, 0xff), 0xff);
      nir_store_deref(b, args->vars.origin, nir_channels(b, result, 0x7 << 10), 0x7);
      nir_store_deref(b, args->vars.dir, nir_channels(b, result, 0x7 << 13), 0x7);

      nir_push_if(b, nir_test_mask(b, bvh_node, BITFIELD64_BIT(ffs(radv_bvh_node_box16) - 1)));
      {
         nir_push_if(b, nir_test_mask(b, bvh_node, BITFIELD64_BIT(ffs(radv_bvh_node_instance) - 1)));
         {
            if (args->vars.iteration_instance_count) {
               nir_def *iteration_instance_count = nir_load_deref(b, args->vars.iteration_instance_count);
               iteration_instance_count = nir_iadd_imm(b, iteration_instance_count, 1 << 16);
               nir_store_deref(b, args->vars.iteration_instance_count, iteration_instance_count, 0x1);
            }

            nir_def *next_node = nir_iand_imm(b, nir_channel(b, result, 7), 0xff);
            nir_push_if(b, nir_ieq_imm(b, next_node, 0xff));
            {
               nir_store_deref(b, args->vars.origin, args->origin, 7);
               nir_store_deref(b, args->vars.dir, args->dir, 7);
               if (args->use_bvh_stack_rtn) {
                  nir_def *skip_0_7 = nir_imm_int(b, RADV_BVH_STACK_SKIP_0_TO_7);
                  nir_store_var(b, intrinsic_result,
                                nir_vector_insert_imm(b, nir_load_var(b, intrinsic_result), skip_0_7, 7), 0xff);
               } else {
                  nir_jump(b, nir_jump_continue);
               }
            }
            nir_push_else(b, NULL);
            {
               /* instance */
               nir_def *instance_node_addr = build_node_to_addr(device, b, global_bvh_node, false);
               nir_store_deref(b, args->vars.instance_addr, instance_node_addr, 1);

               nir_store_deref(b, args->vars.sbt_offset_and_flags, nir_channel(b, result, 6), 1);

               nir_store_deref(b, args->vars.top_stack, nir_load_deref(b, args->vars.stack), 1);
               nir_store_deref(b, args->vars.bvh_base, nir_pack_64_2x32(b, nir_channels(b, result, 0x3 << 2)), 1);

               /* Push the instance root node onto the stack */
               if (args->use_bvh_stack_rtn) {
                  nir_def *comps[8];
                  for (unsigned i = 0; i < 6; ++i)
                     comps[i] = nir_channel(b, result, i);
                  comps[6] = nir_imm_int(b, RADV_BVH_STACK_SKIP_0_TO_7);
                  comps[7] = next_node;
                  nir_store_var(b, intrinsic_result, nir_vec(b, comps, 8), 0xff);
               } else {
                  nir_store_deref(b, args->vars.current_node, next_node, 0x1);
               }
               nir_store_deref(b, args->vars.instance_bottom_node, next_node, 1);
               nir_store_deref(b, args->vars.instance_top_node, bvh_node, 1);
            }
            nir_pop_if(b, NULL);
         }
         nir_push_else(b, NULL);
         {
            nir_store_deref(b, args->vars.parent_node, bvh_node, 0x1);

            /* box */
            if (args->use_bvh_stack_rtn) {
               nir_store_var(b, last_visited_node, prev_node, 0x1);
            } else {
               nir_push_if(b, nir_ieq_imm(b, prev_node, RADV_BVH_INVALID_NODE));
               {
                  nir_def *new_nodes[8];
                  for (unsigned i = 0; i < 8; ++i)
                     new_nodes[i] = nir_channel(b, result, i);

                  for (unsigned i = 1; i < 8; ++i)
                     nir_push_if(b, nir_ine_imm(b, new_nodes[i], RADV_BVH_INVALID_NODE));

                  for (unsigned i = 8; i-- > 1;) {
                     nir_def *stack = nir_load_deref(b, args->vars.stack);
                     nir_def *stack_ptr = nir_umod_imm(b, stack, args->stack_entries * args->stack_stride);
                     args->stack_store_cb(b, stack_ptr, new_nodes[i], args);
                     nir_store_deref(b, args->vars.stack, nir_iadd_imm(b, stack, args->stack_stride), 1);

                     if (i == 1) {
                        nir_def *new_watermark = nir_iadd_imm(b, nir_load_deref(b, args->vars.stack),
                                                              -args->stack_entries * args->stack_stride);
                        new_watermark = nir_imax(b, nir_load_deref(b, args->vars.stack_low_watermark), new_watermark);
                        nir_store_deref(b, args->vars.stack_low_watermark, new_watermark, 0x1);
                     }

                     nir_pop_if(b, NULL);
                  }
                  nir_store_deref(b, args->vars.current_node, new_nodes[0], 0x1);
               }
               nir_push_else(b, NULL);
               {
                  nir_def *next = nir_imm_int(b, RADV_BVH_INVALID_NODE);
                  for (unsigned i = 0; i < 7; ++i) {
                     next = nir_bcsel(b, nir_ieq(b, prev_node, nir_channel(b, result, i)),
                                      nir_channel(b, result, i + 1), next);
                  }
                  nir_store_deref(b, args->vars.current_node, next, 0x1);
               }
               nir_pop_if(b, NULL);
            }
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         if (args->use_bvh_stack_rtn) {
            nir_def *skip_0_7 = nir_imm_int(b, RADV_BVH_STACK_SKIP_0_TO_7);
            nir_store_var(b, intrinsic_result, nir_vector_insert_imm(b, nir_load_var(b, intrinsic_result), skip_0_7, 7),
                          0xff);
         }

         nir_push_if(b, nir_test_mask(b, nir_channel(b, result, 1), 1u << 31));
         {
            nir_push_if(b, ray_flags.no_skip_aabbs);
            insert_traversal_aabb_case_gfx12(device, b, args, &ray_flags, result, global_bvh_node);
            nir_pop_if(b, NULL);
         }
         nir_push_else(b, NULL);
         {
            nir_push_if(b, ray_flags.no_skip_triangles);
            insert_traversal_triangle_case_gfx12(device, b, args, &ray_flags, intrinsic_result, result, global_bvh_node,
                                                 bvh_node);
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);

      if (args->vars.iteration_instance_count) {
         nir_def *iteration_instance_count = nir_load_deref(b, args->vars.iteration_instance_count);
         iteration_instance_count = nir_iadd_imm(b, iteration_instance_count, 1);
         nir_store_deref(b, args->vars.iteration_instance_count, iteration_instance_count, 0x1);
      }

      if (args->use_bvh_stack_rtn) {
         nir_def *stack_result;
         stack_result =
            nir_bvh_stack_rtn_amd(b, 32, nir_load_deref(b, args->vars.stack), nir_load_var(b, last_visited_node),
                                  nir_load_var(b, intrinsic_result), .stack_size = args->stack_entries);
         nir_store_deref(b, args->vars.stack, nir_channel(b, stack_result, 0), 0x1);
         nir_store_deref(b, args->vars.current_node, nir_channel(b, stack_result, 1), 0x1);
      }

      if (args->vars.break_flag) {
         nir_push_if(b, nir_load_deref(b, args->vars.break_flag));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);
      }
   }
   nir_pop_loop(b, NULL);

   return nir_load_var(b, incomplete);
}
