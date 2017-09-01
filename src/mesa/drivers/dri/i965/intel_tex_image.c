
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/enums.h"
#include "main/bufferobj.h"
#include "main/context.h"
#include "main/formats.h"
#include "main/glformats.h"
#include "main/image.h"
#include "main/pbo.h"
#include "main/renderbuffer.h"
#include "main/texcompress.h"
#include "main/texgetimage.h"
#include "main/texobj.h"
#include "main/teximage.h"
#include "main/texstore.h"

#include "drivers/common/meta.h"

#include "intel_mipmap_tree.h"
#include "intel_buffer_objects.h"
#include "intel_batchbuffer.h"
#include "intel_tex.h"
#include "intel_blit.h"
#include "intel_fbo.h"
#include "intel_image.h"
#include "intel_tiled_memcpy.h"
#include "brw_context.h"

#define FILE_DEBUG_FLAG DEBUG_TEXTURE

/* Make sure one doesn't end up shrinking base level zero unnecessarily.
 * Determining the base level dimension by shifting higher level dimension
 * ends up in off-by-one value in case base level has NPOT size (for example,
 * 293 != 146 << 1).
 * Choose the original base level dimension when shifted dimensions agree.
 * Otherwise assume real resize is intended and use the new shifted value.
 */
static unsigned 
get_base_dim(unsigned old_base_dim, unsigned new_level_dim, unsigned level)
{
   const unsigned old_level_dim = old_base_dim >> level;
   const unsigned new_base_dim = new_level_dim << level;

   return old_level_dim == new_level_dim ? old_base_dim : new_base_dim;
}

/* Work back from the specified level of the image to the baselevel and create a
 * miptree of that size.
 */
struct intel_mipmap_tree *
intel_miptree_create_for_teximage(struct brw_context *brw,
				  struct intel_texture_object *intelObj,
				  struct intel_texture_image *intelImage,
                                  enum intel_miptree_create_flags flags)
{
   GLuint lastLevel;
   int width, height, depth;
   unsigned old_width = 0, old_height = 0, old_depth = 0;
   const struct intel_mipmap_tree *old_mt = intelObj->mt;
   const unsigned level = intelImage->base.Base.Level;

   intel_get_image_dims(&intelImage->base.Base, &width, &height, &depth);

   if (old_mt) {
      old_width = old_mt->surf.logical_level0_px.width;
      old_height = old_mt->surf.logical_level0_px.height;
      old_depth = old_mt->surf.dim == ISL_SURF_DIM_3D ?
                     old_mt->surf.logical_level0_px.depth :
                     old_mt->surf.logical_level0_px.array_len;
   }

   DBG("%s\n", __func__);

   /* Figure out image dimensions at start level. */
   switch(intelObj->base.Target) {
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
   case GL_TEXTURE_RECTANGLE:
   case GL_TEXTURE_EXTERNAL_OES:
      assert(level == 0);
      break;
   case GL_TEXTURE_3D:
      depth = old_mt ? get_base_dim(old_depth, depth, level) :
                       depth << level;
      /* Fall through */
   case GL_TEXTURE_2D:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
      height = old_mt ? get_base_dim(old_height, height, level) :
                        height << level;
      /* Fall through */
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY:
      width = old_mt ? get_base_dim(old_width, width, level) :
                       width << level;
      break;
   default:
      unreachable("Unexpected target");
   }

   /* Guess a reasonable value for lastLevel.  This is probably going
    * to be wrong fairly often and might mean that we have to look at
    * resizable buffers, or require that buffers implement lazy
    * pagetable arrangements.
    */
   if ((intelObj->base.Sampler.MinFilter == GL_NEAREST ||
        intelObj->base.Sampler.MinFilter == GL_LINEAR) &&
       intelImage->base.Base.Level == 0 &&
       !intelObj->base.GenerateMipmap) {
      lastLevel = 0;
   } else {
      lastLevel = _mesa_get_tex_max_num_levels(intelObj->base.Target,
                                               width, height, depth) - 1;
   }

   return intel_miptree_create(brw,
			       intelObj->base.Target,
			       intelImage->base.Base.TexFormat,
			       0,
			       lastLevel,
			       width,
			       height,
			       depth,
                               MAX2(intelImage->base.Base.NumSamples, 1),
                               flags);
}

static void
intelTexImage(struct gl_context * ctx,
              GLuint dims,
              struct gl_texture_image *texImage,
              GLenum format, GLenum type, const void *pixels,
              const struct gl_pixelstore_attrib *unpack)
{
   struct intel_texture_image *intelImage = intel_texture_image(texImage);
   bool ok;

   bool tex_busy = intelImage->mt && brw_bo_busy(intelImage->mt->bo);

   DBG("%s mesa_format %s target %s format %s type %s level %d %dx%dx%d\n",
       __func__, _mesa_get_format_name(texImage->TexFormat),
       _mesa_enum_to_string(texImage->TexObject->Target),
       _mesa_enum_to_string(format), _mesa_enum_to_string(type),
       texImage->Level, texImage->Width, texImage->Height, texImage->Depth);

   /* Allocate storage for texture data. */
   if (!ctx->Driver.AllocTextureImageBuffer(ctx, texImage)) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage%uD", dims);
      return;
   }

   assert(intelImage->mt);

   if (intelImage->mt->format == MESA_FORMAT_S_UINT8)
      intelImage->mt->r8stencil_needs_update = true;

   ok = _mesa_meta_pbo_TexSubImage(ctx, dims, texImage, 0, 0, 0,
                                   texImage->Width, texImage->Height,
                                   texImage->Depth,
                                   format, type, pixels,
                                   tex_busy, unpack);
   if (ok)
      return;

   ok = intel_texsubimage_tiled_memcpy(ctx, dims, texImage,
                                       0, 0, 0, /*x,y,z offsets*/
                                       texImage->Width,
                                       texImage->Height,
                                       texImage->Depth,
                                       format, type, pixels, unpack,
                                       false /*allocate_storage*/);
   if (ok)
      return;

   DBG("%s: upload image %dx%dx%d pixels %p\n",
       __func__, texImage->Width, texImage->Height, texImage->Depth,
       pixels);

   _mesa_store_teximage(ctx, dims, texImage,
                        format, type, pixels, unpack);
}


static void
intel_set_texture_image_mt(struct brw_context *brw,
                           struct gl_texture_image *image,
                           GLenum internal_format,
                           struct intel_mipmap_tree *mt)

{
   struct gl_texture_object *texobj = image->TexObject;
   struct intel_texture_object *intel_texobj = intel_texture_object(texobj);
   struct intel_texture_image *intel_image = intel_texture_image(image);

   _mesa_init_teximage_fields(&brw->ctx, image,
                              mt->surf.logical_level0_px.width,
                              mt->surf.logical_level0_px.height, 1,
                              0, internal_format, mt->format);

   brw->ctx.Driver.FreeTextureImageBuffer(&brw->ctx, image);

   intel_texobj->needs_validate = true;
   intel_image->base.RowStride = mt->surf.row_pitch / mt->cpp;
   assert(mt->surf.row_pitch % mt->cpp == 0);

   intel_miptree_reference(&intel_image->mt, mt);

   /* Immediately validate the image to the object. */
   intel_miptree_reference(&intel_texobj->mt, mt);
}


void
intelSetTexBuffer2(__DRIcontext *pDRICtx, GLint target,
		   GLint texture_format,
		   __DRIdrawable *dPriv)
{
   struct gl_framebuffer *fb = dPriv->driverPrivate;
   struct brw_context *brw = pDRICtx->driverPrivate;
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *rb;
   struct gl_texture_object *texObj;
   struct gl_texture_image *texImage;
   mesa_format texFormat = MESA_FORMAT_NONE;
   struct intel_mipmap_tree *mt;
   GLenum internal_format = 0;

   texObj = _mesa_get_current_tex_object(ctx, target);

   if (!texObj)
      return;

   if (dPriv->lastStamp != dPriv->dri2.stamp ||
       !pDRICtx->driScreenPriv->dri2.useInvalidate)
      intel_update_renderbuffers(pDRICtx, dPriv);

   rb = intel_get_renderbuffer(fb, BUFFER_FRONT_LEFT);
   /* If the miptree isn't set, then intel_update_renderbuffers was unable
    * to get the BO for the drawable from the window system.
    */
   if (!rb || !rb->mt)
      return;

   if (rb->mt->cpp == 4) {
      if (texture_format == __DRI_TEXTURE_FORMAT_RGB) {
         internal_format = GL_RGB;
         texFormat = MESA_FORMAT_B8G8R8X8_UNORM;
      }
      else {
         internal_format = GL_RGBA;
         texFormat = MESA_FORMAT_B8G8R8A8_UNORM;
      }
   } else if (rb->mt->cpp == 2) {
      internal_format = GL_RGB;
      texFormat = MESA_FORMAT_B5G6R5_UNORM;
   }

   intel_miptree_make_shareable(brw, rb->mt);
   mt = intel_miptree_create_for_bo(brw, rb->mt->bo, texFormat, 0,
                                    rb->Base.Base.Width,
                                    rb->Base.Base.Height,
                                    1, rb->mt->surf.row_pitch,
                                    MIPTREE_CREATE_DEFAULT);
   if (mt == NULL)
       return;
   mt->target = target;

   _mesa_lock_texture(&brw->ctx, texObj);
   texImage = _mesa_get_tex_image(ctx, texObj, target, 0);
   intel_set_texture_image_mt(brw, texImage, internal_format, mt);
   intel_miptree_release(&mt);
   _mesa_unlock_texture(&brw->ctx, texObj);
}

static GLboolean
intel_bind_renderbuffer_tex_image(struct gl_context *ctx,
                                  struct gl_renderbuffer *rb,
                                  struct gl_texture_image *image)
{
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);
   struct intel_texture_image *intel_image = intel_texture_image(image);
   struct gl_texture_object *texobj = image->TexObject;
   struct intel_texture_object *intel_texobj = intel_texture_object(texobj);

   /* We can only handle RB allocated with AllocRenderbufferStorage, or
    * window-system renderbuffers.
    */
   assert(!rb->TexImage);

   if (!irb->mt)
      return false;

   _mesa_lock_texture(ctx, texobj);
   _mesa_init_teximage_fields(ctx, image,
			      rb->Width, rb->Height, 1,
			      0, rb->InternalFormat, rb->Format);
   image->NumSamples = rb->NumSamples;

   intel_miptree_reference(&intel_image->mt, irb->mt);

   /* Immediately validate the image to the object. */
   intel_miptree_reference(&intel_texobj->mt, intel_image->mt);

   intel_texobj->needs_validate = true;
   _mesa_unlock_texture(ctx, texobj);

   return true;
}

void
intelSetTexBuffer(__DRIcontext *pDRICtx, GLint target, __DRIdrawable *dPriv)
{
   /* The old interface didn't have the format argument, so copy our
    * implementation's behavior at the time.
    */
   intelSetTexBuffer2(pDRICtx, target, __DRI_TEXTURE_FORMAT_RGBA, dPriv);
}

static void
intel_image_target_texture_2d(struct gl_context *ctx, GLenum target,
			      struct gl_texture_object *texObj,
			      struct gl_texture_image *texImage,
			      GLeglImageOES image_handle)
{
   struct brw_context *brw = brw_context(ctx);
   struct intel_mipmap_tree *mt;
   __DRIscreen *dri_screen = brw->screen->driScrnPriv;
   __DRIimage *image;

   image = dri_screen->dri2.image->lookupEGLImage(dri_screen, image_handle,
                                                  dri_screen->loaderPrivate);
   if (image == NULL)
      return;

   /* We support external textures only for EGLImages created with
    * EGL_EXT_image_dma_buf_import. We may lift that restriction in the future.
    */
   if (target == GL_TEXTURE_EXTERNAL_OES && !image->dma_buf_imported) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
            "glEGLImageTargetTexture2DOES(external target is enabled only "
               "for images created with EGL_EXT_image_dma_buf_import");
      return;
   }

   /* Disallow depth/stencil textures: we don't have a way to pass the
    * separate stencil miptree of a GL_DEPTH_STENCIL texture through.
    */
   if (image->has_depthstencil) {
      _mesa_error(ctx, GL_INVALID_OPERATION, __func__);
      return;
   }

   mt = intel_miptree_create_for_dri_image(brw, image, target,
                                           ISL_COLORSPACE_NONE, false);
   if (mt == NULL)
      return;

   struct intel_texture_object *intel_texobj = intel_texture_object(texObj);
   intel_texobj->planar_format = image->planar_format;

   const GLenum internal_format =
      image->internal_format != 0 ?
      image->internal_format : _mesa_get_format_base_format(mt->format);
   intel_set_texture_image_mt(brw, texImage, internal_format, mt);
   intel_miptree_release(&mt);
}

/**
 * \brief A fast path for glGetTexImage.
 *
 * \see intel_readpixels_tiled_memcpy()
 */
bool
intel_gettexsubimage_tiled_memcpy(struct gl_context *ctx,
                                  struct gl_texture_image *texImage,
                                  GLint xoffset, GLint yoffset,
                                  GLsizei width, GLsizei height,
                                  GLenum format, GLenum type,
                                  GLvoid *pixels,
                                  const struct gl_pixelstore_attrib *packing)
{
   struct brw_context *brw = brw_context(ctx);
   struct intel_texture_image *image = intel_texture_image(texImage);
   int dst_pitch;

   /* The miptree's buffer. */
   struct brw_bo *bo;

   uint32_t cpp;
   mem_copy_fn mem_copy = NULL;

   /* This fastpath is restricted to specific texture types:
    * a 2D BGRA, RGBA, L8 or A8 texture. It could be generalized to support
    * more types.
    *
    * FINISHME: The restrictions below on packing alignment and packing row
    * length are likely unneeded now because we calculate the destination stride
    * with _mesa_image_row_stride. However, before removing the restrictions
    * we need tests.
    */
   if (!brw->has_llc ||
       !(type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV) ||
       !(texImage->TexObject->Target == GL_TEXTURE_2D ||
         texImage->TexObject->Target == GL_TEXTURE_RECTANGLE) ||
       pixels == NULL ||
       _mesa_is_bufferobj(packing->BufferObj) ||
       packing->Alignment > 4 ||
       packing->SkipPixels > 0 ||
       packing->SkipRows > 0 ||
       (packing->RowLength != 0 && packing->RowLength != width) ||
       packing->SwapBytes ||
       packing->LsbFirst ||
       packing->Invert)
      return false;

   /* We can't handle copying from RGBX or BGRX because the tiled_memcpy
    * function doesn't set the last channel to 1. Note this checks BaseFormat
    * rather than TexFormat in case the RGBX format is being simulated with an
    * RGBA format.
    */
   if (texImage->_BaseFormat == GL_RGB)
      return false;

   if (!intel_get_memcpy(texImage->TexFormat, format, type, &mem_copy, &cpp))
      return false;

   /* If this is a nontrivial texture view, let another path handle it instead. */
   if (texImage->TexObject->MinLayer)
      return false;

   if (!image->mt ||
       (image->mt->surf.tiling != ISL_TILING_X &&
        image->mt->surf.tiling != ISL_TILING_Y0)) {
      /* The algorithm is written only for X- or Y-tiled memory. */
      return false;
   }

   /* tiled_to_linear() assumes that if the object is swizzled, it is using
    * I915_BIT6_SWIZZLE_9_10 for X and I915_BIT6_SWIZZLE_9 for Y.  This is only
    * true on gen5 and above.
    *
    * The killer on top is that some gen4 have an L-shaped swizzle mode, where
    * parts of the memory aren't swizzled at all. Userspace just can't handle
    * that.
    */
   if (brw->gen < 5 && brw->has_swizzling)
      return false;

   int level = texImage->Level + texImage->TexObject->MinLevel;

   /* Since we are going to write raw data to the miptree, we need to resolve
    * any pending fast color clears before we start.
    */
   assert(image->mt->surf.logical_level0_px.depth == 1);
   assert(image->mt->surf.logical_level0_px.array_len == 1);

   intel_miptree_access_raw(brw, image->mt, level, 0, true);

   bo = image->mt->bo;

   if (brw_batch_references(&brw->batch, bo)) {
      perf_debug("Flushing before mapping a referenced bo.\n");
      intel_batchbuffer_flush(brw);
   }

   void *map = brw_bo_map(brw, bo, MAP_READ | MAP_RAW);
   if (map == NULL) {
      DBG("%s: failed to map bo\n", __func__);
      return false;
   }

   dst_pitch = _mesa_image_row_stride(packing, width, format, type);

   DBG("%s: level=%d x,y=(%d,%d) (w,h)=(%d,%d) format=0x%x type=0x%x "
       "mesa_format=0x%x tiling=%d "
       "packing=(alignment=%d row_length=%d skip_pixels=%d skip_rows=%d)\n",
       __func__, texImage->Level, xoffset, yoffset, width, height,
       format, type, texImage->TexFormat, image->mt->surf.tiling,
       packing->Alignment, packing->RowLength, packing->SkipPixels,
       packing->SkipRows);

   /* Adjust x and y offset based on miplevel */
   unsigned level_x, level_y;
   intel_miptree_get_image_offset(image->mt, level, 0, &level_x, &level_y);
   xoffset += level_x;
   yoffset += level_y;

   tiled_to_linear(
      xoffset * cpp, (xoffset + width) * cpp,
      yoffset, yoffset + height,
      pixels - (ptrdiff_t) yoffset * dst_pitch - (ptrdiff_t) xoffset * cpp,
      map,
      dst_pitch, image->mt->surf.row_pitch,
      brw->has_swizzling,
      image->mt->surf.tiling,
      mem_copy
   );

   brw_bo_unmap(bo);
   return true;
}

static void
intel_get_tex_sub_image(struct gl_context *ctx,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLint depth,
                        GLenum format, GLenum type, GLvoid *pixels,
                        struct gl_texture_image *texImage)
{
   struct brw_context *brw = brw_context(ctx);
   bool ok;

   DBG("%s\n", __func__);

   if (_mesa_is_bufferobj(ctx->Pack.BufferObj)) {
      if (_mesa_meta_pbo_GetTexSubImage(ctx, 3, texImage,
                                        xoffset, yoffset, zoffset,
                                        width, height, depth, format, type,
                                        pixels, &ctx->Pack)) {
         /* Flush to guarantee coherency between the render cache and other
          * caches the PBO could potentially be bound to after this point.
          * See the related comment in intelReadPixels() for a more detailed
          * explanation.
          */
         brw_emit_mi_flush(brw);
         return;
      }

      perf_debug("%s: fallback to CPU mapping in PBO case\n", __func__);
   }

   ok = intel_gettexsubimage_tiled_memcpy(ctx, texImage, xoffset, yoffset,
                                          width, height,
                                          format, type, pixels, &ctx->Pack);

   if(ok)
      return;

   _mesa_meta_GetTexSubImage(ctx, xoffset, yoffset, zoffset,
                             width, height, depth,
                             format, type, pixels, texImage);

   DBG("%s - DONE\n", __func__);
}

static void
flush_astc_denorms(struct gl_context *ctx, GLuint dims,
                   struct gl_texture_image *texImage,
                   GLint xoffset, GLint yoffset, GLint zoffset,
                   GLsizei width, GLsizei height, GLsizei depth)
{
   struct compressed_pixelstore store;
   _mesa_compute_compressed_pixelstore(dims, texImage->TexFormat,
                                       width, height, depth,
                                       &ctx->Unpack, &store);

   for (int slice = 0; slice < store.CopySlices; slice++) {

      /* Map dest texture buffer */
      GLubyte *dstMap;
      GLint dstRowStride;
      ctx->Driver.MapTextureImage(ctx, texImage, slice + zoffset,
                                  xoffset, yoffset, width, height,
                                  GL_MAP_READ_BIT | GL_MAP_WRITE_BIT,
                                  &dstMap, &dstRowStride);
      if (!dstMap)
         continue;

      for (int i = 0; i < store.CopyRowsPerSlice; i++) {

         /* An ASTC block is stored in little endian mode. The byte that
          * contains bits 0..7 is stored at the lower address in memory.
          */
         struct astc_void_extent {
            uint16_t header : 12;
            uint16_t dontcare[3];
            uint16_t R;
            uint16_t G;
            uint16_t B;
            uint16_t A;
         } *blocks = (struct astc_void_extent*) dstMap;

         /* Iterate over every copied block in the row */
         for (int j = 0; j < store.CopyBytesPerRow / 16; j++) {

            /* Check if the header matches that of an LDR void-extent block */
            if (blocks[j].header == 0xDFC) {

               /* Flush UNORM16 values that would be denormalized */
               if (blocks[j].A < 4) blocks[j].A = 0;
               if (blocks[j].B < 4) blocks[j].B = 0;
               if (blocks[j].G < 4) blocks[j].G = 0;
               if (blocks[j].R < 4) blocks[j].R = 0;
            }
         }

         dstMap += dstRowStride;
      }

      ctx->Driver.UnmapTextureImage(ctx, texImage, slice + zoffset);
   }
}


static void
intelCompressedTexSubImage(struct gl_context *ctx, GLuint dims,
                        struct gl_texture_image *texImage,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLsizei depth,
                        GLenum format,
                        GLsizei imageSize, const GLvoid *data)
{
   /* Upload the compressed data blocks */
   _mesa_store_compressed_texsubimage(ctx, dims, texImage,
                                      xoffset, yoffset, zoffset,
                                      width, height, depth,
                                      format, imageSize, data);

   /* Fix up copied ASTC blocks if necessary */
   GLenum gl_format = _mesa_compressed_format_to_glenum(ctx,
                                                        texImage->TexFormat);
   bool is_linear_astc = _mesa_is_astc_format(gl_format) &&
                        !_mesa_is_srgb_format(gl_format);
   struct brw_context *brw = (struct brw_context*) ctx;
   if (brw->gen == 9 && is_linear_astc)
      flush_astc_denorms(ctx, dims, texImage,
                         xoffset, yoffset, zoffset,
                         width, height, depth);
}

void
intelInitTextureImageFuncs(struct dd_function_table *functions)
{
   functions->TexImage = intelTexImage;
   functions->CompressedTexSubImage = intelCompressedTexSubImage;
   functions->EGLImageTargetTexture2D = intel_image_target_texture_2d;
   functions->BindRenderbufferTexImage = intel_bind_renderbuffer_tex_image;
   functions->GetTexSubImage = intel_get_tex_sub_image;
}
