/*
 * Copyright © 2015 Intel Corporation
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
 */

#include "brw_context.h"
#include "brw_state.h"
#include "main/formatquery.h"
#include "main/glformats.h"

static size_t
brw_query_samples_for_format(struct gl_context *ctx, GLenum target,
                             GLenum internalFormat, int samples[16])
{
   struct brw_context *brw = brw_context(ctx);

   (void) target;
   (void) internalFormat;

   switch (brw->gen) {
   case 9:
      samples[0] = 16;
      samples[1] = 8;
      samples[2] = 4;
      samples[3] = 2;
      return 4;

   case 8:
      samples[0] = 8;
      samples[1] = 4;
      samples[2] = 2;
      return 3;

   case 7:
      samples[0] = 8;
      samples[1] = 4;
      return 2;

   case 6:
      samples[0] = 4;
      return 1;

   default:
      assert(brw->gen < 6);
      samples[0] = 1;
      return 1;
   }
}

/**
 * Returns a generic GL type from an internal format, so that it can be used
 * together with the base format to obtain a mesa_format by calling
 * mesa_format_from_format_and_type().
 */
static GLenum
get_generic_type_for_internal_format(GLenum internalFormat)
{
   if (_mesa_is_color_format(internalFormat)) {
      if (_mesa_is_enum_format_unsigned_int(internalFormat))
         return GL_UNSIGNED_BYTE;
      else if (_mesa_is_enum_format_signed_int(internalFormat))
         return GL_BYTE;
   } else {
      switch (internalFormat) {
      case GL_STENCIL_INDEX:
      case GL_STENCIL_INDEX8:
         return GL_UNSIGNED_BYTE;
      case GL_DEPTH_COMPONENT:
      case GL_DEPTH_COMPONENT16:
         return GL_UNSIGNED_SHORT;
      case GL_DEPTH_COMPONENT24:
      case GL_DEPTH_COMPONENT32:
         return GL_UNSIGNED_INT;
      case GL_DEPTH_COMPONENT32F:
         return GL_FLOAT;
      case GL_DEPTH_STENCIL:
      case GL_DEPTH24_STENCIL8:
         return GL_UNSIGNED_INT_24_8;
      case GL_DEPTH32F_STENCIL8:
         return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
      default:
         /* fall-through */
         break;
      }
   }

   return GL_FLOAT;
}

void
brw_query_internal_format(struct gl_context *ctx, GLenum target,
                          GLenum internalFormat, GLenum pname, GLint *params)
{
   /* The Mesa layer gives us a temporary params buffer that is guaranteed
    * to be non-NULL, and have at least 16 elements.
    */
   assert(params != NULL);

   switch (pname) {
   case GL_SAMPLES:
      brw_query_samples_for_format(ctx, target, internalFormat, params);
      break;

   case GL_NUM_SAMPLE_COUNTS: {
      size_t num_samples;
      GLint dummy_buffer[16];

      num_samples = brw_query_samples_for_format(ctx, target, internalFormat,
                                                 dummy_buffer);
      params[0] = (GLint) num_samples;
      break;
   }

   case GL_INTERNALFORMAT_PREFERRED: {
      params[0] = GL_NONE;

      /* We need to resolve an internal format that is compatible with
       * the passed internal format, and optimal to the driver. By now,
       * we just validate that the passed internal format is supported by
       * the driver, and if so return the same internal format, otherwise
       * return GL_NONE.
       *
       * For validating the internal format, we use the
       * ctx->TextureFormatSupported map to check that a BRW surface format
       * exists, that can be derived from the internal format. But this
       * expects a mesa_format, not an internal format. So we need to "come up"
       * with a type that is generic enough, to resolve the mesa_format first.
       */
      GLenum type = get_generic_type_for_internal_format(internalFormat);

      /* Get a mesa_format from the internal format and type. */
      GLint base_format = _mesa_base_tex_format(ctx, internalFormat);
      if (base_format != -1) {
         mesa_format mesa_format =
            _mesa_format_from_format_and_type(base_format, type);

         if (mesa_format < MESA_FORMAT_COUNT &&
             ctx->TextureFormatSupported[mesa_format]) {
            params[0] = internalFormat;
         }
      }
      break;
   }

   default:
      /* By default, we call the driver hook's fallback function from the frontend,
       * which has generic implementation for all pnames.
       */
      _mesa_query_internal_format_default(ctx, target, internalFormat, pname,
                                          params);
      break;
   }
}
