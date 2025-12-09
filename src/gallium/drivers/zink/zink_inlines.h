#ifndef ZINK_INLINES_H
#define ZINK_INLINES_H

/* these go here to avoid include hell */
static inline void
zink_select_draw_vbo(struct zink_context *ctx)
{
   ctx->base.draw_vbo = ctx->draw_vbo[ctx->pipeline_changed[ZINK_PIPELINE_GFX]];
   ctx->base.draw_vertex_state = ctx->draw_state[ctx->pipeline_changed[ZINK_PIPELINE_GFX]];
   assert(ctx->base.draw_vbo);
   assert(ctx->base.draw_vertex_state);
}

static inline void
zink_select_draw_mesh_tasks(struct zink_context *ctx)
{
   ctx->base.draw_mesh_tasks = ctx->draw_mesh_tasks[ctx->pipeline_changed[ZINK_PIPELINE_MESH]];
   assert(ctx->base.draw_mesh_tasks);
}

static inline void
zink_select_launch_grid(struct zink_context *ctx)
{
   ctx->base.launch_grid = ctx->launch_grid[ctx->pipeline_changed[ZINK_PIPELINE_COMPUTE]];
   assert(ctx->base.launch_grid);
}

#endif
