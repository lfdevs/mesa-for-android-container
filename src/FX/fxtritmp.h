
#if (IND & FX_FALLBACK_BIT)
#define TRI(ctx, v0, v1, v2) fxMesa->draw_tri(ctx, v0, v1, v2)
#define LINE(ctx, v0, v1) fxMesa->draw_line(ctx, v0, v1)
#define POINT(ctx, v0) fxMesa->draw_point(ctx, v0)
#else
#define TRI(ctx, v0, v1, v2) grDrawTriangle(&(v0->v), &(v1->v), &(v2->v))
#define LINE(ctx, v0, v1) grDrawLine(&(v0->v), &(v1->v))
#define POINT(ctx, v0) grDrawPoint(&((v0)->v))
#endif

static __inline void TAG(triangle) (GLcontext * ctx,
				    GLuint e0, GLuint e1, GLuint e2)
{
   fxMesaContext fxMesa = FX_CONTEXT(ctx);
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;
   fxVertex *fxVB = fxMesa->verts;
   fxVertex *v[3];
   GLfloat offset;
   GLfloat z[3];
   GLenum mode = GL_FILL;

   v[0] = &fxVB[e0];
   v[1] = &fxVB[e1];
   v[2] = &fxVB[e2];


   if (IND & (FX_TWOSIDE_BIT | FX_OFFSET_BIT | FX_UNFILLED_BIT)) {
      GLfloat ex = v[0]->v.x - v[2]->v.x;
      GLfloat ey = v[0]->v.y - v[2]->v.y;
      GLfloat fx = v[1]->v.x - v[2]->v.x;
      GLfloat fy = v[1]->v.y - v[2]->v.y;
      GLfloat cc = ex * fy - ey * fx;

      if (IND & (FX_TWOSIDE_BIT | FX_UNFILLED_BIT)) {
	 GLuint facing = (cc < 0.0) ^ ctx->Polygon._FrontBit;

	 if (IND & FX_UNFILLED_BIT) {
	    if (facing) {
	       mode = ctx->Polygon.BackMode;
	       if (ctx->Polygon.CullFlag &&
		   ctx->Polygon.CullFaceMode != GL_FRONT) {
		  return;
	       }
	    }
	    else {
	       mode = ctx->Polygon.FrontMode;
	       if (ctx->Polygon.CullFlag &&
		   ctx->Polygon.CullFaceMode != GL_BACK) {
		  return;
	       }
	    }
	 }

	 if (IND & FX_TWOSIDE_BIT) {
	    GLfloat (*vbcolor)[4] = VB->ColorPtr[facing]->Ptr;
	    if (IND & FX_FLAT_BIT) {
	       FX_COLOR(v[0], vbcolor[e2]);
	       FX_COPY_COLOR(v[1], v[0]);
	       FX_COPY_COLOR(v[2], v[0]);
	    }
	    else {
	       FX_COLOR(v[0], vbcolor[e0]);
	       FX_COLOR(v[1], vbcolor[e1]);
	       FX_COLOR(v[2], vbcolor[e2]);
	    }
	 }
      }


      if (IND & FX_OFFSET_BIT) {
	 offset = ctx->Polygon.OffsetUnits;
	 z[0] = v[0]->v.ooz;
	 z[1] = v[1]->v.ooz;
	 z[2] = v[2]->v.ooz;
	 if (cc * cc > 1e-16) {
	    GLfloat ez = z[0] - z[2];
	    GLfloat fz = z[1] - z[2];
	    GLfloat a = ey * fz - ez * fy;
	    GLfloat b = ez * fx - ex * fz;
	    GLfloat ic = 1.0 / cc;
	    GLfloat ac = a * ic;
	    GLfloat bc = b * ic;
	    if (ac < 0.0f)
	       ac = -ac;
	    if (bc < 0.0f)
	       bc = -bc;
	    offset += MAX2(ac, bc) * ctx->Polygon.OffsetFactor;
	 }
      }
   }
   else if (IND & FX_FLAT_BIT) {
      GLfloat (*vbcolor)[4] = VB->ColorPtr[0]->Ptr;
      FX_COLOR(v[0], vbcolor[e2]);
      FX_COPY_COLOR(v[1], v[0]);
      FX_COPY_COLOR(v[2], v[0]);
   }

   if (mode == GL_POINT) {
      GLubyte *ef = VB->EdgeFlag;
      if ((IND & FX_OFFSET_BIT) && ctx->Polygon.OffsetPoint) {
	 v[0]->v.ooz += offset;
	 v[1]->v.ooz += offset;
	 v[2]->v.ooz += offset;
      }
      if (ef[e0])
	 POINT(ctx, v[0]);
      if (ef[e1])
	 POINT(ctx, v[1]);
      if (ef[e2])
	 POINT(ctx, v[2]);
   }
   else if (mode == GL_LINE) {
      GLubyte *ef = VB->EdgeFlag;
      if ((IND & FX_OFFSET_BIT) && ctx->Polygon.OffsetLine) {
	 v[0]->v.ooz += offset;
	 v[1]->v.ooz += offset;
	 v[2]->v.ooz += offset;
      }
      /* Have to do some additional processing for this case:
       */
      if (fxMesa->render_prim == GL_POLYGON) {
	 if (ef[e2])
	    LINE(ctx, v[2], v[0]);	/* rotate vertices */
	 if (ef[e0])
	    LINE(ctx, v[0], v[1]);
	 if (ef[e1])
	    LINE(ctx, v[1], v[2]);
      }
      else {
	 if (ef[e0])
	    LINE(ctx, v[0], v[1]);
	 if (ef[e1])
	    LINE(ctx, v[1], v[2]);
	 if (ef[e2])
	    LINE(ctx, v[2], v[0]);
      }
   }
   else {
      if ((IND & FX_OFFSET_BIT) && ctx->Polygon.OffsetFill) {
	 v[0]->v.ooz += offset;
	 v[1]->v.ooz += offset;
	 v[2]->v.ooz += offset;
      }
      TRI(ctx, v[0], v[1], v[2]);
   }

   if (IND & FX_OFFSET_BIT) {
      v[0]->v.ooz = z[0];
      v[1]->v.ooz = z[1];
      v[2]->v.ooz = z[2];
   }

   /* Color is not restored.  
    */
}

static __inline void TAG(quad) (GLcontext * ctx,
				GLuint e0, GLuint e1, GLuint e2, GLuint e3)
{
   fxMesaContext fxMesa = FX_CONTEXT(ctx);
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;
   fxVertex *fxVB = fxMesa->verts;
   fxVertex *v[4];
   GLfloat offset;
   GLfloat z[4];
   GLenum mode = GL_FILL;

   v[0] = &fxVB[e0];
   v[1] = &fxVB[e1];
   v[2] = &fxVB[e2];
   v[3] = &fxVB[e3];

   if (IND & (FX_TWOSIDE_BIT | FX_OFFSET_BIT | FX_UNFILLED_BIT)) {
      GLfloat ex = v[2]->v.x - v[0]->v.x;
      GLfloat ey = v[2]->v.y - v[0]->v.y;
      GLfloat fx = v[3]->v.x - v[1]->v.x;
      GLfloat fy = v[3]->v.y - v[1]->v.y;
      GLfloat cc = ex * fy - ey * fx;

      if (IND & (FX_TWOSIDE_BIT | FX_UNFILLED_BIT)) {
	 GLuint facing = (cc < 0.0) ^ ctx->Polygon._FrontBit;

	 if (IND & FX_UNFILLED_BIT) {
	    if (facing) {
	       mode = ctx->Polygon.BackMode;
	       if (ctx->Polygon.CullFlag &&
		   ctx->Polygon.CullFaceMode != GL_FRONT) {
		  return;
	       }
	    }
	    else {
	       mode = ctx->Polygon.FrontMode;
	       if (ctx->Polygon.CullFlag &&
		   ctx->Polygon.CullFaceMode != GL_BACK) {
		  return;
	       }
	    }
	 }

	 if (IND & FX_TWOSIDE_BIT) {
	    GLfloat (*vbcolor)[4] = VB->ColorPtr[facing]->Ptr;
	    if (IND & FX_FLAT_BIT) {
	       FX_COLOR(v[0], vbcolor[e3]);
	       FX_COPY_COLOR(v[1], v[0]);
	       FX_COPY_COLOR(v[2], v[0]);
	       FX_COPY_COLOR(v[3], v[0]);
	    }
	    else {
	       FX_COLOR(v[0], vbcolor[e0]);
	       FX_COLOR(v[1], vbcolor[e1]);
	       FX_COLOR(v[2], vbcolor[e2]);
	       FX_COLOR(v[3], vbcolor[e3]);
	    }
	 }
      }


      if (IND & FX_OFFSET_BIT) {
	 offset = ctx->Polygon.OffsetUnits;
	 z[0] = v[0]->v.ooz;
	 z[1] = v[1]->v.ooz;
	 z[2] = v[2]->v.ooz;
	 z[3] = v[3]->v.ooz;
	 if (cc * cc > 1e-16) {
	    GLfloat ez = v[2]->v.ooz - v[0]->v.ooz;
	    GLfloat fz = v[3]->v.ooz - v[1]->v.ooz;
	    GLfloat a = ey * fz - ez * fy;
	    GLfloat b = ez * fx - ex * fz;
	    GLfloat ic = 1.0 / cc;
	    GLfloat ac = a * ic;
	    GLfloat bc = b * ic;
	    if (ac < 0.0f)
	       ac = -ac;
	    if (bc < 0.0f)
	       bc = -bc;
	    offset += MAX2(ac, bc) * ctx->Polygon.OffsetFactor;
	 }
      }
   }
   else if (IND & FX_FLAT_BIT) {
      GLfloat (*vbcolor)[4] = VB->ColorPtr[0]->Ptr;
      FX_COLOR(v[0], vbcolor[e3]);
      FX_COLOR(v[1], vbcolor[e3]);
      FX_COLOR(v[2], vbcolor[e3]);
      FX_COLOR(v[3], vbcolor[e3]);
/*        FX_COPY_COLOR( v[1], v[0] ); */
/*        FX_COPY_COLOR( v[2], v[0] ); */
/*        FX_COPY_COLOR( v[3], v[0] ); */
   }

   if (mode == GL_POINT) {
      GLubyte *ef = VB->EdgeFlag;
      if ((IND & FX_OFFSET_BIT) && ctx->Polygon.OffsetPoint) {
	 v[0]->v.ooz += offset;
	 v[1]->v.ooz += offset;
	 v[2]->v.ooz += offset;
	 v[3]->v.ooz += offset;
      }
      if (ef[e0])
	 POINT(ctx, v[0]);
      if (ef[e1])
	 POINT(ctx, v[1]);
      if (ef[e2])
	 POINT(ctx, v[2]);
      if (ef[e3])
	 POINT(ctx, v[3]);
   }
   else if (mode == GL_LINE) {
      GLubyte *ef = VB->EdgeFlag;
      if ((IND & FX_OFFSET_BIT) && ctx->Polygon.OffsetLine) {
	 v[0]->v.ooz += offset;
	 v[1]->v.ooz += offset;
	 v[2]->v.ooz += offset;
	 v[3]->v.ooz += offset;
      }
      if (ef[e0])
	 LINE(ctx, v[0], v[1]);
      if (ef[e1])
	 LINE(ctx, v[1], v[2]);
      if (ef[e2])
	 LINE(ctx, v[2], v[3]);
      if (ef[e2])
	 LINE(ctx, v[3], v[0]);
   }
   else {
      if ((IND & FX_OFFSET_BIT) && ctx->Polygon.OffsetFill) {
	 v[0]->v.ooz += offset;
	 v[1]->v.ooz += offset;
	 v[2]->v.ooz += offset;
	 v[3]->v.ooz += offset;
      }
      TRI(ctx, v[0], v[1], v[3]);
      TRI(ctx, v[1], v[2], v[3]);
   }

   if (IND & FX_OFFSET_BIT) {
      v[0]->v.ooz = z[0];
      v[1]->v.ooz = z[1];
      v[2]->v.ooz = z[2];
      v[3]->v.ooz = z[3];
   }

   /* Color is not restored.  
    */
}


static __inline void TAG(line) (GLcontext * ctx, GLuint v0, GLuint v1)
{
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;
   fxMesaContext fxMesa = FX_CONTEXT(ctx);
   fxVertex *fxVB = fxMesa->verts;
   fxVertex *vert0 = &fxVB[v0];
   fxVertex *vert1 = &fxVB[v1];

   if (IND & FX_FLAT_BIT) {
      GLfloat (*vbcolor)[4] = VB->ColorPtr[0]->Ptr;
      FX_COLOR(vert0, vbcolor[v1]);
      FX_COPY_COLOR(vert1, vert0);
   }
   else if (IND & FX_TWOSIDE_BIT) {
      /* Have to do this because vertex colors aren't initialized in
       * two-sided lighting.  However, lines at this level are always
       * front-facing.
       */
      GLfloat (*vbcolor)[4] = VB->ColorPtr[0]->Ptr;
      FX_COLOR(vert0, vbcolor[v0]);
      FX_COLOR(vert1, vbcolor[v1]);
   }

   LINE(ctx, vert0, vert1);

   /* Color is not restored.
    */
}


static __inline void TAG(points) (GLcontext * ctx, GLuint first, GLuint last)
{
   fxMesaContext fxMesa = FX_CONTEXT(ctx);
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;
   fxVertex *fxVB = fxMesa->verts;
   int i;

   if (VB->Elts == 0) {
      for (i = first; i < last; i++) {
	 if (VB->ClipMask[i] == 0) {
	    if (IND & (FX_FLAT_BIT | FX_TWOSIDE_BIT)) {
	       /* Have to do this because vertex colors aren't
	        * initialized in flat or two-sided lighting.  However,
	        * points at this level are always front-facing.  
	        */
	       GLfloat (*vbcolor)[4] = VB->ColorPtr[0]->Ptr;
	       FX_COLOR((&fxVB[i]), vbcolor[i]);
	    }

	    POINT(ctx, &fxVB[i]);
	 }
      }
   }
   else {
      for (i = first; i < last; i++) {
	 GLuint e = VB->Elts[i];
	 if (VB->ClipMask[e] == 0) {
	    if (IND & (FX_FLAT_BIT | FX_TWOSIDE_BIT)) {
	       GLfloat (*vbcolor)[4] = VB->ColorPtr[0]->Ptr;
	       FX_COLOR((&fxVB[e]), vbcolor[e]);
	    }
	    POINT(ctx, &fxVB[e]);
	 }
      }
   }
}




static void TAG(init) (void)
{
   rast_tab[IND].triangle = TAG(triangle);
   rast_tab[IND].quad = TAG(quad);
   rast_tab[IND].line = TAG(line);
   rast_tab[IND].points = TAG(points);
}

#undef TRI
#undef LINE
#undef POINT
#undef IND
#undef TAG
