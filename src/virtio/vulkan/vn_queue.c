/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_queue.h"

#include "vn_protocol_driver_queue.h"
#include "vn_protocol_driver_transport.h"

#include "vn_command_buffer.h"
#include "vn_device.h"
#include "vn_feedback.h"
#include "vn_physical_device.h"
#include "vn_renderer.h"
#include "vn_sync.h"
#include "vn_wsi.h"

struct vn_queue_submission_pnext {
   /* intercepted structs */
   VkDeviceGroupSubmitInfo group;
   VkTimelineSemaphoreSubmitInfo timeline;

   /* forwarded structs */
   VkDeviceGroupBindSparseInfo sparse;
   VkProtectedSubmitInfo protected;
};

struct vn_queue_submission {
   VkStructureType batch_type;
   VkQueue queue_handle;
   union {
      const void *batch;
      const VkSubmitInfo *submit_batch;
      const VkSubmitInfo2 *submit2_batch;
      const VkBindSparseInfo *sparse_batch;
   };
   VkFence fence_handle;
   bool can_feedback;

   union {
      /* Bitmask exists for testing if any field is set. */
      uint32_t fix_mask;

      struct {
         /* for submit batch */
         bool batch : 1;
         /* for pNext chain */
         bool pnext : 1;

         /* for sync feedback */
         bool sfb : 1;
         bool qfb : 1;

         /* for renderer syncs */
         bool sync_fence : 1;
         bool sync_sem : 1;
      } fix;
   };

   uint32_t cmd_count;
   uint32_t dev_mask_count;
   uint32_t wait_index_count;
   uint32_t wait_val_count;
   uint32_t wait_sem_count;
   uint32_t sync_count;
   uint32_t sig_index_count;
   uint32_t sig_val_count;
   uint32_t sig_sem_count;

   /* Temporary storage allocation for submission
    *
    * A single alloc for storage is performed and the offsets inside storage
    * are set as below:
    *
    * batch
    *  - copy of original batch
    * cmds
    *  - copy of original batch cmds
    *  - a single cmd for query feedback (qfb)
    *  - one cmd for each signal semaphore that has feedback (sfb)
    * pnext
    *  - a single pnext if batch pnext need fix
    * dev_masks
    *  - for device group submit to append new mask for new cmds
    * wait_indices
    *  - fix wait dev indices in the pNext chain
    * wait_vals
    *  - fix timeline wait values in the pNext chain
    * wait_sems
    *  - fix semaphore handles or infos for dropped wait semaphores
    * syncs
    *  - renderer syncs to signal
    * sync_vals
    *  - renderer sync vals to signal
    * sig_indices
    *  - fix signal dev indices in the pNext chain
    * sig_vals
    *  - fix timeline signal values in the pNext chain
    * sig_sems
    *  - fix semaphore handles or infos for dropped signal semaphores
    */
   struct {
      union {
         void *batch;
         VkSubmitInfo *submit_batch;
         VkSubmitInfo2 *submit2_batch;
         VkBindSparseInfo *sparse_batch;
      };
      struct vn_queue_submission_pnext *pnext;
      union {
         void *cmds;
         VkCommandBuffer *cmd_handles;
         VkCommandBufferSubmitInfo *cmd_infos;
      };
      uint32_t *dev_masks;
      uint32_t *wait_indices;
      union {
         void *wait_sems;
         VkSemaphore *wait_sem_handles;
         VkSemaphoreSubmitInfo *wait_sem_infos;
      };
      uint64_t *wait_vals;
      struct vn_renderer_sync **syncs;
      uint64_t *sync_vals;
      uint32_t *sig_indices;
      uint64_t *sig_vals;
      union {
         void *sig_sems;
         VkSemaphore *sig_sem_handles;
         VkSemaphoreSubmitInfo *sig_sem_infos;
      };
   } temp;
};

static_assert(sizeof(((struct vn_queue_submission *)0)->fix_mask) >=
                 sizeof(((struct vn_queue_submission *)0)->fix),
              "vn_queue_submission::fix_mask is too small");

static inline size_t
vn_get_sem_size(struct vn_queue_submission *submit)
{
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2
             ? sizeof(VkSemaphoreSubmitInfo)
             : sizeof(VkSemaphore);
}

static inline uint32_t
vn_get_wait_semaphore_count(struct vn_queue_submission *submit)
{
   if (!submit->batch)
      return 0;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batch->waitSemaphoreCount;
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batch->waitSemaphoreInfoCount;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batch->waitSemaphoreCount;
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline uint32_t
vn_get_signal_semaphore_count(struct vn_queue_submission *submit)
{
   if (!submit->batch)
      return 0;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batch->signalSemaphoreCount;
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batch->signalSemaphoreInfoCount;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batch->signalSemaphoreCount;
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline VkSemaphore
vn_get_wait_semaphore(struct vn_queue_submission *submit, uint32_t index)
{
   assert(submit->batch);
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batch->pWaitSemaphores[index];
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batch->pWaitSemaphoreInfos[index].semaphore;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batch->pWaitSemaphores[index];
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline VkSemaphore
vn_get_signal_semaphore(struct vn_queue_submission *submit, uint32_t index)
{
   assert(submit->batch);
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return submit->submit_batch->pSignalSemaphores[index];
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batch->pSignalSemaphoreInfos[index].semaphore;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return submit->sparse_batch->pSignalSemaphores[index];
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline size_t
vn_get_batch_size(struct vn_queue_submission *submit)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      return sizeof(VkSubmitInfo);
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return sizeof(VkSubmitInfo2);
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      return sizeof(VkBindSparseInfo);
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static inline size_t
vn_get_cmd_size(struct vn_queue_submission *submit)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) ||
          (submit->cmd_count == 0));
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? sizeof(VkCommandBuffer)
             : sizeof(VkCommandBufferSubmitInfo);
}

static inline uint32_t
vn_get_cmd_count(struct vn_queue_submission *submit)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   if (!submit->batch)
      return 0;
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? submit->submit_batch->commandBufferCount
             : submit->submit2_batch->commandBufferInfoCount;
}

static inline const void *
vn_get_cmds(struct vn_queue_submission *submit)
{
   assert(submit->batch);
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
             ? (const void *)submit->submit_batch->pCommandBuffers
             : (const void *)submit->submit2_batch->pCommandBufferInfos;
}

static inline struct vn_command_buffer *
vn_get_cmd(struct vn_queue_submission *submit, uint32_t index)
{
   assert(submit->batch);
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   return vn_command_buffer_from_handle(
      submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO
         ? submit->submit_batch->pCommandBuffers[index]
         : submit->submit2_batch->pCommandBufferInfos[index].commandBuffer);
}

static inline void
vn_set_temp_cmd(struct vn_queue_submission *submit,
                uint32_t index,
                VkCommandBuffer cmd_handle)
{
   assert((submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) ||
          (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2));
   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      submit->temp.cmd_infos[index] = (VkCommandBufferSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = cmd_handle,
      };
   } else {
      submit->temp.cmd_handles[index] = cmd_handle;
   }
}

static uint64_t
vn_get_signal_semaphore_counter(struct vn_queue_submission *submit,
                                uint32_t index)
{
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO: {
      const VkTimelineSemaphoreSubmitInfo *timeline_sem_info =
         vk_find_struct_const(submit->submit_batch->pNext,
                              TIMELINE_SEMAPHORE_SUBMIT_INFO);
      return timeline_sem_info->pSignalSemaphoreValues[index];
   }
   case VK_STRUCTURE_TYPE_SUBMIT_INFO_2:
      return submit->submit2_batch->pSignalSemaphoreInfos[index].value;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO: {
      const VkTimelineSemaphoreSubmitInfo *timeline_sem_info =
         vk_find_struct_const(submit->sparse_batch->pNext,
                              TIMELINE_SEMAPHORE_SUBMIT_INFO);
      return timeline_sem_info->pSignalSemaphoreValues[index];
   }
   default:
      UNREACHABLE("unexpected batch type");
   }
}

static bool
vn_is_batch_empty(struct vn_queue_submission *submit)
{
   assert(submit->fence_handle == VK_NULL_HANDLE);

   if (!submit->batch)
      return true;

   if (submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO) {
      return !submit->sparse_batch->waitSemaphoreCount &&
             !submit->sparse_batch->bufferBindCount &&
             !submit->sparse_batch->imageOpaqueBindCount &&
             !submit->sparse_batch->imageBindCount &&
             !submit->sparse_batch->pSignalSemaphores;
   }

   return !vn_get_wait_semaphore_count(submit) && !vn_get_cmd_count(submit) &&
          !vn_get_signal_semaphore_count(submit);
}

static void
vn_queue_submission_init_pnext(struct vn_queue_submission *submit)
{
   if (!submit->fix.pnext)
      return;

   struct vn_queue_submission_pnext *pnext = submit->temp.pnext;
   VkBaseOutStructure *cur = (void *)submit->temp.submit_batch;

   vk_foreach_struct_const(sType, src, submit->submit_batch->pNext) {
      void *next = NULL;
      switch (sType) {
      case VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO: {
         memcpy(&pnext->group, src, sizeof(pnext->group));
         next = &pnext->group;

         VkDeviceGroupSubmitInfo *group = (void *)src;

         /* fix the dev masks for the extra feedback cmds */
         if (submit->dev_mask_count) {
            VK_FROM_HANDLE(vk_queue, queue_vk, submit->queue_handle);
            struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);

            assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO);
            assert(group->commandBufferCount == vn_get_cmd_count(submit));

            const uint32_t old_cmd_count = vn_get_cmd_count(submit);
            if (old_cmd_count) {
               memcpy(submit->temp.dev_masks,
                      group->pCommandBufferDeviceMasks,
                      sizeof(uint32_t) * old_cmd_count);
            }

            /* Set the group device mask. Unlike sync2, zero means skip. */
            const uint32_t new_cmd_count =
               submit->temp.submit_batch->commandBufferCount;
            for (uint32_t i = old_cmd_count; i < new_cmd_count; i++)
               submit->temp.dev_masks[i] = dev->device_mask;

            pnext->group.commandBufferCount = new_cmd_count;
            pnext->group.pCommandBufferDeviceMasks = submit->temp.dev_masks;
         }

         /* drop the dev indices for the dropped wait semaphores */
         if (submit->wait_index_count) {
            assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO ||
                   submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
            assert(submit->wait_index_count ==
                   vn_get_wait_semaphore_count(submit));

            uint32_t j = 0;
            for (uint32_t i = 0; i < submit->wait_index_count; i++) {
               VkSemaphore sem_handle = vn_get_wait_semaphore(submit, i);
               if (vn_semaphore_is_sync_fd(sem_handle))
                  continue;

               submit->temp.wait_indices[j++] =
                  group->pWaitSemaphoreDeviceIndices[i];
            }

            pnext->group.waitSemaphoreCount = j;
            pnext->group.pWaitSemaphoreDeviceIndices =
               submit->temp.wait_indices;
         }

         /* drop the dev indices for the dropped signal semaphores */
         if (submit->sig_index_count) {
            assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO ||
                   submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
            assert(submit->sig_index_count ==
                   vn_get_signal_semaphore_count(submit));

            uint32_t j = 0;
            for (uint32_t i = 0; i < submit->sig_index_count; i++) {
               VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i);
               if (vn_semaphore_is_sync_fd(sem_handle))
                  continue;

               submit->temp.sig_indices[j++] =
                  group->pSignalSemaphoreDeviceIndices[i];
            }

            pnext->group.signalSemaphoreCount = j;
            pnext->group.pSignalSemaphoreDeviceIndices =
               submit->temp.sig_indices;
         }

         break;
      }
      case VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO: {
         memcpy(&pnext->timeline, src, sizeof(pnext->timeline));
         next = &pnext->timeline;

         /* drop the sem vals for the dropped wait semaphores */
         if (submit->wait_val_count) {
            VkTimelineSemaphoreSubmitInfo *timeline = (void *)src;

            assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO ||
                   submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
            assert(submit->wait_val_count ==
                   vn_get_wait_semaphore_count(submit));

            uint32_t j = 0;
            for (uint32_t i = 0; i < submit->wait_val_count; i++) {
               VkSemaphore sem_handle = vn_get_wait_semaphore(submit, i);
               if (vn_semaphore_is_sync_fd(sem_handle))
                  continue;

               submit->temp.wait_vals[j++] =
                  timeline->pWaitSemaphoreValues[i];
            }

            pnext->timeline.waitSemaphoreValueCount = j;
            pnext->timeline.pWaitSemaphoreValues = submit->temp.wait_vals;
         }

         /* drop the sem vals for the dropped signal semaphores */
         if (submit->sig_val_count) {
            VkTimelineSemaphoreSubmitInfo *timeline = (void *)src;

            assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO ||
                   submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
            assert(submit->sig_val_count ==
                   vn_get_signal_semaphore_count(submit));

            uint32_t j = 0;
            for (uint32_t i = 0; i < submit->sig_val_count; i++) {
               VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i);
               if (vn_semaphore_is_sync_fd(sem_handle))
                  continue;

               submit->temp.sig_vals[j++] =
                  timeline->pSignalSemaphoreValues[i];
            }

            pnext->timeline.signalSemaphoreValueCount = j;
            pnext->timeline.pSignalSemaphoreValues = submit->temp.sig_vals;
         }

         break;
      }
      case VK_STRUCTURE_TYPE_DEVICE_GROUP_BIND_SPARSE_INFO:
         memcpy(&pnext->sparse, src, sizeof(pnext->sparse));
         next = &pnext->sparse;
         break;
      case VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO:
         memcpy(&pnext->protected, src, sizeof(pnext->protected));
         next = &pnext->protected;
         break;
      default:
         /* The following structs are not supported by venus so are not
          * handled here. VkAmigoProfilingSubmitInfoSEC,
          * VkD3D12FenceSubmitInfoKHR, VkFrameBoundaryEXT,
          * VkLatencySubmissionPresentIdNV, VkPerformanceQuerySubmitInfoKHR,
          * VkWin32KeyedMutexAcquireReleaseInfoKHR,
          * VkWin32KeyedMutexAcquireReleaseInfoNV
          */
         break;
      }

      if (next) {
         cur->pNext = next;
         cur = next;
      }
   }
}

static void
vn_queue_submission_count_semaphore(struct vn_queue_submission *submit)
{
   struct {
      bool wait_sync_fd;
      bool wait_timeline;
      bool sig_sync_fd;
      bool sig_timeline;
   } fix = { 0 };

   const uint32_t wait_count = vn_get_wait_semaphore_count(submit);
   for (uint32_t i = 0; i < wait_count; i++) {
      VkSemaphore sem_handle = vn_get_wait_semaphore(submit, i);
      fix.wait_sync_fd |= vn_semaphore_is_sync_fd(sem_handle);
      fix.wait_timeline |= vn_semaphore_is_timeline(sem_handle);
   }

   const uint32_t sig_count = vn_get_signal_semaphore_count(submit);
   for (uint32_t i = 0; i < sig_count; i++) {
      VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i);
      if (vn_semaphore_is_sync_fd(sem_handle)) {
         fix.sig_sync_fd = true;
         submit->fix.sync_sem = true;
         submit->sync_count++;
      }

      if (vn_semaphore_is_timeline(sem_handle)) {
         VK_FROM_HANDLE(vn_semaphore, sem, sem_handle);

         fix.sig_timeline = true;

         if (sem->payload->type == VN_SYNC_TYPE_TIMELINE_SYNC) {
            submit->fix.sync_sem = true;
            submit->sync_count++;
         }
      }
   }

   if (!fix.wait_sync_fd && !fix.sig_sync_fd)
      return;

   submit->fix.batch = true;

   if (fix.wait_sync_fd) {
      submit->wait_sem_count = wait_count;
      submit->wait_val_count = fix.wait_timeline ? wait_count : 0;
   }

   if (fix.sig_sync_fd) {
      submit->sig_sem_count = sig_count;
      submit->sig_val_count = fix.sig_timeline ? sig_count : 0;
   }

   if (submit->batch_type != VK_STRUCTURE_TYPE_SUBMIT_INFO_2 &&
       (submit->wait_val_count || submit->sig_val_count))
      submit->fix.pnext = true;

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
      const VkDeviceGroupSubmitInfo *device_group = vk_find_struct_const(
         submit->submit_batch->pNext, DEVICE_GROUP_SUBMIT_INFO);
      if (device_group) {
         submit->fix.pnext = true;
         submit->wait_index_count = fix.wait_sync_fd ? wait_count : 0;
         submit->sig_index_count = fix.sig_sync_fd ? sig_count : 0;
      }
   }
}

static void
vn_queue_submission_count_batch_feedback(struct vn_queue_submission *submit)
{
   const uint32_t signal_count = vn_get_signal_semaphore_count(submit);
   for (uint32_t i = 0; i < signal_count; i++) {
      struct vn_semaphore *sem =
         vn_semaphore_from_handle(vn_get_signal_semaphore(submit, i));
      if (!vn_sync_feedback_enabled(&sem->feedback))
         continue;

      if (submit->can_feedback) {
         submit->fix.sfb = true;
         submit->cmd_count++;
      } else {
         const uint64_t counter = vn_get_signal_semaphore_counter(submit, i);
         vn_sync_feedback_suspend(&sem->feedback, counter);
      }
   }

   /* can early return after sync feedbacks have been suspended */
   if (!submit->can_feedback)
      return;

   assert(submit->batch_type != VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);

   const uint32_t cmd_count = vn_get_cmd_count(submit);
   for (uint32_t i = 0; i < cmd_count; i++) {
      struct vn_command_buffer *cmd = vn_get_cmd(submit, i);
      if (!list_is_empty(&cmd->builder.query_records))
         submit->fix.qfb = true;

      /* If a cmd that was submitted previously and already has a feedback
       * cmd linked, as long as
       * VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT was not set we can
       * assume it has completed execution and is no longer in the pending
       * state so its safe to recycle the old feedback command.
       */
      if (cmd->linked_qfb_cmd) {
         assert(!cmd->builder.is_simultaneous);

         vn_query_feedback_cmd_free(cmd->linked_qfb_cmd);
         cmd->linked_qfb_cmd = NULL;
      }
   }
   if (submit->fix.qfb)
      submit->cmd_count++;

   /* no sync feedback cmds to be added */
   if (!submit->cmd_count)
      return;

   submit->cmd_count += cmd_count;
   submit->fix.batch = true;

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
      const VkDeviceGroupSubmitInfo *device_group = vk_find_struct_const(
         submit->submit_batch->pNext, DEVICE_GROUP_SUBMIT_INFO);
      if (device_group) {
         submit->fix.pnext = true;
         submit->dev_mask_count = submit->cmd_count;
      }
   }
}

static inline void
vn_queue_submission_count_fence(struct vn_queue_submission *submit)
{
   if (submit->fence_handle == VK_NULL_HANDLE)
      return;

   submit->fix.sync_fence = true;
   submit->sync_count++;
}

static void
vn_queue_submission_prepare(struct vn_queue_submission *submit)
{
   vn_queue_submission_count_semaphore(submit);
   vn_queue_submission_count_batch_feedback(submit);
   vn_queue_submission_count_fence(submit);
}

static VkResult
vn_queue_submission_alloc_storage(struct vn_queue_submission *submit)
{
   size_t total = 0;
   struct {
      size_t batch;
      size_t cmds;
      size_t pnext;
      size_t dev_masks;
      size_t wait_indices;
      size_t wait_vals;
      size_t wait_sems;
      size_t syncs;
      size_t sync_vals;
      size_t sig_indices;
      size_t sig_vals;
      size_t sig_sems;
   } size = { 0 };

   total += size.batch = submit->fix.batch ? vn_get_batch_size(submit) : 0;
   total += size.pnext = submit->fix.pnext ? sizeof(*submit->temp.pnext) : 0;
   total += size.cmds = submit->cmd_count * vn_get_cmd_size(submit);
   total += size.dev_masks = submit->dev_mask_count * sizeof(uint32_t);
   total += size.wait_indices = submit->wait_index_count * sizeof(uint32_t);
   total += size.wait_vals = submit->wait_val_count * sizeof(uint64_t);
   total += size.wait_sems = submit->wait_sem_count * vn_get_sem_size(submit);
   total += size.syncs = submit->sync_count * sizeof(void *);
   total += size.sync_vals = submit->sync_count * sizeof(uint64_t);
   total += size.sig_indices = submit->sig_index_count * sizeof(uint32_t);
   total += size.sig_vals = submit->sig_val_count * sizeof(uint64_t);
   total += size.sig_sems = submit->sig_sem_count * vn_get_sem_size(submit);

   struct vn_queue *queue = vn_queue_from_handle(submit->queue_handle);
   uint8_t *storage = vn_cached_storage_get(&queue->storage, total);
   if (!storage)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   submit->temp.batch = (void *)storage;
   submit->temp.pnext = (void *)(storage += size.batch);
   submit->temp.cmds = (void *)(storage += size.pnext);
   submit->temp.dev_masks = (void *)(storage += size.cmds);
   submit->temp.wait_indices = (void *)(storage += size.dev_masks);
   submit->temp.wait_vals = (void *)(storage += size.wait_indices);
   submit->temp.wait_sems = (void *)(storage += size.wait_vals);
   submit->temp.syncs = (void *)(storage += size.wait_sems);
   submit->temp.sync_vals = (void *)(storage += size.syncs);
   submit->temp.sig_indices = (void *)(storage += size.sync_vals);
   submit->temp.sig_vals = (void *)(storage += size.sig_indices);
   submit->temp.sig_sems = (void *)(storage += size.sig_vals);

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_get_resolved_query_records(
   struct vn_queue_submission *submit,
   struct vn_feedback_cmd_pool *fb_cmd_pool,
   struct list_head *resolved_records)
{
   struct vn_command_pool *cmd_pool =
      vn_command_pool_from_handle(fb_cmd_pool->pool_handle);
   struct list_head dropped_records;
   VkResult result = VK_SUCCESS;

   list_inithead(resolved_records);
   list_inithead(&dropped_records);
   const uint32_t cmd_count = vn_get_cmd_count(submit);
   for (uint32_t i = 0; i < cmd_count; i++) {
      struct vn_command_buffer *cmd = vn_get_cmd(submit, i);

      list_for_each_entry(struct vn_cmd_query_record, record,
                          &cmd->builder.query_records, head) {
         if (!record->copy) {
            list_for_each_entry_safe(struct vn_cmd_query_record, prev,
                                     resolved_records, head) {
               /* If we previously added a query feedback that is now getting
                * reset, remove it since it is now a no-op and the deferred
                * feedback copy will cause a hang waiting for the reset query
                * to become available.
                */
               if (prev->copy && prev->query_pool == record->query_pool &&
                   prev->query >= record->query &&
                   prev->query < record->query + record->query_count)
                  list_move_to(&prev->head, &dropped_records);
            }
         }

         simple_mtx_lock(&fb_cmd_pool->mutex);
         struct vn_cmd_query_record *curr = vn_cmd_pool_alloc_query_record(
            cmd_pool, record->query_pool, record->query, record->query_count,
            record->copy);
         simple_mtx_unlock(&fb_cmd_pool->mutex);

         if (!curr) {
            list_splicetail(resolved_records, &dropped_records);
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto out_free_dropped_records;
         }

         list_addtail(&curr->head, resolved_records);
      }
   }

   /* further resolve to batch sequential queries */
   struct vn_cmd_query_record *curr =
      list_first_entry(resolved_records, struct vn_cmd_query_record, head);
   list_for_each_entry_safe(struct vn_cmd_query_record, next,
                            resolved_records, head) {
      if (curr->query_pool == next->query_pool && curr->copy == next->copy) {
         if (curr->query + curr->query_count == next->query) {
            curr->query_count += next->query_count;
            list_move_to(&next->head, &dropped_records);
         } else if (curr->query == next->query + next->query_count) {
            curr->query = next->query;
            curr->query_count += next->query_count;
            list_move_to(&next->head, &dropped_records);
         } else {
            curr = next;
         }
      } else {
         curr = next;
      }
   }

out_free_dropped_records:
   simple_mtx_lock(&fb_cmd_pool->mutex);
   vn_cmd_pool_free_query_records(cmd_pool, &dropped_records);
   simple_mtx_unlock(&fb_cmd_pool->mutex);
   return result;
}

static VkResult
vn_queue_submission_add_query_feedback(struct vn_queue_submission *submit,
                                       uint32_t *new_cmd_count)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   VkResult result;

   struct vn_feedback_cmd_pool *fb_cmd_pool = NULL;
   for (uint32_t i = 0; i < dev->queue_family_count; i++) {
      if (dev->queue_families[i] == queue_vk->queue_family_index) {
         fb_cmd_pool = &dev->fb_cmd_pools[i];
         break;
      }
   }
   assert(fb_cmd_pool);

   struct list_head resolved_records;
   result = vn_queue_submission_get_resolved_query_records(
      submit, fb_cmd_pool, &resolved_records);
   if (result != VK_SUCCESS)
      return result;

   /* currently the reset query is always recorded */
   assert(!list_is_empty(&resolved_records));
   struct vn_query_feedback_cmd *qfb_cmd;
   result = vn_query_feedback_cmd_alloc(vn_device_to_handle(dev), fb_cmd_pool,
                                        &resolved_records, &qfb_cmd);
   if (result == VK_SUCCESS) {
      /* link query feedback cmd lifecycle with a cmd in the original batch so
       * that the feedback cmd can be reset and recycled when that cmd gets
       * reset/freed.
       *
       * Avoid cmd buffers with VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
       * since we don't know if all its instances have completed execution.
       * Should be rare enough to just log and leak the feedback cmd.
       */
      bool found_companion_cmd = false;
      const uint32_t cmd_count = vn_get_cmd_count(submit);
      for (uint32_t i = 0; i < cmd_count; i++) {
         struct vn_command_buffer *cmd = vn_get_cmd(submit, i);
         if (!cmd->builder.is_simultaneous) {
            cmd->linked_qfb_cmd = qfb_cmd;
            found_companion_cmd = true;
            break;
         }
      }
      if (!found_companion_cmd)
         vn_log(dev->instance, "WARN: qfb cmd has leaked!");

      vn_set_temp_cmd(submit, (*new_cmd_count)++, qfb_cmd->cmd_handle);
   }

   simple_mtx_lock(&fb_cmd_pool->mutex);
   vn_cmd_pool_free_query_records(
      vn_command_pool_from_handle(fb_cmd_pool->pool_handle),
      &resolved_records);
   simple_mtx_unlock(&fb_cmd_pool->mutex);

   return result;
}

static VkResult
vn_queue_submission_add_semaphore_feedback(struct vn_queue_submission *submit,
                                           uint32_t index,
                                           uint32_t *new_cmd_count)
{
   struct vn_semaphore *sem =
      vn_semaphore_from_handle(vn_get_signal_semaphore(submit, index));
   if (!vn_sync_feedback_enabled(&sem->feedback))
      return VK_SUCCESS;

   VK_FROM_HANDLE(vk_queue, queue_vk, submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   const uint64_t counter = vn_get_signal_semaphore_counter(submit, index);

   VkCommandBuffer sfb_cmd_handle = vn_sync_feedback_command(
      dev, &sem->feedback, queue_vk->queue_family_index, counter);
   if (sfb_cmd_handle == VK_NULL_HANDLE)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_set_temp_cmd(submit, (*new_cmd_count)++, sfb_cmd_handle);
   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_add_feedback_cmds(struct vn_queue_submission *submit)
{
   VkResult result;
   uint32_t new_cmd_count = vn_get_cmd_count(submit);

   if (submit->fix.qfb) {
      result = vn_queue_submission_add_query_feedback(submit, &new_cmd_count);
      if (result != VK_SUCCESS)
         return result;
   }

   if (submit->fix.sfb) {
      const uint32_t signal_count = vn_get_signal_semaphore_count(submit);
      for (uint32_t i = 0; i < signal_count; i++) {
         result = vn_queue_submission_add_semaphore_feedback(submit, i,
                                                             &new_cmd_count);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      submit->temp.submit2_batch->pCommandBufferInfos =
         submit->temp.cmd_infos;
      submit->temp.submit2_batch->commandBufferInfoCount = new_cmd_count;
   } else {
      submit->temp.submit_batch->pCommandBuffers = submit->temp.cmd_handles;
      submit->temp.submit_batch->commandBufferCount = new_cmd_count;
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_init_cmds(struct vn_queue_submission *submit)
{
   if (!submit->cmd_count)
      return VK_SUCCESS;

   assert(submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2 ||
          submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO);

   /* If the batch has sync feedback, copy the original commands and append
    * feedback cmds. Copy is only needed for non-empty batch.
    */
   const size_t cmd_size = vn_get_cmd_count(submit) * vn_get_cmd_size(submit);
   if (cmd_size)
      memcpy(submit->temp.cmds, vn_get_cmds(submit), cmd_size);

   return vn_queue_submission_add_feedback_cmds(submit);
}

static VkResult
vn_queue_submission_init_wait_semaphores(struct vn_queue_submission *submit)
{
   if (!submit->wait_sem_count)
      return VK_SUCCESS;

   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   VkDevice dev_handle = vk_device_to_handle(queue_vk->base.device);

   uint32_t j = 0;
   const uint32_t wait_count = vn_get_wait_semaphore_count(submit);
   for (uint32_t i = 0; i < wait_count; i++) {
      VkSemaphore sem_handle = vn_get_wait_semaphore(submit, i);
      if (vn_semaphore_is_sync_fd(sem_handle)) {
         if (!vn_semaphore_wait_sync_fd(dev_handle, sem_handle))
            return VK_ERROR_DEVICE_LOST;

         /* drop the sync fd semaphore */
         continue;
      }

      if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
         submit->temp.wait_sem_infos[j++] =
            submit->submit2_batch->pWaitSemaphoreInfos[i];
      } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
         submit->temp.wait_sem_handles[j++] =
            submit->submit_batch->pWaitSemaphores[i];
      } else {
         assert(submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
         submit->temp.wait_sem_handles[j++] =
            submit->sparse_batch->pWaitSemaphores[i];
      }
   }

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      submit->temp.submit2_batch->pWaitSemaphoreInfos =
         submit->temp.wait_sem_infos;
      submit->temp.submit2_batch->waitSemaphoreInfoCount = j;
   } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
      submit->temp.submit_batch->pWaitSemaphores =
         submit->temp.wait_sem_handles;
      submit->temp.submit_batch->waitSemaphoreCount = j;
   } else {
      assert(submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
      submit->temp.sparse_batch->pWaitSemaphores =
         submit->temp.wait_sem_handles;
      submit->temp.sparse_batch->waitSemaphoreCount = j;
   }

   return VK_SUCCESS;
}

static void
vn_queue_submission_init_signal_semaphores(struct vn_queue_submission *submit)
{
   if (!submit->sig_sem_count)
      return;

   uint32_t j = 0;
   const uint32_t sig_count = vn_get_signal_semaphore_count(submit);
   for (uint32_t i = 0; i < sig_count; i++) {
      VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i);
      if (vn_semaphore_is_sync_fd(sem_handle)) {
         /* drop the sync fd semaphore */
         continue;
      }

      if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
         submit->temp.sig_sem_infos[j++] =
            submit->submit2_batch->pSignalSemaphoreInfos[i];
      } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
         submit->temp.sig_sem_handles[j++] =
            submit->submit_batch->pSignalSemaphores[i];
      } else {
         assert(submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
         submit->temp.sig_sem_handles[j++] =
            submit->sparse_batch->pSignalSemaphores[i];
      }
   }

   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      submit->temp.submit2_batch->pSignalSemaphoreInfos =
         submit->temp.sig_sem_infos;
      submit->temp.submit2_batch->signalSemaphoreInfoCount = j;
   } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
      submit->temp.submit_batch->pSignalSemaphores =
         submit->temp.sig_sem_handles;
      submit->temp.submit_batch->signalSemaphoreCount = j;
   } else {
      assert(submit->batch_type == VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);
      submit->temp.sparse_batch->pSignalSemaphores =
         submit->temp.sig_sem_handles;
      submit->temp.sparse_batch->signalSemaphoreCount = j;
   }
}

static void
vn_queue_submission_init_syncs(struct vn_queue_submission *submit)
{
   VK_FROM_HANDLE(vn_queue, queue, submit->queue_handle);
   uint32_t sync_index = 0;

   const uint32_t sig_count = vn_get_signal_semaphore_count(submit);
   for (uint32_t i = 0; i < sig_count; i++) {
      VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i);
      VK_FROM_HANDLE(vn_semaphore, sem, sem_handle);

      if (sem->payload->type == VN_SYNC_TYPE_SYNC) {
         submit->temp.syncs[sync_index] = sem->payload->sync;
         submit->temp.sync_vals[sync_index++] = 1;
      } else if (sem->payload->type == VN_SYNC_TYPE_TIMELINE_SYNC) {
         const uint64_t sync_val = vn_get_signal_semaphore_counter(submit, i);
         submit->temp.syncs[sync_index] = sem->payload->syncs[queue->index];
         submit->temp.sync_vals[sync_index++] = sync_val;
      }
   }

   if (submit->fence_handle != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(vn_fence, fence, submit->fence_handle);

      assert(fence->payload->type == VN_SYNC_TYPE_SYNC);

      submit->temp.syncs[sync_index] = fence->payload->sync;
      submit->temp.sync_vals[sync_index] = 1;

      /* fence is backed by renderer sync without renderer object */
      submit->fence_handle = VK_NULL_HANDLE;
   }
}

static VkResult
vn_queue_submission_init(struct vn_queue_submission *submit)
{
   /* For a submission that fixes the batch, copy batch for adding feedbacks
    * or dropping semaphores.
    */
   if (submit->fix.batch)
      memcpy(submit->temp.batch, submit->batch, vn_get_batch_size(submit));

   VkResult result = vn_queue_submission_init_cmds(submit);
   if (result != VK_SUCCESS)
      return result;

   vn_queue_submission_init_pnext(submit);

   /* wait semaphores are initialized the last to ensure validity of
    * vn_semaphore_is_sync_fd used in init pnext
    */
   result = vn_queue_submission_init_wait_semaphores(submit);
   if (result != VK_SUCCESS)
      return result;

   vn_queue_submission_init_syncs(submit);

   /* signal semaphore are initialized after syncs are extracted */
   vn_queue_submission_init_signal_semaphores(submit);

   if (submit->fix.batch)
      submit->batch = submit->temp.batch;

   return VK_SUCCESS;
}

static void
vn_queue_submission_cleanup_semaphore_feedback(
   struct vn_queue_submission *submit)
{
   struct vk_queue *queue_vk = vk_queue_from_handle(submit->queue_handle);
   VkDevice dev_handle = vk_device_to_handle(queue_vk->base.device);

   const uint32_t wait_count = vn_get_wait_semaphore_count(submit);
   for (uint32_t i = 0; i < wait_count; i++) {
      VkSemaphore sem_handle = vn_get_wait_semaphore(submit, i);
      struct vn_semaphore *sem = vn_semaphore_from_handle(sem_handle);
      if (!vn_sync_feedback_enabled(&sem->feedback))
         continue;

      /* sfb pending cmds are recycled when signaled counter is updated */
      uint64_t counter = 0;
      vn_GetSemaphoreCounterValue(dev_handle, sem_handle, &counter);
   }

   const uint32_t signal_count = vn_get_signal_semaphore_count(submit);
   for (uint32_t i = 0; i < signal_count; i++) {
      VkSemaphore sem_handle = vn_get_signal_semaphore(submit, i);
      struct vn_semaphore *sem = vn_semaphore_from_handle(sem_handle);
      if (!vn_sync_feedback_enabled(&sem->feedback))
         continue;

      /* sfb pending cmds are recycled when signaled counter is updated */
      uint64_t counter = 0;
      vn_GetSemaphoreCounterValue(dev_handle, sem_handle, &counter);
   }
}

static void
vn_queue_submission_cleanup(struct vn_queue_submission *submit)
{
   /* TODO clean up pending src feedbacks on failure? */
   if (submit->fix.sfb)
      vn_queue_submission_cleanup_semaphore_feedback(submit);
}

static VkResult
vn_queue_submission_prepare_submit(struct vn_queue_submission *submit)
{
   vn_queue_submission_prepare(submit);

   /* early return if there's nothing to fix */
   if (!submit->fix_mask)
      return VK_SUCCESS;

   VkResult result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_init(submit);
   if (result != VK_SUCCESS) {
      vn_queue_submission_cleanup(submit);
      return result;
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_do_submit(struct vn_queue_submission *submit)
{
   VK_FROM_HANDLE(vn_queue, queue, submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue->base.vk.base.device);

   if (vn_is_batch_empty(submit))
      return VK_SUCCESS;

   if (queue->roundtrip_seqno_valid)
      vn_ring_wait_roundtrip(dev->primary_ring, queue->roundtrip_seqno);

   const uint32_t batch_count = submit->batch ? 1 : 0;
   if (VN_PERF(NO_ASYNC_QUEUE_SUBMIT)) {
      if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
         return vn_call_vkQueueSubmit2(
            dev->primary_ring, submit->queue_handle, batch_count,
            submit->submit2_batch, submit->fence_handle);
      } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
         return vn_call_vkQueueSubmit(dev->primary_ring, submit->queue_handle,
                                      batch_count, submit->submit_batch,
                                      submit->fence_handle);
      } else {
         return vn_call_vkQueueBindSparse(
            dev->primary_ring, submit->queue_handle, batch_count,
            submit->sparse_batch, submit->fence_handle);
      }
   }

   struct vn_ring_submit_command ring_submit;
   if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO_2) {
      vn_submit_vkQueueSubmit2(dev->primary_ring, 0, submit->queue_handle,
                               batch_count, submit->submit2_batch,
                               submit->fence_handle, &ring_submit);
   } else if (submit->batch_type == VK_STRUCTURE_TYPE_SUBMIT_INFO) {
      vn_submit_vkQueueSubmit(dev->primary_ring, 0, submit->queue_handle,
                              batch_count, submit->submit_batch,
                              submit->fence_handle, &ring_submit);
   } else {
      vn_submit_vkQueueBindSparse(dev->primary_ring, 0, submit->queue_handle,
                                  batch_count, submit->sparse_batch,
                                  submit->fence_handle, &ring_submit);
   }
   if (!ring_submit.ring_seqno_valid)
      return VK_ERROR_DEVICE_LOST;

   queue->ring_seqno_valid = true;
   queue->ring_seqno = ring_submit.ring_seqno;

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_signal_syncs(struct vn_queue_submission *submit)
{
   VK_FROM_HANDLE(vn_queue, queue, submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue->base.vk.base.device);

   if (!submit->sync_count)
      return VK_SUCCESS;

   struct vn_renderer_submit_batch batch = {
      .syncs = submit->temp.syncs,
      .sync_values = submit->temp.sync_vals,
      .sync_count = submit->sync_count,
      .ring_idx = queue->ring_idx,
   };

   uint32_t local_data[8];
   struct vn_cs_encoder local_enc =
      VN_CS_ENCODER_INITIALIZER_LOCAL(local_data, sizeof(local_data));
   if (queue->ring_seqno_valid) {
      const uint64_t ring_id = vn_ring_get_id(dev->primary_ring);
      vn_encode_vkWaitRingSeqnoMESA(&local_enc, 0, ring_id,
                                    queue->ring_seqno);
      batch.cs_data = local_data;
      batch.cs_size = vn_cs_encoder_get_len(&local_enc);
   }

   VkResult result = vn_renderer_submit(dev->renderer, &batch);
   if (result != VK_SUCCESS)
      return result;

   result =
      vn_ring_submit_roundtrip(dev->primary_ring, &queue->roundtrip_seqno);
   queue->roundtrip_seqno_valid = result == VK_SUCCESS;

   return result;
}

static VkResult
vn_queue_submit(struct vn_queue_submission *submit)
{
   VK_FROM_HANDLE(vn_queue, queue, submit->queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue->base.vk.base.device);
   VkResult result;

   result = vn_queue_submission_prepare_submit(submit);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   result = vn_queue_submission_do_submit(submit);
   if (result != VK_SUCCESS)
      goto out_cleanup;

   result = vn_queue_submission_signal_syncs(submit);

out_cleanup:
   vn_queue_submission_cleanup(submit);
   return vn_result(dev->instance, result);
}

static inline bool
vn_queue_can_feedback(VkQueue queue_handle)
{
   struct vn_queue *queue = vn_queue_from_handle(queue_handle);
   return queue->can_feedback;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueSubmit(VkQueue queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence fence)
{
   VN_TRACE_FUNC();

   vn_tls_set_async_pipeline_create();
   vn_wsi_flush(queue);

   for (uint32_t i = 0; i < submitCount; i++) {
      struct vn_queue_submission submit = {
         .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .queue_handle = queue,
         .submit_batch = &pSubmits[i],
         .fence_handle = i == submitCount - 1 ? fence : VK_NULL_HANDLE,
         .can_feedback = vn_queue_can_feedback(queue),
      };
      VkResult result = vn_queue_submit(&submit);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!submitCount) {
      return vn_queue_submit(&(struct vn_queue_submission){
         .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .queue_handle = queue,
         .fence_handle = fence,
         .can_feedback = vn_queue_can_feedback(queue),
      });
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submit_2_to_1(struct vn_device *dev,
                       VkQueue queue_handle,
                       const VkSubmitInfo2 *submit,
                       VkFence fence_handle)
{
   VkResult result;
   const void *pnext = NULL;

   assert(submit);

   VkProtectedSubmitInfo _protected;
   VkDeviceGroupSubmitInfo _group;
   VkTimelineSemaphoreSubmitInfo _timeline;

   STACK_ARRAY(VkSemaphore, _wait_sem_handles,
               submit->waitSemaphoreInfoCount);
   STACK_ARRAY(VkPipelineStageFlags, _wait_stages,
               submit->waitSemaphoreInfoCount);
   STACK_ARRAY(uint32_t, _wait_dev_indices, submit->waitSemaphoreInfoCount);
   STACK_ARRAY(uint64_t, _wait_values, submit->waitSemaphoreInfoCount);
   STACK_ARRAY(VkCommandBuffer, _cmd_handles, submit->commandBufferInfoCount);
   STACK_ARRAY(uint32_t, _cmd_dev_indices, submit->commandBufferInfoCount);
   STACK_ARRAY(VkSemaphore, _signal_sem_handles,
               submit->signalSemaphoreInfoCount);
   STACK_ARRAY(uint32_t, _signal_dev_indices,
               submit->signalSemaphoreInfoCount);
   STACK_ARRAY(uint64_t, _signal_values, submit->signalSemaphoreInfoCount);

   if (submit->flags & VK_SUBMIT_PROTECTED_BIT) {
      _protected = (VkProtectedSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO,
         .pNext = pnext,
         .protectedSubmit = VK_TRUE,
      };
      pnext = &_protected;
   }

   if (dev->device_mask > 1) {
      for (uint32_t i = 0; i < submit->waitSemaphoreInfoCount; i++) {
         _wait_dev_indices[i] = submit->pWaitSemaphoreInfos[i].deviceIndex;
      }
      for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++) {
         _cmd_dev_indices[i] = submit->pCommandBufferInfos[i].deviceMask;
      }
      for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++) {
         _signal_dev_indices[i] = submit->pSignalSemaphoreInfos[i].deviceIndex;
      }
      _group = (VkDeviceGroupSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,
         .pNext = pnext,
         .waitSemaphoreCount = submit->waitSemaphoreInfoCount,
         .pWaitSemaphoreDeviceIndices = _wait_dev_indices,
         .commandBufferCount = submit->commandBufferInfoCount,
         .pCommandBufferDeviceMasks = _cmd_dev_indices,
         .signalSemaphoreCount = submit->signalSemaphoreInfoCount,
         .pSignalSemaphoreDeviceIndices = _signal_dev_indices,
      };
      pnext = &_group;
   }

   bool has_wait_timeline_sem = false;
   for (uint32_t i = 0; i < submit->waitSemaphoreInfoCount; i++) {
      _wait_sem_handles[i] = submit->pWaitSemaphoreInfos[i].semaphore;
      _wait_stages[i] = submit->pWaitSemaphoreInfos[i].stageMask
                           ? submit->pWaitSemaphoreInfos[i].stageMask
                           : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

      VK_FROM_HANDLE(vn_semaphore, sem, _wait_sem_handles[i]);
      has_wait_timeline_sem |= sem->type == VK_SEMAPHORE_TYPE_TIMELINE;
   }
   if (has_wait_timeline_sem) {
      for (uint32_t i = 0; i < submit->waitSemaphoreInfoCount; i++)
         _wait_values[i] = submit->pWaitSemaphoreInfos[i].value;
   }

   bool has_signal_timeline_sem = false;
   for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++) {
      _signal_sem_handles[i] = submit->pSignalSemaphoreInfos[i].semaphore;

      VK_FROM_HANDLE(vn_semaphore, sem, _signal_sem_handles[i]);
      has_signal_timeline_sem |= sem->type == VK_SEMAPHORE_TYPE_TIMELINE;
   }
   if (has_signal_timeline_sem) {
      for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++)
         _signal_values[i] = submit->pSignalSemaphoreInfos[i].value;
   }

   if (has_wait_timeline_sem || has_signal_timeline_sem) {
      _timeline = (VkTimelineSemaphoreSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
         .pNext = pnext,
         .waitSemaphoreValueCount =
            has_wait_timeline_sem ? submit->waitSemaphoreInfoCount : 0,
         .pWaitSemaphoreValues = _wait_values,
         .signalSemaphoreValueCount =
            has_signal_timeline_sem ? submit->signalSemaphoreInfoCount : 0,
         .pSignalSemaphoreValues = _signal_values,
      };
      pnext = &_timeline;
   }

   for (uint32_t i = 0; i < submit->commandBufferInfoCount; i++)
      _cmd_handles[i] = submit->pCommandBufferInfos[i].commandBuffer;

   const VkSubmitInfo _submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = pnext,
      .waitSemaphoreCount = submit->waitSemaphoreInfoCount,
      .pWaitSemaphores = _wait_sem_handles,
      .pWaitDstStageMask = _wait_stages,
      .commandBufferCount = submit->commandBufferInfoCount,
      .pCommandBuffers = _cmd_handles,
      .signalSemaphoreCount = submit->signalSemaphoreInfoCount,
      .pSignalSemaphores = _signal_sem_handles,
   };
   result = vn_queue_submit(&(struct vn_queue_submission){
      .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .queue_handle = queue_handle,
      .submit_batch = &_submit,
      .fence_handle = fence_handle,
      .can_feedback = vn_queue_can_feedback(queue_handle),
   });

   STACK_ARRAY_FINISH(_wait_sem_handles);
   STACK_ARRAY_FINISH(_wait_stages);
   STACK_ARRAY_FINISH(_wait_dev_indices);
   STACK_ARRAY_FINISH(_wait_values);
   STACK_ARRAY_FINISH(_cmd_handles);
   STACK_ARRAY_FINISH(_cmd_dev_indices);
   STACK_ARRAY_FINISH(_signal_sem_handles);
   STACK_ARRAY_FINISH(_signal_dev_indices);
   STACK_ARRAY_FINISH(_signal_values);

   return result;
}

static VkResult
vn_queue_submit_2(VkQueue queue_handle,
                  const VkSubmitInfo2 *batch,
                  VkFence fence_handle)
{
   VK_FROM_HANDLE(vk_queue, queue_vk, queue_handle);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);

   assert(batch);

   if (!dev->base.vk.enabled_features.synchronization2) {
      VN_TRACE_SCOPE("2->1");
      return vn_queue_submit_2_to_1(dev, queue_handle, batch, fence_handle);
   }

   struct vn_queue_submission submit = {
      .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .queue_handle = queue_handle,
      .submit2_batch = batch,
      .fence_handle = fence_handle,
      .can_feedback = vn_queue_can_feedback(queue_handle),
   };
   return vn_queue_submit(&submit);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueSubmit2(VkQueue queue,
                uint32_t submitCount,
                const VkSubmitInfo2 *pSubmits,
                VkFence fence)
{
   VN_TRACE_FUNC();

   vn_tls_set_async_pipeline_create();
   vn_wsi_flush(queue);

   for (uint32_t i = 0; i < submitCount; i++) {
      VkResult result = vn_queue_submit_2(
         queue, &pSubmits[i], i == submitCount - 1 ? fence : VK_NULL_HANDLE);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!submitCount) {
      return vn_queue_submit(&(struct vn_queue_submission){
         .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .queue_handle = queue,
         .fence_handle = fence,
         .can_feedback = vn_queue_can_feedback(queue),
      });
   }

   return vn_wsi_fence_wait(queue);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueBindSparse(VkQueue queue,
                   uint32_t bindInfoCount,
                   const VkBindSparseInfo *pBindInfo,
                   VkFence fence)
{
   VN_TRACE_FUNC();

   vn_wsi_flush(queue);

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      struct vn_queue_submission submit = {
         .batch_type = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
         .queue_handle = queue,
         .sparse_batch = &pBindInfo[i],
         .fence_handle = i == bindInfoCount - 1 ? fence : VK_NULL_HANDLE,
      };
      VkResult result = vn_queue_submit(&submit);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!bindInfoCount) {
      return vn_queue_submit(&(struct vn_queue_submission){
         .batch_type = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
         .queue_handle = queue,
         .fence_handle = fence,
      });
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueueWaitIdle(VkQueue _queue)
{
   VN_TRACE_FUNC();
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   VkDevice dev_handle = vk_device_to_handle(queue->base.vk.base.device);
   struct vn_device *dev = vn_device_from_handle(dev_handle);
   VkResult result;

   vn_wsi_flush(_queue);

   /* lazily create queue wait fence for queue idle waiting */
   if (queue->wait_fence == VK_NULL_HANDLE) {
      const VkFenceCreateInfo create_info = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
         .flags = 0,
      };
      result =
         vn_CreateFence(dev_handle, &create_info, NULL, &queue->wait_fence);
      if (result != VK_SUCCESS)
         return result;
   }

   result = vn_queue_submit(&(struct vn_queue_submission){
      .batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .queue_handle = _queue,
      .fence_handle = queue->wait_fence,
      .can_feedback = vn_queue_can_feedback(_queue),
   });
   if (result != VK_SUCCESS)
      return result;

   result =
      vn_WaitForFences(dev_handle, 1, &queue->wait_fence, true, UINT64_MAX);
   vn_ResetFences(dev_handle, 1, &queue->wait_fence);

   return vn_result(dev->instance, result);
}
