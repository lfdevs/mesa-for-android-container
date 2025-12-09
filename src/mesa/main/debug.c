/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
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
 */

#include <stdio.h>
#include "errors.h"
#include "mtypes.h"
#include "attrib.h"
#include "enums.h"
#include "formats.h"
#include "hash.h"

#include "macros.h"
#include "debug.h"
#include "get.h"
#include "pixelstore.h"
#include "readpix.h"
#include "texobj.h"
#include "api_exec_decl.h"

#include "state_tracker/st_cb_texture.h"
#include "state_tracker/st_cb_readpixels.h"

void
_mesa_print_state( const char *msg, GLuint state )
{
   _mesa_debug(NULL,
           "%s: (0x%x) %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
           msg,
           state,
#define S(def) (state & def) ? (#def ", ") + 5 : ""
           S(_NEW_MODELVIEW),
           S(_NEW_PROJECTION),
           S(_NEW_TEXTURE_MATRIX),
           S(_NEW_COLOR),
           S(_NEW_DEPTH),
           S(_NEW_TNL_SPACES),
           S(_NEW_FOG),
           S(_NEW_HINT),
           S(_NEW_LIGHT_CONSTANTS),
           S(_NEW_LINE),
           S(_NEW_PIXEL),
           S(_NEW_POINT),
           S(_NEW_POLYGON),
           S(_NEW_POLYGONSTIPPLE),
           S(_NEW_SCISSOR),
           S(_NEW_STENCIL),
           S(_NEW_TEXTURE_OBJECT),
           S(_NEW_TRANSFORM),
           S(_NEW_VIEWPORT),
           S(_NEW_TEXTURE_STATE),
           S(_NEW_LIGHT_STATE),
           S(_NEW_RENDERMODE),
           S(_NEW_BUFFERS),
           S(_NEW_CURRENT_ATTRIB),
           S(_NEW_MULTISAMPLE),
           S(_NEW_TRACK_MATRIX),
           S(_NEW_PROGRAM),
           S(_NEW_PROGRAM_CONSTANTS),
           S(_NEW_FF_VERT_PROGRAM),
           S(_NEW_FRAG_CLAMP),
           S(_NEW_MATERIAL),
           S(_NEW_FF_FRAG_PROGRAM));
#undef S
}



/**
 * Print information about this Mesa version and build options.
 */
void _mesa_print_info( struct gl_context *ctx )
{
   _mesa_debug(NULL, "Mesa GL_VERSION = %s\n",
	   (char *) _mesa_GetString(GL_VERSION));
   _mesa_debug(NULL, "Mesa GL_RENDERER = %s\n",
	   (char *) _mesa_GetString(GL_RENDERER));
   _mesa_debug(NULL, "Mesa GL_VENDOR = %s\n",
	   (char *) _mesa_GetString(GL_VENDOR));

   /* use ctx as GL_EXTENSIONS will not work on 3.0 or higher
    * core contexts.
    */
   _mesa_debug(NULL, "Mesa GL_EXTENSIONS = %s\n", ctx->Extensions.String);

#if DETECT_ARCH_X86
   _mesa_debug(NULL, "Mesa x86-optimized: YES\n");
#else
   _mesa_debug(NULL, "Mesa x86-optimized: NO\n");
#endif
#if DETECT_ARCH_SPARC64
   _mesa_debug(NULL, "Mesa sparc-optimized: YES\n");
#else
   _mesa_debug(NULL, "Mesa sparc-optimized: NO\n");
#endif
}


/**
 * Set verbose logging flags.  When these flags are set, GL API calls
 * in the various categories will be printed to stderr.
 * \param str  a comma-separated list of keywords
 */
static void
set_verbose_flags(const char *str)
{
#ifndef NDEBUG
   struct option {
      const char *name;
      GLbitfield flag;
   };
   static const struct option opts[] = {
      { "varray",    VERBOSE_VARRAY },
      { "tex",       VERBOSE_TEXTURE },
      { "mat",       VERBOSE_MATERIAL },
      { "pipe",      VERBOSE_PIPELINE },
      { "driver",    VERBOSE_DRIVER },
      { "state",     VERBOSE_STATE },
      { "api",       VERBOSE_API },
      { "list",      VERBOSE_DISPLAY_LIST },
      { "lighting",  VERBOSE_LIGHTING },
      { "disassem",  VERBOSE_DISASSEM },
      { "swap",      VERBOSE_SWAPBUFFERS }
   };
   GLuint i;

   if (!str)
      return;

   MESA_VERBOSE = 0x0;
   for (i = 0; i < ARRAY_SIZE(opts); i++) {
      if (strstr(str, opts[i].name) || strcmp(str, "all") == 0)
         MESA_VERBOSE |= opts[i].flag;
   }
#endif
}


/**
 * Set debugging flags.  When these flags are set, Mesa will do additional
 * debug checks or actions.
 * \param str  a comma-separated list of keywords
 */
static void
set_debug_flags(const char *str)
{
#ifndef NDEBUG
   struct option {
      const char *name;
      GLbitfield flag;
   };
   static const struct option opts[] = {
      { "silent", DEBUG_SILENT }, /* turn off debug messages */
      { "flush", DEBUG_ALWAYS_FLUSH }, /* flush after each drawing command */
      { "incomplete_tex", DEBUG_INCOMPLETE_TEXTURE },
      { "incomplete_fbo", DEBUG_INCOMPLETE_FBO },
      { "context", DEBUG_CONTEXT }, /* force set GL_CONTEXT_FLAG_DEBUG_BIT flag */
      { "fallback_tex", DEBUG_FALLBACK_TEXTURE },
   };
   GLuint i;

   if (!str)
      return;

   MESA_DEBUG_FLAGS = 0x0;
   for (i = 0; i < ARRAY_SIZE(opts); i++) {
      if (strstr(str, opts[i].name))
         MESA_DEBUG_FLAGS |= opts[i].flag;
   }
#endif
}


/**
 * Initialize debugging variables from env vars.
 */
void
_mesa_init_debug( struct gl_context *ctx )
{
   set_debug_flags(os_get_option("MESA_DEBUG"));
   set_verbose_flags(os_get_option("MESA_VERBOSE"));
}
