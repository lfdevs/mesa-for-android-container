/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_program.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_descriptors.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_state.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "tgsi/tgsi_from_mesa.h"

/* for pipeline cache */
#define XXH_INLINE_ALL
#include "util/xxhash.h"

struct gfx_pipeline_cache_entry {
   struct zink_gfx_pipeline_state state;
   VkPipeline pipeline;
};

struct compute_pipeline_cache_entry {
   struct zink_compute_pipeline_state state;
   VkPipeline pipeline;
};

void
debug_describe_zink_gfx_program(char *buf, const struct zink_gfx_program *ptr)
{
   sprintf(buf, "zink_gfx_program");
}

void
debug_describe_zink_compute_program(char *buf, const struct zink_compute_program *ptr)
{
   sprintf(buf, "zink_compute_program");
}

/* copied from iris */
struct keybox {
   uint16_t size;
   gl_shader_stage stage;
   uint8_t data[0];
};

static struct keybox *
make_keybox(void *mem_ctx, gl_shader_stage stage, const void *key, uint32_t key_size, void *base, uint32_t base_size)
{
   struct keybox *keybox =
      ralloc_size(mem_ctx, sizeof(struct keybox) + key_size + base_size);

   keybox->stage = stage;
   keybox->size = key_size + base_size;
   memcpy(keybox->data, key, key_size);
   if (base_size)
      memcpy(&keybox->data[key_size], base, base_size);
   return keybox;
}

static uint32_t
keybox_hash(const void *void_key)
{
   const struct keybox *key = void_key;
   return _mesa_hash_data(&key->stage, key->size + sizeof(key->stage));
}

static bool
keybox_equals(const void *void_a, const void *void_b)
{
   const struct keybox *a = void_a, *b = void_b;
   if (a->size != b->size)
      return false;

   return memcmp(a->data, b->data, a->size) == 0;
}

static void
shader_key_vs_gen(struct zink_context *ctx, struct zink_shader *zs,
                  struct zink_shader *shaders[ZINK_SHADER_COUNT], struct zink_shader_key *key)
{
   struct zink_vs_key *vs_key = &key->key.vs;
   key->size = sizeof(struct zink_vs_key);

   vs_key->clip_halfz = ctx->rast_state->base.clip_halfz;
   switch (zs->nir->info.stage) {
   case MESA_SHADER_VERTEX:
      vs_key->last_vertex_stage = !shaders[PIPE_SHADER_TESS_EVAL] && !shaders[PIPE_SHADER_GEOMETRY];
      vs_key->push_drawid = ctx->drawid_broken;
      break;
   case MESA_SHADER_TESS_EVAL:
      vs_key->last_vertex_stage = !shaders[PIPE_SHADER_GEOMETRY];
      break;
   case MESA_SHADER_GEOMETRY:
      vs_key->last_vertex_stage = true;
      break;
   default:
      unreachable("impossible case");
   }
}

static void
shader_key_fs_gen(struct zink_context *ctx, struct zink_shader *zs,
                  struct zink_shader *shaders[ZINK_SHADER_COUNT], struct zink_shader_key *key)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_fs_key *fs_key = &key->key.fs;
   key->size = sizeof(struct zink_fs_key);

   /* if gl_SampleMask[] is written to, we have to ensure that we get a shader with the same sample count:
    * in GL, rast_samples==1 means ignore gl_SampleMask[]
    * in VK, gl_SampleMask[] is never ignored
    */
   if (zs->nir->info.outputs_written & (1 << FRAG_RESULT_SAMPLE_MASK))
      fs_key->samples = !!ctx->fb_state.samples;
   fs_key->force_dual_color_blend = screen->driconf.dual_color_blend_by_location &&
                                    ctx->gfx_pipeline_state.blend_state->dual_src_blend &&
                                    ctx->gfx_pipeline_state.blend_state->attachments[1].blendEnable;
   if (((shaders[PIPE_SHADER_GEOMETRY] && shaders[PIPE_SHADER_GEOMETRY]->nir->info.gs.output_primitive == GL_POINTS) ||
       ctx->gfx_prim_mode == PIPE_PRIM_POINTS) && ctx->rast_state->base.point_quad_rasterization && ctx->rast_state->base.sprite_coord_enable) {
      fs_key->coord_replace_bits = ctx->rast_state->base.sprite_coord_enable;
      fs_key->coord_replace_yinvert = !!ctx->rast_state->base.sprite_coord_mode;
   }
}

static void
shader_key_tcs_gen(struct zink_context *ctx, struct zink_shader *zs,
                   struct zink_shader *shaders[ZINK_SHADER_COUNT], struct zink_shader_key *key)
{
   struct zink_tcs_key *tcs_key = &key->key.tcs;
   key->size = sizeof(struct zink_tcs_key);

   tcs_key->vertices_per_patch = ctx->gfx_pipeline_state.vertices_per_patch;
   tcs_key->vs_outputs_written = shaders[PIPE_SHADER_VERTEX]->nir->info.outputs_written;
}

typedef void (*zink_shader_key_gen)(struct zink_context *ctx, struct zink_shader *zs,
                                    struct zink_shader *shaders[ZINK_SHADER_COUNT],
                                    struct zink_shader_key *key);
static zink_shader_key_gen shader_key_vtbl[] =
{
   [MESA_SHADER_VERTEX] = shader_key_vs_gen,
   [MESA_SHADER_TESS_CTRL] = shader_key_tcs_gen,
   /* reusing vs key for now since we're only using clip_halfz */
   [MESA_SHADER_TESS_EVAL] = shader_key_vs_gen,
   [MESA_SHADER_GEOMETRY] = shader_key_vs_gen,
   [MESA_SHADER_FRAGMENT] = shader_key_fs_gen,
};

/* return pointer to make function reusable */
static inline struct zink_shader_module **
get_default_shader_module_ptr(struct zink_gfx_program *prog, struct zink_shader *zs, struct zink_shader_key *key)
{
   if (zs->nir->info.stage == MESA_SHADER_VERTEX ||
       zs->nir->info.stage == MESA_SHADER_TESS_EVAL) {
      /* no streamout or halfz */
      if (!zink_vs_key(key)->last_vertex_stage)
         return &prog->default_variants[zs->nir->info.stage][1];
   }
   return &prog->default_variants[zs->nir->info.stage][0];
}

static struct zink_shader_module *
get_shader_module_for_stage(struct zink_context *ctx, struct zink_shader *zs, struct zink_gfx_program *prog)
{
   gl_shader_stage stage = zs->nir->info.stage;
   enum pipe_shader_type pstage = pipe_shader_type_from_mesa(stage);
   struct zink_shader_key key = {0};
   VkShaderModule mod;
   struct zink_shader_module *zm;
   struct zink_shader_module **default_zm = NULL;
   struct keybox *keybox;
   uint32_t hash;
   unsigned base_size = 0;

   shader_key_vtbl[stage](ctx, zs, ctx->gfx_stages, &key);
   /* this is default variant if there is no default or it matches the default */
   if (prog->default_variant_key[pstage]) {
      const struct keybox *tmp = prog->default_variant_key[pstage];
      /* if comparing against the existing default, use the base variant key size since
       * we're only checking the stage-specific data
       */
      key.is_default_variant = !memcmp(tmp->data, &key, key.size);
   } else
      key.is_default_variant = true;

   if (zs->nir->info.num_inlinable_uniforms &&
       ctx->inlinable_uniforms_valid_mask & BITFIELD64_BIT(pstage)) {
      key.inline_uniforms = true;
      memcpy(key.base.inlined_uniform_values,
             ctx->inlinable_uniforms[pstage],
             zs->nir->info.num_inlinable_uniforms * 4);
      base_size = zs->nir->info.num_inlinable_uniforms * sizeof(uint32_t);
      key.is_default_variant = false;
   }
   if (key.is_default_variant) {
      default_zm = get_default_shader_module_ptr(prog, zs, &key);
      if (*default_zm)
         return *default_zm;
   }
   keybox = make_keybox(prog, stage, &key, key.size, &key.base, base_size);
   hash = keybox_hash(keybox);
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(&prog->base.shader_cache[pstage],
                                                                 hash, keybox);

   if (entry) {
      ralloc_free(keybox);
      zm = entry->data;
   } else {
      zm = CALLOC_STRUCT(zink_shader_module);
      if (!zm) {
         ralloc_free(keybox);
         return NULL;
      }
      mod = zink_shader_compile(zink_screen(ctx->base.screen), zs, prog->nir[stage], &key);
      if (!mod) {
         ralloc_free(keybox);
         FREE(zm);
         return NULL;
      }
      zm->shader = mod;

      _mesa_hash_table_insert_pre_hashed(&prog->base.shader_cache[pstage], hash, keybox, zm);
      if (key.is_default_variant) {
         /* previously returned */
         *default_zm = zm;
         zm->default_variant = true;
         prog->default_variant_key[pstage] = keybox;
      }
   }
   return zm;
}

static void
zink_destroy_shader_module(struct zink_screen *screen, struct zink_shader_module *zm)
{
   vkDestroyShaderModule(screen->dev, zm->shader, NULL);
   free(zm);
}

static void
destroy_shader_cache(struct zink_screen *screen, struct hash_table *sc)
{
   hash_table_foreach(sc, entry) {
      struct zink_shader_module *zm = entry->data;
      zink_destroy_shader_module(screen, zm);
   }
}

static void
update_shader_modules(struct zink_context *ctx, struct zink_shader *stages[ZINK_SHADER_COUNT], struct zink_gfx_program *prog, bool disallow_reuse)
{
   struct zink_shader *dirty[ZINK_SHADER_COUNT] = {NULL};

   unsigned gfx_bits = u_bit_consecutive(PIPE_SHADER_VERTEX, 5);
   unsigned dirty_shader_stages = ctx->dirty_shader_stages & gfx_bits;
   if (!dirty_shader_stages)
      return;
   /* we need to map pipe_shader_type -> gl_shader_stage so we can ensure that we're compiling
    * the shaders in pipeline order and have builtin input/output locations match up after being compacted
    */
   while (dirty_shader_stages) {
      unsigned type = u_bit_scan(&dirty_shader_stages);
      dirty[tgsi_processor_to_shader_stage(type)] = stages[type];
   }

   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      /* we need to iterate over the stages in pipeline-order here */
      enum pipe_shader_type type = pipe_shader_type_from_mesa(i);
      assert(type < ZINK_SHADER_COUNT);
      if (dirty[i] || (stages[type] && !prog->modules[type])) {
         struct zink_shader_module *zm;
         zm = get_shader_module_for_stage(ctx, dirty[i] ? dirty[i] : stages[type], prog);
         prog->modules[type] = zm;
         ctx->gfx_pipeline_state.combined_dirty |= zm->shader != ctx->gfx_pipeline_state.modules[type];
         ctx->gfx_pipeline_state.modules[type] = zm->shader;
      } else if (!stages[type]) {
         ctx->gfx_pipeline_state.combined_dirty |= ctx->gfx_pipeline_state.modules[type] != VK_NULL_HANDLE;
         ctx->gfx_pipeline_state.modules[type] = VK_NULL_HANDLE;
      }
   }
   ctx->gfx_pipeline_state.module_hash = _mesa_hash_data(ctx->gfx_pipeline_state.modules, sizeof(ctx->gfx_pipeline_state.modules));
   unsigned clean = u_bit_consecutive(PIPE_SHADER_VERTEX, 5);
   ctx->dirty_shader_stages &= ~clean;
}

static uint32_t
hash_gfx_pipeline_state(const void *key)
{
   const struct zink_gfx_pipeline_state *state = key;
   uint32_t hash = _mesa_hash_data(key, offsetof(struct zink_gfx_pipeline_state, hash));
   if (state->have_EXT_extended_dynamic_state)
      return hash;
   return XXH32(&state->depth_stencil_alpha_state, sizeof(void*), hash);
}

static bool
equals_gfx_pipeline_state(const void *a, const void *b)
{
   const struct zink_gfx_pipeline_state *sa = a;
   const struct zink_gfx_pipeline_state *sb = b;
   if (!sa->have_EXT_extended_dynamic_state) {
      if (sa->vertex_buffers_enabled_mask != sb->vertex_buffers_enabled_mask)
         return false;
      /* if we don't have dynamic states, we have to hash the enabled vertex buffer bindings */
      uint32_t mask_a = sa->vertex_buffers_enabled_mask;
      uint32_t mask_b = sb->vertex_buffers_enabled_mask;
      while (mask_a || mask_b) {
         unsigned idx_a = u_bit_scan(&mask_a);
         unsigned idx_b = u_bit_scan(&mask_b);
         if (sa->vertex_strides[idx_a] != sb->vertex_strides[idx_b])
            return false;
      }
      if (sa->front_face != sb->front_face)
         return false;
      if (!!sa->depth_stencil_alpha_state != !!sb->depth_stencil_alpha_state ||
          (sa && sb && memcmp(sa->depth_stencil_alpha_state, sb->depth_stencil_alpha_state, sizeof(struct zink_depth_stencil_alpha_hw_state))))
         return false;
   }
   return !memcmp(sa->modules, sb->modules, sizeof(sa->modules)) &&
          !memcmp(a, b, offsetof(struct zink_gfx_pipeline_state, hash));
}

void
zink_update_gfx_program(struct zink_context *ctx, struct zink_gfx_program *prog)
{
   update_shader_modules(ctx, ctx->gfx_stages, prog, true);
}

VkPipelineLayout
zink_pipeline_layout_create(struct zink_screen *screen, struct zink_program *pg)
{
   VkPipelineLayoutCreateInfo plci = {0};
   plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

   plci.pSetLayouts = pg->dsl;
   plci.setLayoutCount = pg->num_dsl;

   VkPushConstantRange pcr[2] = {0};
   if (pg->is_compute) {
      if (((struct zink_compute_program*)pg)->shader->nir->info.stage == MESA_SHADER_KERNEL) {
         pcr[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
         pcr[0].offset = 0;
         pcr[0].size = sizeof(struct zink_cs_push_constant);
         plci.pushConstantRangeCount = 1;
      }
   } else {
      pcr[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      pcr[0].offset = offsetof(struct zink_gfx_push_constant, draw_mode_is_indexed);
      pcr[0].size = 2 * sizeof(unsigned);
      pcr[1].stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
      pcr[1].offset = offsetof(struct zink_gfx_push_constant, default_inner_level);
      pcr[1].size = sizeof(float) * 6;
      plci.pushConstantRangeCount = 2;
   }
   plci.pPushConstantRanges = &pcr[0];

   VkPipelineLayout layout;
   if (vkCreatePipelineLayout(screen->dev, &plci, NULL, &layout) != VK_SUCCESS) {
      debug_printf("vkCreatePipelineLayout failed!\n");
      return VK_NULL_HANDLE;
   }

   return layout;
}

static void
assign_io(struct zink_gfx_program *prog, struct zink_shader *stages[ZINK_SHADER_COUNT])
{
   struct zink_shader *shaders[PIPE_SHADER_TYPES];

   /* build array in pipeline order */
   for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++)
      shaders[tgsi_processor_to_shader_stage(i)] = stages[i];

   for (unsigned i = 0; i < MESA_SHADER_FRAGMENT;) {
      nir_shader *producer = shaders[i]->nir;
      for (unsigned j = i + 1; j < ZINK_SHADER_COUNT; i++, j++) {
         struct zink_shader *consumer = shaders[j];
         if (!consumer)
            continue;
         if (!prog->nir[producer->info.stage])
            prog->nir[producer->info.stage] = nir_shader_clone(prog, producer);
         if (!prog->nir[j])
            prog->nir[j] = nir_shader_clone(prog, consumer->nir);
         zink_compiler_assign_io(prog->nir[producer->info.stage], prog->nir[j]);
         i = j;
         break;
      }
   }
}

struct zink_gfx_program *
zink_create_gfx_program(struct zink_context *ctx,
                        struct zink_shader *stages[ZINK_SHADER_COUNT])
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_gfx_program *prog = rzalloc(NULL, struct zink_gfx_program);
   if (!prog)
      goto fail;

   pipe_reference_init(&prog->base.reference, 1);

   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      if (stages[i]) {
         _mesa_hash_table_init(&prog->base.shader_cache[i], prog, keybox_hash, keybox_equals);
         prog->shaders[i] = stages[i];
         prog->stages_present |= BITFIELD_BIT(i);
      }
   }
   if (stages[PIPE_SHADER_TESS_EVAL] && !stages[PIPE_SHADER_TESS_CTRL]) {
      prog->shaders[PIPE_SHADER_TESS_EVAL]->generated =
      prog->shaders[PIPE_SHADER_TESS_CTRL] =
        zink_shader_tcs_create(ctx, stages[PIPE_SHADER_VERTEX]);
        _mesa_hash_table_init(&prog->base.shader_cache[PIPE_SHADER_TESS_CTRL], prog, keybox_hash, keybox_equals);
      prog->stages_present |= BITFIELD_BIT(PIPE_SHADER_TESS_CTRL);
   }

   /* always force shader creation during init */
   ctx->dirty_shader_stages |= prog->stages_present;
   assign_io(prog, prog->shaders);

   update_shader_modules(ctx, prog->shaders, prog, false);
   prog->default_variant_hash = ctx->gfx_pipeline_state.module_hash;

   for (int i = 0; i < ARRAY_SIZE(prog->pipelines); ++i) {
      prog->pipelines[i] = _mesa_hash_table_create(NULL,
                                                   NULL,
                                                   equals_gfx_pipeline_state);
      if (!prog->pipelines[i])
         goto fail;
   }

   if (stages[PIPE_SHADER_GEOMETRY])
      prog->last_vertex_stage = stages[PIPE_SHADER_GEOMETRY];
   else if (stages[PIPE_SHADER_TESS_EVAL])
      prog->last_vertex_stage = stages[PIPE_SHADER_TESS_EVAL];
   else
      prog->last_vertex_stage = stages[PIPE_SHADER_VERTEX];

   struct mesa_sha1 sctx;
   _mesa_sha1_init(&sctx);
   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      if (prog->modules[i]) {
         _mesa_set_add(prog->shaders[i]->programs, prog);
         zink_gfx_program_reference(screen, NULL, prog);
         _mesa_sha1_update(&sctx, prog->shaders[i]->base.sha1, sizeof(prog->shaders[i]->base.sha1));
      }
   }
   _mesa_sha1_final(&sctx, prog->base.sha1);
   p_atomic_dec(&prog->base.reference.count);

   if (!screen->descriptor_program_init(ctx, &prog->base))
      goto fail;

   zink_screen_get_pipeline_cache(screen, &prog->base);
   return prog;

fail:
   if (prog)
      zink_destroy_gfx_program(screen, prog);
   return NULL;
}

static uint32_t
hash_compute_pipeline_state(const void *key)
{
   const struct zink_compute_pipeline_state *state = key;
   uint32_t hash = _mesa_hash_data(state, offsetof(struct zink_compute_pipeline_state, hash));
   if (state->use_local_size)
      hash = XXH32(&state->local_size[0], sizeof(state->local_size), hash);
   return hash;
}

void
zink_program_update_compute_pipeline_state(struct zink_context *ctx, struct zink_compute_program *comp, const uint block[3])
{
   struct zink_shader *zs = comp->shader;
   bool use_local_size = !(zs->nir->info.workgroup_size[0] ||
                           zs->nir->info.workgroup_size[1] ||
                           zs->nir->info.workgroup_size[2]);
   if (ctx->compute_pipeline_state.use_local_size != use_local_size)
      ctx->compute_pipeline_state.dirty = true;
   ctx->compute_pipeline_state.use_local_size = use_local_size;

   if (ctx->compute_pipeline_state.use_local_size) {
      for (int i = 0; i < ARRAY_SIZE(ctx->compute_pipeline_state.local_size); i++) {
         if (ctx->compute_pipeline_state.local_size[i] != block[i])
            ctx->compute_pipeline_state.dirty = true;
         ctx->compute_pipeline_state.local_size[i] = block[i];
      }
   } else
      ctx->compute_pipeline_state.local_size[0] =
      ctx->compute_pipeline_state.local_size[1] =
      ctx->compute_pipeline_state.local_size[2] = 0;
}

static bool
equals_compute_pipeline_state(const void *a, const void *b)
{
   return memcmp(a, b, offsetof(struct zink_compute_pipeline_state, hash)) == 0;
}

struct zink_compute_program *
zink_create_compute_program(struct zink_context *ctx, struct zink_shader *shader)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_compute_program *comp = rzalloc(NULL, struct zink_compute_program);
   if (!comp)
      goto fail;

   pipe_reference_init(&comp->base.reference, 1);
   comp->base.is_compute = true;
   /* TODO: cs shader keys placeholder for now */
   _mesa_hash_table_init(&comp->base.shader_cache[0], comp, _mesa_hash_pointer, _mesa_key_pointer_equal);

   comp->module = CALLOC_STRUCT(zink_shader_module);
   assert(comp->module);
   comp->module->shader = zink_shader_compile(screen, shader, shader->nir, NULL);
   assert(comp->module->shader);
   _mesa_hash_table_insert(&comp->base.shader_cache[0], shader, comp->module);

   ctx->dirty_shader_stages &= ~(1 << PIPE_SHADER_COMPUTE);

   comp->pipelines = _mesa_hash_table_create(NULL, hash_compute_pipeline_state,
                                             equals_compute_pipeline_state);

   _mesa_set_add(shader->programs, comp);
   comp->shader = shader;
   memcpy(comp->base.sha1, shader->base.sha1, sizeof(shader->base.sha1));

   if (!screen->descriptor_program_init(ctx, &comp->base))
      goto fail;

   zink_screen_get_pipeline_cache(screen, &comp->base);
   return comp;

fail:
   if (comp)
      zink_destroy_compute_program(screen, comp);
   return NULL;
}

uint32_t
zink_program_get_descriptor_usage(struct zink_context *ctx, enum pipe_shader_type stage, enum zink_descriptor_type type)
{
   struct zink_shader *zs = NULL;
   switch (stage) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_TESS_EVAL:
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_FRAGMENT:
      zs = ctx->gfx_stages[stage];
      break;
   case PIPE_SHADER_COMPUTE: {
      zs = ctx->compute_stage;
      break;
   }
   default:
      unreachable("unknown shader type");
   }
   if (!zs)
      return 0;
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return zs->ubos_used;
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return zs->ssbos_used;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return BITSET_TEST_RANGE(zs->nir->info.textures_used, 0, PIPE_MAX_SAMPLERS - 1);
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return zs->nir->info.images_used;
   default:
      unreachable("unknown descriptor type!");
   }
   return 0;
}

bool
zink_program_descriptor_is_buffer(struct zink_context *ctx, enum pipe_shader_type stage, enum zink_descriptor_type type, unsigned i)
{
   struct zink_shader *zs = NULL;
   switch (stage) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_TESS_EVAL:
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_FRAGMENT:
      zs = ctx->gfx_stages[stage];
      break;
   case PIPE_SHADER_COMPUTE: {
      zs = ctx->compute_stage;
      break;
   }
   default:
      unreachable("unknown shader type");
   }
   if (!zs)
      return false;
   return zink_shader_descriptor_is_buffer(zs, type, i);
}

static unsigned
get_num_bindings(struct zink_shader *zs, enum zink_descriptor_type type)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return zs->num_bindings[type];
   default:
      break;
   }
   unsigned num_bindings = 0;
   for (int i = 0; i < zs->num_bindings[type]; i++)
      num_bindings += zs->bindings[type][i].size;
   return num_bindings;
}

unsigned
zink_program_num_bindings_typed(const struct zink_program *pg, enum zink_descriptor_type type, bool is_compute)
{
   unsigned num_bindings = 0;
   if (is_compute) {
      struct zink_compute_program *comp = (void*)pg;
      return get_num_bindings(comp->shader, type);
   }
   struct zink_gfx_program *prog = (void*)pg;
   for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
      if (prog->shaders[i])
         num_bindings += get_num_bindings(prog->shaders[i], type);
   }
   return num_bindings;
}

unsigned
zink_program_num_bindings(const struct zink_program *pg, bool is_compute)
{
   unsigned num_bindings = 0;
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++)
      num_bindings += zink_program_num_bindings_typed(pg, i, is_compute);
   return num_bindings;
}

void
zink_destroy_gfx_program(struct zink_screen *screen,
                         struct zink_gfx_program *prog)
{
   if (prog->base.layout)
      vkDestroyPipelineLayout(screen->dev, prog->base.layout, NULL);

   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      if (prog->shaders[i]) {
         _mesa_set_remove_key(prog->shaders[i]->programs, prog);
         prog->shaders[i] = NULL;
         destroy_shader_cache(screen, &prog->base.shader_cache[i]);
      }
   }

   for (int i = 0; i < ARRAY_SIZE(prog->pipelines); ++i) {
      hash_table_foreach(prog->pipelines[i], entry) {
         struct gfx_pipeline_cache_entry *pc_entry = entry->data;

         vkDestroyPipeline(screen->dev, pc_entry->pipeline, NULL);
         free(pc_entry);
      }
      _mesa_hash_table_destroy(prog->pipelines[i], NULL);
   }
   if (prog->base.pipeline_cache)
      vkDestroyPipelineCache(screen->dev, prog->base.pipeline_cache, NULL);
   screen->descriptor_program_deinit(screen, &prog->base);

   ralloc_free(prog);
}

void
zink_destroy_compute_program(struct zink_screen *screen,
                         struct zink_compute_program *comp)
{
   if (comp->base.layout)
      vkDestroyPipelineLayout(screen->dev, comp->base.layout, NULL);

   if (comp->shader)
      _mesa_set_remove_key(comp->shader->programs, comp);

   hash_table_foreach(comp->pipelines, entry) {
      struct compute_pipeline_cache_entry *pc_entry = entry->data;

      vkDestroyPipeline(screen->dev, pc_entry->pipeline, NULL);
      free(pc_entry);
   }
   _mesa_hash_table_destroy(comp->pipelines, NULL);
   destroy_shader_cache(screen, &comp->base.shader_cache[0]);
   if (comp->base.pipeline_cache)
      vkDestroyPipelineCache(screen->dev, comp->base.pipeline_cache, NULL);
   screen->descriptor_program_deinit(screen, &comp->base);

   ralloc_free(comp);
}

static VkPrimitiveTopology
primitive_topology(enum pipe_prim_type mode)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

   case PIPE_PRIM_LINES:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

   case PIPE_PRIM_LINE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

   case PIPE_PRIM_TRIANGLES:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   case PIPE_PRIM_TRIANGLE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

   case PIPE_PRIM_TRIANGLE_FAN:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;

   case PIPE_PRIM_LINES_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;

   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;

   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;

   case PIPE_PRIM_PATCHES:
      return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

   default:
      unreachable("unexpected enum pipe_prim_type");
   }
}

VkPipeline
zink_get_gfx_pipeline(struct zink_context *ctx,
                      struct zink_gfx_program *prog,
                      struct zink_gfx_pipeline_state *state,
                      enum pipe_prim_type mode)
{
   if (!state->dirty && !state->combined_dirty && !state->vertex_state_dirty && mode == state->mode)
      return state->pipeline;

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkPrimitiveTopology vkmode = primitive_topology(mode);
   assert(vkmode <= ARRAY_SIZE(prog->pipelines));

   struct hash_entry *entry = NULL;

   if (state->dirty) {
      state->vertex_state_dirty = state->combined_dirty = true;
      state->hash = hash_gfx_pipeline_state(state);
      state->dirty = false;
   }
   if (state->combined_dirty) {
      state->vertex_state_dirty = true;
      state->combined_hash = XXH32(&state->module_hash, sizeof(uint32_t), state->hash);
      state->combined_dirty = false;
   }
   if (state->vertex_state_dirty) {
      uint32_t hash = state->combined_hash;
      if (!state->have_EXT_extended_dynamic_state) {
         /* if we don't have dynamic states, we have to hash the enabled vertex buffer bindings */
         uint32_t vertex_buffers_enabled_mask = state->vertex_buffers_enabled_mask;
         hash = XXH32(&vertex_buffers_enabled_mask, sizeof(uint32_t), hash);

         for (unsigned i = 0; i < state->element_state->num_bindings; i++) {
            struct pipe_vertex_buffer *vb = ctx->vertex_buffers + ctx->element_state->binding_map[i];
            state->vertex_strides[i] = vb->buffer.resource ? vb->stride : 0;
            hash = XXH32(&state->vertex_strides[i], sizeof(uint32_t), hash);
         }
      }
      state->final_hash = XXH32(&state->element_state, sizeof(void*), hash);
      state->vertex_state_dirty = false;
   }
   entry = _mesa_hash_table_search_pre_hashed(prog->pipelines[vkmode], state->final_hash, state);

   if (!entry) {
      util_queue_fence_wait(&prog->base.cache_fence);
      VkPipeline pipeline = zink_create_gfx_pipeline(screen, prog,
                                                     state, vkmode);
      if (pipeline == VK_NULL_HANDLE)
         return VK_NULL_HANDLE;

      struct gfx_pipeline_cache_entry *pc_entry = CALLOC_STRUCT(gfx_pipeline_cache_entry);
      if (!pc_entry)
         return VK_NULL_HANDLE;

      memcpy(&pc_entry->state, state, sizeof(*state));
      pc_entry->pipeline = pipeline;

      entry = _mesa_hash_table_insert_pre_hashed(prog->pipelines[vkmode], state->final_hash, state, pc_entry);
      assert(entry);
   }

   struct gfx_pipeline_cache_entry *cache_entry = entry->data;
   state->pipeline = cache_entry->pipeline;
   state->mode = mode;
   return state->pipeline;
}

VkPipeline
zink_get_compute_pipeline(struct zink_screen *screen,
                      struct zink_compute_program *comp,
                      struct zink_compute_pipeline_state *state)
{
   struct hash_entry *entry = NULL;

   if (!state->dirty)
      return state->pipeline;
   if (state->dirty) {
      state->hash = hash_compute_pipeline_state(state);
      state->dirty = false;
   }
   entry = _mesa_hash_table_search_pre_hashed(comp->pipelines, state->hash, state);

   if (!entry) {
      util_queue_fence_wait(&comp->base.cache_fence);
      VkPipeline pipeline = zink_create_compute_pipeline(screen, comp, state);

      if (pipeline == VK_NULL_HANDLE)
         return VK_NULL_HANDLE;

      struct compute_pipeline_cache_entry *pc_entry = CALLOC_STRUCT(compute_pipeline_cache_entry);
      if (!pc_entry)
         return VK_NULL_HANDLE;

      memcpy(&pc_entry->state, state, sizeof(*state));
      pc_entry->pipeline = pipeline;

      entry = _mesa_hash_table_insert_pre_hashed(comp->pipelines, state->hash, state, pc_entry);
      assert(entry);
   }

   struct compute_pipeline_cache_entry *cache_entry = entry->data;
   state->pipeline = cache_entry->pipeline;
   return state->pipeline;
}


static inline void
bind_stage(struct zink_context *ctx, enum pipe_shader_type stage,
           struct zink_shader *shader)
{
   if (stage == PIPE_SHADER_COMPUTE)
      ctx->compute_stage = shader;
   else
      ctx->gfx_stages[stage] = shader;
   ctx->dirty_shader_stages |= 1 << stage;
   if (shader && shader->nir->info.num_inlinable_uniforms)
      ctx->shader_has_inlinable_uniforms_mask |= 1 << stage;
   else
      ctx->shader_has_inlinable_uniforms_mask &= ~(1 << stage);
}

static void
zink_bind_vs_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   bind_stage(ctx, PIPE_SHADER_VERTEX, cso);
   if (!ctx->gfx_stages[PIPE_SHADER_GEOMETRY] &&
       !ctx->gfx_stages[PIPE_SHADER_TESS_EVAL]) {
      ctx->last_vertex_stage = cso;
   }
}

static void
zink_bind_fs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_FRAGMENT, cso);
}

static void
zink_bind_gs_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!!ctx->gfx_stages[PIPE_SHADER_GEOMETRY] != !!cso)
      ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_VERTEX) |
                                  BITFIELD_BIT(PIPE_SHADER_TESS_EVAL);
   bind_stage(ctx, PIPE_SHADER_GEOMETRY, cso);
   if (cso)
      ctx->last_vertex_stage = cso;
   else {
      if (ctx->gfx_stages[PIPE_SHADER_TESS_EVAL])
         ctx->last_vertex_stage = ctx->gfx_stages[PIPE_SHADER_TESS_EVAL];
      else
         ctx->last_vertex_stage = ctx->gfx_stages[PIPE_SHADER_VERTEX];
   }
}

static void
zink_bind_tcs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_TESS_CTRL, cso);
}

static void
zink_bind_tes_state(struct pipe_context *pctx,
                   void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   if (!!ctx->gfx_stages[PIPE_SHADER_TESS_EVAL] != !!cso) {
      if (!cso) {
         /* if unsetting a TESS that uses a generated TCS, ensure the TCS is unset */
         if (ctx->gfx_stages[PIPE_SHADER_TESS_EVAL]->generated)
            ctx->gfx_stages[PIPE_SHADER_TESS_CTRL] = NULL;
      }
      ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_VERTEX);
   }
   bind_stage(ctx, PIPE_SHADER_TESS_EVAL, cso);
   if (!ctx->gfx_stages[PIPE_SHADER_GEOMETRY]) {
      if (cso)
         ctx->last_vertex_stage = cso;
      else
         ctx->last_vertex_stage = ctx->gfx_stages[PIPE_SHADER_VERTEX];
   }
}

static void *
zink_create_cs_state(struct pipe_context *pctx,
                     const struct pipe_compute_state *shader)
{
   struct nir_shader *nir;
   if (shader->ir_type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->prog);
   else
      nir = (struct nir_shader *)shader->prog;

   return zink_shader_create(zink_screen(pctx->screen), nir, NULL);
}

static void
zink_bind_cs_state(struct pipe_context *pctx,
                   void *cso)
{
   bind_stage(zink_context(pctx), PIPE_SHADER_COMPUTE, cso);
}

void
zink_delete_shader_state(struct pipe_context *pctx, void *cso)
{
   zink_shader_free(zink_context(pctx), cso);
}

void *
zink_create_gfx_shader_state(struct pipe_context *pctx, const struct pipe_shader_state *shader)
{
   nir_shader *nir;
   if (shader->type != PIPE_SHADER_IR_NIR)
      nir = zink_tgsi_to_nir(pctx->screen, shader->tokens);
   else
      nir = (struct nir_shader *)shader->ir.nir;

   return zink_shader_create(zink_screen(pctx->screen), nir, &shader->stream_output);
}

static void
zink_delete_cached_shader_state(struct pipe_context *pctx, void *cso)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   util_shader_reference(pctx, &screen->shaders, &cso, NULL);
}

static void *
zink_create_cached_shader_state(struct pipe_context *pctx, const struct pipe_shader_state *shader)
{
   bool cache_hit;
   struct zink_screen *screen = zink_screen(pctx->screen);
   return util_live_shader_cache_get(pctx, &screen->shaders, shader, &cache_hit);
}

void
zink_program_init(struct zink_context *ctx)
{
   ctx->base.create_vs_state = zink_create_cached_shader_state;
   ctx->base.bind_vs_state = zink_bind_vs_state;
   ctx->base.delete_vs_state = zink_delete_cached_shader_state;

   ctx->base.create_fs_state = zink_create_cached_shader_state;
   ctx->base.bind_fs_state = zink_bind_fs_state;
   ctx->base.delete_fs_state = zink_delete_cached_shader_state;

   ctx->base.create_gs_state = zink_create_cached_shader_state;
   ctx->base.bind_gs_state = zink_bind_gs_state;
   ctx->base.delete_gs_state = zink_delete_cached_shader_state;

   ctx->base.create_tcs_state = zink_create_cached_shader_state;
   ctx->base.bind_tcs_state = zink_bind_tcs_state;
   ctx->base.delete_tcs_state = zink_delete_cached_shader_state;

   ctx->base.create_tes_state = zink_create_cached_shader_state;
   ctx->base.bind_tes_state = zink_bind_tes_state;
   ctx->base.delete_tes_state = zink_delete_cached_shader_state;

   ctx->base.create_compute_state = zink_create_cs_state;
   ctx->base.bind_compute_state = zink_bind_cs_state;
   ctx->base.delete_compute_state = zink_delete_shader_state;
}
