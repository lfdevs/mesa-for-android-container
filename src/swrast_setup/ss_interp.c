/* $Id: ss_interp.c,v 1.6 2001/04/28 08:39:18 keithw Exp $ */

/*
 * Mesa 3-D graphics library
 * Version:  3.5
 *
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
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
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "glheader.h"
#include "context.h"
#include "colormac.h"
#include "macros.h"
#include "mem.h"
#include "mtypes.h"
#include "mmath.h"

#include "tnl/t_context.h"
#include "swrast_setup/ss_context.h"
#include "swrast_setup/ss_interp.h"

/**********************************************************************/
/*           Interpolate between pairs of vertices                    */
/**********************************************************************/


#define GET_COLOR(ptr, idx) (((GLfloat (*)[4])((ptr)->Ptr))[idx])



#define INTERP_RGBA    0x1
#define INTERP_TEX     0x2
#define INTERP_INDEX   0x4
#define INTERP_SPEC    0x8
#define INTERP_FOG     0x10
#define INTERP_EDGE    0x20
#define MAX_INTERP     0x40

static interp_func interp_tab[MAX_INTERP];
static copy_pv_func copy_tab[MAX_INTERP];


#define IND (0)
#define NAME interp_none
#include "ss_interptmp.h"

#define IND (INTERP_FOG)
#define NAME interp_FOG
#include "ss_interptmp.h"

#define IND (INTERP_TEX)
#define NAME interp_TEX
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_TEX)
#define NAME interp_FOG_TEX
#include "ss_interptmp.h"

#define IND (INTERP_EDGE)
#define NAME interp_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_EDGE)
#define NAME interp_FOG_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_TEX|INTERP_EDGE)
#define NAME interp_TEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_TEX|INTERP_EDGE)
#define NAME interp_FOG_TEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA)
#define NAME interp_RGBA
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC)
#define NAME interp_RGBA_SPEC
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_FOG)
#define NAME interp_RGBA_FOG
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_FOG)
#define NAME interp_RGBA_SPEC_FOG
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_TEX)
#define NAME interp_RGBA_TEX
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_TEX)
#define NAME interp_RGBA_SPEC_TEX
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_FOG|INTERP_TEX)
#define NAME interp_RGBA_FOG_TEX
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_FOG|INTERP_TEX)
#define NAME interp_RGBA_SPEC_FOG_TEX
#include "ss_interptmp.h"

#define IND (INTERP_INDEX)
#define NAME interp_INDEX
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_INDEX)
#define NAME interp_FOG_INDEX
#include "ss_interptmp.h"

#define IND (INTERP_TEX|INTERP_INDEX)
#define NAME interp_TEX_INDEX
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_TEX|INTERP_INDEX)
#define NAME interp_FOG_TEX_INDEX
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_EDGE)
#define NAME interp_RGBA_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_EDGE)
#define NAME interp_RGBA_SPEC_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_FOG|INTERP_EDGE)
#define NAME interp_RGBA_FOG_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_FOG|INTERP_EDGE)
#define NAME interp_RGBA_SPEC_FOG_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_TEX|INTERP_EDGE)
#define NAME interp_RGBA_TEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_TEX|INTERP_EDGE)
#define NAME interp_RGBA_SPEC_TEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_FOG|INTERP_TEX|INTERP_EDGE)
#define NAME interp_RGBA_FOG_TEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC|INTERP_FOG|INTERP_TEX|INTERP_EDGE)
#define NAME interp_RGBA_SPEC_FOG_TEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_INDEX|INTERP_EDGE)
#define NAME interp_INDEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_INDEX|INTERP_EDGE)
#define NAME interp_FOG_INDEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_TEX|INTERP_INDEX|INTERP_EDGE)
#define NAME interp_TEX_INDEX_EDGE
#include "ss_interptmp.h"

#define IND (INTERP_FOG|INTERP_TEX|INTERP_INDEX|INTERP_EDGE)
#define NAME interp_FOG_TEX_INDEX_EDGE
#include "ss_interptmp.h"


#define IND (INTERP_RGBA)
#define NAME copy_RGBA
#include "ss_copytmp.h"

#define IND (INTERP_RGBA|INTERP_SPEC)
#define NAME copy_RGBA_SPEC
#include "ss_copytmp.h"

#define IND (INTERP_INDEX)
#define NAME copy_INDEX
#include "ss_copytmp.h"




static void interp_invalid( GLcontext *ctx,
			    GLfloat t,
			    GLuint dst, GLuint in, GLuint out,
			    GLboolean boundary )
{
   (void)(ctx && t && in && out && boundary);
   fprintf(stderr, "Invalid interpolation function in t_vbrender.c\n");
}

static void copy_invalid( GLcontext *ctx, GLuint dst, GLuint src )
{
   (void)(ctx && dst && src);
   fprintf(stderr, "Invalid copy function in t_vbrender.c\n");
}


void _swsetup_interp_init( GLcontext *ctx )
{
   static int firsttime = 1;
   GLuint i;

   SWSETUP_CONTEXT(ctx)->RenderInterp = _swsetup_validate_interp;
   SWSETUP_CONTEXT(ctx)->RenderCopyPV = _swsetup_validate_copypv;

   if (firsttime == 0)
      return;
   firsttime = 0;

   /* Use the maximal function as the default.  I don't believe any of
    * the non-implemented combinations are reachable, but this gives
    * some safety from crashes.
    */
   for (i = 0 ; i < Elements(interp_tab) ; i++) {
      interp_tab[i] = interp_invalid;
      copy_tab[i] = copy_invalid;
   }

   interp_tab[0] = interp_none;
   interp_tab[INTERP_FOG] = interp_FOG;
   interp_tab[INTERP_TEX] = interp_TEX;
   interp_tab[INTERP_FOG|INTERP_TEX] = interp_FOG_TEX;
   interp_tab[INTERP_EDGE] = interp_EDGE;
   interp_tab[INTERP_FOG|INTERP_EDGE] = interp_FOG_EDGE;
   interp_tab[INTERP_TEX|INTERP_EDGE] = interp_TEX_EDGE;
   interp_tab[INTERP_FOG|INTERP_TEX|INTERP_EDGE] = interp_FOG_TEX_EDGE;

   interp_tab[INTERP_RGBA] = interp_RGBA;
   interp_tab[INTERP_RGBA|INTERP_SPEC] = interp_RGBA_SPEC;
   interp_tab[INTERP_RGBA|INTERP_FOG] = interp_RGBA_FOG;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_FOG] = interp_RGBA_SPEC_FOG;
   interp_tab[INTERP_RGBA|INTERP_TEX] = interp_RGBA_TEX;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_TEX] = interp_RGBA_SPEC_TEX;
   interp_tab[INTERP_RGBA|INTERP_FOG|INTERP_TEX] = interp_RGBA_FOG_TEX;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_FOG|INTERP_TEX] =
      interp_RGBA_SPEC_FOG_TEX;
   interp_tab[INTERP_INDEX] = interp_INDEX;
   interp_tab[INTERP_FOG|INTERP_INDEX] = interp_FOG_INDEX;
   interp_tab[INTERP_TEX|INTERP_INDEX] = interp_TEX_INDEX;
   interp_tab[INTERP_FOG|INTERP_TEX|INTERP_INDEX] = interp_FOG_TEX_INDEX;
   interp_tab[INTERP_RGBA|INTERP_EDGE] = interp_RGBA_EDGE;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_EDGE] = interp_RGBA_SPEC_EDGE;
   interp_tab[INTERP_RGBA|INTERP_FOG|INTERP_EDGE] = interp_RGBA_FOG_EDGE;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_FOG|INTERP_EDGE] =
      interp_RGBA_SPEC_FOG_EDGE;
   interp_tab[INTERP_RGBA|INTERP_TEX|INTERP_EDGE] = interp_RGBA_TEX_EDGE;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_TEX|INTERP_EDGE] =
      interp_RGBA_SPEC_TEX_EDGE;
   interp_tab[INTERP_RGBA|INTERP_FOG|INTERP_TEX|INTERP_EDGE] =
      interp_RGBA_FOG_TEX_EDGE;
   interp_tab[INTERP_RGBA|INTERP_SPEC|INTERP_FOG|INTERP_TEX|INTERP_EDGE] =
      interp_RGBA_SPEC_FOG_TEX_EDGE;
   interp_tab[INTERP_INDEX|INTERP_EDGE] = interp_INDEX_EDGE;
   interp_tab[INTERP_FOG|INTERP_INDEX|INTERP_EDGE] = interp_FOG_INDEX_EDGE;
   interp_tab[INTERP_TEX|INTERP_INDEX|INTERP_EDGE] = interp_TEX_INDEX_EDGE;
   interp_tab[INTERP_FOG|INTERP_TEX|INTERP_INDEX|INTERP_EDGE] =
      interp_FOG_TEX_INDEX_EDGE;


   copy_tab[INTERP_RGBA] = copy_RGBA;
   copy_tab[INTERP_RGBA|INTERP_SPEC] = copy_RGBA_SPEC;
   copy_tab[INTERP_INDEX] = copy_INDEX;
}

static void choose_copy_interp( GLcontext *ctx )
{
   GLuint interp = 0;
   GLuint copy = 0;

   if (ctx->Visual.rgbMode)
   {
      interp |= INTERP_RGBA;

      if (ctx->_TriangleCaps & DD_SEPARATE_SPECULAR) {
         interp |= INTERP_SPEC;
      }

      if (ctx->Texture._ReallyEnabled) {
	 interp |= INTERP_TEX;
      }
   }
   else {
      interp |= INTERP_INDEX;
   }

   if (ctx->Fog.Enabled) {
      interp |= INTERP_FOG;
   }

   if (ctx->_TriangleCaps & DD_TRI_UNFILLED) {
      interp |= INTERP_EDGE;
   }

   if (ctx->RenderMode==GL_FEEDBACK) {
      interp |= INTERP_TEX;
   }

   if (ctx->_TriangleCaps & DD_FLATSHADE) {
      copy = interp & (INTERP_RGBA|INTERP_SPEC|INTERP_INDEX);
      interp &= ~copy;
   }

   SWSETUP_CONTEXT(ctx)->RenderCopyPV = copy_tab[copy];
   SWSETUP_CONTEXT(ctx)->RenderInterp = interp_tab[interp];
}


void _swsetup_validate_interp( GLcontext *ctx, GLfloat t,
				GLuint dst, GLuint out, GLuint in,
				GLboolean force_boundary )
{
   choose_copy_interp( ctx );
   SWSETUP_CONTEXT(ctx)->RenderInterp( ctx, t, dst, out, in, force_boundary );
}

void _swsetup_validate_copypv( GLcontext *ctx, GLuint dst, GLuint src )
{
   choose_copy_interp( ctx );
   SWSETUP_CONTEXT(ctx)->RenderCopyPV( ctx, dst, src );
}

void _swsetup_RenderProjectInterpVerts( GLcontext *ctx )
{
   TNLcontext *tnl = TNL_CONTEXT(ctx);
   struct vertex_buffer *VB = &tnl->vb;


   /* Populate VB->ProjectedClipPtr if necessary.
    */
   if (VB->ProjectedClipPtr) {
      GLfloat (*clip)[4] = VB->ClipPtr->data;
      GLfloat (*proj)[4] = VB->ProjectedClipPtr->data;
      GLuint last = VB->LastClipped;
      GLuint size = VB->ClipPtr->size;
      GLuint i;

      if (size == 4) {
	 for (i = VB->FirstClipped; i < last; i++) {
	    if (VB->ClipMask[i] == 0 && clip[i][3] != 0.0F) {
	       GLfloat wInv = 1.0F / clip[i][3];
	       proj[i][0] = clip[i][0] * wInv;
	       proj[i][1] = clip[i][1] * wInv;
	       proj[i][2] = clip[i][2] * wInv;
	       proj[i][3] = wInv;
	    }
	 }
      } else if (VB->ClipPtr != VB->ProjectedClipPtr) {
	 for (i = VB->FirstClipped; i < last; i++) {
	    COPY_4FV( proj[i], clip[i] );
	 }
      }
   }

   tnl->Driver.BuildProjectedVertices(ctx,
				      VB->FirstClipped,
				      VB->LastClipped,
				      ~0);
}

void _swsetup_RenderClippedPolygon( GLcontext *ctx, const GLuint *elts,
				    GLuint n )
{
   TNLcontext *tnl = TNL_CONTEXT(ctx);
   struct vertex_buffer *VB = &tnl->vb;

   _swsetup_RenderProjectInterpVerts( ctx );

   /* Render the new vertices as an unclipped polygon.
    */
   {
      GLuint *tmp = VB->Elts;
      VB->Elts = (GLuint *)elts;
      tnl->Driver.RenderTabElts[GL_POLYGON]( ctx, 0, n, PRIM_BEGIN|PRIM_END );
      VB->Elts = tmp;
   }
}

void _swsetup_RenderClippedLine( GLcontext *ctx, GLuint ii, GLuint jj )
{
   TNLcontext *tnl = TNL_CONTEXT(ctx);
   _swsetup_RenderProjectInterpVerts( ctx );
   tnl->Driver.LineFunc( ctx, ii, jj );
}
