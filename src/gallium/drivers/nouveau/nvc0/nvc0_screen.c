/*
 * Copyright 2010 Christoph Bumiller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <xf86drm.h>
#include "drm-uapi/nouveau_drm.h"
#include <nvif/class.h>
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_screen.h"
#include "pipe/p_screen.h"

#include "nouveau_vp3_video.h"

#include "nv50_ir_driver.h"

#include "nvc0/nvc0_context.h"
#include "nvc0/nvc0_screen.h"

#include "nvc0/mme/com9097.mme.h"
#include "nvc0/mme/com90c0.mme.h"
#include "nvc0/mme/comc597.mme.h"

#include "nv50/g80_texture.xml.h"

static bool
nvc0_screen_is_format_supported(struct pipe_screen *pscreen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned bindings)
{
   const struct util_format_description *desc = util_format_description(format);

   if (sample_count > 8)
      return false;
   if (!(0x117 & (1 << sample_count))) /* 0, 1, 2, 4 or 8 */
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   /* Short-circuit the rest of the logic -- this is used by the gallium frontend
    * to determine valid MS levels in a no-attachments scenario.
    */
   if (format == PIPE_FORMAT_NONE && bindings & PIPE_BIND_RENDER_TARGET)
      return true;

   if ((bindings & PIPE_BIND_SAMPLER_VIEW) && (target != PIPE_BUFFER))
      if (util_format_get_blocksizebits(format) == 3 * 32)
         return false;

   if (bindings & PIPE_BIND_LINEAR)
      if (util_format_is_depth_or_stencil(format) ||
          (target != PIPE_TEXTURE_1D &&
           target != PIPE_TEXTURE_2D &&
           target != PIPE_TEXTURE_RECT) ||
          sample_count > 1)
         return false;

   /* Restrict ETC2 and ASTC formats here. These are only supported on GK20A
    * and GM20B.
    */
   if ((desc->layout == UTIL_FORMAT_LAYOUT_ETC ||
        desc->layout == UTIL_FORMAT_LAYOUT_ASTC) &&
       nouveau_screen(pscreen)->device->chipset != 0x12b &&
       nouveau_screen(pscreen)->class_3d != NVEA_3D_CLASS)
      return false;

   /* shared is always supported */
   bindings &= ~(PIPE_BIND_LINEAR |
                 PIPE_BIND_SHARED);

   if (bindings & PIPE_BIND_SHADER_IMAGE) {
      if (format == PIPE_FORMAT_B8G8R8A8_UNORM &&
          nouveau_screen(pscreen)->class_3d < NVE4_3D_CLASS) {
         /* This should work on Fermi, but for currently unknown reasons it
          * does not and results in breaking reads from pbos. */
         return false;
      }
   }

   if (bindings & PIPE_BIND_INDEX_BUFFER) {
      if (format != PIPE_FORMAT_R8_UINT &&
          format != PIPE_FORMAT_R16_UINT &&
          format != PIPE_FORMAT_R32_UINT)
         return false;
      bindings &= ~PIPE_BIND_INDEX_BUFFER;
   }

   return (( nvc0_format_table[format].usage |
            nvc0_vertex_format[format].usage) & bindings) == bindings;
}

static void
nvc0_init_shader_caps(struct nvc0_screen *screen)
{
   const uint16_t class_3d = screen->base.class_3d;

   for (unsigned i = 0; i <= MESA_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&screen->base.base.shader_caps[i];

      caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
      caps->max_instructions =
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections = 16384;
      caps->max_control_flow_depth = 16;
      caps->max_inputs = 0x200 / 16;
      caps->max_outputs = 32;
      caps->max_const_buffer0_size = NVC0_MAX_CONSTBUF_SIZE;
      caps->max_const_buffers = NVC0_MAX_PIPE_CONSTBUFS;
      caps->indirect_temp_addr = true;
      caps->indirect_const_addr = true;
      caps->max_temps = NVC0_CAP_MAX_PROGRAM_TEMPS;
      caps->cont_supported = true;
      caps->tgsi_sqrt_supported = true;
      caps->subroutines = true;
      caps->integers = true;
      caps->max_shader_buffers = NVC0_MAX_BUFFERS;
      caps->max_texture_samplers = (class_3d >= NVE4_3D_CLASS) ? 32 : 16;
      caps->max_sampler_views = (class_3d >= NVE4_3D_CLASS) ? 32 : 16;
      caps->max_shader_images =
         class_3d >= NVE4_3D_CLASS ||
         i == MESA_SHADER_FRAGMENT ||
         i == MESA_SHADER_COMPUTE ? NVC0_MAX_IMAGES : 0;
   }
}

static void
nvc0_init_compute_caps(struct nvc0_screen *screen)
{
   struct nouveau_device *dev = screen->base.device;
   const uint16_t obj_class = screen->compute->oclass;

   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&screen->base.base.compute_caps;

   caps->grid_dimension = 3;

   caps->max_grid_size[0] = obj_class >= NVE4_COMPUTE_CLASS ? 0x7fffffff : 65535;
   caps->max_grid_size[1] = 65535;
   caps->max_grid_size[2] = 65535;

   caps->max_block_size[0] = 1024;
   caps->max_block_size[1] = 1024;
   caps->max_block_size[2] = 64;

   caps->max_threads_per_block = 1024;

   caps->max_variable_threads_per_block = obj_class >= NVE4_COMPUTE_CLASS ? 1024 : 512;
   caps->max_mem_alloc_size =
   caps->max_global_size = nouveau_device_get_global_mem_size(dev); /* g[] */

   caps->max_local_size = /* s[] */
      obj_class == GM200_COMPUTE_CLASS ? 96 << 10 :
      (obj_class == GM107_COMPUTE_CLASS ? 64 << 10 : 48 << 10);

   caps->subgroup_sizes = 32;
   caps->max_compute_units = screen->mp_count_compute;
   caps->max_clock_frequency = 512; /* FIXME: arbitrary limit */
   caps->address_bits = 64;
}

static void
nvc0_init_screen_caps(struct nvc0_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.base.caps;

   u_init_pipe_screen_caps(&screen->base.base, 1);

   const uint16_t class_3d = screen->base.class_3d;
   struct nouveau_device *dev = screen->base.device;

   /* non-boolean caps */
   caps->max_texture_2d_size = 16384;
   caps->max_texture_cube_levels = 15;
   caps->max_texture_3d_levels = 12;
   caps->max_texture_array_layers = 2048;
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   caps->min_texture_gather_offset = -32;
   caps->max_texture_gather_offset = 31;
   caps->max_texel_buffer_elements = 128 * 1024 * 1024;
   caps->glsl_feature_level = 430;
   caps->glsl_feature_level_compatibility = 430;
   caps->max_render_targets = 8;
   caps->max_dual_source_render_targets = 1;
   caps->viewport_subpixel_bits =
   caps->rasterizer_subpixel_bits = 8;
   caps->max_stream_output_buffers = 4;
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = 128;
   caps->max_geometry_output_vertices =
   caps->max_geometry_total_output_components = 1024;
   caps->max_vertex_streams = 4;
   caps->max_gs_invocations = 32;
   caps->max_shader_buffer_size = 1 << 27;
   caps->max_vertex_attrib_stride = 2048;
   caps->max_vertex_element_src_offset = 2047;
   caps->constant_buffer_offset_alignment = 256;
   caps->texture_buffer_offset_alignment = class_3d < GM107_3D_CLASS ?
      256 /* IMAGE bindings require alignment to 256 */ : 16;
   caps->shader_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = NOUVEAU_MIN_BUFFER_MAP_ALIGN;
   caps->max_viewports = NVC0_MAX_VIEWPORTS;
   caps->max_texture_gather_components = 4;
   caps->texture_border_color_quirk = PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50;
   caps->endianness = PIPE_ENDIAN_LITTLE;
   caps->max_shader_patch_varyings = 30;
   caps->max_window_rectangles = NVC0_MAX_WINDOW_RECTANGLES;
   caps->max_conservative_raster_subpixel_precision_bias = class_3d >= GM200_3D_CLASS ? 8 : 0;
   caps->max_texture_upload_memory_budget = 64 * 1024 * 1024;
   /* NOTE: These only count our slots for GENERIC varyings.
    * The address space may be larger, but the actual hard limit seems to be
    * less than what the address space layout permits, so don't add TEXCOORD,
    * COLOR, etc. here.
    */
   caps->max_varyings = 0x1f0 / 16;
   caps->max_vertex_buffers = 16;
   caps->gl_begin_end_buffer_size = 512 * 1024; /* TODO: Investigate tuning this */
   caps->max_texture_mb = 0; /* TODO: use 1/2 of VRAM for this? */

   caps->timer_resolution = 1000;

   caps->supported_prim_modes_with_restart =
   caps->supported_prim_modes = BITFIELD_MASK(MESA_PRIM_COUNT);

   /* supported caps */
   caps->texture_mirror_clamp = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->texture_swizzle = true;
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->anisotropic_filter = true;
   caps->seamless_cube_map = true;
   caps->cube_map_array = true;
   caps->texture_buffer_objects = true;
   caps->texture_multisample = true;
   caps->depth_clip_disable = true;
   caps->tgsi_texcoord = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->fragment_color_clamped = true;
   caps->vertex_color_unclamped = true;
   caps->vertex_color_clamped = true;
   caps->query_timestamp = true;
   caps->query_time_elapsed = true;
   caps->occlusion_query = true;
   caps->stream_output_pause_resume = true;
   caps->stream_output_interleave_buffers = true;
   caps->query_pipeline_statistics = true;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->quads_follow_provoking_vertex_convention = true;
   caps->start_instance = true;
   caps->draw_indirect = true;
   caps->user_vertex_buffers = true;
   caps->texture_query_lod = true;
   caps->sample_shading = true;
   caps->texture_gather_offsets = true;
   caps->texture_gather_sm5 = true;
   caps->fs_fine_derivative = true;
   caps->conditional_render_inverted = true;
   caps->sampler_view_target = true;
   caps->clip_halfz = true;
   caps->polygon_offset_clamp = true;
   caps->multisample_z_resolve = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->depth_bounds_test = true;
   caps->texture_query_samples = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->force_persample_interp = true;
   caps->draw_parameters = true;
   caps->shader_pack_half_float = true;
   caps->multi_draw_indirect = true;
   caps->memobj = true;
   caps->multi_draw_indirect_params = true;
   caps->fs_face_is_integer_sysval = true;
   caps->query_buffer_object = true;
   caps->invalidate_buffer = true;
   caps->string_marker = true;
   caps->framebuffer_no_attachment = true;
   caps->cull_distance = true;
   caps->robust_buffer_access_behavior = true;
   caps->shader_group_vote = true;
   caps->shader_array_components = true;
   caps->legacy_math_rules = true;
   caps->doubles = true;
   caps->int64 = true;
   caps->tgsi_tex_txf_lz = true;
   caps->shader_clock = true;
   caps->compute = true;
   caps->can_bind_const_buffer_as_vertex = true;
   caps->query_so_overflow = true;
   caps->image_atomic_inc_wrap = true;
   caps->demote_to_helper_invocation = true;
   caps->device_reset_status_query = true;
   caps->texture_shadow_lod = true;
   caps->clear_scissored = true;
   caps->image_store_formatted = true;
   caps->query_memory_info = true;
   caps->texture_transfer_modes =
      screen->base.vram_domain & NOUVEAU_BO_VRAM ? PIPE_TEXTURE_TRANSFER_BLIT : 0;
   caps->fbfetch = class_3d >= NVE4_3D_CLASS ? 1 : 0; /* needs testing on fermi */
   caps->seamless_cube_map_per_texture =
   caps->shader_ballot = class_3d >= NVE4_3D_CLASS;
   caps->bindless_texture = class_3d >= NVE4_3D_CLASS;
   caps->image_atomic_float_add = class_3d < GM107_3D_CLASS; /* needs additional lowering */
   caps->polygon_mode_fill_rectangle =
   caps->vs_layer_viewport =
   caps->tes_layer_viewport =
   caps->post_depth_coverage =
   caps->conservative_raster_post_snap_triangles =
   caps->conservative_raster_post_snap_points_lines =
   caps->conservative_raster_post_depth_coverage =
   caps->programmable_sample_locations =
   caps->viewport_swizzle =
   caps->viewport_mask =
   caps->sampler_reduction_minmax = class_3d >= GM200_3D_CLASS;
   caps->conservative_raster_pre_snap_triangles = class_3d >= GP100_3D_CLASS;
   caps->resource_from_user_memory_compute_only =
   caps->system_svm = screen->base.has_svm;

   caps->gl_spirv = true;
   caps->gl_spirv_variable_pointers = true;

   /* nir related caps */
   caps->nir_images_as_deref = false;

   caps->pci_group = dev->info.pci.domain;
   caps->pci_bus = dev->info.pci.bus;
   caps->pci_device = dev->info.pci.dev;
   caps->pci_function = dev->info.pci.func;

   caps->opencl_integer_functions = false; /* could be done */
   caps->integer_multiply_32x16 = false; /* could be done */
   caps->map_unsynchronized_thread_safe = false; /* when we fix MT stuff */
   caps->alpha_to_coverage_dither_control = false; /* TODO */
   caps->shader_atomic_int64 = false; /* TODO */
   caps->hardware_gl_select = false;

   caps->vendor_id = 0x10de;
   caps->device_id = dev->info.device_id;
   caps->video_memory = dev->vram_size >> 20;
   caps->uma = screen->base.is_uma;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa = 10.0f;
   caps->max_point_size = 63.0f;
   caps->max_point_size_aa = 63.375f;
   caps->max_texture_anisotropy = 16.0f;
   caps->max_texture_lod_bias = 15.0f;
   caps->min_conservative_raster_dilate = 0.0f;
   caps->max_conservative_raster_dilate = class_3d >= GM200_3D_CLASS ? 0.75f : 0.0f;
   caps->conservative_raster_dilate_granularity = class_3d >= GM200_3D_CLASS ? 0.25f : 0.0f;
}

static void
nvc0_screen_get_sample_pixel_grid(struct pipe_screen *pscreen,
                                  unsigned sample_count,
                                  unsigned *width, unsigned *height)
{
   switch (sample_count) {
   case 0:
   case 1:
      /* this could be 4x4, but the GL state tracker makes it difficult to
       * create a 1x MSAA texture and smaller grids save CB space */
      *width = 2;
      *height = 4;
      break;
   case 2:
      *width = 2;
      *height = 4;
      break;
   case 4:
      *width = 2;
      *height = 2;
      break;
   case 8:
      *width = 1;
      *height = 2;
      break;
   default:
      assert(0);
   }
}

static void
nvc0_screen_destroy(struct pipe_screen *pscreen)
{
   struct nvc0_screen *screen = nvc0_screen(pscreen);

   if (!screen->base.initialized)
      return;

   if (screen->blitter)
      nvc0_blitter_destroy(screen);
   if (screen->pm.prog) {
      screen->pm.prog->code = NULL; /* hardcoded, don't FREE */
      nvc0_program_destroy(NULL, screen->pm.prog);
      FREE(screen->pm.prog);
   }

   nouveau_bo_ref(NULL, &screen->text);
   nouveau_bo_ref(NULL, &screen->uniform_bo);
   nouveau_bo_ref(NULL, &screen->tls);
   nouveau_bo_ref(NULL, &screen->txc);
   nouveau_bo_ref(NULL, &screen->fence.bo);
   nouveau_bo_ref(NULL, &screen->poly_cache);

   nouveau_heap_free(&screen->lib_code);
   nouveau_heap_destroy(&screen->text_heap);

   FREE(screen->tic.entries);

   nouveau_object_del(&screen->eng3d);
   nouveau_object_del(&screen->eng2d);
   nouveau_object_del(&screen->m2mf);
   nouveau_object_del(&screen->copy);
   nouveau_object_del(&screen->compute);
   nouveau_object_del(&screen->nvsw);

   nouveau_screen_fini(&screen->base);
   simple_mtx_destroy(&screen->state_lock);

   FREE(screen);
}

static int
nvc0_graph_set_macro(struct nvc0_screen *screen, uint32_t m, unsigned pos,
                     unsigned size, const uint32_t *data)
{
   struct nouveau_pushbuf *push = screen->base.pushbuf;

   size /= 4;

   assert((pos + size) <= 0x800);

   BEGIN_NVC0(push, SUBC_3D(NVC0_GRAPH_MACRO_ID), 2);
   PUSH_DATA (push, (m - 0x3800) / 8);
   PUSH_DATA (push, pos);
   BEGIN_1IC0(push, SUBC_3D(NVC0_GRAPH_MACRO_UPLOAD_POS), size + 1);
   PUSH_DATA (push, pos);
   PUSH_DATAp(push, data, size);

   return pos + size;
}

static int
tu102_graph_set_macro(struct nvc0_screen *screen, uint32_t m, unsigned pos,
                     unsigned size, const uint32_t *data)
{
   struct nouveau_pushbuf *push = screen->base.pushbuf;

   size /= 4;

   assert((pos + size) <= 0x800);

   BEGIN_NVC0(push, SUBC_3D(NVC0_GRAPH_MACRO_ID), 2);
   PUSH_DATA (push, (m - 0x3800) / 8);
   PUSH_DATA (push, pos);
   BEGIN_1IC0(push, SUBC_3D(NVC0_GRAPH_MACRO_UPLOAD_POS), size + 1);
   PUSH_DATA (push, pos);
   PUSH_DATAp(push, data, size);

   return pos + (size / 3);
}

static void
nvc0_magic_3d_init(struct nouveau_pushbuf *push, uint16_t obj_class)
{
   BEGIN_NVC0(push, SUBC_3D(0x10cc), 1);
   PUSH_DATA (push, 0xff);
   BEGIN_NVC0(push, SUBC_3D(0x10e0), 2);
   PUSH_DATA (push, 0xff);
   PUSH_DATA (push, 0xff);
   BEGIN_NVC0(push, SUBC_3D(0x10ec), 2);
   PUSH_DATA (push, 0xff);
   PUSH_DATA (push, 0xff);
   if (obj_class < GV100_3D_CLASS) {
      BEGIN_NVC0(push, SUBC_3D(0x074c), 1);
      PUSH_DATA (push, 0x3f);
   }

   BEGIN_NVC0(push, SUBC_3D(0x16a8), 1);
   PUSH_DATA (push, (3 << 16) | 3);
   BEGIN_NVC0(push, SUBC_3D(0x1794), 1);
   PUSH_DATA (push, (2 << 16) | 2);

   if (obj_class < GM107_3D_CLASS) {
      BEGIN_NVC0(push, SUBC_3D(0x12ac), 1);
      PUSH_DATA (push, 0);
   }
   BEGIN_NVC0(push, SUBC_3D(0x0218), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x10fc), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x1290), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x12d8), 2);
   PUSH_DATA (push, 0x10);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x1140), 1);
   PUSH_DATA (push, 0x10);
   BEGIN_NVC0(push, SUBC_3D(0x1610), 1);
   PUSH_DATA (push, 0xe);

   BEGIN_NVC0(push, NVC0_3D(VERTEX_ID_GEN_MODE), 1);
   PUSH_DATA (push, NVC0_3D_VERTEX_ID_GEN_MODE_DRAW_ARRAYS_ADD_START);
   BEGIN_NVC0(push, SUBC_3D(0x030c), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, SUBC_3D(0x0300), 1);
   PUSH_DATA (push, 3);

   if (obj_class < GV100_3D_CLASS) {
      BEGIN_NVC0(push, SUBC_3D(0x02d0), 1);
      PUSH_DATA (push, 0x3fffff);
   }
   BEGIN_NVC0(push, SUBC_3D(0x0fdc), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, SUBC_3D(0x19c0), 1);
   PUSH_DATA (push, 1);

   if (obj_class < GM107_3D_CLASS) {
      BEGIN_NVC0(push, SUBC_3D(0x075c), 1);
      PUSH_DATA (push, 3);

      if (obj_class >= NVE4_3D_CLASS) {
         BEGIN_NVC0(push, SUBC_3D(0x07fc), 1);
         PUSH_DATA (push, 1);
      }
   }

   /* TODO: find out what software methods 0x1528, 0x1280 and (on nve4) 0x02dc
    * are supposed to do */
}

static void
nvc0_screen_fence_emit(struct pipe_context *pcontext, u32 *sequence,
                       struct nouveau_bo *wait)
{
   struct nvc0_context *nvc0 = nvc0_context(pcontext);
   struct nvc0_screen *screen = nvc0->screen;
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nouveau_pushbuf_refn ref = { wait, NOUVEAU_BO_GART | NOUVEAU_BO_RDWR };

   /* we need to do it after possible flush in MARK_RING */
   *sequence = ++screen->base.fence.sequence;

   assert(PUSH_AVAIL(push) + push->rsvd_kick >= 5);
   PUSH_DATA (push, NVC0_FIFO_PKHDR_SQ(NVC0_3D(QUERY_ADDRESS_HIGH), 4));
   PUSH_DATAh(push, screen->fence.bo->offset);
   PUSH_DATA (push, screen->fence.bo->offset);
   PUSH_DATA (push, *sequence);
   PUSH_DATA (push, NVC0_3D_QUERY_GET_FENCE | NVC0_3D_QUERY_GET_SHORT |
              (0xf << NVC0_3D_QUERY_GET_UNIT__SHIFT));

   nouveau_pushbuf_refn(push, &ref, 1);
}

static u32
nvc0_screen_fence_update(struct pipe_screen *pscreen)
{
   struct nvc0_screen *screen = nvc0_screen(pscreen);
   return screen->fence.map[0];
}

static int
nvc0_screen_init_compute(struct nvc0_screen *screen)
{
   const struct nouveau_mclass computes[] = {
      { AD102_COMPUTE_CLASS, -1 },
      { GA102_COMPUTE_CLASS, -1 },
      { TU102_COMPUTE_CLASS, -1 },
      { GV100_COMPUTE_CLASS, -1 },
      { GP104_COMPUTE_CLASS, -1 },
      { GP100_COMPUTE_CLASS, -1 },
      { GM200_COMPUTE_CLASS, -1 },
      { GM107_COMPUTE_CLASS, -1 },
      {  NVF0_COMPUTE_CLASS, -1 },
      {  NVE4_COMPUTE_CLASS, -1 },
      /* In theory, GF110+ should also support NVC8_COMPUTE_CLASS but,
       * in practice, a ILLEGAL_CLASS dmesg fail appears when using it. */
//      {  NVC8_COMPUTE_CLASS, -1 },
      {  NVC0_COMPUTE_CLASS, -1 },
      {}
   };
   struct nouveau_object *chan = screen->base.channel;
   int ret;

   ret = nouveau_object_mclass(chan, computes);
   if (ret < 0) {
      NOUVEAU_ERR("No supported compute class: %d\n", ret);
      return ret;
   }

   ret = nouveau_object_new(chan, 0xbeef00c0, computes[ret].oclass, NULL, 0, &screen->compute);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate compute class: %d\n", ret);
      return ret;
   }

   if (screen->compute->oclass < NVE4_COMPUTE_CLASS)
      return nvc0_screen_compute_setup(screen, screen->base.pushbuf);

   return nve4_screen_compute_setup(screen, screen->base.pushbuf);
}

static int
nvc0_screen_resize_tls_area(struct nvc0_screen *screen,
                            uint32_t lpos, uint32_t lneg, uint32_t cstack)
{
   struct nouveau_bo *bo = NULL;
   int ret;
   uint64_t size = (lpos + lneg) * 32 + cstack;

   if (size >= (1 << 20)) {
      NOUVEAU_ERR("requested TLS size too large: 0x%"PRIx64"\n", size);
      return -1;
   }

   size *= (screen->base.device->chipset >= 0xe0) ? 64 : 48; /* max warps */
   size  = align(size, 0x8000);
   size *= screen->mp_count;

   size = align(size, 1 << 17);

   ret = nouveau_bo_new(screen->base.device, NV_VRAM_DOMAIN(&screen->base), 1 << 17, size,
                        NULL, &bo);
   if (ret)
      return ret;

   /* Make sure that the pushbuf has acquired a reference to the old tls
    * segment, as it may have commands that will reference it.
    */
   if (screen->tls)
      PUSH_REF1(screen->base.pushbuf, screen->tls,
                NV_VRAM_DOMAIN(&screen->base) | NOUVEAU_BO_RDWR);
   nouveau_bo_ref(NULL, &screen->tls);
   screen->tls = bo;
   return 0;
}

int
nvc0_screen_resize_text_area(struct nvc0_screen *screen, struct nouveau_pushbuf *push,
                             uint64_t size)
{
   struct nouveau_bo *bo;
   int ret;

   ret = nouveau_bo_new(screen->base.device, NV_VRAM_DOMAIN(&screen->base),
                        1 << 17, size, NULL, &bo);
   if (ret)
      return ret;

   /* Make sure that the pushbuf has acquired a reference to the old text
    * segment, as it may have commands that will reference it.
    */
   if (screen->text)
      PUSH_REF1(screen->base.pushbuf, screen->text,
                NV_VRAM_DOMAIN(&screen->base) | NOUVEAU_BO_RD);
   nouveau_bo_ref(NULL, &screen->text);
   screen->text = bo;

   nouveau_heap_free(&screen->lib_code);
   nouveau_heap_destroy(&screen->text_heap);

   /*
    * Shader storage needs a 2K (from NVIDIA) overallocations at the end
    * to avoid prefetch bugs.
    */
   nouveau_heap_init(&screen->text_heap, 0, size - 0x800);

   /* update the code segment setup */
   if (screen->eng3d->oclass < GV100_3D_CLASS) {
      BEGIN_NVC0(push, NVC0_3D(CODE_ADDRESS_HIGH), 2);
      PUSH_DATAh(push, screen->text->offset);
      PUSH_DATA (push, screen->text->offset);
      if (screen->compute) {
         BEGIN_NVC0(push, NVC0_CP(CODE_ADDRESS_HIGH), 2);
         PUSH_DATAh(push, screen->text->offset);
         PUSH_DATA (push, screen->text->offset);
      }
   }

   return 0;
}

void
nvc0_screen_bind_cb_3d(struct nvc0_screen *screen, struct nouveau_pushbuf *push,
                       bool *can_serialize, int stage, int index, int size, uint64_t addr)
{
   assert(stage != 5);

   if (screen->base.class_3d >= GM107_3D_CLASS) {
      struct nvc0_cb_binding *binding = &screen->cb_bindings[stage][index];

      // TODO: Better figure out the conditions in which this is needed
      bool serialize = binding->addr == addr && binding->size != size;
      if (can_serialize)
         serialize = serialize && *can_serialize;
      if (serialize) {
         IMMED_NVC0(push, NVC0_3D(SERIALIZE), 0);
         if (can_serialize)
            *can_serialize = false;
      }

      binding->addr = addr;
      binding->size = size;
   }

   if (size >= 0) {
      BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 3);
      PUSH_DATA (push, size);
      PUSH_DATAh(push, addr);
      PUSH_DATA (push, addr);
   }
   IMMED_NVC0(push, NVC0_3D(CB_BIND(stage)), (index << 4) | (size >= 0));
}

#define FAIL_SCREEN_INIT(str, err)                    \
   do {                                               \
      NOUVEAU_ERR(str, err);                          \
      goto fail;                                      \
   } while(0)

struct nouveau_screen *
nvc0_screen_create(struct nouveau_device *dev)
{
   struct nvc0_screen *screen;
   struct pipe_screen *pscreen;
   struct nouveau_object *chan;

   struct nouveau_pushbuf *push;
   uint64_t value;
   uint32_t flags;
   int ret;
   unsigned i;

   switch (dev->chipset & ~0xf) {
   case 0xc0:
   case 0xd0:
   case 0xe0:
   case 0xf0:
   case 0x100:
   case 0x110:
   case 0x120:
   case 0x130:
   case 0x140:
   case 0x160:
   case 0x170:
   case 0x190:
      break;
   default:
      return NULL;
   }

   screen = CALLOC_STRUCT(nvc0_screen);
   if (!screen)
      return NULL;
   pscreen = &screen->base.base;
   pscreen->destroy = nvc0_screen_destroy;

   simple_mtx_init(&screen->state_lock, mtx_plain);

   ret = nouveau_screen_init(&screen->base, dev);
   if (ret)
      FAIL_SCREEN_INIT("Base screen init failed: %d\n", ret);
   chan = screen->base.channel;
   push = screen->base.pushbuf;
   push->rsvd_kick = 5;

   /* TODO: could this be higher on Kepler+? how does reclocking vs no
    * reclocking affect performance?
    * TODO: could this be higher on Fermi?
    */
   if (dev->chipset >= 0xe0)
      screen->base.transfer_pushbuf_threshold = 1024;

   screen->base.vidmem_bindings |= PIPE_BIND_CONSTANT_BUFFER |
      PIPE_BIND_SHADER_BUFFER |
      PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER |
      PIPE_BIND_COMMAND_ARGS_BUFFER | PIPE_BIND_QUERY_BUFFER;
   screen->base.sysmem_bindings |=
      PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER;

   if (screen->base.vram_domain & NOUVEAU_BO_GART) {
      screen->base.sysmem_bindings |= screen->base.vidmem_bindings;
      screen->base.vidmem_bindings = 0;
   }

   for (unsigned i = 0; i <= MESA_SHADER_COMPUTE; i++)
      pscreen->nir_options[i] = nv50_ir_nir_shader_compiler_options(screen->base.device->chipset, i);

   pscreen->context_create = nvc0_create;
   pscreen->is_format_supported = nvc0_screen_is_format_supported;
   pscreen->get_sample_pixel_grid = nvc0_screen_get_sample_pixel_grid;
   pscreen->get_driver_query_info = nvc0_screen_get_driver_query_info;
   pscreen->get_driver_query_group_info = nvc0_screen_get_driver_query_group_info;

   nvc0_screen_init_resource_functions(pscreen);

   screen->base.base.get_video_param = nouveau_vp3_screen_get_video_param;
   screen->base.base.is_video_format_supported = nouveau_vp3_screen_video_supported;

   flags = NOUVEAU_BO_GART | NOUVEAU_BO_MAP;
   if (screen->base.drm->version >= 0x01000202)
      flags |= NOUVEAU_BO_COHERENT;

   ret = nouveau_bo_new(dev, flags, 0, 4096, NULL, &screen->fence.bo);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating fence BO: %d\n", ret);
   BO_MAP(&screen->base, screen->fence.bo, 0, NULL);
   screen->fence.map = screen->fence.bo->map;
   screen->base.fence.emit = nvc0_screen_fence_emit;
   screen->base.fence.update = nvc0_screen_fence_update;

   if (dev->chipset < 0x140) {
      ret = nouveau_object_new(chan, (dev->chipset < 0xe0) ? 0x1f906e : 0x906e,
                               NVIF_CLASS_SW_GF100, NULL, 0, &screen->nvsw);
      if (ret)
         FAIL_SCREEN_INIT("Error creating SW object: %d\n", ret);

      BEGIN_NVC0(push, SUBC_SW(NV01_SUBCHAN_OBJECT), 1);
      PUSH_DATA (push, screen->nvsw->handle);
   }

   const struct nouveau_mclass m2mfs[] = {
      { NVF0_P2MF_CLASS, -1 },
      { NVE4_P2MF_CLASS, -1 },
      { NVC0_M2MF_CLASS, -1 },
      {}
   };

   ret = nouveau_object_mclass(chan, m2mfs);
   if (ret < 0)
      FAIL_SCREEN_INIT("No supported m2mf class: %d\n", ret);

   ret = nouveau_object_new(chan, 0xbeef323f, m2mfs[ret].oclass, NULL, 0,
                            &screen->m2mf);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating PGRAPH context for M2MF: %d\n", ret);

   BEGIN_NVC0(push, SUBC_M2MF(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, screen->m2mf->oclass);

   if (screen->m2mf->oclass >= NVE4_P2MF_CLASS) {
      const struct nouveau_mclass copys[] = {
         {  AMPERE_DMA_COPY_B, -1 },
         {  AMPERE_DMA_COPY_A, -1 },
         {  TURING_DMA_COPY_A, -1 },
         {   VOLTA_DMA_COPY_A, -1 },
         {  PASCAL_DMA_COPY_B, -1 },
         {  PASCAL_DMA_COPY_A, -1 },
         { MAXWELL_DMA_COPY_A, -1 },
         {  KEPLER_DMA_COPY_A, -1 },
         {}
      };

      ret = nouveau_object_mclass(chan, copys);
      if (ret < 0)
         FAIL_SCREEN_INIT("No supported copy engine class: %d\n", ret);

      ret = nouveau_object_new(chan, 0, copys[ret].oclass, NULL, 0, &screen->copy);
      if (ret)
         FAIL_SCREEN_INIT("Error allocating copy engine class: %d\n", ret);

      BEGIN_NVC0(push, SUBC_COPY(NV01_SUBCHAN_OBJECT), 1);
      PUSH_DATA (push, screen->copy->oclass);
   }

   ret = nouveau_object_new(chan, 0xbeef902d, NVC0_2D_CLASS, NULL, 0,
                            &screen->eng2d);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating PGRAPH context for 2D: %d\n", ret);

   BEGIN_NVC0(push, SUBC_2D(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, screen->eng2d->oclass);
   BEGIN_NVC0(push, SUBC_2D(NVC0_2D_SINGLE_GPC), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_2D(OPERATION), 1);
   PUSH_DATA (push, NV50_2D_OPERATION_SRCCOPY);
   BEGIN_NVC0(push, NVC0_2D(CLIP_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_2D(COLOR_KEY_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_2D(SET_PIXELS_FROM_MEMORY_CORRAL_SIZE), 1);
   PUSH_DATA (push, 0x3f);
   BEGIN_NVC0(push, NVC0_2D(SET_PIXELS_FROM_MEMORY_SAFE_OVERLAP), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_2D(COND_MODE), 1);
   PUSH_DATA (push, NV50_2D_COND_MODE_ALWAYS);

   BEGIN_NVC0(push, SUBC_2D(NVC0_GRAPH_NOTIFY_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, screen->fence.bo->offset + 16);
   PUSH_DATA (push, screen->fence.bo->offset + 16);

   const struct nouveau_mclass threeds[] = {
      { AD102_3D_CLASS, -1 },
      { GA102_3D_CLASS, -1 },
      { TU102_3D_CLASS, -1 },
      { GV100_3D_CLASS, -1 },
      { GP102_3D_CLASS, -1 },
      { GP100_3D_CLASS, -1 },
      { GM200_3D_CLASS, -1 },
      { GM107_3D_CLASS, -1 },
      {  NVF0_3D_CLASS, -1 },
      {  NVEA_3D_CLASS, -1 },
      {  NVE4_3D_CLASS, -1 },
      {  NVC8_3D_CLASS, -1 },
      {  NVC1_3D_CLASS, -1 },
      {  NVC0_3D_CLASS, -1 },
      {}
   };

   ret = nouveau_object_mclass(chan, threeds);
   if (ret < 0)
      FAIL_SCREEN_INIT("No supported 3d class: %d\n", ret);

   ret = nouveau_object_new(chan, 0xbeef003d, threeds[ret].oclass, NULL, 0,
                            &screen->eng3d);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating PGRAPH context for 3D: %d\n", ret);
   screen->base.class_3d = screen->eng3d->oclass;

   BEGIN_NVC0(push, SUBC_3D(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, screen->eng3d->oclass);

   BEGIN_NVC0(push, NVC0_3D(COND_MODE), 1);
   PUSH_DATA (push, NVC0_3D_COND_MODE_ALWAYS);

   if (debug_get_bool_option("NOUVEAU_SHADER_WATCHDOG", true)) {
      /* kill shaders after about 1 second (at 100 MHz) */
      BEGIN_NVC0(push, NVC0_3D(WATCHDOG_TIMER), 1);
      PUSH_DATA (push, 0x17);
   }

   IMMED_NVC0(push, NVC0_3D(ZETA_COMP_ENABLE),
                    screen->base.drm->version >= 0x01000101);
   BEGIN_NVC0(push, NVC0_3D(RT_COMP_ENABLE(0)), 8);
   for (i = 0; i < 8; ++i)
      PUSH_DATA(push, screen->base.drm->version >= 0x01000101);

   BEGIN_NVC0(push, NVC0_3D(RT_CONTROL), 1);
   PUSH_DATA (push, 1);

   BEGIN_NVC0(push, NVC0_3D(CSAA_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(MULTISAMPLE_ENABLE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(MULTISAMPLE_MODE), 1);
   PUSH_DATA (push, NVC0_3D_MULTISAMPLE_MODE_MS1);
   BEGIN_NVC0(push, NVC0_3D(MULTISAMPLE_CTRL), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(LINE_WIDTH_SEPARATE), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(PRIM_RESTART_WITH_DRAW_ARRAYS), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(BLEND_SEPARATE_ALPHA), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(BLEND_ENABLE_COMMON), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(SHADE_MODEL), 1);
   PUSH_DATA (push, NVC0_3D_SHADE_MODEL_SMOOTH);
   if (screen->eng3d->oclass < NVE4_3D_CLASS) {
      IMMED_NVC0(push, NVC0_3D(TEX_MISC), 0);
   } else if (screen->eng3d->oclass < GA102_3D_CLASS) {
      BEGIN_NVC0(push, NVE4_3D(TEX_CB_INDEX), 1);
      PUSH_DATA (push, 15);
   }
   BEGIN_NVC0(push, NVC0_3D(CALL_LIMIT_LOG), 1);
   PUSH_DATA (push, 8); /* 128 */
   BEGIN_NVC0(push, NVC0_3D(ZCULL_STATCTRS_ENABLE), 1);
   PUSH_DATA (push, 1);
   if (screen->eng3d->oclass >= NVC1_3D_CLASS) {
      BEGIN_NVC0(push, NVC0_3D(CACHE_SPLIT), 1);
      PUSH_DATA (push, NVC0_3D_CACHE_SPLIT_48K_SHARED_16K_L1);
   }

   nvc0_magic_3d_init(push, screen->eng3d->oclass);

   ret = nvc0_screen_resize_text_area(screen, push, 1 << 19);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating TEXT area: %d\n", ret);

   /* 6 user uniform areas, 6 driver areas, and 1 for the runout */
   ret = nouveau_bo_new(dev, NV_VRAM_DOMAIN(&screen->base), 1 << 12, 13 << 16, NULL,
                        &screen->uniform_bo);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating uniform BO: %d\n", ret);

   PUSH_REF1 (push, screen->uniform_bo, NV_VRAM_DOMAIN(&screen->base) | NOUVEAU_BO_WR);

   /* return { 0.0, 0.0, 0.0, 0.0 } for out-of-bounds vtxbuf access */
   BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 3);
   PUSH_DATA (push, 256);
   PUSH_DATAh(push, screen->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);
   PUSH_DATA (push, screen->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);
   BEGIN_1IC0(push, NVC0_3D(CB_POS), 5);
   PUSH_DATA (push, 0);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   PUSH_DATAf(push, 0.0f);
   BEGIN_NVC0(push, NVC0_3D(VERTEX_RUNOUT_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, screen->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);
   PUSH_DATA (push, screen->uniform_bo->offset + NVC0_CB_AUX_RUNOUT_INFO);

   if (screen->base.drm->version >= 0x01000101) {
      ret = nouveau_getparam(dev, NOUVEAU_GETPARAM_GRAPH_UNITS, &value);
      if (ret)
         FAIL_SCREEN_INIT("NOUVEAU_GETPARAM_GRAPH_UNITS failed: %d\n", ret);
   } else {
      if (dev->chipset >= 0xe0 && dev->chipset < 0xf0)
         value = (8 << 8) | 4;
      else
         value = (16 << 8) | 4;
   }
   screen->gpc_count = value & 0x000000ff;
   screen->mp_count = value >> 8;
   screen->mp_count_compute = screen->mp_count;

   ret = nvc0_screen_resize_tls_area(screen, 128 * 16, 0, 0x200);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating TLS area: %d\n", ret);

   BEGIN_NVC0(push, NVC0_3D(TEMP_ADDRESS_HIGH), 4);
   PUSH_DATAh(push, screen->tls->offset);
   PUSH_DATA (push, screen->tls->offset);
   PUSH_DATA (push, screen->tls->size >> 32);
   PUSH_DATA (push, screen->tls->size);
   BEGIN_NVC0(push, NVC0_3D(WARP_TEMP_ALLOC), 1);
   PUSH_DATA (push, 0);
   /* Reduce likelihood of collision with real buffers by placing the hole at
    * the top of the 4G area. This will have to be dealt with for real
    * eventually by blocking off that area from the VM.
    */
   BEGIN_NVC0(push, NVC0_3D(LOCAL_BASE), 1);
   PUSH_DATA (push, 0xff << 24);

   if (screen->eng3d->oclass < GM107_3D_CLASS) {
      ret = nouveau_bo_new(dev, NV_VRAM_DOMAIN(&screen->base), 1 << 17, 1 << 20, NULL,
                           &screen->poly_cache);
      if (ret)
         FAIL_SCREEN_INIT("Error allocating poly cache BO: %d\n", ret);

      BEGIN_NVC0(push, NVC0_3D(VERTEX_QUARANTINE_ADDRESS_HIGH), 3);
      PUSH_DATAh(push, screen->poly_cache->offset);
      PUSH_DATA (push, screen->poly_cache->offset);
      PUSH_DATA (push, 3);
   }

   ret = nouveau_bo_new(dev, NV_VRAM_DOMAIN(&screen->base), 1 << 17, 1 << 17, NULL,
                        &screen->txc);
   if (ret)
      FAIL_SCREEN_INIT("Error allocating txc BO: %d\n", ret);

   BEGIN_NVC0(push, NVC0_3D(TIC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, screen->txc->offset);
   PUSH_DATA (push, screen->txc->offset);
   PUSH_DATA (push, NVC0_TIC_MAX_ENTRIES - 1);
   if (screen->eng3d->oclass >= GM107_3D_CLASS) {
      screen->tic.maxwell = true;
      if (screen->eng3d->oclass == GM107_3D_CLASS) {
         screen->tic.maxwell =
            debug_get_bool_option("NOUVEAU_MAXWELL_TIC", true);
         IMMED_NVC0(push, SUBC_3D(0x0f10), screen->tic.maxwell);
      }
   }

   BEGIN_NVC0(push, NVC0_3D(TSC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, screen->txc->offset + 65536);
   PUSH_DATA (push, screen->txc->offset + 65536);
   PUSH_DATA (push, NVC0_TSC_MAX_ENTRIES - 1);

   BEGIN_NVC0(push, NVC0_3D(SCREEN_Y_CONTROL), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(WINDOW_OFFSET_X), 2);
   PUSH_DATA (push, 0);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(ZCULL_REGION), 1); /* deactivate ZCULL */
   PUSH_DATA (push, 0x3f);

   BEGIN_NVC0(push, NVC0_3D(CLIP_RECTS_MODE), 1);
   PUSH_DATA (push, NVC0_3D_CLIP_RECTS_MODE_INSIDE_ANY);
   BEGIN_NVC0(push, NVC0_3D(CLIP_RECT_HORIZ(0)), 8 * 2);
   for (i = 0; i < 8 * 2; ++i)
      PUSH_DATA(push, 0);
   BEGIN_NVC0(push, NVC0_3D(CLIP_RECTS_EN), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(CLIPID_ENABLE), 1);
   PUSH_DATA (push, 0);

   /* neither scissors, viewport nor stencil mask should affect clears */
   BEGIN_NVC0(push, NVC0_3D(CLEAR_FLAGS), 1);
   PUSH_DATA (push, 0);

   BEGIN_NVC0(push, NVC0_3D(VIEWPORT_TRANSFORM_EN), 1);
   PUSH_DATA (push, 1);
   for (i = 0; i < NVC0_MAX_VIEWPORTS; i++) {
      BEGIN_NVC0(push, NVC0_3D(DEPTH_RANGE_NEAR(i)), 2);
      PUSH_DATAf(push, 0.0f);
      PUSH_DATAf(push, 1.0f);
   }
   BEGIN_NVC0(push, NVC0_3D(VIEW_VOLUME_CLIP_CTRL), 1);
   PUSH_DATA (push, NVC0_3D_VIEW_VOLUME_CLIP_CTRL_UNK1_UNK1);

   /* We use scissors instead of exact view volume clipping,
    * so they're always enabled.
    */
   for (i = 0; i < NVC0_MAX_VIEWPORTS; i++) {
      BEGIN_NVC0(push, NVC0_3D(SCISSOR_ENABLE(i)), 3);
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 16384 << 16);
      PUSH_DATA (push, 16384 << 16);
   }

   if (screen->eng3d->oclass < TU102_3D_CLASS) {
#define MK_MACRO(m, n) i = nvc0_graph_set_macro(screen, m, i, sizeof(n), n);

      i = 0;
      MK_MACRO(NVC0_3D_MACRO_VERTEX_ARRAY_PER_INSTANCE, mme9097_per_instance_bf);
      MK_MACRO(NVC0_3D_MACRO_BLEND_ENABLES, mme9097_blend_enables);
      MK_MACRO(NVC0_3D_MACRO_VERTEX_ARRAY_SELECT, mme9097_vertex_array_select);
      MK_MACRO(NVC0_3D_MACRO_TEP_SELECT, mme9097_tep_select);
      MK_MACRO(NVC0_3D_MACRO_GP_SELECT, mme9097_gp_select);
      MK_MACRO(NVC0_3D_MACRO_POLYGON_MODE_FRONT, mme9097_poly_mode_front);
      MK_MACRO(NVC0_3D_MACRO_POLYGON_MODE_BACK, mme9097_poly_mode_back);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT, mme9097_draw_arrays_indirect);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT, mme9097_draw_elts_indirect);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT_COUNT, mme9097_draw_arrays_indirect_count);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT_COUNT, mme9097_draw_elts_indirect_count);
      MK_MACRO(NVC0_3D_MACRO_QUERY_BUFFER_WRITE, mme9097_query_buffer_write);
      MK_MACRO(NVC0_3D_MACRO_CONSERVATIVE_RASTER_STATE, mme9097_conservative_raster_state);
      MK_MACRO(NVC0_3D_MACRO_SET_PRIV_REG, mme9097_set_priv_reg);
      MK_MACRO(NVC0_3D_MACRO_COMPUTE_COUNTER, mme9097_compute_counter);
      MK_MACRO(NVC0_3D_MACRO_COMPUTE_COUNTER_TO_QUERY, mme9097_compute_counter_to_query);
      MK_MACRO(NVC0_CP_MACRO_LAUNCH_GRID_INDIRECT, mme90c0_launch_grid_indirect);
   } else {
#undef MK_MACRO
#define MK_MACRO(m, n) i = tu102_graph_set_macro(screen, m, i, sizeof(n), n);

      i = 0;
      MK_MACRO(NVC0_3D_MACRO_VERTEX_ARRAY_PER_INSTANCE, mmec597_per_instance_bf);
      MK_MACRO(NVC0_3D_MACRO_BLEND_ENABLES, mmec597_blend_enables);
      MK_MACRO(NVC0_3D_MACRO_VERTEX_ARRAY_SELECT, mmec597_vertex_array_select);
      MK_MACRO(NVC0_3D_MACRO_TEP_SELECT, mmec597_tep_select);
      MK_MACRO(NVC0_3D_MACRO_GP_SELECT, mmec597_gp_select);
      MK_MACRO(NVC0_3D_MACRO_POLYGON_MODE_FRONT, mmec597_poly_mode_front);
      MK_MACRO(NVC0_3D_MACRO_POLYGON_MODE_BACK, mmec597_poly_mode_back);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT, mmec597_draw_arrays_indirect);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT, mmec597_draw_elts_indirect);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT_COUNT, mmec597_draw_arrays_indirect_count);
      MK_MACRO(NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT_COUNT, mmec597_draw_elts_indirect_count);
      MK_MACRO(NVC0_3D_MACRO_QUERY_BUFFER_WRITE, mmec597_query_buffer_write);
      MK_MACRO(NVC0_3D_MACRO_CONSERVATIVE_RASTER_STATE, mmec597_conservative_raster_state);
      MK_MACRO(NVC0_3D_MACRO_SET_PRIV_REG, mmec597_set_priv_reg);
      MK_MACRO(NVC0_3D_MACRO_COMPUTE_COUNTER, mmec597_compute_counter);
      MK_MACRO(NVC0_3D_MACRO_COMPUTE_COUNTER_TO_QUERY, mmec597_compute_counter_to_query);
   }

   BEGIN_NVC0(push, NVC0_3D(RASTERIZE_ENABLE), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(RT_SEPARATE_FRAG_DATA), 1);
   PUSH_DATA (push, 1);
   BEGIN_NVC0(push, NVC0_3D(MACRO_GP_SELECT), 1);
   PUSH_DATA (push, 0x40);
   BEGIN_NVC0(push, NVC0_3D(LAYER), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(MACRO_TEP_SELECT), 1);
   PUSH_DATA (push, 0x30);
   BEGIN_NVC0(push, NVC0_3D(PATCH_VERTICES), 1);
   PUSH_DATA (push, 3);
   BEGIN_NVC0(push, NVC0_3D(SP_SELECT(2)), 1);
   PUSH_DATA (push, 0x20);
   BEGIN_NVC0(push, NVC0_3D(SP_SELECT(0)), 1);
   PUSH_DATA (push, 0x00);
   screen->save_state.patch_vertices = 3;

   BEGIN_NVC0(push, NVC0_3D(POINT_COORD_REPLACE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_3D(POINT_RASTER_RULES), 1);
   PUSH_DATA (push, NVC0_3D_POINT_RASTER_RULES_OGL);

   IMMED_NVC0(push, NVC0_3D(EDGEFLAG), 1);

   if (nvc0_screen_init_compute(screen))
      goto fail;

   /* XXX: Compute and 3D are somehow aliased on Fermi. */
   for (i = 0; i < 5; ++i) {
      unsigned j = 0;
      for (j = 0; j < 16; j++)
         screen->cb_bindings[i][j].size = -1;

      /* TIC and TSC entries for each unit (nve4+ only) */
      /* auxiliary constants (6 user clip planes, base instance id) */
      nvc0_screen_bind_cb_3d(screen, push, NULL, i, 15, NVC0_CB_AUX_SIZE,
                             screen->uniform_bo->offset + NVC0_CB_AUX_INFO(i));
      if (screen->eng3d->oclass >= NVE4_3D_CLASS) {
         unsigned j;
         BEGIN_1IC0(push, NVC0_3D(CB_POS), 9);
         PUSH_DATA (push, NVC0_CB_AUX_UNK_INFO);
         for (j = 0; j < 8; ++j)
            PUSH_DATA(push, j);
      } else {
         BEGIN_NVC0(push, NVC0_3D(TEX_LIMITS(i)), 1);
         PUSH_DATA (push, 0x54);
      }

      /* MS sample coordinate offsets: these do not work with _ALT modes ! */
      BEGIN_1IC0(push, NVC0_3D(CB_POS), 1 + 2 * 8);
      PUSH_DATA (push, NVC0_CB_AUX_MS_INFO);
      PUSH_DATA (push, 0); /* 0 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 1); /* 1 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 0); /* 2 */
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 1); /* 3 */
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 2); /* 4 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 3); /* 5 */
      PUSH_DATA (push, 0);
      PUSH_DATA (push, 2); /* 6 */
      PUSH_DATA (push, 1);
      PUSH_DATA (push, 3); /* 7 */
      PUSH_DATA (push, 1);
   }
   BEGIN_NVC0(push, NVC0_3D(LINKED_TSC), 1);
   PUSH_DATA (push, 0);

   /* requires Nvidia provided firmware */
   if (screen->eng3d->oclass >= GM200_3D_CLASS) {
      unsigned reg = screen->eng3d->oclass >= GV100_3D_CLASS ? 0x419ba4 : 0x419f78;
      BEGIN_1IC0(push, NVC0_3D(MACRO_SET_PRIV_REG), 3);
      PUSH_DATA (push, reg);
      PUSH_DATA (push, 0x00000000);
      PUSH_DATA (push, 0x00000008);
   }

   PUSH_KICK (push);

   screen->tic.entries = CALLOC(
         NVC0_TIC_MAX_ENTRIES + NVC0_TSC_MAX_ENTRIES + NVE4_IMG_MAX_HANDLES,
         sizeof(void *));
   screen->tsc.entries = screen->tic.entries + NVC0_TIC_MAX_ENTRIES;
   screen->img.entries = (void *)(screen->tsc.entries + NVC0_TSC_MAX_ENTRIES);

   if (!nvc0_blitter_create(screen))
      goto fail;

   nouveau_device_set_classes_for_debug(dev,
                                        screen->eng3d->oclass,
                                        screen->compute->oclass,
                                        screen->m2mf->oclass,
                                        screen->copy ? screen->copy->oclass : 0);

   nvc0_init_shader_caps(screen);
   nvc0_init_compute_caps(screen);
   nvc0_init_screen_caps(screen);

   return &screen->base;

fail:
   screen->base.base.context_create = NULL;
   return &screen->base;
}

int
nvc0_screen_tic_alloc(struct nvc0_screen *screen, void *entry)
{
   int i = screen->tic.next;

   while (screen->tic.lock[i / 32] & (1 << (i % 32)))
      i = (i + 1) & (NVC0_TIC_MAX_ENTRIES - 1);

   screen->tic.next = (i + 1) & (NVC0_TIC_MAX_ENTRIES - 1);

   if (screen->tic.entries[i])
      nv50_tic_entry(screen->tic.entries[i])->id = -1;

   screen->tic.entries[i] = entry;
   return i;
}

int
nvc0_screen_tsc_alloc(struct nvc0_screen *screen, void *entry)
{
   int i = screen->tsc.next;

   while (screen->tsc.lock[i / 32] & (1 << (i % 32)))
      i = (i + 1) & (NVC0_TSC_MAX_ENTRIES - 1);

   screen->tsc.next = (i + 1) & (NVC0_TSC_MAX_ENTRIES - 1);

   if (screen->tsc.entries[i])
      nv50_tsc_entry(screen->tsc.entries[i])->id = -1;

   screen->tsc.entries[i] = entry;
   return i;
}
