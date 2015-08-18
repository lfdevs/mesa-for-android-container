/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2015 LunarG, Inc.
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

#include "ilo_debug.h"
#include "ilo_vma.h"
#include "ilo_state_vf.h"

static bool
vf_validate_gen6_elements(const struct ilo_dev *dev,
                          const struct ilo_state_vf_info *info)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 95:
    *
    *     "(Source Element Offset (in bytes))
    *      Format: U11
    *      Range [0,2047"
    *
    * From the Haswell PRM, volume 2d, page 415:
    *
    *     "(Source Element Offset)
    *      Format: U12 byte offset
    *      ...
    *      [0,4095]"
    *
    * From the Broadwell PRM, volume 2d, page 469:
    *
    *     "(Source Element Offset)
    *      Format: U12 byte offset
    *      ...
    *      [0,2047]"
    */
   const uint16_t max_vertex_offset =
      (ilo_dev_gen(dev) == ILO_GEN(7.5)) ? 4096 : 2048;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(info->element_count <= ILO_STATE_VF_MAX_ELEMENT_COUNT);

   for (i = 0; i < info->element_count; i++) {
      const struct ilo_state_vf_element_info *elem = &info->elements[i];

      assert(elem->buffer < ILO_STATE_VF_MAX_BUFFER_COUNT);
      assert(elem->vertex_offset < max_vertex_offset);
      assert(ilo_state_vf_valid_element_format(dev, elem->format));
   }

   return true;
}

static uint32_t
get_gen6_component_controls(const struct ilo_dev *dev,
                            enum gen_vf_component comp_x,
                            enum gen_vf_component comp_y,
                            enum gen_vf_component comp_z,
                            enum gen_vf_component comp_w)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   return comp_x << GEN6_VE_DW1_COMP0__SHIFT |
          comp_y << GEN6_VE_DW1_COMP1__SHIFT |
          comp_z << GEN6_VE_DW1_COMP2__SHIFT |
          comp_w << GEN6_VE_DW1_COMP3__SHIFT;
}

static bool
get_gen6_edge_flag_format(const struct ilo_dev *dev,
                          const struct ilo_state_vf_element_info *elem,
                          enum gen_surface_format *format)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 94:
    *
    *     "The Source Element Format must be set to the UINT format."
    *
    * From the Haswell PRM, volume 2d, page 413:
    *
    *     "The SourceElementFormat needs to be a single-component format with
    *      an element which has edge flag enabled."
    */
   if (elem->component_count != 1)
      return false;

   /* pick the format we like */
   switch (elem->format_size) {
   case 1:
      *format = GEN6_FORMAT_R8_UINT;
      break;
   case 2:
      *format = GEN6_FORMAT_R16_UINT;
      break;
   case 4:
      *format = GEN6_FORMAT_R32_UINT;
      break;
   default:
      return false;
      break;
   }

   return true;
}

static bool
vf_set_gen6_3DSTATE_VERTEX_ELEMENTS(struct ilo_state_vf *vf,
                                    const struct ilo_dev *dev,
                                    const struct ilo_state_vf_info *info)
{
   enum gen_surface_format edge_flag_format;
   uint32_t dw0, dw1;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!vf_validate_gen6_elements(dev, info))
      return false;

   for (i = 0; i < info->element_count; i++) {
      const struct ilo_state_vf_element_info *elem = &info->elements[i];
      enum gen_vf_component components[4] = {
         GEN6_VFCOMP_STORE_0,
         GEN6_VFCOMP_STORE_0,
         GEN6_VFCOMP_STORE_0,
         (elem->is_integer) ? GEN6_VFCOMP_STORE_1_INT :
                              GEN6_VFCOMP_STORE_1_FP,
      };

      switch (elem->component_count) {
      case 4: components[3] = GEN6_VFCOMP_STORE_SRC; /* fall through */
      case 3: components[2] = GEN6_VFCOMP_STORE_SRC; /* fall through */
      case 2: components[1] = GEN6_VFCOMP_STORE_SRC; /* fall through */
      case 1: components[0] = GEN6_VFCOMP_STORE_SRC; break;
      default:
              assert(!"unexpected component count");
              break;
      }

      dw0 = elem->buffer << GEN6_VE_DW0_VB_INDEX__SHIFT |
            GEN6_VE_DW0_VALID |
            elem->format << GEN6_VE_DW0_FORMAT__SHIFT |
            elem->vertex_offset << GEN6_VE_DW0_VB_OFFSET__SHIFT;
      dw1 = get_gen6_component_controls(dev,
            components[0], components[1],
            components[2], components[3]);

      STATIC_ASSERT(ARRAY_SIZE(vf->user_ve[i]) >= 2);
      vf->user_ve[i][0] = dw0;
      vf->user_ve[i][1] = dw1;
   }

   vf->user_ve_count = i;

   vf->edge_flag_supported = (i && get_gen6_edge_flag_format(dev,
         &info->elements[i - 1], &edge_flag_format));
   if (vf->edge_flag_supported) {
      const struct ilo_state_vf_element_info *elem = &info->elements[i - 1];

      /* without edge flag enable */
      vf->last_user_ve[0][0] = dw0;
      vf->last_user_ve[0][1] = dw1;

      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 94:
       *
       *     "This bit (Edge Flag Enable) must only be ENABLED on the last
       *      valid VERTEX_ELEMENT structure.
       *
       *      When set, Component 0 Control must be set to
       *      VFCOMP_STORE_SRC, and Component 1-3 Control must be set to
       *      VFCOMP_NOSTORE."
       */
      dw0 = elem->buffer << GEN6_VE_DW0_VB_INDEX__SHIFT |
            GEN6_VE_DW0_VALID |
            edge_flag_format << GEN6_VE_DW0_FORMAT__SHIFT |
            GEN6_VE_DW0_EDGE_FLAG_ENABLE |
            elem->vertex_offset << GEN6_VE_DW0_VB_OFFSET__SHIFT;
      dw1 = get_gen6_component_controls(dev, GEN6_VFCOMP_STORE_SRC,
            GEN6_VFCOMP_NOSTORE, GEN6_VFCOMP_NOSTORE, GEN6_VFCOMP_NOSTORE);

      /* with edge flag enable */
      vf->last_user_ve[1][0] = dw0;
      vf->last_user_ve[1][1] = dw1;
   }

   return true;
}

static bool
vf_set_gen6_vertex_buffer_state(struct ilo_state_vf *vf,
                                const struct ilo_dev *dev,
                                const struct ilo_state_vf_info *info)
{
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 7.5);

   memset(vf->vb_to_first_elem, -1, sizeof(vf->vb_to_first_elem));

   for (i = 0; i < info->element_count; i++) {
      const struct ilo_state_vf_element_info *elem = &info->elements[i];

      STATIC_ASSERT(ARRAY_SIZE(vf->user_instancing[i]) >= 2);
      /* instancing enable only */
      vf->user_instancing[i][0] = (elem->instancing_enable) ?
         GEN6_VB_DW0_ACCESS_INSTANCEDATA :
         GEN6_VB_DW0_ACCESS_VERTEXDATA;
      vf->user_instancing[i][1] = elem->instancing_step_rate;

      /*
       * Instancing is per VB, not per VE, before Gen8.  Set up a VB-to-VE
       * mapping as well.
       */
      if (vf->vb_to_first_elem[elem->buffer] < 0) {
         vf->vb_to_first_elem[elem->buffer] = i;
      } else {
         const struct ilo_state_vf_element_info *first =
            &info->elements[vf->vb_to_first_elem[elem->buffer]];

         assert(elem->instancing_enable == first->instancing_enable &&
                elem->instancing_step_rate == first->instancing_step_rate);
      }
   }

   return true;
}

static bool
vf_set_gen8_3DSTATE_VF_INSTANCING(struct ilo_state_vf *vf,
                                  const struct ilo_dev *dev,
                                  const struct ilo_state_vf_info *info)
{
   uint8_t i;

   ILO_DEV_ASSERT(dev, 8, 8);

   for (i = 0; i < info->element_count; i++) {
      const struct ilo_state_vf_element_info *elem = &info->elements[i];

      STATIC_ASSERT(ARRAY_SIZE(vf->user_instancing[i]) >= 2);
      vf->user_instancing[i][0] = (elem->instancing_enable) ?
         GEN8_INSTANCING_DW1_ENABLE : 0;
      vf->user_instancing[i][1] = elem->instancing_step_rate;
   }

   return true;
}

static uint32_t
get_gen6_component_zeros(const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   return get_gen6_component_controls(dev,
         GEN6_VFCOMP_STORE_0,
         GEN6_VFCOMP_STORE_0,
         GEN6_VFCOMP_STORE_0,
         GEN6_VFCOMP_STORE_0);
}

static uint32_t
get_gen6_component_ids(const struct ilo_dev *dev,
                       bool vertexid, bool instanceid)
{
   ILO_DEV_ASSERT(dev, 6, 7.5);

   return get_gen6_component_controls(dev,
      (vertexid) ? GEN6_VFCOMP_STORE_VID : GEN6_VFCOMP_STORE_0,
      (instanceid) ? GEN6_VFCOMP_STORE_IID : GEN6_VFCOMP_STORE_0,
      GEN6_VFCOMP_STORE_0,
      GEN6_VFCOMP_STORE_0);
}

static bool
vf_params_set_gen6_internal_ve(struct ilo_state_vf *vf,
                               const struct ilo_dev *dev,
                               const struct ilo_state_vf_params_info *params,
                               uint8_t user_ve_count)
{
   const bool prepend_ids =
      (params->prepend_vertexid || params->prepend_instanceid);
   uint8_t internal_ve_count = 0, i;
   uint32_t dw1[2];


   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 92:
    *
    *     "- At least one VERTEX_ELEMENT_STATE structure must be included.
    *
    *      - Inclusion of partial VERTEX_ELEMENT_STATE structures is
    *        UNDEFINED.
    *
    *      - SW must ensure that at least one vertex element is defined prior
    *        to issuing a 3DPRIMTIVE command, or operation is UNDEFINED.
    *
    *      - There are no "holes" allowed in the destination vertex: NOSTORE
    *        components must be overwritten by subsequent components unless
    *        they are the trailing DWords of the vertex.  Software must
    *        explicitly chose some value (probably 0) to be written into
    *        DWords that would otherwise be "holes"."
    *
    *      - ...
    *
    *      - [DevILK+] Element[0] must be valid."
    */
   if (params->prepend_zeros || (!user_ve_count && !prepend_ids))
      dw1[internal_ve_count++] = get_gen6_component_zeros(dev);

   if (prepend_ids) {
      if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
         /* placeholder for 3DSTATE_VF_SGVS */
         dw1[internal_ve_count++] = get_gen6_component_zeros(dev);
      } else {
         dw1[internal_ve_count++] = get_gen6_component_ids(dev,
               params->prepend_vertexid, params->prepend_instanceid);
      }
   }

   for (i = 0; i < internal_ve_count; i++) {
      STATIC_ASSERT(ARRAY_SIZE(vf->internal_ve[i]) >= 2);
      vf->internal_ve[i][0] = GEN6_VE_DW0_VALID;
      vf->internal_ve[i][1] = dw1[i];
   }

   vf->internal_ve_count = internal_ve_count;

   return true;
}

static bool
vf_params_set_gen8_3DSTATE_VF_SGVS(struct ilo_state_vf *vf,
                                   const struct ilo_dev *dev,
                                   const struct ilo_state_vf_params_info *params)
{
   const uint8_t attr = (params->prepend_zeros) ? 1 : 0;
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 8, 8);

   dw1 = 0;

   if (params->prepend_instanceid) {
      dw1 |= GEN8_SGVS_DW1_IID_ENABLE |
             1 << GEN8_SGVS_DW1_IID_VE_COMP__SHIFT |
             attr << GEN8_SGVS_DW1_IID_VE_INDEX__SHIFT;
   }

   if (params->prepend_vertexid) {
      dw1 |= GEN8_SGVS_DW1_VID_ENABLE |
             0 << GEN8_SGVS_DW1_VID_VE_COMP__SHIFT |
             attr << GEN8_SGVS_DW1_VID_VE_INDEX__SHIFT;
   }

   STATIC_ASSERT(ARRAY_SIZE(vf->sgvs) >= 1);
   vf->sgvs[0] = dw1;

   return true;
}

static uint32_t
get_gen6_fixed_cut_index(const struct ilo_dev *dev,
                         enum gen_index_format format)
{
   const uint32_t fixed = ~0u;

   ILO_DEV_ASSERT(dev, 6, 7);

   switch (format) {
   case GEN6_INDEX_BYTE:   return (uint8_t)  fixed;
   case GEN6_INDEX_WORD:   return (uint16_t) fixed;
   case GEN6_INDEX_DWORD:  return (uint32_t) fixed;
   default:
      assert(!"unknown index format");
      return fixed;
   }
}

static bool
get_gen6_cut_index_supported(const struct ilo_dev *dev,
                             enum gen_3dprim_type topology)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * See the Sandy Bridge PRM, volume 2 part 1, page 80 and the Haswell PRM,
    * volume 7, page 456.
    */
   switch (topology) {
   case GEN6_3DPRIM_TRIFAN:
   case GEN6_3DPRIM_QUADLIST:
   case GEN6_3DPRIM_QUADSTRIP:
   case GEN6_3DPRIM_POLYGON:
   case GEN6_3DPRIM_LINELOOP:
      return (ilo_dev_gen(dev) >= ILO_GEN(7.5));
   case GEN6_3DPRIM_RECTLIST:
   case GEN6_3DPRIM_TRIFAN_NOSTIPPLE:
      return false;
   default:
      return true;
   }
}

static bool
vf_params_set_gen6_3dstate_index_buffer(struct ilo_state_vf *vf,
                                        const struct ilo_dev *dev,
                                        const struct ilo_state_vf_params_info *params)
{
   uint32_t dw0 = 0;

   ILO_DEV_ASSERT(dev, 6, 7);

   /* cut index only, as in 3DSTATE_VF */
   if (params->cut_index_enable) {
      assert(get_gen6_cut_index_supported(dev, params->cv_topology));
      assert(get_gen6_fixed_cut_index(dev, params->cv_index_format) ==
            params->cut_index);

      dw0 |= GEN6_IB_DW0_CUT_INDEX_ENABLE;
   }

   STATIC_ASSERT(ARRAY_SIZE(vf->cut) >= 1);
   vf->cut[0] = dw0;

   return true;
}

static bool
vf_params_set_gen75_3DSTATE_VF(struct ilo_state_vf *vf,
                               const struct ilo_dev *dev,
                               const struct ilo_state_vf_params_info *params)
{
   uint32_t dw0 = 0;

   ILO_DEV_ASSERT(dev, 7.5, 8);

   if (params->cut_index_enable) {
      assert(get_gen6_cut_index_supported(dev, params->cv_topology));
      dw0 |= GEN75_VF_DW0_CUT_INDEX_ENABLE;
   }

   STATIC_ASSERT(ARRAY_SIZE(vf->cut) >= 2);
   vf->cut[0] = dw0;
   vf->cut[1] = params->cut_index;

   return true;
}

static bool
vertex_buffer_validate_gen6(const struct ilo_dev *dev,
                            const struct ilo_state_vertex_buffer_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   if (info->vma)
      assert(info->size && info->offset + info->size <= info->vma->vm_size);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 86:
    *
    *     "(Buffer Pitch)
    *      Range  [DevCTG+]: [0,2048] Bytes"
    */
   assert(info->stride <= 2048);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 86:
    *
    *     "64-bit floating point values must be 64-bit aligned in memory, or
    *      UNPREDICTABLE data will be fetched. When accessing an element
    *      containing 64-bit floating point values, the Buffer Starting
    *      Address and Source Element Offset values must add to a 64-bit
    *      aligned address, and BufferPitch must be a multiple of 64-bits."
    */
   if (info->cv_has_double) {
      if (info->vma)
         assert(info->vma->vm_alignment % 8 == 0);

      assert(info->stride % 8 == 0);
      assert((info->offset + info->cv_double_vertex_offset_mod_8) % 8 == 0);
   }

   return true;
}

static uint32_t
vertex_buffer_get_gen6_size(const struct ilo_dev *dev,
                            const struct ilo_state_vertex_buffer_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);
   return (info->vma) ? info->size : 0;
}

static bool
vertex_buffer_set_gen8_vertex_buffer_state(struct ilo_state_vertex_buffer *vb,
                                           const struct ilo_dev *dev,
                                           const struct ilo_state_vertex_buffer_info *info)
{
   const uint32_t size = vertex_buffer_get_gen6_size(dev, info);
   uint32_t dw0;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!vertex_buffer_validate_gen6(dev, info))
      return false;

   dw0 = info->stride << GEN6_VB_DW0_PITCH__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      dw0 |= GEN7_VB_DW0_ADDR_MODIFIED;
   if (!info->vma)
      dw0 |= GEN6_VB_DW0_IS_NULL;

   STATIC_ASSERT(ARRAY_SIZE(vb->vb) >= 3);
   vb->vb[0] = dw0;
   vb->vb[1] = info->offset;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      vb->vb[2] = size;
   } else {
      /* address of the last valid byte */
      vb->vb[2] = (size) ? info->offset + size - 1 : 0;
   }

   vb->vma = info->vma;

   return true;
}

static uint32_t
get_index_format_size(enum gen_index_format format)
{
   switch (format) {
   case GEN6_INDEX_BYTE:   return 1;
   case GEN6_INDEX_WORD:   return 2;
   case GEN6_INDEX_DWORD:  return 4;
   default:
      assert(!"unknown index format");
      return 1;
   }
}

static bool
index_buffer_validate_gen6(const struct ilo_dev *dev,
                           const struct ilo_state_index_buffer_info *info)
{
   const uint32_t format_size = get_index_format_size(info->format);

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 79:
    *
    *     "This field (Buffer Starting Address) contains the size-aligned (as
    *      specified by Index Format) Graphics Address of the first element of
    *      interest within the index buffer."
    */
   assert(info->offset % format_size == 0);

   if (info->vma) {
      assert(info->vma->vm_alignment % format_size == 0);
      assert(info->size && info->offset + info->size <= info->vma->vm_size);
   }

   return true;
}

static uint32_t
index_buffer_get_gen6_size(const struct ilo_dev *dev,
                           const struct ilo_state_index_buffer_info *info)
{
   uint32_t size;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!info->vma)
      return 0;

   size = info->size;
   if (ilo_dev_gen(dev) < ILO_GEN(8)) {
      const uint32_t format_size = get_index_format_size(info->format);
      size -= (size % format_size);
   }

   return size;
}

static bool
index_buffer_set_gen8_3DSTATE_INDEX_BUFFER(struct ilo_state_index_buffer *ib,
                                           const struct ilo_dev *dev,
                                           const struct ilo_state_index_buffer_info *info)
{
   const uint32_t size = index_buffer_get_gen6_size(dev, info);

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!index_buffer_validate_gen6(dev, info))
      return false;

   STATIC_ASSERT(ARRAY_SIZE(ib->ib) >= 3);
   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      ib->ib[0] = info->format << GEN8_IB_DW1_FORMAT__SHIFT;
      ib->ib[1] = info->offset;
      ib->ib[2] = size;
   } else {
      ib->ib[0] = info->format << GEN6_IB_DW0_FORMAT__SHIFT;
      ib->ib[1] = info->offset;
      /* address of the last valid byte, or 0 */
      ib->ib[2] = (size) ? info->offset + size - 1 : 0;
   }

   ib->vma = info->vma;

   return true;
}

bool
ilo_state_vf_valid_element_format(const struct ilo_dev *dev,
                                  enum gen_surface_format format)
{
   /*
    * This table is based on:
    *
    *  - the Sandy Bridge PRM, volume 4 part 1, page 88-97
    *  - the Ivy Bridge PRM, volume 2 part 1, page 97-99
    *  - the Haswell PRM, volume 7, page 467-470
    */
   static const int vf_element_formats[] = {
      [GEN6_FORMAT_R32G32B32A32_FLOAT]       = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_SINT]        = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_UINT]        = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_UNORM]       = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_SNORM]       = ILO_GEN(  1),
      [GEN6_FORMAT_R64G64_FLOAT]             = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_SSCALED]     = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_USCALED]     = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32A32_SFIXED]      = ILO_GEN(7.5),
      [GEN6_FORMAT_R32G32B32_FLOAT]          = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_SINT]           = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_UINT]           = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_UNORM]          = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_SNORM]          = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_SSCALED]        = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_USCALED]        = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32B32_SFIXED]         = ILO_GEN(7.5),
      [GEN6_FORMAT_R16G16B16A16_UNORM]       = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16A16_SNORM]       = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16A16_SINT]        = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16A16_UINT]        = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16A16_FLOAT]       = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_FLOAT]             = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_SINT]              = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_UINT]              = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_UNORM]             = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_SNORM]             = ILO_GEN(  1),
      [GEN6_FORMAT_R64_FLOAT]                = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16A16_SSCALED]     = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16A16_USCALED]     = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_SSCALED]           = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_USCALED]           = ILO_GEN(  1),
      [GEN6_FORMAT_R32G32_SFIXED]            = ILO_GEN(7.5),
      [GEN6_FORMAT_B8G8R8A8_UNORM]           = ILO_GEN(  1),
      [GEN6_FORMAT_R10G10B10A2_UNORM]        = ILO_GEN(  1),
      [GEN6_FORMAT_R10G10B10A2_UINT]         = ILO_GEN(  1),
      [GEN6_FORMAT_R10G10B10_SNORM_A2_UNORM] = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8A8_UNORM]           = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8A8_SNORM]           = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8A8_SINT]            = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8A8_UINT]            = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_UNORM]             = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_SNORM]             = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_SINT]              = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_UINT]              = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_FLOAT]             = ILO_GEN(  1),
      [GEN6_FORMAT_B10G10R10A2_UNORM]        = ILO_GEN(7.5),
      [GEN6_FORMAT_R11G11B10_FLOAT]          = ILO_GEN(  1),
      [GEN6_FORMAT_R32_SINT]                 = ILO_GEN(  1),
      [GEN6_FORMAT_R32_UINT]                 = ILO_GEN(  1),
      [GEN6_FORMAT_R32_FLOAT]                = ILO_GEN(  1),
      [GEN6_FORMAT_R32_UNORM]                = ILO_GEN(  1),
      [GEN6_FORMAT_R32_SNORM]                = ILO_GEN(  1),
      [GEN6_FORMAT_R10G10B10X2_USCALED]      = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8A8_SSCALED]         = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8A8_USCALED]         = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_SSCALED]           = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16_USCALED]           = ILO_GEN(  1),
      [GEN6_FORMAT_R32_SSCALED]              = ILO_GEN(  1),
      [GEN6_FORMAT_R32_USCALED]              = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8_UNORM]               = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8_SNORM]               = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8_SINT]                = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8_UINT]                = ILO_GEN(  1),
      [GEN6_FORMAT_R16_UNORM]                = ILO_GEN(  1),
      [GEN6_FORMAT_R16_SNORM]                = ILO_GEN(  1),
      [GEN6_FORMAT_R16_SINT]                 = ILO_GEN(  1),
      [GEN6_FORMAT_R16_UINT]                 = ILO_GEN(  1),
      [GEN6_FORMAT_R16_FLOAT]                = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8_SSCALED]             = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8_USCALED]             = ILO_GEN(  1),
      [GEN6_FORMAT_R16_SSCALED]              = ILO_GEN(  1),
      [GEN6_FORMAT_R16_USCALED]              = ILO_GEN(  1),
      [GEN6_FORMAT_R8_UNORM]                 = ILO_GEN(  1),
      [GEN6_FORMAT_R8_SNORM]                 = ILO_GEN(  1),
      [GEN6_FORMAT_R8_SINT]                  = ILO_GEN(  1),
      [GEN6_FORMAT_R8_UINT]                  = ILO_GEN(  1),
      [GEN6_FORMAT_R8_SSCALED]               = ILO_GEN(  1),
      [GEN6_FORMAT_R8_USCALED]               = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8_UNORM]             = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8_SNORM]             = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8_SSCALED]           = ILO_GEN(  1),
      [GEN6_FORMAT_R8G8B8_USCALED]           = ILO_GEN(  1),
      [GEN6_FORMAT_R64G64B64A64_FLOAT]       = ILO_GEN(  1),
      [GEN6_FORMAT_R64G64B64_FLOAT]          = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16_FLOAT]          = ILO_GEN(  6),
      [GEN6_FORMAT_R16G16B16_UNORM]          = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16_SNORM]          = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16_SSCALED]        = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16_USCALED]        = ILO_GEN(  1),
      [GEN6_FORMAT_R16G16B16_UINT]           = ILO_GEN(7.5),
      [GEN6_FORMAT_R16G16B16_SINT]           = ILO_GEN(7.5),
      [GEN6_FORMAT_R32_SFIXED]               = ILO_GEN(7.5),
      [GEN6_FORMAT_R10G10B10A2_SNORM]        = ILO_GEN(7.5),
      [GEN6_FORMAT_R10G10B10A2_USCALED]      = ILO_GEN(7.5),
      [GEN6_FORMAT_R10G10B10A2_SSCALED]      = ILO_GEN(7.5),
      [GEN6_FORMAT_R10G10B10A2_SINT]         = ILO_GEN(7.5),
      [GEN6_FORMAT_B10G10R10A2_SNORM]        = ILO_GEN(7.5),
      [GEN6_FORMAT_B10G10R10A2_USCALED]      = ILO_GEN(7.5),
      [GEN6_FORMAT_B10G10R10A2_SSCALED]      = ILO_GEN(7.5),
      [GEN6_FORMAT_B10G10R10A2_UINT]         = ILO_GEN(7.5),
      [GEN6_FORMAT_B10G10R10A2_SINT]         = ILO_GEN(7.5),
      [GEN6_FORMAT_R8G8B8_UINT]              = ILO_GEN(7.5),
      [GEN6_FORMAT_R8G8B8_SINT]              = ILO_GEN(7.5),
   };

   ILO_DEV_ASSERT(dev, 6, 8);

   return (format < ARRAY_SIZE(vf_element_formats) &&
           vf_element_formats[format] &&
           ilo_dev_gen(dev) >= vf_element_formats[format]);
}

bool
ilo_state_vf_init(struct ilo_state_vf *vf,
                  const struct ilo_dev *dev,
                  const struct ilo_state_vf_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(vf, sizeof(*vf)));
   assert(ilo_is_zeroed(info->data, info->data_size));

   assert(ilo_state_vf_data_size(dev, info->element_count) <=
         info->data_size);
   vf->user_ve = (uint32_t (*)[2]) info->data;
   vf->user_instancing =
      (uint32_t (*)[2]) (vf->user_ve + info->element_count);

   ret &= vf_set_gen6_3DSTATE_VERTEX_ELEMENTS(vf, dev, info);

   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      ret &= vf_set_gen8_3DSTATE_VF_INSTANCING(vf, dev, info);
   else
      ret &= vf_set_gen6_vertex_buffer_state(vf, dev, info);

   ret &= ilo_state_vf_set_params(vf, dev, &info->params);

   assert(ret);

   return ret;
}

bool
ilo_state_vf_init_for_rectlist(struct ilo_state_vf *vf,
                               const struct ilo_dev *dev,
                               void *data, size_t data_size,
                               const struct ilo_state_vf_element_info *elements,
                               uint8_t element_count)
{
   struct ilo_state_vf_info info;

   memset(&info, 0, sizeof(info));

   info.data = data;
   info.data_size = data_size;

   info.elements = elements;
   info.element_count = element_count;

   /*
    * For VUE header,
    *
    *   DW0: Reserved: MBZ
    *   DW1: Render Target Array Index
    *   DW2: Viewport Index
    *   DW3: Point Width
    */
   info.params.prepend_zeros = true;

   return ilo_state_vf_init(vf, dev, &info);
}

bool
ilo_state_vf_set_params(struct ilo_state_vf *vf,
                        const struct ilo_dev *dev,
                        const struct ilo_state_vf_params_info *params)
{
   bool ret = true;

   ILO_DEV_ASSERT(dev, 6, 8);

   ret &= vf_params_set_gen6_internal_ve(vf, dev, params, vf->user_ve_count);
   if (ilo_dev_gen(dev) >= ILO_GEN(8))
      ret &= vf_params_set_gen8_3DSTATE_VF_SGVS(vf, dev, params);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 94:
    *
    *     "Edge flags are supported for the following primitive topology types
    *      only, otherwise EdgeFlagEnable must not be ENABLED.
    *
    *      - 3DPRIM_TRILIST*
    *      - 3DPRIM_TRISTRIP*
    *      - 3DPRIM_TRIFAN*
    *      - 3DPRIM_POLYGON"
    *
    *     "[DevSNB]: Edge Flags are not supported for QUADLIST primitives.
    *      Software may elect to convert QUADLIST primitives to some set of
    *      corresponding edge-flag-supported primitive types (e.g., POLYGONs)
    *      prior to submission to the 3D vf."
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 86:
    *
    *     "Edge flags are supported for all primitive topology types."
    *
    * Both PRMs are confusing...
    */
   if (params->last_element_edge_flag) {
      assert(vf->edge_flag_supported);
      if (ilo_dev_gen(dev) == ILO_GEN(6))
         assert(params->cv_topology != GEN6_3DPRIM_QUADLIST);
   }

   if (vf->edge_flag_supported) {
      assert(vf->user_ve_count);
      memcpy(vf->user_ve[vf->user_ve_count - 1],
            vf->last_user_ve[params->last_element_edge_flag],
            sizeof(vf->user_ve[vf->user_ve_count - 1]));
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      ret &= vf_params_set_gen75_3DSTATE_VF(vf, dev, params);
   else
      ret &= vf_params_set_gen6_3dstate_index_buffer(vf, dev, params);

   assert(ret);

   return ret;
}

void
ilo_state_vf_full_delta(const struct ilo_state_vf *vf,
                        const struct ilo_dev *dev,
                        struct ilo_state_vf_delta *delta)
{
   delta->dirty = ILO_STATE_VF_3DSTATE_VERTEX_ELEMENTS;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      delta->dirty |= ILO_STATE_VF_3DSTATE_VF_SGVS |
                      ILO_STATE_VF_3DSTATE_VF_INSTANCING;
   } else {
      delta->dirty |= ILO_STATE_VF_3DSTATE_VERTEX_BUFFERS;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      delta->dirty |= ILO_STATE_VF_3DSTATE_VF;
   else
      delta->dirty |= ILO_STATE_VF_3DSTATE_INDEX_BUFFER;
}

void
ilo_state_vf_get_delta(const struct ilo_state_vf *vf,
                       const struct ilo_dev *dev,
                       const struct ilo_state_vf *old,
                       struct ilo_state_vf_delta *delta)
{
   /* no shallow copying */
   assert(vf->user_ve != old->user_ve &&
          vf->user_instancing != old->user_instancing);

   delta->dirty = 0;

   if (vf->internal_ve_count != old->internal_ve_count ||
       vf->user_ve_count != old->user_ve_count ||
       memcmp(vf->internal_ve, old->internal_ve,
          sizeof(vf->internal_ve[0]) * vf->internal_ve_count) ||
       memcmp(vf->user_ve, old->user_ve,
          sizeof(vf->user_ve[0]) * vf->user_ve_count))
      delta->dirty |= ILO_STATE_VF_3DSTATE_VERTEX_ELEMENTS;

   if (vf->user_ve_count != old->user_ve_count ||
       memcmp(vf->user_instancing, old->user_instancing,
          sizeof(vf->user_instancing[0]) * vf->user_ve_count)) {
      if (ilo_dev_gen(dev) >= ILO_GEN(8))
         delta->dirty |= ILO_STATE_VF_3DSTATE_VF_INSTANCING;
      else
         delta->dirty |= ILO_STATE_VF_3DSTATE_VERTEX_BUFFERS;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      if (vf->sgvs[0] != old->sgvs[0])
         delta->dirty |= ILO_STATE_VF_3DSTATE_VF_SGVS;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5)) {
      if (memcmp(vf->cut, old->cut, sizeof(vf->cut)))
         delta->dirty |= ILO_STATE_VF_3DSTATE_VF;
   } else {
      if (vf->cut[0] != old->cut[0])
         delta->dirty |= ILO_STATE_VF_3DSTATE_INDEX_BUFFER;
   }
}

uint32_t
ilo_state_vertex_buffer_size(const struct ilo_dev *dev, uint32_t size,
                             uint32_t *alignment)
{
   /* align for doubles without padding */
   *alignment = 8;
   return size;
}

/**
 * No need to initialize first.
 */
bool
ilo_state_vertex_buffer_set_info(struct ilo_state_vertex_buffer *vb,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_vertex_buffer_info *info)
{
   bool ret = true;

   ret &= vertex_buffer_set_gen8_vertex_buffer_state(vb, dev, info);

   assert(ret);

   return ret;
}

uint32_t
ilo_state_index_buffer_size(const struct ilo_dev *dev, uint32_t size,
                            uint32_t *alignment)
{
   /* align for the worst case without padding */
   *alignment = get_index_format_size(GEN6_INDEX_DWORD);
   return size;
}

/**
 * No need to initialize first.
 */
bool
ilo_state_index_buffer_set_info(struct ilo_state_index_buffer *ib,
                                const struct ilo_dev *dev,
                                const struct ilo_state_index_buffer_info *info)
{
   bool ret = true;

   ret &= index_buffer_set_gen8_3DSTATE_INDEX_BUFFER(ib, dev, info);

   assert(ret);

   return ret;
}
