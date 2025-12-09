/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "compiler/nir/nir.h"
#include "draw/draw_context.h"
#include "nir/nir_to_tgsi.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/os_misc.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"

#include "i915_context.h"
#include "i915_debug.h"
#include "i915_fpc.h"
#include "i915_public.h"
#include "i915_reg.h"
#include "i915_resource.h"
#include "i915_screen.h"
#include "i915_winsys.h"

/*
 * Probe functions
 */

static const char *
i915_get_vendor(struct pipe_screen *screen)
{
   return "Mesa Project";
}

static const char *
i915_get_device_vendor(struct pipe_screen *screen)
{
   return "Intel";
}

static const char *
i915_get_name(struct pipe_screen *screen)
{
   static char buffer[128];
   const char *chipset;

   switch (i915_screen(screen)->iws->pci_id) {
   case PCI_CHIP_I915_G:
      chipset = "915G";
      break;
   case PCI_CHIP_I915_GM:
      chipset = "915GM";
      break;
   case PCI_CHIP_I945_G:
      chipset = "945G";
      break;
   case PCI_CHIP_I945_GM:
      chipset = "945GM";
      break;
   case PCI_CHIP_I945_GME:
      chipset = "945GME";
      break;
   case PCI_CHIP_G33_G:
      chipset = "G33";
      break;
   case PCI_CHIP_Q35_G:
      chipset = "Q35";
      break;
   case PCI_CHIP_Q33_G:
      chipset = "Q33";
      break;
   case PCI_CHIP_PINEVIEW_G:
      chipset = "Pineview G";
      break;
   case PCI_CHIP_PINEVIEW_M:
      chipset = "Pineview M";
      break;
   default:
      chipset = "unknown";
      break;
   }

   snprintf(buffer, sizeof(buffer), "i915 (chipset: %s)", chipset);
   return buffer;
}

static const nir_shader_compiler_options i915_compiler_options = {
   .fdot_replicates = true,
   .fuse_ffma32 = true,
   .lower_bitops = true, /* required for !CAP_INTEGERS nir_to_tgsi */
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_fdiv = true,
   .lower_fdph = true,
   .lower_flrp32 = true,
   .lower_fmod = true,
   .lower_sincos = true,
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .force_indirect_unrolling = nir_var_all,
   .force_indirect_unrolling_sampler = true,
   .max_unroll_iterations = 32,
   .no_integers = true,
   .has_fused_comp_and_csel = true,
};

static const struct nir_shader_compiler_options gallivm_nir_options = {
   .fdot_replicates = true,
   .lower_bitops = true, /* required for !CAP_INTEGERS nir_to_tgsi */
   .lower_scmp = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_fsat = true,
   .lower_bitfield_insert = true,
   .lower_bitfield_extract = true,
   .lower_fdph = true,
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_fmod = true,
   .lower_hadd = true,
   .lower_uadd_sat = true,
   .lower_usub_sat = true,
   .lower_iadd_sat = true,
   .lower_ldexp = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_split = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_half_2x16 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   .lower_mul_2x32_64 = true,
   .lower_ifind_msb = true,
   .max_unroll_iterations = 32,
   .lower_cs_local_index_to_id = true,
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .lower_device_index_to_zero = true,
   /* .support_16bit_alu = true, */
   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(MESA_SHADER_STAGES),
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(MESA_SHADER_STAGES),
   .no_integers = true,
};

static void
i915_optimize_nir(struct nir_shader *s)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS(progress, s, nir_lower_vars_to_ssa);
      NIR_PASS(progress, s, nir_opt_copy_prop);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_remove_phis);

      nir_opt_peephole_select_options peephole_discard_options = {
         .limit = 0,
         .discard_ok = true,
      };
      NIR_PASS(progress, s, nir_opt_peephole_select, &peephole_discard_options);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_cse);
      NIR_PASS(progress, s, nir_opt_find_array_copies);
      NIR_PASS(progress, s, nir_opt_if, nir_opt_if_optimize_phi_true_false);

      nir_opt_peephole_select_options peephole_select_options = {
         .limit = ~0, /* flatten all IFs. */
         .indirect_load_ok = true,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, s, nir_opt_peephole_select, &peephole_select_options);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_shrink_stores, true);
      NIR_PASS(progress, s, nir_opt_shrink_vectors, false);
      NIR_PASS(progress, s, nir_opt_loop);
      NIR_PASS(progress, s, nir_opt_undef);
      NIR_PASS(progress, s, nir_opt_loop_unroll);

   } while (progress);

   NIR_PASS(progress, s, nir_remove_dead_variables, nir_var_function_temp,
            NULL);

   /* Group texture loads together to try to avoid hitting the
    * texture indirection phase limit.
    */
   NIR_PASS(_, s, nir_opt_group_loads, nir_group_all, ~0);
}

static void
i915_finalize_nir(struct pipe_screen *pscreen, struct nir_shader *s,
                  bool optimize)
{
   if (s->info.stage == MESA_SHADER_FRAGMENT)
      i915_optimize_nir(s);

   /* st_program.c's parameter list optimization requires that future nir
    * variants don't reallocate the uniform storage, so we have to remove
    * uniforms that occupy storage.  But we don't want to remove samplers,
    * because they're needed for YUV variant lowering.
    */
   nir_remove_dead_derefs(s);
   nir_foreach_uniform_variable_safe (var, s) {
      if (var->data.mode == nir_var_uniform &&
          (glsl_type_get_image_count(var->type) ||
           glsl_type_get_sampler_count(var->type)))
         continue;

      exec_node_remove(&var->node);
   }
   nir_validate_shader(s, "after uniform var removal");

   nir_sweep(s);
}

static void
i915_init_shader_caps(struct i915_screen *is)
{
   struct pipe_shader_caps *caps =
      (struct pipe_shader_caps *)&is->base.shader_caps[MESA_SHADER_VERTEX];

   draw_init_shader_caps(caps);

   caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);
   /* mesa/st requires that this cap is the same across stages, and the FS
    * can't do ints.
    */
   caps->integers = false;
   /* i915 can't do these, and even if gallivm NIR can we call nir_to_tgsi
    * manually and TGSI can't.
    */
   caps->int16 = false;
   caps->fp16 = false;
   caps->fp16_derivatives = false;
   caps->fp16_const_buffers = false;
   caps->glsl_16bit_load_dst = false;
   /* While draw could normally handle this for the VS, the NIR lowering
    * to regs can't handle our non-native-integers, so we have to lower to
    * if ladders.
    */
   caps->indirect_temp_addr = false;
   caps->max_texture_samplers = false;
   caps->max_sampler_views = false;
   caps->max_shader_buffers = false;
   caps->max_shader_images = false;

   caps = (struct pipe_shader_caps *)&is->base.shader_caps[MESA_SHADER_FRAGMENT];

   caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);
   /* XXX: some of these are just shader model 2.0 values, fix this! */
   caps->max_instructions = I915_MAX_ALU_INSN + I915_MAX_TEX_INSN;
   caps->max_alu_instructions = I915_MAX_ALU_INSN;
   caps->max_tex_instructions = I915_MAX_TEX_INSN;
   caps->max_tex_indirections = 4;
   caps->max_inputs = 10;
   caps->max_outputs = 1;
   caps->max_const_buffer0_size = 32 * sizeof(float[4]);
   caps->max_const_buffers = 1;
   /* 16 inter-phase temps, 3 intra-phase temps.  i915c reported 16. too. */
   caps->max_temps = 16;
   caps->max_texture_samplers =
   caps->max_sampler_views = I915_TEX_UNITS;
}

static void
i915_init_screen_caps(struct i915_screen *is)
{
   struct pipe_caps *caps = (struct pipe_caps *)&is->base.caps;

   u_init_pipe_screen_caps(&is->base, 1);

   /* Supported features (boolean caps). */
   caps->anisotropic_filter = true;
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->primitive_restart = true; /* draw module */
   caps->primitive_restart_fixed_index = true;
   caps->vertex_element_instance_divisor = true;
   caps->blend_equation_separate = true;
   caps->vs_instanceid = true;
   caps->vertex_color_clamped = true;
   caps->user_vertex_buffers = true;
   caps->mixed_color_depth_bits = true;
   caps->tgsi_texcoord = true;

   caps->texture_transfer_modes =
   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;

   caps->allow_mapped_buffers_during_execution = false;

   /* Can't expose shareable shaders because the draw shaders reference the
    * draw module's state, which is per-context.
    */
   caps->shareable_shaders = false;

   caps->max_gs_invocations = 32;

   caps->max_shader_buffer_size = 1 << 27;

   caps->max_viewports = 1;

   caps->min_map_buffer_alignment = 64;

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 120;

   caps->constant_buffer_offset_alignment = 16;

   /* Texturing. */
   caps->max_texture_2d_size = 1 << (I915_MAX_TEXTURE_2D_LEVELS - 1);
   caps->max_texture_3d_levels = I915_MAX_TEXTURE_3D_LEVELS;
   caps->max_texture_cube_levels = I915_MAX_TEXTURE_2D_LEVELS;

   /* Render targets. */
   caps->max_render_targets = 1;

   caps->max_vertex_attrib_stride = 2048;

   /* Fragment coordinate conventions. */
   caps->fs_coord_origin_upper_left =
   caps->fs_coord_pixel_center_half_integer = true;
   caps->endianness = PIPE_ENDIAN_LITTLE;
   caps->max_varyings = 10;

   caps->nir_images_as_deref = false;

   caps->vendor_id = 0x8086;
   caps->device_id = is->iws->pci_id;

   /* Once a batch uses more than 75% of the maximum mappable size, we
    * assume that there's some fragmentation, and we start doing extra
    * flushing, etc.  That's the big cliff apps will care about.
    */
   const int gpu_mappable_megabytes = is->iws->aperture_size(is->iws) * 3 / 4;
   uint64_t system_memory;
   caps->video_memory =
      os_get_total_physical_memory(&system_memory) ?
      MIN2(gpu_mappable_megabytes, (int)(system_memory >> 20)) : 0;
   caps->uma = true;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;

   caps->max_line_width =
   caps->max_line_width_aa = 7.5;

   caps->max_point_size =
   caps->max_point_size_aa = 255.0;

   caps->max_texture_anisotropy = 4.0;

   caps->max_texture_lod_bias = 16.0;
}

bool
i915_is_format_supported(struct pipe_screen *screen, enum pipe_format format,
                         enum pipe_texture_target target, unsigned sample_count,
                         unsigned storage_sample_count, unsigned tex_usage)
{
   static const enum pipe_format tex_supported[] = {
      PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_B8G8R8A8_SRGB,
      PIPE_FORMAT_B8G8R8X8_UNORM, PIPE_FORMAT_R8G8B8A8_UNORM,
      PIPE_FORMAT_R8G8B8X8_UNORM, PIPE_FORMAT_B4G4R4A4_UNORM,
      PIPE_FORMAT_B5G6R5_UNORM, PIPE_FORMAT_B5G5R5A1_UNORM,
      PIPE_FORMAT_B10G10R10A2_UNORM, PIPE_FORMAT_L8_UNORM, PIPE_FORMAT_A8_UNORM,
      PIPE_FORMAT_I8_UNORM, PIPE_FORMAT_L8A8_UNORM, PIPE_FORMAT_UYVY,
      PIPE_FORMAT_YUYV,
      /* XXX why not?
      PIPE_FORMAT_Z16_UNORM, */
      PIPE_FORMAT_DXT1_RGB, PIPE_FORMAT_DXT1_SRGB, PIPE_FORMAT_DXT1_RGBA,
      PIPE_FORMAT_DXT1_SRGBA, PIPE_FORMAT_DXT3_RGBA, PIPE_FORMAT_DXT3_SRGBA,
      PIPE_FORMAT_DXT5_RGBA, PIPE_FORMAT_DXT5_SRGBA, PIPE_FORMAT_Z24X8_UNORM,
      PIPE_FORMAT_FXT1_RGB, PIPE_FORMAT_FXT1_RGBA,
      PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_FORMAT_NONE /* list terminator */
   };
   static const enum pipe_format render_supported[] = {
      PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_B8G8R8X8_UNORM,
      PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_FORMAT_R8G8B8X8_UNORM,
      PIPE_FORMAT_B5G6R5_UNORM,   PIPE_FORMAT_B5G5R5A1_UNORM,
      PIPE_FORMAT_B4G4R4A4_UNORM, PIPE_FORMAT_B10G10R10A2_UNORM,
      PIPE_FORMAT_L8_UNORM,       PIPE_FORMAT_A8_UNORM,
      PIPE_FORMAT_I8_UNORM,       PIPE_FORMAT_NONE /* list terminator */
   };
   static const enum pipe_format depth_supported[] = {
      /* XXX why not?
      PIPE_FORMAT_Z16_UNORM, */
      PIPE_FORMAT_Z24X8_UNORM, PIPE_FORMAT_Z24_UNORM_S8_UINT,
      PIPE_FORMAT_NONE /* list terminator */
   };
   const enum pipe_format *list;
   uint32_t i;

   if (sample_count > 1)
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if (tex_usage & PIPE_BIND_DEPTH_STENCIL)
      list = depth_supported;
   else if (tex_usage & PIPE_BIND_RENDER_TARGET)
      list = render_supported;
   else if (tex_usage & PIPE_BIND_SAMPLER_VIEW)
      list = tex_supported;
   else
      return true; /* PIPE_BIND_{VERTEX,INDEX}_BUFFER */

   for (i = 0; list[i] != PIPE_FORMAT_NONE; i++) {
      if (list[i] == format)
         return true;
   }

   return false;
}

/*
 * Fence functions
 */

static void
i915_fence_reference(struct pipe_screen *screen, struct pipe_fence_handle **ptr,
                     struct pipe_fence_handle *fence)
{
   struct i915_screen *is = i915_screen(screen);

   is->iws->fence_reference(is->iws, ptr, fence);
}

static bool
i915_fence_finish(struct pipe_screen *screen, struct pipe_context *ctx,
                  struct pipe_fence_handle *fence, uint64_t timeout)
{
   struct i915_screen *is = i915_screen(screen);

   if (!timeout)
      return is->iws->fence_signalled(is->iws, fence) == 1;

   return is->iws->fence_finish(is->iws, fence) == 1;
}

/*
 * Generic functions
 */

static void
i915_destroy_screen(struct pipe_screen *screen)
{
   struct i915_screen *is = i915_screen(screen);

   if (is->iws)
      is->iws->destroy(is->iws);

   FREE(is);
}

static int
i915_screen_get_fd(struct pipe_screen *screen)
{
   struct i915_screen *is = i915_screen(screen);

   return is->iws->get_fd(is->iws);
}

/**
 * Create a new i915_screen object
 */
struct pipe_screen *
i915_screen_create(struct i915_winsys *iws)
{
   struct i915_screen *is = CALLOC_STRUCT(i915_screen);

   if (!is)
      return NULL;

   switch (iws->pci_id) {
   case PCI_CHIP_I915_G:
   case PCI_CHIP_I915_GM:
      is->is_i945 = false;
      break;

   case PCI_CHIP_I945_G:
   case PCI_CHIP_I945_GM:
   case PCI_CHIP_I945_GME:
   case PCI_CHIP_G33_G:
   case PCI_CHIP_Q33_G:
   case PCI_CHIP_Q35_G:
   case PCI_CHIP_PINEVIEW_G:
   case PCI_CHIP_PINEVIEW_M:
      is->is_i945 = true;
      break;

   default:
      debug_printf("%s: unknown pci id 0x%x, cannot create screen\n", __func__,
                   iws->pci_id);
      FREE(is);
      return NULL;
   }

   is->iws = iws;

   is->base.destroy = i915_destroy_screen;

   is->base.get_name = i915_get_name;
   is->base.get_vendor = i915_get_vendor;
   is->base.get_device_vendor = i915_get_device_vendor;
   is->base.get_screen_fd = i915_screen_get_fd;
   is->base.finalize_nir = i915_finalize_nir;
   is->base.is_format_supported = i915_is_format_supported;

   is->base.context_create = i915_create_context;

   is->base.fence_reference = i915_fence_reference;
   is->base.fence_finish = i915_fence_finish;

   is->base.nir_options[MESA_SHADER_VERTEX] = &gallivm_nir_options;
   is->base.nir_options[MESA_SHADER_FRAGMENT] = &i915_compiler_options;

   i915_init_screen_resource_functions(is);

   i915_init_shader_caps(is);
   i915_init_screen_caps(is);

   i915_debug_init(is);

   return &is->base;
}
