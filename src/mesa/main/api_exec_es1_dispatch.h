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
#define _gloffset_COUNT 188

#define _gloffset_Color4f 29
#define _gloffset_Color4ub 35
#define _gloffset_Normal3f 56
#define _gloffset_CullFace 152
#define _gloffset_Fogf 153
#define _gloffset_Fogfv 154
#define _gloffset_FrontFace 157
#define _gloffset_Hint 158
#define _gloffset_Lightf 159
#define _gloffset_Lightfv 160
#define _gloffset_LightModelf 163
#define _gloffset_LightModelfv 164
#define _gloffset_LineWidth 168
#define _gloffset_Materialf 169
#define _gloffset_Materialfv 170
#define _gloffset_PointSize 173
#define _gloffset_Scissor 176
#define _gloffset_ShadeModel 177
#define _gloffset_TexParameterf 178
#define _gloffset_TexParameterfv 179
#define _gloffset_TexParameteri 180
#define _gloffset_TexParameteriv 181
#define _gloffset_TexImage2D 183
#define _gloffset_TexEnvf 184
#define _gloffset_TexEnvfv 185
#define _gloffset_TexEnvi 186
#define _gloffset_TexEnviv 187
#define _gloffset_TexGenfOES 190
#define _gloffset_TexGenfvOES 191
#define _gloffset_TexGeniOES 192
#define _gloffset_TexGenivOES 193
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
#define _gloffset_AlphaFunc 240
#define _gloffset_BlendFunc 241
#define _gloffset_LogicOp 242
#define _gloffset_StencilFunc 243
#define _gloffset_StencilOp 244
#define _gloffset_DepthFunc 245
#define _gloffset_PixelStorei 250
#define _gloffset_ReadPixels 256
#define _gloffset_GetBooleanv 258
#define _gloffset_GetError 261
#define _gloffset_GetFloatv 262
#define _gloffset_GetIntegerv 263
#define _gloffset_GetLightfv 264
#define _gloffset_GetMaterialfv 269
#define _gloffset_GetString 275
#define _gloffset_GetTexEnvfv 276
#define _gloffset_GetTexEnviv 277
#define _gloffset_GetTexGenfvOES 279
#define _gloffset_GetTexGenivOES 280
#define _gloffset_GetTexParameterfv 282
#define _gloffset_GetTexParameteriv 283
#define _gloffset_IsEnabled 286
#define _gloffset_LoadIdentity 290
#define _gloffset_LoadMatrixf 291
#define _gloffset_MatrixMode 293
#define _gloffset_MultMatrixf 294
#define _gloffset_PopMatrix 297
#define _gloffset_PushMatrix 298
#define _gloffset_Rotatef 300
#define _gloffset_Scalef 302
#define _gloffset_Translatef 304
#define _gloffset_Viewport 305
#define _gloffset_BindTexture 307
#define _gloffset_ColorPointer 308
#define _gloffset_DisableClientState 309
#define _gloffset_DrawArrays 310
#define _gloffset_DrawElements 311
#define _gloffset_EnableClientState 313
#define _gloffset_NormalPointer 318
#define _gloffset_PolygonOffset 319
#define _gloffset_TexCoordPointer 320
#define _gloffset_VertexPointer 321
#define _gloffset_CopyTexImage2D 324
#define _gloffset_CopyTexSubImage2D 326
#define _gloffset_DeleteTextures 327
#define _gloffset_GenTextures 328
#define _gloffset_GetPointerv 329
#define _gloffset_IsTexture 330
#define _gloffset_TexSubImage2D 333
#define _gloffset_BlendEquationOES 337
#define _gloffset_ActiveTexture 374
#define _gloffset_ClientActiveTexture 375
#define _gloffset_MultiTexCoord4f 402

#if !FEATURE_remap_table

#define _gloffset_SampleCoverage 445
#define _gloffset_CompressedTexImage2D 447
#define _gloffset_CompressedTexSubImage2D 450
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
#define _gloffset_ClearDepthf 660
#define _gloffset_DepthRangef 661
#define _gloffset_DrawTexfOES 695
#define _gloffset_DrawTexfvOES 696
#define _gloffset_DrawTexiOES 697
#define _gloffset_DrawTexivOES 698
#define _gloffset_DrawTexsOES 699
#define _gloffset_DrawTexsvOES 700
#define _gloffset_DrawTexxOES 701
#define _gloffset_DrawTexxvOES 702
#define _gloffset_PointSizePointerOES 703
#define _gloffset_QueryMatrixxOES 710
#define _gloffset_PointParameterf 719
#define _gloffset_PointParameterfv 720
#define _gloffset_MultiDrawArraysEXT 740
#define _gloffset_MultiDrawElementsEXT 741
#define _gloffset_BlendFuncSeparateOES 748
#define _gloffset_AlphaFuncxOES 891
#define _gloffset_ClearColorxOES 892
#define _gloffset_ClearDepthxOES 893
#define _gloffset_ClipPlanexOES 894
#define _gloffset_Color4xOES 895
#define _gloffset_DepthRangexOES 896
#define _gloffset_FogxOES 897
#define _gloffset_FogxvOES 898
#define _gloffset_FrustumxOES 899
#define _gloffset_GetClipPlanexOES 900
#define _gloffset_GetFixedvOES 901
#define _gloffset_GetLightxvOES 902
#define _gloffset_GetMaterialxvOES 903
#define _gloffset_GetTexEnvxvOES 904
#define _gloffset_GetTexGenxvOES 905
#define _gloffset_GetTexParameterxvOES 906
#define _gloffset_LightModelxOES 907
#define _gloffset_LightModelxvOES 908
#define _gloffset_LightxOES 909
#define _gloffset_LightxvOES 910
#define _gloffset_LineWidthxOES 911
#define _gloffset_LoadMatrixxOES 912
#define _gloffset_MaterialxOES 913
#define _gloffset_MaterialxvOES 914
#define _gloffset_MultMatrixxOES 915
#define _gloffset_MultiTexCoord4xOES 916
#define _gloffset_Normal3xOES 917
#define _gloffset_OrthoxOES 918
#define _gloffset_PointParameterxOES 919
#define _gloffset_PointParameterxvOES 920
#define _gloffset_PointSizexOES 921
#define _gloffset_PolygonOffsetxOES 922
#define _gloffset_RotatexOES 923
#define _gloffset_SampleCoveragexOES 924
#define _gloffset_ScalexOES 925
#define _gloffset_TexEnvxOES 926
#define _gloffset_TexEnvxvOES 927
#define _gloffset_TexGenxOES 928
#define _gloffset_TexGenxvOES 929
#define _gloffset_TexParameterxOES 930
#define _gloffset_TexParameterxvOES 931
#define _gloffset_TranslatexOES 932
#define _gloffset_ClipPlanefOES 933
#define _gloffset_FrustumfOES 934
#define _gloffset_GetClipPlanefOES 935
#define _gloffset_OrthofOES 936
#define _gloffset_BlendEquationSeparateOES 938
#define _gloffset_BindFramebufferOES 939
#define _gloffset_BindRenderbufferOES 940
#define _gloffset_CheckFramebufferStatusOES 941
#define _gloffset_DeleteFramebuffersOES 942
#define _gloffset_DeleteRenderbuffersOES 943
#define _gloffset_FramebufferRenderbufferOES 944
#define _gloffset_FramebufferTexture2DOES 946
#define _gloffset_GenFramebuffersOES 948
#define _gloffset_GenRenderbuffersOES 949
#define _gloffset_GenerateMipmapOES 950
#define _gloffset_GetFramebufferAttachmentParameterivOES 951
#define _gloffset_GetRenderbufferParameterivOES 952
#define _gloffset_IsFramebufferOES 953
#define _gloffset_IsRenderbufferOES 954
#define _gloffset_RenderbufferStorageOES 955
#define _gloffset_EGLImageTargetRenderbufferStorageOES 1030
#define _gloffset_EGLImageTargetTexture2DOES 1031

#else /* !FEATURE_remap_table */

#define esLocalRemapTable_size 94
static int esLocalRemapTable[ esLocalRemapTable_size ];

#define SampleCoverage_remap_index 0
#define CompressedTexImage2D_remap_index 1
#define CompressedTexSubImage2D_remap_index 2
#define BindBuffer_remap_index 3
#define BufferData_remap_index 4
#define BufferSubData_remap_index 5
#define DeleteBuffers_remap_index 6
#define GenBuffers_remap_index 7
#define GetBufferParameteriv_remap_index 8
#define GetBufferPointervOES_remap_index 9
#define IsBuffer_remap_index 10
#define MapBufferOES_remap_index 11
#define UnmapBufferOES_remap_index 12
#define ClearDepthf_remap_index 13
#define DepthRangef_remap_index 14
#define DrawTexfOES_remap_index 15
#define DrawTexfvOES_remap_index 16
#define DrawTexiOES_remap_index 17
#define DrawTexivOES_remap_index 18
#define DrawTexsOES_remap_index 19
#define DrawTexsvOES_remap_index 20
#define DrawTexxOES_remap_index 21
#define DrawTexxvOES_remap_index 22
#define PointSizePointerOES_remap_index 23
#define QueryMatrixxOES_remap_index 24
#define PointParameterf_remap_index 25
#define PointParameterfv_remap_index 26
#define MultiDrawArraysEXT_remap_index 27
#define MultiDrawElementsEXT_remap_index 28
#define BlendFuncSeparateOES_remap_index 29
#define AlphaFuncxOES_remap_index 30
#define ClearColorxOES_remap_index 31
#define ClearDepthxOES_remap_index 32
#define ClipPlanexOES_remap_index 33
#define Color4xOES_remap_index 34
#define DepthRangexOES_remap_index 35
#define FogxOES_remap_index 36
#define FogxvOES_remap_index 37
#define FrustumxOES_remap_index 38
#define GetClipPlanexOES_remap_index 39
#define GetFixedvOES_remap_index 40
#define GetLightxvOES_remap_index 41
#define GetMaterialxvOES_remap_index 42
#define GetTexEnvxvOES_remap_index 43
#define GetTexGenxvOES_remap_index 44
#define GetTexParameterxvOES_remap_index 45
#define LightModelxOES_remap_index 46
#define LightModelxvOES_remap_index 47
#define LightxOES_remap_index 48
#define LightxvOES_remap_index 49
#define LineWidthxOES_remap_index 50
#define LoadMatrixxOES_remap_index 51
#define MaterialxOES_remap_index 52
#define MaterialxvOES_remap_index 53
#define MultMatrixxOES_remap_index 54
#define MultiTexCoord4xOES_remap_index 55
#define Normal3xOES_remap_index 56
#define OrthoxOES_remap_index 57
#define PointParameterxOES_remap_index 58
#define PointParameterxvOES_remap_index 59
#define PointSizexOES_remap_index 60
#define PolygonOffsetxOES_remap_index 61
#define RotatexOES_remap_index 62
#define SampleCoveragexOES_remap_index 63
#define ScalexOES_remap_index 64
#define TexEnvxOES_remap_index 65
#define TexEnvxvOES_remap_index 66
#define TexGenxOES_remap_index 67
#define TexGenxvOES_remap_index 68
#define TexParameterxOES_remap_index 69
#define TexParameterxvOES_remap_index 70
#define TranslatexOES_remap_index 71
#define ClipPlanefOES_remap_index 72
#define FrustumfOES_remap_index 73
#define GetClipPlanefOES_remap_index 74
#define OrthofOES_remap_index 75
#define BlendEquationSeparateOES_remap_index 76
#define BindFramebufferOES_remap_index 77
#define BindRenderbufferOES_remap_index 78
#define CheckFramebufferStatusOES_remap_index 79
#define DeleteFramebuffersOES_remap_index 80
#define DeleteRenderbuffersOES_remap_index 81
#define FramebufferRenderbufferOES_remap_index 82
#define FramebufferTexture2DOES_remap_index 83
#define GenFramebuffersOES_remap_index 84
#define GenRenderbuffersOES_remap_index 85
#define GenerateMipmapOES_remap_index 86
#define GetFramebufferAttachmentParameterivOES_remap_index 87
#define GetRenderbufferParameterivOES_remap_index 88
#define IsFramebufferOES_remap_index 89
#define IsRenderbufferOES_remap_index 90
#define RenderbufferStorageOES_remap_index 91
#define EGLImageTargetRenderbufferStorageOES_remap_index 92
#define EGLImageTargetTexture2DOES_remap_index 93

#define _gloffset_SampleCoverage esLocalRemapTable[SampleCoverage_remap_index]
#define _gloffset_CompressedTexImage2D esLocalRemapTable[CompressedTexImage2D_remap_index]
#define _gloffset_CompressedTexSubImage2D esLocalRemapTable[CompressedTexSubImage2D_remap_index]
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
#define _gloffset_ClearDepthf esLocalRemapTable[ClearDepthf_remap_index]
#define _gloffset_DepthRangef esLocalRemapTable[DepthRangef_remap_index]
#define _gloffset_DrawTexfOES esLocalRemapTable[DrawTexfOES_remap_index]
#define _gloffset_DrawTexfvOES esLocalRemapTable[DrawTexfvOES_remap_index]
#define _gloffset_DrawTexiOES esLocalRemapTable[DrawTexiOES_remap_index]
#define _gloffset_DrawTexivOES esLocalRemapTable[DrawTexivOES_remap_index]
#define _gloffset_DrawTexsOES esLocalRemapTable[DrawTexsOES_remap_index]
#define _gloffset_DrawTexsvOES esLocalRemapTable[DrawTexsvOES_remap_index]
#define _gloffset_DrawTexxOES esLocalRemapTable[DrawTexxOES_remap_index]
#define _gloffset_DrawTexxvOES esLocalRemapTable[DrawTexxvOES_remap_index]
#define _gloffset_PointSizePointerOES esLocalRemapTable[PointSizePointerOES_remap_index]
#define _gloffset_QueryMatrixxOES esLocalRemapTable[QueryMatrixxOES_remap_index]
#define _gloffset_PointParameterf esLocalRemapTable[PointParameterf_remap_index]
#define _gloffset_PointParameterfv esLocalRemapTable[PointParameterfv_remap_index]
#define _gloffset_MultiDrawArraysEXT esLocalRemapTable[MultiDrawArraysEXT_remap_index]
#define _gloffset_MultiDrawElementsEXT esLocalRemapTable[MultiDrawElementsEXT_remap_index]
#define _gloffset_BlendFuncSeparateOES esLocalRemapTable[BlendFuncSeparateOES_remap_index]
#define _gloffset_AlphaFuncxOES esLocalRemapTable[AlphaFuncxOES_remap_index]
#define _gloffset_ClearColorxOES esLocalRemapTable[ClearColorxOES_remap_index]
#define _gloffset_ClearDepthxOES esLocalRemapTable[ClearDepthxOES_remap_index]
#define _gloffset_ClipPlanexOES esLocalRemapTable[ClipPlanexOES_remap_index]
#define _gloffset_Color4xOES esLocalRemapTable[Color4xOES_remap_index]
#define _gloffset_DepthRangexOES esLocalRemapTable[DepthRangexOES_remap_index]
#define _gloffset_FogxOES esLocalRemapTable[FogxOES_remap_index]
#define _gloffset_FogxvOES esLocalRemapTable[FogxvOES_remap_index]
#define _gloffset_FrustumxOES esLocalRemapTable[FrustumxOES_remap_index]
#define _gloffset_GetClipPlanexOES esLocalRemapTable[GetClipPlanexOES_remap_index]
#define _gloffset_GetFixedvOES esLocalRemapTable[GetFixedvOES_remap_index]
#define _gloffset_GetLightxvOES esLocalRemapTable[GetLightxvOES_remap_index]
#define _gloffset_GetMaterialxvOES esLocalRemapTable[GetMaterialxvOES_remap_index]
#define _gloffset_GetTexEnvxvOES esLocalRemapTable[GetTexEnvxvOES_remap_index]
#define _gloffset_GetTexGenxvOES esLocalRemapTable[GetTexGenxvOES_remap_index]
#define _gloffset_GetTexParameterxvOES esLocalRemapTable[GetTexParameterxvOES_remap_index]
#define _gloffset_LightModelxOES esLocalRemapTable[LightModelxOES_remap_index]
#define _gloffset_LightModelxvOES esLocalRemapTable[LightModelxvOES_remap_index]
#define _gloffset_LightxOES esLocalRemapTable[LightxOES_remap_index]
#define _gloffset_LightxvOES esLocalRemapTable[LightxvOES_remap_index]
#define _gloffset_LineWidthxOES esLocalRemapTable[LineWidthxOES_remap_index]
#define _gloffset_LoadMatrixxOES esLocalRemapTable[LoadMatrixxOES_remap_index]
#define _gloffset_MaterialxOES esLocalRemapTable[MaterialxOES_remap_index]
#define _gloffset_MaterialxvOES esLocalRemapTable[MaterialxvOES_remap_index]
#define _gloffset_MultMatrixxOES esLocalRemapTable[MultMatrixxOES_remap_index]
#define _gloffset_MultiTexCoord4xOES esLocalRemapTable[MultiTexCoord4xOES_remap_index]
#define _gloffset_Normal3xOES esLocalRemapTable[Normal3xOES_remap_index]
#define _gloffset_OrthoxOES esLocalRemapTable[OrthoxOES_remap_index]
#define _gloffset_PointParameterxOES esLocalRemapTable[PointParameterxOES_remap_index]
#define _gloffset_PointParameterxvOES esLocalRemapTable[PointParameterxvOES_remap_index]
#define _gloffset_PointSizexOES esLocalRemapTable[PointSizexOES_remap_index]
#define _gloffset_PolygonOffsetxOES esLocalRemapTable[PolygonOffsetxOES_remap_index]
#define _gloffset_RotatexOES esLocalRemapTable[RotatexOES_remap_index]
#define _gloffset_SampleCoveragexOES esLocalRemapTable[SampleCoveragexOES_remap_index]
#define _gloffset_ScalexOES esLocalRemapTable[ScalexOES_remap_index]
#define _gloffset_TexEnvxOES esLocalRemapTable[TexEnvxOES_remap_index]
#define _gloffset_TexEnvxvOES esLocalRemapTable[TexEnvxvOES_remap_index]
#define _gloffset_TexGenxOES esLocalRemapTable[TexGenxOES_remap_index]
#define _gloffset_TexGenxvOES esLocalRemapTable[TexGenxvOES_remap_index]
#define _gloffset_TexParameterxOES esLocalRemapTable[TexParameterxOES_remap_index]
#define _gloffset_TexParameterxvOES esLocalRemapTable[TexParameterxvOES_remap_index]
#define _gloffset_TranslatexOES esLocalRemapTable[TranslatexOES_remap_index]
#define _gloffset_ClipPlanefOES esLocalRemapTable[ClipPlanefOES_remap_index]
#define _gloffset_FrustumfOES esLocalRemapTable[FrustumfOES_remap_index]
#define _gloffset_GetClipPlanefOES esLocalRemapTable[GetClipPlanefOES_remap_index]
#define _gloffset_OrthofOES esLocalRemapTable[OrthofOES_remap_index]
#define _gloffset_BlendEquationSeparateOES esLocalRemapTable[BlendEquationSeparateOES_remap_index]
#define _gloffset_BindFramebufferOES esLocalRemapTable[BindFramebufferOES_remap_index]
#define _gloffset_BindRenderbufferOES esLocalRemapTable[BindRenderbufferOES_remap_index]
#define _gloffset_CheckFramebufferStatusOES esLocalRemapTable[CheckFramebufferStatusOES_remap_index]
#define _gloffset_DeleteFramebuffersOES esLocalRemapTable[DeleteFramebuffersOES_remap_index]
#define _gloffset_DeleteRenderbuffersOES esLocalRemapTable[DeleteRenderbuffersOES_remap_index]
#define _gloffset_FramebufferRenderbufferOES esLocalRemapTable[FramebufferRenderbufferOES_remap_index]
#define _gloffset_FramebufferTexture2DOES esLocalRemapTable[FramebufferTexture2DOES_remap_index]
#define _gloffset_GenFramebuffersOES esLocalRemapTable[GenFramebuffersOES_remap_index]
#define _gloffset_GenRenderbuffersOES esLocalRemapTable[GenRenderbuffersOES_remap_index]
#define _gloffset_GenerateMipmapOES esLocalRemapTable[GenerateMipmapOES_remap_index]
#define _gloffset_GetFramebufferAttachmentParameterivOES esLocalRemapTable[GetFramebufferAttachmentParameterivOES_remap_index]
#define _gloffset_GetRenderbufferParameterivOES esLocalRemapTable[GetRenderbufferParameterivOES_remap_index]
#define _gloffset_IsFramebufferOES esLocalRemapTable[IsFramebufferOES_remap_index]
#define _gloffset_IsRenderbufferOES esLocalRemapTable[IsRenderbufferOES_remap_index]
#define _gloffset_RenderbufferStorageOES esLocalRemapTable[RenderbufferStorageOES_remap_index]
#define _gloffset_EGLImageTargetRenderbufferStorageOES esLocalRemapTable[EGLImageTargetRenderbufferStorageOES_remap_index]
#define _gloffset_EGLImageTargetTexture2DOES esLocalRemapTable[EGLImageTargetTexture2DOES_remap_index]

#endif /* !FEATURE_remap_table */

typedef void (GLAPIENTRYP _glptr_Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Color4f(disp, parameters) \
    (* GET_Color4f(disp)) parameters
static inline _glptr_Color4f GET_Color4f(struct _glapi_table *disp) {
   return (_glptr_Color4f) (GET_by_offset(disp, _gloffset_Color4f));
}

static inline void SET_Color4f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Color4f, fn);
}

typedef void (GLAPIENTRYP _glptr_Color4ub)(GLubyte, GLubyte, GLubyte, GLubyte);
#define CALL_Color4ub(disp, parameters) \
    (* GET_Color4ub(disp)) parameters
static inline _glptr_Color4ub GET_Color4ub(struct _glapi_table *disp) {
   return (_glptr_Color4ub) (GET_by_offset(disp, _gloffset_Color4ub));
}

static inline void SET_Color4ub(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLubyte, GLubyte, GLubyte, GLubyte)) {
   SET_by_offset(disp, _gloffset_Color4ub, fn);
}

typedef void (GLAPIENTRYP _glptr_Normal3f)(GLfloat, GLfloat, GLfloat);
#define CALL_Normal3f(disp, parameters) \
    (* GET_Normal3f(disp)) parameters
static inline _glptr_Normal3f GET_Normal3f(struct _glapi_table *disp) {
   return (_glptr_Normal3f) (GET_by_offset(disp, _gloffset_Normal3f));
}

static inline void SET_Normal3f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Normal3f, fn);
}

typedef void (GLAPIENTRYP _glptr_CullFace)(GLenum);
#define CALL_CullFace(disp, parameters) \
    (* GET_CullFace(disp)) parameters
static inline _glptr_CullFace GET_CullFace(struct _glapi_table *disp) {
   return (_glptr_CullFace) (GET_by_offset(disp, _gloffset_CullFace));
}

static inline void SET_CullFace(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_CullFace, fn);
}

typedef void (GLAPIENTRYP _glptr_Fogf)(GLenum, GLfloat);
#define CALL_Fogf(disp, parameters) \
    (* GET_Fogf(disp)) parameters
static inline _glptr_Fogf GET_Fogf(struct _glapi_table *disp) {
   return (_glptr_Fogf) (GET_by_offset(disp, _gloffset_Fogf));
}

static inline void SET_Fogf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_Fogf, fn);
}

typedef void (GLAPIENTRYP _glptr_Fogfv)(GLenum, const GLfloat *);
#define CALL_Fogfv(disp, parameters) \
    (* GET_Fogfv(disp)) parameters
static inline _glptr_Fogfv GET_Fogfv(struct _glapi_table *disp) {
   return (_glptr_Fogfv) (GET_by_offset(disp, _gloffset_Fogfv));
}

static inline void SET_Fogfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Fogfv, fn);
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

typedef void (GLAPIENTRYP _glptr_Lightf)(GLenum, GLenum, GLfloat);
#define CALL_Lightf(disp, parameters) \
    (* GET_Lightf(disp)) parameters
static inline _glptr_Lightf GET_Lightf(struct _glapi_table *disp) {
   return (_glptr_Lightf) (GET_by_offset(disp, _gloffset_Lightf));
}

static inline void SET_Lightf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_Lightf, fn);
}

typedef void (GLAPIENTRYP _glptr_Lightfv)(GLenum, GLenum, const GLfloat *);
#define CALL_Lightfv(disp, parameters) \
    (* GET_Lightfv(disp)) parameters
static inline _glptr_Lightfv GET_Lightfv(struct _glapi_table *disp) {
   return (_glptr_Lightfv) (GET_by_offset(disp, _gloffset_Lightfv));
}

static inline void SET_Lightfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Lightfv, fn);
}

typedef void (GLAPIENTRYP _glptr_LightModelf)(GLenum, GLfloat);
#define CALL_LightModelf(disp, parameters) \
    (* GET_LightModelf(disp)) parameters
static inline _glptr_LightModelf GET_LightModelf(struct _glapi_table *disp) {
   return (_glptr_LightModelf) (GET_by_offset(disp, _gloffset_LightModelf));
}

static inline void SET_LightModelf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_LightModelf, fn);
}

typedef void (GLAPIENTRYP _glptr_LightModelfv)(GLenum, const GLfloat *);
#define CALL_LightModelfv(disp, parameters) \
    (* GET_LightModelfv(disp)) parameters
static inline _glptr_LightModelfv GET_LightModelfv(struct _glapi_table *disp) {
   return (_glptr_LightModelfv) (GET_by_offset(disp, _gloffset_LightModelfv));
}

static inline void SET_LightModelfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_LightModelfv, fn);
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

typedef void (GLAPIENTRYP _glptr_Materialf)(GLenum, GLenum, GLfloat);
#define CALL_Materialf(disp, parameters) \
    (* GET_Materialf(disp)) parameters
static inline _glptr_Materialf GET_Materialf(struct _glapi_table *disp) {
   return (_glptr_Materialf) (GET_by_offset(disp, _gloffset_Materialf));
}

static inline void SET_Materialf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_Materialf, fn);
}

typedef void (GLAPIENTRYP _glptr_Materialfv)(GLenum, GLenum, const GLfloat *);
#define CALL_Materialfv(disp, parameters) \
    (* GET_Materialfv(disp)) parameters
static inline _glptr_Materialfv GET_Materialfv(struct _glapi_table *disp) {
   return (_glptr_Materialfv) (GET_by_offset(disp, _gloffset_Materialfv));
}

static inline void SET_Materialfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_Materialfv, fn);
}

typedef void (GLAPIENTRYP _glptr_PointSize)(GLfloat);
#define CALL_PointSize(disp, parameters) \
    (* GET_PointSize(disp)) parameters
static inline _glptr_PointSize GET_PointSize(struct _glapi_table *disp) {
   return (_glptr_PointSize) (GET_by_offset(disp, _gloffset_PointSize));
}

static inline void SET_PointSize(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat)) {
   SET_by_offset(disp, _gloffset_PointSize, fn);
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

typedef void (GLAPIENTRYP _glptr_ShadeModel)(GLenum);
#define CALL_ShadeModel(disp, parameters) \
    (* GET_ShadeModel(disp)) parameters
static inline _glptr_ShadeModel GET_ShadeModel(struct _glapi_table *disp) {
   return (_glptr_ShadeModel) (GET_by_offset(disp, _gloffset_ShadeModel));
}

static inline void SET_ShadeModel(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_ShadeModel, fn);
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

typedef void (GLAPIENTRYP _glptr_TexEnvf)(GLenum, GLenum, GLfloat);
#define CALL_TexEnvf(disp, parameters) \
    (* GET_TexEnvf(disp)) parameters
static inline _glptr_TexEnvf GET_TexEnvf(struct _glapi_table *disp) {
   return (_glptr_TexEnvf) (GET_by_offset(disp, _gloffset_TexEnvf));
}

static inline void SET_TexEnvf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_TexEnvf, fn);
}

typedef void (GLAPIENTRYP _glptr_TexEnvfv)(GLenum, GLenum, const GLfloat *);
#define CALL_TexEnvfv(disp, parameters) \
    (* GET_TexEnvfv(disp)) parameters
static inline _glptr_TexEnvfv GET_TexEnvfv(struct _glapi_table *disp) {
   return (_glptr_TexEnvfv) (GET_by_offset(disp, _gloffset_TexEnvfv));
}

static inline void SET_TexEnvfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_TexEnvfv, fn);
}

typedef void (GLAPIENTRYP _glptr_TexEnvi)(GLenum, GLenum, GLint);
#define CALL_TexEnvi(disp, parameters) \
    (* GET_TexEnvi(disp)) parameters
static inline _glptr_TexEnvi GET_TexEnvi(struct _glapi_table *disp) {
   return (_glptr_TexEnvi) (GET_by_offset(disp, _gloffset_TexEnvi));
}

static inline void SET_TexEnvi(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint)) {
   SET_by_offset(disp, _gloffset_TexEnvi, fn);
}

typedef void (GLAPIENTRYP _glptr_TexEnviv)(GLenum, GLenum, const GLint *);
#define CALL_TexEnviv(disp, parameters) \
    (* GET_TexEnviv(disp)) parameters
static inline _glptr_TexEnviv GET_TexEnviv(struct _glapi_table *disp) {
   return (_glptr_TexEnviv) (GET_by_offset(disp, _gloffset_TexEnviv));
}

static inline void SET_TexEnviv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *)) {
   SET_by_offset(disp, _gloffset_TexEnviv, fn);
}

typedef void (GLAPIENTRYP _glptr_TexGenfOES)(GLenum, GLenum, GLfloat);
#define CALL_TexGenfOES(disp, parameters) \
    (* GET_TexGenfOES(disp)) parameters
static inline _glptr_TexGenfOES GET_TexGenfOES(struct _glapi_table *disp) {
   return (_glptr_TexGenfOES) (GET_by_offset(disp, _gloffset_TexGenfOES));
}

static inline void SET_TexGenfOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_TexGenfOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexGenfvOES)(GLenum, GLenum, const GLfloat *);
#define CALL_TexGenfvOES(disp, parameters) \
    (* GET_TexGenfvOES(disp)) parameters
static inline _glptr_TexGenfvOES GET_TexGenfvOES(struct _glapi_table *disp) {
   return (_glptr_TexGenfvOES) (GET_by_offset(disp, _gloffset_TexGenfvOES));
}

static inline void SET_TexGenfvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_TexGenfvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexGeniOES)(GLenum, GLenum, GLint);
#define CALL_TexGeniOES(disp, parameters) \
    (* GET_TexGeniOES(disp)) parameters
static inline _glptr_TexGeniOES GET_TexGeniOES(struct _glapi_table *disp) {
   return (_glptr_TexGeniOES) (GET_by_offset(disp, _gloffset_TexGeniOES));
}

static inline void SET_TexGeniOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint)) {
   SET_by_offset(disp, _gloffset_TexGeniOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexGenivOES)(GLenum, GLenum, const GLint *);
#define CALL_TexGenivOES(disp, parameters) \
    (* GET_TexGenivOES(disp)) parameters
static inline _glptr_TexGenivOES GET_TexGenivOES(struct _glapi_table *disp) {
   return (_glptr_TexGenivOES) (GET_by_offset(disp, _gloffset_TexGenivOES));
}

static inline void SET_TexGenivOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLint *)) {
   SET_by_offset(disp, _gloffset_TexGenivOES, fn);
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

typedef void (GLAPIENTRYP _glptr_AlphaFunc)(GLenum, GLclampf);
#define CALL_AlphaFunc(disp, parameters) \
    (* GET_AlphaFunc(disp)) parameters
static inline _glptr_AlphaFunc GET_AlphaFunc(struct _glapi_table *disp) {
   return (_glptr_AlphaFunc) (GET_by_offset(disp, _gloffset_AlphaFunc));
}

static inline void SET_AlphaFunc(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLclampf)) {
   SET_by_offset(disp, _gloffset_AlphaFunc, fn);
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

typedef void (GLAPIENTRYP _glptr_LogicOp)(GLenum);
#define CALL_LogicOp(disp, parameters) \
    (* GET_LogicOp(disp)) parameters
static inline _glptr_LogicOp GET_LogicOp(struct _glapi_table *disp) {
   return (_glptr_LogicOp) (GET_by_offset(disp, _gloffset_LogicOp));
}

static inline void SET_LogicOp(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_LogicOp, fn);
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

typedef void (GLAPIENTRYP _glptr_GetLightfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetLightfv(disp, parameters) \
    (* GET_GetLightfv(disp)) parameters
static inline _glptr_GetLightfv GET_GetLightfv(struct _glapi_table *disp) {
   return (_glptr_GetLightfv) (GET_by_offset(disp, _gloffset_GetLightfv));
}

static inline void SET_GetLightfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetLightfv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetMaterialfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetMaterialfv(disp, parameters) \
    (* GET_GetMaterialfv(disp)) parameters
static inline _glptr_GetMaterialfv GET_GetMaterialfv(struct _glapi_table *disp) {
   return (_glptr_GetMaterialfv) (GET_by_offset(disp, _gloffset_GetMaterialfv));
}

static inline void SET_GetMaterialfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetMaterialfv, fn);
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

typedef void (GLAPIENTRYP _glptr_GetTexEnvfv)(GLenum, GLenum, GLfloat *);
#define CALL_GetTexEnvfv(disp, parameters) \
    (* GET_GetTexEnvfv(disp)) parameters
static inline _glptr_GetTexEnvfv GET_GetTexEnvfv(struct _glapi_table *disp) {
   return (_glptr_GetTexEnvfv) (GET_by_offset(disp, _gloffset_GetTexEnvfv));
}

static inline void SET_GetTexEnvfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetTexEnvfv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexEnviv)(GLenum, GLenum, GLint *);
#define CALL_GetTexEnviv(disp, parameters) \
    (* GET_GetTexEnviv(disp)) parameters
static inline _glptr_GetTexEnviv GET_GetTexEnviv(struct _glapi_table *disp) {
   return (_glptr_GetTexEnviv) (GET_by_offset(disp, _gloffset_GetTexEnviv));
}

static inline void SET_GetTexEnviv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetTexEnviv, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexGenfvOES)(GLenum, GLenum, GLfloat *);
#define CALL_GetTexGenfvOES(disp, parameters) \
    (* GET_GetTexGenfvOES(disp)) parameters
static inline _glptr_GetTexGenfvOES GET_GetTexGenfvOES(struct _glapi_table *disp) {
   return (_glptr_GetTexGenfvOES) (GET_by_offset(disp, _gloffset_GetTexGenfvOES));
}

static inline void SET_GetTexGenfvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetTexGenfvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexGenivOES)(GLenum, GLenum, GLint *);
#define CALL_GetTexGenivOES(disp, parameters) \
    (* GET_GetTexGenivOES(disp)) parameters
static inline _glptr_GetTexGenivOES GET_GetTexGenivOES(struct _glapi_table *disp) {
   return (_glptr_GetTexGenivOES) (GET_by_offset(disp, _gloffset_GetTexGenivOES));
}

static inline void SET_GetTexGenivOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetTexGenivOES, fn);
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

typedef void (GLAPIENTRYP _glptr_LoadIdentity)(void);
#define CALL_LoadIdentity(disp, parameters) \
    (* GET_LoadIdentity(disp)) parameters
static inline _glptr_LoadIdentity GET_LoadIdentity(struct _glapi_table *disp) {
   return (_glptr_LoadIdentity) (GET_by_offset(disp, _gloffset_LoadIdentity));
}

static inline void SET_LoadIdentity(struct _glapi_table *disp, void (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_LoadIdentity, fn);
}

typedef void (GLAPIENTRYP _glptr_LoadMatrixf)(const GLfloat *);
#define CALL_LoadMatrixf(disp, parameters) \
    (* GET_LoadMatrixf(disp)) parameters
static inline _glptr_LoadMatrixf GET_LoadMatrixf(struct _glapi_table *disp) {
   return (_glptr_LoadMatrixf) (GET_by_offset(disp, _gloffset_LoadMatrixf));
}

static inline void SET_LoadMatrixf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLfloat *)) {
   SET_by_offset(disp, _gloffset_LoadMatrixf, fn);
}

typedef void (GLAPIENTRYP _glptr_MatrixMode)(GLenum);
#define CALL_MatrixMode(disp, parameters) \
    (* GET_MatrixMode(disp)) parameters
static inline _glptr_MatrixMode GET_MatrixMode(struct _glapi_table *disp) {
   return (_glptr_MatrixMode) (GET_by_offset(disp, _gloffset_MatrixMode));
}

static inline void SET_MatrixMode(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_MatrixMode, fn);
}

typedef void (GLAPIENTRYP _glptr_MultMatrixf)(const GLfloat *);
#define CALL_MultMatrixf(disp, parameters) \
    (* GET_MultMatrixf(disp)) parameters
static inline _glptr_MultMatrixf GET_MultMatrixf(struct _glapi_table *disp) {
   return (_glptr_MultMatrixf) (GET_by_offset(disp, _gloffset_MultMatrixf));
}

static inline void SET_MultMatrixf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLfloat *)) {
   SET_by_offset(disp, _gloffset_MultMatrixf, fn);
}

typedef void (GLAPIENTRYP _glptr_PopMatrix)(void);
#define CALL_PopMatrix(disp, parameters) \
    (* GET_PopMatrix(disp)) parameters
static inline _glptr_PopMatrix GET_PopMatrix(struct _glapi_table *disp) {
   return (_glptr_PopMatrix) (GET_by_offset(disp, _gloffset_PopMatrix));
}

static inline void SET_PopMatrix(struct _glapi_table *disp, void (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_PopMatrix, fn);
}

typedef void (GLAPIENTRYP _glptr_PushMatrix)(void);
#define CALL_PushMatrix(disp, parameters) \
    (* GET_PushMatrix(disp)) parameters
static inline _glptr_PushMatrix GET_PushMatrix(struct _glapi_table *disp) {
   return (_glptr_PushMatrix) (GET_by_offset(disp, _gloffset_PushMatrix));
}

static inline void SET_PushMatrix(struct _glapi_table *disp, void (GLAPIENTRYP fn)(void)) {
   SET_by_offset(disp, _gloffset_PushMatrix, fn);
}

typedef void (GLAPIENTRYP _glptr_Rotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_Rotatef(disp, parameters) \
    (* GET_Rotatef(disp)) parameters
static inline _glptr_Rotatef GET_Rotatef(struct _glapi_table *disp) {
   return (_glptr_Rotatef) (GET_by_offset(disp, _gloffset_Rotatef));
}

static inline void SET_Rotatef(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Rotatef, fn);
}

typedef void (GLAPIENTRYP _glptr_Scalef)(GLfloat, GLfloat, GLfloat);
#define CALL_Scalef(disp, parameters) \
    (* GET_Scalef(disp)) parameters
static inline _glptr_Scalef GET_Scalef(struct _glapi_table *disp) {
   return (_glptr_Scalef) (GET_by_offset(disp, _gloffset_Scalef));
}

static inline void SET_Scalef(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Scalef, fn);
}

typedef void (GLAPIENTRYP _glptr_Translatef)(GLfloat, GLfloat, GLfloat);
#define CALL_Translatef(disp, parameters) \
    (* GET_Translatef(disp)) parameters
static inline _glptr_Translatef GET_Translatef(struct _glapi_table *disp) {
   return (_glptr_Translatef) (GET_by_offset(disp, _gloffset_Translatef));
}

static inline void SET_Translatef(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_Translatef, fn);
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

typedef void (GLAPIENTRYP _glptr_ColorPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_ColorPointer(disp, parameters) \
    (* GET_ColorPointer(disp)) parameters
static inline _glptr_ColorPointer GET_ColorPointer(struct _glapi_table *disp) {
   return (_glptr_ColorPointer) (GET_by_offset(disp, _gloffset_ColorPointer));
}

static inline void SET_ColorPointer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_ColorPointer, fn);
}

typedef void (GLAPIENTRYP _glptr_DisableClientState)(GLenum);
#define CALL_DisableClientState(disp, parameters) \
    (* GET_DisableClientState(disp)) parameters
static inline _glptr_DisableClientState GET_DisableClientState(struct _glapi_table *disp) {
   return (_glptr_DisableClientState) (GET_by_offset(disp, _gloffset_DisableClientState));
}

static inline void SET_DisableClientState(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_DisableClientState, fn);
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

typedef void (GLAPIENTRYP _glptr_EnableClientState)(GLenum);
#define CALL_EnableClientState(disp, parameters) \
    (* GET_EnableClientState(disp)) parameters
static inline _glptr_EnableClientState GET_EnableClientState(struct _glapi_table *disp) {
   return (_glptr_EnableClientState) (GET_by_offset(disp, _gloffset_EnableClientState));
}

static inline void SET_EnableClientState(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_EnableClientState, fn);
}

typedef void (GLAPIENTRYP _glptr_NormalPointer)(GLenum, GLsizei, const GLvoid *);
#define CALL_NormalPointer(disp, parameters) \
    (* GET_NormalPointer(disp)) parameters
static inline _glptr_NormalPointer GET_NormalPointer(struct _glapi_table *disp) {
   return (_glptr_NormalPointer) (GET_by_offset(disp, _gloffset_NormalPointer));
}

static inline void SET_NormalPointer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_NormalPointer, fn);
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

typedef void (GLAPIENTRYP _glptr_TexCoordPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_TexCoordPointer(disp, parameters) \
    (* GET_TexCoordPointer(disp)) parameters
static inline _glptr_TexCoordPointer GET_TexCoordPointer(struct _glapi_table *disp) {
   return (_glptr_TexCoordPointer) (GET_by_offset(disp, _gloffset_TexCoordPointer));
}

static inline void SET_TexCoordPointer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_TexCoordPointer, fn);
}

typedef void (GLAPIENTRYP _glptr_VertexPointer)(GLint, GLenum, GLsizei, const GLvoid *);
#define CALL_VertexPointer(disp, parameters) \
    (* GET_VertexPointer(disp)) parameters
static inline _glptr_VertexPointer GET_VertexPointer(struct _glapi_table *disp) {
   return (_glptr_VertexPointer) (GET_by_offset(disp, _gloffset_VertexPointer));
}

static inline void SET_VertexPointer(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_VertexPointer, fn);
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

typedef void (GLAPIENTRYP _glptr_GetPointerv)(GLenum, GLvoid **);
#define CALL_GetPointerv(disp, parameters) \
    (* GET_GetPointerv(disp)) parameters
static inline _glptr_GetPointerv GET_GetPointerv(struct _glapi_table *disp) {
   return (_glptr_GetPointerv) (GET_by_offset(disp, _gloffset_GetPointerv));
}

static inline void SET_GetPointerv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLvoid **)) {
   SET_by_offset(disp, _gloffset_GetPointerv, fn);
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

typedef void (GLAPIENTRYP _glptr_BlendEquationOES)(GLenum);
#define CALL_BlendEquationOES(disp, parameters) \
    (* GET_BlendEquationOES(disp)) parameters
static inline _glptr_BlendEquationOES GET_BlendEquationOES(struct _glapi_table *disp) {
   return (_glptr_BlendEquationOES) (GET_by_offset(disp, _gloffset_BlendEquationOES));
}

static inline void SET_BlendEquationOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_BlendEquationOES, fn);
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

typedef void (GLAPIENTRYP _glptr_ClientActiveTexture)(GLenum);
#define CALL_ClientActiveTexture(disp, parameters) \
    (* GET_ClientActiveTexture(disp)) parameters
static inline _glptr_ClientActiveTexture GET_ClientActiveTexture(struct _glapi_table *disp) {
   return (_glptr_ClientActiveTexture) (GET_by_offset(disp, _gloffset_ClientActiveTexture));
}

static inline void SET_ClientActiveTexture(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_ClientActiveTexture, fn);
}

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4f)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_MultiTexCoord4f(disp, parameters) \
    (* GET_MultiTexCoord4f(disp)) parameters
static inline _glptr_MultiTexCoord4f GET_MultiTexCoord4f(struct _glapi_table *disp) {
   return (_glptr_MultiTexCoord4f) (GET_by_offset(disp, _gloffset_MultiTexCoord4f));
}

static inline void SET_MultiTexCoord4f(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_MultiTexCoord4f, fn);
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

typedef void (GLAPIENTRYP _glptr_CompressedTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *);
#define CALL_CompressedTexSubImage2D(disp, parameters) \
    (* GET_CompressedTexSubImage2D(disp)) parameters
static inline _glptr_CompressedTexSubImage2D GET_CompressedTexSubImage2D(struct _glapi_table *disp) {
   return (_glptr_CompressedTexSubImage2D) (GET_by_offset(disp, _gloffset_CompressedTexSubImage2D));
}

static inline void SET_CompressedTexSubImage2D(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_CompressedTexSubImage2D, fn);
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

typedef void (GLAPIENTRYP _glptr_DrawTexfOES)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_DrawTexfOES(disp, parameters) \
    (* GET_DrawTexfOES(disp)) parameters
static inline _glptr_DrawTexfOES GET_DrawTexfOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexfOES) (GET_by_offset(disp, _gloffset_DrawTexfOES));
}

static inline void SET_DrawTexfOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_DrawTexfOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexfvOES)(const GLfloat *);
#define CALL_DrawTexfvOES(disp, parameters) \
    (* GET_DrawTexfvOES(disp)) parameters
static inline _glptr_DrawTexfvOES GET_DrawTexfvOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexfvOES) (GET_by_offset(disp, _gloffset_DrawTexfvOES));
}

static inline void SET_DrawTexfvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLfloat *)) {
   SET_by_offset(disp, _gloffset_DrawTexfvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexiOES)(GLint, GLint, GLint, GLint, GLint);
#define CALL_DrawTexiOES(disp, parameters) \
    (* GET_DrawTexiOES(disp)) parameters
static inline _glptr_DrawTexiOES GET_DrawTexiOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexiOES) (GET_by_offset(disp, _gloffset_DrawTexiOES));
}

static inline void SET_DrawTexiOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLint, GLint, GLint, GLint, GLint)) {
   SET_by_offset(disp, _gloffset_DrawTexiOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexivOES)(const GLint *);
#define CALL_DrawTexivOES(disp, parameters) \
    (* GET_DrawTexivOES(disp)) parameters
static inline _glptr_DrawTexivOES GET_DrawTexivOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexivOES) (GET_by_offset(disp, _gloffset_DrawTexivOES));
}

static inline void SET_DrawTexivOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLint *)) {
   SET_by_offset(disp, _gloffset_DrawTexivOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexsOES)(GLshort, GLshort, GLshort, GLshort, GLshort);
#define CALL_DrawTexsOES(disp, parameters) \
    (* GET_DrawTexsOES(disp)) parameters
static inline _glptr_DrawTexsOES GET_DrawTexsOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexsOES) (GET_by_offset(disp, _gloffset_DrawTexsOES));
}

static inline void SET_DrawTexsOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLshort, GLshort, GLshort, GLshort, GLshort)) {
   SET_by_offset(disp, _gloffset_DrawTexsOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexsvOES)(const GLshort *);
#define CALL_DrawTexsvOES(disp, parameters) \
    (* GET_DrawTexsvOES(disp)) parameters
static inline _glptr_DrawTexsvOES GET_DrawTexsvOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexsvOES) (GET_by_offset(disp, _gloffset_DrawTexsvOES));
}

static inline void SET_DrawTexsvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLshort *)) {
   SET_by_offset(disp, _gloffset_DrawTexsvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexxOES)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_DrawTexxOES(disp, parameters) \
    (* GET_DrawTexxOES(disp)) parameters
static inline _glptr_DrawTexxOES GET_DrawTexxOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexxOES) (GET_by_offset(disp, _gloffset_DrawTexxOES));
}

static inline void SET_DrawTexxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_DrawTexxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DrawTexxvOES)(const GLfixed *);
#define CALL_DrawTexxvOES(disp, parameters) \
    (* GET_DrawTexxvOES(disp)) parameters
static inline _glptr_DrawTexxvOES GET_DrawTexxvOES(struct _glapi_table *disp) {
   return (_glptr_DrawTexxvOES) (GET_by_offset(disp, _gloffset_DrawTexxvOES));
}

static inline void SET_DrawTexxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLfixed *)) {
   SET_by_offset(disp, _gloffset_DrawTexxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_PointSizePointerOES)(GLenum, GLsizei, const GLvoid *);
#define CALL_PointSizePointerOES(disp, parameters) \
    (* GET_PointSizePointerOES(disp)) parameters
static inline _glptr_PointSizePointerOES GET_PointSizePointerOES(struct _glapi_table *disp) {
   return (_glptr_PointSizePointerOES) (GET_by_offset(disp, _gloffset_PointSizePointerOES));
}

static inline void SET_PointSizePointerOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLsizei, const GLvoid *)) {
   SET_by_offset(disp, _gloffset_PointSizePointerOES, fn);
}

typedef GLbitfield (GLAPIENTRYP _glptr_QueryMatrixxOES)(GLfixed *, GLint *);
#define CALL_QueryMatrixxOES(disp, parameters) \
    (* GET_QueryMatrixxOES(disp)) parameters
static inline _glptr_QueryMatrixxOES GET_QueryMatrixxOES(struct _glapi_table *disp) {
   return (_glptr_QueryMatrixxOES) (GET_by_offset(disp, _gloffset_QueryMatrixxOES));
}

static inline void SET_QueryMatrixxOES(struct _glapi_table *disp, GLbitfield (GLAPIENTRYP fn)(GLfixed *, GLint *)) {
   SET_by_offset(disp, _gloffset_QueryMatrixxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_PointParameterf)(GLenum, GLfloat);
#define CALL_PointParameterf(disp, parameters) \
    (* GET_PointParameterf(disp)) parameters
static inline _glptr_PointParameterf GET_PointParameterf(struct _glapi_table *disp) {
   return (_glptr_PointParameterf) (GET_by_offset(disp, _gloffset_PointParameterf));
}

static inline void SET_PointParameterf(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfloat)) {
   SET_by_offset(disp, _gloffset_PointParameterf, fn);
}

typedef void (GLAPIENTRYP _glptr_PointParameterfv)(GLenum, const GLfloat *);
#define CALL_PointParameterfv(disp, parameters) \
    (* GET_PointParameterfv(disp)) parameters
static inline _glptr_PointParameterfv GET_PointParameterfv(struct _glapi_table *disp) {
   return (_glptr_PointParameterfv) (GET_by_offset(disp, _gloffset_PointParameterfv));
}

static inline void SET_PointParameterfv(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_PointParameterfv, fn);
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

typedef void (GLAPIENTRYP _glptr_BlendFuncSeparateOES)(GLenum, GLenum, GLenum, GLenum);
#define CALL_BlendFuncSeparateOES(disp, parameters) \
    (* GET_BlendFuncSeparateOES(disp)) parameters
static inline _glptr_BlendFuncSeparateOES GET_BlendFuncSeparateOES(struct _glapi_table *disp) {
   return (_glptr_BlendFuncSeparateOES) (GET_by_offset(disp, _gloffset_BlendFuncSeparateOES));
}

static inline void SET_BlendFuncSeparateOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_BlendFuncSeparateOES, fn);
}

typedef void (GLAPIENTRYP _glptr_AlphaFuncxOES)(GLenum, GLclampx);
#define CALL_AlphaFuncxOES(disp, parameters) \
    (* GET_AlphaFuncxOES(disp)) parameters
static inline _glptr_AlphaFuncxOES GET_AlphaFuncxOES(struct _glapi_table *disp) {
   return (_glptr_AlphaFuncxOES) (GET_by_offset(disp, _gloffset_AlphaFuncxOES));
}

static inline void SET_AlphaFuncxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLclampx)) {
   SET_by_offset(disp, _gloffset_AlphaFuncxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ClearColorxOES)(GLclampx, GLclampx, GLclampx, GLclampx);
#define CALL_ClearColorxOES(disp, parameters) \
    (* GET_ClearColorxOES(disp)) parameters
static inline _glptr_ClearColorxOES GET_ClearColorxOES(struct _glapi_table *disp) {
   return (_glptr_ClearColorxOES) (GET_by_offset(disp, _gloffset_ClearColorxOES));
}

static inline void SET_ClearColorxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampx, GLclampx, GLclampx, GLclampx)) {
   SET_by_offset(disp, _gloffset_ClearColorxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ClearDepthxOES)(GLclampx);
#define CALL_ClearDepthxOES(disp, parameters) \
    (* GET_ClearDepthxOES(disp)) parameters
static inline _glptr_ClearDepthxOES GET_ClearDepthxOES(struct _glapi_table *disp) {
   return (_glptr_ClearDepthxOES) (GET_by_offset(disp, _gloffset_ClearDepthxOES));
}

static inline void SET_ClearDepthxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampx)) {
   SET_by_offset(disp, _gloffset_ClearDepthxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ClipPlanexOES)(GLenum, const GLfixed *);
#define CALL_ClipPlanexOES(disp, parameters) \
    (* GET_ClipPlanexOES(disp)) parameters
static inline _glptr_ClipPlanexOES GET_ClipPlanexOES(struct _glapi_table *disp) {
   return (_glptr_ClipPlanexOES) (GET_by_offset(disp, _gloffset_ClipPlanexOES));
}

static inline void SET_ClipPlanexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_ClipPlanexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_Color4xOES)(GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_Color4xOES(disp, parameters) \
    (* GET_Color4xOES(disp)) parameters
static inline _glptr_Color4xOES GET_Color4xOES(struct _glapi_table *disp) {
   return (_glptr_Color4xOES) (GET_by_offset(disp, _gloffset_Color4xOES));
}

static inline void SET_Color4xOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_Color4xOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DepthRangexOES)(GLclampx, GLclampx);
#define CALL_DepthRangexOES(disp, parameters) \
    (* GET_DepthRangexOES(disp)) parameters
static inline _glptr_DepthRangexOES GET_DepthRangexOES(struct _glapi_table *disp) {
   return (_glptr_DepthRangexOES) (GET_by_offset(disp, _gloffset_DepthRangexOES));
}

static inline void SET_DepthRangexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampx, GLclampx)) {
   SET_by_offset(disp, _gloffset_DepthRangexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_FogxOES)(GLenum, GLfixed);
#define CALL_FogxOES(disp, parameters) \
    (* GET_FogxOES(disp)) parameters
static inline _glptr_FogxOES GET_FogxOES(struct _glapi_table *disp) {
   return (_glptr_FogxOES) (GET_by_offset(disp, _gloffset_FogxOES));
}

static inline void SET_FogxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_FogxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_FogxvOES)(GLenum, const GLfixed *);
#define CALL_FogxvOES(disp, parameters) \
    (* GET_FogxvOES(disp)) parameters
static inline _glptr_FogxvOES GET_FogxvOES(struct _glapi_table *disp) {
   return (_glptr_FogxvOES) (GET_by_offset(disp, _gloffset_FogxvOES));
}

static inline void SET_FogxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_FogxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_FrustumxOES)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_FrustumxOES(disp, parameters) \
    (* GET_FrustumxOES(disp)) parameters
static inline _glptr_FrustumxOES GET_FrustumxOES(struct _glapi_table *disp) {
   return (_glptr_FrustumxOES) (GET_by_offset(disp, _gloffset_FrustumxOES));
}

static inline void SET_FrustumxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_FrustumxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetClipPlanexOES)(GLenum, GLfixed *);
#define CALL_GetClipPlanexOES(disp, parameters) \
    (* GET_GetClipPlanexOES(disp)) parameters
static inline _glptr_GetClipPlanexOES GET_GetClipPlanexOES(struct _glapi_table *disp) {
   return (_glptr_GetClipPlanexOES) (GET_by_offset(disp, _gloffset_GetClipPlanexOES));
}

static inline void SET_GetClipPlanexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetClipPlanexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetFixedvOES)(GLenum, GLfixed *);
#define CALL_GetFixedvOES(disp, parameters) \
    (* GET_GetFixedvOES(disp)) parameters
static inline _glptr_GetFixedvOES GET_GetFixedvOES(struct _glapi_table *disp) {
   return (_glptr_GetFixedvOES) (GET_by_offset(disp, _gloffset_GetFixedvOES));
}

static inline void SET_GetFixedvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetFixedvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetLightxvOES)(GLenum, GLenum, GLfixed *);
#define CALL_GetLightxvOES(disp, parameters) \
    (* GET_GetLightxvOES(disp)) parameters
static inline _glptr_GetLightxvOES GET_GetLightxvOES(struct _glapi_table *disp) {
   return (_glptr_GetLightxvOES) (GET_by_offset(disp, _gloffset_GetLightxvOES));
}

static inline void SET_GetLightxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetLightxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetMaterialxvOES)(GLenum, GLenum, GLfixed *);
#define CALL_GetMaterialxvOES(disp, parameters) \
    (* GET_GetMaterialxvOES(disp)) parameters
static inline _glptr_GetMaterialxvOES GET_GetMaterialxvOES(struct _glapi_table *disp) {
   return (_glptr_GetMaterialxvOES) (GET_by_offset(disp, _gloffset_GetMaterialxvOES));
}

static inline void SET_GetMaterialxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetMaterialxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexEnvxvOES)(GLenum, GLenum, GLfixed *);
#define CALL_GetTexEnvxvOES(disp, parameters) \
    (* GET_GetTexEnvxvOES(disp)) parameters
static inline _glptr_GetTexEnvxvOES GET_GetTexEnvxvOES(struct _glapi_table *disp) {
   return (_glptr_GetTexEnvxvOES) (GET_by_offset(disp, _gloffset_GetTexEnvxvOES));
}

static inline void SET_GetTexEnvxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetTexEnvxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexGenxvOES)(GLenum, GLenum, GLfixed *);
#define CALL_GetTexGenxvOES(disp, parameters) \
    (* GET_GetTexGenxvOES(disp)) parameters
static inline _glptr_GetTexGenxvOES GET_GetTexGenxvOES(struct _glapi_table *disp) {
   return (_glptr_GetTexGenxvOES) (GET_by_offset(disp, _gloffset_GetTexGenxvOES));
}

static inline void SET_GetTexGenxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetTexGenxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetTexParameterxvOES)(GLenum, GLenum, GLfixed *);
#define CALL_GetTexParameterxvOES(disp, parameters) \
    (* GET_GetTexParameterxvOES(disp)) parameters
static inline _glptr_GetTexParameterxvOES GET_GetTexParameterxvOES(struct _glapi_table *disp) {
   return (_glptr_GetTexParameterxvOES) (GET_by_offset(disp, _gloffset_GetTexParameterxvOES));
}

static inline void SET_GetTexParameterxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed *)) {
   SET_by_offset(disp, _gloffset_GetTexParameterxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_LightModelxOES)(GLenum, GLfixed);
#define CALL_LightModelxOES(disp, parameters) \
    (* GET_LightModelxOES(disp)) parameters
static inline _glptr_LightModelxOES GET_LightModelxOES(struct _glapi_table *disp) {
   return (_glptr_LightModelxOES) (GET_by_offset(disp, _gloffset_LightModelxOES));
}

static inline void SET_LightModelxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_LightModelxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_LightModelxvOES)(GLenum, const GLfixed *);
#define CALL_LightModelxvOES(disp, parameters) \
    (* GET_LightModelxvOES(disp)) parameters
static inline _glptr_LightModelxvOES GET_LightModelxvOES(struct _glapi_table *disp) {
   return (_glptr_LightModelxvOES) (GET_by_offset(disp, _gloffset_LightModelxvOES));
}

static inline void SET_LightModelxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_LightModelxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_LightxOES)(GLenum, GLenum, GLfixed);
#define CALL_LightxOES(disp, parameters) \
    (* GET_LightxOES(disp)) parameters
static inline _glptr_LightxOES GET_LightxOES(struct _glapi_table *disp) {
   return (_glptr_LightxOES) (GET_by_offset(disp, _gloffset_LightxOES));
}

static inline void SET_LightxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_LightxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_LightxvOES)(GLenum, GLenum, const GLfixed *);
#define CALL_LightxvOES(disp, parameters) \
    (* GET_LightxvOES(disp)) parameters
static inline _glptr_LightxvOES GET_LightxvOES(struct _glapi_table *disp) {
   return (_glptr_LightxvOES) (GET_by_offset(disp, _gloffset_LightxvOES));
}

static inline void SET_LightxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_LightxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_LineWidthxOES)(GLfixed);
#define CALL_LineWidthxOES(disp, parameters) \
    (* GET_LineWidthxOES(disp)) parameters
static inline _glptr_LineWidthxOES GET_LineWidthxOES(struct _glapi_table *disp) {
   return (_glptr_LineWidthxOES) (GET_by_offset(disp, _gloffset_LineWidthxOES));
}

static inline void SET_LineWidthxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed)) {
   SET_by_offset(disp, _gloffset_LineWidthxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_LoadMatrixxOES)(const GLfixed *);
#define CALL_LoadMatrixxOES(disp, parameters) \
    (* GET_LoadMatrixxOES(disp)) parameters
static inline _glptr_LoadMatrixxOES GET_LoadMatrixxOES(struct _glapi_table *disp) {
   return (_glptr_LoadMatrixxOES) (GET_by_offset(disp, _gloffset_LoadMatrixxOES));
}

static inline void SET_LoadMatrixxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLfixed *)) {
   SET_by_offset(disp, _gloffset_LoadMatrixxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_MaterialxOES)(GLenum, GLenum, GLfixed);
#define CALL_MaterialxOES(disp, parameters) \
    (* GET_MaterialxOES(disp)) parameters
static inline _glptr_MaterialxOES GET_MaterialxOES(struct _glapi_table *disp) {
   return (_glptr_MaterialxOES) (GET_by_offset(disp, _gloffset_MaterialxOES));
}

static inline void SET_MaterialxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_MaterialxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_MaterialxvOES)(GLenum, GLenum, const GLfixed *);
#define CALL_MaterialxvOES(disp, parameters) \
    (* GET_MaterialxvOES(disp)) parameters
static inline _glptr_MaterialxvOES GET_MaterialxvOES(struct _glapi_table *disp) {
   return (_glptr_MaterialxvOES) (GET_by_offset(disp, _gloffset_MaterialxvOES));
}

static inline void SET_MaterialxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_MaterialxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_MultMatrixxOES)(const GLfixed *);
#define CALL_MultMatrixxOES(disp, parameters) \
    (* GET_MultMatrixxOES(disp)) parameters
static inline _glptr_MultMatrixxOES GET_MultMatrixxOES(struct _glapi_table *disp) {
   return (_glptr_MultMatrixxOES) (GET_by_offset(disp, _gloffset_MultMatrixxOES));
}

static inline void SET_MultMatrixxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(const GLfixed *)) {
   SET_by_offset(disp, _gloffset_MultMatrixxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_MultiTexCoord4xOES)(GLenum, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_MultiTexCoord4xOES(disp, parameters) \
    (* GET_MultiTexCoord4xOES(disp)) parameters
static inline _glptr_MultiTexCoord4xOES GET_MultiTexCoord4xOES(struct _glapi_table *disp) {
   return (_glptr_MultiTexCoord4xOES) (GET_by_offset(disp, _gloffset_MultiTexCoord4xOES));
}

static inline void SET_MultiTexCoord4xOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfixed, GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_MultiTexCoord4xOES, fn);
}

typedef void (GLAPIENTRYP _glptr_Normal3xOES)(GLfixed, GLfixed, GLfixed);
#define CALL_Normal3xOES(disp, parameters) \
    (* GET_Normal3xOES(disp)) parameters
static inline _glptr_Normal3xOES GET_Normal3xOES(struct _glapi_table *disp) {
   return (_glptr_Normal3xOES) (GET_by_offset(disp, _gloffset_Normal3xOES));
}

static inline void SET_Normal3xOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_Normal3xOES, fn);
}

typedef void (GLAPIENTRYP _glptr_OrthoxOES)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_OrthoxOES(disp, parameters) \
    (* GET_OrthoxOES(disp)) parameters
static inline _glptr_OrthoxOES GET_OrthoxOES(struct _glapi_table *disp) {
   return (_glptr_OrthoxOES) (GET_by_offset(disp, _gloffset_OrthoxOES));
}

static inline void SET_OrthoxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_OrthoxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_PointParameterxOES)(GLenum, GLfixed);
#define CALL_PointParameterxOES(disp, parameters) \
    (* GET_PointParameterxOES(disp)) parameters
static inline _glptr_PointParameterxOES GET_PointParameterxOES(struct _glapi_table *disp) {
   return (_glptr_PointParameterxOES) (GET_by_offset(disp, _gloffset_PointParameterxOES));
}

static inline void SET_PointParameterxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_PointParameterxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_PointParameterxvOES)(GLenum, const GLfixed *);
#define CALL_PointParameterxvOES(disp, parameters) \
    (* GET_PointParameterxvOES(disp)) parameters
static inline _glptr_PointParameterxvOES GET_PointParameterxvOES(struct _glapi_table *disp) {
   return (_glptr_PointParameterxvOES) (GET_by_offset(disp, _gloffset_PointParameterxvOES));
}

static inline void SET_PointParameterxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_PointParameterxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_PointSizexOES)(GLfixed);
#define CALL_PointSizexOES(disp, parameters) \
    (* GET_PointSizexOES(disp)) parameters
static inline _glptr_PointSizexOES GET_PointSizexOES(struct _glapi_table *disp) {
   return (_glptr_PointSizexOES) (GET_by_offset(disp, _gloffset_PointSizexOES));
}

static inline void SET_PointSizexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed)) {
   SET_by_offset(disp, _gloffset_PointSizexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_PolygonOffsetxOES)(GLfixed, GLfixed);
#define CALL_PolygonOffsetxOES(disp, parameters) \
    (* GET_PolygonOffsetxOES(disp)) parameters
static inline _glptr_PolygonOffsetxOES GET_PolygonOffsetxOES(struct _glapi_table *disp) {
   return (_glptr_PolygonOffsetxOES) (GET_by_offset(disp, _gloffset_PolygonOffsetxOES));
}

static inline void SET_PolygonOffsetxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_PolygonOffsetxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_RotatexOES)(GLfixed, GLfixed, GLfixed, GLfixed);
#define CALL_RotatexOES(disp, parameters) \
    (* GET_RotatexOES(disp)) parameters
static inline _glptr_RotatexOES GET_RotatexOES(struct _glapi_table *disp) {
   return (_glptr_RotatexOES) (GET_by_offset(disp, _gloffset_RotatexOES));
}

static inline void SET_RotatexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_RotatexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_SampleCoveragexOES)(GLclampx, GLboolean);
#define CALL_SampleCoveragexOES(disp, parameters) \
    (* GET_SampleCoveragexOES(disp)) parameters
static inline _glptr_SampleCoveragexOES GET_SampleCoveragexOES(struct _glapi_table *disp) {
   return (_glptr_SampleCoveragexOES) (GET_by_offset(disp, _gloffset_SampleCoveragexOES));
}

static inline void SET_SampleCoveragexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLclampx, GLboolean)) {
   SET_by_offset(disp, _gloffset_SampleCoveragexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ScalexOES)(GLfixed, GLfixed, GLfixed);
#define CALL_ScalexOES(disp, parameters) \
    (* GET_ScalexOES(disp)) parameters
static inline _glptr_ScalexOES GET_ScalexOES(struct _glapi_table *disp) {
   return (_glptr_ScalexOES) (GET_by_offset(disp, _gloffset_ScalexOES));
}

static inline void SET_ScalexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_ScalexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexEnvxOES)(GLenum, GLenum, GLfixed);
#define CALL_TexEnvxOES(disp, parameters) \
    (* GET_TexEnvxOES(disp)) parameters
static inline _glptr_TexEnvxOES GET_TexEnvxOES(struct _glapi_table *disp) {
   return (_glptr_TexEnvxOES) (GET_by_offset(disp, _gloffset_TexEnvxOES));
}

static inline void SET_TexEnvxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_TexEnvxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexEnvxvOES)(GLenum, GLenum, const GLfixed *);
#define CALL_TexEnvxvOES(disp, parameters) \
    (* GET_TexEnvxvOES(disp)) parameters
static inline _glptr_TexEnvxvOES GET_TexEnvxvOES(struct _glapi_table *disp) {
   return (_glptr_TexEnvxvOES) (GET_by_offset(disp, _gloffset_TexEnvxvOES));
}

static inline void SET_TexEnvxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_TexEnvxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexGenxOES)(GLenum, GLenum, GLint);
#define CALL_TexGenxOES(disp, parameters) \
    (* GET_TexGenxOES(disp)) parameters
static inline _glptr_TexGenxOES GET_TexGenxOES(struct _glapi_table *disp) {
   return (_glptr_TexGenxOES) (GET_by_offset(disp, _gloffset_TexGenxOES));
}

static inline void SET_TexGenxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint)) {
   SET_by_offset(disp, _gloffset_TexGenxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexGenxvOES)(GLenum, GLenum, const GLfixed *);
#define CALL_TexGenxvOES(disp, parameters) \
    (* GET_TexGenxvOES(disp)) parameters
static inline _glptr_TexGenxvOES GET_TexGenxvOES(struct _glapi_table *disp) {
   return (_glptr_TexGenxvOES) (GET_by_offset(disp, _gloffset_TexGenxvOES));
}

static inline void SET_TexGenxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_TexGenxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexParameterxOES)(GLenum, GLenum, GLfixed);
#define CALL_TexParameterxOES(disp, parameters) \
    (* GET_TexParameterxOES(disp)) parameters
static inline _glptr_TexParameterxOES GET_TexParameterxOES(struct _glapi_table *disp) {
   return (_glptr_TexParameterxOES) (GET_by_offset(disp, _gloffset_TexParameterxOES));
}

static inline void SET_TexParameterxOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLfixed)) {
   SET_by_offset(disp, _gloffset_TexParameterxOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TexParameterxvOES)(GLenum, GLenum, const GLfixed *);
#define CALL_TexParameterxvOES(disp, parameters) \
    (* GET_TexParameterxvOES(disp)) parameters
static inline _glptr_TexParameterxvOES GET_TexParameterxvOES(struct _glapi_table *disp) {
   return (_glptr_TexParameterxvOES) (GET_by_offset(disp, _gloffset_TexParameterxvOES));
}

static inline void SET_TexParameterxvOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, const GLfixed *)) {
   SET_by_offset(disp, _gloffset_TexParameterxvOES, fn);
}

typedef void (GLAPIENTRYP _glptr_TranslatexOES)(GLfixed, GLfixed, GLfixed);
#define CALL_TranslatexOES(disp, parameters) \
    (* GET_TranslatexOES(disp)) parameters
static inline _glptr_TranslatexOES GET_TranslatexOES(struct _glapi_table *disp) {
   return (_glptr_TranslatexOES) (GET_by_offset(disp, _gloffset_TranslatexOES));
}

static inline void SET_TranslatexOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfixed, GLfixed, GLfixed)) {
   SET_by_offset(disp, _gloffset_TranslatexOES, fn);
}

typedef void (GLAPIENTRYP _glptr_ClipPlanefOES)(GLenum, const GLfloat *);
#define CALL_ClipPlanefOES(disp, parameters) \
    (* GET_ClipPlanefOES(disp)) parameters
static inline _glptr_ClipPlanefOES GET_ClipPlanefOES(struct _glapi_table *disp) {
   return (_glptr_ClipPlanefOES) (GET_by_offset(disp, _gloffset_ClipPlanefOES));
}

static inline void SET_ClipPlanefOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, const GLfloat *)) {
   SET_by_offset(disp, _gloffset_ClipPlanefOES, fn);
}

typedef void (GLAPIENTRYP _glptr_FrustumfOES)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_FrustumfOES(disp, parameters) \
    (* GET_FrustumfOES(disp)) parameters
static inline _glptr_FrustumfOES GET_FrustumfOES(struct _glapi_table *disp) {
   return (_glptr_FrustumfOES) (GET_by_offset(disp, _gloffset_FrustumfOES));
}

static inline void SET_FrustumfOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_FrustumfOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetClipPlanefOES)(GLenum, GLfloat *);
#define CALL_GetClipPlanefOES(disp, parameters) \
    (* GET_GetClipPlanefOES(disp)) parameters
static inline _glptr_GetClipPlanefOES GET_GetClipPlanefOES(struct _glapi_table *disp) {
   return (_glptr_GetClipPlanefOES) (GET_by_offset(disp, _gloffset_GetClipPlanefOES));
}

static inline void SET_GetClipPlanefOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLfloat *)) {
   SET_by_offset(disp, _gloffset_GetClipPlanefOES, fn);
}

typedef void (GLAPIENTRYP _glptr_OrthofOES)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
#define CALL_OrthofOES(disp, parameters) \
    (* GET_OrthofOES(disp)) parameters
static inline _glptr_OrthofOES GET_OrthofOES(struct _glapi_table *disp) {
   return (_glptr_OrthofOES) (GET_by_offset(disp, _gloffset_OrthofOES));
}

static inline void SET_OrthofOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)) {
   SET_by_offset(disp, _gloffset_OrthofOES, fn);
}

typedef void (GLAPIENTRYP _glptr_BlendEquationSeparateOES)(GLenum, GLenum);
#define CALL_BlendEquationSeparateOES(disp, parameters) \
    (* GET_BlendEquationSeparateOES(disp)) parameters
static inline _glptr_BlendEquationSeparateOES GET_BlendEquationSeparateOES(struct _glapi_table *disp) {
   return (_glptr_BlendEquationSeparateOES) (GET_by_offset(disp, _gloffset_BlendEquationSeparateOES));
}

static inline void SET_BlendEquationSeparateOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum)) {
   SET_by_offset(disp, _gloffset_BlendEquationSeparateOES, fn);
}

typedef void (GLAPIENTRYP _glptr_BindFramebufferOES)(GLenum, GLuint);
#define CALL_BindFramebufferOES(disp, parameters) \
    (* GET_BindFramebufferOES(disp)) parameters
static inline _glptr_BindFramebufferOES GET_BindFramebufferOES(struct _glapi_table *disp) {
   return (_glptr_BindFramebufferOES) (GET_by_offset(disp, _gloffset_BindFramebufferOES));
}

static inline void SET_BindFramebufferOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_BindFramebufferOES, fn);
}

typedef void (GLAPIENTRYP _glptr_BindRenderbufferOES)(GLenum, GLuint);
#define CALL_BindRenderbufferOES(disp, parameters) \
    (* GET_BindRenderbufferOES(disp)) parameters
static inline _glptr_BindRenderbufferOES GET_BindRenderbufferOES(struct _glapi_table *disp) {
   return (_glptr_BindRenderbufferOES) (GET_by_offset(disp, _gloffset_BindRenderbufferOES));
}

static inline void SET_BindRenderbufferOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_BindRenderbufferOES, fn);
}

typedef GLenum (GLAPIENTRYP _glptr_CheckFramebufferStatusOES)(GLenum);
#define CALL_CheckFramebufferStatusOES(disp, parameters) \
    (* GET_CheckFramebufferStatusOES(disp)) parameters
static inline _glptr_CheckFramebufferStatusOES GET_CheckFramebufferStatusOES(struct _glapi_table *disp) {
   return (_glptr_CheckFramebufferStatusOES) (GET_by_offset(disp, _gloffset_CheckFramebufferStatusOES));
}

static inline void SET_CheckFramebufferStatusOES(struct _glapi_table *disp, GLenum (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_CheckFramebufferStatusOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteFramebuffersOES)(GLsizei, const GLuint *);
#define CALL_DeleteFramebuffersOES(disp, parameters) \
    (* GET_DeleteFramebuffersOES(disp)) parameters
static inline _glptr_DeleteFramebuffersOES GET_DeleteFramebuffersOES(struct _glapi_table *disp) {
   return (_glptr_DeleteFramebuffersOES) (GET_by_offset(disp, _gloffset_DeleteFramebuffersOES));
}

static inline void SET_DeleteFramebuffersOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *)) {
   SET_by_offset(disp, _gloffset_DeleteFramebuffersOES, fn);
}

typedef void (GLAPIENTRYP _glptr_DeleteRenderbuffersOES)(GLsizei, const GLuint *);
#define CALL_DeleteRenderbuffersOES(disp, parameters) \
    (* GET_DeleteRenderbuffersOES(disp)) parameters
static inline _glptr_DeleteRenderbuffersOES GET_DeleteRenderbuffersOES(struct _glapi_table *disp) {
   return (_glptr_DeleteRenderbuffersOES) (GET_by_offset(disp, _gloffset_DeleteRenderbuffersOES));
}

static inline void SET_DeleteRenderbuffersOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, const GLuint *)) {
   SET_by_offset(disp, _gloffset_DeleteRenderbuffersOES, fn);
}

typedef void (GLAPIENTRYP _glptr_FramebufferRenderbufferOES)(GLenum, GLenum, GLenum, GLuint);
#define CALL_FramebufferRenderbufferOES(disp, parameters) \
    (* GET_FramebufferRenderbufferOES(disp)) parameters
static inline _glptr_FramebufferRenderbufferOES GET_FramebufferRenderbufferOES(struct _glapi_table *disp) {
   return (_glptr_FramebufferRenderbufferOES) (GET_by_offset(disp, _gloffset_FramebufferRenderbufferOES));
}

static inline void SET_FramebufferRenderbufferOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint)) {
   SET_by_offset(disp, _gloffset_FramebufferRenderbufferOES, fn);
}

typedef void (GLAPIENTRYP _glptr_FramebufferTexture2DOES)(GLenum, GLenum, GLenum, GLuint, GLint);
#define CALL_FramebufferTexture2DOES(disp, parameters) \
    (* GET_FramebufferTexture2DOES(disp)) parameters
static inline _glptr_FramebufferTexture2DOES GET_FramebufferTexture2DOES(struct _glapi_table *disp) {
   return (_glptr_FramebufferTexture2DOES) (GET_by_offset(disp, _gloffset_FramebufferTexture2DOES));
}

static inline void SET_FramebufferTexture2DOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLuint, GLint)) {
   SET_by_offset(disp, _gloffset_FramebufferTexture2DOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GenFramebuffersOES)(GLsizei, GLuint *);
#define CALL_GenFramebuffersOES(disp, parameters) \
    (* GET_GenFramebuffersOES(disp)) parameters
static inline _glptr_GenFramebuffersOES GET_GenFramebuffersOES(struct _glapi_table *disp) {
   return (_glptr_GenFramebuffersOES) (GET_by_offset(disp, _gloffset_GenFramebuffersOES));
}

static inline void SET_GenFramebuffersOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, GLuint *)) {
   SET_by_offset(disp, _gloffset_GenFramebuffersOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GenRenderbuffersOES)(GLsizei, GLuint *);
#define CALL_GenRenderbuffersOES(disp, parameters) \
    (* GET_GenRenderbuffersOES(disp)) parameters
static inline _glptr_GenRenderbuffersOES GET_GenRenderbuffersOES(struct _glapi_table *disp) {
   return (_glptr_GenRenderbuffersOES) (GET_by_offset(disp, _gloffset_GenRenderbuffersOES));
}

static inline void SET_GenRenderbuffersOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLsizei, GLuint *)) {
   SET_by_offset(disp, _gloffset_GenRenderbuffersOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GenerateMipmapOES)(GLenum);
#define CALL_GenerateMipmapOES(disp, parameters) \
    (* GET_GenerateMipmapOES(disp)) parameters
static inline _glptr_GenerateMipmapOES GET_GenerateMipmapOES(struct _glapi_table *disp) {
   return (_glptr_GenerateMipmapOES) (GET_by_offset(disp, _gloffset_GenerateMipmapOES));
}

static inline void SET_GenerateMipmapOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum)) {
   SET_by_offset(disp, _gloffset_GenerateMipmapOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetFramebufferAttachmentParameterivOES)(GLenum, GLenum, GLenum, GLint *);
#define CALL_GetFramebufferAttachmentParameterivOES(disp, parameters) \
    (* GET_GetFramebufferAttachmentParameterivOES(disp)) parameters
static inline _glptr_GetFramebufferAttachmentParameterivOES GET_GetFramebufferAttachmentParameterivOES(struct _glapi_table *disp) {
   return (_glptr_GetFramebufferAttachmentParameterivOES) (GET_by_offset(disp, _gloffset_GetFramebufferAttachmentParameterivOES));
}

static inline void SET_GetFramebufferAttachmentParameterivOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetFramebufferAttachmentParameterivOES, fn);
}

typedef void (GLAPIENTRYP _glptr_GetRenderbufferParameterivOES)(GLenum, GLenum, GLint *);
#define CALL_GetRenderbufferParameterivOES(disp, parameters) \
    (* GET_GetRenderbufferParameterivOES(disp)) parameters
static inline _glptr_GetRenderbufferParameterivOES GET_GetRenderbufferParameterivOES(struct _glapi_table *disp) {
   return (_glptr_GetRenderbufferParameterivOES) (GET_by_offset(disp, _gloffset_GetRenderbufferParameterivOES));
}

static inline void SET_GetRenderbufferParameterivOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLint *)) {
   SET_by_offset(disp, _gloffset_GetRenderbufferParameterivOES, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsFramebufferOES)(GLuint);
#define CALL_IsFramebufferOES(disp, parameters) \
    (* GET_IsFramebufferOES(disp)) parameters
static inline _glptr_IsFramebufferOES GET_IsFramebufferOES(struct _glapi_table *disp) {
   return (_glptr_IsFramebufferOES) (GET_by_offset(disp, _gloffset_IsFramebufferOES));
}

static inline void SET_IsFramebufferOES(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsFramebufferOES, fn);
}

typedef GLboolean (GLAPIENTRYP _glptr_IsRenderbufferOES)(GLuint);
#define CALL_IsRenderbufferOES(disp, parameters) \
    (* GET_IsRenderbufferOES(disp)) parameters
static inline _glptr_IsRenderbufferOES GET_IsRenderbufferOES(struct _glapi_table *disp) {
   return (_glptr_IsRenderbufferOES) (GET_by_offset(disp, _gloffset_IsRenderbufferOES));
}

static inline void SET_IsRenderbufferOES(struct _glapi_table *disp, GLboolean (GLAPIENTRYP fn)(GLuint)) {
   SET_by_offset(disp, _gloffset_IsRenderbufferOES, fn);
}

typedef void (GLAPIENTRYP _glptr_RenderbufferStorageOES)(GLenum, GLenum, GLsizei, GLsizei);
#define CALL_RenderbufferStorageOES(disp, parameters) \
    (* GET_RenderbufferStorageOES(disp)) parameters
static inline _glptr_RenderbufferStorageOES GET_RenderbufferStorageOES(struct _glapi_table *disp) {
   return (_glptr_RenderbufferStorageOES) (GET_by_offset(disp, _gloffset_RenderbufferStorageOES));
}

static inline void SET_RenderbufferStorageOES(struct _glapi_table *disp, void (GLAPIENTRYP fn)(GLenum, GLenum, GLsizei, GLsizei)) {
   SET_by_offset(disp, _gloffset_RenderbufferStorageOES, fn);
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


/* define aliases for compatibility */
#define CALL_ClearDepthfOES(disp, parameters) CALL_ClearDepthf(disp, parameters)
#define GET_ClearDepthfOES(disp) GET_ClearDepthf(disp)
#define SET_ClearDepthfOES(disp, fn) SET_ClearDepthf(disp, fn)
#define CALL_DepthRangefOES(disp, parameters) CALL_DepthRangef(disp, parameters)
#define GET_DepthRangefOES(disp) GET_DepthRangef(disp)
#define SET_DepthRangefOES(disp, fn) SET_DepthRangef(disp, fn)
#define CALL_AlphaFuncx(disp, parameters) CALL_AlphaFuncxOES(disp, parameters)
#define GET_AlphaFuncx(disp) GET_AlphaFuncxOES(disp)
#define SET_AlphaFuncx(disp, fn) SET_AlphaFuncxOES(disp, fn)
#define CALL_ClearColorx(disp, parameters) CALL_ClearColorxOES(disp, parameters)
#define GET_ClearColorx(disp) GET_ClearColorxOES(disp)
#define SET_ClearColorx(disp, fn) SET_ClearColorxOES(disp, fn)
#define CALL_ClearDepthx(disp, parameters) CALL_ClearDepthxOES(disp, parameters)
#define GET_ClearDepthx(disp) GET_ClearDepthxOES(disp)
#define SET_ClearDepthx(disp, fn) SET_ClearDepthxOES(disp, fn)
#define CALL_ClipPlanex(disp, parameters) CALL_ClipPlanexOES(disp, parameters)
#define GET_ClipPlanex(disp) GET_ClipPlanexOES(disp)
#define SET_ClipPlanex(disp, fn) SET_ClipPlanexOES(disp, fn)
#define CALL_Color4x(disp, parameters) CALL_Color4xOES(disp, parameters)
#define GET_Color4x(disp) GET_Color4xOES(disp)
#define SET_Color4x(disp, fn) SET_Color4xOES(disp, fn)
#define CALL_DepthRangex(disp, parameters) CALL_DepthRangexOES(disp, parameters)
#define GET_DepthRangex(disp) GET_DepthRangexOES(disp)
#define SET_DepthRangex(disp, fn) SET_DepthRangexOES(disp, fn)
#define CALL_Fogx(disp, parameters) CALL_FogxOES(disp, parameters)
#define GET_Fogx(disp) GET_FogxOES(disp)
#define SET_Fogx(disp, fn) SET_FogxOES(disp, fn)
#define CALL_Fogxv(disp, parameters) CALL_FogxvOES(disp, parameters)
#define GET_Fogxv(disp) GET_FogxvOES(disp)
#define SET_Fogxv(disp, fn) SET_FogxvOES(disp, fn)
#define CALL_Frustumx(disp, parameters) CALL_FrustumxOES(disp, parameters)
#define GET_Frustumx(disp) GET_FrustumxOES(disp)
#define SET_Frustumx(disp, fn) SET_FrustumxOES(disp, fn)
#define CALL_GetClipPlanex(disp, parameters) CALL_GetClipPlanexOES(disp, parameters)
#define GET_GetClipPlanex(disp) GET_GetClipPlanexOES(disp)
#define SET_GetClipPlanex(disp, fn) SET_GetClipPlanexOES(disp, fn)
#define CALL_GetFixedv(disp, parameters) CALL_GetFixedvOES(disp, parameters)
#define GET_GetFixedv(disp) GET_GetFixedvOES(disp)
#define SET_GetFixedv(disp, fn) SET_GetFixedvOES(disp, fn)
#define CALL_GetLightxv(disp, parameters) CALL_GetLightxvOES(disp, parameters)
#define GET_GetLightxv(disp) GET_GetLightxvOES(disp)
#define SET_GetLightxv(disp, fn) SET_GetLightxvOES(disp, fn)
#define CALL_GetMaterialxv(disp, parameters) CALL_GetMaterialxvOES(disp, parameters)
#define GET_GetMaterialxv(disp) GET_GetMaterialxvOES(disp)
#define SET_GetMaterialxv(disp, fn) SET_GetMaterialxvOES(disp, fn)
#define CALL_GetTexEnvxv(disp, parameters) CALL_GetTexEnvxvOES(disp, parameters)
#define GET_GetTexEnvxv(disp) GET_GetTexEnvxvOES(disp)
#define SET_GetTexEnvxv(disp, fn) SET_GetTexEnvxvOES(disp, fn)
#define CALL_GetTexParameterxv(disp, parameters) CALL_GetTexParameterxvOES(disp, parameters)
#define GET_GetTexParameterxv(disp) GET_GetTexParameterxvOES(disp)
#define SET_GetTexParameterxv(disp, fn) SET_GetTexParameterxvOES(disp, fn)
#define CALL_LightModelx(disp, parameters) CALL_LightModelxOES(disp, parameters)
#define GET_LightModelx(disp) GET_LightModelxOES(disp)
#define SET_LightModelx(disp, fn) SET_LightModelxOES(disp, fn)
#define CALL_LightModelxv(disp, parameters) CALL_LightModelxvOES(disp, parameters)
#define GET_LightModelxv(disp) GET_LightModelxvOES(disp)
#define SET_LightModelxv(disp, fn) SET_LightModelxvOES(disp, fn)
#define CALL_Lightx(disp, parameters) CALL_LightxOES(disp, parameters)
#define GET_Lightx(disp) GET_LightxOES(disp)
#define SET_Lightx(disp, fn) SET_LightxOES(disp, fn)
#define CALL_Lightxv(disp, parameters) CALL_LightxvOES(disp, parameters)
#define GET_Lightxv(disp) GET_LightxvOES(disp)
#define SET_Lightxv(disp, fn) SET_LightxvOES(disp, fn)
#define CALL_LineWidthx(disp, parameters) CALL_LineWidthxOES(disp, parameters)
#define GET_LineWidthx(disp) GET_LineWidthxOES(disp)
#define SET_LineWidthx(disp, fn) SET_LineWidthxOES(disp, fn)
#define CALL_LoadMatrixx(disp, parameters) CALL_LoadMatrixxOES(disp, parameters)
#define GET_LoadMatrixx(disp) GET_LoadMatrixxOES(disp)
#define SET_LoadMatrixx(disp, fn) SET_LoadMatrixxOES(disp, fn)
#define CALL_Materialx(disp, parameters) CALL_MaterialxOES(disp, parameters)
#define GET_Materialx(disp) GET_MaterialxOES(disp)
#define SET_Materialx(disp, fn) SET_MaterialxOES(disp, fn)
#define CALL_Materialxv(disp, parameters) CALL_MaterialxvOES(disp, parameters)
#define GET_Materialxv(disp) GET_MaterialxvOES(disp)
#define SET_Materialxv(disp, fn) SET_MaterialxvOES(disp, fn)
#define CALL_MultMatrixx(disp, parameters) CALL_MultMatrixxOES(disp, parameters)
#define GET_MultMatrixx(disp) GET_MultMatrixxOES(disp)
#define SET_MultMatrixx(disp, fn) SET_MultMatrixxOES(disp, fn)
#define CALL_MultiTexCoord4x(disp, parameters) CALL_MultiTexCoord4xOES(disp, parameters)
#define GET_MultiTexCoord4x(disp) GET_MultiTexCoord4xOES(disp)
#define SET_MultiTexCoord4x(disp, fn) SET_MultiTexCoord4xOES(disp, fn)
#define CALL_Normal3x(disp, parameters) CALL_Normal3xOES(disp, parameters)
#define GET_Normal3x(disp) GET_Normal3xOES(disp)
#define SET_Normal3x(disp, fn) SET_Normal3xOES(disp, fn)
#define CALL_Orthox(disp, parameters) CALL_OrthoxOES(disp, parameters)
#define GET_Orthox(disp) GET_OrthoxOES(disp)
#define SET_Orthox(disp, fn) SET_OrthoxOES(disp, fn)
#define CALL_PointParameterx(disp, parameters) CALL_PointParameterxOES(disp, parameters)
#define GET_PointParameterx(disp) GET_PointParameterxOES(disp)
#define SET_PointParameterx(disp, fn) SET_PointParameterxOES(disp, fn)
#define CALL_PointParameterxv(disp, parameters) CALL_PointParameterxvOES(disp, parameters)
#define GET_PointParameterxv(disp) GET_PointParameterxvOES(disp)
#define SET_PointParameterxv(disp, fn) SET_PointParameterxvOES(disp, fn)
#define CALL_PointSizex(disp, parameters) CALL_PointSizexOES(disp, parameters)
#define GET_PointSizex(disp) GET_PointSizexOES(disp)
#define SET_PointSizex(disp, fn) SET_PointSizexOES(disp, fn)
#define CALL_PolygonOffsetx(disp, parameters) CALL_PolygonOffsetxOES(disp, parameters)
#define GET_PolygonOffsetx(disp) GET_PolygonOffsetxOES(disp)
#define SET_PolygonOffsetx(disp, fn) SET_PolygonOffsetxOES(disp, fn)
#define CALL_Rotatex(disp, parameters) CALL_RotatexOES(disp, parameters)
#define GET_Rotatex(disp) GET_RotatexOES(disp)
#define SET_Rotatex(disp, fn) SET_RotatexOES(disp, fn)
#define CALL_SampleCoveragex(disp, parameters) CALL_SampleCoveragexOES(disp, parameters)
#define GET_SampleCoveragex(disp) GET_SampleCoveragexOES(disp)
#define SET_SampleCoveragex(disp, fn) SET_SampleCoveragexOES(disp, fn)
#define CALL_Scalex(disp, parameters) CALL_ScalexOES(disp, parameters)
#define GET_Scalex(disp) GET_ScalexOES(disp)
#define SET_Scalex(disp, fn) SET_ScalexOES(disp, fn)
#define CALL_TexEnvx(disp, parameters) CALL_TexEnvxOES(disp, parameters)
#define GET_TexEnvx(disp) GET_TexEnvxOES(disp)
#define SET_TexEnvx(disp, fn) SET_TexEnvxOES(disp, fn)
#define CALL_TexEnvxv(disp, parameters) CALL_TexEnvxvOES(disp, parameters)
#define GET_TexEnvxv(disp) GET_TexEnvxvOES(disp)
#define SET_TexEnvxv(disp, fn) SET_TexEnvxvOES(disp, fn)
#define CALL_TexParameterx(disp, parameters) CALL_TexParameterxOES(disp, parameters)
#define GET_TexParameterx(disp) GET_TexParameterxOES(disp)
#define SET_TexParameterx(disp, fn) SET_TexParameterxOES(disp, fn)
#define CALL_TexParameterxv(disp, parameters) CALL_TexParameterxvOES(disp, parameters)
#define GET_TexParameterxv(disp) GET_TexParameterxvOES(disp)
#define SET_TexParameterxv(disp, fn) SET_TexParameterxvOES(disp, fn)
#define CALL_Translatex(disp, parameters) CALL_TranslatexOES(disp, parameters)
#define GET_Translatex(disp) GET_TranslatexOES(disp)
#define SET_Translatex(disp, fn) SET_TranslatexOES(disp, fn)
#define CALL_ClipPlanef(disp, parameters) CALL_ClipPlanefOES(disp, parameters)
#define GET_ClipPlanef(disp) GET_ClipPlanefOES(disp)
#define SET_ClipPlanef(disp, fn) SET_ClipPlanefOES(disp, fn)
#define CALL_Frustumf(disp, parameters) CALL_FrustumfOES(disp, parameters)
#define GET_Frustumf(disp) GET_FrustumfOES(disp)
#define SET_Frustumf(disp, fn) SET_FrustumfOES(disp, fn)
#define CALL_GetClipPlanef(disp, parameters) CALL_GetClipPlanefOES(disp, parameters)
#define GET_GetClipPlanef(disp) GET_GetClipPlanefOES(disp)
#define SET_GetClipPlanef(disp, fn) SET_GetClipPlanefOES(disp, fn)
#define CALL_Orthof(disp, parameters) CALL_OrthofOES(disp, parameters)
#define GET_Orthof(disp) GET_OrthofOES(disp)
#define SET_Orthof(disp, fn) SET_OrthofOES(disp, fn)

#if FEATURE_remap_table
#define ClearDepthfOES_remap_index ClearDepthf_remap_index
#define DepthRangefOES_remap_index DepthRangef_remap_index
#define AlphaFuncx_remap_index AlphaFuncxOES_remap_index
#define ClearColorx_remap_index ClearColorxOES_remap_index
#define ClearDepthx_remap_index ClearDepthxOES_remap_index
#define ClipPlanex_remap_index ClipPlanexOES_remap_index
#define Color4x_remap_index Color4xOES_remap_index
#define DepthRangex_remap_index DepthRangexOES_remap_index
#define Fogx_remap_index FogxOES_remap_index
#define Fogxv_remap_index FogxvOES_remap_index
#define Frustumx_remap_index FrustumxOES_remap_index
#define GetClipPlanex_remap_index GetClipPlanexOES_remap_index
#define GetFixedv_remap_index GetFixedvOES_remap_index
#define GetLightxv_remap_index GetLightxvOES_remap_index
#define GetMaterialxv_remap_index GetMaterialxvOES_remap_index
#define GetTexEnvxv_remap_index GetTexEnvxvOES_remap_index
#define GetTexParameterxv_remap_index GetTexParameterxvOES_remap_index
#define LightModelx_remap_index LightModelxOES_remap_index
#define LightModelxv_remap_index LightModelxvOES_remap_index
#define Lightx_remap_index LightxOES_remap_index
#define Lightxv_remap_index LightxvOES_remap_index
#define LineWidthx_remap_index LineWidthxOES_remap_index
#define LoadMatrixx_remap_index LoadMatrixxOES_remap_index
#define Materialx_remap_index MaterialxOES_remap_index
#define Materialxv_remap_index MaterialxvOES_remap_index
#define MultMatrixx_remap_index MultMatrixxOES_remap_index
#define MultiTexCoord4x_remap_index MultiTexCoord4xOES_remap_index
#define Normal3x_remap_index Normal3xOES_remap_index
#define Orthox_remap_index OrthoxOES_remap_index
#define PointParameterx_remap_index PointParameterxOES_remap_index
#define PointParameterxv_remap_index PointParameterxvOES_remap_index
#define PointSizex_remap_index PointSizexOES_remap_index
#define PolygonOffsetx_remap_index PolygonOffsetxOES_remap_index
#define Rotatex_remap_index RotatexOES_remap_index
#define SampleCoveragex_remap_index SampleCoveragexOES_remap_index
#define Scalex_remap_index ScalexOES_remap_index
#define TexEnvx_remap_index TexEnvxOES_remap_index
#define TexEnvxv_remap_index TexEnvxvOES_remap_index
#define TexParameterx_remap_index TexParameterxOES_remap_index
#define TexParameterxv_remap_index TexParameterxvOES_remap_index
#define Translatex_remap_index TranslatexOES_remap_index
#define ClipPlanef_remap_index ClipPlanefOES_remap_index
#define Frustumf_remap_index FrustumfOES_remap_index
#define GetClipPlanef_remap_index GetClipPlanefOES_remap_index
#define Orthof_remap_index OrthofOES_remap_index
#endif /* FEATURE_remap_table */


#endif /* !defined( _DISPATCH_H_ ) */
