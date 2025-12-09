/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "amdgpu_bo.h"
#include "amdgpu_cs.h"
#include "ac_linux_drm.h"
#include "sid.h"

static void
update_vm_timeline_point_to_wait(uint64_t *vm_timeline_point_to_wait, struct pb_buffer_lean *_buf)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);
   struct amdgpu_bo_real *bo_real;

   if (bo->type == AMDGPU_BO_SLAB_ENTRY)
      bo_real = get_slab_entry_real_bo(bo);
   else
      bo_real = get_real_bo(bo);

   if (bo_real->vm_timeline_point > *vm_timeline_point_to_wait)
      *vm_timeline_point_to_wait = bo_real->vm_timeline_point;
}

static bool
amdgpu_userq_ring_init(struct amdgpu_winsys *aws, struct amdgpu_userq *userq,
                       uint64_t *vm_timeline_point_to_wait)
{
   /* Allocate ring and user fence in one buffer. */
   uint32_t gtt_bo_size = AMDGPU_USERQ_RING_SIZE + aws->info.gart_page_size;
   userq->gtt_bo = amdgpu_bo_create(aws, gtt_bo_size, 256, RADEON_DOMAIN_GTT,
                                    RADEON_FLAG_GL2_BYPASS | RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->gtt_bo)
      return false;

   userq->gtt_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->gtt_bo, NULL,
                                     PIPE_MAP_READ | PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!userq->gtt_bo_map)
      return false;

   userq->wptr_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256, RADEON_DOMAIN_GTT,
                                     RADEON_FLAG_GL2_BYPASS | RADEON_FLAG_NO_SUBALLOC |
                                        RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->wptr_bo)
      return false;

   userq->wptr_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->wptr_bo, NULL,
                                      PIPE_MAP_READ | PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!userq->wptr_bo_map)
      return false;

   userq->ring_ptr = (uint32_t*)userq->gtt_bo_map;
   userq->user_fence_ptr = (uint64_t*)(userq->gtt_bo_map + AMDGPU_USERQ_RING_SIZE);
   userq->user_fence_va = amdgpu_bo_get_va(userq->gtt_bo) + AMDGPU_USERQ_RING_SIZE;
   *userq->user_fence_ptr = 0;
   *userq->wptr_bo_map = 0;
   userq->next_wptr = 0;

   /* Allocate memory for rptr. */
   userq->vram_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256, RADEON_DOMAIN_VRAM,
                                     RADEON_FLAG_CLEAR_VRAM | RADEON_FLAG_GL2_BYPASS |
                                        RADEON_FLAG_NO_SUBALLOC |
                                        RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->vram_bo)
      return false;

   update_vm_timeline_point_to_wait(vm_timeline_point_to_wait, userq->vram_bo);
   userq->rptr_va = amdgpu_bo_get_va(userq->vram_bo);
   return true;
}

void
amdgpu_userq_deinit(struct amdgpu_winsys *aws, struct amdgpu_userq *userq)
{
   if (userq->userq_handle)
      ac_drm_free_userqueue(aws->dev, userq->userq_handle);

   radeon_bo_reference(&aws->dummy_sws.base, &userq->gtt_bo, NULL);
   radeon_bo_reference(&aws->dummy_sws.base, &userq->wptr_bo, NULL);
   radeon_bo_reference(&aws->dummy_sws.base, &userq->vram_bo, NULL);
   radeon_bo_reference(&aws->dummy_sws.base, &userq->doorbell_bo, NULL);

   switch (userq->ip_type) {
   case AMD_IP_GFX:
      radeon_bo_reference(&aws->dummy_sws.base, &userq->gfx_data.csa_bo, NULL);
      radeon_bo_reference(&aws->dummy_sws.base, &userq->gfx_data.shadow_bo, NULL);
      radeon_bo_reference(&aws->dummy_sws.base, &userq->cs_preamble_ib_bo, NULL);
      radeon_bo_reference(&aws->dummy_sws.base, &userq->f32_shadowing_ib_bo, NULL);
      break;
   case AMD_IP_COMPUTE:
      radeon_bo_reference(&aws->dummy_sws.base, &userq->compute_data.eop_bo, NULL);
      break;
   case AMD_IP_SDMA:
      radeon_bo_reference(&aws->dummy_sws.base, &userq->sdma_data.csa_bo, NULL);
      break;
   default:
      fprintf(stderr, "amdgpu: userq unsupported for ip = %d\n", userq->ip_type);
   }
}

bool
amdgpu_userq_init(struct amdgpu_winsys *aws, struct amdgpu_userq *userq, enum amd_ip_type ip_type,
                  unsigned queue_index)
{
   int r = -1;
   uint32_t hw_ip_type;
   /* The VA page table for ring, rtr, wptr buffer should be ready before job submission so that
    * the packets submitted can be read by gpu.
    */
   uint64_t vm_timeline_point_to_wait = 0;
   struct drm_amdgpu_userq_mqd_gfx11 gfx_mqd;
   struct drm_amdgpu_userq_mqd_compute_gfx11 compute_mqd;
   struct drm_amdgpu_userq_mqd_sdma_gfx11 sdma_mqd;
   void *mqd;

   simple_mtx_lock(&userq->lock);

   if (userq->gtt_bo) {
      simple_mtx_unlock(&userq->lock);
      return true;
   }

   userq->ip_type = ip_type;
   if (!amdgpu_userq_ring_init(aws, userq, &vm_timeline_point_to_wait))
      goto fail;

   switch (userq->ip_type) {
   case AMD_IP_GFX:
      hw_ip_type = AMDGPU_HW_IP_GFX;
      userq->gfx_data.csa_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.csa_size,
                                                aws->info.fw_based_mcbp.csa_alignment,
                                                RADEON_DOMAIN_VRAM,
                                                RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->gfx_data.csa_bo)
         goto fail;

      userq->gfx_data.shadow_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.shadow_size,
                                                   aws->info.fw_based_mcbp.shadow_alignment,
                                                   RADEON_DOMAIN_VRAM,
                                                   RADEON_FLAG_NO_INTERPROCESS_SHARING |
                                                      RADEON_FLAG_CLEAR_VRAM);
      if (!userq->gfx_data.shadow_bo)
         goto fail;

      gfx_mqd.shadow_va = amdgpu_bo_get_va(userq->gfx_data.shadow_bo);
      gfx_mqd.csa_va = amdgpu_bo_get_va(userq->gfx_data.csa_bo);
      mqd = &gfx_mqd;
      update_vm_timeline_point_to_wait(&vm_timeline_point_to_wait, userq->gfx_data.csa_bo);
      update_vm_timeline_point_to_wait(&vm_timeline_point_to_wait, userq->gfx_data.shadow_bo);
      break;
   case AMD_IP_COMPUTE:
      hw_ip_type = AMDGPU_HW_IP_COMPUTE;
      userq->compute_data.eop_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.eop_size,
                                                    aws->info.fw_based_mcbp.eop_alignment,
                                                    RADEON_DOMAIN_VRAM,
                                                    RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->compute_data.eop_bo)
         goto fail;

      compute_mqd.eop_va = amdgpu_bo_get_va(userq->compute_data.eop_bo);
      mqd = &compute_mqd;
      update_vm_timeline_point_to_wait(&vm_timeline_point_to_wait, userq->compute_data.eop_bo);
      break;
   case AMD_IP_SDMA:
      hw_ip_type = AMDGPU_HW_IP_DMA;
      userq->sdma_data.csa_bo = amdgpu_bo_create(aws, aws->info.fw_based_mcbp.sdma_csa_size,
                                                 aws->info.fw_based_mcbp.sdma_csa_alignment,
                                                 RADEON_DOMAIN_VRAM,
                                                 RADEON_FLAG_NO_INTERPROCESS_SHARING);
      if (!userq->sdma_data.csa_bo)
         goto fail;

      sdma_mqd.csa_va = amdgpu_bo_get_va(userq->sdma_data.csa_bo);
      mqd = &sdma_mqd;
      update_vm_timeline_point_to_wait(&vm_timeline_point_to_wait, userq->sdma_data.csa_bo);
      break;
   default:
      fprintf(stderr, "amdgpu: userq unsupported for ip = %d\n", userq->ip_type);
      goto fail;
   }

   userq->doorbell_bo = amdgpu_bo_create(aws, aws->info.gart_page_size, 256,
                                         RADEON_DOMAIN_DOORBELL,
                                         RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->doorbell_bo)
      goto fail;

   userq->doorbell_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->doorbell_bo, NULL,
                                          PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!userq->doorbell_bo_map)
      goto fail;

   r = ac_drm_cs_syncobj_timeline_wait(aws->dev, &aws->vm_timeline_syncobj,
                                       &vm_timeline_point_to_wait, 1,
                                       INT64_MAX, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
                                          DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
   if (r) {
      fprintf(stderr, "amdgpu: waiting for vm fences failed\n");
      goto fail;
   }

   uint64_t ring_va = amdgpu_bo_get_va(userq->gtt_bo);
   unsigned priority = queue_index == AMDGPU_QUEUE_GFX_HIGH_PRIO ?
                          AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_HIGH :
                          AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_NORMAL_LOW;

   while (1) {
      r = ac_drm_create_userqueue(aws->dev, hw_ip_type,
                                  get_real_bo(amdgpu_winsys_bo(userq->doorbell_bo))->kms_handle,
                                  AMDGPU_USERQ_DOORBELL_INDEX, ring_va, AMDGPU_USERQ_RING_SIZE,
                                  amdgpu_bo_get_va(userq->wptr_bo), userq->rptr_va, mqd, priority,
                                  &userq->userq_handle);
      if (r == -EACCES && priority == AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_HIGH) {
         /* Try again with a lower priority. */
         priority = AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_NORMAL_HIGH;
         continue;
      }
      break;
   }

   if (r) {
      fprintf(stderr, "amdgpu: failed to create userq\n");
      goto fail;
   }

   simple_mtx_unlock(&userq->lock);
   return true;
fail:
   amdgpu_userq_deinit(aws, userq);
   simple_mtx_unlock(&userq->lock);
   return false;
}

static bool
amdgpu_userq_submit_cs_preamble_ib_once(struct radeon_cmdbuf *rcs, struct ac_pm4_state *pm4)
{
   struct amdgpu_cs *acs = amdgpu_cs(rcs);
   struct amdgpu_winsys *aws =  acs->aws;
   struct amdgpu_userq *userq = &aws->queues[acs->queue_index].userq;
   uint8_t *cs_preamble_ib_bo_map;

   simple_mtx_lock(&userq->lock);

   if (userq->is_cs_preamble_ib_sent) {
      simple_mtx_unlock(&userq->lock);
      return true;
   }

   userq->is_cs_preamble_ib_sent = true;
   assert(userq->ip_type == AMD_IP_GFX);

   userq->cs_preamble_ib_bo = amdgpu_bo_create(aws, pm4->ndw * 4, 256, RADEON_DOMAIN_GTT,
                                                  RADEON_FLAG_GL2_BYPASS |
                                                     RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->cs_preamble_ib_bo) {
      simple_mtx_unlock(&userq->lock);
      return false;
   }

   cs_preamble_ib_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->cs_preamble_ib_bo,
                                            NULL, PIPE_MAP_READ | PIPE_MAP_WRITE |
                                               PIPE_MAP_UNSYNCHRONIZED);
   if (!cs_preamble_ib_bo_map) {
      simple_mtx_unlock(&userq->lock);
      return false;
   }

   memcpy(cs_preamble_ib_bo_map, &pm4->pm4, pm4->ndw * 4);

   amdgpu_pkt_begin();
   amdgpu_pkt_add_dw(PKT3(PKT3_INDIRECT_BUFFER, 2, 0));
   amdgpu_pkt_add_dw(amdgpu_bo_get_va(userq->cs_preamble_ib_bo));
   amdgpu_pkt_add_dw(amdgpu_bo_get_va(userq->cs_preamble_ib_bo) >> 32);
   amdgpu_pkt_add_dw(pm4->ndw | S_3F3_INHERIT_VMID_MQD_GFX(1));
   amdgpu_pkt_end();

   simple_mtx_unlock(&userq->lock);
   return true;
}

static bool
amdgpu_userq_f32_init_reg_shadowing(struct radeon_cmdbuf *rcs, struct ac_pm4_state *pm4)
{
   struct amdgpu_cs *acs = amdgpu_cs(rcs);
   struct amdgpu_winsys *aws =  acs->aws;
   struct amdgpu_userq *userq = &aws->queues[acs->queue_index].userq;
   uint8_t *shadowing_ib_bo_map;

   simple_mtx_lock(&userq->lock);

   if (userq->f32_is_shadowing_ib_initialized) {
      simple_mtx_unlock(&userq->lock);
      return true;
   }

   userq->f32_is_shadowing_ib_initialized = true;
   assert(userq->ip_type == AMD_IP_GFX);
   assert(!userq->next_wptr);

   userq->f32_shadowing_ib_bo = amdgpu_bo_create(aws, pm4->ndw * 4, 256, RADEON_DOMAIN_VRAM,
                                                 RADEON_FLAG_NO_INTERPROCESS_SHARING);
   if (!userq->f32_shadowing_ib_bo) {
      simple_mtx_unlock(&userq->lock);
      return false;
   }

   shadowing_ib_bo_map = amdgpu_bo_map(&aws->dummy_sws.base, userq->f32_shadowing_ib_bo, NULL,
                                       PIPE_MAP_READ | PIPE_MAP_WRITE | PIPE_MAP_UNSYNCHRONIZED);
   if (!shadowing_ib_bo_map) {
      simple_mtx_unlock(&userq->lock);
      return false;
   }

   memcpy(shadowing_ib_bo_map, &pm4->pm4, pm4->ndw * 4);
   userq->f32_shadowing_ib_pm4_dw = pm4->ndw;

   amdgpu_pkt_begin();
   amdgpu_pkt_add_dw(PKT3(PKT3_INDIRECT_BUFFER, 2, 0));
   amdgpu_pkt_add_dw(amdgpu_bo_get_va(userq->f32_shadowing_ib_bo));
   amdgpu_pkt_add_dw(amdgpu_bo_get_va(userq->f32_shadowing_ib_bo) >> 32);
   amdgpu_pkt_add_dw(pm4->ndw | S_3F3_INHERIT_VMID_MQD_GFX(1));
   amdgpu_pkt_end();

   simple_mtx_unlock(&userq->lock);
   return true;
}

static uint64_t
amdgpu_userq_f32_get_shadow_regs_va(struct radeon_cmdbuf *rcs) {
   struct amdgpu_cs *acs = amdgpu_cs(rcs);
   struct amdgpu_winsys *aws =  acs->aws;
   struct amdgpu_userq *userq = &aws->queues[acs->queue_index].userq;

   assert(userq->ip_type == AMDGPU_HW_IP_GFX);
   return amdgpu_bo_get_va(userq->gfx_data.shadow_bo);
}

void amdgpu_userq_init_functions(struct amdgpu_screen_winsys *sws)
{
   sws->base.userq_submit_cs_preamble_ib_once = amdgpu_userq_submit_cs_preamble_ib_once;
   sws->base.userq_f32_init_reg_shadowing = amdgpu_userq_f32_init_reg_shadowing;
   sws->base.userq_f32_get_shadow_regs_va = amdgpu_userq_f32_get_shadow_regs_va;
}
