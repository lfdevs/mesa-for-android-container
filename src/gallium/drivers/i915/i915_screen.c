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


#include "draw/draw_context.h"
#include "util/os_misc.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"

#include "i915_reg.h"
#include "i915_debug.h"
#include "i915_context.h"
#include "i915_screen.h"
#include "i915_resource.h"
#include "i915_winsys.h"
#include "i915_public.h"


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

static int
i915_get_shader_param(struct pipe_screen *screen,
                      enum pipe_shader_type shader,
                      enum pipe_shader_cap cap)
{
   switch(shader) {
   case PIPE_SHADER_VERTEX:
      switch (cap) {
      case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
         if (debug_get_bool_option("DRAW_USE_LLVM", TRUE))
            return PIPE_MAX_SAMPLERS;
         else
            return 0;
      case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
         return 0;
       default:
         return draw_get_shader_param(shader, cap);
      }
   case PIPE_SHADER_FRAGMENT:
      /* XXX: some of these are just shader model 2.0 values, fix this! */
      switch(cap) {
      case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
         return I915_MAX_ALU_INSN + I915_MAX_TEX_INSN;
      case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
         return I915_MAX_ALU_INSN;
      case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
         return I915_MAX_TEX_INSN;
      case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
         return 8;
      case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
         return 0;
      case PIPE_SHADER_CAP_MAX_INPUTS:
         return 10;
      case PIPE_SHADER_CAP_MAX_OUTPUTS:
         return 1;
      case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
         return 32 * sizeof(float[4]);
      case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
         return 1;
      case PIPE_SHADER_CAP_MAX_TEMPS:
         return 12; /* XXX: 12 -> 32 ? */
      case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
         return 0;
      case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
         return 1;
      case PIPE_SHADER_CAP_SUBROUTINES:
         return 0;
      case PIPE_SHADER_CAP_INTEGERS:
      case PIPE_SHADER_CAP_INT64_ATOMICS:
      case PIPE_SHADER_CAP_FP16:
      case PIPE_SHADER_CAP_FP16_DERIVATIVES:
      case PIPE_SHADER_CAP_INT16:
      case PIPE_SHADER_CAP_GLSL_16BIT_CONSTS:
         return 0;
      case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
         return I915_TEX_UNITS;
      case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
      case PIPE_SHADER_CAP_PREFERRED_IR:
      case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
         return 0;
      case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
         return 32;
      default:
         debug_printf("%s: Unknown cap %u.\n", __FUNCTION__, cap);
         return 0;
      }
      break;
   default:
      return 0;
   }

}

static int
i915_get_param(struct pipe_screen *screen, enum pipe_cap cap)
{
   struct i915_screen *is = i915_screen(screen);

   switch (cap) {
   /* Supported features (boolean caps). */
   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_PRIMITIVE_RESTART: /* draw module */
   case PIPE_CAP_PRIMITIVE_RESTART_FIXED_INDEX:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_COLOR_CLAMPED:
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
      return 1;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0;

   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
   case PIPE_CAP_ALLOW_MAPPED_BUFFERS_DURING_EXECUTION:
      return 0;

   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return 32;

   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return 1 << 27;

   case PIPE_CAP_MAX_VIEWPORTS:
      return 1;

   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return 64;

   case PIPE_CAP_GLSL_FEATURE_LEVEL:
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 120;

   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 16;

   /* Features we can lie about (boolean caps). */
   case PIPE_CAP_OCCLUSION_QUERY:
      return is->debug.lie ? 1 : 0;

   /* Texturing. */
   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      return 1 << (I915_MAX_TEXTURE_2D_LEVELS - 1);
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return I915_MAX_TEXTURE_3D_LEVELS;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 1 << (I915_MAX_TEXTURE_2D_LEVELS - 1);

   /* Render targets. */
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return 1;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;

   /* Fragment coordinate conventions. */
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return 1;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_LITTLE;
   case PIPE_CAP_MAX_VARYINGS:
      return 10;

   case PIPE_CAP_VENDOR_ID:
      return 0x8086;
   case PIPE_CAP_DEVICE_ID:
      return is->iws->pci_id;
   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_VIDEO_MEMORY: {
      /* Once a batch uses more than 75% of the maximum mappable size, we
       * assume that there's some fragmentation, and we start doing extra
       * flushing, etc.  That's the big cliff apps will care about.
       */
      const int gpu_mappable_megabytes = is->iws->aperture_size(is->iws) * 3 / 4;
      uint64_t system_memory;

      if (!os_get_total_physical_memory(&system_memory))
         return 0;

      return MIN2(gpu_mappable_megabytes, (int)(system_memory >> 20));
   }
   case PIPE_CAP_UMA:
      return 1;

   default:
      return u_pipe_screen_get_param_defaults(screen, cap);
   }
}

static float
i915_get_paramf(struct pipe_screen *screen, enum pipe_capf cap)
{
   switch(cap) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
      /* fall-through */
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return 7.5;

   case PIPE_CAPF_MAX_POINT_WIDTH:
      /* fall-through */
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 255.0;

   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 4.0;

   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 16.0;

   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
      /* fall-through */
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
      /* fall-through */
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f;

   default:
      debug_printf("%s: Unknown cap %u.\n", __FUNCTION__, cap);
      return 0;
   }
}

bool
i915_is_format_supported(struct pipe_screen *screen,
                         enum pipe_format format,
                         enum pipe_texture_target target,
                         unsigned sample_count,
                         unsigned storage_sample_count,
                         unsigned tex_usage)
{
   static const enum pipe_format tex_supported[] = {
      PIPE_FORMAT_B8G8R8A8_UNORM,
      PIPE_FORMAT_B8G8R8A8_SRGB,
      PIPE_FORMAT_B8G8R8X8_UNORM,
      PIPE_FORMAT_R8G8B8A8_UNORM,
      PIPE_FORMAT_R8G8B8X8_UNORM,
      PIPE_FORMAT_B4G4R4A4_UNORM,
      PIPE_FORMAT_B5G6R5_UNORM,
      PIPE_FORMAT_B5G5R5A1_UNORM,
      PIPE_FORMAT_B10G10R10A2_UNORM,
      PIPE_FORMAT_L8_UNORM,
      PIPE_FORMAT_A8_UNORM,
      PIPE_FORMAT_I8_UNORM,
      PIPE_FORMAT_L8A8_UNORM,
      PIPE_FORMAT_UYVY,
      PIPE_FORMAT_YUYV,
      /* XXX why not?
      PIPE_FORMAT_Z16_UNORM, */
      PIPE_FORMAT_DXT1_RGB,
      PIPE_FORMAT_DXT1_RGBA,
      PIPE_FORMAT_DXT3_RGBA,
      PIPE_FORMAT_DXT5_RGBA,
      PIPE_FORMAT_Z24X8_UNORM,
      PIPE_FORMAT_Z24_UNORM_S8_UINT,
      PIPE_FORMAT_NONE  /* list terminator */
   };
   static const enum pipe_format render_supported[] = {
      PIPE_FORMAT_B8G8R8A8_UNORM,
      PIPE_FORMAT_B8G8R8X8_UNORM,
      PIPE_FORMAT_R8G8B8A8_UNORM,
      PIPE_FORMAT_R8G8B8X8_UNORM,
      PIPE_FORMAT_B5G6R5_UNORM,
      PIPE_FORMAT_B5G5R5A1_UNORM,
      PIPE_FORMAT_B4G4R4A4_UNORM,
      PIPE_FORMAT_B10G10R10A2_UNORM,
      PIPE_FORMAT_L8_UNORM,
      PIPE_FORMAT_A8_UNORM,
      PIPE_FORMAT_I8_UNORM,
      PIPE_FORMAT_NONE  /* list terminator */
   };
   static const enum pipe_format depth_supported[] = {
      /* XXX why not?
      PIPE_FORMAT_Z16_UNORM, */
      PIPE_FORMAT_Z24X8_UNORM,
      PIPE_FORMAT_Z24_UNORM_S8_UINT,
      PIPE_FORMAT_NONE  /* list terminator */
   };
   const enum pipe_format *list;
   uint i;

   if (sample_count > 1)
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if(tex_usage & PIPE_BIND_DEPTH_STENCIL)
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
i915_fence_reference(struct pipe_screen *screen,
                     struct pipe_fence_handle **ptr,
                     struct pipe_fence_handle *fence)
{
   struct i915_screen *is = i915_screen(screen);

   is->iws->fence_reference(is->iws, ptr, fence);
}

static bool
i915_fence_finish(struct pipe_screen *screen,
                  struct pipe_context *ctx,
                  struct pipe_fence_handle *fence,
                  uint64_t timeout)
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
i915_flush_frontbuffer(struct pipe_screen *screen,
                       struct pipe_context *pipe,
                       struct pipe_resource *resource,
                       unsigned level, unsigned layer,
                       void *winsys_drawable_handle,
                       struct pipe_box *sub_box)
{
   /* XXX: Dummy right now. */
   (void)screen;
   (void)pipe;
   (void)resource;
   (void)level;
   (void)layer;
   (void)winsys_drawable_handle;
   (void)sub_box;
}

static void
i915_destroy_screen(struct pipe_screen *screen)
{
   struct i915_screen *is = i915_screen(screen);

   if (is->iws)
      is->iws->destroy(is->iws);

   FREE(is);
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
      is->is_i945 = FALSE;
      break;

   case PCI_CHIP_I945_G:
   case PCI_CHIP_I945_GM:
   case PCI_CHIP_I945_GME:
   case PCI_CHIP_G33_G:
   case PCI_CHIP_Q33_G:
   case PCI_CHIP_Q35_G:
   case PCI_CHIP_PINEVIEW_G:
   case PCI_CHIP_PINEVIEW_M:
      is->is_i945 = TRUE;
      break;

   default:
      debug_printf("%s: unknown pci id 0x%x, cannot create screen\n", 
                   __FUNCTION__, iws->pci_id);
      FREE(is);
      return NULL;
   }

   is->iws = iws;

   is->base.destroy = i915_destroy_screen;
   is->base.flush_frontbuffer = i915_flush_frontbuffer;

   is->base.get_name = i915_get_name;
   is->base.get_vendor = i915_get_vendor;
   is->base.get_device_vendor = i915_get_device_vendor;
   is->base.get_param = i915_get_param;
   is->base.get_shader_param = i915_get_shader_param;
   is->base.get_paramf = i915_get_paramf;
   is->base.is_format_supported = i915_is_format_supported;

   is->base.context_create = i915_create_context;

   is->base.fence_reference = i915_fence_reference;
   is->base.fence_finish = i915_fence_finish;

   i915_init_screen_resource_functions(is);

   i915_debug_init(is);

   return &is->base;
}
