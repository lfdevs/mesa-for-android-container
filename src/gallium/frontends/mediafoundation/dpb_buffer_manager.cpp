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

#include "dpb_buffer_manager.h"
#include "frontend/winsys_handle.h"
#include "gallium/drivers/d3d12/d3d12_interop_public.h"
#include "vl/vl_video_buffer.h"

#include "wpptrace.h"

#include "dpb_buffer_manager.tmh"



// retrieve a buffer from the pool
struct pipe_video_buffer *
dpb_buffer_manager::get_fresh_dpb_buffer()
{
   for( auto &entry : m_pool )
   {
      if( !entry.used )
      {
         entry.used = true;
         return entry.buffer;
      }
   }

   MFE_ERROR( "[dx12 hmft 0x%p] failed to find a free buffer", m_logId );
   assert( false );   // Did not find an unused buffer
   return NULL;
}

// release a buffer back to the pool
void
dpb_buffer_manager::release_dpb_buffer( struct pipe_video_buffer *target )
{
   for( auto &entry : m_pool )
   {
      if( entry.buffer == target )
      {
         entry.used = false;
         break;
      }
   }
}

dpb_buffer_manager::dpb_buffer_manager( void *logId,
                                        struct pipe_video_codec *codec,
                                        unsigned width,
                                        unsigned height,
                                        enum pipe_format buffer_format,
                                        unsigned pool_size,
                                        HRESULT &hr )
   : m_logId( logId ), m_codec( codec ), m_pool( pool_size, { NULL, false } )
{
   m_template.width = width;
   m_template.height = height;
   m_template.buffer_format = buffer_format;

   if( codec->context->screen->get_video_param( codec->context->screen,
                                                codec->profile,
                                                PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                PIPE_VIDEO_CAP_ENC_READABLE_RECONSTRUCTED_PICTURE ) != 0 )
   {
      m_template.bind = PIPE_BIND_SHARED;   // Indicate we want shared resource capabilities
   }

   for( auto &entry : m_pool )
   {
      entry.buffer = m_codec->create_dpb_buffer( m_codec, NULL, &m_template );
      if( !entry.buffer )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] create_dpb_buffer failed", m_logId );
         assert( false );
         hr = E_FAIL;
         break;
      }
   }

   if( FAILED( hr ) )
   {
      for( auto &entry : m_pool )
      {
         if( entry.buffer )
         {
            entry.buffer->destroy( entry.buffer );
         }
      }
   }
}

dpb_buffer_manager::~dpb_buffer_manager()
{
   for( auto &entry : m_pool )
   {
      if( entry.buffer )
      {
         entry.buffer->destroy( entry.buffer );
      }
   }
}
