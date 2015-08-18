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

#include "util/u_dual_blend.h"
#include "util/u_dynarray.h"
#include "util/u_framebuffer.h"
#include "util/u_helpers.h"
#include "util/u_resource.h"
#include "util/u_upload_mgr.h"

#include "ilo_context.h"
#include "ilo_format.h"
#include "ilo_resource.h"
#include "ilo_shader.h"
#include "ilo_state.h"

/**
 * Translate a pipe primitive type to the matching hardware primitive type.
 */
static enum gen_3dprim_type
ilo_translate_draw_mode(unsigned mode)
{
   static const enum gen_3dprim_type prim_mapping[PIPE_PRIM_MAX] = {
      [PIPE_PRIM_POINTS]                     = GEN6_3DPRIM_POINTLIST,
      [PIPE_PRIM_LINES]                      = GEN6_3DPRIM_LINELIST,
      [PIPE_PRIM_LINE_LOOP]                  = GEN6_3DPRIM_LINELOOP,
      [PIPE_PRIM_LINE_STRIP]                 = GEN6_3DPRIM_LINESTRIP,
      [PIPE_PRIM_TRIANGLES]                  = GEN6_3DPRIM_TRILIST,
      [PIPE_PRIM_TRIANGLE_STRIP]             = GEN6_3DPRIM_TRISTRIP,
      [PIPE_PRIM_TRIANGLE_FAN]               = GEN6_3DPRIM_TRIFAN,
      [PIPE_PRIM_QUADS]                      = GEN6_3DPRIM_QUADLIST,
      [PIPE_PRIM_QUAD_STRIP]                 = GEN6_3DPRIM_QUADSTRIP,
      [PIPE_PRIM_POLYGON]                    = GEN6_3DPRIM_POLYGON,
      [PIPE_PRIM_LINES_ADJACENCY]            = GEN6_3DPRIM_LINELIST_ADJ,
      [PIPE_PRIM_LINE_STRIP_ADJACENCY]       = GEN6_3DPRIM_LINESTRIP_ADJ,
      [PIPE_PRIM_TRIANGLES_ADJACENCY]        = GEN6_3DPRIM_TRILIST_ADJ,
      [PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY]   = GEN6_3DPRIM_TRISTRIP_ADJ,
   };

   assert(prim_mapping[mode]);

   return prim_mapping[mode];
}

static enum gen_index_format
ilo_translate_index_size(unsigned index_size)
{
   switch (index_size) {
   case 1:                             return GEN6_INDEX_BYTE;
   case 2:                             return GEN6_INDEX_WORD;
   case 4:                             return GEN6_INDEX_DWORD;
   default:
      assert(!"unknown index size");
      return GEN6_INDEX_BYTE;
   }
}

static enum gen_mip_filter
ilo_translate_mip_filter(unsigned filter)
{
   switch (filter) {
   case PIPE_TEX_MIPFILTER_NEAREST:    return GEN6_MIPFILTER_NEAREST;
   case PIPE_TEX_MIPFILTER_LINEAR:     return GEN6_MIPFILTER_LINEAR;
   case PIPE_TEX_MIPFILTER_NONE:       return GEN6_MIPFILTER_NONE;
   default:
      assert(!"unknown mipfilter");
      return GEN6_MIPFILTER_NONE;
   }
}

static int
ilo_translate_img_filter(unsigned filter)
{
   switch (filter) {
   case PIPE_TEX_FILTER_NEAREST:       return GEN6_MAPFILTER_NEAREST;
   case PIPE_TEX_FILTER_LINEAR:        return GEN6_MAPFILTER_LINEAR;
   default:
      assert(!"unknown sampler filter");
      return GEN6_MAPFILTER_NEAREST;
   }
}

static enum gen_texcoord_mode
ilo_translate_address_wrap(unsigned wrap)
{
   switch (wrap) {
   case PIPE_TEX_WRAP_CLAMP:           return GEN8_TEXCOORDMODE_HALF_BORDER;
   case PIPE_TEX_WRAP_REPEAT:          return GEN6_TEXCOORDMODE_WRAP;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:   return GEN6_TEXCOORDMODE_CLAMP;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return GEN6_TEXCOORDMODE_CLAMP_BORDER;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:   return GEN6_TEXCOORDMODE_MIRROR;
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
   default:
      assert(!"unknown sampler wrap mode");
      return GEN6_TEXCOORDMODE_WRAP;
   }
}

static enum gen_aniso_ratio
ilo_translate_max_anisotropy(unsigned max_anisotropy)
{
   switch (max_anisotropy) {
   case 0: case 1: case 2:             return GEN6_ANISORATIO_2;
   case 3: case 4:                     return GEN6_ANISORATIO_4;
   case 5: case 6:                     return GEN6_ANISORATIO_6;
   case 7: case 8:                     return GEN6_ANISORATIO_8;
   case 9: case 10:                    return GEN6_ANISORATIO_10;
   case 11: case 12:                   return GEN6_ANISORATIO_12;
   case 13: case 14:                   return GEN6_ANISORATIO_14;
   default:                            return GEN6_ANISORATIO_16;
   }
}

static enum gen_prefilter_op
ilo_translate_shadow_func(unsigned func)
{
   /*
    * For PIPE_FUNC_x, the reference value is on the left-hand side of the
    * comparison, and 1.0 is returned when the comparison is true.
    *
    * For GEN6_PREFILTEROP_x, the reference value is on the right-hand side of
    * the comparison, and 0.0 is returned when the comparison is true.
    */
   switch (func) {
   case PIPE_FUNC_NEVER:               return GEN6_PREFILTEROP_ALWAYS;
   case PIPE_FUNC_LESS:                return GEN6_PREFILTEROP_LEQUAL;
   case PIPE_FUNC_EQUAL:               return GEN6_PREFILTEROP_NOTEQUAL;
   case PIPE_FUNC_LEQUAL:              return GEN6_PREFILTEROP_LESS;
   case PIPE_FUNC_GREATER:             return GEN6_PREFILTEROP_GEQUAL;
   case PIPE_FUNC_NOTEQUAL:            return GEN6_PREFILTEROP_EQUAL;
   case PIPE_FUNC_GEQUAL:              return GEN6_PREFILTEROP_GREATER;
   case PIPE_FUNC_ALWAYS:              return GEN6_PREFILTEROP_NEVER;
   default:
      assert(!"unknown shadow compare function");
      return GEN6_PREFILTEROP_NEVER;
   }
}

static enum gen_front_winding
ilo_translate_front_ccw(unsigned front_ccw)
{
   return (front_ccw) ? GEN6_FRONTWINDING_CCW : GEN6_FRONTWINDING_CW;
}

static enum gen_cull_mode
ilo_translate_cull_face(unsigned cull_face)
{
   switch (cull_face) {
   case PIPE_FACE_NONE:                return GEN6_CULLMODE_NONE;
   case PIPE_FACE_FRONT:               return GEN6_CULLMODE_FRONT;
   case PIPE_FACE_BACK:                return GEN6_CULLMODE_BACK;
   case PIPE_FACE_FRONT_AND_BACK:      return GEN6_CULLMODE_BOTH;
   default:
      assert(!"unknown face culling");
      return GEN6_CULLMODE_NONE;
   }
}

static enum gen_fill_mode
ilo_translate_poly_mode(unsigned poly_mode)
{
   switch (poly_mode) {
   case PIPE_POLYGON_MODE_FILL:        return GEN6_FILLMODE_SOLID;
   case PIPE_POLYGON_MODE_LINE:        return GEN6_FILLMODE_WIREFRAME;
   case PIPE_POLYGON_MODE_POINT:       return GEN6_FILLMODE_POINT;
   default:
      assert(!"unknown polygon mode");
      return GEN6_FILLMODE_SOLID;
   }
}

static enum gen_pixel_location
ilo_translate_half_pixel_center(bool half_pixel_center)
{
   return (half_pixel_center) ? GEN6_PIXLOC_CENTER : GEN6_PIXLOC_UL_CORNER;
}

static enum gen_compare_function
ilo_translate_compare_func(unsigned func)
{
   switch (func) {
   case PIPE_FUNC_NEVER:               return GEN6_COMPAREFUNCTION_NEVER;
   case PIPE_FUNC_LESS:                return GEN6_COMPAREFUNCTION_LESS;
   case PIPE_FUNC_EQUAL:               return GEN6_COMPAREFUNCTION_EQUAL;
   case PIPE_FUNC_LEQUAL:              return GEN6_COMPAREFUNCTION_LEQUAL;
   case PIPE_FUNC_GREATER:             return GEN6_COMPAREFUNCTION_GREATER;
   case PIPE_FUNC_NOTEQUAL:            return GEN6_COMPAREFUNCTION_NOTEQUAL;
   case PIPE_FUNC_GEQUAL:              return GEN6_COMPAREFUNCTION_GEQUAL;
   case PIPE_FUNC_ALWAYS:              return GEN6_COMPAREFUNCTION_ALWAYS;
   default:
      assert(!"unknown compare function");
      return GEN6_COMPAREFUNCTION_NEVER;
   }
}

static enum gen_stencil_op
ilo_translate_stencil_op(unsigned stencil_op)
{
   switch (stencil_op) {
   case PIPE_STENCIL_OP_KEEP:          return GEN6_STENCILOP_KEEP;
   case PIPE_STENCIL_OP_ZERO:          return GEN6_STENCILOP_ZERO;
   case PIPE_STENCIL_OP_REPLACE:       return GEN6_STENCILOP_REPLACE;
   case PIPE_STENCIL_OP_INCR:          return GEN6_STENCILOP_INCRSAT;
   case PIPE_STENCIL_OP_DECR:          return GEN6_STENCILOP_DECRSAT;
   case PIPE_STENCIL_OP_INCR_WRAP:     return GEN6_STENCILOP_INCR;
   case PIPE_STENCIL_OP_DECR_WRAP:     return GEN6_STENCILOP_DECR;
   case PIPE_STENCIL_OP_INVERT:        return GEN6_STENCILOP_INVERT;
   default:
      assert(!"unknown stencil op");
      return GEN6_STENCILOP_KEEP;
   }
}

static enum gen_logic_op
ilo_translate_logicop(unsigned logicop)
{
   switch (logicop) {
   case PIPE_LOGICOP_CLEAR:            return GEN6_LOGICOP_CLEAR;
   case PIPE_LOGICOP_NOR:              return GEN6_LOGICOP_NOR;
   case PIPE_LOGICOP_AND_INVERTED:     return GEN6_LOGICOP_AND_INVERTED;
   case PIPE_LOGICOP_COPY_INVERTED:    return GEN6_LOGICOP_COPY_INVERTED;
   case PIPE_LOGICOP_AND_REVERSE:      return GEN6_LOGICOP_AND_REVERSE;
   case PIPE_LOGICOP_INVERT:           return GEN6_LOGICOP_INVERT;
   case PIPE_LOGICOP_XOR:              return GEN6_LOGICOP_XOR;
   case PIPE_LOGICOP_NAND:             return GEN6_LOGICOP_NAND;
   case PIPE_LOGICOP_AND:              return GEN6_LOGICOP_AND;
   case PIPE_LOGICOP_EQUIV:            return GEN6_LOGICOP_EQUIV;
   case PIPE_LOGICOP_NOOP:             return GEN6_LOGICOP_NOOP;
   case PIPE_LOGICOP_OR_INVERTED:      return GEN6_LOGICOP_OR_INVERTED;
   case PIPE_LOGICOP_COPY:             return GEN6_LOGICOP_COPY;
   case PIPE_LOGICOP_OR_REVERSE:       return GEN6_LOGICOP_OR_REVERSE;
   case PIPE_LOGICOP_OR:               return GEN6_LOGICOP_OR;
   case PIPE_LOGICOP_SET:              return GEN6_LOGICOP_SET;
   default:
      assert(!"unknown logicop function");
      return GEN6_LOGICOP_CLEAR;
   }
}

static int
ilo_translate_blend_func(unsigned blend)
{
   switch (blend) {
   case PIPE_BLEND_ADD:                return GEN6_BLENDFUNCTION_ADD;
   case PIPE_BLEND_SUBTRACT:           return GEN6_BLENDFUNCTION_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT:   return GEN6_BLENDFUNCTION_REVERSE_SUBTRACT;
   case PIPE_BLEND_MIN:                return GEN6_BLENDFUNCTION_MIN;
   case PIPE_BLEND_MAX:                return GEN6_BLENDFUNCTION_MAX;
   default:
      assert(!"unknown blend function");
      return GEN6_BLENDFUNCTION_ADD;
   }
}

static int
ilo_translate_blend_factor(unsigned factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_ONE:                return GEN6_BLENDFACTOR_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR:          return GEN6_BLENDFACTOR_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA:          return GEN6_BLENDFACTOR_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_ALPHA:          return GEN6_BLENDFACTOR_DST_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR:          return GEN6_BLENDFACTOR_DST_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return GEN6_BLENDFACTOR_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_CONST_COLOR:        return GEN6_BLENDFACTOR_CONST_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA:        return GEN6_BLENDFACTOR_CONST_ALPHA;
   case PIPE_BLENDFACTOR_SRC1_COLOR:         return GEN6_BLENDFACTOR_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA:         return GEN6_BLENDFACTOR_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_ZERO:               return GEN6_BLENDFACTOR_ZERO;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:      return GEN6_BLENDFACTOR_INV_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:      return GEN6_BLENDFACTOR_INV_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:      return GEN6_BLENDFACTOR_INV_DST_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:      return GEN6_BLENDFACTOR_INV_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:    return GEN6_BLENDFACTOR_INV_CONST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:    return GEN6_BLENDFACTOR_INV_CONST_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:     return GEN6_BLENDFACTOR_INV_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:     return GEN6_BLENDFACTOR_INV_SRC1_ALPHA;
   default:
      assert(!"unknown blend factor");
      return GEN6_BLENDFACTOR_ONE;
   }
}

static void
finalize_shader_states(struct ilo_state_vector *vec)
{
   unsigned type;

   for (type = 0; type < PIPE_SHADER_TYPES; type++) {
      struct ilo_shader_state *shader;
      uint32_t state;

      switch (type) {
      case PIPE_SHADER_VERTEX:
         shader = vec->vs;
         state = ILO_DIRTY_VS;
         break;
      case PIPE_SHADER_GEOMETRY:
         shader = vec->gs;
         state = ILO_DIRTY_GS;
         break;
      case PIPE_SHADER_FRAGMENT:
         shader = vec->fs;
         state = ILO_DIRTY_FS;
         break;
      default:
         shader = NULL;
         state = 0;
         break;
      }

      if (!shader)
         continue;

      /* compile if the shader or the states it depends on changed */
      if (vec->dirty & state) {
         ilo_shader_select_kernel(shader, vec, ILO_DIRTY_ALL);
      }
      else if (ilo_shader_select_kernel(shader, vec, vec->dirty)) {
         /* mark the state dirty if a new kernel is selected */
         vec->dirty |= state;
      }

      /* need to setup SBE for FS */
      if (type == PIPE_SHADER_FRAGMENT && vec->dirty &
            (state | ILO_DIRTY_GS | ILO_DIRTY_VS | ILO_DIRTY_RASTERIZER)) {
         if (ilo_shader_select_kernel_sbe(shader,
               (vec->gs) ? vec->gs : vec->vs, vec->rasterizer))
            vec->dirty |= state;
      }
   }
}

static void
finalize_cbuf_state(struct ilo_context *ilo,
                    struct ilo_cbuf_state *cbuf,
                    const struct ilo_shader_state *sh)
{
   uint32_t upload_mask = cbuf->enabled_mask;

   /* skip CBUF0 if the kernel does not need it */
   upload_mask &=
      ~ilo_shader_get_kernel_param(sh, ILO_KERNEL_SKIP_CBUF0_UPLOAD);

   while (upload_mask) {
      unsigned offset, i;

      i = u_bit_scan(&upload_mask);
      /* no need to upload */
      if (cbuf->cso[i].resource)
         continue;

      u_upload_data(ilo->uploader, 0, cbuf->cso[i].info.size,
            cbuf->cso[i].user_buffer, &offset, &cbuf->cso[i].resource);

      cbuf->cso[i].info.vma = ilo_resource_get_vma(cbuf->cso[i].resource);
      cbuf->cso[i].info.offset = offset;

      memset(&cbuf->cso[i].surface, 0, sizeof(cbuf->cso[i].surface));
      ilo_state_surface_init_for_buffer(&cbuf->cso[i].surface,
            ilo->dev, &cbuf->cso[i].info);

      ilo->state_vector.dirty |= ILO_DIRTY_CBUF;
   }
}

static void
finalize_constant_buffers(struct ilo_context *ilo)
{
   struct ilo_state_vector *vec = &ilo->state_vector;

   if (vec->dirty & (ILO_DIRTY_CBUF | ILO_DIRTY_VS))
      finalize_cbuf_state(ilo, &vec->cbuf[PIPE_SHADER_VERTEX], vec->vs);

   if (ilo->state_vector.dirty & (ILO_DIRTY_CBUF | ILO_DIRTY_FS))
      finalize_cbuf_state(ilo, &vec->cbuf[PIPE_SHADER_FRAGMENT], vec->fs);
}

static void
finalize_index_buffer(struct ilo_context *ilo)
{
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;
   const bool need_upload = (vec->draw->indexed &&
         (vec->ib.state.user_buffer ||
          vec->ib.state.offset % vec->ib.state.index_size));
   struct pipe_resource *current_hw_res = NULL;
   struct ilo_state_index_buffer_info info;
   int64_t vertex_start_bias = 0;

   if (!(vec->dirty & ILO_DIRTY_IB) && !need_upload)
      return;

   /* make sure vec->ib.hw_resource changes when reallocated */
   pipe_resource_reference(&current_hw_res, vec->ib.hw_resource);

   if (need_upload) {
      const unsigned offset = vec->ib.state.index_size * vec->draw->start;
      const unsigned size = vec->ib.state.index_size * vec->draw->count;
      unsigned hw_offset;

      if (vec->ib.state.user_buffer) {
         u_upload_data(ilo->uploader, 0, size,
               vec->ib.state.user_buffer + offset,
               &hw_offset, &vec->ib.hw_resource);
      } else {
         u_upload_buffer(ilo->uploader, 0,
               vec->ib.state.offset + offset, size, vec->ib.state.buffer,
               &hw_offset, &vec->ib.hw_resource);
      }

      /* the HW offset should be aligned */
      assert(hw_offset % vec->ib.state.index_size == 0);
      vertex_start_bias = hw_offset / vec->ib.state.index_size;

      /*
       * INDEX[vec->draw->start] in the original buffer is INDEX[0] in the HW
       * resource
       */
      vertex_start_bias -= vec->draw->start;
   } else {
      pipe_resource_reference(&vec->ib.hw_resource, vec->ib.state.buffer);

      /* note that index size may be zero when the draw is not indexed */
      if (vec->draw->indexed)
         vertex_start_bias = vec->ib.state.offset / vec->ib.state.index_size;
   }

   vec->draw_info.vertex_start += vertex_start_bias;

   /* treat the IB as clean if the HW states do not change */
   if (vec->ib.hw_resource == current_hw_res &&
       vec->ib.hw_index_size == vec->ib.state.index_size)
      vec->dirty &= ~ILO_DIRTY_IB;
   else
      vec->ib.hw_index_size = vec->ib.state.index_size;

   pipe_resource_reference(&current_hw_res, NULL);

   memset(&info, 0, sizeof(info));
   if (vec->ib.hw_resource) {
      info.vma = ilo_resource_get_vma(vec->ib.hw_resource);
      info.size = info.vma->vm_size;
      info.format = ilo_translate_index_size(vec->ib.hw_index_size);
   }

   ilo_state_index_buffer_set_info(&vec->ib.ib, dev, &info);
}

static void
finalize_vertex_elements(struct ilo_context *ilo)
{
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;
   struct ilo_ve_state *ve = vec->ve;
   const bool last_element_edge_flag = (vec->vs &&
         ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_VS_INPUT_EDGEFLAG));
   const bool prepend_vertexid = (vec->vs &&
         ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_VS_INPUT_VERTEXID));
   const bool prepend_instanceid = (vec->vs &&
         ilo_shader_get_kernel_param(vec->vs,
            ILO_KERNEL_VS_INPUT_INSTANCEID));
   const enum gen_index_format index_format = (vec->draw->indexed) ?
      ilo_translate_index_size(vec->ib.state.index_size) : GEN6_INDEX_DWORD;

   /* check for non-orthogonal states */
   if (ve->vf_params.cv_topology != vec->draw_info.topology ||
       ve->vf_params.prepend_vertexid != prepend_vertexid ||
       ve->vf_params.prepend_instanceid != prepend_instanceid ||
       ve->vf_params.last_element_edge_flag != last_element_edge_flag ||
       ve->vf_params.cv_index_format != index_format ||
       ve->vf_params.cut_index_enable != vec->draw->primitive_restart ||
       ve->vf_params.cut_index != vec->draw->restart_index) {
      ve->vf_params.cv_topology = vec->draw_info.topology;
      ve->vf_params.prepend_vertexid = prepend_vertexid;
      ve->vf_params.prepend_instanceid = prepend_instanceid;
      ve->vf_params.last_element_edge_flag = last_element_edge_flag;
      ve->vf_params.cv_index_format = index_format;
      ve->vf_params.cut_index_enable = vec->draw->primitive_restart;
      ve->vf_params.cut_index = vec->draw->restart_index;

      ilo_state_vf_set_params(&ve->vf, dev, &ve->vf_params);

      vec->dirty |= ILO_DIRTY_VE;
   }
}

static void
finalize_vertex_buffers(struct ilo_context *ilo)
{
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;
   struct ilo_state_vertex_buffer_info info;
   unsigned i;

   if (!(vec->dirty & (ILO_DIRTY_VE | ILO_DIRTY_VB)))
      return;

   memset(&info, 0, sizeof(info));

   for (i = 0; i < vec->ve->vb_count; i++) {
      const unsigned pipe_idx = vec->ve->vb_mapping[i];
      const struct pipe_vertex_buffer *cso = &vec->vb.states[pipe_idx];

      if (cso->buffer) {
         info.vma = ilo_resource_get_vma(cso->buffer);
         info.offset = cso->buffer_offset;
         info.size = info.vma->vm_size - cso->buffer_offset;

         info.stride = cso->stride;
      } else {
         memset(&info, 0, sizeof(info));
      }

      ilo_state_vertex_buffer_set_info(&vec->vb.vb[i], dev, &info);
   }
}

static void
finalize_urb(struct ilo_context *ilo)
{
   const uint16_t attr_size = sizeof(uint32_t) * 4;
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;
   struct ilo_state_urb_info info;

   if (!(vec->dirty & (ILO_DIRTY_VE | ILO_DIRTY_VS |
                       ILO_DIRTY_GS | ILO_DIRTY_FS)))
      return;

   memset(&info, 0, sizeof(info));

   info.ve_entry_size = attr_size * ilo_state_vf_get_attr_count(&vec->ve->vf);

   if (vec->vs) {
      info.vs_const_data = (bool)
         (ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_PCB_CBUF0_SIZE) +
          ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_VS_PCB_UCP_SIZE));
      info.vs_entry_size = attr_size *
         ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_OUTPUT_COUNT);
   }

   if (vec->gs) {
      info.gs_const_data = (bool)
         ilo_shader_get_kernel_param(vec->gs, ILO_KERNEL_PCB_CBUF0_SIZE);

      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 189:
       *
       *     "All outputs of a GS thread will be stored in the single GS
       *      thread output URB entry."
       *
       * TODO
       */
      info.gs_entry_size = attr_size *
         ilo_shader_get_kernel_param(vec->gs, ILO_KERNEL_OUTPUT_COUNT);
   }

   if (vec->fs) {
      info.ps_const_data = (bool)
         ilo_shader_get_kernel_param(vec->fs, ILO_KERNEL_PCB_CBUF0_SIZE);
   }

   ilo_state_urb_set_info(&vec->urb, dev, &info);
}

static void
finalize_viewport(struct ilo_context *ilo)
{
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;

   if (vec->dirty & ILO_DIRTY_VIEWPORT) {
      ilo_state_viewport_set_params(&vec->viewport.vp,
            dev, &vec->viewport.params, false);
   } else if (vec->dirty & ILO_DIRTY_SCISSOR) {
      ilo_state_viewport_set_params(&vec->viewport.vp,
            dev, &vec->viewport.params, true);
      vec->dirty |= ILO_DIRTY_VIEWPORT;
   }
}

static bool
can_enable_gb_test(const struct ilo_rasterizer_state *rasterizer,
                   const struct ilo_viewport_state *viewport,
                   const struct ilo_fb_state *fb)
{
   unsigned i;

   /*
    * There are several reasons that guard band test should be disabled
    *
    *  - GL wide points (to avoid partially visibie object)
    *  - GL wide or AA lines (to avoid partially visibie object)
    *  - missing 2D clipping
    */
   if (rasterizer->state.point_size_per_vertex ||
       rasterizer->state.point_size > 1.0f ||
       rasterizer->state.line_width > 1.0f ||
       rasterizer->state.line_smooth)
      return false;

   for (i = 0; i < viewport->params.count; i++) {
      const struct ilo_state_viewport_matrix_info *mat =
         &viewport->matrices[i];
      float min_x, max_x, min_y, max_y;

      min_x = -1.0f * fabsf(mat->scale[0]) + mat->translate[0];
      max_x =  1.0f * fabsf(mat->scale[0]) + mat->translate[0];
      min_y = -1.0f * fabsf(mat->scale[1]) + mat->translate[1];
      max_y =  1.0f * fabsf(mat->scale[1]) + mat->translate[1];

      if (min_x > 0.0f || max_x < fb->state.width ||
          min_y > 0.0f || max_y < fb->state.height)
         return false;
   }

   return true;
}

static void
finalize_rasterizer(struct ilo_context *ilo)
{
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;
   struct ilo_rasterizer_state *rasterizer = vec->rasterizer;
   struct ilo_state_raster_info *info = &vec->rasterizer->info;
   const bool gb_test_enable =
      can_enable_gb_test(rasterizer, &vec->viewport, &vec->fb);
   const bool multisample =
      (rasterizer->state.multisample && vec->fb.num_samples > 1);
   const uint8_t barycentric_interps = ilo_shader_get_kernel_param(vec->fs,
         ILO_KERNEL_FS_BARYCENTRIC_INTERPOLATIONS);

   /* check for non-orthogonal states */
   if (info->clip.viewport_count != vec->viewport.params.count ||
       info->clip.gb_test_enable != gb_test_enable ||
       info->setup.msaa_enable != multisample ||
       info->setup.line_msaa_enable != multisample ||
       info->tri.depth_offset_format != vec->fb.depth_offset_format ||
       info->scan.sample_count != vec->fb.num_samples ||
       info->scan.sample_mask != vec->sample_mask ||
       info->scan.barycentric_interps != barycentric_interps ||
       info->params.any_integer_rt != vec->fb.has_integer_rt ||
       info->params.hiz_enable != vec->fb.has_hiz) {
      info->clip.viewport_count = vec->viewport.params.count;
      info->clip.gb_test_enable = gb_test_enable;
      info->setup.msaa_enable = multisample;
      info->setup.line_msaa_enable = multisample;
      info->tri.depth_offset_format = vec->fb.depth_offset_format;
      info->scan.sample_count = vec->fb.num_samples;
      info->scan.sample_mask = vec->sample_mask;
      info->scan.barycentric_interps = barycentric_interps;
      info->params.any_integer_rt = vec->fb.has_integer_rt;
      info->params.hiz_enable = vec->fb.has_hiz;

      ilo_state_raster_set_info(&rasterizer->rs, dev, &rasterizer->info);

      vec->dirty |= ILO_DIRTY_RASTERIZER;
   }
}

static bool
finalize_blend_rt(struct ilo_context *ilo)
{
   struct ilo_state_vector *vec = &ilo->state_vector;
   const struct ilo_fb_state *fb = &vec->fb;
   struct ilo_blend_state *blend = vec->blend;
   struct ilo_state_cc_blend_info *info = &vec->blend->info.blend;
   bool changed = false;
   unsigned i;

   if (!(vec->dirty & (ILO_DIRTY_FB | ILO_DIRTY_BLEND)))
      return false;

   /* set up one for dummy RT writes */
   if (!fb->state.nr_cbufs) {
      if (info->rt != &blend->dummy_rt) {
         info->rt = &blend->dummy_rt;
         info->rt_count = 1;
         changed = true;
      }

      return changed;
   }

   if (info->rt != blend->effective_rt ||
       info->rt_count != fb->state.nr_cbufs) {
      info->rt = blend->effective_rt;
      info->rt_count = fb->state.nr_cbufs;
      changed = true;
   }

   for (i = 0; i < fb->state.nr_cbufs; i++) {
      const struct ilo_fb_blend_caps *caps = &fb->blend_caps[i];
      struct ilo_state_cc_blend_rt_info *rt = &blend->effective_rt[i];
      /* ignore logicop when not UNORM */
      const bool logicop_enable =
         (blend->rt[i].logicop_enable && caps->is_unorm);

      if (rt->cv_is_unorm != caps->is_unorm ||
          rt->cv_is_integer != caps->is_integer ||
          rt->logicop_enable != logicop_enable ||
          rt->force_dst_alpha_one != caps->force_dst_alpha_one) {
         rt->cv_is_unorm = caps->is_unorm;
         rt->cv_is_integer = caps->is_integer;
         rt->logicop_enable = logicop_enable;
         rt->force_dst_alpha_one = caps->force_dst_alpha_one;

         changed = true;
      }
   }

   return changed;
}

static void
finalize_blend(struct ilo_context *ilo)
{
   const struct ilo_dev *dev = ilo->dev;
   struct ilo_state_vector *vec = &ilo->state_vector;
   struct ilo_blend_state *blend = vec->blend;
   struct ilo_state_cc_info *info = &blend->info;
   const bool sample_count_one = (vec->fb.num_samples <= 1);
   const bool float_source0_alpha =
      (!vec->fb.state.nr_cbufs || !vec->fb.state.cbufs[0] ||
       !util_format_is_pure_integer(vec->fb.state.cbufs[0]->format));

   /* check for non-orthogonal states */
   if (finalize_blend_rt(ilo) ||
       info->alpha.cv_sample_count_one != sample_count_one ||
       info->alpha.cv_float_source0_alpha != float_source0_alpha ||
       info->alpha.test_enable != vec->dsa->alpha_test ||
       info->alpha.test_func != vec->dsa->alpha_func ||
       memcmp(&info->stencil, &vec->dsa->stencil, sizeof(info->stencil)) ||
       memcmp(&info->depth, &vec->dsa->depth, sizeof(info->depth)) ||
       memcmp(&info->params, &vec->cc_params, sizeof(info->params))) {
      info->alpha.cv_sample_count_one = sample_count_one;
      info->alpha.cv_float_source0_alpha = float_source0_alpha;
      info->alpha.test_enable = vec->dsa->alpha_test;
      info->alpha.test_func = vec->dsa->alpha_func;
      info->stencil = vec->dsa->stencil;
      info->depth = vec->dsa->depth;
      info->params = vec->cc_params;

      ilo_state_cc_set_info(&blend->cc, dev, info);

      blend->alpha_may_kill = (info->alpha.alpha_to_coverage ||
                               info->alpha.test_enable);

      vec->dirty |= ILO_DIRTY_BLEND;
   }
}

/**
 * Finalize states.  Some states depend on other states and are
 * incomplete/invalid until finalized.
 */
void
ilo_finalize_3d_states(struct ilo_context *ilo,
                       const struct pipe_draw_info *draw)
{
   ilo->state_vector.draw = draw;

   ilo->state_vector.draw_info.topology = ilo_translate_draw_mode(draw->mode);
   ilo->state_vector.draw_info.indexed = draw->indexed;
   ilo->state_vector.draw_info.vertex_count = draw->count;
   ilo->state_vector.draw_info.vertex_start = draw->start;
   ilo->state_vector.draw_info.instance_count = draw->instance_count;
   ilo->state_vector.draw_info.instance_start = draw->start_instance;
   ilo->state_vector.draw_info.vertex_base = draw->index_bias;

   finalize_blend(ilo);
   finalize_shader_states(&ilo->state_vector);
   finalize_constant_buffers(ilo);
   finalize_index_buffer(ilo);
   finalize_vertex_elements(ilo);
   finalize_vertex_buffers(ilo);

   finalize_urb(ilo);
   finalize_rasterizer(ilo);
   finalize_viewport(ilo);

   u_upload_unmap(ilo->uploader);
}

static void
finalize_global_binding(struct ilo_state_vector *vec)
{
   struct ilo_shader_state *cs = vec->cs;
   int base, count, shift;
   int i;

   count = ilo_shader_get_kernel_param(cs,
         ILO_KERNEL_CS_SURFACE_GLOBAL_COUNT);
   if (!count)
      return;

   base = ilo_shader_get_kernel_param(cs, ILO_KERNEL_CS_SURFACE_GLOBAL_BASE);
   shift = 32 - util_last_bit(base + count - 1);

   if (count > vec->global_binding.count)
      count = vec->global_binding.count;

   for (i = 0; i < count; i++) {
      struct ilo_global_binding_cso *cso =
         util_dynarray_element(&vec->global_binding.bindings,
               struct ilo_global_binding_cso, i);
      const uint32_t offset = *cso->handle & ((1 << shift) - 1);

      *cso->handle = ((base + i) << shift) | offset;
   }
}

void
ilo_finalize_compute_states(struct ilo_context *ilo)
{
   finalize_global_binding(&ilo->state_vector);
}

static void *
ilo_create_blend_state(struct pipe_context *pipe,
                       const struct pipe_blend_state *state)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_state_cc_info *info;
   struct ilo_blend_state *blend;
   int i;

   blend = CALLOC_STRUCT(ilo_blend_state);
   assert(blend);

   info = &blend->info;

   info->alpha.cv_float_source0_alpha = true;
   info->alpha.cv_sample_count_one = true;
   info->alpha.alpha_to_one = state->alpha_to_one;
   info->alpha.alpha_to_coverage = state->alpha_to_coverage;
   info->alpha.test_enable = false;
   info->alpha.test_func = GEN6_COMPAREFUNCTION_ALWAYS;

   info->stencil.cv_has_buffer = true;
   info->depth.cv_has_buffer= true;

   info->blend.rt = blend->effective_rt;
   info->blend.rt_count = 1;
   info->blend.dither_enable = state->dither;

   for (i = 0; i < ARRAY_SIZE(blend->rt); i++) {
      const struct pipe_rt_blend_state *rt = &state->rt[i];
      struct ilo_state_cc_blend_rt_info *rt_info = &blend->rt[i];

      rt_info->cv_has_buffer = true;
      rt_info->cv_is_unorm = true;
      rt_info->cv_is_integer = false;

      /* logic op takes precedence over blending */
      if (state->logicop_enable) {
         rt_info->logicop_enable = true;
         rt_info->logicop_func = ilo_translate_logicop(state->logicop_func);
      } else if (rt->blend_enable) {
         rt_info->blend_enable = true;

         rt_info->rgb_src = ilo_translate_blend_factor(rt->rgb_src_factor);
         rt_info->rgb_dst = ilo_translate_blend_factor(rt->rgb_dst_factor);
         rt_info->rgb_func = ilo_translate_blend_func(rt->rgb_func);

         rt_info->a_src = ilo_translate_blend_factor(rt->alpha_src_factor);
         rt_info->a_dst = ilo_translate_blend_factor(rt->alpha_dst_factor);
         rt_info->a_func = ilo_translate_blend_func(rt->alpha_func);
      }

      if (!(rt->colormask & PIPE_MASK_A))
         rt_info->argb_write_disables |= (1 << 3);
      if (!(rt->colormask & PIPE_MASK_R))
         rt_info->argb_write_disables |= (1 << 2);
      if (!(rt->colormask & PIPE_MASK_G))
         rt_info->argb_write_disables |= (1 << 1);
      if (!(rt->colormask & PIPE_MASK_B))
         rt_info->argb_write_disables |= (1 << 0);

      if (!state->independent_blend_enable) {
         for (i = 1; i < ARRAY_SIZE(blend->rt); i++)
            blend->rt[i] = *rt_info;
         break;
      }
   }

   memcpy(blend->effective_rt, blend->rt, sizeof(blend->rt));

   blend->dummy_rt.argb_write_disables = 0xf;

   if (!ilo_state_cc_init(&blend->cc, dev, &blend->info)) {
      FREE(blend);
      return NULL;
   }

   blend->dual_blend = util_blend_state_is_dual(state, 0);

   return blend;
}

static void
ilo_bind_blend_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->blend = state;

   vec->dirty |= ILO_DIRTY_BLEND;
}

static void
ilo_delete_blend_state(struct pipe_context *pipe, void  *state)
{
   FREE(state);
}

static void *
ilo_create_sampler_state(struct pipe_context *pipe,
                         const struct pipe_sampler_state *state)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_sampler_cso *sampler;
   struct ilo_state_sampler_info info;
   struct ilo_state_sampler_border_info border;

   sampler = CALLOC_STRUCT(ilo_sampler_cso);
   assert(sampler);

   memset(&info, 0, sizeof(info));

   info.non_normalized = !state->normalized_coords;
   if (state->normalized_coords) {
      info.lod_bias = state->lod_bias;
      info.min_lod = state->min_lod;
      info.max_lod = state->max_lod;

      info.mip_filter = ilo_translate_mip_filter(state->min_mip_filter);
   } else {
      /* work around a bug in util_blitter */
      info.mip_filter = GEN6_MIPFILTER_NONE;
   }

   if (state->max_anisotropy) {
      info.min_filter = GEN6_MAPFILTER_ANISOTROPIC;
      info.mag_filter = GEN6_MAPFILTER_ANISOTROPIC;
   } else {
      info.min_filter = ilo_translate_img_filter(state->min_img_filter);
      info.mag_filter = ilo_translate_img_filter(state->mag_img_filter);
   }

   info.max_anisotropy = ilo_translate_max_anisotropy(state->max_anisotropy);

   /* use LOD 0 when no mipmapping (see sampler_set_gen6_SAMPLER_STATE()) */
   if (info.mip_filter == GEN6_MIPFILTER_NONE && info.min_lod > 0.0f) {
      info.min_lod = 0.0f;
      info.mag_filter = info.min_filter;
   }

   if (state->seamless_cube_map) {
      if (state->min_img_filter == PIPE_TEX_FILTER_NEAREST ||
          state->mag_img_filter == PIPE_TEX_FILTER_NEAREST) {
         info.tcx_ctrl = GEN6_TEXCOORDMODE_CLAMP;
         info.tcy_ctrl = GEN6_TEXCOORDMODE_CLAMP;
         info.tcz_ctrl = GEN6_TEXCOORDMODE_CLAMP;
      } else {
         info.tcx_ctrl = GEN6_TEXCOORDMODE_CUBE;
         info.tcy_ctrl = GEN6_TEXCOORDMODE_CUBE;
         info.tcz_ctrl = GEN6_TEXCOORDMODE_CUBE;
      }
   } else {
      info.tcx_ctrl = ilo_translate_address_wrap(state->wrap_s);
      info.tcy_ctrl = ilo_translate_address_wrap(state->wrap_t);
      info.tcz_ctrl = ilo_translate_address_wrap(state->wrap_r);

      if (ilo_dev_gen(dev) < ILO_GEN(8)) {
         /*
          * For nearest filtering, PIPE_TEX_WRAP_CLAMP means
          * PIPE_TEX_WRAP_CLAMP_TO_EDGE;  for linear filtering,
          * PIPE_TEX_WRAP_CLAMP means PIPE_TEX_WRAP_CLAMP_TO_BORDER while
          * additionally clamping the texture coordinates to [0.0, 1.0].
          *
          * PIPE_TEX_WRAP_CLAMP is not supported natively until Gen8.  The
          * clamping has to be taken care of in the shaders.  There are two
          * filters here, but let the minification one has a say.
          */
         const bool clamp_is_to_edge =
            (state->min_img_filter == PIPE_TEX_FILTER_NEAREST);

         if (clamp_is_to_edge) {
            if (info.tcx_ctrl == GEN8_TEXCOORDMODE_HALF_BORDER)
               info.tcx_ctrl = GEN6_TEXCOORDMODE_CLAMP;
            if (info.tcy_ctrl == GEN8_TEXCOORDMODE_HALF_BORDER)
               info.tcy_ctrl = GEN6_TEXCOORDMODE_CLAMP;
            if (info.tcz_ctrl == GEN8_TEXCOORDMODE_HALF_BORDER)
               info.tcz_ctrl = GEN6_TEXCOORDMODE_CLAMP;
         } else {
            if (info.tcx_ctrl == GEN8_TEXCOORDMODE_HALF_BORDER) {
               info.tcx_ctrl = GEN6_TEXCOORDMODE_CLAMP_BORDER;
               sampler->saturate_s = true;
            }
            if (info.tcy_ctrl == GEN8_TEXCOORDMODE_HALF_BORDER) {
               info.tcy_ctrl = GEN6_TEXCOORDMODE_CLAMP_BORDER;
               sampler->saturate_t = true;
            }
            if (info.tcz_ctrl == GEN8_TEXCOORDMODE_HALF_BORDER) {
               info.tcz_ctrl = GEN6_TEXCOORDMODE_CLAMP_BORDER;
               sampler->saturate_r = true;
            }
         }
      }
   }

   if (state->compare_mode == PIPE_TEX_COMPARE_R_TO_TEXTURE)
      info.shadow_func = ilo_translate_shadow_func(state->compare_func);

   ilo_state_sampler_init(&sampler->sampler, dev, &info);

   memset(&border, 0, sizeof(border));
   memcpy(border.rgba.f, state->border_color.f, sizeof(border.rgba.f));

   ilo_state_sampler_border_init(&sampler->border, dev, &border);

   return sampler;
}

static void
ilo_bind_sampler_states(struct pipe_context *pipe, unsigned shader,
                        unsigned start, unsigned count, void **samplers)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_sampler_state *dst = &vec->sampler[shader];
   bool changed = false;
   unsigned i;

   assert(start + count <= Elements(dst->cso));

   if (samplers) {
      for (i = 0; i < count; i++) {
         if (dst->cso[start + i] != samplers[i]) {
            dst->cso[start + i] = samplers[i];

            /*
             * This function is sometimes called to reduce the number of bound
             * samplers.  Do not consider that as a state change (and create a
             * new array of SAMPLER_STATE).
             */
            if (samplers[i])
               changed = true;
         }
      }
   }
   else {
      for (i = 0; i < count; i++)
         dst->cso[start + i] = NULL;
   }

   if (changed) {
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         vec->dirty |= ILO_DIRTY_SAMPLER_VS;
         break;
      case PIPE_SHADER_GEOMETRY:
         vec->dirty |= ILO_DIRTY_SAMPLER_GS;
         break;
      case PIPE_SHADER_FRAGMENT:
         vec->dirty |= ILO_DIRTY_SAMPLER_FS;
         break;
      case PIPE_SHADER_COMPUTE:
         vec->dirty |= ILO_DIRTY_SAMPLER_CS;
         break;
      }
   }
}

static void
ilo_delete_sampler_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void *
ilo_create_rasterizer_state(struct pipe_context *pipe,
                            const struct pipe_rasterizer_state *state)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_rasterizer_state *rast;
   struct ilo_state_raster_info *info;

   rast = CALLOC_STRUCT(ilo_rasterizer_state);
   assert(rast);

   rast->state = *state;

   info = &rast->info;

   info->clip.clip_enable = true;
   info->clip.stats_enable = true;
   info->clip.viewport_count = 1;
   info->clip.force_rtaindex_zero = true;
   info->clip.user_clip_enables = state->clip_plane_enable;
   info->clip.gb_test_enable = true;
   info->clip.xy_test_enable = true;
   info->clip.z_far_enable = state->depth_clip;
   info->clip.z_near_enable = state->depth_clip;
   info->clip.z_near_zero = state->clip_halfz;

   info->setup.first_vertex_provoking = state->flatshade_first;
   info->setup.viewport_transform = true;
   info->setup.scissor_enable = state->scissor;
   info->setup.msaa_enable = false;
   info->setup.line_msaa_enable = false;
   info->point.aa_enable = state->point_smooth;
   info->point.programmable_width = state->point_size_per_vertex;
   info->line.aa_enable = state->line_smooth;
   info->line.stipple_enable = state->line_stipple_enable;
   info->line.giq_enable = true;
   info->line.giq_last_pixel = state->line_last_pixel;
   info->tri.front_winding = ilo_translate_front_ccw(state->front_ccw);
   info->tri.cull_mode = ilo_translate_cull_face(state->cull_face);
   info->tri.fill_mode_front = ilo_translate_poly_mode(state->fill_front);
   info->tri.fill_mode_back = ilo_translate_poly_mode(state->fill_back);
   info->tri.depth_offset_format = GEN6_ZFORMAT_D24_UNORM_X8_UINT;
   info->tri.depth_offset_solid = state->offset_tri;
   info->tri.depth_offset_wireframe = state->offset_line;
   info->tri.depth_offset_point = state->offset_point;
   info->tri.poly_stipple_enable = state->poly_stipple_enable;

   info->scan.stats_enable = true;
   info->scan.sample_count = 1;
   info->scan.pixloc =
      ilo_translate_half_pixel_center(state->half_pixel_center);
   info->scan.sample_mask = ~0u;
   info->scan.zw_interp = GEN6_ZW_INTERP_PIXEL;
   info->scan.barycentric_interps = GEN6_INTERP_PERSPECTIVE_PIXEL;
   info->scan.earlyz_control = GEN7_EDSC_NORMAL;
   info->scan.earlyz_op = ILO_STATE_RASTER_EARLYZ_NORMAL;
   info->scan.earlyz_stencil_clear = false;

   info->params.any_integer_rt = false;
   info->params.hiz_enable = true;
   info->params.point_width =
      (state->point_size == 0.0f) ? 1.0f : state->point_size;
   info->params.line_width =
      (state->line_width == 0.0f) ? 1.0f : state->line_width;

   info->params.depth_offset_scale = state->offset_scale;
   /*
    * Scale the constant term.  The minimum representable value used by the HW
    * is not large enouch to be the minimum resolvable difference.
    */
   info->params.depth_offset_const = state->offset_units * 2.0f;
   info->params.depth_offset_clamp = state->offset_clamp;

   ilo_state_raster_init(&rast->rs, dev, info);

   return rast;
}

static void
ilo_bind_rasterizer_state(struct pipe_context *pipe, void *state)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->rasterizer = state;

   if (vec->rasterizer) {
      struct ilo_state_line_stipple_info info;

      info.pattern = vec->rasterizer->state.line_stipple_pattern;
      info.repeat_count = vec->rasterizer->state.line_stipple_factor + 1;

      ilo_state_line_stipple_set_info(&vec->line_stipple, dev, &info);
   }

   vec->dirty |= ILO_DIRTY_RASTERIZER;
}

static void
ilo_delete_rasterizer_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void *
ilo_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                     const struct pipe_depth_stencil_alpha_state *state)
{
   struct ilo_dsa_state *dsa;
   int i;

   dsa = CALLOC_STRUCT(ilo_dsa_state);
   assert(dsa);

   dsa->depth.cv_has_buffer = true;
   dsa->depth.test_enable = state->depth.enabled;
   dsa->depth.write_enable = state->depth.writemask;
   dsa->depth.test_func = ilo_translate_compare_func(state->depth.func);

   dsa->stencil.cv_has_buffer = true;
   for (i = 0; i < ARRAY_SIZE(state->stencil); i++) {
      const struct pipe_stencil_state *stencil = &state->stencil[i];
      struct ilo_state_cc_stencil_op_info *op;

      if (!stencil->enabled)
         break;

      if (i == 0) {
         dsa->stencil.test_enable = true;
         dsa->stencil_front.test_mask = stencil->valuemask;
         dsa->stencil_front.write_mask = stencil->writemask;

         op = &dsa->stencil.front;
      } else {
         dsa->stencil.twosided_enable = true;
         dsa->stencil_back.test_mask = stencil->valuemask;
         dsa->stencil_back.write_mask = stencil->writemask;

         op = &dsa->stencil.back;
      }

      op->test_func = ilo_translate_compare_func(stencil->func);
      op->fail_op = ilo_translate_stencil_op(stencil->fail_op);
      op->zfail_op = ilo_translate_stencil_op(stencil->zfail_op);
      op->zpass_op = ilo_translate_stencil_op(stencil->zpass_op);
   }

   dsa->alpha_test = state->alpha.enabled;
   dsa->alpha_ref = state->alpha.ref_value;
   dsa->alpha_func = ilo_translate_compare_func(state->alpha.func);

   return dsa;
}

static void
ilo_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->dsa = state;
   if (vec->dsa) {
      vec->cc_params.alpha_ref = vec->dsa->alpha_ref;
      vec->cc_params.stencil_front.test_mask =
         vec->dsa->stencil_front.test_mask;
      vec->cc_params.stencil_front.write_mask =
         vec->dsa->stencil_front.write_mask;
      vec->cc_params.stencil_back.test_mask =
         vec->dsa->stencil_back.test_mask;
      vec->cc_params.stencil_back.write_mask =
         vec->dsa->stencil_back.write_mask;
   }

   vec->dirty |= ILO_DIRTY_DSA;
}

static void
ilo_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *state)
{
   FREE(state);
}

static void *
ilo_create_fs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *shader;

   shader = ilo_shader_create_fs(ilo->dev, state, &ilo->state_vector);
   assert(shader);

   ilo_shader_cache_add(ilo->shader_cache, shader);

   return shader;
}

static void
ilo_bind_fs_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->fs = state;

   vec->dirty |= ILO_DIRTY_FS;
}

static void
ilo_delete_fs_state(struct pipe_context *pipe, void *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *fs = (struct ilo_shader_state *) state;

   ilo_shader_cache_remove(ilo->shader_cache, fs);
   ilo_shader_destroy(fs);
}

static void *
ilo_create_vs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *shader;

   shader = ilo_shader_create_vs(ilo->dev, state, &ilo->state_vector);
   assert(shader);

   ilo_shader_cache_add(ilo->shader_cache, shader);

   return shader;
}

static void
ilo_bind_vs_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->vs = state;

   vec->dirty |= ILO_DIRTY_VS;
}

static void
ilo_delete_vs_state(struct pipe_context *pipe, void *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *vs = (struct ilo_shader_state *) state;

   ilo_shader_cache_remove(ilo->shader_cache, vs);
   ilo_shader_destroy(vs);
}

static void *
ilo_create_gs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *shader;

   shader = ilo_shader_create_gs(ilo->dev, state, &ilo->state_vector);
   assert(shader);

   ilo_shader_cache_add(ilo->shader_cache, shader);

   return shader;
}

static void
ilo_bind_gs_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   /* util_blitter may set this unnecessarily */
   if (vec->gs == state)
      return;

   vec->gs = state;

   vec->dirty |= ILO_DIRTY_GS;
}

static void
ilo_delete_gs_state(struct pipe_context *pipe, void *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *gs = (struct ilo_shader_state *) state;

   ilo_shader_cache_remove(ilo->shader_cache, gs);
   ilo_shader_destroy(gs);
}

static void *
ilo_create_vertex_elements_state(struct pipe_context *pipe,
                                 unsigned num_elements,
                                 const struct pipe_vertex_element *elements)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_state_vf_element_info vf_elements[PIPE_MAX_ATTRIBS];
   unsigned instance_divisors[PIPE_MAX_ATTRIBS];
   struct ilo_state_vf_info vf_info;
   struct ilo_ve_state *ve;
   unsigned i;

   ve = CALLOC_STRUCT(ilo_ve_state);
   assert(ve);

   for (i = 0; i < num_elements; i++) {
      const struct pipe_vertex_element *elem = &elements[i];
      struct ilo_state_vf_element_info *attr = &vf_elements[i];
      unsigned hw_idx;

      /*
       * map the pipe vb to the hardware vb, which has a fixed instance
       * divisor
       */
      for (hw_idx = 0; hw_idx < ve->vb_count; hw_idx++) {
         if (ve->vb_mapping[hw_idx] == elem->vertex_buffer_index &&
             instance_divisors[hw_idx] == elem->instance_divisor)
            break;
      }

      /* create one if there is no matching hardware vb */
      if (hw_idx >= ve->vb_count) {
         hw_idx = ve->vb_count++;

         ve->vb_mapping[hw_idx] = elem->vertex_buffer_index;
         instance_divisors[hw_idx] = elem->instance_divisor;
      }

      attr->buffer = hw_idx;
      attr->vertex_offset = elem->src_offset;
      attr->format = ilo_format_translate_vertex(dev, elem->src_format);
      attr->format_size = util_format_get_blocksize(elem->src_format);
      attr->component_count = util_format_get_nr_components(elem->src_format);
      attr->is_integer = util_format_is_pure_integer(elem->src_format);

      attr->instancing_enable = (elem->instance_divisor != 0);
      attr->instancing_step_rate = elem->instance_divisor;
   }

   memset(&vf_info, 0, sizeof(vf_info));
   vf_info.data = ve->vf_data;
   vf_info.data_size = sizeof(ve->vf_data);
   vf_info.elements = vf_elements;
   vf_info.element_count = num_elements;
   /* vf_info.params and ve->vf_params are both zeroed */

   if (!ilo_state_vf_init(&ve->vf, dev, &vf_info)) {
      FREE(ve);
      return NULL;
   }

   return ve;
}

static void
ilo_bind_vertex_elements_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->ve = state;

   vec->dirty |= ILO_DIRTY_VE;
}

static void
ilo_delete_vertex_elements_state(struct pipe_context *pipe, void *state)
{
   struct ilo_ve_state *ve = state;

   FREE(ve);
}

static void
ilo_set_blend_color(struct pipe_context *pipe,
                    const struct pipe_blend_color *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   memcpy(vec->cc_params.blend_rgba, state->color, sizeof(state->color));

   vec->dirty |= ILO_DIRTY_BLEND_COLOR;
}

static void
ilo_set_stencil_ref(struct pipe_context *pipe,
                    const struct pipe_stencil_ref *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   /* util_blitter may set this unnecessarily */
   if (!memcmp(&vec->stencil_ref, state, sizeof(*state)))
      return;

   vec->stencil_ref = *state;

   vec->cc_params.stencil_front.test_ref = state->ref_value[0];
   vec->cc_params.stencil_back.test_ref = state->ref_value[1];

   vec->dirty |= ILO_DIRTY_STENCIL_REF;
}

static void
ilo_set_sample_mask(struct pipe_context *pipe,
                    unsigned sample_mask)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   /* util_blitter may set this unnecessarily */
   if (vec->sample_mask == sample_mask)
      return;

   vec->sample_mask = sample_mask;

   vec->dirty |= ILO_DIRTY_SAMPLE_MASK;
}

static void
ilo_set_clip_state(struct pipe_context *pipe,
                   const struct pipe_clip_state *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->clip = *state;

   vec->dirty |= ILO_DIRTY_CLIP;
}

static void
ilo_set_constant_buffer(struct pipe_context *pipe,
                        uint shader, uint index,
                        struct pipe_constant_buffer *buf)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_cbuf_state *cbuf = &vec->cbuf[shader];
   const unsigned count = 1;
   unsigned i;

   assert(shader < Elements(vec->cbuf));
   assert(index + count <= Elements(vec->cbuf[shader].cso));

   if (buf) {
      for (i = 0; i < count; i++) {
         struct ilo_cbuf_cso *cso = &cbuf->cso[index + i];

         pipe_resource_reference(&cso->resource, buf[i].buffer);

         cso->info.access = ILO_STATE_SURFACE_ACCESS_DP_DATA;
         cso->info.format = GEN6_FORMAT_R32G32B32A32_FLOAT;
         cso->info.format_size = 16;
         cso->info.struct_size = 16;
         cso->info.readonly = true;
         cso->info.size = buf[i].buffer_size;

         if (buf[i].buffer) {
            cso->info.vma = ilo_resource_get_vma(buf[i].buffer);
            cso->info.offset = buf[i].buffer_offset;

            memset(&cso->surface, 0, sizeof(cso->surface));
            ilo_state_surface_init_for_buffer(&cso->surface, dev, &cso->info);

            cso->user_buffer = NULL;

            cbuf->enabled_mask |= 1 << (index + i);
         } else if (buf[i].user_buffer) {
            cso->info.vma = NULL;
            /* buffer_offset does not apply for user buffer */
            cso->user_buffer = buf[i].user_buffer;

            cbuf->enabled_mask |= 1 << (index + i);
         } else {
            cso->info.vma = NULL;
            cso->info.size = 0;
            cso->user_buffer = NULL;

            cbuf->enabled_mask &= ~(1 << (index + i));
         }
      }
   } else {
      for (i = 0; i < count; i++) {
         struct ilo_cbuf_cso *cso = &cbuf->cso[index + i];

         pipe_resource_reference(&cso->resource, NULL);

         cso->info.vma = NULL;
         cso->info.size = 0;
         cso->user_buffer = NULL;

         cbuf->enabled_mask &= ~(1 << (index + i));
      }
   }

   vec->dirty |= ILO_DIRTY_CBUF;
}

static void
fb_set_blend_caps(const struct ilo_dev *dev,
                  enum pipe_format format,
                  struct ilo_fb_blend_caps *caps)
{
   const struct util_format_description *desc =
      util_format_description(format);
   const int ch = util_format_get_first_non_void_channel(format);

   memset(caps, 0, sizeof(*caps));

   if (format == PIPE_FORMAT_NONE || desc->is_mixed)
      return;

   caps->is_unorm = (ch >= 0 && desc->channel[ch].normalized &&
         desc->channel[ch].type == UTIL_FORMAT_TYPE_UNSIGNED &&
         desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB);
   caps->is_integer = util_format_is_pure_integer(format);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 365:
    *
    *     "Logic Ops are only supported on *_UNORM surfaces (excluding _SRGB
    *      variants), otherwise Logic Ops must be DISABLED."
    *
    * According to the classic driver, this is lifted on Gen8+.
    */
   caps->can_logicop = (ilo_dev_gen(dev) >= ILO_GEN(8) || caps->is_unorm);

   /* no blending for pure integer formats */
   caps->can_blend = !caps->is_integer;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 382:
    *
    *     "Alpha Test can only be enabled if Pixel Shader outputs a float
    *      alpha value."
    */
   caps->can_alpha_test = !caps->is_integer;

   caps->force_dst_alpha_one =
      (ilo_format_translate_render(dev, format) !=
       ilo_format_translate_color(dev, format));

   /* sanity check */
   if (caps->force_dst_alpha_one) {
      enum pipe_format render_format;

      switch (format) {
      case PIPE_FORMAT_B8G8R8X8_UNORM:
         render_format = PIPE_FORMAT_B8G8R8A8_UNORM;
         break;
      default:
         render_format = PIPE_FORMAT_NONE;
         break;
      }

      assert(ilo_format_translate_render(dev, format) ==
             ilo_format_translate_color(dev, render_format));
   }
}

static void
ilo_set_framebuffer_state(struct pipe_context *pipe,
                          const struct pipe_framebuffer_state *state)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_fb_state *fb = &vec->fb;
   const struct pipe_surface *first_surf = NULL;
   int i;

   util_copy_framebuffer_state(&fb->state, state);

   fb->has_integer_rt = false;
   for (i = 0; i < state->nr_cbufs; i++) {
      if (state->cbufs[i]) {
         fb_set_blend_caps(dev, state->cbufs[i]->format, &fb->blend_caps[i]);

         fb->has_integer_rt |= fb->blend_caps[i].is_integer;

         if (!first_surf)
            first_surf = state->cbufs[i];
      } else {
         fb_set_blend_caps(dev, PIPE_FORMAT_NONE, &fb->blend_caps[i]);
      }
   }

   if (!first_surf && state->zsbuf)
      first_surf = state->zsbuf;

   fb->num_samples = (first_surf) ? first_surf->texture->nr_samples : 1;
   if (!fb->num_samples)
      fb->num_samples = 1;

   if (state->zsbuf) {
      const struct ilo_surface_cso *cso =
         (const struct ilo_surface_cso *) state->zsbuf;
      const struct ilo_texture *tex = ilo_texture(cso->base.texture);

      fb->has_hiz = cso->u.zs.hiz_vma;
      fb->depth_offset_format =
         ilo_format_translate_depth(dev, tex->image_format);
   } else {
      fb->has_hiz = false;
      fb->depth_offset_format = GEN6_ZFORMAT_D32_FLOAT;
   }

   /*
    * The PRMs list several restrictions when the framebuffer has more than
    * one surface.  It seems they are actually lifted on GEN6+.
    */

   vec->dirty |= ILO_DIRTY_FB;
}

static void
ilo_set_polygon_stipple(struct pipe_context *pipe,
                        const struct pipe_poly_stipple *state)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_state_poly_stipple_info info;
   int i;

   for (i = 0; i < 32; i++)
      info.pattern[i] = state->stipple[i];

   ilo_state_poly_stipple_set_info(&vec->poly_stipple, dev, &info);

   vec->dirty |= ILO_DIRTY_POLY_STIPPLE;
}

static void
ilo_set_scissor_states(struct pipe_context *pipe,
                       unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *scissors)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   unsigned i;

   for (i = 0; i < num_scissors; i++) {
      struct ilo_state_viewport_scissor_info *info =
         &vec->viewport.scissors[start_slot + i];

      if (scissors[i].minx < scissors[i].maxx &&
          scissors[i].miny < scissors[i].maxy) {
         info->min_x = scissors[i].minx;
         info->min_y = scissors[i].miny;
         info->max_x = scissors[i].maxx - 1;
         info->max_y = scissors[i].maxy - 1;
      } else {
         info->min_x = 1;
         info->min_y = 1;
         info->max_x = 0;
         info->max_y = 0;
      }
   }

   vec->dirty |= ILO_DIRTY_SCISSOR;
}

static void
ilo_set_viewport_states(struct pipe_context *pipe,
                        unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *viewports)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   if (viewports) {
      unsigned i;

      for (i = 0; i < num_viewports; i++) {
         struct ilo_state_viewport_matrix_info *info =
            &vec->viewport.matrices[start_slot + i];

         memcpy(info->scale, viewports[i].scale, sizeof(info->scale));
         memcpy(info->translate, viewports[i].translate,
               sizeof(info->translate));
      }

      if (vec->viewport.params.count < start_slot + num_viewports)
         vec->viewport.params.count = start_slot + num_viewports;

      /* need to save viewport 0 for util_blitter */
      if (!start_slot && num_viewports)
         vec->viewport.viewport0 = viewports[0];
   }
   else {
      if (vec->viewport.params.count <= start_slot + num_viewports &&
          vec->viewport.params.count > start_slot)
         vec->viewport.params.count = start_slot;
   }

   vec->dirty |= ILO_DIRTY_VIEWPORT;
}

static void
ilo_set_sampler_views(struct pipe_context *pipe, unsigned shader,
                      unsigned start, unsigned count,
                      struct pipe_sampler_view **views)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_view_state *dst = &vec->view[shader];
   unsigned i;

   assert(start + count <= Elements(dst->states));

   if (views) {
      for (i = 0; i < count; i++)
         pipe_sampler_view_reference(&dst->states[start + i], views[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_sampler_view_reference(&dst->states[start + i], NULL);
   }

   if (dst->count <= start + count) {
      if (views)
         count += start;
      else
         count = start;

      while (count > 0 && !dst->states[count - 1])
         count--;

      dst->count = count;
   }

   switch (shader) {
   case PIPE_SHADER_VERTEX:
      vec->dirty |= ILO_DIRTY_VIEW_VS;
      break;
   case PIPE_SHADER_GEOMETRY:
      vec->dirty |= ILO_DIRTY_VIEW_GS;
      break;
   case PIPE_SHADER_FRAGMENT:
      vec->dirty |= ILO_DIRTY_VIEW_FS;
      break;
   case PIPE_SHADER_COMPUTE:
      vec->dirty |= ILO_DIRTY_VIEW_CS;
      break;
   }
}

static void
ilo_set_shader_images(struct pipe_context *pipe, unsigned shader,
                      unsigned start, unsigned count,
                      struct pipe_image_view **views)
{
#if 0
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_resource_state *dst = &vec->resource;
   unsigned i;

   assert(start + count <= Elements(dst->states));

   if (surfaces) {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst->states[start + i], surfaces[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst->states[start + i], NULL);
   }

   if (dst->count <= start + count) {
      if (surfaces)
         count += start;
      else
         count = start;

      while (count > 0 && !dst->states[count - 1])
         count--;

      dst->count = count;
   }

   vec->dirty |= ILO_DIRTY_RESOURCE;
#endif
}

static void
ilo_set_vertex_buffers(struct pipe_context *pipe,
                       unsigned start_slot, unsigned num_buffers,
                       const struct pipe_vertex_buffer *buffers)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   unsigned i;

   /* no PIPE_CAP_USER_VERTEX_BUFFERS */
   if (buffers) {
      for (i = 0; i < num_buffers; i++)
         assert(!buffers[i].user_buffer);
   }

   util_set_vertex_buffers_mask(vec->vb.states,
         &vec->vb.enabled_mask, buffers, start_slot, num_buffers);

   vec->dirty |= ILO_DIRTY_VB;
}

static void
ilo_set_index_buffer(struct pipe_context *pipe,
                     const struct pipe_index_buffer *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   if (state) {
      pipe_resource_reference(&vec->ib.state.buffer, state->buffer);
      vec->ib.state = *state;
   } else {
      pipe_resource_reference(&vec->ib.state.buffer, NULL);
      memset(&vec->ib.state, 0, sizeof(vec->ib.state));
   }

   vec->dirty |= ILO_DIRTY_IB;
}

static struct pipe_stream_output_target *
ilo_create_stream_output_target(struct pipe_context *pipe,
                                struct pipe_resource *res,
                                unsigned buffer_offset,
                                unsigned buffer_size)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_stream_output_target *target;
   struct ilo_state_sol_buffer_info info;

   target = CALLOC_STRUCT(ilo_stream_output_target);
   assert(target);

   pipe_reference_init(&target->base.reference, 1);
   pipe_resource_reference(&target->base.buffer, res);
   target->base.context = pipe;
   target->base.buffer_offset = buffer_offset;
   target->base.buffer_size = buffer_size;

   memset(&info, 0, sizeof(info));
   info.vma = ilo_resource_get_vma(res);
   info.offset = buffer_offset;
   info.size = buffer_size;

   ilo_state_sol_buffer_init(&target->sb, dev, &info);

   return &target->base;
}

static void
ilo_set_stream_output_targets(struct pipe_context *pipe,
                              unsigned num_targets,
                              struct pipe_stream_output_target **targets,
                              const unsigned *offset)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   unsigned i;
   unsigned append_bitmask = 0;

   if (!targets)
      num_targets = 0;

   /* util_blitter may set this unnecessarily */
   if (!vec->so.count && !num_targets)
      return;

   for (i = 0; i < num_targets; i++) {
      pipe_so_target_reference(&vec->so.states[i], targets[i]);
      if (offset[i] == (unsigned)-1)
         append_bitmask |= 1 << i;
   }

   for (; i < vec->so.count; i++)
      pipe_so_target_reference(&vec->so.states[i], NULL);

   vec->so.count = num_targets;
   vec->so.append_bitmask = append_bitmask;

   vec->so.enabled = (vec->so.count > 0);

   vec->dirty |= ILO_DIRTY_SO;
}

static void
ilo_stream_output_target_destroy(struct pipe_context *pipe,
                                 struct pipe_stream_output_target *target)
{
   pipe_resource_reference(&target->buffer, NULL);
   FREE(target);
}

static struct pipe_sampler_view *
ilo_create_sampler_view(struct pipe_context *pipe,
                        struct pipe_resource *res,
                        const struct pipe_sampler_view *templ)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_view_cso *view;

   view = CALLOC_STRUCT(ilo_view_cso);
   assert(view);

   view->base = *templ;
   pipe_reference_init(&view->base.reference, 1);
   view->base.texture = NULL;
   pipe_resource_reference(&view->base.texture, res);
   view->base.context = pipe;

   if (res->target == PIPE_BUFFER) {
      struct ilo_state_surface_buffer_info info;

      memset(&info, 0, sizeof(info));
      info.vma = ilo_resource_get_vma(res);
      info.offset = templ->u.buf.first_element * info.struct_size;
      info.size = (templ->u.buf.last_element -
            templ->u.buf.first_element + 1) * info.struct_size;
      info.access = ILO_STATE_SURFACE_ACCESS_SAMPLER;
      info.format = ilo_format_translate_color(dev, templ->format);
      info.format_size = util_format_get_blocksize(templ->format);
      info.struct_size = info.format_size;
      info.readonly = true;

      ilo_state_surface_init_for_buffer(&view->surface, dev, &info);
   } else {
      struct ilo_texture *tex = ilo_texture(res);
      struct ilo_state_surface_image_info info;

      /* warn about degraded performance because of a missing binding flag */
      if (tex->image.tiling == GEN6_TILING_NONE &&
          !(tex->base.bind & PIPE_BIND_SAMPLER_VIEW)) {
         ilo_warn("creating sampler view for a resource "
                  "not created for sampling\n");
      }

      memset(&info, 0, sizeof(info));

      info.img = &tex->image;
      info.level_base = templ->u.tex.first_level;
      info.level_count = templ->u.tex.last_level -
         templ->u.tex.first_level + 1;
      info.slice_base = templ->u.tex.first_layer;
      info.slice_count = templ->u.tex.last_layer -
         templ->u.tex.first_layer + 1;

      info.vma = &tex->vma;
      info.access = ILO_STATE_SURFACE_ACCESS_SAMPLER;
      info.type = tex->image.type;

      if (templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
          tex->separate_s8) {
         info.format = ilo_format_translate_texture(dev,
               PIPE_FORMAT_Z32_FLOAT);
      } else {
         info.format = ilo_format_translate_texture(dev, templ->format);
      }

      info.is_array = util_resource_is_array_texture(&tex->base);
      info.readonly = true;

      ilo_state_surface_init_for_image(&view->surface, dev, &info);
   }

   return &view->base;
}

static void
ilo_sampler_view_destroy(struct pipe_context *pipe,
                         struct pipe_sampler_view *view)
{
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static struct pipe_surface *
ilo_create_surface(struct pipe_context *pipe,
                   struct pipe_resource *res,
                   const struct pipe_surface *templ)
{
   const struct ilo_dev *dev = ilo_context(pipe)->dev;
   struct ilo_texture *tex = ilo_texture(res);
   struct ilo_surface_cso *surf;

   surf = CALLOC_STRUCT(ilo_surface_cso);
   assert(surf);

   surf->base = *templ;
   pipe_reference_init(&surf->base.reference, 1);
   surf->base.texture = NULL;
   pipe_resource_reference(&surf->base.texture, &tex->base);

   surf->base.context = pipe;
   surf->base.width = u_minify(tex->base.width0, templ->u.tex.level);
   surf->base.height = u_minify(tex->base.height0, templ->u.tex.level);

   surf->is_rt = !util_format_is_depth_or_stencil(templ->format);

   if (surf->is_rt) {
      struct ilo_state_surface_image_info info;

      /* relax this? */
      assert(tex->base.target != PIPE_BUFFER);

      memset(&info, 0, sizeof(info));

      info.img = &tex->image;
      info.level_base = templ->u.tex.level;
      info.level_count = 1;
      info.slice_base = templ->u.tex.first_layer;
      info.slice_count = templ->u.tex.last_layer -
         templ->u.tex.first_layer + 1;

      info.vma = &tex->vma;
      if (ilo_image_can_enable_aux(&tex->image, templ->u.tex.level))
         info.aux_vma = &tex->aux_vma;

      info.access = ILO_STATE_SURFACE_ACCESS_DP_RENDER;

      info.type = (tex->image.type == GEN6_SURFTYPE_CUBE) ?
         GEN6_SURFTYPE_2D : tex->image.type;

      info.format = ilo_format_translate_render(dev, templ->format);
      info.is_array = util_resource_is_array_texture(&tex->base);

      ilo_state_surface_init_for_image(&surf->u.rt, dev, &info);
   } else {
      struct ilo_state_zs_info info;

      assert(res->target != PIPE_BUFFER);

      memset(&info, 0, sizeof(info));

      if (templ->format == PIPE_FORMAT_S8_UINT) {
         info.s_vma = &tex->vma;
         info.s_img = &tex->image;
      } else {
         info.z_vma = &tex->vma;
         info.z_img = &tex->image;

         if (tex->separate_s8) {
            info.s_vma = &tex->separate_s8->vma;
            info.s_img = &tex->separate_s8->image;
         }

         if (ilo_image_can_enable_aux(&tex->image, templ->u.tex.level))
            info.hiz_vma = &tex->aux_vma;
      }

      info.level = templ->u.tex.level;
      info.slice_base = templ->u.tex.first_layer;
      info.slice_count = templ->u.tex.last_layer -
         templ->u.tex.first_layer + 1;

      info.type = (tex->image.type == GEN6_SURFTYPE_CUBE) ?
         GEN6_SURFTYPE_2D : tex->image.type;

      info.format = ilo_format_translate_depth(dev, tex->image_format);
      if (ilo_dev_gen(dev) == ILO_GEN(6) && !info.hiz_vma &&
          tex->image_format == PIPE_FORMAT_Z24X8_UNORM)
         info.format = GEN6_ZFORMAT_D24_UNORM_S8_UINT;

      ilo_state_zs_init(&surf->u.zs, dev, &info);
   }

   return &surf->base;
}

static void
ilo_surface_destroy(struct pipe_context *pipe,
                    struct pipe_surface *surface)
{
   pipe_resource_reference(&surface->texture, NULL);
   FREE(surface);
}

static void *
ilo_create_compute_state(struct pipe_context *pipe,
                         const struct pipe_compute_state *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *shader;

   shader = ilo_shader_create_cs(ilo->dev, state, &ilo->state_vector);
   assert(shader);

   ilo_shader_cache_add(ilo->shader_cache, shader);

   return shader;
}

static void
ilo_bind_compute_state(struct pipe_context *pipe, void *state)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;

   vec->cs = state;

   vec->dirty |= ILO_DIRTY_CS;
}

static void
ilo_delete_compute_state(struct pipe_context *pipe, void *state)
{
   struct ilo_context *ilo = ilo_context(pipe);
   struct ilo_shader_state *cs = (struct ilo_shader_state *) state;

   ilo_shader_cache_remove(ilo->shader_cache, cs);
   ilo_shader_destroy(cs);
}

static void
ilo_set_compute_resources(struct pipe_context *pipe,
                          unsigned start, unsigned count,
                          struct pipe_surface **surfaces)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_resource_state *dst = &vec->cs_resource;
   unsigned i;

   assert(start + count <= Elements(dst->states));

   if (surfaces) {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst->states[start + i], surfaces[i]);
   }
   else {
      for (i = 0; i < count; i++)
         pipe_surface_reference(&dst->states[start + i], NULL);
   }

   if (dst->count <= start + count) {
      if (surfaces)
         count += start;
      else
         count = start;

      while (count > 0 && !dst->states[count - 1])
         count--;

      dst->count = count;
   }

   vec->dirty |= ILO_DIRTY_CS_RESOURCE;
}

static void
ilo_set_global_binding(struct pipe_context *pipe,
                       unsigned start, unsigned count,
                       struct pipe_resource **resources,
                       uint32_t **handles)
{
   struct ilo_state_vector *vec = &ilo_context(pipe)->state_vector;
   struct ilo_global_binding_cso *dst;
   unsigned i;

   /* make room */
   if (vec->global_binding.count < start + count) {
      if (resources) {
         const unsigned old_size = vec->global_binding.bindings.size;
         const unsigned new_size = sizeof(*dst) * (start + count);

         if (old_size < new_size) {
            util_dynarray_resize(&vec->global_binding.bindings, new_size);
            memset(vec->global_binding.bindings.data + old_size, 0,
                  new_size - old_size);
         }
      } else {
         count = vec->global_binding.count - start;
      }
   }

   dst = util_dynarray_element(&vec->global_binding.bindings,
         struct ilo_global_binding_cso, start);

   if (resources) {
      for (i = 0; i < count; i++) {
         pipe_resource_reference(&dst[i].resource, resources[i]);
         dst[i].handle = handles[i];
      }
   } else {
      for (i = 0; i < count; i++) {
         pipe_resource_reference(&dst[i].resource, NULL);
         dst[i].handle = NULL;
      }
   }

   if (vec->global_binding.count <= start + count) {
      dst = util_dynarray_begin(&vec->global_binding.bindings);

      if (resources)
         count += start;
      else
         count = start;

      while (count > 0 && !dst[count - 1].resource)
         count--;

      vec->global_binding.count = count;
   }

   vec->dirty |= ILO_DIRTY_GLOBAL_BINDING;
}

/**
 * Initialize state-related functions.
 */
void
ilo_init_state_functions(struct ilo_context *ilo)
{
   STATIC_ASSERT(ILO_STATE_COUNT <= 32);

   ilo->base.create_blend_state = ilo_create_blend_state;
   ilo->base.bind_blend_state = ilo_bind_blend_state;
   ilo->base.delete_blend_state = ilo_delete_blend_state;
   ilo->base.create_sampler_state = ilo_create_sampler_state;
   ilo->base.bind_sampler_states = ilo_bind_sampler_states;
   ilo->base.delete_sampler_state = ilo_delete_sampler_state;
   ilo->base.create_rasterizer_state = ilo_create_rasterizer_state;
   ilo->base.bind_rasterizer_state = ilo_bind_rasterizer_state;
   ilo->base.delete_rasterizer_state = ilo_delete_rasterizer_state;
   ilo->base.create_depth_stencil_alpha_state = ilo_create_depth_stencil_alpha_state;
   ilo->base.bind_depth_stencil_alpha_state = ilo_bind_depth_stencil_alpha_state;
   ilo->base.delete_depth_stencil_alpha_state = ilo_delete_depth_stencil_alpha_state;
   ilo->base.create_fs_state = ilo_create_fs_state;
   ilo->base.bind_fs_state = ilo_bind_fs_state;
   ilo->base.delete_fs_state = ilo_delete_fs_state;
   ilo->base.create_vs_state = ilo_create_vs_state;
   ilo->base.bind_vs_state = ilo_bind_vs_state;
   ilo->base.delete_vs_state = ilo_delete_vs_state;
   ilo->base.create_gs_state = ilo_create_gs_state;
   ilo->base.bind_gs_state = ilo_bind_gs_state;
   ilo->base.delete_gs_state = ilo_delete_gs_state;
   ilo->base.create_vertex_elements_state = ilo_create_vertex_elements_state;
   ilo->base.bind_vertex_elements_state = ilo_bind_vertex_elements_state;
   ilo->base.delete_vertex_elements_state = ilo_delete_vertex_elements_state;

   ilo->base.set_blend_color = ilo_set_blend_color;
   ilo->base.set_stencil_ref = ilo_set_stencil_ref;
   ilo->base.set_sample_mask = ilo_set_sample_mask;
   ilo->base.set_clip_state = ilo_set_clip_state;
   ilo->base.set_constant_buffer = ilo_set_constant_buffer;
   ilo->base.set_framebuffer_state = ilo_set_framebuffer_state;
   ilo->base.set_polygon_stipple = ilo_set_polygon_stipple;
   ilo->base.set_scissor_states = ilo_set_scissor_states;
   ilo->base.set_viewport_states = ilo_set_viewport_states;
   ilo->base.set_sampler_views = ilo_set_sampler_views;
   ilo->base.set_shader_images = ilo_set_shader_images;
   ilo->base.set_vertex_buffers = ilo_set_vertex_buffers;
   ilo->base.set_index_buffer = ilo_set_index_buffer;

   ilo->base.create_stream_output_target = ilo_create_stream_output_target;
   ilo->base.stream_output_target_destroy = ilo_stream_output_target_destroy;
   ilo->base.set_stream_output_targets = ilo_set_stream_output_targets;

   ilo->base.create_sampler_view = ilo_create_sampler_view;
   ilo->base.sampler_view_destroy = ilo_sampler_view_destroy;

   ilo->base.create_surface = ilo_create_surface;
   ilo->base.surface_destroy = ilo_surface_destroy;

   ilo->base.create_compute_state = ilo_create_compute_state;
   ilo->base.bind_compute_state = ilo_bind_compute_state;
   ilo->base.delete_compute_state = ilo_delete_compute_state;
   ilo->base.set_compute_resources = ilo_set_compute_resources;
   ilo->base.set_global_binding = ilo_set_global_binding;
}

void
ilo_state_vector_init(const struct ilo_dev *dev,
                      struct ilo_state_vector *vec)
{
   struct ilo_state_urb_info urb_info;

   vec->sample_mask = ~0u;

   ilo_state_viewport_init_data_only(&vec->viewport.vp, dev,
         vec->viewport.vp_data, sizeof(vec->viewport.vp_data));
   assert(vec->viewport.vp.array_size >= ILO_MAX_VIEWPORTS);

   vec->viewport.params.matrices = vec->viewport.matrices;
   vec->viewport.params.scissors = vec->viewport.scissors;

   ilo_state_hs_init_disabled(&vec->disabled_hs, dev);
   ilo_state_ds_init_disabled(&vec->disabled_ds, dev);
   ilo_state_gs_init_disabled(&vec->disabled_gs, dev);

   ilo_state_sol_buffer_init_disabled(&vec->so.dummy_sb, dev);

   ilo_state_surface_init_for_null(&vec->fb.null_rt, dev);
   ilo_state_zs_init_for_null(&vec->fb.null_zs, dev);

   ilo_state_sampler_init_disabled(&vec->disabled_sampler, dev);

   memset(&urb_info, 0, sizeof(urb_info));
   ilo_state_urb_init(&vec->urb, dev, &urb_info);

   util_dynarray_init(&vec->global_binding.bindings);

   vec->dirty = ILO_DIRTY_ALL;
}

void
ilo_state_vector_cleanup(struct ilo_state_vector *vec)
{
   unsigned i, sh;

   for (i = 0; i < Elements(vec->vb.states); i++) {
      if (vec->vb.enabled_mask & (1 << i))
         pipe_resource_reference(&vec->vb.states[i].buffer, NULL);
   }

   pipe_resource_reference(&vec->ib.state.buffer, NULL);
   pipe_resource_reference(&vec->ib.hw_resource, NULL);

   for (i = 0; i < vec->so.count; i++)
      pipe_so_target_reference(&vec->so.states[i], NULL);

   for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
      for (i = 0; i < vec->view[sh].count; i++) {
         struct pipe_sampler_view *view = vec->view[sh].states[i];
         pipe_sampler_view_reference(&view, NULL);
      }

      for (i = 0; i < Elements(vec->cbuf[sh].cso); i++) {
         struct ilo_cbuf_cso *cbuf = &vec->cbuf[sh].cso[i];
         pipe_resource_reference(&cbuf->resource, NULL);
      }
   }

   for (i = 0; i < vec->resource.count; i++)
      pipe_surface_reference(&vec->resource.states[i], NULL);

   for (i = 0; i < vec->fb.state.nr_cbufs; i++)
      pipe_surface_reference(&vec->fb.state.cbufs[i], NULL);

   if (vec->fb.state.zsbuf)
      pipe_surface_reference(&vec->fb.state.zsbuf, NULL);

   for (i = 0; i < vec->cs_resource.count; i++)
      pipe_surface_reference(&vec->cs_resource.states[i], NULL);

   for (i = 0; i < vec->global_binding.count; i++) {
      struct ilo_global_binding_cso *cso =
         util_dynarray_element(&vec->global_binding.bindings,
               struct ilo_global_binding_cso, i);
      pipe_resource_reference(&cso->resource, NULL);
   }

   util_dynarray_fini(&vec->global_binding.bindings);
}

/**
 * Mark all states that have the resource dirty.
 */
void
ilo_state_vector_resource_renamed(struct ilo_state_vector *vec,
                                  struct pipe_resource *res)
{
   uint32_t states = 0;
   unsigned sh, i;

   if (res->target == PIPE_BUFFER) {
      uint32_t vb_mask = vec->vb.enabled_mask;

      while (vb_mask) {
         const unsigned idx = u_bit_scan(&vb_mask);

         if (vec->vb.states[idx].buffer == res) {
            states |= ILO_DIRTY_VB;
            break;
         }
      }

      if (vec->ib.state.buffer == res) {
         states |= ILO_DIRTY_IB;

         /*
          * finalize_index_buffer() has an optimization that clears
          * ILO_DIRTY_IB when the HW states do not change.  However, it fails
          * to flush the VF cache when the HW states do not change, but the
          * contents of the IB has changed.  Here, we set the index size to an
          * invalid value to avoid the optimization.
          */
         vec->ib.hw_index_size = 0;
      }

      for (i = 0; i < vec->so.count; i++) {
         if (vec->so.states[i]->buffer == res) {
            states |= ILO_DIRTY_SO;
            break;
         }
      }
   }

   for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
      for (i = 0; i < vec->view[sh].count; i++) {
         struct ilo_view_cso *cso = (struct ilo_view_cso *) vec->view[sh].states[i];

         if (cso->base.texture == res) {
            static const unsigned view_dirty_bits[PIPE_SHADER_TYPES] = {
               [PIPE_SHADER_VERTEX]    = ILO_DIRTY_VIEW_VS,
               [PIPE_SHADER_FRAGMENT]  = ILO_DIRTY_VIEW_FS,
               [PIPE_SHADER_GEOMETRY]  = ILO_DIRTY_VIEW_GS,
               [PIPE_SHADER_COMPUTE]   = ILO_DIRTY_VIEW_CS,
            };

            states |= view_dirty_bits[sh];
            break;
         }
      }

      if (res->target == PIPE_BUFFER) {
         for (i = 0; i < Elements(vec->cbuf[sh].cso); i++) {
            struct ilo_cbuf_cso *cbuf = &vec->cbuf[sh].cso[i];

            if (cbuf->resource == res) {
               states |= ILO_DIRTY_CBUF;
               break;
            }
         }
      }
   }

   for (i = 0; i < vec->resource.count; i++) {
      struct ilo_surface_cso *cso =
         (struct ilo_surface_cso *) vec->resource.states[i];

      if (cso->base.texture == res) {
         states |= ILO_DIRTY_RESOURCE;
         break;
      }
   }

   /* for now? */
   if (res->target != PIPE_BUFFER) {
      for (i = 0; i < vec->fb.state.nr_cbufs; i++) {
         struct ilo_surface_cso *cso =
            (struct ilo_surface_cso *) vec->fb.state.cbufs[i];
         if (cso && cso->base.texture == res) {
            states |= ILO_DIRTY_FB;
            break;
         }
      }

      if (vec->fb.state.zsbuf && vec->fb.state.zsbuf->texture == res)
         states |= ILO_DIRTY_FB;
   }

   for (i = 0; i < vec->cs_resource.count; i++) {
      struct ilo_surface_cso *cso =
         (struct ilo_surface_cso *) vec->cs_resource.states[i];
      if (cso->base.texture == res) {
         states |= ILO_DIRTY_CS_RESOURCE;
         break;
      }
   }

   for (i = 0; i < vec->global_binding.count; i++) {
      struct ilo_global_binding_cso *cso =
         util_dynarray_element(&vec->global_binding.bindings,
               struct ilo_global_binding_cso, i);

      if (cso->resource == res) {
         states |= ILO_DIRTY_GLOBAL_BINDING;
         break;
      }
   }

   vec->dirty |= states;
}

void
ilo_state_vector_dump_dirty(const struct ilo_state_vector *vec)
{
   static const char *state_names[ILO_STATE_COUNT] = {
      [ILO_STATE_VB]              = "VB",
      [ILO_STATE_VE]              = "VE",
      [ILO_STATE_IB]              = "IB",
      [ILO_STATE_VS]              = "VS",
      [ILO_STATE_GS]              = "GS",
      [ILO_STATE_SO]              = "SO",
      [ILO_STATE_CLIP]            = "CLIP",
      [ILO_STATE_VIEWPORT]        = "VIEWPORT",
      [ILO_STATE_SCISSOR]         = "SCISSOR",
      [ILO_STATE_RASTERIZER]      = "RASTERIZER",
      [ILO_STATE_POLY_STIPPLE]    = "POLY_STIPPLE",
      [ILO_STATE_SAMPLE_MASK]     = "SAMPLE_MASK",
      [ILO_STATE_FS]              = "FS",
      [ILO_STATE_DSA]             = "DSA",
      [ILO_STATE_STENCIL_REF]     = "STENCIL_REF",
      [ILO_STATE_BLEND]           = "BLEND",
      [ILO_STATE_BLEND_COLOR]     = "BLEND_COLOR",
      [ILO_STATE_FB]              = "FB",
      [ILO_STATE_SAMPLER_VS]      = "SAMPLER_VS",
      [ILO_STATE_SAMPLER_GS]      = "SAMPLER_GS",
      [ILO_STATE_SAMPLER_FS]      = "SAMPLER_FS",
      [ILO_STATE_SAMPLER_CS]      = "SAMPLER_CS",
      [ILO_STATE_VIEW_VS]         = "VIEW_VS",
      [ILO_STATE_VIEW_GS]         = "VIEW_GS",
      [ILO_STATE_VIEW_FS]         = "VIEW_FS",
      [ILO_STATE_VIEW_CS]         = "VIEW_CS",
      [ILO_STATE_CBUF]            = "CBUF",
      [ILO_STATE_RESOURCE]        = "RESOURCE",
      [ILO_STATE_CS]              = "CS",
      [ILO_STATE_CS_RESOURCE]     = "CS_RESOURCE",
      [ILO_STATE_GLOBAL_BINDING]  = "GLOBAL_BINDING",
   };
   uint32_t dirty = vec->dirty;

   if (!dirty) {
      ilo_printf("no state is dirty\n");
      return;
   }

   dirty &= (1U << ILO_STATE_COUNT) - 1;

   ilo_printf("%2d states are dirty:", util_bitcount(dirty));
   while (dirty) {
      const enum ilo_state state = u_bit_scan(&dirty);
      ilo_printf(" %s", state_names[state]);
   }
   ilo_printf("\n");
}
