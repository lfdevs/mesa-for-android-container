#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <memory>

#include <gtest/gtest.h>

#include "GL/osmesa.h"
#include "util/macros.h"
#include "util/u_endian.h"
#include "util/u_math.h"

typedef struct {
   unsigned format;
   GLenum type;
   int bpp;
   uint64_t expected;
} Params;

class OSMesaRenderTestFixture : public testing::TestWithParam<Params> {};

std::string
name_params(const testing::TestParamInfo<Params> params) {
   auto p = params.param;
   std::string first, second;
   switch (p.format) {
   case OSMESA_RGBA:
      first = "rgba";
      break;
   case OSMESA_BGRA:
      first = "bgra";
      break;
   case OSMESA_RGB:
      first = "rgb";
      break;
   case OSMESA_RGB_565:
      first = "rgb_565";
      break;
   case OSMESA_ARGB:
      first = "argb";
      break;
   }

   switch (p.type) {
   case GL_UNSIGNED_SHORT:
      second = "unsigned_short";
      break;
   case GL_UNSIGNED_BYTE:
      second = "unsigned_byte";
      break;
   case GL_FLOAT:
      second = "float";
      break;
   case GL_UNSIGNED_SHORT_5_6_5:
      second = "unsigned_short_565";
      break;
   }

   return first + "_" + second;
};

TEST_P(OSMesaRenderTestFixture, Render)
{
   auto p = GetParam();
   const int w = 2, h = 2;
   uint8_t pixels[w * h * 8] = { 0 };

   std::unique_ptr<osmesa_context, decltype(&OSMesaDestroyContext)> ctx{
      OSMesaCreateContext(p.format, NULL), &OSMesaDestroyContext};
   ASSERT_TRUE(ctx);

   auto ret = OSMesaMakeCurrent(ctx.get(), &pixels, p.type, w, h);
   ASSERT_EQ(ret, GL_TRUE);

   glClearColor(0.25, 1.0, 0.5, 0.75);

   uint64_t expected = p.expected;

   /* All the formats other than 565 and RGB/byte are array formats, but our
    * expected values are packed, so byte swap appropriately.
    */
   if (UTIL_ARCH_BIG_ENDIAN) {
      switch (p.bpp) {
      case 8:
         expected = util_bswap64(expected);
         break;

      case 4:
         expected = util_bswap32(expected);
         break;

      case 3:
      case 2:
         break;
      }
   }

   glClear(GL_COLOR_BUFFER_BIT);
   glFinish();

#if 0 /* XXX */
   for (unsigned i = 0; i < ARRAY_SIZE(pixels); i += 4) {
      fprintf(stderr, "pixel %d: %02x %02x %02x %02x\n",
              i / 4,
              pixels[i + 0],
              pixels[i + 1],
              pixels[i + 2],
              pixels[i + 3]);
   }
#endif

   for (unsigned i = 0; i < w * h; i++) {
      switch (p.bpp) {
      case 2: {
         uint16_t color = 0;
         memcpy(&color, &pixels[i * p.bpp], p.bpp);
         ASSERT_EQ(expected, color);
         break;
      }

      case 3: {
         uint32_t color = ((pixels[i * p.bpp + 0] << 0) |
                           (pixels[i * p.bpp + 1] << 8) |
                           (pixels[i * p.bpp + 2] << 16));
         ASSERT_EQ(expected, color);
         break;
      }

      case 4: {
         uint32_t color = 0;
         memcpy(&color, &pixels[i * p.bpp], p.bpp);
         ASSERT_EQ(expected, color);
         break;
      }

      case 8: {
         uint64_t color = 0;
         memcpy(&color, &pixels[i * p.bpp], p.bpp);
         ASSERT_EQ(expected, color);
         break;
      }

      default:
         unreachable("bad bpp");
      }
   }
}

INSTANTIATE_TEST_CASE_P(
   OSMesaRenderTest,
   OSMesaRenderTestFixture,
   testing::Values(
      Params{ OSMESA_RGBA, GL_UNSIGNED_BYTE,  4, 0xbf80ff40 },
      Params{ OSMESA_BGRA, GL_UNSIGNED_BYTE,  4, 0xbf40ff80 },
      Params{ OSMESA_ARGB, GL_UNSIGNED_BYTE,  4, 0x80ff40bf},
      Params{ OSMESA_RGB,  GL_UNSIGNED_BYTE,  3, 0x80ff40 },
      Params{ OSMESA_RGBA, GL_UNSIGNED_SHORT, 8, 0xbfff8000ffff4000ull },
      Params{ OSMESA_RGB_565, GL_UNSIGNED_SHORT_5_6_5, 2, ((0x10 << 0) |
                                                           (0x3f << 5) |
                                                           (0x8 << 11)) }
   ),
   name_params
);
