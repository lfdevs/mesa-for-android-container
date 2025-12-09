/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_texture.h"

/* Utils*/
uint64_t
mtl_texture_get_gpu_resource_id(mtl_texture *texture)
{
   return 0u;
}

/* Texture view creation */
mtl_texture *
mtl_new_texture_view_with(mtl_texture *texture,
                          const struct kk_view_layout *layout)
{
   return NULL;
}

mtl_texture *
mtl_new_texture_view_with_no_swizzle(mtl_texture *texture,
                                     const struct kk_view_layout *layout)
{
   return NULL;
}
