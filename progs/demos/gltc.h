/**
 * gltc: GL Texture Compression library.
 * Copyright (c) 2007 Nicolas Martyanoff <khaelin@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the 
 * "Software"), to deal in the Software without restriction, including 
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to 
 * permit persons to whom the Software is furnished to do so, subject to 
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __GLTC_GLTC_H__
#define __GLTC_GLTC_H__

/* -------------------------------------------------------------------
 *  The features supported by this version.
 * ------------------------------------------------------------------- */
/* Uncompressed formats */
#define GLTC_HAS_A4_R4_G4_B4     1
#define GLTC_HAS_A1_R5_G5_B5     1
#define GLTC_HAS_R5_G6_B5        1
#define GLTC_HAS_R8_G8_B8        1
#define GLTC_HAS_A8_R8_G8_B8     1

/* S3TC formats */
#define GLTC_HAS_DXT1C           1
#define GLTC_HAS_DXT1A           1
#define GLTC_HAS_DXT3            1
#define GLTC_HAS_DXT5            1

/* -------------------------------------------------------------------
 *  Known-size types.
 * ------------------------------------------------------------------- */
typedef unsigned char gltc_byte;
typedef unsigned short gltc_word;
typedef unsigned long gltc_dword;

/* -------------------------------------------------------------------
 *  Texture compression formats.
 * ------------------------------------------------------------------- */
/* Raw uncompressed formats */
#define GLTC_A4_R4_G4_B4 0    /* 16 bits alpha uncompressed. */
#define GLTC_A1_R5_G5_B5 1    /* 16 bits alpha uncompressed. */
#define GLTC_R5_G6_B5 2       /* 16 bits alpha-less uncompressed. */
#define GLTC_R8_G8_B8 3       /* 24 bits uncompressed. */
#define GLTC_A8_R8_G8_B8 4    /* 32 bits alpha uncompressed. */

/* S3TC compressed formats */
#define GLTC_DXT1C 5          /* Alpha-less. */
#define GLTC_DXT1A 6          /* 1bit alpha. */
#define GLTC_DXT3 7           /* Direct alpha encoding. */
#define GLTC_DXT5 8           /* Interpolated alpha encoding. */

/* -------------------------------------------------------------------
 *  File formats.
 * ------------------------------------------------------------------- */
#define GLTC_DDS 0 /*  The DirectDraw Surface format. */

/* -------------------------------------------------------------------
 *  Errors.
 * ------------------------------------------------------------------- */
#define GLTC_OK 0
#define GLTC_MEM 1
#define GLTC_NOSUP 2
#define GLTC_BADARG 3
#define GLTC_IO 4
#define GLTC_BADFILE 5
#define GLTC_COMP 6

const char* gltc_error_string(gltc_dword error);

void gltc_dlog(const char *filename, ...);

/* -------------------------------------------------------------------
 *  A mipmap level in an image.
 * ------------------------------------------------------------------- */
struct gltc_level {
   gltc_dword width;
   gltc_dword height;
   gltc_dword size;

   gltc_byte *data;
};

/* -------------------------------------------------------------------
 *  A generic image.
 * ------------------------------------------------------------------- */
struct gltc_image {
   gltc_dword format;
   struct gltc_level *mipmap;
   gltc_dword num_levels;

   void (*get_texel)(const struct gltc_image*, gltc_dword,
         gltc_dword, gltc_dword, gltc_byte[4]);
};

gltc_dword gltc_image_width(const struct gltc_image *img,
      gltc_dword level);
gltc_dword gltc_image_height(const struct gltc_image *img,
      gltc_dword level);
gltc_byte* gltc_image_data(const struct gltc_image *img,
      gltc_dword level);
gltc_dword gltc_image_size(const struct gltc_image *img,
      gltc_dword level);
gltc_dword gltc_image_num_levels(const struct gltc_image *img);
gltc_dword gltc_image_bpp(const struct gltc_image *img,
      gltc_dword level);

gltc_dword gltc_image_create(struct gltc_image *img, gltc_dword format,
      gltc_dword width, gltc_dword height, gltc_dword num_levels);
void gltc_image_delete(struct gltc_image *img);

void gltc_image_get_texel(const struct gltc_image *img,
      gltc_dword level, gltc_dword x, gltc_dword y, gltc_byte col[4]);

gltc_dword gltc_image_convert(struct gltc_image *src,
      struct gltc_image *dst, gltc_dword format);

gltc_dword gltc_file_load(const char *filename,
      struct gltc_image *img, gltc_dword format);
gltc_dword gltc_file_save(const char *filename,
      const struct gltc_image *img, gltc_dword format);

gltc_dword gltc_has_format(gltc_dword format);

#endif
