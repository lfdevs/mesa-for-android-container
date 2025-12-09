/*
 * Copyright 2012 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 *
 */

#include <xf86drm.h>
#include "drm-uapi/nouveau_drm.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_screen.h"

#include "nv_object.xml.h"
#include "nv_m2mf.xml.h"
#include "nv30/nv30-40_3d.xml.h"
#include "nv30/nv01_2d.xml.h"

#include "nouveau_fence.h"
#include "nv30/nv30_screen.h"
#include "nv30/nv30_context.h"
#include "nv30/nv30_resource.h"
#include "nv30/nv30_format.h"
#include "nv30/nv30_winsys.h"

#define RANKINE_0397_CHIPSET 0x00000003
#define RANKINE_0497_CHIPSET 0x000001e0
#define RANKINE_0697_CHIPSET 0x00000010
#define CURIE_4097_CHIPSET   0x00000baf
#define CURIE_4497_CHIPSET   0x00005450
#define CURIE_4497_CHIPSET6X 0x00000088

static void
nv30_init_shader_caps(struct nv30_screen *screen)
{
   struct nouveau_object *eng3d = screen->eng3d;

   struct pipe_shader_caps *caps =
      (struct pipe_shader_caps *)&screen->base.base.shader_caps[MESA_SHADER_VERTEX];

   caps->max_instructions =
   caps->max_alu_instructions = (eng3d->oclass >= NV40_3D_CLASS) ? 512 : 256;
   caps->max_tex_instructions =
   caps->max_tex_indirections = (eng3d->oclass >= NV40_3D_CLASS) ? 512 : 0;
   caps->max_inputs =
   caps->max_outputs = 16;
   caps->max_const_buffer0_size =
      ((eng3d->oclass >= NV40_3D_CLASS) ? (468 - 6): (256 - 6)) * sizeof(float[4]);
   caps->max_const_buffers = 1;
   caps->max_temps = (eng3d->oclass >= NV40_3D_CLASS) ? 32 : 13;
   caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);

   caps = (struct pipe_shader_caps *)&screen->base.base.shader_caps[MESA_SHADER_FRAGMENT];

   caps->max_instructions =
   caps->max_alu_instructions =
   caps->max_tex_instructions =
   caps->max_tex_indirections = 4096;
   caps->max_inputs = 8; /* should be possible to do 10 with nv4x */
   caps->max_outputs = 4;
   caps->max_const_buffer0_size =
      ((eng3d->oclass >= NV40_3D_CLASS) ? 224 : 32) * sizeof(float[4]);
   caps->max_const_buffers = 1;
   caps->max_temps = 32;
   caps->max_texture_samplers =
   caps->max_sampler_views = 16;
   caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);
}

static void
nv30_init_screen_caps(struct nv30_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.base.caps;

   u_init_pipe_screen_caps(&screen->base.base, 1);

   struct nouveau_object *eng3d = screen->eng3d;
   struct nouveau_device *dev = screen->base.device;

   /* non-boolean capabilities */
   caps->max_render_targets = (eng3d->oclass >= NV40_3D_CLASS) ? 4 : 1;
   caps->max_texture_2d_size = 4096;
   caps->max_texture_3d_levels = 10;
   caps->max_texture_cube_levels = 13;
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 120;
   caps->endianness = PIPE_ENDIAN_LITTLE;
   caps->constant_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = NOUVEAU_MIN_BUFFER_MAP_ALIGN;
   caps->max_viewports = 1;
   caps->max_vertex_attrib_stride = 2048;
   caps->max_texture_upload_memory_budget = 8 * 1024 * 1024;
   caps->max_varyings = 8;

   /* supported capabilities */
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->query_time_elapsed = true;
   caps->query_timestamp = true;
   caps->texture_swizzle = true;
   caps->depth_clip_disable = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_origin_lower_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->tgsi_texcoord = true;
   caps->buffer_map_persistent_coherent = true;
   caps->clear_scissored = true;
   caps->allow_mapped_buffers_during_execution = true;
   caps->query_memory_info = true;
   caps->vertex_input_alignment = PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE;
   caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;
   /* nv35 capabilities */
   caps->depth_bounds_test =
      eng3d->oclass == NV35_3D_CLASS || eng3d->oclass >= NV40_3D_CLASS;
   caps->supported_prim_modes_with_restart =
   caps->supported_prim_modes = BITFIELD_MASK(MESA_PRIM_COUNT);
   /* nv4x capabilities */
   caps->blend_equation_separate =
   caps->npot_textures =
   caps->conditional_render =
   caps->texture_mirror_clamp =
   caps->texture_mirror_clamp_to_edge =
   caps->primitive_restart =
   caps->primitive_restart_fixed_index = eng3d->oclass >= NV40_3D_CLASS;
   /* unsupported */
   caps->emulate_nonfixed_primitive_restart = false;
   caps->depth_clip_disable_separate = false;
   caps->max_dual_source_render_targets = 0;
   caps->fragment_shader_texture_lod = false;
   caps->fragment_shader_derivatives = false;
   caps->indep_blend_enable = false;
   caps->indep_blend_func = false;
   caps->max_texture_array_layers = 0;
   caps->shader_stencil_export = false;
   caps->vs_instanceid = false;
   caps->vertex_element_instance_divisor = false; /* xxx = yes? */
   caps->max_stream_output_buffers = 0;
   caps->stream_output_pause_resume = false;
   caps->stream_output_interleave_buffers = false;
   caps->min_texel_offset = 0;
   caps->max_texel_offset = 0;
   caps->min_texture_gather_offset = 0;
   caps->max_texture_gather_offset = 0;
   caps->max_stream_output_separate_components = 0;
   caps->max_stream_output_interleaved_components = 0;
   caps->max_geometry_output_vertices = 0;
   caps->max_geometry_total_output_components = 0;
   caps->max_vertex_streams = 0;
   caps->tgsi_can_compact_constants = false;
   caps->texture_barrier = false;
   caps->seamless_cube_map = false;
   caps->seamless_cube_map_per_texture = false;
   caps->cube_map_array = false;
   caps->vertex_color_unclamped = false;
   caps->fragment_color_clamped = false;
   caps->vertex_color_clamped = false;
   caps->quads_follow_provoking_vertex_convention = false;
   caps->mixed_colorbuffer_formats = false;
   caps->start_instance = false;
   caps->texture_multisample = false;
   caps->texture_buffer_objects = false;
   caps->texture_buffer_offset_alignment = 0;
   caps->query_pipeline_statistics = false;
   caps->texture_border_color_quirk = false;
   caps->max_texel_buffer_elements = 0;
   caps->mixed_framebuffer_sizes = false;
   caps->vs_layer_viewport = false;
   caps->max_texture_gather_components = 0;
   caps->texture_gather_sm5 = false;
   caps->fake_sw_msaa = false;
   caps->texture_query_lod = false;
   caps->sample_shading = false;
   caps->texture_gather_offsets = false;
   caps->vs_window_space_position = false;
   caps->user_vertex_buffers = false;
   caps->compute = false;
   caps->draw_indirect = false;
   caps->multi_draw_indirect = false;
   caps->multi_draw_indirect_params = false;
   caps->fs_fine_derivative = false;
   caps->conditional_render_inverted = false;
   caps->sampler_view_target = false;
   caps->clip_halfz = false;
   caps->polygon_offset_clamp = false;
   caps->multisample_z_resolve = false;
   caps->resource_from_user_memory = false;
   caps->device_reset_status_query = false;
   caps->max_shader_patch_varyings = 0;
   caps->texture_float_linear = false;
   caps->texture_half_float_linear = false;
   caps->texture_query_samples = false;
   caps->force_persample_interp = false;
   caps->copy_between_compressed_and_plain_formats = false;
   caps->shareable_shaders = false;
   caps->draw_parameters = false;
   caps->shader_pack_half_float = false;
   caps->fs_position_is_sysval = false;
   caps->fs_face_is_integer_sysval = false;
   caps->shader_buffer_offset_alignment = 0;
   caps->invalidate_buffer = false;
   caps->generate_mipmap = false;
   caps->string_marker = false;
   caps->buffer_sampler_view_rgba_only = false;
   caps->surface_reinterpret_blocks = false;
   caps->compressed_surface_reinterpret_blocks_layered = false;
   caps->query_buffer_object = false;
   caps->framebuffer_no_attachment = false;
   caps->robust_buffer_access_behavior = false;
   caps->cull_distance = false;
   caps->shader_group_vote = false;
   caps->max_window_rectangles = 0;
   caps->viewport_subpixel_bits = 0;
   caps->mixed_color_depth_bits = 0;
   caps->shader_array_components = false;
   caps->native_fence_fd = false;
   caps->fbfetch = 0;
   caps->legacy_math_rules = false;
   caps->doubles = false;
   caps->int64 = false;
   caps->tgsi_tex_txf_lz = false;
   caps->shader_clock = false;
   caps->polygon_mode_fill_rectangle = false;
   caps->sparse_buffer_page_size = 0;
   caps->shader_ballot = false;
   caps->tes_layer_viewport = false;
   caps->can_bind_const_buffer_as_vertex = false;
   caps->post_depth_coverage = false;
   caps->bindless_texture = false;
   caps->nir_samplers_as_deref = false;
   caps->query_so_overflow = false;
   caps->memobj = false;
   caps->load_constbuf = false;
   caps->tile_raster_order = false;
   caps->max_combined_shader_output_resources = 0;
   caps->framebuffer_msaa_constraints = false;
   caps->signed_vertex_buffer_offset = false;
   caps->context_priority_mask = 0;
   caps->fence_signal = false;
   caps->constbuf0_flags = 0;
   caps->packed_uniforms = false;
   caps->conservative_raster_post_snap_triangles = false;
   caps->conservative_raster_post_snap_points_lines = false;
   caps->conservative_raster_pre_snap_triangles = false;
   caps->conservative_raster_pre_snap_points_lines = false;
   caps->conservative_raster_post_depth_coverage = false;
   caps->max_conservative_raster_subpixel_precision_bias = false;
   caps->programmable_sample_locations = false;
   caps->image_load_formatted = false;
   caps->image_atomic_inc_wrap = false;
   caps->image_store_formatted = false;

   caps->pci_group = dev->info.pci.domain;
   caps->pci_bus = dev->info.pci.bus;
   caps->pci_device = dev->info.pci.dev;
   caps->pci_function = dev->info.pci.func;

   caps->max_gs_invocations = 32;
   caps->max_shader_buffer_size = 1 << 27;
   caps->vendor_id = 0x10de;
   caps->device_id = dev->info.device_id;
   caps->video_memory = dev->vram_size >> 20;
   caps->uma = false;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;

   caps->max_line_width =
   caps->max_line_width_aa = 10.0;

   caps->max_point_size =
   caps->max_point_size_aa = 64.0;

   caps->max_texture_anisotropy = (eng3d->oclass >= NV40_3D_CLASS) ? 16.0 : 8.0;
   caps->max_texture_lod_bias = 15.0;
}

static bool
nv30_screen_is_format_supported(struct pipe_screen *pscreen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned bindings)
{
   if (sample_count > nv30_screen(pscreen)->max_sample_count)
      return false;

   if (!(0x00000017 & (1 << sample_count)))
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   /* No way to render to a swizzled 3d texture. We don't necessarily know if
    * it's swizzled or not here, but we have to assume anyways.
    */
   if (target == PIPE_TEXTURE_3D && (bindings & PIPE_BIND_RENDER_TARGET))
      return false;

   /* shared is always supported */
   bindings &= ~PIPE_BIND_SHARED;

   if (bindings & PIPE_BIND_INDEX_BUFFER) {
      if (format != PIPE_FORMAT_R8_UINT &&
          format != PIPE_FORMAT_R16_UINT &&
          format != PIPE_FORMAT_R32_UINT)
         return false;
      bindings &= ~PIPE_BIND_INDEX_BUFFER;
   }

   return (nv30_format_info(pscreen, format)->bindings & bindings) == bindings;
}

static const nir_shader_compiler_options nv30_base_compiler_options = {
   .fuse_ffma32 = true,
   .fuse_ffma64 = true,
   .lower_bitops = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_fdiv = true,
   .lower_fsat = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_fdph = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_fmod = true,
   .lower_fpow = true, /* In hardware as of nv40 FS */
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .force_indirect_unrolling = nir_var_all,
   .force_indirect_unrolling_sampler = true,
   .max_unroll_iterations = 32,
   .no_integers = true,
};

static void
nv30_screen_fence_emit(struct pipe_context *pcontext, uint32_t *sequence,
                       struct nouveau_bo *wait)
{
   struct nv30_context *nv30 = nv30_context(pcontext);
   struct nv30_screen *screen = nv30->screen;
   struct nouveau_pushbuf *push = nv30->base.pushbuf;
   struct nouveau_pushbuf_refn ref = { wait, NOUVEAU_BO_GART | NOUVEAU_BO_RDWR };

   *sequence = ++screen->base.fence.sequence;

   assert(PUSH_AVAIL(push) + push->rsvd_kick >= 3);
   PUSH_DATA (push, NV30_3D_FENCE_OFFSET |
              (2 /* size */ << 18) | (7 /* subchan */ << 13));
   PUSH_DATA (push, 0);
   PUSH_DATA (push, *sequence);

   nouveau_pushbuf_refn(push, &ref, 1);
}

static uint32_t
nv30_screen_fence_update(struct pipe_screen *pscreen)
{
   struct nv30_screen *screen = nv30_screen(pscreen);
   struct nv04_notify *fence = screen->fence->data;
   return *(uint32_t *)((char *)screen->notify->map + fence->offset);
}

static void
nv30_screen_destroy(struct pipe_screen *pscreen)
{
   struct nv30_screen *screen = nv30_screen(pscreen);

   if (!screen->base.initialized)
      return;

   nouveau_bo_ref(NULL, &screen->notify);

   nouveau_heap_destroy(&screen->query_heap);
   nouveau_heap_destroy(&screen->vp_exec_heap);
   nouveau_heap_destroy(&screen->vp_data_heap);

   nouveau_object_del(&screen->query);
   nouveau_object_del(&screen->fence);
   nouveau_object_del(&screen->ntfy);

   nouveau_object_del(&screen->sifm);
   nouveau_object_del(&screen->swzsurf);
   nouveau_object_del(&screen->surf2d);
   nouveau_object_del(&screen->m2mf);
   nouveau_object_del(&screen->eng3d);
   nouveau_object_del(&screen->null);

   nouveau_screen_fini(&screen->base);
   FREE(screen);
}

#define FAIL_SCREEN_INIT(str, err)                    \
   do {                                               \
      NOUVEAU_ERR(str, err);                          \
      screen->base.base.context_create = NULL;        \
      return &screen->base;                           \
   } while(0)

struct nouveau_screen *
nv30_screen_create(struct nouveau_device *dev)
{
   struct nv30_screen *screen;
   struct pipe_screen *pscreen;
   struct nouveau_pushbuf *push;
   struct nv04_fifo *fifo;
   unsigned oclass = 0;
   int ret, i;

   switch (dev->chipset & 0xf0) {
   case 0x30:
      if (RANKINE_0397_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV30_3D_CLASS;
      else
      if (RANKINE_0697_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV34_3D_CLASS;
      else
      if (RANKINE_0497_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV35_3D_CLASS;
      break;
   case 0x40:
      if (CURIE_4097_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV40_3D_CLASS;
      else
      if (CURIE_4497_CHIPSET & (1 << (dev->chipset & 0x0f)))
         oclass = NV44_3D_CLASS;
      break;
   case 0x60:
      if (CURIE_4497_CHIPSET6X & (1 << (dev->chipset & 0x0f)))
         oclass = NV44_3D_CLASS;
      break;
   default:
      break;
   }

   if (!oclass) {
      NOUVEAU_ERR("unknown 3d class for 0x%02x\n", dev->chipset);
      return NULL;
   }

   screen = CALLOC_STRUCT(nv30_screen);
   if (!screen)
      return NULL;

   pscreen = &screen->base.base;
   pscreen->destroy = nv30_screen_destroy;

   /*
    * Some modern apps try to use msaa without keeping in mind the
    * restrictions on videomem of older cards. Resulting in dmesg saying:
    * [ 1197.850642] nouveau E[soffice.bin[3785]] fail ttm_validate
    * [ 1197.850648] nouveau E[soffice.bin[3785]] validating bo list
    * [ 1197.850654] nouveau E[soffice.bin[3785]] validate: -12
    *
    * Because we are running out of video memory, after which the program
    * using the msaa visual freezes, and eventually the entire system freezes.
    *
    * To work around this we do not allow msaa visauls by default and allow
    * the user to override this via NV30_MAX_MSAA.
    */
   screen->max_sample_count = debug_get_num_option("NV30_MAX_MSAA", 0);
   if (screen->max_sample_count > 4)
      screen->max_sample_count = 4;

   pscreen->context_create = nv30_context_create;
   pscreen->is_format_supported = nv30_screen_is_format_supported;

   pscreen->nir_options[MESA_SHADER_VERTEX] = &nv30_base_compiler_options;
   pscreen->nir_options[MESA_SHADER_FRAGMENT] = &screen->fs_compiler_options;

   nv30_resource_screen_init(pscreen);
   nouveau_screen_init_vdec(&screen->base);

   screen->base.fence.emit = nv30_screen_fence_emit;
   screen->base.fence.update = nv30_screen_fence_update;

   ret = nouveau_screen_init(&screen->base, dev);
   if (ret)
      FAIL_SCREEN_INIT("nv30_screen_init failed: %d\n", ret);

   screen->base.vidmem_bindings |= PIPE_BIND_VERTEX_BUFFER;
   screen->base.sysmem_bindings |= PIPE_BIND_VERTEX_BUFFER;
   if (oclass == NV40_3D_CLASS) {
      screen->base.vidmem_bindings |= PIPE_BIND_INDEX_BUFFER;
      screen->base.sysmem_bindings |= PIPE_BIND_INDEX_BUFFER;
   }

   screen->fs_compiler_options = nv30_base_compiler_options;
   screen->fs_compiler_options.lower_fsat = false;
   if (oclass >= NV40_3D_CLASS)
      screen->fs_compiler_options.lower_fpow = false;

   fifo = screen->base.channel->data;
   push = screen->base.pushbuf;
   push->rsvd_kick = 16;

   ret = nouveau_object_new(screen->base.channel, 0x00000000, NV01_NULL_CLASS,
                            NULL, 0, &screen->null);
   if (ret)
      FAIL_SCREEN_INIT("error allocating null object: %d\n", ret);

   /* DMA_FENCE refuses to accept DMA objects with "adjust" filled in,
    * this means that the address pointed at by the DMA object must
    * be 4KiB aligned, which means this object needs to be the first
    * one allocated on the channel.
    */
   ret = nouveau_object_new(screen->base.channel, 0xbeef1e00,
                            NOUVEAU_NOTIFIER_CLASS, &(struct nv04_notify) {
                            .length = 32 }, sizeof(struct nv04_notify),
                            &screen->fence);
   if (ret)
      FAIL_SCREEN_INIT("error allocating fence notifier: %d\n", ret);

   /* DMA_NOTIFY object, we don't actually use this but M2MF fails without */
   ret = nouveau_object_new(screen->base.channel, 0xbeef0301,
                            NOUVEAU_NOTIFIER_CLASS, &(struct nv04_notify) {
                            .length = 32 }, sizeof(struct nv04_notify),
                            &screen->ntfy);
   if (ret)
      FAIL_SCREEN_INIT("error allocating sync notifier: %d\n", ret);

   /* DMA_QUERY, used to implement occlusion queries, we attempt to allocate
    * the remainder of the "notifier block" assigned by the kernel for
    * use as query objects
    */
   ret = nouveau_object_new(screen->base.channel, 0xbeef0351,
                            NOUVEAU_NOTIFIER_CLASS, &(struct nv04_notify) {
                            .length = 4096 - 128 }, sizeof(struct nv04_notify),
                            &screen->query);
   if (ret)
      FAIL_SCREEN_INIT("error allocating query notifier: %d\n", ret);

   ret = nouveau_heap_init(&screen->query_heap, 0, 4096 - 128);
   if (ret)
      FAIL_SCREEN_INIT("error creating query heap: %d\n", ret);

   list_inithead(&screen->queries);

   /* Vertex program resources (code/data), currently 6 of the constant
    * slots are reserved to implement user clipping planes
    */
   if (oclass < NV40_3D_CLASS) {
      nouveau_heap_init(&screen->vp_exec_heap, 0, 256);
      nouveau_heap_init(&screen->vp_data_heap, 6, 256 - 6);
   } else {
      nouveau_heap_init(&screen->vp_exec_heap, 0, 512);
      nouveau_heap_init(&screen->vp_data_heap, 6, 468 - 6);
   }

   ret = nouveau_bo_wrap(screen->base.device, fifo->base.notify, &screen->notify);
   if (ret == 0)
      ret = BO_MAP(&screen->base, screen->notify, 0, screen->base.client);
   if (ret)
      FAIL_SCREEN_INIT("error mapping notifier memory: %d\n", ret);

   ret = nouveau_object_new(screen->base.channel, 0xbeef3097, oclass,
                            NULL, 0, &screen->eng3d);
   if (ret)
      FAIL_SCREEN_INIT("error allocating 3d object: %d\n", ret);

   nv30_init_shader_caps(screen);
   nv30_init_screen_caps(screen);

   BEGIN_NV04(push, NV01_SUBC(3D, OBJECT), 1);
   PUSH_DATA (push, screen->eng3d->handle);
   BEGIN_NV04(push, NV30_3D(DMA_NOTIFY), 13);
   PUSH_DATA (push, screen->ntfy->handle);
   PUSH_DATA (push, fifo->vram);     /* TEXTURE0 */
   PUSH_DATA (push, fifo->gart);     /* TEXTURE1 */
   PUSH_DATA (push, fifo->vram);     /* COLOR1 */
   PUSH_DATA (push, screen->null->handle);  /* UNK190 */
   PUSH_DATA (push, fifo->vram);     /* COLOR0 */
   PUSH_DATA (push, fifo->vram);     /* ZETA */
   PUSH_DATA (push, fifo->vram);     /* VTXBUF0 */
   PUSH_DATA (push, fifo->gart);     /* VTXBUF1 */
   PUSH_DATA (push, screen->fence->handle);  /* FENCE */
   PUSH_DATA (push, screen->query->handle);  /* QUERY - intr 0x80 if nullobj */
   PUSH_DATA (push, screen->null->handle);  /* UNK1AC */
   PUSH_DATA (push, screen->null->handle);  /* UNK1B0 */
   if (screen->eng3d->oclass < NV40_3D_CLASS) {
      BEGIN_NV04(push, SUBC_3D(0x03b0), 1);
      PUSH_DATA (push, 0x00100000);
      BEGIN_NV04(push, SUBC_3D(0x1d80), 1);
      PUSH_DATA (push, 3);

      BEGIN_NV04(push, SUBC_3D(0x1e98), 1);
      PUSH_DATA (push, 0);
      BEGIN_NV04(push, SUBC_3D(0x17e0), 3);
      PUSH_DATA (push, fui(0.0));
      PUSH_DATA (push, fui(0.0));
      PUSH_DATA (push, fui(1.0));
      BEGIN_NV04(push, SUBC_3D(0x1f80), 16);
      for (i = 0; i < 16; i++)
         PUSH_DATA (push, (i == 8) ? 0x0000ffff : 0);

      BEGIN_NV04(push, NV30_3D(RC_ENABLE), 1);
      PUSH_DATA (push, 0);
   } else {
      BEGIN_NV04(push, NV40_3D(DMA_COLOR2), 2);
      PUSH_DATA (push, fifo->vram);
      PUSH_DATA (push, fifo->vram);  /* COLOR3 */

      BEGIN_NV04(push, SUBC_3D(0x1450), 1);
      PUSH_DATA (push, 0x00000004);

      BEGIN_NV04(push, SUBC_3D(0x1ea4), 3); /* ZCULL */
      PUSH_DATA (push, 0x00000010);
      PUSH_DATA (push, 0x01000100);
      PUSH_DATA (push, 0xff800006);

      /* vtxprog output routing */
      BEGIN_NV04(push, SUBC_3D(0x1fc4), 1);
      PUSH_DATA (push, 0x06144321);
      BEGIN_NV04(push, SUBC_3D(0x1fc8), 2);
      PUSH_DATA (push, 0xedcba987);
      PUSH_DATA (push, 0x0000006f);
      BEGIN_NV04(push, SUBC_3D(0x1fd0), 1);
      PUSH_DATA (push, 0x00171615);
      BEGIN_NV04(push, SUBC_3D(0x1fd4), 1);
      PUSH_DATA (push, 0x001b1a19);

      BEGIN_NV04(push, SUBC_3D(0x1ef8), 1);
      PUSH_DATA (push, 0x0020ffff);
      BEGIN_NV04(push, SUBC_3D(0x1d64), 1);
      PUSH_DATA (push, 0x01d300d4);

      BEGIN_NV04(push, NV40_3D(MIPMAP_ROUNDING), 1);
      PUSH_DATA (push, NV40_3D_MIPMAP_ROUNDING_MODE_DOWN);
   }

   ret = nouveau_object_new(screen->base.channel, 0xbeef3901, NV03_M2MF_CLASS,
                            NULL, 0, &screen->m2mf);
   if (ret)
      FAIL_SCREEN_INIT("error allocating m2mf object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(M2MF, OBJECT), 1);
   PUSH_DATA (push, screen->m2mf->handle);
   BEGIN_NV04(push, NV03_M2MF(DMA_NOTIFY), 1);
   PUSH_DATA (push, screen->ntfy->handle);

   ret = nouveau_object_new(screen->base.channel, 0xbeef6201,
                            NV10_SURFACE_2D_CLASS, NULL, 0, &screen->surf2d);
   if (ret)
      FAIL_SCREEN_INIT("error allocating surf2d object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(SF2D, OBJECT), 1);
   PUSH_DATA (push, screen->surf2d->handle);
   BEGIN_NV04(push, NV04_SF2D(DMA_NOTIFY), 1);
   PUSH_DATA (push, screen->ntfy->handle);

   if (dev->chipset < 0x40)
      oclass = NV30_SURFACE_SWZ_CLASS;
   else
      oclass = NV40_SURFACE_SWZ_CLASS;

   ret = nouveau_object_new(screen->base.channel, 0xbeef5201, oclass,
                            NULL, 0, &screen->swzsurf);
   if (ret)
      FAIL_SCREEN_INIT("error allocating swizzled surface object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(SSWZ, OBJECT), 1);
   PUSH_DATA (push, screen->swzsurf->handle);
   BEGIN_NV04(push, NV04_SSWZ(DMA_NOTIFY), 1);
   PUSH_DATA (push, screen->ntfy->handle);

   if (dev->chipset < 0x40)
      oclass = NV30_SIFM_CLASS;
   else
      oclass = NV40_SIFM_CLASS;

   ret = nouveau_object_new(screen->base.channel, 0xbeef7701, oclass,
                            NULL, 0, &screen->sifm);
   if (ret)
      FAIL_SCREEN_INIT("error allocating scaled image object: %d\n", ret);

   BEGIN_NV04(push, NV01_SUBC(SIFM, OBJECT), 1);
   PUSH_DATA (push, screen->sifm->handle);
   BEGIN_NV04(push, NV03_SIFM(DMA_NOTIFY), 1);
   PUSH_DATA (push, screen->ntfy->handle);
   BEGIN_NV04(push, NV05_SIFM(COLOR_CONVERSION), 1);
   PUSH_DATA (push, NV05_SIFM_COLOR_CONVERSION_TRUNCATE);
   PUSH_KICK (push);

   return &screen->base;
}
