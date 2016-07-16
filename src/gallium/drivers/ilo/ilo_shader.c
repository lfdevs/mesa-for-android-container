/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2013 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "genhw/genhw.h" /* for SBE setup */
#include "core/ilo_builder.h"
#include "core/intel_winsys.h"
#include "shader/ilo_shader_internal.h"
#include "tgsi/tgsi_parse.h"

#include "ilo_state.h"
#include "ilo_shader.h"

struct ilo_shader_cache {
   struct list_head shaders;
   struct list_head changed;

   int max_vs_scratch_size;
   int max_gs_scratch_size;
   int max_fs_scratch_size;
};

/**
 * Create a shader cache.  A shader cache can manage shaders and upload them
 * to a bo as a whole.
 */
struct ilo_shader_cache *
ilo_shader_cache_create(void)
{
   struct ilo_shader_cache *shc;

   shc = CALLOC_STRUCT(ilo_shader_cache);
   if (!shc)
      return NULL;

   list_inithead(&shc->shaders);
   list_inithead(&shc->changed);

   return shc;
}

/**
 * Destroy a shader cache.
 */
void
ilo_shader_cache_destroy(struct ilo_shader_cache *shc)
{
   FREE(shc);
}

/**
 * Add a shader to the cache.
 */
void
ilo_shader_cache_add(struct ilo_shader_cache *shc,
                     struct ilo_shader_state *shader)
{
   struct ilo_shader *sh;

   shader->cache = shc;
   LIST_FOR_EACH_ENTRY(sh, &shader->variants, list)
      sh->uploaded = false;

   list_add(&shader->list, &shc->changed);
}

/**
 * Remove a shader from the cache.
 */
void
ilo_shader_cache_remove(struct ilo_shader_cache *shc,
                        struct ilo_shader_state *shader)
{
   list_del(&shader->list);
   shader->cache = NULL;
}

/**
 * Notify the cache that a managed shader has changed.
 */
static void
ilo_shader_cache_notify_change(struct ilo_shader_cache *shc,
                               struct ilo_shader_state *shader)
{
   if (shader->cache == shc) {
      list_del(&shader->list);
      list_add(&shader->list, &shc->changed);
   }
}

/**
 * Upload managed shaders to the bo.  Only shaders that are changed or added
 * after the last upload are uploaded.
 */
void
ilo_shader_cache_upload(struct ilo_shader_cache *shc,
                        struct ilo_builder *builder)
{
   struct ilo_shader_state *shader, *next;

   LIST_FOR_EACH_ENTRY_SAFE(shader, next, &shc->changed, list) {
      struct ilo_shader *sh;

      LIST_FOR_EACH_ENTRY(sh, &shader->variants, list) {
         int scratch_size, *cur_max;

         if (sh->uploaded)
            continue;

         sh->cache_offset = ilo_builder_instruction_write(builder,
               sh->kernel_size, sh->kernel);

         sh->uploaded = true;

         switch (shader->info.type) {
         case PIPE_SHADER_VERTEX:
            scratch_size = ilo_state_vs_get_scratch_size(&sh->cso.vs);
            cur_max = &shc->max_vs_scratch_size;
            break;
         case PIPE_SHADER_GEOMETRY:
            scratch_size = ilo_state_gs_get_scratch_size(&sh->cso.gs);
            cur_max = &shc->max_gs_scratch_size;
            break;
         case PIPE_SHADER_FRAGMENT:
            scratch_size = ilo_state_ps_get_scratch_size(&sh->cso.ps);
            cur_max = &shc->max_fs_scratch_size;
            break;
         default:
            assert(!"unknown shader type");
            scratch_size = 0;
            cur_max = &shc->max_vs_scratch_size;
            break;
         }

         if (*cur_max < scratch_size)
            *cur_max = scratch_size;
      }

      list_del(&shader->list);
      list_add(&shader->list, &shc->shaders);
   }
}

/**
 * Invalidate all shaders so that they get uploaded in next
 * ilo_shader_cache_upload().
 */
void
ilo_shader_cache_invalidate(struct ilo_shader_cache *shc)
{
   struct ilo_shader_state *shader, *next;

   LIST_FOR_EACH_ENTRY_SAFE(shader, next, &shc->shaders, list) {
      list_del(&shader->list);
      list_add(&shader->list, &shc->changed);
   }

   LIST_FOR_EACH_ENTRY(shader, &shc->changed, list) {
      struct ilo_shader *sh;

      LIST_FOR_EACH_ENTRY(sh, &shader->variants, list)
         sh->uploaded = false;
   }

   shc->max_vs_scratch_size = 0;
   shc->max_gs_scratch_size = 0;
   shc->max_fs_scratch_size = 0;
}

void
ilo_shader_cache_get_max_scratch_sizes(const struct ilo_shader_cache *shc,
                                       int *vs_scratch_size,
                                       int *gs_scratch_size,
                                       int *fs_scratch_size)
{
   *vs_scratch_size = shc->max_vs_scratch_size;
   *gs_scratch_size = shc->max_gs_scratch_size;
   *fs_scratch_size = shc->max_fs_scratch_size;
}

/**
 * Initialize a shader variant.
 */
void
ilo_shader_variant_init(struct ilo_shader_variant *variant,
                        const struct ilo_shader_info *info,
                        const struct ilo_state_vector *vec)
{
   int num_views, i;

   memset(variant, 0, sizeof(*variant));

   switch (info->type) {
   case PIPE_SHADER_VERTEX:
      variant->u.vs.rasterizer_discard =
         vec->rasterizer->state.rasterizer_discard;
      variant->u.vs.num_ucps =
         util_last_bit(vec->rasterizer->state.clip_plane_enable);
      break;
   case PIPE_SHADER_GEOMETRY:
      variant->u.gs.rasterizer_discard =
         vec->rasterizer->state.rasterizer_discard;
      variant->u.gs.num_inputs = vec->vs->shader->out.count;
      for (i = 0; i < vec->vs->shader->out.count; i++) {
         variant->u.gs.semantic_names[i] =
            vec->vs->shader->out.semantic_names[i];
         variant->u.gs.semantic_indices[i] =
            vec->vs->shader->out.semantic_indices[i];
      }
      break;
   case PIPE_SHADER_FRAGMENT:
      variant->u.fs.flatshade =
         (info->has_color_interp && vec->rasterizer->state.flatshade);
      variant->u.fs.fb_height = (info->has_pos) ?
         vec->fb.state.height : 1;
      variant->u.fs.num_cbufs = vec->fb.state.nr_cbufs;
      break;
   default:
      assert(!"unknown shader type");
      break;
   }

   /* use PCB unless constant buffer 0 is not in user buffer  */
   if ((vec->cbuf[info->type].enabled_mask & 0x1) &&
       !vec->cbuf[info->type].cso[0].user_buffer)
      variant->use_pcb = false;
   else
      variant->use_pcb = true;

   num_views = vec->view[info->type].count;
   assert(info->num_samplers <= num_views);

   variant->num_sampler_views = info->num_samplers;
   for (i = 0; i < info->num_samplers; i++) {
      const struct pipe_sampler_view *view = vec->view[info->type].states[i];
      const struct ilo_sampler_cso *sampler = vec->sampler[info->type].cso[i];

      if (view) {
         variant->sampler_view_swizzles[i].r = view->swizzle_r;
         variant->sampler_view_swizzles[i].g = view->swizzle_g;
         variant->sampler_view_swizzles[i].b = view->swizzle_b;
         variant->sampler_view_swizzles[i].a = view->swizzle_a;
      }
      else if (info->shadow_samplers & (1 << i)) {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_1;
      }
      else {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_Y;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_Z;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_W;
      }

      /*
       * When non-nearest filter and PIPE_TEX_WRAP_CLAMP wrap mode is used,
       * the HW wrap mode is set to GEN6_TEXCOORDMODE_CLAMP_BORDER, and we
       * need to manually saturate the texture coordinates.
       */
      if (sampler) {
         variant->saturate_tex_coords[0] |= sampler->saturate_s << i;
         variant->saturate_tex_coords[1] |= sampler->saturate_t << i;
         variant->saturate_tex_coords[2] |= sampler->saturate_r << i;
      }
   }
}

/**
 * Guess the shader variant, knowing that the context may still change.
 */
static void
ilo_shader_variant_guess(struct ilo_shader_variant *variant,
                         const struct ilo_shader_info *info,
                         const struct ilo_state_vector *vec)
{
   int i;

   memset(variant, 0, sizeof(*variant));

   switch (info->type) {
   case PIPE_SHADER_VERTEX:
      break;
   case PIPE_SHADER_GEOMETRY:
      break;
   case PIPE_SHADER_FRAGMENT:
      variant->u.fs.flatshade = false;
      variant->u.fs.fb_height = (info->has_pos) ?
         vec->fb.state.height : 1;
      variant->u.fs.num_cbufs = 1;
      break;
   default:
      assert(!"unknown shader type");
      break;
   }

   variant->use_pcb = true;

   variant->num_sampler_views = info->num_samplers;
   for (i = 0; i < info->num_samplers; i++) {
      if (info->shadow_samplers & (1 << i)) {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_1;
      }
      else {
         variant->sampler_view_swizzles[i].r = PIPE_SWIZZLE_X;
         variant->sampler_view_swizzles[i].g = PIPE_SWIZZLE_Y;
         variant->sampler_view_swizzles[i].b = PIPE_SWIZZLE_Z;
         variant->sampler_view_swizzles[i].a = PIPE_SWIZZLE_W;
      }
   }
}


/**
 * Parse a TGSI instruction for the shader info.
 */
static void
ilo_shader_info_parse_inst(struct ilo_shader_info *info,
                           const struct tgsi_full_instruction *inst)
{
   int i;

   /* look for edgeflag passthrough */
   if (info->edgeflag_out >= 0 &&
       inst->Instruction.Opcode == TGSI_OPCODE_MOV &&
       inst->Dst[0].Register.File == TGSI_FILE_OUTPUT &&
       inst->Dst[0].Register.Index == info->edgeflag_out) {

      assert(inst->Src[0].Register.File == TGSI_FILE_INPUT);
      info->edgeflag_in = inst->Src[0].Register.Index;
   }

   if (inst->Instruction.Texture) {
      bool shadow;

      switch (inst->Texture.Texture) {
      case TGSI_TEXTURE_SHADOW1D:
      case TGSI_TEXTURE_SHADOW2D:
      case TGSI_TEXTURE_SHADOWRECT:
      case TGSI_TEXTURE_SHADOW1D_ARRAY:
      case TGSI_TEXTURE_SHADOW2D_ARRAY:
      case TGSI_TEXTURE_SHADOWCUBE:
      case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
         shadow = true;
         break;
      default:
         shadow = false;
         break;
      }

      for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
         const struct tgsi_full_src_register *src = &inst->Src[i];

         if (src->Register.File == TGSI_FILE_SAMPLER) {
            const int idx = src->Register.Index;

            if (idx >= info->num_samplers)
               info->num_samplers = idx + 1;

            if (shadow)
               info->shadow_samplers |= 1 << idx;
         }
      }
   }
}

/**
 * Parse a TGSI property for the shader info.
 */
static void
ilo_shader_info_parse_prop(struct ilo_shader_info *info,
                           const struct tgsi_full_property *prop)
{
   switch (prop->Property.PropertyName) {
   case TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS:
      info->fs_color0_writes_all_cbufs = prop->u[0].Data;
      break;
   default:
      break;
   }
}

/**
 * Parse a TGSI declaration for the shader info.
 */
static void
ilo_shader_info_parse_decl(struct ilo_shader_info *info,
                           const struct tgsi_full_declaration *decl)
{
   switch (decl->Declaration.File) {
   case TGSI_FILE_INPUT:
      if (decl->Declaration.Interpolate &&
          decl->Interp.Interpolate == TGSI_INTERPOLATE_COLOR)
         info->has_color_interp = true;
      if (decl->Declaration.Semantic &&
          decl->Semantic.Name == TGSI_SEMANTIC_POSITION)
         info->has_pos = true;
      break;
   case TGSI_FILE_OUTPUT:
      if (decl->Declaration.Semantic &&
          decl->Semantic.Name == TGSI_SEMANTIC_EDGEFLAG)
         info->edgeflag_out = decl->Range.First;
      break;
   case TGSI_FILE_CONSTANT:
      {
         const int idx = (decl->Declaration.Dimension) ?
            decl->Dim.Index2D : 0;
         if (info->constant_buffer_count <= idx)
            info->constant_buffer_count = idx + 1;
      }
      break;
   case TGSI_FILE_SYSTEM_VALUE:
      if (decl->Declaration.Semantic &&
          decl->Semantic.Name == TGSI_SEMANTIC_INSTANCEID)
         info->has_instanceid = true;
      if (decl->Declaration.Semantic &&
          decl->Semantic.Name == TGSI_SEMANTIC_VERTEXID)
         info->has_vertexid = true;
      break;
   default:
      break;
   }
}

static void
ilo_shader_info_parse_tokens(struct ilo_shader_info *info)
{
   struct tgsi_parse_context parse;

   info->edgeflag_in = -1;
   info->edgeflag_out = -1;

   tgsi_parse_init(&parse, info->tokens);
   while (!tgsi_parse_end_of_tokens(&parse)) {
      const union tgsi_full_token *token;

      tgsi_parse_token(&parse);
      token = &parse.FullToken;

      switch (token->Token.Type) {
      case TGSI_TOKEN_TYPE_DECLARATION:
         ilo_shader_info_parse_decl(info, &token->FullDeclaration);
         break;
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         ilo_shader_info_parse_inst(info, &token->FullInstruction);
         break;
      case TGSI_TOKEN_TYPE_PROPERTY:
         ilo_shader_info_parse_prop(info, &token->FullProperty);
         break;
      default:
         break;
      }
   }
   tgsi_parse_free(&parse);
}

/**
 * Create a shader state.
 */
static struct ilo_shader_state *
ilo_shader_state_create(const struct ilo_dev *dev,
                        const struct ilo_state_vector *vec,
                        int type, const void *templ)
{
   struct ilo_shader_state *state;
   struct ilo_shader_variant variant;

   state = CALLOC_STRUCT(ilo_shader_state);
   if (!state)
      return NULL;

   state->info.dev = dev;
   state->info.type = type;

   if (type == PIPE_SHADER_COMPUTE) {
      const struct pipe_compute_state *c =
         (const struct pipe_compute_state *) templ;

      state->info.tokens = tgsi_dup_tokens(c->prog);
      state->info.compute.req_local_mem = c->req_local_mem;
      state->info.compute.req_private_mem = c->req_private_mem;
      state->info.compute.req_input_mem = c->req_input_mem;
   }
   else {
      const struct pipe_shader_state *s =
         (const struct pipe_shader_state *) templ;

      state->info.tokens = tgsi_dup_tokens(s->tokens);
      state->info.stream_output = s->stream_output;
   }

   list_inithead(&state->variants);

   ilo_shader_info_parse_tokens(&state->info);

   /* guess and compile now */
   ilo_shader_variant_guess(&variant, &state->info, vec);
   if (!ilo_shader_state_use_variant(state, &variant)) {
      ilo_shader_destroy(state);
      return NULL;
   }

   return state;
}

/**
 * Add a compiled shader to the shader state.
 */
static void
ilo_shader_state_add_shader(struct ilo_shader_state *state,
                            struct ilo_shader *sh)
{
   list_add(&sh->list, &state->variants);
   state->num_variants++;
   state->total_size += sh->kernel_size;

   if (state->cache)
      ilo_shader_cache_notify_change(state->cache, state);
}

/**
 * Remove a compiled shader from the shader state.
 */
static void
ilo_shader_state_remove_shader(struct ilo_shader_state *state,
                               struct ilo_shader *sh)
{
   list_del(&sh->list);
   state->num_variants--;
   state->total_size -= sh->kernel_size;
}

/**
 * Garbage collect shader variants in the shader state.
 */
static void
ilo_shader_state_gc(struct ilo_shader_state *state)
{
   /* activate when the variants take up more than 4KiB of space */
   const int limit = 4 * 1024;
   struct ilo_shader *sh, *next;

   if (state->total_size < limit)
      return;

   /* remove from the tail as the most recently ones are at the head */
   LIST_FOR_EACH_ENTRY_SAFE_REV(sh, next, &state->variants, list) {
      ilo_shader_state_remove_shader(state, sh);
      ilo_shader_destroy_kernel(sh);

      if (state->total_size <= limit / 2)
         break;
   }
}

/**
 * Search for a shader variant.
 */
static struct ilo_shader *
ilo_shader_state_search_variant(struct ilo_shader_state *state,
                                const struct ilo_shader_variant *variant)
{
   struct ilo_shader *sh = NULL, *tmp;

   LIST_FOR_EACH_ENTRY(tmp, &state->variants, list) {
      if (memcmp(&tmp->variant, variant, sizeof(*variant)) == 0) {
         sh = tmp;
         break;
      }
   }

   return sh;
}

static void
init_shader_urb(const struct ilo_shader *kernel,
                const struct ilo_shader_state *state,
                struct ilo_state_shader_urb_info *urb)
{
   urb->cv_input_attr_count = kernel->in.count;
   urb->read_base = 0;
   urb->read_count = kernel->in.count;

   urb->output_attr_count = kernel->out.count;
   urb->user_cull_enables = 0x0;
   urb->user_clip_enables = 0x0;
}

static void
init_shader_kernel(const struct ilo_shader *kernel,
                   const struct ilo_shader_state *state,
                   struct ilo_state_shader_kernel_info *kern)
{
   kern->offset = 0;
   kern->grf_start = kernel->in.start_grf;
   kern->pcb_attr_count =
      (kernel->pcb.cbuf0_size + kernel->pcb.clip_state_size + 15) / 16;
}

static void
init_shader_resource(const struct ilo_shader *kernel,
                     const struct ilo_shader_state *state,
                     struct ilo_state_shader_resource_info *resource)
{
   resource->sampler_count = state->info.num_samplers;
   resource->surface_count = 0;
   resource->has_uav = false;
}

static void
init_vs(struct ilo_shader *kernel,
        const struct ilo_shader_state *state)
{
   struct ilo_state_vs_info info;

   memset(&info, 0, sizeof(info));

   init_shader_urb(kernel, state, &info.urb);
   init_shader_kernel(kernel, state, &info.kernel);
   init_shader_resource(kernel, state, &info.resource);
   info.per_thread_scratch_size = kernel->per_thread_scratch_size;
   info.dispatch_enable = true;
   info.stats_enable = true;

   if (ilo_dev_gen(state->info.dev) == ILO_GEN(6) && kernel->stream_output) {
      struct ilo_state_gs_info gs_info;

      memset(&gs_info, 0, sizeof(gs_info));

      gs_info.urb.cv_input_attr_count = kernel->out.count;
      gs_info.urb.read_count = kernel->out.count;
      gs_info.kernel.grf_start = kernel->gs_start_grf;
      gs_info.sol.sol_enable = true;
      gs_info.sol.stats_enable = true;
      gs_info.sol.render_disable = kernel->variant.u.vs.rasterizer_discard;
      gs_info.sol.svbi_post_inc = kernel->svbi_post_inc;
      gs_info.sol.tristrip_reorder = GEN7_REORDER_LEADING;
      gs_info.dispatch_enable = true;
      gs_info.stats_enable = true;

      ilo_state_vs_init(&kernel->cso.vs_sol.vs, state->info.dev, &info);
      ilo_state_gs_init(&kernel->cso.vs_sol.sol, state->info.dev, &gs_info);
   } else {
      ilo_state_vs_init(&kernel->cso.vs, state->info.dev, &info);
   }
}

static void
init_gs(struct ilo_shader *kernel,
        const struct ilo_shader_state *state)
{
   const struct pipe_stream_output_info *so_info = &state->info.stream_output;
   struct ilo_state_gs_info info;

   memset(&info, 0, sizeof(info));

   init_shader_urb(kernel, state, &info.urb);
   init_shader_kernel(kernel, state, &info.kernel);
   init_shader_resource(kernel, state, &info.resource);
   info.per_thread_scratch_size = kernel->per_thread_scratch_size;
   info.dispatch_enable = true;
   info.stats_enable = true;

   if (so_info->num_outputs > 0) {
      info.sol.sol_enable = true;
      info.sol.stats_enable = true;
      info.sol.render_disable = kernel->variant.u.gs.rasterizer_discard;
      info.sol.tristrip_reorder = GEN7_REORDER_LEADING;
   }

   ilo_state_gs_init(&kernel->cso.gs, state->info.dev, &info);
}

static void
init_ps(struct ilo_shader *kernel,
        const struct ilo_shader_state *state)
{
   struct ilo_state_ps_info info;

   memset(&info, 0, sizeof(info));

   init_shader_kernel(kernel, state, &info.kernel_8);
   init_shader_resource(kernel, state, &info.resource);

   info.per_thread_scratch_size = kernel->per_thread_scratch_size;
   info.io.has_rt_write = true;
   info.io.posoffset = GEN6_POSOFFSET_NONE;
   info.io.attr_count = kernel->in.count;
   info.io.use_z = kernel->in.has_pos;
   info.io.use_w = kernel->in.has_pos;
   info.io.use_coverage_mask = false;
   info.io.pscdepth = (kernel->out.has_pos) ?
      GEN7_PSCDEPTH_ON : GEN7_PSCDEPTH_OFF;
   info.io.write_pixel_mask = kernel->has_kill;
   info.io.write_omask = false;

   info.params.sample_mask = 0x1;
   info.params.earlyz_control_psexec = false;
   info.params.alpha_may_kill = false;
   info.params.dual_source_blending = false;
   info.params.has_writeable_rt = true;

   info.valid_kernels = GEN6_PS_DISPATCH_8;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 284:
    *
    *     "(MSDISPMODE_PERSAMPLE) This is the high-quality multisample mode
    *      where (over and above PERPIXEL mode) the PS is run for each covered
    *      sample. This mode is also used for "normal" non-multisample
    *      rendering (aka 1X), given Number of Multisamples is programmed to
    *      NUMSAMPLES_1."
    */
   info.per_sample_dispatch = true;

   info.rt_clear_enable = false;
   info.rt_resolve_enable = false;
   info.cv_per_sample_interp = false;
   info.cv_has_earlyz_op = false;
   info.sample_count_one = true;
   info.cv_has_depth_buffer = true;

   ilo_state_ps_init(&kernel->cso.ps, state->info.dev, &info);

   /* remember current parameters */
   kernel->ps_params = info.params;
}

static void
init_sol(struct ilo_shader *kernel,
         const struct ilo_dev *dev,
         const struct pipe_stream_output_info *so_info,
         bool rasterizer_discard)
{
   struct ilo_state_sol_decl_info decls[4][PIPE_MAX_SO_OUTPUTS];
   unsigned buf_offsets[PIPE_MAX_SO_BUFFERS];
   struct ilo_state_sol_info info;
   unsigned i;

   if (!so_info->num_outputs) {
      ilo_state_sol_init_disabled(&kernel->sol, dev, rasterizer_discard);
      return;
   }

   memset(&info, 0, sizeof(info));
   info.data = kernel->sol_data;
   info.data_size = sizeof(kernel->sol_data);
   info.sol_enable = true;
   info.stats_enable = true;
   info.tristrip_reorder = GEN7_REORDER_TRAILING;
   info.render_disable = rasterizer_discard;
   info.render_stream = 0;

   for (i = 0; i < 4; i++) {
      info.buffer_strides[i] = so_info->stride[i] * 4;

      info.streams[i].cv_vue_attr_count = kernel->out.count;
      info.streams[i].decls = decls[i];
   }

   memset(decls, 0, sizeof(decls));
   memset(buf_offsets, 0, sizeof(buf_offsets));
   for (i = 0; i < so_info->num_outputs; i++) {
      const unsigned stream = so_info->output[i].stream;
      const unsigned buffer = so_info->output[i].output_buffer;
      struct ilo_state_sol_decl_info *decl;
      unsigned attr;

      /* figure out which attribute is sourced */
      for (attr = 0; attr < kernel->out.count; attr++) {
         const int reg_idx = kernel->out.register_indices[attr];
         if (reg_idx == so_info->output[i].register_index)
            break;
      }
      if (attr >= kernel->out.count) {
         assert(!"stream output an undefined register");
         attr = 0;
      }

      if (info.streams[stream].vue_read_count < attr + 1)
         info.streams[stream].vue_read_count = attr + 1;

      /* pad with holes first */
      while (buf_offsets[buffer] < so_info->output[i].dst_offset) {
         int num_dwords;

         num_dwords = so_info->output[i].dst_offset - buf_offsets[buffer];
         if (num_dwords > 4)
            num_dwords = 4;

         assert(info.streams[stream].decl_count < ARRAY_SIZE(decls[stream]));
         decl = &decls[stream][info.streams[stream].decl_count];

         decl->attr = 0;
         decl->is_hole = true;
         decl->component_base = 0;
         decl->component_count = num_dwords;
         decl->buffer = buffer;

         info.streams[stream].decl_count++;
         buf_offsets[buffer] += num_dwords;
      }
      assert(buf_offsets[buffer] == so_info->output[i].dst_offset);

      assert(info.streams[stream].decl_count < ARRAY_SIZE(decls[stream]));
      decl = &decls[stream][info.streams[stream].decl_count];

      decl->attr = attr;
      decl->is_hole = false;
      /* PSIZE is at W channel */
      if (kernel->out.semantic_names[attr] == TGSI_SEMANTIC_PSIZE) {
         assert(so_info->output[i].start_component == 0);
         assert(so_info->output[i].num_components == 1);
         decl->component_base = 3;
         decl->component_count = 1;
      } else {
         decl->component_base = so_info->output[i].start_component;
         decl->component_count = so_info->output[i].num_components;
      }
      decl->buffer = buffer;

      info.streams[stream].decl_count++;
      buf_offsets[buffer] += so_info->output[i].num_components;
   }

   ilo_state_sol_init(&kernel->sol, dev, &info);
}

/**
 * Add a shader variant to the shader state.
 */
static struct ilo_shader *
ilo_shader_state_add_variant(struct ilo_shader_state *state,
                             const struct ilo_shader_variant *variant)
{
   bool rasterizer_discard = false;
   struct ilo_shader *sh;

   switch (state->info.type) {
   case PIPE_SHADER_VERTEX:
      sh = ilo_shader_compile_vs(state, variant);
      rasterizer_discard = variant->u.vs.rasterizer_discard;
      break;
   case PIPE_SHADER_FRAGMENT:
      sh = ilo_shader_compile_fs(state, variant);
      break;
   case PIPE_SHADER_GEOMETRY:
      sh = ilo_shader_compile_gs(state, variant);
      rasterizer_discard = variant->u.gs.rasterizer_discard;
      break;
   case PIPE_SHADER_COMPUTE:
      sh = ilo_shader_compile_cs(state, variant);
      break;
   default:
      sh = NULL;
      break;
   }
   if (!sh) {
      assert(!"failed to compile shader");
      return NULL;
   }

   sh->variant = *variant;

   init_sol(sh, state->info.dev, &state->info.stream_output,
         rasterizer_discard);

   ilo_shader_state_add_shader(state, sh);

   return sh;
}

/**
 * Update state->shader to point to a variant.  If the variant does not exist,
 * it will be added first.
 */
bool
ilo_shader_state_use_variant(struct ilo_shader_state *state,
                             const struct ilo_shader_variant *variant)
{
   struct ilo_shader *sh;
   bool construct_cso = false;

   sh = ilo_shader_state_search_variant(state, variant);
   if (!sh) {
      ilo_shader_state_gc(state);

      sh = ilo_shader_state_add_variant(state, variant);
      if (!sh)
         return false;

      construct_cso = true;
   }

   /* move to head */
   if (state->variants.next != &sh->list) {
      list_del(&sh->list);
      list_add(&sh->list, &state->variants);
   }

   state->shader = sh;

   if (construct_cso) {
      switch (state->info.type) {
      case PIPE_SHADER_VERTEX:
         init_vs(sh, state);
         break;
      case PIPE_SHADER_GEOMETRY:
         init_gs(sh, state);
         break;
      case PIPE_SHADER_FRAGMENT:
         init_ps(sh, state);
         break;
      default:
         break;
      }
   }

   return true;
}

struct ilo_shader_state *
ilo_shader_create_vs(const struct ilo_dev *dev,
                     const struct pipe_shader_state *state,
                     const struct ilo_state_vector *precompile)
{
   struct ilo_shader_state *shader;

   shader = ilo_shader_state_create(dev, precompile,
         PIPE_SHADER_VERTEX, state);

   /* states used in ilo_shader_variant_init() */
   shader->info.non_orthogonal_states = ILO_DIRTY_VIEW_VS |
                                        ILO_DIRTY_RASTERIZER |
                                        ILO_DIRTY_CBUF;

   return shader;
}

struct ilo_shader_state *
ilo_shader_create_gs(const struct ilo_dev *dev,
                     const struct pipe_shader_state *state,
                     const struct ilo_state_vector *precompile)
{
   struct ilo_shader_state *shader;

   shader = ilo_shader_state_create(dev, precompile,
         PIPE_SHADER_GEOMETRY, state);

   /* states used in ilo_shader_variant_init() */
   shader->info.non_orthogonal_states = ILO_DIRTY_VIEW_GS |
                                        ILO_DIRTY_VS |
                                        ILO_DIRTY_RASTERIZER |
                                        ILO_DIRTY_CBUF;

   return shader;
}

struct ilo_shader_state *
ilo_shader_create_fs(const struct ilo_dev *dev,
                     const struct pipe_shader_state *state,
                     const struct ilo_state_vector *precompile)
{
   struct ilo_shader_state *shader;

   shader = ilo_shader_state_create(dev, precompile,
         PIPE_SHADER_FRAGMENT, state);

   /* states used in ilo_shader_variant_init() */
   shader->info.non_orthogonal_states = ILO_DIRTY_VIEW_FS |
                                        ILO_DIRTY_RASTERIZER |
                                        ILO_DIRTY_FB |
                                        ILO_DIRTY_CBUF;

   return shader;
}

struct ilo_shader_state *
ilo_shader_create_cs(const struct ilo_dev *dev,
                     const struct pipe_compute_state *state,
                     const struct ilo_state_vector *precompile)
{
   struct ilo_shader_state *shader;

   shader = ilo_shader_state_create(dev, precompile,
         PIPE_SHADER_COMPUTE, state);

   shader->info.non_orthogonal_states = 0;

   return shader;
}

/**
 * Destroy a shader state.
 */
void
ilo_shader_destroy(struct ilo_shader_state *shader)
{
   struct ilo_shader *sh, *next;

   LIST_FOR_EACH_ENTRY_SAFE(sh, next, &shader->variants, list)
      ilo_shader_destroy_kernel(sh);

   FREE((struct tgsi_token *) shader->info.tokens);
   FREE(shader);
}

/**
 * Select a kernel for the given context.  This will compile a new kernel if
 * none of the existing kernels work with the context.
 *
 * \param ilo the context
 * \param dirty states of the context that are considered changed
 * \return true if a different kernel is selected
 */
bool
ilo_shader_select_kernel(struct ilo_shader_state *shader,
                         const struct ilo_state_vector *vec,
                         uint32_t dirty)
{
   struct ilo_shader_variant variant;
   bool changed = false;

   if (shader->info.non_orthogonal_states & dirty) {
      const struct ilo_shader * const old = shader->shader;

      ilo_shader_variant_init(&variant, &shader->info, vec);
      ilo_shader_state_use_variant(shader, &variant);
      changed = (shader->shader != old);
   }

   if (shader->info.type == PIPE_SHADER_FRAGMENT) {
      struct ilo_shader *kernel = shader->shader;

      if (kernel->ps_params.sample_mask != vec->sample_mask ||
          kernel->ps_params.alpha_may_kill != vec->blend->alpha_may_kill) {
         kernel->ps_params.sample_mask = vec->sample_mask;
         kernel->ps_params.alpha_may_kill = vec->blend->alpha_may_kill;

         ilo_state_ps_set_params(&kernel->cso.ps, shader->info.dev,
               &kernel->ps_params);

         changed = true;
      }
   }

   return changed;
}

static int
route_attr(const int *semantics, const int *indices, int len,
           int semantic, int index)
{
   int i;

   for (i = 0; i < len; i++) {
      if (semantics[i] == semantic && indices[i] == index)
         return i;
   }

   /* failed to match for COLOR, try BCOLOR */
   if (semantic == TGSI_SEMANTIC_COLOR) {
      for (i = 0; i < len; i++) {
         if (semantics[i] == TGSI_SEMANTIC_BCOLOR && indices[i] == index)
            return i;
      }
   }

   return -1;
}

/**
 * Select a routing for the given source shader and rasterizer state.
 *
 * \return true if a different routing is selected
 */
bool
ilo_shader_select_kernel_sbe(struct ilo_shader_state *shader,
                             const struct ilo_shader_state *source,
                             const struct ilo_rasterizer_state *rasterizer)
{
   const bool is_point = true;
   const bool light_twoside = rasterizer->state.light_twoside;
   const uint32_t sprite_coord_enable = rasterizer->state.sprite_coord_enable;
   const int sprite_coord_mode = rasterizer->state.sprite_coord_mode;
   struct ilo_shader *kernel = shader->shader;
   struct ilo_kernel_routing *routing = &kernel->routing;
   struct ilo_state_sbe_swizzle_info swizzles[ILO_STATE_SBE_MAX_SWIZZLE_COUNT];
   struct ilo_state_sbe_info info;
   const int *src_semantics, *src_indices;
   int src_skip, src_len, src_slot;
   int dst_len, dst_slot;

   assert(kernel);

   if (source) {
      assert(source->shader);

      src_semantics = source->shader->out.semantic_names;
      src_indices = source->shader->out.semantic_indices;
      src_len = source->shader->out.count;
      src_skip = 0;

      assert(src_len >= 2 &&
             src_semantics[0] == TGSI_SEMANTIC_PSIZE &&
             src_semantics[1] == TGSI_SEMANTIC_POSITION);

      /*
       * skip PSIZE and POSITION (how about the optional CLIPDISTs?), unless
       * they are all the source shader has and FS needs to read some
       * attributes.
       */
      if (src_len > 2 || !kernel->in.count) {
         src_semantics += 2;
         src_indices += 2;
         src_len -= 2;
         src_skip = 2;
      }
   } else {
      src_semantics = kernel->in.semantic_names;
      src_indices = kernel->in.semantic_indices;
      src_len = kernel->in.count;
      src_skip = 0;
   }

   /* no change */
   if (routing->initialized &&
       routing->is_point == is_point &&
       routing->light_twoside == light_twoside &&
       routing->sprite_coord_enable == sprite_coord_enable &&
       routing->sprite_coord_mode == sprite_coord_mode &&
       routing->src_len <= src_len &&
       !memcmp(routing->src_semantics, src_semantics,
          sizeof(src_semantics[0]) * routing->src_len) &&
       !memcmp(routing->src_indices, src_indices,
          sizeof(src_indices[0]) * routing->src_len))
      return false;

   routing->is_point = is_point;
   routing->light_twoside = light_twoside;
   routing->sprite_coord_enable = sprite_coord_enable;
   routing->sprite_coord_mode = sprite_coord_mode;

   assert(kernel->in.count <= ARRAY_SIZE(swizzles));
   dst_len = MIN2(kernel->in.count, ARRAY_SIZE(swizzles));

   memset(&swizzles, 0, sizeof(swizzles));
   memset(&info, 0, sizeof(info));

   info.attr_count = dst_len;
   info.cv_vue_attr_count = src_skip + src_len;
   info.vue_read_base = src_skip;
   info.vue_read_count = 0;
   info.has_min_read_count = true;
   info.swizzle_enable = false;
   info.swizzle_16_31 = false;
   info.swizzle_count = 0;
   info.swizzles = swizzles;
   info.const_interp_enables = kernel->in.const_interp_enable;
   info.point_sprite_enables = 0x0;
   info.point_sprite_origin_lower_left =
      (sprite_coord_mode == PIPE_SPRITE_COORD_LOWER_LEFT);
   info.cv_is_point = is_point;

   for (dst_slot = 0; dst_slot < dst_len; dst_slot++) {
      const int semantic = kernel->in.semantic_names[dst_slot];
      const int index = kernel->in.semantic_indices[dst_slot];

      if (semantic == TGSI_SEMANTIC_GENERIC &&
          (sprite_coord_enable & (1 << index)))
         info.point_sprite_enables |= 1 << dst_slot;

      if (source) {
         src_slot = route_attr(src_semantics, src_indices, src_len,
               semantic, index);

         /*
          * The source shader stage does not output this attribute.  The value
          * is supposed to be undefined, unless the attribute goes through
          * point sprite replacement or the attribute is
          * TGSI_SEMANTIC_POSITION.  In all cases, we do not care which source
          * attribute is picked.
          *
          * We should update the kernel code and omit the output of
          * TGSI_SEMANTIC_POSITION here.
          */
         if (src_slot < 0)
            src_slot = 0;
      } else {
         src_slot = dst_slot;
      }

      /* use the following slot for two-sided lighting */
      if (semantic == TGSI_SEMANTIC_COLOR && light_twoside &&
          src_slot + 1 < src_len &&
          src_semantics[src_slot + 1] == TGSI_SEMANTIC_BCOLOR &&
          src_indices[src_slot + 1] == index) {
         swizzles[dst_slot].attr_select = GEN6_INPUTATTR_FACING;
         swizzles[dst_slot].attr = src_slot;
         info.swizzle_enable = true;
         src_slot++;
      } else {
         swizzles[dst_slot].attr_select = GEN6_INPUTATTR_NORMAL;
         swizzles[dst_slot].attr = src_slot;
         if (src_slot != dst_slot)
            info.swizzle_enable = true;
      }

      swizzles[dst_slot].force_zeros = false;

      if (info.vue_read_count < src_slot + 1)
         info.vue_read_count = src_slot + 1;
   }

   if (info.swizzle_enable)
      info.swizzle_count = dst_len;

   if (routing->initialized)
      ilo_state_sbe_set_info(&routing->sbe, shader->info.dev, &info);
   else
      ilo_state_sbe_init(&routing->sbe, shader->info.dev, &info);

   routing->src_len = info.vue_read_count;
   memcpy(routing->src_semantics, src_semantics,
         sizeof(src_semantics[0]) * routing->src_len);
   memcpy(routing->src_indices, src_indices,
         sizeof(src_indices[0]) * routing->src_len);

   routing->initialized = true;

   return true;
}

/**
 * Return the cache offset of the selected kernel.  This must be called after
 * ilo_shader_select_kernel() and ilo_shader_cache_upload().
 */
uint32_t
ilo_shader_get_kernel_offset(const struct ilo_shader_state *shader)
{
   const struct ilo_shader *kernel = shader->shader;

   assert(kernel && kernel->uploaded);

   return kernel->cache_offset;
}

/**
 * Query a kernel parameter for the selected kernel.
 */
int
ilo_shader_get_kernel_param(const struct ilo_shader_state *shader,
                            enum ilo_kernel_param param)
{
   const struct ilo_shader *kernel = shader->shader;
   int val;

   assert(kernel);

   switch (param) {
   case ILO_KERNEL_INPUT_COUNT:
      val = kernel->in.count;
      break;
   case ILO_KERNEL_OUTPUT_COUNT:
      val = kernel->out.count;
      break;
   case ILO_KERNEL_SAMPLER_COUNT:
      val = shader->info.num_samplers;
      break;
   case ILO_KERNEL_SKIP_CBUF0_UPLOAD:
      val = kernel->skip_cbuf0_upload;
      break;
   case ILO_KERNEL_PCB_CBUF0_SIZE:
      val = kernel->pcb.cbuf0_size;
      break;

   case ILO_KERNEL_SURFACE_TOTAL_COUNT:
      val = kernel->bt.total_count;
      break;
   case ILO_KERNEL_SURFACE_TEX_BASE:
      val = kernel->bt.tex_base;
      break;
   case ILO_KERNEL_SURFACE_TEX_COUNT:
      val = kernel->bt.tex_count;
      break;
   case ILO_KERNEL_SURFACE_CONST_BASE:
      val = kernel->bt.const_base;
      break;
   case ILO_KERNEL_SURFACE_CONST_COUNT:
      val = kernel->bt.const_count;
      break;
   case ILO_KERNEL_SURFACE_RES_BASE:
      val = kernel->bt.res_base;
      break;
   case ILO_KERNEL_SURFACE_RES_COUNT:
      val = kernel->bt.res_count;
      break;

   case ILO_KERNEL_VS_INPUT_INSTANCEID:
      val = shader->info.has_instanceid;
      break;
   case ILO_KERNEL_VS_INPUT_VERTEXID:
      val = shader->info.has_vertexid;
      break;
   case ILO_KERNEL_VS_INPUT_EDGEFLAG:
      if (shader->info.edgeflag_in >= 0) {
         /* we rely on the state tracker here */
         assert(shader->info.edgeflag_in == kernel->in.count - 1);
         val = true;
      }
      else {
         val = false;
      }
      break;
   case ILO_KERNEL_VS_PCB_UCP_SIZE:
      val = kernel->pcb.clip_state_size;
      break;
   case ILO_KERNEL_VS_GEN6_SO:
      val = kernel->stream_output;
      break;
   case ILO_KERNEL_VS_GEN6_SO_POINT_OFFSET:
      val = kernel->gs_offsets[0];
      break;
   case ILO_KERNEL_VS_GEN6_SO_LINE_OFFSET:
      val = kernel->gs_offsets[1];
      break;
   case ILO_KERNEL_VS_GEN6_SO_TRI_OFFSET:
      val = kernel->gs_offsets[2];
      break;
   case ILO_KERNEL_VS_GEN6_SO_SURFACE_COUNT:
      val = kernel->gs_bt_so_count;
      break;

   case ILO_KERNEL_GS_DISCARD_ADJACENCY:
      val = kernel->in.discard_adj;
      break;
   case ILO_KERNEL_GS_GEN6_SVBI_POST_INC:
      val = kernel->svbi_post_inc;
      break;
   case ILO_KERNEL_GS_GEN6_SURFACE_SO_BASE:
      val = kernel->bt.gen6_so_base;
      break;
   case ILO_KERNEL_GS_GEN6_SURFACE_SO_COUNT:
      val = kernel->bt.gen6_so_count;
      break;

   case ILO_KERNEL_FS_BARYCENTRIC_INTERPOLATIONS:
      val = kernel->in.barycentric_interpolation_mode;
      break;
   case ILO_KERNEL_FS_DISPATCH_16_OFFSET:
      val = 0;
      break;
   case ILO_KERNEL_FS_SURFACE_RT_BASE:
      val = kernel->bt.rt_base;
      break;
   case ILO_KERNEL_FS_SURFACE_RT_COUNT:
      val = kernel->bt.rt_count;
      break;

   case ILO_KERNEL_CS_LOCAL_SIZE:
      val = shader->info.compute.req_local_mem;
      break;
   case ILO_KERNEL_CS_PRIVATE_SIZE:
      val = shader->info.compute.req_private_mem;
      break;
   case ILO_KERNEL_CS_INPUT_SIZE:
      val = shader->info.compute.req_input_mem;
      break;
   case ILO_KERNEL_CS_SIMD_SIZE:
      val = 16;
      break;
   case ILO_KERNEL_CS_SURFACE_GLOBAL_BASE:
      val = kernel->bt.global_base;
      break;
   case ILO_KERNEL_CS_SURFACE_GLOBAL_COUNT:
      val = kernel->bt.global_count;
      break;

   default:
      assert(!"unknown kernel parameter");
      val = 0;
      break;
   }

   return val;
}

/**
 * Return the CSO of the selected kernel.
 */
const union ilo_shader_cso *
ilo_shader_get_kernel_cso(const struct ilo_shader_state *shader)
{
   const struct ilo_shader *kernel = shader->shader;

   assert(kernel);

   return &kernel->cso;
}

/**
 * Return the SO info of the selected kernel.
 */
const struct pipe_stream_output_info *
ilo_shader_get_kernel_so_info(const struct ilo_shader_state *shader)
{
   return &shader->info.stream_output;
}

const struct ilo_state_sol *
ilo_shader_get_kernel_sol(const struct ilo_shader_state *shader)
{
   const struct ilo_shader *kernel = shader->shader;

   assert(kernel);

   return &kernel->sol;
}

/**
 * Return the routing info of the selected kernel.
 */
const struct ilo_state_sbe *
ilo_shader_get_kernel_sbe(const struct ilo_shader_state *shader)
{
   const struct ilo_shader *kernel = shader->shader;

   assert(kernel);

   return &kernel->routing.sbe;
}
