/*
 * Copyright © 2021 Google
 * Copyright © 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "lvp_nir.h"
#include "lvp_acceleration_structure.h"
#include "lvp_private.h"

#include "compiler/spirv/spirv.h"

#include <float.h>
#include <math.h>

nir_def *
lvp_mul_vec3_mat(nir_builder *b, nir_def *vec, nir_def *matrix[], bool translation)
{
   nir_def *result_components[3] = {
      nir_channel(b, matrix[0], 3),
      nir_channel(b, matrix[1], 3),
      nir_channel(b, matrix[2], 3),
   };
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         nir_def *v =
            nir_fmul(b, nir_channels(b, vec, 1 << j), nir_channels(b, matrix[i], 1 << j));
         result_components[i] = (translation || j) ? nir_fadd(b, result_components[i], v) : v;
      }
   }
   return nir_vec(b, result_components, 3);
}

static nir_def *
lvp_load_node_data(nir_builder *b, nir_def *addr, nir_def **node_data, uint32_t offset)
{
   if (offset < LVP_BVH_NODE_PREFETCH_SIZE && node_data)
      return node_data[offset / 4];

   return nir_load_global(b, 1, 32, nir_iadd_imm(b, addr, offset));
}

void
lvp_load_wto_matrix(nir_builder *b, nir_def *instance_addr, nir_def **node_data, nir_def **out)
{
   unsigned offset = offsetof(struct lvp_bvh_instance_node, wto_matrix);
   for (unsigned i = 0; i < 3; ++i) {
      out[i] = nir_load_global(b, 4, 32, nir_iadd_imm(b, instance_addr, offset + i * 16));
      out[i] = nir_vec4(b,
         lvp_load_node_data(b, instance_addr, node_data, offset + i * 16 + 0),
         lvp_load_node_data(b, instance_addr, node_data, offset + i * 16 + 4),
         lvp_load_node_data(b, instance_addr, node_data, offset + i * 16 + 8),
         lvp_load_node_data(b, instance_addr, node_data, offset + i * 16 + 12)
      );
   }
}

nir_def *
lvp_load_vertex_position(nir_builder *b, nir_def *primitive_addr, uint32_t index)
{
   return nir_load_global(b, 3, 32, nir_iadd_imm(b, primitive_addr, index * 3 * sizeof(float)));
}

static nir_def *
lvp_build_intersect_ray_box(nir_builder *b, nir_def **node_data, nir_def *ray_tmax,
                            nir_def *origin, nir_def *dir, nir_def *inv_dir)
{
   const struct glsl_type *vec2_type = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
   const struct glsl_type *uvec2_type = glsl_vector_type(GLSL_TYPE_UINT, 2);

   nir_variable *distances =
      nir_variable_create(b->shader, nir_var_shader_temp, vec2_type, "distances");
   nir_store_var(b, distances, nir_imm_vec2(b, INFINITY, INFINITY), 0xf);

   nir_variable *child_indices =
      nir_variable_create(b->shader, nir_var_shader_temp, uvec2_type, "child_indices");
   nir_store_var(b, child_indices, nir_imm_ivec2(b, 0xffffffffu, 0xffffffffu), 0xf);

   inv_dir = nir_bcsel(b, nir_feq_imm(b, dir, 0), nir_imm_float(b, FLT_MAX), inv_dir);

   for (int i = 0; i < 2; i++) {
      const uint32_t child_offset = offsetof(struct lvp_bvh_box_node, children[i]);
      const uint32_t coord_offsets[2] = {
         offsetof(struct lvp_bvh_box_node, bounds[i].min.x),
         offsetof(struct lvp_bvh_box_node, bounds[i].max.x),
      };

      nir_def *child_index = lvp_load_node_data(b, NULL, node_data, child_offset);

      nir_def *node_coords[2] = {
         nir_vec3(b,
            lvp_load_node_data(b, NULL, node_data, coord_offsets[0] + 0),
            lvp_load_node_data(b, NULL, node_data, coord_offsets[0] + 4),
            lvp_load_node_data(b, NULL, node_data, coord_offsets[0] + 8)
         ),
         nir_vec3(b,
            lvp_load_node_data(b, NULL, node_data, coord_offsets[1] + 0),
            lvp_load_node_data(b, NULL, node_data, coord_offsets[1] + 4),
            lvp_load_node_data(b, NULL, node_data, coord_offsets[1] + 8)
         ),
      };

      /* If x of the aabb min is NaN, then this is an inactive aabb.
       * We don't need to care about any other components being NaN as that is UB.
       * https://docs.vulkan.org/spec/latest/chapters/accelstructures.html#acceleration-structure-inactive-prims
       */
      nir_def *min_x = nir_channel(b, node_coords[0], 0);
      nir_def *min_x_is_not_nan =
         nir_inot(b, nir_fneu(b, min_x, min_x)); /* NaN != NaN -> true */

      nir_def *bound0 = nir_fmul(b, nir_fsub(b, node_coords[0], origin), inv_dir);
      nir_def *bound1 = nir_fmul(b, nir_fsub(b, node_coords[1], origin), inv_dir);

      nir_def *tmin =
         nir_fmax(b,
                  nir_fmax(b, nir_fmin(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
                           nir_fmin(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
                  nir_fmin(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      nir_def *tmax =
         nir_fmin(b,
                  nir_fmin(b, nir_fmax(b, nir_channel(b, bound0, 0), nir_channel(b, bound1, 0)),
                           nir_fmax(b, nir_channel(b, bound0, 1), nir_channel(b, bound1, 1))),
                  nir_fmax(b, nir_channel(b, bound0, 2), nir_channel(b, bound1, 2)));

      nir_push_if(b,
                  nir_iand(b, min_x_is_not_nan,
                           nir_iand(b, nir_fge(b, tmax, nir_fmax(b, nir_imm_float(b, 0.0f), tmin)),
                                    nir_flt(b, tmin, ray_tmax))));
      {
         nir_def *new_child_indices[2] = {child_index, child_index};
         nir_store_var(b, child_indices, nir_vec(b, new_child_indices, 2), 1u << i);

         nir_def *new_distances[2] = {tmin, tmin};
         nir_store_var(b, distances, nir_vec(b, new_distances, 2), 1u << i);
      }
      nir_pop_if(b, NULL);
   }

   nir_def *ssa_distances = nir_load_var(b, distances);
   nir_def *ssa_indices = nir_load_var(b, child_indices);
   nir_push_if(b, nir_flt(b, nir_channel(b, ssa_distances, 1), nir_channel(b, ssa_distances, 0)));
   {
      nir_store_var(b, child_indices,
                    nir_vec2(b, nir_channel(b, ssa_indices, 1), nir_channel(b, ssa_indices, 0)),
                    0b11);
   }
   nir_pop_if(b, NULL);

   return nir_load_var(b, child_indices);
}

static nir_def *
lvp_build_intersect_edge(nir_builder *b, nir_def *v0_x, nir_def *v0_y, nir_def *v1_x, nir_def *v1_y)
{
   /* Test (1 0 0) direction: t = <v1-v0, (1 0 0)> */
   nir_def *t_x = nir_fsub(b, v1_x, v0_x);
   nir_def *test_y = nir_feq_imm(b, t_x, 0.0);
   /* Test (0 1 0) direction: t = <v1-v0, (0 1 0)> */
   nir_def *t_y = nir_fsub(b, v1_y, v0_y);

   return nir_bcsel(b, test_y, nir_flt_imm(b, t_y, 0.0), nir_flt_imm(b, t_x, 0.0));
}

static nir_def *
lvp_build_intersect_vertex(nir_builder *b, nir_def *v0_x, nir_def *v1_x, nir_def *v2_x)
{
   /* Choose n=(1 0 0) to simplify the dot product. */
   nir_def *edge0 = nir_fsub(b, v1_x, v0_x);
   nir_def *edge1 = nir_fsub(b, v2_x, v0_x);
   return nir_iand(b, nir_fle_imm(b, edge0, 0.0), nir_fgt_imm(b, edge1, 0.0));
}

static nir_def *
lvp_build_intersect_ray_tri(nir_builder *b, nir_def **node_data, nir_def *ray_tmax,
                            nir_def *origin, nir_def *dir, nir_def *inv_dir)
{
   const struct glsl_type *vec4_type = glsl_vector_type(GLSL_TYPE_FLOAT, 4);

   const uint32_t coord_offsets[3] = {
      offsetof(struct lvp_bvh_triangle_node, coords[0]),
      offsetof(struct lvp_bvh_triangle_node, coords[1]),
      offsetof(struct lvp_bvh_triangle_node, coords[2]),
   };

   nir_def *node_coords[3] = {
      nir_vec3(b,
         lvp_load_node_data(b, NULL, node_data, coord_offsets[0] + 0),
         lvp_load_node_data(b, NULL, node_data, coord_offsets[0] + 4),
         lvp_load_node_data(b, NULL, node_data, coord_offsets[0] + 8)
      ),
      nir_vec3(b,
         lvp_load_node_data(b, NULL, node_data, coord_offsets[1] + 0),
         lvp_load_node_data(b, NULL, node_data, coord_offsets[1] + 4),
         lvp_load_node_data(b, NULL, node_data, coord_offsets[1] + 8)
      ),
      nir_vec3(b,
         lvp_load_node_data(b, NULL, node_data, coord_offsets[2] + 0),
         lvp_load_node_data(b, NULL, node_data, coord_offsets[2] + 4),
         lvp_load_node_data(b, NULL, node_data, coord_offsets[2] + 8)
      ),
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
   nir_def *kz = nir_bcsel(
      b, nir_fge(b, abs_dirs[0], abs_dirs[1]),
      nir_bcsel(b, nir_fge(b, abs_dirs[0], abs_dirs[2]), nir_imm_int(b, 0), nir_imm_int(b, 2)),
      nir_bcsel(b, nir_fge(b, abs_dirs[1], abs_dirs[2]), nir_imm_int(b, 1), nir_imm_int(b, 2)));
   nir_def *kx = nir_imod_imm(b, nir_iadd_imm(b, kz, 1), 3);
   nir_def *ky = nir_imod_imm(b, nir_iadd_imm(b, kx, 1), 3);
   nir_def *k_indices[3] = {kx, ky, kz};
   nir_def *k = nir_vec(b, k_indices, 3);

   /* Swap kx and ky dimensions to preserve winding order */
   unsigned swap_xy_swizzle[4] = {1, 0, 2, 3};
   k = nir_bcsel(b, nir_flt_imm(b, nir_vector_extract(b, dir, kz), 0.0f),
                 nir_swizzle(b, k, swap_xy_swizzle, 3), k);

   kx = nir_channel(b, k, 0);
   ky = nir_channel(b, k, 1);
   kz = nir_channel(b, k, 2);

   /* Calculate shear constants */
   nir_def *sz = nir_frcp(b, nir_vector_extract(b, dir, kz));
   nir_def *sx = nir_fmul(b, nir_vector_extract(b, dir, kx), sz);
   nir_def *sy = nir_fmul(b, nir_vector_extract(b, dir, ky), sz);

   /* Calculate vertices relative to ray origin */
   nir_def *v_a = nir_fsub(b, node_coords[0], origin);
   nir_def *v_b = nir_fsub(b, node_coords[1], origin);
   nir_def *v_c = nir_fsub(b, node_coords[2], origin);

   /* Perform shear and scale */
   nir_def *ax =
      nir_fsub(b, nir_vector_extract(b, v_a, kx), nir_fmul(b, sx, nir_vector_extract(b, v_a, kz)));
   nir_def *ay =
      nir_fsub(b, nir_vector_extract(b, v_a, ky), nir_fmul(b, sy, nir_vector_extract(b, v_a, kz)));
   nir_def *bx =
      nir_fsub(b, nir_vector_extract(b, v_b, kx), nir_fmul(b, sx, nir_vector_extract(b, v_b, kz)));
   nir_def *by =
      nir_fsub(b, nir_vector_extract(b, v_b, ky), nir_fmul(b, sy, nir_vector_extract(b, v_b, kz)));
   nir_def *cx =
      nir_fsub(b, nir_vector_extract(b, v_c, kx), nir_fmul(b, sx, nir_vector_extract(b, v_c, kz)));
   nir_def *cy =
      nir_fsub(b, nir_vector_extract(b, v_c, ky), nir_fmul(b, sy, nir_vector_extract(b, v_c, kz)));

   nir_def *u = nir_fsub(b, nir_fmul(b, cx, by), nir_fmul(b, cy, bx));
   nir_def *v = nir_fsub(b, nir_fmul(b, ax, cy), nir_fmul(b, ay, cx));
   nir_def *w = nir_fsub(b, nir_fmul(b, bx, ay), nir_fmul(b, by, ax));

   /* Perform edge tests. */
   nir_def *cond_back = nir_ior(b, nir_ior(b, nir_flt_imm(b, u, 0.0f), nir_flt_imm(b, v, 0.0f)),
                                    nir_flt_imm(b, w, 0.0f));

   nir_def *cond_front = nir_ior(
      b, nir_ior(b, nir_fgt_imm(b, u, 0.0f), nir_fgt_imm(b, v, 0.0f)), nir_fgt_imm(b, w, 0.0f));

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
      nir_def *intersect_edge_a = nir_iand(b, is_edge_a, lvp_build_intersect_edge(b, bx, by, cx, cy));
      nir_def *intersect_edge_b = nir_iand(b, is_edge_b, lvp_build_intersect_edge(b, cx, cy, ax, ay));
      nir_def *intersect_edge_c = nir_iand(b, is_edge_c, lvp_build_intersect_edge(b, ax, ay, bx, by));
      intersect_edge = nir_iand(b, intersect_edge, nir_ior(b, nir_ior(b, intersect_edge_a, intersect_edge_b), intersect_edge_c));

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
      nir_def *intersect_vertex_a = nir_iand(b, is_vertex_a, lvp_build_intersect_vertex(b, ax, bx, cx));
      nir_def *intersect_vertex_b = nir_iand(b, is_vertex_b, lvp_build_intersect_vertex(b, bx, cx, ax));
      nir_def *intersect_vertex_c = nir_iand(b, is_vertex_c, lvp_build_intersect_vertex(b, cx, ax, bx));
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

      nir_def *t =
         nir_fadd(b, nir_fadd(b, nir_fmul(b, u, az), nir_fmul(b, v, bz)), nir_fmul(b, w, cz));

      nir_def *t_signed = nir_fmul(b, nir_fsign(b, det), t);

      nir_def *det_cond_front = nir_inot(b, nir_flt_imm(b, t_signed, 0.0f));

      nir_push_if(b, det_cond_front);
      {
         t = nir_fdiv(b, t, det);
         v = nir_fdiv(b, v, det);
         w = nir_fdiv(b, w, det);

         nir_def *indices[4] = {t, det, v, w};
         nir_store_var(b, result, nir_vec(b, indices, 4), 0xf);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);

   return nir_load_var(b, result);
}

static nir_def *
lvp_build_hit_is_opaque(nir_builder *b, nir_def *sbt_offset_and_flags,
                        const struct lvp_ray_flags *ray_flags, nir_def *geometry_id_and_flags)
{
   nir_def *opaque = nir_uge_imm(b, nir_ior(b, geometry_id_and_flags, sbt_offset_and_flags),
                                     LVP_INSTANCE_FORCE_OPAQUE | LVP_INSTANCE_NO_FORCE_NOT_OPAQUE);
   opaque = nir_bcsel(b, ray_flags->force_opaque, nir_imm_true(b), opaque);
   opaque = nir_bcsel(b, ray_flags->force_not_opaque, nir_imm_false(b), opaque);
   return opaque;
}

static void
lvp_build_triangle_case(nir_builder *b, const struct lvp_ray_traversal_args *args,
                        const struct lvp_ray_flags *ray_flags, nir_def *result,
                        nir_def *node_addr, nir_def **node_data)
{
   if (!args->triangle_cb)
      return;

   struct lvp_triangle_intersection intersection;
   intersection.t = nir_channel(b, result, 0);
   intersection.barycentrics = nir_channels(b, result, 0xc);

   nir_push_if(b, nir_flt(b, intersection.t, nir_load_deref(b, args->vars.tmax)));
   {
      intersection.frontface = nir_fgt_imm(b, nir_channel(b, result, 1), 0);
      nir_def *switch_ccw = nir_test_mask(b, nir_load_deref(b, args->vars.sbt_offset_and_flags),
                                              LVP_INSTANCE_TRIANGLE_FLIP_FACING);
      intersection.frontface = nir_ixor(b, intersection.frontface, switch_ccw);

      nir_def *not_cull = ray_flags->no_skip_triangles;
      nir_def *not_facing_cull =
         nir_bcsel(b, intersection.frontface, ray_flags->no_cull_front, ray_flags->no_cull_back);

      not_cull =
         nir_iand(b, not_cull,
                  nir_ior(b, not_facing_cull,
                          nir_test_mask(b, nir_load_deref(b, args->vars.sbt_offset_and_flags),
                                        LVP_INSTANCE_TRIANGLE_FACING_CULL_DISABLE)));

      nir_push_if(b, nir_iand(b, nir_flt(b, args->tmin, intersection.t), not_cull));
      {
         intersection.base.node_addr = node_addr;
         intersection.base.primitive_id =
            lvp_load_node_data(b, node_addr, node_data, offsetof(struct lvp_bvh_triangle_node, primitive_id));
         intersection.base.geometry_id_and_flags =
            lvp_load_node_data(b, node_addr, node_data, offsetof(struct lvp_bvh_triangle_node, geometry_id_and_flags));
         intersection.base.opaque =
            lvp_build_hit_is_opaque(b, nir_load_deref(b, args->vars.sbt_offset_and_flags), ray_flags,
                                    intersection.base.geometry_id_and_flags);

         not_cull = nir_bcsel(b, intersection.base.opaque, ray_flags->no_cull_opaque,
                              ray_flags->no_cull_no_opaque);
         nir_push_if(b, not_cull);
         {
            args->triangle_cb(b, &intersection, args, ray_flags);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
lvp_build_aabb_case(nir_builder *b, const struct lvp_ray_traversal_args *args,
                           const struct lvp_ray_flags *ray_flags, nir_def *node_addr,
                           nir_def **node_data)
{
   if (!args->aabb_cb)
      return;

   struct lvp_leaf_intersection intersection;
   intersection.node_addr = node_addr;
   intersection.primitive_id =
      lvp_load_node_data(b, node_addr, node_data, offsetof(struct lvp_bvh_aabb_node, primitive_id));
   intersection.geometry_id_and_flags =
      lvp_load_node_data(b, node_addr, node_data, offsetof(struct lvp_bvh_aabb_node, geometry_id_and_flags));
   intersection.opaque = lvp_build_hit_is_opaque(b, nir_load_deref(b, args->vars.sbt_offset_and_flags),
                                                 ray_flags, intersection.geometry_id_and_flags);

   nir_def *not_cull =
      nir_bcsel(b, intersection.opaque, ray_flags->no_cull_opaque, ray_flags->no_cull_no_opaque);
   not_cull = nir_iand(b, not_cull, ray_flags->no_skip_aabbs);
   nir_push_if(b, not_cull);
   {
      args->aabb_cb(b, &intersection, args, ray_flags);
   }
   nir_pop_if(b, NULL);
}

static void
lvp_build_push_stack(nir_builder *b, const struct lvp_ray_traversal_args *args, nir_def *node)
{
   nir_def *stack_ptr = nir_load_deref(b, args->vars.stack_ptr);
   nir_store_deref(b, nir_build_deref_array(b, args->vars.stack, stack_ptr), node, 0x1);
   nir_store_deref(b, args->vars.stack_ptr, nir_iadd_imm(b, nir_load_deref(b, args->vars.stack_ptr), 1), 0x1);
}

static nir_def *
lvp_build_pop_stack(nir_builder *b, const struct lvp_ray_traversal_args *args)
{
   nir_def *stack_ptr = nir_iadd_imm(b, nir_load_deref(b, args->vars.stack_ptr), -1);
   nir_store_deref(b, args->vars.stack_ptr, stack_ptr, 0x1);
   return nir_load_deref(b, nir_build_deref_array(b, args->vars.stack, stack_ptr));
}

nir_def *
lvp_build_ray_traversal(nir_builder *b, const struct lvp_ray_traversal_args *args)
{
   nir_variable *incomplete = nir_local_variable_create(b->impl, glsl_bool_type(), "incomplete");
   nir_store_var(b, incomplete, nir_imm_true(b), 0x1);

   nir_def *vec3ones = nir_imm_vec3(b, 1.0, 1.0, 1.0);

   struct lvp_ray_flags ray_flags = {
      .force_opaque = nir_test_mask(b, args->flags, SpvRayFlagsOpaqueKHRMask),
      .force_not_opaque = nir_test_mask(b, args->flags, SpvRayFlagsNoOpaqueKHRMask),
      .terminate_on_first_hit =
         nir_test_mask(b, args->flags, SpvRayFlagsTerminateOnFirstHitKHRMask),
      .no_cull_front = nir_ieq_imm(
         b, nir_iand_imm(b, args->flags, SpvRayFlagsCullFrontFacingTrianglesKHRMask), 0),
      .no_cull_back =
         nir_ieq_imm(b, nir_iand_imm(b, args->flags, SpvRayFlagsCullBackFacingTrianglesKHRMask), 0),
      .no_cull_opaque =
         nir_ieq_imm(b, nir_iand_imm(b, args->flags, SpvRayFlagsCullOpaqueKHRMask), 0),
      .no_cull_no_opaque =
         nir_ieq_imm(b, nir_iand_imm(b, args->flags, SpvRayFlagsCullNoOpaqueKHRMask), 0),
      .no_skip_triangles =
         nir_ieq_imm(b, nir_iand_imm(b, args->flags, SpvRayFlagsSkipTrianglesKHRMask), 0),
      .no_skip_aabbs = nir_ieq_imm(b, nir_iand_imm(b, args->flags, SpvRayFlagsSkipAABBsKHRMask), 0),
   };

   nir_push_loop(b);
   {
      nir_push_if(b, nir_ieq_imm(b, nir_load_deref(b, args->vars.current_node), LVP_BVH_INVALID_NODE));
      {
         nir_push_if(b, nir_ieq_imm(b, nir_load_deref(b, args->vars.stack_ptr), 0));
         {
            nir_store_var(b, incomplete, nir_imm_false(b), 0x1);
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);

         nir_push_if(b, nir_ige(b, nir_load_deref(b, args->vars.stack_base), nir_load_deref(b, args->vars.stack_ptr)));
         {
            nir_store_deref(b, args->vars.stack_base, nir_imm_int(b, -1), 1);

            nir_store_deref(b, args->vars.bvh_base, args->root_bvh_base, 1);
            nir_store_deref(b, args->vars.origin, args->origin, 7);
            nir_store_deref(b, args->vars.dir, args->dir, 7);
            nir_store_deref(b, args->vars.inv_dir, nir_fdiv(b, vec3ones, args->dir), 7);
         }
         nir_pop_if(b, NULL);

         nir_store_deref(b, args->vars.current_node, lvp_build_pop_stack(b, args), 0x1);
      }
      nir_pop_if(b, NULL);

      nir_def *bvh_node = nir_load_deref(b, args->vars.current_node);
      nir_store_deref(b, args->vars.current_node, nir_imm_int(b, LVP_BVH_INVALID_NODE), 0x1);

      nir_def *node_addr = nir_iadd(b, nir_load_deref(b, args->vars.bvh_base), nir_u2u64(b, nir_iand_imm(b, bvh_node, ~3u)));

      nir_def *node_data[LVP_BVH_NODE_PREFETCH_SIZE / 4];
      for (uint32_t i = 0; i < ARRAY_SIZE(node_data); i++)
         node_data[i] = nir_load_global(b, 1, 32, nir_iadd_imm(b, node_addr, i * 4));

      nir_def *tmax = nir_load_deref(b, args->vars.tmax);

      nir_def *node_type = nir_iand_imm(b, bvh_node, 3);
      nir_push_if(b, nir_uge_imm(b, node_type, lvp_bvh_node_internal));
      {
         nir_push_if(b, nir_uge_imm(b, node_type, lvp_bvh_node_instance));
         {
            nir_push_if(b, nir_ieq_imm(b, node_type, lvp_bvh_node_aabb));
            {
               lvp_build_aabb_case(b, args, &ray_flags, node_addr, node_data);
            }
            nir_push_else(b, NULL);
            {
               /* instance */
               nir_store_deref(b, args->vars.instance_addr, node_addr, 1);

               nir_def *wto_matrix[3];
               lvp_load_wto_matrix(b, node_addr, node_data, wto_matrix);

               nir_store_deref(b, args->vars.sbt_offset_and_flags, node_data[3],
                               1);

               nir_def *instance_and_mask = node_data[2];
               nir_push_if(b, nir_ult(b, nir_iand(b, instance_and_mask, args->cull_mask),
                                      nir_imm_int(b, 1 << 24)));
               {
                  nir_jump(b, nir_jump_continue);
               }
               nir_pop_if(b, NULL);

               nir_store_deref(b, args->vars.bvh_base,
                               nir_pack_64_2x32_split(b, node_data[0], node_data[1]), 1);

               nir_store_deref(b, args->vars.stack_base, nir_load_deref(b, args->vars.stack_ptr), 0x1);

               /* Push the instance root node onto the stack */
               nir_store_deref(b, args->vars.current_node, nir_imm_int(b, LVP_BVH_ROOT_NODE), 0x1);

               /* Transform the ray into object space */
               nir_store_deref(b, args->vars.origin,
                               lvp_mul_vec3_mat(b, args->origin, wto_matrix, true), 7);
               nir_store_deref(b, args->vars.dir,
                               lvp_mul_vec3_mat(b, args->dir, wto_matrix, false), 7);
               nir_store_deref(b, args->vars.inv_dir,
                               nir_fdiv(b, vec3ones, nir_load_deref(b, args->vars.dir)), 7);
            }
            nir_pop_if(b, NULL);
         }
         nir_push_else(b, NULL);
         {
            nir_def *result = lvp_build_intersect_ray_box(
               b, node_data, tmax,
               nir_load_deref(b, args->vars.origin), nir_load_deref(b, args->vars.dir),
               nir_load_deref(b, args->vars.inv_dir));

            nir_store_deref(b, args->vars.current_node, nir_channel(b, result, 0), 0x1);

            nir_push_if(b, nir_ine_imm(b, nir_channel(b, result, 1), LVP_BVH_INVALID_NODE));
            {
               lvp_build_push_stack(b, args, nir_channel(b, result, 1));
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         nir_def *result = lvp_build_intersect_ray_tri(
            b, node_data, tmax, nir_load_deref(b, args->vars.origin),
            nir_load_deref(b, args->vars.dir), nir_load_deref(b, args->vars.inv_dir));

         lvp_build_triangle_case(b, args, &ray_flags, result, node_addr, node_data);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_loop(b, NULL);

   return nir_load_var(b, incomplete);
}
