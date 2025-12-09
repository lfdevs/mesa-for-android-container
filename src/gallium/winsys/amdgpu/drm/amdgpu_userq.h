/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AMDGPU_USERQ_H
#define AMDGPU_USERQ_H

#ifdef __cplusplus
extern "C" {
#endif

/* ring size should be power of 2 and enough to hold AMDGPU_FENCE_RING_SIZE ibs */
#define AMDGPU_USERQ_RING_SIZE 0x10000
#define AMDGPU_USERQ_RING_SIZE_DW (AMDGPU_USERQ_RING_SIZE >> 2)
#define AMDGPU_USERQ_RING_SIZE_DW_MASK (AMDGPU_USERQ_RING_SIZE_DW - 1)

/* An offset into doorbell page. Any number will work. */
#define AMDGPU_USERQ_DOORBELL_INDEX 4

#define amdgpu_pkt_begin() uint32_t *__ring_ptr = userq->ring_ptr; \
   uint64_t __next_wptr = userq->next_wptr;

#define amdgpu_pkt_add_dw(value) do { \
   *(__ring_ptr + (__next_wptr & AMDGPU_USERQ_RING_SIZE_DW_MASK)) = value; \
   __next_wptr++; \
} while (0)

#define amdgpu_pkt_end() do { \
   assert(__next_wptr - *userq->user_fence_ptr <= AMDGPU_USERQ_RING_SIZE_DW); \
   userq->next_wptr = __next_wptr; \
} while (0)

#define amdgpu_pkt_get_ptr_skip_dw() \
   (__ring_ptr + (__next_wptr++ & AMDGPU_USERQ_RING_SIZE_DW_MASK))

#define amdgpu_pkt_get_next_wptr() __next_wptr

struct amdgpu_winsys;
struct amdgpu_screen_winsys;

struct amdgpu_userq_gfx_data {
   struct pb_buffer_lean *csa_bo;
   struct pb_buffer_lean *shadow_bo;
};

struct amdgpu_userq_compute_data {
   struct pb_buffer_lean *eop_bo;
};

struct amdgpu_userq_sdma_data {
   struct pb_buffer_lean *csa_bo;
};

/* For gfx, compute and sdma ip there will be one userqueue per process.
 * i.e. commands from all context will be submitted to single userqueue.
 * There will be one struct amdgpu_userq per gfx, compute and sdma ip.
 */
struct amdgpu_userq {
   struct pb_buffer_lean *gtt_bo;
   uint8_t *gtt_bo_map;

   uint32_t *ring_ptr;
   uint64_t *user_fence_ptr;
   uint64_t user_fence_va;
   uint64_t user_fence_seq_num;

   struct pb_buffer_lean *wptr_bo;
   uint64_t *wptr_bo_map;
   /* Holds the wptr value for the in-progress submission. When we're ready
    * to submit it, this value will be written to the door bell.
    * (this avoids writing multiple times to the door bell for the same
    * submission) */
   uint64_t next_wptr;
   struct pb_buffer_lean *vram_bo;
   uint64_t rptr_va;

   struct pb_buffer_lean *doorbell_bo;
   uint64_t *doorbell_bo_map;

   /* In case of gfx11.5 shadow register address has to be initialized using LOAD_* packet.
    * Also for every new ib/job submission, the shadowed registers has to be loaded using LOAD_*
    * packets.
    */
   struct pb_buffer_lean *f32_shadowing_ib_bo;
   uint32_t f32_shadowing_ib_pm4_dw;
   bool f32_is_shadowing_ib_initialized;
   struct pb_buffer_lean *cs_preamble_ib_bo;
   bool is_cs_preamble_ib_sent;
   uint32_t userq_handle;
   enum amd_ip_type ip_type;
   simple_mtx_t lock;
   /* flags used for queue priority level */
   uint32_t flags;

   union {
      struct amdgpu_userq_gfx_data gfx_data;
      struct amdgpu_userq_compute_data compute_data;
      struct amdgpu_userq_sdma_data sdma_data;
   };
};

bool
amdgpu_userq_init(struct amdgpu_winsys *aws, struct amdgpu_userq *userq, enum amd_ip_type ip_type,
                  unsigned queue_index);
void
amdgpu_userq_deinit(struct amdgpu_winsys *aws, struct amdgpu_userq *userq);

void amdgpu_userq_init_functions(struct amdgpu_screen_winsys *sws);

#ifdef __cplusplus
}
#endif

#endif
