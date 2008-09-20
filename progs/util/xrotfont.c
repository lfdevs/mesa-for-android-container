/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2004  Brian Paul   All Rights Reserved.
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
 */


/*
 * glXUseRotatedXFontMESA() function - like glXUseXFont() but allows
 * specification of a 0, 90, 180 or 270 degree rotation.
 * Handy for drawing labels along vertical axes of graphs, etc.
 *
 * Based on Mesa's glXUseXFont code written and copyrighted by Thorsten Ohl.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>



/**
 * Generate OpenGL-compatible bitmap by drawing an X character glyph
 * to an off-screen pixmap, then getting the image and testing pixels.
 * \param width  bitmap width in pixels
 * \param height  bitmap height in pixels
 */
static void
fill_bitmap(Display *dpy, Pixmap pixmap, GC gc,
	    unsigned int bitmapWidth, unsigned int bitmapHeight,
            unsigned int charWidth, unsigned int charHeight,
	    int xPos, int yPos, unsigned int c, GLubyte * bitmap,
            int rotation)
{
   const int bytesPerRow = (bitmapWidth + 7) / 8;
   XImage *image;
   XChar2b char2b;

   /* clear pixmap to 0 */
   XSetForeground(dpy, gc, 0);
   XFillRectangle(dpy, pixmap, gc, 0, 0, charWidth, charHeight);

   /* The glyph is drawn snug up against the left/top edges of the pixmap */
   XSetForeground(dpy, gc, 1);
   char2b.byte1 = (c >> 8) & 0xff;
   char2b.byte2 = (c & 0xff);
   XDrawString16(dpy, pixmap, gc, xPos, yPos, &char2b, 1);

   /* initialize GL bitmap */
   memset(bitmap, 0, bytesPerRow * bitmapHeight);

   image = XGetImage(dpy, pixmap, 0, 0, charWidth, charHeight, 1, XYPixmap);
   if (image) {
      /* Set appropriate bits in the GL bitmap.
       * Note: X11 and OpenGL are upside down wrt each other).
       */
      unsigned int x, y;
      if (rotation == 0) {
         for (y = 0; y < charHeight; y++) {
            for (x = 0; x < charWidth; x++) {
               if (XGetPixel(image, x, y)) {
                  int y2 = bitmapHeight - y - 1;
                  bitmap[bytesPerRow * y2 + x / 8] |= (1 << (7 - (x % 8)));
               }
            }
         }
      }
      else if (rotation == 90) {
         for (y = 0; y < charHeight; y++) {
            for (x = 0; x < charWidth; x++) {
               if (XGetPixel(image, x, y)) {
                  int x2 = y;
                  int y2 = x;
                  bitmap[bytesPerRow * y2 + x2 / 8] |= (1 << (7 - (x2 % 8)));
               }
            }
         }
      }
      else if (rotation == 180) {
         for (y = 0; y < charHeight; y++) {
            for (x = 0; x < charWidth; x++) {
               if (XGetPixel(image, x, y)) {
                  int x2 = charWidth - x - 1;
                  bitmap[bytesPerRow * y + x2 / 8] |= (1 << (7 - (x2 % 8)));
               }
            }
         }
      }
      else {
         for (y = 0; y < charHeight; y++) {
            for (x = 0; x < charWidth; x++) {
               if (XGetPixel(image, x, y)) {
                  int x2 = charHeight - y - 1;
                  int y2 = charWidth - x - 1;
                  bitmap[bytesPerRow * y2 + x2 / 8] |= (1 << (7 - (x2 % 8)));
               }
            }
         }
      }
      XDestroyImage(image);
   }
}


/**
 * Determine if a given glyph is valid and return the
 * corresponding XCharStruct.
 */
static const XCharStruct *
isvalid(const XFontStruct * fs, unsigned int which)
{
   unsigned int rows, pages;
   unsigned int byte1 = 0, byte2 = 0;
   int i, valid = 1;

   rows = fs->max_byte1 - fs->min_byte1 + 1;
   pages = fs->max_char_or_byte2 - fs->min_char_or_byte2 + 1;

   if (rows == 1) {
      /* "linear" fonts */
      if ((fs->min_char_or_byte2 > which) || (fs->max_char_or_byte2 < which))
	 valid = 0;
   }
   else {
      /* "matrix" fonts */
      byte2 = which & 0xff;
      byte1 = which >> 8;
      if ((fs->min_char_or_byte2 > byte2) ||
	  (fs->max_char_or_byte2 < byte2) ||
	  (fs->min_byte1 > byte1) || (fs->max_byte1 < byte1))
	 valid = 0;
   }

   if (valid) {
      if (fs->per_char) {
	 if (rows == 1) {
	    /* "linear" fonts */
	    return fs->per_char + (which - fs->min_char_or_byte2);
	 }
	 else {
	    /* "matrix" fonts */
	    i = ((byte1 - fs->min_byte1) * pages) +
	       (byte2 - fs->min_char_or_byte2);
	    return fs->per_char + i;
	 }
      }
      else {
	 return &fs->min_bounds;
      }
   }
   return NULL;
}


/**
 * Just like glXUseXFont() but with a rotation parameter.
 * \param rotation may be 0, 90, 180 or 270 only.
 */
void
glXUseRotatedXFontMESA(Font font, int first, int count, int listbase,
                       int rotation)
{
   Display *dpy;
   Window win;
   Pixmap pixmap;
   GC gc;
   XFontStruct *fs;
   GLint swapbytes, lsbfirst, rowlength;
   GLint skiprows, skippixels, alignment;
   unsigned int maxCharWidth, maxCharHeight;
   GLubyte *bm;
   int i;

   if (rotation != 0 &&
       rotation != 90 &&
       rotation != 180 &&
       rotation != 270)
      return;

   dpy = glXGetCurrentDisplay();
   if (!dpy)
      return;			/* I guess glXMakeCurrent wasn't called */
   win = RootWindow(dpy, DefaultScreen(dpy));

   fs = XQueryFont(dpy, font);
   if (!fs) {
      fprintf(stderr, "XQueryFont failed");
      return;
   }

   /* Allocate a GL bitmap that can fit any character */
   maxCharWidth = fs->max_bounds.rbearing - fs->min_bounds.lbearing;
   maxCharHeight = fs->max_bounds.ascent + fs->max_bounds.descent;
   /* use max, in case we're rotating */
   if (rotation == 90 || rotation == 270) {
      /* swap width/height */
      bm = (GLubyte *) malloc((maxCharHeight + 7) / 8 * maxCharWidth);
   }
   else {
      /* normal or upside down */
      bm = (GLubyte *) malloc((maxCharWidth + 7) / 8 * maxCharHeight);
   }
   if (!bm) {
      XFreeFontInfo(NULL, fs, 1);
      fprintf(stderr, "Out of memory in glXUseRotatedXFontMESA");
      return;
   }

   /* Save the current packing mode for bitmaps.  */
   glGetIntegerv(GL_UNPACK_SWAP_BYTES, &swapbytes);
   glGetIntegerv(GL_UNPACK_LSB_FIRST, &lsbfirst);
   glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlength);
   glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skiprows);
   glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skippixels);
   glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);

   /* Enforce a standard packing mode which is compatible with
      fill_bitmap() from above.  This is actually the default mode,
      except for the (non)alignment.  */
   glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
   glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
   glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
   glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

   /* Create pixmap and GC */
   pixmap = XCreatePixmap(dpy, win, maxCharWidth, maxCharHeight, 1);
   {
      XGCValues values;
      unsigned long valuemask;
      values.foreground = BlackPixel(dpy, DefaultScreen(dpy));
      values.background = WhitePixel(dpy, DefaultScreen(dpy));
      values.font = fs->fid;
      valuemask = GCForeground | GCBackground | GCFont;
      gc = XCreateGC(dpy, pixmap, valuemask, &values);
   }

   for (i = 0; i < count; i++) {
      const unsigned int c = first + i;
      const int list = listbase + i;
      unsigned int charWidth, charHeight;
      unsigned int bitmapWidth, bitmapHeight;
      GLfloat xOrig, yOrig, xStep, yStep, dtemp;
      const XCharStruct *ch;
      int xPos, yPos;
      int valid;

      /* check on index validity and get the bounds */
      ch = isvalid(fs, c);
      if (!ch) {
	 ch = &fs->max_bounds;
	 valid = 0;
      }
      else {
	 valid = 1;
      }

      /* glBitmap()' parameters:
         straight from the glXUseXFont(3) manpage.  */
      charWidth = ch->rbearing - ch->lbearing;
      charHeight = ch->ascent + ch->descent;
      xOrig = -ch->lbearing;
      yOrig = ch->descent;
      xStep = ch->width;
      yStep = 0;

      /* X11's starting point.  */
      xPos = -ch->lbearing;
      yPos = ch->ascent;

      /* Apply rotation */
      switch (rotation) {
      case 0:
         /* nothing */
         bitmapWidth = charWidth;
         bitmapHeight = charHeight;
         break;
      case 90:
         /* xStep, yStep */
         dtemp = xStep;
         xStep = -yStep;
         yStep = dtemp;
         /* xOrig, yOrig */
         yOrig = xOrig;
         xOrig = charHeight - (charHeight - yPos);
         /* width, height */
         bitmapWidth = charHeight;
         bitmapHeight = charWidth;
         break;
      case 180:
         /* xStep, yStep */
         xStep = -xStep;
         yStep = -yStep;
         /* xOrig, yOrig */
         xOrig = charWidth - xOrig - 1;
         yOrig = charHeight - yOrig - 1;
         bitmapWidth = charWidth;
         bitmapHeight = charHeight;
         break;
      case 270:
         /* xStep, yStep */
         dtemp = xStep;
         xStep = yStep;
         yStep = -dtemp;
         /* xOrig, yOrig */
         dtemp = yOrig;
         yOrig = charWidth - xOrig;
         xOrig = dtemp;
         /* width, height */
         bitmapWidth = charHeight;
         bitmapHeight = charWidth;
         break;
      default:
         /* should never get here */
         ;
      }

      glNewList(list, GL_COMPILE);
      if (valid && bitmapWidth > 0 && bitmapHeight > 0) {

	 fill_bitmap(dpy, pixmap, gc, bitmapWidth, bitmapHeight,
                     charWidth, charHeight,
                     xPos, yPos, c, bm, rotation);

	 glBitmap(bitmapWidth, bitmapHeight, xOrig, yOrig, xStep, yStep, bm);
      }
      else {
	 glBitmap(0, 0, 0.0, 0.0, xStep, yStep, NULL);
      }
      glEndList();
   }

   free(bm);
   XFreeFontInfo(NULL, fs, 1);
   XFreePixmap(dpy, pixmap);
   XFreeGC(dpy, gc);

   /* Restore saved packing modes.  */
   glPixelStorei(GL_UNPACK_SWAP_BYTES, swapbytes);
   glPixelStorei(GL_UNPACK_LSB_FIRST, lsbfirst);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlength);
   glPixelStorei(GL_UNPACK_SKIP_ROWS, skiprows);
   glPixelStorei(GL_UNPACK_SKIP_PIXELS, skippixels);
   glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
}
