/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Certain Win32-like platforms (i.e. Xbox GDK) do not support the GDI library.
 * stw_gdishim acts as a shim layer to provide the APIs required for gallium.
 */

#ifndef STW_GDISHIM_H
#define STW_GDISHIM_H

#ifdef _GAMING_XBOX

#include <windows.h>

/* Handles */
typedef void* HMONITOR;

/* Stubs */
#define WindowFromDC(hdc) (HWND)hdc
#define GetDC(hwnd) (HDC)hwnd
#define ReleaseDC(hwnd, hdc) 1

void StretchDIBits(HDC hdc, unsigned int xDest, unsigned int yDest, unsigned int DestWidth, unsigned int DestHeight, unsigned int xSrc, unsigned int ySrc, unsigned int SrcWidth, unsigned int SrcHeight, void* lpBits, void* lpbmi, unsigned int iUsage, DWORD rop);

/* Layer plane descriptor */
typedef struct tagLAYERPLANEDESCRIPTOR { // lpd
   WORD  nSize;
   WORD  nVersion;
   DWORD dwFlags;
   BYTE  iPixelType;
   BYTE  cColorBits;
   BYTE  cRedBits;
   BYTE  cRedShift;
   BYTE  cGreenBits;
   BYTE  cGreenShift;
   BYTE  cBlueBits;
   BYTE  cBlueShift;
   BYTE  cAlphaBits;
   BYTE  cAlphaShift;
   BYTE  cAccumBits;
   BYTE  cAccumRedBits;
   BYTE  cAccumGreenBits;
   BYTE  cAccumBlueBits;
   BYTE  cAccumAlphaBits;
   BYTE  cDepthBits;
   BYTE  cStencilBits;
   BYTE  cAuxBuffers;
   BYTE  iLayerPlane;
   BYTE  bReserved;
   COLORREF crTransparent;
} LAYERPLANEDESCRIPTOR, * PLAYERPLANEDESCRIPTOR, FAR* LPLAYERPLANEDESCRIPTOR;

/* WGL */
typedef struct _WGLSWAP
{
   HDC hdc;
   UINT uiFlags;
} WGLSWAP;

#define WGL_SWAPMULTIPLE_MAX 16

WINGDIAPI DWORD WINAPI
wglSwapMultipleBuffers(UINT n,
   CONST WGLSWAP* ps);

WINGDIAPI BOOL  WINAPI wglDeleteContext(HGLRC);

/* wglSwapLayerBuffers flags */
#define WGL_SWAP_MAIN_PLANE     0x00000001

/* Pixel format descriptor */
typedef struct tagPIXELFORMATDESCRIPTOR
{
   WORD  nSize;
   WORD  nVersion;
   DWORD dwFlags;
   BYTE  iPixelType;
   BYTE  cColorBits;
   BYTE  cRedBits;
   BYTE  cRedShift;
   BYTE  cGreenBits;
   BYTE  cGreenShift;
   BYTE  cBlueBits;
   BYTE  cBlueShift;
   BYTE  cAlphaBits;
   BYTE  cAlphaShift;
   BYTE  cAccumBits;
   BYTE  cAccumRedBits;
   BYTE  cAccumGreenBits;
   BYTE  cAccumBlueBits;
   BYTE  cAccumAlphaBits;
   BYTE  cDepthBits;
   BYTE  cStencilBits;
   BYTE  cAuxBuffers;
   BYTE  iLayerType;
   BYTE  bReserved;
   DWORD dwLayerMask;
   DWORD dwVisibleMask;
   DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, * PPIXELFORMATDESCRIPTOR, FAR* LPPIXELFORMATDESCRIPTOR;

/* Bitmap Info */
typedef struct tagRGBQUAD {
   BYTE rgbBlue;
   BYTE rgbGreen;
   BYTE rgbRed;
   BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFO {
   BITMAPINFOHEADER bmiHeader;
   RGBQUAD          bmiColors[1];
} BITMAPINFO, * LPBITMAPINFO, * PBITMAPINFO;

/* Glyph Info */

typedef struct tagGLYPHMETRICSFLOAT {
  FLOAT      gmfBlackBoxX;
  FLOAT      gmfBlackBoxY;
#ifndef _GAMING_XBOX
  POINTFLOAT gmfptGlyphOrigin;
#else
  FLOAT gmfptGlyphOriginX;
  FLOAT gmfptGlyphOriginY;
#endif
  FLOAT      gmfCellIncX;
  FLOAT      gmfCellIncY;
} GLYPHMETRICSFLOAT, *PGLYPHMETRICSFLOAT, *LPGLYPHMETRICSFLOAT;

#endif /* _GAMING_XBOX */

#endif /* STW_PIXELFORMAT_H */
