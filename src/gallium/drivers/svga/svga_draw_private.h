/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#ifndef SVGA_DRAW_H_
#define SVGA_DRAW_H_

#include "pipe/p_compiler.h"
#include "pipe/p_defines.h"
#include "indices/u_indices.h"
#include "svga_hw_reg.h"
#include "svga3d_shaderdefs.h"

struct svga_context;
struct u_upload_mgr;

/**
 * Mask indicating which types of gallium primitives are actually
 * handled by the svga device.  Other types will be converted to
 * these types by the index/translation code.
 */
static const unsigned svga_hw_prims = 
   ((1 << PIPE_PRIM_POINTS) |
    (1 << PIPE_PRIM_LINES) |
    (1 << PIPE_PRIM_LINE_STRIP) |
    (1 << PIPE_PRIM_TRIANGLES) |
    (1 << PIPE_PRIM_TRIANGLE_STRIP) |
    (1 << PIPE_PRIM_TRIANGLE_FAN) |
    (1 << PIPE_PRIM_LINES_ADJACENCY) |
    (1 << PIPE_PRIM_LINE_STRIP_ADJACENCY) |
    (1 << PIPE_PRIM_TRIANGLES_ADJACENCY) |
    (1 << PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY));


/**
 * Translate a gallium PIPE_PRIM_x value to an SVGA3D_PRIMITIVE_x value.
 * Also, compute the number of primitives that'll be drawn given a
 * vertex count.
 * Note that this function doesn't have to handle PIPE_PRIM_LINE_LOOP,
 * PIPE_PRIM_QUADS, PIPE_PRIM_QUAD_STRIP or PIPE_PRIM_POLYGON.  We convert
 * those to other types of primitives with index/translation code.
 */
static inline SVGA3dPrimitiveType
svga_translate_prim(unsigned mode, unsigned vcount, unsigned *prim_count)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      *prim_count = vcount;
      return SVGA3D_PRIMITIVE_POINTLIST;

   case PIPE_PRIM_LINES:
      *prim_count = vcount / 2;
      return SVGA3D_PRIMITIVE_LINELIST; 

   case PIPE_PRIM_LINE_STRIP:
      *prim_count = vcount - 1;
      return SVGA3D_PRIMITIVE_LINESTRIP; 

   case PIPE_PRIM_TRIANGLES:
      *prim_count = vcount / 3;
      return SVGA3D_PRIMITIVE_TRIANGLELIST; 

   case PIPE_PRIM_TRIANGLE_STRIP:
      *prim_count = vcount - 2;
      return SVGA3D_PRIMITIVE_TRIANGLESTRIP; 

   case PIPE_PRIM_TRIANGLE_FAN:
      *prim_count = vcount - 2;
      return SVGA3D_PRIMITIVE_TRIANGLEFAN; 

   case PIPE_PRIM_LINES_ADJACENCY:
      *prim_count = vcount / 4;
      return SVGA3D_PRIMITIVE_LINELIST_ADJ;

   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      *prim_count = vcount - 3;
      return SVGA3D_PRIMITIVE_LINESTRIP_ADJ;

   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      *prim_count = vcount / 6;
      return SVGA3D_PRIMITIVE_TRIANGLELIST_ADJ;

   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      *prim_count = vcount / 2 - 2 ;
      return SVGA3D_PRIMITIVE_TRIANGLESTRIP_ADJ;

   default:
      assert(0);
      *prim_count = 0;
      return 0;
   }
}


struct index_cache {
   u_generate_func generate;
   unsigned gen_nr;

   /* If non-null, this buffer is filled by calling 
    *   generate(nr, map(buffer))
    */
   struct pipe_resource *buffer;
};


/** Max number of primitives per draw call */
#define QSZ SVGA3D_MAX_DRAW_PRIMITIVE_RANGES

struct draw_cmd {
   struct svga_winsys_context *swc;

   /* vertex layout info */
   SVGA3dVertexDecl vdecl[SVGA3D_INPUTREG_MAX];
   unsigned vdecl_count;
   SVGA3dElementLayoutId vdecl_layout_id;
   unsigned vdecl_buffer_index[SVGA3D_INPUTREG_MAX];

   /* vertex buffer info */
   struct pipe_vertex_buffer vbufs[SVGA3D_INPUTREG_MAX];
   unsigned vbuf_count;

   SVGA3dPrimitiveRange prim[QSZ];
   struct pipe_resource *prim_ib[QSZ];
   unsigned prim_count;   /**< number of primitives for this draw */
   unsigned min_index[QSZ];
   unsigned max_index[QSZ];
};

#define IDX_CACHE_MAX  8

struct svga_hwtnl {
   struct svga_context *svga;
   struct u_upload_mgr *upload_ib;

   /* Additional negative index bias due to partial buffer uploads
    * This is compensated for in the offset associated with all
    * vertex buffers.
    */

   int index_bias;
   
   /* Flatshade information:
    */
   unsigned api_pv;
   unsigned hw_pv;
   unsigned api_fillmode;

   /* Cache the results of running a particular generate func on each
    * primitive type.
    */
   struct index_cache index_cache[PIPE_PRIM_MAX][IDX_CACHE_MAX];

   /* Try to build the maximal draw command packet before emitting:
    */
   struct draw_cmd cmd;
};



/***********************************************************************
 * Internal functions
 */
enum pipe_error 
svga_hwtnl_prim( struct svga_hwtnl *hwtnl,
                 const SVGA3dPrimitiveRange *range,
                 unsigned vcount,
                 unsigned min_index,
                 unsigned max_index,
                 struct pipe_resource *ib,
                 unsigned start_instance, unsigned instance_count);

enum pipe_error
svga_hwtnl_simple_draw_range_elements( struct svga_hwtnl *hwtnl,
                                       struct pipe_resource *indexBuffer,
                                       unsigned index_size,
                                       int index_bias,
                                       unsigned min_index,
                                       unsigned max_index,
                                       unsigned prim, 
                                       unsigned start,
                                       unsigned count,
                                       unsigned start_instance,
                                       unsigned instance_count);


#endif
