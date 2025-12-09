#include "zink_batch.h"
#include "zink_context.h"
#include "zink_descriptors.h"
#include "zink_kopper.h"
#include "zink_program.h"
#include "zink_query.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"

#ifdef VK_USE_PLATFORM_METAL_EXT
#include "QuartzCore/CAMetalLayer.h"
#endif

#define MAX_VIEW_COUNT 500

void
debug_describe_zink_batch_state(char *buf, const struct zink_batch_state *ptr)
{
   sprintf(buf, "zink_batch_state");
}

/* this resets the batch usage and tracking for a resource object */
static void
reset_obj(struct zink_screen *screen, struct zink_batch_state *bs, struct zink_resource_object *obj)
{
   /* if no batch usage exists after removing the usage from 'bs', this resource is considered fully idle */
   if (!zink_resource_object_usage_unset(obj, bs)) {
      /* the resource is idle, so reset all access/reordering info */
      obj->unordered_read = true;
      obj->unordered_write = true;
      obj->access = 0;
      obj->unordered_access = 0;
      obj->last_write = 0;
      obj->access_stage = 0;
      obj->unordered_access_stage = 0;
      obj->copies_need_reset = true;
      obj->unsync_access = true;
      if (obj->dt)
         zink_kopper_prune_batch_usage(obj->dt, &bs->usage);
   }
   zink_resource_object_reference(screen, &obj, NULL);
}

/* reset all the resource objects in a given batch object list */
static void
reset_obj_list(struct zink_screen *screen, struct zink_batch_state *bs, struct zink_batch_obj_list *list)
{
   for (unsigned i = 0; i < list->num_buffers; i++)
      reset_obj(screen, bs, list->objs[i]);
   list->num_buffers = 0;
}

/* reset a given batch state */
static void
reset_batch_state_internal(struct zink_screen *screen, struct zink_batch_state *bs)
{
   MESA_TRACE_FUNC();
   VkResult result = VKSCR(ResetCommandPool)(screen->dev, bs->cmdpool, 0);
   if (result != VK_SUCCESS)
      mesa_loge("ZINK: vkResetCommandPool failed (%s)", vk_Result_to_str(result));
   result = VKSCR(ResetCommandPool)(screen->dev, bs->unsynchronized_cmdpool, 0);
   if (result != VK_SUCCESS)
      mesa_loge("ZINK: vkResetCommandPool failed (%s)", vk_Result_to_str(result));

   /* unref/reset all used resources */
   reset_obj_list(screen, bs, &bs->real_objs);
   reset_obj_list(screen, bs, &bs->slab_objs);
   reset_obj_list(screen, bs, &bs->sparse_objs);
   reset_obj_list(screen, bs, &bs->unsync_objs);
   while (util_dynarray_contains(&bs->swapchain_obj_unsync, struct zink_resource_object*)) {
      struct zink_resource_object *obj = util_dynarray_pop(&bs->swapchain_obj_unsync, struct zink_resource_object*);
      reset_obj(screen, bs, obj);
   }
   while (util_dynarray_contains(&bs->swapchain_obj, struct zink_resource_object*)) {
      struct zink_resource_object *obj = util_dynarray_pop(&bs->swapchain_obj, struct zink_resource_object*);
      reset_obj(screen, bs, obj);
   }

   memset(&bs->buffer_indices_hashlist, -1, sizeof(bs->buffer_indices_hashlist));

   util_dynarray_foreach(&bs->dead_querypools, VkQueryPool, pool)
      VKSCR(DestroyQueryPool)(screen->dev, *pool, NULL);
   util_dynarray_clear(&bs->dead_querypools);

   /* samplers are appended to the batch state in which they are destroyed
    * to ensure deferred deletion without destroying in-use objects
    */
   util_dynarray_foreach(&bs->zombie_samplers, VkSampler, samp) {
      VKSCR(DestroySampler)(screen->dev, *samp, NULL);
   }
   util_dynarray_clear(&bs->zombie_samplers);

   zink_batch_descriptor_reset(screen, bs);

   while (util_dynarray_contains(&bs->freed_sparse_backing_bos, struct zink_bo*)) {
      struct zink_bo *bo = util_dynarray_pop(&bs->freed_sparse_backing_bos, struct zink_bo*);
      zink_bo_unref(screen, bo);
   }

   /* programs are refcounted and batch-tracked */
   set_foreach_remove(&bs->programs, entry) {
      struct zink_program *pg = (struct zink_program*)entry->key;
      zink_batch_usage_unset(&pg->batch_uses, bs);
      zink_program_reference(screen, &pg, NULL);
   }

   bs->resource_size = 0;
   bs->signal_semaphore = VK_NULL_HANDLE;
   bs->sparse_semaphore = VK_NULL_HANDLE;
   util_dynarray_clear(&bs->wait_semaphore_stages);
   util_dynarray_clear(&bs->wait_semaphore_values);
   util_dynarray_clear(&bs->wait_semaphores);
   util_dynarray_clear(&bs->user_signal_semaphores);
   util_dynarray_clear(&bs->user_signal_semaphore_values);

   bs->present = VK_NULL_HANDLE;
   /* check the arrays first to avoid locking unnecessarily */
   if (util_dynarray_contains(&bs->acquires, VkSemaphore) || util_dynarray_contains(&bs->tracked_semaphores, VkSemaphore)) {
      simple_mtx_lock(&screen->semaphores_lock);
      util_dynarray_append_dynarray(&screen->semaphores, &bs->acquires);
      util_dynarray_clear(&bs->acquires);
      util_dynarray_append_dynarray(&screen->semaphores, &bs->tracked_semaphores);
      util_dynarray_clear(&bs->tracked_semaphores);
      simple_mtx_unlock(&screen->semaphores_lock);
   }
   if (util_dynarray_contains(&bs->signal_semaphores, VkSemaphore) || util_dynarray_contains(&bs->fd_wait_semaphores, VkSemaphore)) {
      simple_mtx_lock(&screen->semaphores_lock);
      util_dynarray_append_dynarray(&screen->fd_semaphores, &bs->signal_semaphores);
      util_dynarray_clear(&bs->signal_semaphores);
      util_dynarray_append_dynarray(&screen->fd_semaphores, &bs->fd_wait_semaphores);
      util_dynarray_clear(&bs->fd_wait_semaphores);
      simple_mtx_unlock(&screen->semaphores_lock);
   }
   bs->swapchain = NULL;

   util_dynarray_foreach(&bs->fences, struct zink_tc_fence*, mfence)
      zink_fence_reference(screen, mfence, NULL);
   util_dynarray_clear(&bs->fences);

   bs->unordered_write_access = VK_ACCESS_NONE;
   bs->unordered_write_stages = VK_PIPELINE_STAGE_NONE;

   /* only increment batch generation if previously in-use to avoid false detection of batch completion */
   if (bs->fence.submitted)
      bs->usage.submit_count++;
   /* only reset submitted here so that tc fence desync can pick up the 'completed' flag
    * before the state is reused
    */
   bs->fence.submitted = false;
   if (bs->fence.batch_id)
      zink_screen_update_last_finished(screen, bs->fence.batch_id);
   bs->fence.batch_id = 0;
   bs->usage.usage = 0;
   bs->next = NULL;
   bs->last_added_obj = NULL;

   bs->has_work = false;
   bs->has_reordered_work = false;
   bs->has_unsync = false;
}

static void
reset_batch_state_ctx(struct zink_context *ctx, struct zink_batch_state *bs)
{
   /* this is where bindless texture/buffer ids get recycled */
   for (unsigned i = 0; i < 2; i++) {
      while (util_dynarray_contains(&bs->bindless_releases[i], uint32_t)) {
         uint32_t handle = util_dynarray_pop(&bs->bindless_releases[i], uint32_t);
         bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
         struct util_idalloc *ids = i ? &ctx->di.bindless[is_buffer].img_slots : &ctx->di.bindless[is_buffer].tex_slots;
         util_idalloc_free(ids, is_buffer ? handle - ZINK_MAX_BINDLESS_HANDLES : handle);
      }
   }

   /* queries must only be destroyed once they are inactive */
   set_foreach_remove(&bs->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      zink_prune_query(bs, query);
   }
}

void
zink_reset_batch_state(struct zink_context *ctx, struct zink_batch_state *bs)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   reset_batch_state_ctx(ctx, bs);
   reset_batch_state_internal(screen, bs);
}

/* utility for resetting a batch state; called on context destruction */
void
zink_clear_batch_state(struct zink_context *ctx, struct zink_batch_state *bs)
{
   bs->fence.completed = true;
   zink_reset_batch_state(ctx, bs);
}

/* utility for managing the singly-linked batch state list */
static void
pop_batch_state(struct zink_context *ctx)
{
   const struct zink_batch_state *bs = ctx->batch_states;
   ctx->batch_states = bs->next;
   ctx->batch_states_count--;
   if (ctx->last_batch_state == bs)
      ctx->last_batch_state = NULL;
}

/* reset all batch states and append to the free state list
 * only usable after a full stall
 */
void
zink_batch_reset_all(struct zink_context *ctx)
{
   while (ctx->batch_states) {
      struct zink_batch_state *bs = ctx->batch_states;
      bs->fence.completed = true;
      pop_batch_state(ctx);
      zink_reset_batch_state(ctx, bs);
      zink_batch_state_append(&ctx->free_batch_states, &ctx->last_free_batch_state, bs);
   }
}

/* called only on context destruction */
void
zink_batch_state_destroy(struct zink_screen *screen, struct zink_batch_state *bs)
{
   if (!bs)
      return;

   reset_batch_state_internal(screen, bs);

   util_queue_fence_destroy(&bs->flush_completed);

   cnd_destroy(&bs->usage.flush);
   mtx_destroy(&bs->usage.mtx);

   if (bs->cmdbuf)
      VKSCR(FreeCommandBuffers)(screen->dev, bs->cmdpool, 1, &bs->cmdbuf);
   if (bs->reordered_cmdbuf)
      VKSCR(FreeCommandBuffers)(screen->dev, bs->cmdpool, 1, &bs->reordered_cmdbuf);
   if (bs->cmdpool)
      VKSCR(DestroyCommandPool)(screen->dev, bs->cmdpool, NULL);
   if (bs->unsynchronized_cmdbuf)
      VKSCR(FreeCommandBuffers)(screen->dev, bs->unsynchronized_cmdpool, 1, &bs->unsynchronized_cmdbuf);
   if (bs->unsynchronized_cmdpool)
      VKSCR(DestroyCommandPool)(screen->dev, bs->unsynchronized_cmdpool, NULL);
   free(bs->real_objs.objs);
   free(bs->slab_objs.objs);
   free(bs->unsync_objs.objs);
   free(bs->sparse_objs.objs);
   util_dynarray_fini(&bs->freed_sparse_backing_bos);
   util_dynarray_fini(&bs->dead_querypools);
   util_dynarray_fini(&bs->swapchain_obj);
   util_dynarray_fini(&bs->swapchain_obj_unsync);
   util_dynarray_fini(&bs->zombie_samplers);
   util_dynarray_fini(&bs->bindless_releases[0]);
   util_dynarray_fini(&bs->bindless_releases[1]);
   util_dynarray_fini(&bs->acquires);
   util_dynarray_fini(&bs->signal_semaphores);
   util_dynarray_fini(&bs->user_signal_semaphores);
   util_dynarray_fini(&bs->user_signal_semaphore_values);
   util_dynarray_fini(&bs->wait_semaphores);
   util_dynarray_fini(&bs->wait_semaphore_stages);
   util_dynarray_fini(&bs->wait_semaphore_values);
   util_dynarray_fini(&bs->fd_wait_semaphores);
   util_dynarray_fini(&bs->fd_wait_semaphore_stages);
   util_dynarray_fini(&bs->tracked_semaphores);
   util_dynarray_fini(&bs->acquire_flags);
   unsigned num_mfences = util_dynarray_num_elements(&bs->fence.mfences, void *);
   struct zink_tc_fence **mfence = bs->fence.mfences.data;
   for (unsigned i = 0; i < num_mfences; i++) {
      mfence[i]->fence = NULL;
   }
   util_dynarray_fini(&bs->fence.mfences);
   zink_batch_descriptor_deinit(screen, bs);
   ralloc_free(bs);
}

static void
zink_label_cmd_buffer(struct zink_context *ctx, VkDevice device, VkCommandBuffer cmd_buffer, const char *name)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   VkDebugUtilsObjectNameInfoEXT name_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .pNext = NULL,
      .objectType = VK_OBJECT_TYPE_COMMAND_BUFFER,
      .objectHandle = (uintptr_t)cmd_buffer,
      .pObjectName = name,
   };

   VKSCR(SetDebugUtilsObjectNameEXT)(device, &name_info);
}

/* batch states are created:
 * - on context creation
 * - dynamically up to a threshold if no free ones are available
 */
static struct zink_batch_state *
create_batch_state(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = rzalloc(NULL, struct zink_batch_state);
   VkCommandPoolCreateInfo cpci = {0};
   cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   cpci.queueFamilyIndex = screen->gfx_queue;
   VkResult result;

   VRAM_ALLOC_LOOP(result,
      VKSCR(CreateCommandPool)(screen->dev, &cpci, NULL, &bs->cmdpool),
      if (result != VK_SUCCESS) {
         mesa_loge("ZINK: vkCreateCommandPool failed (%s)", vk_Result_to_str(result));
         goto fail;
      }
   );
   VRAM_ALLOC_LOOP(result,
      VKSCR(CreateCommandPool)(screen->dev, &cpci, NULL, &bs->unsynchronized_cmdpool),
      if (result != VK_SUCCESS) {
         mesa_loge("ZINK: vkCreateCommandPool failed (%s)", vk_Result_to_str(result));
         goto fail;
      }
   );

   VkCommandBuffer cmdbufs[2];
   VkCommandBufferAllocateInfo cbai = {0};
   cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cbai.commandPool = bs->cmdpool;
   cbai.commandBufferCount = 2;

   VRAM_ALLOC_LOOP(result,
      VKSCR(AllocateCommandBuffers)(screen->dev, &cbai, cmdbufs),
      if (result != VK_SUCCESS) {
         mesa_loge("ZINK: vkAllocateCommandBuffers failed (%s)", vk_Result_to_str(result));
         goto fail;
      }
   );

   bs->cmdbuf = cmdbufs[0];
   bs->reordered_cmdbuf = cmdbufs[1];

   cbai.commandPool = bs->unsynchronized_cmdpool;
   cbai.commandBufferCount = 1;
   VRAM_ALLOC_LOOP(result,
      VKSCR(AllocateCommandBuffers)(screen->dev, &cbai, &bs->unsynchronized_cmdbuf);,
      if (result != VK_SUCCESS) {
         mesa_loge("ZINK: vkAllocateCommandBuffers failed (%s)", vk_Result_to_str(result));
         goto fail;
      }
   );

   zink_label_cmd_buffer(ctx, screen->dev, cmdbufs[0], "zink cmdbuf");
   zink_label_cmd_buffer(ctx, screen->dev, cmdbufs[1], "zink barrier cmdbuf");

#define SET_CREATE(ptr) \
   _mesa_set_init(ptr, bs, _mesa_hash_pointer, _mesa_key_pointer_equal)

   bs->ctx = ctx;

   SET_CREATE(&bs->programs);
   SET_CREATE(&bs->active_queries);
   SET_CREATE(&bs->dmabuf_exports);
   bs->signal_semaphores = UTIL_DYNARRAY_INIT;
   bs->user_signal_semaphores = UTIL_DYNARRAY_INIT;
   bs->user_signal_semaphore_values = UTIL_DYNARRAY_INIT;
   bs->wait_semaphores = UTIL_DYNARRAY_INIT;
   bs->tracked_semaphores = UTIL_DYNARRAY_INIT;
   bs->fd_wait_semaphores = UTIL_DYNARRAY_INIT;
   bs->fences = UTIL_DYNARRAY_INIT;
   bs->dead_querypools = UTIL_DYNARRAY_INIT;
   bs->wait_semaphore_stages = UTIL_DYNARRAY_INIT;
   bs->wait_semaphore_values = UTIL_DYNARRAY_INIT;
   bs->fd_wait_semaphore_stages = UTIL_DYNARRAY_INIT;
   bs->zombie_samplers = UTIL_DYNARRAY_INIT;
   bs->freed_sparse_backing_bos = UTIL_DYNARRAY_INIT;
   bs->acquires = UTIL_DYNARRAY_INIT;
   bs->acquire_flags = UTIL_DYNARRAY_INIT;
   bs->bindless_releases[0] = UTIL_DYNARRAY_INIT;
   bs->bindless_releases[1] = UTIL_DYNARRAY_INIT;
   bs->swapchain_obj = UTIL_DYNARRAY_INIT;
   bs->swapchain_obj_unsync = UTIL_DYNARRAY_INIT;
   bs->fence.mfences = UTIL_DYNARRAY_INIT;

   cnd_init(&bs->usage.flush);
   mtx_init(&bs->usage.mtx, mtx_plain);
   simple_mtx_init(&bs->exportable_lock, mtx_plain);
   memset(&bs->buffer_indices_hashlist, -1, sizeof(bs->buffer_indices_hashlist));

   if (!zink_batch_descriptor_init(screen, bs))
      goto fail;

   util_queue_fence_init(&bs->flush_completed);

   return bs;
fail:
   zink_batch_state_destroy(screen, bs);
   return NULL;
}

/* a batch state is considered "free" if it is both submitted and completed */
static inline bool
find_unused_state(struct zink_batch_state *bs)
{
   struct zink_fence *fence = &bs->fence;
   /* we can't reset these from fence_finish because threads */
   bool completed = p_atomic_read(&fence->completed);
   bool submitted = p_atomic_read(&fence->submitted);
   return submitted && completed;
}

static struct zink_batch_state *
find_screen_state(struct zink_screen *screen, struct zink_context *ctx)
{
   struct zink_batch_state *bs = NULL;
   simple_mtx_lock(&screen->free_batch_states_lock);
   if (screen->free_batch_states) {
      bs = screen->free_batch_states;
      bs->ctx = ctx;
      screen->free_batch_states = bs->next;
      bs->next = NULL;
      if (bs == screen->last_free_batch_state)
         screen->last_free_batch_state = NULL;
   }
   simple_mtx_unlock(&screen->free_batch_states_lock);
   return bs;
}

/* only returns batch state for COPY_ONLY contexts */
static struct zink_batch_state *
find_completed_batch_state(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   /* states are stored sequentially, so if the first one doesn't work, none of them will */
   struct zink_batch_state *i = ctx->batch_states;
   while (i) {
      struct zink_batch_state *j = i->next;
      /* only a submitted state can be reused */
      if (i->fence.submitted &&
          /* a submitted state must have completed before it can be reused */
          (zink_screen_check_last_finished(screen, i->fence.batch_id) || i->fence.completed)) {
         pop_batch_state(ctx);
         reset_batch_state_ctx(ctx, i);
         if (ctx->flags & ZINK_CONTEXT_COPY_ONLY) {
            /* copy context batch states don't have descriptors and cannot be used interchangeably with others */
            reset_batch_state_internal(screen, i);
            return i;
         }
         simple_mtx_lock(&screen->active_batch_states_lock);
         i->ctx = NULL;
         zink_batch_state_append(&screen->active_batch_states, &screen->last_active_batch_state, i);
         simple_mtx_unlock(&screen->active_batch_states_lock);
         i = j;
      } else {
         break;
      }
   }
   return NULL;
}

/* find a "free" batch state */
static struct zink_batch_state *
get_batch_state(struct zink_context *ctx)
{
   MESA_TRACE_FUNC();
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = NULL;

   /* try from the ones that are known to be free first */
   if (ctx->free_batch_states) {
      bs = ctx->free_batch_states;
      ctx->free_batch_states = bs->next;
      if (bs == ctx->last_free_batch_state)
         ctx->last_free_batch_state = NULL;
   }
   /* always try to recycle ctx states first */
   if (!bs)
      bs = find_completed_batch_state(ctx);

   if (!bs)
      bs = find_screen_state(screen, ctx);

   if (bs) {
      bs->next = NULL;
      if  (zink_descriptor_mode == ZINK_DESCRIPTOR_MODE_DB && bs->dd.db &&
           bs->dd.db->base.b.width0 < bs->ctx->dd.db.max_db_size * screen->base_descriptor_size) {
         /* reset again to catch descriptor buffer resize */
         zink_batch_descriptor_reset(screen, bs);
      }
   } else {
      if (!ctx->bs) {
         /* this is batch init, so create a few more states for later use */
         for (int i = 0; i < 3; i++) {
            struct zink_batch_state *state = create_batch_state(ctx);
            zink_batch_state_append(&ctx->free_batch_states, &ctx->last_free_batch_state, state);
         }
      }
      /* no batch states were available: make a new one */
      bs = create_batch_state(ctx);
   }
   return bs;
}

/* reset the batch object: get a new state and unset 'state->has_work' to disable flushing */
void
zink_reset_batch(struct zink_context *ctx)
{
   ctx->bs = get_batch_state(ctx);
   assert(ctx->bs);
}

void
zink_batch_bind_db(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = ctx->bs;
   unsigned count = 1;
   VkDescriptorBufferBindingInfoEXT infos[2] = {0};
   infos[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
   infos[0].address = bs->dd.db->obj->bda;
   infos[0].usage = bs->dd.db->obj->vkusage;
   assert(infos[0].usage);

   if (ctx->dd.bindless_init) {
      infos[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
      infos[1].address = ctx->dd.db.bindless_db->obj->bda;
      infos[1].usage = ctx->dd.db.bindless_db->obj->vkusage;
      assert(infos[1].usage);
      count++;
   }
   VKSCR(CmdBindDescriptorBuffersEXT)(bs->cmdbuf, count, infos);
   VKSCR(CmdBindDescriptorBuffersEXT)(bs->reordered_cmdbuf, count, infos);
   bs->dd.db_bound = true;
}

/* called on context creation and after flushing an old batch */
void
zink_start_batch(struct zink_context *ctx)
{
   MESA_TRACE_FUNC();
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   zink_reset_batch(ctx);
   struct zink_batch_state *bs = ctx->bs;

   bs->ctx = ctx;
   bs->usage.unflushed = true;

   VkCommandBufferBeginInfo cbbi = {0};
   cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   VkResult result;
   VRAM_ALLOC_LOOP(result,
      VKCTX(BeginCommandBuffer)(bs->cmdbuf, &cbbi),
      if (result != VK_SUCCESS)
         mesa_loge("ZINK: vkBeginCommandBuffer failed (%s)", vk_Result_to_str(result));
   );
   VRAM_ALLOC_LOOP(result,
      VKCTX(BeginCommandBuffer)(bs->reordered_cmdbuf, &cbbi),
      if (result != VK_SUCCESS)
         mesa_loge("ZINK: vkBeginCommandBuffer failed (%s)", vk_Result_to_str(result));
   );
   VRAM_ALLOC_LOOP(result,
      VKCTX(BeginCommandBuffer)(bs->unsynchronized_cmdbuf, &cbbi),
      if (result != VK_SUCCESS)
         mesa_loge("ZINK: vkBeginCommandBuffer failed (%s)", vk_Result_to_str(result));
   );

   bs->fence.completed = false;

   if (VKCTX(CmdInsertDebugUtilsLabelEXT) && screen->renderdoc_api) {
      VkDebugUtilsLabelEXT capture_label;
      /* Magic fallback which lets us bridge the Wine barrier over to Linux RenderDoc. */
      capture_label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
      capture_label.pNext = NULL;
      capture_label.pLabelName = "vr-marker,frame_end,type,application";
      memset(capture_label.color, 0, sizeof(capture_label.color));
      VKCTX(CmdInsertDebugUtilsLabelEXT)(bs->unsynchronized_cmdbuf, &capture_label);
      VKCTX(CmdInsertDebugUtilsLabelEXT)(bs->reordered_cmdbuf, &capture_label);
      VKCTX(CmdInsertDebugUtilsLabelEXT)(bs->cmdbuf, &capture_label);
   }

   unsigned renderdoc_frame = p_atomic_read(&screen->renderdoc_frame);
   if (!(ctx->flags & ZINK_CONTEXT_COPY_ONLY) && screen->renderdoc_api && !screen->renderdoc_capturing &&
        ((screen->renderdoc_capture_all && screen->screen_id == 1) || (renderdoc_frame >= screen->renderdoc_capture_start && renderdoc_frame <= screen->renderdoc_capture_end))) {
      screen->renderdoc_api->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(screen->instance), NULL);
      screen->renderdoc_capturing = true;
   }

   /* descriptor buffers must always be bound at the start of a batch */
   if (zink_descriptor_mode == ZINK_DESCRIPTOR_MODE_DB && !(ctx->flags & ZINK_CONTEXT_COPY_ONLY))
      zink_batch_bind_db(ctx);
   /* zero init for unordered blits */
   if (screen->info.have_EXT_attachment_feedback_loop_dynamic_state) {
      VKCTX(CmdSetAttachmentFeedbackLoopEnableEXT)(ctx->bs->cmdbuf, 0);
      VKCTX(CmdSetAttachmentFeedbackLoopEnableEXT)(ctx->bs->reordered_cmdbuf, 0);
      VKCTX(CmdSetAttachmentFeedbackLoopEnableEXT)(ctx->bs->unsynchronized_cmdbuf, 0);
   }
   if (screen->info.dynamic_state3_feats.extendedDynamicState3RepresentativeFragmentTestEnable) {
      VKCTX(CmdSetRepresentativeFragmentTestEnableNV)(ctx->bs->cmdbuf, 0);
      VKCTX(CmdSetRepresentativeFragmentTestEnableNV)(ctx->bs->reordered_cmdbuf, 0);
      VKCTX(CmdSetRepresentativeFragmentTestEnableNV)(ctx->bs->unsynchronized_cmdbuf, 0);
   }
}

/* common operations to run post submit; split out for clarity */
static void
post_submit(struct zink_batch_state *bs, struct zink_screen *screen)
{
   MESA_TRACE_FUNC();

   if (bs->is_device_lost) {
      if (bs->ctx->reset.reset)
         bs->ctx->reset.reset(bs->ctx->reset.data, PIPE_GUILTY_CONTEXT_RESET);
      else if (screen->abort_on_hang && !screen->robust_ctx_count)
         /* if nothing can save us, abort */
         abort();
      screen->device_lost = true;
   } else if (bs->ctx->batch_states_count > 5000) {
      /* throttle in case something crazy is happening */
      zink_screen_timeline_wait(screen, bs->fence.batch_id - 2500, OS_TIMEOUT_INFINITE);
   } else if (screen->curr_batch - screen->last_finished > 5) {
      /* try to avoid ooming by regularly checking for batch completion */
      zink_screen_timeline_wait(screen, screen->last_finished + 1, 0);
   }
}

typedef enum {
   ZINK_SUBMIT_WAIT_ACQUIRE,
   ZINK_SUBMIT_WAIT_FD,
   ZINK_SUBMIT_CMDBUF,
   ZINK_SUBMIT_SIGNAL_INTERNAL,
   ZINK_SUBMIT_SIGNAL_USER,
   ZINK_SUBMIT_MAX
} zink_submit;

#define ZINK_MAX_SIGNALS 3

static void
submit_queue(void *data, void *gdata, int thread_index)
{
   MESA_TRACE_FUNC();
   struct zink_batch_state *bs = data;
   struct zink_context *ctx = bs->ctx;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkSubmitInfo si[ZINK_SUBMIT_MAX] = {0};
   VkSubmitInfo *submit = si;
   int num_si = ZINK_SUBMIT_MAX;
   while (!bs->fence.batch_id)
      bs->fence.batch_id = (uint32_t)p_atomic_inc_return(&screen->curr_batch);
   bs->usage.usage = bs->fence.batch_id;
   assert(bs->usage.usage);
   bs->usage.unflushed = false;

   uint64_t batch_id = bs->fence.batch_id;
   /* first submit is just for acquire waits since they have a separate array */
   for (unsigned i = 0; i < ARRAY_SIZE(si); i++)
      si[i].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   if (bs->sparse_semaphore)
      util_dynarray_append(&bs->acquires, bs->sparse_semaphore);
   si[ZINK_SUBMIT_WAIT_ACQUIRE].waitSemaphoreCount = util_dynarray_num_elements(&bs->acquires, VkSemaphore);
   si[ZINK_SUBMIT_WAIT_ACQUIRE].pWaitSemaphores = bs->acquires.data;
   while (util_dynarray_num_elements(&bs->acquire_flags, VkPipelineStageFlags) < si[ZINK_SUBMIT_WAIT_ACQUIRE].waitSemaphoreCount) {
      VkPipelineStageFlags mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      util_dynarray_append(&bs->acquire_flags, mask);
   }
   assert(util_dynarray_num_elements(&bs->acquires, VkSemaphore) <= util_dynarray_num_elements(&bs->acquire_flags, VkPipelineStageFlags));
   si[ZINK_SUBMIT_WAIT_ACQUIRE].pWaitDstStageMask = bs->acquire_flags.data;

   si[ZINK_SUBMIT_WAIT_FD].waitSemaphoreCount = util_dynarray_num_elements(&bs->fd_wait_semaphores, VkSemaphore);
   si[ZINK_SUBMIT_WAIT_FD].pWaitSemaphores = bs->fd_wait_semaphores.data;
   while (util_dynarray_num_elements(&bs->fd_wait_semaphore_stages, VkPipelineStageFlags) < si[ZINK_SUBMIT_WAIT_FD].waitSemaphoreCount) {
      VkPipelineStageFlags mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      util_dynarray_append(&bs->fd_wait_semaphore_stages, mask);
   }
   assert(util_dynarray_num_elements(&bs->fd_wait_semaphores, VkSemaphore) <= util_dynarray_num_elements(&bs->fd_wait_semaphore_stages, VkPipelineStageFlags));
   si[ZINK_SUBMIT_WAIT_FD].pWaitDstStageMask = bs->fd_wait_semaphore_stages.data;

   if (si[ZINK_SUBMIT_WAIT_ACQUIRE].waitSemaphoreCount == 0) {
      num_si--;
      submit++;
      if (si[ZINK_SUBMIT_WAIT_FD].waitSemaphoreCount == 0) {
         num_si--;
         submit++;
      }
   }

   /* then the real submit */
   si[ZINK_SUBMIT_CMDBUF].waitSemaphoreCount = util_dynarray_num_elements(&bs->wait_semaphores, VkSemaphore);
   si[ZINK_SUBMIT_CMDBUF].pWaitSemaphores = bs->wait_semaphores.data;
   si[ZINK_SUBMIT_CMDBUF].pWaitDstStageMask = bs->wait_semaphore_stages.data;
   VkTimelineSemaphoreSubmitInfo sem_submit = {
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      NULL,
      si[ZINK_SUBMIT_CMDBUF].waitSemaphoreCount,
      bs->wait_semaphore_values.data
   };
   if (si[ZINK_SUBMIT_CMDBUF].waitSemaphoreCount)
      si[ZINK_SUBMIT_CMDBUF].pNext = &sem_submit;
   VkCommandBuffer cmdbufs[3];
   unsigned c = 0;
   if (bs->has_unsync)
      cmdbufs[c++] = bs->unsynchronized_cmdbuf;
   if (bs->has_reordered_work)
      cmdbufs[c++] = bs->reordered_cmdbuf;
   if (bs->has_work)
      cmdbufs[c++] = bs->cmdbuf;
   si[ZINK_SUBMIT_CMDBUF].pCommandBuffers = cmdbufs;
   si[ZINK_SUBMIT_CMDBUF].commandBufferCount = c;
   /* assorted signal submit from wsi/externals */
   si[ZINK_SUBMIT_CMDBUF].signalSemaphoreCount = util_dynarray_num_elements(&bs->signal_semaphores, VkSemaphore);
   si[ZINK_SUBMIT_CMDBUF].pSignalSemaphores = bs->signal_semaphores.data;

   /* then the signal submit with the timeline (fence) semaphore */
   VkSemaphore signals[ZINK_MAX_SIGNALS];
   si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount = !!bs->signal_semaphore;
   signals[0] = bs->signal_semaphore;
   si[ZINK_SUBMIT_SIGNAL_INTERNAL].pSignalSemaphores = signals;
   VkTimelineSemaphoreSubmitInfo tsi = {0};
   uint64_t signal_values[ZINK_MAX_SIGNALS] = {0};
   tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
   si[ZINK_SUBMIT_SIGNAL_INTERNAL].pNext = &tsi;
   tsi.pSignalSemaphoreValues = signal_values;
   signal_values[si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount] = batch_id;
   signals[si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount++] = screen->sem;
   tsi.signalSemaphoreValueCount = si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount;

   if (bs->present)
      signals[si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount++] = bs->present;
   tsi.signalSemaphoreValueCount = si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount;

   assert(si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount <= ZINK_MAX_SIGNALS);
   assert(tsi.signalSemaphoreValueCount <= ZINK_MAX_SIGNALS);

   si[ZINK_SUBMIT_SIGNAL_USER].signalSemaphoreCount = util_dynarray_num_elements(&bs->user_signal_semaphores, VkSemaphore);
   si[ZINK_SUBMIT_SIGNAL_USER].pSignalSemaphores = bs->user_signal_semaphores.data;
   VkTimelineSemaphoreSubmitInfo user_sem_submit = {
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      NULL,
      0,
      NULL,
      si[ZINK_SUBMIT_SIGNAL_USER].signalSemaphoreCount,
      bs->user_signal_semaphore_values.data
   };
   if (si[ZINK_SUBMIT_SIGNAL_USER].signalSemaphoreCount) {
      si[ZINK_SUBMIT_SIGNAL_USER].pNext = &user_sem_submit;
   } else {
      num_si--;
      if (!si[ZINK_SUBMIT_SIGNAL_INTERNAL].signalSemaphoreCount)
         num_si--;
   }

   VkResult result;
   if (bs->has_work) {
      VRAM_ALLOC_LOOP(result,
         VKSCR(EndCommandBuffer)(bs->cmdbuf),
         if (result != VK_SUCCESS) {
            mesa_loge("ZINK: vkEndCommandBuffer failed (%s)", vk_Result_to_str(result));
            bs->is_device_lost = true;
            goto end;
         }
      );
   }
   if (bs->has_reordered_work) {
      if (bs->unordered_write_access) {
         VkMemoryBarrier mb;
         mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
         mb.pNext = NULL;
         mb.srcAccessMask = bs->unordered_write_access;
         mb.dstAccessMask = VK_ACCESS_NONE;
         VKSCR(CmdPipelineBarrier)(bs->reordered_cmdbuf,
                                   bs->unordered_write_stages,
                                   screen->info.have_KHR_synchronization2 ? VK_PIPELINE_STAGE_NONE : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                   0, 1, &mb, 0, NULL, 0, NULL);
      }
      VRAM_ALLOC_LOOP(result,
         VKSCR(EndCommandBuffer)(bs->reordered_cmdbuf),
         if (result != VK_SUCCESS) {
            mesa_loge("ZINK: vkEndCommandBuffer failed (%s)", vk_Result_to_str(result));
            bs->is_device_lost = true;
            goto end;
         }
      );
   }
   if (bs->has_unsync) {
      VRAM_ALLOC_LOOP(result,
         VKSCR(EndCommandBuffer)(bs->unsynchronized_cmdbuf),
         if (result != VK_SUCCESS) {
            mesa_loge("ZINK: vkEndCommandBuffer failed (%s)", vk_Result_to_str(result));
            bs->is_device_lost = true;
            goto end;
         }
      );
   }

   simple_mtx_lock(screen->queue_lock);
   VRAM_ALLOC_LOOP(result,
      VKSCR(QueueSubmit)(screen->queue, num_si, submit, VK_NULL_HANDLE),
      if (result != VK_SUCCESS) {
         mesa_loge("ZINK: vkQueueSubmit failed (%s)", vk_Result_to_str(result));
         bs->is_device_lost = true;
      }
   );
   simple_mtx_unlock(screen->queue_lock);

   unsigned i = 0;
   VkSemaphore *sem = bs->signal_semaphores.data;
   set_foreach(&bs->dmabuf_exports, entry) {
      struct zink_resource *res = (void*)entry->key;
      for (; res; res = zink_resource(res->base.b.next))
         zink_screen_import_dmabuf_semaphore(screen, res, sem[i++]);

      struct pipe_resource *pres = (void*)entry->key;
      pipe_resource_reference(&pres, NULL);
   }
   _mesa_set_clear(&bs->dmabuf_exports, NULL);

   if (bs->sparse_semaphore)
      (void)util_dynarray_pop(&bs->acquires, VkSemaphore);

   bs->usage.submit_count++;
end:
   cnd_broadcast(&bs->usage.flush);

   post_submit(bs, screen);

   p_atomic_set(&bs->fence.submitted, true);

   simple_mtx_lock(&screen->active_batch_states_lock);
   simple_mtx_lock(&screen->free_batch_states_lock);
   for (struct zink_batch_state *i = screen->active_batch_states, *j = i ? i->next : NULL; i; i = j, j = j ? j->next : NULL) {
      reset_batch_state_internal(screen, i);
      zink_batch_state_append(&screen->free_batch_states, &screen->last_free_batch_state, i);
   }
   screen->active_batch_states = NULL;
   screen->last_active_batch_state = NULL;
   simple_mtx_unlock(&screen->free_batch_states_lock);
   simple_mtx_unlock(&screen->active_batch_states_lock);
}

/* called during flush */
void
zink_end_batch(struct zink_context *ctx)
{
   MESA_TRACE_FUNC();
   if (!ctx->queries_disabled)
      zink_suspend_queries(ctx);


   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (ctx->tc && !ctx->track_renderpasses)
      tc_driver_internal_flush_notify(ctx->tc);
   struct zink_batch_state *bs;

   bs = ctx->bs;
   zink_batch_state_append(&ctx->batch_states, &ctx->last_batch_state, bs);
   ctx->batch_states_count++;
   ctx->work_count = 0;

   /* this is swapchain presentation semaphore handling */
   if (ctx->swapchain) {
      if (zink_kopper_acquired(ctx->swapchain->obj->dt, ctx->swapchain->obj->dt_idx) && !ctx->swapchain->obj->present) {
         bs->present = zink_kopper_present(screen, ctx->swapchain);
         bs->swapchain = ctx->swapchain;
      }
      ctx->swapchain = NULL;
   }

   if (screen->device_lost)
      return;

   if (ctx->tc) {
      set_foreach(&bs->active_queries, entry)
         zink_query_sync(ctx, (void*)entry->key);
   }

   set_foreach(&bs->dmabuf_exports, entry) {
      struct zink_resource *res = (void*)entry->key;
      if (screen->info.have_KHR_synchronization2) {
         VkImageMemoryBarrier2 imb;
         zink_resource_image_barrier2_init(&imb, res, res->layout, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
         imb.srcQueueFamilyIndex = screen->gfx_queue;
         imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
         VkDependencyInfo dep = {
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            NULL,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &imb
         };
         VKCTX(CmdPipelineBarrier2)(bs->cmdbuf, &dep);
      } else {
         VkImageMemoryBarrier imb;
         zink_resource_image_barrier_init(&imb, res, res->layout, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
         imb.srcQueueFamilyIndex = screen->gfx_queue;
         imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
         VKCTX(CmdPipelineBarrier)(
            bs->cmdbuf,
            res->obj->access_stage,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,
            0, NULL,
            0, NULL,
            1, &imb
         );
      }
      res->queue = VK_QUEUE_FAMILY_FOREIGN_EXT;

      /* We just transitioned to VK_QUEUE_FAMILY_FOREIGN_EXT.  We'll need a
       * barrier to transition back to our queue before we can use this
       * resource again.  Set need_barriers if bound.
       */
      for (unsigned i = 0; i < ARRAY_SIZE(ctx->need_barriers); i++) {
         if (res->bind_count[i])
            _mesa_set_add(ctx->need_barriers[i], res);
      }

      for (; res; res = zink_resource(res->base.b.next)) {
         VkSemaphore sem = zink_create_exportable_semaphore(screen);
         if (sem) {
            util_dynarray_append(&ctx->bs->signal_semaphores, sem);
         }
      }
      bs->has_work = true;
   }

   util_dynarray_foreach(&bs->fences, struct zink_tc_fence*, mfence)
      (*mfence)->deferred_ctx = NULL;

   if (screen->threaded_submit) {
      util_queue_add_job(&screen->flush_queue, bs, &bs->flush_completed,
                         submit_queue, NULL, 0);
   } else {
      submit_queue(bs, NULL, 0);
   }

   if (!(ctx->flags & ZINK_CONTEXT_COPY_ONLY) && screen->renderdoc_capturing && !screen->renderdoc_capture_all &&
       p_atomic_read(&screen->renderdoc_frame) > screen->renderdoc_capture_end) {
      screen->renderdoc_api->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(screen->instance), NULL);
      screen->renderdoc_capturing = false;
   }
}

static int
batch_find_resource(struct zink_batch_state *bs, struct zink_resource_object *obj, struct zink_batch_obj_list *list)
{
   unsigned hash = obj->bo->unique_id & (BUFFER_HASHLIST_SIZE-1);
   int buffer_index = bs->buffer_indices_hashlist[hash];

   /* not found or found */
   if (buffer_index < 0 || (buffer_index < list->num_buffers && list->objs[buffer_index] == obj))
      return buffer_index;

   /* Hash collision, look for the BO in the list of list->objs linearly. */
   for (int i = list->num_buffers - 1; i >= 0; i--) {
      if (list->objs[i] == obj) {
         /* Put this buffer in the hash list.
          * This will prevent additional hash collisions if there are
          * several consecutive lookup_buffer calls for the same buffer.
          *
          * Example: Assuming list->objs A,B,C collide in the hash list,
          * the following sequence of list->objs:
          *         AAAAAAAAAAABBBBBBBBBBBBBBCCCCCCCC
          * will collide here: ^ and here:   ^,
          * meaning that we should get very few collisions in the end. */
         bs->buffer_indices_hashlist[hash] = i & (BUFFER_HASHLIST_SIZE-1);
         return i;
      }
   }
   return -1;
}

void
zink_batch_reference_resource_rw(struct zink_context *ctx, struct zink_resource *res, bool write)
{
   /* if the resource already has usage of any sort set for this batch, */
   if (!zink_resource_usage_matches(res, ctx->bs) ||
       /* or if it's bound somewhere */
       !zink_resource_has_binds(res))
      /* then it already has a batch ref and doesn't need one here */
      zink_batch_reference_resource(ctx, res);
   zink_batch_resource_usage_set(ctx->bs, res, write, res->obj->is_buffer);
}

static bool
batch_ptr_add_usage(struct zink_context *ctx, struct set *s, void *ptr)
{
   bool found = false;
   _mesa_set_search_or_add(s, ptr, &found);
   return !found;
}

/* this is a vague, handwave-y estimate */
ALWAYS_INLINE static void
check_oom_flush(struct zink_context *ctx)
{
   const VkDeviceSize resource_size = ctx->bs->resource_size;
   if (resource_size >= zink_screen(ctx->base.screen)->clamp_video_mem) {
       ctx->oom_flush = true;
       ctx->oom_stall = true;
    }
}

/* this adds a ref (batch tracking) */
void
zink_batch_reference_resource(struct zink_context *ctx, struct zink_resource *res)
{
   if (!zink_batch_reference_resource_move(ctx, res))
      zink_resource_object_reference(NULL, NULL, res->obj);
}

/* this adds batch usage */
ALWAYS_INLINE static bool
batch_reference_resource_move_internal(struct zink_batch_state *bs, struct zink_batch_obj_list *list, struct zink_resource *res)
{
   struct zink_bo *bo = res->obj->bo;
   int idx = batch_find_resource(bs, res->obj, list);
   if (idx >= 0) {
      return true;
   }

   if (list->num_buffers >= list->max_buffers) {
      unsigned new_max = MAX2(list->max_buffers + 16, (unsigned)(list->max_buffers * 1.3));
      struct zink_resource_object **objs = realloc(list->objs, new_max * sizeof(void*));
      if (!objs) {
         /* things are about to go dramatically wrong anyway */
         mesa_loge("zink: buffer list realloc failed due to oom!\n");
         abort();
      }
      list->objs = objs;
      list->max_buffers = new_max;
   }
   idx = list->num_buffers++;
   list->objs[idx] = res->obj;
   unsigned hash = bo->unique_id & (BUFFER_HASHLIST_SIZE-1);
   bs->buffer_indices_hashlist[hash] = idx & 0x7fff;
   bs->last_added_obj = res->obj;
   if (!(res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE)) {
      bs->resource_size += res->obj->size;
   } else {
      /* Sparse backing pages are not directly referenced by the batch as
       * there can be a lot of them.
       * Instead, they are kept referenced in one of two ways:
       * - While they are committed, they are directly referenced from the
       *   resource's state.
       * - Upon de-commit, they are added to the freed_sparse_backing_bos
       *   list, which will defer destroying the resource until the batch
       *   performing unbind finishes.
       */
   }
   check_oom_flush(bs->ctx);
   return false;
}

bool
zink_batch_reference_resource_move(struct zink_context *ctx, struct zink_resource *res)
{
   MESA_TRACE_FUNC();
   struct zink_batch_state *bs = ctx->bs;

   /* swapchains are special */
   if (zink_is_swapchain(res)) {
      struct zink_resource_object **swapchains = bs->swapchain_obj.data;
      unsigned count = util_dynarray_num_elements(&bs->swapchain_obj, struct zink_resource_object*);
      for (unsigned i = 0; i < count; i++) {
         if (swapchains[i] == res->obj) {
            return true;
         }
      }
      util_dynarray_append(&bs->swapchain_obj, res->obj);
      return false;
   }
   /* Fast exit for no-op calls.
    * This is very effective with suballocators and linear uploaders that
    * are outside of the winsys.
    */
   if (res->obj == bs->last_added_obj) {
      return true;
   }

   struct zink_bo *bo = res->obj->bo;
   struct zink_batch_obj_list *list;
   if (!(res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE)) {
      if (!bo->mem) {
         list = &bs->slab_objs;
      } else {
         list = &bs->real_objs;
      }
   } else {
      list = &bs->sparse_objs;
   }
   return batch_reference_resource_move_internal(bs, list, res);
}

bool
zink_batch_reference_resource_move_unsync(struct zink_context *ctx, struct zink_resource *res)
{
   struct zink_batch_state *bs = ctx->bs;

   /* swapchains are special */
   if (zink_is_swapchain(res)) {
      struct zink_resource_object **swapchains = bs->swapchain_obj_unsync.data;
      unsigned count = util_dynarray_num_elements(&bs->swapchain_obj_unsync, struct zink_resource_object*);
      for (unsigned i = 0; i < count; i++) {
         if (swapchains[i] == res->obj) {
            return true;
         }
      }
      util_dynarray_append(&bs->swapchain_obj_unsync, res->obj);
      return false;
   }

   /* unsync is not as common, skip LRU */
   return batch_reference_resource_move_internal(bs, &bs->unsync_objs, res);
}

/* this is how programs achieve deferred deletion */
void
zink_batch_reference_program(struct zink_context *ctx,
                             struct zink_program *pg)
{
   struct zink_batch_state *bs = ctx->bs;
   if (zink_batch_usage_matches(pg->batch_uses, bs) ||
       !batch_ptr_add_usage(ctx, &bs->programs, pg))
      return;
   pipe_reference(NULL, &pg->reference);
   zink_batch_usage_set(&pg->batch_uses, bs);
   bs->has_work = true;
}

/* a fast (hopefully) way to check whether a given batch has completed */
bool
zink_screen_usage_check_completion(struct zink_screen *screen, const struct zink_batch_usage *u)
{
   if (!zink_batch_usage_exists(u))
      return true;
   if (zink_batch_usage_is_unflushed(u))
      return false;

   return zink_screen_timeline_wait(screen, u->usage, 0);
}

/* an even faster check that doesn't ioctl */
bool
zink_screen_usage_check_completion_fast(struct zink_screen *screen, const struct zink_batch_usage *u)
{
   if (!zink_batch_usage_exists(u))
      return true;
   if (zink_batch_usage_is_unflushed(u))
      return false;

   return zink_screen_check_last_finished(screen, u->usage);
}

bool
zink_batch_usage_check_completion(struct zink_context *ctx, const struct zink_batch_usage *u)
{
   if (!zink_batch_usage_exists(u))
      return true;
   if (zink_batch_usage_is_unflushed(u))
      return false;
   return zink_check_batch_completion(ctx, u->usage);
}

bool
zink_batch_usage_unflushed_wait(struct zink_context *ctx, struct zink_batch_usage *u, unsigned submit_count, bool trywait)
{
   MESA_TRACE_FUNC();
   if (!zink_batch_usage_exists(u))
      return true;
   /* this batch state was already completed and reset */
   if (u->submit_count - submit_count > 1)
      return true;
   if (zink_batch_usage_is_unflushed(u)) {
      if (likely(u == &ctx->bs->usage)) {
         ctx->base.flush(&ctx->base, NULL, PIPE_FLUSH_HINT_FINISH);
         return true;
      } else { //multi-context
         mtx_lock(&u->mtx);
         if (trywait) {
            struct timespec ts = {0, 10000};
            cnd_timedwait(&u->flush, &u->mtx, &ts);
         } else
            cnd_wait(&u->flush, &u->mtx);
         mtx_unlock(&u->mtx);
      }
   }
   return !u->unflushed;
}

static void
batch_usage_wait(struct zink_context *ctx, struct zink_batch_usage *u, unsigned submit_count, bool trywait)
{
   MESA_TRACE_FUNC();
   if (!zink_batch_usage_exists(u))
      return;
   /* this batch state was already completed and reset */
   if (u->submit_count - submit_count > 1)
      return;
   if (zink_batch_usage_unflushed_wait(ctx, u, submit_count, trywait))
      zink_wait_on_batch(ctx, u->usage);
}

void
zink_batch_usage_wait(struct zink_context *ctx, struct zink_batch_usage *u, unsigned submit_count)
{
   batch_usage_wait(ctx, u, submit_count, false);
}

void
zink_batch_usage_try_wait(struct zink_context *ctx, struct zink_batch_usage *u, unsigned submit_count)
{
   batch_usage_wait(ctx, u, submit_count, true);
}
