/*
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2017-2019 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/u_math.h"
#include "util/u_format.h"
#include "util/u_debug.h"
#include "util/u_half.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_pack_color.h"
#include "util/hash_table.h"
#include "util/u_upload_mgr.h"
#include "util/u_prim.h"
#include "util/u_vbuf.h"

#include "lima_context.h"
#include "lima_screen.h"
#include "lima_resource.h"
#include "lima_program.h"
#include "lima_bo.h"
#include "lima_submit.h"
#include "lima_texture.h"
#include "lima_util.h"
#include "lima_fence.h"

#include <drm-uapi/lima_drm.h>

struct lima_gp_frame_reg {
   uint32_t vs_cmd_start;
   uint32_t vs_cmd_end;
   uint32_t plbu_cmd_start;
   uint32_t plbu_cmd_end;
   uint32_t tile_heap_start;
   uint32_t tile_heap_end;
};

struct lima_pp_frame_reg {
   uint32_t plbu_array_address;
   uint32_t render_address;
   uint32_t unused_0;
   uint32_t flags;
   uint32_t clear_value_depth;
   uint32_t clear_value_stencil;
   uint32_t clear_value_color;
   uint32_t clear_value_color_1;
   uint32_t clear_value_color_2;
   uint32_t clear_value_color_3;
   uint32_t width;
   uint32_t height;
   uint32_t fragment_stack_address;
   uint32_t fragment_stack_size;
   uint32_t unused_1;
   uint32_t unused_2;
   uint32_t one;
   uint32_t supersampled_height;
   uint32_t dubya;
   uint32_t onscreen;
   uint32_t blocking;
   uint32_t scale;
   uint32_t foureight;
};

struct lima_pp_wb_reg {
   uint32_t type;
   uint32_t address;
   uint32_t pixel_format;
   uint32_t downsample_factor;
   uint32_t pixel_layout;
   uint32_t pitch;
   uint32_t mrt_bits;
   uint32_t mrt_pitch;
   uint32_t zero;
   uint32_t unused0;
   uint32_t unused1;
   uint32_t unused2;
};

struct lima_render_state {
   uint32_t blend_color_bg;
   uint32_t blend_color_ra;
   uint32_t alpha_blend;
   uint32_t depth_test;
   uint32_t depth_range;
   uint32_t stencil_front;
   uint32_t stencil_back;
   uint32_t stencil_test;
   uint32_t multi_sample;
   uint32_t shader_address;
   uint32_t varying_types;
   uint32_t uniforms_address;
   uint32_t textures_address;
   uint32_t aux0;
   uint32_t aux1;
   uint32_t varyings_address;
};

#define LIMA_PIXEL_FORMAT_B8G8R8A8     0x03
#define LIMA_PIXEL_FORMAT_Z16          0x0e
#define LIMA_PIXEL_FORMAT_Z24S8        0x0f

/* plbu commands */
#define PLBU_CMD_BEGIN(max) { \
   int i = 0, max_n = max; \
   uint32_t *plbu_cmd = util_dynarray_grow_cap(&ctx->plbu_cmd_array, max_n * 4);

#define PLBU_CMD_END() \
   assert(i <= max_n); \
   ctx->plbu_cmd_array.size += i * 4; \
}

#define PLBU_CMD(v1, v2) \
   do { \
      plbu_cmd[i++] = v1; \
      plbu_cmd[i++] = v2; \
   } while (0)

#define PLBU_CMD_BLOCK_STEP(shift_min, shift_h, shift_w) \
   PLBU_CMD(((shift_min) << 28) | ((shift_h) << 16) | (shift_w), 0x1000010C)
#define PLBU_CMD_TILED_DIMENSIONS(tiled_w, tiled_h) \
   PLBU_CMD((((tiled_w) - 1) << 24) | (((tiled_h) - 1) << 8), 0x10000109)
#define PLBU_CMD_BLOCK_STRIDE(block_w) PLBU_CMD(block_w, 0x30000000)
#define PLBU_CMD_ARRAY_ADDRESS(gp_stream, block_num) \
   PLBU_CMD(gp_stream, 0x28000000 | ((block_num) - 1) | 1)
#define PLBU_CMD_VIEWPORT_X(v) PLBU_CMD(v, 0x10000107)
#define PLBU_CMD_VIEWPORT_W(v) PLBU_CMD(v, 0x10000108)
#define PLBU_CMD_VIEWPORT_Y(v) PLBU_CMD(v, 0x10000105)
#define PLBU_CMD_VIEWPORT_H(v) PLBU_CMD(v, 0x10000106)
#define PLBU_CMD_ARRAYS_SEMAPHORE_BEGIN() PLBU_CMD(0x00010002, 0x60000000)
#define PLBU_CMD_ARRAYS_SEMAPHORE_END() PLBU_CMD(0x00010001, 0x60000000)
#define PLBU_CMD_PRIMITIVE_SETUP(low_prim, cull, index_size) \
   PLBU_CMD(((low_prim) ? 0x00003200 : 0x00002200) | (cull) | ((index_size) << 9), 0x1000010B)
#define PLBU_CMD_RSW_VERTEX_ARRAY(rsw, gl_pos) \
   PLBU_CMD(rsw, 0x80000000 | ((gl_pos) >> 4))
#define PLBU_CMD_SCISSORS(minx, maxx, miny, maxy) \
   PLBU_CMD(((minx) << 30) | ((maxy) - 1) << 15 | (miny), \
            0x70000000 | ((maxx) - 1) << 13 | ((minx) >> 2))
#define PLBU_CMD_UNKNOWN1() PLBU_CMD(0x00000000, 0x1000010A)
#define PLBU_CMD_UNKNOWN2() PLBU_CMD(0x00000200, 0x1000010B)
#define PLBU_CMD_LOW_PRIM_SIZE(v) PLBU_CMD(v, 0x1000010D)
#define PLBU_CMD_DEPTH_RANGE_NEAR(v) PLBU_CMD(v, 0x1000010E)
#define PLBU_CMD_DEPTH_RANGE_FAR(v) PLBU_CMD(v, 0x1000010F)
#define PLBU_CMD_INDEXED_DEST(gl_pos) PLBU_CMD(gl_pos, 0x10000100)
#define PLBU_CMD_INDICES(va) PLBU_CMD(va, 0x10000101)
#define PLBU_CMD_DRAW_ARRAYS(mode, start, count) \
   PLBU_CMD(((count) << 24) | (start), (((mode) & 0x1F) << 16) | ((count) >> 8))
#define PLBU_CMD_DRAW_ELEMENTS(mode, start, count) \
   PLBU_CMD(((count) << 24) | (start), \
            0x00200000 | (((mode) & 0x1F) << 16) | ((count) >> 8))

/* vs commands */
#define VS_CMD_BEGIN(max) { \
   int i = 0, max_n = max; \
   uint32_t *vs_cmd = util_dynarray_grow_cap(&ctx->vs_cmd_array, max_n * 4);

#define VS_CMD_END() \
   assert(i <= max_n); \
   ctx->vs_cmd_array.size += i * 4; \
}

#define VS_CMD(v1, v2) \
   do { \
      vs_cmd[i++] = v1; \
      vs_cmd[i++] = v2; \
   } while (0)

#define VS_CMD_ARRAYS_SEMAPHORE_BEGIN_1() VS_CMD(0x00028000, 0x50000000)
#define VS_CMD_ARRAYS_SEMAPHORE_BEGIN_2() VS_CMD(0x00000001, 0x50000000)
#define VS_CMD_ARRAYS_SEMAPHORE_END(index_draw) \
   VS_CMD((index_draw) ? 0x00018000 : 0x00000000, 0x50000000)
#define VS_CMD_UNIFORMS_ADDRESS(addr, size) \
   VS_CMD(addr, 0x30000000 | ((size) << 12))
#define VS_CMD_SHADER_ADDRESS(addr, size) \
   VS_CMD(addr, 0x40000000 | ((size) << 12))
#define VS_CMD_SHADER_INFO(prefetch, size) \
   VS_CMD(((prefetch) << 20) | ((((size) >> 4) - 1) << 10), 0x10000040)
#define VS_CMD_VARYING_ATTRIBUTE_COUNT(nv, na) \
   VS_CMD((((nv) - 1) << 8) | (((na) - 1) << 24), 0x10000042)
#define VS_CMD_UNKNOWN1() VS_CMD(0x00000003, 0x10000041)
#define VS_CMD_UNKNOWN2() VS_CMD(0x00000000, 0x60000000)
#define VS_CMD_ATTRIBUTES_ADDRESS(addr, na) \
   VS_CMD(addr, 0x20000000 | ((na) << 17))
#define VS_CMD_VARYINGS_ADDRESS(addr, nv) \
   VS_CMD(addr, 0x20000008 | ((nv) << 17))
#define VS_CMD_DRAW(num, index_draw) \
   VS_CMD(((num) << 24) | ((index_draw) ? 1 : 0), ((num) >> 8))

static inline bool
lima_ctx_dirty(struct lima_context *ctx)
{
   return ctx->plbu_cmd_array.size;
}

static bool
lima_fb_need_reload(struct lima_context *ctx)
{
   /* Depth buffer is always discarded */
   if (!ctx->framebuffer.base.nr_cbufs)
      return false;
   if (ctx->damage.region) {
      /* for EGL_KHR_partial_update we just want to reload the
       * region not aligned to tile boundary */
      if (!ctx->damage.aligned)
         return true;
   }
   else {
      struct lima_surface *surf = lima_surface(ctx->framebuffer.base.cbufs[0]);
      if (surf->reload)
         return true;
   }

   return false;
}

static void
lima_pack_reload_plbu_cmd(struct lima_context *ctx)
{
   #define lima_reload_render_state_offset 0x0000
   #define lima_reload_gl_pos_offset       0x0040
   #define lima_reload_varying_offset      0x0080
   #define lima_reload_tex_desc_offset     0x00c0
   #define lima_reload_tex_array_offset    0x0100
   #define lima_reload_buffer_size         0x0140

   void *cpu;
   unsigned offset;
   struct pipe_resource *pres = NULL;
   u_upload_alloc(ctx->uploader, 0, lima_reload_buffer_size,
                  0x40, &offset, &pres, &cpu);

   struct lima_resource *res = lima_resource(pres);
   uint32_t va = res->bo->va + offset;

   struct lima_screen *screen = lima_screen(ctx->base.screen);

   uint32_t reload_shader_first_instr_size =
      ((uint32_t *)(screen->pp_buffer->map + pp_reload_program_offset))[0] & 0x1f;
   uint32_t reload_shader_va = screen->pp_buffer->va + pp_reload_program_offset;

   struct lima_render_state reload_render_state = {
      .alpha_blend = 0xf03b1ad2,
      .depth_test = 0x0000000e,
      .depth_range = 0xffff0000,
      .stencil_front = 0x00000007,
      .stencil_back = 0x00000007,
      .multi_sample = 0x0000f007,
      .shader_address = reload_shader_va | reload_shader_first_instr_size,
      .varying_types = 0x00000001,
      .textures_address = va + lima_reload_tex_array_offset,
      .aux0 = 0x00004021,
      .varyings_address = va + lima_reload_varying_offset,
   };
   memcpy(cpu + lima_reload_render_state_offset, &reload_render_state,
          sizeof(reload_render_state));

   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   uint32_t *td = cpu + lima_reload_tex_desc_offset;
   memset(td, 0, lima_tex_desc_size);
   lima_texture_desc_set_res(ctx, td, fb->base.cbufs[0]->texture, 0, 0);
   td[1] = 0x00000480;
   td[2] |= 0x00093800;
   td[4] = 0x00000000;
   td[5] = 0x00000000;

   uint32_t *ta = cpu + lima_reload_tex_array_offset;
   ta[0] = va + lima_reload_tex_desc_offset;

   float reload_gl_pos[] = {
      fb->base.width, 0,               0, 1,
      0,              0,               0, 1,
      0,              fb->base.height, 0, 1,
   };
   memcpy(cpu + lima_reload_gl_pos_offset, reload_gl_pos,
          sizeof(reload_gl_pos));

   float reload_varying[] = {
      fb->base.width, 0,               0, 0,
      0,              fb->base.height, 0, 0,
   };
   memcpy(cpu + lima_reload_varying_offset, reload_varying,
          sizeof(reload_varying));

   lima_submit_add_bo(ctx->pp_submit, res->bo, LIMA_SUBMIT_BO_READ);
   pipe_resource_reference(&pres, NULL);

   PLBU_CMD_BEGIN(20);

   PLBU_CMD_VIEWPORT_X(0);
   PLBU_CMD_VIEWPORT_W(fui(fb->base.width));
   PLBU_CMD_VIEWPORT_Y(0);
   PLBU_CMD_VIEWPORT_H(fui(fb->base.height));

   PLBU_CMD_RSW_VERTEX_ARRAY(
      va + lima_reload_render_state_offset,
      va + lima_reload_gl_pos_offset);

   PLBU_CMD_UNKNOWN2();
   PLBU_CMD_UNKNOWN1();

   PLBU_CMD_INDICES(screen->pp_buffer->va + pp_shared_index_offset);
   PLBU_CMD_INDEXED_DEST(va + lima_reload_gl_pos_offset);
   PLBU_CMD_DRAW_ELEMENTS(0xf, 0, 3);

   PLBU_CMD_END();
}

static void
lima_pack_clear_plbu_cmd(struct lima_context *ctx)
{
   #define lima_clear_render_state_offset 0x0000
   #define lima_clear_shader_offset       0x0040
   #define lima_clear_buffer_size         0x0080

   void *cpu;
   unsigned offset;
   struct pipe_resource *pres = NULL;
   u_upload_alloc(ctx->uploader, 0, lima_clear_buffer_size,
                  0x40, &offset, &pres, &cpu);

   struct lima_resource *res = lima_resource(pres);
   uint32_t va = res->bo->va + offset;

   struct lima_screen *screen = lima_screen(ctx->base.screen);
   uint32_t gl_pos_va = screen->pp_buffer->va + pp_clear_gl_pos_offset;

   /* const0 clear_color, mov.v1 $0 ^const0.xxxx, stop */
   uint32_t clear_shader[] = {
      0x00021025, 0x0000000c,
      (ctx->clear.color_16pc << 12) | 0x000007cf,
      ctx->clear.color_16pc >> 12,
      ctx->clear.color_16pc >> 44,
   };
   memcpy(cpu + lima_clear_shader_offset, &clear_shader,
          sizeof(clear_shader));

   uint32_t clear_shader_va = va + lima_clear_shader_offset;
   uint32_t clear_shader_first_instr_size = clear_shader[0] & 0x1f;

   struct lima_render_state clear_render_state = {
      .blend_color_bg = 0x00800080,
      .blend_color_ra = 0x00ff0080,
      .alpha_blend = 0xfc321892,
      .depth_test = 0x0000003e,
      .depth_range = 0xffff0000,
      .stencil_front = 0x00000007,
      .stencil_back = 0x00000007,
      .multi_sample = 0x0000f007,
      .shader_address = clear_shader_va | clear_shader_first_instr_size,
   };
   memcpy(cpu + lima_clear_render_state_offset, &clear_render_state,
          sizeof(clear_render_state));

   PLBU_CMD_BEGIN(22);

   PLBU_CMD_VIEWPORT_X(0);
   PLBU_CMD_VIEWPORT_W(0x45800000);
   PLBU_CMD_VIEWPORT_Y(0);
   PLBU_CMD_VIEWPORT_H(0x45800000);

   struct pipe_scissor_state *scissor = &ctx->scissor;
   PLBU_CMD_SCISSORS(scissor->minx, scissor->maxx, scissor->miny, scissor->maxy);

   PLBU_CMD_RSW_VERTEX_ARRAY(va + lima_clear_render_state_offset, gl_pos_va);

   PLBU_CMD_UNKNOWN2();
   PLBU_CMD_UNKNOWN1();

   PLBU_CMD_INDICES(screen->pp_buffer->va + pp_shared_index_offset);
   PLBU_CMD_INDEXED_DEST(gl_pos_va);
   PLBU_CMD_DRAW_ELEMENTS(0xf, 0, 3);

   PLBU_CMD_END();
}

static void
lima_pack_head_plbu_cmd(struct lima_context *ctx)
{
   /* first draw need create a PLBU command header */
   if (lima_ctx_dirty(ctx))
      return;

   struct lima_context_framebuffer *fb = &ctx->framebuffer;

   PLBU_CMD_BEGIN(10);

   PLBU_CMD_UNKNOWN2();
   PLBU_CMD_BLOCK_STEP(fb->shift_min, fb->shift_h, fb->shift_w);
   PLBU_CMD_TILED_DIMENSIONS(fb->tiled_w, fb->tiled_h);
   PLBU_CMD_BLOCK_STRIDE(fb->block_w);

   PLBU_CMD_ARRAY_ADDRESS(
      ctx->plb_gp_stream->va + ctx->plb_index * ctx->plb_gp_size,
      fb->block_w * fb->block_h);

   PLBU_CMD_END();

   if (lima_fb_need_reload(ctx))
      lima_pack_reload_plbu_cmd(ctx);
}

static bool
lima_is_scissor_zero(struct lima_context *ctx)
{
   if (!ctx->rasterizer || !ctx->rasterizer->base.scissor)
      return false;

   struct pipe_scissor_state *scissor = &ctx->scissor;
   return
      scissor->minx == scissor->maxx
      && scissor->miny == scissor->maxy;
}

static bool
lima_is_scissor_full_fb(struct lima_context *ctx)
{
   if (!ctx->rasterizer || !ctx->rasterizer->base.scissor)
      return true;

   struct pipe_scissor_state *scissor = &ctx->scissor;
   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   return
      scissor->minx == 0 && scissor->maxx == fb->base.width &&
      scissor->miny == 0 && scissor->maxy == fb->base.height;
}

static void
hilbert_rotate(int n, int *x, int *y, int rx, int ry)
{
   if (ry == 0) {
      if (rx == 1) {
         *x = n-1 - *x;
         *y = n-1 - *y;
      }

      /* Swap x and y */
      int t  = *x;
      *x = *y;
      *y = t;
   }
}

static void
hilbert_coords(int n, int d, int *x, int *y)
{
   int rx, ry, i, t=d;

   *x = *y = 0;

   for (i = 0; (1 << i) < n; i++) {

      rx = 1 & (t / 2);
      ry = 1 & (t ^ rx);

      hilbert_rotate(1 << i, x, y, rx, ry);

      *x += rx << i;
      *y += ry << i;

      t /= 4;
   }
}

static int
lima_get_pp_stream_size(int num_pp, int tiled_w, int tiled_h, uint32_t *off)
{
   /* carefully calculate each stream start address:
    * 1. overflow: each stream size may be different due to
    *    fb->tiled_w * fb->tiled_h can't be divided by num_pp,
    *    extra size should be added to the preceeding stream
    * 2. alignment: each stream address should be 0x20 aligned
    */
   int delta = tiled_w * tiled_h / num_pp * 16 + 8;
   int remain = tiled_w * tiled_h % num_pp;
   int offset = 0;

   for (int i = 0; i < num_pp; i++) {
      off[i] = offset;

      offset += delta;
      if (remain) {
         offset += 16;
         remain--;
      }
      offset = align(offset, 0x20);
   }

   return offset;
}

static bool
inside_damage_region(int x, int y, struct lima_damage_state *ds)
{
   if (!ds->region)
      return true;

   for (int i = 0; i < ds->num_region; i++) {
      struct pipe_scissor_state *ss = ds->region + i;
      if (x >= ss->minx && x < ss->maxx &&
          y >= ss->miny && y < ss->maxy)
         return true;
   }

   return false;
}

static void
lima_update_pp_stream(struct lima_context *ctx, int off_x, int off_y,
                      int tiled_w, int tiled_h)
{
   struct lima_pp_stream_state *ps = &ctx->pp_stream;
   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   struct lima_screen *screen = lima_screen(ctx->base.screen);
   int i, num_pp = screen->num_pp;

   /* use hilbert_coords to generates 1D to 2D relationship.
    * 1D for pp stream index and 2D for plb block x/y on framebuffer.
    * if multi pp, interleave the 1D index to make each pp's render target
    * close enough which should result close workload
    */
   int max = MAX2(tiled_w, tiled_h);
   int dim = util_logbase2_ceil(max);
   int count = 1 << (dim + dim);
   int index = 0;
   uint32_t *stream[4];
   int si[4] = {0};

   for (i = 0; i < num_pp; i++)
      stream[i] = ps->bo->map + ps->bo_offset + ps->offset[i];

   for (i = 0; i < count; i++) {
      int x, y;
      hilbert_coords(max, i, &x, &y);
      if (x < tiled_w && y < tiled_h) {
         x += off_x;
         y += off_y;

         if (!inside_damage_region(x, y, &ctx->damage))
            continue;

         int pp = index % num_pp;
         int offset = ((y >> fb->shift_h) * fb->block_w +
                       (x >> fb->shift_w)) * LIMA_CTX_PLB_BLK_SIZE;
         int plb_va = ctx->plb[ctx->plb_index]->va + offset;

         stream[pp][si[pp]++] = 0;
         stream[pp][si[pp]++] = 0xB8000000 | x | (y << 8);
         stream[pp][si[pp]++] = 0xE0000002 | ((plb_va >> 3) & ~0xE0000003);
         stream[pp][si[pp]++] = 0xB0000000;

         index++;
      }
   }

   for (i = 0; i < num_pp; i++) {
      stream[i][si[i]++] = 0;
      stream[i][si[i]++] = 0xBC000000;

      lima_dump_command_stream_print(
         stream[i], si[i] * 4, false, "pp plb stream %d at va %x\n",
         i, ps->bo->va + ps->bo_offset + ps->offset[i]);
   }
}

static void
lima_update_damage_pp_stream(struct lima_context *ctx)
{
   struct lima_damage_state *ds = &ctx->damage;
   struct pipe_scissor_state max = ds->region[0];

   /* find a max region to cover all the damage region */
   for (int i = 1; i < ds->num_region; i++) {
      struct pipe_scissor_state *ss = ds->region + i;
      max.minx = MIN2(max.minx, ss->minx);
      max.miny = MIN2(max.miny, ss->miny);
      max.maxx = MAX2(max.maxx, ss->maxx);
      max.maxy = MAX2(max.maxy, ss->maxy);
   }

   int tiled_w = max.maxx - max.minx;
   int tiled_h = max.maxy - max.miny;
   struct lima_screen *screen = lima_screen(ctx->base.screen);
   int size = lima_get_pp_stream_size(
      screen->num_pp, tiled_w, tiled_h, ctx->pp_stream.offset);

   void *cpu;
   unsigned offset;
   struct pipe_resource *pres = NULL;
   u_upload_alloc(ctx->uploader, 0, size, 0x40, &offset, &pres, &cpu);

   struct lima_resource *res = lima_resource(pres);
   ctx->pp_stream.bo = res->bo;
   ctx->pp_stream.bo_offset = offset;

   lima_update_pp_stream(ctx, max.minx, max.miny, tiled_w, tiled_h);

   lima_submit_add_bo(ctx->pp_submit, res->bo, LIMA_SUBMIT_BO_READ);
   pipe_resource_reference(&pres, NULL);
}

static void
lima_update_full_pp_stream(struct lima_context *ctx)
{
   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   struct lima_ctx_plb_pp_stream_key key = {
      .plb_index = ctx->plb_index,
      .tiled_w = fb->tiled_w,
      .tiled_h = fb->tiled_h,
   };

   struct hash_entry *entry =
      _mesa_hash_table_search(ctx->plb_pp_stream, &key);
   struct lima_ctx_plb_pp_stream *s = entry->data;

   if (s->bo) {
      ctx->pp_stream.bo = s->bo;
      ctx->pp_stream.bo_offset = 0;
      memcpy(ctx->pp_stream.offset, s->offset, sizeof(s->offset));
   }
   else {
      struct lima_screen *screen = lima_screen(ctx->base.screen);
      int size = lima_get_pp_stream_size(
         screen->num_pp, fb->tiled_w, fb->tiled_h, s->offset);
      s->bo = lima_bo_create(screen, size, 0);
      lima_bo_map(s->bo);

      ctx->pp_stream.bo = s->bo;
      ctx->pp_stream.bo_offset = 0;
      memcpy(ctx->pp_stream.offset, s->offset, sizeof(s->offset));

      lima_update_pp_stream(ctx, 0, 0, fb->tiled_w, fb->tiled_h);
   }

   lima_submit_add_bo(ctx->pp_submit, s->bo, LIMA_SUBMIT_BO_READ);
}

static void
lima_update_submit_bo(struct lima_context *ctx)
{
   if (lima_ctx_dirty(ctx))
      return;

   struct lima_screen *screen = lima_screen(ctx->base.screen);
   lima_submit_add_bo(ctx->gp_submit, ctx->plb_gp_stream, LIMA_SUBMIT_BO_READ);
   lima_submit_add_bo(ctx->gp_submit, ctx->plb[ctx->plb_index], LIMA_SUBMIT_BO_WRITE);
   lima_submit_add_bo(ctx->gp_submit, ctx->gp_tile_heap[ctx->plb_index], LIMA_SUBMIT_BO_WRITE);

   lima_dump_command_stream_print(
      ctx->plb_gp_stream->map + ctx->plb_index * ctx->plb_gp_size,
      ctx->plb_gp_size, false, "gp plb stream at va %x\n",
      ctx->plb_gp_stream->va + ctx->plb_index * ctx->plb_gp_size);

   if (ctx->damage.region)
      lima_update_damage_pp_stream(ctx);
   else if (ctx->plb_pp_stream)
      lima_update_full_pp_stream(ctx);
   else
      ctx->pp_stream.bo = NULL;

   if (ctx->framebuffer.base.nr_cbufs) {
      struct lima_resource *res = lima_resource(ctx->framebuffer.base.cbufs[0]->texture);
      lima_submit_add_bo(ctx->pp_submit, res->bo, LIMA_SUBMIT_BO_WRITE);
   }
   if (ctx->framebuffer.base.zsbuf) {
      struct lima_resource *res = lima_resource(ctx->framebuffer.base.zsbuf->texture);
      lima_submit_add_bo(ctx->pp_submit, res->bo, LIMA_SUBMIT_BO_WRITE);
   }
   lima_submit_add_bo(ctx->pp_submit, ctx->plb[ctx->plb_index], LIMA_SUBMIT_BO_READ);
   lima_submit_add_bo(ctx->pp_submit, ctx->gp_tile_heap[ctx->plb_index], LIMA_SUBMIT_BO_READ);
   lima_submit_add_bo(ctx->pp_submit, screen->pp_buffer, LIMA_SUBMIT_BO_READ);
}

static void
lima_clear(struct pipe_context *pctx, unsigned buffers,
           const union pipe_color_union *color, double depth, unsigned stencil)
{
   struct lima_context *ctx = lima_context(pctx);
   bool full_fb_clear = lima_is_scissor_full_fb(ctx);

   if (full_fb_clear) {
      lima_flush(ctx);

      /* no need to reload if cleared */
      if (ctx->framebuffer.base.nr_cbufs && (buffers & PIPE_CLEAR_COLOR0)) {
         struct lima_surface *surf = lima_surface(ctx->framebuffer.base.cbufs[0]);
         surf->reload = false;
      }
   }

   struct lima_context_clear *clear = &ctx->clear;
   clear->buffers = buffers;

   if (buffers & PIPE_CLEAR_COLOR0) {
      clear->color_8pc =
         ((uint32_t)float_to_ubyte(color->f[3]) << 24) |
         ((uint32_t)float_to_ubyte(color->f[2]) << 16) |
         ((uint32_t)float_to_ubyte(color->f[1]) << 8) |
         float_to_ubyte(color->f[0]);

      clear->color_16pc =
         ((uint64_t)float_to_ushort(color->f[3]) << 48) |
         ((uint64_t)float_to_ushort(color->f[2]) << 32) |
         ((uint64_t)float_to_ushort(color->f[1]) << 16) |
         float_to_ushort(color->f[0]);
   }

   if (buffers & PIPE_CLEAR_DEPTH)
      clear->depth = util_pack_z(PIPE_FORMAT_Z24X8_UNORM, depth);

   if (buffers & PIPE_CLEAR_STENCIL)
      clear->stencil = stencil;

   lima_update_submit_bo(ctx);

   lima_pack_head_plbu_cmd(ctx);

   /* partial clear */
   if (!full_fb_clear)
      lima_pack_clear_plbu_cmd(ctx);

   ctx->dirty |= LIMA_CONTEXT_DIRTY_CLEAR;
}

enum lima_attrib_type {
   LIMA_ATTRIB_FLOAT = 0x000,
   /* todo: find out what lives here. */
   LIMA_ATTRIB_I16   = 0x004,
   LIMA_ATTRIB_U16   = 0x005,
   LIMA_ATTRIB_I8    = 0x006,
   LIMA_ATTRIB_U8    = 0x007,
   LIMA_ATTRIB_I8N   = 0x008,
   LIMA_ATTRIB_U8N   = 0x009,
   LIMA_ATTRIB_I16N  = 0x00A,
   LIMA_ATTRIB_U16N  = 0x00B,
   /* todo: where is the 32 int */
   /* todo: find out what lives here. */
   LIMA_ATTRIB_FIXED = 0x101
};

static enum lima_attrib_type
lima_pipe_format_to_attrib_type(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   int i = util_format_get_first_non_void_channel(format);
   const struct util_format_channel_description *c = desc->channel + i;

   switch (c->type) {
   case UTIL_FORMAT_TYPE_FLOAT:
      return LIMA_ATTRIB_FLOAT;
   case UTIL_FORMAT_TYPE_FIXED:
      return LIMA_ATTRIB_FIXED;
   case UTIL_FORMAT_TYPE_SIGNED:
      if (c->size == 8) {
         if (c->normalized)
            return LIMA_ATTRIB_I8N;
         else
            return LIMA_ATTRIB_I8;
      }
      else if (c->size == 16) {
         if (c->normalized)
            return LIMA_ATTRIB_I16N;
         else
            return LIMA_ATTRIB_I16;
      }
      break;
   case UTIL_FORMAT_TYPE_UNSIGNED:
      if (c->size == 8) {
         if (c->normalized)
            return LIMA_ATTRIB_U8N;
         else
            return LIMA_ATTRIB_U8;
      }
      else if (c->size == 16) {
         if (c->normalized)
            return LIMA_ATTRIB_U16N;
         else
            return LIMA_ATTRIB_U16;
      }
      break;
   }

   return LIMA_ATTRIB_FLOAT;
}

static void
lima_pack_vs_cmd(struct lima_context *ctx, const struct pipe_draw_info *info)
{
   VS_CMD_BEGIN(24);

   if (!info->index_size) {
      VS_CMD_ARRAYS_SEMAPHORE_BEGIN_1();
      VS_CMD_ARRAYS_SEMAPHORE_BEGIN_2();
   }

   int uniform_size = ctx->vs->uniform_pending_offset + ctx->vs->constant_size + 32;
   VS_CMD_UNIFORMS_ADDRESS(
      lima_ctx_buff_va(ctx, lima_ctx_buff_gp_uniform, LIMA_CTX_BUFF_SUBMIT_GP),
      align(uniform_size, 16));

   VS_CMD_SHADER_ADDRESS(ctx->vs->bo->va, ctx->vs->shader_size);
   VS_CMD_SHADER_INFO(ctx->vs->prefetch, ctx->vs->shader_size);

   int num_varryings = ctx->vs->num_varying;
   int num_attributes = ctx->vertex_elements->num_elements;
   VS_CMD_VARYING_ATTRIBUTE_COUNT(num_varryings, num_attributes);

   VS_CMD_UNKNOWN1();

   VS_CMD_ATTRIBUTES_ADDRESS(
      lima_ctx_buff_va(ctx, lima_ctx_buff_gp_attribute_info, LIMA_CTX_BUFF_SUBMIT_GP),
      num_attributes);

   VS_CMD_VARYINGS_ADDRESS(
      lima_ctx_buff_va(ctx, lima_ctx_buff_gp_varying_info, LIMA_CTX_BUFF_SUBMIT_GP),
      num_varryings);

   unsigned num = info->index_size ? (ctx->max_index - ctx->min_index + 1) : info->count;
   VS_CMD_DRAW(num, info->index_size);

   VS_CMD_UNKNOWN2();

   VS_CMD_ARRAYS_SEMAPHORE_END(info->index_size);

   VS_CMD_END();
}

static void
lima_pack_plbu_cmd(struct lima_context *ctx, const struct pipe_draw_info *info)
{
   lima_pack_head_plbu_cmd(ctx);

   /* If it's zero scissor, we skip adding all other commands */
   if (lima_is_scissor_zero(ctx))
      return;

   PLBU_CMD_BEGIN(30);

   PLBU_CMD_VIEWPORT_X(fui(ctx->viewport.x));
   PLBU_CMD_VIEWPORT_W(fui(ctx->viewport.width));
   PLBU_CMD_VIEWPORT_Y(fui(ctx->viewport.y));
   PLBU_CMD_VIEWPORT_H(fui(ctx->viewport.height));

   if (!info->index_size)
      PLBU_CMD_ARRAYS_SEMAPHORE_BEGIN();

   bool low_prim = info->mode < PIPE_PRIM_TRIANGLES;
   int cf = ctx->rasterizer->base.cull_face;
   int ccw = ctx->rasterizer->base.front_ccw;
   uint32_t cull = 0;
   if (cf != PIPE_FACE_NONE) {
      if (cf & PIPE_FACE_FRONT)
         cull |= ccw ? 0x00040000 : 0x00020000;
      if (cf & PIPE_FACE_BACK)
         cull |= ccw ? 0x00020000 : 0x00040000;
   }
   PLBU_CMD_PRIMITIVE_SETUP(low_prim, cull, info->index_size);

   uint32_t gl_position_va =
      lima_ctx_buff_va(ctx, lima_ctx_buff_sh_gl_pos,
                       LIMA_CTX_BUFF_SUBMIT_GP | LIMA_CTX_BUFF_SUBMIT_PP);
   PLBU_CMD_RSW_VERTEX_ARRAY(
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_plb_rsw, LIMA_CTX_BUFF_SUBMIT_PP),
      gl_position_va);

   /* TODO
    * - we should set it only for the first draw that enabled the scissor and for
    *   latter draw only if scissor is dirty
    */
   if (ctx->rasterizer->base.scissor) {
      struct pipe_scissor_state *scissor = &ctx->scissor;
      PLBU_CMD_SCISSORS(scissor->minx, scissor->maxx, scissor->miny, scissor->maxy);
   }

   PLBU_CMD_UNKNOWN1();

   PLBU_CMD_DEPTH_RANGE_NEAR(fui(ctx->viewport.near));
   PLBU_CMD_DEPTH_RANGE_FAR(fui(ctx->viewport.far));

   if (low_prim) {
      uint32_t v = info->mode == PIPE_PRIM_POINTS ?
         fui(ctx->rasterizer->base.point_size) : fui(ctx->rasterizer->base.line_width);
      PLBU_CMD_LOW_PRIM_SIZE(v);
   }

   if (info->index_size) {
      PLBU_CMD_INDEXED_DEST(gl_position_va);

      struct pipe_resource *indexbuf = NULL;
      unsigned index_offset = 0;
      struct lima_resource *res;
      if (info->has_user_indices) {
         util_upload_index_buffer(&ctx->base, info, &indexbuf, &index_offset);
         res = lima_resource(indexbuf);
      }
      else
         res = lima_resource(info->index.resource);

      lima_submit_add_bo(ctx->gp_submit, res->bo, LIMA_SUBMIT_BO_READ);
      PLBU_CMD_INDICES(res->bo->va + info->start * info->index_size + index_offset);

      if (indexbuf)
         pipe_resource_reference(&indexbuf, NULL);
   }
   else {
      /* can this make the attribute info static? */
      PLBU_CMD_DRAW_ARRAYS(info->mode, info->start, info->count);
   }

   PLBU_CMD_ARRAYS_SEMAPHORE_END();

   if (info->index_size)
      PLBU_CMD_DRAW_ELEMENTS(info->mode, ctx->min_index, info->count);

   PLBU_CMD_END();
}

static int
lima_blend_func(enum pipe_blend_func pipe)
{
   switch (pipe) {
   case PIPE_BLEND_ADD:
      return 2;
   case PIPE_BLEND_SUBTRACT:
      return 0;
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return 1;
   case PIPE_BLEND_MIN:
      return 4;
   case PIPE_BLEND_MAX:
      return 5;
   }
   return -1;
}

static int
lima_blend_factor(enum pipe_blendfactor pipe)
{
   switch (pipe) {
   case PIPE_BLENDFACTOR_ONE:
      return 11;
   case PIPE_BLENDFACTOR_SRC_COLOR:
      return 0;
   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return 16;
   case PIPE_BLENDFACTOR_DST_ALPHA:
      return 17;
   case PIPE_BLENDFACTOR_DST_COLOR:
      return 1;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return 7;
   case PIPE_BLENDFACTOR_CONST_COLOR:
      return 2;
   case PIPE_BLENDFACTOR_CONST_ALPHA:
      return 18;
   case PIPE_BLENDFACTOR_ZERO:
      return 3;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:
      return 8;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      return 24;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      return 25;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:
      return 9;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
      return 10;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return 26;
   case PIPE_BLENDFACTOR_SRC1_COLOR:
   case PIPE_BLENDFACTOR_SRC1_ALPHA:
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      return -1; /* not support */
   }
   return -1;
}

static int
lima_calculate_alpha_blend(enum pipe_blend_func rgb_func, enum pipe_blend_func alpha_func,
                           enum pipe_blendfactor rgb_src_factor, enum pipe_blendfactor rgb_dst_factor,
                           enum pipe_blendfactor alpha_src_factor, enum pipe_blendfactor alpha_dst_factor)
{
   return lima_blend_func(rgb_func) |
      (lima_blend_func(alpha_func) << 3) |
      (lima_blend_factor(rgb_src_factor) << 6) |
      (lima_blend_factor(rgb_dst_factor) << 11) |
      ((lima_blend_factor(alpha_src_factor) & 0xF) << 16) |
      ((lima_blend_factor(alpha_dst_factor) & 0xF) << 20) |
      0x0C000000; /* need check if this GLESv1 glAlphaFunc */
}

static int
lima_stencil_op(enum pipe_stencil_op pipe)
{
   switch (pipe) {
   case PIPE_STENCIL_OP_KEEP:
      return 0;
   case PIPE_STENCIL_OP_ZERO:
      return 2;
   case PIPE_STENCIL_OP_REPLACE:
      return 1;
   case PIPE_STENCIL_OP_INCR:
      return 6;
   case PIPE_STENCIL_OP_DECR:
      return 7;
   case PIPE_STENCIL_OP_INCR_WRAP:
      return 4;
   case PIPE_STENCIL_OP_DECR_WRAP:
      return 5;
   case PIPE_STENCIL_OP_INVERT:
      return 3;
   }
   return -1;
}

static int
lima_calculate_depth_test(struct pipe_depth_state *depth, struct pipe_rasterizer_state *rst)
{
   enum pipe_compare_func func = (depth->enabled ? depth->func : PIPE_FUNC_ALWAYS);

   int offset_scale = 0;

   //TODO: implement polygon offset
#if 0
   if (rst->offset_scale < -32)
      offset_scale = -32;
   else if (rst->offset_scale > 31)
      offset_scale = 31;
   else
      offset_scale = rst->offset_scale * 4;

   if (offset_scale < 0)
      offset_scale = 0x100 + offset_scale;
#endif

   return (depth->enabled && depth->writemask) |
      ((int)func << 1) |
      (offset_scale << 16) |
      0x30; /* find out what is this */
}

static void
lima_pack_render_state(struct lima_context *ctx, const struct pipe_draw_info *info)
{
   struct lima_render_state *render =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_pp_plb_rsw,
                          sizeof(*render), true);

   /* do hw support RGBA independ blend?
    * PIPE_CAP_INDEP_BLEND_ENABLE
    *
    * how to handle the no cbuf only zbuf case?
    */
   struct pipe_rt_blend_state *rt = ctx->blend->base.rt;
   render->blend_color_bg = float_to_ubyte(ctx->blend_color.color[2]) |
      (float_to_ubyte(ctx->blend_color.color[1]) << 16);
   render->blend_color_ra = float_to_ubyte(ctx->blend_color.color[0]) |
      (float_to_ubyte(ctx->blend_color.color[3]) << 16);

   if (rt->blend_enable) {
      render->alpha_blend = lima_calculate_alpha_blend(rt->rgb_func, rt->alpha_func,
         rt->rgb_src_factor, rt->rgb_dst_factor,
         rt->alpha_src_factor, rt->alpha_dst_factor);
   }
   else {
      /*
       * Special handling for blending disabled.
       * Binary driver is generating the same alpha_value,
       * as when we would just enable blending, without changing/setting any blend equation/params.
       * Normaly in this case mesa would set all rt fields (func/factor) to zero.
       */
      render->alpha_blend = lima_calculate_alpha_blend(PIPE_BLEND_ADD, PIPE_BLEND_ADD,
         PIPE_BLENDFACTOR_ONE, PIPE_BLENDFACTOR_ZERO,
         PIPE_BLENDFACTOR_ONE, PIPE_BLENDFACTOR_ZERO);
   }

   render->alpha_blend |= (rt->colormask & PIPE_MASK_RGBA) << 28;

   struct pipe_rasterizer_state *rst = &ctx->rasterizer->base;
   struct pipe_depth_state *depth = &ctx->zsa->base.depth;
   render->depth_test = lima_calculate_depth_test(depth, rst);

   /* overlap with plbu? any place can remove one? */
   render->depth_range = float_to_ushort(ctx->viewport.near) |
      (float_to_ushort(ctx->viewport.far) << 16);

#if 0
   struct pipe_stencil_state *stencil = ctx->zsa->base.stencil;
   struct pipe_stencil_ref *ref = &ctx->stencil_ref;
   render->stencil_front = stencil[0].func |
      (lima_stencil_op(stencil[0].fail_op) << 3) |
      (lima_stencil_op(stencil[0].zfail_op) << 6) |
      (lima_stencil_op(stencil[0].zpass_op) << 9) |
      (ref->ref_value[0] << 16) |
      (stencil[0].valuemask << 24);
   render->stencil_back = stencil[1].func |
      (lima_stencil_op(stencil[1].fail_op) << 3) |
      (lima_stencil_op(stencil[1].zfail_op) << 6) |
      (lima_stencil_op(stencil[1].zpass_op) << 9) |
      (ref->ref_value[1] << 16) |
      (stencil[1].valuemask << 24);
#else
   render->stencil_front = 0xff000007;
   render->stencil_back = 0xff000007;
#endif

   /* seems not correct? */
   //struct pipe_alpha_state *alpha = &ctx->zsa->base.alpha;
   render->stencil_test = 0;
   //(stencil->enabled ? 0xFF : 0x00) | (float_to_ubyte(alpha->ref_value) << 16)

   /* need more investigation */
   if (info->mode == PIPE_PRIM_POINTS)
      render->multi_sample = 0x0000F007;
   else if (info->mode < PIPE_PRIM_TRIANGLES)
      render->multi_sample = 0x0000F407;
   else
      render->multi_sample = 0x0000F807;
   if (ctx->framebuffer.base.samples)
      render->multi_sample |= 0x68;

   render->shader_address =
      ctx->fs->bo->va | (((uint32_t *)ctx->fs->bo->map)[0] & 0x1F);

   /* seems not needed */
   render->uniforms_address = 0x00000000;

   render->textures_address = 0x00000000;

   /* more investigation */
   render->aux0 = 0x00000300 | (ctx->vs->varying_stride >> 3);
   render->aux1 = 0x00003000;

   if (ctx->tex_stateobj.num_samplers) {
      render->textures_address =
         lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc, LIMA_CTX_BUFF_SUBMIT_PP);
      render->aux0 |= ctx->tex_stateobj.num_samplers << 14;
      render->aux0 |= 0x20;
   }

   if (ctx->const_buffer[PIPE_SHADER_FRAGMENT].buffer) {
      render->uniforms_address =
         lima_ctx_buff_va(ctx, lima_ctx_buff_pp_uniform_array, LIMA_CTX_BUFF_SUBMIT_PP);
      render->uniforms_address |= ((ctx->buffer_state[lima_ctx_buff_pp_uniform].size) / 4 - 1);
      render->aux0 |= 0x80;
      render->aux1 |= 0x10000;
   }

   if (ctx->vs->num_varying > 1) {
      render->varying_types = 0x00000000;
      render->varyings_address =
         lima_ctx_buff_va(ctx, lima_ctx_buff_sh_varying, LIMA_CTX_BUFF_SUBMIT_PP);
      for (int i = 1; i < ctx->vs->num_varying; i++) {
         int val;

         struct lima_varying_info *v = ctx->vs->varying + i;
         if (v->component_size == 4)
            val = v->components > 2 ? 0 : 1;
         else
            val = v->components > 2 ? 2 : 3;

         int index = i - 1;
         if (index < 10)
            render->varying_types |= val << (3 * index);
         else if (index == 10) {
            render->varying_types |= val << 30;
            render->varyings_address |= val >> 2;
         }
         else if (index == 11)
            render->varyings_address |= val << 1;
      }
   }
   else {
      render->varying_types = 0x00000000;
      render->varyings_address = 0x00000000;
   }

   lima_dump_command_stream_print(
      render, sizeof(*render), false, "add render state at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_plb_rsw, 0));
}

static void
lima_update_gp_attribute_info(struct lima_context *ctx, const struct pipe_draw_info *info)
{
   struct lima_vertex_element_state *ve = ctx->vertex_elements;
   struct lima_context_vertex_buffer *vb = &ctx->vertex_buffers;

   uint32_t *attribute =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_gp_attribute_info,
                          ve->num_elements * 8, true);

   int n = 0;
   for (int i = 0; i < ve->num_elements; i++) {
      struct pipe_vertex_element *pve = ve->pipe + i;

      assert(pve->vertex_buffer_index < vb->count);
      assert(vb->enabled_mask & (1 << pve->vertex_buffer_index));

      struct pipe_vertex_buffer *pvb = vb->vb + pve->vertex_buffer_index;
      struct lima_resource *res = lima_resource(pvb->buffer.resource);

      lima_submit_add_bo(ctx->gp_submit, res->bo, LIMA_SUBMIT_BO_READ);

      unsigned start = info->index_size ? ctx->min_index : info->start;
      attribute[n++] = res->bo->va + pvb->buffer_offset + pve->src_offset
         + start * pvb->stride;
      attribute[n++] = (pvb->stride << 11) |
         (lima_pipe_format_to_attrib_type(pve->src_format) << 2) |
         (util_format_get_nr_components(pve->src_format) - 1);
   }

   lima_dump_command_stream_print(
      attribute, n * 4, false, "update attribute info at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_gp_attribute_info, 0));
}

static void
lima_update_gp_uniform(struct lima_context *ctx)
{
   struct lima_context_constant_buffer *ccb =
      ctx->const_buffer + PIPE_SHADER_VERTEX;
   struct lima_vs_shader_state *vs = ctx->vs;

   int size = vs->uniform_pending_offset + vs->constant_size + 32;
   void *vs_const_buff =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_gp_uniform, size, true);

   if (ccb->buffer)
      memcpy(vs_const_buff, ccb->buffer, ccb->size);

   memcpy(vs_const_buff + vs->uniform_pending_offset,
          ctx->viewport.transform.scale,
          sizeof(ctx->viewport.transform.scale));
   memcpy(vs_const_buff + vs->uniform_pending_offset + 16,
          ctx->viewport.transform.translate,
          sizeof(ctx->viewport.transform.translate));

   if (vs->constant)
      memcpy(vs_const_buff + vs->uniform_pending_offset + 32,
             vs->constant, vs->constant_size);

   lima_dump_command_stream_print(
      vs_const_buff, size, true,
      "update gp uniform at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_gp_uniform, 0));
}

static void
lima_update_pp_uniform(struct lima_context *ctx)
{
   const float *const_buff = ctx->const_buffer[PIPE_SHADER_FRAGMENT].buffer;
   size_t const_buff_size = ctx->const_buffer[PIPE_SHADER_FRAGMENT].size / sizeof(float);

   if (!const_buff)
      return;

   uint16_t *fp16_const_buff =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_pp_uniform,
                          const_buff_size * sizeof(uint16_t), true);

   uint32_t *array =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_pp_uniform_array, 4, true);

   for (int i = 0; i < const_buff_size; i++)
       fp16_const_buff[i] = util_float_to_half(const_buff[i]);

   *array = lima_ctx_buff_va(ctx, lima_ctx_buff_pp_uniform, LIMA_CTX_BUFF_SUBMIT_PP);

   lima_dump_command_stream_print(
      fp16_const_buff, const_buff_size * 2, false, "add pp uniform data at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_uniform, 0));
   lima_dump_command_stream_print(
      array, 4, false, "add pp uniform info at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_uniform_array, 0));
}

static void
lima_update_varying(struct lima_context *ctx, const struct pipe_draw_info *info)
{
   struct lima_vs_shader_state *vs = ctx->vs;

   uint32_t *varying =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_gp_varying_info,
                          vs->num_varying * 8, true);
   int n = 0;

   /* should be LIMA_SUBMIT_BO_WRITE for GP, but each draw will use
    * different part of this bo, so no need to set exclusive constraint */
   lima_ctx_buff_alloc(ctx, lima_ctx_buff_sh_gl_pos,
                       4 * 4 * info->count, false);

   /* for gl_Position */
   varying[n++] =
      lima_ctx_buff_va(ctx, lima_ctx_buff_sh_gl_pos,
                       LIMA_CTX_BUFF_SUBMIT_GP | LIMA_CTX_BUFF_SUBMIT_PP);
   varying[n++] = 0x8020;

   int offset = 0;
   for (int i = 1; i < vs->num_varying; i++) {
      struct lima_varying_info *v = vs->varying + i;
      int size = v->component_size * 4;

      /* does component_size == 2 need to be 16 aligned? */
      if (v->component_size == 4)
         offset = align(offset, 16);

      v->offset = offset;
      offset += size;
   }
   vs->varying_stride = align(offset, 16);

   if (vs->num_varying > 1)
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_sh_varying,
                          vs->varying_stride * info->count, false);

   for (int i = 1; i < vs->num_varying; i++) {
      struct lima_varying_info *v = vs->varying + i;
      varying[n++] =
         lima_ctx_buff_va(ctx, lima_ctx_buff_sh_varying, LIMA_CTX_BUFF_SUBMIT_GP) +
         v->offset;
      varying[n++] = (vs->varying_stride << 11) | (v->components - 1) |
         (v->component_size == 2 ? 0x0C : 0);
   }

   lima_dump_command_stream_print(
      varying, n * 4, false, "update varying info at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_gp_varying_info, 0));
}

static void
lima_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info)
{
   /* check if draw mode and vertex/index count match,
    * otherwise gp will hang */
   if (!u_trim_pipe_prim(info->mode, (unsigned*)&info->count)) {
      debug_printf("draw mode and vertex/index count mismatch\n");
      return;
   }

   struct lima_context *ctx = lima_context(pctx);

   if (!ctx->vs || !ctx->fs) {
      debug_warn_once("no shader, skip draw\n");
      return;
   }

   if (!lima_update_vs_state(ctx) || !lima_update_fs_state(ctx))
      return;

   lima_dump_command_stream_print(
      ctx->vs->bo->map, ctx->vs->shader_size, false,
      "add vs at va %x\n", ctx->vs->bo->va);

   lima_dump_command_stream_print(
      ctx->fs->bo->map, ctx->fs->shader_size, false,
      "add fs at va %x\n", ctx->fs->bo->va);

   lima_submit_add_bo(ctx->gp_submit, ctx->vs->bo, LIMA_SUBMIT_BO_READ);
   lima_submit_add_bo(ctx->pp_submit, ctx->fs->bo, LIMA_SUBMIT_BO_READ);

   lima_update_submit_bo(ctx);

   /* Mali Utgard GPU always need min/max index info for index draw,
    * compute it if upper layer does not do for us */
   if (info->index_size && info->max_index == ~0u)
      u_vbuf_get_minmax_index(pctx, info, &ctx->min_index, &ctx->max_index);
   else {
      ctx->min_index = info->min_index;
      ctx->max_index = info->max_index;
   }

   lima_update_gp_attribute_info(ctx, info);

   if ((ctx->dirty & LIMA_CONTEXT_DIRTY_CONST_BUFF &&
        ctx->const_buffer[PIPE_SHADER_VERTEX].dirty) ||
       ctx->dirty & LIMA_CONTEXT_DIRTY_VIEWPORT ||
       ctx->dirty & LIMA_CONTEXT_DIRTY_SHADER_VERT) {
      lima_update_gp_uniform(ctx);
      ctx->const_buffer[PIPE_SHADER_VERTEX].dirty = false;
   }

   lima_update_varying(ctx, info);

   /* If it's zero scissor, don't build vs cmd list */
   if (!lima_is_scissor_zero(ctx))
      lima_pack_vs_cmd(ctx, info);

   if (ctx->dirty & LIMA_CONTEXT_DIRTY_CONST_BUFF &&
       ctx->const_buffer[PIPE_SHADER_FRAGMENT].dirty) {
      lima_update_pp_uniform(ctx);
      ctx->const_buffer[PIPE_SHADER_FRAGMENT].dirty = false;
   }

   if (ctx->dirty & LIMA_CONTEXT_DIRTY_TEXTURES)
      lima_update_textures(ctx);

   lima_pack_render_state(ctx, info);
   lima_pack_plbu_cmd(ctx, info);

   ctx->dirty = 0;
}

static void
lima_finish_plbu_cmd(struct lima_context *ctx)
{
   int i = 0;
   uint32_t *plbu_cmd = util_dynarray_grow_cap(&ctx->plbu_cmd_array, 2 * 4);

   plbu_cmd[i++] = 0x00000000;
   plbu_cmd[i++] = 0x50000000; /* END */

   ctx->plbu_cmd_array.size += i * 4;
}

static void
lima_pack_wb_zsbuf_reg(struct lima_context *ctx, uint32_t *wb_reg, int wb_idx)
{
   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   struct lima_resource *res = lima_resource(fb->base.zsbuf->texture);
   int level = fb->base.zsbuf->u.tex.level;

   uint32_t format;

   switch (fb->base.zsbuf->format) {
   case PIPE_FORMAT_Z16_UNORM:
      format = LIMA_PIXEL_FORMAT_Z16;
      break;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z24X8_UNORM:
   default:
      /* Assume Z24S8 */
      format = LIMA_PIXEL_FORMAT_Z24S8;
      break;
   }

   struct lima_pp_wb_reg *wb = (void *)wb_reg;
   wb[wb_idx].type = 0x01; /* 1 for depth, stencil */
   wb[wb_idx].address = res->bo->va + res->levels[level].offset;
   wb[wb_idx].pixel_format = format;
   if (res->tiled) {
      wb[wb_idx].pixel_layout = 0x2;
      wb[wb_idx].pitch = fb->tiled_w;
   } else {
      wb[wb_idx].pixel_layout = 0x0;
      wb[wb_idx].pitch = res->levels[level].stride / 8;
   }
   wb[wb_idx].mrt_bits = 0;
}

static void
lima_pack_wb_cbuf_reg(struct lima_context *ctx, uint32_t *wb_reg, int wb_idx)
{
   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   struct lima_resource *res = lima_resource(fb->base.cbufs[0]->texture);
   int level = fb->base.cbufs[0]->u.tex.level;

   bool swap_channels = false;
   switch (fb->base.cbufs[0]->format) {
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      swap_channels = true;
      break;
   default:
      break;
   }

   struct lima_pp_wb_reg *wb = (void *)wb_reg;
   wb[wb_idx].type = 0x02; /* 2 for color buffer */
   wb[wb_idx].address = res->bo->va + res->levels[level].offset;
   wb[wb_idx].pixel_format = LIMA_PIXEL_FORMAT_B8G8R8A8;
   if (res->tiled) {
      wb[wb_idx].pixel_layout = 0x2;
      wb[wb_idx].pitch = fb->tiled_w;
   } else {
      wb[wb_idx].pixel_layout = 0x0;
      wb[wb_idx].pitch = res->levels[level].stride / 8;
   }
   wb[wb_idx].mrt_bits = swap_channels ? 0x4 : 0x0;
}


static void
lima_pack_pp_frame_reg(struct lima_context *ctx, uint32_t *frame_reg,
                       uint32_t *wb_reg)
{
   struct lima_context_framebuffer *fb = &ctx->framebuffer;
   struct lima_pp_frame_reg *frame = (void *)frame_reg;
   struct lima_screen *screen = lima_screen(ctx->base.screen);
   int wb_idx = 0;

   frame->render_address = screen->pp_buffer->va + pp_frame_rsw_offset;
   frame->flags = 0x02;
   frame->clear_value_depth = ctx->clear.depth;
   frame->clear_value_stencil = ctx->clear.stencil;
   frame->clear_value_color = ctx->clear.color_8pc;
   frame->clear_value_color_1 = ctx->clear.color_8pc;
   frame->clear_value_color_2 = ctx->clear.color_8pc;
   frame->clear_value_color_3 = ctx->clear.color_8pc;
   frame->one = 1;

   frame->width = fb->base.width - 1;
   frame->height = fb->base.height - 1;

   /* frame->fragment_stack_address is overwritten per-pp in the kernel
    * by the values of pp_frame.fragment_stack_address[i] */

   /* These are "stack size" and "stack offset" shifted,
    * here they are assumed to be always the same. */
   uint32_t fs_stack_size = ctx->fs ? ctx->fs->stack_size : 0;
   frame->fragment_stack_size = fs_stack_size << 16 | fs_stack_size;

   /* related with MSAA and different value when r4p0/r7p0 */
   frame->supersampled_height = fb->base.height * 2 - 1;
   frame->scale = 0xE0C;

   frame->dubya = 0x77;
   frame->onscreen = 1;
   frame->blocking = (fb->shift_min << 28) | (fb->shift_h << 16) | fb->shift_w;
   frame->foureight = 0x8888;

   if (fb->base.nr_cbufs)
      lima_pack_wb_cbuf_reg(ctx, wb_reg, wb_idx++);

   /* Mali4x0 can use on-tile buffer for depth/stencil, so to save some
    * memory bandwidth don't write depth/stencil back to memory if we're
    * rendering to scanout
    */
   if (!lima_is_scanout(ctx) && fb->base.zsbuf)
      lima_pack_wb_zsbuf_reg(ctx, wb_reg, wb_idx++);
}

static void
_lima_flush(struct lima_context *ctx, bool end_of_frame)
{
   lima_finish_plbu_cmd(ctx);

   int vs_cmd_size = ctx->vs_cmd_array.size;
   int plbu_cmd_size = ctx->plbu_cmd_array.size;
   uint32_t vs_cmd_va = 0;
   uint32_t plbu_cmd_va;

   if (vs_cmd_size) {
      void *vs_cmd =
         lima_ctx_buff_alloc(ctx, lima_ctx_buff_gp_vs_cmd, vs_cmd_size, true);
      memcpy(vs_cmd, util_dynarray_begin(&ctx->vs_cmd_array), vs_cmd_size);
      util_dynarray_clear(&ctx->vs_cmd_array);
      vs_cmd_va = lima_ctx_buff_va(ctx, lima_ctx_buff_gp_vs_cmd,
                                   LIMA_CTX_BUFF_SUBMIT_GP);

      lima_dump_command_stream_print(
         vs_cmd, vs_cmd_size, false, "flush vs cmd at va %x\n", vs_cmd_va);
   }

   void *plbu_cmd =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_gp_plbu_cmd, plbu_cmd_size, true);
   memcpy(plbu_cmd, util_dynarray_begin(&ctx->plbu_cmd_array), plbu_cmd_size);
   util_dynarray_clear(&ctx->plbu_cmd_array);
   plbu_cmd_va = lima_ctx_buff_va(ctx, lima_ctx_buff_gp_plbu_cmd,
                                  LIMA_CTX_BUFF_SUBMIT_GP);

   lima_dump_command_stream_print(
      plbu_cmd, plbu_cmd_size, false, "flush plbu cmd at va %x\n", plbu_cmd_va);

   struct lima_screen *screen = lima_screen(ctx->base.screen);
   struct drm_lima_gp_frame gp_frame;
   struct lima_gp_frame_reg *gp_frame_reg = (void *)gp_frame.frame;
   gp_frame_reg->vs_cmd_start = vs_cmd_va;
   gp_frame_reg->vs_cmd_end = vs_cmd_va + vs_cmd_size;
   gp_frame_reg->plbu_cmd_start = plbu_cmd_va;
   gp_frame_reg->plbu_cmd_end = plbu_cmd_va + plbu_cmd_size;
   gp_frame_reg->tile_heap_start = ctx->gp_tile_heap[ctx->plb_index]->va;
   gp_frame_reg->tile_heap_end = ctx->gp_tile_heap[ctx->plb_index]->va + gp_tile_heap_size;

   lima_dump_command_stream_print(
      &gp_frame, sizeof(gp_frame), false, "add gp frame\n");

   if (!lima_submit_start(ctx->gp_submit, &gp_frame, sizeof(gp_frame)))
      fprintf(stderr, "gp submit error\n");

   if (lima_dump_command_stream) {
      if (lima_submit_wait(ctx->gp_submit, PIPE_TIMEOUT_INFINITE)) {
         if (ctx->buffer_state[lima_ctx_buff_sh_gl_pos].res) {
            float *pos = lima_ctx_buff_map(ctx, lima_ctx_buff_sh_gl_pos);
            lima_dump_command_stream_print(
               pos, 4 * 4 * 16, true, "gl_pos dump at va %x\n",
               lima_ctx_buff_va(ctx, lima_ctx_buff_sh_gl_pos, 0));
         }

         uint32_t *plb = lima_bo_map(ctx->plb[ctx->plb_index]);
         lima_dump_command_stream_print(
            plb, LIMA_CTX_PLB_BLK_SIZE, false, "plb dump at va %x\n",
            ctx->plb[ctx->plb_index]->va);
      }
      else {
         fprintf(stderr, "gp submit wait error\n");
         exit(1);
      }
   }

   struct lima_pp_stream_state *ps = &ctx->pp_stream;
   if (screen->gpu_type == DRM_LIMA_PARAM_GPU_ID_MALI400) {
      struct drm_lima_m400_pp_frame pp_frame = {0};
      lima_pack_pp_frame_reg(ctx, pp_frame.frame, pp_frame.wb);
      pp_frame.num_pp = screen->num_pp;

      for (int i = 0; i < screen->num_pp; i++) {
         pp_frame.plbu_array_address[i] = ps->bo->va + ps->bo_offset + ps->offset[i];
         pp_frame.fragment_stack_address[i] = screen->pp_buffer->va +
            pp_stack_offset + pp_stack_pp_size * i;
      }

      lima_dump_command_stream_print(
         &pp_frame, sizeof(pp_frame), false, "add pp frame\n");

      if (!lima_submit_start(ctx->pp_submit, &pp_frame, sizeof(pp_frame)))
         fprintf(stderr, "pp submit error\n");
   }
   else {
      struct drm_lima_m450_pp_frame pp_frame = {0};
      lima_pack_pp_frame_reg(ctx, pp_frame.frame, pp_frame.wb);
      pp_frame.num_pp = screen->num_pp;

      for (int i = 0; i < screen->num_pp; i++)
         pp_frame.fragment_stack_address[i] = screen->pp_buffer->va +
            pp_stack_offset + pp_stack_pp_size * i;

      if (ps->bo) {
         for (int i = 0; i < screen->num_pp; i++)
            pp_frame.plbu_array_address[i] = ps->bo->va + ps->bo_offset + ps->offset[i];
      }
      else {
         pp_frame.use_dlbu = true;

         struct lima_context_framebuffer *fb = &ctx->framebuffer;
         pp_frame.dlbu_regs[0] = ctx->plb[ctx->plb_index]->va;
         pp_frame.dlbu_regs[1] = ((fb->tiled_h - 1) << 16) | (fb->tiled_w - 1);
         unsigned s = util_logbase2(LIMA_CTX_PLB_BLK_SIZE) - 7;
         pp_frame.dlbu_regs[2] = (s << 28) | (fb->shift_h << 16) | fb->shift_w;
         pp_frame.dlbu_regs[3] = ((fb->tiled_h - 1) << 24) | ((fb->tiled_w - 1) << 16);
      }

      lima_dump_command_stream_print(
         &pp_frame, sizeof(pp_frame), false, "add pp frame\n");

      if (!lima_submit_start(ctx->pp_submit, &pp_frame, sizeof(pp_frame)))
         fprintf(stderr, "pp submit error\n");
   }

   if (lima_dump_command_stream) {
      if (!lima_submit_wait(ctx->pp_submit, PIPE_TIMEOUT_INFINITE)) {
         fprintf(stderr, "pp wait error\n");
         exit(1);
      }
   }

   ctx->plb_index = (ctx->plb_index + 1) % lima_ctx_num_plb;

   if (ctx->framebuffer.base.nr_cbufs) {
      /* this surface may need reload when next draw if not end of frame */
      struct lima_surface *surf = lima_surface(ctx->framebuffer.base.cbufs[0]);
      surf->reload = !end_of_frame;
   }
}

void
lima_flush(struct lima_context *ctx)
{
   if (!lima_ctx_dirty(ctx))
      return;

   _lima_flush(ctx, false);
}

static void
lima_pipe_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
                unsigned flags)
{
   struct lima_context *ctx = lima_context(pctx);
   if (!lima_ctx_dirty(ctx))
      return;

   _lima_flush(ctx, flags & PIPE_FLUSH_END_OF_FRAME);

   if (fence) {
      int fd;
      if (lima_submit_get_out_sync(ctx->pp_submit, &fd))
         *fence = lima_fence_create(fd);
   }
}

void
lima_draw_init(struct lima_context *ctx)
{
   ctx->base.clear = lima_clear;
   ctx->base.draw_vbo = lima_draw_vbo;
   ctx->base.flush = lima_pipe_flush;
}
