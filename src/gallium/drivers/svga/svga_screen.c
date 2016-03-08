/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_string.h"
#include "util/u_math.h"

#include "svga_winsys.h"
#include "svga_public.h"
#include "svga_context.h"
#include "svga_format.h"
#include "svga_screen.h"
#include "svga_tgsi.h"
#include "svga_resource_texture.h"
#include "svga_resource.h"
#include "svga_debug.h"

#include "svga3d_shaderdefs.h"
#include "VGPU10ShaderTokens.h"

/* NOTE: this constant may get moved into a svga3d*.h header file */
#define SVGA3D_DX_MAX_RESOURCE_SIZE (128 * 1024 * 1024)

#ifdef DEBUG
int SVGA_DEBUG = 0;

static const struct debug_named_value svga_debug_flags[] = {
   { "dma",         DEBUG_DMA, NULL },
   { "tgsi",        DEBUG_TGSI, NULL },
   { "pipe",        DEBUG_PIPE, NULL },
   { "state",       DEBUG_STATE, NULL },
   { "screen",      DEBUG_SCREEN, NULL },
   { "tex",         DEBUG_TEX, NULL },
   { "swtnl",       DEBUG_SWTNL, NULL },
   { "const",       DEBUG_CONSTS, NULL },
   { "viewport",    DEBUG_VIEWPORT, NULL },
   { "views",       DEBUG_VIEWS, NULL },
   { "perf",        DEBUG_PERF, NULL },
   { "flush",       DEBUG_FLUSH, NULL },
   { "sync",        DEBUG_SYNC, NULL },
   { "cache",       DEBUG_CACHE, NULL },
   { "streamout",   DEBUG_STREAMOUT, NULL },
   { "query",       DEBUG_QUERY, NULL },
   DEBUG_NAMED_VALUE_END
};
#endif

static const char *
svga_get_vendor( struct pipe_screen *pscreen )
{
   return "VMware, Inc.";
}


static const char *
svga_get_name( struct pipe_screen *pscreen )
{
   const char *build = "", *llvm = "", *mutex = "";
   static char name[100];
#ifdef DEBUG
   /* Only return internal details in the DEBUG version:
    */
   build = "build: DEBUG;";
   mutex = "mutex: " PIPE_ATOMIC ";";
#else
   build = "build: RELEASE;";
#endif
#ifdef HAVE_LLVM
   llvm = "LLVM;";
#endif

   util_snprintf(name, sizeof(name), "SVGA3D; %s %s %s", build, mutex, llvm);
   return name;
}


/** Helper for querying float-valued device cap */
static float
get_float_cap(struct svga_winsys_screen *sws, unsigned cap, float defaultVal)
{
   SVGA3dDevCapResult result;
   if (sws->get_cap(sws, cap, &result))
      return result.f;
   else
      return defaultVal;
}


/** Helper for querying uint-valued device cap */
static unsigned
get_uint_cap(struct svga_winsys_screen *sws, unsigned cap, unsigned defaultVal)
{
   SVGA3dDevCapResult result;
   if (sws->get_cap(sws, cap, &result))
      return result.u;
   else
      return defaultVal;
}


/** Helper for querying boolean-valued device cap */
static boolean
get_bool_cap(struct svga_winsys_screen *sws, unsigned cap, boolean defaultVal)
{
   SVGA3dDevCapResult result;
   if (sws->get_cap(sws, cap, &result))
      return result.b;
   else
      return defaultVal;
}


static float
svga_get_paramf(struct pipe_screen *screen, enum pipe_capf param)
{
   struct svga_screen *svgascreen = svga_screen(screen);
   struct svga_winsys_screen *sws = svgascreen->sws;

   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
      return svgascreen->maxLineWidth;
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return svgascreen->maxLineWidthAA;

   case PIPE_CAPF_MAX_POINT_WIDTH:
      /* fall-through */
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return svgascreen->maxPointSize;

   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return (float) get_uint_cap(sws, SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY, 4);

   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 15.0;

   case PIPE_CAPF_GUARD_BAND_LEFT:
   case PIPE_CAPF_GUARD_BAND_TOP:
   case PIPE_CAPF_GUARD_BAND_RIGHT:
   case PIPE_CAPF_GUARD_BAND_BOTTOM:
      return 0.0;
   }

   debug_printf("Unexpected PIPE_CAPF_ query %u\n", param);
   return 0;
}


static int
svga_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
   struct svga_screen *svgascreen = svga_screen(screen);
   struct svga_winsys_screen *sws = svgascreen->sws;
   SVGA3dDevCapResult result;

   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
      return 1;
   case PIPE_CAP_TWO_SIDED_STENCIL:
      return 1;
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      /*
       * "In virtually every OpenGL implementation and hardware,
       * GL_MAX_DUAL_SOURCE_DRAW_BUFFERS is 1"
       * http://www.opengl.org/wiki/Blending
       */
      return sws->have_vgpu10 ? 1 : 0;
   case PIPE_CAP_ANISOTROPIC_FILTER:
      return 1;
   case PIPE_CAP_POINT_SPRITE:
      return 1;
   case PIPE_CAP_TGSI_TEXCOORD:
      return 0;
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return svgascreen->max_color_buffers;
   case PIPE_CAP_OCCLUSION_QUERY:
      return 1;
   case PIPE_CAP_QUERY_TIME_ELAPSED:
      return 0;
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
      return sws->have_vgpu10;
   case PIPE_CAP_TEXTURE_SHADOW_MAP:
      return 1;
   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;
   case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
      return 0;
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_USER_INDEX_BUFFERS:
      return 0;
   case PIPE_CAP_USER_CONSTANT_BUFFERS:
      return 1;
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 256;

   case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
      {
         unsigned levels = SVGA_MAX_TEXTURE_LEVELS;
         if (sws->get_cap(sws, SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH, &result))
            levels = MIN2(util_logbase2(result.u) + 1, levels);
         else
            levels = 12 /* 2048x2048 */;
         if (sws->get_cap(sws, SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT, &result))
            levels = MIN2(util_logbase2(result.u) + 1, levels);
         else
            levels = 12 /* 2048x2048 */;
         return levels;
      }

   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      if (!sws->get_cap(sws, SVGA3D_DEVCAP_MAX_VOLUME_EXTENT, &result))
         return 8;  /* max 128x128x128 */
      return MIN2(util_logbase2(result.u) + 1, SVGA_MAX_TEXTURE_LEVELS);

   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      /*
       * No mechanism to query the host, and at least limited to 2048x2048 on
       * certain hardware.
       */
      return MIN2(screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_2D_LEVELS),
                  12 /* 2048x2048 */);

   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return sws->have_vgpu10 ? SVGA3D_MAX_SURFACE_ARRAYSIZE : 0;

   case PIPE_CAP_BLEND_EQUATION_SEPARATE: /* req. for GL 1.5 */
      return 1;

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
      return 1;
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return sws->have_vgpu10;
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
      return 0;
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
      return !sws->have_vgpu10;

   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
      return 1; /* The color outputs of vertex shaders are not clamped */
   case PIPE_CAP_VERTEX_COLOR_CLAMPED:
      return 0; /* The driver can't clamp vertex colors */
   case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
      return 0; /* The driver can't clamp fragment colors */

   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
      return 1; /* expected for GL_ARB_framebuffer_object */

   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return sws->have_vgpu10 ? 330 : 120;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 0;

   case PIPE_CAP_SM3:
      return 1;

   case PIPE_CAP_DEPTH_CLIP_DISABLE:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_FAKE_SW_MSAA:
      return sws->have_vgpu10;

   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return sws->have_vgpu10 ? SVGA3D_DX_MAX_SOTARGETS : 0;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
      return sws->have_vgpu10 ? 4 : 0;
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return sws->have_vgpu10 ? SVGA3D_MAX_STREAMOUT_DECLS : 0;
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
      return 0;
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
      return svgascreen->ms_samples ? 1 : 0;

   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return SVGA3D_DX_MAX_RESOURCE_SIZE;

   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return sws->have_vgpu10 ? VGPU10_MIN_TEXEL_FETCH_OFFSET : 0;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return sws->have_vgpu10 ? VGPU10_MAX_TEXEL_FETCH_OFFSET : 0;

   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return 0;

   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
      return sws->have_vgpu10 ? 256 : 0;
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return sws->have_vgpu10 ? 1024 : 0;

   case PIPE_CAP_PRIMITIVE_RESTART:
      return 1; /* may be a sw fallback, depending on restart index */

   case PIPE_CAP_GENERATE_MIPMAP:
      return sws->have_vgpu10;

   /* Unsupported features */
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
   case PIPE_CAP_SHADER_STENCIL_EXPORT:
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_TEXTURE_BARRIER:
   case PIPE_CAP_MAX_VERTEX_STREAMS:
   case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
   case PIPE_CAP_COMPUTE:
   case PIPE_CAP_START_INSTANCE:
   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
   case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
   case PIPE_CAP_TEXTURE_GATHER_SM5:
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_SAMPLE_SHADING:
   case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
   case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT:
   case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
   case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
   case PIPE_CAP_SAMPLER_VIEW_TARGET:
   case PIPE_CAP_CLIP_HALFZ:
   case PIPE_CAP_VERTEXID_NOBASE:
   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
   case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
   case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
   case PIPE_CAP_INVALIDATE_BUFFER:
   case PIPE_CAP_STRING_MARKER:
   case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
   case PIPE_CAP_QUERY_MEMORY_INFO:
      return 0;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return 64;
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
      return 1;  /* need 4-byte alignment for all offsets and strides */
   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;
   case PIPE_CAP_MAX_VIEWPORTS:
      return 1;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_LITTLE;

   case PIPE_CAP_VENDOR_ID:
      return 0x15ad; /* VMware Inc. */
   case PIPE_CAP_DEVICE_ID:
      return 0x0405; /* assume SVGA II */
   case PIPE_CAP_ACCELERATED:
      return 0; /* XXX: */
   case PIPE_CAP_VIDEO_MEMORY:
      /* XXX: Query the host ? */
      return 1;
   case PIPE_CAP_UMA:
   case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
   case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
   case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
   case PIPE_CAP_DEPTH_BOUNDS_TEST:
   case PIPE_CAP_TGSI_TXQS:
   case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
   case PIPE_CAP_SHAREABLE_SHADERS:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_DRAW_PARAMETERS:
   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
   case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
   case PIPE_CAP_QUERY_BUFFER_OBJECT:
      return 0;
   }

   debug_printf("Unexpected PIPE_CAP_ query %u\n", param);
   return 0;
}


static int
vgpu9_get_shader_param(struct pipe_screen *screen, unsigned shader,
                       enum pipe_shader_cap param)
{
   struct svga_screen *svgascreen = svga_screen(screen);
   struct svga_winsys_screen *sws = svgascreen->sws;
   unsigned val;

   assert(!sws->have_vgpu10);

   switch (shader)
   {
   case PIPE_SHADER_FRAGMENT:
      switch (param)
      {
      case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
         return 512;
      case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
         return SVGA3D_MAX_NESTING_LEVEL;
      case PIPE_SHADER_CAP_MAX_INPUTS:
         return 10;
      case PIPE_SHADER_CAP_MAX_OUTPUTS:
         return svgascreen->max_color_buffers;
      case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
         return 224 * sizeof(float[4]);
      case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
         return 1;
      case PIPE_SHADER_CAP_MAX_TEMPS:
         val = get_uint_cap(sws, SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_TEMPS, 32);
         return MIN2(val, SVGA3D_TEMPREG_MAX);
      case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
	 /* 
	  * Although PS 3.0 has some addressing abilities it can only represent
	  * loops that can be statically determined and unrolled. Given we can
	  * only handle a subset of the cases that the state tracker already
	  * does it is better to defer loop unrolling to the state tracker.
	  */
         return 0;
      case PIPE_SHADER_CAP_MAX_PREDS:
         return 1;
      case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
         return 0;
      case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
         return 0;
      case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
         return 0;
      case PIPE_SHADER_CAP_SUBROUTINES:
         return 0;
      case PIPE_SHADER_CAP_INTEGERS:
         return 0;
      case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
         return 16;
      case PIPE_SHADER_CAP_PREFERRED_IR:
         return PIPE_SHADER_IR_TGSI;
      case PIPE_SHADER_CAP_SUPPORTED_IRS:
         return 0;
      case PIPE_SHADER_CAP_DOUBLES:
      case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
         return 0;
      case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
         return 32;
      }
      /* If we get here, we failed to handle a cap above */
      debug_printf("Unexpected fragment shader query %u\n", param);
      return 0;
   case PIPE_SHADER_VERTEX:
      switch (param)
      {
      case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
         return get_uint_cap(sws, SVGA3D_DEVCAP_MAX_VERTEX_SHADER_INSTRUCTIONS,
                             512);
      case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
      case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
         /* XXX: until we have vertex texture support */
         return 0;
      case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
         return SVGA3D_MAX_NESTING_LEVEL;
      case PIPE_SHADER_CAP_MAX_INPUTS:
         return 16;
      case PIPE_SHADER_CAP_MAX_OUTPUTS:
         return 10;
      case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
         return 256 * sizeof(float[4]);
      case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
         return 1;
      case PIPE_SHADER_CAP_MAX_TEMPS:
         val = get_uint_cap(sws, SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEMPS, 32);
         return MIN2(val, SVGA3D_TEMPREG_MAX);
      case PIPE_SHADER_CAP_MAX_PREDS:
         return 1;
      case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
         return 0;
      case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
         return 0;
      case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
      case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
         return 1;
      case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
         return 0;
      case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
         return 1;
      case PIPE_SHADER_CAP_SUBROUTINES:
         return 0;
      case PIPE_SHADER_CAP_INTEGERS:
         return 0;
      case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
         return 0;
      case PIPE_SHADER_CAP_PREFERRED_IR:
         return PIPE_SHADER_IR_TGSI;
      case PIPE_SHADER_CAP_SUPPORTED_IRS:
         return 0;
      case PIPE_SHADER_CAP_DOUBLES:
      case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
         return 0;
      case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
         return 32;
      }
      /* If we get here, we failed to handle a cap above */
      debug_printf("Unexpected vertex shader query %u\n", param);
      return 0;
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_COMPUTE:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_TESS_EVAL:
      /* no support for geometry, tess or compute shaders at this time */
      return 0;
   default:
      debug_printf("Unexpected shader type (%u) query\n", shader);
      return 0;
   }
   return 0;
}


static int
vgpu10_get_shader_param(struct pipe_screen *screen, unsigned shader,
                        enum pipe_shader_cap param)
{
   struct svga_screen *svgascreen = svga_screen(screen);
   struct svga_winsys_screen *sws = svgascreen->sws;

   assert(sws->have_vgpu10);
   (void) sws;  /* silence unused var warnings in non-debug builds */

   /* Only VS, GS, FS supported */
   if (shader != PIPE_SHADER_VERTEX &&
       shader != PIPE_SHADER_GEOMETRY &&
       shader != PIPE_SHADER_FRAGMENT) {
      return 0;
   }

   /* NOTE: we do not query the device for any caps/limits at this time */

   /* Generally the same limits for vertex, geometry and fragment shaders */
   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return 64 * 1024;
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return 64;
   case PIPE_SHADER_CAP_MAX_INPUTS:
      if (shader == PIPE_SHADER_FRAGMENT)
         return VGPU10_MAX_FS_INPUTS;
      else if (shader == PIPE_SHADER_GEOMETRY)
         return VGPU10_MAX_GS_INPUTS;
      else
         return VGPU10_MAX_VS_INPUTS;
   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      if (shader == PIPE_SHADER_FRAGMENT)
         return VGPU10_MAX_FS_OUTPUTS;
      else if (shader == PIPE_SHADER_GEOMETRY)
         return VGPU10_MAX_GS_OUTPUTS;
      else
         return VGPU10_MAX_VS_OUTPUTS;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return VGPU10_MAX_CONSTANT_BUFFER_ELEMENT_COUNT * sizeof(float[4]);
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return svgascreen->max_const_buffers;
   case PIPE_SHADER_CAP_MAX_TEMPS:
      return VGPU10_MAX_TEMPS;
   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return TRUE; /* XXX verify */
   case PIPE_SHADER_CAP_MAX_PREDS:
      return 0;
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
   case PIPE_SHADER_CAP_SUBROUTINES:
   case PIPE_SHADER_CAP_INTEGERS:
      return TRUE;
   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return SVGA3D_DX_MAX_SAMPLERS;
   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_TGSI;
   case PIPE_SHADER_CAP_SUPPORTED_IRS:
         return 0;
   case PIPE_SHADER_CAP_DOUBLES:
   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      return 0;
   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 32;
   default:
      debug_printf("Unexpected vgpu10 shader query %u\n", param);
      return 0;
   }
   return 0;
}


static int
svga_get_shader_param(struct pipe_screen *screen, unsigned shader,
                      enum pipe_shader_cap param)
{
   struct svga_screen *svgascreen = svga_screen(screen);
   struct svga_winsys_screen *sws = svgascreen->sws;
   if (sws->have_vgpu10) {
      return vgpu10_get_shader_param(screen, shader, param);
   }
   else {
      return vgpu9_get_shader_param(screen, shader, param);
   }
}


/**
 * Implement pipe_screen::is_format_supported().
 * \param bindings  bitmask of PIPE_BIND_x flags
 */
static boolean
svga_is_format_supported( struct pipe_screen *screen,
                          enum pipe_format format,
                          enum pipe_texture_target target,
                          unsigned sample_count,
                          unsigned bindings)
{
   struct svga_screen *ss = svga_screen(screen);
   SVGA3dSurfaceFormat svga_format;
   SVGA3dSurfaceFormatCaps caps;
   SVGA3dSurfaceFormatCaps mask;

   assert(bindings);

   if (sample_count > 1) {
      /* In ms_samples, if bit N is set it means that we support
       * multisample with N+1 samples per pixel.
       */
      if ((ss->ms_samples & (1 << (sample_count - 1))) == 0) {
         return FALSE;
      }
   }

   svga_format = svga_translate_format(ss, format, bindings);
   if (svga_format == SVGA3D_FORMAT_INVALID) {
      return FALSE;
   }

   /* we don't support sRGB rendering into display targets */
   if (util_format_is_srgb(format) && (bindings & PIPE_BIND_DISPLAY_TARGET)) {
      return FALSE;
   }

   /*
    * For VGPU10 vertex formats, skip querying host capabilities
    */

   if (ss->sws->have_vgpu10 && (bindings & PIPE_BIND_VERTEX_BUFFER)) {
      SVGA3dSurfaceFormat svga_format;
      unsigned flags;
      svga_translate_vertex_format_vgpu10(format, &svga_format, &flags);
      return svga_format != SVGA3D_FORMAT_INVALID;
   }

   /*
    * Override host capabilities, so that we end up with the same
    * visuals for all virtual hardware implementations.
    */

   if (bindings & PIPE_BIND_DISPLAY_TARGET) {
      switch (svga_format) {
      case SVGA3D_A8R8G8B8:
      case SVGA3D_X8R8G8B8:
      case SVGA3D_R5G6B5:
         break;

      /* VGPU10 formats */
      case SVGA3D_B8G8R8A8_UNORM:
      case SVGA3D_B8G8R8X8_UNORM:
      case SVGA3D_B5G6R5_UNORM:
         break;

      /* Often unsupported/problematic. This means we end up with the same
       * visuals for all virtual hardware implementations.
       */
      case SVGA3D_A4R4G4B4:
      case SVGA3D_A1R5G5B5:
         return FALSE;
         
      default:
         return FALSE;
      }
   }
   
   /*
    * Query the host capabilities.
    */

   svga_get_format_cap(ss, svga_format, &caps);

   if (bindings & PIPE_BIND_RENDER_TARGET) {
      /* Check that the color surface is blendable, unless it's an
       * integer format.
       */
      if (!svga_format_is_integer(svga_format) &&
          (caps.value & SVGA3DFORMAT_OP_NOALPHABLEND)) {
         return FALSE;
      }
   }

   mask.value = 0;
   if (bindings & PIPE_BIND_RENDER_TARGET) {
      mask.value |= SVGA3DFORMAT_OP_OFFSCREEN_RENDERTARGET;
   }
   if (bindings & PIPE_BIND_DEPTH_STENCIL) {
      mask.value |= SVGA3DFORMAT_OP_ZSTENCIL;
   }
   if (bindings & PIPE_BIND_SAMPLER_VIEW) {
      mask.value |= SVGA3DFORMAT_OP_TEXTURE;
   }

   if (target == PIPE_TEXTURE_CUBE) {
      mask.value |= SVGA3DFORMAT_OP_CUBETEXTURE;
   }
   else if (target == PIPE_TEXTURE_3D) {
      mask.value |= SVGA3DFORMAT_OP_VOLUMETEXTURE;
   }

   return (caps.value & mask.value) == mask.value;
}


static void
svga_fence_reference(struct pipe_screen *screen,
                     struct pipe_fence_handle **ptr,
                     struct pipe_fence_handle *fence)
{
   struct svga_winsys_screen *sws = svga_screen(screen)->sws;
   sws->fence_reference(sws, ptr, fence);
}


static boolean
svga_fence_finish(struct pipe_screen *screen,
                  struct pipe_fence_handle *fence,
                  uint64_t timeout)
{
   struct svga_winsys_screen *sws = svga_screen(screen)->sws;

   if (!timeout)
      return sws->fence_signalled(sws, fence, 0) == 0;

   SVGA_DBG(DEBUG_DMA|DEBUG_PERF, "%s fence_ptr %p\n",
            __FUNCTION__, fence);

   return sws->fence_finish(sws, fence, 0) == 0;
}


static int
svga_get_driver_query_info(struct pipe_screen *screen,
                           unsigned index,
                           struct pipe_driver_query_info *info)
{
#define QUERY(NAME, ENUM, UNITS) \
   {NAME, ENUM, {0}, UNITS, PIPE_DRIVER_QUERY_RESULT_TYPE_AVERAGE, 0, 0x0}

   static const struct pipe_driver_query_info queries[] = {
      /* per-frame counters */
      QUERY("num-draw-calls", SVGA_QUERY_NUM_DRAW_CALLS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-fallbacks", SVGA_QUERY_NUM_FALLBACKS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-flushes", SVGA_QUERY_NUM_FLUSHES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-validations", SVGA_QUERY_NUM_VALIDATIONS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("map-buffer-time", SVGA_QUERY_MAP_BUFFER_TIME,
            PIPE_DRIVER_QUERY_TYPE_MICROSECONDS),
      QUERY("num-resources-mapped", SVGA_QUERY_NUM_RESOURCES_MAPPED,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-bytes-uploaded", SVGA_QUERY_NUM_BYTES_UPLOADED,
            PIPE_DRIVER_QUERY_TYPE_BYTES),

      /* running total counters */
      QUERY("memory-used", SVGA_QUERY_MEMORY_USED,
            PIPE_DRIVER_QUERY_TYPE_BYTES),
      QUERY("num-shaders", SVGA_QUERY_NUM_SHADERS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-resources", SVGA_QUERY_NUM_RESOURCES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-state-objects", SVGA_QUERY_NUM_STATE_OBJECTS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-surface-views", SVGA_QUERY_NUM_SURFACE_VIEWS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-generate-mipmap", SVGA_QUERY_NUM_GENERATE_MIPMAP,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
   };
#undef QUERY

   if (!info)
      return Elements(queries);

   if (index >= Elements(queries))
      return 0;

   *info = queries[index];
   return 1;
}


static void
svga_destroy_screen( struct pipe_screen *screen )
{
   struct svga_screen *svgascreen = svga_screen(screen);
   
   svga_screen_cache_cleanup(svgascreen);

   pipe_mutex_destroy(svgascreen->swc_mutex);
   pipe_mutex_destroy(svgascreen->tex_mutex);

   svgascreen->sws->destroy(svgascreen->sws);
   
   FREE(svgascreen);
}


/**
 * Create a new svga_screen object
 */
struct pipe_screen *
svga_screen_create(struct svga_winsys_screen *sws)
{
   struct svga_screen *svgascreen;
   struct pipe_screen *screen;

#ifdef DEBUG
   SVGA_DEBUG = debug_get_flags_option("SVGA_DEBUG", svga_debug_flags, 0 );
#endif

   svgascreen = CALLOC_STRUCT(svga_screen);
   if (!svgascreen)
      goto error1;

   svgascreen->debug.force_level_surface_view =
      debug_get_bool_option("SVGA_FORCE_LEVEL_SURFACE_VIEW", FALSE);
   svgascreen->debug.force_surface_view =
      debug_get_bool_option("SVGA_FORCE_SURFACE_VIEW", FALSE);
   svgascreen->debug.force_sampler_view =
      debug_get_bool_option("SVGA_FORCE_SAMPLER_VIEW", FALSE);
   svgascreen->debug.no_surface_view =
      debug_get_bool_option("SVGA_NO_SURFACE_VIEW", FALSE);
   svgascreen->debug.no_sampler_view =
      debug_get_bool_option("SVGA_NO_SAMPLER_VIEW", FALSE);

   screen = &svgascreen->screen;

   screen->destroy = svga_destroy_screen;
   screen->get_name = svga_get_name;
   screen->get_vendor = svga_get_vendor;
   screen->get_device_vendor = svga_get_vendor; // TODO actual device vendor
   screen->get_param = svga_get_param;
   screen->get_shader_param = svga_get_shader_param;
   screen->get_paramf = svga_get_paramf;
   screen->get_timestamp = NULL;
   screen->is_format_supported = svga_is_format_supported;
   screen->context_create = svga_context_create;
   screen->fence_reference = svga_fence_reference;
   screen->fence_finish = svga_fence_finish;
   screen->get_driver_query_info = svga_get_driver_query_info;
   svgascreen->sws = sws;

   svga_init_screen_resource_functions(svgascreen);

   if (sws->get_hw_version) {
      svgascreen->hw_version = sws->get_hw_version(sws);
   } else {
      svgascreen->hw_version = SVGA3D_HWVERSION_WS65_B1;
   }

   /*
    * The D16, D24X8, and D24S8 formats always do an implicit shadow compare
    * when sampled from, where as the DF16, DF24, and D24S8_INT do not.  So
    * we prefer the later when available.
    *
    * This mimics hardware vendors extensions for D3D depth sampling. See also
    * http://aras-p.info/texts/D3D9GPUHacks.html
    */

   {
      boolean has_df16, has_df24, has_d24s8_int;
      SVGA3dSurfaceFormatCaps caps;
      SVGA3dSurfaceFormatCaps mask;
      mask.value = 0;
      mask.zStencil = 1;
      mask.texture = 1;

      svgascreen->depth.z16 = SVGA3D_Z_D16;
      svgascreen->depth.x8z24 = SVGA3D_Z_D24X8;
      svgascreen->depth.s8z24 = SVGA3D_Z_D24S8;

      svga_get_format_cap(svgascreen, SVGA3D_Z_DF16, &caps);
      has_df16 = (caps.value & mask.value) == mask.value;

      svga_get_format_cap(svgascreen, SVGA3D_Z_DF24, &caps);
      has_df24 = (caps.value & mask.value) == mask.value;

      svga_get_format_cap(svgascreen, SVGA3D_Z_D24S8_INT, &caps);
      has_d24s8_int = (caps.value & mask.value) == mask.value;

      /* XXX: We might want some other logic here.
       * Like if we only have d24s8_int we should
       * emulate the other formats with that.
       */
      if (has_df16) {
         svgascreen->depth.z16 = SVGA3D_Z_DF16;
      }
      if (has_df24) {
         svgascreen->depth.x8z24 = SVGA3D_Z_DF24;
      }
      if (has_d24s8_int) {
         svgascreen->depth.s8z24 = SVGA3D_Z_D24S8_INT;
      }
   }

   /* Query device caps
    */
   if (sws->have_vgpu10) {
      svgascreen->haveProvokingVertex
         = get_bool_cap(sws, SVGA3D_DEVCAP_DX_PROVOKING_VERTEX, FALSE);
      svgascreen->haveLineSmooth = TRUE;
      svgascreen->maxPointSize = 80.0F;
      svgascreen->max_color_buffers = SVGA3D_DX_MAX_RENDER_TARGETS;

      /* Multisample samples per pixel */
      svgascreen->ms_samples =
         get_uint_cap(sws, SVGA3D_DEVCAP_MULTISAMPLE_MASKABLESAMPLES, 0);

      /* Maximum number of constant buffers */
      svgascreen->max_const_buffers =
         get_uint_cap(sws, SVGA3D_DEVCAP_DX_MAX_CONSTANT_BUFFERS, 1);
      assert(svgascreen->max_const_buffers <= SVGA_MAX_CONST_BUFS);
   }
   else {
      /* VGPU9 */
      unsigned vs_ver = get_uint_cap(sws, SVGA3D_DEVCAP_VERTEX_SHADER_VERSION,
                                     SVGA3DVSVERSION_NONE);
      unsigned fs_ver = get_uint_cap(sws, SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION,
                                     SVGA3DPSVERSION_NONE);

      /* we require Shader model 3.0 or later */
      if (fs_ver < SVGA3DPSVERSION_30 || vs_ver < SVGA3DVSVERSION_30) {
         goto error2;
      }

      svgascreen->haveProvokingVertex = FALSE;

      svgascreen->haveLineSmooth =
         get_bool_cap(sws, SVGA3D_DEVCAP_LINE_AA, FALSE);

      svgascreen->maxPointSize =
         get_float_cap(sws, SVGA3D_DEVCAP_MAX_POINT_SIZE, 1.0f);
      /* Keep this to a reasonable size to avoid failures in conform/pntaa.c */
      svgascreen->maxPointSize = MIN2(svgascreen->maxPointSize, 80.0f);

      /* The SVGA3D device always supports 4 targets at this time, regardless
       * of what querying SVGA3D_DEVCAP_MAX_RENDER_TARGETS might return.
       */
      svgascreen->max_color_buffers = 4;

      /* Only support one constant buffer
       */
      svgascreen->max_const_buffers = 1;

      /* No multisampling */
      svgascreen->ms_samples = 0;
   }

   /* common VGPU9 / VGPU10 caps */
   svgascreen->haveLineStipple =
      get_bool_cap(sws, SVGA3D_DEVCAP_LINE_STIPPLE, FALSE);

   svgascreen->maxLineWidth =
      get_float_cap(sws, SVGA3D_DEVCAP_MAX_LINE_WIDTH, 1.0f);

   svgascreen->maxLineWidthAA =
      get_float_cap(sws, SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH, 1.0f);

   if (0) {
      debug_printf("svga: haveProvokingVertex %u\n",
                   svgascreen->haveProvokingVertex);
      debug_printf("svga: haveLineStip %u  "
                   "haveLineSmooth %u  maxLineWidth %f\n",
                   svgascreen->haveLineStipple, svgascreen->haveLineSmooth,
                   svgascreen->maxLineWidth);
      debug_printf("svga: maxPointSize %g\n", svgascreen->maxPointSize);
   }

   pipe_mutex_init(svgascreen->tex_mutex);
   pipe_mutex_init(svgascreen->swc_mutex);

   svga_screen_cache_init(svgascreen);

   return screen;
error2:
   FREE(svgascreen);
error1:
   return NULL;
}

struct svga_winsys_screen *
svga_winsys_screen(struct pipe_screen *screen)
{
   return svga_screen(screen)->sws;
}

#ifdef DEBUG
struct svga_screen *
svga_screen(struct pipe_screen *screen)
{
   assert(screen);
   assert(screen->destroy == svga_destroy_screen);
   return (struct svga_screen *)screen;
}
#endif
