/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */
#include "nv_fatbin.h"

#include "nv_fatbin_defs.h"

#include "util/macros.h"

bool
nv_fatbin_init(struct nv_fatbin *fatbin, const void *data, size_t size)
{
   if (size < sizeof(struct nv_fatbin_header_t))
      return false;

   const struct nv_fatbin_header_t *header = data;
   if (header->magic != NV_FATBIN_HEADER_MAGIC)
      return false;

   fatbin->header = data;
   fatbin->data = ((const uint8_t *)data) + header->header_size;
   fatbin->size = MIN2(header->size, size - sizeof(struct nv_fatbin_header_t));
   return true;
}

bool
nv_fatbin_get_bytecode(struct nv_fatbin *fatbin, uint32_t sm, bool backwards_compat,
                       const void **out_data, size_t *out_size)
{
   uint32_t sm_found = 0;
   const uint8_t *ptr = fatbin->data;
   const uint8_t *end = fatbin->data + fatbin->size;
   while (ptr < end) {
      const struct nv_fatbin_object_header_t *object =
         (const struct nv_fatbin_object_header_t *)ptr;
      ptr += object->header_size;

      /* pick bytecode for our gpu */
      if (object->kind == NV_FATBIN_OBJECT_KIND_BYTECODE &&
          nv_is_sm_compatible(sm, object->sm, backwards_compat)) {
         if (object->sm > sm_found) {
            sm_found = object->sm;
            *out_data = ptr;
            *out_size = object->size;
         }
      }
      ptr += object->size;
   }

   if (sm_found == 0)
      fprintf(stderr, "Could not find bytecode for GPU in fatbin\n");
   return sm_found != 0;
}

bool
nv_is_sm_compatible(uint32_t device_sm, uint32_t requested_sm, bool backwards_compat)
{
   if (backwards_compat) {
      /* Binaries of the same SM major version are compatible if the minor version
      * is lower or equal */
      return (device_sm / 10) == (requested_sm / 10) &&
            device_sm >= requested_sm;
   } else {
      return device_sm == requested_sm;
   }
}
