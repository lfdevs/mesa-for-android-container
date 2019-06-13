/*
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

#include <string.h>

#include "util/ralloc.h"
#include "util/u_debug.h"
#include "util/u_screen.h"
#include "renderonly/renderonly.h"

#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/lima_drm.h"

#include "lima_screen.h"
#include "lima_context.h"
#include "lima_resource.h"
#include "lima_program.h"
#include "lima_bo.h"
#include "lima_fence.h"
#include "ir/lima_ir.h"

#include "xf86drm.h"

static void
lima_screen_destroy(struct pipe_screen *pscreen)
{
   struct lima_screen *screen = lima_screen(pscreen);

   if (lima_dump_command_stream) {
      fclose(lima_dump_command_stream);
      lima_dump_command_stream = NULL;
   }

   slab_destroy_parent(&screen->transfer_pool);

   if (screen->ro)
      free(screen->ro);

   if (screen->pp_buffer)
      lima_bo_free(screen->pp_buffer);

   lima_bo_table_fini(screen);
   ralloc_free(screen);
}

static const char *
lima_screen_get_name(struct pipe_screen *pscreen)
{
   struct lima_screen *screen = lima_screen(pscreen);

   switch (screen->gpu_type) {
   case DRM_LIMA_PARAM_GPU_ID_MALI400:
     return "Mali400";
   case DRM_LIMA_PARAM_GPU_ID_MALI450:
     return "Mali450";
   }

   return NULL;
}

static const char *
lima_screen_get_vendor(struct pipe_screen *pscreen)
{
   return "lima";
}

static const char *
lima_screen_get_device_vendor(struct pipe_screen *pscreen)
{
   return "ARM";
}

static int
lima_screen_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_ACCELERATED:
   case PIPE_CAP_UMA:
   case PIPE_CAP_NATIVE_FENCE_FD:
      return 1;

   /* Unimplemented, but for exporting OpenGL 2.0 */
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_POINT_SPRITE:
      return 1;

   /* not clear supported */
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return 1;

   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return LIMA_MAX_MIP_LEVELS;

   case PIPE_CAP_VENDOR_ID:
      return 0x13B5;

   case PIPE_CAP_VIDEO_MEMORY:
      return 0;

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 0;

   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
lima_screen_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
   case PIPE_CAPF_MAX_POINT_WIDTH:
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 255.0f;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 16.0f;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 16.0f;

   default:
      return 0.0f;
   }
}

static int
get_vertex_shader_param(struct lima_screen *screen,
                        enum pipe_shader_cap param)
{
   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return 16384; /* need investigate */

   case PIPE_SHADER_CAP_MAX_INPUTS:
      return 16; /* attributes */

   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      return LIMA_MAX_VARYING_NUM; /* varying */

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return 4096; /* need investigate */
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return 1;

   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_MAX_TEMPS:
      return 256; /* need investigate */

   default:
      return 0;
   }
}

static int
get_fragment_shader_param(struct lima_screen *screen,
                          enum pipe_shader_cap param)
{
   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return 16384; /* need investigate */

   case PIPE_SHADER_CAP_MAX_INPUTS:
      return LIMA_MAX_VARYING_NUM - 1; /* varying, minus gl_Position */

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return 4096; /* need investigate */
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return 1;

   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      return 16; /* need investigate */

   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_MAX_TEMPS:
      return 256; /* need investigate */

   default:
      return 0;
   }
}

static int
lima_screen_get_shader_param(struct pipe_screen *pscreen,
                             enum pipe_shader_type shader,
                             enum pipe_shader_cap param)
{
   struct lima_screen *screen = lima_screen(pscreen);

   switch (shader) {
   case PIPE_SHADER_FRAGMENT:
      return get_fragment_shader_param(screen, param);
   case PIPE_SHADER_VERTEX:
      return get_vertex_shader_param(screen, param);

   default:
      return 0;
   }
}

static boolean
lima_screen_is_format_supported(struct pipe_screen *pscreen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned usage)
{
   switch (target) {
   case PIPE_BUFFER:
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_2D:
      break;
   default:
      return FALSE;
   }

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   /* be able to support 16, now limit to 4 */
   if (sample_count > 1 && sample_count != 4)
      return FALSE;

   if (usage & PIPE_BIND_RENDER_TARGET) {
      switch (format) {
      case PIPE_FORMAT_B8G8R8A8_UNORM:
      case PIPE_FORMAT_B8G8R8X8_UNORM:
      case PIPE_FORMAT_R8G8B8A8_UNORM:
      case PIPE_FORMAT_R8G8B8X8_UNORM:
      case PIPE_FORMAT_Z16_UNORM:
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      case PIPE_FORMAT_Z24X8_UNORM:
         break;
      default:
         return FALSE;
      }
   }

   if (usage & PIPE_BIND_DEPTH_STENCIL) {
      switch (format) {
      case PIPE_FORMAT_Z16_UNORM:
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      case PIPE_FORMAT_Z24X8_UNORM:
         break;
      default:
         return FALSE;
      }
   }

   if (usage & PIPE_BIND_VERTEX_BUFFER) {
      switch (format) {
      case PIPE_FORMAT_R32G32B32_FLOAT:
         break;
      default:
         return FALSE;
      }
   }

   if (usage & PIPE_BIND_INDEX_BUFFER) {
      switch (format) {
      case PIPE_FORMAT_I8_UINT:
      case PIPE_FORMAT_I16_UINT:
      case PIPE_FORMAT_I32_UINT:
         break;
      default:
         return FALSE;
      }
   }

   if (usage & PIPE_BIND_SAMPLER_VIEW) {
      switch (format) {
      case PIPE_FORMAT_R8G8B8X8_UNORM:
      case PIPE_FORMAT_R8G8B8A8_UNORM:
      case PIPE_FORMAT_B8G8R8X8_UNORM:
      case PIPE_FORMAT_B8G8R8A8_UNORM:
      case PIPE_FORMAT_A8B8G8R8_SRGB:
      case PIPE_FORMAT_B8G8R8A8_SRGB:
      case PIPE_FORMAT_Z16_UNORM:
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      case PIPE_FORMAT_Z24X8_UNORM:
         break;
      default:
         return FALSE;
      }
   }

   return TRUE;
}

static const void *
lima_screen_get_compiler_options(struct pipe_screen *pscreen,
                                 enum pipe_shader_ir ir,
                                 enum pipe_shader_type shader)
{
   return lima_program_get_compiler_options(shader);
}

static bool
lima_screen_query_info(struct lima_screen *screen)
{
   struct drm_lima_get_param param;

   memset(&param, 0, sizeof(param));
   param.param = DRM_LIMA_PARAM_GPU_ID;
   if (drmIoctl(screen->fd, DRM_IOCTL_LIMA_GET_PARAM, &param))
      return false;

   switch (param.value) {
   case DRM_LIMA_PARAM_GPU_ID_MALI400:
   case DRM_LIMA_PARAM_GPU_ID_MALI450:
      screen->gpu_type = param.value;
      break;
   default:
      return false;
   }

   memset(&param, 0, sizeof(param));
   param.param = DRM_LIMA_PARAM_NUM_PP;
   if (drmIoctl(screen->fd, DRM_IOCTL_LIMA_GET_PARAM, &param))
      return false;

   screen->num_pp = param.value;

   return true;
}

static void
lima_screen_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                                   enum pipe_format format, int max,
                                   uint64_t *modifiers,
                                   unsigned int *external_only,
                                   int *count)
{
   uint64_t available_modifiers[] = {
      DRM_FORMAT_MOD_LINEAR,
   };

   if (!modifiers) {
      *count = ARRAY_SIZE(available_modifiers);
      return;
   }

   for (int i = 0; i < *count; i++) {
      modifiers[i] = available_modifiers[i];
      if (external_only)
         external_only = false;
   }
}

static const struct debug_named_value debug_options[] = {
        { "gp",       LIMA_DEBUG_GP,
          "print GP shader compiler result of each stage" },
        { "pp",       LIMA_DEBUG_PP,
          "print PP shader compiler result of each stage" },
        { "dump",     LIMA_DEBUG_DUMP,
          "dump GPU command stream to $PWD/lima.dump" },
        { NULL }
};

DEBUG_GET_ONCE_FLAGS_OPTION(lima_debug, "LIMA_DEBUG", debug_options, 0)
uint32_t lima_debug;

static void
lima_screen_parse_env(void)
{
   lima_debug = debug_get_option_lima_debug();

   if (lima_debug & LIMA_DEBUG_DUMP) {
      const char *dump_command = "lima.dump";
      printf("lima: dump command stream to file %s\n", dump_command);
      lima_dump_command_stream = fopen(dump_command, "w");
      if (!lima_dump_command_stream)
         fprintf(stderr, "lima: fail to open command stream log file %s\n",
                 dump_command);
   }

   lima_ctx_num_plb = debug_get_num_option("LIMA_CTX_NUM_PLB", LIMA_CTX_PLB_DEF_NUM);
   if (lima_ctx_num_plb > LIMA_CTX_PLB_MAX_NUM ||
       lima_ctx_num_plb < LIMA_CTX_PLB_MIN_NUM) {
      fprintf(stderr, "lima: LIMA_CTX_NUM_PLB %d out of range [%d %d], "
              "reset to default %d\n", lima_ctx_num_plb, LIMA_CTX_PLB_MIN_NUM,
              LIMA_CTX_PLB_MAX_NUM, LIMA_CTX_PLB_DEF_NUM);
      lima_ctx_num_plb = LIMA_CTX_PLB_DEF_NUM;
   }

   lima_ppir_force_spilling = debug_get_num_option("LIMA_PPIR_FORCE_SPILLING", 0);
   if (lima_ppir_force_spilling < 0) {
      fprintf(stderr, "lima: LIMA_PPIR_FORCE_SPILLING %d less than 0, "
              "reset to default 0\n", lima_ppir_force_spilling);
      lima_ppir_force_spilling = 0;
   }
}

struct pipe_screen *
lima_screen_create(int fd, struct renderonly *ro)
{
   struct lima_screen *screen;

   screen = rzalloc(NULL, struct lima_screen);
   if (!screen)
      return NULL;

   screen->fd = fd;

   if (!lima_screen_query_info(screen))
      goto err_out0;

   if (!lima_bo_table_init(screen))
      goto err_out0;

   screen->pp_ra = ppir_regalloc_init(screen);
   if (!screen->pp_ra)
      goto err_out1;

   screen->pp_buffer = lima_bo_create(screen, pp_buffer_size, 0);
   if (!screen->pp_buffer)
      goto err_out1;

   /* fs program for clear buffer?
    * const0 1 0 0 -1.67773, mov.v0 $0 ^const0.xxxx, stop
    */
   static const uint32_t pp_clear_program[] = {
      0x00020425, 0x0000000c, 0x01e007cf, 0xb0000000,
      0x000005f5, 0x00000000, 0x00000000, 0x00000000,
   };
   memcpy(lima_bo_map(screen->pp_buffer) + pp_clear_program_offset,
          pp_clear_program, sizeof(pp_clear_program));

   /* copy texture to framebuffer, used to reload gpu tile buffer
    * load.v $1 0.xy, texld_2d 0, mov.v0 $0 ^tex_sampler, sync, stop
    */
   static const uint32_t pp_reload_program[] = {
      0x000005e6, 0xf1003c20, 0x00000000, 0x39001000,
      0x00000e4e, 0x000007cf, 0x00000000, 0x00000000,
   };
   memcpy(lima_bo_map(screen->pp_buffer) + pp_reload_program_offset,
          pp_reload_program, sizeof(pp_reload_program));

   /* 0/1/2 vertex index for reload/clear draw */
   static const uint8_t pp_shared_index[] = { 0, 1, 2 };
   memcpy(lima_bo_map(screen->pp_buffer) + pp_shared_index_offset,
          pp_shared_index, sizeof(pp_shared_index));

   /* 4096x4096 gl pos used for partial clear */
   static const float pp_clear_gl_pos[] = {
      4096, 0,    1, 1,
      0,    0,    1, 1,
      0,    4096, 1, 1,
   };
   memcpy(lima_bo_map(screen->pp_buffer) + pp_clear_gl_pos_offset,
          pp_clear_gl_pos, sizeof(pp_clear_gl_pos));

   /* is pp frame render state static? */
   uint32_t *pp_frame_rsw = lima_bo_map(screen->pp_buffer) + pp_frame_rsw_offset;
   memset(pp_frame_rsw, 0, 0x40);
   pp_frame_rsw[8] = 0x0000f008;
   pp_frame_rsw[9] = screen->pp_buffer->va + pp_clear_program_offset;
   pp_frame_rsw[13] = 0x00000100;

   if (ro) {
      screen->ro = renderonly_dup(ro);
      if (!screen->ro) {
         fprintf(stderr, "Failed to dup renderonly object\n");
         goto err_out2;
      }
   }

   screen->base.destroy = lima_screen_destroy;
   screen->base.get_name = lima_screen_get_name;
   screen->base.get_vendor = lima_screen_get_vendor;
   screen->base.get_device_vendor = lima_screen_get_device_vendor;
   screen->base.get_param = lima_screen_get_param;
   screen->base.get_paramf = lima_screen_get_paramf;
   screen->base.get_shader_param = lima_screen_get_shader_param;
   screen->base.context_create = lima_context_create;
   screen->base.is_format_supported = lima_screen_is_format_supported;
   screen->base.get_compiler_options = lima_screen_get_compiler_options;
   screen->base.query_dmabuf_modifiers = lima_screen_query_dmabuf_modifiers;

   lima_resource_screen_init(screen);
   lima_fence_screen_init(screen);

   slab_create_parent(&screen->transfer_pool, sizeof(struct lima_transfer), 16);

   screen->refcnt = 1;

   lima_screen_parse_env();

   return &screen->base;

err_out2:
   lima_bo_free(screen->pp_buffer);
err_out1:
   lima_bo_table_fini(screen);
err_out0:
   ralloc_free(screen);
   return NULL;
}
