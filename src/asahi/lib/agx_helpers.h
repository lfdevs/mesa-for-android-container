/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef __AGX_HELPERS_H
#define __AGX_HELPERS_H

#include <stdbool.h>
#include "agx_pack.h"

static inline enum agx_sampler_states
agx_translate_sampler_state_count(unsigned count, bool extended)
{
   assert(count <= 16 && "max 16 sampler state registers supported");

   if (count == 0) {
      return AGX_SAMPLER_STATES_0;
   } else if (extended) {
      if (count <= 8)
         return AGX_SAMPLER_STATES_8_EXTENDED;
      else
         return AGX_SAMPLER_STATES_16_EXTENDED;
   } else {
      if (count <= 4)
         return AGX_SAMPLER_STATES_4_COMPACT;
      else if (count <= 8)
         return AGX_SAMPLER_STATES_8_COMPACT;
      else if (count <= 12)
         return AGX_SAMPLER_STATES_12_COMPACT;
      else
         return AGX_SAMPLER_STATES_16_COMPACT;
   }
}

#endif
