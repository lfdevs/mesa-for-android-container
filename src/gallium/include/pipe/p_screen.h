/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

/**
 * @file
 * 
 * Screen, Adapter or GPU
 *
 * These are driver functions/facilities that are context independent.
 */


#ifndef P_SCREEN_H
#define P_SCREEN_H


#include "pipe/p_compiler.h"
#include "pipe/p_format.h"
#include "pipe/p_defines.h"
#include "pipe/p_video_enums.h"



#ifdef __cplusplus
extern "C" {
#endif


/** Opaque types */
struct winsys_handle;
struct pipe_fence_handle;
struct pipe_resource;
struct pipe_surface;
struct pipe_transfer;
struct pipe_box;
struct pipe_memory_info;


/**
 * Gallium screen/adapter context.  Basically everything
 * hardware-specific that doesn't actually require a rendering
 * context.
 */
struct pipe_screen {
   void (*destroy)( struct pipe_screen * );

   const char *(*get_name)( struct pipe_screen * );

   const char *(*get_vendor)( struct pipe_screen * );

   /**
    * Returns the device vendor.
    *
    * The returned value should return the actual device vendor/manufacturer,
    * rather than a potentially generic driver string.
    */
   const char *(*get_device_vendor)( struct pipe_screen * );

   /**
    * Query an integer-valued capability/parameter/limit
    * \param param  one of PIPE_CAP_x
    */
   int (*get_param)( struct pipe_screen *, enum pipe_cap param );

   /**
    * Query a float-valued capability/parameter/limit
    * \param param  one of PIPE_CAP_x
    */
   float (*get_paramf)( struct pipe_screen *, enum pipe_capf param );

   /**
    * Query a per-shader-stage integer-valued capability/parameter/limit
    * \param param  one of PIPE_CAP_x
    */
   int (*get_shader_param)( struct pipe_screen *, unsigned shader, enum pipe_shader_cap param );

   /**
    * Query an integer-valued capability/parameter/limit for a codec/profile
    * \param param  one of PIPE_VIDEO_CAP_x
    */
   int (*get_video_param)( struct pipe_screen *,
			   enum pipe_video_profile profile,
			   enum pipe_video_entrypoint entrypoint,
			   enum pipe_video_cap param );

   /**
    * Query a compute-specific capability/parameter/limit.
    * \param ir_type shader IR type for which the param applies, or don't care
    *                if the param is not shader related
    * \param param   one of PIPE_COMPUTE_CAP_x
    * \param ret     pointer to a preallocated buffer that will be
    *                initialized to the parameter value, or NULL.
    * \return        size in bytes of the parameter value that would be
    *                returned.
    */
   int (*get_compute_param)(struct pipe_screen *,
			    enum pipe_shader_ir ir_type,
			    enum pipe_compute_cap param,
			    void *ret);

   /**
    * Query a timestamp in nanoseconds. The returned value should match
    * PIPE_QUERY_TIMESTAMP. This function returns immediately and doesn't
    * wait for rendering to complete (which cannot be achieved with queries).
    */
   uint64_t (*get_timestamp)(struct pipe_screen *);

   /**
    * Create a context.
    *
    * \param screen      pipe screen
    * \param priv        a pointer to set in pipe_context::priv
    * \param flags       a mask of PIPE_CONTEXT_* flags
    */
   struct pipe_context * (*context_create)(struct pipe_screen *screen,
					   void *priv, unsigned flags);

   /**
    * Check if the given pipe_format is supported as a texture or
    * drawing surface.
    * \param bindings  bitmask of PIPE_BIND_*
    */
   boolean (*is_format_supported)( struct pipe_screen *,
                                   enum pipe_format format,
                                   enum pipe_texture_target target,
                                   unsigned sample_count,
                                   unsigned bindings );

   /**
    * Check if the given pipe_format is supported as output for this codec/profile.
    * \param profile  profile to check, may also be PIPE_VIDEO_PROFILE_UNKNOWN
    */
   boolean (*is_video_format_supported)( struct pipe_screen *,
                                         enum pipe_format format,
                                         enum pipe_video_profile profile,
                                         enum pipe_video_entrypoint entrypoint );

   /**
    * Check if we can actually create the given resource (test the dimension,
    * overall size, etc).  Used to implement proxy textures.
    * \return TRUE if size is OK, FALSE if too large.
    */
   boolean (*can_create_resource)(struct pipe_screen *screen,
                                  const struct pipe_resource *templat);

   /**
    * Create a new texture object, using the given template info.
    */
   struct pipe_resource * (*resource_create)(struct pipe_screen *,
					     const struct pipe_resource *templat);

   struct pipe_resource * (*resource_create_front)(struct pipe_screen *,
                                                   const struct pipe_resource *templat,
                                                   const void *map_front_private);

   /**
    * Create a texture from a winsys_handle. The handle is often created in
    * another process by first creating a pipe texture and then calling
    * resource_get_handle.
    *
    * NOTE: in the case of DRM_API_HANDLE_TYPE_FD handles, the caller
    * retains ownership of the FD.  (This is consistent with
    * EGL_EXT_image_dma_buf_import)
    *
    * \param usage  A combination of PIPE_HANDLE_USAGE_* flags.
    */
   struct pipe_resource * (*resource_from_handle)(struct pipe_screen *,
						  const struct pipe_resource *templat,
						  struct winsys_handle *handle,
						  unsigned usage);

   /**
    * Create a resource from user memory. This maps the user memory into
    * the device address space.
    */
   struct pipe_resource * (*resource_from_user_memory)(struct pipe_screen *,
                                                       const struct pipe_resource *t,
                                                       void *user_memory);

   /**
    * Get a winsys_handle from a texture. Some platforms/winsys requires
    * that the texture is created with a special usage flag like
    * DISPLAYTARGET or PRIMARY.
    *
    * NOTE: in the case of DRM_API_HANDLE_TYPE_FD handles, the caller
    * takes ownership of the FD.  (This is consistent with
    * EGL_MESA_image_dma_buf_export)
    *
    * \param usage  A combination of PIPE_HANDLE_USAGE_* flags.
    */
   boolean (*resource_get_handle)(struct pipe_screen *,
				  struct pipe_resource *tex,
				  struct winsys_handle *handle,
				  unsigned usage);


   void (*resource_destroy)(struct pipe_screen *,
			    struct pipe_resource *pt);


   /**
    * Do any special operations to ensure frontbuffer contents are
    * displayed, eg copy fake frontbuffer.
    * \param winsys_drawable_handle  an opaque handle that the calling context
    *                                gets out-of-band
    * \param subbox an optional sub region to flush
    */
   void (*flush_frontbuffer)( struct pipe_screen *screen,
                              struct pipe_resource *resource,
                              unsigned level, unsigned layer,
                              void *winsys_drawable_handle,
                              struct pipe_box *subbox );

   /** Set ptr = fence, with reference counting */
   void (*fence_reference)( struct pipe_screen *screen,
                            struct pipe_fence_handle **ptr,
                            struct pipe_fence_handle *fence );

   /**
    * Wait for the fence to finish.
    * \param timeout  in nanoseconds (may be PIPE_TIMEOUT_INFINITE).
    */
   boolean (*fence_finish)( struct pipe_screen *screen,
                            struct pipe_fence_handle *fence,
                            uint64_t timeout );

   /**
    * Returns a driver-specific query.
    *
    * If \p info is NULL, the number of available queries is returned.
    * Otherwise, the driver query at the specified \p index is returned
    * in \p info. The function returns non-zero on success.
    */
   int (*get_driver_query_info)(struct pipe_screen *screen,
                                unsigned index,
                                struct pipe_driver_query_info *info);

   /**
    * Returns a driver-specific query group.
    *
    * If \p info is NULL, the number of available groups is returned.
    * Otherwise, the driver query group at the specified \p index is returned
    * in \p info. The function returns non-zero on success.
    */
   int (*get_driver_query_group_info)(struct pipe_screen *screen,
                                      unsigned index,
                                      struct pipe_driver_query_group_info *info);

   /**
    * Query information about memory usage.
    */
   void (*query_memory_info)(struct pipe_screen *screen,
                             struct pipe_memory_info *info);

   /**
    * Get IR specific compiler options struct.  For PIPE_SHADER_IR_NIR this
    * returns a 'struct nir_shader_compiler_options'.  Drivers reporting
    * NIR as the preferred IR must implement this.
    */
   const void *(*get_compiler_options)(struct pipe_screen *screen,
                                      enum pipe_shader_ir ir,
                                      unsigned shader);
};


#ifdef __cplusplus
}
#endif

#endif /* P_SCREEN_H */
