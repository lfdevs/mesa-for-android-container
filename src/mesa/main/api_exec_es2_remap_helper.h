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
   /* _mesa_function_pool[25]: MapBufferOES (will be remapped) */
   "ii\0"
   "glMapBufferOES\0"
   "\0"
   /* _mesa_function_pool[44]: DeleteShader (will be remapped) */
   "i\0"
   "glDeleteShader\0"
   "\0"
   /* _mesa_function_pool[62]: VertexAttrib2f (will be remapped) */
   "iff\0"
   "glVertexAttrib2f\0"
   "\0"
   /* _mesa_function_pool[84]: Uniform2f (will be remapped) */
   "iff\0"
   "glUniform2f\0"
   "\0"
   /* _mesa_function_pool[101]: CompileShader (will be remapped) */
   "i\0"
   "glCompileShader\0"
   "\0"
   /* _mesa_function_pool[120]: GetProgramiv (will be remapped) */
   "iip\0"
   "glGetProgramiv\0"
   "\0"
   /* _mesa_function_pool[140]: LineWidth (offset 168) */
   "f\0"
   "glLineWidth\0"
   "\0"
   /* _mesa_function_pool[155]: GetBufferPointervOES (will be remapped) */
   "iip\0"
   "glGetBufferPointervOES\0"
   "\0"
   /* _mesa_function_pool[183]: Uniform2i (will be remapped) */
   "iii\0"
   "glUniform2i\0"
   "\0"
   /* _mesa_function_pool[200]: Finish (offset 216) */
   "\0"
   "glFinish\0"
   "\0"
   /* _mesa_function_pool[211]: ClearStencil (offset 207) */
   "i\0"
   "glClearStencil\0"
   "\0"
   /* _mesa_function_pool[229]: Uniform1fv (will be remapped) */
   "iip\0"
   "glUniform1fv\0"
   "\0"
   /* _mesa_function_pool[247]: GenBuffers (will be remapped) */
   "ip\0"
   "glGenBuffers\0"
   "\0"
   /* _mesa_function_pool[264]: GetProgramInfoLog (will be remapped) */
   "iipp\0"
   "glGetProgramInfoLog\0"
   "\0"
   /* _mesa_function_pool[290]: Uniform2fv (will be remapped) */
   "iip\0"
   "glUniform2fv\0"
   "\0"
   /* _mesa_function_pool[308]: EnableVertexAttribArray (will be remapped) */
   "i\0"
   "glEnableVertexAttribArray\0"
   "\0"
   /* _mesa_function_pool[337]: GetAttachedShaders (will be remapped) */
   "iipp\0"
   "glGetAttachedShaders\0"
   "\0"
   /* _mesa_function_pool[364]: TexImage3DOES (offset 371) */
   "iiiiiiiiip\0"
   "glTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[392]: VertexAttrib4fv (will be remapped) */
   "ip\0"
   "glVertexAttrib4fv\0"
   "\0"
   /* _mesa_function_pool[414]: IsEnabled (offset 286) */
   "i\0"
   "glIsEnabled\0"
   "\0"
   /* _mesa_function_pool[429]: LinkProgram (will be remapped) */
   "i\0"
   "glLinkProgram\0"
   "\0"
   /* _mesa_function_pool[446]: DeleteProgram (will be remapped) */
   "i\0"
   "glDeleteProgram\0"
   "\0"
   /* _mesa_function_pool[465]: CompressedTexSubImage3DOES (will be remapped) */
   "iiiiiiiiiip\0"
   "glCompressedTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[507]: Uniform1f (will be remapped) */
   "if\0"
   "glUniform1f\0"
   "\0"
   /* _mesa_function_pool[523]: DetachShader (will be remapped) */
   "ii\0"
   "glDetachShader\0"
   "\0"
   /* _mesa_function_pool[542]: Uniform1i (will be remapped) */
   "ii\0"
   "glUniform1i\0"
   "\0"
   /* _mesa_function_pool[558]: CopyTexSubImage2D (offset 326) */
   "iiiiiiii\0"
   "glCopyTexSubImage2D\0"
   "\0"
   /* _mesa_function_pool[588]: SampleCoverage (will be remapped) */
   "fi\0"
   "glSampleCoverage\0"
   "\0"
   /* _mesa_function_pool[609]: VertexAttrib4f (will be remapped) */
   "iffff\0"
   "glVertexAttrib4f\0"
   "\0"
   /* _mesa_function_pool[633]: DrawElements (offset 311) */
   "iiip\0"
   "glDrawElements\0"
   "\0"
   /* _mesa_function_pool[654]: GetShaderiv (will be remapped) */
   "iip\0"
   "glGetShaderiv\0"
   "\0"
   /* _mesa_function_pool[673]: BindFramebuffer (will be remapped) */
   "ii\0"
   "glBindFramebuffer\0"
   "\0"
   /* _mesa_function_pool[695]: Uniform4iv (will be remapped) */
   "iip\0"
   "glUniform4iv\0"
   "\0"
   /* _mesa_function_pool[713]: ReleaseShaderCompiler (will be remapped) */
   "\0"
   "glReleaseShaderCompiler\0"
   "\0"
   /* _mesa_function_pool[739]: BlendEquation (offset 337) */
   "i\0"
   "glBlendEquation\0"
   "\0"
   /* _mesa_function_pool[758]: StencilFuncSeparate (will be remapped) */
   "iiii\0"
   "glStencilFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[786]: ShaderSource (will be remapped) */
   "iipp\0"
   "glShaderSource\0"
   "\0"
   /* _mesa_function_pool[807]: Uniform2iv (will be remapped) */
   "iip\0"
   "glUniform2iv\0"
   "\0"
   /* _mesa_function_pool[825]: BindRenderbuffer (will be remapped) */
   "ii\0"
   "glBindRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[848]: UseProgram (will be remapped) */
   "i\0"
   "glUseProgram\0"
   "\0"
   /* _mesa_function_pool[864]: CreateShader (will be remapped) */
   "i\0"
   "glCreateShader\0"
   "\0"
   /* _mesa_function_pool[882]: GetVertexAttribfv (will be remapped) */
   "iip\0"
   "glGetVertexAttribfv\0"
   "\0"
   /* _mesa_function_pool[907]: DeleteFramebuffers (will be remapped) */
   "ip\0"
   "glDeleteFramebuffers\0"
   "\0"
   /* _mesa_function_pool[932]: BufferData (will be remapped) */
   "iipi\0"
   "glBufferData\0"
   "\0"
   /* _mesa_function_pool[951]: RenderbufferStorage (will be remapped) */
   "iiii\0"
   "glRenderbufferStorage\0"
   "\0"
   /* _mesa_function_pool[979]: BindTexture (offset 307) */
   "ii\0"
   "glBindTexture\0"
   "\0"
   /* _mesa_function_pool[997]: FramebufferRenderbuffer (will be remapped) */
   "iiii\0"
   "glFramebufferRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[1029]: ClearColor (offset 206) */
   "ffff\0"
   "glClearColor\0"
   "\0"
   /* _mesa_function_pool[1048]: TexSubImage2D (offset 333) */
   "iiiiiiiip\0"
   "glTexSubImage2D\0"
   "\0"
   /* _mesa_function_pool[1075]: Uniform3i (will be remapped) */
   "iiii\0"
   "glUniform3i\0"
   "\0"
   /* _mesa_function_pool[1093]: Enable (offset 215) */
   "i\0"
   "glEnable\0"
   "\0"
   /* _mesa_function_pool[1105]: BlendColor (offset 336) */
   "ffff\0"
   "glBlendColor\0"
   "\0"
   /* _mesa_function_pool[1124]: UniformMatrix3fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix3fv\0"
   "\0"
   /* _mesa_function_pool[1149]: GetVertexAttribiv (will be remapped) */
   "iip\0"
   "glGetVertexAttribiv\0"
   "\0"
   /* _mesa_function_pool[1174]: ShaderBinary (will be remapped) */
   "ipipi\0"
   "glShaderBinary\0"
   "\0"
   /* _mesa_function_pool[1196]: Uniform3f (will be remapped) */
   "ifff\0"
   "glUniform3f\0"
   "\0"
   /* _mesa_function_pool[1214]: IsBuffer (will be remapped) */
   "i\0"
   "glIsBuffer\0"
   "\0"
   /* _mesa_function_pool[1228]: GetUniformLocation (will be remapped) */
   "ip\0"
   "glGetUniformLocation\0"
   "\0"
   /* _mesa_function_pool[1253]: PixelStorei (offset 250) */
   "ii\0"
   "glPixelStorei\0"
   "\0"
   /* _mesa_function_pool[1271]: Viewport (offset 305) */
   "iiii\0"
   "glViewport\0"
   "\0"
   /* _mesa_function_pool[1288]: BlendFuncSeparate (will be remapped) */
   "iiii\0"
   "glBlendFuncSeparate\0"
   "\0"
   /* _mesa_function_pool[1314]: TexSubImage3DOES (offset 372) */
   "iiiiiiiiiip\0"
   "glTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[1346]: GetError (offset 261) */
   "\0"
   "glGetError\0"
   "\0"
   /* _mesa_function_pool[1359]: DrawArrays (offset 310) */
   "iii\0"
   "glDrawArrays\0"
   "\0"
   /* _mesa_function_pool[1377]: VertexAttrib3f (will be remapped) */
   "ifff\0"
   "glVertexAttrib3f\0"
   "\0"
   /* _mesa_function_pool[1400]: DeleteRenderbuffers (will be remapped) */
   "ip\0"
   "glDeleteRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[1426]: CompressedTexSubImage2D (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexSubImage2D\0"
   "\0"
   /* _mesa_function_pool[1463]: CopyTexSubImage3DOES (offset 373) */
   "iiiiiiiii\0"
   "glCopyTexSubImage3DOES\0"
   "\0"
   /* _mesa_function_pool[1497]: StencilMask (offset 209) */
   "i\0"
   "glStencilMask\0"
   "\0"
   /* _mesa_function_pool[1514]: EGLImageTargetTexture2DOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetTexture2DOES\0"
   "\0"
   /* _mesa_function_pool[1547]: MultiDrawElementsEXT (will be remapped) */
   "ipipi\0"
   "glMultiDrawElementsEXT\0"
   "\0"
   /* _mesa_function_pool[1577]: StencilMaskSeparate (will be remapped) */
   "ii\0"
   "glStencilMaskSeparate\0"
   "\0"
   /* _mesa_function_pool[1603]: CheckFramebufferStatus (will be remapped) */
   "i\0"
   "glCheckFramebufferStatus\0"
   "\0"
   /* _mesa_function_pool[1631]: DepthMask (offset 211) */
   "i\0"
   "glDepthMask\0"
   "\0"
   /* _mesa_function_pool[1646]: IsShader (will be remapped) */
   "i\0"
   "glIsShader\0"
   "\0"
   /* _mesa_function_pool[1660]: StencilOp (offset 244) */
   "iii\0"
   "glStencilOp\0"
   "\0"
   /* _mesa_function_pool[1677]: GetShaderInfoLog (will be remapped) */
   "iipp\0"
   "glGetShaderInfoLog\0"
   "\0"
   /* _mesa_function_pool[1702]: EGLImageTargetRenderbufferStorageOES (will be remapped) */
   "ip\0"
   "glEGLImageTargetRenderbufferStorageOES\0"
   "\0"
   /* _mesa_function_pool[1745]: BindBuffer (will be remapped) */
   "ii\0"
   "glBindBuffer\0"
   "\0"
   /* _mesa_function_pool[1762]: Uniform1iv (will be remapped) */
   "iip\0"
   "glUniform1iv\0"
   "\0"
   /* _mesa_function_pool[1780]: GenFramebuffers (will be remapped) */
   "ip\0"
   "glGenFramebuffers\0"
   "\0"
   /* _mesa_function_pool[1802]: ClearDepthf (will be remapped) */
   "f\0"
   "glClearDepthf\0"
   "\0"
   /* _mesa_function_pool[1819]: FramebufferTexture2D (will be remapped) */
   "iiiii\0"
   "glFramebufferTexture2D\0"
   "\0"
   /* _mesa_function_pool[1849]: IsFramebuffer (will be remapped) */
   "i\0"
   "glIsFramebuffer\0"
   "\0"
   /* _mesa_function_pool[1868]: StencilFunc (offset 243) */
   "iii\0"
   "glStencilFunc\0"
   "\0"
   /* _mesa_function_pool[1887]: CullFace (offset 152) */
   "i\0"
   "glCullFace\0"
   "\0"
   /* _mesa_function_pool[1901]: CompressedTexImage3DOES (will be remapped) */
   "iiiiiiiip\0"
   "glCompressedTexImage3DOES\0"
   "\0"
   /* _mesa_function_pool[1938]: BlendEquationSeparate (will be remapped) */
   "ii\0"
   "glBlendEquationSeparate\0"
   "\0"
   /* _mesa_function_pool[1966]: Uniform4fv (will be remapped) */
   "iip\0"
   "glUniform4fv\0"
   "\0"
   /* _mesa_function_pool[1984]: ActiveTexture (offset 374) */
   "i\0"
   "glActiveTexture\0"
   "\0"
   /* _mesa_function_pool[2003]: IsProgram (will be remapped) */
   "i\0"
   "glIsProgram\0"
   "\0"
   /* _mesa_function_pool[2018]: VertexAttrib1f (will be remapped) */
   "if\0"
   "glVertexAttrib1f\0"
   "\0"
   /* _mesa_function_pool[2039]: Hint (offset 158) */
   "ii\0"
   "glHint\0"
   "\0"
   /* _mesa_function_pool[2050]: GenTextures (offset 328) */
   "ip\0"
   "glGenTextures\0"
   "\0"
   /* _mesa_function_pool[2068]: TexParameterfv (offset 179) */
   "iip\0"
   "glTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[2090]: BindAttribLocation (will be remapped) */
   "iip\0"
   "glBindAttribLocation\0"
   "\0"
   /* _mesa_function_pool[2116]: GetProgramBinaryOES (will be remapped) */
   "iippp\0"
   "glGetProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[2145]: VertexAttribPointer (will be remapped) */
   "iiiiip\0"
   "glVertexAttribPointer\0"
   "\0"
   /* _mesa_function_pool[2175]: GetAttribLocation (will be remapped) */
   "ip\0"
   "glGetAttribLocation\0"
   "\0"
   /* _mesa_function_pool[2199]: ReadPixels (offset 256) */
   "iiiiiip\0"
   "glReadPixels\0"
   "\0"
   /* _mesa_function_pool[2221]: GetFloatv (offset 262) */
   "ip\0"
   "glGetFloatv\0"
   "\0"
   /* _mesa_function_pool[2237]: BlendFunc (offset 241) */
   "ii\0"
   "glBlendFunc\0"
   "\0"
   /* _mesa_function_pool[2253]: Uniform4i (will be remapped) */
   "iiiii\0"
   "glUniform4i\0"
   "\0"
   /* _mesa_function_pool[2272]: FramebufferTexture3DOES (will be remapped) */
   "iiiiii\0"
   "glFramebufferTexture3DOES\0"
   "\0"
   /* _mesa_function_pool[2306]: Uniform4f (will be remapped) */
   "iffff\0"
   "glUniform4f\0"
   "\0"
   /* _mesa_function_pool[2325]: GetShaderPrecisionFormat (will be remapped) */
   "iipp\0"
   "glGetShaderPrecisionFormat\0"
   "\0"
   /* _mesa_function_pool[2358]: IsRenderbuffer (will be remapped) */
   "i\0"
   "glIsRenderbuffer\0"
   "\0"
   /* _mesa_function_pool[2378]: Flush (offset 217) */
   "\0"
   "glFlush\0"
   "\0"
   /* _mesa_function_pool[2388]: UnmapBufferOES (will be remapped) */
   "i\0"
   "glUnmapBufferOES\0"
   "\0"
   /* _mesa_function_pool[2408]: GetIntegerv (offset 263) */
   "ip\0"
   "glGetIntegerv\0"
   "\0"
   /* _mesa_function_pool[2426]: GetUniformfv (will be remapped) */
   "iip\0"
   "glGetUniformfv\0"
   "\0"
   /* _mesa_function_pool[2446]: Disable (offset 214) */
   "i\0"
   "glDisable\0"
   "\0"
   /* _mesa_function_pool[2459]: GetShaderSource (will be remapped) */
   "iipp\0"
   "glGetShaderSource\0"
   "\0"
   /* _mesa_function_pool[2483]: DrawBuffersNV (will be remapped) */
   "ip\0"
   "glDrawBuffersNV\0"
   "\0"
   /* _mesa_function_pool[2503]: Uniform3iv (will be remapped) */
   "iip\0"
   "glUniform3iv\0"
   "\0"
   /* _mesa_function_pool[2521]: GetBooleanv (offset 258) */
   "ip\0"
   "glGetBooleanv\0"
   "\0"
   /* _mesa_function_pool[2539]: TexParameteriv (offset 181) */
   "iip\0"
   "glTexParameteriv\0"
   "\0"
   /* _mesa_function_pool[2561]: DisableVertexAttribArray (will be remapped) */
   "i\0"
   "glDisableVertexAttribArray\0"
   "\0"
   /* _mesa_function_pool[2591]: GetActiveUniform (will be remapped) */
   "iiipppp\0"
   "glGetActiveUniform\0"
   "\0"
   /* _mesa_function_pool[2619]: DeleteBuffers (will be remapped) */
   "ip\0"
   "glDeleteBuffers\0"
   "\0"
   /* _mesa_function_pool[2639]: IsTexture (offset 330) */
   "i\0"
   "glIsTexture\0"
   "\0"
   /* _mesa_function_pool[2654]: TexParameterf (offset 178) */
   "iif\0"
   "glTexParameterf\0"
   "\0"
   /* _mesa_function_pool[2675]: TexParameteri (offset 180) */
   "iii\0"
   "glTexParameteri\0"
   "\0"
   /* _mesa_function_pool[2696]: BufferSubData (will be remapped) */
   "iiip\0"
   "glBufferSubData\0"
   "\0"
   /* _mesa_function_pool[2718]: VertexAttrib2fv (will be remapped) */
   "ip\0"
   "glVertexAttrib2fv\0"
   "\0"
   /* _mesa_function_pool[2740]: GetString (offset 275) */
   "i\0"
   "glGetString\0"
   "\0"
   /* _mesa_function_pool[2755]: TexImage2D (offset 183) */
   "iiiiiiiip\0"
   "glTexImage2D\0"
   "\0"
   /* _mesa_function_pool[2779]: UniformMatrix2fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix2fv\0"
   "\0"
   /* _mesa_function_pool[2804]: GetTexParameterfv (offset 282) */
   "iip\0"
   "glGetTexParameterfv\0"
   "\0"
   /* _mesa_function_pool[2829]: VertexAttrib3fv (will be remapped) */
   "ip\0"
   "glVertexAttrib3fv\0"
   "\0"
   /* _mesa_function_pool[2851]: GetUniformiv (will be remapped) */
   "iip\0"
   "glGetUniformiv\0"
   "\0"
   /* _mesa_function_pool[2871]: FrontFace (offset 157) */
   "i\0"
   "glFrontFace\0"
   "\0"
   /* _mesa_function_pool[2886]: DeleteTextures (offset 327) */
   "ip\0"
   "glDeleteTextures\0"
   "\0"
   /* _mesa_function_pool[2907]: CreateProgram (will be remapped) */
   "\0"
   "glCreateProgram\0"
   "\0"
   /* _mesa_function_pool[2925]: DepthRangef (will be remapped) */
   "ff\0"
   "glDepthRangef\0"
   "\0"
   /* _mesa_function_pool[2943]: ColorMask (offset 210) */
   "iiii\0"
   "glColorMask\0"
   "\0"
   /* _mesa_function_pool[2961]: Uniform3fv (will be remapped) */
   "iip\0"
   "glUniform3fv\0"
   "\0"
   /* _mesa_function_pool[2979]: Clear (offset 203) */
   "i\0"
   "glClear\0"
   "\0"
   /* _mesa_function_pool[2990]: ValidateProgram (will be remapped) */
   "i\0"
   "glValidateProgram\0"
   "\0"
   /* _mesa_function_pool[3011]: Scissor (offset 176) */
   "iiii\0"
   "glScissor\0"
   "\0"
   /* _mesa_function_pool[3027]: GetRenderbufferParameteriv (will be remapped) */
   "iip\0"
   "glGetRenderbufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[3061]: PolygonOffset (offset 319) */
   "ff\0"
   "glPolygonOffset\0"
   "\0"
   /* _mesa_function_pool[3081]: GetFramebufferAttachmentParameteriv (will be remapped) */
   "iiip\0"
   "glGetFramebufferAttachmentParameteriv\0"
   "\0"
   /* _mesa_function_pool[3125]: GetBufferParameteriv (will be remapped) */
   "iip\0"
   "glGetBufferParameteriv\0"
   "\0"
   /* _mesa_function_pool[3153]: GenRenderbuffers (will be remapped) */
   "ip\0"
   "glGenRenderbuffers\0"
   "\0"
   /* _mesa_function_pool[3176]: UniformMatrix4fv (will be remapped) */
   "iiip\0"
   "glUniformMatrix4fv\0"
   "\0"
   /* _mesa_function_pool[3201]: GetActiveAttrib (will be remapped) */
   "iiipppp\0"
   "glGetActiveAttrib\0"
   "\0"
   /* _mesa_function_pool[3228]: GenerateMipmap (will be remapped) */
   "i\0"
   "glGenerateMipmap\0"
   "\0"
   /* _mesa_function_pool[3248]: DepthFunc (offset 245) */
   "i\0"
   "glDepthFunc\0"
   "\0"
   /* _mesa_function_pool[3263]: ProgramBinaryOES (will be remapped) */
   "iipi\0"
   "glProgramBinaryOES\0"
   "\0"
   /* _mesa_function_pool[3288]: VertexAttrib1fv (will be remapped) */
   "ip\0"
   "glVertexAttrib1fv\0"
   "\0"
   /* _mesa_function_pool[3310]: CompressedTexImage2D (will be remapped) */
   "iiiiiiip\0"
   "glCompressedTexImage2D\0"
   "\0"
   /* _mesa_function_pool[3343]: CopyTexImage2D (offset 324) */
   "iiiiiiii\0"
   "glCopyTexImage2D\0"
   "\0"
   /* _mesa_function_pool[3370]: AttachShader (will be remapped) */
   "ii\0"
   "glAttachShader\0"
   "\0"
   /* _mesa_function_pool[3389]: MultiDrawArraysEXT (will be remapped) */
   "ippi\0"
   "glMultiDrawArraysEXT\0"
   "\0"
   /* _mesa_function_pool[3416]: StencilOpSeparate (will be remapped) */
   "iiii\0"
   "glStencilOpSeparate\0"
   "\0"
   /* _mesa_function_pool[3442]: GetVertexAttribPointerv (will be remapped) */
   "iip\0"
   "glGetVertexAttribPointerv\0"
   "\0"
   ;

/* these functions need to be remapped */
static const struct gl_function_pool_remap MESA_remap_table_functions[] = {
   {  3370, AttachShader_remap_index },
   {  2907, CreateProgram_remap_index },
   {   864, CreateShader_remap_index },
   {   446, DeleteProgram_remap_index },
   {    44, DeleteShader_remap_index },
   {   523, DetachShader_remap_index },
   {   337, GetAttachedShaders_remap_index },
   {   264, GetProgramInfoLog_remap_index },
   {   120, GetProgramiv_remap_index },
   {  1677, GetShaderInfoLog_remap_index },
   {   654, GetShaderiv_remap_index },
   {  2003, IsProgram_remap_index },
   {  1646, IsShader_remap_index },
   {   758, StencilFuncSeparate_remap_index },
   {  1577, StencilMaskSeparate_remap_index },
   {  3416, StencilOpSeparate_remap_index },
   {   588, SampleCoverage_remap_index },
   {  3310, CompressedTexImage2D_remap_index },
   {  1901, CompressedTexImage3DOES_remap_index },
   {  1426, CompressedTexSubImage2D_remap_index },
   {   465, CompressedTexSubImage3DOES_remap_index },
   {  2561, DisableVertexAttribArray_remap_index },
   {   308, EnableVertexAttribArray_remap_index },
   {   882, GetVertexAttribfv_remap_index },
   {  1149, GetVertexAttribiv_remap_index },
   {  2018, VertexAttrib1f_remap_index },
   {  3288, VertexAttrib1fv_remap_index },
   {    62, VertexAttrib2f_remap_index },
   {  2718, VertexAttrib2fv_remap_index },
   {  1377, VertexAttrib3f_remap_index },
   {  2829, VertexAttrib3fv_remap_index },
   {   609, VertexAttrib4f_remap_index },
   {   392, VertexAttrib4fv_remap_index },
   {  2145, VertexAttribPointer_remap_index },
   {  1745, BindBuffer_remap_index },
   {   932, BufferData_remap_index },
   {  2696, BufferSubData_remap_index },
   {  2619, DeleteBuffers_remap_index },
   {   247, GenBuffers_remap_index },
   {  3125, GetBufferParameteriv_remap_index },
   {   155, GetBufferPointervOES_remap_index },
   {  1214, IsBuffer_remap_index },
   {    25, MapBufferOES_remap_index },
   {  2388, UnmapBufferOES_remap_index },
   {   101, CompileShader_remap_index },
   {  2591, GetActiveUniform_remap_index },
   {  2459, GetShaderSource_remap_index },
   {  1228, GetUniformLocation_remap_index },
   {  2426, GetUniformfv_remap_index },
   {  2851, GetUniformiv_remap_index },
   {   429, LinkProgram_remap_index },
   {   786, ShaderSource_remap_index },
   {   507, Uniform1f_remap_index },
   {   229, Uniform1fv_remap_index },
   {   542, Uniform1i_remap_index },
   {  1762, Uniform1iv_remap_index },
   {    84, Uniform2f_remap_index },
   {   290, Uniform2fv_remap_index },
   {   183, Uniform2i_remap_index },
   {   807, Uniform2iv_remap_index },
   {  1196, Uniform3f_remap_index },
   {  2961, Uniform3fv_remap_index },
   {  1075, Uniform3i_remap_index },
   {  2503, Uniform3iv_remap_index },
   {  2306, Uniform4f_remap_index },
   {  1966, Uniform4fv_remap_index },
   {  2253, Uniform4i_remap_index },
   {   695, Uniform4iv_remap_index },
   {  2779, UniformMatrix2fv_remap_index },
   {  1124, UniformMatrix3fv_remap_index },
   {  3176, UniformMatrix4fv_remap_index },
   {   848, UseProgram_remap_index },
   {  2990, ValidateProgram_remap_index },
   {  2090, BindAttribLocation_remap_index },
   {  3201, GetActiveAttrib_remap_index },
   {  2175, GetAttribLocation_remap_index },
   {  2483, DrawBuffersNV_remap_index },
   {  1802, ClearDepthf_remap_index },
   {  2925, DepthRangef_remap_index },
   {  2325, GetShaderPrecisionFormat_remap_index },
   {   713, ReleaseShaderCompiler_remap_index },
   {  1174, ShaderBinary_remap_index },
   {  2116, GetProgramBinaryOES_remap_index },
   {  3263, ProgramBinaryOES_remap_index },
   {  3389, MultiDrawArraysEXT_remap_index },
   {  1547, MultiDrawElementsEXT_remap_index },
   {  1288, BlendFuncSeparate_remap_index },
   {  3442, GetVertexAttribPointerv_remap_index },
   {  1938, BlendEquationSeparate_remap_index },
   {   673, BindFramebuffer_remap_index },
   {   825, BindRenderbuffer_remap_index },
   {  1603, CheckFramebufferStatus_remap_index },
   {   907, DeleteFramebuffers_remap_index },
   {  1400, DeleteRenderbuffers_remap_index },
   {   997, FramebufferRenderbuffer_remap_index },
   {  1819, FramebufferTexture2D_remap_index },
   {  2272, FramebufferTexture3DOES_remap_index },
   {  1780, GenFramebuffers_remap_index },
   {  3153, GenRenderbuffers_remap_index },
   {  3228, GenerateMipmap_remap_index },
   {  3081, GetFramebufferAttachmentParameteriv_remap_index },
   {  3027, GetRenderbufferParameteriv_remap_index },
   {  1849, IsFramebuffer_remap_index },
   {  2358, IsRenderbuffer_remap_index },
   {   951, RenderbufferStorage_remap_index },
   {  1702, EGLImageTargetRenderbufferStorageOES_remap_index },
   {  1514, EGLImageTargetTexture2DOES_remap_index },
   {    -1, -1 }
};

/* these functions are in the ABI, but have alternative names */
static const struct gl_function_remap MESA_alt_functions[] = {
   {    -1, -1 }
};

