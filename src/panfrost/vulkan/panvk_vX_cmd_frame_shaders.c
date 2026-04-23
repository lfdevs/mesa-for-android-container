/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_frame_shaders.h"

#include "panvk_cmd_alloc.h"
#include "panvk_image_view.h"
#include "panvk_meta.h"
#include "panvk_shader.h"

#include "nir_builder.h"

#include "pan_afbc.h"
#include "pan_shader.h"
#include "pan_nir.h"

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct panvk_frame_shader_key {
   enum panvk_meta_object_key_type type;
   struct pan_fb_shader_key key;
};
PRAGMA_DIAGNOSTIC_POP

struct panvk_fb_iviews {
   const struct pan_image_view *rt_iviews[PAN_MAX_RTS];
   const struct pan_image_view *z, *s;
};

struct panvk_fb_sysvals {
   struct pan_fb_bbox render_area_px;
   float clear_depth;
   uint8_t clear_stencil;
   uint8_t _pad;
   uint16_t layer_id;
} __attribute__((aligned(FAU_WORD_SIZE)));
static_assert(sizeof(struct panvk_fb_sysvals) % FAU_WORD_SIZE == 0,
              "panvk_fb_sysvals is FAU_WORD-aligned.");

static uint32_t
key_locations_written(const struct panvk_frame_shader_key *key)
{
   uint32_t locations = 0;
   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
      if (pan_fb_shader_key_target_written(&key->key.rts[rt]))
         locations |= BITFIELD_BIT(FRAG_RESULT_DATA0 + rt);
   }

   if (pan_fb_shader_key_target_written(&key->key.z))
      locations |= BITFIELD_BIT(FRAG_RESULT_DEPTH);

   if (pan_fb_shader_key_target_written(&key->key.s))
      locations |= BITFIELD_BIT(FRAG_RESULT_STENCIL);

   return locations;
}

static uint32_t
shader_op_to_location_bit(enum pan_fb_shader_op op)
{
   if (op == PAN_FB_SHADER_COPY_Z)
      return BITFIELD_BIT(FRAG_RESULT_DEPTH);
   else if (op == PAN_FB_SHADER_COPY_S)
      return BITFIELD_BIT(FRAG_RESULT_STENCIL);

   if (op < PAN_FB_SHADER_COPY_RT_0 ||
       op >= PAN_FB_SHADER_COPY_RT_0 + PAN_MAX_RTS)
      return 0;

   uint8_t rt = op - PAN_FB_SHADER_COPY_RT_0;
   return BITFIELD_BIT(FRAG_RESULT_DATA0 + rt);
}

static uint32_t
key_target_to_locations_read(const struct pan_fb_shader_key_target *target)
{
   return shader_op_to_location_bit(target->in_bounds_op) |
          shader_op_to_location_bit(target->border_op);
}

static uint32_t
key_locations_read(const struct panvk_frame_shader_key *key)
{
   uint32_t locations = key_target_to_locations_read(&key->key.z) |
                        key_target_to_locations_read(&key->key.s);
   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++)
      locations |= key_target_to_locations_read(&key->key.rts[rt]);

   return locations;
}

static uint32_t
tex_index_for_loc(gl_frag_result loc, uint32_t locations_written)
{
   /* We could compact further than locations_written but this is probably
    * good enough.
    */
   assert(locations_written & BITFIELD_BIT(loc));
   return util_bitcount(locations_written & BITFIELD_MASK(loc));
}

static bool
compact_tex_indices(nir_builder *b, nir_tex_instr *tex,
                    const struct panvk_frame_shader_key *key)
{
   const uint32_t tex_idx =
      tex_index_for_loc(tex->texture_index, key_locations_written(key));

#if PAN_ARCH < 9
   tex->sampler_index = 0;
   tex->texture_index = tex_idx;
#else
   tex->sampler_index = pan_res_handle(0, 0);
   tex->texture_index = pan_res_handle(0, tex_idx + 1);
#endif

   return true;
}

#ifdef load_sysval
#undef load_sysval
#endif

static nir_def *
load_sysval(nir_builder *b, unsigned num_components,
            unsigned bit_size, unsigned offset)
{
   assert((offset * 8) % bit_size == 0);
   ASSERTED unsigned size = (num_components * bit_size) / 8;
   assert(offset + size <= sizeof(struct panvk_fb_sysvals));

   return nir_load_push_constant(b, num_components, bit_size,
                                 nir_imm_int(b, offset));
}

static bool
lower_sysval(nir_builder *b, nir_intrinsic_instr *intr,
             const struct panvk_frame_shader_key *key)
{
   b->cursor = nir_before_instr(&intr->instr);

   unsigned offset;
   nir_def *val;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_fb_render_area_pan:
      offset = offsetof(struct panvk_fb_sysvals, render_area_px);
      val = load_sysval(b, 4, 16, offset);
      break;
   case nir_intrinsic_load_clear_value_pan:
      switch (nir_intrinsic_io_semantics(intr).location) {
      case FRAG_RESULT_DEPTH:
         offset = offsetof(struct panvk_fb_sysvals, clear_depth);
         val = load_sysval(b, 1, 32, offset);
         break;
      case FRAG_RESULT_STENCIL:
         offset = offsetof(struct panvk_fb_sysvals, clear_stencil);
         val = nir_u2u32(b, load_sysval(b, 1, 8, offset));
         break;
      default:
         UNREACHABLE("Bifrost+ doesn't need to push clear colors");
      }
      break;
   case nir_intrinsic_load_layer_id:
      /* On v9+, we have real layered rendering */
      if (PAN_ARCH >= 9)
         return false;

      offset = offsetof(struct panvk_fb_sysvals, layer_id);
      val = nir_u2u32(b, load_sysval(b, 1, 16, offset));
      break;
   default:
      return false;
   }

   nir_def_replace(&intr->def, val);

   return true;
}

static bool
lower_instr(nir_builder *b, nir_instr *instr, void *cb_data)
{
   switch (instr->type) {
   case nir_instr_type_tex:
      return compact_tex_indices(b, nir_instr_as_tex(instr), cb_data);
   case nir_instr_type_intrinsic:
      return lower_sysval(b, nir_instr_as_intrinsic(instr), cb_data);
   default:
      return false;
   }
}

static VkResult
get_frame_shader(struct panvk_device *dev,
                 const struct panvk_frame_shader_key *key,
                 struct panvk_internal_shader **shader_out)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_internal_shader *shader;
   VkShaderEXT shader_handle = (VkShaderEXT)vk_meta_lookup_object(
      &dev->meta, VK_OBJECT_TYPE_SHADER_EXT, key, sizeof(*key));
   if (shader_handle != VK_NULL_HANDLE)
      goto out;

   const struct nir_shader_compiler_options *nir_options =
      pan_get_nir_shader_compiler_options(PAN_ARCH, false);
   nir_shader *nir = GENX(pan_get_fb_shader)(&key->key, nir_options);

   NIR_PASS(_, nir, nir_shader_instructions_pass, lower_instr,
            nir_metadata_control_flow, (void *)key);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   struct pan_compile_inputs inputs = {
      .gpu_id = phys_dev->kmod.dev->props.gpu_id,
      .gpu_variant = phys_dev->kmod.dev->props.gpu_variant,
      .is_blit = true,
   };

   pan_preprocess_nir(nir, inputs.gpu_id);
   pan_postprocess_nir(nir, inputs.gpu_id);

   VkResult result = panvk_per_arch(create_internal_shader)(
      dev, nir, &inputs, &shader);
   ralloc_free(nir);

   if (result != VK_SUCCESS)
      return result;

#if PAN_ARCH >= 9
   shader->spd = panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
   if (!panvk_priv_mem_check_alloc(shader->spd)) {
      vk_shader_destroy(&dev->vk, &shader->vk, NULL);
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   panvk_priv_mem_write_desc(shader->spd, 0, SHADER_PROGRAM, cfg) {
      cfg.stage = MALI_SHADER_STAGE_FRAGMENT;
      cfg.fragment_coverage_bitmask_type = MALI_COVERAGE_BITMASK_TYPE_GL;
      cfg.register_allocation =
         pan_register_allocation(shader->info.work_reg_count);
      cfg.binary = panvk_priv_mem_dev_addr(shader->code_mem);
      cfg.preload.r48_r63 = shader->info.preload >> 48;
   }
#endif

   shader_handle = (VkShaderEXT)vk_meta_cache_object(
      &dev->vk, &dev->meta, key, sizeof(*key), VK_OBJECT_TYPE_SHADER_EXT,
      (uint64_t)panvk_internal_shader_to_handle(shader));

out:
   shader = panvk_internal_shader_from_handle(shader_handle);
   *shader_out = shader;
   return VK_SUCCESS;
}

static VkResult
alloc_pre_post_dcds(struct panvk_cmd_buffer *cmdbuf,
                    struct pan_fb_frame_shaders *fs,
                    struct mali_draw_packed **dcds)
{
   if (fs->dcd_pointer)
      return VK_SUCCESS;

   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   uint32_t dcd_count = 3 * (PAN_ARCH < 9 ? render->layer_count : 1);

   struct pan_ptr dcd = panvk_cmd_alloc_desc_array(cmdbuf, dcd_count, DRAW);
   if (!dcd.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   fs->dcd_pointer = dcd.gpu;
   *dcds = dcd.cpu;

   return VK_SUCCESS;
}

static enum mali_register_file_format
get_reg_fmt(enum pan_fb_shader_data_type data_type)
{
   switch (data_type) {
   case PAN_FB_SHADER_DATA_TYPE_F32:
      return MALI_REGISTER_FILE_FORMAT_F32;
   case PAN_FB_SHADER_DATA_TYPE_U32:
      return MALI_REGISTER_FILE_FORMAT_U32;
   case PAN_FB_SHADER_DATA_TYPE_I32:
      return MALI_REGISTER_FILE_FORMAT_I32;
   default:
      assert(!"Invalid reg type");
      return MALI_REGISTER_FILE_FORMAT_F32;
   }
}

static struct panvk_image_view *
get_color_attachment_view(struct panvk_cmd_buffer *cmdbuf, uint32_t i)
{
   return cmdbuf->state.gfx.render.color_attachments.preload_iviews[i] != NULL
             ? cmdbuf->state.gfx.render.color_attachments.preload_iviews[i]
             : cmdbuf->state.gfx.render.color_attachments.iviews[i];
}

static struct panvk_image_view *
get_z_attachment_view(struct panvk_cmd_buffer *cmdbuf)
{
   return cmdbuf->state.gfx.render.z_attachment.preload_iview
             ? cmdbuf->state.gfx.render.z_attachment.preload_iview
             : cmdbuf->state.gfx.render.z_attachment.iview;
}

static struct panvk_image_view *
get_s_attachment_view(struct panvk_cmd_buffer *cmdbuf)
{
   return cmdbuf->state.gfx.render.s_attachment.preload_iview
             ? cmdbuf->state.gfx.render.s_attachment.preload_iview
             : cmdbuf->state.gfx.render.s_attachment.iview;
}

static const struct pan_image_view *
get_iview(const struct panvk_fb_iviews *iviews, gl_frag_result location)
{
   switch (location) {
   default: {
      assert(location >= FRAG_RESULT_DATA0);
      unsigned rt = location - FRAG_RESULT_DATA0;
      assert(rt < PAN_MAX_RTS);
      return iviews->rt_iviews[rt];
   }

   case FRAG_RESULT_DEPTH:
      return iviews->z;

   case FRAG_RESULT_STENCIL:
      return iviews->s;
   }
}

static void
fill_textures(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_fb_iviews *iviews,
              const struct panvk_frame_shader_key *key,
              struct mali_texture_packed *textures)
{
   uint32_t locations = key_locations_written(key);
   uint32_t idx = 0;

   u_foreach_bit(loc, locations) {
      assert(idx == tex_index_for_loc(loc, locations));
      const struct pan_image_view *iview = get_iview(iviews, loc);
      if (iview == NULL) {
         textures[idx++] = (struct mali_texture_packed){0};
         continue;
      }

      struct pan_image_view pview = *iview;
      if (loc == FRAG_RESULT_DEPTH)
         pview.format = util_format_get_depth_only(pview.format);
      else if (loc == FRAG_RESULT_STENCIL)
         pview.format = util_format_stencil_only(pview.format);
#if PAN_ARCH == 7
      else if (pan_afbc_supports_format(PAN_ARCH, pview.format))
         GENX(pan_texture_afbc_reswizzle)(&pview);
#endif

      const uint32_t tex_payload_size =
         GENX(pan_texture_estimate_payload_size)(&pview);

#if PAN_ARCH <= 7
      const uint32_t tex_payload_align = pan_alignment(SURFACE_WITH_STRIDE);
#else
      const uint32_t tex_payload_align = pan_alignment(NULL_PLANE);
#endif

      struct pan_ptr payload = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, tex_payload_size, tex_payload_align);

      GENX(pan_sampled_texture_emit)(&pview, &textures[idx++], &payload);
   }

   assert(idx == util_bitcount(locations));
}

static void
fill_bds(const struct pan_fb_layout *fb,
         const struct panvk_frame_shader_key *key,
         struct mali_blend_packed *bds)
{
   for (unsigned i = 0; i < fb->rt_count; i++) {
      pan_pack(&bds[i], BLEND, cfg) {
         if (!pan_fb_shader_key_target_written(&key->key.rts[i]) ||
             fb->rt_formats[i] == PIPE_FORMAT_NONE) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
            continue;
         }

         cfg.round_to_fb_precision = true;
         cfg.srgb = util_format_is_srgb(fb->rt_formats[i]);
         cfg.internal.mode = MALI_BLEND_MODE_OPAQUE;
         cfg.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
         cfg.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
         cfg.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
         cfg.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
         cfg.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
         cfg.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
         cfg.equation.color_mask = 0xf;

         cfg.internal.fixed_function.num_comps = 4;
         cfg.internal.fixed_function.conversion.memory_format =
            GENX(pan_dithered_format_from_pipe_format)(fb->rt_formats[i], false);
         cfg.internal.fixed_function.rt = i;
#if PAN_ARCH < 9
         cfg.internal.fixed_function.conversion.register_format =
            get_reg_fmt(key->key.rts[i].data_type);
#endif
      }
   }
}

#if PAN_ARCH < 9
static VkResult
cmd_emit_dcd(struct panvk_cmd_buffer *cmdbuf,
             const struct pan_fb_layout *fb,
             const struct pan_fb_load *load,
             const struct panvk_fb_iviews *iviews,
             const struct panvk_frame_shader_key *key,
             struct pan_fb_frame_shaders *fs,
             unsigned dcd_idx,
             struct mali_draw_packed **dcds)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_internal_shader *shader = NULL;

   VkResult result = get_frame_shader(dev, key, &shader);
   if (result != VK_SUCCESS)
      return result;

   uint32_t locations_written = key_locations_written(key);
   bool preload_z = locations_written & BITFIELD_BIT(FRAG_RESULT_DEPTH);
   bool preload_s = locations_written & BITFIELD_BIT(FRAG_RESULT_STENCIL);

   uint32_t tex_count = util_bitcount(locations_written);
   uint32_t bd_count = fb->rt_count;

   struct pan_ptr rsd = panvk_cmd_alloc_desc_aggregate(
      cmdbuf, PAN_DESC(RENDERER_STATE),
      PAN_DESC_ARRAY(bd_count, BLEND));
   if (!rsd.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_cast_and_pack(rsd.cpu, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(&shader->info,
                             panvk_priv_mem_dev_addr(shader->code_mem), &cfg);

      cfg.shader.texture_count = tex_count;
      cfg.shader.sampler_count = 1;

      cfg.multisample_misc.sample_mask = 0xFFFF;
      cfg.multisample_misc.multisample_enable = fb->sample_count > 1;
      cfg.multisample_misc.evaluate_per_sample = shader->info.fs.sample_shading;

      cfg.multisample_misc.depth_function = MALI_FUNC_ALWAYS;
      cfg.multisample_misc.depth_write_mask = preload_z;

      cfg.stencil_mask_misc.stencil_enable = preload_s;
      cfg.stencil_mask_misc.stencil_mask_front = 0xFF;
      cfg.stencil_mask_misc.stencil_mask_back = 0xFF;
      cfg.stencil_front.compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_front.stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.stencil_front.mask = 0xFF;

      cfg.stencil_back = cfg.stencil_front;

      if (preload_z || preload_s) {
         /* Writing Z/S requires late updates */
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
         cfg.properties.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_LATE;
      } else {
         /* Skipping ATEST requires forcing Z/S */
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.properties.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
      }

      /* However, while shaders writing Z/S can normally be killed, on v6
       * for frame shaders it can cause GPU timeouts, so only allow colour
       * blit shaders to be killed. */
      cfg.properties.allow_forward_pixel_to_kill =
         key->type == PANVK_META_OBJECT_KEY_FB_COLOR_PRELOAD_SHADER;

      if (PAN_ARCH == 6)
         cfg.properties.allow_forward_pixel_to_be_killed =
            key->type == PANVK_META_OBJECT_KEY_FB_COLOR_PRELOAD_SHADER;
   }

   fill_bds(fb, key, rsd.cpu + pan_size(RENDERER_STATE));

   struct panvk_batch *batch = cmdbuf->cur_batch;

   /* Align on 32x32 tiles */
   const struct pan_fb_bbox aligned_bbox =
      pan_fb_bbox_align(fb->render_area_px, 32, 32);

   struct pan_ptr vpd = panvk_cmd_alloc_desc(cmdbuf, VIEWPORT);
   if (!vpd.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_cast_and_pack(vpd.cpu, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = aligned_bbox.min_x;
      cfg.scissor_minimum_y = aligned_bbox.min_y;
      cfg.scissor_maximum_x = aligned_bbox.max_x;
      cfg.scissor_maximum_y = aligned_bbox.max_y;
   }

   struct pan_ptr sampler = panvk_cmd_alloc_desc(cmdbuf, SAMPLER);
   if (!sampler.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_cast_and_pack(sampler.cpu, SAMPLER, cfg) {
      cfg.seamless_cube_map = false;
      cfg.normalized_coordinates = false;
      cfg.clamp_integer_array_indices = false;
      cfg.minify_nearest = true;
      cfg.magnify_nearest = true;
   }

   struct pan_ptr textures =
      panvk_cmd_alloc_desc_array(cmdbuf, tex_count, TEXTURE);
   if (!textures.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   fill_textures(cmdbuf, iviews, key, textures.cpu);

   result = alloc_pre_post_dcds(cmdbuf, fs, dcds);
   if (result != VK_SUCCESS)
      return result;

   struct mali_draw_packed dcd_base;

   /* If we got a preload without any draw, we end up with a NULL TLS
    * descriptor. Allocate a dummy one (no TLS, no WLS) to get things working. */
   if (!batch->tls.cpu) {
      panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true);
      GENX(pan_emit_tls)(&batch->tlsinfo, batch->tls.cpu);
   }

   pan_pack(&dcd_base, DRAW, cfg) {
      cfg.thread_storage = batch->tls.gpu;
      cfg.state = rsd.gpu;

      cfg.viewport = vpd.gpu;

      cfg.textures = textures.gpu;
      cfg.samplers = sampler.gpu;

#if PAN_ARCH >= 6
      /* Until we decide to support FB CRC, we can consider that untouched tiles
       * should never be written back. */
      cfg.clean_fragment_write = true;
#endif
   }

   uint32_t layer_count = cmdbuf->state.gfx.render.layer_count;
   struct pan_ptr faus = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, layer_count * sizeof(struct panvk_fb_sysvals), 16);
   for (uint32_t l = 0; l < layer_count; l++) {
      const struct panvk_fb_sysvals sysvals = {
         .render_area_px = fb->render_area_px,
         .clear_depth = load ? load->z.clear.depth : 0,
         .clear_stencil = load? load->s.clear.stencil : 0,
         .layer_id = l,
      };
      struct pan_ptr layer_faus = pan_ptr_offset(faus, sizeof(sysvals) * l);
      memcpy(layer_faus.cpu, &sysvals, sizeof(sysvals));

      struct mali_draw_packed dcd_layer;
      pan_pack(&dcd_layer, DRAW, cfg) {
         cfg.push_uniforms = layer_faus.gpu;
      };

      pan_merge(&dcd_layer, &dcd_base, DRAW);
      (*dcds)[(l * 3) + dcd_idx] = dcd_layer;
   }

   return VK_SUCCESS;
}
#else
static VkResult
cmd_emit_dcd(struct panvk_cmd_buffer *cmdbuf,
             const struct pan_fb_layout *fb,
             const struct pan_fb_load *load,
             const struct panvk_fb_iviews *iviews,
             struct panvk_frame_shader_key *key,
             struct pan_fb_frame_shaders *fs,
             unsigned dcd_idx,
             struct mali_draw_packed **dcds)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_internal_shader *shader = NULL;

   VkResult result = get_frame_shader(dev, key, &shader);
   if (result != VK_SUCCESS)
      return result;

   uint32_t locations_written = key_locations_written(key);
   uint32_t locations_read = key_locations_read(key);
   bool preload_color =
      locations_written & BITFIELD_RANGE(FRAG_RESULT_DATA0, PAN_MAX_RTS);
   bool preload_z = locations_written & BITFIELD_BIT(FRAG_RESULT_DEPTH);
   bool preload_s = locations_written & BITFIELD_BIT(FRAG_RESULT_STENCIL);

   uint32_t bd_count = preload_color ? fb->rt_count : 0;
   struct pan_ptr bds = panvk_cmd_alloc_desc_array(cmdbuf, bd_count, BLEND);
   if (bd_count > 0 && !bds.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   uint32_t tex_count = util_bitcount(locations_written);
   uint32_t desc_count = tex_count + 1;

   struct pan_ptr descs = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   if (!descs.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_sampler_packed *sampler = descs.cpu;

   pan_pack(sampler, SAMPLER, cfg) {
      cfg.seamless_cube_map = false;
      cfg.normalized_coordinates = false;
      cfg.clamp_integer_array_indices = false;
      cfg.minify_nearest = true;
      cfg.magnify_nearest = true;
   }

   fill_textures(cmdbuf, iviews, key, descs.cpu + PANVK_DESCRIPTOR_SIZE);

   uint32_t rt_written =
      (locations_written >> FRAG_RESULT_DATA0) & BITFIELD_MASK(PAN_MAX_RTS);
   uint32_t rt_read =
      (locations_read >> FRAG_RESULT_DATA0) & BITFIELD_MASK(PAN_MAX_RTS);

   if (preload_color)
      fill_bds(fb, key, bds.cpu);

   /* Resource table sizes need to be multiples of 4. We use only one
    * element here though.
    */
   const uint32_t res_table_size = MALI_RESOURCE_TABLE_SIZE_ALIGNMENT;
   struct pan_ptr res_table =
      panvk_cmd_alloc_desc_array(cmdbuf, res_table_size, RESOURCE);
   if (!res_table.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   memset(res_table.cpu, 0, pan_size(RESOURCE) * res_table_size);

   pan_cast_and_pack(res_table.cpu, RESOURCE, cfg) {
      cfg.address = descs.gpu;
      cfg.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   }

   struct pan_ptr zsd = panvk_cmd_alloc_desc(cmdbuf, DEPTH_STENCIL);
   if (!zsd.cpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_cast_and_pack(zsd.cpu, DEPTH_STENCIL, cfg) {
      cfg.depth_function = MALI_FUNC_ALWAYS;
      cfg.depth_write_enable = preload_z;

      if (preload_z)
         cfg.depth_source = MALI_DEPTH_SOURCE_SHADER;

      cfg.stencil_test_enable = preload_s;
      cfg.stencil_from_shader = preload_s;

      cfg.front_compare_function = MALI_FUNC_ALWAYS;
      cfg.front_stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.front_depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.front_depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.front_write_mask = 0xFF;
      cfg.front_value_mask = 0xFF;

      cfg.back_compare_function = MALI_FUNC_ALWAYS;
      cfg.back_stencil_fail = MALI_STENCIL_OP_REPLACE;
      cfg.back_depth_fail = MALI_STENCIL_OP_REPLACE;
      cfg.back_depth_pass = MALI_STENCIL_OP_REPLACE;
      cfg.back_write_mask = 0xFF;
      cfg.back_value_mask = 0xFF;

      cfg.depth_cull_enable = false;
   }

   result = alloc_pre_post_dcds(cmdbuf, fs, dcds);
   if (result != VK_SUCCESS)
      return result;

   struct pan_ptr faus = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panvk_fb_sysvals), 16);
   const struct panvk_fb_sysvals sysvals = {
      .render_area_px = fb->render_area_px,
      .clear_depth = load ? load->z.clear.depth : 0,
      .clear_stencil = load ? load->s.clear.stencil : 0,
   };
   memcpy(faus.cpu, &sysvals, sizeof(sysvals));

   pan_pack(&(*dcds)[dcd_idx], DRAW, cfg) {
      if (preload_z || preload_s) {
         /* ZS_EMIT requires late update/kill */
         cfg.flags_0.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
         cfg.flags_0.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_LATE;
      } else {
         /* Skipping ATEST requires forcing Z/S */
         cfg.flags_0.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
      }

      if (preload_color) {
         cfg.blend = bds.gpu;
         cfg.blend_count = bd_count;
         cfg.flags_1.render_target_mask =
            cmdbuf->state.gfx.render.bound_attachments &
            MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;
      } else {
         cfg.blend_count = 0;
      }

      /* Only allow color preload shaders to be killed since post-frame
       * shaders are likely to read from the framebuffer.
       */
      cfg.flags_0.allow_forward_pixel_to_kill =
         key->type == PANVK_META_OBJECT_KEY_FB_COLOR_PRELOAD_SHADER;
      cfg.flags_0.allow_forward_pixel_to_be_killed = true;
      cfg.depth_stencil = zsd.gpu;
      cfg.flags_1.sample_mask = 0xFFFF;
      cfg.flags_0.multisample_enable = fb->sample_count > 1;
      cfg.flags_0.evaluate_per_sample = shader->info.fs.sample_shading;
      cfg.flags_0.clean_fragment_write = true;

#if PAN_ARCH >= 12
      cfg.fragment_resources = res_table.gpu | res_table_size;
      cfg.fragment_shader = panvk_priv_mem_dev_addr(shader->spd);
      cfg.fragment_fau.pointer = faus.gpu;
      cfg.fragment_fau.count = sizeof(struct panvk_fb_sysvals) / FAU_WORD_SIZE;
      cfg.thread_storage = cmdbuf->state.gfx.tsd;
#else
      cfg.maximum_z = 1.0;
      cfg.shader.resources = res_table.gpu | res_table_size;
      cfg.shader.shader = panvk_priv_mem_dev_addr(shader->spd);
      cfg.shader.thread_storage = cmdbuf->state.gfx.tsd;
      cfg.shader.fau_count = sizeof(struct panvk_fb_sysvals) / FAU_WORD_SIZE;
      cfg.shader.fau = faus.gpu;
#endif
      cfg.flags_2.write_mask = rt_written;
      cfg.flags_2.read_mask = rt_read;
#if PAN_ARCH >= 11
      cfg.flags_2.no_shader_depth_read =
         !(locations_read & BITFIELD_BIT(FRAG_RESULT_DEPTH));
      cfg.flags_2.no_shader_stencil_read =
         !(locations_read & BITFIELD_BIT(FRAG_RESULT_STENCIL));
#endif
   }

   return VK_SUCCESS;
}
#endif

static bool
always_load(const struct pan_fb_load *load,
            const struct panvk_frame_shader_key *key)
{
   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
      if (pan_fb_shader_key_target_written(&key->key.rts[rt]) &&
          load->rts[rt].always)
         return true;
   }

   return (pan_fb_shader_key_target_written(&key->key.z) && load->z.always) ||
          (pan_fb_shader_key_target_written(&key->key.s) && load->s.always);
}

static VkResult
cmd_preload_zs_attachments(struct panvk_cmd_buffer *cmdbuf,
                           const struct pan_fb_layout *fb,
                           const struct pan_fb_load *load,
                           struct pan_fb_frame_shaders *fs,
                           struct mali_draw_packed **dcds)
{
   struct panvk_frame_shader_key key = {
      .type = PANVK_META_OBJECT_KEY_FB_ZS_PRELOAD_SHADER,
   };
   if (!GENX(pan_fb_load_shader_key_fill)(&key.key, fb, load, true))
      return VK_SUCCESS;

   const struct panvk_fb_iviews iviews = {
      .z = load->z.iview,
      .s = load->s.iview,
   };

   const uint32_t dcd_idx = 1;
   VkResult result = cmd_emit_dcd(cmdbuf, fb, load, &iviews, &key,
                                  fs, dcd_idx, dcds);
   if (result != VK_SUCCESS)
      return result;

   /* We could use INTERSECT on Valhall too, but EARLY_ZS_ALWAYS has the
    * advantage of reloading the ZS tile buffer one or more tiles ahead,
    * making ZS data immediately available for any ZS tests taking place in
    * other shaders.  Thing's haven't been benchmarked to determine what's
    * preferable (saving bandwidth vs having ZS preloaded earlier), so let's
    * leave it like that for now.
    *
    * On v13+, we don't have EARLY_ZS_ALWAYS instead we use PREPASS_ALWAYS.
    */
#if PAN_ARCH >= 13
   fs->modes[dcd_idx] = MALI_PRE_POST_FRAME_SHADER_MODE_PREPASS_ALWAYS;
#elif PAN_ARCH >= 9
   fs->modes[dcd_idx] = MALI_PRE_POST_FRAME_SHADER_MODE_EARLY_ZS_ALWAYS;
#else
   /* We could use INTERSECT on Bifrost v7 too, but EARLY_ZS_ALWAYS has the
    * advantage of reloading the ZS tile buffer one or more tiles ahead,
    * making ZS data immediately available for any ZS tests taking place in
    * other shaders.  Thing's haven't been benchmarked to determine what's
    * preferable (saving bandwidth vs having ZS preloaded earlier), so let's
    * leave it like that for now.  HOWEVER, EARLY_ZS_ALWAYS doesn't exist on
    * 7.0, only on 7.2 and later, so check for that!
    */
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   unsigned arch_major = PAN_ARCH_MAJOR(pdev->kmod.dev->props.gpu_id);
   unsigned arch_minor = PAN_ARCH_MINOR(pdev->kmod.dev->props.gpu_id);

   if (arch_major > 7 || (arch_major == 7 && arch_minor >= 2))
      fs->modes[dcd_idx] = MALI_PRE_POST_FRAME_SHADER_MODE_EARLY_ZS_ALWAYS;
   else if (always_load(load, &key))
      fs->modes[dcd_idx] = MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS;
   else
      fs->modes[dcd_idx] = MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT;
#endif

   return VK_SUCCESS;
}

static VkResult
cmd_preload_color_attachments(struct panvk_cmd_buffer *cmdbuf,
                              const struct pan_fb_layout *fb,
                              const struct pan_fb_load *load,
                              struct pan_fb_frame_shaders *fs,
                              struct mali_draw_packed **dcds)
{
   struct panvk_frame_shader_key key = {
      .type = PANVK_META_OBJECT_KEY_FB_COLOR_PRELOAD_SHADER,
   };
   if (!GENX(pan_fb_load_shader_key_fill)(&key.key, fb, load, false))
      return VK_SUCCESS;

   struct panvk_fb_iviews iviews = { };
   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++)
      iviews.rt_iviews[rt] = load->rts[rt].iview;

   const uint32_t dcd_idx = 0;
   VkResult result = cmd_emit_dcd(cmdbuf, fb, load, &iviews, &key,
                                  fs, dcd_idx, dcds);
   if (result != VK_SUCCESS)
      return result;

   fs->modes[dcd_idx] = always_load(load, &key)
                        ? MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS
                        : MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT;

   return VK_SUCCESS;
}

static VkResult
cmd_resolve_attachments(struct panvk_cmd_buffer *cmdbuf,
                        const struct pan_fb_layout *fb,
                        const struct pan_fb_resolve *resolve,
                        struct pan_fb_frame_shaders *fs,
                        struct mali_draw_packed **dcds)
{
   if (resolve == NULL)
      return VK_SUCCESS;

   struct panvk_frame_shader_key key = {
      .type = PANVK_META_OBJECT_KEY_FB_RESOLVE_SHADER,
   };
   if (!GENX(pan_fb_resolve_shader_key_fill)(&key.key, fb, resolve))
      return VK_SUCCESS;

   struct panvk_fb_iviews iviews = {
      .z = resolve->z.iview,
      .s = resolve->s.iview,
   };
   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++)
      iviews.rt_iviews[rt] = resolve->rts[rt].iview;

   const uint32_t dcd_idx = 2;
   VkResult result = cmd_emit_dcd(cmdbuf, fb, NULL, &iviews, &key,
                                  fs, dcd_idx, dcds);
   if (result != VK_SUCCESS)
      return result;

   fs->modes[dcd_idx] = MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS;

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(cmd_get_frame_shaders)(struct panvk_cmd_buffer *cmdbuf,
                                      const struct pan_fb_layout *fb,
                                      const struct pan_fb_load *load,
                                      const struct pan_fb_resolve *resolve,
                                      struct pan_fb_frame_shaders *fs_out)
{
   *fs_out = (struct pan_fb_frame_shaders) { .dcd_pointer = 0 };
   struct mali_draw_packed *dcds = NULL;

   VkResult result = cmd_preload_color_attachments(cmdbuf, fb, load,
                                                   fs_out, &dcds);
   if (result != VK_SUCCESS)
      return result;

   result = cmd_preload_zs_attachments(cmdbuf, fb, load, fs_out, &dcds);
   if (result != VK_SUCCESS)
      return result;

   result = cmd_resolve_attachments(cmdbuf, fb, resolve, fs_out, &dcds);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}
