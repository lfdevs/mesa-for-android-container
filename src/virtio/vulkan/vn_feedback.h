/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VN_FEEDBACK_H
#define VN_FEEDBACK_H

#include "vn_common.h"

struct vn_feedback_pool {
   /* single lock for simplicity though free_slots can use another */
   simple_mtx_t mutex;

   struct vn_device *dev;
   const VkAllocationCallbacks *alloc;

   /* size in bytes of the feedback buffer */
   uint32_t size;
   /* size in bytes used of the active feedback buffer */
   uint32_t used;
   /* alignment in bytes for slot suballocation from the feedback buffer */
   uint32_t alignment;

   /* first entry is the active feedback buffer */
   struct list_head fb_bufs;

   /* cache for returned feedback slots */
   struct list_head free_slots;
};

enum vn_feedback_type {
   VN_FEEDBACK_TYPE_SEMAPHORE = 0x2,
   VN_FEEDBACK_TYPE_EVENT = 0x4,
   VN_FEEDBACK_TYPE_QUERY = 0x8,
};

struct vn_feedback_slot {
   enum vn_feedback_type type;
   uint32_t offset;
   VkBuffer buf_handle;

   union {
      void *data;
      VkResult *status;
      uint64_t *counter;
   };

   struct list_head head;
};

struct vn_feedback_cmd_pool {
   simple_mtx_t mutex;

   VkCommandPool pool_handle;
   struct list_head free_qfb_cmds;
};

/* coherent buffer with bound and mapped memory */
struct vn_feedback_buffer {
   VkBuffer buf_handle;
   VkDeviceMemory mem_handle;
   void *data;

   struct list_head head;
};

struct vn_sync_feedback_cmd {
   struct vn_feedback_slot *src_slot;
   VkCommandBuffer *cmd_handles;

   struct list_head head;
};

struct vn_sync_feedback {
   struct vn_feedback_slot *slot;

   /* Lists of allocated vn_sync_feedback_cmd
    *
    * On submission prepare, sfb cmd is cache allocated from the free list
    * and is moved to the pending list after initialization.
    *
    * On submission cleanup, sfb cmds of the owner semaphores are checked
    * and cached to the free list if they have been "signaled", which is
    * proxyed via the src slot value having been reached.
    */
   struct list_head pending_cmds;
   struct list_head free_cmds;
   uint32_t free_cmd_count;

   /* Lock for accessing free/pending sfb cmds */
   simple_mtx_t cmd_mtx;

   /* Indicate whether the timeline counter value in the feedback slot is
    * pollable. When pollable is false, the feedback has been suspended and
    * the slot won't be signaled to the pending counter.
    * - suspend: submit on queues not supporting feedback
    * - resume if any of below occurs:
    *   - vn_SignalSemaphore
    *   - when the queried counter value is no smaller than the suspended
    *     counter value
    */
   bool pollable;

   /* When feedback is active, signaled_counter is the cached counter value
    * to track if an async sem wait call is needed.
    *
    * When feedback is suspended, suspended_counter tracks the greatest
    * signal counter value submitted on queues not supporting feedback.
    *
    * They share the same storage and the value is monotonic.
    */
   union {
      uint64_t signaled_counter;
      uint64_t suspended_counter;
   };

   /* Lock for checking if an async sem wait call is needed based on
    * the current counter value and signaled_counter to ensure async
    * wait order across threads.
    *
    * Also lock to protect suspended_counter and pollable updates.
    */
   simple_mtx_t counter_mtx;
};

struct vn_query_feedback_cmd {
   struct vn_feedback_cmd_pool *fb_cmd_pool;
   VkCommandBuffer cmd_handle;

   struct list_head head;
};

VkResult
vn_feedback_pool_init(struct vn_device *dev,
                      struct vn_feedback_pool *pool,
                      uint32_t size,
                      const VkAllocationCallbacks *alloc);

void
vn_feedback_pool_fini(struct vn_feedback_pool *pool);

struct vn_feedback_slot *
vn_feedback_pool_alloc(struct vn_feedback_pool *pool,
                       enum vn_feedback_type type);

void
vn_feedback_pool_free(struct vn_feedback_pool *pool,
                      struct vn_feedback_slot *slot);

static inline VkResult
vn_feedback_get_status(struct vn_feedback_slot *slot)
{
   return *slot->status;
}

static inline void
vn_feedback_reset_status(struct vn_feedback_slot *slot)
{
   assert(slot->type == VN_FEEDBACK_TYPE_EVENT);
   *slot->status = VK_EVENT_RESET;
}

static inline void
vn_feedback_set_status(struct vn_feedback_slot *slot, VkResult status)
{
   assert(slot->type == VN_FEEDBACK_TYPE_EVENT);
   *slot->status = status;
}

static inline uint64_t
vn_feedback_get_counter(struct vn_feedback_slot *slot)
{
   assert(slot->type == VN_FEEDBACK_TYPE_SEMAPHORE);
   return *slot->counter;
}

static inline void
vn_feedback_set_counter(struct vn_feedback_slot *slot, uint64_t counter)
{
   assert(slot->type == VN_FEEDBACK_TYPE_SEMAPHORE);
   *slot->counter = counter;
}

VkResult
vn_feedback_buffer_create(struct vn_device *dev,
                          uint32_t size,
                          const VkAllocationCallbacks *alloc,
                          struct vn_feedback_buffer **out_fb_buf);

void
vn_feedback_buffer_destroy(struct vn_device *dev,
                           struct vn_feedback_buffer *fb_buf,
                           const VkAllocationCallbacks *alloc);

void
vn_event_feedback_cmd_record(VkCommandBuffer cmd_handle,
                             VkEvent ev_handle,
                             VkPipelineStageFlags2 src_stage_mask,
                             VkResult status,
                             bool sync2);

VkCommandBuffer
vn_sync_feedback_command(struct vn_device *dev,
                         struct vn_sync_feedback *sfb,
                         uint32_t qfi,
                         uint64_t counter);

bool
vn_sync_feedback_query(struct vn_device *dev,
                       struct vn_sync_feedback *sfb,
                       uint64_t *out_counter);

void
vn_sync_feedback_cmd_recycle(struct vn_device *dev,
                             struct vn_sync_feedback *sfb);

void
vn_sync_feedback_write(struct vn_sync_feedback *sfb, uint64_t counter);

void
vn_sync_feedback_suspend(struct vn_sync_feedback *sfb, uint64_t counter);

void
vn_sync_feedback_try_resume(struct vn_sync_feedback *sfb, uint64_t counter);

static inline bool
vn_sync_feedback_pollable(struct vn_sync_feedback *sfb)
{
   return sfb->pollable;
}

static inline bool
vn_sync_feedback_enabled(struct vn_sync_feedback *sfb)
{
   return sfb->slot != NULL;
}

VkResult
vn_sync_feedback_init(struct vn_device *dev,
                      struct vn_sync_feedback *sfb,
                      uint64_t initial_value);

void
vn_sync_feedback_fini(struct vn_device *dev, struct vn_sync_feedback *sfb);

VkResult
vn_query_feedback_cmd_alloc(VkDevice dev_handle,
                            struct vn_feedback_cmd_pool *fb_cmd_pool,
                            struct list_head *resolved_query_records,
                            struct vn_query_feedback_cmd **out_qfb_cmd);

void
vn_query_feedback_cmd_free(struct vn_query_feedback_cmd *qfb_cmd);

VkResult
vn_feedback_cmd_pools_init(struct vn_device *dev);

void
vn_feedback_cmd_pools_fini(struct vn_device *dev);

#endif /* VN_FEEDBACK_H */
