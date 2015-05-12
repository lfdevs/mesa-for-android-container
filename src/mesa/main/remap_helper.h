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
   /* _mesa_function_pool[0]: MapGrid1d (offset 224) */
   "idd\0"
   "glMapGrid1d\0"
   "\0"
   /* _mesa_function_pool[17]: MapGrid1f (offset 225) */
   "iff\0"
   "glMapGrid1f\0"
   "\0"
   /* _mesa_function_pool[34]: ReplacementCodeuiVertex3fvSUN (dynamic) */
   "pp\0"
   "glReplacementCodeuiVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[70]: PolygonOffsetx (will be remapped) */
   "ii\0"
   "glPolygonOffsetxOES\0"
   "glPolygonOffsetx\0"
   "\0"
   /* _mesa_function_pool[111]: FramebufferTexture (will be remapped) */
   "iiii\0"
   "glFramebufferTextureARB\0"
   "glFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[162]: TexCoordP1ui (will be remapped) */
   "ii\0"
   "glTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[181]: PolygonStipple (offset 175) */
   "p\0"
   "glPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[201]: ListParameterfSGIX (dynamic) */
   "iif\0"
   "glListParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[227]: MultiTexCoord1dv (offset 377) */
   "ip\0"
   "glMultiTexCoord1dv\0"
   "glMultiTexCoord1dvARB\0"
   "\0"
   /* _mesa_function_pool[272]: IsEnabled (offset 286) */
   "i\0"
   "glIsEnabled\0"
   "\0"
   /* _mesa_function_pool[287]: GetTexFilterFuncSGIS (dynamic) */
   "iip\0"
   "glGetTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[315]: AttachShader (will be remapped) */
   "ii\0"
   "glAttachShader\0"
   "\0"
   /* _mesa_function_pool[334]: VertexAttrib3fARB (will be remapped) */
   "ifff\0"
   "glVertexAttrib3f\0"
   "glVertexAttrib3fARB\0"
   "\0"
   /* _mesa_function_pool[377]: Indexubv (offset 316) */
   "p\0"
   "glIndexubv\0"
   "\0"
   /* _mesa_function_pool[391]: GetCompressedTextureImage (will be remapped) */
   "iiip\0"
   "glGetCompressedTextureImage\0"
   "\0"
   /* _mesa_function_pool[425]: MultiTexCoordP3uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[451]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[492]: Color3ubv (offset 20) */
   "p\0"
   "glColor3ubv\0"
   "\0"
   /* _mesa_function_pool[507]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[546]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[566]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[604]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[643]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[663]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[741]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[780]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[811]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[836]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[869]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[898]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[923]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[956]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[983]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[1005]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[1020]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1041]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1058]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[1114]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[1136]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[1167]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysAPPLE\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[1242]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[1267]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[1301]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[1335]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[1399]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[1444]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[1473]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[1521]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[1546]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1577]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[1592]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1623]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[1635]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[1662]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[1720]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "\0"
   /* _mesa_function_pool[1756]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[1775]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[1803]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[1823]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[1849]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "\0"
   /* _mesa_function_pool[1882]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[1931]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[1961]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[1978]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[2010]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[2050]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[2066]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2087]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2108]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[2146]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2176]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[2202]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[2218]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[2245]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[2311]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[2358]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[2394]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[2441]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[2462]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2473]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[2517]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[2532]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[2543]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[2572]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[2587]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[2617]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[2665]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "\0"
   /* _mesa_function_pool[2720]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[2777]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[2833]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[2853]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[2887]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[2905]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[2949]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[2969]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[3014]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[3025]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[3043]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[3067]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[3084]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[3121]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[3138]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[3166]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[3205]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "\0"
   /* _mesa_function_pool[3230]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[3256]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[3352]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[3368]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[3402]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "\0"
   /* _mesa_function_pool[3424]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[3440]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[3463]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[3479]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[3503]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[3536]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[3554]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[3611]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[3643]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[3656]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[3681]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[3714]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[3729]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[3751]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[3776]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[3803]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "\0"
   /* _mesa_function_pool[3885]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[3917]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[3960]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[3988]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[4010]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[4066]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[4101]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[4118]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[4170]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[4196]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[4211]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4243]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[4265]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[4289]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[4324]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4352]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[4376]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[4402]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[4427]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[4461]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[4490]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[4524]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[4572]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[4620]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[4710]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[4770]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[4785]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[4811]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[4838]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[4865]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[4918]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[4964]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[4990]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5017]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[5042]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[5088]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[5111]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[5138]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[5184]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5210]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[5243]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5268]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[5326]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[5341]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[5467]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[5510]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[5533]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[5566]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[5590]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[5614]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[5632]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[5645]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[5723]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[5758]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[5782]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[5808]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[5834]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[5865]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "\0"
   /* _mesa_function_pool[5900]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[5997]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[6022]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[6036]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "\0"
   /* _mesa_function_pool[6069]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[6087]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[6136]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[6154]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[6209]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[6227]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[6253]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[6285]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[6312]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[6358]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[6442]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[6471]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[6497]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[6522]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[6579]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[6615]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[6632]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "\0"
   /* _mesa_function_pool[6656]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[6677]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[6694]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[6721]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[6748]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[6793]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[6841]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "\0"
   /* _mesa_function_pool[6896]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[6935]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[6959]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[7006]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[7038]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[7087]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7116]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7143]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[7188]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[7225]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[7250]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[7281]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[7347]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[7367]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[7452]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "\0"
   /* _mesa_function_pool[7500]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[7545]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[7576]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[7597]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[7611]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[7671]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[7717]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[7783]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "\0"
   /* _mesa_function_pool[7829]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[7871]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[7917]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[7959]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[7993]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[8058]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[8083]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[8104]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[8159]: BindVertexArrayAPPLE (will be remapped) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[8185]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[8245]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[8289]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[8312]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[8357]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[8403]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[8456]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[8503]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "\0"
   /* _mesa_function_pool[8533]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[8559]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[8585]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[8602]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[8630]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[8656]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[8690]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[8716]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[8743]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[8776]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[8804]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[8837]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[8888]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[8901]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[8934]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[8948]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[8983]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[9029]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[9055]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[9081]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[9102]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[9127]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[9151]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[9175]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[9218]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[9245]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[9293]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[9314]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[9327]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[9357]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[9383]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[9399]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[9444]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[9493]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[9538]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[9555]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[9567]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[9593]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[9638]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[9660]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[9702]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[9727]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[9742]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[9783]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[9819]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[9830]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[9848]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[9895]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[9913]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[9938]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[9963]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[9989]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[10028]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[10057]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[10102]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[10147]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[10173]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[10191]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[10216]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[10262]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[10299]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[10344]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[10388]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[10435]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[10462]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[10509]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[10559]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[10585]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[10616]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[10637]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[10665]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[10710]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[10727]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[10778]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[10803]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[10834]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[10859]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[10889]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[10905]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[10931]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[10959]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[11005]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[11025]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[11043]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[11067]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[11116]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[11143]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[11165]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[11218]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[11271]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[11324]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[11377]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[11422]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[11438]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[11460]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[11546]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[11593]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[11639]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[11669]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[11695]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[11738]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[11765]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[11786]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "\0"
   /* _mesa_function_pool[11820]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[11863]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[11897]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[11943]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[11988]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[12029]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[12054]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[12090]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[12131]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[12176]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[12197]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "\0"
   /* _mesa_function_pool[12223]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteriARB\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[12294]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[12332]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[12346]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[12372]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[12393]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[12437]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[12481]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[12509]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[12553]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[12589]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12642]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[12684]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[12722]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[12767]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[12819]: PolygonOffsetEXT (will be remapped) */
   "ff\0"
   "glPolygonOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[12842]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[12868]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[12882]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[12896]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[12920]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[12958]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[12992]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[13016]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[13038]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[13067]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "\0"
   /* _mesa_function_pool[13086]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[13111]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[13160]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[13192]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[13227]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[13259]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[13277]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "\0"
   /* _mesa_function_pool[13319]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[13355]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[13374]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[13393]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[13412]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[13454]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[13473]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[13525]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[13570]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[13613]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[13643]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[13685]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[13700]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[13753]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[13767]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[13796]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "\0"
   /* _mesa_function_pool[13822]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[13838]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[13896]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[13917]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[13936]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[13961]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[13980]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[13995]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[14033]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[14059]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[14089]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[14132]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[14150]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[14184]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[14210]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[14236]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[14265]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[14308]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[14343]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[14371]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[14394]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "\0"
   /* _mesa_function_pool[14417]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[14465]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[14502]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[14515]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[14532]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[14562]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[14596]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[14624]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[14651]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[14694]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[14749]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[14767]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[14788]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[14826]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[14843]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[14864]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[14913]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[14963]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[15009]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[15028]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[15051]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[15078]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[15103]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[15121]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[15134]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[15210]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[15264]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "\0"
   /* _mesa_function_pool[15297]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[15351]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[15388]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[15406]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[15460]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[15505]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[15522]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[15576]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[15595]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "\0"
   /* _mesa_function_pool[15648]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[15677]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[15736]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[15757]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[15769]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[15784]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[15829]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[15858]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[15874]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[15892]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[15908]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15949]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[15971]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[15989]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[16007]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[16025]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[16061]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[16078]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[16096]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[16131]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[16158]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[16193]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[16238]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[16288]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[16314]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "\0"
   /* _mesa_function_pool[16367]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[16410]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[16465]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[16511]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[16537]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[16580]: GenVertexArraysAPPLE (will be remapped) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[16607]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[16650]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[16706]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[16744]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[16763]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[16797]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[16859]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[16880]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[16908]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[16935]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[16983]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[17010]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[17032]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[17050]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[17079]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[17098]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "\0"
   /* _mesa_function_pool[17140]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[17164]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[17192]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[17220]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[17235]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[17262]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[17291]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[17305]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[17344]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[17396]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[17448]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[17464]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[17522]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[17578]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[17608]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[17659]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[17685]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[17720]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[17746]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[17762]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[17805]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[17854]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[17882]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[17898]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[17925]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[17952]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[17972]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[17991]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[18008]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[18034]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[18062]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[18096]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[18132]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "\0"
   /* _mesa_function_pool[18150]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[18191]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[18249]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "\0"
   /* _mesa_function_pool[18273]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[18314]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[18349]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[18381]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[18406]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[18498]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[18543]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[18574]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[18589]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[18610]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[18634]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[18661]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[18685]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[18704]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "\0"
   /* _mesa_function_pool[18726]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[18797]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[18850]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[18895]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[18931]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[18955]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[18984]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[19002]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[19014]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "\0"
   /* _mesa_function_pool[19042]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[19059]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[19076]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[19103]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[19114]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[19157]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[19173]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[19209]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[19263]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[19285]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[19327]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[19364]: FramebufferTextureFaceARB (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureFaceARB\0"
   "\0"
   /* _mesa_function_pool[19399]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[19461]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[19496]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[19522]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[19542]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[19577]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[19622]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[19659]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[19710]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[19728]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[19776]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "\0"
   /* _mesa_function_pool[19800]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[19822]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[19836]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[19854]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[19873]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[19912]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[19956]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[19978]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[20006]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[20061]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[20096]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[20130]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[20161]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[20192]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[20212]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[20255]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[20288]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[20361]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[20387]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[20432]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[20477]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[20494]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[20538]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[20555]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[20572]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[20608]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[20625]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[20656]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[20687]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[20739]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[20758]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[20773]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[20804]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[20826]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[20843]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[20855]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[20876]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[20921]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[20970]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[20996]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[21040]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[21088]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[21127]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[21149]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[21197]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[21227]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[21275]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[21322]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[21342]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[21371]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[21394]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[21414]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[21435]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[21465]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[21479]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[21494]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "\0"
   /* _mesa_function_pool[21533]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[21562]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[21573]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[21592]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[21642]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[21660]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[21691]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[21727]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[21745]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[21758]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[21777]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[21796]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[21827]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[21845]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[21867]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[21933]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[21958]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[21979]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[21999]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[22019]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[22039]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[22065]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[22083]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[22103]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[22120]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[22140]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[22173]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[22216]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[22249]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[22283]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[22313]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "glDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[22398]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[22426]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[22441]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[22459]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[22490]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[22532]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[22587]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[22607]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[22664]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[22696]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[22750]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[22769]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[22784]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[22809]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[22824]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[22841]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[22868]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[22885]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[22912]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[22959]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[23009]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[23056]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[23077]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[23114]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[23154]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[23178]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[23226]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[23276]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[23313]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[23327]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[23354]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "\0"
   /* _mesa_function_pool[23446]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[23463]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[23508]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[23530]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[23566]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[23648]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[23677]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[23720]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[23735]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[23785]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[23818]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[23843]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[23898]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[23926]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[23981]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[24011]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[24028]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[24044]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[24073]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[24087]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[24125]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[24140]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[24175]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[24202]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[24220]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[24250]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[24286]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[24321]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[24347]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[24384]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[24426]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[24452]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[24468]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[24524]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[24541]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[24558]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[24575]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[24592]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[24609]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[24636]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[24653]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[24667]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[24690]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[24718]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[24760]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[24786]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[24818]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[24833]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[24847]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[24872]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[24890]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[24907]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[24928]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[24973]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[25030]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[25048]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[25078]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[25093]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[25105]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[25156]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[25178]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[25204]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[25222]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "\0"
   /* _mesa_function_pool[25278]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[25305]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[25339]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "\0"
   /* _mesa_function_pool[25363]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[25371]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[25398]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[25427]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[25478]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[25490]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[25536]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[25558]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[25591]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[25635]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[25664]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[25694]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[25738]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "\0"
   /* _mesa_function_pool[25765]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[25799]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[25815]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[25849]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[25865]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[25900]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[25916]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[25932]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[25948]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[25960]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[25996]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[26042]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[26102]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[26120]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[26132]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[26144]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[26165]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[26177]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[26203]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[26259]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[26274]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[26289]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[26303]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[26327]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[26342]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[26380]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[26407]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[26424]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[26445]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[26462]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[26495]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[26520]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[26567]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[26604]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[26620]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[26665]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[26680]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[26696]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[26714]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[26733]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[26753]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[26771]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[26790]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[26812]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[26855]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[26921]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[26956]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[26985]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[27045]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[27063]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[27107]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[27125]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[27143]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[27172]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[27190]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[27214]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[27255]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[27283]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[27301]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[27335]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[27363]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[27397]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[27422]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[27441]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[27457]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[27474]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[27497]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[27580]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[27611]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[27665]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[27689]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[27719]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[27766]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[27781]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[27803]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[27832]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[27858]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "\0"
   /* _mesa_function_pool[27884]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[27910]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[27936]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[27990]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "\0"
   /* _mesa_function_pool[28057]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[28104]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[28136]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[28154]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[28200]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[28243]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[28288]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[28322]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[28347]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[28381]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[28430]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[28479]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[28506]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[28550]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[28574]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[28618]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[28642]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[28675]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[28710]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[28730]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[28756]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[28782]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "\0"
   /* _mesa_function_pool[28804]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[28834]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[28866]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[28883]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[28926]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[28948]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[28966]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[28989]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[29053]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[29106]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[29132]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[29154]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[29172]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[29195]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[29245]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[29280]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[29295]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[29327]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[29346]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[29366]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[29381]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[29395]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[29411]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[29432]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[29446]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[29477]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "\0"
   /* _mesa_function_pool[29500]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[29526]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[29575]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[29592]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[29609]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[29654]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[29681]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[29703]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[29728]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[29771]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[29805]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[29847]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[29933]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[29960]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[29986]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[30017]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[30064]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[30082]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[30114]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[30149]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[30188]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[30235]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[30266]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[30286]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[30311]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[30365]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[30383]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[30429]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[30456]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[30484]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[30500]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[30516]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[30532]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[30586]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[30617]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[30673]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[30723]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[30739]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[30795]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[30887]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[30959]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[30989]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[31006]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[31044]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[31103]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[31140]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "\0"
   /* _mesa_function_pool[31160]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[31187]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[31205]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[31243]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[31258]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[31274]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[31311]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[31345]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[31372]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[31399]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[31442]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[31458]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[31484]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[31502]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[31533]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[31578]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[31615]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[31658]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[31681]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[31727]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[31789]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[31810]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[31900]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[31932]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[31952]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[31972]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[31997]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[32031]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[32077]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "\0"
   /* _mesa_function_pool[32103]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[32149]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[32170]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "\0"
   /* _mesa_function_pool[32225]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[32271]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[32311]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[32345]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[32370]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[32383]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[32426]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[32443]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[32461]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[32479]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[32531]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[32582]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[32621]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[32637]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[32680]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[32726]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[32753]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[32771]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[32820]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[32857]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[32908]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[32941]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[32960]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[32980]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[32999]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[33034]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[33081]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[33101]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[33119]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[33174]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[33189]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[33215]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[33243]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[33274]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[33315]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[33358]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[33372]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[33414]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[33449]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[33477]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[33491]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[33520]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[33580]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[33646]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[33666]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[33691]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[33724]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[33771]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[33789]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[33822]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[33870]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[33927]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[33946]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[33966]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[33979]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[34005]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[34032]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[34099]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[34153]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "\0"
   /* _mesa_function_pool[34192]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[34222]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[34238]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[34326]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[34353]: NewList (dynamic) */
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[34367]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "\0"
   /* _mesa_function_pool[34399]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[34416]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[34432]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[34448]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[34464]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[34489]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[34505]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[34532]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[34553]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[34604]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[34629]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[34685]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[34710]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[34726]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[34745]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[34788]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[34812]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[34827]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[34852]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[34873]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[34897]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[34945]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[34971]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[35001]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[35020]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[35055]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[35072]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[35096]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[35136]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[35202]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[35219]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[35245]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[35290]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[35317]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[35334]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[35379]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[35406]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[35443]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[35470]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[35486]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[35527]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[35574]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[35609]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[35626]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[35675]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[35710]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[35729]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[35779]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[35809]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[35836]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[35862]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[35900]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[35932]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[35951]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[35979]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[36016]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[36061]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[36076]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[36123]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[36141]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "\0"
   /* _mesa_function_pool[36160]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[36205]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[36225]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[36285]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[36313]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[36339]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[36353]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[36373]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[36393]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[36413]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[36441]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[36489]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[36527]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[36545]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[36570]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[36585]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "\0"
   /* _mesa_function_pool[36621]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[36670]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[36688]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "\0"
   /* _mesa_function_pool[36706]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[36727]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[36749]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[36779]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[36791]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[36812]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[36841]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[36899]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[36915]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[36940]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[36980]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[36996]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[37012]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[37028]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[37044]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[37066]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[37096]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[37114]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[37142]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[37170]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[37209]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[37224]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[37259]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[37276]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[37321]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[37353]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[37388]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[37434]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[37461]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[37497]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[37527]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[37575]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[37595]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[37639]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[37673]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[37730]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[37755]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[37819]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[37838]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[37864]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[37878]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[37896]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[37921]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[37997]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[38040]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[38074]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[38088]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[38104]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[38118]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[38163]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[38192]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerARB\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[38283]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[38299]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[38361]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[38378]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[38415]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[38447]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[38486]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[38510]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[38530]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[38576]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[38604]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[38643]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[38661]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[38683]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[38698]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[38715]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[38747]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[38777]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[38793]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[38815]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[38848]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[38883]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[38893]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[38939]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[38969]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[39003]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[39037]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[39057]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[39081]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[39124]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[39201]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "\0"
   /* _mesa_function_pool[39230]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[39250]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[39272]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[39285]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[39300]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[39315]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[39343]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[39368]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[39424]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[39455]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[39482]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[39505]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[39525]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[39550]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[39576]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[39615]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[39645]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[39703]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[39719]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[39743]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[39757]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[39781]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[39802]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[39837]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[39852]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[39877]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[39942]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[39985]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayAPPLE\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[40044]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[40066]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[40117]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[40148]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[40177]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[40208]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[40231]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[40252]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[40279]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[40317]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[40342]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[40377]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[40428]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[40447]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[40470]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[40490]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[40545]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[40568]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[40611]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[40635]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[40650]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[40688]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[40712]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[40727]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[40745]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[40763]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[40778]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[40802]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[40817]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[40832]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[40880]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[40951]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[41015]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[41048]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[41082]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[41191]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[41226]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[41251]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[41307]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[41365]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[41384]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[41402]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[41437]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[41456]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[41475]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "\0"
   /* _mesa_function_pool[41499]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[41515]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[41582]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[41600]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[41651]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[41686]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[41704]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[41727]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[41754]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41791]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[41841]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[41874]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[41919]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[41939]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[41955]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[41971]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[42023]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[42060]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[42081]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[42105]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42147]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[42172]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[42192]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42219]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[42243]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[42256]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[42301]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[42324]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[42344]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[42359]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[42385]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[42400]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[42417]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[42446]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[42478]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[42503]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[42518]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[42550]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[42588]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[42616]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[42641]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[42705]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[42736]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[42773]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[42794]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[42827]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[42851]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[42878]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[42897]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[42950]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[42991]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[43022]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[43036]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[43057]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "\0"
   /* _mesa_function_pool[43078]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[43107]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[43124]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[43140]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[43154]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[43173]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[43192]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[43228]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[43258]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[43280]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[43316]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[43345]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[43396]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[43413]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[43430]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[43453]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[43470]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[43496]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[43547]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[43565]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[43597]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[43615]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[43648]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[43694]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[43740]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[43795]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[43858]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[43885]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[43903]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[43930]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[43944]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[43989]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[44010]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[44035]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[44049]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[44063]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[44097]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[44111]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[44138]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[44188]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[44210]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[44229]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[44259]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[44278]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[44306]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[44325]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[44344]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[44362]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[44385]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[44405]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[44437]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[44472]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "\0"
   /* _mesa_function_pool[44494]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[44507]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[44535]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[44569]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[44587]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "\0"
   /* _mesa_function_pool[44631]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[44729]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[44759]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[44814]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[44839]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[44879]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[44904]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 18191, CompressedTexImage1D_remap_index },
   { 15677, CompressedTexImage2D_remap_index },
   { 11460, CompressedTexImage3D_remap_index },
   { 28989, CompressedTexSubImage1D_remap_index },
   { 35136, CompressedTexSubImage2D_remap_index },
   {  5900, CompressedTexSubImage3D_remap_index },
   {  4010, GetCompressedTexImage_remap_index },
   { 17396, LoadTransposeMatrixd_remap_index },
   { 17344, LoadTransposeMatrixf_remap_index },
   { 32479, MultTransposeMatrixd_remap_index },
   { 12767, MultTransposeMatrixf_remap_index },
   { 42950, SampleCoverage_remap_index },
   {  3256, BlendFuncSeparate_remap_index },
   { 20996, FogCoordPointer_remap_index },
   { 38939, FogCoordd_remap_index },
   { 38715, FogCoorddv_remap_index },
   { 31533, MultiDrawArrays_remap_index },
   { 29847, PointParameterf_remap_index },
   {  4620, PointParameterfv_remap_index },
   { 29805, PointParameteri_remap_index },
   {  8245, PointParameteriv_remap_index },
   {  5042, SecondaryColor3b_remap_index },
   { 38530, SecondaryColor3bv_remap_index },
   { 32680, SecondaryColor3d_remap_index },
   { 11593, SecondaryColor3dv_remap_index },
   {  5138, SecondaryColor3i_remap_index },
   { 28154, SecondaryColor3iv_remap_index },
   {  4918, SecondaryColor3s_remap_index },
   { 14963, SecondaryColor3sv_remap_index },
   { 21149, SecondaryColor3ub_remap_index },
   {  6793, SecondaryColor3ubv_remap_index },
   { 21227, SecondaryColor3ui_remap_index },
   { 23178, SecondaryColor3uiv_remap_index },
   { 21040, SecondaryColor3us_remap_index },
   {  9245, SecondaryColor3usv_remap_index },
   { 33870, SecondaryColorPointer_remap_index },
   { 11218, WindowPos2d_remap_index },
   { 16410, WindowPos2dv_remap_index },
   { 11165, WindowPos2f_remap_index },
   { 22532, WindowPos2fv_remap_index },
   { 11271, WindowPos2i_remap_index },
   {  6154, WindowPos2iv_remap_index },
   { 11324, WindowPos2s_remap_index },
   { 44759, WindowPos2sv_remap_index },
   { 15210, WindowPos3d_remap_index },
   { 14694, WindowPos3dv_remap_index },
   { 15297, WindowPos3f_remap_index },
   {  8104, WindowPos3fv_remap_index },
   { 15406, WindowPos3i_remap_index },
   { 43740, WindowPos3iv_remap_index },
   { 15522, WindowPos3s_remap_index },
   { 23926, WindowPos3sv_remap_index },
   {  6036, BeginQuery_remap_index },
   { 43615, BindBuffer_remap_index },
   { 37353, BufferData_remap_index },
   {  9742, BufferSubData_remap_index },
   { 30149, DeleteBuffers_remap_index },
   { 21494, DeleteQueries_remap_index },
   { 19014, EndQuery_remap_index },
   { 41015, GenBuffers_remap_index },
   {  1849, GenQueries_remap_index },
   { 27611, GetBufferParameteriv_remap_index },
   { 40880, GetBufferPointerv_remap_index },
   { 30188, GetBufferSubData_remap_index },
   {  7783, GetQueryObjectiv_remap_index },
   {  7452, GetQueryObjectuiv_remap_index },
   { 11786, GetQueryiv_remap_index },
   { 17854, IsBuffer_remap_index },
   { 27858, IsQuery_remap_index },
   { 11897, MapBuffer_remap_index },
   { 43345, UnmapBuffer_remap_index },
   {   315, AttachShader_remap_index },
   { 35729, BindAttribLocation_remap_index },
   { 41082, BlendEquationSeparate_remap_index },
   { 31205, CompileShader_remap_index },
   { 14132, CreateProgram_remap_index },
   { 30064, CreateShader_remap_index },
   { 19854, DeleteProgram_remap_index },
   { 31187, DeleteShader_remap_index },
   { 42878, DetachShader_remap_index },
   { 33520, DisableVertexAttribArray_remap_index },
   { 22313, DrawBuffers_remap_index },
   { 41307, EnableVertexAttribArray_remap_index },
   { 36441, GetActiveAttrib_remap_index },
   { 41791, GetActiveUniform_remap_index },
   { 16983, GetAttachedShaders_remap_index },
   { 26520, GetAttribLocation_remap_index },
   { 10905, GetProgramInfoLog_remap_index },
   { 22083, GetProgramiv_remap_index },
   {  3751, GetShaderInfoLog_remap_index },
   {  7143, GetShaderSource_remap_index },
   { 16744, GetShaderiv_remap_index },
   {  6087, GetUniformLocation_remap_index },
   { 12920, GetUniformfv_remap_index },
   {  2108, GetUniformiv_remap_index },
   { 34238, GetVertexAttribPointerv_remap_index },
   { 40832, GetVertexAttribdv_remap_index },
   { 34897, GetVertexAttribfv_remap_index },
   { 37527, GetVertexAttribiv_remap_index },
   {  4196, IsProgram_remap_index },
   { 38074, IsShader_remap_index },
   { 28347, LinkProgram_remap_index },
   { 37170, ShaderSource_remap_index },
   { 37142, StencilFuncSeparate_remap_index },
   { 35219, StencilMaskSeparate_remap_index },
   { 36621, StencilOpSeparate_remap_index },
   { 40117, Uniform1f_remap_index },
   {  7959, Uniform1fv_remap_index },
   { 40177, Uniform1i_remap_index },
   { 18062, Uniform1iv_remap_index },
   { 42446, Uniform2f_remap_index },
   { 22249, Uniform2fv_remap_index },
   { 42518, Uniform2i_remap_index },
   { 20096, Uniform2iv_remap_index },
   {   923, Uniform3f_remap_index },
   { 37639, Uniform3fv_remap_index },
   {   836, Uniform3i_remap_index },
   { 38969, Uniform3iv_remap_index },
   {  4490, Uniform4f_remap_index },
   {  8656, Uniform4fv_remap_index },
   {  4427, Uniform4i_remap_index },
   { 44904, Uniform4iv_remap_index },
   {  9848, UniformMatrix2fv_remap_index },
   { 22912, UniformMatrix3fv_remap_index },
   { 10388, UniformMatrix4fv_remap_index },
   { 40279, UseProgram_remap_index },
   { 24384, ValidateProgram_remap_index },
   { 18150, VertexAttrib1d_remap_index },
   { 37997, VertexAttrib1dv_remap_index },
   { 18273, VertexAttrib1s_remap_index },
   { 34745, VertexAttrib1sv_remap_index },
   {  7917, VertexAttrib2d_remap_index },
   { 23677, VertexAttrib2dv_remap_index },
   {  7829, VertexAttrib2s_remap_index },
   { 14265, VertexAttrib2sv_remap_index },
   { 11820, VertexAttrib3d_remap_index },
   { 22173, VertexAttrib3dv_remap_index },
   { 11695, VertexAttrib3s_remap_index },
   { 39942, VertexAttrib3sv_remap_index },
   { 11943, VertexAttrib4Nbv_remap_index },
   { 28243, VertexAttrib4Niv_remap_index },
   { 20432, VertexAttrib4Nsv_remap_index },
   {  1473, VertexAttrib4Nub_remap_index },
   { 33034, VertexAttrib4Nubv_remap_index },
   { 10462, VertexAttrib4Nuiv_remap_index },
   { 35527, VertexAttrib4Nusv_remap_index },
   {  9175, VertexAttrib4bv_remap_index },
   { 28506, VertexAttrib4d_remap_index },
   { 28883, VertexAttrib4dv_remap_index },
   { 39081, VertexAttrib4iv_remap_index },
   { 28574, VertexAttrib4s_remap_index },
   { 19114, VertexAttrib4sv_remap_index },
   { 10102, VertexAttrib4ubv_remap_index },
   { 20387, VertexAttrib4uiv_remap_index },
   {  1399, VertexAttrib4usv_remap_index },
   { 33119, VertexAttribPointer_remap_index },
   { 29654, UniformMatrix2x3fv_remap_index },
   {   956, UniformMatrix2x4fv_remap_index },
   { 10435, UniformMatrix3x2fv_remap_index },
   { 43903, UniformMatrix3x4fv_remap_index },
   { 39455, UniformMatrix4x2fv_remap_index },
   { 11738, UniformMatrix4x3fv_remap_index },
   { 16650, BeginConditionalRender_remap_index },
   { 24468, BeginTransformFeedback_remap_index },
   { 12642, BindBufferBase_remap_index },
   {  7671, BindBufferRange_remap_index },
   { 22696, BindFragDataLocation_remap_index },
   { 23785, ClampColor_remap_index },
   { 17010, ClearBufferfi_remap_index },
   { 16859, ClearBufferfv_remap_index },
   { 20855, ClearBufferiv_remap_index },
   { 38793, ClearBufferuiv_remap_index },
   { 13277, ColorMaski_remap_index },
   {  5865, Disablei_remap_index },
   { 15264, Enablei_remap_index },
   { 23226, EndConditionalRender_remap_index },
   { 19659, EndTransformFeedback_remap_index },
   { 12131, GetBooleani_v_remap_index },
   { 40377, GetFragDataLocation_remap_index },
   { 20876, GetIntegeri_v_remap_index },
   { 28866, GetStringi_remap_index },
   { 30673, GetTexParameterIiv_remap_index },
   { 13473, GetTexParameterIuiv_remap_index },
   { 30887, GetTransformFeedbackVarying_remap_index },
   { 36940, GetUniformuiv_remap_index },
   { 44138, GetVertexAttribIiv_remap_index },
   { 20687, GetVertexAttribIuiv_remap_index },
   { 34153, IsEnabledi_remap_index },
   { 19912, TexParameterIiv_remap_index },
   { 16465, TexParameterIuiv_remap_index },
   { 39877, TransformFeedbackVaryings_remap_index },
   {  5533, Uniform1ui_remap_index },
   { 25960, Uniform1uiv_remap_index },
   { 25305, Uniform2ui_remap_index },
   { 13319, Uniform2uiv_remap_index },
   { 33414, Uniform3ui_remap_index },
   { 19173, Uniform3uiv_remap_index },
   { 12054, Uniform4ui_remap_index },
   { 18096, Uniform4uiv_remap_index },
   { 36016, VertexAttribI1iv_remap_index },
   { 11546, VertexAttribI1uiv_remap_index },
   {  7500, VertexAttribI4bv_remap_index },
   { 10299, VertexAttribI4sv_remap_index },
   {  8456, VertexAttribI4ubv_remap_index },
   {  6959, VertexAttribI4usv_remap_index },
   { 41251, VertexAttribIPointer_remap_index },
   {  8403, PrimitiveRestartIndex_remap_index },
   { 34367, TexBuffer_remap_index },
   {   111, FramebufferTexture_remap_index },
   { 24220, GetBufferParameteri64v_remap_index },
   { 40545, GetInteger64i_v_remap_index },
   { 41600, VertexAttribDivisor_remap_index },
   { 44587, MinSampleShading_remap_index },
   {  7188, BindProgramARB_remap_index },
   { 31399, DeleteProgramsARB_remap_index },
   { 15351, GenProgramsARB_remap_index },
   { 14308, GetProgramEnvParameterdvARB_remap_index },
   { 30114, GetProgramEnvParameterfvARB_remap_index },
   { 31578, GetProgramLocalParameterdvARB_remap_index },
   { 38378, GetProgramLocalParameterfvARB_remap_index },
   { 22841, GetProgramStringARB_remap_index },
   {  8289, GetProgramivARB_remap_index },
   { 31900, IsProgramARB_remap_index },
   { 17464, ProgramEnvParameter4dARB_remap_index },
   {  2720, ProgramEnvParameter4dvARB_remap_index },
   { 39645, ProgramEnvParameter4fARB_remap_index },
   { 24973, ProgramEnvParameter4fvARB_remap_index },
   { 23276, ProgramLocalParameter4dARB_remap_index },
   {  4066, ProgramLocalParameter4dvARB_remap_index },
   { 31103, ProgramLocalParameter4fARB_remap_index },
   { 19461, ProgramLocalParameter4fvARB_remap_index },
   { 31972, ProgramStringARB_remap_index },
   { 12090, VertexAttrib1fARB_remap_index },
   { 32383, VertexAttrib1fvARB_remap_index },
   { 22490, VertexAttrib2fARB_remap_index },
   { 13570, VertexAttrib2fvARB_remap_index },
   {   334, VertexAttrib3fARB_remap_index },
   { 26812, VertexAttrib3fvARB_remap_index },
   { 25694, VertexAttrib4fARB_remap_index },
   { 14651, VertexAttrib4fvARB_remap_index },
   { 36727, AttachObjectARB_remap_index },
   { 22885, CreateProgramObjectARB_remap_index },
   { 16908, CreateShaderObjectARB_remap_index },
   { 15736, DeleteObjectARB_remap_index },
   { 39250, DetachObjectARB_remap_index },
   { 37066, GetAttachedObjectsARB_remap_index },
   { 19710, GetHandleARB_remap_index },
   { 20804, GetInfoLogARB_remap_index },
   { 21660, GetObjectParameterfvARB_remap_index },
   { 42991, GetObjectParameterivARB_remap_index },
   {  5645, DrawArraysInstancedARB_remap_index },
   {  7367, DrawElementsInstancedARB_remap_index },
   { 14089, BindFramebuffer_remap_index },
   {  8312, BindRenderbuffer_remap_index },
   { 34553, BlitFramebuffer_remap_index },
   {  6358, CheckFramebufferStatus_remap_index },
   { 20288, DeleteFramebuffers_remap_index },
   { 37921, DeleteRenderbuffers_remap_index },
   { 31810, FramebufferRenderbuffer_remap_index },
   { 34629, FramebufferTexture1D_remap_index },
   { 23566, FramebufferTexture2D_remap_index },
   { 27497, FramebufferTexture3D_remap_index },
   { 38192, FramebufferTextureLayer_remap_index },
   { 40951, GenFramebuffers_remap_index },
   { 34032, GenRenderbuffers_remap_index },
   {  7611, GenerateMipmap_remap_index },
   {  5341, GetFramebufferAttachmentParameteriv_remap_index },
   { 44631, GetRenderbufferParameteriv_remap_index },
   {  6522, IsFramebuffer_remap_index },
   { 26042, IsRenderbuffer_remap_index },
   {   663, RenderbufferStorage_remap_index },
   { 15134, RenderbufferStorageMultisample_remap_index },
   { 19364, FramebufferTextureFaceARB_remap_index },
   {  5268, FlushMappedBufferRange_remap_index },
   { 31615, MapBufferRange_remap_index },
   { 13412, BindVertexArray_remap_index },
   {  1167, DeleteVertexArrays_remap_index },
   { 17762, GenVertexArrays_remap_index },
   { 39985, IsVertexArray_remap_index },
   { 13192, GetActiveUniformBlockName_remap_index },
   { 44405, GetActiveUniformBlockiv_remap_index },
   { 21197, GetActiveUniformName_remap_index },
   { 14236, GetActiveUniformsiv_remap_index },
   { 42851, GetUniformBlockIndex_remap_index },
   { 10559, GetUniformIndices_remap_index },
   { 35443, UniformBlockBinding_remap_index },
   { 43858, CopyBufferSubData_remap_index },
   { 44188, ClientWaitSync_remap_index },
   { 11422, DeleteSync_remap_index },
   { 35470, FenceSync_remap_index },
   { 39230, GetInteger64v_remap_index },
   { 41456, GetSynciv_remap_index },
   { 15757, IsSync_remap_index },
   { 34222, WaitSync_remap_index },
   { 13227, DrawElementsBaseVertex_remap_index },
   { 17098, DrawElementsInstancedBaseVertex_remap_index },
   { 38447, DrawRangeElementsBaseVertex_remap_index },
   { 42550, MultiDrawElementsBaseVertex_remap_index },
   { 24718, ProvokingVertex_remap_index },
   {  5590, GetMultisamplefv_remap_index },
   { 36123, SampleMaski_remap_index },
   {  1978, TexImage2DMultisample_remap_index },
   { 42794, TexImage3DMultisample_remap_index },
   { 23354, BlendEquationSeparateiARB_remap_index },
   { 27990, BlendEquationiARB_remap_index },
   {  3803, BlendFuncSeparateiARB_remap_index },
   { 25222, BlendFunciARB_remap_index },
   {  1720, BindFragDataLocationIndexed_remap_index },
   { 29477, GetFragDataIndex_remap_index },
   {  2887, BindSampler_remap_index },
   { 43989, DeleteSamplers_remap_index },
   { 36670, GenSamplers_remap_index },
   {  2587, GetSamplerParameterIiv_remap_index },
   {  5834, GetSamplerParameterIuiv_remap_index },
   { 23648, GetSamplerParameterfv_remap_index },
   { 25398, GetSamplerParameteriv_remap_index },
   { 26665, IsSampler_remap_index },
   {  8716, SamplerParameterIiv_remap_index },
   { 12481, SamplerParameterIuiv_remap_index },
   { 29703, SamplerParameterf_remap_index },
   { 39550, SamplerParameterfv_remap_index },
   { 44010, SamplerParameteri_remap_index },
   { 28730, SamplerParameteriv_remap_index },
   { 23735, GetQueryObjecti64v_remap_index },
   {  4118, GetQueryObjectui64v_remap_index },
   { 13067, QueryCounter_remap_index },
   { 38777, ColorP3ui_remap_index },
   {  6677, ColorP3uiv_remap_index },
   { 17882, ColorP4ui_remap_index },
   { 26407, ColorP4uiv_remap_index },
   { 13936, MultiTexCoordP1ui_remap_index },
   { 26177, MultiTexCoordP1uiv_remap_index },
   { 34827, MultiTexCoordP2ui_remap_index },
   {  9029, MultiTexCoordP2uiv_remap_index },
   { 26495, MultiTexCoordP3ui_remap_index },
   {   425, MultiTexCoordP3uiv_remap_index },
   { 41226, MultiTexCoordP4ui_remap_index },
   { 34945, MultiTexCoordP4uiv_remap_index },
   { 37259, NormalP3ui_remap_index },
   { 26102, NormalP3uiv_remap_index },
   { 42147, SecondaryColorP3ui_remap_index },
   {  5782, SecondaryColorP3uiv_remap_index },
   {   162, TexCoordP1ui_remap_index },
   {   643, TexCoordP1uiv_remap_index },
   { 26771, TexCoordP2ui_remap_index },
   { 37575, TexCoordP2uiv_remap_index },
   { 15009, TexCoordP3ui_remap_index },
   { 17952, TexCoordP3uiv_remap_index },
   { 34726, TexCoordP4ui_remap_index },
   {  1803, TexCoordP4uiv_remap_index },
   { 15078, VertexAttribP1ui_remap_index },
   {  4170, VertexAttribP1uiv_remap_index },
   { 30286, VertexAttribP2ui_remap_index },
   {  4964, VertexAttribP2uiv_remap_index },
   {  1521, VertexAttribP3ui_remap_index },
   { 28756, VertexAttribP3uiv_remap_index },
   {  4402, VertexAttribP4ui_remap_index },
   { 16288, VertexAttribP4uiv_remap_index },
   { 35609, VertexP2ui_remap_index },
   { 33101, VertexP2uiv_remap_index },
   { 22868, VertexP3ui_remap_index },
   {  6069, VertexP3uiv_remap_index },
   {  3121, VertexP4ui_remap_index },
   { 44569, VertexP4uiv_remap_index },
   {   811, DrawArraysIndirect_remap_index },
   { 23898, DrawElementsIndirect_remap_index },
   { 10931, BindTransformFeedback_remap_index },
   { 10803, DeleteTransformFeedbacks_remap_index },
   { 36285, DrawTransformFeedback_remap_index },
   {  3960, GenTransformFeedbacks_remap_index },
   { 33666, IsTransformFeedback_remap_index },
   { 31160, PauseTransformFeedback_remap_index },
   { 35951, ResumeTransformFeedback_remap_index },
   { 22784, BeginQueryIndexed_remap_index },
   { 41651, DrawTransformFeedbackStream_remap_index },
   { 19263, EndQueryIndexed_remap_index },
   { 22039, GetQueryIndexediv_remap_index },
   { 44535, ClearDepthf_remap_index },
   { 24286, DepthRangef_remap_index },
   { 38815, GetShaderPrecisionFormat_remap_index },
   {  3230, ReleaseShaderCompiler_remap_index },
   { 25536, ShaderBinary_remap_index },
   { 19728, GetProgramBinary_remap_index },
   { 11988, ProgramBinary_remap_index },
   { 12223, ProgramParameteri_remap_index },
   { 27190, DepthRangeArrayv_remap_index },
   { 42616, DepthRangeIndexed_remap_index },
   { 33646, GetDoublei_v_remap_index },
   { 36141, GetFloati_v_remap_index },
   { 43057, ScissorArrayv_remap_index },
   { 25339, ScissorIndexed_remap_index },
   { 28782, ScissorIndexedv_remap_index },
   { 18704, ViewportArrayv_remap_index },
   { 32077, ViewportIndexedf_remap_index },
   { 19776, ViewportIndexedfv_remap_index },
   {  8503, GetGraphicsResetStatusARB_remap_index },
   { 30429, GetnColorTableARB_remap_index },
   {  2853, GetnCompressedTexImageARB_remap_index },
   {  1267, GetnConvolutionFilterARB_remap_index },
   {  4838, GetnHistogramARB_remap_index },
   { 18589, GetnMapdvARB_remap_index },
   { 12372, GetnMapfvARB_remap_index },
   { 34852, GetnMapivARB_remap_index },
   { 39757, GetnMinmaxARB_remap_index },
   {  3656, GetnPixelMapfvARB_remap_index },
   {  5808, GetnPixelMapuivARB_remap_index },
   { 11669, GetnPixelMapusvARB_remap_index },
   { 22398, GetnPolygonStippleARB_remap_index },
   { 29245, GetnSeparableFilterARB_remap_index },
   { 10147, GetnTexImageARB_remap_index },
   { 28322, GetnUniformdvARB_remap_index },
   { 34464, GetnUniformfvARB_remap_index },
   {  3205, GetnUniformivARB_remap_index },
   { 13796, GetnUniformuivARB_remap_index },
   { 25738, ReadnPixelsARB_remap_index },
   { 33274, DrawArraysInstancedBaseInstance_remap_index },
   { 10344, DrawElementsInstancedBaseInstance_remap_index },
   {  2665, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 35862, DrawTransformFeedbackInstanced_remap_index },
   { 13525, DrawTransformFeedbackStreamInstanced_remap_index },
   { 43078, GetInternalformativ_remap_index },
   { 19873, GetActiveAtomicCounterBufferiv_remap_index },
   { 44507, BindImageTexture_remap_index },
   { 21573, MemoryBarrier_remap_index },
   { 34532, TexStorage1D_remap_index },
   { 23508, TexStorage2D_remap_index },
   { 27474, TexStorage3D_remap_index },
   {  1444, TextureStorage1DEXT_remap_index },
   { 35779, TextureStorage2DEXT_remap_index },
   { 22459, TextureStorage3DEXT_remap_index },
   { 36545, ClearBufferData_remap_index },
   {  2146, ClearBufferSubData_remap_index },
   { 31658, DispatchCompute_remap_index },
   {  6442, DispatchComputeIndirect_remap_index },
   { 36585, CopyImageSubData_remap_index },
   { 41475, TextureView_remap_index },
   { 21933, BindVertexBuffer_remap_index },
   { 29960, VertexAttribBinding_remap_index },
   { 30456, VertexAttribFormat_remap_index },
   { 33215, VertexAttribIFormat_remap_index },
   { 37114, VertexAttribLFormat_remap_index },
   { 35379, VertexBindingDivisor_remap_index },
   { 38415, MultiDrawArraysIndirect_remap_index },
   { 18314, MultiDrawElementsIndirect_remap_index },
   { 18249, TexBufferRange_remap_index },
   { 39003, TexStorage2DMultisample_remap_index },
   { 28675, TexStorage3DMultisample_remap_index },
   {  3402, BufferStorage_remap_index },
   { 39482, ClearTexImage_remap_index },
   { 13160, ClearTexSubImage_remap_index },
   {  4265, BindBuffersBase_remap_index },
   { 14624, BindBuffersRange_remap_index },
   { 10778, BindImageTextures_remap_index },
   {  2833, BindSamplers_remap_index },
   { 42172, BindTextures_remap_index },
   { 25371, BindVertexBuffers_remap_index },
   { 36688, ClipControl_remap_index },
   { 19800, BindTextureUnit_remap_index },
   { 39576, CompressedTextureSubImage1D_remap_index },
   { 35486, CompressedTextureSubImage2D_remap_index },
   { 33315, CompressedTextureSubImage3D_remap_index },
   { 35900, CopyTextureSubImage1D_remap_index },
   { 31997, CopyTextureSubImage2D_remap_index },
   { 41402, CopyTextureSubImage3D_remap_index },
   { 28926, CreateTextures_remap_index },
   { 31372, GenerateTextureMipmap_remap_index },
   {   391, GetCompressedTextureImage_remap_index },
   { 17659, GetTextureImage_remap_index },
   { 32999, GetTextureLevelParameterfv_remap_index },
   { 35675, GetTextureLevelParameteriv_remap_index },
   { 14059, GetTextureParameterIiv_remap_index },
   { 21796, GetTextureParameterIuiv_remap_index },
   { 24044, GetTextureParameterfv_remap_index },
   { 27143, GetTextureParameteriv_remap_index },
   { 21414, TextureBuffer_remap_index },
   { 35809, TextureParameterIiv_remap_index },
   { 27335, TextureParameterIuiv_remap_index },
   { 34604, TextureParameterf_remap_index },
   {  2176, TextureParameterfv_remap_index },
   { 34685, TextureParameteri_remap_index },
   { 24321, TextureParameteriv_remap_index },
   {  9938, TextureStorage1D_remap_index },
   { 14033, TextureStorage2D_remap_index },
   { 40650, TextureStorage2DMultisample_remap_index },
   { 17898, TextureStorage3D_remap_index },
   {  3166, TextureStorage3DMultisample_remap_index },
   { 25635, TextureSubImage1D_remap_index },
   { 29986, TextureSubImage2D_remap_index },
   { 32908, TextureSubImage3D_remap_index },
   {  6471, InvalidateBufferData_remap_index },
   { 39424, InvalidateBufferSubData_remap_index },
   { 21533, InvalidateFramebuffer_remap_index },
   { 16025, InvalidateSubFramebuffer_remap_index },
   { 12029, InvalidateTexImage_remap_index },
   { 25765, InvalidateTexSubImage_remap_index },
   { 12819, PolygonOffsetEXT_remap_index },
   { 36706, DrawTexfOES_remap_index },
   { 25204, DrawTexfvOES_remap_index },
   {  1020, DrawTexiOES_remap_index },
   { 30365, DrawTexivOES_remap_index },
   { 12176, DrawTexsOES_remap_index },
   { 21727, DrawTexsvOES_remap_index },
   { 26424, DrawTexxOES_remap_index },
   { 38643, DrawTexxvOES_remap_index },
   { 24609, PointSizePointerOES_remap_index },
   {   983, QueryMatrixxOES_remap_index },
   { 19327, SampleMaskSGIS_remap_index },
   { 33372, SamplePatternSGIS_remap_index },
   { 42478, ColorPointerEXT_remap_index },
   { 27884, EdgeFlagPointerEXT_remap_index },
   { 12896, IndexPointerEXT_remap_index },
   { 13086, NormalPointerEXT_remap_index },
   { 27255, TexCoordPointerEXT_remap_index },
   { 24426, VertexPointerEXT_remap_index },
   { 42417, DiscardFramebufferEXT_remap_index },
   { 10727, ActiveShaderProgram_remap_index },
   { 16238, BindProgramPipeline_remap_index },
   { 27936, CreateShaderProgramv_remap_index },
   {  3554, DeleteProgramPipelines_remap_index },
   { 25427, GenProgramPipelines_remap_index },
   {  7993, GetProgramPipelineInfoLog_remap_index },
   { 30532, GetProgramPipelineiv_remap_index },
   { 25490, IsProgramPipeline_remap_index },
   { 44385, LockArraysEXT_remap_index },
   { 43694, ProgramUniform1f_remap_index },
   {  9444, ProgramUniform1fv_remap_index },
   { 43648, ProgramUniform1i_remap_index },
   { 14864, ProgramUniform1iv_remap_index },
   { 33822, ProgramUniform1ui_remap_index },
   { 43496, ProgramUniform1uiv_remap_index },
   {  2311, ProgramUniform2f_remap_index },
   { 35626, ProgramUniform2fv_remap_index },
   {  2394, ProgramUniform2i_remap_index },
   { 20921, ProgramUniform2iv_remap_index },
   {  7038, ProgramUniform2ui_remap_index },
   {  8837, ProgramUniform2uiv_remap_index },
   {  4524, ProgramUniform3f_remap_index },
   { 29526, ProgramUniform3fv_remap_index },
   {  4572, ProgramUniform3i_remap_index },
   { 13111, ProgramUniform3iv_remap_index },
   { 14913, ProgramUniform3ui_remap_index },
   { 17608, ProgramUniform3uiv_remap_index },
   { 28381, ProgramUniform4f_remap_index },
   { 32771, ProgramUniform4fv_remap_index },
   { 28430, ProgramUniform4i_remap_index },
   {  1882, ProgramUniform4iv_remap_index },
   { 40066, ProgramUniform4ui_remap_index },
   { 32531, ProgramUniform4uiv_remap_index },
   { 19399, ProgramUniformMatrix2fv_remap_index },
   { 21867, ProgramUniformMatrix2x3fv_remap_index },
   {  7717, ProgramUniformMatrix2x4fv_remap_index },
   { 38299, ProgramUniformMatrix3fv_remap_index },
   { 33580, ProgramUniformMatrix3x2fv_remap_index },
   { 26855, ProgramUniformMatrix3x4fv_remap_index },
   { 31727, ProgramUniformMatrix4fv_remap_index },
   {  2245, ProgramUniformMatrix4x2fv_remap_index },
   {  7281, ProgramUniformMatrix4x3fv_remap_index },
   { 39037, UnlockArraysEXT_remap_index },
   { 31681, UseProgramStages_remap_index },
   {  1662, ValidateProgramPipeline_remap_index },
   { 16314, DebugMessageCallback_remap_index },
   { 32170, DebugMessageControl_remap_index },
   { 15595, DebugMessageInsert_remap_index },
   {  6841, GetDebugMessageLog_remap_index },
   {  6632, GetObjectLabel_remap_index },
   { 12197, GetObjectPtrLabel_remap_index },
   { 31140, ObjectLabel_remap_index },
   { 44472, ObjectPtrLabel_remap_index },
   { 18132, PopDebugGroup_remap_index },
   { 14394, PushDebugGroup_remap_index },
   {  8357, SecondaryColor3fEXT_remap_index },
   {  7871, SecondaryColor3fvEXT_remap_index },
   { 29195, MultiDrawElementsEXT_remap_index },
   { 10859, FogCoordfEXT_remap_index },
   { 18349, FogCoordfvEXT_remap_index },
   {  4243, ResizeBuffersMESA_remap_index },
   { 34788, WindowPos4dMESA_remap_index },
   { 27781, WindowPos4dvMESA_remap_index },
   {  4352, WindowPos4fMESA_remap_index },
   { 11438, WindowPos4fvMESA_remap_index },
   {  9127, WindowPos4iMESA_remap_index },
   {  3729, WindowPos4ivMESA_remap_index },
   { 28550, WindowPos4sMESA_remap_index },
   {  1114, WindowPos4svMESA_remap_index },
   { 29295, MultiModeDrawArraysIBM_remap_index },
   { 20061, MultiModeDrawElementsIBM_remap_index },
   { 33491, AreProgramsResidentNV_remap_index },
   { 40778, ExecuteProgramNV_remap_index },
   { 30082, GetProgramParameterdvNV_remap_index },
   { 37321, GetProgramParameterfvNV_remap_index },
   { 19496, GetProgramStringNV_remap_index },
   { 15949, GetProgramivNV_remap_index },
   { 18634, GetTrackMatrixivNV_remap_index },
   { 19076, GetVertexAttribdvNV_remap_index },
   { 17235, GetVertexAttribfvNV_remap_index },
   { 16131, GetVertexAttribivNV_remap_index },
   { 37044, LoadProgramNV_remap_index },
   { 20161, ProgramParameters4dvNV_remap_index },
   { 20773, ProgramParameters4fvNV_remap_index },
   {  6253, RequestResidentProgramsNV_remap_index },
   { 29681, TrackMatrixNV_remap_index },
   { 14371, VertexAttrib1dNV_remap_index },
   { 28618, VertexAttrib1dvNV_remap_index },
   { 28966, VertexAttrib1fNV_remap_index },
   { 42827, VertexAttrib1fvNV_remap_index },
   { 21371, VertexAttrib1sNV_remap_index },
   { 39057, VertexAttrib1svNV_remap_index },
   { 18610, VertexAttrib2dNV_remap_index },
   { 35072, VertexAttrib2dvNV_remap_index },
   { 27665, VertexAttrib2fNV_remap_index },
   { 26303, VertexAttrib2fvNV_remap_index },
   { 12992, VertexAttrib2sNV_remap_index },
   {  5758, VertexAttrib2svNV_remap_index },
   { 37730, VertexAttrib3dNV_remap_index },
   { 39719, VertexAttrib3dvNV_remap_index },
   {  5017, VertexAttrib3fNV_remap_index },
   { 42081, VertexAttrib3fvNV_remap_index },
   {  7225, VertexAttrib3sNV_remap_index },
   { 18661, VertexAttrib3svNV_remap_index },
   {  8559, VertexAttrib4dNV_remap_index },
   {  3479, VertexAttrib4dvNV_remap_index },
   {  8630, VertexAttrib4fNV_remap_index },
   { 42219, VertexAttrib4fvNV_remap_index },
   { 17720, VertexAttrib4sNV_remap_index },
   { 11043, VertexAttrib4svNV_remap_index },
   {  1635, VertexAttrib4ubNV_remap_index },
   { 10834, VertexAttrib4ubvNV_remap_index },
   { 29446, VertexAttribPointerNV_remap_index },
   { 27832, VertexAttribs1dvNV_remap_index },
   { 31458, VertexAttribs1fvNV_remap_index },
   {  6227, VertexAttribs1svNV_remap_index },
   { 43470, VertexAttribs2dvNV_remap_index },
   {  4376, VertexAttribs2fvNV_remap_index },
   { 27910, VertexAttribs2svNV_remap_index },
   {  1823, VertexAttribs3dvNV_remap_index },
   { 36313, VertexAttribs3fvNV_remap_index },
   { 14210, VertexAttribs3svNV_remap_index },
   { 25178, VertexAttribs4dvNV_remap_index },
   { 24760, VertexAttribs4fvNV_remap_index },
   { 20970, VertexAttribs4svNV_remap_index },
   { 32726, VertexAttribs4ubvNV_remap_index },
   { 42705, GetTexBumpParameterfvATI_remap_index },
   { 10585, GetTexBumpParameterivATI_remap_index },
   { 36413, TexBumpParameterfvATI_remap_index },
   {  8602, TexBumpParameterivATI_remap_index },
   {  9327, AlphaFragmentOp1ATI_remap_index },
   {  3503, AlphaFragmentOp2ATI_remap_index },
   {  9783, AlphaFragmentOp3ATI_remap_index },
   { 34005, BeginFragmentShaderATI_remap_index },
   {  3776, BindFragmentShaderATI_remap_index },
   {  7250, ColorFragmentOp1ATI_remap_index },
   { 12958, ColorFragmentOp2ATI_remap_index },
   { 24347, ColorFragmentOp3ATI_remap_index },
   { 17050, DeleteFragmentShaderATI_remap_index },
   { 44879, EndFragmentShaderATI_remap_index },
   { 23327, GenFragmentShadersATI_remap_index },
   { 42301, PassTexCoordATI_remap_index },
   { 36393, SampleMapATI_remap_index },
   { 35574, SetFragmentShaderConstantATI_remap_index },
   {  8533, ActiveStencilFaceEXT_remap_index },
   {  8159, BindVertexArrayAPPLE_remap_index },
   { 16580, GenVertexArraysAPPLE_remap_index },
   { 35979, GetProgramNamedParameterdvNV_remap_index },
   { 23077, GetProgramNamedParameterfvNV_remap_index },
   { 42736, ProgramNamedParameter4dNV_remap_index },
   { 38848, ProgramNamedParameter4dvNV_remap_index },
   { 42023, ProgramNamedParameter4fNV_remap_index },
   { 25865, ProgramNamedParameter4fvNV_remap_index },
   { 24667, PrimitiveRestartNV_remap_index },
   { 25156, GetTexGenxvOES_remap_index },
   { 14749, TexGenxOES_remap_index },
   { 32980, TexGenxvOES_remap_index },
   {  8083, DepthBoundsEXT_remap_index },
   {  5997, BindFramebufferEXT_remap_index },
   { 42359, BindRenderbufferEXT_remap_index },
   {  4461, BufferParameteriAPPLE_remap_index },
   { 40342, FlushMappedBufferRangeAPPLE_remap_index },
   { 28200, VertexAttribI1iEXT_remap_index },
   { 11377, VertexAttribI1uiEXT_remap_index },
   { 20494, VertexAttribI2iEXT_remap_index },
   { 41874, VertexAttribI2ivEXT_remap_index },
   { 25996, VertexAttribI2uiEXT_remap_index },
   { 36076, VertexAttribI2uivEXT_remap_index },
   { 19577, VertexAttribI3iEXT_remap_index },
   { 43944, VertexAttribI3ivEXT_remap_index },
   { 23009, VertexAttribI3uiEXT_remap_index },
   { 21275, VertexAttribI3uivEXT_remap_index },
   { 38893, VertexAttribI4iEXT_remap_index },
   {  6748, VertexAttribI4ivEXT_remap_index },
   {  2617, VertexAttribI4uiEXT_remap_index },
   { 28057, VertexAttribI4uivEXT_remap_index },
   {  3043, ClearColorIiEXT_remap_index },
   {  1242, ClearColorIuiEXT_remap_index },
   { 24690, BindBufferOffsetEXT_remap_index },
   { 18381, BeginPerfMonitorAMD_remap_index },
   { 33449, DeletePerfMonitorsAMD_remap_index },
   {  5510, EndPerfMonitorAMD_remap_index },
   { 27397, GenPerfMonitorsAMD_remap_index },
   { 12684, GetPerfMonitorCounterDataAMD_remap_index },
   { 35406, GetPerfMonitorCounterInfoAMD_remap_index },
   { 44839, GetPerfMonitorCounterStringAMD_remap_index },
   { 44437, GetPerfMonitorCountersAMD_remap_index },
   { 14465, GetPerfMonitorGroupStringAMD_remap_index },
   { 30235, GetPerfMonitorGroupsAMD_remap_index },
   { 13995, SelectPerfMonitorCountersAMD_remap_index },
   { 14562, GetObjectParameterivAPPLE_remap_index },
   { 44278, ObjectPurgeableAPPLE_remap_index },
   {  1931, ObjectUnpurgeableAPPLE_remap_index },
   { 43258, ActiveProgramEXT_remap_index },
   { 27803, CreateShaderProgramEXT_remap_index },
   { 37838, UseShaderProgramEXT_remap_index },
   { 31006, TextureBarrierNV_remap_index },
   {  2202, VDPAUFiniNV_remap_index },
   {   869, VDPAUGetSurfaceivNV_remap_index },
   { 24202, VDPAUInitNV_remap_index },
   { 21845, VDPAUIsSurfaceNV_remap_index },
   {  6497, VDPAUMapSurfacesNV_remap_index },
   {  3084, VDPAURegisterOutputSurfaceNV_remap_index },
   { 12553, VDPAURegisterVideoSurfaceNV_remap_index },
   { 11116, VDPAUSurfaceAccessNV_remap_index },
   {  4811, VDPAUUnmapSurfacesNV_remap_index },
   { 38747, VDPAUUnregisterSurfaceNV_remap_index },
   { 39525, BeginPerfQueryINTEL_remap_index },
   { 34326, CreatePerfQueryINTEL_remap_index },
   { 16511, DeletePerfQueryINTEL_remap_index },
   { 41704, EndPerfQueryINTEL_remap_index },
   { 43228, GetFirstPerfQueryIdINTEL_remap_index },
   { 30959, GetNextPerfQueryIdINTEL_remap_index },
   { 32582, GetPerfCounterInfoINTEL_remap_index },
   {   780, GetPerfQueryDataINTEL_remap_index },
   { 22664, GetPerfQueryIdByNameINTEL_remap_index },
   { 20255, GetPerfQueryInfoINTEL_remap_index },
   { 39201, PolygonOffsetClampEXT_remap_index },
   { 20656, StencilFuncSeparateATI_remap_index },
   {  5723, ProgramEnvParameters4fvEXT_remap_index },
   { 31274, ProgramLocalParameters4fvEXT_remap_index },
   {  3917, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  3681, EGLImageTargetTexture2DOES_remap_index },
   { 41841, AlphaFuncx_remap_index },
   { 19622, ClearColorx_remap_index },
   { 41048, ClearDepthx_remap_index },
   { 36812, Color4x_remap_index },
   { 24140, DepthRangex_remap_index },
   {  2441, Fogx_remap_index },
   { 15028, Fogxv_remap_index },
   {  8804, Frustumf_remap_index },
   {  8901, Frustumx_remap_index },
   { 19542, LightModelx_remap_index },
   { 32820, LightModelxv_remap_index },
   { 29500, Lightx_remap_index },
   { 42588, Lightxv_remap_index },
   {  3611, LineWidthx_remap_index },
   {  3368, LoadMatrixx_remap_index },
   { 43565, Materialx_remap_index },
   { 25815, Materialxv_remap_index },
   { 44063, MultMatrixx_remap_index },
   { 10216, MultiTexCoord4x_remap_index },
   { 25664, Normal3x_remap_index },
   { 15648, Orthof_remap_index },
   { 15829, Orthox_remap_index },
   { 28104, PointSizex_remap_index },
   {    70, PolygonOffsetx_remap_index },
   { 38163, Rotatex_remap_index },
   { 20212, SampleCoveragex_remap_index },
   { 12842, Scalex_remap_index },
   { 39315, TexEnvx_remap_index },
   { 44229, TexEnvxv_remap_index },
   {  2010, TexParameterx_remap_index },
   { 32311, Translatex_remap_index },
   { 33789, ClipPlanef_remap_index },
   { 33691, ClipPlanex_remap_index },
   {   741, GetClipPlanef_remap_index },
   {   604, GetClipPlanex_remap_index },
   { 20130, GetFixedv_remap_index },
   {  1301, GetLightxv_remap_index },
   { 23114, GetMaterialxv_remap_index },
   { 21691, GetTexEnvxv_remap_index },
   { 16935, GetTexParameterxv_remap_index },
   { 29728, PointParameterx_remap_index },
   { 38118, PointParameterxv_remap_index },
   { 19285, TexParameterxv_remap_index },
   {    -1, -1 }
};

/* these functions are in the ABI, but have alternative names */
static const struct gl_function_remap MESA_alt_functions[] = {
   /* from GL_EXT_blend_color */
   { 35020, _gloffset_BlendColor },
   /* from GL_EXT_blend_minmax */
   { 37673, _gloffset_BlendEquation },
   /* from GL_EXT_color_subtable */
   {  5467, _gloffset_ColorSubTable },
   { 21592, _gloffset_CopyColorSubTable },
   /* from GL_EXT_convolution */
   {  1335, _gloffset_GetConvolutionParameteriv },
   { 13838, _gloffset_ConvolutionParameterfv },
   { 16797, _gloffset_CopyConvolutionFilter1D },
   { 18797, _gloffset_SeparableFilter2D },
   { 20006, _gloffset_GetConvolutionFilter },
   { 23843, _gloffset_ConvolutionFilter1D },
   { 26203, _gloffset_ConvolutionFilter2D },
   { 29053, _gloffset_GetSeparableFilter },
   { 30617, _gloffset_ConvolutionParameteri },
   { 30739, _gloffset_ConvolutionParameterf },
   { 36841, _gloffset_ConvolutionParameteriv },
   { 42641, _gloffset_GetConvolutionParameterfv },
   { 43795, _gloffset_CopyConvolutionFilter2D },
   /* from GL_EXT_copy_texture */
   { 27719, _gloffset_CopyTexImage2D },
   { 30383, _gloffset_CopyTexImage1D },
   { 32857, _gloffset_CopyTexSubImage1D },
   { 39124, _gloffset_CopyTexSubImage3D },
   { 42897, _gloffset_CopyTexSubImage2D },
   /* from GL_EXT_draw_range_elements */
   { 25105, _gloffset_DrawRangeElements },
   /* from GL_EXT_histogram */
   {  4710, _gloffset_GetHistogramParameterfv },
   {  8185, _gloffset_GetHistogramParameteriv },
   {  9567, _gloffset_Minmax },
   { 14150, _gloffset_GetMinmax },
   { 22216, _gloffset_Histogram },
   { 30311, _gloffset_GetMinmaxParameteriv },
   { 31311, _gloffset_ResetMinmax },
   { 32271, _gloffset_GetHistogram },
   { 34099, _gloffset_GetMinmaxParameterfv },
   { 35096, _gloffset_ResetHistogram },
   /* from GL_EXT_paletted_texture */
   { 13700, _gloffset_ColorTable },
   { 18406, _gloffset_GetColorTableParameterfv },
   { 26985, _gloffset_GetColorTable },
   { 30795, _gloffset_GetColorTableParameteriv },
   /* from GL_EXT_subtexture */
   {  2473, _gloffset_TexSubImage1D },
   { 37388, _gloffset_TexSubImage2D },
   /* from GL_EXT_texture3D */
   { 22607, _gloffset_TexImage3D },
   { 41515, _gloffset_TexSubImage3D },
   /* from GL_EXT_texture_object */
   {  4289, _gloffset_GenTextures },
   {  8948, _gloffset_BindTexture },
   { 17578, _gloffset_IsTexture },
   { 22959, _gloffset_PrioritizeTextures },
   { 27214, _gloffset_DeleteTextures },
   { 41971, _gloffset_AreTexturesResident },
   /* from GL_EXT_vertex_array */
   { 18895, _gloffset_ArrayElement },
   { 29771, _gloffset_DrawArrays },
   { 39802, _gloffset_GetPointerv },
   /* from GL_NV_read_buffer */
   { 30586, _gloffset_ReadBuffer },
   /* from GL_OES_blend_subtract */
   { 37673, _gloffset_BlendEquation },
   /* from GL_OES_texture_3D */
   { 22607, _gloffset_TexImage3D },
   { 39124, _gloffset_CopyTexSubImage3D },
   { 41515, _gloffset_TexSubImage3D },
   /* from GL_OES_texture_cube_map */
   { 17164, _gloffset_TexGeni },
   { 17192, _gloffset_TexGenf },
   { 20572, _gloffset_GetTexGenfv },
   { 34971, _gloffset_TexGeniv },
   { 37497, _gloffset_TexGenfv },
   { 43192, _gloffset_GetTexGeniv },
   /* from GL_SGI_color_table */
   {  2777, _gloffset_ColorTableParameteriv },
   { 13700, _gloffset_ColorTable },
   { 17522, _gloffset_ColorTableParameterfv },
   { 18406, _gloffset_GetColorTableParameterfv },
   { 26985, _gloffset_GetColorTable },
   { 27063, _gloffset_CopyColorTable },
   { 30795, _gloffset_GetColorTableParameteriv },
   {    -1, -1 }
};

