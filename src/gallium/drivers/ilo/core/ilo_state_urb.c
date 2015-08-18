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
#include "ilo_state_urb.h"

struct urb_configuration {
   uint8_t vs_pcb_alloc_kb;
   uint8_t hs_pcb_alloc_kb;
   uint8_t ds_pcb_alloc_kb;
   uint8_t gs_pcb_alloc_kb;
   uint8_t ps_pcb_alloc_kb;

   uint8_t urb_offset_8kb;

   uint8_t vs_urb_alloc_8kb;
   uint8_t hs_urb_alloc_8kb;
   uint8_t ds_urb_alloc_8kb;
   uint8_t gs_urb_alloc_8kb;

   uint8_t vs_entry_rows;
   uint8_t hs_entry_rows;
   uint8_t ds_entry_rows;
   uint8_t gs_entry_rows;

   int vs_entry_count;
   int hs_entry_count;
   int ds_entry_count;
   int gs_entry_count;
};

static void
urb_alloc_gen7_pcb(const struct ilo_dev *dev,
                   const struct ilo_state_urb_info *info,
                   struct urb_configuration *conf)
{
   /*
    * From the Haswell PRM, volume 2b, page 940:
    *
    *     "[0,16] (0KB - 16KB) Increments of 1KB DevHSW:GT1, DevHSW:GT2
    *      [0,32] (0KB - 32KB) Increments of 2KB DevHSW:GT3"
    */
   const uint8_t increment_kb =
      (ilo_dev_gen(dev) >= ILO_GEN(8) ||
       (ilo_dev_gen(dev) == ILO_GEN(7.5) && dev->gt == 3)) ? 2 : 1;

   ILO_DEV_ASSERT(dev, 7, 8);

   /*
    * Keep the strategy simple as we do not know the workloads and how
    * expensive it is to change the configuration frequently.
    */
   if (info->hs_const_data || info->ds_const_data) {
      conf->vs_pcb_alloc_kb = increment_kb * 4;
      conf->hs_pcb_alloc_kb = increment_kb * 3;
      conf->ds_pcb_alloc_kb = increment_kb * 3;
      conf->gs_pcb_alloc_kb = increment_kb * 3;
      conf->ps_pcb_alloc_kb = increment_kb * 3;
   } else if (info->gs_const_data) {
      conf->vs_pcb_alloc_kb = increment_kb * 6;
      conf->gs_pcb_alloc_kb = increment_kb * 5;
      conf->ps_pcb_alloc_kb = increment_kb * 5;
   } else {
      conf->vs_pcb_alloc_kb = increment_kb * 8;
      conf->ps_pcb_alloc_kb = increment_kb * 8;
   }

   conf->urb_offset_8kb = increment_kb * 16 / 8;
}

static void
urb_alloc_gen6_urb(const struct ilo_dev *dev,
                   const struct ilo_state_urb_info *info,
                   struct urb_configuration *conf)
{
   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 34:
    *
    *     "(VS URB Starting Address) Offset from the start of the URB memory
    *      where VS starts its allocation, specified in multiples of 8 KB."
    *
    * Same for other stages.
    */
   const int space_avail_8kb = dev->urb_size / 8192 - conf->urb_offset_8kb;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 173:
    *
    *     "Programming Note: If the GS stage is enabled, software must always
    *      allocate at least one GS URB Entry. This is true even if the GS
    *      thread never needs to output vertices to the urb, e.g., when only
    *      performing stream output. This is an artifact of the need to pass
    *      the GS thread an initial destination URB handle."
    */
   const bool force_gs_alloc =
      (ilo_dev_gen(dev) == ILO_GEN(6) && info->gs_enable);

   ILO_DEV_ASSERT(dev, 6, 8);

   if (info->hs_entry_size || info->ds_entry_size) {
      conf->vs_urb_alloc_8kb = space_avail_8kb / 4;
      conf->hs_urb_alloc_8kb = space_avail_8kb / 4;
      conf->ds_urb_alloc_8kb = space_avail_8kb / 4;
      conf->gs_urb_alloc_8kb = space_avail_8kb / 4;

      if (space_avail_8kb % 4) {
         assert(space_avail_8kb % 2 == 0);
         conf->vs_urb_alloc_8kb++;
         conf->gs_urb_alloc_8kb++;
      }
   } else if (info->gs_entry_size || force_gs_alloc) {
      assert(space_avail_8kb % 2 == 0);
      conf->vs_urb_alloc_8kb = space_avail_8kb / 2;
      conf->gs_urb_alloc_8kb = space_avail_8kb / 2;
   } else {
      conf->vs_urb_alloc_8kb = space_avail_8kb;
   }
}

static bool
urb_init_gen6_vs_entry(const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info,
                       struct urb_configuration *conf)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 28:
    *
    *     "(VS URB Entry Allocation Size)
    *      Range [0,4] = [1,5] 1024-bit URB rows"
    *
    *     "(VS Number of URB Entries)
    *      Range [24,256] in multiples of 4
    *            [24, 128] in multiples of 4[DevSNBGT1]"
    */
   const int max_entry_count = (dev->gt == 2) ? 256 : 252;
   const int row_size = 1024 / 8;
   int row_count, entry_count;
   int entry_size;

   ILO_DEV_ASSERT(dev, 6, 6);

   /* VE and VS share the same VUE for each vertex */
   entry_size = info->vs_entry_size;
   if (entry_size < info->ve_entry_size)
      entry_size = info->ve_entry_size;

   row_count = (entry_size + row_size - 1) / row_size;
   if (row_count > 5)
      return false;
   else if (!row_count)
      row_count++;

   entry_count = conf->vs_urb_alloc_8kb * 8192 / (row_size * row_count);
   if (entry_count > max_entry_count)
      entry_count = max_entry_count;
   entry_count &= ~3;
   assert(entry_count >= 24);

   conf->vs_entry_rows = row_count;
   conf->vs_entry_count = entry_count;

   return true;
}

static bool
urb_init_gen6_gs_entry(const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info,
                       struct urb_configuration *conf)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 29:
    *
    *     "(GS Number of URB Entries)
    *      Range [0,256] in multiples of 4
    *            [0, 254] in multiples of 4[DevSNBGT1]"
    *
    *     "(GS URB Entry Allocation Size)
    *      Range [0,4] = [1,5] 1024-bit URB rows"
    */
   const int max_entry_count = (dev->gt == 2) ? 256 : 252;
   const int row_size = 1024 / 8;
   int row_count, entry_count;

   ILO_DEV_ASSERT(dev, 6, 6);

   row_count = (info->gs_entry_size + row_size - 1) / row_size;
   if (row_count > 5)
      return false;
   else if (!row_count)
      row_count++;

   entry_count = conf->gs_urb_alloc_8kb * 8192 / (row_size * row_count);
   if (entry_count > max_entry_count)
      entry_count = max_entry_count;
   entry_count &= ~3;

   conf->gs_entry_rows = row_count;
   conf->gs_entry_count = entry_count;

   return true;
}

static bool
urb_init_gen7_vs_entry(const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info,
                       struct urb_configuration *conf)
{
   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 34-35:
    *
    *     "VS URB Entry Allocation Size equal to 4(5 512-bit URB rows) may
    *      cause performance to decrease due to banking in the URB. Element
    *      sizes of 16 to 20 should be programmed with six 512-bit URB rows."
    *
    *     "(VS URB Entry Allocation Size)
    *      Format: U9-1 count of 512-bit units"
    *
    *     "(VS Number of URB Entries)
    *      [32,704]
    *      [32,512]
    *
    *      Programming Restriction: VS Number of URB Entries must be divisible
    *      by 8 if the VS URB Entry Allocation Size is less than 9 512-bit URB
    *      entries."2:0" = reserved "000b""
    *
    * From the Haswell PRM, volume 2b, page 847:
    *
    *     "(VS Number of URB Entries)
    *      [64,1664] DevHSW:GT3
    *      [64,1664] DevHSW:GT2
    *      [32,640]  DevHSW:GT1"
    */
   const int row_size = 512 / 8;
   int row_count, entry_count;
   int entry_size;
   int max_entry_count, min_entry_count;

   ILO_DEV_ASSERT(dev, 7, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 35:
    *
    *     "Programming Restriction: As the VS URB entry serves as both the
    *      per-vertex input and output of the VS shader, the VS URB Allocation
    *      Size must be sized to the maximum of the vertex input and output
    *      structures."
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 42:
    *
    *     "If the VS function is enabled, the VF-written VUEs are not required
    *      to have Vertex Headers, as the VS-incoming vertices are guaranteed
    *      to be consumed by the VS (i.e., the VS thread is responsible for
    *      overwriting the input vertex data)."
    *
    * VE and VS share the same VUE for each vertex.
    */
   entry_size = info->vs_entry_size;
   if (entry_size < info->ve_entry_size)
      entry_size = info->ve_entry_size;

   row_count = (entry_size + row_size - 1) / row_size;
   if (row_count == 5 || !row_count)
      row_count++;

   entry_count = conf->vs_urb_alloc_8kb * 8192 / (row_size * row_count);
   if (row_count < 9)
      entry_count &= ~7;

   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
   case ILO_GEN(7.5):
      max_entry_count = (dev->gt >= 2) ? 1664 : 640;
      min_entry_count = (dev->gt >= 2) ? 64 : 32;
      break;
   case ILO_GEN(7):
      max_entry_count = (dev->gt == 2) ? 704 : 512;
      min_entry_count = 32;
      break;
   default:
      assert(!"unexpected gen");
      return false;
      break;
   }

   if (entry_count > max_entry_count)
      entry_count = max_entry_count;
   else if (entry_count < min_entry_count)
      return false;

   conf->vs_entry_rows = row_count;
   conf->vs_entry_count = entry_count;

   return true;
}

static bool
urb_init_gen7_hs_entry(const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info,
                       struct urb_configuration *conf)
{
   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 37:
    *
    *     "HS Number of URB Entries must be divisible by 8 if the HS URB Entry
    *      Allocation Size is less than 9 512-bit URB
    *      entries."2:0" = reserved "000"
    *
    *      [0,64]
    *      [0,32]"
    *
    * From the Haswell PRM, volume 2b, page 849:
    *
    *     "(HS Number of URB Entries)
    *      [0,128] DevHSW:GT2
    *      [0,64]  DevHSW:GT1"
    */
   const int row_size = 512 / 8;
   int row_count, entry_count;
   int max_entry_count;

   ILO_DEV_ASSERT(dev, 7, 8);

   row_count = (info->hs_entry_size + row_size - 1) / row_size;
   if (!row_count)
      row_count++;

   entry_count = conf->hs_urb_alloc_8kb * 8192 / (row_size * row_count);
   if (row_count < 9)
      entry_count &= ~7;

   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
   case ILO_GEN(7.5):
      max_entry_count = (dev->gt >= 2) ? 128 : 64;
      break;
   case ILO_GEN(7):
      max_entry_count = (dev->gt == 2) ? 64 : 32;
      break;
   default:
      assert(!"unexpected gen");
      return false;
      break;
   }

   if (entry_count > max_entry_count)
      entry_count = max_entry_count;
   else if (info->hs_entry_size && !entry_count)
      return false;

   conf->hs_entry_rows = row_count;
   conf->hs_entry_count = entry_count;

   return true;
}

static bool
urb_init_gen7_ds_entry(const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info,
                       struct urb_configuration *conf)
{
   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 38:
    *
    *     "(DS URB Entry Allocation Size)
    *      [0,9]"
    *
    *     "(DS Number of URB Entries) If Domain Shader Thread Dispatch is
    *      Enabled then the minimum number handles that must be allocated is
    *      138 URB entries.
    *      "2:0" = reserved "000"
    *
    *      [0,448]
    *      [0,288]
    *
    *      DS Number of URB Entries must be divisible by 8 if the DS URB Entry
    *      Allocation Size is less than 9 512-bit URB entries.If Domain Shader
    *      Thread Dispatch is Enabled then the minimum number of handles that
    *      must be allocated is 10 URB entries."
    *
    * From the Haswell PRM, volume 2b, page 851:
    *
    *     "(DS Number of URB Entries)
    *      [0,960] DevHSW:GT2
    *      [0,384] DevHSW:GT1"
    */
   const int row_size = 512 / 8;
   int row_count, entry_count;
   int max_entry_count;

   ILO_DEV_ASSERT(dev, 7, 8);

   row_count = (info->ds_entry_size + row_size - 1) / row_size;
   if (row_count > 10)
      return false;
   else if (!row_count)
      row_count++;

   entry_count = conf->ds_urb_alloc_8kb * 8192 / (row_size * row_count);
   if (row_count < 9)
      entry_count &= ~7;

   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
   case ILO_GEN(7.5):
      max_entry_count = (dev->gt >= 2) ? 960 : 384;
      break;
   case ILO_GEN(7):
      max_entry_count = (dev->gt == 2) ? 448 : 288;
      break;
   default:
      assert(!"unexpected gen");
      return false;
      break;
   }

   if (entry_count > max_entry_count)
      entry_count = max_entry_count;
   else if (info->ds_entry_size && entry_count < 10)
      return false;

   conf->ds_entry_rows = row_count;
   conf->ds_entry_count = entry_count;

   return true;
}

static bool
urb_init_gen7_gs_entry(const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info,
                       struct urb_configuration *conf)
{
   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 40:
    *
    *     "(GS Number of URB Entries) GS Number of URB Entries must be
    *      divisible by 8 if the GS URB Entry Allocation Size is less than 9
    *      512-bit URB entries.
    *      "2:0" = reserved "000"
    *
    *      [0,320]
    *      [0,192]"
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 171:
    *
    *     "(DUAL_INSTANCE and DUAL_OBJECT) The GS must be allocated at least
    *      two URB handles or behavior is UNDEFINED."
    *
    * From the Haswell PRM, volume 2b, page 853:
    *
    *     "(GS Number of URB Entries)
    *      [0,640] DevHSW:GT2
    *      [0,256] DevHSW:GT1
    *
    *      Only if GS is disabled can this field be programmed to 0.  If GS is
    *      enabled this field shall be programmed to a value greater than 0.
    *      For GS Dispatch Mode "Single", this field shall be programmed to a
    *      value greater than or equal to 1. For other GS Dispatch Modes,
    *      refer to the definition of Dispatch Mode (3DSTATE_GS) for minimum
    *      values of this field."
    */
   const int row_size = 512 / 8;
   int row_count, entry_count;
   int max_entry_count;

   ILO_DEV_ASSERT(dev, 7, 8);

   row_count = (info->gs_entry_size + row_size - 1) / row_size;
   if (!row_count)
      row_count++;

   entry_count = conf->gs_urb_alloc_8kb * 8192 / (row_size * row_count);
   if (row_count < 9)
      entry_count &= ~7;

   switch (ilo_dev_gen(dev)) {
   case ILO_GEN(8):
   case ILO_GEN(7.5):
      max_entry_count = (dev->gt >= 2) ? 640 : 256;
      break;
   case ILO_GEN(7):
      max_entry_count = (dev->gt == 2) ? 320 : 192;
      break;
   default:
      assert(!"unexpected gen");
      return false;
      break;
   }

   if (entry_count > max_entry_count)
      entry_count = max_entry_count;
   else if (info->gs_entry_size && entry_count < 2)
      return false;

   conf->gs_entry_rows = row_count;
   conf->gs_entry_count = entry_count;

   return true;
}

static bool
urb_get_gen6_configuration(const struct ilo_dev *dev,
                           const struct ilo_state_urb_info *info,
                           struct urb_configuration *conf)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   memset(conf, 0, sizeof(*conf));

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      urb_alloc_gen7_pcb(dev, info, conf);

   urb_alloc_gen6_urb(dev, info, conf);

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      if (!urb_init_gen7_vs_entry(dev, info, conf) ||
          !urb_init_gen7_hs_entry(dev, info, conf) ||
          !urb_init_gen7_ds_entry(dev, info, conf) ||
          !urb_init_gen7_gs_entry(dev, info, conf))
         return false;
   } else {
      if (!urb_init_gen6_vs_entry(dev, info, conf) ||
          !urb_init_gen6_gs_entry(dev, info, conf))
         return false;
   }

   return true;
}

static bool
urb_set_gen7_3dstate_push_constant_alloc(struct ilo_state_urb *urb,
                                         const struct ilo_dev *dev,
                                         const struct ilo_state_urb_info *info,
                                         const struct urb_configuration *conf)
{
   uint32_t dw1[5];
   uint8_t sizes_kb[5], offset_kb;
   int i;

   ILO_DEV_ASSERT(dev, 7, 8);

   sizes_kb[0] = conf->vs_pcb_alloc_kb;
   sizes_kb[1] = conf->hs_pcb_alloc_kb;
   sizes_kb[2] = conf->ds_pcb_alloc_kb;
   sizes_kb[3] = conf->gs_pcb_alloc_kb;
   sizes_kb[4] = conf->ps_pcb_alloc_kb;
   offset_kb = 0;

   for (i = 0; i < 5; i++) {
      /* careful for the valid range of offsets */
      if (sizes_kb[i]) {
         dw1[i] = offset_kb << GEN7_PCB_ALLOC_DW1_OFFSET__SHIFT |
                  sizes_kb[i] << GEN7_PCB_ALLOC_DW1_SIZE__SHIFT;
         offset_kb += sizes_kb[i];
      } else {
         dw1[i] = 0;
      }
   }

   STATIC_ASSERT(ARRAY_SIZE(urb->pcb) >= 5);
   memcpy(urb->pcb, dw1, sizeof(dw1));

   return true;
}

static bool
urb_set_gen6_3DSTATE_URB(struct ilo_state_urb *urb,
                         const struct ilo_dev *dev,
                         const struct ilo_state_urb_info *info,
                         const struct urb_configuration *conf)
{
   uint32_t dw1, dw2;

   ILO_DEV_ASSERT(dev, 6, 6);

   assert(conf->vs_entry_rows && conf->gs_entry_rows);

   dw1 = (conf->vs_entry_rows - 1) << GEN6_URB_DW1_VS_ENTRY_SIZE__SHIFT |
         conf->vs_entry_count << GEN6_URB_DW1_VS_ENTRY_COUNT__SHIFT;
   dw2 = conf->gs_entry_count << GEN6_URB_DW2_GS_ENTRY_COUNT__SHIFT |
         (conf->gs_entry_rows - 1) << GEN6_URB_DW2_GS_ENTRY_SIZE__SHIFT;

   STATIC_ASSERT(ARRAY_SIZE(urb->urb) >= 2);
   urb->urb[0] = dw1;
   urb->urb[1] = dw2;

   return true;
}

static bool
urb_set_gen7_3dstate_urb(struct ilo_state_urb *urb,
                         const struct ilo_dev *dev,
                         const struct ilo_state_urb_info *info,
                         const struct urb_configuration *conf)
{
   uint32_t dw1[4];
   struct {
      uint8_t alloc_8kb;
      uint8_t entry_rows;
      int entry_count;
   } stages[4];
   uint8_t offset_8kb;
   int i;

   ILO_DEV_ASSERT(dev, 7, 8);

   stages[0].alloc_8kb = conf->vs_urb_alloc_8kb;
   stages[1].alloc_8kb = conf->hs_urb_alloc_8kb;
   stages[2].alloc_8kb = conf->ds_urb_alloc_8kb;
   stages[3].alloc_8kb = conf->gs_urb_alloc_8kb;

   stages[0].entry_rows = conf->vs_entry_rows;
   stages[1].entry_rows = conf->hs_entry_rows;
   stages[2].entry_rows = conf->ds_entry_rows;
   stages[3].entry_rows = conf->gs_entry_rows;

   stages[0].entry_count = conf->vs_entry_count;
   stages[1].entry_count = conf->hs_entry_count;
   stages[2].entry_count = conf->ds_entry_count;
   stages[3].entry_count = conf->gs_entry_count;

   offset_8kb = conf->urb_offset_8kb;

   for (i = 0; i < 4; i++) {
      /* careful for the valid range of offsets */
      if (stages[i].alloc_8kb) {
         assert(stages[i].entry_rows);
         dw1[i] =
            offset_8kb << GEN7_URB_DW1_OFFSET__SHIFT |
            (stages[i].entry_rows - 1) << GEN7_URB_DW1_ENTRY_SIZE__SHIFT |
            stages[i].entry_count << GEN7_URB_DW1_ENTRY_COUNT__SHIFT;
         offset_8kb += stages[i].alloc_8kb;
      } else {
         dw1[i] = 0;
      }
   }

   STATIC_ASSERT(ARRAY_SIZE(urb->urb) >= 4);
   memcpy(urb->urb, dw1, sizeof(dw1));

   return true;
}

bool
ilo_state_urb_init(struct ilo_state_urb *urb,
                   const struct ilo_dev *dev,
                   const struct ilo_state_urb_info *info)
{
   assert(ilo_is_zeroed(urb, sizeof(*urb)));
   return ilo_state_urb_set_info(urb, dev, info);
}

bool
ilo_state_urb_init_for_rectlist(struct ilo_state_urb *urb,
                                const struct ilo_dev *dev,
                                uint8_t vf_attr_count)
{
   struct ilo_state_urb_info info;

   memset(&info, 0, sizeof(info));
   info.ve_entry_size = sizeof(uint32_t) * 4 * vf_attr_count;

   return ilo_state_urb_init(urb, dev, &info);
}

bool
ilo_state_urb_set_info(struct ilo_state_urb *urb,
                       const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info)
{
   struct urb_configuration conf;
   bool ret = true;

   ret &= urb_get_gen6_configuration(dev, info, &conf);
   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      ret &= urb_set_gen7_3dstate_push_constant_alloc(urb, dev, info, &conf);
      ret &= urb_set_gen7_3dstate_urb(urb, dev, info, &conf);
   } else {
      ret &= urb_set_gen6_3DSTATE_URB(urb, dev, info, &conf);
   }

   assert(ret);

   return ret;
}

void
ilo_state_urb_full_delta(const struct ilo_state_urb *urb,
                         const struct ilo_dev *dev,
                         struct ilo_state_urb_delta *delta)
{
   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      delta->dirty = ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_VS |
                     ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_HS |
                     ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_DS |
                     ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_GS |
                     ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_PS |
                     ILO_STATE_URB_3DSTATE_URB_VS |
                     ILO_STATE_URB_3DSTATE_URB_HS |
                     ILO_STATE_URB_3DSTATE_URB_DS |
                     ILO_STATE_URB_3DSTATE_URB_GS;
   } else {
      delta->dirty = ILO_STATE_URB_3DSTATE_URB_VS |
                     ILO_STATE_URB_3DSTATE_URB_GS;
   }
}

void
ilo_state_urb_get_delta(const struct ilo_state_urb *urb,
                        const struct ilo_dev *dev,
                        const struct ilo_state_urb *old,
                        struct ilo_state_urb_delta *delta)
{
   delta->dirty = 0;

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      if (memcmp(urb->pcb, old->pcb, sizeof(urb->pcb))) {
         delta->dirty |= ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_VS |
                         ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_HS |
                         ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_DS |
                         ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_GS |
                         ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_PS;
      }

      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 34:
       *
       *     "3DSTATE_URB_HS, 3DSTATE_URB_DS, and 3DSTATE_URB_GS must also be
       *      programmed in order for the programming of this state
       *      (3DSTATE_URB_VS) to be valid."
       *
       * The same is true for the other three states.
       */
      if (memcmp(urb->urb, old->urb, sizeof(urb->urb))) {
         delta->dirty |= ILO_STATE_URB_3DSTATE_URB_VS |
                         ILO_STATE_URB_3DSTATE_URB_HS |
                         ILO_STATE_URB_3DSTATE_URB_DS |
                         ILO_STATE_URB_3DSTATE_URB_GS;
      }
   } else {
      if (memcmp(urb->urb, old->urb, sizeof(uint32_t) * 2)) {
         delta->dirty |= ILO_STATE_URB_3DSTATE_URB_VS |
                         ILO_STATE_URB_3DSTATE_URB_GS;
      }
   }
}
