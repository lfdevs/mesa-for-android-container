/* DO NOT EDIT - This file generated automatically by remap_helper.py (from Mesa) script */

/*
 * Copyright (C) 2009 Chia-I Wu <olv@0xlab.org>
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * Chia-I Wu,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "main/dispatch.h"
#include "main/remap.h"

/* this is internal to remap.c */
#ifndef need_MESA_remap_table
#error Only remap.c should include this file!
#endif /* need_MESA_remap_table */


static const char _mesa_function_pool[] =
   /* _mesa_function_pool[0]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[25]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[49]: MapBufferOES (will be remapped) */
   "ii\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[68]: MultMatrixxOES (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[102]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[123]: BindRenderbufferOES (will be remapped) */
   "ii\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[149]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[166]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[191]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[212]: MaterialxvOES (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[246]: IsFramebufferOES (will be remapped) */
   "i\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[268]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[311]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[326]: GetBufferPointervOES (will be remapped) */
   "iip\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[354]: DepthRangexOES (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[389]: FramebufferTexture2DOES (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[422]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[433]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[451]: GetTexGenfvOES (offset 279) */
   "iip\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[473]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "\0"
   /* _mesa_function_pool[490]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[506]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[524]: LineWidthxOES (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[556]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[572]: TexGenfOES (offset 190) */
   "iif\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[590]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[611]: BlendFuncSeparateOES (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[640]: IsEnabled (offset 286) */
   "i\0"
   "glIsEnabled\0"
   "\0"
   /* _mesa_function_pool[655]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[671]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[689]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[700]: TexGenfvOES (offset 191) */
   "iip\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[719]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[741]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "\0"
   /* _mesa_function_pool[771]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "\0"
   /* _mesa_function_pool[792]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[814]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[832]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[853]: PointSizexOES (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[885]: TexParameterxvOES (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[927]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "\0"
   /* _mesa_function_pool[941]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[959]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[972]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[984]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[1003]: TexEnvxvOES (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[1033]: TexEnvxOES (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[1061]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "\0"
   /* _mesa_function_pool[1080]: BlendEquationOES (offset 337) */
   "i\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[1102]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "\0"
   /* _mesa_function_pool[1117]: Color4xOES (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[1146]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "\0"
   /* _mesa_function_pool[1173]: LightxvOES (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[1201]: ClipPlanexOES (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[1234]: LightxOES (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[1260]: GetFixedvOES (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[1291]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1312]: GetTexEnvxvOES (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[1348]: MultiTexCoord4f (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[1373]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1390]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[1403]: RenderbufferStorageOES (will be remapped) */
   "iiii\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[1434]: AlphaFuncxOES (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[1467]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[1486]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "\0"
   /* _mesa_function_pool[1523]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[1539]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[1556]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[1589]: LightModelxOES (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[1624]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[1654]: OrthoxOES (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[1683]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[1698]: SampleCoveragexOES (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[1741]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[1765]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[1782]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[1797]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[1813]: FrustumxOES (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[1846]: BindFramebufferOES (will be remapped) */
   "ii\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[1871]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[1892]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[1910]: GetClipPlanefOES (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[1949]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[1963]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[1986]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[2003]: MaterialxOES (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[2035]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[2069]: GetMaterialxvOES (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[2109]: IsRenderbufferOES (will be remapped) */
   "i\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[2132]: MultiTexCoord4xOES (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[2178]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[2192]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "\0"
   /* _mesa_function_pool[2210]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[2223]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[2241]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "\0"
   /* _mesa_function_pool[2260]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[2275]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[2294]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[2311]: GetFramebufferAttachmentParameterivOES (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[2358]: PointParameterxOES (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[2401]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[2423]: GenRenderbuffersOES (will be remapped) */
   "ip\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[2449]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[2466]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[2487]: DeleteRenderbuffersOES (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[2516]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "\0"
   /* _mesa_function_pool[2534]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[2556]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[2577]: TranslatexOES (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[2611]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[2629]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[2648]: FrustumfOES (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[2681]: PolygonOffsetxOES (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[2722]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[2741]: ClearColorxOES (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[2778]: ClearDepthxOES (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[2812]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "\0"
   /* _mesa_function_pool[2830]: GetTexParameterxvOES (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[2878]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "\0"
   /* _mesa_function_pool[2900]: GetLightxvOES (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[2934]: GenFramebuffersOES (will be remapped) */
   "ip\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[2959]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[2975]: OrthofOES (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[3004]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[3020]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[3036]: RotatexOES (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[3065]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "\0"
   /* _mesa_function_pool[3098]: TexGeniOES (offset 192) */
   "iii\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[3116]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[3130]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[3152]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[3162]: UnmapBufferOES (will be remapped) */
   "i\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[3182]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[3200]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[3216]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[3234]: BlendEquationSeparateOES (will be remapped) */
   "ii\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[3265]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[3287]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "\0"
   /* _mesa_function_pool[3307]: Normal3xOES (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[3337]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[3356]: FogxOES (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[3377]: TexGenivOES (offset 193) */
   "iip\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[3396]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[3417]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[3438]: FogxvOES (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[3461]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "\0"
   /* _mesa_function_pool[3483]: GetRenderbufferParameterivOES (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[3520]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "\0"
   /* _mesa_function_pool[3543]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[3558]: TexParameterxOES (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[3598]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[3613]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[3637]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[3655]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[3673]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[3691]: DeleteFramebuffersOES (will be remapped) */
   "ip\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[3719]: CheckFramebufferStatusOES (will be remapped) */
   "i\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[3750]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[3765]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "\0"
   /* _mesa_function_pool[3786]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "\0"
   /* _mesa_function_pool[3804]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[3839]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[3855]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "\0"
   /* _mesa_function_pool[3872]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[3890]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[3901]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[3915]: GetTexGenivOES (offset 280) */
   "iip\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[3937]: ClipPlanefOES (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[3970]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[3986]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[3997]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[4009]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[4029]: PointParameterxvOES (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[4074]: ScalexOES (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[4100]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[4128]: MultiDrawArraysEXT (will be remapped) */
   "ippi\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[4155]: FramebufferRenderbufferOES (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[4190]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "\0"
   /* _mesa_function_pool[4215]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[4230]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[4245]: GetClipPlanexOES (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[4284]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[4301]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "\0"
   /* _mesa_function_pool[4328]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[4343]: LoadMatrixxOES (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[4377]: LightModelxvOES (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[4414]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[4441]: GenerateMipmapOES (will be remapped) */
   "i\0"
   "glGenerateMipmapOES\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   {   771, SampleCoverage_remap_index },
   {  3065, CompressedTexImage2D_remap_index },
   {  1486, CompressedTexSubImage2D_remap_index },
   {  3855, BindBuffer_remap_index },
   {  1061, BufferData_remap_index },
   {  3461, BufferSubData_remap_index },
   {  3287, DeleteBuffers_remap_index },
   {   473, GenBuffers_remap_index },
   {  4100, GetBufferParameteriv_remap_index },
   {   326, GetBufferPointervOES_remap_index },
   {   927, IsBuffer_remap_index },
   {    49, MapBufferOES_remap_index },
   {  3162, UnmapBufferOES_remap_index },
   {  2035, ClearDepthf_remap_index },
   {  3804, DepthRangef_remap_index },
   {   102, DrawTexfOES_remap_index },
   {  1892, DrawTexfvOES_remap_index },
   {  1291, DrawTexiOES_remap_index },
   {   506, DrawTexivOES_remap_index },
   {  1871, DrawTexsOES_remap_index },
   {  3655, DrawTexsvOES_remap_index },
   {  2466, DrawTexxOES_remap_index },
   {  2611, DrawTexxvOES_remap_index },
   {  4414, PointSizePointerOES_remap_index },
   {  3130, QueryMatrixxOES_remap_index },
   {  2878, PointParameterf_remap_index },
   {  3520, PointParameterfv_remap_index },
   {  4128, MultiDrawArraysEXT_remap_index },
   {  1624, MultiDrawElementsEXT_remap_index },
   {   611, BlendFuncSeparateOES_remap_index },
   {  1434, AlphaFuncxOES_remap_index },
   {  2741, ClearColorxOES_remap_index },
   {  2778, ClearDepthxOES_remap_index },
   {  1201, ClipPlanexOES_remap_index },
   {  1117, Color4xOES_remap_index },
   {   354, DepthRangexOES_remap_index },
   {  3356, FogxOES_remap_index },
   {  3438, FogxvOES_remap_index },
   {  1813, FrustumxOES_remap_index },
   {  4245, GetClipPlanexOES_remap_index },
   {  1260, GetFixedvOES_remap_index },
   {  2900, GetLightxvOES_remap_index },
   {  2069, GetMaterialxvOES_remap_index },
   {  1312, GetTexEnvxvOES_remap_index },
   {   719, GetTexGenxvOES_remap_index },
   {  2830, GetTexParameterxvOES_remap_index },
   {  1589, LightModelxOES_remap_index },
   {  4377, LightModelxvOES_remap_index },
   {  1234, LightxOES_remap_index },
   {  1173, LightxvOES_remap_index },
   {   524, LineWidthxOES_remap_index },
   {  4343, LoadMatrixxOES_remap_index },
   {  2003, MaterialxOES_remap_index },
   {   212, MaterialxvOES_remap_index },
   {    68, MultMatrixxOES_remap_index },
   {  2132, MultiTexCoord4xOES_remap_index },
   {  3307, Normal3xOES_remap_index },
   {  1654, OrthoxOES_remap_index },
   {  2358, PointParameterxOES_remap_index },
   {  4029, PointParameterxvOES_remap_index },
   {   853, PointSizexOES_remap_index },
   {  2681, PolygonOffsetxOES_remap_index },
   {  3036, RotatexOES_remap_index },
   {  1698, SampleCoveragexOES_remap_index },
   {  4074, ScalexOES_remap_index },
   {  1033, TexEnvxOES_remap_index },
   {  1003, TexEnvxvOES_remap_index },
   {   941, TexGenxOES_remap_index },
   {   984, TexGenxvOES_remap_index },
   {  3558, TexParameterxOES_remap_index },
   {   885, TexParameterxvOES_remap_index },
   {  2577, TranslatexOES_remap_index },
   {  3937, ClipPlanefOES_remap_index },
   {  2648, FrustumfOES_remap_index },
   {  1910, GetClipPlanefOES_remap_index },
   {  2975, OrthofOES_remap_index },
   {  3234, BlendEquationSeparateOES_remap_index },
   {  1846, BindFramebufferOES_remap_index },
   {   123, BindRenderbufferOES_remap_index },
   {  3719, CheckFramebufferStatusOES_remap_index },
   {  3691, DeleteFramebuffersOES_remap_index },
   {  2487, DeleteRenderbuffersOES_remap_index },
   {  4155, FramebufferRenderbufferOES_remap_index },
   {   389, FramebufferTexture2DOES_remap_index },
   {  2934, GenFramebuffersOES_remap_index },
   {  2423, GenRenderbuffersOES_remap_index },
   {  4441, GenerateMipmapOES_remap_index },
   {  2311, GetFramebufferAttachmentParameterivOES_remap_index },
   {  3483, GetRenderbufferParameterivOES_remap_index },
   {   246, IsFramebufferOES_remap_index },
   {  2109, IsRenderbufferOES_remap_index },
   {  1403, RenderbufferStorageOES_remap_index },
   {   268, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  1556, EGLImageTargetTexture2DOES_remap_index },
   {    -1, -1 }
};

/* these functions are in the ABI, but have alternative names */
static const struct gl_function_remap MESA_alt_functions[] = {
   {    -1, -1 }
};

