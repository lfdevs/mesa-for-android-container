/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2015 LunarG, Inc.
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

#ifndef ILO_STATE_VF_H
#define ILO_STATE_VF_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

/*
 * From the Sandy Bridge PRM, volume 2 part 1, page 93:
 *
 *     "Up to 34 (DevSNB+) vertex elements are supported."
 *
 *     "Up to 33 VBs are supported"
 *
 * Reserve two VEs and one VB for internal use.
 */
#define ILO_STATE_VF_MAX_ELEMENT_COUNT (34 - 2)
#define ILO_STATE_VF_MAX_BUFFER_COUNT (33 - 1)

enum ilo_state_vf_dirty_bits {
   ILO_STATE_VF_3DSTATE_VERTEX_ELEMENTS            = (1 << 0),
   ILO_STATE_VF_3DSTATE_VF_SGVS                    = (1 << 1),
   ILO_STATE_VF_3DSTATE_VF_INSTANCING              = (1 << 2),
   ILO_STATE_VF_3DSTATE_VERTEX_BUFFERS             = (1 << 3),
   ILO_STATE_VF_3DSTATE_VF                         = (1 << 4),
   ILO_STATE_VF_3DSTATE_INDEX_BUFFER               = (1 << 5),
};

/**
 * Fetch a 128-bit vertex attribute.
 */
struct ilo_state_vf_element_info {
   uint8_t buffer;
   uint16_t vertex_offset;
   enum gen_surface_format format;

   uint8_t format_size;
   uint8_t component_count;
   bool is_integer;

   /* must be the same for those share the same buffer before Gen8 */
   bool instancing_enable;
   uint32_t instancing_step_rate;
};

/**
 * VF parameters.
 */
struct ilo_state_vf_params_info {
   enum gen_3dprim_type cv_topology;

   /* prepend an attribute of zeros */
   bool prepend_zeros;

   /* prepend an attribute of VertexID and/or InstanceID */
   bool prepend_vertexid;
   bool prepend_instanceid;

   bool last_element_edge_flag;

   enum gen_index_format cv_index_format;
   bool cut_index_enable;
   uint32_t cut_index;
};

/**
 * Vertex fetch.
 */
struct ilo_state_vf_info {
   void *data;
   size_t data_size;

   const struct ilo_state_vf_element_info *elements;
   uint8_t element_count;

   struct ilo_state_vf_params_info params;
};

struct ilo_state_vf {
   uint32_t (*user_ve)[2];
   uint32_t (*user_instancing)[2];
   int8_t vb_to_first_elem[ILO_STATE_VF_MAX_BUFFER_COUNT];
   uint8_t user_ve_count;

   bool edge_flag_supported;
   uint32_t last_user_ve[2][2];

   /* two VEs are reserved for internal use */
   uint32_t internal_ve[2][2];
   uint8_t internal_ve_count;

   uint32_t sgvs[1];

   uint32_t cut[2];
};

struct ilo_state_vf_delta {
   uint32_t dirty;
};

struct ilo_vma;

struct ilo_state_vertex_buffer_info {
   const struct ilo_vma *vma;
   uint32_t offset;
   uint32_t size;

   uint16_t stride;

   /* doubles must be at 64-bit aligned addresses */
   bool cv_has_double;
   uint8_t cv_double_vertex_offset_mod_8;
};

struct ilo_state_vertex_buffer {
   uint32_t vb[3];

   const struct ilo_vma *vma;
};

struct ilo_state_index_buffer_info {
   const struct ilo_vma *vma;
   uint32_t offset;
   uint32_t size;

   enum gen_index_format format;
};

struct ilo_state_index_buffer {
   uint32_t ib[3];

   const struct ilo_vma *vma;
};

static inline size_t
ilo_state_vf_data_size(const struct ilo_dev *dev, uint8_t element_count)
{
   const struct ilo_state_vf *vf = NULL;
   return (sizeof(vf->user_ve[0]) +
           sizeof(vf->user_instancing[0])) * element_count;
}

bool
ilo_state_vf_valid_element_format(const struct ilo_dev *dev,
                                  enum gen_surface_format format);

bool
ilo_state_vf_init(struct ilo_state_vf *vf,
                  const struct ilo_dev *dev,
                  const struct ilo_state_vf_info *info);

bool
ilo_state_vf_init_for_rectlist(struct ilo_state_vf *vf,
                               const struct ilo_dev *dev,
                               void *data, size_t data_size,
                               const struct ilo_state_vf_element_info *elements,
                               uint8_t element_count);

bool
ilo_state_vf_set_params(struct ilo_state_vf *vf,
                        const struct ilo_dev *dev,
                        const struct ilo_state_vf_params_info *params);

/**
 * Return the number of attributes in the VUE.
 */
static inline uint8_t
ilo_state_vf_get_attr_count(const struct ilo_state_vf *vf)
{
   return vf->internal_ve_count + vf->user_ve_count;
}

void
ilo_state_vf_full_delta(const struct ilo_state_vf *vf,
                        const struct ilo_dev *dev,
                        struct ilo_state_vf_delta *delta);

void
ilo_state_vf_get_delta(const struct ilo_state_vf *vf,
                       const struct ilo_dev *dev,
                       const struct ilo_state_vf *old,
                       struct ilo_state_vf_delta *delta);

uint32_t
ilo_state_vertex_buffer_size(const struct ilo_dev *dev, uint32_t size,
                             uint32_t *alignment);

bool
ilo_state_vertex_buffer_set_info(struct ilo_state_vertex_buffer *vb,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_vertex_buffer_info *info);

uint32_t
ilo_state_index_buffer_size(const struct ilo_dev *dev, uint32_t size,
                            uint32_t *alignment);

bool
ilo_state_index_buffer_set_info(struct ilo_state_index_buffer *ib,
                                const struct ilo_dev *dev,
                                const struct ilo_state_index_buffer_info *info);

#endif /* ILO_STATE_VF_H */
