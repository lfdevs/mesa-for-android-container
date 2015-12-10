/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2015 LunarG, Inc.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "ilo_debug.h"
#include "ilo_state_compute.h"

struct compute_urb_configuration {
   int idrt_entry_count;
   int curbe_entry_count;

   int urb_entry_count;
   /* in 256-bit register increments */
   int urb_entry_size;
};

static int
get_gen6_rob_entry_count(const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 60:
    *
    *     "ROB has 64KB of storage; 2048 entries."
    *
    * From the valid ranges of "CURBE Allocation Size", we can also conclude
    * that interface entries and CURBE data must be in ROB.  And that ROB
    * should be 16KB, or 512 entries, on Gen7 GT1.
    */
   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      return 2048;
   else if (ilo_dev_gen(dev) >= ILO_GEN(7))
      return (dev->gt == 2) ? 2048 : 512;
   else
      return (dev->gt == 2) ? 2048 : 1024;
}

static int
get_gen6_idrt_entry_count(const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 21:
    *
    *     "The first 32 URB entries are reserved for the interface
    *      descriptor..."
    *
    * From the Haswell PRM, volume 7, page 836:
    *
    *     "The first 64 URB entries are reserved for the interface
    *      description..."
    */
   return (ilo_dev_gen(dev) >= ILO_GEN(7.5)) ? 64 : 32;
}

static int
get_gen6_curbe_entry_count(const struct ilo_dev *dev, uint32_t curbe_size)
{
   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 21:
    *
    *     "(CURBE Allocation Size) Specifies the total length allocated for
    *      CURBE, in 256-bit register increments.
    */
   const int entry_count = (curbe_size + 31) / 32;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(get_gen6_idrt_entry_count(dev) + entry_count <=
         get_gen6_rob_entry_count(dev));

   return entry_count;
}

static bool
compute_get_gen6_urb_configuration(const struct ilo_dev *dev,
                                   const struct ilo_state_compute_info *info,
                                   struct compute_urb_configuration *urb)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   urb->idrt_entry_count = get_gen6_idrt_entry_count(dev);
   urb->curbe_entry_count =
      get_gen6_curbe_entry_count(dev, info->curbe_alloc_size);

   /*
    * From the Broadwell PRM, volume 2b, page 451:
    *
    *     "Please note that 0 is not allowed for this field (Number of URB
    *      Entries)."
    */
   urb->urb_entry_count = (ilo_dev_gen(dev) >= ILO_GEN(8)) ? 1 : 0;

   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 52:
    *
    *     "(URB Entry Allocation Size) Specifies the length of each URB entry
    *      used by the unit, in 256-bit register increments - 1."
    */
   urb->urb_entry_size = 1;

   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 22:
    *
    *      MEDIA_VFE_STATE specifies the amount of CURBE space, the URB handle
    *      size and the number of URB handles. The driver must ensure that
    *      ((URB_handle_size * URB_num_handle) - CURBE - 32) <=
    *      URB_allocation_in_L3."
    */
   assert(urb->idrt_entry_count + urb->curbe_entry_count +
         urb->urb_entry_count * urb->urb_entry_size <=
         info->cv_urb_alloc_size / 32);

   return true;
}

static int
compute_interface_get_gen6_read_end(const struct ilo_dev *dev,
                                    const struct ilo_state_compute_interface_info *interface)
{
   const int per_thread_read = (interface->curbe_read_length + 31) / 32;
   const int cross_thread_read =
      (interface->cross_thread_curbe_read_length + 31) / 32;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(interface->curbe_read_offset % 32 == 0);

   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 60:
    *
    *     "(Constant URB Entry Read Length) [0,63]"
    */
   assert(per_thread_read <= 63);

   /*
    * From the Haswell PRM, volume 2d, page 199:
    *
    *     "(Cross-Thread Constant Data Read Length) [0,127]"
    */
   if (ilo_dev_gen(dev) >= ILO_GEN(7.5))
      assert(cross_thread_read <= 127);
   else
      assert(!cross_thread_read);

   if (per_thread_read || cross_thread_read) {
      return interface->curbe_read_offset / 32 + cross_thread_read +
         per_thread_read * interface->thread_group_size;
   } else {
      return 0;
   }
}

static bool
compute_validate_gen6(const struct ilo_dev *dev,
                      const struct ilo_state_compute_info *info,
                      const struct compute_urb_configuration *urb)
{
   int min_curbe_entry_count;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(info->interface_count <= urb->idrt_entry_count);

   min_curbe_entry_count = 0;
   for (i = 0; i < info->interface_count; i++) {
      const int read_end =
         compute_interface_get_gen6_read_end(dev, &info->interfaces[i]);

      if (min_curbe_entry_count < read_end)
         min_curbe_entry_count = read_end;
   }

   assert(min_curbe_entry_count <= urb->curbe_entry_count);

   /*
    * From the Broadwell PRM, volume 2b, page 452:
    *
    *     "CURBE Allocation Size should be 0 for GPGPU workloads that uses
    *      indirect instead of CURBE."
    */
   if (!min_curbe_entry_count)
      assert(!urb->curbe_entry_count);

   return true;
}

static uint32_t
compute_get_gen6_per_thread_scratch_size(const struct ilo_dev *dev,
                                         const struct ilo_state_compute_info *info,
                                         uint8_t *per_thread_space)
{
   ILO_DEV_ASSERT(dev, 6, 7);

   /*
    * From the Sandy Bridge PRM, volume 2 part 2, page 30:
    *
    *     "(Per Thread Scratch Space)
    *      Range = [0,11] indicating [1k bytes, 12k bytes] [DevSNB]"
    */
   assert(info->per_thread_scratch_size <= 12 * 1024);

   if (!info->per_thread_scratch_size) {
      *per_thread_space = 0;
      return 0;
   }

   *per_thread_space = (info->per_thread_scratch_size > 1024) ?
      (info->per_thread_scratch_size - 1) / 1024 : 0;

   return 1024 * (1 + *per_thread_space);
}

static uint32_t
compute_get_gen75_per_thread_scratch_size(const struct ilo_dev *dev,
                                          const struct ilo_state_compute_info *info,
                                          uint8_t *per_thread_space)
{
   ILO_DEV_ASSERT(dev, 7.5, 8);

   /*
    * From the Haswell PRM, volume 2b, page 407:
    *
    *     "(Per Thread Scratch Space)
    *      [0,10]  Indicating [2k bytes, 2 Mbytes]"
    *
    *     "Note: The scratch space should be declared as 2x the desired
    *      scratch space. The stack will start at the half-way point instead
    *      of the end. The upper half of scratch space will not be accessed
    *      and so does not have to be allocated in memory."
    *
    * From the Broadwell PRM, volume 2a, page 450:
    *
    *     "(Per Thread Scratch Space)
    *      [0,11]  indicating [1k bytes, 2 Mbytes]"
    */
   assert(info->per_thread_scratch_size <=
         ((ilo_dev_gen(dev) >= ILO_GEN(8)) ? 2 : 1) * 1024 * 1024);

   if (!info->per_thread_scratch_size) {
      *per_thread_space = 0;
      return 0;
   }

   /* next power of two, starting from 1KB */
   *per_thread_space = (info->per_thread_scratch_size > 1024) ?
      (util_last_bit(info->per_thread_scratch_size - 1) - 10) : 0;

   return 1 << (10 + *per_thread_space);
}

static bool
compute_set_gen6_MEDIA_VFE_STATE(struct ilo_state_compute *compute,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_compute_info *info)
{
   struct compute_urb_configuration urb;
   uint32_t per_thread_size;
   uint8_t per_thread_space;

   uint32_t dw1, dw2, dw4;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!compute_get_gen6_urb_configuration(dev, info, &urb) ||
       !compute_validate_gen6(dev, info, &urb))
      return false;

   if (ilo_dev_gen(dev) >= ILO_GEN(7.5)) {
      per_thread_size = compute_get_gen75_per_thread_scratch_size(dev,
            info, &per_thread_space);
   } else {
      per_thread_size = compute_get_gen6_per_thread_scratch_size(dev,
            info, &per_thread_space);
   }

   dw1 = per_thread_space << GEN6_VFE_DW1_SCRATCH_SPACE_PER_THREAD__SHIFT;

   dw2 = (dev->thread_count - 1) << GEN6_VFE_DW2_MAX_THREADS__SHIFT |
         urb.urb_entry_count << GEN6_VFE_DW2_URB_ENTRY_COUNT__SHIFT |
         GEN6_VFE_DW2_RESET_GATEWAY_TIMER |
         GEN6_VFE_DW2_BYPASS_GATEWAY_CONTROL;

   if (ilo_dev_gen(dev) >= ILO_GEN(7) && ilo_dev_gen(dev) <= ILO_GEN(7.5))
      dw2 |= GEN7_VFE_DW2_GPGPU_MODE;

   assert(urb.urb_entry_size);

   dw4 = (urb.urb_entry_size - 1) << GEN6_VFE_DW4_URB_ENTRY_SIZE__SHIFT |
         urb.curbe_entry_count << GEN6_VFE_DW4_CURBE_SIZE__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(compute->vfe) >= 3);
   compute->vfe[0] = dw1;
   compute->vfe[1] = dw2;
   compute->vfe[2] = dw4;

   compute->scratch_size = per_thread_size * dev->thread_count;

   return true;
}

static uint8_t
compute_interface_get_gen6_sampler_count(const struct ilo_dev *dev,
                                         const struct ilo_state_compute_interface_info *interface)
{
   ILO_DEV_ASSERT(dev, 6, 8);
   return (interface->sampler_count <= 12) ?
      (interface->sampler_count + 3) / 4 : 4;
}

static uint8_t
compute_interface_get_gen6_surface_count(const struct ilo_dev *dev,
                                         const struct ilo_state_compute_interface_info *interface)
{
   ILO_DEV_ASSERT(dev, 6, 8);
   return (interface->surface_count <= 31) ? interface->surface_count : 31;
}

static uint8_t
compute_interface_get_gen7_slm_size(const struct ilo_dev *dev,
                                    const struct ilo_state_compute_interface_info *interface)
{
   ILO_DEV_ASSERT(dev, 7, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 2, page 61:
    *
    *     "The amount is specified in 4k blocks, but only powers of 2 are
    *      allowed: 0, 4k, 8k, 16k, 32k and 64k per half-slice."
    */
   assert(interface->slm_size <= 64 * 1024);

   return util_next_power_of_two((interface->slm_size + 4095) / 4096);
}

static bool
compute_set_gen6_INTERFACE_DESCRIPTOR_DATA(struct ilo_state_compute *compute,
                                           const struct ilo_dev *dev,
                                           const struct ilo_state_compute_info *info)
{
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   for (i = 0; i < info->interface_count; i++) {
      const struct ilo_state_compute_interface_info *interface =
         &info->interfaces[i];
      uint16_t read_offset, per_thread_read_len, cross_thread_read_len;
      uint8_t sampler_count, surface_count;
      uint32_t dw0, dw2, dw3, dw4, dw5, dw6;

      assert(interface->kernel_offset % 64 == 0);
      assert(interface->thread_group_size);

      read_offset = interface->curbe_read_offset / 32;
      per_thread_read_len = (interface->curbe_read_length + 31) / 32;
      cross_thread_read_len =
         (interface->cross_thread_curbe_read_length + 31) / 32;

      sampler_count =
         compute_interface_get_gen6_sampler_count(dev, interface);
      surface_count =
         compute_interface_get_gen6_surface_count(dev, interface);

      dw0 = interface->kernel_offset;
      dw2 = sampler_count << GEN6_IDRT_DW2_SAMPLER_COUNT__SHIFT;
      dw3 = surface_count << GEN6_IDRT_DW3_BINDING_TABLE_SIZE__SHIFT;
      dw4 = per_thread_read_len << GEN6_IDRT_DW4_CURBE_READ_LEN__SHIFT |
            read_offset << GEN6_IDRT_DW4_CURBE_READ_OFFSET__SHIFT;

      dw5 = 0;
      dw6 = 0;
      if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
         const uint8_t slm_size =
            compute_interface_get_gen7_slm_size(dev, interface);

         dw5 |= GEN7_IDRT_DW5_ROUNDING_MODE_RTNE;

         if (slm_size) {
            dw5 |= GEN7_IDRT_DW5_BARRIER_ENABLE |
                   slm_size << GEN7_IDRT_DW5_SLM_SIZE__SHIFT;
         }

         /*
          * From the Haswell PRM, volume 2d, page 199:
          *
          *     "(Number of Threads in GPGPU Thread Group) Specifies the
          *      number of threads that are in this thread group.  Used to
          *      program the barrier for the number of messages to expect. The
          *      minimum value is 0 (which will disable the barrier), while
          *      the maximum value is the number of threads in a subslice for
          *      local barriers."
          *
          * From the Broadwell PRM, volume 2d, page 183:
          *
          *     "(Number of Threads in GPGPU Thread Group) Specifies the
          *      number of threads that are in this thread group.  The minimum
          *      value is 1, while the maximum value is the number of threads
          *      in a subslice for local barriers. See vol1b Configurations
          *      for the number of threads per subslice for different
          *      products.  The maximum value for global barriers is limited
          *      by the number of threads in the system, or by 511, whichever
          *      is lower. This field should not be set to 0 even if the
          *      barrier is disabled, since an accurate value is needed for
          *      proper pre-emption."
          */
         if (slm_size || ilo_dev_gen(dev) >= ILO_GEN(8)) {
            dw5 |= interface->thread_group_size <<
               GEN7_IDRT_DW5_THREAD_GROUP_SIZE__SHIFT;
         }

         if (ilo_dev_gen(dev) >= ILO_GEN(7.5)) {
            dw6 |= cross_thread_read_len <<
               GEN75_IDRT_DW6_CROSS_THREAD_CURBE_READ_LEN__SHIFT;
         }
      }

      STATIC_ASSERT(ARRAY_SIZE(compute->idrt[i]) >= 6);
      compute->idrt[i][0] = dw0;
      compute->idrt[i][1] = dw2;
      compute->idrt[i][2] = dw3;
      compute->idrt[i][3] = dw4;
      compute->idrt[i][4] = dw5;
      compute->idrt[i][5] = dw6;
   }

   return true;
}

bool
ilo_state_compute_init(struct ilo_state_compute *compute,
                       const struct ilo_dev *dev,
                       const struct ilo_state_compute_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(compute, sizeof(*compute)));
   assert(ilo_is_zeroed(info->data, info->data_size));

   assert(ilo_state_compute_data_size(dev, info->interface_count) <=
         info->data_size);
   compute->idrt = (uint32_t (*)[6]) info->data;

   ret &= compute_set_gen6_MEDIA_VFE_STATE(compute, dev, info);
   ret &= compute_set_gen6_INTERFACE_DESCRIPTOR_DATA(compute, dev, info);

   assert(ret);

   return ret;
}
