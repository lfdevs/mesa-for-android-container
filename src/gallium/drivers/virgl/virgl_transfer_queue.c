/*
 * Copyright 2018 Chromium.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "util/u_box.h"
#include "util/u_inlines.h"

#include "virgl_protocol.h"
#include "virgl_screen.h"
#include "virgl_encode.h"
#include "virgl_transfer_queue.h"

struct list_action_args
{
   void *data;
   struct virgl_transfer *queued;
   struct virgl_transfer *current;
};

typedef bool (*compare_transfers_t)(struct virgl_transfer *queued,
                                    struct virgl_transfer *current);

typedef void (*list_action_t)(struct virgl_transfer_queue *queue,
                              struct list_action_args *args);

struct list_iteration_args
{
   void *data;
   list_action_t action;
   compare_transfers_t compare;
   struct virgl_transfer *current;
   enum virgl_transfer_queue_lists type;
};

static bool transfers_intersect(struct virgl_transfer *queued,
                                struct virgl_transfer *current)
{
   boolean tmp;
   struct pipe_resource *queued_res = queued->base.resource;
   struct pipe_resource *current_res = current->base.resource;

   if (queued_res != current_res)
      return false;

   tmp = u_box_test_intersection_2d(&queued->base.box, &current->base.box);
   return (tmp == TRUE);
}

static bool transfers_overlap(struct virgl_transfer *queued,
                              struct virgl_transfer *current)
{
   boolean tmp;
   struct pipe_resource *queued_res = queued->base.resource;
   struct pipe_resource *current_res = current->base.resource;

   if (queued_res != current_res)
      return false;

   if (queued->base.level != current->base.level)
      return false;

   if (queued->base.box.z != current->base.box.z)
      return true;

   if (queued->base.box.depth != 1 || current->base.box.depth != 1)
      return true;

   /*
    * Special case for boxes with [x: 0, width: 1] and [x: 1, width: 1].
    */
   if (queued_res->target == PIPE_BUFFER) {
      if (queued->base.box.x + queued->base.box.width == current->base.box.x)
         return false;

      if (current->base.box.x + current->base.box.width == queued->base.box.x)
         return false;
   }

   tmp = u_box_test_intersection_2d(&queued->base.box, &current->base.box);
   return (tmp == TRUE);
}

static void set_true(UNUSED struct virgl_transfer_queue *queue,
                     struct list_action_args *args)
{
   bool *val = args->data;
   *val = true;
}

static void set_queued(UNUSED struct virgl_transfer_queue *queue,
                       struct list_action_args *args)
{
   struct virgl_transfer *queued = args->queued;
   struct virgl_transfer **val = args->data;
   *val = queued;
}

static void remove_transfer(struct virgl_transfer_queue *queue,
                            struct list_action_args *args)
{
   struct virgl_transfer *queued = args->queued;
   struct pipe_resource *pres = queued->base.resource;
   list_del(&queued->queue_link);
   pipe_resource_reference(&pres, NULL);
   virgl_resource_destroy_transfer(queue->pool, queued);
}

static void replace_unmapped_transfer(struct virgl_transfer_queue *queue,
                                      struct list_action_args *args)
{
   struct virgl_transfer *current = args->current;
   struct virgl_transfer *queued = args->queued;

   u_box_union_2d(&current->base.box, &current->base.box, &queued->base.box);
   current->offset = current->base.box.x;

   remove_transfer(queue, args);
   queue->num_dwords -= (VIRGL_TRANSFER3D_SIZE + 1);
}

static void transfer_put(struct virgl_transfer_queue *queue,
                         struct list_action_args *args)
{
   struct virgl_transfer *queued = args->queued;
   struct virgl_resource *res = virgl_resource(queued->base.resource);

   queue->vs->vws->transfer_put(queue->vs->vws, res->hw_res, &queued->base.box,
                                queued->base.stride, queued->l_stride,
                                queued->offset, queued->base.level);

   remove_transfer(queue, args);
}

static void transfer_write(struct virgl_transfer_queue *queue,
                           struct list_action_args *args)
{
   struct virgl_transfer *queued = args->queued;
   struct virgl_cmd_buf *buf = args->data;

   // Takes a reference on the HW resource, which is released after
   // the exec buffer command.
   virgl_encode_transfer(queue->vs, buf, queued, VIRGL_TRANSFER_TO_HOST);

   list_delinit(&queued->queue_link);
   list_addtail(&queued->queue_link, &queue->lists[COMPLETED_LIST]);
}

static void compare_and_perform_action(struct virgl_transfer_queue *queue,
                                       struct list_iteration_args *iter)
{
   struct list_action_args args;
   struct virgl_transfer *queued, *tmp;
   enum virgl_transfer_queue_lists type = iter->type;

   memset(&args, 0, sizeof(args));
   args.current = iter->current;
   args.data = iter->data;

   LIST_FOR_EACH_ENTRY_SAFE(queued, tmp, &queue->lists[type], queue_link) {
      if (iter->compare(queued, iter->current)) {
         args.queued = queued;
         iter->action(queue, &args);
      }
   }
}

static void intersect_and_set_queued_once(struct virgl_transfer_queue *queue,
                                          struct list_iteration_args *iter)
{
   struct list_action_args args;
   struct virgl_transfer *queued, *tmp;
   enum virgl_transfer_queue_lists type = iter->type;

   memset(&args, 0, sizeof(args));
   args.current = iter->current;
   args.data = iter->data;

   LIST_FOR_EACH_ENTRY_SAFE(queued, tmp, &queue->lists[type], queue_link) {
      if (transfers_intersect(queued, iter->current)) {
         args.queued = queued;
         set_queued(queue, &args);
         return;
      }
   }
}

static void perform_action(struct virgl_transfer_queue *queue,
                           struct list_iteration_args *iter)
{
   struct list_action_args args;
   struct virgl_transfer *queued, *tmp;
   enum virgl_transfer_queue_lists type = iter->type;

   memset(&args, 0, sizeof(args));
   args.data = iter->data;

   LIST_FOR_EACH_ENTRY_SAFE(queued, tmp, &queue->lists[type], queue_link) {
      args.queued = queued;
      iter->action(queue, &args);
   }
}

static void add_internal(struct virgl_transfer_queue *queue,
                         struct virgl_transfer *transfer)
{
   uint32_t dwords = VIRGL_TRANSFER3D_SIZE + 1;
   if (queue->tbuf) {
      if (queue->num_dwords + dwords >= VIRGL_MAX_TBUF_DWORDS) {
         struct list_iteration_args iter;
         struct virgl_winsys *vws = queue->vs->vws;

         memset(&iter, 0, sizeof(iter));
         iter.type = PENDING_LIST;
         iter.action = transfer_write;
         iter.data = queue->tbuf;
         perform_action(queue, &iter);

         vws->submit_cmd(vws, queue->tbuf, NULL);
         queue->num_dwords = 0;
      }
   }

   list_addtail(&transfer->queue_link, &queue->lists[PENDING_LIST]);
   queue->num_dwords += dwords;
}


void virgl_transfer_queue_init(struct virgl_transfer_queue *queue,
                               struct virgl_screen *vs,
                               struct slab_child_pool *pool)
{
   queue->vs = vs;
   queue->pool = pool;
   queue->num_dwords = 0;

   for (uint32_t i = 0; i < MAX_LISTS; i++)
      list_inithead(&queue->lists[i]);

   if ((vs->caps.caps.v2.capability_bits & VIRGL_CAP_TRANSFER) &&
        vs->vws->supports_encoded_transfers)
      queue->tbuf = vs->vws->cmd_buf_create(vs->vws, VIRGL_MAX_TBUF_DWORDS);
   else
      queue->tbuf = NULL;
}

void virgl_transfer_queue_fini(struct virgl_transfer_queue *queue)
{
   struct virgl_winsys *vws = queue->vs->vws;
   struct list_iteration_args iter;

   memset(&iter, 0, sizeof(iter));

   iter.action = transfer_put;
   iter.type = PENDING_LIST;
   perform_action(queue, &iter);

   iter.action = remove_transfer;
   iter.type = COMPLETED_LIST;
   perform_action(queue, &iter);

   if (queue->tbuf)
      vws->cmd_buf_destroy(queue->tbuf);

   queue->vs = NULL;
   queue->pool = NULL;
   queue->tbuf = NULL;
   queue->num_dwords = 0;
}

int virgl_transfer_queue_unmap(struct virgl_transfer_queue *queue,
                               struct virgl_transfer *transfer)
{
   struct pipe_resource *res, *pres;
   struct list_iteration_args iter;

   pres = NULL;
   res = transfer->base.resource;
   pipe_resource_reference(&pres, res);

   if (res->target == PIPE_BUFFER) {
      memset(&iter, 0, sizeof(iter));
      iter.current = transfer;
      iter.compare = transfers_intersect;
      iter.action = replace_unmapped_transfer;
      iter.type = PENDING_LIST;
      compare_and_perform_action(queue, &iter);
   }

   add_internal(queue, transfer);
   return 0;
}

int virgl_transfer_queue_clear(struct virgl_transfer_queue *queue,
                               struct virgl_cmd_buf *cbuf)
{
   struct list_iteration_args iter;

   memset(&iter, 0, sizeof(iter));
   iter.type = PENDING_LIST;
   if (queue->tbuf) {
      uint32_t prior_num_dwords = cbuf->cdw;
      cbuf->cdw = 0;

      iter.action = transfer_write;
      iter.data = cbuf;
      perform_action(queue, &iter);

      virgl_encode_end_transfers(cbuf);
      cbuf->cdw = prior_num_dwords;
   } else {
      iter.action = transfer_put;
      perform_action(queue, &iter);
   }

   iter.action = remove_transfer;
   iter.type = COMPLETED_LIST;
   perform_action(queue, &iter);
   queue->num_dwords = 0;

   return 0;
}

bool virgl_transfer_queue_is_queued(struct virgl_transfer_queue *queue,
                                    struct virgl_transfer *transfer)
{
   bool queued = false;
   struct list_iteration_args iter;

   memset(&iter, 0, sizeof(iter));
   iter.current = transfer;
   iter.compare = transfers_overlap;
   iter.action = set_true;
   iter.data = &queued;

   iter.type = PENDING_LIST;
   compare_and_perform_action(queue, &iter);

   iter.type = COMPLETED_LIST;
   compare_and_perform_action(queue, &iter);

   return queued;
}

struct virgl_transfer *
virgl_transfer_queue_extend(struct virgl_transfer_queue *queue,
                            struct virgl_transfer *transfer)
{
   struct virgl_transfer *queued = NULL;
   struct list_iteration_args iter;

   if (transfer->base.resource->target == PIPE_BUFFER) {
      memset(&iter, 0, sizeof(iter));
      iter.current = transfer;
      iter.data = &queued;
      iter.type = PENDING_LIST;
      intersect_and_set_queued_once(queue, &iter);
   }

   if (queued) {
      u_box_union_2d(&queued->base.box, &queued->base.box, &transfer->base.box);
      queued->offset = queued->base.box.x;
   }

   return queued;
}
