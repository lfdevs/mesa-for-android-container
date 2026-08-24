/*
 * Copyright © 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * @file iris_genx_protos.h
 *
 * Don't include this directly, it will be included by iris_context.h.
 *
 * NOTE: This header can be included multiple times, from the same file.
 */

/* iris_state.c */
void genX(init_state)(struct iris_context *ice);
void genX(init_screen_state)(struct iris_screen *screen);
void genX(emit_hashing_mode)(struct iris_context *ice,
                             struct iris_batch *batch,
                             unsigned width, unsigned height,
                             unsigned scale);
void genX(emit_urb_config)(struct iris_batch *batch,
                           bool has_tess_eval,
                           bool has_geometry);
void genX(batch_disable_hiz_planes)(struct iris_batch *batch);
void genX(update_pma_fix)(struct iris_context *ice,
                          struct iris_batch *batch,
                          bool enable);

void genX(invalidate_aux_map_state)(struct iris_batch *batch);

void genX(emit_breakpoint)(struct iris_batch *batch, bool emit_before_draw);
void genX(emit_3dprimitive_was)(struct iris_batch *batch,
                                const struct pipe_draw_indirect_info *indirect,
                                uint32_t primitive_topology,
                                uint32_t vertex_count);
void genX(urb_workaround)(struct iris_batch *batch,
                          const struct intel_urb_config *urb_cfg);

static inline void
genX(maybe_emit_breakpoint)(struct iris_batch *batch,
                            bool emit_before_draw)
{
   if (INTEL_DEBUG(DEBUG_DRAW_BKP))
      genX(emit_breakpoint)(batch, emit_before_draw);
}


/* iris_blorp.c */
void genX(init_blorp)(struct iris_context *ice);

/* iris_query.c */
void genX(init_query)(struct iris_context *ice);
void genX(math_add32_gpr0)(struct iris_context *ice,
                           struct iris_batch *batch,
                           uint32_t x);
void genX(math_div32_gpr0)(struct iris_context *ice,
                           struct iris_batch *batch,
                           uint32_t D);

/* iris_indirect_gen.c */
void genX(init_screen_gen_state)(struct iris_screen *screen);
struct iris_gen_indirect_params *
genX(emit_indirect_generate)(struct iris_batch *batch,
                             const struct pipe_draw_info *draw,
                             const struct pipe_draw_indirect_info *indirect,
                             const struct pipe_draw_start_count_bias *sc,
                             struct iris_address *out_params_addr);
