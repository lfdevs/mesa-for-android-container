/* $Id: ss_copytmp.h,v 1.4 2001/04/28 08:39:18 keithw Exp $ */

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


static void NAME( GLcontext *ctx, GLuint dst, GLuint src )
{
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;

   /* For flatshading, copy the 'out' values to the new vertex.  This
    * guarentees that if the pv is clipped away, its colors are copied
    * to any vertex that replaces it.
    */
   if (IND & INTERP_RGBA) {
      COPY_4FV( GET_COLOR(VB->ColorPtr[0], dst), 
		GET_COLOR(VB->ColorPtr[0], src) );

      if (VB->ColorPtr[1]) {
	 COPY_4FV( GET_COLOR(VB->ColorPtr[1], dst), 
		   GET_COLOR(VB->ColorPtr[1], src) );
      }
   }

   if (IND & INTERP_SPEC) {
      COPY_4FV( GET_COLOR(VB->SecondaryColorPtr[0], dst), 
		GET_COLOR(VB->SecondaryColorPtr[0], src) );

      if (VB->SecondaryColorPtr[1]) {
	 COPY_4FV( GET_COLOR(VB->SecondaryColorPtr[1], dst), 
		   GET_COLOR(VB->SecondaryColorPtr[1], src) );
      }
   }

   if (IND & INTERP_INDEX) {
      VB->IndexPtr[0]->data[dst] = VB->IndexPtr[0]->data[src];

      if (VB->IndexPtr[1])
	 VB->IndexPtr[1]->data[dst] = VB->IndexPtr[1]->data[src];
   }

}


#undef IND
#undef NAME
