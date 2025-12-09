/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <vector>
#include "hmft_entrypoints.h"

class dpb_buffer_manager
{
 public:
   dpb_buffer_manager( void *logId,
                       struct pipe_video_codec *codec,
                       unsigned width,
                       unsigned height,
                       enum pipe_format buffer_format,
                       unsigned pool_size,
                       HRESULT &hr );
   ~dpb_buffer_manager();

   // retrieve a buffer from the pool
   struct pipe_video_buffer *get_fresh_dpb_buffer();

   // release a buffer back to the pool
   void release_dpb_buffer( struct pipe_video_buffer *target );


 private:
   const void *m_logId = {};
   struct pipe_video_codec *m_codec = NULL;
   struct pipe_video_buffer m_template = {};

   struct dpb_buffer_manager_pool_entry
   {
      struct pipe_video_buffer *buffer;
      bool used;
   };

   std::vector<struct dpb_buffer_manager_pool_entry> m_pool;
};
