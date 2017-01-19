/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 Intel Corporation All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jason Ekstrand <jason.ekstrand@intel.com>
 */

#include "brw_blorp.h"
#include "intel_fbo.h"
#include "intel_tex.h"
#include "intel_blit.h"
#include "intel_mipmap_tree.h"
#include "main/formats.h"
#include "main/teximage.h"
#include "drivers/common/meta.h"

static void
copy_image_with_memcpy(struct brw_context *brw,
                       struct intel_mipmap_tree *src_mt, int src_level,
                       int src_x, int src_y, int src_z,
                       struct intel_mipmap_tree *dst_mt, int dst_level,
                       int dst_x, int dst_y, int dst_z,
                       int src_width, int src_height)
{
   bool same_slice;
   void *mapped, *src_mapped, *dst_mapped;
   ptrdiff_t src_stride, dst_stride, cpp;
   int map_x1, map_y1, map_x2, map_y2;
   GLuint src_bw, src_bh;

   cpp = _mesa_get_format_bytes(src_mt->format);
   _mesa_get_format_block_size(src_mt->format, &src_bw, &src_bh);

   assert(src_width % src_bw == 0);
   assert(src_height % src_bh == 0);
   assert(src_x % src_bw == 0);
   assert(src_y % src_bh == 0);

   /* If we are on the same miptree, same level, and same slice, then
    * intel_miptree_map won't let us map it twice.  We have to do things a
    * bit differently.  In particular, we do a single map large enough for
    * both portions and in read-write mode.
    */
   same_slice = src_mt == dst_mt && src_level == dst_level && src_z == dst_z;

   if (same_slice) {
      assert(dst_x % src_bw == 0);
      assert(dst_y % src_bh == 0);

      map_x1 = MIN2(src_x, dst_x);
      map_y1 = MIN2(src_y, dst_y);
      map_x2 = MAX2(src_x, dst_x) + src_width;
      map_y2 = MAX2(src_y, dst_y) + src_height;

      intel_miptree_map(brw, src_mt, src_level, src_z,
                        map_x1, map_y1, map_x2 - map_x1, map_y2 - map_y1,
                        GL_MAP_READ_BIT | GL_MAP_WRITE_BIT,
                        &mapped, &src_stride);

      dst_stride = src_stride;

      /* Set the offsets here so we don't have to think about while looping */
      src_mapped = mapped + ((src_y - map_y1) / src_bh) * src_stride +
                            ((src_x - map_x1) / src_bw) * cpp;
      dst_mapped = mapped + ((dst_y - map_y1) / src_bh) * dst_stride +
                            ((dst_x - map_x1) / src_bw) * cpp;
   } else {
      intel_miptree_map(brw, src_mt, src_level, src_z,
                        src_x, src_y, src_width, src_height,
                        GL_MAP_READ_BIT, &src_mapped, &src_stride);
      intel_miptree_map(brw, dst_mt, dst_level, dst_z,
                        dst_x, dst_y, src_width, src_height,
                        GL_MAP_WRITE_BIT, &dst_mapped, &dst_stride);
   }

   src_width /= (int)src_bw;
   src_height /= (int)src_bh;

   for (int i = 0; i < src_height; ++i) {
      memcpy(dst_mapped, src_mapped, src_width * cpp);
      src_mapped += src_stride;
      dst_mapped += dst_stride;
   }

   if (same_slice) {
      intel_miptree_unmap(brw, src_mt, src_level, src_z);
   } else {
      intel_miptree_unmap(brw, dst_mt, dst_level, dst_z);
      intel_miptree_unmap(brw, src_mt, src_level, src_z);
   }
}

static void
copy_miptrees(struct brw_context *brw,
              struct intel_mipmap_tree *src_mt,
              int src_x, int src_y, int src_z, unsigned src_level,
              struct intel_mipmap_tree *dst_mt,
              int dst_x, int dst_y, int dst_z, unsigned dst_level,
              int src_width, int src_height)
{
   unsigned bw, bh;

   if (brw->gen >= 6) {
      brw_blorp_copy_miptrees(brw,
                              src_mt, src_level, src_z,
                              dst_mt, dst_level, dst_z,
                              src_x, src_y, dst_x, dst_y,
                              src_width, src_height);
      return;
   }

   /* We are now going to try and copy the texture using the blitter.  If
    * that fails, we will fall back mapping the texture and using memcpy.
    * In either case, we need to do a full resolve.
    */
   intel_miptree_all_slices_resolve_hiz(brw, src_mt);
   intel_miptree_all_slices_resolve_depth(brw, src_mt);
   intel_miptree_all_slices_resolve_color(brw, src_mt, 0);

   intel_miptree_all_slices_resolve_hiz(brw, dst_mt);
   intel_miptree_all_slices_resolve_depth(brw, dst_mt);
   intel_miptree_all_slices_resolve_color(brw, dst_mt, 0);

   _mesa_get_format_block_size(src_mt->format, &bw, &bh);

   /* It's legal to have a WxH that's smaller than a compressed block. This
    * happens for example when you are using a higher level LOD. For this case,
    * we still want to copy the entire block, or else the decompression will be
    * incorrect.
    */
   if (src_width < bw)
      src_width = ALIGN_NPOT(src_width, bw);

   if (src_height < bh)
      src_height = ALIGN_NPOT(src_height, bh);

   if (intel_miptree_copy(brw, src_mt, src_level, src_z, src_x, src_y,
                          dst_mt, dst_level, dst_z, dst_x, dst_y,
                          src_width, src_height))
      return;

   /* This is a worst-case scenario software fallback that maps the two
    * textures and does a memcpy between them.
    */
   copy_image_with_memcpy(brw, src_mt, src_level,
                          src_x, src_y, src_z,
                          dst_mt, dst_level,
                          dst_x, dst_y, dst_z,
                          src_width, src_height);
}

static void
intel_copy_image_sub_data(struct gl_context *ctx,
                          struct gl_texture_image *src_image,
                          struct gl_renderbuffer *src_renderbuffer,
                          int src_x, int src_y, int src_z,
                          struct gl_texture_image *dst_image,
                          struct gl_renderbuffer *dst_renderbuffer,
                          int dst_x, int dst_y, int dst_z,
                          int src_width, int src_height)
{
   struct brw_context *brw = brw_context(ctx);
   struct intel_mipmap_tree *src_mt, *dst_mt;
   unsigned src_level, dst_level;

   if (src_image) {
      src_mt = intel_texture_image(src_image)->mt;
      src_level = src_image->Level + src_image->TexObject->MinLevel;

      /* Cube maps actually have different images per face */
      if (src_image->TexObject->Target == GL_TEXTURE_CUBE_MAP)
         src_z = src_image->Face;

      src_z += src_image->TexObject->MinLayer;
   } else {
      assert(src_renderbuffer);
      src_mt = intel_renderbuffer(src_renderbuffer)->mt;
      src_image = src_renderbuffer->TexImage;
      src_level = 0;
   }

   if (dst_image) {
      dst_mt = intel_texture_image(dst_image)->mt;

      dst_level = dst_image->Level + dst_image->TexObject->MinLevel;

      /* Cube maps actually have different images per face */
      if (dst_image->TexObject->Target == GL_TEXTURE_CUBE_MAP)
         dst_z = dst_image->Face;

      dst_z += dst_image->TexObject->MinLayer;
   } else {
      assert(dst_renderbuffer);
      dst_mt = intel_renderbuffer(dst_renderbuffer)->mt;
      dst_image = dst_renderbuffer->TexImage;
      dst_level = 0;
   }

   copy_miptrees(brw, src_mt, src_x, src_y, src_z, src_level,
                 dst_mt, dst_x, dst_y, dst_z, dst_level,
                 src_width, src_height);

   /* CopyImage only works for equal formats, texture view equivalence
    * classes, and a couple special cases for compressed textures.
    *
    * Notably, GL_DEPTH_STENCIL does not appear in any equivalence
    * classes, so we know the formats must be the same, and thus both
    * will either have stencil, or not.  They can't be mismatched.
    */
   assert((src_mt->stencil_mt != NULL) == (dst_mt->stencil_mt != NULL));

   if (dst_mt->stencil_mt) {
      copy_miptrees(brw, src_mt->stencil_mt, src_x, src_y, src_z, src_level,
                    dst_mt->stencil_mt, dst_x, dst_y, dst_z, dst_level,
                    src_width, src_height);
   }
}

void
intelInitCopyImageFuncs(struct dd_function_table *functions)
{
   functions->CopyImageSubData = intel_copy_image_sub_data;
}
