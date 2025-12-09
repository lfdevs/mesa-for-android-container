/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "anv_private.h"
#include "anv_nir.h"

#include "nir/nir_xfb_info.h"

static inline struct anv_batch *
anv_shader_add(struct anv_batch *batch,
               struct anv_gfx_state_ptr *ptr,
               uint32_t n_dwords)
{
   assert(ptr->len == 0 ||
          (batch->next - batch->start) / 4 == (ptr->offset + ptr->len));
   if (ptr->len == 0)
      ptr->offset = (batch->next - batch->start) / 4;
   ptr->len += n_dwords;

   return batch;
}

#define anv_shader_emit(batch, shader, state, cmd, name)                \
   for (struct cmd name = { __anv_cmd_header(cmd) },                    \
           *_dst = anv_batch_emit_dwords(                               \
              anv_shader_add(batch, &(shader)->state,                   \
                             __anv_cmd_length(cmd)),                    \
              __anv_cmd_length(cmd));                                   \
        __builtin_expect(_dst != NULL, 1);                              \
        ({ __anv_cmd_pack(cmd)(batch, _dst, &name);                     \
           VG(VALGRIND_CHECK_MEM_IS_DEFINED(                            \
                 _dst, __anv_cmd_length(cmd) * 4));                     \
           _dst = NULL;                                                 \
        }))

#define anv_shader_emitn(batch, shader, state, n, cmd, ...) ({          \
         void *__dst = anv_batch_emit_dwords(                           \
            anv_shader_add(batch, &(shader)->state, n), n);             \
         if (__dst) {                                                   \
            struct cmd __template = {                                   \
               __anv_cmd_header(cmd),                                   \
               .DWordLength = n - __anv_cmd_length_bias(cmd),           \
               __VA_ARGS__                                              \
            };                                                          \
            __anv_cmd_pack(cmd)(batch, __dst, &__template);             \
         }                                                              \
         __dst;                                                         \
      })

#define anv_shader_emit_tmp(batch, storage, cmd, name)                  \
   for (struct cmd name = { __anv_cmd_header(cmd) },                    \
           *_dst = (void *) storage;                                    \
        __builtin_expect(_dst != NULL, 1);                              \
        ({ __anv_cmd_pack(cmd)(batch, _dst, &name);                     \
           VG(VALGRIND_CHECK_MEM_IS_DEFINED(                            \
                 _dst, __anv_cmd_length(cmd) * 4));                     \
           _dst = NULL;                                                 \
        }))

#define anv_shader_emit_merge(batch, shader, state, dwords, cmd, name)  \
   for (struct cmd name = { 0 },                                        \
           *_dst = anv_batch_emit_dwords(                               \
              anv_shader_add(batch, &(shader)->state,                   \
                             __anv_cmd_length(cmd)),                    \
              __anv_cmd_length(cmd));                                   \
        __builtin_expect(_dst != NULL, 1);                              \
        ({ uint32_t _partial[__anv_cmd_length(cmd)];                    \
           assert((shader)->state.len == __anv_cmd_length(cmd));        \
           __anv_cmd_pack(cmd)(batch, _partial, &name);                 \
           for (uint32_t i = 0; i < __anv_cmd_length(cmd); i++) {       \
              ((uint32_t *)_dst)[i] = _partial[i] | dwords[i];          \
           }                                                            \
           VG(VALGRIND_CHECK_MEM_IS_DEFINED(                            \
                 _dst, __anv_cmd_length(cmd) * 4));                     \
           _dst = NULL;                                                 \
         }))

struct anv_shader_data {
   struct vk_shader_compile_info *info;

   struct vk_shader **shader_out;

   union brw_any_prog_key key;
   uint32_t key_size;

   union brw_any_prog_data prog_data;

   uint32_t source_hash;

   const nir_xfb_info *xfb_info;

   uint32_t num_stats;
   struct genisa_stats stats[3];
   char *disasm[3];

   bool use_primitive_replication;
   uint32_t instance_multiplier;

   /* For fragment shaders only */
   struct brw_mue_map *mue_map;

   struct anv_push_descriptor_info push_desc_info;

   struct anv_pipeline_bind_map bind_map;

   struct anv_pipeline_push_map push_map;

   uint8_t dynamic_descriptors[MAX_SETS];

   bool uses_bt_for_push_descs;

   unsigned *code;

   debug_archiver *archiver;
};

VkResult anv_shader_create(struct anv_device *device,
                           mesa_shader_stage stage,
                           void *mem_ctx,
                           struct anv_shader_data *shader_data,
                           const VkAllocationCallbacks *pAllocator,
                           struct vk_shader **shader_out);

VkResult anv_shader_deserialize(struct vk_device *device,
                                struct blob_reader *blob,
                                uint32_t binary_version,
                                const VkAllocationCallbacks* pAllocator,
                                struct vk_shader **shader_out);

extern struct vk_device_shader_ops anv_device_shader_ops;
