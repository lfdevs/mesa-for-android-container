/*
 * Copyright © 2026 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef V3DV_DESCRIPTOR_SET_H
#define V3DV_DESCRIPTOR_SET_H

#include "v3dv_common.h"
#include "v3dv_limits.h"
#include "v3dv_cl.h"
#include "common/v3d_limits.h"
#include "util/u_atomic.h"

struct v3dv_buffer;
struct v3dv_buffer_view;
struct v3dv_descriptor_state;
struct v3dv_device;
struct v3dv_image_view;
struct v3dv_pipeline_layout;
struct vk_ycbcr_conversion;

/* The following struct represents the info from a descriptor that we store on
 * the host memory. They are mostly links to other existing vulkan objects,
 * like the image_view in order to access to swizzle info, or the buffer used
 * for a UBO/SSBO, for example.
 *
 * FIXME: revisit if makes sense to just move everything that would be needed
 * from a descriptor to the bo.
 */
struct v3dv_descriptor {
   VkDescriptorType type;

   union {
      struct {
         struct v3dv_image_view *image_view;
         struct v3dv_sampler *sampler;
      };

      struct {
         struct v3dv_buffer *buffer;
         size_t offset;
         size_t range;
      };

      struct v3dv_buffer_view *buffer_view;
   };
};

struct v3dv_descriptor_pool_entry
{
   struct v3dv_descriptor_set *set;
   /* Offset and size of the subregion allocated for this entry from the
    * pool->bo
    */
   uint32_t offset;
   uint32_t size;
};

struct v3dv_descriptor_pool {
   struct vk_object_base base;

   /* A list with all descriptor sets allocated from the pool. */
   struct list_head set_list;

   /* If this descriptor pool has been allocated for the driver for internal
    * use, typically to implement meta operations.
    */
   bool is_driver_internal;

   struct v3dv_bo *bo;
   /* Current offset at the descriptor bo. 0 means that we didn't use it for
    * any descriptor. If the descriptor bo is NULL, current offset is
    * meaningless
    */
   uint32_t current_offset;

   /* If VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is not set the
    * descriptor sets are handled as a whole as pool memory and handled by the
    * following pointers. If set, they are not used, and individually
    * descriptor sets are allocated/freed.
    */
   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;

   uint32_t entry_count;
   uint32_t max_entry_count;
   struct v3dv_descriptor_pool_entry entries[0];
};

struct v3dv_descriptor_set {
   struct vk_object_base base;

   /* List link into the list of all sets allocated from the pool */
   struct list_head pool_link;

   struct v3dv_descriptor_pool *pool;

   struct v3dv_descriptor_set_layout *layout;

   /* Offset relative to the descriptor pool bo for this set */
   uint32_t base_offset;

   /* The descriptors below can be indexed (set/binding) using the set_layout
    */
   struct v3dv_descriptor descriptors[0];
};

struct v3dv_descriptor_set_binding_layout {
   VkDescriptorType type;

   /* Number of array elements in this binding */
   uint32_t array_size;

   /* Index into the flattened descriptor set */
   uint32_t descriptor_index;

   uint32_t dynamic_offset_count;
   uint32_t dynamic_offset_index;

   /* Offset into the descriptor set where this descriptor lives (final offset
    * on the descriptor bo need to take into account set->base_offset)
    */
   uint32_t descriptor_offset;

   /* Offset in the v3dv_descriptor_set_layout of the immutable samplers, or 0
    * if there are no immutable samplers.
    */
   uint32_t immutable_samplers_offset;

   /* Descriptors for multiplanar combined image samplers are larger.
    * For mutable descriptors, this is always 1.
    */
   uint8_t plane_stride;
};

struct v3dv_descriptor_set_layout {
   struct vk_object_base base;

   VkDescriptorSetLayoutCreateFlags flags;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Total bo size needed for this descriptor set
    */
   uint32_t bo_size;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   /* Number of descriptors in this descriptor set */
   uint32_t descriptor_count;

   /* Descriptor set layouts can be destroyed even if they are still being
    * used.
    */
   uint32_t ref_cnt;

   /* Bindings in this descriptor set */
   struct v3dv_descriptor_set_binding_layout binding[0];
};

void
v3dv_descriptor_set_layout_destroy(struct v3dv_device *device,
                                   struct v3dv_descriptor_set_layout *set_layout);

static inline void
v3dv_descriptor_set_layout_ref(struct v3dv_descriptor_set_layout *set_layout)
{
   assert(set_layout && set_layout->ref_cnt >= 1);
   p_atomic_inc(&set_layout->ref_cnt);
}

static inline void
v3dv_descriptor_set_layout_unref(struct v3dv_device *device,
                                 struct v3dv_descriptor_set_layout *set_layout)
{
   assert(set_layout && set_layout->ref_cnt >= 1);
   if (p_atomic_dec_zero(&set_layout->ref_cnt))
      v3dv_descriptor_set_layout_destroy(device, set_layout);
}

/* Number of entries the sampler map reserves for the "no sampler" cases, see
 * V3DV_NO_SAMPLER_16BIT_IDX below. They are added before any real sampler, so
 * the sampler map needs room for them on top of V3D_MAX_TEXTURE_SAMPLERS.
 */
#define V3DV_NUM_NO_SAMPLER_IDX 2

/*
 * We are using descriptor maps for ubo/ssbo and texture/samplers, so we need
 * it to be big enough to include the max value for all of them.
 *
 * FIXME: one alternative would be to allocate the map as big as you need for
 * each descriptor type. That would means more individual allocations.
 */
#define DESCRIPTOR_MAP_SIZE MAX3(V3D_MAX_TEXTURE_SAMPLERS +                        \
                                 V3DV_NUM_NO_SAMPLER_IDX,                          \
                                 MAX_UNIFORM_BUFFERS + MAX_INLINE_UNIFORM_BUFFERS, \
                                 MAX_STORAGE_BUFFERS)


struct v3dv_descriptor_map {
   /* FIXME: avoid fixed size array/justify the size */
   unsigned num_desc; /* Number of descriptors  */
   int set[DESCRIPTOR_MAP_SIZE];
   int binding[DESCRIPTOR_MAP_SIZE];
   int array_index[DESCRIPTOR_MAP_SIZE];
   int array_size[DESCRIPTOR_MAP_SIZE];
   uint8_t plane[DESCRIPTOR_MAP_SIZE];
   bool used[DESCRIPTOR_MAP_SIZE];

   /* NOTE: the following is only for sampler, but this is the easier place to
    * put it.
    */
   bool sampler_is_32b[DESCRIPTOR_MAP_SIZE];
};

struct v3dv_sampler {
   struct vk_object_base base;
   struct vk_ycbcr_conversion *conversion;

   bool compare_enable;
   bool unnormalized_coordinates;

   /* Prepacked per plane SAMPLER_STATE, that is referenced as part of the tmu
    * configuration. If needed it will be copied to the descriptor info during
    * UpdateDescriptorSets
    */
   uint8_t plane_count;
   uint8_t sampler_state[V3DV_SAMPLER_STATE_LENGTH];
};

/* We keep two special values for the sampler idx that represents exactly when a
 * sampler is not needed/provided. The main use is that even if we don't have
 * sampler, we still need to do the output unpacking (through
 * nir_lower_tex). The easier way to do this is to add those special "no
 * sampler" in the sampler_map, and then use the proper unpacking for that
 * case.
 *
 * We have one when we want a 16bit output size, and other when we want a
 * 32bit output size. We use the info coming from the RelaxedPrecision
 * decoration to decide between one and the other.
 */
#define V3DV_NO_SAMPLER_16BIT_IDX 0
#define V3DV_NO_SAMPLER_32BIT_IDX 1

struct v3dv_descriptor *
v3dv_descriptor_map_get_descriptor(struct v3dv_descriptor_state *descriptor_state,
                                   struct v3dv_descriptor_map *map,
                                   struct v3dv_pipeline_layout *pipeline_layout,
                                   uint32_t index,
                                   uint32_t *dynamic_offset);

struct v3dv_cl_reloc
v3dv_descriptor_map_get_descriptor_bo(struct v3dv_device *device,
                                      struct v3dv_descriptor_state *descriptor_state,
                                      struct v3dv_descriptor_map *map,
                                      struct v3dv_pipeline_layout *pipeline_layout,
                                      uint32_t index,
                                      VkDescriptorType *out_type);

const struct v3dv_sampler *
v3dv_descriptor_map_get_sampler(struct v3dv_descriptor_state *descriptor_state,
                                struct v3dv_descriptor_map *map,
                                struct v3dv_pipeline_layout *pipeline_layout,
                                uint32_t index);

struct v3dv_cl_reloc
v3dv_descriptor_map_get_sampler_state(struct v3dv_device *device,
                                      struct v3dv_descriptor_state *descriptor_state,
                                      struct v3dv_descriptor_map *map,
                                      struct v3dv_pipeline_layout *pipeline_layout,
                                      uint32_t index);

struct v3dv_cl_reloc
v3dv_descriptor_map_get_texture_shader_state(struct v3dv_device *device,
                                             struct v3dv_descriptor_state *descriptor_state,
                                             struct v3dv_descriptor_map *map,
                                             struct v3dv_pipeline_layout *pipeline_layout,
                                             uint32_t index);

struct v3dv_bo*
v3dv_descriptor_map_get_texture_bo(struct v3dv_descriptor_state *descriptor_state,
                                   struct v3dv_descriptor_map *map,
                                   struct v3dv_pipeline_layout *pipeline_layout,
                                   uint32_t index);

static inline const struct v3dv_sampler *
v3dv_immutable_samplers(const struct v3dv_descriptor_set_layout *set,
                        const struct v3dv_descriptor_set_binding_layout *binding)
{
   assert(binding->immutable_samplers_offset);
   return (const struct v3dv_sampler *) ((const char *) set + binding->immutable_samplers_offset);
}

VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_descriptor_set_layout, base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_sampler, base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)

#endif /* V3DV_DESCRIPTOR_SET_H */
