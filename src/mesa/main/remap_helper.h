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
   /* _mesa_function_pool[451]: VertexAttribI4usv (will be remapped) */
   "ip\0"
   "glVertexAttribI4usvEXT\0"
   "glVertexAttribI4usv\0"
   "\0"
   /* _mesa_function_pool[498]: Color3ubv (offset 20) */
   "p\0"
   "glColor3ubv\0"
   "\0"
   /* _mesa_function_pool[513]: GetCombinerOutputParameterfvNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[552]: Binormal3ivEXT (dynamic) */
   "p\0"
   "glBinormal3ivEXT\0"
   "\0"
   /* _mesa_function_pool[572]: GetImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[610]: GetClipPlanex (will be remapped) */
   "ip\0"
   "glGetClipPlanexOES\0"
   "glGetClipPlanex\0"
   "\0"
   /* _mesa_function_pool[649]: TexCoordP1uiv (will be remapped) */
   "ip\0"
   "glTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[669]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "glRenderbufferStorageEXT\0"
   "glRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[747]: GetClipPlanef (will be remapped) */
   "ip\0"
   "glGetClipPlanefOES\0"
   "glGetClipPlanef\0"
   "\0"
   /* _mesa_function_pool[786]: GetPerfQueryDataINTEL (will be remapped) */
   "iiipp\0"
   "glGetPerfQueryDataINTEL\0"
   "\0"
   /* _mesa_function_pool[817]: DrawArraysIndirect (will be remapped) */
   "ip\0"
   "glDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[842]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "glUniform3iARB\0"
   "\0"
   /* _mesa_function_pool[875]: VDPAUGetSurfaceivNV (will be remapped) */
   "iiipp\0"
   "glVDPAUGetSurfaceivNV\0"
   "\0"
   /* _mesa_function_pool[904]: Uniform3d (will be remapped) */
   "iddd\0"
   "glUniform3d\0"
   "\0"
   /* _mesa_function_pool[922]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "glUniform3fARB\0"
   "\0"
   /* _mesa_function_pool[955]: UniformMatrix2x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4fv\0"
   "\0"
   /* _mesa_function_pool[982]: QueryMatrixxOES (will be remapped) */
   "pp\0"
   "glQueryMatrixxOES\0"
   "\0"
   /* _mesa_function_pool[1004]: Normal3iv (offset 59) */
   "p\0"
   "glNormal3iv\0"
   "\0"
   /* _mesa_function_pool[1019]: DrawTexiOES (will be remapped) */
   "iiiii\0"
   "glDrawTexiOES\0"
   "\0"
   /* _mesa_function_pool[1040]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1057]: ReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[1113]: CreateProgramPipelines (will be remapped) */
   "ip\0"
   "glCreateProgramPipelines\0"
   "\0"
   /* _mesa_function_pool[1142]: FragmentLightModelivSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelivSGIX\0"
   "\0"
   /* _mesa_function_pool[1173]: DeleteVertexArrays (will be remapped) */
   "ip\0"
   "glDeleteVertexArrays\0"
   "glDeleteVertexArraysAPPLE\0"
   "glDeleteVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[1248]: ClearColorIuiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIuiEXT\0"
   "\0"
   /* _mesa_function_pool[1273]: GetnConvolutionFilterARB (will be remapped) */
   "iiiip\0"
   "glGetnConvolutionFilterARB\0"
   "\0"
   /* _mesa_function_pool[1307]: GetLightxv (will be remapped) */
   "iip\0"
   "glGetLightxvOES\0"
   "glGetLightxv\0"
   "\0"
   /* _mesa_function_pool[1341]: GetConvolutionParameteriv (offset 358) */
   "iip\0"
   "glGetConvolutionParameteriv\0"
   "glGetConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[1405]: GetProgramResourceLocation (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocation\0"
   "\0"
   /* _mesa_function_pool[1439]: GetSubroutineUniformLocation (will be remapped) */
   "iip\0"
   "glGetSubroutineUniformLocation\0"
   "\0"
   /* _mesa_function_pool[1475]: VertexAttrib4usv (will be remapped) */
   "ip\0"
   "glVertexAttrib4usv\0"
   "glVertexAttrib4usvARB\0"
   "\0"
   /* _mesa_function_pool[1520]: TextureStorage1DEXT (will be remapped) */
   "iiiii\0"
   "glTextureStorage1DEXT\0"
   "\0"
   /* _mesa_function_pool[1549]: VertexAttrib4Nub (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4Nub\0"
   "glVertexAttrib4NubARB\0"
   "\0"
   /* _mesa_function_pool[1597]: VertexAttribP3ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP3ui\0"
   "\0"
   /* _mesa_function_pool[1622]: Color4ubVertex3fSUN (dynamic) */
   "iiiifff\0"
   "glColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1653]: PointSize (offset 173) */
   "f\0"
   "glPointSize\0"
   "\0"
   /* _mesa_function_pool[1668]: TexCoord2fVertex3fSUN (dynamic) */
   "fffff\0"
   "glTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[1699]: PopName (offset 200) */
   "\0"
   "glPopName\0"
   "\0"
   /* _mesa_function_pool[1711]: GetProgramResourceLocationIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceLocationIndex\0"
   "\0"
   /* _mesa_function_pool[1750]: CreateTransformFeedbacks (will be remapped) */
   "ip\0"
   "glCreateTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[1781]: VertexAttrib4ubNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4ubNV\0"
   "\0"
   /* _mesa_function_pool[1808]: ValidateProgramPipeline (will be remapped) */
   "i\0"
   "glValidateProgramPipeline\0"
   "glValidateProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[1866]: BindFragDataLocationIndexed (will be remapped) */
   "iiip\0"
   "glBindFragDataLocationIndexed\0"
   "\0"
   /* _mesa_function_pool[1902]: GetClipPlane (offset 259) */
   "ip\0"
   "glGetClipPlane\0"
   "\0"
   /* _mesa_function_pool[1921]: CombinerParameterfvNV (dynamic) */
   "ip\0"
   "glCombinerParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[1949]: TexCoordP4uiv (will be remapped) */
   "ip\0"
   "glTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[1969]: VertexAttribs3dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3dvNV\0"
   "\0"
   /* _mesa_function_pool[1995]: ProgramUniformMatrix2x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[2030]: GenQueries (will be remapped) */
   "ip\0"
   "glGenQueries\0"
   "glGenQueriesARB\0"
   "\0"
   /* _mesa_function_pool[2063]: ProgramUniform4iv (will be remapped) */
   "iiip\0"
   "glProgramUniform4iv\0"
   "glProgramUniform4ivEXT\0"
   "\0"
   /* _mesa_function_pool[2112]: ObjectUnpurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectUnpurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[2142]: GetCompressedTextureSubImage (will be remapped) */
   "iiiiiiiiip\0"
   "glGetCompressedTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[2185]: TexCoord2iv (offset 107) */
   "p\0"
   "glTexCoord2iv\0"
   "\0"
   /* _mesa_function_pool[2202]: TexImage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexImage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[2234]: TexParameterx (will be remapped) */
   "iii\0"
   "glTexParameterxOES\0"
   "glTexParameterx\0"
   "\0"
   /* _mesa_function_pool[2274]: Rotatef (offset 300) */
   "ffff\0"
   "glRotatef\0"
   "\0"
   /* _mesa_function_pool[2290]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2311]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2332]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "glGetUniformivARB\0"
   "\0"
   /* _mesa_function_pool[2370]: ClearBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2400]: TextureParameterfv (will be remapped) */
   "iip\0"
   "glTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[2426]: VDPAUFiniNV (will be remapped) */
   "\0"
   "glVDPAUFiniNV\0"
   "\0"
   /* _mesa_function_pool[2442]: GlobalAlphaFactordSUN (dynamic) */
   "d\0"
   "glGlobalAlphaFactordSUN\0"
   "\0"
   /* _mesa_function_pool[2469]: ProgramUniformMatrix4x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2fv\0"
   "glProgramUniformMatrix4x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[2535]: ProgramUniform2f (will be remapped) */
   "iiff\0"
   "glProgramUniform2f\0"
   "glProgramUniform2fEXT\0"
   "\0"
   /* _mesa_function_pool[2582]: ProgramUniform2d (will be remapped) */
   "iidd\0"
   "glProgramUniform2d\0"
   "\0"
   /* _mesa_function_pool[2607]: ProgramUniform2i (will be remapped) */
   "iiii\0"
   "glProgramUniform2i\0"
   "glProgramUniform2iEXT\0"
   "\0"
   /* _mesa_function_pool[2654]: Fogx (will be remapped) */
   "ii\0"
   "glFogxOES\0"
   "glFogx\0"
   "\0"
   /* _mesa_function_pool[2675]: Fogf (offset 153) */
   "if\0"
   "glFogf\0"
   "\0"
   /* _mesa_function_pool[2686]: TexSubImage1D (offset 332) */
   "iiiiiip\0"
   "glTexSubImage1D\0"
   "glTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[2730]: Color4usv (offset 40) */
   "p\0"
   "glColor4usv\0"
   "\0"
   /* _mesa_function_pool[2745]: Fogi (offset 155) */
   "ii\0"
   "glFogi\0"
   "\0"
   /* _mesa_function_pool[2756]: FinalCombinerInputNV (dynamic) */
   "iiii\0"
   "glFinalCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[2785]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[2800]: GetSamplerParameterIiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[2830]: VertexArrayAttribLFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[2864]: VertexAttribI4uiEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4uiEXT\0"
   "glVertexAttribI4ui\0"
   "\0"
   /* _mesa_function_pool[2912]: DrawElementsInstancedBaseVertexBaseInstance (will be remapped) */
   "iiipiii\0"
   "glDrawElementsInstancedBaseVertexBaseInstance\0"
   "\0"
   /* _mesa_function_pool[2967]: ProgramEnvParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4dvARB\0"
   "glProgramParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[3024]: ColorTableParameteriv (offset 341) */
   "iip\0"
   "glColorTableParameteriv\0"
   "glColorTableParameterivSGI\0"
   "\0"
   /* _mesa_function_pool[3080]: BindSamplers (will be remapped) */
   "iip\0"
   "glBindSamplers\0"
   "\0"
   /* _mesa_function_pool[3100]: GetnCompressedTexImageARB (will be remapped) */
   "iiip\0"
   "glGetnCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[3134]: CopyNamedBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[3166]: BindSampler (will be remapped) */
   "ii\0"
   "glBindSampler\0"
   "\0"
   /* _mesa_function_pool[3184]: GetUniformuiv (will be remapped) */
   "iip\0"
   "glGetUniformuivEXT\0"
   "glGetUniformuiv\0"
   "\0"
   /* _mesa_function_pool[3224]: GetQueryBufferObjectuiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectuiv\0"
   "\0"
   /* _mesa_function_pool[3256]: MultiTexCoord2fARB (offset 386) */
   "iff\0"
   "glMultiTexCoord2f\0"
   "glMultiTexCoord2fARB\0"
   "\0"
   /* _mesa_function_pool[3300]: GetTextureImage (will be remapped) */
   "iiiiip\0"
   "glGetTextureImage\0"
   "\0"
   /* _mesa_function_pool[3326]: MultiTexCoord3iv (offset 397) */
   "ip\0"
   "glMultiTexCoord3iv\0"
   "glMultiTexCoord3ivARB\0"
   "\0"
   /* _mesa_function_pool[3371]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[3382]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[3400]: ClearColorIiEXT (will be remapped) */
   "iiii\0"
   "glClearColorIiEXT\0"
   "\0"
   /* _mesa_function_pool[3424]: LoadMatrixd (offset 292) */
   "p\0"
   "glLoadMatrixd\0"
   "\0"
   /* _mesa_function_pool[3441]: VDPAURegisterOutputSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterOutputSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[3478]: VertexP4ui (will be remapped) */
   "ii\0"
   "glVertexP4ui\0"
   "\0"
   /* _mesa_function_pool[3495]: GetProgramResourceIndex (will be remapped) */
   "iip\0"
   "glGetProgramResourceIndex\0"
   "\0"
   /* _mesa_function_pool[3526]: SpriteParameterfvSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[3554]: TextureStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[3593]: GetnUniformivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformivARB\0"
   "\0"
   /* _mesa_function_pool[3618]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[3644]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "glBlendFuncSeparateEXT\0"
   "glBlendFuncSeparateINGR\0"
   "glBlendFuncSeparateOES\0"
   "\0"
   /* _mesa_function_pool[3740]: Color3us (offset 23) */
   "iii\0"
   "glColor3us\0"
   "\0"
   /* _mesa_function_pool[3756]: LoadMatrixx (will be remapped) */
   "p\0"
   "glLoadMatrixxOES\0"
   "glLoadMatrixx\0"
   "\0"
   /* _mesa_function_pool[3790]: BufferStorage (will be remapped) */
   "iipi\0"
   "glBufferStorage\0"
   "\0"
   /* _mesa_function_pool[3812]: Color3ub (offset 19) */
   "iii\0"
   "glColor3ub\0"
   "\0"
   /* _mesa_function_pool[3828]: GetInstrumentsSGIX (dynamic) */
   "\0"
   "glGetInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[3851]: Color3ui (offset 21) */
   "iii\0"
   "glColor3ui\0"
   "\0"
   /* _mesa_function_pool[3867]: VertexAttrib4dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4dvNV\0"
   "\0"
   /* _mesa_function_pool[3891]: AlphaFragmentOp2ATI (will be remapped) */
   "iiiiiiiii\0"
   "glAlphaFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[3924]: RasterPos4dv (offset 79) */
   "p\0"
   "glRasterPos4dv\0"
   "\0"
   /* _mesa_function_pool[3942]: DeleteProgramPipelines (will be remapped) */
   "ip\0"
   "glDeleteProgramPipelines\0"
   "glDeleteProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[3999]: LineWidthx (will be remapped) */
   "i\0"
   "glLineWidthxOES\0"
   "glLineWidthx\0"
   "\0"
   /* _mesa_function_pool[4031]: GetTransformFeedbacki_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki_v\0"
   "\0"
   /* _mesa_function_pool[4063]: Indexdv (offset 45) */
   "p\0"
   "glIndexdv\0"
   "\0"
   /* _mesa_function_pool[4076]: GetnPixelMapfvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapfvARB\0"
   "\0"
   /* _mesa_function_pool[4101]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[4134]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[4149]: WindowPos4ivMESA (will be remapped) */
   "p\0"
   "glWindowPos4ivMESA\0"
   "\0"
   /* _mesa_function_pool[4171]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[4196]: BindFragmentShaderATI (will be remapped) */
   "i\0"
   "glBindFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[4223]: BlendFuncSeparateiARB (will be remapped) */
   "iiiii\0"
   "glBlendFuncSeparateiARB\0"
   "glBlendFuncSeparateIndexedAMD\0"
   "glBlendFuncSeparatei\0"
   "\0"
   /* _mesa_function_pool[4305]: PixelTexGenParameteriSGIS (dynamic) */
   "ii\0"
   "glPixelTexGenParameteriSGIS\0"
   "\0"
   /* _mesa_function_pool[4337]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[4380]: GenTransformFeedbacks (will be remapped) */
   "ip\0"
   "glGenTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[4408]: VertexPointer (offset 321) */
   "iiip\0"
   "glVertexPointer\0"
   "\0"
   /* _mesa_function_pool[4430]: GetCompressedTexImage (will be remapped) */
   "iip\0"
   "glGetCompressedTexImage\0"
   "glGetCompressedTexImageARB\0"
   "\0"
   /* _mesa_function_pool[4486]: ProgramLocalParameter4dvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4dvARB\0"
   "\0"
   /* _mesa_function_pool[4521]: UniformMatrix2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[4546]: GetQueryObjectui64v (will be remapped) */
   "iip\0"
   "glGetQueryObjectui64v\0"
   "glGetQueryObjectui64vEXT\0"
   "\0"
   /* _mesa_function_pool[4598]: VertexAttribP1uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP1uiv\0"
   "\0"
   /* _mesa_function_pool[4624]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[4639]: TexCoordPointerListIBM (dynamic) */
   "iiipi\0"
   "glTexCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4671]: ResizeBuffersMESA (will be remapped) */
   "\0"
   "glResizeBuffersMESA\0"
   "\0"
   /* _mesa_function_pool[4693]: BindBuffersBase (will be remapped) */
   "iiip\0"
   "glBindBuffersBase\0"
   "\0"
   /* _mesa_function_pool[4717]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "glGenTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[4752]: IndexPointerListIBM (dynamic) */
   "iipi\0"
   "glIndexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[4780]: UnmapNamedBuffer (will be remapped) */
   "i\0"
   "glUnmapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[4802]: UniformMatrix3x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[4829]: WindowPos4fMESA (will be remapped) */
   "ffff\0"
   "glWindowPos4fMESA\0"
   "\0"
   /* _mesa_function_pool[4853]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "glGenerateMipmapEXT\0"
   "glGenerateMipmapOES\0"
   "\0"
   /* _mesa_function_pool[4913]: VertexAttribP4ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP4ui\0"
   "\0"
   /* _mesa_function_pool[4938]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "glUniform4iARB\0"
   "\0"
   /* _mesa_function_pool[4972]: Uniform4d (will be remapped) */
   "idddd\0"
   "glUniform4d\0"
   "\0"
   /* _mesa_function_pool[4991]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "glUniform4fARB\0"
   "\0"
   /* _mesa_function_pool[5025]: ProgramUniform3dv (will be remapped) */
   "iiip\0"
   "glProgramUniform3dv\0"
   "\0"
   /* _mesa_function_pool[5051]: GetNamedBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[5086]: NamedFramebufferTexture (will be remapped) */
   "iiii\0"
   "glNamedFramebufferTexture\0"
   "\0"
   /* _mesa_function_pool[5118]: ProgramUniform3d (will be remapped) */
   "iiddd\0"
   "glProgramUniform3d\0"
   "\0"
   /* _mesa_function_pool[5144]: ProgramUniform3f (will be remapped) */
   "iifff\0"
   "glProgramUniform3f\0"
   "glProgramUniform3fEXT\0"
   "\0"
   /* _mesa_function_pool[5192]: ProgramUniform3i (will be remapped) */
   "iiiii\0"
   "glProgramUniform3i\0"
   "glProgramUniform3iEXT\0"
   "\0"
   /* _mesa_function_pool[5240]: PointParameterfv (will be remapped) */
   "ip\0"
   "glPointParameterfv\0"
   "glPointParameterfvARB\0"
   "glPointParameterfvEXT\0"
   "glPointParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[5330]: GetHistogramParameterfv (offset 362) */
   "iip\0"
   "glGetHistogramParameterfv\0"
   "glGetHistogramParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[5390]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[5405]: ColorPointervINTEL (dynamic) */
   "iip\0"
   "glColorPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[5431]: VDPAUUnmapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUUnmapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[5458]: GetnHistogramARB (will be remapped) */
   "iiiiip\0"
   "glGetnHistogramARB\0"
   "\0"
   /* _mesa_function_pool[5485]: ReplacementCodeuiColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[5538]: SecondaryColor3s (will be remapped) */
   "iii\0"
   "glSecondaryColor3s\0"
   "glSecondaryColor3sEXT\0"
   "\0"
   /* _mesa_function_pool[5584]: VertexAttribP2uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP2uiv\0"
   "\0"
   /* _mesa_function_pool[5610]: UniformMatrix3x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[5637]: VertexAttrib3fNV (will be remapped) */
   "ifff\0"
   "glVertexAttrib3fNV\0"
   "\0"
   /* _mesa_function_pool[5662]: SecondaryColor3b (will be remapped) */
   "iii\0"
   "glSecondaryColor3b\0"
   "glSecondaryColor3bEXT\0"
   "\0"
   /* _mesa_function_pool[5708]: EnableClientState (offset 313) */
   "i\0"
   "glEnableClientState\0"
   "\0"
   /* _mesa_function_pool[5731]: Color4ubVertex2fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex2fvSUN\0"
   "\0"
   /* _mesa_function_pool[5758]: GetActiveSubroutineName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineName\0"
   "\0"
   /* _mesa_function_pool[5792]: SecondaryColor3i (will be remapped) */
   "iii\0"
   "glSecondaryColor3i\0"
   "glSecondaryColor3iEXT\0"
   "\0"
   /* _mesa_function_pool[5838]: TexFilterFuncSGIS (dynamic) */
   "iiip\0"
   "glTexFilterFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5864]: GetFragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[5897]: DetailTexFuncSGIS (dynamic) */
   "iip\0"
   "glDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[5922]: FlushMappedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRange\0"
   "glFlushMappedBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[5980]: Lightfv (offset 160) */
   "iip\0"
   "glLightfv\0"
   "\0"
   /* _mesa_function_pool[5995]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "glGetFramebufferAttachmentParameterivEXT\0"
   "glGetFramebufferAttachmentParameterivOES\0"
   "\0"
   /* _mesa_function_pool[6121]: ColorSubTable (offset 346) */
   "iiiiip\0"
   "glColorSubTable\0"
   "glColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[6164]: GetVertexArrayIndexed64iv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexed64iv\0"
   "\0"
   /* _mesa_function_pool[6198]: EndPerfMonitorAMD (will be remapped) */
   "i\0"
   "glEndPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[6221]: ReadInstrumentsSGIX (dynamic) */
   "i\0"
   "glReadInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[6246]: CreateBuffers (will be remapped) */
   "ip\0"
   "glCreateBuffers\0"
   "\0"
   /* _mesa_function_pool[6266]: MapParameterivNV (dynamic) */
   "iip\0"
   "glMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[6290]: GetMultisamplefv (will be remapped) */
   "iip\0"
   "glGetMultisamplefv\0"
   "\0"
   /* _mesa_function_pool[6314]: WeightbvARB (dynamic) */
   "ip\0"
   "glWeightbvARB\0"
   "\0"
   /* _mesa_function_pool[6332]: GetActiveSubroutineUniformName (will be remapped) */
   "iiiipp\0"
   "glGetActiveSubroutineUniformName\0"
   "\0"
   /* _mesa_function_pool[6373]: Rectdv (offset 87) */
   "pp\0"
   "glRectdv\0"
   "\0"
   /* _mesa_function_pool[6386]: DrawArraysInstancedARB (will be remapped) */
   "iiii\0"
   "glDrawArraysInstancedARB\0"
   "glDrawArraysInstancedEXT\0"
   "glDrawArraysInstanced\0"
   "\0"
   /* _mesa_function_pool[6464]: ProgramEnvParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramEnvParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[6499]: VertexAttrib2svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2svNV\0"
   "\0"
   /* _mesa_function_pool[6523]: SecondaryColorP3uiv (will be remapped) */
   "ip\0"
   "glSecondaryColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[6549]: GetnPixelMapuivARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapuivARB\0"
   "\0"
   /* _mesa_function_pool[6575]: GetSamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[6606]: Disablei (will be remapped) */
   "ii\0"
   "glDisableIndexedEXT\0"
   "glDisablei\0"
   "\0"
   /* _mesa_function_pool[6641]: CompressedTexSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3D\0"
   "glCompressedTexSubImage3DARB\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[6738]: WindowPos4svMESA (will be remapped) */
   "p\0"
   "glWindowPos4svMESA\0"
   "\0"
   /* _mesa_function_pool[6760]: ObjectLabel (will be remapped) */
   "iiip\0"
   "glObjectLabel\0"
   "\0"
   /* _mesa_function_pool[6780]: Color3dv (offset 12) */
   "p\0"
   "glColor3dv\0"
   "\0"
   /* _mesa_function_pool[6794]: BeginQuery (will be remapped) */
   "ii\0"
   "glBeginQuery\0"
   "glBeginQueryARB\0"
   "\0"
   /* _mesa_function_pool[6827]: VertexP3uiv (will be remapped) */
   "ip\0"
   "glVertexP3uiv\0"
   "\0"
   /* _mesa_function_pool[6845]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "glGetUniformLocationARB\0"
   "\0"
   /* _mesa_function_pool[6894]: PixelStoref (offset 249) */
   "if\0"
   "glPixelStoref\0"
   "\0"
   /* _mesa_function_pool[6912]: WindowPos2iv (will be remapped) */
   "p\0"
   "glWindowPos2iv\0"
   "glWindowPos2ivARB\0"
   "glWindowPos2ivMESA\0"
   "\0"
   /* _mesa_function_pool[6967]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[6985]: GetNamedFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetNamedFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[7034]: VertexAttribs1svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1svNV\0"
   "\0"
   /* _mesa_function_pool[7060]: CheckNamedFramebufferStatus (will be remapped) */
   "ii\0"
   "glCheckNamedFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[7094]: RequestResidentProgramsNV (will be remapped) */
   "ip\0"
   "glRequestResidentProgramsNV\0"
   "\0"
   /* _mesa_function_pool[7126]: UniformSubroutinesuiv (will be remapped) */
   "iip\0"
   "glUniformSubroutinesuiv\0"
   "\0"
   /* _mesa_function_pool[7155]: ListParameterivSGIX (dynamic) */
   "iip\0"
   "glListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[7182]: TexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7228]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "glCheckFramebufferStatusEXT\0"
   "glCheckFramebufferStatusOES\0"
   "\0"
   /* _mesa_function_pool[7312]: DispatchComputeIndirect (will be remapped) */
   "i\0"
   "glDispatchComputeIndirect\0"
   "\0"
   /* _mesa_function_pool[7341]: InvalidateBufferData (will be remapped) */
   "i\0"
   "glInvalidateBufferData\0"
   "\0"
   /* _mesa_function_pool[7367]: GetUniformdv (will be remapped) */
   "iip\0"
   "glGetUniformdv\0"
   "\0"
   /* _mesa_function_pool[7387]: ProgramLocalParameters4fvEXT (will be remapped) */
   "iiip\0"
   "glProgramLocalParameters4fvEXT\0"
   "\0"
   /* _mesa_function_pool[7424]: VertexAttribL1dv (will be remapped) */
   "ip\0"
   "glVertexAttribL1dv\0"
   "\0"
   /* _mesa_function_pool[7447]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "glIsFramebufferEXT\0"
   "glIsFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[7504]: GetPixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[7540]: GetDoublev (offset 260) */
   "ip\0"
   "glGetDoublev\0"
   "\0"
   /* _mesa_function_pool[7557]: GetObjectLabel (will be remapped) */
   "iiipp\0"
   "glGetObjectLabel\0"
   "\0"
   /* _mesa_function_pool[7581]: ColorP3uiv (will be remapped) */
   "ip\0"
   "glColorP3uiv\0"
   "\0"
   /* _mesa_function_pool[7598]: CombinerParameteriNV (dynamic) */
   "ii\0"
   "glCombinerParameteriNV\0"
   "\0"
   /* _mesa_function_pool[7625]: GetTextureSubImage (will be remapped) */
   "iiiiiiiiiiip\0"
   "glGetTextureSubImage\0"
   "\0"
   /* _mesa_function_pool[7660]: Normal3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[7687]: VertexAttribI4ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4ivEXT\0"
   "glVertexAttribI4iv\0"
   "\0"
   /* _mesa_function_pool[7732]: SecondaryColor3ubv (will be remapped) */
   "p\0"
   "glSecondaryColor3ubv\0"
   "glSecondaryColor3ubvEXT\0"
   "\0"
   /* _mesa_function_pool[7780]: GetDebugMessageLog (will be remapped) */
   "iipppppp\0"
   "glGetDebugMessageLogARB\0"
   "glGetDebugMessageLog\0"
   "\0"
   /* _mesa_function_pool[7835]: DeformationMap3fSGIX (dynamic) */
   "iffiiffiiffiip\0"
   "glDeformationMap3fSGIX\0"
   "\0"
   /* _mesa_function_pool[7874]: MatrixIndexubvARB (dynamic) */
   "ip\0"
   "glMatrixIndexubvARB\0"
   "\0"
   /* _mesa_function_pool[7898]: Color4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffff\0"
   "glColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[7939]: PixelTexGenParameterfSGIS (dynamic) */
   "if\0"
   "glPixelTexGenParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[7971]: ProgramUniform2ui (will be remapped) */
   "iiii\0"
   "glProgramUniform2ui\0"
   "glProgramUniform2uiEXT\0"
   "\0"
   /* _mesa_function_pool[8020]: TexCoord2fVertex3fvSUN (dynamic) */
   "pp\0"
   "glTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8049]: Color4ubVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[8076]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "glGetShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[8121]: BindProgramARB (will be remapped) */
   "ii\0"
   "glBindProgramARB\0"
   "glBindProgramNV\0"
   "\0"
   /* _mesa_function_pool[8158]: VertexAttrib3sNV (will be remapped) */
   "iiii\0"
   "glVertexAttrib3sNV\0"
   "\0"
   /* _mesa_function_pool[8183]: ColorFragmentOp1ATI (will be remapped) */
   "iiiiiii\0"
   "glColorFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[8214]: ProgramUniformMatrix4x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3fv\0"
   "glProgramUniformMatrix4x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[8280]: PopClientAttrib (offset 334) */
   "\0"
   "glPopClientAttrib\0"
   "\0"
   /* _mesa_function_pool[8300]: DrawElementsInstancedARB (will be remapped) */
   "iiipi\0"
   "glDrawElementsInstancedARB\0"
   "glDrawElementsInstancedEXT\0"
   "glDrawElementsInstanced\0"
   "\0"
   /* _mesa_function_pool[8385]: GetQueryObjectuiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectuiv\0"
   "glGetQueryObjectuivARB\0"
   "\0"
   /* _mesa_function_pool[8433]: VertexAttribI4bv (will be remapped) */
   "ip\0"
   "glVertexAttribI4bvEXT\0"
   "glVertexAttribI4bv\0"
   "\0"
   /* _mesa_function_pool[8478]: FogCoordPointerListIBM (dynamic) */
   "iipi\0"
   "glFogCoordPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[8509]: DisableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glDisableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[8540]: VertexAttribL4d (will be remapped) */
   "idddd\0"
   "glVertexAttribL4d\0"
   "\0"
   /* _mesa_function_pool[8565]: Binormal3sEXT (dynamic) */
   "iii\0"
   "glBinormal3sEXT\0"
   "\0"
   /* _mesa_function_pool[8586]: ListBase (offset 6) */
   "i\0"
   "glListBase\0"
   "\0"
   /* _mesa_function_pool[8600]: VertexAttribs2fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2fvNV\0"
   "\0"
   /* _mesa_function_pool[8626]: BindBufferRange (will be remapped) */
   "iiiii\0"
   "glBindBufferRange\0"
   "glBindBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[8672]: ProgramUniformMatrix2x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x4fv\0"
   "glProgramUniformMatrix2x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[8738]: BindBufferBase (will be remapped) */
   "iii\0"
   "glBindBufferBase\0"
   "glBindBufferBaseEXT\0"
   "\0"
   /* _mesa_function_pool[8780]: GetQueryObjectiv (will be remapped) */
   "iip\0"
   "glGetQueryObjectiv\0"
   "glGetQueryObjectivARB\0"
   "\0"
   /* _mesa_function_pool[8826]: VertexAttrib2s (will be remapped) */
   "iii\0"
   "glVertexAttrib2s\0"
   "glVertexAttrib2sARB\0"
   "\0"
   /* _mesa_function_pool[8868]: SecondaryColor3fvEXT (will be remapped) */
   "p\0"
   "glSecondaryColor3fv\0"
   "glSecondaryColor3fvEXT\0"
   "\0"
   /* _mesa_function_pool[8914]: VertexAttrib2d (will be remapped) */
   "idd\0"
   "glVertexAttrib2d\0"
   "glVertexAttrib2dARB\0"
   "\0"
   /* _mesa_function_pool[8956]: ClearNamedFramebufferiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferiv\0"
   "\0"
   /* _mesa_function_pool[8988]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "glUniform1fvARB\0"
   "\0"
   /* _mesa_function_pool[9022]: GetProgramPipelineInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramPipelineInfoLog\0"
   "glGetProgramPipelineInfoLogEXT\0"
   "\0"
   /* _mesa_function_pool[9087]: TextureMaterialEXT (dynamic) */
   "ii\0"
   "glTextureMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[9112]: DepthBoundsEXT (will be remapped) */
   "dd\0"
   "glDepthBoundsEXT\0"
   "\0"
   /* _mesa_function_pool[9133]: WindowPos3fv (will be remapped) */
   "p\0"
   "glWindowPos3fv\0"
   "glWindowPos3fvARB\0"
   "glWindowPos3fvMESA\0"
   "\0"
   /* _mesa_function_pool[9188]: BindVertexArrayAPPLE (will be remapped) */
   "i\0"
   "glBindVertexArrayAPPLE\0"
   "\0"
   /* _mesa_function_pool[9214]: GetHistogramParameteriv (offset 363) */
   "iip\0"
   "glGetHistogramParameteriv\0"
   "glGetHistogramParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[9274]: PointParameteriv (will be remapped) */
   "ip\0"
   "glPointParameteriv\0"
   "glPointParameterivNV\0"
   "\0"
   /* _mesa_function_pool[9318]: NamedRenderbufferStorage (will be remapped) */
   "iiii\0"
   "glNamedRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[9351]: GetProgramivARB (will be remapped) */
   "iip\0"
   "glGetProgramivARB\0"
   "\0"
   /* _mesa_function_pool[9374]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "glBindRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[9419]: SecondaryColor3fEXT (will be remapped) */
   "fff\0"
   "glSecondaryColor3f\0"
   "glSecondaryColor3fEXT\0"
   "\0"
   /* _mesa_function_pool[9465]: PrimitiveRestartIndex (will be remapped) */
   "i\0"
   "glPrimitiveRestartIndex\0"
   "glPrimitiveRestartIndexNV\0"
   "\0"
   /* _mesa_function_pool[9518]: VertexAttribI4ubv (will be remapped) */
   "ip\0"
   "glVertexAttribI4ubvEXT\0"
   "glVertexAttribI4ubv\0"
   "\0"
   /* _mesa_function_pool[9565]: GetGraphicsResetStatusARB (will be remapped) */
   "\0"
   "glGetGraphicsResetStatusARB\0"
   "\0"
   /* _mesa_function_pool[9595]: CreateRenderbuffers (will be remapped) */
   "ip\0"
   "glCreateRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[9621]: ActiveStencilFaceEXT (will be remapped) */
   "i\0"
   "glActiveStencilFaceEXT\0"
   "\0"
   /* _mesa_function_pool[9647]: VertexAttrib4dNV (will be remapped) */
   "idddd\0"
   "glVertexAttrib4dNV\0"
   "\0"
   /* _mesa_function_pool[9673]: DepthRange (offset 288) */
   "dd\0"
   "glDepthRange\0"
   "\0"
   /* _mesa_function_pool[9690]: TexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[9718]: VertexAttrib4fNV (will be remapped) */
   "iffff\0"
   "glVertexAttrib4fNV\0"
   "\0"
   /* _mesa_function_pool[9744]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "glUniform4fvARB\0"
   "\0"
   /* _mesa_function_pool[9778]: DrawMeshArraysSUN (dynamic) */
   "iiii\0"
   "glDrawMeshArraysSUN\0"
   "\0"
   /* _mesa_function_pool[9804]: SamplerParameterIiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIiv\0"
   "\0"
   /* _mesa_function_pool[9831]: GetMapControlPointsNV (dynamic) */
   "iiiiiip\0"
   "glGetMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[9864]: SpriteParameterivSGIX (dynamic) */
   "ip\0"
   "glSpriteParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[9892]: Frustumf (will be remapped) */
   "ffffff\0"
   "glFrustumfOES\0"
   "glFrustumf\0"
   "\0"
   /* _mesa_function_pool[9925]: GetQueryBufferObjectui64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectui64v\0"
   "\0"
   /* _mesa_function_pool[9959]: ProgramUniform2uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform2uiv\0"
   "glProgramUniform2uivEXT\0"
   "\0"
   /* _mesa_function_pool[10010]: Rectsv (offset 93) */
   "pp\0"
   "glRectsv\0"
   "\0"
   /* _mesa_function_pool[10023]: Frustumx (will be remapped) */
   "iiiiii\0"
   "glFrustumxOES\0"
   "glFrustumx\0"
   "\0"
   /* _mesa_function_pool[10056]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[10070]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "glBindTextureEXT\0"
   "\0"
   /* _mesa_function_pool[10105]: MultiTexCoord4fARB (offset 402) */
   "iffff\0"
   "glMultiTexCoord4f\0"
   "glMultiTexCoord4fARB\0"
   "\0"
   /* _mesa_function_pool[10151]: MultiTexCoordP2uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[10177]: BeginPerfQueryINTEL (will be remapped) */
   "i\0"
   "glBeginPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[10202]: NormalPointer (offset 318) */
   "iip\0"
   "glNormalPointer\0"
   "\0"
   /* _mesa_function_pool[10223]: TangentPointerEXT (dynamic) */
   "iip\0"
   "glTangentPointerEXT\0"
   "\0"
   /* _mesa_function_pool[10248]: WindowPos4iMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4iMESA\0"
   "\0"
   /* _mesa_function_pool[10272]: ReferencePlaneSGIX (dynamic) */
   "p\0"
   "glReferencePlaneSGIX\0"
   "\0"
   /* _mesa_function_pool[10296]: VertexAttrib4bv (will be remapped) */
   "ip\0"
   "glVertexAttrib4bv\0"
   "glVertexAttrib4bvARB\0"
   "\0"
   /* _mesa_function_pool[10339]: ReplacementCodeuivSUN (dynamic) */
   "p\0"
   "glReplacementCodeuivSUN\0"
   "\0"
   /* _mesa_function_pool[10366]: SecondaryColor3usv (will be remapped) */
   "p\0"
   "glSecondaryColor3usv\0"
   "glSecondaryColor3usvEXT\0"
   "\0"
   /* _mesa_function_pool[10414]: GetPixelMapuiv (offset 272) */
   "ip\0"
   "glGetPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[10435]: MapNamedBuffer (will be remapped) */
   "ii\0"
   "glMapNamedBuffer\0"
   "\0"
   /* _mesa_function_pool[10456]: Indexfv (offset 47) */
   "p\0"
   "glIndexfv\0"
   "\0"
   /* _mesa_function_pool[10469]: AlphaFragmentOp1ATI (will be remapped) */
   "iiiiii\0"
   "glAlphaFragmentOp1ATI\0"
   "\0"
   /* _mesa_function_pool[10499]: ListParameteriSGIX (dynamic) */
   "iii\0"
   "glListParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[10525]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[10541]: ProgramUniform2dv (will be remapped) */
   "iiip\0"
   "glProgramUniform2dv\0"
   "\0"
   /* _mesa_function_pool[10567]: MultiTexCoord3i (offset 396) */
   "iiii\0"
   "glMultiTexCoord3i\0"
   "glMultiTexCoord3iARB\0"
   "\0"
   /* _mesa_function_pool[10612]: ProgramUniform1fv (will be remapped) */
   "iiip\0"
   "glProgramUniform1fv\0"
   "glProgramUniform1fvEXT\0"
   "\0"
   /* _mesa_function_pool[10661]: MultiTexCoord3d (offset 392) */
   "iddd\0"
   "glMultiTexCoord3d\0"
   "glMultiTexCoord3dARB\0"
   "\0"
   /* _mesa_function_pool[10706]: TexCoord3sv (offset 117) */
   "p\0"
   "glTexCoord3sv\0"
   "\0"
   /* _mesa_function_pool[10723]: Fogfv (offset 154) */
   "ip\0"
   "glFogfv\0"
   "\0"
   /* _mesa_function_pool[10735]: Minmax (offset 368) */
   "iii\0"
   "glMinmax\0"
   "glMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[10761]: MultiTexCoord3s (offset 398) */
   "iiii\0"
   "glMultiTexCoord3s\0"
   "glMultiTexCoord3sARB\0"
   "\0"
   /* _mesa_function_pool[10806]: FinishTextureSUNX (dynamic) */
   "\0"
   "glFinishTextureSUNX\0"
   "\0"
   /* _mesa_function_pool[10828]: GetFinalCombinerInputParameterfvNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[10870]: PollInstrumentsSGIX (dynamic) */
   "p\0"
   "glPollInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[10895]: Vertex4iv (offset 147) */
   "p\0"
   "glVertex4iv\0"
   "\0"
   /* _mesa_function_pool[10910]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "glBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[10951]: AlphaFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiii\0"
   "glAlphaFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[10987]: Normal3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[11017]: Begin (offset 7) */
   "i\0"
   "glBegin\0"
   "\0"
   /* _mesa_function_pool[11028]: LightModeli (offset 165) */
   "ii\0"
   "glLightModeli\0"
   "\0"
   /* _mesa_function_pool[11046]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "glUniformMatrix2fvARB\0"
   "\0"
   /* _mesa_function_pool[11093]: LightModelf (offset 163) */
   "if\0"
   "glLightModelf\0"
   "\0"
   /* _mesa_function_pool[11111]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[11136]: TextureStorage1D (will be remapped) */
   "iiii\0"
   "glTextureStorage1D\0"
   "\0"
   /* _mesa_function_pool[11161]: BinormalPointerEXT (dynamic) */
   "iip\0"
   "glBinormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[11187]: GetCombinerInputParameterivNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[11226]: DeleteAsyncMarkersSGIX (dynamic) */
   "ii\0"
   "glDeleteAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[11255]: MultiTexCoord2fvARB (offset 387) */
   "ip\0"
   "glMultiTexCoord2fv\0"
   "glMultiTexCoord2fvARB\0"
   "\0"
   /* _mesa_function_pool[11300]: VertexAttrib4ubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubv\0"
   "glVertexAttrib4ubvARB\0"
   "\0"
   /* _mesa_function_pool[11345]: GetnTexImageARB (will be remapped) */
   "iiiiip\0"
   "glGetnTexImageARB\0"
   "\0"
   /* _mesa_function_pool[11371]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[11389]: GenAsyncMarkersSGIX (dynamic) */
   "i\0"
   "glGenAsyncMarkersSGIX\0"
   "\0"
   /* _mesa_function_pool[11414]: MultiTexCoord4x (will be remapped) */
   "iiiii\0"
   "glMultiTexCoord4xOES\0"
   "glMultiTexCoord4x\0"
   "\0"
   /* _mesa_function_pool[11460]: ReplacementCodeuiVertex3fSUN (dynamic) */
   "ifff\0"
   "glReplacementCodeuiVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[11497]: VertexAttribs4svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4svNV\0"
   "\0"
   /* _mesa_function_pool[11523]: DrawElementsInstancedBaseInstance (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[11567]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "glUniformMatrix4fvARB\0"
   "\0"
   /* _mesa_function_pool[11614]: UniformMatrix3x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x2fv\0"
   "\0"
   /* _mesa_function_pool[11641]: VertexAttrib4Nuiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nuiv\0"
   "glVertexAttrib4NuivARB\0"
   "\0"
   /* _mesa_function_pool[11688]: ClientActiveTexture (offset 375) */
   "i\0"
   "glClientActiveTexture\0"
   "glClientActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[11738]: GetUniformIndices (will be remapped) */
   "iipp\0"
   "glGetUniformIndices\0"
   "\0"
   /* _mesa_function_pool[11764]: GetTexBumpParameterivATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterivATI\0"
   "\0"
   /* _mesa_function_pool[11795]: Binormal3bEXT (dynamic) */
   "iii\0"
   "glBinormal3bEXT\0"
   "\0"
   /* _mesa_function_pool[11816]: CombinerParameterivNV (dynamic) */
   "ip\0"
   "glCombinerParameterivNV\0"
   "\0"
   /* _mesa_function_pool[11844]: MultiTexCoord2sv (offset 391) */
   "ip\0"
   "glMultiTexCoord2sv\0"
   "glMultiTexCoord2svARB\0"
   "\0"
   /* _mesa_function_pool[11889]: NamedBufferStorage (will be remapped) */
   "iipi\0"
   "glNamedBufferStorage\0"
   "\0"
   /* _mesa_function_pool[11916]: NamedFramebufferDrawBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[11949]: NamedFramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glNamedFramebufferTextureLayer\0"
   "\0"
   /* _mesa_function_pool[11987]: LoadIdentity (offset 290) */
   "\0"
   "glLoadIdentity\0"
   "\0"
   /* _mesa_function_pool[12004]: ActiveShaderProgram (will be remapped) */
   "ii\0"
   "glActiveShaderProgram\0"
   "glActiveShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[12055]: BindImageTextures (will be remapped) */
   "iip\0"
   "glBindImageTextures\0"
   "\0"
   /* _mesa_function_pool[12080]: DeleteTransformFeedbacks (will be remapped) */
   "ip\0"
   "glDeleteTransformFeedbacks\0"
   "\0"
   /* _mesa_function_pool[12111]: VertexAttrib4ubvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4ubvNV\0"
   "\0"
   /* _mesa_function_pool[12136]: FogCoordfEXT (will be remapped) */
   "f\0"
   "glFogCoordf\0"
   "glFogCoordfEXT\0"
   "\0"
   /* _mesa_function_pool[12166]: GetMapfv (offset 267) */
   "iip\0"
   "glGetMapfv\0"
   "\0"
   /* _mesa_function_pool[12182]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[12208]: BindTransformFeedback (will be remapped) */
   "ii\0"
   "glBindTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[12236]: TexCoord4fColor4fNormal3fVertex4fvSUN (dynamic) */
   "pppp\0"
   "glTexCoord4fColor4fNormal3fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[12282]: GetPixelMapfv (offset 271) */
   "ip\0"
   "glGetPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[12302]: TextureBufferRange (will be remapped) */
   "iiiii\0"
   "glTextureBufferRange\0"
   "\0"
   /* _mesa_function_pool[12330]: WeightivARB (dynamic) */
   "ip\0"
   "glWeightivARB\0"
   "\0"
   /* _mesa_function_pool[12348]: VertexAttrib4svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4svNV\0"
   "\0"
   /* _mesa_function_pool[12372]: PatchParameteri (will be remapped) */
   "ii\0"
   "glPatchParameteri\0"
   "\0"
   /* _mesa_function_pool[12394]: ReplacementCodeuiTexCoord2fVertex3fSUN (dynamic) */
   "ifffff\0"
   "glReplacementCodeuiTexCoord2fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[12443]: GetNamedBufferSubData (will be remapped) */
   "iiip\0"
   "glGetNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[12473]: VDPAUSurfaceAccessNV (will be remapped) */
   "ii\0"
   "glVDPAUSurfaceAccessNV\0"
   "\0"
   /* _mesa_function_pool[12500]: EdgeFlagPointer (offset 312) */
   "ip\0"
   "glEdgeFlagPointer\0"
   "\0"
   /* _mesa_function_pool[12522]: WindowPos2f (will be remapped) */
   "ff\0"
   "glWindowPos2f\0"
   "glWindowPos2fARB\0"
   "glWindowPos2fMESA\0"
   "\0"
   /* _mesa_function_pool[12575]: WindowPos2d (will be remapped) */
   "dd\0"
   "glWindowPos2d\0"
   "glWindowPos2dARB\0"
   "glWindowPos2dMESA\0"
   "\0"
   /* _mesa_function_pool[12628]: GetVertexAttribLdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribLdv\0"
   "\0"
   /* _mesa_function_pool[12654]: WindowPos2i (will be remapped) */
   "ii\0"
   "glWindowPos2i\0"
   "glWindowPos2iARB\0"
   "glWindowPos2iMESA\0"
   "\0"
   /* _mesa_function_pool[12707]: WindowPos2s (will be remapped) */
   "ii\0"
   "glWindowPos2s\0"
   "glWindowPos2sARB\0"
   "glWindowPos2sMESA\0"
   "\0"
   /* _mesa_function_pool[12760]: VertexAttribI1uiEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1uiEXT\0"
   "glVertexAttribI1ui\0"
   "\0"
   /* _mesa_function_pool[12805]: DeleteSync (will be remapped) */
   "i\0"
   "glDeleteSync\0"
   "\0"
   /* _mesa_function_pool[12821]: WindowPos4fvMESA (will be remapped) */
   "p\0"
   "glWindowPos4fvMESA\0"
   "\0"
   /* _mesa_function_pool[12843]: CompressedTexImage3D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3D\0"
   "glCompressedTexImage3DARB\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[12929]: VertexAttribI1uiv (will be remapped) */
   "ip\0"
   "glVertexAttribI1uivEXT\0"
   "glVertexAttribI1uiv\0"
   "\0"
   /* _mesa_function_pool[12976]: SecondaryColor3dv (will be remapped) */
   "p\0"
   "glSecondaryColor3dv\0"
   "glSecondaryColor3dvEXT\0"
   "\0"
   /* _mesa_function_pool[13022]: GetListParameterivSGIX (dynamic) */
   "iip\0"
   "glGetListParameterivSGIX\0"
   "\0"
   /* _mesa_function_pool[13052]: GetnPixelMapusvARB (will be remapped) */
   "iip\0"
   "glGetnPixelMapusvARB\0"
   "\0"
   /* _mesa_function_pool[13078]: VertexAttrib3s (will be remapped) */
   "iiii\0"
   "glVertexAttrib3s\0"
   "glVertexAttrib3sARB\0"
   "\0"
   /* _mesa_function_pool[13121]: UniformMatrix4x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3fv\0"
   "\0"
   /* _mesa_function_pool[13148]: Binormal3dEXT (dynamic) */
   "ddd\0"
   "glBinormal3dEXT\0"
   "\0"
   /* _mesa_function_pool[13169]: GetQueryiv (will be remapped) */
   "iip\0"
   "glGetQueryiv\0"
   "glGetQueryivARB\0"
   "\0"
   /* _mesa_function_pool[13203]: VertexAttrib3d (will be remapped) */
   "iddd\0"
   "glVertexAttrib3d\0"
   "glVertexAttrib3dARB\0"
   "\0"
   /* _mesa_function_pool[13246]: ImageTransformParameterfHP (dynamic) */
   "iif\0"
   "glImageTransformParameterfHP\0"
   "\0"
   /* _mesa_function_pool[13280]: MapNamedBufferRange (will be remapped) */
   "iiii\0"
   "glMapNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[13308]: MapBuffer (will be remapped) */
   "ii\0"
   "glMapBuffer\0"
   "glMapBufferARB\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[13354]: GetProgramStageiv (will be remapped) */
   "iiip\0"
   "glGetProgramStageiv\0"
   "\0"
   /* _mesa_function_pool[13380]: VertexAttrib4Nbv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nbv\0"
   "glVertexAttrib4NbvARB\0"
   "\0"
   /* _mesa_function_pool[13425]: ProgramBinary (will be remapped) */
   "iipi\0"
   "glProgramBinary\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[13466]: InvalidateTexImage (will be remapped) */
   "ii\0"
   "glInvalidateTexImage\0"
   "\0"
   /* _mesa_function_pool[13491]: Uniform4ui (will be remapped) */
   "iiiii\0"
   "glUniform4uiEXT\0"
   "glUniform4ui\0"
   "\0"
   /* _mesa_function_pool[13527]: VertexAttrib1fARB (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "glVertexAttrib1fARB\0"
   "\0"
   /* _mesa_function_pool[13568]: GetBooleani_v (will be remapped) */
   "iip\0"
   "glGetBooleanIndexedvEXT\0"
   "glGetBooleani_v\0"
   "\0"
   /* _mesa_function_pool[13613]: DrawTexsOES (will be remapped) */
   "iiiii\0"
   "glDrawTexsOES\0"
   "\0"
   /* _mesa_function_pool[13634]: GetObjectPtrLabel (will be remapped) */
   "pipp\0"
   "glGetObjectPtrLabel\0"
   "\0"
   /* _mesa_function_pool[13660]: ProgramParameteri (will be remapped) */
   "iii\0"
   "glProgramParameteriARB\0"
   "glProgramParameteri\0"
   "glProgramParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[13731]: SecondaryColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glSecondaryColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[13769]: Color3fv (offset 14) */
   "p\0"
   "glColor3fv\0"
   "\0"
   /* _mesa_function_pool[13783]: ReplacementCodeubSUN (dynamic) */
   "i\0"
   "glReplacementCodeubSUN\0"
   "\0"
   /* _mesa_function_pool[13809]: GetnMapfvARB (will be remapped) */
   "iiip\0"
   "glGetnMapfvARB\0"
   "\0"
   /* _mesa_function_pool[13830]: MultiTexCoord2i (offset 388) */
   "iii\0"
   "glMultiTexCoord2i\0"
   "glMultiTexCoord2iARB\0"
   "\0"
   /* _mesa_function_pool[13874]: MultiTexCoord2d (offset 384) */
   "idd\0"
   "glMultiTexCoord2d\0"
   "glMultiTexCoord2dARB\0"
   "\0"
   /* _mesa_function_pool[13918]: SamplerParameterIuiv (will be remapped) */
   "iip\0"
   "glSamplerParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[13946]: MultiTexCoord2s (offset 390) */
   "iii\0"
   "glMultiTexCoord2s\0"
   "glMultiTexCoord2sARB\0"
   "\0"
   /* _mesa_function_pool[13990]: VDPAURegisterVideoSurfaceNV (will be remapped) */
   "piip\0"
   "glVDPAURegisterVideoSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[14026]: TexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "ffffffffffff\0"
   "glTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[14079]: Indexub (offset 315) */
   "i\0"
   "glIndexub\0"
   "\0"
   /* _mesa_function_pool[14092]: GetPerfMonitorCounterDataAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterDataAMD\0"
   "\0"
   /* _mesa_function_pool[14130]: MultTransposeMatrixf (will be remapped) */
   "p\0"
   "glMultTransposeMatrixf\0"
   "glMultTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[14182]: PolygonOffsetEXT (will be remapped) */
   "ff\0"
   "glPolygonOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[14205]: Scalex (will be remapped) */
   "iii\0"
   "glScalexOES\0"
   "glScalex\0"
   "\0"
   /* _mesa_function_pool[14231]: Scaled (offset 301) */
   "ddd\0"
   "glScaled\0"
   "\0"
   /* _mesa_function_pool[14245]: Scalef (offset 302) */
   "fff\0"
   "glScalef\0"
   "\0"
   /* _mesa_function_pool[14259]: IndexPointerEXT (will be remapped) */
   "iiip\0"
   "glIndexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[14283]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "glGetUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[14321]: ColorFragmentOp2ATI (will be remapped) */
   "iiiiiiiiii\0"
   "glColorFragmentOp2ATI\0"
   "\0"
   /* _mesa_function_pool[14355]: VertexAttrib2sNV (will be remapped) */
   "iii\0"
   "glVertexAttrib2sNV\0"
   "\0"
   /* _mesa_function_pool[14379]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[14401]: NormalPointerListIBM (dynamic) */
   "iipi\0"
   "glNormalPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[14430]: QueryCounter (will be remapped) */
   "ii\0"
   "glQueryCounter\0"
   "\0"
   /* _mesa_function_pool[14449]: NormalPointerEXT (will be remapped) */
   "iiip\0"
   "glNormalPointerEXT\0"
   "\0"
   /* _mesa_function_pool[14474]: GetSubroutineIndex (will be remapped) */
   "iip\0"
   "glGetSubroutineIndex\0"
   "\0"
   /* _mesa_function_pool[14500]: ProgramUniform3iv (will be remapped) */
   "iiip\0"
   "glProgramUniform3iv\0"
   "glProgramUniform3ivEXT\0"
   "\0"
   /* _mesa_function_pool[14549]: ProgramUniformMatrix2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2dv\0"
   "\0"
   /* _mesa_function_pool[14582]: ClearTexSubImage (will be remapped) */
   "iiiiiiiiiip\0"
   "glClearTexSubImage\0"
   "\0"
   /* _mesa_function_pool[14614]: GetActiveUniformBlockName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformBlockName\0"
   "\0"
   /* _mesa_function_pool[14649]: DrawElementsBaseVertex (will be remapped) */
   "iiipi\0"
   "glDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[14681]: RasterPos3iv (offset 75) */
   "p\0"
   "glRasterPos3iv\0"
   "\0"
   /* _mesa_function_pool[14699]: ColorMaski (will be remapped) */
   "iiiii\0"
   "glColorMaskIndexedEXT\0"
   "glColorMaski\0"
   "\0"
   /* _mesa_function_pool[14741]: Uniform2uiv (will be remapped) */
   "iip\0"
   "glUniform2uivEXT\0"
   "glUniform2uiv\0"
   "\0"
   /* _mesa_function_pool[14777]: RasterPos3s (offset 76) */
   "iii\0"
   "glRasterPos3s\0"
   "\0"
   /* _mesa_function_pool[14796]: RasterPos3d (offset 70) */
   "ddd\0"
   "glRasterPos3d\0"
   "\0"
   /* _mesa_function_pool[14815]: RasterPos3f (offset 72) */
   "fff\0"
   "glRasterPos3f\0"
   "\0"
   /* _mesa_function_pool[14834]: BindVertexArray (will be remapped) */
   "i\0"
   "glBindVertexArray\0"
   "glBindVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[14876]: RasterPos3i (offset 74) */
   "iii\0"
   "glRasterPos3i\0"
   "\0"
   /* _mesa_function_pool[14895]: VertexAttribL3dv (will be remapped) */
   "ip\0"
   "glVertexAttribL3dv\0"
   "\0"
   /* _mesa_function_pool[14918]: GetTexParameteriv (offset 283) */
   "iip\0"
   "glGetTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[14943]: DrawTransformFeedbackStreamInstanced (will be remapped) */
   "iiii\0"
   "glDrawTransformFeedbackStreamInstanced\0"
   "\0"
   /* _mesa_function_pool[14988]: VertexAttrib2fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "glVertexAttrib2fvARB\0"
   "\0"
   /* _mesa_function_pool[15031]: VertexPointerListIBM (dynamic) */
   "iiipi\0"
   "glVertexPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[15061]: GetProgramResourceName (will be remapped) */
   "iiiipp\0"
   "glGetProgramResourceName\0"
   "\0"
   /* _mesa_function_pool[15094]: TexCoord2fNormal3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15136]: ProgramUniformMatrix4x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[15171]: IsFenceNV (dynamic) */
   "i\0"
   "glIsFenceNV\0"
   "\0"
   /* _mesa_function_pool[15186]: ColorTable (offset 339) */
   "iiiiip\0"
   "glColorTable\0"
   "glColorTableSGI\0"
   "glColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[15239]: LoadName (offset 198) */
   "i\0"
   "glLoadName\0"
   "\0"
   /* _mesa_function_pool[15253]: Color3fVertex3fSUN (dynamic) */
   "ffffff\0"
   "glColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[15282]: GetnUniformuivARB (will be remapped) */
   "iiip\0"
   "glGetnUniformuivARB\0"
   "\0"
   /* _mesa_function_pool[15308]: ClearIndex (offset 205) */
   "f\0"
   "glClearIndex\0"
   "\0"
   /* _mesa_function_pool[15324]: ConvolutionParameterfv (offset 351) */
   "iip\0"
   "glConvolutionParameterfv\0"
   "glConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[15382]: TbufferMask3DFX (dynamic) */
   "i\0"
   "glTbufferMask3DFX\0"
   "\0"
   /* _mesa_function_pool[15403]: GetTexGendv (offset 278) */
   "iip\0"
   "glGetTexGendv\0"
   "\0"
   /* _mesa_function_pool[15422]: FlushMappedNamedBufferRange (will be remapped) */
   "iii\0"
   "glFlushMappedNamedBufferRange\0"
   "\0"
   /* _mesa_function_pool[15457]: MultiTexCoordP1ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP1ui\0"
   "\0"
   /* _mesa_function_pool[15482]: EvalMesh2 (offset 238) */
   "iiiii\0"
   "glEvalMesh2\0"
   "\0"
   /* _mesa_function_pool[15501]: Vertex4fv (offset 145) */
   "p\0"
   "glVertex4fv\0"
   "\0"
   /* _mesa_function_pool[15516]: SelectPerfMonitorCountersAMD (will be remapped) */
   "iiiip\0"
   "glSelectPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[15554]: TextureStorage2D (will be remapped) */
   "iiiii\0"
   "glTextureStorage2D\0"
   "\0"
   /* _mesa_function_pool[15580]: GetTextureParameterIiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[15610]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "glBindFramebufferOES\0"
   "\0"
   /* _mesa_function_pool[15653]: ReplacementCodeuiNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[15698]: GetMinmax (offset 364) */
   "iiiip\0"
   "glGetMinmax\0"
   "glGetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[15732]: Color3fVertex3fvSUN (dynamic) */
   "pp\0"
   "glColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[15758]: VertexAttribs3svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3svNV\0"
   "\0"
   /* _mesa_function_pool[15784]: GetActiveUniformsiv (will be remapped) */
   "iipip\0"
   "glGetActiveUniformsiv\0"
   "\0"
   /* _mesa_function_pool[15813]: VertexAttrib2sv (will be remapped) */
   "ip\0"
   "glVertexAttrib2sv\0"
   "glVertexAttrib2svARB\0"
   "\0"
   /* _mesa_function_pool[15856]: GetProgramEnvParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[15891]: GetSharpenTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[15919]: Uniform1dv (will be remapped) */
   "iip\0"
   "glUniform1dv\0"
   "\0"
   /* _mesa_function_pool[15937]: PixelTransformParameterfvEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[15973]: TransformFeedbackBufferRange (will be remapped) */
   "iiiii\0"
   "glTransformFeedbackBufferRange\0"
   "\0"
   /* _mesa_function_pool[16011]: PushDebugGroup (will be remapped) */
   "iiip\0"
   "glPushDebugGroup\0"
   "\0"
   /* _mesa_function_pool[16034]: ReplacementCodeuiNormal3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[16082]: GetPerfMonitorGroupStringAMD (will be remapped) */
   "iipp\0"
   "glGetPerfMonitorGroupStringAMD\0"
   "\0"
   /* _mesa_function_pool[16119]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[16132]: PassThrough (offset 199) */
   "f\0"
   "glPassThrough\0"
   "\0"
   /* _mesa_function_pool[16149]: GetListParameterfvSGIX (dynamic) */
   "iip\0"
   "glGetListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[16179]: PatchParameterfv (will be remapped) */
   "ip\0"
   "glPatchParameterfv\0"
   "\0"
   /* _mesa_function_pool[16202]: GetObjectParameterivAPPLE (will be remapped) */
   "iiip\0"
   "glGetObjectParameterivAPPLE\0"
   "\0"
   /* _mesa_function_pool[16236]: GlobalAlphaFactorubSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorubSUN\0"
   "\0"
   /* _mesa_function_pool[16264]: BindBuffersRange (will be remapped) */
   "iiippp\0"
   "glBindBuffersRange\0"
   "\0"
   /* _mesa_function_pool[16291]: VertexAttrib4fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "glVertexAttrib4fvARB\0"
   "\0"
   /* _mesa_function_pool[16334]: WindowPos3dv (will be remapped) */
   "p\0"
   "glWindowPos3dv\0"
   "glWindowPos3dvARB\0"
   "glWindowPos3dvMESA\0"
   "\0"
   /* _mesa_function_pool[16389]: TexGenxOES (will be remapped) */
   "iii\0"
   "glTexGenxOES\0"
   "\0"
   /* _mesa_function_pool[16407]: VertexArrayAttribIFormat (will be remapped) */
   "iiiii\0"
   "glVertexArrayAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[16441]: DeleteFencesNV (dynamic) */
   "ip\0"
   "glDeleteFencesNV\0"
   "\0"
   /* _mesa_function_pool[16462]: GetImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glGetImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[16500]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[16517]: Binormal3fEXT (dynamic) */
   "fff\0"
   "glBinormal3fEXT\0"
   "\0"
   /* _mesa_function_pool[16538]: ProgramUniform1iv (will be remapped) */
   "iiip\0"
   "glProgramUniform1iv\0"
   "glProgramUniform1ivEXT\0"
   "\0"
   /* _mesa_function_pool[16587]: ProgramUniform3ui (will be remapped) */
   "iiiii\0"
   "glProgramUniform3ui\0"
   "glProgramUniform3uiEXT\0"
   "\0"
   /* _mesa_function_pool[16637]: SecondaryColor3sv (will be remapped) */
   "p\0"
   "glSecondaryColor3sv\0"
   "glSecondaryColor3svEXT\0"
   "\0"
   /* _mesa_function_pool[16683]: TexCoordP3ui (will be remapped) */
   "ii\0"
   "glTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[16702]: VertexArrayElementBuffer (will be remapped) */
   "ii\0"
   "glVertexArrayElementBuffer\0"
   "\0"
   /* _mesa_function_pool[16733]: Fogxv (will be remapped) */
   "ip\0"
   "glFogxvOES\0"
   "glFogxv\0"
   "\0"
   /* _mesa_function_pool[16756]: VertexPointervINTEL (dynamic) */
   "iip\0"
   "glVertexPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[16783]: VertexAttribP1ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP1ui\0"
   "\0"
   /* _mesa_function_pool[16808]: DeleteLists (offset 4) */
   "ii\0"
   "glDeleteLists\0"
   "\0"
   /* _mesa_function_pool[16826]: LogicOp (offset 242) */
   "i\0"
   "glLogicOp\0"
   "\0"
   /* _mesa_function_pool[16839]: RenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glRenderbufferStorageMultisample\0"
   "glRenderbufferStorageMultisampleEXT\0"
   "\0"
   /* _mesa_function_pool[16915]: GetTransformFeedbacki64_v (will be remapped) */
   "iiip\0"
   "glGetTransformFeedbacki64_v\0"
   "\0"
   /* _mesa_function_pool[16949]: WindowPos3d (will be remapped) */
   "ddd\0"
   "glWindowPos3d\0"
   "glWindowPos3dARB\0"
   "glWindowPos3dMESA\0"
   "\0"
   /* _mesa_function_pool[17003]: Enablei (will be remapped) */
   "ii\0"
   "glEnableIndexedEXT\0"
   "glEnablei\0"
   "\0"
   /* _mesa_function_pool[17036]: WindowPos3f (will be remapped) */
   "fff\0"
   "glWindowPos3f\0"
   "glWindowPos3fARB\0"
   "glWindowPos3fMESA\0"
   "\0"
   /* _mesa_function_pool[17090]: GenProgramsARB (will be remapped) */
   "ip\0"
   "glGenProgramsARB\0"
   "glGenProgramsNV\0"
   "\0"
   /* _mesa_function_pool[17127]: RasterPos2sv (offset 69) */
   "p\0"
   "glRasterPos2sv\0"
   "\0"
   /* _mesa_function_pool[17145]: WindowPos3i (will be remapped) */
   "iii\0"
   "glWindowPos3i\0"
   "glWindowPos3iARB\0"
   "glWindowPos3iMESA\0"
   "\0"
   /* _mesa_function_pool[17199]: MultiTexCoord4iv (offset 405) */
   "ip\0"
   "glMultiTexCoord4iv\0"
   "glMultiTexCoord4ivARB\0"
   "\0"
   /* _mesa_function_pool[17244]: TexCoord1sv (offset 101) */
   "p\0"
   "glTexCoord1sv\0"
   "\0"
   /* _mesa_function_pool[17261]: WindowPos3s (will be remapped) */
   "iii\0"
   "glWindowPos3s\0"
   "glWindowPos3sARB\0"
   "glWindowPos3sMESA\0"
   "\0"
   /* _mesa_function_pool[17315]: PixelMapusv (offset 253) */
   "iip\0"
   "glPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[17334]: DebugMessageInsert (will be remapped) */
   "iiiiip\0"
   "glDebugMessageInsertARB\0"
   "glDebugMessageInsert\0"
   "\0"
   /* _mesa_function_pool[17387]: Orthof (will be remapped) */
   "ffffff\0"
   "glOrthofOES\0"
   "glOrthof\0"
   "\0"
   /* _mesa_function_pool[17416]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "glCompressedTexImage2DARB\0"
   "\0"
   /* _mesa_function_pool[17475]: DeleteObjectARB (will be remapped) */
   "i\0"
   "glDeleteObjectARB\0"
   "\0"
   /* _mesa_function_pool[17496]: ProgramUniformMatrix2x3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[17531]: GetVertexArrayiv (will be remapped) */
   "iip\0"
   "glGetVertexArrayiv\0"
   "\0"
   /* _mesa_function_pool[17555]: IsSync (will be remapped) */
   "i\0"
   "glIsSync\0"
   "\0"
   /* _mesa_function_pool[17567]: Color4uiv (offset 38) */
   "p\0"
   "glColor4uiv\0"
   "\0"
   /* _mesa_function_pool[17582]: MultiTexCoord1sv (offset 383) */
   "ip\0"
   "glMultiTexCoord1sv\0"
   "glMultiTexCoord1svARB\0"
   "\0"
   /* _mesa_function_pool[17627]: Orthox (will be remapped) */
   "iiiiii\0"
   "glOrthoxOES\0"
   "glOrthox\0"
   "\0"
   /* _mesa_function_pool[17656]: PushAttrib (offset 219) */
   "i\0"
   "glPushAttrib\0"
   "\0"
   /* _mesa_function_pool[17672]: RasterPos2i (offset 66) */
   "ii\0"
   "glRasterPos2i\0"
   "\0"
   /* _mesa_function_pool[17690]: ClipPlane (offset 150) */
   "ip\0"
   "glClipPlane\0"
   "\0"
   /* _mesa_function_pool[17706]: TexCoord2fColor3fVertex3fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord2fColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[17747]: GetProgramivNV (will be remapped) */
   "iip\0"
   "glGetProgramivNV\0"
   "\0"
   /* _mesa_function_pool[17769]: RasterPos2f (offset 64) */
   "ff\0"
   "glRasterPos2f\0"
   "\0"
   /* _mesa_function_pool[17787]: GetActiveSubroutineUniformiv (will be remapped) */
   "iiiip\0"
   "glGetActiveSubroutineUniformiv\0"
   "\0"
   /* _mesa_function_pool[17825]: RasterPos2d (offset 62) */
   "dd\0"
   "glRasterPos2d\0"
   "\0"
   /* _mesa_function_pool[17843]: RasterPos3fv (offset 73) */
   "p\0"
   "glRasterPos3fv\0"
   "\0"
   /* _mesa_function_pool[17861]: InvalidateSubFramebuffer (will be remapped) */
   "iipiiii\0"
   "glInvalidateSubFramebuffer\0"
   "\0"
   /* _mesa_function_pool[17897]: Color4ub (offset 35) */
   "iiii\0"
   "glColor4ub\0"
   "\0"
   /* _mesa_function_pool[17914]: UniformMatrix2x4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x4dv\0"
   "\0"
   /* _mesa_function_pool[17941]: RasterPos2s (offset 68) */
   "ii\0"
   "glRasterPos2s\0"
   "\0"
   /* _mesa_function_pool[17959]: VertexP2uiv (will be remapped) */
   "ip\0"
   "glVertexP2uiv\0"
   "\0"
   /* _mesa_function_pool[17977]: Color4fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[18012]: VertexArrayBindingDivisor (will be remapped) */
   "iii\0"
   "glVertexArrayBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[18045]: GetVertexAttribivNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribivNV\0"
   "\0"
   /* _mesa_function_pool[18072]: TexSubImage4DSGIS (dynamic) */
   "iiiiiiiiiiiip\0"
   "glTexSubImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[18107]: MultiTexCoord3dv (offset 393) */
   "ip\0"
   "glMultiTexCoord3dv\0"
   "glMultiTexCoord3dvARB\0"
   "\0"
   /* _mesa_function_pool[18152]: BindProgramPipeline (will be remapped) */
   "i\0"
   "glBindProgramPipeline\0"
   "glBindProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[18202]: VertexAttribP4uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP4uiv\0"
   "\0"
   /* _mesa_function_pool[18228]: DebugMessageCallback (will be remapped) */
   "pp\0"
   "glDebugMessageCallbackARB\0"
   "glDebugMessageCallback\0"
   "\0"
   /* _mesa_function_pool[18281]: MultiTexCoord1i (offset 380) */
   "ii\0"
   "glMultiTexCoord1i\0"
   "glMultiTexCoord1iARB\0"
   "\0"
   /* _mesa_function_pool[18324]: WindowPos2dv (will be remapped) */
   "p\0"
   "glWindowPos2dv\0"
   "glWindowPos2dvARB\0"
   "glWindowPos2dvMESA\0"
   "\0"
   /* _mesa_function_pool[18379]: TexParameterIuiv (will be remapped) */
   "iip\0"
   "glTexParameterIuivEXT\0"
   "glTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[18425]: DeletePerfQueryINTEL (will be remapped) */
   "i\0"
   "glDeletePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[18451]: MultiTexCoord1d (offset 376) */
   "id\0"
   "glMultiTexCoord1d\0"
   "glMultiTexCoord1dARB\0"
   "\0"
   /* _mesa_function_pool[18494]: GenVertexArraysAPPLE (will be remapped) */
   "ip\0"
   "glGenVertexArraysAPPLE\0"
   "\0"
   /* _mesa_function_pool[18521]: MultiTexCoord1s (offset 382) */
   "ii\0"
   "glMultiTexCoord1s\0"
   "glMultiTexCoord1sARB\0"
   "\0"
   /* _mesa_function_pool[18564]: BeginConditionalRender (will be remapped) */
   "ii\0"
   "glBeginConditionalRender\0"
   "glBeginConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[18620]: LoadPaletteFromModelViewMatrixOES (dynamic) */
   "\0"
   "glLoadPaletteFromModelViewMatrixOES\0"
   "\0"
   /* _mesa_function_pool[18658]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[18677]: GetMapAttribParameterfvNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[18711]: CopyConvolutionFilter1D (offset 354) */
   "iiiii\0"
   "glCopyConvolutionFilter1D\0"
   "glCopyConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[18773]: ClearBufferfv (will be remapped) */
   "iip\0"
   "glClearBufferfv\0"
   "\0"
   /* _mesa_function_pool[18794]: UniformMatrix4dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[18819]: InstrumentsBufferSGIX (dynamic) */
   "ip\0"
   "glInstrumentsBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[18847]: CreateShaderObjectARB (will be remapped) */
   "i\0"
   "glCreateShaderObjectARB\0"
   "\0"
   /* _mesa_function_pool[18874]: GetTexParameterxv (will be remapped) */
   "iip\0"
   "glGetTexParameterxvOES\0"
   "glGetTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[18922]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[18949]: ClearBufferfi (will be remapped) */
   "iifi\0"
   "glClearBufferfi\0"
   "\0"
   /* _mesa_function_pool[18971]: Materialiv (offset 172) */
   "iip\0"
   "glMaterialiv\0"
   "\0"
   /* _mesa_function_pool[18989]: DeleteFragmentShaderATI (will be remapped) */
   "i\0"
   "glDeleteFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[19018]: VertexArrayVertexBuffers (will be remapped) */
   "iiippp\0"
   "glVertexArrayVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[19053]: DrawElementsInstancedBaseVertex (will be remapped) */
   "iiipii\0"
   "glDrawElementsInstancedBaseVertex\0"
   "\0"
   /* _mesa_function_pool[19095]: DisableClientState (offset 309) */
   "i\0"
   "glDisableClientState\0"
   "\0"
   /* _mesa_function_pool[19119]: TexGeni (offset 192) */
   "iii\0"
   "glTexGeni\0"
   "glTexGeniOES\0"
   "\0"
   /* _mesa_function_pool[19147]: TexGenf (offset 190) */
   "iif\0"
   "glTexGenf\0"
   "glTexGenfOES\0"
   "\0"
   /* _mesa_function_pool[19175]: TexGend (offset 188) */
   "iid\0"
   "glTexGend\0"
   "\0"
   /* _mesa_function_pool[19190]: GetVertexAttribfvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribfvNV\0"
   "\0"
   /* _mesa_function_pool[19217]: ColorPointerListIBM (dynamic) */
   "iiipi\0"
   "glColorPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[19246]: Color4sv (offset 34) */
   "p\0"
   "glColor4sv\0"
   "\0"
   /* _mesa_function_pool[19260]: GetCombinerInputParameterfvNV (dynamic) */
   "iiiip\0"
   "glGetCombinerInputParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[19299]: LoadTransposeMatrixf (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixf\0"
   "glLoadTransposeMatrixfARB\0"
   "\0"
   /* _mesa_function_pool[19351]: LoadTransposeMatrixd (will be remapped) */
   "p\0"
   "glLoadTransposeMatrixd\0"
   "glLoadTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[19403]: PixelZoom (offset 246) */
   "ff\0"
   "glPixelZoom\0"
   "\0"
   /* _mesa_function_pool[19419]: ProgramEnvParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramEnvParameter4dARB\0"
   "glProgramParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[19477]: ColorTableParameterfv (offset 340) */
   "iip\0"
   "glColorTableParameterfv\0"
   "glColorTableParameterfvSGI\0"
   "\0"
   /* _mesa_function_pool[19533]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "glIsTextureEXT\0"
   "\0"
   /* _mesa_function_pool[19563]: ProgramUniform3uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform3uiv\0"
   "glProgramUniform3uivEXT\0"
   "\0"
   /* _mesa_function_pool[19614]: IndexPointer (offset 314) */
   "iip\0"
   "glIndexPointer\0"
   "\0"
   /* _mesa_function_pool[19634]: ImageTransformParameterivHP (dynamic) */
   "iip\0"
   "glImageTransformParameterivHP\0"
   "\0"
   /* _mesa_function_pool[19669]: VertexAttrib4sNV (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4sNV\0"
   "\0"
   /* _mesa_function_pool[19695]: GetMapdv (offset 266) */
   "iip\0"
   "glGetMapdv\0"
   "\0"
   /* _mesa_function_pool[19711]: GetInteger64i_v (will be remapped) */
   "iip\0"
   "glGetInteger64i_v\0"
   "\0"
   /* _mesa_function_pool[19734]: ReplacementCodeuiColor4ubVertex3fSUN (dynamic) */
   "iiiiifff\0"
   "glReplacementCodeuiColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[19783]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "glIsBufferARB\0"
   "\0"
   /* _mesa_function_pool[19811]: ColorP4ui (will be remapped) */
   "ii\0"
   "glColorP4ui\0"
   "\0"
   /* _mesa_function_pool[19827]: TextureStorage3D (will be remapped) */
   "iiiiii\0"
   "glTextureStorage3D\0"
   "\0"
   /* _mesa_function_pool[19854]: SpriteParameteriSGIX (dynamic) */
   "ii\0"
   "glSpriteParameteriSGIX\0"
   "\0"
   /* _mesa_function_pool[19881]: TexCoordP3uiv (will be remapped) */
   "ip\0"
   "glTexCoordP3uiv\0"
   "\0"
   /* _mesa_function_pool[19901]: VertexArrayAttribFormat (will be remapped) */
   "iiiiii\0"
   "glVertexArrayAttribFormat\0"
   "\0"
   /* _mesa_function_pool[19935]: EvalMapsNV (dynamic) */
   "ii\0"
   "glEvalMapsNV\0"
   "\0"
   /* _mesa_function_pool[19952]: ReplacementCodeuiSUN (dynamic) */
   "i\0"
   "glReplacementCodeuiSUN\0"
   "\0"
   /* _mesa_function_pool[19978]: GlobalAlphaFactoruiSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoruiSUN\0"
   "\0"
   /* _mesa_function_pool[20006]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "glUniform1ivARB\0"
   "\0"
   /* _mesa_function_pool[20040]: Uniform4uiv (will be remapped) */
   "iip\0"
   "glUniform4uivEXT\0"
   "glUniform4uiv\0"
   "\0"
   /* _mesa_function_pool[20076]: PopDebugGroup (will be remapped) */
   "\0"
   "glPopDebugGroup\0"
   "\0"
   /* _mesa_function_pool[20094]: VertexAttrib1d (will be remapped) */
   "id\0"
   "glVertexAttrib1d\0"
   "glVertexAttrib1dARB\0"
   "\0"
   /* _mesa_function_pool[20135]: CompressedTexImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexImage1D\0"
   "glCompressedTexImage1DARB\0"
   "\0"
   /* _mesa_function_pool[20193]: NamedBufferSubData (will be remapped) */
   "iiip\0"
   "glNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[20220]: TexBufferRange (will be remapped) */
   "iiiii\0"
   "glTexBufferRange\0"
   "\0"
   /* _mesa_function_pool[20244]: VertexAttrib1s (will be remapped) */
   "ii\0"
   "glVertexAttrib1s\0"
   "glVertexAttrib1sARB\0"
   "\0"
   /* _mesa_function_pool[20285]: MultiDrawElementsIndirect (will be remapped) */
   "iipii\0"
   "glMultiDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[20320]: UniformMatrix4x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x3dv\0"
   "\0"
   /* _mesa_function_pool[20347]: TransformFeedbackBufferBase (will be remapped) */
   "iii\0"
   "glTransformFeedbackBufferBase\0"
   "\0"
   /* _mesa_function_pool[20382]: FogCoordfvEXT (will be remapped) */
   "p\0"
   "glFogCoordfv\0"
   "glFogCoordfvEXT\0"
   "\0"
   /* _mesa_function_pool[20414]: BeginPerfMonitorAMD (will be remapped) */
   "i\0"
   "glBeginPerfMonitorAMD\0"
   "\0"
   /* _mesa_function_pool[20439]: GetColorTableParameterfv (offset 344) */
   "iip\0"
   "glGetColorTableParameterfv\0"
   "glGetColorTableParameterfvSGI\0"
   "glGetColorTableParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[20531]: MultiTexCoord3fARB (offset 394) */
   "ifff\0"
   "glMultiTexCoord3f\0"
   "glMultiTexCoord3fARB\0"
   "\0"
   /* _mesa_function_pool[20576]: GetTexLevelParameterfv (offset 284) */
   "iiip\0"
   "glGetTexLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[20607]: Vertex2sv (offset 133) */
   "p\0"
   "glVertex2sv\0"
   "\0"
   /* _mesa_function_pool[20622]: WeightusvARB (dynamic) */
   "ip\0"
   "glWeightusvARB\0"
   "\0"
   /* _mesa_function_pool[20641]: VertexAttrib2dNV (will be remapped) */
   "idd\0"
   "glVertexAttrib2dNV\0"
   "\0"
   /* _mesa_function_pool[20665]: GetTrackMatrixivNV (will be remapped) */
   "iiip\0"
   "glGetTrackMatrixivNV\0"
   "\0"
   /* _mesa_function_pool[20692]: VertexAttrib3svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3svNV\0"
   "\0"
   /* _mesa_function_pool[20716]: GetTexEnviv (offset 277) */
   "iip\0"
   "glGetTexEnviv\0"
   "\0"
   /* _mesa_function_pool[20735]: ViewportArrayv (will be remapped) */
   "iip\0"
   "glViewportArrayv\0"
   "\0"
   /* _mesa_function_pool[20757]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffffff\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[20828]: SeparableFilter2D (offset 360) */
   "iiiiiipp\0"
   "glSeparableFilter2D\0"
   "glSeparableFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[20881]: ReplacementCodeuiColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[20926]: ArrayElement (offset 306) */
   "i\0"
   "glArrayElement\0"
   "glArrayElementEXT\0"
   "\0"
   /* _mesa_function_pool[20962]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[20986]: FragmentMaterialiSGIX (dynamic) */
   "iii\0"
   "glFragmentMaterialiSGIX\0"
   "\0"
   /* _mesa_function_pool[21015]: RasterPos2dv (offset 63) */
   "p\0"
   "glRasterPos2dv\0"
   "\0"
   /* _mesa_function_pool[21033]: Fogiv (offset 156) */
   "ip\0"
   "glFogiv\0"
   "\0"
   /* _mesa_function_pool[21045]: EndQuery (will be remapped) */
   "i\0"
   "glEndQuery\0"
   "glEndQueryARB\0"
   "\0"
   /* _mesa_function_pool[21073]: TexCoord1dv (offset 95) */
   "p\0"
   "glTexCoord1dv\0"
   "\0"
   /* _mesa_function_pool[21090]: TexCoord4dv (offset 119) */
   "p\0"
   "glTexCoord4dv\0"
   "\0"
   /* _mesa_function_pool[21107]: GetVertexAttribdvNV (will be remapped) */
   "iip\0"
   "glGetVertexAttribdvNV\0"
   "\0"
   /* _mesa_function_pool[21134]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[21145]: VertexAttrib4sv (will be remapped) */
   "ip\0"
   "glVertexAttrib4sv\0"
   "glVertexAttrib4svARB\0"
   "\0"
   /* _mesa_function_pool[21188]: Ortho (offset 296) */
   "dddddd\0"
   "glOrtho\0"
   "\0"
   /* _mesa_function_pool[21204]: Uniform3uiv (will be remapped) */
   "iip\0"
   "glUniform3uivEXT\0"
   "glUniform3uiv\0"
   "\0"
   /* _mesa_function_pool[21240]: MatrixIndexPointerARB (dynamic) */
   "iiip\0"
   "glMatrixIndexPointerARB\0"
   "glMatrixIndexPointerOES\0"
   "\0"
   /* _mesa_function_pool[21294]: EndQueryIndexed (will be remapped) */
   "ii\0"
   "glEndQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[21316]: TexParameterxv (will be remapped) */
   "iip\0"
   "glTexParameterxvOES\0"
   "glTexParameterxv\0"
   "\0"
   /* _mesa_function_pool[21358]: SampleMaskSGIS (will be remapped) */
   "fi\0"
   "glSampleMaskSGIS\0"
   "glSampleMaskEXT\0"
   "\0"
   /* _mesa_function_pool[21395]: FramebufferTextureFaceARB (dynamic) */
   "iiiii\0"
   "glFramebufferTextureFaceARB\0"
   "\0"
   /* _mesa_function_pool[21430]: ProgramUniformMatrix2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2fv\0"
   "glProgramUniformMatrix2fvEXT\0"
   "\0"
   /* _mesa_function_pool[21492]: ProgramLocalParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramLocalParameter4fvARB\0"
   "\0"
   /* _mesa_function_pool[21527]: GetProgramStringNV (will be remapped) */
   "iip\0"
   "glGetProgramStringNV\0"
   "\0"
   /* _mesa_function_pool[21553]: Binormal3svEXT (dynamic) */
   "p\0"
   "glBinormal3svEXT\0"
   "\0"
   /* _mesa_function_pool[21573]: Uniform4dv (will be remapped) */
   "iip\0"
   "glUniform4dv\0"
   "\0"
   /* _mesa_function_pool[21591]: LightModelx (will be remapped) */
   "ii\0"
   "glLightModelxOES\0"
   "glLightModelx\0"
   "\0"
   /* _mesa_function_pool[21626]: VertexAttribI3iEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3iEXT\0"
   "glVertexAttribI3i\0"
   "\0"
   /* _mesa_function_pool[21671]: ClearColorx (will be remapped) */
   "iiii\0"
   "glClearColorxOES\0"
   "glClearColorx\0"
   "\0"
   /* _mesa_function_pool[21708]: EndTransformFeedback (will be remapped) */
   "\0"
   "glEndTransformFeedback\0"
   "glEndTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[21759]: VertexAttribL2dv (will be remapped) */
   "ip\0"
   "glVertexAttribL2dv\0"
   "\0"
   /* _mesa_function_pool[21782]: GetHandleARB (will be remapped) */
   "i\0"
   "glGetHandleARB\0"
   "\0"
   /* _mesa_function_pool[21800]: GetProgramBinary (will be remapped) */
   "iippp\0"
   "glGetProgramBinary\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[21848]: ViewportIndexedfv (will be remapped) */
   "ip\0"
   "glViewportIndexedfv\0"
   "\0"
   /* _mesa_function_pool[21872]: BindTextureUnit (will be remapped) */
   "ii\0"
   "glBindTextureUnit\0"
   "\0"
   /* _mesa_function_pool[21894]: CallList (offset 2) */
   "i\0"
   "glCallList\0"
   "\0"
   /* _mesa_function_pool[21908]: Materialfv (offset 170) */
   "iip\0"
   "glMaterialfv\0"
   "\0"
   /* _mesa_function_pool[21926]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[21945]: GetActiveAtomicCounterBufferiv (will be remapped) */
   "iiip\0"
   "glGetActiveAtomicCounterBufferiv\0"
   "\0"
   /* _mesa_function_pool[21984]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "glClearDepthfOES\0"
   "\0"
   /* _mesa_function_pool[22018]: VertexWeightfEXT (dynamic) */
   "f\0"
   "glVertexWeightfEXT\0"
   "\0"
   /* _mesa_function_pool[22040]: FlushVertexArrayRangeNV (dynamic) */
   "\0"
   "glFlushVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[22068]: GetConvolutionFilter (offset 356) */
   "iiip\0"
   "glGetConvolutionFilter\0"
   "glGetConvolutionFilterEXT\0"
   "\0"
   /* _mesa_function_pool[22123]: MultiModeDrawElementsIBM (will be remapped) */
   "ppipii\0"
   "glMultiModeDrawElementsIBM\0"
   "\0"
   /* _mesa_function_pool[22158]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "glUniform2ivARB\0"
   "\0"
   /* _mesa_function_pool[22192]: GetFixedv (will be remapped) */
   "ip\0"
   "glGetFixedvOES\0"
   "glGetFixedv\0"
   "\0"
   /* _mesa_function_pool[22223]: ProgramParameters4dvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4dvNV\0"
   "\0"
   /* _mesa_function_pool[22254]: Binormal3dvEXT (dynamic) */
   "p\0"
   "glBinormal3dvEXT\0"
   "\0"
   /* _mesa_function_pool[22274]: SampleCoveragex (will be remapped) */
   "ii\0"
   "glSampleCoveragexOES\0"
   "glSampleCoveragex\0"
   "\0"
   /* _mesa_function_pool[22317]: GetPerfQueryInfoINTEL (will be remapped) */
   "iippppp\0"
   "glGetPerfQueryInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[22350]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "glDeleteFramebuffersEXT\0"
   "glDeleteFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[22423]: CombinerInputNV (dynamic) */
   "iiiiii\0"
   "glCombinerInputNV\0"
   "\0"
   /* _mesa_function_pool[22449]: VertexAttrib4uiv (will be remapped) */
   "ip\0"
   "glVertexAttrib4uiv\0"
   "glVertexAttrib4uivARB\0"
   "\0"
   /* _mesa_function_pool[22494]: VertexAttrib4Nsv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nsv\0"
   "glVertexAttrib4NsvARB\0"
   "\0"
   /* _mesa_function_pool[22539]: Vertex4s (offset 148) */
   "iiii\0"
   "glVertex4s\0"
   "\0"
   /* _mesa_function_pool[22556]: VertexAttribI2iEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2iEXT\0"
   "glVertexAttribI2i\0"
   "\0"
   /* _mesa_function_pool[22600]: Vertex4f (offset 144) */
   "ffff\0"
   "glVertex4f\0"
   "\0"
   /* _mesa_function_pool[22617]: Vertex4d (offset 142) */
   "dddd\0"
   "glVertex4d\0"
   "\0"
   /* _mesa_function_pool[22634]: VertexAttribL4dv (will be remapped) */
   "ip\0"
   "glVertexAttribL4dv\0"
   "\0"
   /* _mesa_function_pool[22657]: GetTexGenfv (offset 279) */
   "iip\0"
   "glGetTexGenfv\0"
   "glGetTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[22693]: Vertex4i (offset 146) */
   "iiii\0"
   "glVertex4i\0"
   "\0"
   /* _mesa_function_pool[22710]: VertexWeightPointerEXT (dynamic) */
   "iiip\0"
   "glVertexWeightPointerEXT\0"
   "\0"
   /* _mesa_function_pool[22741]: MemoryBarrierByRegion (will be remapped) */
   "i\0"
   "glMemoryBarrierByRegion\0"
   "\0"
   /* _mesa_function_pool[22768]: StencilFuncSeparateATI (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparateATI\0"
   "\0"
   /* _mesa_function_pool[22799]: GetVertexAttribIuiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIuivEXT\0"
   "glGetVertexAttribIuiv\0"
   "\0"
   /* _mesa_function_pool[22851]: LightModelfv (offset 164) */
   "ip\0"
   "glLightModelfv\0"
   "\0"
   /* _mesa_function_pool[22870]: Vertex4dv (offset 143) */
   "p\0"
   "glVertex4dv\0"
   "\0"
   /* _mesa_function_pool[22885]: ProgramParameters4fvNV (will be remapped) */
   "iiip\0"
   "glProgramParameters4fvNV\0"
   "\0"
   /* _mesa_function_pool[22916]: GetInfoLogARB (will be remapped) */
   "iipp\0"
   "glGetInfoLogARB\0"
   "\0"
   /* _mesa_function_pool[22938]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[22955]: NamedFramebufferReadBuffer (will be remapped) */
   "ii\0"
   "glNamedFramebufferReadBuffer\0"
   "\0"
   /* _mesa_function_pool[22988]: IsList (offset 287) */
   "i\0"
   "glIsList\0"
   "\0"
   /* _mesa_function_pool[23000]: ClearBufferiv (will be remapped) */
   "iip\0"
   "glClearBufferiv\0"
   "\0"
   /* _mesa_function_pool[23021]: GetIntegeri_v (will be remapped) */
   "iip\0"
   "glGetIntegerIndexedvEXT\0"
   "glGetIntegeri_v\0"
   "\0"
   /* _mesa_function_pool[23066]: ProgramUniform2iv (will be remapped) */
   "iiip\0"
   "glProgramUniform2iv\0"
   "glProgramUniform2ivEXT\0"
   "\0"
   /* _mesa_function_pool[23115]: CreateVertexArrays (will be remapped) */
   "ip\0"
   "glCreateVertexArrays\0"
   "\0"
   /* _mesa_function_pool[23140]: FogCoordPointer (will be remapped) */
   "iip\0"
   "glFogCoordPointer\0"
   "glFogCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[23184]: SecondaryColor3us (will be remapped) */
   "iii\0"
   "glSecondaryColor3us\0"
   "glSecondaryColor3usEXT\0"
   "\0"
   /* _mesa_function_pool[23232]: DeformationMap3dSGIX (dynamic) */
   "iddiiddiiddiip\0"
   "glDeformationMap3dSGIX\0"
   "\0"
   /* _mesa_function_pool[23271]: TextureNormalEXT (dynamic) */
   "i\0"
   "glTextureNormalEXT\0"
   "\0"
   /* _mesa_function_pool[23293]: SecondaryColor3ub (will be remapped) */
   "iii\0"
   "glSecondaryColor3ub\0"
   "glSecondaryColor3ubEXT\0"
   "\0"
   /* _mesa_function_pool[23341]: GetActiveUniformName (will be remapped) */
   "iiipp\0"
   "glGetActiveUniformName\0"
   "\0"
   /* _mesa_function_pool[23371]: SecondaryColor3ui (will be remapped) */
   "iii\0"
   "glSecondaryColor3ui\0"
   "glSecondaryColor3uiEXT\0"
   "\0"
   /* _mesa_function_pool[23419]: VertexAttribI3uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3uivEXT\0"
   "glVertexAttribI3uiv\0"
   "\0"
   /* _mesa_function_pool[23466]: Binormal3fvEXT (dynamic) */
   "p\0"
   "glBinormal3fvEXT\0"
   "\0"
   /* _mesa_function_pool[23486]: TexCoordPointervINTEL (dynamic) */
   "iip\0"
   "glTexCoordPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[23515]: VertexAttrib1sNV (will be remapped) */
   "ii\0"
   "glVertexAttrib1sNV\0"
   "\0"
   /* _mesa_function_pool[23538]: Tangent3bEXT (dynamic) */
   "iii\0"
   "glTangent3bEXT\0"
   "\0"
   /* _mesa_function_pool[23558]: TextureBuffer (will be remapped) */
   "iii\0"
   "glTextureBuffer\0"
   "\0"
   /* _mesa_function_pool[23579]: FragmentLightModelfSGIX (dynamic) */
   "if\0"
   "glFragmentLightModelfSGIX\0"
   "\0"
   /* _mesa_function_pool[23609]: InitNames (offset 197) */
   "\0"
   "glInitNames\0"
   "\0"
   /* _mesa_function_pool[23623]: Normal3sv (offset 61) */
   "p\0"
   "glNormal3sv\0"
   "\0"
   /* _mesa_function_pool[23638]: DeleteQueries (will be remapped) */
   "ip\0"
   "glDeleteQueries\0"
   "glDeleteQueriesARB\0"
   "\0"
   /* _mesa_function_pool[23677]: InvalidateFramebuffer (will be remapped) */
   "iip\0"
   "glInvalidateFramebuffer\0"
   "\0"
   /* _mesa_function_pool[23706]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[23717]: MemoryBarrier (will be remapped) */
   "i\0"
   "glMemoryBarrier\0"
   "\0"
   /* _mesa_function_pool[23736]: CopyColorSubTable (offset 347) */
   "iiiii\0"
   "glCopyColorSubTable\0"
   "glCopyColorSubTableEXT\0"
   "\0"
   /* _mesa_function_pool[23786]: WeightdvARB (dynamic) */
   "ip\0"
   "glWeightdvARB\0"
   "\0"
   /* _mesa_function_pool[23804]: GetObjectParameterfvARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[23835]: GetTexEnvxv (will be remapped) */
   "iip\0"
   "glGetTexEnvxvOES\0"
   "glGetTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[23871]: DrawTexsvOES (will be remapped) */
   "p\0"
   "glDrawTexsvOES\0"
   "\0"
   /* _mesa_function_pool[23889]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[23902]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[23921]: WeightuivARB (dynamic) */
   "ip\0"
   "glWeightuivARB\0"
   "\0"
   /* _mesa_function_pool[23940]: GetTextureParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[23971]: RasterPos4iv (offset 83) */
   "p\0"
   "glRasterPos4iv\0"
   "\0"
   /* _mesa_function_pool[23989]: VDPAUIsSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUIsSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[24011]: ProgramUniformMatrix2x3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix2x3fv\0"
   "glProgramUniformMatrix2x3fvEXT\0"
   "\0"
   /* _mesa_function_pool[24077]: BindVertexBuffer (will be remapped) */
   "iiii\0"
   "glBindVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[24102]: Binormal3iEXT (dynamic) */
   "iii\0"
   "glBinormal3iEXT\0"
   "\0"
   /* _mesa_function_pool[24123]: RasterPos4i (offset 82) */
   "iiii\0"
   "glRasterPos4i\0"
   "\0"
   /* _mesa_function_pool[24143]: RasterPos4d (offset 78) */
   "dddd\0"
   "glRasterPos4d\0"
   "\0"
   /* _mesa_function_pool[24163]: RasterPos4f (offset 80) */
   "ffff\0"
   "glRasterPos4f\0"
   "\0"
   /* _mesa_function_pool[24183]: VDPAUMapSurfacesNV (will be remapped) */
   "ip\0"
   "glVDPAUMapSurfacesNV\0"
   "\0"
   /* _mesa_function_pool[24208]: GetQueryIndexediv (will be remapped) */
   "iiip\0"
   "glGetQueryIndexediv\0"
   "\0"
   /* _mesa_function_pool[24234]: RasterPos3dv (offset 71) */
   "p\0"
   "glRasterPos3dv\0"
   "\0"
   /* _mesa_function_pool[24252]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[24272]: TexCoord1iv (offset 99) */
   "p\0"
   "glTexCoord1iv\0"
   "\0"
   /* _mesa_function_pool[24289]: RasterPos4s (offset 84) */
   "iiii\0"
   "glRasterPos4s\0"
   "\0"
   /* _mesa_function_pool[24309]: PixelTexGenParameterfvSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterfvSGIS\0"
   "\0"
   /* _mesa_function_pool[24342]: VertexAttrib3dv (will be remapped) */
   "ip\0"
   "glVertexAttrib3dv\0"
   "glVertexAttrib3dvARB\0"
   "\0"
   /* _mesa_function_pool[24385]: Histogram (offset 367) */
   "iiii\0"
   "glHistogram\0"
   "glHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[24418]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "glUniform2fvARB\0"
   "\0"
   /* _mesa_function_pool[24452]: TexImage4DSGIS (dynamic) */
   "iiiiiiiiiip\0"
   "glTexImage4DSGIS\0"
   "\0"
   /* _mesa_function_pool[24482]: ProgramUniformMatrix3x4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4dv\0"
   "\0"
   /* _mesa_function_pool[24517]: DrawBuffers (will be remapped) */
   "ip\0"
   "glDrawBuffers\0"
   "glDrawBuffersARB\0"
   "glDrawBuffersATI\0"
   "glDrawBuffersNV\0"
   "glDrawBuffersEXT\0"
   "\0"
   /* _mesa_function_pool[24602]: GetnPolygonStippleARB (will be remapped) */
   "ip\0"
   "glGetnPolygonStippleARB\0"
   "\0"
   /* _mesa_function_pool[24630]: Color3uiv (offset 22) */
   "p\0"
   "glColor3uiv\0"
   "\0"
   /* _mesa_function_pool[24645]: EvalCoord2fv (offset 235) */
   "p\0"
   "glEvalCoord2fv\0"
   "\0"
   /* _mesa_function_pool[24663]: TextureStorage3DEXT (will be remapped) */
   "iiiiiii\0"
   "glTextureStorage3DEXT\0"
   "\0"
   /* _mesa_function_pool[24694]: VertexAttrib2fARB (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "glVertexAttrib2fARB\0"
   "\0"
   /* _mesa_function_pool[24736]: WindowPos2fv (will be remapped) */
   "p\0"
   "glWindowPos2fv\0"
   "glWindowPos2fvARB\0"
   "glWindowPos2fvMESA\0"
   "\0"
   /* _mesa_function_pool[24791]: Tangent3fEXT (dynamic) */
   "fff\0"
   "glTangent3fEXT\0"
   "\0"
   /* _mesa_function_pool[24811]: TexImage3D (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3D\0"
   "glTexImage3DEXT\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[24868]: GetPerfQueryIdByNameINTEL (will be remapped) */
   "pp\0"
   "glGetPerfQueryIdByNameINTEL\0"
   "\0"
   /* _mesa_function_pool[24900]: BindFragDataLocation (will be remapped) */
   "iip\0"
   "glBindFragDataLocationEXT\0"
   "glBindFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[24954]: LightModeliv (offset 166) */
   "ip\0"
   "glLightModeliv\0"
   "\0"
   /* _mesa_function_pool[24973]: Normal3bv (offset 53) */
   "p\0"
   "glNormal3bv\0"
   "\0"
   /* _mesa_function_pool[24988]: BeginQueryIndexed (will be remapped) */
   "iii\0"
   "glBeginQueryIndexed\0"
   "\0"
   /* _mesa_function_pool[25013]: ClearNamedBufferData (will be remapped) */
   "iiiip\0"
   "glClearNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[25043]: Vertex3iv (offset 139) */
   "p\0"
   "glVertex3iv\0"
   "\0"
   /* _mesa_function_pool[25058]: UniformMatrix2x3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3dv\0"
   "\0"
   /* _mesa_function_pool[25085]: TexCoord3dv (offset 111) */
   "p\0"
   "glTexCoord3dv\0"
   "\0"
   /* _mesa_function_pool[25102]: GetProgramStringARB (will be remapped) */
   "iip\0"
   "glGetProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[25129]: VertexP3ui (will be remapped) */
   "ii\0"
   "glVertexP3ui\0"
   "\0"
   /* _mesa_function_pool[25146]: CreateProgramObjectARB (will be remapped) */
   "\0"
   "glCreateProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[25173]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "glUniformMatrix3fvARB\0"
   "\0"
   /* _mesa_function_pool[25220]: PrioritizeTextures (offset 331) */
   "ipp\0"
   "glPrioritizeTextures\0"
   "glPrioritizeTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[25270]: VertexAttribI3uiEXT (will be remapped) */
   "iiii\0"
   "glVertexAttribI3uiEXT\0"
   "glVertexAttribI3ui\0"
   "\0"
   /* _mesa_function_pool[25317]: AsyncMarkerSGIX (dynamic) */
   "i\0"
   "glAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[25338]: GetProgramNamedParameterfvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[25375]: GetMaterialxv (will be remapped) */
   "iip\0"
   "glGetMaterialxvOES\0"
   "glGetMaterialxv\0"
   "\0"
   /* _mesa_function_pool[25415]: MatrixIndexusvARB (dynamic) */
   "ip\0"
   "glMatrixIndexusvARB\0"
   "\0"
   /* _mesa_function_pool[25439]: SecondaryColor3uiv (will be remapped) */
   "p\0"
   "glSecondaryColor3uiv\0"
   "glSecondaryColor3uivEXT\0"
   "\0"
   /* _mesa_function_pool[25487]: EndConditionalRender (will be remapped) */
   "\0"
   "glEndConditionalRender\0"
   "glEndConditionalRenderNV\0"
   "\0"
   /* _mesa_function_pool[25537]: ProgramLocalParameter4dARB (will be remapped) */
   "iidddd\0"
   "glProgramLocalParameter4dARB\0"
   "\0"
   /* _mesa_function_pool[25574]: Color3sv (offset 18) */
   "p\0"
   "glColor3sv\0"
   "\0"
   /* _mesa_function_pool[25588]: GenFragmentShadersATI (will be remapped) */
   "i\0"
   "glGenFragmentShadersATI\0"
   "\0"
   /* _mesa_function_pool[25615]: GetNamedBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[25648]: BlendEquationSeparateiARB (will be remapped) */
   "iii\0"
   "glBlendEquationSeparateiARB\0"
   "glBlendEquationSeparateIndexedAMD\0"
   "glBlendEquationSeparatei\0"
   "\0"
   /* _mesa_function_pool[25740]: TestFenceNV (dynamic) */
   "i\0"
   "glTestFenceNV\0"
   "\0"
   /* _mesa_function_pool[25757]: MultiTexCoord1fvARB (offset 379) */
   "ip\0"
   "glMultiTexCoord1fv\0"
   "glMultiTexCoord1fvARB\0"
   "\0"
   /* _mesa_function_pool[25802]: TexStorage2D (will be remapped) */
   "iiiii\0"
   "glTexStorage2D\0"
   "\0"
   /* _mesa_function_pool[25824]: GetPixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glGetPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[25860]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "glFramebufferTexture2DEXT\0"
   "glFramebufferTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[25942]: GetnMapdvARB (will be remapped) */
   "iiip\0"
   "glGetnMapdvARB\0"
   "\0"
   /* _mesa_function_pool[25963]: GetSamplerParameterfv (will be remapped) */
   "iip\0"
   "glGetSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[25992]: VertexAttrib2dv (will be remapped) */
   "ip\0"
   "glVertexAttrib2dv\0"
   "glVertexAttrib2dvARB\0"
   "\0"
   /* _mesa_function_pool[26035]: Vertex4sv (offset 149) */
   "p\0"
   "glVertex4sv\0"
   "\0"
   /* _mesa_function_pool[26050]: GetQueryObjecti64v (will be remapped) */
   "iip\0"
   "glGetQueryObjecti64v\0"
   "glGetQueryObjecti64vEXT\0"
   "\0"
   /* _mesa_function_pool[26100]: ClampColor (will be remapped) */
   "ii\0"
   "glClampColorARB\0"
   "glClampColor\0"
   "\0"
   /* _mesa_function_pool[26133]: TextureRangeAPPLE (dynamic) */
   "iip\0"
   "glTextureRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[26158]: ConvolutionFilter1D (offset 348) */
   "iiiiip\0"
   "glConvolutionFilter1D\0"
   "glConvolutionFilter1DEXT\0"
   "\0"
   /* _mesa_function_pool[26213]: DrawElementsIndirect (will be remapped) */
   "iip\0"
   "glDrawElementsIndirect\0"
   "\0"
   /* _mesa_function_pool[26241]: WindowPos3sv (will be remapped) */
   "p\0"
   "glWindowPos3sv\0"
   "glWindowPos3svARB\0"
   "glWindowPos3svMESA\0"
   "\0"
   /* _mesa_function_pool[26296]: FragmentMaterialfvSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialfvSGIX\0"
   "\0"
   /* _mesa_function_pool[26326]: CallLists (offset 3) */
   "iip\0"
   "glCallLists\0"
   "\0"
   /* _mesa_function_pool[26343]: AlphaFunc (offset 240) */
   "if\0"
   "glAlphaFunc\0"
   "\0"
   /* _mesa_function_pool[26359]: GetTextureParameterfv (will be remapped) */
   "iip\0"
   "glGetTextureParameterfv\0"
   "\0"
   /* _mesa_function_pool[26388]: EdgeFlag (offset 41) */
   "i\0"
   "glEdgeFlag\0"
   "\0"
   /* _mesa_function_pool[26402]: TexCoord2fNormal3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[26440]: EdgeFlagv (offset 42) */
   "p\0"
   "glEdgeFlagv\0"
   "\0"
   /* _mesa_function_pool[26455]: DepthRangex (will be remapped) */
   "ii\0"
   "glDepthRangexOES\0"
   "glDepthRangex\0"
   "\0"
   /* _mesa_function_pool[26490]: ReplacementCodeubvSUN (dynamic) */
   "p\0"
   "glReplacementCodeubvSUN\0"
   "\0"
   /* _mesa_function_pool[26517]: VDPAUInitNV (will be remapped) */
   "pp\0"
   "glVDPAUInitNV\0"
   "\0"
   /* _mesa_function_pool[26535]: GetBufferParameteri64v (will be remapped) */
   "iip\0"
   "glGetBufferParameteri64v\0"
   "\0"
   /* _mesa_function_pool[26565]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[26583]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "glDepthRangefOES\0"
   "\0"
   /* _mesa_function_pool[26618]: TextureParameteriv (will be remapped) */
   "iip\0"
   "glTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[26644]: ColorFragmentOp3ATI (will be remapped) */
   "iiiiiiiiiiiii\0"
   "glColorFragmentOp3ATI\0"
   "\0"
   /* _mesa_function_pool[26681]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "glValidateProgramARB\0"
   "\0"
   /* _mesa_function_pool[26723]: VertexPointerEXT (will be remapped) */
   "iiiip\0"
   "glVertexPointerEXT\0"
   "\0"
   /* _mesa_function_pool[26749]: VertexAttribI4sv (will be remapped) */
   "ip\0"
   "glVertexAttribI4svEXT\0"
   "glVertexAttribI4sv\0"
   "\0"
   /* _mesa_function_pool[26794]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[26810]: BeginTransformFeedback (will be remapped) */
   "i\0"
   "glBeginTransformFeedback\0"
   "glBeginTransformFeedbackEXT\0"
   "\0"
   /* _mesa_function_pool[26866]: TexCoord2i (offset 106) */
   "ii\0"
   "glTexCoord2i\0"
   "\0"
   /* _mesa_function_pool[26883]: VertexArrayAttribBinding (will be remapped) */
   "iii\0"
   "glVertexArrayAttribBinding\0"
   "\0"
   /* _mesa_function_pool[26915]: Color4ui (offset 37) */
   "iiii\0"
   "glColor4ui\0"
   "\0"
   /* _mesa_function_pool[26932]: TexCoord2f (offset 104) */
   "ff\0"
   "glTexCoord2f\0"
   "\0"
   /* _mesa_function_pool[26949]: TexCoord2d (offset 102) */
   "dd\0"
   "glTexCoord2d\0"
   "\0"
   /* _mesa_function_pool[26966]: GetTransformFeedbackiv (will be remapped) */
   "iip\0"
   "glGetTransformFeedbackiv\0"
   "\0"
   /* _mesa_function_pool[26996]: TexCoord2s (offset 108) */
   "ii\0"
   "glTexCoord2s\0"
   "\0"
   /* _mesa_function_pool[27013]: PointSizePointerOES (will be remapped) */
   "iip\0"
   "glPointSizePointerOES\0"
   "\0"
   /* _mesa_function_pool[27040]: Color4us (offset 39) */
   "iiii\0"
   "glColor4us\0"
   "\0"
   /* _mesa_function_pool[27057]: Color3bv (offset 10) */
   "p\0"
   "glColor3bv\0"
   "\0"
   /* _mesa_function_pool[27071]: PrimitiveRestartNV (will be remapped) */
   "\0"
   "glPrimitiveRestartNV\0"
   "\0"
   /* _mesa_function_pool[27094]: BindBufferOffsetEXT (will be remapped) */
   "iiii\0"
   "glBindBufferOffsetEXT\0"
   "\0"
   /* _mesa_function_pool[27122]: ProvokingVertex (will be remapped) */
   "i\0"
   "glProvokingVertexEXT\0"
   "glProvokingVertex\0"
   "\0"
   /* _mesa_function_pool[27164]: VertexAttribs4fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4fvNV\0"
   "\0"
   /* _mesa_function_pool[27190]: MapControlPointsNV (dynamic) */
   "iiiiiiiip\0"
   "glMapControlPointsNV\0"
   "\0"
   /* _mesa_function_pool[27222]: Vertex2i (offset 130) */
   "ii\0"
   "glVertex2i\0"
   "\0"
   /* _mesa_function_pool[27237]: HintPGI (dynamic) */
   "ii\0"
   "glHintPGI\0"
   "\0"
   /* _mesa_function_pool[27251]: GetQueryBufferObjecti64v (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjecti64v\0"
   "\0"
   /* _mesa_function_pool[27284]: InterleavedArrays (offset 317) */
   "iip\0"
   "glInterleavedArrays\0"
   "\0"
   /* _mesa_function_pool[27309]: RasterPos2fv (offset 65) */
   "p\0"
   "glRasterPos2fv\0"
   "\0"
   /* _mesa_function_pool[27327]: TexCoord1fv (offset 97) */
   "p\0"
   "glTexCoord1fv\0"
   "\0"
   /* _mesa_function_pool[27344]: ProgramNamedParameter4fNV (will be remapped) */
   "iipffff\0"
   "glProgramNamedParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[27381]: MultiTexCoord4dv (offset 401) */
   "ip\0"
   "glMultiTexCoord4dv\0"
   "glMultiTexCoord4dvARB\0"
   "\0"
   /* _mesa_function_pool[27426]: ProgramEnvParameter4fvARB (will be remapped) */
   "iip\0"
   "glProgramEnvParameter4fvARB\0"
   "glProgramParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[27483]: RasterPos4fv (offset 81) */
   "p\0"
   "glRasterPos4fv\0"
   "\0"
   /* _mesa_function_pool[27501]: FragmentLightModeliSGIX (dynamic) */
   "ii\0"
   "glFragmentLightModeliSGIX\0"
   "\0"
   /* _mesa_function_pool[27531]: PushMatrix (offset 298) */
   "\0"
   "glPushMatrix\0"
   "\0"
   /* _mesa_function_pool[27546]: EndList (offset 1) */
   "\0"
   "glEndList\0"
   "\0"
   /* _mesa_function_pool[27558]: DrawRangeElements (offset 338) */
   "iiiiip\0"
   "glDrawRangeElements\0"
   "glDrawRangeElementsEXT\0"
   "\0"
   /* _mesa_function_pool[27609]: GetTexGenxvOES (will be remapped) */
   "iip\0"
   "glGetTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[27631]: VertexAttribs4dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4dvNV\0"
   "\0"
   /* _mesa_function_pool[27657]: DrawTexfvOES (will be remapped) */
   "p\0"
   "glDrawTexfvOES\0"
   "\0"
   /* _mesa_function_pool[27675]: BlendFunciARB (will be remapped) */
   "iii\0"
   "glBlendFunciARB\0"
   "glBlendFuncIndexedAMD\0"
   "glBlendFunci\0"
   "\0"
   /* _mesa_function_pool[27731]: ClearNamedFramebufferfi (will be remapped) */
   "iifi\0"
   "glClearNamedFramebufferfi\0"
   "\0"
   /* _mesa_function_pool[27763]: ClearNamedFramebufferfv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferfv\0"
   "\0"
   /* _mesa_function_pool[27795]: GlobalAlphaFactorbSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorbSUN\0"
   "\0"
   /* _mesa_function_pool[27822]: Uniform2ui (will be remapped) */
   "iii\0"
   "glUniform2uiEXT\0"
   "glUniform2ui\0"
   "\0"
   /* _mesa_function_pool[27856]: ScissorIndexed (will be remapped) */
   "iiiii\0"
   "glScissorIndexed\0"
   "\0"
   /* _mesa_function_pool[27880]: End (offset 43) */
   "\0"
   "glEnd\0"
   "\0"
   /* _mesa_function_pool[27888]: NamedFramebufferParameteri (will be remapped) */
   "iii\0"
   "glNamedFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[27922]: BindVertexBuffers (will be remapped) */
   "iippp\0"
   "glBindVertexBuffers\0"
   "\0"
   /* _mesa_function_pool[27949]: GetSamplerParameteriv (will be remapped) */
   "iip\0"
   "glGetSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[27978]: GenProgramPipelines (will be remapped) */
   "ip\0"
   "glGenProgramPipelines\0"
   "glGenProgramPipelinesEXT\0"
   "\0"
   /* _mesa_function_pool[28029]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[28041]: IsProgramPipeline (will be remapped) */
   "i\0"
   "glIsProgramPipeline\0"
   "glIsProgramPipelineEXT\0"
   "\0"
   /* _mesa_function_pool[28087]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[28109]: GetFragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[28142]: WeightPointerARB (dynamic) */
   "iiip\0"
   "glWeightPointerARB\0"
   "glWeightPointerOES\0"
   "\0"
   /* _mesa_function_pool[28186]: TextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[28215]: Normal3x (will be remapped) */
   "iii\0"
   "glNormal3xOES\0"
   "glNormal3x\0"
   "\0"
   /* _mesa_function_pool[28245]: VertexAttrib4fARB (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "glVertexAttrib4fARB\0"
   "\0"
   /* _mesa_function_pool[28289]: TexCoord4fv (offset 121) */
   "p\0"
   "glTexCoord4fv\0"
   "\0"
   /* _mesa_function_pool[28306]: ReadnPixelsARB (will be remapped) */
   "iiiiiiip\0"
   "glReadnPixelsARB\0"
   "\0"
   /* _mesa_function_pool[28333]: InvalidateTexSubImage (will be remapped) */
   "iiiiiiii\0"
   "glInvalidateTexSubImage\0"
   "\0"
   /* _mesa_function_pool[28367]: Normal3s (offset 60) */
   "iii\0"
   "glNormal3s\0"
   "\0"
   /* _mesa_function_pool[28383]: Materialxv (will be remapped) */
   "iip\0"
   "glMaterialxvOES\0"
   "glMaterialxv\0"
   "\0"
   /* _mesa_function_pool[28417]: Normal3i (offset 58) */
   "iii\0"
   "glNormal3i\0"
   "\0"
   /* _mesa_function_pool[28433]: ProgramNamedParameter4fvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4fvNV\0"
   "\0"
   /* _mesa_function_pool[28468]: Normal3b (offset 52) */
   "iii\0"
   "glNormal3b\0"
   "\0"
   /* _mesa_function_pool[28484]: Normal3d (offset 54) */
   "ddd\0"
   "glNormal3d\0"
   "\0"
   /* _mesa_function_pool[28500]: Normal3f (offset 56) */
   "fff\0"
   "glNormal3f\0"
   "\0"
   /* _mesa_function_pool[28516]: Indexi (offset 48) */
   "i\0"
   "glIndexi\0"
   "\0"
   /* _mesa_function_pool[28528]: Uniform1uiv (will be remapped) */
   "iip\0"
   "glUniform1uivEXT\0"
   "glUniform1uiv\0"
   "\0"
   /* _mesa_function_pool[28564]: VertexAttribI2uiEXT (will be remapped) */
   "iii\0"
   "glVertexAttribI2uiEXT\0"
   "glVertexAttribI2ui\0"
   "\0"
   /* _mesa_function_pool[28610]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "glIsRenderbufferEXT\0"
   "glIsRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[28670]: NormalP3uiv (will be remapped) */
   "ip\0"
   "glNormalP3uiv\0"
   "\0"
   /* _mesa_function_pool[28688]: Indexf (offset 46) */
   "f\0"
   "glIndexf\0"
   "\0"
   /* _mesa_function_pool[28700]: Indexd (offset 44) */
   "d\0"
   "glIndexd\0"
   "\0"
   /* _mesa_function_pool[28712]: GetMaterialiv (offset 270) */
   "iip\0"
   "glGetMaterialiv\0"
   "\0"
   /* _mesa_function_pool[28733]: Indexs (offset 50) */
   "i\0"
   "glIndexs\0"
   "\0"
   /* _mesa_function_pool[28745]: MultiTexCoordP1uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP1uiv\0"
   "\0"
   /* _mesa_function_pool[28771]: ConvolutionFilter2D (offset 349) */
   "iiiiiip\0"
   "glConvolutionFilter2D\0"
   "glConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[28827]: Vertex2d (offset 126) */
   "dd\0"
   "glVertex2d\0"
   "\0"
   /* _mesa_function_pool[28842]: Vertex2f (offset 128) */
   "ff\0"
   "glVertex2f\0"
   "\0"
   /* _mesa_function_pool[28857]: Color4bv (offset 26) */
   "p\0"
   "glColor4bv\0"
   "\0"
   /* _mesa_function_pool[28871]: ProgramUniformMatrix3x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2dv\0"
   "\0"
   /* _mesa_function_pool[28906]: VertexAttrib2fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2fvNV\0"
   "\0"
   /* _mesa_function_pool[28930]: Vertex2s (offset 132) */
   "ii\0"
   "glVertex2s\0"
   "\0"
   /* _mesa_function_pool[28945]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "glActiveTextureARB\0"
   "\0"
   /* _mesa_function_pool[28983]: GlobalAlphaFactorfSUN (dynamic) */
   "f\0"
   "glGlobalAlphaFactorfSUN\0"
   "\0"
   /* _mesa_function_pool[29010]: InvalidateNamedFramebufferSubData (will be remapped) */
   "iipiiii\0"
   "glInvalidateNamedFramebufferSubData\0"
   "\0"
   /* _mesa_function_pool[29055]: ColorP4uiv (will be remapped) */
   "ip\0"
   "glColorP4uiv\0"
   "\0"
   /* _mesa_function_pool[29072]: DrawTexxOES (will be remapped) */
   "iiiii\0"
   "glDrawTexxOES\0"
   "\0"
   /* _mesa_function_pool[29093]: SetFenceNV (dynamic) */
   "ii\0"
   "glSetFenceNV\0"
   "\0"
   /* _mesa_function_pool[29110]: PixelTexGenParameterivSGIS (dynamic) */
   "ip\0"
   "glPixelTexGenParameterivSGIS\0"
   "\0"
   /* _mesa_function_pool[29143]: MultiTexCoordP3ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP3ui\0"
   "\0"
   /* _mesa_function_pool[29168]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "glGetAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[29215]: GetCombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glGetCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[29252]: DrawBuffer (offset 202) */
   "i\0"
   "glDrawBuffer\0"
   "\0"
   /* _mesa_function_pool[29268]: MultiTexCoord2dv (offset 385) */
   "ip\0"
   "glMultiTexCoord2dv\0"
   "glMultiTexCoord2dvARB\0"
   "\0"
   /* _mesa_function_pool[29313]: IsSampler (will be remapped) */
   "i\0"
   "glIsSampler\0"
   "\0"
   /* _mesa_function_pool[29328]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[29344]: NamedRenderbufferStorageMultisample (will be remapped) */
   "iiiii\0"
   "glNamedRenderbufferStorageMultisample\0"
   "\0"
   /* _mesa_function_pool[29389]: Tangent3fvEXT (dynamic) */
   "p\0"
   "glTangent3fvEXT\0"
   "\0"
   /* _mesa_function_pool[29408]: ColorMaterial (offset 151) */
   "ii\0"
   "glColorMaterial\0"
   "\0"
   /* _mesa_function_pool[29428]: RasterPos3sv (offset 77) */
   "p\0"
   "glRasterPos3sv\0"
   "\0"
   /* _mesa_function_pool[29446]: TexCoordP2ui (will be remapped) */
   "ii\0"
   "glTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[29465]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[29487]: VertexAttrib3fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "glVertexAttrib3fvARB\0"
   "\0"
   /* _mesa_function_pool[29530]: ProgramUniformMatrix3x4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x4fv\0"
   "glProgramUniformMatrix3x4fvEXT\0"
   "\0"
   /* _mesa_function_pool[29596]: PixelTransformParameterfEXT (dynamic) */
   "iif\0"
   "glPixelTransformParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[29631]: TextureColorMaskSGIS (dynamic) */
   "iiii\0"
   "glTextureColorMaskSGIS\0"
   "\0"
   /* _mesa_function_pool[29660]: GetColorTable (offset 343) */
   "iiip\0"
   "glGetColorTable\0"
   "glGetColorTableSGI\0"
   "glGetColorTableEXT\0"
   "\0"
   /* _mesa_function_pool[29720]: TexCoord3i (offset 114) */
   "iii\0"
   "glTexCoord3i\0"
   "\0"
   /* _mesa_function_pool[29738]: CopyColorTable (offset 342) */
   "iiiii\0"
   "glCopyColorTable\0"
   "glCopyColorTableSGI\0"
   "\0"
   /* _mesa_function_pool[29782]: Frustum (offset 289) */
   "dddddd\0"
   "glFrustum\0"
   "\0"
   /* _mesa_function_pool[29800]: TexCoord3d (offset 110) */
   "ddd\0"
   "glTexCoord3d\0"
   "\0"
   /* _mesa_function_pool[29818]: GetTextureParameteriv (will be remapped) */
   "iip\0"
   "glGetTextureParameteriv\0"
   "\0"
   /* _mesa_function_pool[29847]: TexCoord3f (offset 112) */
   "fff\0"
   "glTexCoord3f\0"
   "\0"
   /* _mesa_function_pool[29865]: DepthRangeArrayv (will be remapped) */
   "iip\0"
   "glDepthRangeArrayv\0"
   "\0"
   /* _mesa_function_pool[29889]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "glDeleteTexturesEXT\0"
   "\0"
   /* _mesa_function_pool[29930]: TexCoordPointerEXT (will be remapped) */
   "iiiip\0"
   "glTexCoordPointerEXT\0"
   "\0"
   /* _mesa_function_pool[29958]: TexCoord3s (offset 116) */
   "iii\0"
   "glTexCoord3s\0"
   "\0"
   /* _mesa_function_pool[29976]: GetTexLevelParameteriv (offset 285) */
   "iiip\0"
   "glGetTexLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[30007]: TextureParameterIuiv (will be remapped) */
   "iip\0"
   "glTextureParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[30035]: CombinerStageParameterfvNV (dynamic) */
   "iip\0"
   "glCombinerStageParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[30069]: GenPerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glGenPerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[30094]: ClearAccum (offset 204) */
   "ffff\0"
   "glClearAccum\0"
   "\0"
   /* _mesa_function_pool[30113]: DeformSGIX (dynamic) */
   "i\0"
   "glDeformSGIX\0"
   "\0"
   /* _mesa_function_pool[30129]: TexCoord4iv (offset 123) */
   "p\0"
   "glTexCoord4iv\0"
   "\0"
   /* _mesa_function_pool[30146]: TexStorage3D (will be remapped) */
   "iiiiii\0"
   "glTexStorage3D\0"
   "\0"
   /* _mesa_function_pool[30169]: FramebufferTexture3D (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3D\0"
   "glFramebufferTexture3DEXT\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[30252]: FragmentLightModelfvSGIX (dynamic) */
   "ip\0"
   "glFragmentLightModelfvSGIX\0"
   "\0"
   /* _mesa_function_pool[30283]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "glGetBufferParameterivARB\0"
   "\0"
   /* _mesa_function_pool[30337]: VertexAttrib2fNV (will be remapped) */
   "iff\0"
   "glVertexAttrib2fNV\0"
   "\0"
   /* _mesa_function_pool[30361]: GetFragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[30391]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "glCopyTexImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[30438]: Vertex3fv (offset 137) */
   "p\0"
   "glVertex3fv\0"
   "\0"
   /* _mesa_function_pool[30453]: WindowPos4dvMESA (will be remapped) */
   "p\0"
   "glWindowPos4dvMESA\0"
   "\0"
   /* _mesa_function_pool[30475]: MultiTexCoordP2ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP2ui\0"
   "\0"
   /* _mesa_function_pool[30500]: VertexAttribs1dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1dvNV\0"
   "\0"
   /* _mesa_function_pool[30526]: IsQuery (will be remapped) */
   "i\0"
   "glIsQuery\0"
   "glIsQueryARB\0"
   "\0"
   /* _mesa_function_pool[30552]: EdgeFlagPointerEXT (will be remapped) */
   "iip\0"
   "glEdgeFlagPointerEXT\0"
   "\0"
   /* _mesa_function_pool[30578]: VertexAttribs2svNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2svNV\0"
   "\0"
   /* _mesa_function_pool[30604]: CreateShaderProgramv (will be remapped) */
   "iip\0"
   "glCreateShaderProgramv\0"
   "glCreateShaderProgramvEXT\0"
   "\0"
   /* _mesa_function_pool[30658]: BlendEquationiARB (will be remapped) */
   "ii\0"
   "glBlendEquationiARB\0"
   "glBlendEquationIndexedAMD\0"
   "glBlendEquationi\0"
   "\0"
   /* _mesa_function_pool[30725]: VertexAttribI4uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI4uivEXT\0"
   "glVertexAttribI4uiv\0"
   "\0"
   /* _mesa_function_pool[30772]: PointSizex (will be remapped) */
   "i\0"
   "glPointSizexOES\0"
   "glPointSizex\0"
   "\0"
   /* _mesa_function_pool[30804]: PolygonMode (offset 174) */
   "ii\0"
   "glPolygonMode\0"
   "\0"
   /* _mesa_function_pool[30822]: CreateFramebuffers (will be remapped) */
   "ip\0"
   "glCreateFramebuffers\0"
   "\0"
   /* _mesa_function_pool[30847]: VertexAttribI1iEXT (will be remapped) */
   "ii\0"
   "glVertexAttribI1iEXT\0"
   "glVertexAttribI1i\0"
   "\0"
   /* _mesa_function_pool[30890]: VertexAttrib4Niv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Niv\0"
   "glVertexAttrib4NivARB\0"
   "\0"
   /* _mesa_function_pool[30935]: GetMapAttribParameterivNV (dynamic) */
   "iiip\0"
   "glGetMapAttribParameterivNV\0"
   "\0"
   /* _mesa_function_pool[30969]: GetnUniformdvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformdvARB\0"
   "\0"
   /* _mesa_function_pool[30994]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "glLinkProgramARB\0"
   "\0"
   /* _mesa_function_pool[31028]: ProgramUniform4d (will be remapped) */
   "iidddd\0"
   "glProgramUniform4d\0"
   "\0"
   /* _mesa_function_pool[31055]: ProgramUniform4f (will be remapped) */
   "iiffff\0"
   "glProgramUniform4f\0"
   "glProgramUniform4fEXT\0"
   "\0"
   /* _mesa_function_pool[31104]: ProgramUniform4i (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4i\0"
   "glProgramUniform4iEXT\0"
   "\0"
   /* _mesa_function_pool[31153]: GetFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[31186]: ListParameterfvSGIX (dynamic) */
   "iip\0"
   "glListParameterfvSGIX\0"
   "\0"
   /* _mesa_function_pool[31213]: GetNamedBufferPointerv (will be remapped) */
   "iip\0"
   "glGetNamedBufferPointerv\0"
   "\0"
   /* _mesa_function_pool[31243]: VertexAttrib4d (will be remapped) */
   "idddd\0"
   "glVertexAttrib4d\0"
   "glVertexAttrib4dARB\0"
   "\0"
   /* _mesa_function_pool[31287]: WindowPos4sMESA (will be remapped) */
   "iiii\0"
   "glWindowPos4sMESA\0"
   "\0"
   /* _mesa_function_pool[31311]: VertexAttrib4s (will be remapped) */
   "iiiii\0"
   "glVertexAttrib4s\0"
   "glVertexAttrib4sARB\0"
   "\0"
   /* _mesa_function_pool[31355]: VertexAttrib1dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1dvNV\0"
   "\0"
   /* _mesa_function_pool[31379]: ReplacementCodePointerSUN (dynamic) */
   "iip\0"
   "glReplacementCodePointerSUN\0"
   "\0"
   /* _mesa_function_pool[31412]: TexStorage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexStorage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[31447]: Binormal3bvEXT (dynamic) */
   "p\0"
   "glBinormal3bvEXT\0"
   "\0"
   /* _mesa_function_pool[31467]: SamplerParameteriv (will be remapped) */
   "iip\0"
   "glSamplerParameteriv\0"
   "\0"
   /* _mesa_function_pool[31493]: VertexAttribP3uiv (will be remapped) */
   "iiip\0"
   "glVertexAttribP3uiv\0"
   "\0"
   /* _mesa_function_pool[31519]: ScissorIndexedv (will be remapped) */
   "ip\0"
   "glScissorIndexedv\0"
   "\0"
   /* _mesa_function_pool[31541]: Color4ubVertex2fSUN (dynamic) */
   "iiiiff\0"
   "glColor4ubVertex2fSUN\0"
   "\0"
   /* _mesa_function_pool[31571]: FragmentColorMaterialSGIX (dynamic) */
   "ii\0"
   "glFragmentColorMaterialSGIX\0"
   "\0"
   /* _mesa_function_pool[31603]: GetStringi (will be remapped) */
   "ii\0"
   "glGetStringi\0"
   "\0"
   /* _mesa_function_pool[31620]: Uniform2dv (will be remapped) */
   "iip\0"
   "glUniform2dv\0"
   "\0"
   /* _mesa_function_pool[31638]: VertexAttrib4dv (will be remapped) */
   "ip\0"
   "glVertexAttrib4dv\0"
   "glVertexAttrib4dvARB\0"
   "\0"
   /* _mesa_function_pool[31681]: CreateTextures (will be remapped) */
   "iip\0"
   "glCreateTextures\0"
   "\0"
   /* _mesa_function_pool[31703]: EvalCoord2dv (offset 233) */
   "p\0"
   "glEvalCoord2dv\0"
   "\0"
   /* _mesa_function_pool[31721]: VertexAttrib1fNV (will be remapped) */
   "if\0"
   "glVertexAttrib1fNV\0"
   "\0"
   /* _mesa_function_pool[31744]: CompressedTexSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTexSubImage1D\0"
   "glCompressedTexSubImage1DARB\0"
   "\0"
   /* _mesa_function_pool[31808]: GetSeparableFilter (offset 359) */
   "iiippp\0"
   "glGetSeparableFilter\0"
   "glGetSeparableFilterEXT\0"
   "\0"
   /* _mesa_function_pool[31861]: ReplacementCodeusSUN (dynamic) */
   "i\0"
   "glReplacementCodeusSUN\0"
   "\0"
   /* _mesa_function_pool[31887]: FeedbackBuffer (offset 194) */
   "iip\0"
   "glFeedbackBuffer\0"
   "\0"
   /* _mesa_function_pool[31909]: RasterPos2iv (offset 67) */
   "p\0"
   "glRasterPos2iv\0"
   "\0"
   /* _mesa_function_pool[31927]: TexImage1D (offset 182) */
   "iiiiiiip\0"
   "glTexImage1D\0"
   "\0"
   /* _mesa_function_pool[31950]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElements\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[32000]: GetnSeparableFilterARB (will be remapped) */
   "iiiipipp\0"
   "glGetnSeparableFilterARB\0"
   "\0"
   /* _mesa_function_pool[32035]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[32050]: MultiModeDrawArraysIBM (will be remapped) */
   "pppii\0"
   "glMultiModeDrawArraysIBM\0"
   "\0"
   /* _mesa_function_pool[32082]: Tangent3ivEXT (dynamic) */
   "p\0"
   "glTangent3ivEXT\0"
   "\0"
   /* _mesa_function_pool[32101]: LightEnviSGIX (dynamic) */
   "ii\0"
   "glLightEnviSGIX\0"
   "\0"
   /* _mesa_function_pool[32121]: Normal3dv (offset 55) */
   "p\0"
   "glNormal3dv\0"
   "\0"
   /* _mesa_function_pool[32136]: Lightf (offset 159) */
   "iif\0"
   "glLightf\0"
   "\0"
   /* _mesa_function_pool[32150]: MatrixMode (offset 293) */
   "i\0"
   "glMatrixMode\0"
   "\0"
   /* _mesa_function_pool[32166]: GetPixelMapusv (offset 273) */
   "ip\0"
   "glGetPixelMapusv\0"
   "\0"
   /* _mesa_function_pool[32187]: Lighti (offset 161) */
   "iii\0"
   "glLighti\0"
   "\0"
   /* _mesa_function_pool[32201]: VertexAttribPointerNV (will be remapped) */
   "iiiip\0"
   "glVertexAttribPointerNV\0"
   "\0"
   /* _mesa_function_pool[32232]: GetFragDataIndex (will be remapped) */
   "ip\0"
   "glGetFragDataIndex\0"
   "\0"
   /* _mesa_function_pool[32255]: Lightx (will be remapped) */
   "iii\0"
   "glLightxOES\0"
   "glLightx\0"
   "\0"
   /* _mesa_function_pool[32281]: ProgramUniform3fv (will be remapped) */
   "iiip\0"
   "glProgramUniform3fv\0"
   "glProgramUniform3fvEXT\0"
   "\0"
   /* _mesa_function_pool[32330]: MultMatrixd (offset 295) */
   "p\0"
   "glMultMatrixd\0"
   "\0"
   /* _mesa_function_pool[32347]: MultMatrixf (offset 294) */
   "p\0"
   "glMultMatrixf\0"
   "\0"
   /* _mesa_function_pool[32364]: MultiTexCoord4fvARB (offset 403) */
   "ip\0"
   "glMultiTexCoord4fv\0"
   "glMultiTexCoord4fvARB\0"
   "\0"
   /* _mesa_function_pool[32409]: UniformMatrix2x3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2x3fv\0"
   "\0"
   /* _mesa_function_pool[32436]: TrackMatrixNV (will be remapped) */
   "iiii\0"
   "glTrackMatrixNV\0"
   "\0"
   /* _mesa_function_pool[32458]: SamplerParameterf (will be remapped) */
   "iif\0"
   "glSamplerParameterf\0"
   "\0"
   /* _mesa_function_pool[32483]: UniformMatrix3dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[32508]: PointParameterx (will be remapped) */
   "ii\0"
   "glPointParameterxOES\0"
   "glPointParameterx\0"
   "\0"
   /* _mesa_function_pool[32551]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "glDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[32585]: Uniform3dv (will be remapped) */
   "iip\0"
   "glUniform3dv\0"
   "\0"
   /* _mesa_function_pool[32603]: PointParameteri (will be remapped) */
   "ii\0"
   "glPointParameteri\0"
   "glPointParameteriNV\0"
   "\0"
   /* _mesa_function_pool[32645]: PointParameterf (will be remapped) */
   "if\0"
   "glPointParameterf\0"
   "glPointParameterfARB\0"
   "glPointParameterfEXT\0"
   "glPointParameterfSGIS\0"
   "\0"
   /* _mesa_function_pool[32731]: GlobalAlphaFactorsSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorsSUN\0"
   "\0"
   /* _mesa_function_pool[32758]: VertexAttribBinding (will be remapped) */
   "ii\0"
   "glVertexAttribBinding\0"
   "\0"
   /* _mesa_function_pool[32784]: TextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[32815]: ReplacementCodeuiTexCoord2fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiTexCoord2fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[32862]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[32880]: GetProgramParameterdvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[32912]: ProgramUniform1dv (will be remapped) */
   "iiip\0"
   "glProgramUniform1dv\0"
   "\0"
   /* _mesa_function_pool[32938]: GetProgramEnvParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramEnvParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[32973]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "glDeleteBuffersARB\0"
   "\0"
   /* _mesa_function_pool[33012]: GetBufferSubData (will be remapped) */
   "iiip\0"
   "glGetBufferSubData\0"
   "glGetBufferSubDataARB\0"
   "\0"
   /* _mesa_function_pool[33059]: GetNamedRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[33098]: GetPerfMonitorGroupsAMD (will be remapped) */
   "pip\0"
   "glGetPerfMonitorGroupsAMD\0"
   "\0"
   /* _mesa_function_pool[33129]: FlushRasterSGIX (dynamic) */
   "\0"
   "glFlushRasterSGIX\0"
   "\0"
   /* _mesa_function_pool[33149]: VertexAttribP2ui (will be remapped) */
   "iiii\0"
   "glVertexAttribP2ui\0"
   "\0"
   /* _mesa_function_pool[33174]: ProgramUniform4dv (will be remapped) */
   "iiip\0"
   "glProgramUniform4dv\0"
   "\0"
   /* _mesa_function_pool[33200]: GetMinmaxParameteriv (offset 366) */
   "iip\0"
   "glGetMinmaxParameteriv\0"
   "glGetMinmaxParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[33254]: DrawTexivOES (will be remapped) */
   "p\0"
   "glDrawTexivOES\0"
   "\0"
   /* _mesa_function_pool[33272]: CopyTexImage1D (offset 323) */
   "iiiiiii\0"
   "glCopyTexImage1D\0"
   "glCopyTexImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[33318]: InvalidateNamedFramebufferData (will be remapped) */
   "iip\0"
   "glInvalidateNamedFramebufferData\0"
   "\0"
   /* _mesa_function_pool[33356]: GetnColorTableARB (will be remapped) */
   "iiiip\0"
   "glGetnColorTableARB\0"
   "\0"
   /* _mesa_function_pool[33383]: VertexAttribFormat (will be remapped) */
   "iiiii\0"
   "glVertexAttribFormat\0"
   "\0"
   /* _mesa_function_pool[33411]: Vertex3i (offset 138) */
   "iii\0"
   "glVertex3i\0"
   "\0"
   /* _mesa_function_pool[33427]: Vertex3f (offset 136) */
   "fff\0"
   "glVertex3f\0"
   "\0"
   /* _mesa_function_pool[33443]: Vertex3d (offset 134) */
   "ddd\0"
   "glVertex3d\0"
   "\0"
   /* _mesa_function_pool[33459]: GetProgramPipelineiv (will be remapped) */
   "iip\0"
   "glGetProgramPipelineiv\0"
   "glGetProgramPipelineivEXT\0"
   "\0"
   /* _mesa_function_pool[33513]: ReadBuffer (offset 254) */
   "i\0"
   "glReadBuffer\0"
   "glReadBufferNV\0"
   "\0"
   /* _mesa_function_pool[33544]: ConvolutionParameteri (offset 352) */
   "iii\0"
   "glConvolutionParameteri\0"
   "glConvolutionParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[33600]: GetTexParameterIiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIivEXT\0"
   "glGetTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[33650]: Vertex3s (offset 140) */
   "iii\0"
   "glVertex3s\0"
   "\0"
   /* _mesa_function_pool[33666]: ConvolutionParameterf (offset 350) */
   "iif\0"
   "glConvolutionParameterf\0"
   "glConvolutionParameterfEXT\0"
   "\0"
   /* _mesa_function_pool[33722]: GetColorTableParameteriv (offset 345) */
   "iip\0"
   "glGetColorTableParameteriv\0"
   "glGetColorTableParameterivSGI\0"
   "glGetColorTableParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[33814]: GetTransformFeedbackVarying (will be remapped) */
   "iiipppp\0"
   "glGetTransformFeedbackVarying\0"
   "glGetTransformFeedbackVaryingEXT\0"
   "\0"
   /* _mesa_function_pool[33886]: GetNextPerfQueryIdINTEL (will be remapped) */
   "ip\0"
   "glGetNextPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[33916]: TexCoord3fv (offset 113) */
   "p\0"
   "glTexCoord3fv\0"
   "\0"
   /* _mesa_function_pool[33933]: TextureBarrierNV (will be remapped) */
   "\0"
   "glTextureBarrier\0"
   "glTextureBarrierNV\0"
   "\0"
   /* _mesa_function_pool[33971]: GetProgramInterfaceiv (will be remapped) */
   "iiip\0"
   "glGetProgramInterfaceiv\0"
   "\0"
   /* _mesa_function_pool[34001]: ReplacementCodeuiColor4fNormal3fVertex3fSUN (dynamic) */
   "iffffffffff\0"
   "glReplacementCodeuiColor4fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[34060]: ProgramLocalParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramLocalParameter4fARB\0"
   "\0"
   /* _mesa_function_pool[34097]: PauseTransformFeedback (will be remapped) */
   "\0"
   "glPauseTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[34124]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[34142]: NamedFramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glNamedFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[34179]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "glCompileShaderARB\0"
   "\0"
   /* _mesa_function_pool[34217]: Vertex2iv (offset 131) */
   "p\0"
   "glVertex2iv\0"
   "\0"
   /* _mesa_function_pool[34232]: GetVertexArrayIndexediv (will be remapped) */
   "iiip\0"
   "glGetVertexArrayIndexediv\0"
   "\0"
   /* _mesa_function_pool[34264]: TexParameterIiv (will be remapped) */
   "iip\0"
   "glTexParameterIivEXT\0"
   "glTexParameterIiv\0"
   "\0"
   /* _mesa_function_pool[34308]: TexGendv (offset 189) */
   "iip\0"
   "glTexGendv\0"
   "\0"
   /* _mesa_function_pool[34324]: TextureLightEXT (dynamic) */
   "i\0"
   "glTextureLightEXT\0"
   "\0"
   /* _mesa_function_pool[34345]: ResetMinmax (offset 370) */
   "i\0"
   "glResetMinmax\0"
   "glResetMinmaxEXT\0"
   "\0"
   /* _mesa_function_pool[34379]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "glSampleCoverageARB\0"
   "\0"
   /* _mesa_function_pool[34420]: SpriteParameterfSGIX (dynamic) */
   "if\0"
   "glSpriteParameterfSGIX\0"
   "\0"
   /* _mesa_function_pool[34447]: GenerateTextureMipmap (will be remapped) */
   "i\0"
   "glGenerateTextureMipmap\0"
   "\0"
   /* _mesa_function_pool[34474]: DeleteProgramsARB (will be remapped) */
   "ip\0"
   "glDeleteProgramsARB\0"
   "glDeleteProgramsNV\0"
   "\0"
   /* _mesa_function_pool[34517]: ShadeModel (offset 177) */
   "i\0"
   "glShadeModel\0"
   "\0"
   /* _mesa_function_pool[34533]: CreateQueries (will be remapped) */
   "iip\0"
   "glCreateQueries\0"
   "\0"
   /* _mesa_function_pool[34554]: FogFuncSGIS (dynamic) */
   "ip\0"
   "glFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[34572]: TexCoord4fVertex4fSUN (dynamic) */
   "ffffffff\0"
   "glTexCoord4fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[34606]: MultiDrawArrays (will be remapped) */
   "ippi\0"
   "glMultiDrawArrays\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[34651]: GetProgramLocalParameterdvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterdvARB\0"
   "\0"
   /* _mesa_function_pool[34688]: BufferParameteriAPPLE (will be remapped) */
   "iii\0"
   "glBufferParameteriAPPLE\0"
   "\0"
   /* _mesa_function_pool[34717]: MapBufferRange (will be remapped) */
   "iiii\0"
   "glMapBufferRange\0"
   "glMapBufferRangeEXT\0"
   "\0"
   /* _mesa_function_pool[34760]: DispatchCompute (will be remapped) */
   "iii\0"
   "glDispatchCompute\0"
   "\0"
   /* _mesa_function_pool[34783]: UseProgramStages (will be remapped) */
   "iii\0"
   "glUseProgramStages\0"
   "glUseProgramStagesEXT\0"
   "\0"
   /* _mesa_function_pool[34829]: ProgramUniformMatrix4fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4fv\0"
   "glProgramUniformMatrix4fvEXT\0"
   "\0"
   /* _mesa_function_pool[34891]: FinishAsyncSGIX (dynamic) */
   "p\0"
   "glFinishAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[34912]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "glFramebufferRenderbufferEXT\0"
   "glFramebufferRenderbufferOES\0"
   "\0"
   /* _mesa_function_pool[35002]: IsProgramARB (will be remapped) */
   "i\0"
   "glIsProgramARB\0"
   "glIsProgramNV\0"
   "\0"
   /* _mesa_function_pool[35034]: Map2d (offset 222) */
   "iddiiddiip\0"
   "glMap2d\0"
   "\0"
   /* _mesa_function_pool[35054]: Map2f (offset 223) */
   "iffiiffiip\0"
   "glMap2f\0"
   "\0"
   /* _mesa_function_pool[35074]: ProgramStringARB (will be remapped) */
   "iiip\0"
   "glProgramStringARB\0"
   "\0"
   /* _mesa_function_pool[35099]: CopyTextureSubImage2D (will be remapped) */
   "iiiiiiii\0"
   "glCopyTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[35133]: MultiTexCoord4s (offset 406) */
   "iiiii\0"
   "glMultiTexCoord4s\0"
   "glMultiTexCoord4sARB\0"
   "\0"
   /* _mesa_function_pool[35179]: ViewportIndexedf (will be remapped) */
   "iffff\0"
   "glViewportIndexedf\0"
   "\0"
   /* _mesa_function_pool[35205]: MultiTexCoord4i (offset 404) */
   "iiiii\0"
   "glMultiTexCoord4i\0"
   "glMultiTexCoord4iARB\0"
   "\0"
   /* _mesa_function_pool[35251]: ApplyTextureEXT (dynamic) */
   "i\0"
   "glApplyTextureEXT\0"
   "\0"
   /* _mesa_function_pool[35272]: DebugMessageControl (will be remapped) */
   "iiiipi\0"
   "glDebugMessageControlARB\0"
   "glDebugMessageControl\0"
   "\0"
   /* _mesa_function_pool[35327]: MultiTexCoord4d (offset 400) */
   "idddd\0"
   "glMultiTexCoord4d\0"
   "glMultiTexCoord4dARB\0"
   "\0"
   /* _mesa_function_pool[35373]: GetHistogram (offset 361) */
   "iiiip\0"
   "glGetHistogram\0"
   "glGetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[35413]: Translatex (will be remapped) */
   "iii\0"
   "glTranslatexOES\0"
   "glTranslatex\0"
   "\0"
   /* _mesa_function_pool[35447]: IglooInterfaceSGIX (dynamic) */
   "ip\0"
   "glIglooInterfaceSGIX\0"
   "\0"
   /* _mesa_function_pool[35472]: Indexsv (offset 51) */
   "p\0"
   "glIndexsv\0"
   "\0"
   /* _mesa_function_pool[35485]: VertexAttrib1fvARB (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "glVertexAttrib1fvARB\0"
   "\0"
   /* _mesa_function_pool[35528]: TexCoord2dv (offset 103) */
   "p\0"
   "glTexCoord2dv\0"
   "\0"
   /* _mesa_function_pool[35545]: GetDetailTexFuncSGIS (dynamic) */
   "ip\0"
   "glGetDetailTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[35572]: Translated (offset 303) */
   "ddd\0"
   "glTranslated\0"
   "\0"
   /* _mesa_function_pool[35590]: Translatef (offset 304) */
   "fff\0"
   "glTranslatef\0"
   "\0"
   /* _mesa_function_pool[35608]: MultTransposeMatrixd (will be remapped) */
   "p\0"
   "glMultTransposeMatrixd\0"
   "glMultTransposeMatrixdARB\0"
   "\0"
   /* _mesa_function_pool[35660]: ProgramUniform4uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform4uiv\0"
   "glProgramUniform4uivEXT\0"
   "\0"
   /* _mesa_function_pool[35711]: GetPerfCounterInfoINTEL (will be remapped) */
   "iiipipppppp\0"
   "glGetPerfCounterInfoINTEL\0"
   "\0"
   /* _mesa_function_pool[35750]: RenderMode (offset 196) */
   "i\0"
   "glRenderMode\0"
   "\0"
   /* _mesa_function_pool[35766]: MultiTexCoord1fARB (offset 378) */
   "if\0"
   "glMultiTexCoord1f\0"
   "glMultiTexCoord1fARB\0"
   "\0"
   /* _mesa_function_pool[35809]: SecondaryColor3d (will be remapped) */
   "ddd\0"
   "glSecondaryColor3d\0"
   "glSecondaryColor3dEXT\0"
   "\0"
   /* _mesa_function_pool[35855]: FramebufferParameteri (will be remapped) */
   "iii\0"
   "glFramebufferParameteri\0"
   "\0"
   /* _mesa_function_pool[35884]: VertexAttribs4ubvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs4ubvNV\0"
   "\0"
   /* _mesa_function_pool[35911]: WeightsvARB (dynamic) */
   "ip\0"
   "glWeightsvARB\0"
   "\0"
   /* _mesa_function_pool[35929]: LightModelxv (will be remapped) */
   "ip\0"
   "glLightModelxvOES\0"
   "glLightModelxv\0"
   "\0"
   /* _mesa_function_pool[35966]: CopyTexSubImage1D (offset 325) */
   "iiiiii\0"
   "glCopyTexSubImage1D\0"
   "glCopyTexSubImage1DEXT\0"
   "\0"
   /* _mesa_function_pool[36017]: TextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[36050]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[36069]: CopyPixels (offset 255) */
   "iiiii\0"
   "glCopyPixels\0"
   "\0"
   /* _mesa_function_pool[36089]: TexGenxvOES (will be remapped) */
   "iip\0"
   "glTexGenxvOES\0"
   "\0"
   /* _mesa_function_pool[36108]: GetTextureLevelParameterfv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameterfv\0"
   "\0"
   /* _mesa_function_pool[36143]: VertexAttrib4Nubv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nubv\0"
   "glVertexAttrib4NubvARB\0"
   "\0"
   /* _mesa_function_pool[36190]: GetFogFuncSGIS (dynamic) */
   "p\0"
   "glGetFogFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36210]: UniformMatrix4x2dv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[36237]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "glVertexAttribPointerARB\0"
   "\0"
   /* _mesa_function_pool[36292]: IndexMask (offset 212) */
   "i\0"
   "glIndexMask\0"
   "\0"
   /* _mesa_function_pool[36307]: SharpenTexFuncSGIS (dynamic) */
   "iip\0"
   "glSharpenTexFuncSGIS\0"
   "\0"
   /* _mesa_function_pool[36333]: VertexAttribIFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribIFormat\0"
   "\0"
   /* _mesa_function_pool[36361]: CombinerOutputNV (dynamic) */
   "iiiiiiiiii\0"
   "glCombinerOutputNV\0"
   "\0"
   /* _mesa_function_pool[36392]: DrawArraysInstancedBaseInstance (will be remapped) */
   "iiiii\0"
   "glDrawArraysInstancedBaseInstance\0"
   "\0"
   /* _mesa_function_pool[36433]: CompressedTextureSubImage3D (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[36476]: PopAttrib (offset 218) */
   "\0"
   "glPopAttrib\0"
   "\0"
   /* _mesa_function_pool[36490]: SamplePatternSGIS (will be remapped) */
   "i\0"
   "glSamplePatternSGIS\0"
   "glSamplePatternEXT\0"
   "\0"
   /* _mesa_function_pool[36532]: Uniform3ui (will be remapped) */
   "iiii\0"
   "glUniform3uiEXT\0"
   "glUniform3ui\0"
   "\0"
   /* _mesa_function_pool[36567]: DeletePerfMonitorsAMD (will be remapped) */
   "ip\0"
   "glDeletePerfMonitorsAMD\0"
   "\0"
   /* _mesa_function_pool[36595]: Color4dv (offset 28) */
   "p\0"
   "glColor4dv\0"
   "\0"
   /* _mesa_function_pool[36609]: AreProgramsResidentNV (will be remapped) */
   "ipp\0"
   "glAreProgramsResidentNV\0"
   "\0"
   /* _mesa_function_pool[36638]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "glDisableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[36698]: ProgramUniformMatrix3x2fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3x2fv\0"
   "glProgramUniformMatrix3x2fvEXT\0"
   "\0"
   /* _mesa_function_pool[36764]: GetDoublei_v (will be remapped) */
   "iip\0"
   "glGetDoublei_v\0"
   "\0"
   /* _mesa_function_pool[36784]: IsTransformFeedback (will be remapped) */
   "i\0"
   "glIsTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[36809]: ClipPlanex (will be remapped) */
   "ip\0"
   "glClipPlanexOES\0"
   "glClipPlanex\0"
   "\0"
   /* _mesa_function_pool[36842]: ReplacementCodeuiColor3fVertex3fSUN (dynamic) */
   "iffffff\0"
   "glReplacementCodeuiColor3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[36889]: GetLightfv (offset 264) */
   "iip\0"
   "glGetLightfv\0"
   "\0"
   /* _mesa_function_pool[36907]: ClipPlanef (will be remapped) */
   "ip\0"
   "glClipPlanefOES\0"
   "glClipPlanef\0"
   "\0"
   /* _mesa_function_pool[36940]: ProgramUniform1ui (will be remapped) */
   "iii\0"
   "glProgramUniform1ui\0"
   "glProgramUniform1uiEXT\0"
   "\0"
   /* _mesa_function_pool[36988]: SecondaryColorPointer (will be remapped) */
   "iiip\0"
   "glSecondaryColorPointer\0"
   "glSecondaryColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[37045]: Tangent3svEXT (dynamic) */
   "p\0"
   "glTangent3svEXT\0"
   "\0"
   /* _mesa_function_pool[37064]: Tangent3iEXT (dynamic) */
   "iii\0"
   "glTangent3iEXT\0"
   "\0"
   /* _mesa_function_pool[37084]: LineStipple (offset 167) */
   "ii\0"
   "glLineStipple\0"
   "\0"
   /* _mesa_function_pool[37102]: FragmentLightfSGIX (dynamic) */
   "iif\0"
   "glFragmentLightfSGIX\0"
   "\0"
   /* _mesa_function_pool[37128]: BeginFragmentShaderATI (will be remapped) */
   "\0"
   "glBeginFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[37155]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "glGenRenderbuffersEXT\0"
   "glGenRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[37222]: GetMinmaxParameterfv (offset 365) */
   "iip\0"
   "glGetMinmaxParameterfv\0"
   "glGetMinmaxParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[37276]: IsEnabledi (will be remapped) */
   "ii\0"
   "glIsEnabledIndexedEXT\0"
   "glIsEnabledi\0"
   "\0"
   /* _mesa_function_pool[37315]: FragmentMaterialivSGIX (dynamic) */
   "iip\0"
   "glFragmentMaterialivSGIX\0"
   "\0"
   /* _mesa_function_pool[37345]: WaitSync (will be remapped) */
   "iii\0"
   "glWaitSync\0"
   "\0"
   /* _mesa_function_pool[37361]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "glGetVertexAttribPointervARB\0"
   "glGetVertexAttribPointervNV\0"
   "\0"
   /* _mesa_function_pool[37449]: CreatePerfQueryINTEL (will be remapped) */
   "ip\0"
   "glCreatePerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[37476]: NewList (dynamic) */
   "ii\0"
   "glNewList\0"
   "\0"
   /* _mesa_function_pool[37490]: TexBuffer (will be remapped) */
   "iii\0"
   "glTexBufferARB\0"
   "glTexBuffer\0"
   "\0"
   /* _mesa_function_pool[37522]: TexCoord4sv (offset 125) */
   "p\0"
   "glTexCoord4sv\0"
   "\0"
   /* _mesa_function_pool[37539]: TexCoord1f (offset 96) */
   "f\0"
   "glTexCoord1f\0"
   "\0"
   /* _mesa_function_pool[37555]: TexCoord1d (offset 94) */
   "d\0"
   "glTexCoord1d\0"
   "\0"
   /* _mesa_function_pool[37571]: TexCoord1i (offset 98) */
   "i\0"
   "glTexCoord1i\0"
   "\0"
   /* _mesa_function_pool[37587]: GetnUniformfvARB (will be remapped) */
   "iiip\0"
   "glGetnUniformfvARB\0"
   "\0"
   /* _mesa_function_pool[37612]: TexCoord1s (offset 100) */
   "i\0"
   "glTexCoord1s\0"
   "\0"
   /* _mesa_function_pool[37628]: GlobalAlphaFactoriSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactoriSUN\0"
   "\0"
   /* _mesa_function_pool[37655]: Uniform1ui (will be remapped) */
   "ii\0"
   "glUniform1uiEXT\0"
   "glUniform1ui\0"
   "\0"
   /* _mesa_function_pool[37688]: TexStorage1D (will be remapped) */
   "iiii\0"
   "glTexStorage1D\0"
   "\0"
   /* _mesa_function_pool[37709]: BlitFramebuffer (will be remapped) */
   "iiiiiiiiii\0"
   "glBlitFramebuffer\0"
   "glBlitFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[37760]: TextureParameterf (will be remapped) */
   "iif\0"
   "glTextureParameterf\0"
   "\0"
   /* _mesa_function_pool[37785]: FramebufferTexture1D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture1D\0"
   "glFramebufferTexture1DEXT\0"
   "\0"
   /* _mesa_function_pool[37841]: TextureParameteri (will be remapped) */
   "iii\0"
   "glTextureParameteri\0"
   "\0"
   /* _mesa_function_pool[37866]: GetMapiv (offset 268) */
   "iip\0"
   "glGetMapiv\0"
   "\0"
   /* _mesa_function_pool[37882]: TexCoordP4ui (will be remapped) */
   "ii\0"
   "glTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[37901]: VertexAttrib1sv (will be remapped) */
   "ip\0"
   "glVertexAttrib1sv\0"
   "glVertexAttrib1svARB\0"
   "\0"
   /* _mesa_function_pool[37944]: WindowPos4dMESA (will be remapped) */
   "dddd\0"
   "glWindowPos4dMESA\0"
   "\0"
   /* _mesa_function_pool[37968]: Vertex3dv (offset 135) */
   "p\0"
   "glVertex3dv\0"
   "\0"
   /* _mesa_function_pool[37983]: CreateShaderProgramEXT (will be remapped) */
   "ip\0"
   "glCreateShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[38012]: VertexAttribL2d (will be remapped) */
   "idd\0"
   "glVertexAttribL2d\0"
   "\0"
   /* _mesa_function_pool[38035]: GetnMapivARB (will be remapped) */
   "iiip\0"
   "glGetnMapivARB\0"
   "\0"
   /* _mesa_function_pool[38056]: MapParameterfvNV (dynamic) */
   "iip\0"
   "glMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[38080]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "glGetVertexAttribfvARB\0"
   "\0"
   /* _mesa_function_pool[38128]: MultiTexCoordP4uiv (will be remapped) */
   "iip\0"
   "glMultiTexCoordP4uiv\0"
   "\0"
   /* _mesa_function_pool[38154]: TexGeniv (offset 193) */
   "iip\0"
   "glTexGeniv\0"
   "glTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[38184]: WeightubvARB (dynamic) */
   "ip\0"
   "glWeightubvARB\0"
   "\0"
   /* _mesa_function_pool[38203]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "glBlendColorEXT\0"
   "\0"
   /* _mesa_function_pool[38238]: Materiali (offset 171) */
   "iii\0"
   "glMateriali\0"
   "\0"
   /* _mesa_function_pool[38255]: VertexAttrib2dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib2dvNV\0"
   "\0"
   /* _mesa_function_pool[38279]: NamedFramebufferDrawBuffers (will be remapped) */
   "iip\0"
   "glNamedFramebufferDrawBuffers\0"
   "\0"
   /* _mesa_function_pool[38314]: ResetHistogram (offset 369) */
   "i\0"
   "glResetHistogram\0"
   "glResetHistogramEXT\0"
   "\0"
   /* _mesa_function_pool[38354]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "glCompressedTexSubImage2DARB\0"
   "\0"
   /* _mesa_function_pool[38420]: TexCoord2sv (offset 109) */
   "p\0"
   "glTexCoord2sv\0"
   "\0"
   /* _mesa_function_pool[38437]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[38463]: MultiTexCoord3sv (offset 399) */
   "ip\0"
   "glMultiTexCoord3sv\0"
   "glMultiTexCoord3svARB\0"
   "\0"
   /* _mesa_function_pool[38508]: GetMapParameterfvNV (dynamic) */
   "iip\0"
   "glGetMapParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[38535]: TexCoord3iv (offset 115) */
   "p\0"
   "glTexCoord3iv\0"
   "\0"
   /* _mesa_function_pool[38552]: MultiTexCoord4sv (offset 407) */
   "ip\0"
   "glMultiTexCoord4sv\0"
   "glMultiTexCoord4svARB\0"
   "\0"
   /* _mesa_function_pool[38597]: VertexBindingDivisor (will be remapped) */
   "ii\0"
   "glVertexBindingDivisor\0"
   "\0"
   /* _mesa_function_pool[38624]: GetPerfMonitorCounterInfoAMD (will be remapped) */
   "iiip\0"
   "glGetPerfMonitorCounterInfoAMD\0"
   "\0"
   /* _mesa_function_pool[38661]: UniformBlockBinding (will be remapped) */
   "iii\0"
   "glUniformBlockBinding\0"
   "\0"
   /* _mesa_function_pool[38688]: FenceSync (will be remapped) */
   "ii\0"
   "glFenceSync\0"
   "\0"
   /* _mesa_function_pool[38704]: CompressedTextureSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTextureSubImage2D\0"
   "\0"
   /* _mesa_function_pool[38745]: VertexAttrib4Nusv (will be remapped) */
   "ip\0"
   "glVertexAttrib4Nusv\0"
   "glVertexAttrib4NusvARB\0"
   "\0"
   /* _mesa_function_pool[38792]: SetFragmentShaderConstantATI (will be remapped) */
   "ip\0"
   "glSetFragmentShaderConstantATI\0"
   "\0"
   /* _mesa_function_pool[38827]: VertexP2ui (will be remapped) */
   "ii\0"
   "glVertexP2ui\0"
   "\0"
   /* _mesa_function_pool[38844]: ProgramUniform2fv (will be remapped) */
   "iiip\0"
   "glProgramUniform2fv\0"
   "glProgramUniform2fvEXT\0"
   "\0"
   /* _mesa_function_pool[38893]: GetTextureLevelParameteriv (will be remapped) */
   "iiip\0"
   "glGetTextureLevelParameteriv\0"
   "\0"
   /* _mesa_function_pool[38928]: GetTexEnvfv (offset 276) */
   "iip\0"
   "glGetTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[38947]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "glBindAttribLocationARB\0"
   "\0"
   /* _mesa_function_pool[38997]: TextureStorage2DEXT (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DEXT\0"
   "\0"
   /* _mesa_function_pool[39027]: TextureParameterIiv (will be remapped) */
   "iip\0"
   "glTextureParameterIiv\0"
   "\0"
   /* _mesa_function_pool[39054]: FragmentLightiSGIX (dynamic) */
   "iii\0"
   "glFragmentLightiSGIX\0"
   "\0"
   /* _mesa_function_pool[39080]: DrawTransformFeedbackInstanced (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackInstanced\0"
   "\0"
   /* _mesa_function_pool[39118]: CopyTextureSubImage1D (will be remapped) */
   "iiiiii\0"
   "glCopyTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[39150]: PollAsyncSGIX (dynamic) */
   "p\0"
   "glPollAsyncSGIX\0"
   "\0"
   /* _mesa_function_pool[39169]: ResumeTransformFeedback (will be remapped) */
   "\0"
   "glResumeTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[39197]: GetProgramNamedParameterdvNV (will be remapped) */
   "iipp\0"
   "glGetProgramNamedParameterdvNV\0"
   "\0"
   /* _mesa_function_pool[39234]: VertexAttribI1iv (will be remapped) */
   "ip\0"
   "glVertexAttribI1ivEXT\0"
   "glVertexAttribI1iv\0"
   "\0"
   /* _mesa_function_pool[39279]: Vertex2dv (offset 127) */
   "p\0"
   "glVertex2dv\0"
   "\0"
   /* _mesa_function_pool[39294]: VertexAttribI2uivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2uivEXT\0"
   "glVertexAttribI2uiv\0"
   "\0"
   /* _mesa_function_pool[39341]: SampleMaski (will be remapped) */
   "ii\0"
   "glSampleMaski\0"
   "\0"
   /* _mesa_function_pool[39359]: GetFloati_v (will be remapped) */
   "iip\0"
   "glGetFloati_v\0"
   "\0"
   /* _mesa_function_pool[39378]: MultiTexCoord2iv (offset 389) */
   "ip\0"
   "glMultiTexCoord2iv\0"
   "glMultiTexCoord2ivARB\0"
   "\0"
   /* _mesa_function_pool[39423]: DrawPixels (offset 257) */
   "iiiip\0"
   "glDrawPixels\0"
   "\0"
   /* _mesa_function_pool[39443]: ReplacementCodeuiTexCoord2fNormal3fVertex3fSUN (dynamic) */
   "iffffffff\0"
   "glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[39503]: SecondaryColor3iv (will be remapped) */
   "p\0"
   "glSecondaryColor3iv\0"
   "glSecondaryColor3ivEXT\0"
   "\0"
   /* _mesa_function_pool[39549]: DrawTransformFeedback (will be remapped) */
   "ii\0"
   "glDrawTransformFeedback\0"
   "\0"
   /* _mesa_function_pool[39577]: VertexAttribs3fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs3fvNV\0"
   "\0"
   /* _mesa_function_pool[39603]: GenLists (offset 5) */
   "i\0"
   "glGenLists\0"
   "\0"
   /* _mesa_function_pool[39617]: MapGrid2d (offset 226) */
   "iddidd\0"
   "glMapGrid2d\0"
   "\0"
   /* _mesa_function_pool[39637]: MapGrid2f (offset 227) */
   "iffiff\0"
   "glMapGrid2f\0"
   "\0"
   /* _mesa_function_pool[39657]: SampleMapATI (will be remapped) */
   "iii\0"
   "glSampleMapATI\0"
   "\0"
   /* _mesa_function_pool[39677]: TexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[39705]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "glGetActiveAttribARB\0"
   "\0"
   /* _mesa_function_pool[39753]: TexCoord2fColor4ubVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor4ubVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[39791]: PixelMapfv (offset 251) */
   "iip\0"
   "glPixelMapfv\0"
   "\0"
   /* _mesa_function_pool[39809]: ClearBufferData (will be remapped) */
   "iiiip\0"
   "glClearBufferData\0"
   "\0"
   /* _mesa_function_pool[39834]: Color3usv (offset 24) */
   "p\0"
   "glColor3usv\0"
   "\0"
   /* _mesa_function_pool[39849]: CopyImageSubData (will be remapped) */
   "iiiiiiiiiiiiiii\0"
   "glCopyImageSubData\0"
   "\0"
   /* _mesa_function_pool[39885]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "glStencilOpSeparateATI\0"
   "\0"
   /* _mesa_function_pool[39934]: GenSamplers (will be remapped) */
   "ip\0"
   "glGenSamplers\0"
   "\0"
   /* _mesa_function_pool[39952]: ClipControl (will be remapped) */
   "ii\0"
   "glClipControl\0"
   "\0"
   /* _mesa_function_pool[39970]: DrawTexfOES (will be remapped) */
   "fffff\0"
   "glDrawTexfOES\0"
   "\0"
   /* _mesa_function_pool[39991]: AttachObjectARB (will be remapped) */
   "ii\0"
   "glAttachObjectARB\0"
   "\0"
   /* _mesa_function_pool[40013]: GetFragmentLightivSGIX (dynamic) */
   "iip\0"
   "glGetFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[40043]: Accum (offset 213) */
   "if\0"
   "glAccum\0"
   "\0"
   /* _mesa_function_pool[40055]: GetTexImage (offset 281) */
   "iiiip\0"
   "glGetTexImage\0"
   "\0"
   /* _mesa_function_pool[40076]: Color4x (will be remapped) */
   "iiii\0"
   "glColor4xOES\0"
   "glColor4x\0"
   "\0"
   /* _mesa_function_pool[40105]: ConvolutionParameteriv (offset 353) */
   "iip\0"
   "glConvolutionParameteriv\0"
   "glConvolutionParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[40163]: Color4s (offset 33) */
   "iiii\0"
   "glColor4s\0"
   "\0"
   /* _mesa_function_pool[40179]: CullParameterdvEXT (dynamic) */
   "ip\0"
   "glCullParameterdvEXT\0"
   "\0"
   /* _mesa_function_pool[40204]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "glEnableVertexAttribArrayARB\0"
   "\0"
   /* _mesa_function_pool[40262]: Color4i (offset 31) */
   "iiii\0"
   "glColor4i\0"
   "\0"
   /* _mesa_function_pool[40278]: Color4f (offset 29) */
   "ffff\0"
   "glColor4f\0"
   "\0"
   /* _mesa_function_pool[40294]: Color4d (offset 27) */
   "dddd\0"
   "glColor4d\0"
   "\0"
   /* _mesa_function_pool[40310]: Color4b (offset 25) */
   "iiii\0"
   "glColor4b\0"
   "\0"
   /* _mesa_function_pool[40326]: LoadProgramNV (will be remapped) */
   "iiip\0"
   "glLoadProgramNV\0"
   "\0"
   /* _mesa_function_pool[40348]: GetAttachedObjectsARB (will be remapped) */
   "iipp\0"
   "glGetAttachedObjectsARB\0"
   "\0"
   /* _mesa_function_pool[40378]: EvalCoord1fv (offset 231) */
   "p\0"
   "glEvalCoord1fv\0"
   "\0"
   /* _mesa_function_pool[40396]: VertexAttribLFormat (will be remapped) */
   "iiii\0"
   "glVertexAttribLFormat\0"
   "\0"
   /* _mesa_function_pool[40424]: VertexAttribL3d (will be remapped) */
   "iddd\0"
   "glVertexAttribL3d\0"
   "\0"
   /* _mesa_function_pool[40448]: ClearNamedFramebufferuiv (will be remapped) */
   "iiip\0"
   "glClearNamedFramebufferuiv\0"
   "\0"
   /* _mesa_function_pool[40481]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[40509]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "glShaderSourceARB\0"
   "\0"
   /* _mesa_function_pool[40548]: Normal3fv (offset 57) */
   "p\0"
   "glNormal3fv\0"
   "\0"
   /* _mesa_function_pool[40563]: ImageTransformParameterfvHP (dynamic) */
   "iip\0"
   "glImageTransformParameterfvHP\0"
   "\0"
   /* _mesa_function_pool[40598]: NormalP3ui (will be remapped) */
   "ii\0"
   "glNormalP3ui\0"
   "\0"
   /* _mesa_function_pool[40615]: CreateSamplers (will be remapped) */
   "ip\0"
   "glCreateSamplers\0"
   "\0"
   /* _mesa_function_pool[40636]: MultiTexCoord3fvARB (offset 395) */
   "ip\0"
   "glMultiTexCoord3fv\0"
   "glMultiTexCoord3fvARB\0"
   "\0"
   /* _mesa_function_pool[40681]: GetProgramParameterfvNV (will be remapped) */
   "iiip\0"
   "glGetProgramParameterfvNV\0"
   "\0"
   /* _mesa_function_pool[40713]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "glBufferDataARB\0"
   "\0"
   /* _mesa_function_pool[40748]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "glTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[40794]: FragmentLightivSGIX (dynamic) */
   "iip\0"
   "glFragmentLightivSGIX\0"
   "\0"
   /* _mesa_function_pool[40821]: GetTexParameterPointervAPPLE (dynamic) */
   "iip\0"
   "glGetTexParameterPointervAPPLE\0"
   "\0"
   /* _mesa_function_pool[40857]: TexGenfv (offset 191) */
   "iip\0"
   "glTexGenfv\0"
   "glTexGenfvOES\0"
   "\0"
   /* _mesa_function_pool[40887]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "glGetVertexAttribivARB\0"
   "\0"
   /* _mesa_function_pool[40935]: TexCoordP2uiv (will be remapped) */
   "ip\0"
   "glTexCoordP2uiv\0"
   "\0"
   /* _mesa_function_pool[40955]: ReplacementCodeuiColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glReplacementCodeuiColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[40999]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "glUniform3fvARB\0"
   "\0"
   /* _mesa_function_pool[41033]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "glBlendEquationEXT\0"
   "glBlendEquationOES\0"
   "\0"
   /* _mesa_function_pool[41090]: VertexAttrib3dNV (will be remapped) */
   "iddd\0"
   "glVertexAttrib3dNV\0"
   "\0"
   /* _mesa_function_pool[41115]: ReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN (dynamic) */
   "ppppp\0"
   "glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[41179]: IndexFuncEXT (dynamic) */
   "if\0"
   "glIndexFuncEXT\0"
   "\0"
   /* _mesa_function_pool[41198]: UseShaderProgramEXT (will be remapped) */
   "ii\0"
   "glUseShaderProgramEXT\0"
   "\0"
   /* _mesa_function_pool[41224]: PushName (offset 201) */
   "i\0"
   "glPushName\0"
   "\0"
   /* _mesa_function_pool[41238]: GenFencesNV (dynamic) */
   "ip\0"
   "glGenFencesNV\0"
   "\0"
   /* _mesa_function_pool[41256]: CullParameterfvEXT (dynamic) */
   "ip\0"
   "glCullParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[41281]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "glDeleteRenderbuffersEXT\0"
   "glDeleteRenderbuffersOES\0"
   "\0"
   /* _mesa_function_pool[41357]: VertexAttrib1dv (will be remapped) */
   "ip\0"
   "glVertexAttrib1dv\0"
   "glVertexAttrib1dvARB\0"
   "\0"
   /* _mesa_function_pool[41400]: ImageTransformParameteriHP (dynamic) */
   "iii\0"
   "glImageTransformParameteriHP\0"
   "\0"
   /* _mesa_function_pool[41434]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[41448]: Rotated (offset 299) */
   "dddd\0"
   "glRotated\0"
   "\0"
   /* _mesa_function_pool[41464]: Color4iv (offset 32) */
   "p\0"
   "glColor4iv\0"
   "\0"
   /* _mesa_function_pool[41478]: PointParameterxv (will be remapped) */
   "ip\0"
   "glPointParameterxvOES\0"
   "glPointParameterxv\0"
   "\0"
   /* _mesa_function_pool[41523]: Rotatex (will be remapped) */
   "iiii\0"
   "glRotatexOES\0"
   "glRotatex\0"
   "\0"
   /* _mesa_function_pool[41552]: FramebufferTextureLayer (will be remapped) */
   "iiiii\0"
   "glFramebufferTextureLayer\0"
   "glFramebufferTextureLayerARB\0"
   "glFramebufferTextureLayerEXT\0"
   "\0"
   /* _mesa_function_pool[41643]: TexEnvfv (offset 185) */
   "iip\0"
   "glTexEnvfv\0"
   "\0"
   /* _mesa_function_pool[41659]: ProgramUniformMatrix3fv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3fv\0"
   "glProgramUniformMatrix3fvEXT\0"
   "\0"
   /* _mesa_function_pool[41721]: LoadMatrixf (offset 291) */
   "p\0"
   "glLoadMatrixf\0"
   "\0"
   /* _mesa_function_pool[41738]: GetProgramLocalParameterfvARB (will be remapped) */
   "iip\0"
   "glGetProgramLocalParameterfvARB\0"
   "\0"
   /* _mesa_function_pool[41775]: MultiDrawArraysIndirect (will be remapped) */
   "ipii\0"
   "glMultiDrawArraysIndirect\0"
   "\0"
   /* _mesa_function_pool[41807]: DrawRangeElementsBaseVertex (will be remapped) */
   "iiiiipi\0"
   "glDrawRangeElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[41846]: ProgramUniformMatrix4dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4dv\0"
   "\0"
   /* _mesa_function_pool[41879]: MatrixIndexuivARB (dynamic) */
   "ip\0"
   "glMatrixIndexuivARB\0"
   "\0"
   /* _mesa_function_pool[41903]: Tangent3sEXT (dynamic) */
   "iii\0"
   "glTangent3sEXT\0"
   "\0"
   /* _mesa_function_pool[41923]: SecondaryColor3bv (will be remapped) */
   "p\0"
   "glSecondaryColor3bv\0"
   "glSecondaryColor3bvEXT\0"
   "\0"
   /* _mesa_function_pool[41969]: GlobalAlphaFactorusSUN (dynamic) */
   "i\0"
   "glGlobalAlphaFactorusSUN\0"
   "\0"
   /* _mesa_function_pool[41997]: GetCombinerOutputParameterivNV (dynamic) */
   "iiip\0"
   "glGetCombinerOutputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[42036]: DrawTexxvOES (will be remapped) */
   "p\0"
   "glDrawTexxvOES\0"
   "\0"
   /* _mesa_function_pool[42054]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[42076]: Color4ubv (offset 36) */
   "p\0"
   "glColor4ubv\0"
   "\0"
   /* _mesa_function_pool[42091]: TexCoord2fv (offset 105) */
   "p\0"
   "glTexCoord2fv\0"
   "\0"
   /* _mesa_function_pool[42108]: FogCoorddv (will be remapped) */
   "p\0"
   "glFogCoorddv\0"
   "glFogCoorddvEXT\0"
   "\0"
   /* _mesa_function_pool[42140]: VDPAUUnregisterSurfaceNV (will be remapped) */
   "i\0"
   "glVDPAUUnregisterSurfaceNV\0"
   "\0"
   /* _mesa_function_pool[42170]: ColorP3ui (will be remapped) */
   "ii\0"
   "glColorP3ui\0"
   "\0"
   /* _mesa_function_pool[42186]: ClearBufferuiv (will be remapped) */
   "iip\0"
   "glClearBufferuiv\0"
   "\0"
   /* _mesa_function_pool[42208]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[42241]: ProgramNamedParameter4dvNV (will be remapped) */
   "iipp\0"
   "glProgramNamedParameter4dvNV\0"
   "\0"
   /* _mesa_function_pool[42276]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[42286]: VertexAttribI4iEXT (will be remapped) */
   "iiiii\0"
   "glVertexAttribI4iEXT\0"
   "glVertexAttribI4i\0"
   "\0"
   /* _mesa_function_pool[42332]: FogCoordd (will be remapped) */
   "d\0"
   "glFogCoordd\0"
   "glFogCoorddEXT\0"
   "\0"
   /* _mesa_function_pool[42362]: BindFramebufferEXT (will be remapped) */
   "ii\0"
   "glBindFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[42387]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "glUniform3ivARB\0"
   "\0"
   /* _mesa_function_pool[42421]: TexStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTexStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[42455]: UnlockArraysEXT (will be remapped) */
   "\0"
   "glUnlockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[42475]: VertexAttrib1svNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1svNV\0"
   "\0"
   /* _mesa_function_pool[42499]: VertexAttrib4iv (will be remapped) */
   "ip\0"
   "glVertexAttrib4iv\0"
   "glVertexAttrib4ivARB\0"
   "\0"
   /* _mesa_function_pool[42542]: CopyTexSubImage3D (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3D\0"
   "glCopyTexSubImage3DEXT\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[42619]: PolygonOffsetClampEXT (will be remapped) */
   "fff\0"
   "glPolygonOffsetClampEXT\0"
   "\0"
   /* _mesa_function_pool[42648]: GetInteger64v (will be remapped) */
   "ip\0"
   "glGetInteger64v\0"
   "\0"
   /* _mesa_function_pool[42668]: DetachObjectARB (will be remapped) */
   "ii\0"
   "glDetachObjectARB\0"
   "\0"
   /* _mesa_function_pool[42690]: Indexiv (offset 49) */
   "p\0"
   "glIndexiv\0"
   "\0"
   /* _mesa_function_pool[42703]: TexEnvi (offset 186) */
   "iii\0"
   "glTexEnvi\0"
   "\0"
   /* _mesa_function_pool[42718]: TexEnvf (offset 184) */
   "iif\0"
   "glTexEnvf\0"
   "\0"
   /* _mesa_function_pool[42733]: TexEnvx (will be remapped) */
   "iii\0"
   "glTexEnvxOES\0"
   "glTexEnvx\0"
   "\0"
   /* _mesa_function_pool[42761]: LoadIdentityDeformationMapSGIX (dynamic) */
   "i\0"
   "glLoadIdentityDeformationMapSGIX\0"
   "\0"
   /* _mesa_function_pool[42797]: StopInstrumentsSGIX (dynamic) */
   "i\0"
   "glStopInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[42822]: TexCoord4fColor4fNormal3fVertex4fSUN (dynamic) */
   "fffffffffffffff\0"
   "glTexCoord4fColor4fNormal3fVertex4fSUN\0"
   "\0"
   /* _mesa_function_pool[42878]: InvalidateBufferSubData (will be remapped) */
   "iii\0"
   "glInvalidateBufferSubData\0"
   "\0"
   /* _mesa_function_pool[42909]: UniformMatrix4x2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4x2fv\0"
   "\0"
   /* _mesa_function_pool[42936]: ClearTexImage (will be remapped) */
   "iiiip\0"
   "glClearTexImage\0"
   "\0"
   /* _mesa_function_pool[42959]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[42979]: NormalPointervINTEL (dynamic) */
   "ip\0"
   "glNormalPointervINTEL\0"
   "\0"
   /* _mesa_function_pool[43005]: SamplerParameterfv (will be remapped) */
   "iip\0"
   "glSamplerParameterfv\0"
   "\0"
   /* _mesa_function_pool[43031]: CompressedTextureSubImage1D (will be remapped) */
   "iiiiiip\0"
   "glCompressedTextureSubImage1D\0"
   "\0"
   /* _mesa_function_pool[43070]: ProgramUniformMatrix4x2dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix4x2dv\0"
   "\0"
   /* _mesa_function_pool[43105]: ProgramEnvParameter4fARB (will be remapped) */
   "iiffff\0"
   "glProgramEnvParameter4fARB\0"
   "glProgramParameter4fNV\0"
   "\0"
   /* _mesa_function_pool[43163]: ClearDepth (offset 208) */
   "d\0"
   "glClearDepth\0"
   "\0"
   /* _mesa_function_pool[43179]: VertexAttrib3dvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3dvNV\0"
   "\0"
   /* _mesa_function_pool[43203]: Color4fv (offset 30) */
   "p\0"
   "glColor4fv\0"
   "\0"
   /* _mesa_function_pool[43217]: GetnMinmaxARB (will be remapped) */
   "iiiiip\0"
   "glGetnMinmaxARB\0"
   "\0"
   /* _mesa_function_pool[43241]: ColorPointer (offset 308) */
   "iiip\0"
   "glColorPointer\0"
   "\0"
   /* _mesa_function_pool[43262]: GetPointerv (offset 329) */
   "ip\0"
   "glGetPointerv\0"
   "glGetPointervEXT\0"
   "\0"
   /* _mesa_function_pool[43297]: Lightiv (offset 162) */
   "iip\0"
   "glLightiv\0"
   "\0"
   /* _mesa_function_pool[43312]: GetTexParameterIuiv (will be remapped) */
   "iip\0"
   "glGetTexParameterIuivEXT\0"
   "glGetTexParameterIuiv\0"
   "\0"
   /* _mesa_function_pool[43364]: TransformFeedbackVaryings (will be remapped) */
   "iipi\0"
   "glTransformFeedbackVaryings\0"
   "glTransformFeedbackVaryingsEXT\0"
   "\0"
   /* _mesa_function_pool[43429]: VertexAttrib3sv (will be remapped) */
   "ip\0"
   "glVertexAttrib3sv\0"
   "glVertexAttrib3svARB\0"
   "\0"
   /* _mesa_function_pool[43472]: IsVertexArray (will be remapped) */
   "i\0"
   "glIsVertexArray\0"
   "glIsVertexArrayAPPLE\0"
   "glIsVertexArrayOES\0"
   "\0"
   /* _mesa_function_pool[43531]: PushClientAttrib (offset 335) */
   "i\0"
   "glPushClientAttrib\0"
   "\0"
   /* _mesa_function_pool[43553]: ProgramUniform4ui (will be remapped) */
   "iiiiii\0"
   "glProgramUniform4ui\0"
   "glProgramUniform4uiEXT\0"
   "\0"
   /* _mesa_function_pool[43604]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "glUniform1fARB\0"
   "\0"
   /* _mesa_function_pool[43635]: Uniform1d (will be remapped) */
   "id\0"
   "glUniform1d\0"
   "\0"
   /* _mesa_function_pool[43651]: FragmentMaterialfSGIX (dynamic) */
   "iif\0"
   "glFragmentMaterialfSGIX\0"
   "\0"
   /* _mesa_function_pool[43680]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "glUniform1iARB\0"
   "\0"
   /* _mesa_function_pool[43711]: GetPolygonStipple (offset 274) */
   "p\0"
   "glGetPolygonStipple\0"
   "\0"
   /* _mesa_function_pool[43734]: Tangent3dvEXT (dynamic) */
   "p\0"
   "glTangent3dvEXT\0"
   "\0"
   /* _mesa_function_pool[43753]: BlitNamedFramebuffer (will be remapped) */
   "iiiiiiiiiiii\0"
   "glBlitNamedFramebuffer\0"
   "\0"
   /* _mesa_function_pool[43790]: PixelTexGenSGIX (dynamic) */
   "i\0"
   "glPixelTexGenSGIX\0"
   "\0"
   /* _mesa_function_pool[43811]: ReplacementCodeusvSUN (dynamic) */
   "p\0"
   "glReplacementCodeusvSUN\0"
   "\0"
   /* _mesa_function_pool[43838]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "glUseProgramObjectARB\0"
   "\0"
   /* _mesa_function_pool[43876]: StartInstrumentsSGIX (dynamic) */
   "\0"
   "glStartInstrumentsSGIX\0"
   "\0"
   /* _mesa_function_pool[43901]: FlushMappedBufferRangeAPPLE (will be remapped) */
   "iii\0"
   "glFlushMappedBufferRangeAPPLE\0"
   "\0"
   /* _mesa_function_pool[43936]: GetFragDataLocation (will be remapped) */
   "ip\0"
   "glGetFragDataLocationEXT\0"
   "glGetFragDataLocation\0"
   "\0"
   /* _mesa_function_pool[43987]: PixelMapuiv (offset 252) */
   "iip\0"
   "glPixelMapuiv\0"
   "\0"
   /* _mesa_function_pool[44006]: ClearNamedBufferSubData (will be remapped) */
   "iiiiiip\0"
   "glClearNamedBufferSubData\0"
   "\0"
   /* _mesa_function_pool[44041]: VertexWeightfvEXT (dynamic) */
   "p\0"
   "glVertexWeightfvEXT\0"
   "\0"
   /* _mesa_function_pool[44064]: GetFenceivNV (dynamic) */
   "iip\0"
   "glGetFenceivNV\0"
   "\0"
   /* _mesa_function_pool[44084]: CurrentPaletteMatrixARB (dynamic) */
   "i\0"
   "glCurrentPaletteMatrixARB\0"
   "glCurrentPaletteMatrixOES\0"
   "\0"
   /* _mesa_function_pool[44139]: GenVertexArrays (will be remapped) */
   "ip\0"
   "glGenVertexArrays\0"
   "glGenVertexArraysOES\0"
   "\0"
   /* _mesa_function_pool[44182]: TexCoord2fColor4ubVertex3fSUN (dynamic) */
   "ffiiiifff\0"
   "glTexCoord2fColor4ubVertex3fSUN\0"
   "\0"
   /* _mesa_function_pool[44225]: TagSampleBufferSGIX (dynamic) */
   "\0"
   "glTagSampleBufferSGIX\0"
   "\0"
   /* _mesa_function_pool[44249]: Color3s (offset 17) */
   "iii\0"
   "glColor3s\0"
   "\0"
   /* _mesa_function_pool[44264]: TextureStorage2DMultisample (will be remapped) */
   "iiiiii\0"
   "glTextureStorage2DMultisample\0"
   "\0"
   /* _mesa_function_pool[44302]: TexCoordPointer (offset 320) */
   "iiip\0"
   "glTexCoordPointer\0"
   "\0"
   /* _mesa_function_pool[44326]: Color3i (offset 15) */
   "iii\0"
   "glColor3i\0"
   "\0"
   /* _mesa_function_pool[44341]: EvalCoord2d (offset 232) */
   "dd\0"
   "glEvalCoord2d\0"
   "\0"
   /* _mesa_function_pool[44359]: EvalCoord2f (offset 234) */
   "ff\0"
   "glEvalCoord2f\0"
   "\0"
   /* _mesa_function_pool[44377]: Color3b (offset 9) */
   "iii\0"
   "glColor3b\0"
   "\0"
   /* _mesa_function_pool[44392]: ExecuteProgramNV (will be remapped) */
   "iip\0"
   "glExecuteProgramNV\0"
   "\0"
   /* _mesa_function_pool[44416]: Color3f (offset 13) */
   "fff\0"
   "glColor3f\0"
   "\0"
   /* _mesa_function_pool[44431]: Color3d (offset 11) */
   "ddd\0"
   "glColor3d\0"
   "\0"
   /* _mesa_function_pool[44446]: GetVertexAttribdv (will be remapped) */
   "iip\0"
   "glGetVertexAttribdv\0"
   "glGetVertexAttribdvARB\0"
   "\0"
   /* _mesa_function_pool[44494]: GetBufferPointerv (will be remapped) */
   "iip\0"
   "glGetBufferPointerv\0"
   "glGetBufferPointervARB\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[44565]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "glGenFramebuffersEXT\0"
   "glGenFramebuffersOES\0"
   "\0"
   /* _mesa_function_pool[44629]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "glGenBuffersARB\0"
   "\0"
   /* _mesa_function_pool[44662]: ClearDepthx (will be remapped) */
   "i\0"
   "glClearDepthxOES\0"
   "glClearDepthx\0"
   "\0"
   /* _mesa_function_pool[44696]: EnableVertexArrayAttrib (will be remapped) */
   "ii\0"
   "glEnableVertexArrayAttrib\0"
   "\0"
   /* _mesa_function_pool[44726]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "glBlendEquationSeparateEXT\0"
   "glBlendEquationSeparateATI\0"
   "glBlendEquationSeparateOES\0"
   "\0"
   /* _mesa_function_pool[44835]: PixelTransformParameteriEXT (dynamic) */
   "iii\0"
   "glPixelTransformParameteriEXT\0"
   "\0"
   /* _mesa_function_pool[44870]: MultiTexCoordP4ui (will be remapped) */
   "iii\0"
   "glMultiTexCoordP4ui\0"
   "\0"
   /* _mesa_function_pool[44895]: VertexAttribs1fvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs1fvNV\0"
   "\0"
   /* _mesa_function_pool[44921]: VertexAttribIPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribIPointerEXT\0"
   "glVertexAttribIPointer\0"
   "\0"
   /* _mesa_function_pool[44977]: ProgramUniform4fv (will be remapped) */
   "iiip\0"
   "glProgramUniform4fv\0"
   "glProgramUniform4fvEXT\0"
   "\0"
   /* _mesa_function_pool[45026]: FrameZoomSGIX (dynamic) */
   "i\0"
   "glFrameZoomSGIX\0"
   "\0"
   /* _mesa_function_pool[45045]: RasterPos4sv (offset 85) */
   "p\0"
   "glRasterPos4sv\0"
   "\0"
   /* _mesa_function_pool[45063]: CopyTextureSubImage3D (will be remapped) */
   "iiiiiiiii\0"
   "glCopyTextureSubImage3D\0"
   "\0"
   /* _mesa_function_pool[45098]: SelectBuffer (offset 195) */
   "ip\0"
   "glSelectBuffer\0"
   "\0"
   /* _mesa_function_pool[45117]: GetSynciv (will be remapped) */
   "iiipp\0"
   "glGetSynciv\0"
   "\0"
   /* _mesa_function_pool[45136]: TextureView (will be remapped) */
   "iiiiiiii\0"
   "glTextureView\0"
   "\0"
   /* _mesa_function_pool[45160]: TexEnviv (offset 187) */
   "iip\0"
   "glTexEnviv\0"
   "\0"
   /* _mesa_function_pool[45176]: TexSubImage3D (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3D\0"
   "glTexSubImage3DEXT\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[45243]: Bitmap (offset 8) */
   "iiffffp\0"
   "glBitmap\0"
   "\0"
   /* _mesa_function_pool[45261]: VertexAttribDivisor (will be remapped) */
   "ii\0"
   "glVertexAttribDivisorARB\0"
   "glVertexAttribDivisor\0"
   "\0"
   /* _mesa_function_pool[45312]: DrawTransformFeedbackStream (will be remapped) */
   "iii\0"
   "glDrawTransformFeedbackStream\0"
   "\0"
   /* _mesa_function_pool[45347]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[45365]: EndPerfQueryINTEL (will be remapped) */
   "i\0"
   "glEndPerfQueryINTEL\0"
   "\0"
   /* _mesa_function_pool[45388]: FragmentLightfvSGIX (dynamic) */
   "iip\0"
   "glFragmentLightfvSGIX\0"
   "\0"
   /* _mesa_function_pool[45415]: TexCoord2fColor3fVertex3fvSUN (dynamic) */
   "ppp\0"
   "glTexCoord2fColor3fVertex3fvSUN\0"
   "\0"
   /* _mesa_function_pool[45452]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "glGetActiveUniformARB\0"
   "\0"
   /* _mesa_function_pool[45502]: AlphaFuncx (will be remapped) */
   "ii\0"
   "glAlphaFuncxOES\0"
   "glAlphaFuncx\0"
   "\0"
   /* _mesa_function_pool[45535]: VertexAttribI2ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI2ivEXT\0"
   "glVertexAttribI2iv\0"
   "\0"
   /* _mesa_function_pool[45580]: VertexBlendARB (dynamic) */
   "i\0"
   "glVertexBlendARB\0"
   "\0"
   /* _mesa_function_pool[45600]: Map1d (offset 220) */
   "iddiip\0"
   "glMap1d\0"
   "\0"
   /* _mesa_function_pool[45616]: Map1f (offset 221) */
   "iffiip\0"
   "glMap1f\0"
   "\0"
   /* _mesa_function_pool[45632]: AreTexturesResident (offset 322) */
   "ipp\0"
   "glAreTexturesResident\0"
   "glAreTexturesResidentEXT\0"
   "\0"
   /* _mesa_function_pool[45684]: VertexArrayVertexBuffer (will be remapped) */
   "iiiii\0"
   "glVertexArrayVertexBuffer\0"
   "\0"
   /* _mesa_function_pool[45717]: PixelTransferf (offset 247) */
   "if\0"
   "glPixelTransferf\0"
   "\0"
   /* _mesa_function_pool[45738]: PixelTransferi (offset 248) */
   "ii\0"
   "glPixelTransferi\0"
   "\0"
   /* _mesa_function_pool[45759]: GetProgramResourceiv (will be remapped) */
   "iiiipipp\0"
   "glGetProgramResourceiv\0"
   "\0"
   /* _mesa_function_pool[45792]: VertexAttrib3fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib3fvNV\0"
   "\0"
   /* _mesa_function_pool[45816]: GetFinalCombinerInputParameterivNV (dynamic) */
   "iip\0"
   "glGetFinalCombinerInputParameterivNV\0"
   "\0"
   /* _mesa_function_pool[45858]: SecondaryColorP3ui (will be remapped) */
   "ii\0"
   "glSecondaryColorP3ui\0"
   "\0"
   /* _mesa_function_pool[45883]: BindTextures (will be remapped) */
   "iip\0"
   "glBindTextures\0"
   "\0"
   /* _mesa_function_pool[45903]: GetMapParameterivNV (dynamic) */
   "iip\0"
   "glGetMapParameterivNV\0"
   "\0"
   /* _mesa_function_pool[45930]: VertexAttrib4fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib4fvNV\0"
   "\0"
   /* _mesa_function_pool[45954]: Rectiv (offset 91) */
   "pp\0"
   "glRectiv\0"
   "\0"
   /* _mesa_function_pool[45967]: MultiTexCoord1iv (offset 381) */
   "ip\0"
   "glMultiTexCoord1iv\0"
   "glMultiTexCoord1ivARB\0"
   "\0"
   /* _mesa_function_pool[46012]: PassTexCoordATI (will be remapped) */
   "iii\0"
   "glPassTexCoordATI\0"
   "\0"
   /* _mesa_function_pool[46035]: Tangent3dEXT (dynamic) */
   "ddd\0"
   "glTangent3dEXT\0"
   "\0"
   /* _mesa_function_pool[46055]: Vertex2fv (offset 129) */
   "p\0"
   "glVertex2fv\0"
   "\0"
   /* _mesa_function_pool[46070]: BindRenderbufferEXT (will be remapped) */
   "ii\0"
   "glBindRenderbufferEXT\0"
   "\0"
   /* _mesa_function_pool[46096]: Vertex3sv (offset 141) */
   "p\0"
   "glVertex3sv\0"
   "\0"
   /* _mesa_function_pool[46111]: EvalMesh1 (offset 236) */
   "iii\0"
   "glEvalMesh1\0"
   "\0"
   /* _mesa_function_pool[46128]: DiscardFramebufferEXT (will be remapped) */
   "iip\0"
   "glDiscardFramebufferEXT\0"
   "\0"
   /* _mesa_function_pool[46157]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "glUniform2fARB\0"
   "\0"
   /* _mesa_function_pool[46189]: Uniform2d (will be remapped) */
   "idd\0"
   "glUniform2d\0"
   "\0"
   /* _mesa_function_pool[46206]: ColorPointerEXT (will be remapped) */
   "iiiip\0"
   "glColorPointerEXT\0"
   "\0"
   /* _mesa_function_pool[46231]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[46246]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "glUniform2iARB\0"
   "\0"
   /* _mesa_function_pool[46278]: MultiDrawElementsBaseVertex (will be remapped) */
   "ipipip\0"
   "glMultiDrawElementsBaseVertex\0"
   "\0"
   /* _mesa_function_pool[46316]: Lightxv (will be remapped) */
   "iip\0"
   "glLightxvOES\0"
   "glLightxv\0"
   "\0"
   /* _mesa_function_pool[46344]: DepthRangeIndexed (will be remapped) */
   "idd\0"
   "glDepthRangeIndexed\0"
   "\0"
   /* _mesa_function_pool[46369]: GetConvolutionParameterfv (offset 357) */
   "iip\0"
   "glGetConvolutionParameterfv\0"
   "glGetConvolutionParameterfvEXT\0"
   "\0"
   /* _mesa_function_pool[46433]: GetTexBumpParameterfvATI (will be remapped) */
   "ip\0"
   "glGetTexBumpParameterfvATI\0"
   "\0"
   /* _mesa_function_pool[46464]: ProgramNamedParameter4dNV (will be remapped) */
   "iipdddd\0"
   "glProgramNamedParameter4dNV\0"
   "\0"
   /* _mesa_function_pool[46501]: GetMaterialfv (offset 269) */
   "iip\0"
   "glGetMaterialfv\0"
   "\0"
   /* _mesa_function_pool[46522]: TexImage3DMultisample (will be remapped) */
   "iiiiiii\0"
   "glTexImage3DMultisample\0"
   "\0"
   /* _mesa_function_pool[46555]: VertexAttrib1fvNV (will be remapped) */
   "ip\0"
   "glVertexAttrib1fvNV\0"
   "\0"
   /* _mesa_function_pool[46579]: GetUniformBlockIndex (will be remapped) */
   "ip\0"
   "glGetUniformBlockIndex\0"
   "\0"
   /* _mesa_function_pool[46606]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[46625]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "glCopyTexSubImage2DEXT\0"
   "\0"
   /* _mesa_function_pool[46678]: GetNamedFramebufferParameteriv (will be remapped) */
   "iip\0"
   "glGetNamedFramebufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[46716]: GetObjectParameterivARB (will be remapped) */
   "iip\0"
   "glGetObjectParameterivARB\0"
   "\0"
   /* _mesa_function_pool[46747]: Color3iv (offset 16) */
   "p\0"
   "glColor3iv\0"
   "\0"
   /* _mesa_function_pool[46761]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[46782]: ScissorArrayv (will be remapped) */
   "iip\0"
   "glScissorArrayv\0"
   "\0"
   /* _mesa_function_pool[46803]: GetInternalformativ (will be remapped) */
   "iiiip\0"
   "glGetInternalformativ\0"
   "\0"
   /* _mesa_function_pool[46832]: EvalPoint2 (offset 239) */
   "ii\0"
   "glEvalPoint2\0"
   "\0"
   /* _mesa_function_pool[46849]: EvalPoint1 (offset 237) */
   "i\0"
   "glEvalPoint1\0"
   "\0"
   /* _mesa_function_pool[46865]: VertexAttribLPointer (will be remapped) */
   "iiiip\0"
   "glVertexAttribLPointer\0"
   "\0"
   /* _mesa_function_pool[46895]: PopMatrix (offset 297) */
   "\0"
   "glPopMatrix\0"
   "\0"
   /* _mesa_function_pool[46909]: FinishFenceNV (dynamic) */
   "i\0"
   "glFinishFenceNV\0"
   "\0"
   /* _mesa_function_pool[46928]: Tangent3bvEXT (dynamic) */
   "p\0"
   "glTangent3bvEXT\0"
   "\0"
   /* _mesa_function_pool[46947]: NamedBufferData (will be remapped) */
   "iipi\0"
   "glNamedBufferData\0"
   "\0"
   /* _mesa_function_pool[46971]: GetTexGeniv (offset 280) */
   "iip\0"
   "glGetTexGeniv\0"
   "glGetTexGenivOES\0"
   "\0"
   /* _mesa_function_pool[47007]: GetFirstPerfQueryIdINTEL (will be remapped) */
   "p\0"
   "glGetFirstPerfQueryIdINTEL\0"
   "\0"
   /* _mesa_function_pool[47037]: ActiveProgramEXT (will be remapped) */
   "i\0"
   "glActiveProgramEXT\0"
   "\0"
   /* _mesa_function_pool[47059]: PixelTransformParameterivEXT (dynamic) */
   "iip\0"
   "glPixelTransformParameterivEXT\0"
   "\0"
   /* _mesa_function_pool[47095]: TexCoord4fVertex4fvSUN (dynamic) */
   "pp\0"
   "glTexCoord4fVertex4fvSUN\0"
   "\0"
   /* _mesa_function_pool[47124]: UnmapBuffer (will be remapped) */
   "i\0"
   "glUnmapBuffer\0"
   "glUnmapBufferARB\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[47175]: EvalCoord1d (offset 228) */
   "d\0"
   "glEvalCoord1d\0"
   "\0"
   /* _mesa_function_pool[47192]: VertexAttribL1d (will be remapped) */
   "id\0"
   "glVertexAttribL1d\0"
   "\0"
   /* _mesa_function_pool[47214]: EvalCoord1f (offset 230) */
   "f\0"
   "glEvalCoord1f\0"
   "\0"
   /* _mesa_function_pool[47231]: IndexMaterialEXT (dynamic) */
   "ii\0"
   "glIndexMaterialEXT\0"
   "\0"
   /* _mesa_function_pool[47254]: Materialf (offset 169) */
   "iif\0"
   "glMaterialf\0"
   "\0"
   /* _mesa_function_pool[47271]: VertexAttribs2dvNV (will be remapped) */
   "iip\0"
   "glVertexAttribs2dvNV\0"
   "\0"
   /* _mesa_function_pool[47297]: ProgramUniform1uiv (will be remapped) */
   "iiip\0"
   "glProgramUniform1uiv\0"
   "glProgramUniform1uivEXT\0"
   "\0"
   /* _mesa_function_pool[47348]: EvalCoord1dv (offset 229) */
   "p\0"
   "glEvalCoord1dv\0"
   "\0"
   /* _mesa_function_pool[47366]: Materialx (will be remapped) */
   "iii\0"
   "glMaterialxOES\0"
   "glMaterialx\0"
   "\0"
   /* _mesa_function_pool[47398]: GetQueryBufferObjectiv (will be remapped) */
   "iiii\0"
   "glGetQueryBufferObjectiv\0"
   "\0"
   /* _mesa_function_pool[47429]: GetLightiv (offset 265) */
   "iip\0"
   "glGetLightiv\0"
   "\0"
   /* _mesa_function_pool[47447]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "glBindBufferARB\0"
   "\0"
   /* _mesa_function_pool[47480]: ProgramUniform1i (will be remapped) */
   "iii\0"
   "glProgramUniform1i\0"
   "glProgramUniform1iEXT\0"
   "\0"
   /* _mesa_function_pool[47526]: ProgramUniform1f (will be remapped) */
   "iif\0"
   "glProgramUniform1f\0"
   "glProgramUniform1fEXT\0"
   "\0"
   /* _mesa_function_pool[47572]: ProgramUniform1d (will be remapped) */
   "iid\0"
   "glProgramUniform1d\0"
   "\0"
   /* _mesa_function_pool[47596]: WindowPos3iv (will be remapped) */
   "p\0"
   "glWindowPos3iv\0"
   "glWindowPos3ivARB\0"
   "glWindowPos3ivMESA\0"
   "\0"
   /* _mesa_function_pool[47651]: CopyConvolutionFilter2D (offset 355) */
   "iiiiii\0"
   "glCopyConvolutionFilter2D\0"
   "glCopyConvolutionFilter2DEXT\0"
   "\0"
   /* _mesa_function_pool[47714]: CopyBufferSubData (will be remapped) */
   "iiiii\0"
   "glCopyBufferSubData\0"
   "\0"
   /* _mesa_function_pool[47741]: WeightfvARB (dynamic) */
   "ip\0"
   "glWeightfvARB\0"
   "\0"
   /* _mesa_function_pool[47759]: UniformMatrix3x4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3x4fv\0"
   "\0"
   /* _mesa_function_pool[47786]: Recti (offset 90) */
   "iiii\0"
   "glRecti\0"
   "\0"
   /* _mesa_function_pool[47800]: VertexAttribI3ivEXT (will be remapped) */
   "ip\0"
   "glVertexAttribI3ivEXT\0"
   "glVertexAttribI3iv\0"
   "\0"
   /* _mesa_function_pool[47845]: DeleteSamplers (will be remapped) */
   "ip\0"
   "glDeleteSamplers\0"
   "\0"
   /* _mesa_function_pool[47866]: SamplerParameteri (will be remapped) */
   "iii\0"
   "glSamplerParameteri\0"
   "\0"
   /* _mesa_function_pool[47891]: Rectf (offset 88) */
   "ffff\0"
   "glRectf\0"
   "\0"
   /* _mesa_function_pool[47905]: Rectd (offset 86) */
   "dddd\0"
   "glRectd\0"
   "\0"
   /* _mesa_function_pool[47919]: MultMatrixx (will be remapped) */
   "p\0"
   "glMultMatrixxOES\0"
   "glMultMatrixx\0"
   "\0"
   /* _mesa_function_pool[47953]: Rects (offset 92) */
   "iiii\0"
   "glRects\0"
   "\0"
   /* _mesa_function_pool[47967]: CombinerParameterfNV (dynamic) */
   "if\0"
   "glCombinerParameterfNV\0"
   "\0"
   /* _mesa_function_pool[47994]: GetVertexAttribIiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribIivEXT\0"
   "glGetVertexAttribIiv\0"
   "\0"
   /* _mesa_function_pool[48044]: ClientWaitSync (will be remapped) */
   "iii\0"
   "glClientWaitSync\0"
   "\0"
   /* _mesa_function_pool[48066]: TexCoord4s (offset 124) */
   "iiii\0"
   "glTexCoord4s\0"
   "\0"
   /* _mesa_function_pool[48085]: TexEnvxv (will be remapped) */
   "iip\0"
   "glTexEnvxvOES\0"
   "glTexEnvxv\0"
   "\0"
   /* _mesa_function_pool[48115]: TexCoord4i (offset 122) */
   "iiii\0"
   "glTexCoord4i\0"
   "\0"
   /* _mesa_function_pool[48134]: ObjectPurgeableAPPLE (will be remapped) */
   "iii\0"
   "glObjectPurgeableAPPLE\0"
   "\0"
   /* _mesa_function_pool[48162]: TexCoord4d (offset 118) */
   "dddd\0"
   "glTexCoord4d\0"
   "\0"
   /* _mesa_function_pool[48181]: TexCoord4f (offset 120) */
   "ffff\0"
   "glTexCoord4f\0"
   "\0"
   /* _mesa_function_pool[48200]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[48218]: IsAsyncMarkerSGIX (dynamic) */
   "i\0"
   "glIsAsyncMarkerSGIX\0"
   "\0"
   /* _mesa_function_pool[48241]: ProgramUniformMatrix3dv (will be remapped) */
   "iiiip\0"
   "glProgramUniformMatrix3dv\0"
   "\0"
   /* _mesa_function_pool[48274]: LockArraysEXT (will be remapped) */
   "ii\0"
   "glLockArraysEXT\0"
   "\0"
   /* _mesa_function_pool[48294]: GetActiveUniformBlockiv (will be remapped) */
   "iiip\0"
   "glGetActiveUniformBlockiv\0"
   "\0"
   /* _mesa_function_pool[48326]: GetPerfMonitorCountersAMD (will be remapped) */
   "ippip\0"
   "glGetPerfMonitorCountersAMD\0"
   "\0"
   /* _mesa_function_pool[48361]: ObjectPtrLabel (will be remapped) */
   "pip\0"
   "glObjectPtrLabel\0"
   "\0"
   /* _mesa_function_pool[48383]: Rectfv (offset 89) */
   "pp\0"
   "glRectfv\0"
   "\0"
   /* _mesa_function_pool[48396]: BindImageTexture (will be remapped) */
   "iiiiiii\0"
   "glBindImageTexture\0"
   "\0"
   /* _mesa_function_pool[48424]: VertexP4uiv (will be remapped) */
   "ip\0"
   "glVertexP4uiv\0"
   "\0"
   /* _mesa_function_pool[48442]: GetUniformSubroutineuiv (will be remapped) */
   "iip\0"
   "glGetUniformSubroutineuiv\0"
   "\0"
   /* _mesa_function_pool[48473]: MinSampleShading (will be remapped) */
   "f\0"
   "glMinSampleShadingARB\0"
   "glMinSampleShading\0"
   "\0"
   /* _mesa_function_pool[48517]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "glGetRenderbufferParameterivEXT\0"
   "glGetRenderbufferParameterivOES\0"
   "\0"
   /* _mesa_function_pool[48615]: EdgeFlagPointerListIBM (dynamic) */
   "ipi\0"
   "glEdgeFlagPointerListIBM\0"
   "\0"
   /* _mesa_function_pool[48645]: VertexAttrib1dNV (will be remapped) */
   "id\0"
   "glVertexAttrib1dNV\0"
   "\0"
   /* _mesa_function_pool[48668]: WindowPos2sv (will be remapped) */
   "p\0"
   "glWindowPos2sv\0"
   "glWindowPos2svARB\0"
   "glWindowPos2svMESA\0"
   "\0"
   /* _mesa_function_pool[48723]: VertexArrayRangeNV (dynamic) */
   "ip\0"
   "glVertexArrayRangeNV\0"
   "\0"
   /* _mesa_function_pool[48748]: GetPerfMonitorCounterStringAMD (will be remapped) */
   "iiipp\0"
   "glGetPerfMonitorCounterStringAMD\0"
   "\0"
   /* _mesa_function_pool[48788]: EndFragmentShaderATI (will be remapped) */
   "\0"
   "glEndFragmentShaderATI\0"
   "\0"
   /* _mesa_function_pool[48813]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "glUniform4ivARB\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   { 20135, CompressedTexImage1D_remap_index },
   { 17416, CompressedTexImage2D_remap_index },
   { 12843, CompressedTexImage3D_remap_index },
   { 31744, CompressedTexSubImage1D_remap_index },
   { 38354, CompressedTexSubImage2D_remap_index },
   {  6641, CompressedTexSubImage3D_remap_index },
   {  4430, GetCompressedTexImage_remap_index },
   { 19351, LoadTransposeMatrixd_remap_index },
   { 19299, LoadTransposeMatrixf_remap_index },
   { 35608, MultTransposeMatrixd_remap_index },
   { 14130, MultTransposeMatrixf_remap_index },
   { 34379, SampleCoverage_remap_index },
   {  3644, BlendFuncSeparate_remap_index },
   { 23140, FogCoordPointer_remap_index },
   { 42332, FogCoordd_remap_index },
   { 42108, FogCoorddv_remap_index },
   { 34606, MultiDrawArrays_remap_index },
   { 32645, PointParameterf_remap_index },
   {  5240, PointParameterfv_remap_index },
   { 32603, PointParameteri_remap_index },
   {  9274, PointParameteriv_remap_index },
   {  5662, SecondaryColor3b_remap_index },
   { 41923, SecondaryColor3bv_remap_index },
   { 35809, SecondaryColor3d_remap_index },
   { 12976, SecondaryColor3dv_remap_index },
   {  5792, SecondaryColor3i_remap_index },
   { 39503, SecondaryColor3iv_remap_index },
   {  5538, SecondaryColor3s_remap_index },
   { 16637, SecondaryColor3sv_remap_index },
   { 23293, SecondaryColor3ub_remap_index },
   {  7732, SecondaryColor3ubv_remap_index },
   { 23371, SecondaryColor3ui_remap_index },
   { 25439, SecondaryColor3uiv_remap_index },
   { 23184, SecondaryColor3us_remap_index },
   { 10366, SecondaryColor3usv_remap_index },
   { 36988, SecondaryColorPointer_remap_index },
   { 12575, WindowPos2d_remap_index },
   { 18324, WindowPos2dv_remap_index },
   { 12522, WindowPos2f_remap_index },
   { 24736, WindowPos2fv_remap_index },
   { 12654, WindowPos2i_remap_index },
   {  6912, WindowPos2iv_remap_index },
   { 12707, WindowPos2s_remap_index },
   { 48668, WindowPos2sv_remap_index },
   { 16949, WindowPos3d_remap_index },
   { 16334, WindowPos3dv_remap_index },
   { 17036, WindowPos3f_remap_index },
   {  9133, WindowPos3fv_remap_index },
   { 17145, WindowPos3i_remap_index },
   { 47596, WindowPos3iv_remap_index },
   { 17261, WindowPos3s_remap_index },
   { 26241, WindowPos3sv_remap_index },
   {  6794, BeginQuery_remap_index },
   { 47447, BindBuffer_remap_index },
   { 40713, BufferData_remap_index },
   { 10910, BufferSubData_remap_index },
   { 32973, DeleteBuffers_remap_index },
   { 23638, DeleteQueries_remap_index },
   { 21045, EndQuery_remap_index },
   { 44629, GenBuffers_remap_index },
   {  2030, GenQueries_remap_index },
   { 30283, GetBufferParameteriv_remap_index },
   { 44494, GetBufferPointerv_remap_index },
   { 33012, GetBufferSubData_remap_index },
   {  8780, GetQueryObjectiv_remap_index },
   {  8385, GetQueryObjectuiv_remap_index },
   { 13169, GetQueryiv_remap_index },
   { 19783, IsBuffer_remap_index },
   { 30526, IsQuery_remap_index },
   { 13308, MapBuffer_remap_index },
   { 47124, UnmapBuffer_remap_index },
   {   315, AttachShader_remap_index },
   { 38947, BindAttribLocation_remap_index },
   { 44726, BlendEquationSeparate_remap_index },
   { 34179, CompileShader_remap_index },
   { 26565, CreateProgram_remap_index },
   { 32862, CreateShader_remap_index },
   { 21926, DeleteProgram_remap_index },
   { 34124, DeleteShader_remap_index },
   { 46606, DetachShader_remap_index },
   { 36638, DisableVertexAttribArray_remap_index },
   { 24517, DrawBuffers_remap_index },
   { 40204, EnableVertexAttribArray_remap_index },
   { 39705, GetActiveAttrib_remap_index },
   { 45452, GetActiveUniform_remap_index },
   { 18922, GetAttachedShaders_remap_index },
   { 29168, GetAttribLocation_remap_index },
   { 12182, GetProgramInfoLog_remap_index },
   { 24252, GetProgramiv_remap_index },
   {  4171, GetShaderInfoLog_remap_index },
   {  8076, GetShaderSource_remap_index },
   { 18658, GetShaderiv_remap_index },
   {  6845, GetUniformLocation_remap_index },
   { 14283, GetUniformfv_remap_index },
   {  2332, GetUniformiv_remap_index },
   { 37361, GetVertexAttribPointerv_remap_index },
   { 44446, GetVertexAttribdv_remap_index },
   { 38080, GetVertexAttribfv_remap_index },
   { 40887, GetVertexAttribiv_remap_index },
   {  4624, IsProgram_remap_index },
   { 41434, IsShader_remap_index },
   { 30994, LinkProgram_remap_index },
   { 40509, ShaderSource_remap_index },
   { 40481, StencilFuncSeparate_remap_index },
   { 38437, StencilMaskSeparate_remap_index },
   { 39885, StencilOpSeparate_remap_index },
   { 43604, Uniform1f_remap_index },
   {  8988, Uniform1fv_remap_index },
   { 43680, Uniform1i_remap_index },
   { 20006, Uniform1iv_remap_index },
   { 46157, Uniform2f_remap_index },
   { 24418, Uniform2fv_remap_index },
   { 46246, Uniform2i_remap_index },
   { 22158, Uniform2iv_remap_index },
   {   922, Uniform3f_remap_index },
   { 40999, Uniform3fv_remap_index },
   {   842, Uniform3i_remap_index },
   { 42387, Uniform3iv_remap_index },
   {  4991, Uniform4f_remap_index },
   {  9744, Uniform4fv_remap_index },
   {  4938, Uniform4i_remap_index },
   { 48813, Uniform4iv_remap_index },
   { 11046, UniformMatrix2fv_remap_index },
   { 25173, UniformMatrix3fv_remap_index },
   { 11567, UniformMatrix4fv_remap_index },
   { 43838, UseProgram_remap_index },
   { 26681, ValidateProgram_remap_index },
   { 20094, VertexAttrib1d_remap_index },
   { 41357, VertexAttrib1dv_remap_index },
   { 20244, VertexAttrib1s_remap_index },
   { 37901, VertexAttrib1sv_remap_index },
   {  8914, VertexAttrib2d_remap_index },
   { 25992, VertexAttrib2dv_remap_index },
   {  8826, VertexAttrib2s_remap_index },
   { 15813, VertexAttrib2sv_remap_index },
   { 13203, VertexAttrib3d_remap_index },
   { 24342, VertexAttrib3dv_remap_index },
   { 13078, VertexAttrib3s_remap_index },
   { 43429, VertexAttrib3sv_remap_index },
   { 13380, VertexAttrib4Nbv_remap_index },
   { 30890, VertexAttrib4Niv_remap_index },
   { 22494, VertexAttrib4Nsv_remap_index },
   {  1549, VertexAttrib4Nub_remap_index },
   { 36143, VertexAttrib4Nubv_remap_index },
   { 11641, VertexAttrib4Nuiv_remap_index },
   { 38745, VertexAttrib4Nusv_remap_index },
   { 10296, VertexAttrib4bv_remap_index },
   { 31243, VertexAttrib4d_remap_index },
   { 31638, VertexAttrib4dv_remap_index },
   { 42499, VertexAttrib4iv_remap_index },
   { 31311, VertexAttrib4s_remap_index },
   { 21145, VertexAttrib4sv_remap_index },
   { 11300, VertexAttrib4ubv_remap_index },
   { 22449, VertexAttrib4uiv_remap_index },
   {  1475, VertexAttrib4usv_remap_index },
   { 36237, VertexAttribPointer_remap_index },
   { 32409, UniformMatrix2x3fv_remap_index },
   {   955, UniformMatrix2x4fv_remap_index },
   { 11614, UniformMatrix3x2fv_remap_index },
   { 47759, UniformMatrix3x4fv_remap_index },
   { 42909, UniformMatrix4x2fv_remap_index },
   { 13121, UniformMatrix4x3fv_remap_index },
   { 18564, BeginConditionalRender_remap_index },
   { 26810, BeginTransformFeedback_remap_index },
   {  8738, BindBufferBase_remap_index },
   {  8626, BindBufferRange_remap_index },
   { 24900, BindFragDataLocation_remap_index },
   { 26100, ClampColor_remap_index },
   { 18949, ClearBufferfi_remap_index },
   { 18773, ClearBufferfv_remap_index },
   { 23000, ClearBufferiv_remap_index },
   { 42186, ClearBufferuiv_remap_index },
   { 14699, ColorMaski_remap_index },
   {  6606, Disablei_remap_index },
   { 17003, Enablei_remap_index },
   { 25487, EndConditionalRender_remap_index },
   { 21708, EndTransformFeedback_remap_index },
   { 13568, GetBooleani_v_remap_index },
   { 43936, GetFragDataLocation_remap_index },
   { 23021, GetIntegeri_v_remap_index },
   { 31603, GetStringi_remap_index },
   { 33600, GetTexParameterIiv_remap_index },
   { 43312, GetTexParameterIuiv_remap_index },
   { 33814, GetTransformFeedbackVarying_remap_index },
   {  3184, GetUniformuiv_remap_index },
   { 47994, GetVertexAttribIiv_remap_index },
   { 22799, GetVertexAttribIuiv_remap_index },
   { 37276, IsEnabledi_remap_index },
   { 34264, TexParameterIiv_remap_index },
   { 18379, TexParameterIuiv_remap_index },
   { 43364, TransformFeedbackVaryings_remap_index },
   { 37655, Uniform1ui_remap_index },
   { 28528, Uniform1uiv_remap_index },
   { 27822, Uniform2ui_remap_index },
   { 14741, Uniform2uiv_remap_index },
   { 36532, Uniform3ui_remap_index },
   { 21204, Uniform3uiv_remap_index },
   { 13491, Uniform4ui_remap_index },
   { 20040, Uniform4uiv_remap_index },
   { 39234, VertexAttribI1iv_remap_index },
   { 12929, VertexAttribI1uiv_remap_index },
   {  8433, VertexAttribI4bv_remap_index },
   { 26749, VertexAttribI4sv_remap_index },
   {  9518, VertexAttribI4ubv_remap_index },
   {   451, VertexAttribI4usv_remap_index },
   { 44921, VertexAttribIPointer_remap_index },
   {  9465, PrimitiveRestartIndex_remap_index },
   { 37490, TexBuffer_remap_index },
   {   111, FramebufferTexture_remap_index },
   { 26535, GetBufferParameteri64v_remap_index },
   { 19711, GetInteger64i_v_remap_index },
   { 45261, VertexAttribDivisor_remap_index },
   { 48473, MinSampleShading_remap_index },
   { 22741, MemoryBarrierByRegion_remap_index },
   {  8121, BindProgramARB_remap_index },
   { 34474, DeleteProgramsARB_remap_index },
   { 17090, GenProgramsARB_remap_index },
   { 15856, GetProgramEnvParameterdvARB_remap_index },
   { 32938, GetProgramEnvParameterfvARB_remap_index },
   { 34651, GetProgramLocalParameterdvARB_remap_index },
   { 41738, GetProgramLocalParameterfvARB_remap_index },
   { 25102, GetProgramStringARB_remap_index },
   {  9351, GetProgramivARB_remap_index },
   { 35002, IsProgramARB_remap_index },
   { 19419, ProgramEnvParameter4dARB_remap_index },
   {  2967, ProgramEnvParameter4dvARB_remap_index },
   { 43105, ProgramEnvParameter4fARB_remap_index },
   { 27426, ProgramEnvParameter4fvARB_remap_index },
   { 25537, ProgramLocalParameter4dARB_remap_index },
   {  4486, ProgramLocalParameter4dvARB_remap_index },
   { 34060, ProgramLocalParameter4fARB_remap_index },
   { 21492, ProgramLocalParameter4fvARB_remap_index },
   { 35074, ProgramStringARB_remap_index },
   { 13527, VertexAttrib1fARB_remap_index },
   { 35485, VertexAttrib1fvARB_remap_index },
   { 24694, VertexAttrib2fARB_remap_index },
   { 14988, VertexAttrib2fvARB_remap_index },
   {   334, VertexAttrib3fARB_remap_index },
   { 29487, VertexAttrib3fvARB_remap_index },
   { 28245, VertexAttrib4fARB_remap_index },
   { 16291, VertexAttrib4fvARB_remap_index },
   { 39991, AttachObjectARB_remap_index },
   { 25146, CreateProgramObjectARB_remap_index },
   { 18847, CreateShaderObjectARB_remap_index },
   { 17475, DeleteObjectARB_remap_index },
   { 42668, DetachObjectARB_remap_index },
   { 40348, GetAttachedObjectsARB_remap_index },
   { 21782, GetHandleARB_remap_index },
   { 22916, GetInfoLogARB_remap_index },
   { 23804, GetObjectParameterfvARB_remap_index },
   { 46716, GetObjectParameterivARB_remap_index },
   {  6386, DrawArraysInstancedARB_remap_index },
   {  8300, DrawElementsInstancedARB_remap_index },
   { 15610, BindFramebuffer_remap_index },
   {  9374, BindRenderbuffer_remap_index },
   { 37709, BlitFramebuffer_remap_index },
   {  7228, CheckFramebufferStatus_remap_index },
   { 22350, DeleteFramebuffers_remap_index },
   { 41281, DeleteRenderbuffers_remap_index },
   { 34912, FramebufferRenderbuffer_remap_index },
   { 37785, FramebufferTexture1D_remap_index },
   { 25860, FramebufferTexture2D_remap_index },
   { 30169, FramebufferTexture3D_remap_index },
   { 41552, FramebufferTextureLayer_remap_index },
   { 44565, GenFramebuffers_remap_index },
   { 37155, GenRenderbuffers_remap_index },
   {  4853, GenerateMipmap_remap_index },
   {  5995, GetFramebufferAttachmentParameteriv_remap_index },
   { 48517, GetRenderbufferParameteriv_remap_index },
   {  7447, IsFramebuffer_remap_index },
   { 28610, IsRenderbuffer_remap_index },
   {   669, RenderbufferStorage_remap_index },
   { 16839, RenderbufferStorageMultisample_remap_index },
   {  5922, FlushMappedBufferRange_remap_index },
   { 34717, MapBufferRange_remap_index },
   { 14834, BindVertexArray_remap_index },
   {  1173, DeleteVertexArrays_remap_index },
   { 44139, GenVertexArrays_remap_index },
   { 43472, IsVertexArray_remap_index },
   { 14614, GetActiveUniformBlockName_remap_index },
   { 48294, GetActiveUniformBlockiv_remap_index },
   { 23341, GetActiveUniformName_remap_index },
   { 15784, GetActiveUniformsiv_remap_index },
   { 46579, GetUniformBlockIndex_remap_index },
   { 11738, GetUniformIndices_remap_index },
   { 38661, UniformBlockBinding_remap_index },
   { 47714, CopyBufferSubData_remap_index },
   { 48044, ClientWaitSync_remap_index },
   { 12805, DeleteSync_remap_index },
   { 38688, FenceSync_remap_index },
   { 42648, GetInteger64v_remap_index },
   { 45117, GetSynciv_remap_index },
   { 17555, IsSync_remap_index },
   { 37345, WaitSync_remap_index },
   { 14649, DrawElementsBaseVertex_remap_index },
   { 19053, DrawElementsInstancedBaseVertex_remap_index },
   { 41807, DrawRangeElementsBaseVertex_remap_index },
   { 46278, MultiDrawElementsBaseVertex_remap_index },
   { 27122, ProvokingVertex_remap_index },
   {  6290, GetMultisamplefv_remap_index },
   { 39341, SampleMaski_remap_index },
   {  2202, TexImage2DMultisample_remap_index },
   { 46522, TexImage3DMultisample_remap_index },
   { 25648, BlendEquationSeparateiARB_remap_index },
   { 30658, BlendEquationiARB_remap_index },
   {  4223, BlendFuncSeparateiARB_remap_index },
   { 27675, BlendFunciARB_remap_index },
   {  1866, BindFragDataLocationIndexed_remap_index },
   { 32232, GetFragDataIndex_remap_index },
   {  3166, BindSampler_remap_index },
   { 47845, DeleteSamplers_remap_index },
   { 39934, GenSamplers_remap_index },
   {  2800, GetSamplerParameterIiv_remap_index },
   {  6575, GetSamplerParameterIuiv_remap_index },
   { 25963, GetSamplerParameterfv_remap_index },
   { 27949, GetSamplerParameteriv_remap_index },
   { 29313, IsSampler_remap_index },
   {  9804, SamplerParameterIiv_remap_index },
   { 13918, SamplerParameterIuiv_remap_index },
   { 32458, SamplerParameterf_remap_index },
   { 43005, SamplerParameterfv_remap_index },
   { 47866, SamplerParameteri_remap_index },
   { 31467, SamplerParameteriv_remap_index },
   { 26050, GetQueryObjecti64v_remap_index },
   {  4546, GetQueryObjectui64v_remap_index },
   { 14430, QueryCounter_remap_index },
   { 42170, ColorP3ui_remap_index },
   {  7581, ColorP3uiv_remap_index },
   { 19811, ColorP4ui_remap_index },
   { 29055, ColorP4uiv_remap_index },
   { 15457, MultiTexCoordP1ui_remap_index },
   { 28745, MultiTexCoordP1uiv_remap_index },
   { 30475, MultiTexCoordP2ui_remap_index },
   { 10151, MultiTexCoordP2uiv_remap_index },
   { 29143, MultiTexCoordP3ui_remap_index },
   {   425, MultiTexCoordP3uiv_remap_index },
   { 44870, MultiTexCoordP4ui_remap_index },
   { 38128, MultiTexCoordP4uiv_remap_index },
   { 40598, NormalP3ui_remap_index },
   { 28670, NormalP3uiv_remap_index },
   { 45858, SecondaryColorP3ui_remap_index },
   {  6523, SecondaryColorP3uiv_remap_index },
   {   162, TexCoordP1ui_remap_index },
   {   649, TexCoordP1uiv_remap_index },
   { 29446, TexCoordP2ui_remap_index },
   { 40935, TexCoordP2uiv_remap_index },
   { 16683, TexCoordP3ui_remap_index },
   { 19881, TexCoordP3uiv_remap_index },
   { 37882, TexCoordP4ui_remap_index },
   {  1949, TexCoordP4uiv_remap_index },
   { 16783, VertexAttribP1ui_remap_index },
   {  4598, VertexAttribP1uiv_remap_index },
   { 33149, VertexAttribP2ui_remap_index },
   {  5584, VertexAttribP2uiv_remap_index },
   {  1597, VertexAttribP3ui_remap_index },
   { 31493, VertexAttribP3uiv_remap_index },
   {  4913, VertexAttribP4ui_remap_index },
   { 18202, VertexAttribP4uiv_remap_index },
   { 38827, VertexP2ui_remap_index },
   { 17959, VertexP2uiv_remap_index },
   { 25129, VertexP3ui_remap_index },
   {  6827, VertexP3uiv_remap_index },
   {  3478, VertexP4ui_remap_index },
   { 48424, VertexP4uiv_remap_index },
   {   817, DrawArraysIndirect_remap_index },
   { 26213, DrawElementsIndirect_remap_index },
   {  7367, GetUniformdv_remap_index },
   { 43635, Uniform1d_remap_index },
   { 15919, Uniform1dv_remap_index },
   { 46189, Uniform2d_remap_index },
   { 31620, Uniform2dv_remap_index },
   {   904, Uniform3d_remap_index },
   { 32585, Uniform3dv_remap_index },
   {  4972, Uniform4d_remap_index },
   { 21573, Uniform4dv_remap_index },
   {  4521, UniformMatrix2dv_remap_index },
   { 25058, UniformMatrix2x3dv_remap_index },
   { 17914, UniformMatrix2x4dv_remap_index },
   { 32483, UniformMatrix3dv_remap_index },
   {  4802, UniformMatrix3x2dv_remap_index },
   {  5610, UniformMatrix3x4dv_remap_index },
   { 18794, UniformMatrix4dv_remap_index },
   { 36210, UniformMatrix4x2dv_remap_index },
   { 20320, UniformMatrix4x3dv_remap_index },
   {  5758, GetActiveSubroutineName_remap_index },
   {  6332, GetActiveSubroutineUniformName_remap_index },
   { 17787, GetActiveSubroutineUniformiv_remap_index },
   { 13354, GetProgramStageiv_remap_index },
   { 14474, GetSubroutineIndex_remap_index },
   {  1439, GetSubroutineUniformLocation_remap_index },
   { 48442, GetUniformSubroutineuiv_remap_index },
   {  7126, UniformSubroutinesuiv_remap_index },
   { 16179, PatchParameterfv_remap_index },
   { 12372, PatchParameteri_remap_index },
   { 12208, BindTransformFeedback_remap_index },
   { 12080, DeleteTransformFeedbacks_remap_index },
   { 39549, DrawTransformFeedback_remap_index },
   {  4380, GenTransformFeedbacks_remap_index },
   { 36784, IsTransformFeedback_remap_index },
   { 34097, PauseTransformFeedback_remap_index },
   { 39169, ResumeTransformFeedback_remap_index },
   { 24988, BeginQueryIndexed_remap_index },
   { 45312, DrawTransformFeedbackStream_remap_index },
   { 21294, EndQueryIndexed_remap_index },
   { 24208, GetQueryIndexediv_remap_index },
   { 21984, ClearDepthf_remap_index },
   { 26583, DepthRangef_remap_index },
   { 42208, GetShaderPrecisionFormat_remap_index },
   {  3618, ReleaseShaderCompiler_remap_index },
   { 28087, ShaderBinary_remap_index },
   { 21800, GetProgramBinary_remap_index },
   { 13425, ProgramBinary_remap_index },
   { 13660, ProgramParameteri_remap_index },
   { 12628, GetVertexAttribLdv_remap_index },
   { 47192, VertexAttribL1d_remap_index },
   {  7424, VertexAttribL1dv_remap_index },
   { 38012, VertexAttribL2d_remap_index },
   { 21759, VertexAttribL2dv_remap_index },
   { 40424, VertexAttribL3d_remap_index },
   { 14895, VertexAttribL3dv_remap_index },
   {  8540, VertexAttribL4d_remap_index },
   { 22634, VertexAttribL4dv_remap_index },
   { 46865, VertexAttribLPointer_remap_index },
   { 29865, DepthRangeArrayv_remap_index },
   { 46344, DepthRangeIndexed_remap_index },
   { 36764, GetDoublei_v_remap_index },
   { 39359, GetFloati_v_remap_index },
   { 46782, ScissorArrayv_remap_index },
   { 27856, ScissorIndexed_remap_index },
   { 31519, ScissorIndexedv_remap_index },
   { 20735, ViewportArrayv_remap_index },
   { 35179, ViewportIndexedf_remap_index },
   { 21848, ViewportIndexedfv_remap_index },
   {  9565, GetGraphicsResetStatusARB_remap_index },
   { 33356, GetnColorTableARB_remap_index },
   {  3100, GetnCompressedTexImageARB_remap_index },
   {  1273, GetnConvolutionFilterARB_remap_index },
   {  5458, GetnHistogramARB_remap_index },
   { 25942, GetnMapdvARB_remap_index },
   { 13809, GetnMapfvARB_remap_index },
   { 38035, GetnMapivARB_remap_index },
   { 43217, GetnMinmaxARB_remap_index },
   {  4076, GetnPixelMapfvARB_remap_index },
   {  6549, GetnPixelMapuivARB_remap_index },
   { 13052, GetnPixelMapusvARB_remap_index },
   { 24602, GetnPolygonStippleARB_remap_index },
   { 32000, GetnSeparableFilterARB_remap_index },
   { 11345, GetnTexImageARB_remap_index },
   { 30969, GetnUniformdvARB_remap_index },
   { 37587, GetnUniformfvARB_remap_index },
   {  3593, GetnUniformivARB_remap_index },
   { 15282, GetnUniformuivARB_remap_index },
   { 28306, ReadnPixelsARB_remap_index },
   { 36392, DrawArraysInstancedBaseInstance_remap_index },
   { 11523, DrawElementsInstancedBaseInstance_remap_index },
   {  2912, DrawElementsInstancedBaseVertexBaseInstance_remap_index },
   { 39080, DrawTransformFeedbackInstanced_remap_index },
   { 14943, DrawTransformFeedbackStreamInstanced_remap_index },
   { 46803, GetInternalformativ_remap_index },
   { 21945, GetActiveAtomicCounterBufferiv_remap_index },
   { 48396, BindImageTexture_remap_index },
   { 23717, MemoryBarrier_remap_index },
   { 37688, TexStorage1D_remap_index },
   { 25802, TexStorage2D_remap_index },
   { 30146, TexStorage3D_remap_index },
   {  1520, TextureStorage1DEXT_remap_index },
   { 38997, TextureStorage2DEXT_remap_index },
   { 24663, TextureStorage3DEXT_remap_index },
   { 39809, ClearBufferData_remap_index },
   {  2370, ClearBufferSubData_remap_index },
   { 34760, DispatchCompute_remap_index },
   {  7312, DispatchComputeIndirect_remap_index },
   { 39849, CopyImageSubData_remap_index },
   { 45136, TextureView_remap_index },
   { 24077, BindVertexBuffer_remap_index },
   { 32758, VertexAttribBinding_remap_index },
   { 33383, VertexAttribFormat_remap_index },
   { 36333, VertexAttribIFormat_remap_index },
   { 40396, VertexAttribLFormat_remap_index },
   { 38597, VertexBindingDivisor_remap_index },
   { 35855, FramebufferParameteri_remap_index },
   { 31153, GetFramebufferParameteriv_remap_index },
   { 41775, MultiDrawArraysIndirect_remap_index },
   { 20285, MultiDrawElementsIndirect_remap_index },
   { 33971, GetProgramInterfaceiv_remap_index },
   {  3495, GetProgramResourceIndex_remap_index },
   {  1405, GetProgramResourceLocation_remap_index },
   {  1711, GetProgramResourceLocationIndex_remap_index },
   { 15061, GetProgramResourceName_remap_index },
   { 45759, GetProgramResourceiv_remap_index },
   { 20220, TexBufferRange_remap_index },
   { 42421, TexStorage2DMultisample_remap_index },
   { 31412, TexStorage3DMultisample_remap_index },
   {  3790, BufferStorage_remap_index },
   { 42936, ClearTexImage_remap_index },
   { 14582, ClearTexSubImage_remap_index },
   {  4693, BindBuffersBase_remap_index },
   { 16264, BindBuffersRange_remap_index },
   { 12055, BindImageTextures_remap_index },
   {  3080, BindSamplers_remap_index },
   { 45883, BindTextures_remap_index },
   { 27922, BindVertexBuffers_remap_index },
   { 39952, ClipControl_remap_index },
   { 21872, BindTextureUnit_remap_index },
   { 43753, BlitNamedFramebuffer_remap_index },
   {  7060, CheckNamedFramebufferStatus_remap_index },
   { 25013, ClearNamedBufferData_remap_index },
   { 44006, ClearNamedBufferSubData_remap_index },
   { 27731, ClearNamedFramebufferfi_remap_index },
   { 27763, ClearNamedFramebufferfv_remap_index },
   {  8956, ClearNamedFramebufferiv_remap_index },
   { 40448, ClearNamedFramebufferuiv_remap_index },
   { 43031, CompressedTextureSubImage1D_remap_index },
   { 38704, CompressedTextureSubImage2D_remap_index },
   { 36433, CompressedTextureSubImage3D_remap_index },
   {  3134, CopyNamedBufferSubData_remap_index },
   { 39118, CopyTextureSubImage1D_remap_index },
   { 35099, CopyTextureSubImage2D_remap_index },
   { 45063, CopyTextureSubImage3D_remap_index },
   {  6246, CreateBuffers_remap_index },
   { 30822, CreateFramebuffers_remap_index },
   {  1113, CreateProgramPipelines_remap_index },
   { 34533, CreateQueries_remap_index },
   {  9595, CreateRenderbuffers_remap_index },
   { 40615, CreateSamplers_remap_index },
   { 31681, CreateTextures_remap_index },
   {  1750, CreateTransformFeedbacks_remap_index },
   { 23115, CreateVertexArrays_remap_index },
   {  8509, DisableVertexArrayAttrib_remap_index },
   { 44696, EnableVertexArrayAttrib_remap_index },
   { 15422, FlushMappedNamedBufferRange_remap_index },
   { 34447, GenerateTextureMipmap_remap_index },
   {   391, GetCompressedTextureImage_remap_index },
   {  5051, GetNamedBufferParameteri64v_remap_index },
   { 25615, GetNamedBufferParameteriv_remap_index },
   { 31213, GetNamedBufferPointerv_remap_index },
   { 12443, GetNamedBufferSubData_remap_index },
   {  6985, GetNamedFramebufferAttachmentParameteriv_remap_index },
   { 46678, GetNamedFramebufferParameteriv_remap_index },
   { 33059, GetNamedRenderbufferParameteriv_remap_index },
   { 27251, GetQueryBufferObjecti64v_remap_index },
   { 47398, GetQueryBufferObjectiv_remap_index },
   {  9925, GetQueryBufferObjectui64v_remap_index },
   {  3224, GetQueryBufferObjectuiv_remap_index },
   {  3300, GetTextureImage_remap_index },
   { 36108, GetTextureLevelParameterfv_remap_index },
   { 38893, GetTextureLevelParameteriv_remap_index },
   { 15580, GetTextureParameterIiv_remap_index },
   { 23940, GetTextureParameterIuiv_remap_index },
   { 26359, GetTextureParameterfv_remap_index },
   { 29818, GetTextureParameteriv_remap_index },
   { 16915, GetTransformFeedbacki64_v_remap_index },
   {  4031, GetTransformFeedbacki_v_remap_index },
   { 26966, GetTransformFeedbackiv_remap_index },
   {  6164, GetVertexArrayIndexed64iv_remap_index },
   { 34232, GetVertexArrayIndexediv_remap_index },
   { 17531, GetVertexArrayiv_remap_index },
   { 33318, InvalidateNamedFramebufferData_remap_index },
   { 29010, InvalidateNamedFramebufferSubData_remap_index },
   { 10435, MapNamedBuffer_remap_index },
   { 13280, MapNamedBufferRange_remap_index },
   { 46947, NamedBufferData_remap_index },
   { 11889, NamedBufferStorage_remap_index },
   { 20193, NamedBufferSubData_remap_index },
   { 11916, NamedFramebufferDrawBuffer_remap_index },
   { 38279, NamedFramebufferDrawBuffers_remap_index },
   { 27888, NamedFramebufferParameteri_remap_index },
   { 22955, NamedFramebufferReadBuffer_remap_index },
   { 34142, NamedFramebufferRenderbuffer_remap_index },
   {  5086, NamedFramebufferTexture_remap_index },
   { 11949, NamedFramebufferTextureLayer_remap_index },
   {  9318, NamedRenderbufferStorage_remap_index },
   { 29344, NamedRenderbufferStorageMultisample_remap_index },
   { 23558, TextureBuffer_remap_index },
   { 12302, TextureBufferRange_remap_index },
   { 39027, TextureParameterIiv_remap_index },
   { 30007, TextureParameterIuiv_remap_index },
   { 37760, TextureParameterf_remap_index },
   {  2400, TextureParameterfv_remap_index },
   { 37841, TextureParameteri_remap_index },
   { 26618, TextureParameteriv_remap_index },
   { 11136, TextureStorage1D_remap_index },
   { 15554, TextureStorage2D_remap_index },
   { 44264, TextureStorage2DMultisample_remap_index },
   { 19827, TextureStorage3D_remap_index },
   {  3554, TextureStorage3DMultisample_remap_index },
   { 28186, TextureSubImage1D_remap_index },
   { 32784, TextureSubImage2D_remap_index },
   { 36017, TextureSubImage3D_remap_index },
   { 20347, TransformFeedbackBufferBase_remap_index },
   { 15973, TransformFeedbackBufferRange_remap_index },
   {  4780, UnmapNamedBuffer_remap_index },
   { 26883, VertexArrayAttribBinding_remap_index },
   { 19901, VertexArrayAttribFormat_remap_index },
   { 16407, VertexArrayAttribIFormat_remap_index },
   {  2830, VertexArrayAttribLFormat_remap_index },
   { 18012, VertexArrayBindingDivisor_remap_index },
   { 16702, VertexArrayElementBuffer_remap_index },
   { 45684, VertexArrayVertexBuffer_remap_index },
   { 19018, VertexArrayVertexBuffers_remap_index },
   {  2142, GetCompressedTextureSubImage_remap_index },
   {  7625, GetTextureSubImage_remap_index },
   {  7341, InvalidateBufferData_remap_index },
   { 42878, InvalidateBufferSubData_remap_index },
   { 23677, InvalidateFramebuffer_remap_index },
   { 17861, InvalidateSubFramebuffer_remap_index },
   { 13466, InvalidateTexImage_remap_index },
   { 28333, InvalidateTexSubImage_remap_index },
   { 14182, PolygonOffsetEXT_remap_index },
   { 39970, DrawTexfOES_remap_index },
   { 27657, DrawTexfvOES_remap_index },
   {  1019, DrawTexiOES_remap_index },
   { 33254, DrawTexivOES_remap_index },
   { 13613, DrawTexsOES_remap_index },
   { 23871, DrawTexsvOES_remap_index },
   { 29072, DrawTexxOES_remap_index },
   { 42036, DrawTexxvOES_remap_index },
   { 27013, PointSizePointerOES_remap_index },
   {   982, QueryMatrixxOES_remap_index },
   { 21358, SampleMaskSGIS_remap_index },
   { 36490, SamplePatternSGIS_remap_index },
   { 46206, ColorPointerEXT_remap_index },
   { 30552, EdgeFlagPointerEXT_remap_index },
   { 14259, IndexPointerEXT_remap_index },
   { 14449, NormalPointerEXT_remap_index },
   { 29930, TexCoordPointerEXT_remap_index },
   { 26723, VertexPointerEXT_remap_index },
   { 46128, DiscardFramebufferEXT_remap_index },
   { 12004, ActiveShaderProgram_remap_index },
   { 18152, BindProgramPipeline_remap_index },
   { 30604, CreateShaderProgramv_remap_index },
   {  3942, DeleteProgramPipelines_remap_index },
   { 27978, GenProgramPipelines_remap_index },
   {  9022, GetProgramPipelineInfoLog_remap_index },
   { 33459, GetProgramPipelineiv_remap_index },
   { 28041, IsProgramPipeline_remap_index },
   { 48274, LockArraysEXT_remap_index },
   { 47572, ProgramUniform1d_remap_index },
   { 32912, ProgramUniform1dv_remap_index },
   { 47526, ProgramUniform1f_remap_index },
   { 10612, ProgramUniform1fv_remap_index },
   { 47480, ProgramUniform1i_remap_index },
   { 16538, ProgramUniform1iv_remap_index },
   { 36940, ProgramUniform1ui_remap_index },
   { 47297, ProgramUniform1uiv_remap_index },
   {  2582, ProgramUniform2d_remap_index },
   { 10541, ProgramUniform2dv_remap_index },
   {  2535, ProgramUniform2f_remap_index },
   { 38844, ProgramUniform2fv_remap_index },
   {  2607, ProgramUniform2i_remap_index },
   { 23066, ProgramUniform2iv_remap_index },
   {  7971, ProgramUniform2ui_remap_index },
   {  9959, ProgramUniform2uiv_remap_index },
   {  5118, ProgramUniform3d_remap_index },
   {  5025, ProgramUniform3dv_remap_index },
   {  5144, ProgramUniform3f_remap_index },
   { 32281, ProgramUniform3fv_remap_index },
   {  5192, ProgramUniform3i_remap_index },
   { 14500, ProgramUniform3iv_remap_index },
   { 16587, ProgramUniform3ui_remap_index },
   { 19563, ProgramUniform3uiv_remap_index },
   { 31028, ProgramUniform4d_remap_index },
   { 33174, ProgramUniform4dv_remap_index },
   { 31055, ProgramUniform4f_remap_index },
   { 44977, ProgramUniform4fv_remap_index },
   { 31104, ProgramUniform4i_remap_index },
   {  2063, ProgramUniform4iv_remap_index },
   { 43553, ProgramUniform4ui_remap_index },
   { 35660, ProgramUniform4uiv_remap_index },
   { 14549, ProgramUniformMatrix2dv_remap_index },
   { 21430, ProgramUniformMatrix2fv_remap_index },
   { 17496, ProgramUniformMatrix2x3dv_remap_index },
   { 24011, ProgramUniformMatrix2x3fv_remap_index },
   {  1995, ProgramUniformMatrix2x4dv_remap_index },
   {  8672, ProgramUniformMatrix2x4fv_remap_index },
   { 48241, ProgramUniformMatrix3dv_remap_index },
   { 41659, ProgramUniformMatrix3fv_remap_index },
   { 28871, ProgramUniformMatrix3x2dv_remap_index },
   { 36698, ProgramUniformMatrix3x2fv_remap_index },
   { 24482, ProgramUniformMatrix3x4dv_remap_index },
   { 29530, ProgramUniformMatrix3x4fv_remap_index },
   { 41846, ProgramUniformMatrix4dv_remap_index },
   { 34829, ProgramUniformMatrix4fv_remap_index },
   { 43070, ProgramUniformMatrix4x2dv_remap_index },
   {  2469, ProgramUniformMatrix4x2fv_remap_index },
   { 15136, ProgramUniformMatrix4x3dv_remap_index },
   {  8214, ProgramUniformMatrix4x3fv_remap_index },
   { 42455, UnlockArraysEXT_remap_index },
   { 34783, UseProgramStages_remap_index },
   {  1808, ValidateProgramPipeline_remap_index },
   { 18228, DebugMessageCallback_remap_index },
   { 35272, DebugMessageControl_remap_index },
   { 17334, DebugMessageInsert_remap_index },
   {  7780, GetDebugMessageLog_remap_index },
   {  7557, GetObjectLabel_remap_index },
   { 13634, GetObjectPtrLabel_remap_index },
   {  6760, ObjectLabel_remap_index },
   { 48361, ObjectPtrLabel_remap_index },
   { 20076, PopDebugGroup_remap_index },
   { 16011, PushDebugGroup_remap_index },
   {  9419, SecondaryColor3fEXT_remap_index },
   {  8868, SecondaryColor3fvEXT_remap_index },
   { 31950, MultiDrawElementsEXT_remap_index },
   { 12136, FogCoordfEXT_remap_index },
   { 20382, FogCoordfvEXT_remap_index },
   {  4671, ResizeBuffersMESA_remap_index },
   { 37944, WindowPos4dMESA_remap_index },
   { 30453, WindowPos4dvMESA_remap_index },
   {  4829, WindowPos4fMESA_remap_index },
   { 12821, WindowPos4fvMESA_remap_index },
   { 10248, WindowPos4iMESA_remap_index },
   {  4149, WindowPos4ivMESA_remap_index },
   { 31287, WindowPos4sMESA_remap_index },
   {  6738, WindowPos4svMESA_remap_index },
   { 32050, MultiModeDrawArraysIBM_remap_index },
   { 22123, MultiModeDrawElementsIBM_remap_index },
   { 36609, AreProgramsResidentNV_remap_index },
   { 44392, ExecuteProgramNV_remap_index },
   { 32880, GetProgramParameterdvNV_remap_index },
   { 40681, GetProgramParameterfvNV_remap_index },
   { 21527, GetProgramStringNV_remap_index },
   { 17747, GetProgramivNV_remap_index },
   { 20665, GetTrackMatrixivNV_remap_index },
   { 21107, GetVertexAttribdvNV_remap_index },
   { 19190, GetVertexAttribfvNV_remap_index },
   { 18045, GetVertexAttribivNV_remap_index },
   { 40326, LoadProgramNV_remap_index },
   { 22223, ProgramParameters4dvNV_remap_index },
   { 22885, ProgramParameters4fvNV_remap_index },
   {  7094, RequestResidentProgramsNV_remap_index },
   { 32436, TrackMatrixNV_remap_index },
   { 48645, VertexAttrib1dNV_remap_index },
   { 31355, VertexAttrib1dvNV_remap_index },
   { 31721, VertexAttrib1fNV_remap_index },
   { 46555, VertexAttrib1fvNV_remap_index },
   { 23515, VertexAttrib1sNV_remap_index },
   { 42475, VertexAttrib1svNV_remap_index },
   { 20641, VertexAttrib2dNV_remap_index },
   { 38255, VertexAttrib2dvNV_remap_index },
   { 30337, VertexAttrib2fNV_remap_index },
   { 28906, VertexAttrib2fvNV_remap_index },
   { 14355, VertexAttrib2sNV_remap_index },
   {  6499, VertexAttrib2svNV_remap_index },
   { 41090, VertexAttrib3dNV_remap_index },
   { 43179, VertexAttrib3dvNV_remap_index },
   {  5637, VertexAttrib3fNV_remap_index },
   { 45792, VertexAttrib3fvNV_remap_index },
   {  8158, VertexAttrib3sNV_remap_index },
   { 20692, VertexAttrib3svNV_remap_index },
   {  9647, VertexAttrib4dNV_remap_index },
   {  3867, VertexAttrib4dvNV_remap_index },
   {  9718, VertexAttrib4fNV_remap_index },
   { 45930, VertexAttrib4fvNV_remap_index },
   { 19669, VertexAttrib4sNV_remap_index },
   { 12348, VertexAttrib4svNV_remap_index },
   {  1781, VertexAttrib4ubNV_remap_index },
   { 12111, VertexAttrib4ubvNV_remap_index },
   { 32201, VertexAttribPointerNV_remap_index },
   { 30500, VertexAttribs1dvNV_remap_index },
   { 44895, VertexAttribs1fvNV_remap_index },
   {  7034, VertexAttribs1svNV_remap_index },
   { 47271, VertexAttribs2dvNV_remap_index },
   {  8600, VertexAttribs2fvNV_remap_index },
   { 30578, VertexAttribs2svNV_remap_index },
   {  1969, VertexAttribs3dvNV_remap_index },
   { 39577, VertexAttribs3fvNV_remap_index },
   { 15758, VertexAttribs3svNV_remap_index },
   { 27631, VertexAttribs4dvNV_remap_index },
   { 27164, VertexAttribs4fvNV_remap_index },
   { 11497, VertexAttribs4svNV_remap_index },
   { 35884, VertexAttribs4ubvNV_remap_index },
   { 46433, GetTexBumpParameterfvATI_remap_index },
   { 11764, GetTexBumpParameterivATI_remap_index },
   { 39677, TexBumpParameterfvATI_remap_index },
   {  9690, TexBumpParameterivATI_remap_index },
   { 10469, AlphaFragmentOp1ATI_remap_index },
   {  3891, AlphaFragmentOp2ATI_remap_index },
   { 10951, AlphaFragmentOp3ATI_remap_index },
   { 37128, BeginFragmentShaderATI_remap_index },
   {  4196, BindFragmentShaderATI_remap_index },
   {  8183, ColorFragmentOp1ATI_remap_index },
   { 14321, ColorFragmentOp2ATI_remap_index },
   { 26644, ColorFragmentOp3ATI_remap_index },
   { 18989, DeleteFragmentShaderATI_remap_index },
   { 48788, EndFragmentShaderATI_remap_index },
   { 25588, GenFragmentShadersATI_remap_index },
   { 46012, PassTexCoordATI_remap_index },
   { 39657, SampleMapATI_remap_index },
   { 38792, SetFragmentShaderConstantATI_remap_index },
   {  9621, ActiveStencilFaceEXT_remap_index },
   {  9188, BindVertexArrayAPPLE_remap_index },
   { 18494, GenVertexArraysAPPLE_remap_index },
   { 39197, GetProgramNamedParameterdvNV_remap_index },
   { 25338, GetProgramNamedParameterfvNV_remap_index },
   { 46464, ProgramNamedParameter4dNV_remap_index },
   { 42241, ProgramNamedParameter4dvNV_remap_index },
   { 27344, ProgramNamedParameter4fNV_remap_index },
   { 28433, ProgramNamedParameter4fvNV_remap_index },
   { 27071, PrimitiveRestartNV_remap_index },
   { 27609, GetTexGenxvOES_remap_index },
   { 16389, TexGenxOES_remap_index },
   { 36089, TexGenxvOES_remap_index },
   {  9112, DepthBoundsEXT_remap_index },
   { 42362, BindFramebufferEXT_remap_index },
   { 46070, BindRenderbufferEXT_remap_index },
   { 34688, BufferParameteriAPPLE_remap_index },
   { 43901, FlushMappedBufferRangeAPPLE_remap_index },
   { 30847, VertexAttribI1iEXT_remap_index },
   { 12760, VertexAttribI1uiEXT_remap_index },
   { 22556, VertexAttribI2iEXT_remap_index },
   { 45535, VertexAttribI2ivEXT_remap_index },
   { 28564, VertexAttribI2uiEXT_remap_index },
   { 39294, VertexAttribI2uivEXT_remap_index },
   { 21626, VertexAttribI3iEXT_remap_index },
   { 47800, VertexAttribI3ivEXT_remap_index },
   { 25270, VertexAttribI3uiEXT_remap_index },
   { 23419, VertexAttribI3uivEXT_remap_index },
   { 42286, VertexAttribI4iEXT_remap_index },
   {  7687, VertexAttribI4ivEXT_remap_index },
   {  2864, VertexAttribI4uiEXT_remap_index },
   { 30725, VertexAttribI4uivEXT_remap_index },
   {  3400, ClearColorIiEXT_remap_index },
   {  1248, ClearColorIuiEXT_remap_index },
   { 27094, BindBufferOffsetEXT_remap_index },
   { 20414, BeginPerfMonitorAMD_remap_index },
   { 36567, DeletePerfMonitorsAMD_remap_index },
   {  6198, EndPerfMonitorAMD_remap_index },
   { 30069, GenPerfMonitorsAMD_remap_index },
   { 14092, GetPerfMonitorCounterDataAMD_remap_index },
   { 38624, GetPerfMonitorCounterInfoAMD_remap_index },
   { 48748, GetPerfMonitorCounterStringAMD_remap_index },
   { 48326, GetPerfMonitorCountersAMD_remap_index },
   { 16082, GetPerfMonitorGroupStringAMD_remap_index },
   { 33098, GetPerfMonitorGroupsAMD_remap_index },
   { 15516, SelectPerfMonitorCountersAMD_remap_index },
   { 16202, GetObjectParameterivAPPLE_remap_index },
   { 48134, ObjectPurgeableAPPLE_remap_index },
   {  2112, ObjectUnpurgeableAPPLE_remap_index },
   { 47037, ActiveProgramEXT_remap_index },
   { 37983, CreateShaderProgramEXT_remap_index },
   { 41198, UseShaderProgramEXT_remap_index },
   { 33933, TextureBarrierNV_remap_index },
   {  2426, VDPAUFiniNV_remap_index },
   {   875, VDPAUGetSurfaceivNV_remap_index },
   { 26517, VDPAUInitNV_remap_index },
   { 23989, VDPAUIsSurfaceNV_remap_index },
   { 24183, VDPAUMapSurfacesNV_remap_index },
   {  3441, VDPAURegisterOutputSurfaceNV_remap_index },
   { 13990, VDPAURegisterVideoSurfaceNV_remap_index },
   { 12473, VDPAUSurfaceAccessNV_remap_index },
   {  5431, VDPAUUnmapSurfacesNV_remap_index },
   { 42140, VDPAUUnregisterSurfaceNV_remap_index },
   { 10177, BeginPerfQueryINTEL_remap_index },
   { 37449, CreatePerfQueryINTEL_remap_index },
   { 18425, DeletePerfQueryINTEL_remap_index },
   { 45365, EndPerfQueryINTEL_remap_index },
   { 47007, GetFirstPerfQueryIdINTEL_remap_index },
   { 33886, GetNextPerfQueryIdINTEL_remap_index },
   { 35711, GetPerfCounterInfoINTEL_remap_index },
   {   786, GetPerfQueryDataINTEL_remap_index },
   { 24868, GetPerfQueryIdByNameINTEL_remap_index },
   { 22317, GetPerfQueryInfoINTEL_remap_index },
   { 42619, PolygonOffsetClampEXT_remap_index },
   { 22768, StencilFuncSeparateATI_remap_index },
   {  6464, ProgramEnvParameters4fvEXT_remap_index },
   {  7387, ProgramLocalParameters4fvEXT_remap_index },
   {  4337, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  4101, EGLImageTargetTexture2DOES_remap_index },
   { 45502, AlphaFuncx_remap_index },
   { 21671, ClearColorx_remap_index },
   { 44662, ClearDepthx_remap_index },
   { 40076, Color4x_remap_index },
   { 26455, DepthRangex_remap_index },
   {  2654, Fogx_remap_index },
   { 16733, Fogxv_remap_index },
   {  9892, Frustumf_remap_index },
   { 10023, Frustumx_remap_index },
   { 21591, LightModelx_remap_index },
   { 35929, LightModelxv_remap_index },
   { 32255, Lightx_remap_index },
   { 46316, Lightxv_remap_index },
   {  3999, LineWidthx_remap_index },
   {  3756, LoadMatrixx_remap_index },
   { 47366, Materialx_remap_index },
   { 28383, Materialxv_remap_index },
   { 47919, MultMatrixx_remap_index },
   { 11414, MultiTexCoord4x_remap_index },
   { 28215, Normal3x_remap_index },
   { 17387, Orthof_remap_index },
   { 17627, Orthox_remap_index },
   { 30772, PointSizex_remap_index },
   {    70, PolygonOffsetx_remap_index },
   { 41523, Rotatex_remap_index },
   { 22274, SampleCoveragex_remap_index },
   { 14205, Scalex_remap_index },
   { 42733, TexEnvx_remap_index },
   { 48085, TexEnvxv_remap_index },
   {  2234, TexParameterx_remap_index },
   { 35413, Translatex_remap_index },
   { 36907, ClipPlanef_remap_index },
   { 36809, ClipPlanex_remap_index },
   {   747, GetClipPlanef_remap_index },
   {   610, GetClipPlanex_remap_index },
   { 22192, GetFixedv_remap_index },
   {  1307, GetLightxv_remap_index },
   { 25375, GetMaterialxv_remap_index },
   { 23835, GetTexEnvxv_remap_index },
   { 18874, GetTexParameterxv_remap_index },
   { 32508, PointParameterx_remap_index },
   { 41478, PointParameterxv_remap_index },
   { 21316, TexParameterxv_remap_index },
   {    -1, -1 }
};

/* these functions are in the ABI, but have alternative names */
static const struct gl_function_remap MESA_alt_functions[] = {
   /* from GL_EXT_blend_color */
   { 38203, _gloffset_BlendColor },
   /* from GL_EXT_blend_minmax */
   { 41033, _gloffset_BlendEquation },
   /* from GL_EXT_color_subtable */
   {  6121, _gloffset_ColorSubTable },
   { 23736, _gloffset_CopyColorSubTable },
   /* from GL_EXT_convolution */
   {  1341, _gloffset_GetConvolutionParameteriv },
   { 15324, _gloffset_ConvolutionParameterfv },
   { 18711, _gloffset_CopyConvolutionFilter1D },
   { 20828, _gloffset_SeparableFilter2D },
   { 22068, _gloffset_GetConvolutionFilter },
   { 26158, _gloffset_ConvolutionFilter1D },
   { 28771, _gloffset_ConvolutionFilter2D },
   { 31808, _gloffset_GetSeparableFilter },
   { 33544, _gloffset_ConvolutionParameteri },
   { 33666, _gloffset_ConvolutionParameterf },
   { 40105, _gloffset_ConvolutionParameteriv },
   { 46369, _gloffset_GetConvolutionParameterfv },
   { 47651, _gloffset_CopyConvolutionFilter2D },
   /* from GL_EXT_copy_texture */
   { 30391, _gloffset_CopyTexImage2D },
   { 33272, _gloffset_CopyTexImage1D },
   { 35966, _gloffset_CopyTexSubImage1D },
   { 42542, _gloffset_CopyTexSubImage3D },
   { 46625, _gloffset_CopyTexSubImage2D },
   /* from GL_EXT_draw_range_elements */
   { 27558, _gloffset_DrawRangeElements },
   /* from GL_EXT_histogram */
   {  5330, _gloffset_GetHistogramParameterfv },
   {  9214, _gloffset_GetHistogramParameteriv },
   { 10735, _gloffset_Minmax },
   { 15698, _gloffset_GetMinmax },
   { 24385, _gloffset_Histogram },
   { 33200, _gloffset_GetMinmaxParameteriv },
   { 34345, _gloffset_ResetMinmax },
   { 35373, _gloffset_GetHistogram },
   { 37222, _gloffset_GetMinmaxParameterfv },
   { 38314, _gloffset_ResetHistogram },
   /* from GL_EXT_paletted_texture */
   { 15186, _gloffset_ColorTable },
   { 20439, _gloffset_GetColorTableParameterfv },
   { 29660, _gloffset_GetColorTable },
   { 33722, _gloffset_GetColorTableParameteriv },
   /* from GL_EXT_subtexture */
   {  2686, _gloffset_TexSubImage1D },
   { 40748, _gloffset_TexSubImage2D },
   /* from GL_EXT_texture3D */
   { 24811, _gloffset_TexImage3D },
   { 45176, _gloffset_TexSubImage3D },
   /* from GL_EXT_texture_object */
   {  4717, _gloffset_GenTextures },
   { 10070, _gloffset_BindTexture },
   { 19533, _gloffset_IsTexture },
   { 25220, _gloffset_PrioritizeTextures },
   { 29889, _gloffset_DeleteTextures },
   { 45632, _gloffset_AreTexturesResident },
   /* from GL_EXT_vertex_array */
   { 20926, _gloffset_ArrayElement },
   { 32551, _gloffset_DrawArrays },
   { 43262, _gloffset_GetPointerv },
   /* from GL_NV_read_buffer */
   { 33513, _gloffset_ReadBuffer },
   /* from GL_OES_blend_subtract */
   { 41033, _gloffset_BlendEquation },
   /* from GL_OES_texture_3D */
   { 24811, _gloffset_TexImage3D },
   { 42542, _gloffset_CopyTexSubImage3D },
   { 45176, _gloffset_TexSubImage3D },
   /* from GL_OES_texture_cube_map */
   { 19119, _gloffset_TexGeni },
   { 19147, _gloffset_TexGenf },
   { 22657, _gloffset_GetTexGenfv },
   { 38154, _gloffset_TexGeniv },
   { 40857, _gloffset_TexGenfv },
   { 46971, _gloffset_GetTexGeniv },
   /* from GL_SGI_color_table */
   {  3024, _gloffset_ColorTableParameteriv },
   { 15186, _gloffset_ColorTable },
   { 19477, _gloffset_ColorTableParameterfv },
   { 20439, _gloffset_GetColorTableParameterfv },
   { 29660, _gloffset_GetColorTable },
   { 29738, _gloffset_CopyColorTable },
   { 33722, _gloffset_GetColorTableParameteriv },
   {    -1, -1 }
};

