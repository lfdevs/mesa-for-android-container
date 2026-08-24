/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vl_proc.h"
#include "vl_compositor_proc.h"
#include "util/u_dynarray.h"

struct vl_proc {
   struct pipe_video_codec b;
   struct pipe_video_codec *main;
   struct pipe_video_codec *fallback;
   struct util_dynarray frames;
};

struct vl_proc_frame {
   struct pipe_video_buffer *src;
   struct pipe_vpp_desc vpp;
};

static void
proc_destroy(struct pipe_video_codec *codec)
{
   struct vl_proc *proc = (struct vl_proc *)codec;

   util_dynarray_fini(&proc->frames);
   proc->main->destroy(proc->main);
   if (proc->fallback)
      proc->fallback->destroy(proc->fallback);
   free(proc);
}

static void
proc_begin_frame(struct pipe_video_codec *codec,
                 struct pipe_video_buffer *target,
                 struct pipe_picture_desc *picture)
{
   struct vl_proc *proc = (struct vl_proc *)codec;

   util_dynarray_clear(&proc->frames);
}

static int
proc_process_frame(struct pipe_video_codec *codec,
                   struct pipe_video_buffer *src,
                   const struct pipe_vpp_desc *process_properties)
{
   struct vl_proc *proc = (struct vl_proc *)codec;

   struct vl_proc_frame frame = {
      .src = src,
      .vpp = *process_properties,
   };
   util_dynarray_append(&proc->frames, frame);

   return 0;
}

static int
proc_end_frame(struct pipe_video_codec *codec,
               struct pipe_video_buffer *target,
               struct pipe_picture_desc *picture)
{
   struct vl_proc *proc = (struct vl_proc *)codec;
   struct pipe_video_codec *current = proc->main;
   int ret = 0;

   if (!proc->frames.size)
      return ret;

   current->begin_frame(current, target, picture);

   util_dynarray_foreach(&proc->frames, struct vl_proc_frame, frame) {
      if (current != proc->main) {
         ret = current->end_frame(current, target, picture);
         if (ret != 0)
            return ret;
         current = proc->main;
         current->begin_frame(current, target, picture);
      }
      ret = current->process_frame(current, frame->src, &frame->vpp);
      if (ret != 0 && proc->fallback) {
         ret = current->end_frame(current, target, picture);
         if (ret != 0)
            return ret;
         current = proc->fallback;
         current->begin_frame(current, target, picture);
         ret = current->process_frame(current, frame->src, &frame->vpp);
      }
      if (ret != 0)
         goto out;
   }

out:
   current->end_frame(current, target, picture);
   return ret;
}

static int
proc_fence_wait(struct pipe_video_codec *codec,
                struct pipe_fence_handle *fence,
                uint64_t timeout)
{
   struct vl_proc *proc = (struct vl_proc *)codec;

   return proc->main->fence_wait(proc->main, fence, timeout);
}

static void
proc_destroy_fence(struct pipe_video_codec *codec,
                   struct pipe_fence_handle *fence)
{
   struct vl_proc *proc = (struct vl_proc *)codec;

   proc->main->destroy_fence(proc->main, fence);
}

struct pipe_video_codec *
vl_create_proc(struct pipe_context *context, struct pipe_video_codec *templat)
{
   struct vl_proc *proc = calloc(1, sizeof(*proc));
   if (!proc)
      return NULL;

   if (context->screen->get_video_param(context->screen,
                                        PIPE_VIDEO_PROFILE_UNKNOWN,
                                        PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                        PIPE_VIDEO_CAP_SUPPORTED)) {
      proc->main = context->create_video_codec(context, templat);
      if (!proc->main)
         goto error;
   }

#if HAVE_GFX_COMPUTE
   if (context->screen->caps.graphics || context->screen->caps.compute) {
      bool compute_only = context->screen->caps.prefer_compute_for_multimedia;
      struct pipe_video_codec *comp = vl_compositor_create_proc(context, compute_only);
      if (comp) {
         if (proc->main)
            proc->fallback = comp;
         else
            proc->main = comp;
      }
   }
#endif

   if (!proc->main)
      goto error;

   proc->b = *templat;
   proc->b.context = context;
   proc->b.destroy = proc_destroy;
   proc->b.begin_frame = proc_begin_frame;
   proc->b.process_frame = proc_process_frame;
   proc->b.end_frame = proc_end_frame;
   if (proc->main->fence_wait)
      proc->b.fence_wait = proc_fence_wait;
   if (proc->main->destroy_fence)
      proc->b.destroy_fence = proc_destroy_fence;
   return &proc->b;

error:
   free(proc);
   return NULL;
}
