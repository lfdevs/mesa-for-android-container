#include "wrapper_bcdec.h"
#include "wrapper_log.h"
#include "wrapper_util.h"

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "util/xxhash.h"

#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION

#include "bcdec.h"

#define WRAPPER_CACHE_DIR "/data/data/com.winlator.cmod/files/imagefs/usr/cache"

struct decompression_params {
   int block_x;
   int block_y_count;
   int block_y_start;
   int stride;
   int texel_size;
   VkFormat format;
   char *src;
   char *dst;
};

static int
get_block_size(VkFormat format)
{
    switch(format) {
       case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
       case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
       case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
       case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
       case VK_FORMAT_BC4_UNORM_BLOCK:
       case VK_FORMAT_BC4_SNORM_BLOCK:
          return 8;
       default:
          return 16;
    }
}

static void *
decompression_routine(void *args)
{
   struct decompression_params *params = args;

   char *src = params->src;
   char *dst_base = params->dst;

   for (int by = 0; by < params->block_y_count; by++) {
      for (int bx = 0; bx < params->block_x; bx++) {
         int pixel_x = (bx * 4);
         int pixel_y = (by + params->block_y_start) * 4;
         char *dst = dst_base + (pixel_y * params->stride) + (pixel_x * params->texel_size);
         if (!dst || !src)
            return NULL;

         switch (params->format) {
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
               bcdec_bc1(src, dst, params->stride);
               src += BCDEC_BC1_BLOCK_SIZE;
               break;
            case VK_FORMAT_BC2_SRGB_BLOCK:
            case VK_FORMAT_BC2_UNORM_BLOCK:
               bcdec_bc2(src, dst, params->stride);
               src += BCDEC_BC2_BLOCK_SIZE;
               break;
            case VK_FORMAT_BC3_UNORM_BLOCK:
            case VK_FORMAT_BC3_SRGB_BLOCK:
               bcdec_bc3(src, dst, params->stride);
               src += BCDEC_BC3_BLOCK_SIZE;
               break;
            case VK_FORMAT_BC4_UNORM_BLOCK:
            case VK_FORMAT_BC4_SNORM_BLOCK:
               bcdec_bc4(src, dst, params->stride, params->format == VK_FORMAT_BC4_SNORM_BLOCK);
               src += BCDEC_BC4_BLOCK_SIZE;
               break;
            case VK_FORMAT_BC5_SNORM_BLOCK:
            case VK_FORMAT_BC5_UNORM_BLOCK:
               bcdec_bc5(src, dst, params->stride, params->format == VK_FORMAT_BC5_SNORM_BLOCK);
               src += BCDEC_BC5_BLOCK_SIZE;
               break;
            case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            case VK_FORMAT_BC6H_UFLOAT_BLOCK:
               bcdec_bc6h_half(src, dst, (params->stride / params->texel_size) * 3, params->format == VK_FORMAT_BC6H_SFLOAT_BLOCK);
               src += BCDEC_BC6H_BLOCK_SIZE;
               break;
            case VK_FORMAT_BC7_SRGB_BLOCK:
            case VK_FORMAT_BC7_UNORM_BLOCK:
               bcdec_bc7(src, dst, params->stride);
               src += BCDEC_BC7_BLOCK_SIZE;
               break;
            default:
               break;
         }
      }
   }

   return NULL;
}

VkFormat
get_format_for_bcn(VkFormat bcn_format)
{
   switch(bcn_format) {
      case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
      case VK_FORMAT_BC2_SRGB_BLOCK:
      case VK_FORMAT_BC3_SRGB_BLOCK:
      case VK_FORMAT_BC7_SRGB_BLOCK:
         return VK_FORMAT_R8G8B8A8_SRGB;
      case VK_FORMAT_BC4_UNORM_BLOCK:
         return VK_FORMAT_R8_UNORM;
      case VK_FORMAT_BC4_SNORM_BLOCK:
         return VK_FORMAT_R8_SNORM;
      case VK_FORMAT_BC5_UNORM_BLOCK:
          return VK_FORMAT_R8G8_UNORM;
      case VK_FORMAT_BC5_SNORM_BLOCK:
         return VK_FORMAT_R8G8_SNORM;
      case VK_FORMAT_BC6H_SFLOAT_BLOCK:
      case VK_FORMAT_BC6H_UFLOAT_BLOCK:
         return VK_FORMAT_R16G16B16_SFLOAT;
      default:
         return VK_FORMAT_R8G8B8A8_UNORM;
   }
}

int
get_texel_size_for_format(VkFormat format)
{
   switch (format) {
      case VK_FORMAT_R16G16B16_SFLOAT:
         return 6;
      case VK_FORMAT_R8G8_UNORM:
      case VK_FORMAT_R8G8_SNORM:
         return 2;
      case VK_FORMAT_R8_UNORM:
      case VK_FORMAT_R8_SNORM:
         return 1;
      default:
         return 4;
   }
}

int
is_emulated_bcn(struct wrapper_physical_device *pdev, VkFormat format)
{
   switch(format) {
      case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
      case VK_FORMAT_BC2_SRGB_BLOCK:
      case VK_FORMAT_BC2_UNORM_BLOCK:
      case VK_FORMAT_BC3_UNORM_BLOCK:
      case VK_FORMAT_BC3_SRGB_BLOCK:
         if (pdev->emulate_bcn == 3 &&
             pdev->driver_properties.driverID == VK_DRIVER_ID_SAMSUNG_PROPRIETARY)
         {
            return 0;
         }
         else if (pdev->emulate_bcn > 1) {
            return 1;
         } else {
            return 0;
         }
         break;
      case VK_FORMAT_BC4_UNORM_BLOCK:
      case VK_FORMAT_BC4_SNORM_BLOCK:
      case VK_FORMAT_BC5_SNORM_BLOCK:
      case VK_FORMAT_BC5_UNORM_BLOCK:
      case VK_FORMAT_BC6H_SFLOAT_BLOCK:
      case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      case VK_FORMAT_BC7_SRGB_BLOCK:
      case VK_FORMAT_BC7_UNORM_BLOCK:
         if (pdev->emulate_bcn > 1)
            return 1;
         else
            return 0;
         break;
      default:
         return 0;
   }
}

void
decompress_bcn_format(void *srcBuffer,
					  void *dstBuffer,
					  int w,
					  int h,
					  VkFormat format,
					  int offset)
{
   static int wrapper_mark_bcn =  -1;
   static int wrapper_no_bcn_thread = -1;
   static int wrapper_use_bcn_cache = -1;
   static char *wrapper_cache_path = NULL;

   if (wrapper_mark_bcn == -1)
      wrapper_mark_bcn = getenv("WRAPPER_MARK_BCN") && atoi(getenv("WRAPPER_MARK_BCN"));

   if (wrapper_no_bcn_thread == -1)
      wrapper_no_bcn_thread = getenv("WRAPPER_NO_BCN_THREAD") && atoi(getenv("WRAPPER_NO_BCN_THREAD"));

   if (wrapper_use_bcn_cache == -1)
      wrapper_use_bcn_cache = getenv("WRAPPER_USE_BCN_CACHE") ? atoi(getenv("WRAPPER_USE_BCN_CACHE")) : 0;

   if (wrapper_cache_path == NULL)
      wrapper_cache_path = getenv("WRAPPER_CACHE_PATH") ? getenv("WRAPPER_CACHE_PATH") : WRAPPER_CACHE_DIR;

   int texel_size = get_texel_size_for_format(get_format_for_bcn(format));
   int block_size = get_block_size(format);
   int block_x = (w + 3) / 4;
   int block_y = (h + 3) / 4;
   int stride = w * texel_size;
   int compressed_size = (block_x * block_y * block_size);
   int uncompressed_size = w * h * texel_size;
   char *src = srcBuffer + offset;
   char *dst = dstBuffer;

   if (wrapper_mark_bcn) {
      WRAPPER_LOG(bcn, "Filling %dx%d BCn %d texture with custom color", w, h, format);

      for (int i = 0; i < h; i++) {
         for (int j = 0; j < w; j++) {
            dst = dstBuffer + (i * stride) + (j * texel_size);

            switch(format) {
               case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
               case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
               case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
               case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
                  /* Yellow */
                  dst[0] = 0xFF;
                  dst[1] = 0xFF;
                  dst[2] = 0;
                  dst[3] = 255;
                  break;
               case VK_FORMAT_BC2_SRGB_BLOCK:
               case VK_FORMAT_BC2_UNORM_BLOCK:
                  /* Blue */
                  dst[0] = 0;
                  dst[1] = 0;
                  dst[2] = 0xFF;
                  dst[3] = 255;
                  break;
                case VK_FORMAT_BC3_UNORM_BLOCK:
                case VK_FORMAT_BC3_SRGB_BLOCK:
                  /* Light Blue */
                  dst[0] = 0;
                  dst[1] = 0xFF;
                  dst[2] = 0xFF;
                  dst[3] = 255;
                  break;
               case VK_FORMAT_BC4_UNORM_BLOCK:
               case VK_FORMAT_BC4_SNORM_BLOCK:
                  /* Red */
                  dst[0] = 0xFF;
                  break;
               case VK_FORMAT_BC5_UNORM_BLOCK:
               case VK_FORMAT_BC5_SNORM_BLOCK:
                  /* Green */
                  dst[0] = 0;
                  dst[1] = 0xFF;
                  break;
               case VK_FORMAT_BC6H_SFLOAT_BLOCK:
               case VK_FORMAT_BC6H_UFLOAT_BLOCK:
                  /* Purple */
                  dst[0] = 0x90;
                  dst[1] = 0x40;
                  dst[2] = 0xA0;
                  break;
               case VK_FORMAT_BC7_UNORM_BLOCK:
               case VK_FORMAT_BC7_SRGB_BLOCK:
                  /* Black */
                  dst[0] = 0xFF;
                  dst[1] = 0;
                  dst[2] = 0xFF;
                  dst[3] = 255;
                  break;
               default:
                  break;
            }
         }
      }

      return;
   }

   CREATE_FOLDER(WRAPPER_CACHE_DIR, 0700);

   XXH64_hash_t hash = XXH64(src, compressed_size, 0);
   char *cache_filename;
   asprintf(&cache_filename, "%s/%s_%llu.cache", wrapper_cache_path, get_executable_name(), (unsigned long long)hash);

   if (access(cache_filename, F_OK) == 0 && wrapper_use_bcn_cache) {
      FILE *fp = fopen(cache_filename, "rb");
      size_t length = fread(dst, 1, uncompressed_size, fp);
      fclose(fp);
      if (length == uncompressed_size) {
         WRAPPER_LOG(bcn, "Successfully restored texture %s from cache", cache_filename);
         free(cache_filename);
         return;
      }
      else {
         WRAPPER_LOG(bcn, "Failed to restore texture %s from cache, decompressing from huffer", cache_filename);
         unlink(cache_filename);
      }
   }

   if (wrapper_no_bcn_thread) {
      WRAPPER_LOG(bcn, "Decompressing %dx%d BCN %d texture from main thread",
         w, h, format);

      struct decompression_params args[1];
      args[0].src = src;
      args[0].dst = dst;
      args[0].block_x = block_x;
      args[0].format = format;
      args[0].block_y_count = block_y;
      args[0].block_y_start = 0;
      args[0].stride = stride;
      args[0].texel_size = texel_size;
      decompression_routine(&args[0]);
   } else {
      int core_count = sysconf(_SC_NPROCESSORS_CONF);
      int num_threads;
      if (block_y >= core_count)
         num_threads = core_count;
      else if (block_y >= 4)
         num_threads = 4;
      else
         num_threads = 1;

      int rows_per_thread = block_y / num_threads;
      int rem = block_y % num_threads;

      pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
      struct decompression_params *args = malloc(sizeof(struct decompression_params) * num_threads);
      int current_row = 0;

      WRAPPER_LOG(bcn, "Decompressing %dx%d BCN %d texture using %d threads",
         w, h, format, num_threads);

      for (int i = 0; i < num_threads; i++) {
         int rows = rows_per_thread + ((i < rem) ? 1 : 0);
         args[i].src = src + (current_row * block_x * block_size);
         args[i].dst = dst;
         args[i].block_x = block_x;
         args[i].format = format;
         args[i].block_y_count = rows;
         args[i].block_y_start = current_row;
         args[i].stride = stride;
         args[i].texel_size = texel_size;
         pthread_create(&threads[i], NULL, decompression_routine, &args[i]);
         current_row += rows;
      }

      for (int i = 0; i < num_threads; i++) {
         pthread_join(threads[i], NULL);
      }

      free(threads);
      free(args);
   }

   if (access(cache_filename, F_OK) != 0 && wrapper_use_bcn_cache) {
      FILE *fp = fopen(cache_filename, "wb");
      size_t length = fwrite(dst, 1, uncompressed_size, fp);
      fclose(fp);
      if (length == uncompressed_size)
         WRAPPER_LOG(bcn, "Saved texture %s to cache", cache_filename);
      else {
         WRAPPER_LOG(bcn, "Failed to save texture %s to cache", cache_filename);
         unlink(cache_filename);
      }
   }

   free(cache_filename);

}
