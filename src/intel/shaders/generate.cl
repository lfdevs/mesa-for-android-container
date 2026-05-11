/* Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "libintel_shaders.h"

void genX(write_address)(global void *dst_ptr, global void *address, uint64_t value)
{
   struct GENX(MI_STORE_DATA_IMM) v = {
      GENX(MI_STORE_DATA_IMM_header),
      .DWordLength = GENX(MI_STORE_DATA_IMM_length) -
                     GENX(MI_STORE_DATA_IMM_length_bias) + 1,
#if GFX_VER >= 12
      .ForceWriteCompletionCheck = true,
#endif
      .Address = (uint64_t)address,
      .ImmediateData = value,
   };
   GENX(MI_STORE_DATA_IMM_pack)(dst_ptr, &v);
}

void genX(write_3DSTATE_VF_TOPOLOGY)(global void *dst_ptr,
                                     uint32_t topology)
{
   struct GENX(3DSTATE_VF_TOPOLOGY) v = {
      GENX(3DSTATE_VF_TOPOLOGY_header),
      .PrimitiveTopologyType = topology,
   };
   GENX(3DSTATE_VF_TOPOLOGY_pack)(dst_ptr, &v);
}

void genX(write_3DSTATE_VERTEX_BUFFERS)(global void *dst_ptr,
                                        uint32_t buffer_count)
{
   struct GENX(3DSTATE_VERTEX_BUFFERS) v = {
      GENX(3DSTATE_VERTEX_BUFFERS_header),
      .DWordLength = 1 + (buffer_count * 4) -
                     GENX(3DSTATE_VERTEX_BUFFERS_length_bias),
   };
   GENX(3DSTATE_VERTEX_BUFFERS_pack)(dst_ptr, &v);
}

void genX(write_VERTEX_BUFFER_STATE)(global void *dst_ptr,
                                     uint32_t mocs,
                                     uint32_t buffer_idx,
                                     uint64_t address,
                                     uint32_t size,
                                     uint32_t stride)
{
   bool buffer_null = address == 0;
   struct GENX(VERTEX_BUFFER_STATE) v = {
      .BufferPitch = stride,
      .NullVertexBuffer = address == 0,
      .AddressModifyEnable = true,
      .MOCS = mocs,
#if GFX_VER >= 12
      .L3BypassDisable = true,
#endif
      .VertexBufferIndex = buffer_idx,
      .BufferStartingAddress = address,
      .BufferSize = size,
   };
   GENX(VERTEX_BUFFER_STATE_pack)(dst_ptr, &v);
}

void genX(write_3DSTATE_INDEX_BUFFER)(global void *dst_ptr,
                                      uint64_t buffer_addr,
                                      uint32_t buffer_size,
                                      uint32_t index_format,
                                      uint32_t mocs)
{
   struct GENX(3DSTATE_INDEX_BUFFER) v = {
      GENX(3DSTATE_INDEX_BUFFER_header),
      .MOCS = mocs,
      .IndexFormat = index_format,
#if GFX_VER >= 12
      .L3BypassDisable = true,
#endif
      .BufferStartingAddress = buffer_addr,
      .BufferSize = buffer_size,
   };
   GENX(3DSTATE_INDEX_BUFFER_pack)(dst_ptr, &v);
}

void genX(write_3DPRIMITIVE)(global void *dst_ptr,
                             bool is_predicated,
                             bool is_indexed,
                             bool uses_tbimr,
                             uint32_t vertex_count_per_instance,
                             uint32_t start_vertex_location,
                             uint32_t instance_count,
                             uint32_t start_instance_location,
                             uint32_t base_vertex_location)
{
   struct GENX(3DPRIMITIVE) v = {
      GENX(3DPRIMITIVE_header),
#if GFX_VERx10 >= 125
      .TBIMREnable = uses_tbimr,
#endif
      .PredicateEnable = is_predicated,
      .VertexAccessType = is_indexed ? RANDOM : SEQUENTIAL,
      .VertexCountPerInstance = vertex_count_per_instance,
      .StartVertexLocation = start_vertex_location,
      .InstanceCount = instance_count,
      .StartInstanceLocation = start_instance_location,
      .BaseVertexLocation = base_vertex_location,
   };
   GENX(3DPRIMITIVE_pack)(dst_ptr, &v);
}

#if GFX_VER >= 11
void genX(write_3DPRIMITIVE_EXTENDED)(global void *dst_ptr,
                                      bool is_predicated,
                                      bool is_indexed,
                                      bool uses_tbimr,
                                      uint32_t vertex_count_per_instance,
                                      uint32_t start_vertex_location,
                                      uint32_t instance_count,
                                      uint32_t start_instance_location,
                                      uint32_t base_vertex_location,
                                      uint32_t param_base_vertex,
                                      uint32_t param_base_instance,
                                      uint32_t param_draw_id)
{
   struct GENX(3DPRIMITIVE_EXTENDED) v = {
      GENX(3DPRIMITIVE_EXTENDED_header),
#if GFX_VERx10 >= 125
      .TBIMREnable = uses_tbimr,
#endif
      .PredicateEnable = is_predicated,
      .VertexAccessType = is_indexed ? RANDOM : SEQUENTIAL,
      .VertexCountPerInstance = vertex_count_per_instance,
      .StartVertexLocation = start_vertex_location,
      .InstanceCount = instance_count,
      .StartInstanceLocation = start_instance_location,
      .BaseVertexLocation = base_vertex_location,
      .ExtendedParameter0 = param_base_vertex,
      .ExtendedParameter1 = param_base_instance,
      .ExtendedParameter2 = param_draw_id,
   };
   GENX(3DPRIMITIVE_EXTENDED_pack)(dst_ptr, &v);
}
#endif

void genX(write_MI_BATCH_BUFFER_START)(global void *dst_ptr, uint64_t addr)
{
   struct GENX(MI_BATCH_BUFFER_START) v = {
      GENX(MI_BATCH_BUFFER_START_header),
      .AddressSpaceIndicator = ASI_PPGTT,
      .BatchBufferStartAddress = addr,
   };
   GENX(MI_BATCH_BUFFER_START_pack)(dst_ptr, &v);
}

void genX(write_draw)(global uint32_t *dst_ptr,
                      global void *indirect_ptr,
                      global uint32_t *draw_id_ptr,
                      uint32_t draw_id,
                      uint32_t instance_multiplier,
                      bool is_indexed,
                      bool is_predicated,
                      bool uses_tbimr,
                      bool uses_base,
                      bool uses_drawid,
                      uint32_t mocs)
{
#if GFX_VER <= 9
   if (uses_base || uses_drawid) {
      uint32_t vertex_buffer_count =
         (uses_base ? 1 : 0) + (uses_drawid ? 1 : 0);
      genX(write_3DSTATE_VERTEX_BUFFERS)(dst_ptr, vertex_buffer_count);
      dst_ptr += 1; /* GENX(3DSTATE_VERTEX_BUFFERS_length); */
      if (uses_base) {
         uint64_t base_addr = (uint64_t)indirect_ptr + (is_indexed ? 12 : 8);
         genX(write_VERTEX_BUFFER_STATE)(dst_ptr, mocs, 31, base_addr, 8, 0);
         dst_ptr += GENX(VERTEX_BUFFER_STATE_length);
      }
      if (uses_drawid) {
         *draw_id_ptr = draw_id;
         genX(write_VERTEX_BUFFER_STATE)(dst_ptr, mocs, 32,
                                         (uint64_t)draw_id_ptr, 4, 0);
         dst_ptr += GENX(VERTEX_BUFFER_STATE_length);
      }
   }

   if (is_indexed) {
      VkDrawIndexedIndirectCommand data =
         *((global VkDrawIndexedIndirectCommand *)indirect_ptr);

      genX(write_3DPRIMITIVE)(dst_ptr,
                              is_predicated,
                              is_indexed,
                              uses_tbimr,
                              data.indexCount,
                              data.firstIndex,
                              data.instanceCount * instance_multiplier,
                              data.firstInstance,
                              data.vertexOffset);
   } else {
      VkDrawIndirectCommand data =
         *((global VkDrawIndirectCommand *)indirect_ptr);

      genX(write_3DPRIMITIVE)(dst_ptr,
                              is_predicated,
                              is_indexed,
                              uses_tbimr,
                              data.vertexCount,
                              data.firstVertex,
                              data.instanceCount * instance_multiplier,
                              data.firstInstance,
                              0 /* base_vertex_location */);
   }
#else
   if (is_indexed) {
      VkDrawIndexedIndirectCommand data =
         *((global VkDrawIndexedIndirectCommand *)indirect_ptr);

      genX(write_3DPRIMITIVE_EXTENDED)(dst_ptr,
                                       is_predicated,
                                       is_indexed,
                                       uses_tbimr,
                                       data.indexCount,
                                       data.firstIndex,
                                       data.instanceCount * instance_multiplier,
                                       data.firstInstance,
                                       data.vertexOffset,
                                       data.vertexOffset,
                                       data.firstInstance,
                                       draw_id);
   } else {
      VkDrawIndirectCommand data =
         *((global VkDrawIndirectCommand *)indirect_ptr);

      genX(write_3DPRIMITIVE_EXTENDED)(dst_ptr,
                                       is_predicated,
                                       is_indexed,
                                       uses_tbimr,
                                       data.vertexCount,
                                       data.firstVertex,
                                       data.instanceCount * instance_multiplier,
                                       data.firstInstance,
                                       0 /* base_vertex_location */,
                                       data.firstVertex,
                                       data.firstInstance,
                                       draw_id);
   }
#endif
}

#if GFX_VERx10 >= 125
void genX(write_3DMESH_3D)(global uint32_t *dst_ptr,
                           global void *indirect_ptr,
                           bool is_predicated,
                           bool uses_tbimr)
{
   VkDrawMeshTasksIndirectCommandEXT data =
      *((global VkDrawMeshTasksIndirectCommandEXT *)indirect_ptr);

   struct GENX(3DMESH_3D) v = {
      GENX(3DMESH_3D_header),
      .TBIMREnabled      = uses_tbimr,
      .PredicateEnable   = is_predicated,
      .ThreadGroupCountX = data.groupCountX,
      .ThreadGroupCountY = data.groupCountY,
      .ThreadGroupCountZ = data.groupCountZ,

   };
   GENX(3DMESH_3D_pack)(dst_ptr, &v);
}
#endif
