/* DO NOT EDIT - This file generated automatically by gl_table.py (from Mesa) script */

/*
 * (C) Copyright IBM Corporation 2005
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
 * IBM,
 * AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined( _DISPATCH_H_ )
#  define _DISPATCH_H_


/**
 * \file main/dispatch.h
 * Macros for handling GL dispatch tables.
 *
 * For each known GL function, there are 3 macros in this file.  The first
 * macro is named CALL_FuncName and is used to call that GL function using
 * the specified dispatch table.  The other 2 macros, called GET_FuncName
 * can SET_FuncName, are used to get and set the dispatch pointer for the
 * named function in the specified dispatch table.
 */

/* GLXEXT is defined when building the GLX extension in the xserver.
 */
#if !defined(GLXEXT)
#include "main/mfeatures.h"
#endif

#define CALL_by_offset(disp, cast, offset, parameters) \
    (*(cast (GET_by_offset(disp, offset)))) parameters
#define GET_by_offset(disp, offset) \
    (offset >= 0) ? (((_glapi_proc *)(disp))[offset]) : NULL
#define SET_by_offset(disp, offset, fn) \
    do { \
        if ( (offset) < 0 ) { \
            /* fprintf( stderr, "[%s:%u] SET_by_offset(%p, %d, %s)!\n", */ \
            /*         __func__, __LINE__, disp, offset, # fn); */ \
            /* abort(); */ \
        } \
        else { \
            ( (_glapi_proc *) (disp) )[offset] = (_glapi_proc) fn; \
        } \
    } while(0)

/* total number of offsets below */
#define _gloffset_COUNT 158

#define _gloffset_CullFace 152
#define _gloffset_FrontFace 157
#define _gloffset_Hint 158
#define _gloffset_LineWidth 168
#define _gloffset_Scissor 176
#define _gloffset_TexParameterf 178
#define _gloffset_TexParameterfv 179
#define _gloffset_TexParameteri 180
#define _gloffset_TexParameteriv 181
#define _gloffset_TexImage2D 183
#define _gloffset_Clear 203
#define _gloffset_ClearColor 206
#define _gloffset_ClearStencil 207
#define _gloffset_StencilMask 209
#define _gloffset_ColorMask 210
#define _gloffset_DepthMask 211
#define _gloffset_Disable 214
#define _gloffset_Enable 215
#define _gloffset_Finish 216
#define _gloffset_Flush 217
#define _gloffset_BlendFunc 241
#define _gloffset_StencilFunc 243
#define _gloffset_StencilOp 244
#define _gloffset_DepthFunc 245
#define _gloffset_PixelStorei 250
#define _gloffset_ReadPixels 256
#define _gloffset_GetBooleanv 258
#define _gloffset_GetError 261
#define _gloffset_GetFloatv 262
#define _gloffset_GetIntegerv 263
#define _gloffset_GetString 275
#define _gloffset_GetTexParameterfv 282
#define _gloffset_GetTexParameteriv 283
#define _gloffset_IsEnabled 286
#define _gloffset_Viewport 305
#define _gloffset_BindTexture 307
#define _gloffset_DrawArrays 310
#define _gloffset_DrawElements 311
#define _gloffset_PolygonOffset 319
#define _gloffset_CopyTexImage2D 324
#define _gloffset_CopyTexSubImage2D 326
#define _gloffset_DeleteTextures 327
#define _gloffset_GenTextures 328
#define _gloffset_IsTexture 330
#define _gloffset_TexSubImage2D 333
#define _gloffset_BlendColor 336
#define _gloffset_BlendEquation 337
#define _gloffset_TexImage3DOES 371
#define _gloffset_TexSubImage3DOES 372
#define _gloffset_CopyTexSubImage3DOES 373
#define _gloffset_ActiveTexture 374

#if !FEATURE_remap_table

#define _gloffset_AttachShader 408
#define _gloffset_CreateProgram 409
#define _gloffset_CreateShader 410
#define _gloffset_DeleteProgram 411
#define _gloffset_DeleteShader 412
#define _gloffset_DetachShader 413
#define _gloffset_GetAttachedShaders 414
#define _gloffset_GetProgramInfoLog 415
#define _gloffset_GetProgramiv 416
#define _gloffset_GetShaderInfoLog 417
#define _gloffset_GetShaderiv 418
#define _gloffset_IsProgram 419
#define _gloffset_IsShader 420
#define _gloffset_StencilFuncSeparate 421
#define _gloffset_StencilMaskSeparate 422
#define _gloffset_StencilOpSeparate 423
#define _gloffset_SampleCoverage 445
#define _gloffset_CompressedTexImage2D 447
#define _gloffset_CompressedTexImage3DOES 448
#define _gloffset_CompressedTexSubImage2D 450
#define _gloffset_CompressedTexSubImage3DOES 451
#define _gloffset_DisableVertexAttribArray 453
#define _gloffset_EnableVertexAttribArray 454
#define _gloffset_GetVertexAttribfv 462
#define _gloffset_GetVertexAttribiv 463
#define _gloffset_VertexAttrib1f 475
#define _gloffset_VertexAttrib1fv 476
#define _gloffset_VertexAttrib2f 481
#define _gloffset_VertexAttrib2fv 482
#define _gloffset_VertexAttrib3f 487
#define _gloffset_VertexAttrib3fv 488
#define _gloffset_VertexAttrib4f 501
#define _gloffset_VertexAttrib4fv 502
#define _gloffset_VertexAttribPointer 509
#define _gloffset_BindBuffer 510
#define _gloffset_BufferData 511
#define _gloffset_BufferSubData 512
#define _gloffset_DeleteBuffers 513
#define _gloffset_GenBuffers 514
#define _gloffset_GetBufferParameteriv 515
#define _gloffset_GetBufferPointervOES 516
#define _gloffset_IsBuffer 518
#define _gloffset_MapBufferOES 519
#define _gloffset_UnmapBufferOES 520
#define _gloffset_CompileShader 530
#define _gloffset_GetActiveUniform 535
#define _gloffset_GetShaderSource 541
#define _gloffset_GetUniformLocation 542
#define _gloffset_GetUniformfv 543
#define _gloffset_GetUniformiv 544
#define _gloffset_LinkProgram 545
#define _gloffset_ShaderSource 546
#define _gloffset_Uniform1f 547
#define _gloffset_Uniform1fv 548
#define _gloffset_Uniform1i 549
#define _gloffset_Uniform1iv 550
#define _gloffset_Uniform2f 551
#define _gloffset_Uniform2fv 552
#define _gloffset_Uniform2i 553
#define _gloffset_Uniform2iv 554
#define _gloffset_Uniform3f 555
#define _gloffset_Uniform3fv 556
#define _gloffset_Uniform3i 557
#define _gloffset_Uniform3iv 558
#define _gloffset_Uniform4f 559
#define _gloffset_Uniform4fv 560
#define _gloffset_Uniform4i 561
#define _gloffset_Uniform4iv 562
#define _gloffset_UniformMatrix2fv 563
#define _gloffset_UniformMatrix3fv 564
#define _gloffset_UniformMatrix4fv 565
#define _gloffset_UseProgram 566
#define _gloffset_ValidateProgram 567
#define _gloffset_BindAttribLocation 568
#define _gloffset_GetActiveAttrib 569
#define _gloffset_GetAttribLocation 570
#define _gloffset_DrawBuffersNV 571
#define _gloffset_ClearDepthf 660
#define _gloffset_DepthRangef 661
#define _gloffset_GetShaderPrecisionFormat 662
#define _gloffset_ReleaseShaderCompiler 663
#define _gloffset_ShaderBinary 664
#define _gloffset_GetProgramBinaryOES 665
#define _gloffset_ProgramBinaryOES 666
#define _gloffset_MultiDrawArraysEXT 740
#define _gloffset_MultiDrawElementsEXT 741
#define _gloffset_BlendFuncSeparate 748
#define _gloffset_GetVertexAttribPointerv 808
#define _gloffset_BlendEquationSeparate 938
#define _gloffset_BindFramebuffer 939
#define _gloffset_BindRenderbuffer 940
#define _gloffset_CheckFramebufferStatus 941
#define _gloffset_DeleteFramebuffers 942
#define _gloffset_DeleteRenderbuffers 943
#define _gloffset_FramebufferRenderbuffer 944
#define _gloffset_FramebufferTexture2D 946
#define _gloffset_FramebufferTexture3DOES 947
#define _gloffset_GenFramebuffers 948
#define _gloffset_GenRenderbuffers 949
#define _gloffset_GenerateMipmap 950
#define _gloffset_GetFramebufferAttachmentParameteriv 951
#define _gloffset_GetRenderbufferParameteriv 952
#define _gloffset_IsFramebuffer 953
#define _gloffset_IsRenderbuffer 954
#define _gloffset_RenderbufferStorage 955
#define _gloffset_EGLImageTargetRenderbufferStorageOES 1030
#define _gloffset_EGLImageTargetTexture2DOES 1031

#else /* !FEATURE_remap_table */

#define esLocalRemapTable_size 107
static int esLocalRemapTable[ esLocalRemapTable_size ];

#define AttachShader_remap_index 0
#define CreateProgram_remap_index 1
#define CreateShader_remap_index 2
#define DeleteProgram_remap_index 3
#define DeleteShader_remap_index 4
#define DetachShader_remap_index 5
#define GetAttachedShaders_remap_index 6
#define GetProgramInfoLog_remap_index 7
#define GetProgramiv_remap_index 8
#define GetShaderInfoLog_remap_index 9
#define GetShaderiv_remap_index 10
#define IsProgram_remap_index 11
#define IsShader_remap_index 12
#define StencilFuncSeparate_remap_index 13
#define StencilMaskSeparate_remap_index 14
#define StencilOpSeparate_remap_index 15
#define SampleCoverage_remap_index 16
#define CompressedTexImage2D_remap_index 17
#define CompressedTexImage3DOES_remap_index 18
#define CompressedTexSubImage2D_remap_index 19
#define CompressedTexSubImage3DOES_remap_index 20
#define DisableVertexAttribArray_remap_index 21
#define EnableVertexAttribArray_remap_index 22
#define GetVertexAttribfv_remap_index 23
#define GetVertexAttribiv_remap_index 24
#define VertexAttrib1f_remap_index 25
#define VertexAttrib1fv_remap_index 26
#define VertexAttrib2f_remap_index 27
#define VertexAttrib2fv_remap_index 28
#define VertexAttrib3f_remap_index 29
#define VertexAttrib3fv_remap_index 30
#define VertexAttrib4f_remap_index 31
#define VertexAttrib4fv_remap_index 32
#define VertexAttribPointer_remap_index 33
#define BindBuffer_remap_index 34
#define BufferData_remap_index 35
#define BufferSubData_remap_index 36
#define DeleteBuffers_remap_index 37
#define GenBuffers_remap_index 38
#define GetBufferParameteriv_remap_index 39
#define GetBufferPointervOES_remap_index 40
#define IsBuffer_remap_index 41
#define MapBufferOES_remap_index 42
#define UnmapBufferOES_remap_index 43
#define CompileShader_remap_index 44
#define GetActiveUniform_remap_index 45
#define GetShaderSource_remap_index 46
#define GetUniformLocation_remap_index 47
#define GetUniformfv_remap_index 48
#define GetUniformiv_remap_index 49
#define LinkProgram_remap_index 50
#define ShaderSource_remap_index 51
#define Uniform1f_remap_index 52
#define Uniform1fv_remap_index 53
#define Uniform1i_remap_index 54
#define Uniform1iv_remap_index 55
#define Uniform2f_remap_index 56
#define Uniform2fv_remap_index 57
#define Uniform2i_remap_index 58
#define Uniform2iv_remap_index 59
#define Uniform3f_remap_index 60
#define Uniform3fv_remap_index 61
#define Uniform3i_remap_index 62
#define Uniform3iv_remap_index 63
#define Uniform4f_remap_index 64
#define Uniform4fv_remap_index 65
#define Uniform4i_remap_index 66
#define Uniform4iv_remap_index 67
#define UniformMatrix2fv_remap_index 68
#define UniformMatrix3fv_remap_index 69
#define UniformMatrix4fv_remap_index 70
#define UseProgram_remap_index 71
#define ValidateProgram_remap_index 72
#define BindAttribLocation_remap_index 73
#define GetActiveAttrib_remap_index 74
#define GetAttribLocation_remap_index 75
#define DrawBuffersNV_remap_index 76
#define ClearDepthf_remap_index 77
#define DepthRangef_remap_index 78
#define GetShaderPrecisionFormat_remap_index 79
#define ReleaseShaderCompiler_remap_index 80
#define ShaderBinary_remap_index 81
#define GetProgramBinaryOES_remap_index 82
#define ProgramBinaryOES_remap_index 83
#define MultiDrawArraysEXT_remap_index 84
#define MultiDrawElementsEXT_remap_index 85
#define BlendFuncSeparate_remap_index 86
#define GetVertexAttribPointerv_remap_index 87
#define BlendEquationSeparate_remap_index 88
#define BindFramebuffer_remap_index 89
#define BindRenderbuffer_remap_index 90
#define CheckFramebufferStatus_remap_index 91
#define DeleteFramebuffers_remap_index 92
#define DeleteRenderbuffers_remap_index 93
#define FramebufferRenderbuffer_remap_index 94
#define FramebufferTexture2D_remap_index 95
#define FramebufferTexture3DOES_remap_index 96
#define GenFramebuffers_remap_index 97
#define GenRenderbuffers_remap_index 98
#define GenerateMipmap_remap_index 99
#define GetFramebufferAttachmentParameteriv_remap_index 100
#define GetRenderbufferParameteriv_remap_index 101
#define IsFramebuffer_remap_index 102
#define IsRenderbuffer_remap_index 103
#define RenderbufferStorage_remap_index 104
#define EGLImageTargetRenderbufferStorageOES_remap_index 105
#define EGLImageTargetTexture2DOES_remap_index 106

#define _gloffset_AttachShader esLocalRemapTable[AttachShader_remap_index]
#define _gloffset_CreateProgram esLocalRemapTable[CreateProgram_remap_index]
#define _gloffset_CreateShader esLocalRemapTable[CreateShader_remap_index]
#define _gloffset_DeleteProgram esLocalRemapTable[DeleteProgram_remap_index]
#define _gloffset_DeleteShader esLocalRemapTable[DeleteShader_remap_index]
#define _gloffset_DetachShader esLocalRemapTable[DetachShader_remap_index]
#define _gloffset_GetAttachedShaders esLocalRemapTable[GetAttachedShaders_remap_index]
#define _gloffset_GetProgramInfoLog esLocalRemapTable[GetProgramInfoLog_remap_index]
#define _gloffset_GetProgramiv esLocalRemapTable[GetProgramiv_remap_index]
#define _gloffset_GetShaderInfoLog esLocalRemapTable[GetShaderInfoLog_remap_index]
#define _gloffset_GetShaderiv esLocalRemapTable[GetShaderiv_remap_index]
#define _gloffset_IsProgram esLocalRemapTable[IsProgram_remap_index]
#define _gloffset_IsShader esLocalRemapTable[IsShader_remap_index]
#define _gloffset_StencilFuncSeparate esLocalRemapTable[StencilFuncSeparate_remap_index]
#define _gloffset_StencilMaskSeparate esLocalRemapTable[StencilMaskSeparate_remap_index]
#define _gloffset_StencilOpSeparate esLocalRemapTable[StencilOpSeparate_remap_index]
#define _gloffset_SampleCoverage esLocalRemapTable[SampleCoverage_remap_index]
#define _gloffset_CompressedTexImage2D esLocalRemapTable[CompressedTexImage2D_remap_index]
#define _gloffset_CompressedTexImage3DOES esLocalRemapTable[CompressedTexImage3DOES_remap_index]
#define _gloffset_CompressedTexSubImage2D esLocalRemapTable[CompressedTexSubImage2D_remap_index]
#define _gloffset_CompressedTexSubImage3DOES esLocalRemapTable[CompressedTexSubImage3DOES_remap_index]
#define _gloffset_DisableVertexAttribArray esLocalRemapTable[DisableVertexAttribArray_remap_index]
#define _gloffset_EnableVertexAttribArray esLocalRemapTable[EnableVertexAttribArray_remap_index]
#define _gloffset_GetVertexAttribfv esLocalRemapTable[GetVertexAttribfv_remap_index]
#define _gloffset_GetVertexAttribiv esLocalRemapTable[GetVertexAttribiv_remap_index]
#define _gloffset_VertexAttrib1f esLocalRemapTable[VertexAttrib1f_remap_index]
#define _gloffset_VertexAttrib1fv esLocalRemapTable[VertexAttrib1fv_remap_index]
#define _gloffset_VertexAttrib2f esLocalRemapTable[VertexAttrib2f_remap_index]
#define _gloffset_VertexAttrib2fv esLocalRemapTable[VertexAttrib2fv_remap_index]
#define _gloffset_VertexAttrib3f esLocalRemapTable[VertexAttrib3f_remap_index]
#define _gloffset_VertexAttrib3fv esLocalRemapTable[VertexAttrib3fv_remap_index]
#define _gloffset_VertexAttrib4f esLocalRemapTable[VertexAttrib4f_remap_index]
#define _gloffset_VertexAttrib4fv esLocalRemapTable[VertexAttrib4fv_remap_index]
#define _gloffset_VertexAttribPointer esLocalRemapTable[VertexAttribPointer_remap_index]
#define _gloffset_BindBuffer esLocalRemapTable[BindBuffer_remap_index]
#define _gloffset_BufferData esLocalRemapTable[BufferData_remap_index]
#define _gloffset_BufferSubData esLocalRemapTable[BufferSubData_remap_index]
#define _gloffset_DeleteBuffers esLocalRemapTable[DeleteBuffers_remap_index]
#define _gloffset_GenBuffers esLocalRemapTable[GenBuffers_remap_index]
#define _gloffset_GetBufferParameteriv esLocalRemapTable[GetBufferParameteriv_remap_index]
#define _gloffset_GetBufferPointervOES esLocalRemapTable[GetBufferPointervOES_remap_index]
#define _gloffset_IsBuffer esLocalRemapTable[IsBuffer_remap_index]
#define _gloffset_MapBufferOES esLocalRemapTable[MapBufferOES_remap_index]
#define _gloffset_UnmapBufferOES esLocalRemapTable[UnmapBufferOES_remap_index]
#define _gloffset_CompileShader esLocalRemapTable[CompileShader_remap_index]
#define _gloffset_GetActiveUniform esLocalRemapTable[GetActiveUniform_remap_index]
#define _gloffset_GetShaderSource esLocalRemapTable[GetShaderSource_remap_index]
#define _gloffset_GetUniformLocation esLocalRemapTable[GetUniformLocation_remap_index]
#define _gloffset_GetUniformfv esLocalRemapTable[GetUniformfv_remap_index]
#define _gloffset_GetUniformiv esLocalRemapTable[GetUniformiv_remap_index]
#define _gloffset_LinkProgram esLocalRemapTable[LinkProgram_remap_index]
#define _gloffset_ShaderSource esLocalRemapTable[ShaderSource_remap_index]
#define _gloffset_Uniform1f esLocalRemapTable[Uniform1f_remap_index]
#define _gloffset_Uniform1fv esLocalRemapTable[Uniform1fv_remap_index]
#define _gloffset_Uniform1i esLocalRemapTable[Uniform1i_remap_index]
#define _gloffset_Uniform1iv esLocalRemapTable[Uniform1iv_remap_index]
#define _gloffset_Uniform2f esLocalRemapTable[Uniform2f_remap_index]
#define _gloffset_Uniform2fv esLocalRemapTable[Uniform2fv_remap_index]
#define _gloffset_Uniform2i esLocalRemapTable[Uniform2i_remap_index]
#define _gloffset_Uniform2iv esLocalRemapTable[Uniform2iv_remap_index]
#define _gloffset_Uniform3f esLocalRemapTable[Uniform3f_remap_index]
#define _gloffset_Uniform3fv esLocalRemapTable[Uniform3fv_remap_index]
#define _gloffset_Uniform3i esLocalRemapTable[Uniform3i_remap_index]
#define _gloffset_Uniform3iv esLocalRemapTable[Uniform3iv_remap_index]
#define _gloffset_Uniform4f esLocalRemapTable[Uniform4f_remap_index]
#define _gloffset_Uniform4fv esLocalRemapTable[Uniform4fv_remap_index]
#define _gloffset_Uniform4i esLocalRemapTable[Uniform4i_remap_index]
#define _gloffset_Uniform4iv esLocalRemapTable[Uniform4iv_remap_index]
#define _gloffset_UniformMatrix2fv esLocalRemapTable[UniformMatrix2fv_remap_index]
#define _gloffset_UniformMatrix3fv esLocalRemapTable[UniformMatrix3fv_remap_index]
#define _gloffset_UniformMatrix4fv esLocalRemapTable[UniformMatrix4fv_remap_index]
#define _gloffset_UseProgram esLocalRemapTable[UseProgram_remap_index]
#define _gloffset_ValidateProgram esLocalRemapTable[ValidateProgram_remap_index]
#define _gloffset_BindAttribLocation esLocalRemapTable[BindAttribLocation_remap_index]
#define _gloffset_GetActiveAttrib esLocalRemapTable[GetActiveAttrib_remap_index]
#define _gloffset_GetAttribLocation esLocalRemapTable[GetAttribLocation_remap_index]
#define _gloffset_DrawBuffersNV esLocalRemapTable[DrawBuffersNV_remap_index]
#define _gloffset_ClearDepthf esLocalRemapTable[ClearDepthf_remap_index]
#define _gloffset_DepthRangef esLocalRemapTable[DepthRangef_remap_index]
#define _gloffset_GetShaderPrecisionFormat esLocalRemapTable[GetShaderPrecisionFormat_remap_index]
#define _gloffset_ReleaseShaderCompiler esLocalRemapTable[ReleaseShaderCompiler_remap_index]
#define _gloffset_ShaderBinary esLocalRemapTable[ShaderBinary_remap_index]
#define _gloffset_GetProgramBinaryOES esLocalRemapTable[GetProgramBinaryOES_remap_index]
#define _gloffset_ProgramBinaryOES esLocalRemapTable[ProgramBinaryOES_remap_index]
#define _gloffset_MultiDrawArraysEXT esLocalRemapTable[MultiDrawArraysEXT_remap_index]
#define _gloffset_MultiDrawElementsEXT esLocalRemapTable[MultiDrawElementsEXT_remap_index]
#define _gloffset_BlendFuncSeparate esLocalRemapTable[BlendFuncSeparate_remap_index]
#define _gloffset_GetVertexAttribPointerv esLocalRemapTable[GetVertexAttribPointerv_remap_index]
#define _gloffset_BlendEquationSeparate esLocalRemapTable[BlendEquationSeparate_remap_index]
#define _gloffset_BindFramebuffer esLocalRemapTable[BindFramebuffer_remap_index]
#define _gloffset_BindRenderbuffer esLocalRemapTable[BindRenderbuffer_remap_index]
#define _gloffset_CheckFramebufferStatus esLocalRemapTable[CheckFramebufferStatus_remap_index]
#define _gloffset_DeleteFramebuffers esLocalRemapTable[DeleteFramebuffers_remap_index]
#define _gloffset_DeleteRenderbuffers esLocalRemapTable[DeleteRenderbuffers_remap_index]
#define _gloffset_FramebufferRenderbuffer esLocalRemapTable[FramebufferRenderbuffer_remap_index]
#define _gloffset_FramebufferTexture2D esLocalRemapTable[FramebufferTexture2D_remap_index]
#define _gloffset_FramebufferTexture3DOES esLocalRemapTable[FramebufferTexture3DOES_remap_index]
#define _gloffset_GenFramebuffers esLocalRemapTable[GenFramebuffers_remap_index]
#define _gloffset_GenRenderbuffers esLocalRemapTable[GenRenderbuffers_remap_index]
#define _gloffset_GenerateMipmap esLocalRemapTable[GenerateMipmap_remap_index]
#define _gloffset_GetFramebufferAttachmentParameteriv esLocalRemapTable[GetFramebufferAttachmentParameteriv_remap_index]
#define _gloffset_GetRenderbufferParameteriv esLocalRemapTable[GetRenderbufferParameteriv_remap_index]
#define _gloffset_IsFramebuffer esLocalRemapTable[IsFramebuffer_remap_index]
#define _gloffset_IsRenderbuffer esLocalRemapTable[IsRenderbuffer_remap_index]
#define _gloffset_RenderbufferStorage esLocalRemapTable[RenderbufferStorage_remap_index]
#define _gloffset_EGLImageTargetRenderbufferStorageOES esLocalRemapTable[EGLImageTargetRenderbufferStorageOES_remap_index]
#define _gloffset_EGLImageTargetTexture2DOES esLocalRemapTable[EGLImageTargetTexture2DOES_remap_index]

#endif /* !FEATURE_remap_table */

typedef void (GLAPIENTRYP _glptr_CullFace)(GLenum);
#define CALL_CullFace(disp, parameters) \
    (* GET_CullFace(disp)) parameters
static inline _glptr_CullFace GET_CullFace(struct _glapi_table *disp) {
   return (_glptr_CullFace) (GET_by_offset(disp, _gloffset_CullFace));
}

static inline void SET_CullFace(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_CullFace, fn);
}

typedef void (GLAPIENTRYP _glptr_FrontFace)(GLenum);
#define CALL_FrontFace(disp, parameters) \
    (* GET_FrontFace(disp)) parameters
static inline _glptr_FrontFace GET_FrontFace(struct _glapi_table *disp) {
   return (_glptr_FrontFace) (GET_by_offset(disp, _gloffset_FrontFace));
}

static inline void SET_FrontFace(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_FrontFace, fn);
}

typedef void (GLAPIENTRYP _glptr_Hint)(GLenum, GLenum);
#define CALL_Hint(disp, parameters) \
    (* GET_Hint(disp)) parameters
static inline _glptr_Hint GET_Hint(struct _glapi_table *disp) {
   return (_glptr_Hint) (GET_by_offset(disp, _gloffset_Hint));
}

static inline void SET_Hint(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_Hint, fn);
}

typedef void (GLAPIENTRYP _glptr_LineWidth)(GLfloat);
#define CALL_LineWidth(disp, parameters) \
    (* GET_LineWidth(disp)) parameters
static inline _glptr_LineWidth GET_LineWidth(struct _glapi_table *disp) {
   return (_glptr_LineWidth) (GET_by_offset(disp, _gloffset_LineWidth));
}

static inline void SET_LineWidth(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat)) {
   SET_by_offset(disp, _gloffset_LineWidth, fn);
}

typedef void (GLAPIENTRYP _glptr_Scissor)(GLint, GLint, GLsizei, GLsizei);
#define CALL_Scissor(disp, parameters) \
    (* GET_Scissor(disp)) parameters
static inline _glptr_Scissor GET_Scissor(struct _glapi_table *disp) {
   return (_glptr_Scissor) (GET_by_offset(disp, _gloffset_Scissor));
}

static inline void SET_Scissor(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei)) {
   SET_by_offset(disp, _gloffset_Scissor, fn);
}

typedef void (GLAPIENTRYP _glptr_TexParameterf)(GLenum, GLenum, GLfloat);
#define CALL_TexParameterf(disp, parameters) \
    (* GET_TexParameterf(disp)) parameters
static inline _glptr_TexParameterf GET_TexParameterf(struct _glapi_table *disp) {
   return (_glptr_TexParameterf) (GET_by_offset(disp, _gloffset_TexParameterf));
}

static inline void SET_TexParameterf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_TexParameterf, fn);
}

typedef void (GLAPIENTRYP _glptr_TexParameterfv)(GLenum, GLenum, const GLfloat *);
#define CALL_TexParameterfv(disp, parameters) \
    (* GET_TexParameterfv(disp)) parameters
static inline _glptr_TexParameterfv GET_TexParameterfv(struct _glapi_table *disp) {
   return (_glptr_TexParameterfv) (GET_by_offset(disp, _gloffset_TexParameterfv));
}

static inline void SET_TexParameterfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_TexParameterfv, fn);
}

typedef void (GLAPIENTRYP _glptr_TexParameteri)(GLenum, GLenum, GLint);
#define CALL_TexParameteri(disp, parameters) \
    (* GET_TexParameteri(disp)) parameters
static inline _glptr_TexParameteri GET_TexParameteri(struct _glapi_table *disp) {
   return (_glptr_TexParameteri) (GET_by_offset(disp, _gloffset_TexParameteri));
}

static inline void SET_TexParameteri(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint)) {
   SET_by_offset(disp, _gloffset_TexParameteri, fn);
}

typedef void (GLAPIENTRYP _glptr_TexParameteriv)(GLenum, GLenum, const GLint *);
#define CALL_TexParameteriv(disp, parameters) \
    (* GET_TexParameteriv(disp)) parameters
static inline _glptr_TexParameteriv GET_TexParameteriv(struct _glapi_table *disp) {
   return (_glptr_TexParameteriv) (GET_by_offset(disp, _gloffset_TexParameteriv));
}

static inline void SET_TexParameteriv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *)) {
   SET_by_offset(disp, _gloffset_TexParameteriv, fn);
}

typedef void (GLAPIENTRYP _glptr_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TexImage2D(disp, parameters) \
    (* GET_TexImage2D(disp)) parameters
static inline _glptr_TexImage2D GET_TexImage2D(struct _glapi_table *disp) {
   return (_glptr_TexImage2D) (GET_by_offset(disp, _gloffset_TexImage2D));
}

static inline void SET_TexImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_TexImage2D, fn);
}

typedef void (GLAPIENTRYP _glptr_Clear)(GLbitfield);
#define CALL_Clear(disp, parameters) \
    (* GET_Clear(disp)) parameters
static inline _glptr_Clear GET_Clear(struct _glapi_table *disp) {
   return (_glptr_Clear) (GET_by_offset(disp, _gloffset_Clear));
}

static inline void SET_Clear(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLbitfield)) {
   SET_by_offset(disp, _gloffset_Clear, fn);
}

typedef void (GLAPIENTRYP _glptr_ClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
#define CALL_ClearColor(disp, parameters) \
    (* GET_ClearColor(disp)) parameters
static inline _glptr_ClearColor GET_ClearColor(struct _glapi_table *disp) {
   return (_glptr_ClearColor) (GET_by_offset(disp, _gloffset_ClearColor));
}

static inline void SET_ClearColor(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampf, GLclampf, GLclampf, GLclampf)) {
   SET_by_offset(disp, _gloffset_ClearColor, fn);
}

typedef void (GLAPIENTRYP _glptr_ClearStencil)(GLint);
#define CALL_ClearStencil(disp, parameters) \
    (* GET_ClearStencil(disp)) parameters
static inline _glptr_ClearStencil GET_ClearStencil(struct _glapi_table *disp) {
   return (_glptr_ClearStencil) (GET_by_offset(disp, _gloffset_ClearStencil));
}

static inline void SET_ClearStencil(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint)) {
   SET_by_offset(disp, _gloffset_ClearStencil, fn);
}

typedef void (GLAPIENTRYP _glptr_StencilMask)(GLuint);
#define CALL_StencilMask(disp, parameters) \
    (* GET_StencilMask(disp)) parameters
static inline _glptr_StencilMask GET_StencilMask(struct _glapi_table *disp) {
   return (_glptr_StencilMask) (GET_by_offset(disp, _gloffset_StencilMask));
}

static inline void SET_StencilMask(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_StencilMask, fn);
}

typedef void (GLAPIENTRYP _glptr_ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
#define CALL_ColorMask(disp, parameters) \
    (* GET_ColorMask(disp)) parameters
static inline _glptr_ColorMask GET_ColorMask(struct _glapi_table *disp) {
   return (_glptr_ColorMask) (GET_by_offset(disp, _gloffset_ColorMask));
}

static inline void SET_ColorMask(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLboolean, GLboolean, GLboolean, GLboolean)) {
   SET_by_offset(disp, _gloffset_ColorMask, fn);
}

typedef void (GLAPIENTRYP _glptr_DepthMask)(GLboolean);
#define CALL_DepthMask(disp, parameters) \
    (* GET_DepthMask(disp)) parameters
static inline _glptr_DepthMask GET_DepthMask(struct _glapi_table *disp) {
   return (_glptr_DepthMask) (GET_by_offset(disp, _gloffset_DepthMask));
}

static inline void SET_DepthMask(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLboolean)) {
   SET_by_offset(disp, _gloffset_DepthMask, fn);
}

typedef void (GLAPIENTRYP _glptr_Disable)(GLenum);
#define CALL_Disable(disp, parameters) \
    (* GET_Disable(disp)) parameters
static inline _glptr_Disable GET_Disable(struct _glapi_table *disp) {
   return (_glptr_Disable) (GET_by_offset(disp, _gloffset_Disable));
}

static inline void SET_Disable(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_Disable, fn);
}

typedef void (GLAPIENTRYP _glptr_Enable)(GLenum);
#define CALL_Enable(disp, parameters) \
    (* GET_Enable(disp)) parameters
static inline _glptr_Enable GET_Enable(struct _glapi_table *disp) {
   return (_glptr_Enable) (GET_by_offset(disp, _gloffset_Enable));
}

static inline void SET_Enable(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_Enable, fn);
}

typedef void (GLAPIENTRYP _glptr_Finish)(void);
#define CALL_Finish(disp, parameters) \
    (* GET_Finish(disp)) parameters
static inline _glptr_Finish GET_Finish(struct _glapi_table *disp) {
   return (_glptr_Finish) (GET_by_offset(disp, _gloffset_Finish));
}

static inline void SET_Finish(struct _glapi_table *disp, void (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_Finish, fn);
}

typedef void (GLAPIENTRYP _glptr_Flush)(void);
#define CALL_Flush(disp, parameters) \
    (* GET_Flush(disp)) parameters
static inline _glptr_Flush GET_Flush(struct _glapi_table *disp) {
   return (_glptr_Flush) (GET_by_offset(disp, _gloffset_Flush));
}

static inline void SET_Flush(struct _glapi_table *disp, void (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_Flush, fn);
}

typedef void (GLAPIENTRYP _glptr_BlendFunc)(GLenum, GLenum);
#define CALL_BlendFunc(disp, parameters) \
    (* GET_BlendFunc(disp)) parameters
static inline _glptr_BlendFunc GET_BlendFunc(struct _glapi_table *disp) {
   return (_glptr_BlendFunc) (GET_by_offset(disp, _gloffset_BlendFunc));
}

static inline void SET_BlendFunc(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_BlendFunc, fn);
}

typedef void (GLAPIENTRYP _glptr_StencilFunc)(GLenum, GLint, GLuint);
#define CALL_StencilFunc(disp, parameters) \
    (* GET_StencilFunc(disp)) parameters
static inline _glptr_StencilFunc GET_StencilFunc(struct _glapi_table *disp) {
   return (_glptr_StencilFunc) (GET_by_offset(disp, _gloffset_StencilFunc));
}

static inline void SET_StencilFunc(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLuint)) {
   SET_by_offset(disp, _gloffset_StencilFunc, fn);
}

typedef void (GLAPIENTRYP _glptr_StencilOp)(GLenum, GLenum, GLenum);
#define CALL_StencilOp(disp, parameters) \
    (* GET_StencilOp(disp)) parameters
static inline _glptr_StencilOp GET_StencilOp(struct _glapi_table *disp) {
   return (_glptr_StencilOp) (GET_by_offset(disp, _gloffset_StencilOp));
}

static inline void SET_StencilOp(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_StencilOp, fn);
}

typedef void (GLAPIENTRYP _glptr_DepthFunc)(GLenum);
#define CALL_DepthFunc(disp, parameters) \
    (* GET_DepthFunc(disp)) parameters
static inline _glptr_DepthFunc GET_DepthFunc(struct _glapi_table *disp) {
   return (_glptr_DepthFunc) (GET_by_offset(disp, _gloffset_DepthFunc));
}

static inline void SET_DepthFunc(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_DepthFunc, fn);
}

typedef void (GLAPIENTRYP _glptr_PixelStorei)(GLenum, GLint);
#define CALL_PixelStorei(disp, parameters) \
    (* GET_PixelStorei(disp)) parameters
static inline _glptr_PixelStorei GET_PixelStorei(struct _glapi_table *disp) {
   return (_glptr_PixelStorei) (GET_by_offset(disp, _gloffset_PixelStorei));
}

static inline void SET_PixelStorei(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint)) {
   SET_by_offset(disp, _gloffset_PixelStorei, fn);
}

typedef void (GLAPIENTRYP _glptr_ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *);
#define CALL_ReadPixels(disp, parameters) \
    (* GET_ReadPixels(disp)) parameters
static inline _glptr_ReadPixels GET_ReadPixels(struct _glapi_table *disp) {
   return (_glptr_ReadPixels) (GET_by_offset(disp, _gloffset_ReadPixels));
}

static inline void SET_ReadPixels(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid *)) {
   SET_by_offset(disp, _gloffset_ReadPixels, fn);
}

typedef void (GLAPIENTRYP _glptr_GetBooleanv)(GLenum, GLboolean *);
#define CALL_GetBooleanv(disp, parameters) \
    (* GET_GetBooleanv(disp)) parameters
static inline _glptr_GetBooleanv GET_GetBooleanv(struct _glapi_table *disp) {
   return (_glptr_GetBooleanv) (GET_by_offset(disp, _gloffset_GetBooleanv));
}

static inline void SET_GetBooleanv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLboolean *)) {
   SET_by_offset(disp, _gloffset_GetBooleanv, fn);
}

typedef GLenum (GLAPIENTRYP _glptr_GetError)(void);
#define CALL_GetError(disp, parameters) \
    (* GET_GetError(disp)) parameters
static inline _glptr_GetError GET_GetError(struct _glapi_table *disp) {
   return (_glptr_GetError) (GET_by_offset(disp, _gloffset_GetError));
}

static inline void SET_GetError(struct _glapi_table *disp, GLenum (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_GetError, fn);
}

typedef void (GLAPIENTRYP _glptr_GetFloatv)(GLenum, GLfloat *);
#define CALL_GetFloatv(disp, parameters) \
    (* GET_GetFloatv(disp)) parameters
static inline _glptr_GetFloatv GET_GetFloatv(struct _glapi_table *disp) {
   return (_glptr_GetFloatv) (GET_by_offset(disp, _gloffset_GetFloatv));
}

static inline void SET_GetFloatv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetFloatv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetIntegerv)(GLenum, GLint *);
#define CALL_GetIntegerv(disp, parameters) \
    (* GET_GetIntegerv(disp)) parameters
static inline _glptr_GetIntegerv GET_GetIntegerv(struct _glapi_table *disp) {
   return (_glptr_GetIntegerv) (GET_by_offset(disp, _gloffset_GetIntegerv));
}

static inline void SET_GetIntegerv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetIntegerv, fn);
}

typedef const GLubyte * (GLAPIENTRYP _glptr_GetString)(GLenum);
#define CALL_GetString(disp, parameters) \
    (* GET_GetString(disp)) parameters
static inline _glptr_GetString GET_GetString(struct _glapi_table *disp) {
   return (_glptr_GetString) (GET_by_offset(disp, _gloffset_GetString));
}

static inline void SET_GetString(struct _glapi_table *disp, const GLubyte * (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_GetString, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexParameterfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetTexParameterfv(disp, parameters) \
    (* GET_GetTexParameterfv(disp)) parameters
static inline _glptr_GetTexParameterfv GET_GetTexParameterfv(struct _glapi_table *disp) {
   return (_glptr_GetTexParameterfv) (GET_by_offset(disp, _gloffset_GetTexParameterfv));
}

static inline void SET_GetTexParameterfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetTexParameterfv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetTexParameteriv(disp, parameters) \
    (* GET_GetTexParameteriv(disp)) parameters
static inline _glptr_GetTexParameteriv GET_GetTexParameteriv(struct _glapi_table *disp) {
   return (_glptr_GetTexParameteriv) (GET_by_offset(disp, _gloffset_GetTexParameteriv));
}

static inline void SET_GetTexParameteriv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetTexParameteriv, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsEnabled)(GLenum);
#define CALL_IsEnabled(disp, parameters) \
    (* GET_IsEnabled(disp)) parameters
static inline _glptr_IsEnabled GET_IsEnabled(struct _glapi_table *disp) {
   return (_glptr_IsEnabled) (GET_by_offset(disp, _gloffset_IsEnabled));
}

static inline void SET_IsEnabled(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_IsEnabled, fn);
}

typedef void (GLAPIENTRYP _glptr_Viewport)(GLint, GLint, GLsizei, GLsizei);
#define CALL_Viewport(disp, parameters) \
    (* GET_Viewport(disp)) parameters
static inline _glptr_Viewport GET_Viewport(struct _glapi_table *disp) {
   return (_glptr_Viewport) (GET_by_offset(disp, _gloffset_Viewport));
}

static inline void SET_Viewport(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLsizei, GLsizei)) {
   SET_by_offset(disp, _gloffset_Viewport, fn);
}

typedef void (GLAPIENTRYP _glptr_BindTexture)(GLenum, GLuint);
#define CALL_BindTexture(disp, parameters) \
    (* GET_BindTexture(disp)) parameters
static inline _glptr_BindTexture GET_BindTexture(struct _glapi_table *disp) {
   return (_glptr_BindTexture) (GET_by_offset(disp, _gloffset_BindTexture));
}

static inline void SET_BindTexture(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_BindTexture, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawArrays)(GLenum, GLint, GLsizei);
#define CALL_DrawArrays(disp, parameters) \
    (* GET_DrawArrays(disp)) parameters
static inline _glptr_DrawArrays GET_DrawArrays(struct _glapi_table *disp) {
   return (_glptr_DrawArrays) (GET_by_offset(disp, _gloffset_DrawArrays));
}

static inline void SET_DrawArrays(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLsizei)) {
   SET_by_offset(disp, _gloffset_DrawArrays, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawElements)(GLenum, GLsizei, GLenum, const GLvoid *);
#define CALL_DrawElements(disp, parameters) \
    (* GET_DrawElements(disp)) parameters
static inline _glptr_DrawElements GET_DrawElements(struct _glapi_table *disp) {
   return (_glptr_DrawElements) (GET_by_offset(disp, _gloffset_DrawElements));
}

static inline void SET_DrawElements(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLsizei, GLenum, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_DrawElements, fn);
}

typedef void (GLAPIENTRYP _glptr_PolygonOffset)(GLfloat, GLfloat);
#define CALL_PolygonOffset(disp, parameters) \
    (* GET_PolygonOffset(disp)) parameters
static inline _glptr_PolygonOffset GET_PolygonOffset(struct _glapi_table *disp) {
   return (_glptr_PolygonOffset) (GET_by_offset(disp, _gloffset_PolygonOffset));
}

static inline void SET_PolygonOffset(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_PolygonOffset, fn);
}

typedef void (GLAPIENTRYP _glptr_CopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
#define CALL_CopyTexImage2D(disp, parameters) \
    (* GET_CopyTexImage2D(disp)) parameters
static inline _glptr_CopyTexImage2D GET_CopyTexImage2D(struct _glapi_table *disp) {
   return (_glptr_CopyTexImage2D) (GET_by_offset(disp, _gloffset_CopyTexImage2D));
}

static inline void SET_CopyTexImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)) {
   SET_by_offset(disp, _gloffset_CopyTexImage2D, fn);
}

typedef void (GLAPIENTRYP _glptr_CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTexSubImage2D(disp, parameters) \
    (* GET_CopyTexSubImage2D(disp)) parameters
static inline _glptr_CopyTexSubImage2D GET_CopyTexSubImage2D(struct _glapi_table *disp) {
   return (_glptr_CopyTexSubImage2D) (GET_by_offset(disp, _gloffset_CopyTexSubImage2D));
}

static inline void SET_CopyTexSubImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)) {
   SET_by_offset(disp, _gloffset_CopyTexSubImage2D, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteTextures)(GLsizei, const GLuint *);
#define CALL_DeleteTextures(disp, parameters) \
    (* GET_DeleteTextures(disp)) parameters
static inline _glptr_DeleteTextures GET_DeleteTextures(struct _glapi_table *disp) {
   return (_glptr_DeleteTextures) (GET_by_offset(disp, _gloffset_DeleteTextures));
}

static inline void SET_DeleteTextures(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *)) {
   SET_by_offset(disp, _gloffset_DeleteTextures, fn);
}

typedef void (GLAPIENTRYP _glptr_GenTextures)(GLsizei, GLuint *);
#define CALL_GenTextures(disp, parameters) \
    (* GET_GenTextures(disp)) parameters
static inline _glptr_GenTextures GET_GenTextures(struct _glapi_table *disp) {
   return (_glptr_GenTextures) (GET_by_offset(disp, _gloffset_GenTextures));
}

static inline void SET_GenTextures(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, GLuint *)) {
   SET_by_offset(disp, _gloffset_GenTextures, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsTexture)(GLuint);
#define CALL_IsTexture(disp, parameters) \
    (* GET_IsTexture(disp)) parameters
static inline _glptr_IsTexture GET_IsTexture(struct _glapi_table *disp) {
   return (_glptr_IsTexture) (GET_by_offset(disp, _gloffset_IsTexture));
}

static inline void SET_IsTexture(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsTexture, fn);
}

typedef void (GLAPIENTRYP _glptr_TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TexSubImage2D(disp, parameters) \
    (* GET_TexSubImage2D(disp)) parameters
static inline _glptr_TexSubImage2D GET_TexSubImage2D(struct _glapi_table *disp) {
   return (_glptr_TexSubImage2D) (GET_by_offset(disp, _gloffset_TexSubImage2D));
}

static inline void SET_TexSubImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_TexSubImage2D, fn);
}

typedef void (GLAPIENTRYP _glptr_BlendColor)(GLclampf, GLclampf, GLclampf, GLclampf);
#define CALL_BlendColor(disp, parameters) \
    (* GET_BlendColor(disp)) parameters
static inline _glptr_BlendColor GET_BlendColor(struct _glapi_table *disp) {
   return (_glptr_BlendColor) (GET_by_offset(disp, _gloffset_BlendColor));
}

static inline void SET_BlendColor(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampf, GLclampf, GLclampf, GLclampf)) {
   SET_by_offset(disp, _gloffset_BlendColor, fn);
}

typedef void (GLAPIENTRYP _glptr_BlendEquation)(GLenum);
#define CALL_BlendEquation(disp, parameters) \
    (* GET_BlendEquation(disp)) parameters
static inline _glptr_BlendEquation GET_BlendEquation(struct _glapi_table *disp) {
   return (_glptr_BlendEquation) (GET_by_offset(disp, _gloffset_BlendEquation));
}

static inline void SET_BlendEquation(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_BlendEquation, fn);
}

typedef void (GLAPIENTRYP _glptr_TexImage3DOES)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
#define CALL_TexImage3DOES(disp, parameters) \
    (* GET_TexImage3DOES(disp)) parameters
static inline _glptr_TexImage3DOES GET_TexImage3DOES(struct _glapi_table *disp) {
   return (_glptr_TexImage3DOES) (GET_by_offset(disp, _gloffset_TexImage3DOES));
}

static inline void SET_TexImage3DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_TexImage3DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexSubImage3DOES)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
#define CALL_TexSubImage3DOES(disp, parameters) \
    (* GET_TexSubImage3DOES(disp)) parameters
static inline _glptr_TexSubImage3DOES GET_TexSubImage3DOES(struct _glapi_table *disp) {
   return (_glptr_TexSubImage3DOES) (GET_by_offset(disp, _gloffset_TexSubImage3DOES));
}

static inline void SET_TexSubImage3DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_TexSubImage3DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_CopyTexSubImage3DOES)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
#define CALL_CopyTexSubImage3DOES(disp, parameters) \
    (* GET_CopyTexSubImage3DOES(disp)) parameters
static inline _glptr_CopyTexSubImage3DOES GET_CopyTexSubImage3DOES(struct _glapi_table *disp) {
   return (_glptr_CopyTexSubImage3DOES) (GET_by_offset(disp, _gloffset_CopyTexSubImage3DOES));
}

static inline void SET_CopyTexSubImage3DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)) {
   SET_by_offset(disp, _gloffset_CopyTexSubImage3DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ActiveTexture)(GLenum);
#define CALL_ActiveTexture(disp, parameters) \
    (* GET_ActiveTexture(disp)) parameters
static inline _glptr_ActiveTexture GET_ActiveTexture(struct _glapi_table *disp) {
   return (_glptr_ActiveTexture) (GET_by_offset(disp, _gloffset_ActiveTexture));
}

static inline void SET_ActiveTexture(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_ActiveTexture, fn);
}

typedef void (GLAPIENTRYP _glptr_AttachShader)(GLuint, GLuint);
#define CALL_AttachShader(disp, parameters) \
    (* GET_AttachShader(disp)) parameters
static inline _glptr_AttachShader GET_AttachShader(struct _glapi_table *disp) {
   return (_glptr_AttachShader) (GET_by_offset(disp, _gloffset_AttachShader));
}

static inline void SET_AttachShader(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLuint)) {
   SET_by_offset(disp, _gloffset_AttachShader, fn);
}

typedef GLuint (GLAPIENTRYP _glptr_CreateProgram)(void);
#define CALL_CreateProgram(disp, parameters) \
    (* GET_CreateProgram(disp)) parameters
static inline _glptr_CreateProgram GET_CreateProgram(struct _glapi_table *disp) {
   return (_glptr_CreateProgram) (GET_by_offset(disp, _gloffset_CreateProgram));
}

static inline void SET_CreateProgram(struct _glapi_table *disp, GLuint (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_CreateProgram, fn);
}

typedef GLuint (GLAPIENTRYP _glptr_CreateShader)(GLenum);
#define CALL_CreateShader(disp, parameters) \
    (* GET_CreateShader(disp)) parameters
static inline _glptr_CreateShader GET_CreateShader(struct _glapi_table *disp) {
   return (_glptr_CreateShader) (GET_by_offset(disp, _gloffset_CreateShader));
}

static inline void SET_CreateShader(struct _glapi_table *disp, GLuint (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_CreateShader, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteProgram)(GLuint);
#define CALL_DeleteProgram(disp, parameters) \
    (* GET_DeleteProgram(disp)) parameters
static inline _glptr_DeleteProgram GET_DeleteProgram(struct _glapi_table *disp) {
   return (_glptr_DeleteProgram) (GET_by_offset(disp, _gloffset_DeleteProgram));
}

static inline void SET_DeleteProgram(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_DeleteProgram, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteShader)(GLuint);
#define CALL_DeleteShader(disp, parameters) \
    (* GET_DeleteShader(disp)) parameters
static inline _glptr_DeleteShader GET_DeleteShader(struct _glapi_table *disp) {
   return (_glptr_DeleteShader) (GET_by_offset(disp, _gloffset_DeleteShader));
}

static inline void SET_DeleteShader(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_DeleteShader, fn);
}

typedef void (GLAPIENTRYP _glptr_DetachShader)(GLuint, GLuint);
#define CALL_DetachShader(disp, parameters) \
    (* GET_DetachShader(disp)) parameters
static inline _glptr_DetachShader GET_DetachShader(struct _glapi_table *disp) {
   return (_glptr_DetachShader) (GET_by_offset(disp, _gloffset_DetachShader));
}

static inline void SET_DetachShader(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLuint)) {
   SET_by_offset(disp, _gloffset_DetachShader, fn);
}

typedef void (GLAPIENTRYP _glptr_GetAttachedShaders)(GLuint, GLsizei, GLsizei *, GLuint *);
#define CALL_GetAttachedShaders(disp, parameters) \
    (* GET_GetAttachedShaders(disp)) parameters
static inline _glptr_GetAttachedShaders GET_GetAttachedShaders(struct _glapi_table *disp) {
   return (_glptr_GetAttachedShaders) (GET_by_offset(disp, _gloffset_GetAttachedShaders));
}

static inline void SET_GetAttachedShaders(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLuint *)) {
   SET_by_offset(disp, _gloffset_GetAttachedShaders, fn);
}

typedef void (GLAPIENTRYP _glptr_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetProgramInfoLog(disp, parameters) \
    (* GET_GetProgramInfoLog(disp)) parameters
static inline _glptr_GetProgramInfoLog GET_GetProgramInfoLog(struct _glapi_table *disp) {
   return (_glptr_GetProgramInfoLog) (GET_by_offset(disp, _gloffset_GetProgramInfoLog));
}

static inline void SET_GetProgramInfoLog(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *)) {
   SET_by_offset(disp, _gloffset_GetProgramInfoLog, fn);
}

typedef void (GLAPIENTRYP _glptr_GetProgramiv)(GLuint, GLenum, GLint *);
#define CALL_GetProgramiv(disp, parameters) \
    (* GET_GetProgramiv(disp)) parameters
static inline _glptr_GetProgramiv GET_GetProgramiv(struct _glapi_table *disp) {
   return (_glptr_GetProgramiv) (GET_by_offset(disp, _gloffset_GetProgramiv));
}

static inline void SET_GetProgramiv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetProgramiv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetShaderInfoLog(disp, parameters) \
    (* GET_GetShaderInfoLog(disp)) parameters
static inline _glptr_GetShaderInfoLog GET_GetShaderInfoLog(struct _glapi_table *disp) {
   return (_glptr_GetShaderInfoLog) (GET_by_offset(disp, _gloffset_GetShaderInfoLog));
}

static inline void SET_GetShaderInfoLog(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *)) {
   SET_by_offset(disp, _gloffset_GetShaderInfoLog, fn);
}

typedef void (GLAPIENTRYP _glptr_GetShaderiv)(GLuint, GLenum, GLint *);
#define CALL_GetShaderiv(disp, parameters) \
    (* GET_GetShaderiv(disp)) parameters
static inline _glptr_GetShaderiv GET_GetShaderiv(struct _glapi_table *disp) {
   return (_glptr_GetShaderiv) (GET_by_offset(disp, _gloffset_GetShaderiv));
}

static inline void SET_GetShaderiv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetShaderiv, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsProgram)(GLuint);
#define CALL_IsProgram(disp, parameters) \
    (* GET_IsProgram(disp)) parameters
static inline _glptr_IsProgram GET_IsProgram(struct _glapi_table *disp) {
   return (_glptr_IsProgram) (GET_by_offset(disp, _gloffset_IsProgram));
}

static inline void SET_IsProgram(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsProgram, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsShader)(GLuint);
#define CALL_IsShader(disp, parameters) \
    (* GET_IsShader(disp)) parameters
static inline _glptr_IsShader GET_IsShader(struct _glapi_table *disp) {
   return (_glptr_IsShader) (GET_by_offset(disp, _gloffset_IsShader));
}

static inline void SET_IsShader(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsShader, fn);
}

typedef void (GLAPIENTRYP _glptr_StencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
#define CALL_StencilFuncSeparate(disp, parameters) \
    (* GET_StencilFuncSeparate(disp)) parameters
static inline _glptr_StencilFuncSeparate GET_StencilFuncSeparate(struct _glapi_table *disp) {
   return (_glptr_StencilFuncSeparate) (GET_by_offset(disp, _gloffset_StencilFuncSeparate));
}

static inline void SET_StencilFuncSeparate(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint, GLuint)) {
   SET_by_offset(disp, _gloffset_StencilFuncSeparate, fn);
}

typedef void (GLAPIENTRYP _glptr_StencilMaskSeparate)(GLenum, GLuint);
#define CALL_StencilMaskSeparate(disp, parameters) \
    (* GET_StencilMaskSeparate(disp)) parameters
static inline _glptr_StencilMaskSeparate GET_StencilMaskSeparate(struct _glapi_table *disp) {
   return (_glptr_StencilMaskSeparate) (GET_by_offset(disp, _gloffset_StencilMaskSeparate));
}

static inline void SET_StencilMaskSeparate(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_StencilMaskSeparate, fn);
}

typedef void (GLAPIENTRYP _glptr_StencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
#define CALL_StencilOpSeparate(disp, parameters) \
    (* GET_StencilOpSeparate(disp)) parameters
static inline _glptr_StencilOpSeparate GET_StencilOpSeparate(struct _glapi_table *disp) {
   return (_glptr_StencilOpSeparate) (GET_by_offset(disp, _gloffset_StencilOpSeparate));
}

static inline void SET_StencilOpSeparate(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_StencilOpSeparate, fn);
}

typedef void (GLAPIENTRYP _glptr_SampleCoverage)(GLclampf, GLboolean);
#define CALL_SampleCoverage(disp, parameters) \
    (* GET_SampleCoverage(disp)) parameters
static inline _glptr_SampleCoverage GET_SampleCoverage(struct _glapi_table *disp) {
   return (_glptr_SampleCoverage) (GET_by_offset(disp, _gloffset_SampleCoverage));
}

static inline void SET_SampleCoverage(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampf, GLboolean)) {
   SET_by_offset(disp, _gloffset_SampleCoverage, fn);
}

typedef void (GLAPIENTRYP _glptr_CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
#define CALL_CompressedTexImage2D(disp, parameters) \
    (* GET_CompressedTexImage2D(disp)) parameters
static inline _glptr_CompressedTexImage2D GET_CompressedTexImage2D(struct _glapi_table *disp) {
   return (_glptr_CompressedTexImage2D) (GET_by_offset(disp, _gloffset_CompressedTexImage2D));
}

static inline void SET_CompressedTexImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_CompressedTexImage2D, fn);
}

typedef void (GLAPIENTRYP _glptr_CompressedTexImage3DOES)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *);
#define CALL_CompressedTexImage3DOES(disp, parameters) \
    (* GET_CompressedTexImage3DOES(disp)) parameters
static inline _glptr_CompressedTexImage3DOES GET_CompressedTexImage3DOES(struct _glapi_table *disp) {
   return (_glptr_CompressedTexImage3DOES) (GET_by_offset(disp, _gloffset_CompressedTexImage3DOES));
}

static inline void SET_CompressedTexImage3DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_CompressedTexImage3DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_CompressedTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTexSubImage2D(disp, parameters) \
    (* GET_CompressedTexSubImage2D(disp)) parameters
static inline _glptr_CompressedTexSubImage2D GET_CompressedTexSubImage2D(struct _glapi_table *disp) {
   return (_glptr_CompressedTexSubImage2D) (GET_by_offset(disp, _gloffset_CompressedTexSubImage2D));
}

static inline void SET_CompressedTexSubImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_CompressedTexSubImage2D, fn);
}

typedef void (GLAPIENTRYP _glptr_CompressedTexSubImage3DOES)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTexSubImage3DOES(disp, parameters) \
    (* GET_CompressedTexSubImage3DOES(disp)) parameters
static inline _glptr_CompressedTexSubImage3DOES GET_CompressedTexSubImage3DOES(struct _glapi_table *disp) {
   return (_glptr_CompressedTexSubImage3DOES) (GET_by_offset(disp, _gloffset_CompressedTexSubImage3DOES));
}

static inline void SET_CompressedTexSubImage3DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_CompressedTexSubImage3DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DisableVertexAttribArray)(GLuint);
#define CALL_DisableVertexAttribArray(disp, parameters) \
    (* GET_DisableVertexAttribArray(disp)) parameters
static inline _glptr_DisableVertexAttribArray GET_DisableVertexAttribArray(struct _glapi_table *disp) {
   return (_glptr_DisableVertexAttribArray) (GET_by_offset(disp, _gloffset_DisableVertexAttribArray));
}

static inline void SET_DisableVertexAttribArray(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_DisableVertexAttribArray, fn);
}

typedef void (GLAPIENTRYP _glptr_EnableVertexAttribArray)(GLuint);
#define CALL_EnableVertexAttribArray(disp, parameters) \
    (* GET_EnableVertexAttribArray(disp)) parameters
static inline _glptr_EnableVertexAttribArray GET_EnableVertexAttribArray(struct _glapi_table *disp) {
   return (_glptr_EnableVertexAttribArray) (GET_by_offset(disp, _gloffset_EnableVertexAttribArray));
}

static inline void SET_EnableVertexAttribArray(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_EnableVertexAttribArray, fn);
}

typedef void (GLAPIENTRYP _glptr_GetVertexAttribfv)(GLuint, GLenum, GLfloat *);
#define CALL_GetVertexAttribfv(disp, parameters) \
    (* GET_GetVertexAttribfv(disp)) parameters
static inline _glptr_GetVertexAttribfv GET_GetVertexAttribfv(struct _glapi_table *disp) {
   return (_glptr_GetVertexAttribfv) (GET_by_offset(disp, _gloffset_GetVertexAttribfv));
}

static inline void SET_GetVertexAttribfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetVertexAttribfv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetVertexAttribiv)(GLuint, GLenum, GLint *);
#define CALL_GetVertexAttribiv(disp, parameters) \
    (* GET_GetVertexAttribiv(disp)) parameters
static inline _glptr_GetVertexAttribiv GET_GetVertexAttribiv(struct _glapi_table *disp) {
   return (_glptr_GetVertexAttribiv) (GET_by_offset(disp, _gloffset_GetVertexAttribiv));
}

static inline void SET_GetVertexAttribiv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetVertexAttribiv, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib1f)(GLuint, GLfloat);
#define CALL_VertexAttrib1f(disp, parameters) \
    (* GET_VertexAttrib1f(disp)) parameters
static inline _glptr_VertexAttrib1f GET_VertexAttrib1f(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib1f) (GET_by_offset(disp, _gloffset_VertexAttrib1f));
}

static inline void SET_VertexAttrib1f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLfloat)) {
   SET_by_offset(disp, _gloffset_VertexAttrib1f, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib1fv)(GLuint, const GLfloat *);
#define CALL_VertexAttrib1fv(disp, parameters) \
    (* GET_VertexAttrib1fv(disp)) parameters
static inline _glptr_VertexAttrib1fv GET_VertexAttrib1fv(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib1fv) (GET_by_offset(disp, _gloffset_VertexAttrib1fv));
}

static inline void SET_VertexAttrib1fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_VertexAttrib1fv, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib2f)(GLuint, GLfloat, GLfloat);
#define CALL_VertexAttrib2f(disp, parameters) \
    (* GET_VertexAttrib2f(disp)) parameters
static inline _glptr_VertexAttrib2f GET_VertexAttrib2f(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib2f) (GET_by_offset(disp, _gloffset_VertexAttrib2f));
}

static inline void SET_VertexAttrib2f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_VertexAttrib2f, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib2fv)(GLuint, const GLfloat *);
#define CALL_VertexAttrib2fv(disp, parameters) \
    (* GET_VertexAttrib2fv(disp)) parameters
static inline _glptr_VertexAttrib2fv GET_VertexAttrib2fv(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib2fv) (GET_by_offset(disp, _gloffset_VertexAttrib2fv));
}

static inline void SET_VertexAttrib2fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_VertexAttrib2fv, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib3f)(GLuint, GLfloat, GLfloat, GLfloat);
#define CALL_VertexAttrib3f(disp, parameters) \
    (* GET_VertexAttrib3f(disp)) parameters
static inline _glptr_VertexAttrib3f GET_VertexAttrib3f(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib3f) (GET_by_offset(disp, _gloffset_VertexAttrib3f));
}

static inline void SET_VertexAttrib3f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_VertexAttrib3f, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib3fv)(GLuint, const GLfloat *);
#define CALL_VertexAttrib3fv(disp, parameters) \
    (* GET_VertexAttrib3fv(disp)) parameters
static inline _glptr_VertexAttrib3fv GET_VertexAttrib3fv(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib3fv) (GET_by_offset(disp, _gloffset_VertexAttrib3fv));
}

static inline void SET_VertexAttrib3fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_VertexAttrib3fv, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_VertexAttrib4f(disp, parameters) \
    (* GET_VertexAttrib4f(disp)) parameters
static inline _glptr_VertexAttrib4f GET_VertexAttrib4f(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib4f) (GET_by_offset(disp, _gloffset_VertexAttrib4f));
}

static inline void SET_VertexAttrib4f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_VertexAttrib4f, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttrib4fv)(GLuint, const GLfloat *);
#define CALL_VertexAttrib4fv(disp, parameters) \
    (* GET_VertexAttrib4fv(disp)) parameters
static inline _glptr_VertexAttrib4fv GET_VertexAttrib4fv(struct _glapi_table *disp) {
   return (_glptr_VertexAttrib4fv) (GET_by_offset(disp, _gloffset_VertexAttrib4fv));
}

static inline void SET_VertexAttrib4fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_VertexAttrib4fv, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
#define CALL_VertexAttribPointer(disp, parameters) \
    (* GET_VertexAttribPointer(disp)) parameters
static inline _glptr_VertexAttribPointer GET_VertexAttribPointer(struct _glapi_table *disp) {
   return (_glptr_VertexAttribPointer) (GET_by_offset(disp, _gloffset_VertexAttribPointer));
}

static inline void SET_VertexAttribPointer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_VertexAttribPointer, fn);
}

typedef void (GLAPIENTRYP _glptr_BindBuffer)(GLenum, GLuint);
#define CALL_BindBuffer(disp, parameters) \
    (* GET_BindBuffer(disp)) parameters
static inline _glptr_BindBuffer GET_BindBuffer(struct _glapi_table *disp) {
   return (_glptr_BindBuffer) (GET_by_offset(disp, _gloffset_BindBuffer));
}

static inline void SET_BindBuffer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_BindBuffer, fn);
}

typedef void (GLAPIENTRYP _glptr_BufferData)(GLenum, GLsizeiptr, const GLvoid *, GLenum);
#define CALL_BufferData(disp, parameters) \
    (* GET_BufferData(disp)) parameters
static inline _glptr_BufferData GET_BufferData(struct _glapi_table *disp) {
   return (_glptr_BufferData) (GET_by_offset(disp, _gloffset_BufferData));
}

static inline void SET_BufferData(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLsizeiptr, const GLvoid *, GLenum)) {
   SET_by_offset(disp, _gloffset_BufferData, fn);
}

typedef void (GLAPIENTRYP _glptr_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const GLvoid *);
#define CALL_BufferSubData(disp, parameters) \
    (* GET_BufferSubData(disp)) parameters
static inline _glptr_BufferSubData GET_BufferSubData(struct _glapi_table *disp) {
   return (_glptr_BufferSubData) (GET_by_offset(disp, _gloffset_BufferSubData));
}

static inline void SET_BufferSubData(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLintptr, GLsizeiptr, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_BufferSubData, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteBuffers)(GLsizei, const GLuint *);
#define CALL_DeleteBuffers(disp, parameters) \
    (* GET_DeleteBuffers(disp)) parameters
static inline _glptr_DeleteBuffers GET_DeleteBuffers(struct _glapi_table *disp) {
   return (_glptr_DeleteBuffers) (GET_by_offset(disp, _gloffset_DeleteBuffers));
}

static inline void SET_DeleteBuffers(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *)) {
   SET_by_offset(disp, _gloffset_DeleteBuffers, fn);
}

typedef void (GLAPIENTRYP _glptr_GenBuffers)(GLsizei, GLuint *);
#define CALL_GenBuffers(disp, parameters) \
    (* GET_GenBuffers(disp)) parameters
static inline _glptr_GenBuffers GET_GenBuffers(struct _glapi_table *disp) {
   return (_glptr_GenBuffers) (GET_by_offset(disp, _gloffset_GenBuffers));
}

static inline void SET_GenBuffers(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, GLuint *)) {
   SET_by_offset(disp, _gloffset_GenBuffers, fn);
}

typedef void (GLAPIENTRYP _glptr_GetBufferParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetBufferParameteriv(disp, parameters) \
    (* GET_GetBufferParameteriv(disp)) parameters
static inline _glptr_GetBufferParameteriv GET_GetBufferParameteriv(struct _glapi_table *disp) {
   return (_glptr_GetBufferParameteriv) (GET_by_offset(disp, _gloffset_GetBufferParameteriv));
}

static inline void SET_GetBufferParameteriv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetBufferParameteriv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetBufferPointervOES)(GLenum, GLenum, GLvoid **);
#define CALL_GetBufferPointervOES(disp, parameters) \
    (* GET_GetBufferPointervOES(disp)) parameters
static inline _glptr_GetBufferPointervOES GET_GetBufferPointervOES(struct _glapi_table *disp) {
   return (_glptr_GetBufferPointervOES) (GET_by_offset(disp, _gloffset_GetBufferPointervOES));
}

static inline void SET_GetBufferPointervOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLvoid **)) {
   SET_by_offset(disp, _gloffset_GetBufferPointervOES, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsBuffer)(GLuint);
#define CALL_IsBuffer(disp, parameters) \
    (* GET_IsBuffer(disp)) parameters
static inline _glptr_IsBuffer GET_IsBuffer(struct _glapi_table *disp) {
   return (_glptr_IsBuffer) (GET_by_offset(disp, _gloffset_IsBuffer));
}

static inline void SET_IsBuffer(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsBuffer, fn);
}

typedef GLvoid * (GLAPIENTRYP _glptr_MapBufferOES)(GLenum, GLenum);
#define CALL_MapBufferOES(disp, parameters) \
    (* GET_MapBufferOES(disp)) parameters
static inline _glptr_MapBufferOES GET_MapBufferOES(struct _glapi_table *disp) {
   return (_glptr_MapBufferOES) (GET_by_offset(disp, _gloffset_MapBufferOES));
}

static inline void SET_MapBufferOES(struct _glapi_table *disp, GLvoid * (GLAPIENTRYP fn)(GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_MapBufferOES, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_UnmapBufferOES)(GLenum);
#define CALL_UnmapBufferOES(disp, parameters) \
    (* GET_UnmapBufferOES(disp)) parameters
static inline _glptr_UnmapBufferOES GET_UnmapBufferOES(struct _glapi_table *disp) {
   return (_glptr_UnmapBufferOES) (GET_by_offset(disp, _gloffset_UnmapBufferOES));
}

static inline void SET_UnmapBufferOES(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_UnmapBufferOES, fn);
}

typedef void (GLAPIENTRYP _glptr_CompileShader)(GLuint);
#define CALL_CompileShader(disp, parameters) \
    (* GET_CompileShader(disp)) parameters
static inline _glptr_CompileShader GET_CompileShader(struct _glapi_table *disp) {
   return (_glptr_CompileShader) (GET_by_offset(disp, _gloffset_CompileShader));
}

static inline void SET_CompileShader(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_CompileShader, fn);
}

typedef void (GLAPIENTRYP _glptr_GetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
#define CALL_GetActiveUniform(disp, parameters) \
    (* GET_GetActiveUniform(disp)) parameters
static inline _glptr_GetActiveUniform GET_GetActiveUniform(struct _glapi_table *disp) {
   return (_glptr_GetActiveUniform) (GET_by_offset(disp, _gloffset_GetActiveUniform));
}

static inline void SET_GetActiveUniform(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *)) {
   SET_by_offset(disp, _gloffset_GetActiveUniform, fn);
}

typedef void (GLAPIENTRYP _glptr_GetShaderSource)(GLuint, GLsizei, GLsizei *, GLchar *);
#define CALL_GetShaderSource(disp, parameters) \
    (* GET_GetShaderSource(disp)) parameters
static inline _glptr_GetShaderSource GET_GetShaderSource(struct _glapi_table *disp) {
   return (_glptr_GetShaderSource) (GET_by_offset(disp, _gloffset_GetShaderSource));
}

static inline void SET_GetShaderSource(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLchar *)) {
   SET_by_offset(disp, _gloffset_GetShaderSource, fn);
}

typedef GLint (GLAPIENTRYP _glptr_GetUniformLocation)(GLuint, const GLchar *);
#define CALL_GetUniformLocation(disp, parameters) \
    (* GET_GetUniformLocation(disp)) parameters
static inline _glptr_GetUniformLocation GET_GetUniformLocation(struct _glapi_table *disp) {
   return (_glptr_GetUniformLocation) (GET_by_offset(disp, _gloffset_GetUniformLocation));
}

static inline void SET_GetUniformLocation(struct _glapi_table *disp, GLint (GLAPIENTRYP fn)(GLuint, const GLchar *)) {
   SET_by_offset(disp, _gloffset_GetUniformLocation, fn);
}

typedef void (GLAPIENTRYP _glptr_GetUniformfv)(GLuint, GLint, GLfloat *);
#define CALL_GetUniformfv(disp, parameters) \
    (* GET_GetUniformfv(disp)) parameters
static inline _glptr_GetUniformfv GET_GetUniformfv(struct _glapi_table *disp) {
   return (_glptr_GetUniformfv) (GET_by_offset(disp, _gloffset_GetUniformfv));
}

static inline void SET_GetUniformfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLint, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetUniformfv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetUniformiv)(GLuint, GLint, GLint *);
#define CALL_GetUniformiv(disp, parameters) \
    (* GET_GetUniformiv(disp)) parameters
static inline _glptr_GetUniformiv GET_GetUniformiv(struct _glapi_table *disp) {
   return (_glptr_GetUniformiv) (GET_by_offset(disp, _gloffset_GetUniformiv));
}

static inline void SET_GetUniformiv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLint, GLint *)) {
   SET_by_offset(disp, _gloffset_GetUniformiv, fn);
}

typedef void (GLAPIENTRYP _glptr_LinkProgram)(GLuint);
#define CALL_LinkProgram(disp, parameters) \
    (* GET_LinkProgram(disp)) parameters
static inline _glptr_LinkProgram GET_LinkProgram(struct _glapi_table *disp) {
   return (_glptr_LinkProgram) (GET_by_offset(disp, _gloffset_LinkProgram));
}

static inline void SET_LinkProgram(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_LinkProgram, fn);
}

typedef void (GLAPIENTRYP _glptr_ShaderSource)(GLuint, GLsizei, const GLchar **, const GLint *);
#define CALL_ShaderSource(disp, parameters) \
    (* GET_ShaderSource(disp)) parameters
static inline _glptr_ShaderSource GET_ShaderSource(struct _glapi_table *disp) {
   return (_glptr_ShaderSource) (GET_by_offset(disp, _gloffset_ShaderSource));
}

static inline void SET_ShaderSource(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLsizei, const GLchar **, const GLint *)) {
   SET_by_offset(disp, _gloffset_ShaderSource, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform1f)(GLint, GLfloat);
#define CALL_Uniform1f(disp, parameters) \
    (* GET_Uniform1f(disp)) parameters
static inline _glptr_Uniform1f GET_Uniform1f(struct _glapi_table *disp) {
   return (_glptr_Uniform1f) (GET_by_offset(disp, _gloffset_Uniform1f));
}

static inline void SET_Uniform1f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLfloat)) {
   SET_by_offset(disp, _gloffset_Uniform1f, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform1fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform1fv(disp, parameters) \
    (* GET_Uniform1fv(disp)) parameters
static inline _glptr_Uniform1fv GET_Uniform1fv(struct _glapi_table *disp) {
   return (_glptr_Uniform1fv) (GET_by_offset(disp, _gloffset_Uniform1fv));
}

static inline void SET_Uniform1fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Uniform1fv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform1i)(GLint, GLint);
#define CALL_Uniform1i(disp, parameters) \
    (* GET_Uniform1i(disp)) parameters
static inline _glptr_Uniform1i GET_Uniform1i(struct _glapi_table *disp) {
   return (_glptr_Uniform1i) (GET_by_offset(disp, _gloffset_Uniform1i));
}

static inline void SET_Uniform1i(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint)) {
   SET_by_offset(disp, _gloffset_Uniform1i, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform1iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform1iv(disp, parameters) \
    (* GET_Uniform1iv(disp)) parameters
static inline _glptr_Uniform1iv GET_Uniform1iv(struct _glapi_table *disp) {
   return (_glptr_Uniform1iv) (GET_by_offset(disp, _gloffset_Uniform1iv));
}

static inline void SET_Uniform1iv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *)) {
   SET_by_offset(disp, _gloffset_Uniform1iv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform2f)(GLint, GLfloat, GLfloat);
#define CALL_Uniform2f(disp, parameters) \
    (* GET_Uniform2f(disp)) parameters
static inline _glptr_Uniform2f GET_Uniform2f(struct _glapi_table *disp) {
   return (_glptr_Uniform2f) (GET_by_offset(disp, _gloffset_Uniform2f));
}

static inline void SET_Uniform2f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Uniform2f, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform2fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform2fv(disp, parameters) \
    (* GET_Uniform2fv(disp)) parameters
static inline _glptr_Uniform2fv GET_Uniform2fv(struct _glapi_table *disp) {
   return (_glptr_Uniform2fv) (GET_by_offset(disp, _gloffset_Uniform2fv));
}

static inline void SET_Uniform2fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Uniform2fv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform2i)(GLint, GLint, GLint);
#define CALL_Uniform2i(disp, parameters) \
    (* GET_Uniform2i(disp)) parameters
static inline _glptr_Uniform2i GET_Uniform2i(struct _glapi_table *disp) {
   return (_glptr_Uniform2i) (GET_by_offset(disp, _gloffset_Uniform2i));
}

static inline void SET_Uniform2i(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLint)) {
   SET_by_offset(disp, _gloffset_Uniform2i, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform2iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform2iv(disp, parameters) \
    (* GET_Uniform2iv(disp)) parameters
static inline _glptr_Uniform2iv GET_Uniform2iv(struct _glapi_table *disp) {
   return (_glptr_Uniform2iv) (GET_by_offset(disp, _gloffset_Uniform2iv));
}

static inline void SET_Uniform2iv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *)) {
   SET_by_offset(disp, _gloffset_Uniform2iv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
#define CALL_Uniform3f(disp, parameters) \
    (* GET_Uniform3f(disp)) parameters
static inline _glptr_Uniform3f GET_Uniform3f(struct _glapi_table *disp) {
   return (_glptr_Uniform3f) (GET_by_offset(disp, _gloffset_Uniform3f));
}

static inline void SET_Uniform3f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Uniform3f, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform3fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform3fv(disp, parameters) \
    (* GET_Uniform3fv(disp)) parameters
static inline _glptr_Uniform3fv GET_Uniform3fv(struct _glapi_table *disp) {
   return (_glptr_Uniform3fv) (GET_by_offset(disp, _gloffset_Uniform3fv));
}

static inline void SET_Uniform3fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Uniform3fv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform3i)(GLint, GLint, GLint, GLint);
#define CALL_Uniform3i(disp, parameters) \
    (* GET_Uniform3i(disp)) parameters
static inline _glptr_Uniform3i GET_Uniform3i(struct _glapi_table *disp) {
   return (_glptr_Uniform3i) (GET_by_offset(disp, _gloffset_Uniform3i));
}

static inline void SET_Uniform3i(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint)) {
   SET_by_offset(disp, _gloffset_Uniform3i, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform3iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform3iv(disp, parameters) \
    (* GET_Uniform3iv(disp)) parameters
static inline _glptr_Uniform3iv GET_Uniform3iv(struct _glapi_table *disp) {
   return (_glptr_Uniform3iv) (GET_by_offset(disp, _gloffset_Uniform3iv));
}

static inline void SET_Uniform3iv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *)) {
   SET_by_offset(disp, _gloffset_Uniform3iv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Uniform4f(disp, parameters) \
    (* GET_Uniform4f(disp)) parameters
static inline _glptr_Uniform4f GET_Uniform4f(struct _glapi_table *disp) {
   return (_glptr_Uniform4f) (GET_by_offset(disp, _gloffset_Uniform4f));
}

static inline void SET_Uniform4f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Uniform4f, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform4fv)(GLint, GLsizei, const GLfloat *);
#define CALL_Uniform4fv(disp, parameters) \
    (* GET_Uniform4fv(disp)) parameters
static inline _glptr_Uniform4fv GET_Uniform4fv(struct _glapi_table *disp) {
   return (_glptr_Uniform4fv) (GET_by_offset(disp, _gloffset_Uniform4fv));
}

static inline void SET_Uniform4fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Uniform4fv, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform4i)(GLint, GLint, GLint, GLint, GLint);
#define CALL_Uniform4i(disp, parameters) \
    (* GET_Uniform4i(disp)) parameters
static inline _glptr_Uniform4i GET_Uniform4i(struct _glapi_table *disp) {
   return (_glptr_Uniform4i) (GET_by_offset(disp, _gloffset_Uniform4i));
}

static inline void SET_Uniform4i(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint, GLint)) {
   SET_by_offset(disp, _gloffset_Uniform4i, fn);
}

typedef void (GLAPIENTRYP _glptr_Uniform4iv)(GLint, GLsizei, const GLint *);
#define CALL_Uniform4iv(disp, parameters) \
    (* GET_Uniform4iv(disp)) parameters
static inline _glptr_Uniform4iv GET_Uniform4iv(struct _glapi_table *disp) {
   return (_glptr_Uniform4iv) (GET_by_offset(disp, _gloffset_Uniform4iv));
}

static inline void SET_Uniform4iv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, const GLint *)) {
   SET_by_offset(disp, _gloffset_Uniform4iv, fn);
}

typedef void (GLAPIENTRYP _glptr_UniformMatrix2fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix2fv(disp, parameters) \
    (* GET_UniformMatrix2fv(disp)) parameters
static inline _glptr_UniformMatrix2fv GET_UniformMatrix2fv(struct _glapi_table *disp) {
   return (_glptr_UniformMatrix2fv) (GET_by_offset(disp, _gloffset_UniformMatrix2fv));
}

static inline void SET_UniformMatrix2fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_UniformMatrix2fv, fn);
}

typedef void (GLAPIENTRYP _glptr_UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix3fv(disp, parameters) \
    (* GET_UniformMatrix3fv(disp)) parameters
static inline _glptr_UniformMatrix3fv GET_UniformMatrix3fv(struct _glapi_table *disp) {
   return (_glptr_UniformMatrix3fv) (GET_by_offset(disp, _gloffset_UniformMatrix3fv));
}

static inline void SET_UniformMatrix3fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_UniformMatrix3fv, fn);
}

typedef void (GLAPIENTRYP _glptr_UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
#define CALL_UniformMatrix4fv(disp, parameters) \
    (* GET_UniformMatrix4fv(disp)) parameters
static inline _glptr_UniformMatrix4fv GET_UniformMatrix4fv(struct _glapi_table *disp) {
   return (_glptr_UniformMatrix4fv) (GET_by_offset(disp, _gloffset_UniformMatrix4fv));
}

static inline void SET_UniformMatrix4fv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLsizei, GLboolean, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_UniformMatrix4fv, fn);
}

typedef void (GLAPIENTRYP _glptr_UseProgram)(GLuint);
#define CALL_UseProgram(disp, parameters) \
    (* GET_UseProgram(disp)) parameters
static inline _glptr_UseProgram GET_UseProgram(struct _glapi_table *disp) {
   return (_glptr_UseProgram) (GET_by_offset(disp, _gloffset_UseProgram));
}

static inline void SET_UseProgram(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_UseProgram, fn);
}

typedef void (GLAPIENTRYP _glptr_ValidateProgram)(GLuint);
#define CALL_ValidateProgram(disp, parameters) \
    (* GET_ValidateProgram(disp)) parameters
static inline _glptr_ValidateProgram GET_ValidateProgram(struct _glapi_table *disp) {
   return (_glptr_ValidateProgram) (GET_by_offset(disp, _gloffset_ValidateProgram));
}

static inline void SET_ValidateProgram(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_ValidateProgram, fn);
}

typedef void (GLAPIENTRYP _glptr_BindAttribLocation)(GLuint, GLuint, const GLchar *);
#define CALL_BindAttribLocation(disp, parameters) \
    (* GET_BindAttribLocation(disp)) parameters
static inline _glptr_BindAttribLocation GET_BindAttribLocation(struct _glapi_table *disp) {
   return (_glptr_BindAttribLocation) (GET_by_offset(disp, _gloffset_BindAttribLocation));
}

static inline void SET_BindAttribLocation(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLuint, const GLchar *)) {
   SET_by_offset(disp, _gloffset_BindAttribLocation, fn);
}

typedef void (GLAPIENTRYP _glptr_GetActiveAttrib)(GLuint, GLuint, GLsizei , GLsizei *, GLint *, GLenum *, GLchar *);
#define CALL_GetActiveAttrib(disp, parameters) \
    (* GET_GetActiveAttrib(disp)) parameters
static inline _glptr_GetActiveAttrib GET_GetActiveAttrib(struct _glapi_table *disp) {
   return (_glptr_GetActiveAttrib) (GET_by_offset(disp, _gloffset_GetActiveAttrib));
}

static inline void SET_GetActiveAttrib(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLuint, GLsizei , GLsizei *, GLint *, GLenum *, GLchar *)) {
   SET_by_offset(disp, _gloffset_GetActiveAttrib, fn);
}

typedef GLint (GLAPIENTRYP _glptr_GetAttribLocation)(GLuint, const GLchar *);
#define CALL_GetAttribLocation(disp, parameters) \
    (* GET_GetAttribLocation(disp)) parameters
static inline _glptr_GetAttribLocation GET_GetAttribLocation(struct _glapi_table *disp) {
   return (_glptr_GetAttribLocation) (GET_by_offset(disp, _gloffset_GetAttribLocation));
}

static inline void SET_GetAttribLocation(struct _glapi_table *disp, GLint (GLAPIENTRYP fn)(GLuint, const GLchar *)) {
   SET_by_offset(disp, _gloffset_GetAttribLocation, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawBuffersNV)(GLsizei, const GLenum *);
#define CALL_DrawBuffersNV(disp, parameters) \
    (* GET_DrawBuffersNV(disp)) parameters
static inline _glptr_DrawBuffersNV GET_DrawBuffersNV(struct _glapi_table *disp) {
   return (_glptr_DrawBuffersNV) (GET_by_offset(disp, _gloffset_DrawBuffersNV));
}

static inline void SET_DrawBuffersNV(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLenum *)) {
   SET_by_offset(disp, _gloffset_DrawBuffersNV, fn);
}

typedef void (GLAPIENTRYP _glptr_ClearDepthf)(GLclampf);
#define CALL_ClearDepthf(disp, parameters) \
    (* GET_ClearDepthf(disp)) parameters
static inline _glptr_ClearDepthf GET_ClearDepthf(struct _glapi_table *disp) {
   return (_glptr_ClearDepthf) (GET_by_offset(disp, _gloffset_ClearDepthf));
}

static inline void SET_ClearDepthf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampf)) {
   SET_by_offset(disp, _gloffset_ClearDepthf, fn);
}

typedef void (GLAPIENTRYP _glptr_DepthRangef)(GLclampf, GLclampf);
#define CALL_DepthRangef(disp, parameters) \
    (* GET_DepthRangef(disp)) parameters
static inline _glptr_DepthRangef GET_DepthRangef(struct _glapi_table *disp) {
   return (_glptr_DepthRangef) (GET_by_offset(disp, _gloffset_DepthRangef));
}

static inline void SET_DepthRangef(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampf, GLclampf)) {
   SET_by_offset(disp, _gloffset_DepthRangef, fn);
}

typedef void (GLAPIENTRYP _glptr_GetShaderPrecisionFormat)(GLenum, GLenum, GLint *, GLint *);
#define CALL_GetShaderPrecisionFormat(disp, parameters) \
    (* GET_GetShaderPrecisionFormat(disp)) parameters
static inline _glptr_GetShaderPrecisionFormat GET_GetShaderPrecisionFormat(struct _glapi_table *disp) {
   return (_glptr_GetShaderPrecisionFormat) (GET_by_offset(disp, _gloffset_GetShaderPrecisionFormat));
}

static inline void SET_GetShaderPrecisionFormat(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *, GLint *)) {
   SET_by_offset(disp, _gloffset_GetShaderPrecisionFormat, fn);
}

typedef void (GLAPIENTRYP _glptr_ReleaseShaderCompiler)(void);
#define CALL_ReleaseShaderCompiler(disp, parameters) \
    (* GET_ReleaseShaderCompiler(disp)) parameters
static inline _glptr_ReleaseShaderCompiler GET_ReleaseShaderCompiler(struct _glapi_table *disp) {
   return (_glptr_ReleaseShaderCompiler) (GET_by_offset(disp, _gloffset_ReleaseShaderCompiler));
}

static inline void SET_ReleaseShaderCompiler(struct _glapi_table *disp, void (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_ReleaseShaderCompiler, fn);
}

typedef void (GLAPIENTRYP _glptr_ShaderBinary)(GLsizei, const GLuint *, GLenum, const GLvoid *, GLsizei);
#define CALL_ShaderBinary(disp, parameters) \
    (* GET_ShaderBinary(disp)) parameters
static inline _glptr_ShaderBinary GET_ShaderBinary(struct _glapi_table *disp) {
   return (_glptr_ShaderBinary) (GET_by_offset(disp, _gloffset_ShaderBinary));
}

static inline void SET_ShaderBinary(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *, GLenum, const GLvoid *, GLsizei)) {
   SET_by_offset(disp, _gloffset_ShaderBinary, fn);
}

typedef void (GLAPIENTRYP _glptr_GetProgramBinaryOES)(GLuint, GLsizei, GLsizei *, GLenum *, GLvoid *);
#define CALL_GetProgramBinaryOES(disp, parameters) \
    (* GET_GetProgramBinaryOES(disp)) parameters
static inline _glptr_GetProgramBinaryOES GET_GetProgramBinaryOES(struct _glapi_table *disp) {
   return (_glptr_GetProgramBinaryOES) (GET_by_offset(disp, _gloffset_GetProgramBinaryOES));
}

static inline void SET_GetProgramBinaryOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLsizei, GLsizei *, GLenum *, GLvoid *)) {
   SET_by_offset(disp, _gloffset_GetProgramBinaryOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ProgramBinaryOES)(GLuint, GLenum, const GLvoid *, GLint);
#define CALL_ProgramBinaryOES(disp, parameters) \
    (* GET_ProgramBinaryOES(disp)) parameters
static inline _glptr_ProgramBinaryOES GET_ProgramBinaryOES(struct _glapi_table *disp) {
   return (_glptr_ProgramBinaryOES) (GET_by_offset(disp, _gloffset_ProgramBinaryOES));
}

static inline void SET_ProgramBinaryOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLenum, const GLvoid *, GLint)) {
   SET_by_offset(disp, _gloffset_ProgramBinaryOES, fn);
}

typedef void (GLAPIENTRYP _glptr_MultiDrawArraysEXT)(GLenum, const GLint *, const GLsizei *, GLsizei);
#define CALL_MultiDrawArraysEXT(disp, parameters) \
    (* GET_MultiDrawArraysEXT(disp)) parameters
static inline _glptr_MultiDrawArraysEXT GET_MultiDrawArraysEXT(struct _glapi_table *disp) {
   return (_glptr_MultiDrawArraysEXT) (GET_by_offset(disp, _gloffset_MultiDrawArraysEXT));
}

static inline void SET_MultiDrawArraysEXT(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLint *, const GLsizei *, GLsizei)) {
   SET_by_offset(disp, _gloffset_MultiDrawArraysEXT, fn);
}

typedef void (GLAPIENTRYP _glptr_MultiDrawElementsEXT)(GLenum, const GLsizei *, GLenum, const GLvoid **, GLsizei);
#define CALL_MultiDrawElementsEXT(disp, parameters) \
    (* GET_MultiDrawElementsEXT(disp)) parameters
static inline _glptr_MultiDrawElementsEXT GET_MultiDrawElementsEXT(struct _glapi_table *disp) {
   return (_glptr_MultiDrawElementsEXT) (GET_by_offset(disp, _gloffset_MultiDrawElementsEXT));
}

static inline void SET_MultiDrawElementsEXT(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLsizei *, GLenum, const GLvoid **, GLsizei)) {
   SET_by_offset(disp, _gloffset_MultiDrawElementsEXT, fn);
}

typedef void (GLAPIENTRYP _glptr_BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
#define CALL_BlendFuncSeparate(disp, parameters) \
    (* GET_BlendFuncSeparate(disp)) parameters
static inline _glptr_BlendFuncSeparate GET_BlendFuncSeparate(struct _glapi_table *disp) {
   return (_glptr_BlendFuncSeparate) (GET_by_offset(disp, _gloffset_BlendFuncSeparate));
}

static inline void SET_BlendFuncSeparate(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_BlendFuncSeparate, fn);
}

typedef void (GLAPIENTRYP _glptr_GetVertexAttribPointerv)(GLuint, GLenum, GLvoid **);
#define CALL_GetVertexAttribPointerv(disp, parameters) \
    (* GET_GetVertexAttribPointerv(disp)) parameters
static inline _glptr_GetVertexAttribPointerv GET_GetVertexAttribPointerv(struct _glapi_table *disp) {
   return (_glptr_GetVertexAttribPointerv) (GET_by_offset(disp, _gloffset_GetVertexAttribPointerv));
}

static inline void SET_GetVertexAttribPointerv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLuint, GLenum, GLvoid **)) {
   SET_by_offset(disp, _gloffset_GetVertexAttribPointerv, fn);
}

typedef void (GLAPIENTRYP _glptr_BlendEquationSeparate)(GLenum, GLenum);
#define CALL_BlendEquationSeparate(disp, parameters) \
    (* GET_BlendEquationSeparate(disp)) parameters
static inline _glptr_BlendEquationSeparate GET_BlendEquationSeparate(struct _glapi_table *disp) {
   return (_glptr_BlendEquationSeparate) (GET_by_offset(disp, _gloffset_BlendEquationSeparate));
}

static inline void SET_BlendEquationSeparate(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_BlendEquationSeparate, fn);
}

typedef void (GLAPIENTRYP _glptr_BindFramebuffer)(GLenum, GLuint);
#define CALL_BindFramebuffer(disp, parameters) \
    (* GET_BindFramebuffer(disp)) parameters
static inline _glptr_BindFramebuffer GET_BindFramebuffer(struct _glapi_table *disp) {
   return (_glptr_BindFramebuffer) (GET_by_offset(disp, _gloffset_BindFramebuffer));
}

static inline void SET_BindFramebuffer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_BindFramebuffer, fn);
}

typedef void (GLAPIENTRYP _glptr_BindRenderbuffer)(GLenum, GLuint);
#define CALL_BindRenderbuffer(disp, parameters) \
    (* GET_BindRenderbuffer(disp)) parameters
static inline _glptr_BindRenderbuffer GET_BindRenderbuffer(struct _glapi_table *disp) {
   return (_glptr_BindRenderbuffer) (GET_by_offset(disp, _gloffset_BindRenderbuffer));
}

static inline void SET_BindRenderbuffer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_BindRenderbuffer, fn);
}

typedef GLenum (GLAPIENTRYP _glptr_CheckFramebufferStatus)(GLenum);
#define CALL_CheckFramebufferStatus(disp, parameters) \
    (* GET_CheckFramebufferStatus(disp)) parameters
static inline _glptr_CheckFramebufferStatus GET_CheckFramebufferStatus(struct _glapi_table *disp) {
   return (_glptr_CheckFramebufferStatus) (GET_by_offset(disp, _gloffset_CheckFramebufferStatus));
}

static inline void SET_CheckFramebufferStatus(struct _glapi_table *disp, GLenum (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_CheckFramebufferStatus, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteFramebuffers)(GLsizei, const GLuint *);
#define CALL_DeleteFramebuffers(disp, parameters) \
    (* GET_DeleteFramebuffers(disp)) parameters
static inline _glptr_DeleteFramebuffers GET_DeleteFramebuffers(struct _glapi_table *disp) {
   return (_glptr_DeleteFramebuffers) (GET_by_offset(disp, _gloffset_DeleteFramebuffers));
}

static inline void SET_DeleteFramebuffers(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *)) {
   SET_by_offset(disp, _gloffset_DeleteFramebuffers, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteRenderbuffers)(GLsizei, const GLuint *);
#define CALL_DeleteRenderbuffers(disp, parameters) \
    (* GET_DeleteRenderbuffers(disp)) parameters
static inline _glptr_DeleteRenderbuffers GET_DeleteRenderbuffers(struct _glapi_table *disp) {
   return (_glptr_DeleteRenderbuffers) (GET_by_offset(disp, _gloffset_DeleteRenderbuffers));
}

static inline void SET_DeleteRenderbuffers(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *)) {
   SET_by_offset(disp, _gloffset_DeleteRenderbuffers, fn);
}

typedef void (GLAPIENTRYP _glptr_FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
#define CALL_FramebufferRenderbuffer(disp, parameters) \
    (* GET_FramebufferRenderbuffer(disp)) parameters
static inline _glptr_FramebufferRenderbuffer GET_FramebufferRenderbuffer(struct _glapi_table *disp) {
   return (_glptr_FramebufferRenderbuffer) (GET_by_offset(disp, _gloffset_FramebufferRenderbuffer));
}

static inline void SET_FramebufferRenderbuffer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_FramebufferRenderbuffer, fn);
}

typedef void (GLAPIENTRYP _glptr_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
#define CALL_FramebufferTexture2D(disp, parameters) \
    (* GET_FramebufferTexture2D(disp)) parameters
static inline _glptr_FramebufferTexture2D GET_FramebufferTexture2D(struct _glapi_table *disp) {
   return (_glptr_FramebufferTexture2D) (GET_by_offset(disp, _gloffset_FramebufferTexture2D));
}

static inline void SET_FramebufferTexture2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint)) {
   SET_by_offset(disp, _gloffset_FramebufferTexture2D, fn);
}

typedef void (GLAPIENTRYP _glptr_FramebufferTexture3DOES)(GLenum, GLenum, GLenum, GLuint, GLint, GLint);
#define CALL_FramebufferTexture3DOES(disp, parameters) \
    (* GET_FramebufferTexture3DOES(disp)) parameters
static inline _glptr_FramebufferTexture3DOES GET_FramebufferTexture3DOES(struct _glapi_table *disp) {
   return (_glptr_FramebufferTexture3DOES) (GET_by_offset(disp, _gloffset_FramebufferTexture3DOES));
}

static inline void SET_FramebufferTexture3DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint, GLint)) {
   SET_by_offset(disp, _gloffset_FramebufferTexture3DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GenFramebuffers)(GLsizei, GLuint *);
#define CALL_GenFramebuffers(disp, parameters) \
    (* GET_GenFramebuffers(disp)) parameters
static inline _glptr_GenFramebuffers GET_GenFramebuffers(struct _glapi_table *disp) {
   return (_glptr_GenFramebuffers) (GET_by_offset(disp, _gloffset_GenFramebuffers));
}

static inline void SET_GenFramebuffers(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, GLuint *)) {
   SET_by_offset(disp, _gloffset_GenFramebuffers, fn);
}

typedef void (GLAPIENTRYP _glptr_GenRenderbuffers)(GLsizei, GLuint *);
#define CALL_GenRenderbuffers(disp, parameters) \
    (* GET_GenRenderbuffers(disp)) parameters
static inline _glptr_GenRenderbuffers GET_GenRenderbuffers(struct _glapi_table *disp) {
   return (_glptr_GenRenderbuffers) (GET_by_offset(disp, _gloffset_GenRenderbuffers));
}

static inline void SET_GenRenderbuffers(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, GLuint *)) {
   SET_by_offset(disp, _gloffset_GenRenderbuffers, fn);
}

typedef void (GLAPIENTRYP _glptr_GenerateMipmap)(GLenum);
#define CALL_GenerateMipmap(disp, parameters) \
    (* GET_GenerateMipmap(disp)) parameters
static inline _glptr_GenerateMipmap GET_GenerateMipmap(struct _glapi_table *disp) {
   return (_glptr_GenerateMipmap) (GET_by_offset(disp, _gloffset_GenerateMipmap));
}

static inline void SET_GenerateMipmap(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_GenerateMipmap, fn);
}

typedef void (GLAPIENTRYP _glptr_GetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint *);
#define CALL_GetFramebufferAttachmentParameteriv(disp, parameters) \
    (* GET_GetFramebufferAttachmentParameteriv(disp)) parameters
static inline _glptr_GetFramebufferAttachmentParameteriv GET_GetFramebufferAttachmentParameteriv(struct _glapi_table *disp) {
   return (_glptr_GetFramebufferAttachmentParameteriv) (GET_by_offset(disp, _gloffset_GetFramebufferAttachmentParameteriv));
}

static inline void SET_GetFramebufferAttachmentParameteriv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetFramebufferAttachmentParameteriv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetRenderbufferParameteriv)(GLenum, GLenum, GLint *);
#define CALL_GetRenderbufferParameteriv(disp, parameters) \
    (* GET_GetRenderbufferParameteriv(disp)) parameters
static inline _glptr_GetRenderbufferParameteriv GET_GetRenderbufferParameteriv(struct _glapi_table *disp) {
   return (_glptr_GetRenderbufferParameteriv) (GET_by_offset(disp, _gloffset_GetRenderbufferParameteriv));
}

static inline void SET_GetRenderbufferParameteriv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetRenderbufferParameteriv, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsFramebuffer)(GLuint);
#define CALL_IsFramebuffer(disp, parameters) \
    (* GET_IsFramebuffer(disp)) parameters
static inline _glptr_IsFramebuffer GET_IsFramebuffer(struct _glapi_table *disp) {
   return (_glptr_IsFramebuffer) (GET_by_offset(disp, _gloffset_IsFramebuffer));
}

static inline void SET_IsFramebuffer(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsFramebuffer, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsRenderbuffer)(GLuint);
#define CALL_IsRenderbuffer(disp, parameters) \
    (* GET_IsRenderbuffer(disp)) parameters
static inline _glptr_IsRenderbuffer GET_IsRenderbuffer(struct _glapi_table *disp) {
   return (_glptr_IsRenderbuffer) (GET_by_offset(disp, _gloffset_IsRenderbuffer));
}

static inline void SET_IsRenderbuffer(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsRenderbuffer, fn);
}

typedef void (GLAPIENTRYP _glptr_RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
#define CALL_RenderbufferStorage(disp, parameters) \
    (* GET_RenderbufferStorage(disp)) parameters
static inline _glptr_RenderbufferStorage GET_RenderbufferStorage(struct _glapi_table *disp) {
   return (_glptr_RenderbufferStorage) (GET_by_offset(disp, _gloffset_RenderbufferStorage));
}

static inline void SET_RenderbufferStorage(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLsizei)) {
   SET_by_offset(disp, _gloffset_RenderbufferStorage, fn);
}

typedef void (GLAPIENTRYP _glptr_EGLImageTargetRenderbufferStorageOES)(GLenum, GLvoid *);
#define CALL_EGLImageTargetRenderbufferStorageOES(disp, parameters) \
    (* GET_EGLImageTargetRenderbufferStorageOES(disp)) parameters
static inline _glptr_EGLImageTargetRenderbufferStorageOES GET_EGLImageTargetRenderbufferStorageOES(struct _glapi_table *disp) {
   return (_glptr_EGLImageTargetRenderbufferStorageOES) (GET_by_offset(disp, _gloffset_EGLImageTargetRenderbufferStorageOES));
}

static inline void SET_EGLImageTargetRenderbufferStorageOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLvoid *)) {
   SET_by_offset(disp, _gloffset_EGLImageTargetRenderbufferStorageOES, fn);
}

typedef void (GLAPIENTRYP _glptr_EGLImageTargetTexture2DOES)(GLenum, GLvoid *);
#define CALL_EGLImageTargetTexture2DOES(disp, parameters) \
    (* GET_EGLImageTargetTexture2DOES(disp)) parameters
static inline _glptr_EGLImageTargetTexture2DOES GET_EGLImageTargetTexture2DOES(struct _glapi_table *disp) {
   return (_glptr_EGLImageTargetTexture2DOES) (GET_by_offset(disp, _gloffset_EGLImageTargetTexture2DOES));
}

static inline void SET_EGLImageTargetTexture2DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLvoid *)) {
   SET_by_offset(disp, _gloffset_EGLImageTargetTexture2DOES, fn);
}


#endif /* !defined( _DISPATCH_H_ ) */
