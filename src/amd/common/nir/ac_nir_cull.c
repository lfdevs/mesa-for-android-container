/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 * Copyright 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"
#include "nir_builder.h"

/* This code is adapted from ac_llvm_cull.c, hence the copyright to AMD. */

typedef struct
{
   nir_def *w_reflection;
   nir_def *all_w_negative_or_zero_or_nan;
   nir_def *any_w_negative;
} position_w_info;

static void
analyze_position_w(nir_builder *b, nir_def *pos[][4], unsigned num_vertices,
                   position_w_info *w_info)
{
   w_info->all_w_negative_or_zero_or_nan = nir_imm_true(b);
   w_info->w_reflection = nir_imm_false(b);
   w_info->any_w_negative = nir_imm_false(b);

   for (unsigned i = 0; i < num_vertices; ++i) {
      nir_def *neg_w = nir_flt_imm(b, pos[i][3], 0.0f);
      nir_def *neg_or_zero_or_nan_w = nir_fgeu(b, nir_imm_float(b, 0.0f), pos[i][3]);

      w_info->w_reflection = nir_ixor(b, neg_w, w_info->w_reflection);
      w_info->any_w_negative = nir_ior(b, neg_w, w_info->any_w_negative);
      w_info->all_w_negative_or_zero_or_nan = nir_iand(b, neg_or_zero_or_nan_w, w_info->all_w_negative_or_zero_or_nan);
   }
}

static nir_def *
cull_face_triangle(nir_builder *b, nir_def *pos[3][4], const position_w_info *w_info)
{
   nir_def *det_t0 = nir_fsub(b, pos[2][0], pos[0][0]);
   nir_def *det_t1 = nir_fsub(b, pos[1][1], pos[0][1]);
   nir_def *det_t2 = nir_fsub(b, pos[0][0], pos[1][0]);
   nir_def *det_t3 = nir_fsub(b, pos[0][1], pos[2][1]);
   nir_def *det_p0 = nir_fmul(b, det_t0, det_t1);
   nir_def *det_p1 = nir_fmul(b, det_t2, det_t3);
   nir_def *det = nir_fsub(b, det_p0, det_p1);

   det = nir_bcsel(b, w_info->w_reflection, nir_fneg(b, det), det);

   nir_def *front_facing_ccw = nir_fgt_imm(b, det, 0.0f);
   nir_def *zero_area = nir_feq_imm(b, det, 0.0f);
   nir_def *ccw = nir_load_cull_ccw_amd(b);
   nir_def *front_facing = nir_ieq(b, front_facing_ccw, ccw);
   nir_def *cull_front = nir_load_cull_front_face_enabled_amd(b);
   nir_def *cull_back = nir_load_cull_back_face_enabled_amd(b);

   nir_def *face_culled = nir_bcsel(b, front_facing, cull_front, cull_back);
   face_culled = nir_ior(b, face_culled, zero_area);

   /* Don't reject NaN and +/-infinity, these are tricky.
    * Just trust fixed-function HW to handle these cases correctly.
    */
   return nir_iand(b, face_culled, nir_fisfinite(b, det));
}

static void
calc_bbox_triangle(nir_builder *b, nir_def *pos[3][4], nir_def *bbox_min[2], nir_def *bbox_max[2])
{
   for (unsigned chan = 0; chan < 2; ++chan) {
      bbox_min[chan] = nir_fmin(b, pos[0][chan], nir_fmin(b, pos[1][chan], pos[2][chan]));
      bbox_max[chan] = nir_fmax(b, pos[0][chan], nir_fmax(b, pos[1][chan], pos[2][chan]));
   }
}

static nir_def *
cull_frustrum(nir_builder *b, nir_def *bbox_min[2], nir_def *bbox_max[2])
{
   nir_def *prim_outside_view = nir_imm_false(b);

   for (unsigned chan = 0; chan < 2; ++chan) {
      prim_outside_view = nir_ior(b, prim_outside_view, nir_flt_imm(b, bbox_max[chan], -1.0f));
      prim_outside_view = nir_ior(b, prim_outside_view, nir_fgt_imm(b, bbox_min[chan], 1.0f));
   }

   return prim_outside_view;
}

static nir_def *
cross(nir_builder *b, nir_def *p[2], nir_def *q[2])
{
   nir_def *left = nir_fmul(b, p[0], q[1]);
   nir_def *right = nir_fmul(b, q[0], p[1]);
   return nir_fsub(b, left, right);
}

/* Return whether the distance between the point and the triangle is greater than the given
 * distance.
 */
static nir_def *
point_outside_triangle(nir_builder *b, nir_def *p[2], nir_def *pos[3][2], nir_def *distance)
{
   nir_def **vtx_a = pos[0], **vtx_b = pos[1], **vtx_c = pos[2];
   nir_def *a_b[2] = { nir_fsub(b, vtx_b[0], vtx_a[0]), nir_fsub(b, vtx_b[1], vtx_a[1]) };
   nir_def *a_c[2] = { nir_fsub(b, vtx_c[0], vtx_a[0]), nir_fsub(b, vtx_c[1], vtx_a[1]) };
   nir_def *b_c[2] = { nir_fsub(b, vtx_c[0], vtx_b[0]), nir_fsub(b, vtx_c[1], vtx_b[1]) };
   nir_def *a_p[2] = { nir_fsub(b, p[0], vtx_a[0]), nir_fsub(b, p[1], vtx_a[1]) };
   nir_def *b_p[2] = { nir_fsub(b, p[0], vtx_b[0]), nir_fsub(b, p[1], vtx_b[1]) };

   /* Compute 2D cross products, which we need for computing distances from lines. */
   nir_def *crosses[3] = { cross(b, a_p, a_c), cross(b, a_b, a_p), cross(b, b_c, b_p) };

   /* These are distances from the 3 infinite lines going through triangle edges.
    *
    * A distance is positive if the point is on one side of the half space, and negative
    * if the point is on the other side of the half space. That's because the distance is
    * a normalized 2D cross product, which is always scalar and signed.
    */
   nir_def *line_distances[3] = {
      nir_fmul(b, crosses[0], nir_frsq(b, nir_fdot2(b, nir_vec(b, a_c, 2), nir_vec(b, a_c, 2)))),
      nir_fmul(b, crosses[1], nir_frsq(b, nir_fdot2(b, nir_vec(b, a_b, 2), nir_vec(b, a_b, 2)))),
      nir_fmul(b, crosses[2], nir_frsq(b, nir_fdot2(b, nir_vec(b, b_c, 2), nir_vec(b, b_c, 2)))),
   };

   nir_def *max_distance =
      nir_fmax(b, line_distances[0], nir_fmax(b, line_distances[1], line_distances[2]));
   nir_def *min_distance =
      nir_fmin(b, line_distances[0], nir_fmin(b, line_distances[1], line_distances[2]));

   /* If max_distance > distance && min_distance < -distance, the point is outside the triangle.
    *
    * Explanation:
    *
    * If the point it outside the triangle, 2 distances are positive and 1 is negative, or 2 distances
    * are negative and 1 is positive (depending on winding and where the point is). max_distance > distance
    * will pass because at least 1 distance is positive, and min_distance < -distance will pass because at
    * least 1 distance is negative.
    *
    * However, if the point is inside the triangle, either all distances are positive (min_distance < -distance
    * will fail) or all distances are negative (max_distance > distance will fail), depending on winding.
    *
    * Note that min/max_distance are not distances from the triangle, but they are distances from
    * the lines. This can falsely return that the distance between the point and the triangle is
    * less than than the given distance if 2 infinite lines are sticking out of 1 vertex, are
    * pointing in the direction of the point, and there is a very small angle between them.
    * Most of these cases should be eliminated by the rounding-based small prim culling.
    */
   return nir_iand(b, nir_flt(b, distance, max_distance),
                   nir_flt(b, min_distance, nir_fneg(b, distance)));
}

static nir_def *
cull_small_primitive_triangle(nir_builder *b, bool use_point_tri_intersection,
                              nir_def *bbox_min[2], nir_def *bbox_max[2], nir_def *pos[3][4])
{
   nir_def *vp = nir_load_cull_triangle_viewport_xy_scale_and_offset_amd(b);
   nir_def *small_prim_precision = nir_load_cull_small_triangle_precision_amd(b);
   nir_def *rejected = nir_imm_false(b);

   nir_def *bbox_pixel_min[2], *bbox_pixel_max[2], *vp_scale[2], *vp_translate[2];

   for (unsigned chan = 0; chan < 2; ++chan) {
      vp_scale[chan] = nir_channel(b, vp, chan);
      vp_translate[chan] = nir_channel(b, vp, 2 + chan);

      /* Convert the position to screen-space coordinates. */
      nir_def *min = nir_ffma(b, bbox_min[chan], vp_scale[chan], vp_translate[chan]);
      nir_def *max = nir_ffma(b, bbox_max[chan], vp_scale[chan], vp_translate[chan]);

      /* Scale the bounding box according to precision. */
      min = nir_fsub(b, min, small_prim_precision);
      max = nir_fadd(b, max, small_prim_precision);

      /* Determine if the bbox intersects the sample point, by checking if the min and max round to the same int. */
      bbox_pixel_min[chan] = nir_fround_even(b, min);
      bbox_pixel_max[chan] = nir_fround_even(b, max);

      nir_def *rounded_to_eq = nir_feq(b, bbox_pixel_min[chan], bbox_pixel_max[chan]);
      rejected = nir_ior(b, rejected, rounded_to_eq);
   }

   /* If the triangle hasn't been filtered out yet, try another way.
    * Only execute this code if this subgroup has culled at least 1 small triangle, which indicates
    * that there are probably more small triangles that could be culled.
    */
   if (use_point_tri_intersection) {
      nir_def *outside_center = NULL;
      nir_if *if_passed = nir_push_if(b, nir_inot(b, rejected));
      {
         /* Calculate rounded bounding box dimensions. */
         nir_def *bbox_pixel_w = nir_fsub(b, bbox_pixel_max[0], bbox_pixel_min[0]);
         nir_def *bbox_pixel_h = nir_fsub(b, bbox_pixel_max[1], bbox_pixel_min[1]);

         /* The largest bounding box (rounded to integer coordinates) that contains the triangle
          * that we accept has 1x1 pixel area and looks like this:
          *
          *    X         X         X
          *
          *         ┌─────────┐
          *         │         │
          *    X    │    X    │    X
          *         │         │
          *         └─────────┘
          *
          *    X         X         X
          *
          * However, the largest bounding box before the rounding that contains the triangle can be
          * this:
          *
          *    X         X         X
          *     ┌─────────────────┐
          *     │                 │
          *     │                 │
          *    X│        X        │X
          *     │                 │
          *     │                 │
          *     └─────────────────┘
          *    X         X         X
          *
          * which is the largest area that has 1 pixel center in the middle and 8 pixel centers
          * outside. Therefore, a 1x1 pixels-large rounded bounding box represents an area that's
          * slightly smaller than 2x2 pixels and has only a single pixel in the center. Thanks to
          * that and given that the triangle is always inside the bounding box, we only have to
          * compute a single point-triangle intersection.
          *
          * Check if the triangle's rounded bounding box is a single pixel, which means the triangle
          * can only potentially affect this pixel.
          *
          * 1.01 is used to prevent possible FP precision issues.
          */
         nir_def *w_1px = nir_flt_imm(b, bbox_pixel_w, 1.01);
         nir_def *h_1px = nir_flt_imm(b, bbox_pixel_h, 1.01);
         nir_def *fals = nir_imm_false(b);
         nir_if *if_tri_1px = nir_push_if(b, nir_iand(b, w_1px, h_1px));
         {
            /* The coordinates of the pixel center in screen space. */
            nir_def *pix_center[] = {
               nir_fadd_imm(b, bbox_pixel_min[0], 0.5),
               nir_fadd_imm(b, bbox_pixel_min[1], 0.5),
            };

            /* These are the X, Y coordinates of the 3 points of the triangle. */
            nir_def *screen_pos[3][2] = {{0}};

            /* Transform the coordinates to screen space. */
            for (unsigned vtx = 0; vtx < 3; ++vtx) {
               for (unsigned chan = 0; chan < 2; ++chan)
                  screen_pos[vtx][chan] = nir_ffma(b, pos[vtx][chan], vp_scale[chan], vp_translate[chan]);
            }

            /* small_prim_precision is the rasterization precision in X an Y axes, meaning it's the size of
             * one cell in the fixed-point grid that vertex positions are snapped to. When floating-point
             * coordinates are snapped (rounded) to fixed-point, vertex positions can be shifted by
             * +-small_prim_precision.
             *
             * We need a precision value that works in all directions. Compute the worst-case
             * omnidirectional precision, which is the length of the hypotenuse where
             * small_prim_precision is the length of the catheti.
             *
             * x = small_prim_precision
             * sqrt(x*x + x*x) = sqrt(x*x*2) = x * sqrt(2)
             */
            nir_def *precision_distance = nir_fmul_imm(b, small_prim_precision, sqrt(2));

            /* Check if the pixel center is outside the triangle. If it is, the triangle can be
             * safely removed.
             */
            outside_center = point_outside_triangle(b, pix_center, screen_pos, precision_distance);
         }
         nir_pop_if(b, if_tri_1px);

         outside_center = nir_if_phi(b, outside_center, fals);
      }
      nir_pop_if(b, if_passed);
      rejected = nir_if_phi(b, outside_center, rejected);
   }

   return rejected;
}

static void
call_accept_func(nir_builder *b, nir_def *accepted, ac_nir_cull_accepted accept_func,
                 void *state)
{
   if (!accept_func)
      return;

   nir_if *if_accepted = nir_push_if(b, accepted);
   if_accepted->control = nir_selection_control_divergent_always_taken;
   {
      accept_func(b, state);
   }
   nir_pop_if(b, if_accepted);
}

static nir_def *
ac_nir_cull_triangle(nir_builder *b,
                     bool skip_viewport_state_culling,
                     bool use_point_tri_intersection,
                     nir_def *initially_accepted,
                     nir_def *pos[3][4],
                     position_w_info *w_info,
                     ac_nir_cull_accepted accept_func,
                     void *state)
{
   nir_def *accepted = initially_accepted;
   accepted = nir_iand(b, accepted, nir_inot(b, w_info->all_w_negative_or_zero_or_nan));
   accepted = nir_iand(b, accepted, nir_inot(b, cull_face_triangle(b, pos, w_info)));

   nir_def *bbox_accepted = NULL;

   nir_if *if_accepted = nir_push_if(b, accepted);
   {
      nir_def *bbox_min[2] = {0}, *bbox_max[2] = {0};
      calc_bbox_triangle(b, pos, bbox_min, bbox_max);

      nir_def *prim_outside_view = cull_frustrum(b, bbox_min, bbox_max);
      nir_def *bbox_rejected = prim_outside_view;

      if (!skip_viewport_state_culling) {
         nir_if *if_cull_small_prims = nir_push_if(b, nir_load_cull_small_triangles_enabled_amd(b));
         {
            nir_def *small_prim_rejected = cull_small_primitive_triangle(b, use_point_tri_intersection,
                                                                         bbox_min, bbox_max, pos);
            bbox_rejected = nir_ior(b, bbox_rejected, small_prim_rejected);
         }
         nir_pop_if(b, if_cull_small_prims);

         bbox_rejected = nir_if_phi(b, bbox_rejected, prim_outside_view);
      }

      bbox_accepted = nir_ior(b, nir_inot(b, bbox_rejected), w_info->any_w_negative);
      call_accept_func(b, bbox_accepted, accept_func, state);
   }
   nir_pop_if(b, if_accepted);

   return nir_if_phi(b, bbox_accepted, accepted);
}

static void
rotate_45degrees(nir_builder *b, nir_def *v[2])
{
   /* Rotating a triangle by 45 degrees:
    *
    *    x2  =  x*cos(45) - y*sin(45)
    *    y2  =  x*sin(45) + y*cos(45)
    *
    * Since sin(45) == cos(45), we can write:
    *
    *    x2  =  x*cos(45) - y*cos(45)  =  (x - y) * cos(45)
    *    y2  =  x*cos(45) + y*cos(45)  =  (x + y) * cos(45)
    *
    * The width of each square (rotated diamond) is sqrt(0.5), so we have to scale it to 1
    * by multiplying by 1/sqrt(0.5) = sqrt(2) because we want round() to give us the position
    * of the closest center of the square (rotated diamond). After scaling, we get:
    *
    *    x2  =  (x - y) * cos(45) * sqrt(2)
    *    y2  =  (x + y) * cos(45) * sqrt(2)
    *
    * Since cos(45) * sqrt(2) = 1, we get:
    *
    *    x2  =  x - y
    *    y2  =  x + y
    */
   nir_def *result[2];
   result[0] = nir_fsub(b, v[0], v[1]);
   result[1] = nir_fadd(b, v[0], v[1]);

   memcpy(v, result, sizeof(result));
}

static void
calc_bbox_line(nir_builder *b, nir_def *pos[3][4], nir_def *bbox_min[2], nir_def *bbox_max[2])
{
   nir_def *clip_half_line_width = nir_load_clip_half_line_width_amd(b);

   for (unsigned chan = 0; chan < 2; ++chan) {
      bbox_min[chan] = nir_fmin(b, pos[0][chan], pos[1][chan]);
      bbox_max[chan] = nir_fmax(b, pos[0][chan], pos[1][chan]);

      nir_def *width = nir_channel(b, clip_half_line_width, chan);
      bbox_min[chan] = nir_fsub(b, bbox_min[chan], width);
      bbox_max[chan] = nir_fadd(b, bbox_max[chan], width);
   }
}

static nir_def *
cull_small_primitive_line(nir_builder *b, nir_def *pos[3][4],
                          nir_def *bbox_min[2], nir_def *bbox_max[2],
                          nir_def *prim_is_small_else)
{
   nir_def *prim_is_small = NULL;

   /* Small primitive filter - eliminate lines that are too small to affect a sample. */
   nir_if *if_cull_small_prims = nir_push_if(b, nir_load_cull_small_lines_enabled_amd(b));
   {
      /* This only works with lines without perpendicular end caps (lines with perpendicular
       * end caps are rasterized as quads and thus can't be culled as small prims in 99% of
       * cases because line_width >= 1).
       *
       * This takes advantage of the diamond exit rule, which says that every pixel
       * has a diamond inside it touching the pixel boundary and only if a line exits
       * the diamond, that pixel is filled. If a line enters the diamond or stays
       * outside the diamond, the pixel isn't filled.
       *
       * This algorithm is a little simpler than that. The space outside all diamonds also
       * has the same diamond shape, which we'll call corner diamonds.
       *
       * The idea is to cull all lines that are entirely inside a diamond, including
       * corner diamonds. If a line is entirely inside a diamond, it can be culled because
       * it doesn't exit it. If a line is entirely inside a corner diamond, it can be culled
       * because it doesn't enter any diamond and thus can't exit any diamond.
       *
       * The viewport is rotated by 45 degrees to turn diamonds into squares, and a bounding
       * box test is used to determine whether a line is entirely inside any square (diamond).
       *
       * The line width doesn't matter. Wide lines only duplicate filled pixels in either X or
       * Y direction from the filled pixels. MSAA also doesn't matter. MSAA should ideally use
       * perpendicular end caps that enable quad rasterization for lines. Thus, this should
       * always use non-MSAA viewport transformation and non-MSAA small prim precision.
       *
       * A good test is piglit/lineloop because it draws 10k subpixel lines in a circle.
       * It should contain no holes if this matches hw behavior.
       */
      nir_def *v0[2], *v1[2];
      nir_def *vp = nir_load_cull_line_viewport_xy_scale_and_offset_amd(b);

      /* Get vertex positions in pixels. */
      for (unsigned chan = 0; chan < 2; chan++) {
         nir_def *vp_scale = nir_channel(b, vp, chan);
         nir_def *vp_translate = nir_channel(b, vp, 2 + chan);

         v0[chan] = nir_ffma(b, pos[0][chan], vp_scale, vp_translate);
         v1[chan] = nir_ffma(b, pos[1][chan], vp_scale, vp_translate);
      }

      /* Rotate the viewport by 45 degrees, so that diamonds become squares. */
      rotate_45degrees(b, v0);
      rotate_45degrees(b, v1);

      nir_def *small_prim_precision = nir_load_cull_small_line_precision_amd(b);

      nir_def *rounded_to_eq[2];
      for (unsigned chan = 0; chan < 2; chan++) {
         /* Compute the bounding box around both vertices. We do this because we must
          * enlarge the line area by the precision of the rasterizer.
          */
         nir_def *min = nir_fmin(b, v0[chan], v1[chan]);
         nir_def *max = nir_fmax(b, v0[chan], v1[chan]);

         /* Enlarge the bounding box by the precision of the rasterizer. */
         min = nir_fsub(b, min, small_prim_precision);
         max = nir_fadd(b, max, small_prim_precision);

         /* Round the bounding box corners. If both rounded corners are equal,
          * the bounding box is entirely inside a square (diamond).
          */
         min = nir_fround_even(b, min);
         max = nir_fround_even(b, max);

         rounded_to_eq[chan] = nir_feq(b, min, max);
      }

      prim_is_small = nir_iand(b, rounded_to_eq[0], rounded_to_eq[1]);
      prim_is_small = nir_ior(b, prim_is_small, prim_is_small_else);
   }
   nir_pop_if(b, if_cull_small_prims);

   return nir_if_phi(b, prim_is_small, prim_is_small_else);
}

static nir_def *
ac_nir_cull_line(nir_builder *b,
                 bool skip_viewport_state_culling,
                 nir_def *initially_accepted,
                 nir_def *pos[3][4],
                 position_w_info *w_info,
                 ac_nir_cull_accepted accept_func,
                 void *state)
{
   nir_def *accepted = initially_accepted;
   accepted = nir_iand(b, accepted, nir_inot(b, w_info->all_w_negative_or_zero_or_nan));

   if (skip_viewport_state_culling) {
      call_accept_func(b, accepted, accept_func, state);
      return accepted;
   }

   nir_def *bbox_accepted = NULL;

   nir_if *if_accepted = nir_push_if(b, accepted);
   {
      nir_def *bbox_min[2] = {0}, *bbox_max[2] = {0};
      calc_bbox_line(b, pos, bbox_min, bbox_max);

      /* Frustrum culling - eliminate lines that are fully outside the view. */
      nir_def *prim_outside_view = cull_frustrum(b, bbox_min, bbox_max);
      nir_def *prim_invisible =
         cull_small_primitive_line(b, pos, bbox_min, bbox_max, prim_outside_view);

      bbox_accepted = nir_ior(b, nir_inot(b, prim_invisible), w_info->any_w_negative);
      call_accept_func(b, bbox_accepted, accept_func, state);
   }
   nir_pop_if(b, if_accepted);

   return nir_if_phi(b, bbox_accepted, accepted);
}

nir_def *
ac_nir_cull_primitive(nir_builder *b,
                      bool skip_viewport_state_culling,
                      bool use_point_tri_intersection,
                      nir_def *initially_accepted,
                      nir_def *pos[3][4],
                      unsigned num_vertices,
                      ac_nir_cull_accepted accept_func,
                      void *state)
{
   position_w_info w_info = {0};
   analyze_position_w(b, pos, num_vertices, &w_info);

   if (num_vertices == 3) {
      return ac_nir_cull_triangle(b, skip_viewport_state_culling, use_point_tri_intersection,
                                  initially_accepted, pos, &w_info, accept_func, state);
   } else if (num_vertices == 2) {
      return ac_nir_cull_line(b, skip_viewport_state_culling, initially_accepted, pos, &w_info,
                              accept_func, state);
   } else {
      UNREACHABLE("point culling not implemented");
   }

   return NULL;
}
