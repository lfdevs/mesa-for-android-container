/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#include "util/u_memory.h"
#include "swr_context.h"
#include "swr_scratch.h"
#include "api.h"


void *
swr_copy_to_scratch_space(struct swr_context *ctx,
                          struct swr_scratch_space *space,
                          const void *user_buffer,
                          unsigned int size)
{
   void *ptr;
   assert(space);
   assert(user_buffer);
   assert(size);

   if (size >= 2048) { /* XXX TODO create KNOB_ for this */
      /* Use per draw SwrAllocDrawContextMemory for larger copies */
      ptr = SwrAllocDrawContextMemory(ctx->swrContext, size, 4);
   } else {
      /* Allocate enough so that MAX_DRAWS_IN_FLIGHT sets fit. */
      unsigned int max_size_in_flight = size * KNOB_MAX_DRAWS_IN_FLIGHT;

      /* Need to grow space */
      if (max_size_in_flight > space->current_size) {
         /* Must idle the pipeline, this is infrequent */
         SwrWaitForIdle(ctx->swrContext);

         space->current_size = max_size_in_flight;

         if (space->base) {
            align_free(space->base);
            space->base = NULL;
         }

         if (!space->base) {
            space->base = (uint8_t *)align_malloc(space->current_size, 4);
            space->head = (void *)space->base;
         }
      }

      /* Wrap */
      if (((uint8_t *)space->head + size)
          >= ((uint8_t *)space->base + space->current_size)) {
         /*
          * TODO XXX: Should add a fence on wrap.  Assumption is that
          * current_space >> size, and there are at least MAX_DRAWS_IN_FLIGHT
          * draws in scratch.  So fence would always be met on wrap.  A fence
          * would ensure that first frame in buffer is done before wrapping.
          * If fence ever needs to be waited on, can increase buffer size.
          * So far in testing, this hasn't been necessary.
          */
         space->head = space->base;
      }

      ptr = space->head;
      space->head = (uint8_t *)space->head + size;
   }

   /* Copy user_buffer to scratch */
   memcpy(ptr, user_buffer, size);

   return ptr;
}


void
swr_init_scratch_buffers(struct swr_context *ctx)
{
   struct swr_scratch_buffers *scratch;

   scratch = CALLOC_STRUCT(swr_scratch_buffers);
   ctx->scratch = scratch;
}

void
swr_destroy_scratch_buffers(struct swr_context *ctx)
{
   struct swr_scratch_buffers *scratch = ctx->scratch;

   if (scratch) {
      if (scratch->vs_constants.base)
         align_free(scratch->vs_constants.base);
      if (scratch->fs_constants.base)
         align_free(scratch->fs_constants.base);
      if (scratch->vertex_buffer.base)
         align_free(scratch->vertex_buffer.base);
      if (scratch->index_buffer.base)
         align_free(scratch->index_buffer.base);
      FREE(scratch);
   }
}
