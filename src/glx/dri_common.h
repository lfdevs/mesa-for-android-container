/*
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright © 2008 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Authors:
 *   Kevin E. Martin <kevin@precisioninsight.com>
 *   Brian Paul <brian@precisioninsight.com>
 *   Kristian Høgsberg (krh@redhat.com)
 */

#ifndef _DRI_COMMON_H
#define _DRI_COMMON_H

#ifdef GLX_DIRECT_RENDERING

#include "mesa_interface.h"
#include <stdbool.h>
#include "loader.h"
#include "util/macros.h" /* for PRINTFLIKE */

typedef struct __GLXDRIconfigPrivateRec __GLXDRIconfigPrivate;

struct __GLXDRIconfigPrivateRec
{
   struct glx_config base;
   const struct dri_config *driConfig;
};

extern struct glx_config *driConvertConfigs(struct glx_config * modes,
                                           const struct dri_config ** configs);

extern void driDestroyConfigs(const struct dri_config **configs);

extern __GLXDRIdrawable *
driFetchDrawable(struct glx_context *gc, GLXDrawable glxDrawable);

extern void
driReleaseDrawables(struct glx_context *gc);

struct dri_ctx_attribs {
   unsigned major_ver;
   unsigned minor_ver;
   uint32_t render_type;
   uint32_t flags;
   unsigned api;
   int reset;
   int release;
   int no_error;
};

extern const struct glx_screen_vtable dri_screen_vtable;

extern unsigned
dri_context_error_to_glx_error(unsigned error);

extern int
dri_convert_glx_attribs(unsigned num_attribs, const uint32_t *attribs,
                        struct dri_ctx_attribs *dca);

extern struct glx_context *
dri_common_create_context(struct glx_screen *base,
                          struct glx_config *config_base,
                          struct glx_context *shareList,
                          int renderType);

extern const __DRIbackgroundCallableExtension driBackgroundCallable;
extern const __DRIuseInvalidateExtension dri2UseInvalidate;

Bool
dri_bind_context(struct glx_context *context, GLXDrawable draw, GLXDrawable read);
void
dri_unbind_context(struct glx_context *context);
void
dri_destroy_context(struct glx_context *context);
struct glx_context *
dri_create_context_attribs(struct glx_screen *base,
                           struct glx_config *config_base,
                           struct glx_context *shareList,
                           unsigned num_attribs,
                           const uint32_t *attribs,
                           unsigned *error);
_X_HIDDEN int
glx_dri_query_renderer_integer(struct glx_screen *base, int attribute,
                            unsigned int *value);
_X_HIDDEN int
glx_dri_query_renderer_string(struct glx_screen *base, int attribute,
                           const char **value);
char *
dri_get_driver_name(struct glx_screen *glx_screen);
void
dri_bind_tex_image(__GLXDRIdrawable *base, int buffer, const int *attrib_list);
bool
dri_screen_init(struct glx_screen *psc, struct glx_display *priv, int screen, int fd, const __DRIextension **loader_extensions, bool driver_name_is_inferred);
#endif /* GLX_DIRECT_RENDERING */

#endif /* _DRI_COMMON_H */
