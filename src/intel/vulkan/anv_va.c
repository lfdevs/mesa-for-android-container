/*
 * Copyright © 2023 Intel Corporation
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

#include "anv_private.h"

#include "util/u_math.h"

static uint64_t
va_add(struct anv_va_range *range, uint64_t addr, uint64_t size)
{
   range->addr = addr;
   range->size = size;

   return addr + size;
}

static void
va_at(struct anv_va_range *range, uint64_t addr, uint64_t size)
{
   range->addr = addr;
   range->size = size;
}

static void
anv_device_print_vas(struct anv_physical_device *device)
{
   fprintf(stderr, "Driver heaps:\n");
#define PRINT_HEAP(name) \
   fprintf(stderr, "   0x%016"PRIx64"-0x%016"PRIx64": %s\n", \
           device->va.name.addr, \
           device->va.name.addr + device->va.name.size, \
           #name);
   PRINT_HEAP(general_state_pool);
   PRINT_HEAP(low_heap);
   PRINT_HEAP(dynamic_state_pool);
   PRINT_HEAP(binding_table_pool);
   PRINT_HEAP(internal_surface_state_pool);
   PRINT_HEAP(scratch_surface_state_pool);
   PRINT_HEAP(bindless_surface_state_pool);
   PRINT_HEAP(descriptor_pool);
   PRINT_HEAP(push_descriptor_pool);
   PRINT_HEAP(instruction_state_pool);
   PRINT_HEAP(client_visible_heap);
   PRINT_HEAP(high_heap);
}

void
anv_physical_device_init_va_ranges(struct anv_physical_device *device)
{
   /* anv Virtual Memory Layout
    * =========================
    *
    * When the anv driver is determining the virtual graphics addresses of
    * memory objects itself using the softpin mechanism, the following memory
    * ranges will be used.
    *
    * Three special considerations to notice:
    *
    * (1) the dynamic state pool is located within the same 4 GiB as the low
    * heap. This is to work around a VF cache issue described in a comment in
    * anv_physical_device_init_heaps.
    *
    * (2) the binding table pool is located at lower addresses than the BT
    * (binding table) surface state pool, within a 4 GiB range which also
    * contains the bindless surface state pool. This allows surface state base
    * addresses to cover both binding tables (16 bit offsets), the internal
    * surface states (32 bit offsets) and the bindless surface states.
    *
    * (3) the last 4 GiB of the address space is withheld from the high heap.
    * Various hardware units will read past the end of an object for various
    * reasons. This healthy margin prevents reads from wrapping around 48-bit
    * addresses.
    */
   uint64_t _1Mb = 1ull * 1024 * 1024;
   uint64_t _1Gb = 1ull * 1024 * 1024 * 1024;
   uint64_t _4Gb = 4ull * 1024 * 1024 * 1024;

   uint64_t address = 0x000000200000ULL; /* 2MiB */

   address = va_add(&device->va.general_state_pool, address,
                    _1Gb - address);

   address = va_add(&device->va.low_heap, address, _1Gb);
   /* The STATE_BASE_ADDRESS can only express up to 4Gb - 4Kb */
   address = va_add(&device->va.dynamic_state_pool, address, 4 * _1Gb - 4096);
   address = align64(address, _1Gb);

   /* The following addresses have to be located in a 4Gb range so that the
    * binding tables can address internal surface states & bindless surface
    * states.
    */
   address = align64(address, _4Gb);
   address = va_add(&device->va.binding_table_pool, address, _1Gb);
   address = va_add(&device->va.internal_surface_state_pool, address, 1 * _1Gb);
   /* Scratch surface state overlaps with the internal surface state */
   va_at(&device->va.scratch_surface_state_pool,
         device->va.internal_surface_state_pool.addr,
         8 * _1Mb);

   /* Both of the following heaps have be in the same 4Gb range from the
    * binding table pool start so they can be addressed from binding table
    * entries.
    */
   if (device->indirect_descriptors) {
      /* With indirect descriptors, we allocate bindless surface states from
       * this pool.
       */
      address = va_add(&device->va.bindless_surface_state_pool, address, 2 * _1Gb);

      /* Descriptor buffers can go anywhere */
      address = align64(address, _4Gb);
      address = va_add(&device->va.descriptor_pool, address, 3 * _1Gb);
      address = va_add(&device->va.push_descriptor_pool, address, _1Gb);
   } else {
      /* With direct descriptor, descriptors set buffers are allocated
       * here.
       */
      address = va_add(&device->va.descriptor_pool, address, 2 * _1Gb);
   }

   /* We use a trick to compute constant data offsets in the shaders to avoid
    * unnecessary 64bit address computations (see lower_load_constant() in
    * anv_nir_apply_pipeline_layout.c). This assumes the instruction pool is
    * located at an address with the lower 32bits at 0.
    */
   address = align64(address, _4Gb);
   address = va_add(&device->va.instruction_state_pool, address, 2 * _1Gb);

   /* Whatever we have left we split in 2 for app allocations client-visible &
    * non-client-visible.
    *
    * Leave the last 4GiB out of the high vma range, so that no state
    * base address + size can overflow 48 bits. For more information see
    * the comment about Wa32bitGeneralStateOffset in anv_allocator.c
    */
   uint64_t user_heaps_size = device->gtt_size - address - 4 * _1Gb;
   uint64_t heaps_size_Gb = user_heaps_size / _1Gb / 2 ;

   address = va_add(&device->va.client_visible_heap, address, heaps_size_Gb * _1Gb);
   address = va_add(&device->va.high_heap, address, heaps_size_Gb * _1Gb);

   if (INTEL_DEBUG(DEBUG_HEAPS))
      anv_device_print_vas(device);
}
