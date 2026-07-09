/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_command_queue.h"

#include <Metal/MTLDevice.h>
#include <Metal/MTLCommandQueue.h>

/* MTL4CommitOptions */
mtl_commit_options *
mtl_new_commit_options(void)
{
   @autoreleasepool {
      return [[MTL4CommitOptions new] init];
   }
}

enum mtl_command_queue_error
error_code_to_mtl_command_queue_error(MTL4CommandQueueError code)
{
   switch (code) {
   case MTL4CommandQueueErrorNone:
      return MTL_COMMAND_QUEUE_ERROR_NONE;
    case MTL4CommandQueueErrorTimeout:
      return MTL_COMMAND_QUEUE_ERROR_TIMEOUT;
    case MTL4CommandQueueErrorNotPermitted:
      return MTL_COMMAND_QUEUE_ERROR_NOT_PERMITTED;
    case MTL4CommandQueueErrorOutOfMemory:
      return MTL_COMMAND_QUEUE_ERROR_OUT_OF_MEMORY;
    case MTL4CommandQueueErrorDeviceRemoved:
      return MTL_COMMAND_QUEUE_ERROR_DEVICE_REMOVED;
    case MTL4CommandQueueErrorAccessRevoked:
      return MTL_COMMAND_QUEUE_ERROR_ACCESS_REVOKED;
    case MTL4CommandQueueErrorInternal:
      return MTL_COMMAND_QUEUE_ERROR_INTERNAL;
   }
}

void
mtl_commit_options_add_feedback_handler(mtl_commit_options *options,
                                        mtl_feedback_handler_callback callback,
                                        void *data)
{
   @autoreleasepool {
      MTL4CommitOptions* opt = (MTL4CommitOptions *)options;
      [opt addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
         @autoreleasepool {
            NSError *error = feedback.error;
            struct mtl_feedback_data feedback_data = (struct mtl_feedback_data){
               .user_data = data,
               .error_message =
                  error ? error.localizedDescription.UTF8String : NULL,
               .gpu_start = feedback.GPUStartTime,
               .gpu_end = feedback.GPUEndTime,
               .error = error
                           ? error_code_to_mtl_command_queue_error(error.code)
                           : MTL_COMMAND_QUEUE_ERROR_NONE,
            };
            callback(&feedback_data);
         }
      }];
   }
}

/* MTL4CommandQueue */
mtl_command_queue *
mtl_new_command_queue(mtl_device *device)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev newMTL4CommandQueue];
   }
}

void
mtl_command_queue_add_residency_set(mtl_command_queue *cmd_queue,
                                    mtl_residency_set *set)
{
   @autoreleasepool {
      id<MTL4CommandQueue> queue = (id<MTL4CommandQueue>)cmd_queue;
      id<MTLResidencySet> s = (id<MTLResidencySet>)set;
      return [queue addResidencySet:s];
   }
}

void
mtl_command_queue_remove_residency_set(mtl_command_queue *cmd_queue,
                                       mtl_residency_set *set)
{
   @autoreleasepool {
      id<MTL4CommandQueue> queue = (id<MTL4CommandQueue>)cmd_queue;
      id<MTLResidencySet> s = (id<MTLResidencySet>)set;
      return [queue removeResidencySet:s];
   }
}

void
mtl_signal_event(mtl_command_queue *queue, mtl_event *event, uint64_t value)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTLEvent> e = (id<MTLEvent>)event;
      [q signalEvent:e value:value];
   }
}

void
mtl_wait_for_event(mtl_command_queue *queue, mtl_event *event, uint64_t value)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTLEvent> e = (id<MTLEvent>)event;
      [q waitForEvent:e value:value];
   }
}

void
mtl_command_queue_commit(mtl_command_queue *queue,
                         mtl_command_buffer **command_buffers, uint32_t count,
                         mtl_commit_options *options)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTL4CommandBuffer> *cmds = (id<MTL4CommandBuffer> *)command_buffers;
      MTL4CommitOptions *opt = (MTL4CommitOptions *)options;
      [q commit:cmds count:count options:opt];
   }
}

void
mtl_command_queue_signal_drawable(mtl_command_queue *queue, void *drawable)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTLDrawable> d = (id<MTLDrawable>)drawable;
      [q signalDrawable:d];
   }
}

void
mtl_drawable_present(void *drawable)
{
   @autoreleasepool {
      id<MTLDrawable> d = (id<MTLDrawable>)drawable;
      [d present];
   }
}
