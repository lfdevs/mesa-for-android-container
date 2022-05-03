/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef DZN_PRIVATE_H
#define DZN_PRIVATE_H

#include "vk_command_pool.h"
#include "vk_command_buffer.h"
#include "vk_cmd_queue.h"
#include "vk_debug_report.h"
#include "vk_device.h"
#include "vk_image.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_sync.h"
#include "vk_queue.h"
#include "vk_shader_module.h"
#include "wsi_common.h"

#include "util/bitset.h"
#include "util/blob.h"
#include "util/hash_table.h"
#include "util/u_dynarray.h"
#include "util/log.h"

#include "shader_enums.h"

#include "dzn_entrypoints.h"
#include "dzn_nir.h"

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#include <dxgi1_4.h>

#define D3D12_IGNORE_SDK_LAYERS
#include <directx/d3d12.h>
#include <wrl/client.h>

#include "spirv_to_dxil.h"

using Microsoft::WRL::ComPtr;

#define DZN_SWAP(a, b) \
   do { \
      auto __tmp = a; \
      a = b; \
      b = __tmp; \
   } while (0)

#define dzn_stub() unreachable("Unsupported feature")

struct dxil_validator;

struct dzn_instance;
struct dzn_device;

struct dzn_meta_indirect_draw {
   ID3D12RootSignature *root_sig;
   ID3D12PipelineState *pipeline_state;
};

enum dzn_index_type {
   DZN_NO_INDEX,
   DZN_INDEX_2B,
   DZN_INDEX_4B,
   DZN_NUM_INDEX_TYPE,
};

static inline enum dzn_index_type
dzn_index_type_from_size(uint8_t index_size)
{
   switch (index_size) {
   case 0: return DZN_NO_INDEX;
   case 2: return DZN_INDEX_2B;
   case 4: return DZN_INDEX_4B;
   default: unreachable("Invalid index size");
   }
}

static inline enum dzn_index_type
dzn_index_type_from_dxgi_format(DXGI_FORMAT format)
{
   switch (format) {
   case DXGI_FORMAT_UNKNOWN: return DZN_NO_INDEX;
   case DXGI_FORMAT_R16_UINT: return DZN_INDEX_2B;
   case DXGI_FORMAT_R32_UINT: return DZN_INDEX_4B;
   default: unreachable("Invalid index format");
   }
}

static inline uint8_t
dzn_index_size(enum dzn_index_type type)
{
   switch (type) {
   case DZN_NO_INDEX: return 0;
   case DZN_INDEX_2B: return 2;
   case DZN_INDEX_4B: return 4;
   default: unreachable("Invalid index type");
   }
}

struct dzn_meta_triangle_fan_rewrite_index {
   ID3D12RootSignature *root_sig;
   ID3D12PipelineState *pipeline_state;
   ID3D12CommandSignature *cmd_sig;
};

struct dzn_meta_blit_key {
   union {
      struct {
         DXGI_FORMAT out_format;
         uint32_t samples : 6;
         uint32_t loc : 4;
         uint32_t out_type : 4;
         uint32_t sampler_dim : 4;
         uint32_t src_is_array : 1;
         uint32_t resolve : 1;
         uint32_t linear_filter : 1;
         uint32_t padding : 11;
      };
      const uint64_t u64;
   };
};

struct dzn_meta_blit {
   ID3D12RootSignature *root_sig;
   ID3D12PipelineState *pipeline_state;
};

struct dzn_meta_blits {
   mtx_t shaders_lock;
   D3D12_SHADER_BYTECODE vs;
   struct hash_table *fs;
   mtx_t contexts_lock;
   struct hash_table_u64 *contexts;
};

const dzn_meta_blit *
dzn_meta_blits_get_context(dzn_device *device, const dzn_meta_blit_key *key);

#define MAX_SYNC_TYPES 2
#define MAX_QUEUE_FAMILIES 3

struct dzn_physical_device {
   struct vk_physical_device vk;
   struct list_head link;

   struct vk_device_extension_table supported_extensions;
   struct vk_physical_device_dispatch_table dispatch;

   IDXGIAdapter1 *adapter;
   DXGI_ADAPTER_DESC1 adapter_desc;

   uint32_t queue_family_count;
   struct {
      VkQueueFamilyProperties props;
      D3D12_COMMAND_QUEUE_DESC desc;
   } queue_families[MAX_QUEUE_FAMILIES];

   uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];

   struct wsi_device wsi_device;

   mtx_t dev_lock;
   ID3D12Device1 *dev;
   D3D_FEATURE_LEVEL feature_level;
   D3D12_FEATURE_DATA_ARCHITECTURE1 architecture;
   D3D12_FEATURE_DATA_D3D12_OPTIONS options;
   VkPhysicalDeviceMemoryProperties memory;
   D3D12_HEAP_FLAGS heap_flags_for_mem_type[VK_MAX_MEMORY_TYPES];
   const struct vk_sync_type *sync_types[MAX_SYNC_TYPES + 1];
   float timestamp_period;
};

D3D12_FEATURE_DATA_FORMAT_SUPPORT
dzn_physical_device_get_format_support(dzn_physical_device *pdev,
                                       VkFormat format);

uint32_t
dzn_physical_device_get_mem_type_mask_for_resource(const dzn_physical_device *pdev,
                                                   const D3D12_RESOURCE_DESC *desc);

#define dzn_debug_ignored_stype(sType) \
   mesa_logd("%s: ignored VkStructureType %u\n", __func__, (sType))

IDXGIFactory4 *
dxgi_get_factory(bool debug);

PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE
d3d12_get_serialize_root_sig(void);

void
d3d12_enable_debug_layer();

void
d3d12_enable_gpu_validation();

ID3D12Device1 *
d3d12_create_device(IUnknown *adapter, bool experimental_features);

struct dzn_queue {
   struct vk_queue vk;

   ID3D12CommandQueue *cmdqueue;
   ID3D12Fence *fence;
   uint64_t fence_point = 0;
};

struct dzn_device {
   struct vk_device vk;
   struct vk_device_extension_table enabled_extensions;
   struct vk_device_dispatch_table cmd_dispatch;

   ID3D12Device1 *dev;

   struct dzn_meta_indirect_draw indirect_draws[DZN_NUM_INDIRECT_DRAW_TYPES];
   struct dzn_meta_triangle_fan_rewrite_index triangle_fan[DZN_NUM_INDEX_TYPE];
   struct dzn_meta_blits blits;

   struct {
#define DZN_QUERY_REFS_SECTION_SIZE 4096
#define DZN_QUERY_REFS_ALL_ONES_OFFSET 0
#define DZN_QUERY_REFS_ALL_ZEROS_OFFSET (DZN_QUERY_REFS_ALL_ONES_OFFSET + DZN_QUERY_REFS_SECTION_SIZE)
#define DZN_QUERY_REFS_RES_SIZE (DZN_QUERY_REFS_ALL_ZEROS_OFFSET + DZN_QUERY_REFS_SECTION_SIZE)
      ID3D12Resource *refs;
   } queries;
};

void dzn_meta_finish(dzn_device *device);

VkResult dzn_meta_init(dzn_device *device);

const dzn_meta_blit *
dzn_meta_blits_get_context(dzn_device *device,
                           const dzn_meta_blit_key *key);

ID3D12RootSignature *
dzn_device_create_root_sig(dzn_device *device,
                           const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc);

struct dzn_device_memory {
   struct vk_object_base base;

   struct list_head link;

   ID3D12Heap *heap;
   VkDeviceSize size;
   D3D12_RESOURCE_STATES initial_state; /* initial state for this memory type */

   /* A buffer-resource spanning the entire heap, used for mapping memory */
   ID3D12Resource *map_res;

   VkDeviceSize map_size;
   void *map;
};

enum dzn_cmd_bindpoint_dirty {
   DZN_CMD_BINDPOINT_DIRTY_PIPELINE = 1 << 0,
   DZN_CMD_BINDPOINT_DIRTY_HEAPS = 1 << 1,
   DZN_CMD_BINDPOINT_DIRTY_SYSVALS = 1 << 2,
};

enum dzn_cmd_dirty {
   DZN_CMD_DIRTY_VIEWPORTS = 1 << 0,
   DZN_CMD_DIRTY_SCISSORS = 1 << 1,
   DZN_CMD_DIRTY_IB = 1 << 2,
   DZN_CMD_DIRTY_STENCIL_REF = 1 << 3,
   DZN_CMD_DIRTY_STENCIL_COMPARE_MASK = 1 << 4,
   DZN_CMD_DIRTY_STENCIL_WRITE_MASK = 1 << 5,
   DZN_CMD_DIRTY_BLEND_CONSTANTS = 1 << 6,
};

#define MAX_VBS 16
#define MAX_VP 16
#define MAX_SCISSOR 16
#define MAX_SETS 4
#define MAX_DYNAMIC_UNIFORM_BUFFERS 8
#define MAX_DYNAMIC_STORAGE_BUFFERS 4
#define MAX_DYNAMIC_BUFFERS                                                  \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)
#define MAX_PUSH_CONSTANT_DWORDS 32

#define NUM_BIND_POINT VK_PIPELINE_BIND_POINT_COMPUTE + 1
#define NUM_POOL_TYPES D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER + 1

#define dzn_foreach_pool_type(type) \
   for (D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; \
        type <= D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; \
        type = (D3D12_DESCRIPTOR_HEAP_TYPE)(type + 1))

struct dzn_cmd_event_signal {
   struct dzn_event *event;
   bool value;
};

struct dzn_cmd_buffer;

struct dzn_attachment {
   uint32_t idx;
   VkFormat format;
   uint32_t samples;
   union {
      bool color;
      struct {
         bool depth;
         bool stencil;
      };
   } clear;
   VkImageAspectFlags aspects;
   D3D12_RESOURCE_STATES before, last, after;
   struct {
      D3D12_RESOURCE_STATES before, last, after;
   } stencil;
};

struct dzn_attachment_ref {
   uint32_t idx;
   D3D12_RESOURCE_STATES before, during;
   struct {
      D3D12_RESOURCE_STATES before, during;
   } stencil;
   VkImageAspectFlags aspects;
};

struct dzn_descriptor_state {
   struct {
      const struct dzn_descriptor_set *set;
      uint32_t dynamic_offsets[MAX_DYNAMIC_BUFFERS];
   } sets[MAX_SETS];
   struct dzn_descriptor_heap *heaps[NUM_POOL_TYPES];
};

struct dzn_sampler;
struct dzn_image_view;

struct dzn_buffer_desc {
   VkDescriptorType type;
   const struct dzn_buffer *buffer;
   VkDeviceSize range;
   VkDeviceSize offset;
};

struct dzn_descriptor_heap {
   ID3D12Device *dev;
   ID3D12DescriptorHeap *heap;
   D3D12_DESCRIPTOR_HEAP_TYPE type;
   SIZE_T cpu_base;
   uint64_t gpu_base;
   uint32_t desc_count;
   uint32_t desc_sz;
};

D3D12_CPU_DESCRIPTOR_HANDLE
dzn_descriptor_heap_get_cpu_handle(const dzn_descriptor_heap *heap, uint32_t slot);

D3D12_GPU_DESCRIPTOR_HANDLE
dzn_descriptor_heap_get_gpu_handle(const dzn_descriptor_heap *heap, uint32_t slot);

void
dzn_descriptor_heap_write_image_view_desc(dzn_descriptor_heap *heap,
                                          uint32_t heap_offset,
                                          bool writeable,
                                          bool cube_as_2darray,
                                          const dzn_image_view *iview);

void
dzn_descriptor_heap_write_buffer_desc(dzn_descriptor_heap *heap,
                                      uint32_t heap_offset,
                                      bool writeable,
                                      const dzn_buffer_desc *bdesc);

void
dzn_descriptor_heap_copy(dzn_descriptor_heap *dst_heap, uint32_t dst_heap_offset,
                         const dzn_descriptor_heap *src_heap, uint32_t src_heap_offset,
                         uint32_t desc_count);

struct dzn_descriptor_heap_pool_entry {
   struct list_head link;
   dzn_descriptor_heap heap;
};

struct dzn_descriptor_heap_pool {
   const VkAllocationCallbacks *alloc;
   D3D12_DESCRIPTOR_HEAP_TYPE type;
   bool shader_visible;
   struct list_head active_heaps, free_heaps;
   uint32_t offset;
   uint32_t desc_sz;
};

void
dzn_descriptor_heap_pool_init(dzn_descriptor_heap_pool *pool,
                              dzn_device *device,
                              D3D12_DESCRIPTOR_HEAP_TYPE type,
                              bool shader_visible,
			      const VkAllocationCallbacks *alloc);

void
dzn_descriptor_heap_pool_finish(dzn_descriptor_heap_pool *pool);

void
dzn_descriptor_heap_pool_reset(dzn_descriptor_heap_pool *pool);

VkResult
dzn_descriptor_heap_pool_alloc_slots(dzn_descriptor_heap_pool *pool,
                                     dzn_device *device,
                                     uint32_t num_slots,
                                     dzn_descriptor_heap **heap,
                                     uint32_t *first_slot);

struct dzn_cmd_buffer_query_range {
   struct dzn_query_pool *qpool;
   uint32_t start, count;
};

struct dzn_cmd_buffer_query_pool_state {
   struct util_dynarray reset, collect, wait, signal;
};

struct dzn_internal_resource {
   struct list_head link;
   ID3D12Resource *res;
};

enum dzn_event_state {
   DZN_EVENT_STATE_EXTERNAL_WAIT = -1,
   DZN_EVENT_STATE_RESET = 0,
   DZN_EVENT_STATE_SET = 1,
};

struct dzn_cmd_buffer_push_constant_state {
   uint32_t offset;
   uint32_t end;
   uint32_t values[MAX_PUSH_CONSTANT_DWORDS];
};

struct dzn_cmd_buffer_state {
   struct dzn_framebuffer *framebuffer;
   D3D12_RECT render_area;
   const struct dzn_pipeline *pipeline;
   dzn_descriptor_heap *heaps[NUM_POOL_TYPES];
   struct dzn_render_pass *pass;
   struct {
      BITSET_DECLARE(dirty, MAX_VBS);
      D3D12_VERTEX_BUFFER_VIEW views[MAX_VBS];
   } vb;
   struct {
      D3D12_INDEX_BUFFER_VIEW view;
   } ib;
   struct {
      struct {
         struct {
            uint32_t ref, compare_mask, write_mask;
         } front, back;
      } stencil_test;
   } zsa;
   struct {
      float constants[4];
   } blend;
   D3D12_VIEWPORT viewports[MAX_VP];
   D3D12_RECT scissors[MAX_SCISSOR];
   struct {
      struct dzn_cmd_buffer_push_constant_state gfx, compute;
   } push_constant;
   uint32_t dirty;
   uint32_t subpass;
   struct {
      struct dzn_pipeline *pipeline;
      struct dzn_descriptor_state desc_state;
      uint32_t dirty;
   } bindpoint[NUM_BIND_POINT];
   union {
      struct dxil_spirv_vertex_runtime_data gfx;
      struct dxil_spirv_compute_runtime_data compute;
   } sysvals;
};

struct dzn_cmd_buffer_rtv_key {
   const struct dzn_image *image;
   D3D12_RENDER_TARGET_VIEW_DESC desc;
};

struct dzn_cmd_buffer_rtv_entry {
   struct dzn_cmd_buffer_rtv_key key;
   D3D12_CPU_DESCRIPTOR_HANDLE handle;
};

struct dzn_cmd_buffer_dsv_key {
   const struct dzn_image *image;
   D3D12_DEPTH_STENCIL_VIEW_DESC desc;
};

struct dzn_cmd_buffer_dsv_entry {
   struct dzn_cmd_buffer_dsv_key key;
   D3D12_CPU_DESCRIPTOR_HANDLE handle;
};

struct dzn_cmd_buffer {
   struct vk_command_buffer vk;
   VkResult error;
   struct dzn_cmd_buffer_state state;

   struct {
      struct hash_table *ht;
      struct util_dynarray reset;
      struct util_dynarray wait;
      struct util_dynarray signal;
   } queries;

   struct {
      struct hash_table *ht;
      struct util_dynarray wait;
      struct util_dynarray signal;
   } events;

   struct {
      struct hash_table *ht;
      dzn_descriptor_heap_pool pool;
   } rtvs, dsvs;

   dzn_descriptor_heap_pool cbv_srv_uav_pool, sampler_pool;

   struct list_head internal_bufs;

   ID3D12CommandAllocator *cmdalloc;
   ID3D12GraphicsCommandList1 *cmdlist;
};

struct dzn_descriptor_pool {
   struct vk_object_base base;
   VkAllocationCallbacks alloc;

   uint32_t set_count;
   uint32_t used_set_count;
   dzn_descriptor_set *sets;
   dzn_descriptor_heap heaps[NUM_POOL_TYPES];
   uint32_t desc_count[NUM_POOL_TYPES];
   uint32_t used_desc_count[NUM_POOL_TYPES];
   uint32_t free_offset[NUM_POOL_TYPES];
   mtx_t defragment_lock;
};

#define MAX_SHADER_VISIBILITIES (D3D12_SHADER_VISIBILITY_PIXEL + 1)

struct dzn_descriptor_set_layout_binding {
   VkDescriptorType type;
   D3D12_SHADER_VISIBILITY visibility;
   uint32_t base_shader_register;
   uint32_t range_idx[NUM_POOL_TYPES];
   union {
      struct {
         uint32_t static_sampler_idx;
         uint32_t immutable_sampler_idx;
      };
      uint32_t dynamic_buffer_idx;
   };
};

struct dzn_descriptor_set_layout {
   struct vk_object_base base;
   uint32_t range_count[MAX_SHADER_VISIBILITIES][NUM_POOL_TYPES];
   const D3D12_DESCRIPTOR_RANGE1 *ranges[MAX_SHADER_VISIBILITIES][NUM_POOL_TYPES];
   uint32_t range_desc_count[NUM_POOL_TYPES];
   uint32_t static_sampler_count;
   const D3D12_STATIC_SAMPLER_DESC *static_samplers;
   uint32_t immutable_sampler_count;
   const dzn_sampler **immutable_samplers;
   struct {
      uint32_t bindings[MAX_DYNAMIC_BUFFERS];
      uint32_t count;
      uint32_t desc_count;
      uint32_t range_offset;
   } dynamic_buffers;
   uint32_t binding_count;
   const struct dzn_descriptor_set_layout_binding *bindings;
};

struct dzn_descriptor_set {
   struct vk_object_base base;
   struct dzn_buffer_desc dynamic_buffers[MAX_DYNAMIC_BUFFERS];
   dzn_descriptor_pool *pool;
   uint32_t heap_offsets[NUM_POOL_TYPES];
   uint32_t heap_sizes[NUM_POOL_TYPES];
   const struct dzn_descriptor_set_layout *layout;
};

struct dzn_pipeline_layout {
   struct vk_object_base base;
   int32_t refcount;
   struct {
      uint32_t heap_offsets[NUM_POOL_TYPES];
      struct {
         uint32_t srv, uav;
      } dynamic_buffer_heap_offsets[MAX_DYNAMIC_BUFFERS];
      uint32_t dynamic_buffer_count;
      uint32_t range_desc_count[NUM_POOL_TYPES];
   } sets[MAX_SETS];
   dxil_spirv_vulkan_descriptor_set binding_translation[MAX_SETS];
   uint32_t set_count;
   uint32_t desc_count[NUM_POOL_TYPES];
   struct {
      uint32_t param_count;
      uint32_t sets_param_count;
      uint32_t sysval_cbv_param_idx;
      uint32_t push_constant_cbv_param_idx;
      D3D12_DESCRIPTOR_HEAP_TYPE type[MAX_SHADER_VISIBILITIES];
      ID3D12RootSignature *sig;
   } root;
};

dzn_pipeline_layout *
dzn_pipeline_layout_ref(dzn_pipeline_layout *layout);

void
dzn_pipeline_layout_unref(dzn_pipeline_layout *layout);

#define MAX_RTS 8
#define MAX_INPUT_ATTACHMENTS 4

struct dzn_subpass {
   uint32_t color_count;
   struct dzn_attachment_ref colors[MAX_RTS];
   struct dzn_attachment_ref resolve[MAX_RTS];
   struct dzn_attachment_ref zs;
   uint32_t input_count;
   struct dzn_attachment_ref inputs[MAX_INPUT_ATTACHMENTS];
};

struct dzn_render_pass {
   struct vk_object_base base;
   uint32_t attachment_count;
   struct dzn_attachment *attachments;
   uint32_t subpass_count;
   struct dzn_subpass *subpasses;
};

struct dzn_pipeline_cache {
   struct vk_object_base base;
};

enum dzn_register_space {
   DZN_REGISTER_SPACE_SYSVALS = MAX_SETS,
   DZN_REGISTER_SPACE_PUSH_CONSTANT,
};

struct dzn_pipeline {
   struct vk_object_base base;
   VkPipelineBindPoint type;
   dzn_device *device;
   struct {
      uint32_t sets_param_count;
      uint32_t sysval_cbv_param_idx;
      uint32_t push_constant_cbv_param_idx;
      D3D12_DESCRIPTOR_HEAP_TYPE type[MAX_SHADER_VISIBILITIES];
      ID3D12RootSignature *sig;
   } root;
   struct {
      uint32_t heap_offsets[NUM_POOL_TYPES];
      struct {
         uint32_t srv, uav;
      } dynamic_buffer_heap_offsets[MAX_DYNAMIC_BUFFERS];
      uint32_t dynamic_buffer_count;
      uint32_t range_desc_count[NUM_POOL_TYPES];
   } sets[MAX_SETS];
   uint32_t desc_count[NUM_POOL_TYPES];
   ID3D12PipelineState *state;
};

enum dzn_indirect_draw_cmd_sig_type {
   DZN_INDIRECT_DRAW_CMD_SIG,
   DZN_INDIRECT_INDEXED_DRAW_CMD_SIG,
   DZN_INDIRECT_DRAW_TRIANGLE_FAN_CMD_SIG,
   DZN_NUM_INDIRECT_DRAW_CMD_SIGS,
};

struct dzn_graphics_pipeline {
   dzn_pipeline base;
   struct {
      unsigned count;
      uint32_t strides[MAX_VBS];
   } vb;

   struct {
      bool triangle_fan;
      D3D_PRIMITIVE_TOPOLOGY topology;
   } ia;

   struct {
      unsigned count;
      bool dynamic;
      D3D12_VIEWPORT desc[MAX_VP];
   } vp;

   struct {
      unsigned count;
      bool dynamic;
      D3D12_RECT desc[MAX_SCISSOR];
   } scissor;

   struct {
      struct {
         bool enable;
         bool independent_front_back;
         bool dynamic_ref;
         bool dynamic_write_mask;
         bool dynamic_compare_mask;
         struct {
            uint32_t ref;
            uint32_t write_mask;
            uint32_t compare_mask;
            bool uses_ref;
        } front, back;
      } stencil_test;
   } zsa;

   struct {
      bool dynamic_constants;
      float constants[4];
   } blend;

   ID3D12CommandSignature *indirect_cmd_sigs[DZN_NUM_INDIRECT_DRAW_CMD_SIGS];
};

ID3D12CommandSignature *
dzn_graphics_pipeline_get_indirect_cmd_sig(dzn_graphics_pipeline *pipeline,
                                           enum dzn_indirect_draw_cmd_sig_type cmd_sig_type);

struct dzn_compute_pipeline {
   dzn_pipeline base;
   struct {
      uint32_t x, y, z;
   } local_size;

   ID3D12CommandSignature *indirect_cmd_sig;
};

ID3D12CommandSignature *
dzn_compute_pipeline_get_indirect_cmd_sig(dzn_compute_pipeline *pipeline);

#define MAX_MIP_LEVELS 14

struct dzn_image {
   struct vk_image vk;

   struct {
      uint32_t row_stride = 0;
      uint32_t size = 0;
   } linear;
   D3D12_RESOURCE_DESC desc;
   ID3D12Resource *res;
   dzn_device_memory *mem;
   VkDeviceSize mem_offset;
};

DXGI_FORMAT
dzn_image_get_dxgi_format(VkFormat format,
                          VkImageUsageFlags usage,
                          VkImageAspectFlags aspects);

VkFormat
dzn_image_get_plane_format(VkFormat fmt, VkImageAspectFlags aspect);

DXGI_FORMAT
dzn_image_get_placed_footprint_format(VkFormat fmt, VkImageAspectFlags aspect);

D3D12_DEPTH_STENCIL_VIEW_DESC
dzn_image_get_dsv_desc(const dzn_image *image,
                       const VkImageSubresourceRange *range,
                       uint32_t level);

D3D12_RENDER_TARGET_VIEW_DESC
dzn_image_get_rtv_desc(const dzn_image *image,
                       const VkImageSubresourceRange *range,
                       uint32_t level);

D3D12_RESOURCE_STATES
dzn_image_layout_to_state(VkImageLayout layout, VkImageAspectFlagBits aspect);

uint32_t
dzn_image_layers_get_subresource_index(const dzn_image *image,
                                       const VkImageSubresourceLayers *subres,
                                       VkImageAspectFlagBits aspect,
                                       uint32_t layer);
uint32_t
dzn_image_range_get_subresource_index(const dzn_image *image,
                                      const VkImageSubresourceRange *range,
                                      VkImageAspectFlagBits aspect,
                                      uint32_t level, uint32_t layer);

D3D12_TEXTURE_COPY_LOCATION
dzn_image_get_copy_loc(const dzn_image *image,
                       const VkImageSubresourceLayers *layers,
                       VkImageAspectFlagBits aspect,
                       uint32_t layer);

struct dzn_image_view {
   struct vk_image_view vk;
   D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
   D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
   D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
   D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
};

void
dzn_image_view_init(dzn_device *device,
                    dzn_image_view *iview,
                    const VkImageViewCreateInfo *info);

void
dzn_image_view_finish(dzn_image_view *iview);

struct dzn_buffer {
   struct vk_object_base base;

   VkDeviceSize size;

   D3D12_RESOURCE_DESC desc;
   ID3D12Resource *res;

   VkBufferCreateFlags create_flags;
   VkBufferUsageFlags usage;
};

DXGI_FORMAT
dzn_buffer_get_dxgi_format(VkFormat format);

D3D12_TEXTURE_COPY_LOCATION
dzn_buffer_get_copy_loc(const dzn_buffer *buf, VkFormat format,
                        const VkBufferImageCopy2KHR *info,
                        VkImageAspectFlagBits aspect,
                        uint32_t layer);

D3D12_TEXTURE_COPY_LOCATION
dzn_buffer_get_line_copy_loc(const dzn_buffer *buf, VkFormat format,
                             const VkBufferImageCopy2KHR *region,
                             const D3D12_TEXTURE_COPY_LOCATION *loc,
                             uint32_t y, uint32_t z, uint32_t *start_x);

bool
dzn_buffer_supports_region_copy(const D3D12_TEXTURE_COPY_LOCATION *loc);

struct dzn_buffer_view {
   struct vk_object_base base;

   const dzn_buffer *buffer;

   D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
   D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
};

struct dzn_framebuffer {
   struct vk_object_base base;

   uint32_t width, height, layers;

   uint32_t attachment_count;
   struct dzn_image_view **attachments;
};

struct dzn_sampler {
   struct vk_object_base base;
   D3D12_SAMPLER_DESC desc;
   D3D12_STATIC_BORDER_COLOR static_border_color = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
};

/* This is defined as a macro so that it works for both
 * VkImageSubresourceRange and VkImageSubresourceLayers
 */
#define dzn_get_layer_count(_image, _range) \
   ((_range)->layerCount == VK_REMAINING_ARRAY_LAYERS ? \
    (_image)->vk.array_layers - (_range)->baseArrayLayer : (_range)->layerCount)

#define dzn_get_level_count(_image, _range) \
   ((_range)->levelCount == VK_REMAINING_MIP_LEVELS ? \
    (_image)->vk.mip_levels - (_range)->baseMipLevel : (_range)->levelCount)

#ifdef __cplusplus
extern "C" {
#endif
DXGI_FORMAT dzn_pipe_to_dxgi_format(enum pipe_format in);
D3D12_FILTER dzn_translate_sampler_filter(const VkSamplerCreateInfo *create_info);
D3D12_COMPARISON_FUNC dzn_translate_compare_op(VkCompareOp in);
void dzn_translate_viewport(D3D12_VIEWPORT *out, const VkViewport *in);
void dzn_translate_rect(D3D12_RECT *out, const VkRect2D *in);
#ifdef __cplusplus
}
#endif

#define dzn_foreach_aspect(aspect, mask) \
        for (VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT; \
             aspect <= VK_IMAGE_ASPECT_STENCIL_BIT; \
             aspect = (VkImageAspectFlagBits)(aspect << 1)) \
           if (mask & aspect)

VkResult dzn_wsi_init(struct dzn_physical_device *physical_device);
void dzn_wsi_finish(struct dzn_physical_device *physical_device);

struct dzn_app_info {
   const char *app_name;
   uint32_t app_version;
   const char *engine_name;
   uint32_t engine_version;
   uint32_t api_version;
};

enum dzn_debug_flags {
   DZN_DEBUG_SYNC = 1 << 0,
   DZN_DEBUG_NIR = 1 << 1,
   DZN_DEBUG_DXIL = 1 << 2,
   DZN_DEBUG_WARP = 1 << 3,
   DZN_DEBUG_INTERNAL = 1 << 4,
   DZN_DEBUG_SIG = 1 << 5,
   DZN_DEBUG_GBV = 1 << 6,
   DZN_DEBUG_D3D12 = 1 << 7,
};

struct dzn_instance {
   struct vk_instance vk;

   struct dxil_validator *dxil_validator;
   struct {
      PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE serialize_root_sig;
   } d3d12;
   bool physical_devices_enumerated;
   uint32_t debug_flags;

   struct list_head physical_devices;
};

struct dzn_event {
   struct vk_object_base base;
   ID3D12Fence *fence;
};

struct dzn_sync {
   struct vk_sync vk;
   ID3D12Fence *fence;
};

extern const struct vk_sync_type dzn_sync_type;

struct dzn_query {
   D3D12_QUERY_TYPE type;
   ID3D12Fence *fence;
   uint64_t fence_value;
};

struct dzn_query_pool {
   struct vk_object_base base;

   D3D12_QUERY_HEAP_TYPE heap_type;
   ID3D12QueryHeap *heap;
   uint32_t query_count;
   struct dzn_query *queries;
   mtx_t queries_lock;
   ID3D12Resource *resolve_buffer;
   ID3D12Resource *collect_buffer;
   VkQueryPipelineStatisticFlags pipeline_statistics;
   uint32_t query_size;
   uint64_t *collect_map;
};

D3D12_QUERY_TYPE
dzn_query_pool_get_query_type(const dzn_query_pool *qpool, VkQueryControlFlags flag);

uint32_t
dzn_query_pool_get_result_offset(const dzn_query_pool *qpool, uint32_t query);

uint32_t
dzn_query_pool_get_availability_offset(const dzn_query_pool *qpool, uint32_t query);

uint32_t
dzn_query_pool_get_result_size(const dzn_query_pool *qpool, uint32_t count);

VK_DEFINE_HANDLE_CASTS(dzn_cmd_buffer, vk.base, VkCommandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER)
VK_DEFINE_HANDLE_CASTS(dzn_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(dzn_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(dzn_physical_device, vk.base, VkPhysicalDevice, VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(dzn_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_buffer, base, VkBuffer, VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_buffer_view, base, VkBufferView, VK_OBJECT_TYPE_BUFFER_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_device_memory, base, VkDeviceMemory, VK_OBJECT_TYPE_DEVICE_MEMORY)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_descriptor_pool, base, VkDescriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_descriptor_set, base, VkDescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_descriptor_set_layout, base, VkDescriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_framebuffer, base, VkFramebuffer, VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_image_view, vk.base, VkImageView, VK_OBJECT_TYPE_IMAGE_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_pipeline, base, VkPipeline, VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_graphics_pipeline, base.base, VkPipeline, VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_compute_pipeline, base.base, VkPipeline, VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_pipeline_cache, base, VkPipelineCache, VK_OBJECT_TYPE_PIPELINE_CACHE)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_pipeline_layout, base, VkPipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_query_pool, base, VkQueryPool, VK_OBJECT_TYPE_QUERY_POOL)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_render_pass, base, VkRenderPass, VK_OBJECT_TYPE_RENDER_PASS)
VK_DEFINE_NONDISP_HANDLE_CASTS(dzn_sampler, base, VkSampler, VK_OBJECT_TYPE_SAMPLER)

#endif /* DZN_PRIVATE_H */
