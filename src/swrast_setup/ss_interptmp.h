/* $Id: ss_interptmp.h,v 1.3 2001/04/28 08:39:18 keithw Exp $ */

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
 *
 * Authors:
 *    Keith Whitwell <keithw@valinux.com>
 */

static void NAME( GLcontext *ctx,
		  GLfloat t,
		  GLuint dst, GLuint out, GLuint in,
		  GLboolean force_boundary )
{
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;

   if (IND & INTERP_RGBA) {
      ASSERT(VB->ColorPtr[0]->Ptr != ctx->Array.Color.Ptr);
      INTERP_4F( t,
		  GET_COLOR(VB->ColorPtr[0], dst),
		  GET_COLOR(VB->ColorPtr[0], out),
		  GET_COLOR(VB->ColorPtr[0], in) );
      
      if (ctx->_TriangleCaps & DD_TRI_LIGHT_TWOSIDE) {
	 INTERP_4F( t,
		     GET_COLOR(VB->ColorPtr[1], dst),
		     GET_COLOR(VB->ColorPtr[1], out),
		     GET_COLOR(VB->ColorPtr[1], in) );
      }

      if (IND & INTERP_SPEC) {
	 INTERP_3F( t,
		     GET_COLOR(VB->SecondaryColorPtr[0], dst),
		     GET_COLOR(VB->SecondaryColorPtr[0], out),
		     GET_COLOR(VB->SecondaryColorPtr[0], in) );

	 if (ctx->_TriangleCaps & DD_TRI_LIGHT_TWOSIDE) {
	    INTERP_3F( t,
			GET_COLOR(VB->SecondaryColorPtr[1], dst),
			GET_COLOR(VB->SecondaryColorPtr[1], out),
			GET_COLOR(VB->SecondaryColorPtr[1], in) );
	 }
      }
   }

   if (IND & INTERP_INDEX) {
      VB->IndexPtr[0]->data[dst] = (GLuint) (GLint)
	 LINTERP( t,
		  (GLfloat) VB->IndexPtr[0]->data[out],
		  (GLfloat) VB->IndexPtr[0]->data[in] );

      if (ctx->_TriangleCaps & DD_TRI_LIGHT_TWOSIDE)
	 VB->IndexPtr[1]->data[dst] = (GLuint) (GLint)
	    LINTERP( t,
		     (GLfloat) VB->IndexPtr[1]->data[out],
		     (GLfloat) VB->IndexPtr[1]->data[in] );
   }

   if (IND & INTERP_TEX) {
      GLuint i;
      for (i = 0 ; i < ctx->Const.MaxTextureUnits ; i++) {
	 if (ctx->Texture.Unit[i]._ReallyEnabled) {
	    INTERP_SZ( t,
		       VB->TexCoordPtr[i]->data,
		       dst, out, in,
		       VB->TexCoordPtr[i]->size );
	 }
      }
   }

   if (IND & INTERP_FOG) {
      VB->FogCoordPtr->data[dst] =
	 LINTERP( t,
		  VB->FogCoordPtr->data[out],
		  VB->FogCoordPtr->data[in] );
   }


   if (IND & INTERP_EDGE) {
      VB->EdgeFlag[dst] = VB->EdgeFlag[out] || force_boundary;
   }
}


#undef IND
#undef NAME
