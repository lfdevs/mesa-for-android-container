/* Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdlib.h>

#include "util/macros.h"
#include "util/u_math.h"
#include "amdgpu_devices.h"
#include "amdgfxregs.h"

#include "addrlib/inc/addrinterface.h"

#define addr_check(call) do { \
   int r = call; \
   if (r) { \
      fprintf(stderr, "%s failed. (error = %u)\n", #call, r); \
      exit(1); \
   } \
} while (0)

static void *ADDR_API allocSysMem(const ADDR_ALLOCSYSMEM_INPUT *pInput)
{
   return malloc(pInput->sizeInBytes);
}

static ADDR_E_RETURNCODE ADDR_API freeSysMem(const ADDR_FREESYSMEM_INPUT *pInput)
{
   free(pInput->pVirtAddr);
   return ADDR_OK;
}

static uint32_t
get_register_value(const struct amdgpu_device *dev, unsigned reg)
{
   for (unsigned i = 0; i < dev->mmr_reg_count; i++) {
      if (dev->mmr_regs[i * 3] == reg / 4)
         return dev->mmr_regs[i * 3 + 2];
   }

   UNREACHABLE("invalid reg");
}

static ADDR_HANDLE
addrlib_create(const struct amdgpu_device *dev)
{
   unsigned gfx_version = dev->hw_ip_gfx.hw_ip_version_major;
   uint32_t mc_arb_ramcfg = 0;
   uint32_t gb_tile_mode[32] = {0};
   uint32_t gb_macrotile_mode[32] = {0};

   if (gfx_version < 9) {
      mc_arb_ramcfg = get_register_value(dev, 0x9D8 * 4);

      for (unsigned i = 0; i < 32; i++)
         gb_tile_mode[i] = get_register_value(dev, R_009910_GB_TILE_MODE0 + i * 4);

      if (gfx_version >= 7) {
         for (unsigned i = 0; i < 16; i++)
            gb_macrotile_mode[i] = get_register_value(dev, R_009990_GB_MACROTILE_MODE0 + i * 4);
      }
   }

   const unsigned CIASICIDGFXENGINE_SOUTHERNISLAND = 0xA;
   const unsigned CIASICIDGFXENGINE_ARCTICISLAND = 0xD;

   ADDR_CREATE_INPUT create_info = {
      .size = sizeof(create_info),
      .chipEngine = gfx_version < 9 ? CIASICIDGFXENGINE_SOUTHERNISLAND :
                                      CIASICIDGFXENGINE_ARCTICISLAND,
      .chipFamily = dev->dev.family,
      .chipRevision = dev->dev.external_rev,
      .callbacks = {
         .allocSysMem = allocSysMem,
         .freeSysMem = freeSysMem,
      },
      .createFlags = {
         .useTileIndex = gfx_version < 9,
      },
      .regValue = {
         .gbAddrConfig = get_register_value(dev, R_0098F8_GB_ADDR_CONFIG),
         .backendDisables = gfx_version < 9 ? dev->dev.enabled_rb_pipes_mask : 0,
         .noOfBanks = gfx_version < 9 ? mc_arb_ramcfg & 0x3 : 0,
         .noOfRanks = gfx_version < 9 ? (mc_arb_ramcfg & 0x4) >> 2 : 0,
         .pTileConfig = gfx_version < 9 ? gb_tile_mode : NULL,
         .noOfEntries = 32,
         .pMacroTileConfig = gfx_version >= 7 && gfx_version < 9 ? gb_macrotile_mode : NULL,
         .noOfMacroEntries = gfx_version >= 7 && gfx_version < 9 ? 16 : 0,
      },
   };
   ADDR_CREATE_OUTPUT lib = {
      .size = sizeof(lib),
   };

   addr_check(AddrCreate(&create_info, &lib));
   return lib.hLib;
}

#define DIPLAYABLE BITFIELD_BIT(5)

typedef struct {
   unsigned tile_mode_index;
} legacy_tile_info;

static void
get_tile_size(ADDR_HANDLE hlib, unsigned gfx_version, unsigned bpp, unsigned swizzle_mode,
              unsigned *tile_width, unsigned *tile_height, legacy_tile_info *legacy_info)
{
   AddrFormat format;

   switch (bpp) {
   case 8:
      format = ADDR_FMT_8;
      break;
   case 16:
      format = ADDR_FMT_8_8;
      break;
   case 32:
      format = ADDR_FMT_8_8_8_8;
      break;
   case 64:
      format = ADDR_FMT_16_16_16_16;
      break;
   default:
      UNREACHABLE("invalid bpp");
   }

   /* We only need the tile size from this. */
   if (gfx_version >= 12) {
      ADDR3_COMPUTE_SURFACE_INFO_INPUT in = {
         .size = sizeof(in),
         .flags = {{0}},
         .swizzleMode = swizzle_mode,
         .resourceType = ADDR_RSRC_TEX_2D,
         .format = format,
         .bpp = bpp,
         .width = 4096,
         .height = 4096,
         .numSlices = 1,
         .numMipLevels = 1,
         .numSamples = 1,
         .pitchInElement = 4096,
      };
      ADDR3_COMPUTE_SURFACE_INFO_OUTPUT out = {
         .size = sizeof(out),
      };

      addr_check(Addr3ComputeSurfaceInfo(hlib, &in, &out));

      *tile_width = out.blockExtent.width;
      *tile_height = out.blockExtent.height;
      assert(out.blockExtent.depth == 1);
   } else if (gfx_version >= 9) {
      ADDR2_COMPUTE_SURFACE_INFO_INPUT in = {
         .size = sizeof(in),
         .flags = {
            .color = 1,
         },
         .swizzleMode = swizzle_mode,
         .resourceType = ADDR_RSRC_TEX_2D,
         .format = format,
         .bpp = bpp,
         .width = 4096,
         .height = 4096,
         .numSlices = 1,
         .numMipLevels = 1,
         .numSamples = 1,
         .numFrags = 1,
         .pitchInElement = 4096,
      };
      ADDR2_COMPUTE_SURFACE_INFO_OUTPUT out = {
         .size = sizeof(out),
      };

      addr_check(Addr2ComputeSurfaceInfo(hlib, &in, &out));

      *tile_width = out.blockWidth;
      *tile_height = out.blockHeight;
      assert(out.blockSlices == 1);
   } else {
      AddrTileMode mode = swizzle_mode & ~DIPLAYABLE;
      bool display = !!(swizzle_mode & DIPLAYABLE);
      ADDR_TILEINFO tile_info = {0};
      ADDR_COMPUTE_SURFACE_INFO_INPUT in = {
         .size = sizeof(in),
         .tileMode = mode,
         .format = format,
         .bpp = bpp,
         .numSamples = 1,
         .width = 4096,
         .height = 4096,
         .numSlices = 1,
         .numMipLevels = 1,
         .flags = {
            .color = 1,
            .display = display,
            .prt = mode == ADDR_TM_PRT_TILED_THIN1 ||
                   mode == ADDR_TM_PRT_2D_TILED_THIN1,
         },
         .numFrags = 1,
         .tileType = display ? ADDR_DISPLAYABLE : ADDR_NON_DISPLAYABLE,
         .pTileInfo = &tile_info,
         .tileIndex = -1,
      };
      ADDR_COMPUTE_SURFACE_INFO_OUTPUT out = {
         .size = sizeof(out),
      };

      addr_check(AddrComputeSurfaceInfo(hlib, &in, &out));

      *tile_width = out.pitchAlign;
      *tile_height = out.heightAlign;
      legacy_info->tile_mode_index = out.tileIndex;
      assert(out.depthAlign == 1);
      assert(out.tileMode == mode);
   }

   assert(*tile_width);
   assert(*tile_height);
}

static unsigned
addr_from_coord(ADDR_HANDLE hlib, unsigned gfx_version, legacy_tile_info *legacy_info,
                unsigned bpp, unsigned swizzle_mode, unsigned x, unsigned y)
{
   if (gfx_version >= 12) {
      ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT in = {
         .size = sizeof(in),
         .x = x,
         .y = y,

         .swizzleMode = swizzle_mode,
         .flags = {{0}},
         .resourceType = ADDR_RSRC_TEX_2D,
         .bpp = bpp,
         .unAlignedDims = {
            .width = 4096,
            .height = 4096,
            .depth = 1,
         },
         .numMipLevels = 1,
         .numSamples = 1,
         .pitchInElement = 4096,
      };
      ADDR3_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT out = {
         .size = sizeof(out),
      };

      addr_check(Addr3ComputeSurfaceAddrFromCoord(hlib, &in, &out));
      return out.addr;
   } else if (gfx_version >= 9) {
      ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT in = {
         .size = sizeof(in),
         .x = x,
         .y = y,

         .swizzleMode = swizzle_mode,
         .flags = {
            .color = 1,
         },
         .resourceType = ADDR_RSRC_TEX_2D,
         .bpp = bpp,
         .unalignedWidth = 4096,
         .unalignedHeight = 4096,
         .numSlices = 1,
         .numMipLevels = 1,
         .numSamples = 1,
         .numFrags = 1,
         .pitchInElement = 4096,
      };
      ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT out = {
         .size = sizeof(out),
      };

      addr_check(Addr2ComputeSurfaceAddrFromCoord(hlib, &in, &out));
      return out.addr;
   } else {
      ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT in = {
         .size = sizeof(in),
         .x = x,
         .y = y,

         .bpp = bpp,
         .pitch = 4096,
         .height = 4096,
         .numSlices = 1,
         .numSamples = 1,
         .numFrags = 1,
         .tileIndex = legacy_info->tile_mode_index,
      };
      ADDR_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT out = {
         .size = sizeof(out),
      };

      addr_check(AddrComputeSurfaceAddrFromCoord(hlib, &in, &out));
      return out.addr;
   }
}

static void
print_tiling_layouts(const struct amdgpu_device *dev)
{
   unsigned gfx_version = dev->hw_ip_gfx.hw_ip_version_major;

   ADDR_HANDLE lib = addrlib_create(dev);

   static const unsigned gfx6_array_modes_2D[] = {
      ADDR_TM_LINEAR_ALIGNED,
      ADDR_TM_1D_TILED_THIN1,
      ADDR_TM_1D_TILED_THIN1 | DIPLAYABLE,
#if 0 /* Layouts with XORs are commented out for now. */
      ADDR_TM_2D_TILED_THIN1,
      ADDR_TM_2D_TILED_THIN1 | DIPLAYABLE,
      ADDR_TM_PRT_TILED_THIN1,
#endif
      /* ADDR_TM_PRT_2D_TILED_THIN1 is not supported by gfx7-8 */
   };
   /* Note: Layouts with XORs are commented out for now. */
   static const unsigned gfx9_swizzle_mode_2D[] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, /*16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,*/
   };
   static const unsigned gfx10_swizzle_mode_2D[] = {
      0, 1, 2, 5, 6, 9, 10, /*17, 18, 21, 22, 23, 24, 25, 26, 27*/
   };
   static const unsigned gfx11_swizzle_mode_2D[] = {
      0, 2, 6, 10, /*18, 21, 22, 24, 25, 26, 27, 28, 29, 30, 31*/
   };
   static const unsigned gfx12_swizzle_modes_2D[] = {
      0, 1, 2, 3, 4
   };
   const unsigned *swizzle_modes_2D = NULL;
   unsigned num_swizzle_modes_2D = 0;

   if (gfx_version >= 12) {
      swizzle_modes_2D = gfx12_swizzle_modes_2D;
      num_swizzle_modes_2D = ARRAY_SIZE(gfx12_swizzle_modes_2D);
   } else if (gfx_version >= 11) {
      swizzle_modes_2D = gfx11_swizzle_mode_2D;
      num_swizzle_modes_2D = ARRAY_SIZE(gfx11_swizzle_mode_2D);
   } else if (gfx_version >= 10) {
      swizzle_modes_2D = gfx10_swizzle_mode_2D;
      num_swizzle_modes_2D = ARRAY_SIZE(gfx10_swizzle_mode_2D);
   } else if (gfx_version >= 9) {
      swizzle_modes_2D = gfx9_swizzle_mode_2D;
      num_swizzle_modes_2D = ARRAY_SIZE(gfx9_swizzle_mode_2D);
   } else {
      swizzle_modes_2D = gfx6_array_modes_2D;
      num_swizzle_modes_2D = ARRAY_SIZE(gfx6_array_modes_2D);
   }

   for (unsigned bpp = 8; bpp <= 64; bpp *= 2) {
      for (unsigned sw_index = 0; sw_index < num_swizzle_modes_2D; sw_index++) {
         unsigned swizzle_mode = swizzle_modes_2D[sw_index];

         unsigned tile_width = 0, tile_height = 0;
         legacy_tile_info legacy_info = {0};
         get_tile_size(lib, gfx_version, bpp, swizzle_mode, &tile_width, &tile_height, &legacy_info);

         unsigned num_x_bits = util_logbase2(tile_width);
         unsigned num_y_bits = util_logbase2(tile_height);

         ADDR_EQUATION equation = {
            .numBits = util_logbase2(bpp / 8) + num_x_bits + num_y_bits,
            .numBitComponents = 1,
         };

         for (unsigned coord = 0; coord < 2; coord++) {
            for (unsigned bit = 0; bit < (coord ? num_y_bits : num_x_bits); bit++) {
               unsigned x = coord == 0 ? BITFIELD_BIT(bit) : 0;
               unsigned y = coord == 1 ? BITFIELD_BIT(bit) : 0;
               unsigned addr = addr_from_coord(lib, gfx_version, &legacy_info, bpp, swizzle_mode, x, y);
               unsigned addr_pos = u_bit_scan(&addr);
               assert(addr == 0 && "layout with XORs are unhandled");
#if 0
               printf("bpp %u, sw %u, %cbit %u (%u,%u), pos %u\n", bpp, swizzle_mode, "xy"[coord], bit, x, y, bit_pos);
#endif
               assert(addr_pos < equation.numBits);
               assert(!equation.addr[addr_pos].valid);

               equation.addr[addr_pos].valid = true;
               equation.addr[addr_pos].channel = coord;
               equation.addr[addr_pos].index = bit;
            }
         }

         /* Print the equation. */
         printf("%s, bpe %u, sw %2u, {", dev->name, bpp / 8, swizzle_mode);
         for (unsigned bit = 0; bit < equation.numBits; bit++) {
            for (unsigned comp = 0; comp < equation.numBitComponents; comp++) {
               ADDR_CHANNEL_SETTING swizzle = equation.comps[comp][bit];

               if (swizzle.valid) {
                  printf("%c%u", "XYZS"[swizzle.channel], swizzle.index);
               } else {
                  printf(" 0");
               }
            }

            if (bit < equation.numBits - 1)
               printf(", ");
         }
         printf("}\n");
      }
   }

   AddrDestroy(lib);
}

int
main(int argc, char **argv)
{
   for (int d = 0; d < num_amdgpu_devices; d++) {
      const struct amdgpu_device *dev = &amdgpu_devices[d];

      print_tiling_layouts(dev);
   }

   return 0;
}
