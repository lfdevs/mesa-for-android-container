/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2017 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vk_internal_exts.h"
#include "vk_util.h"
#include "util/u_debug.h"

#include "compiler/spirv/nir_spirv.h"

uint32_t vk_get_driver_version(void)
{
   const char *minor_string = strchr(PACKAGE_VERSION, '.');
   const char *patch_string = minor_string ? strchr(minor_string + 1, '.') : NULL;
   int major = atoi(PACKAGE_VERSION);
   int minor = minor_string ? atoi(minor_string + 1) : 0;
   int patch = patch_string ? atoi(patch_string + 1) : 0;
   if (strstr(PACKAGE_VERSION, "devel")) {
      if (patch == 0) {
         patch = 99;
         if (minor == 0) {
            minor = 99;
            --major;
         } else
            --minor;
      } else
         --patch;
   }
   return VK_MAKE_VERSION(major, minor, patch);
}

uint32_t vk_get_version_override(void)
{
   const char *str = os_get_option("MESA_VK_VERSION_OVERRIDE");
   if (str == NULL)
      return 0;

   const char *minor_str = strchr(str, '.');
   const char *patch_str = minor_str ? strchr(minor_str + 1, '.') : NULL;

   int major = atoi(str);
   int minor = minor_str ? atoi(minor_str + 1) : 0;
   int patch = patch_str ? atoi(patch_str + 1) : VK_HEADER_VERSION;

   /* Do some basic version sanity checking */
   if (major < 1 || minor < 0 || patch < 0 || minor > 1023 || patch > 4095)
      return 0;

   return VK_MAKE_VERSION(major, minor, patch);
}

void
vk_warn_non_conformant_implementation(const char *driver_name)
{
   if (debug_get_bool_option("MESA_VK_IGNORE_CONFORMANCE_WARNING", false))
      return;

   fprintf(stderr, "WARNING: %s is not a conformant Vulkan implementation, "
                   "testing use only.\n", driver_name);
}

struct nir_spirv_specialization*
vk_spec_info_to_nir_spirv(const VkSpecializationInfo *vk_spec_info)
{
   if (vk_spec_info == NULL || vk_spec_info->mapEntryCount == 0)
      return NULL;

   struct nir_spirv_specialization *spec =
      vtn_alloc_specialization(vk_spec_info->mapEntryCount);
   if (!spec)
      return NULL;

   for (uint32_t i = 0; i < vk_spec_info->mapEntryCount; i++) {
      VkSpecializationMapEntry vk_entry = vk_spec_info->pMapEntries[i];
      const void *vk_data = (uint8_t *)vk_spec_info->pData + vk_entry.offset;

      assert((uint8_t *)vk_data + vk_entry.size <=
             (uint8_t *)vk_spec_info->pData + vk_spec_info->dataSize);

      if (!vtn_add_specialization_entry(spec, i,
                                        vk_spec_info->pMapEntries[i].constantID,
                                        vk_entry.size, vk_data, false))
         goto fail;
   }

   return spec;

fail:
   vtn_free_specialization(spec);
   return NULL;
}

enum mesa_prim
vk_topology_to_mesa(VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MESA_PRIM_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MESA_PRIM_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MESA_PRIM_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_IGNORED(-Wswitch)
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
PRAGMA_DIAGNOSTIC_POP
      return MESA_PRIM_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MESA_PRIM_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MESA_PRIM_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return MESA_PRIM_LINES_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return MESA_PRIM_LINE_STRIP_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return MESA_PRIM_TRIANGLES_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return MESA_PRIM_TRIANGLE_STRIP_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return MESA_PRIM_PATCHES;
   default:
      UNREACHABLE("invalid");
   }
}
