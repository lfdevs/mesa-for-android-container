/*
 * Copyright © 2010, 2022 Intel Corporation
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

/**
 * @file
 */

#include "brw_eu.h"
#include "brw_fs.h"
#include "brw_fs_builder.h"

using namespace brw;

static void
lower_urb_read_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const bool per_slot_present =
      inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS].file != BAD_FILE;

   assert(inst->size_written % REG_SIZE == 0);
   assert(inst->header_size == 0);

   brw_reg payload_sources[2];
   unsigned header_size = 0;
   payload_sources[header_size++] = inst->src[URB_LOGICAL_SRC_HANDLE];
   if (per_slot_present)
      payload_sources[header_size++] = inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];

   brw_reg payload = brw_vgrf(bld.shader->alloc.allocate(header_size),
                             BRW_TYPE_F);
   bld.LOAD_PAYLOAD(payload, payload_sources, header_size, header_size);

   inst->opcode = SHADER_OPCODE_SEND;
   inst->header_size = header_size;

   inst->sfid = BRW_SFID_URB;
   inst->desc = brw_urb_desc(devinfo,
                             GFX8_URB_OPCODE_SIMD8_READ,
                             per_slot_present,
                             false,
                             inst->offset);

   inst->mlen = header_size;
   inst->ex_desc = 0;
   inst->ex_mlen = 0;
   inst->send_is_volatile = true;

   inst->resize_sources(4);

   inst->src[0] = brw_imm_ud(0); /* desc */
   inst->src[1] = brw_imm_ud(0); /* ex_desc */
   inst->src[2] = payload;
   inst->src[3] = brw_null_reg();
}

static void
lower_urb_read_logical_send_xe2(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->has_lsc);

   assert(inst->size_written % (REG_SIZE * reg_unit(devinfo)) == 0);
   assert(inst->header_size == 0);

   /* Get the logical send arguments. */
   const brw_reg handle = inst->src[URB_LOGICAL_SRC_HANDLE];

   /* Calculate the total number of components of the payload. */
   const unsigned dst_comps = inst->size_written / (REG_SIZE * reg_unit(devinfo));

   brw_reg payload = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(payload, handle);

   /* The low 24-bits of the URB handle is a byte offset into the URB area.
    * Add the (OWord) offset of the write to this value.
    */
   if (inst->offset) {
      bld.ADD(payload, payload, brw_imm_ud(inst->offset * 16));
      inst->offset = 0;
   }

   brw_reg offsets = inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];
   if (offsets.file != BAD_FILE) {
      bld.ADD(payload, payload, offsets);
   }

   inst->sfid = BRW_SFID_URB;

   assert((dst_comps >= 1 && dst_comps <= 4) || dst_comps == 8);

   inst->desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                             LSC_ADDR_SURFTYPE_FLAT, LSC_ADDR_SIZE_A32,
                             LSC_DATA_SIZE_D32, dst_comps /* num_channels */,
                             false /* transpose */,
                             LSC_CACHE(devinfo, LOAD, L1UC_L3UC));

   /* Update the original instruction. */
   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, inst->exec_size);
   inst->ex_mlen = 0;
   inst->header_size = 0;
   inst->send_has_side_effects = true;
   inst->send_is_volatile = false;

   inst->resize_sources(4);

   inst->src[0] = brw_imm_ud(0);
   inst->src[1] = brw_imm_ud(0);

   inst->src[2] = payload;
   inst->src[3] = brw_null_reg();
}

static void
lower_urb_write_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const bool per_slot_present =
      inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS].file != BAD_FILE;
   const bool channel_mask_present =
      inst->src[URB_LOGICAL_SRC_CHANNEL_MASK].file != BAD_FILE;

   assert(inst->header_size == 0);

   const unsigned length = 1 + per_slot_present + channel_mask_present +
                           inst->components_read(URB_LOGICAL_SRC_DATA);

   brw_reg *payload_sources = new brw_reg[length];
   brw_reg payload = brw_vgrf(bld.shader->alloc.allocate(length),
                             BRW_TYPE_F);

   unsigned header_size = 0;
   payload_sources[header_size++] = inst->src[URB_LOGICAL_SRC_HANDLE];
   if (per_slot_present)
      payload_sources[header_size++] = inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];

   if (channel_mask_present)
      payload_sources[header_size++] = inst->src[URB_LOGICAL_SRC_CHANNEL_MASK];

   for (unsigned i = header_size, j = 0; i < length; i++, j++)
      payload_sources[i] = offset(inst->src[URB_LOGICAL_SRC_DATA], bld, j);

   bld.LOAD_PAYLOAD(payload, payload_sources, length, header_size);

   delete [] payload_sources;

   inst->opcode = SHADER_OPCODE_SEND;
   inst->header_size = header_size;
   inst->dst = brw_null_reg();

   inst->sfid = BRW_SFID_URB;
   inst->desc = brw_urb_desc(devinfo,
                             GFX8_URB_OPCODE_SIMD8_WRITE,
                             per_slot_present,
                             channel_mask_present,
                             inst->offset);

   inst->mlen = length;
   inst->ex_desc = 0;
   inst->ex_mlen = 0;
   inst->send_has_side_effects = true;

   inst->resize_sources(4);

   inst->src[0] = brw_imm_ud(0); /* desc */
   inst->src[1] = brw_imm_ud(0); /* ex_desc */
   inst->src[2] = payload;
   inst->src[3] = brw_null_reg();
}

static void
lower_urb_write_logical_send_xe2(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->has_lsc);

   /* Get the logical send arguments. */
   const brw_reg handle = inst->src[URB_LOGICAL_SRC_HANDLE];
   const brw_reg src = inst->components_read(URB_LOGICAL_SRC_DATA) ?
      inst->src[URB_LOGICAL_SRC_DATA] : brw_reg(brw_imm_ud(0));
   assert(brw_type_size_bytes(src.type) == 4);

   /* Calculate the total number of components of the payload. */
   const unsigned src_comps = MAX2(1, inst->components_read(URB_LOGICAL_SRC_DATA));
   const unsigned src_sz = brw_type_size_bytes(src.type);

   brw_reg payload = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(payload, handle);

   /* The low 24-bits of the URB handle is a byte offset into the URB area.
    * Add the (OWord) offset of the write to this value.
    */
   if (inst->offset) {
      bld.ADD(payload, payload, brw_imm_ud(inst->offset * 16));
      inst->offset = 0;
   }

   brw_reg offsets = inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];
   if (offsets.file != BAD_FILE) {
      bld.ADD(payload, payload, offsets);
   }

   const brw_reg cmask = inst->src[URB_LOGICAL_SRC_CHANNEL_MASK];
   unsigned mask = 0;

   if (cmask.file != BAD_FILE) {
      assert(cmask.file == IMM);
      assert(cmask.type == BRW_TYPE_UD);
      mask = cmask.ud >> 16;
   }

   brw_reg payload2 = bld.move_to_vgrf(src, src_comps);
   const unsigned ex_mlen = (src_comps * src_sz * inst->exec_size) / REG_SIZE;

   inst->sfid = BRW_SFID_URB;

   enum lsc_opcode op = mask ? LSC_OP_STORE_CMASK : LSC_OP_STORE;
   inst->desc = lsc_msg_desc(devinfo, op,
                             LSC_ADDR_SURFTYPE_FLAT, LSC_ADDR_SIZE_A32,
                             LSC_DATA_SIZE_D32,
                             mask ? mask : src_comps /* num_channels */,
                             false /* transpose */,
                             LSC_CACHE(devinfo, STORE, L1UC_L3UC));


   /* Update the original instruction. */
   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, inst->exec_size);
   inst->ex_mlen = ex_mlen;
   inst->header_size = 0;
   inst->send_has_side_effects = true;
   inst->send_is_volatile = false;

   inst->resize_sources(4);

   inst->src[0] = brw_imm_ud(0);
   inst->src[1] = brw_imm_ud(0);

   inst->src[2] = payload;
   inst->src[3] = payload2;
}

static void
setup_color_payload(const fs_builder &bld, const brw_wm_prog_key *key,
                    brw_reg *dst, brw_reg color, unsigned components)
{
   if (key->clamp_fragment_color) {
      brw_reg tmp = bld.vgrf(BRW_TYPE_F, 4);
      assert(color.type == BRW_TYPE_F);

      for (unsigned i = 0; i < components; i++)
         set_saturate(true,
                      bld.MOV(offset(tmp, bld, i), offset(color, bld, i)));

      color = tmp;
   }

   for (unsigned i = 0; i < components; i++)
      dst[i] = offset(color, bld, i);
}

static void
lower_fb_write_logical_send(const fs_builder &bld, fs_inst *inst,
                            const struct brw_wm_prog_data *prog_data,
                            const brw_wm_prog_key *key,
                            const fs_thread_payload &fs_payload)
{
   assert(inst->src[FB_WRITE_LOGICAL_SRC_COMPONENTS].file == IMM);
   assert(inst->src[FB_WRITE_LOGICAL_SRC_NULL_RT].file == IMM);
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_reg color0 = inst->src[FB_WRITE_LOGICAL_SRC_COLOR0];
   const brw_reg color1 = inst->src[FB_WRITE_LOGICAL_SRC_COLOR1];
   const brw_reg src0_alpha = inst->src[FB_WRITE_LOGICAL_SRC_SRC0_ALPHA];
   const brw_reg src_depth = inst->src[FB_WRITE_LOGICAL_SRC_SRC_DEPTH];
   const brw_reg dst_depth = inst->src[FB_WRITE_LOGICAL_SRC_DST_DEPTH];
   const brw_reg src_stencil = inst->src[FB_WRITE_LOGICAL_SRC_SRC_STENCIL];
   brw_reg sample_mask = inst->src[FB_WRITE_LOGICAL_SRC_OMASK];
   const unsigned components =
      inst->src[FB_WRITE_LOGICAL_SRC_COMPONENTS].ud;
   const bool null_rt = inst->src[FB_WRITE_LOGICAL_SRC_NULL_RT].ud != 0;

   assert(inst->target != 0 || src0_alpha.file == BAD_FILE);

   brw_reg sources[15];
   int header_size = 2, payload_header_size;
   unsigned length = 0;

   if (devinfo->ver < 11 &&
      (color1.file != BAD_FILE || key->nr_color_regions > 1)) {

      /* From the Sandy Bridge PRM, volume 4, page 198:
       *
       *     "Dispatched Pixel Enables. One bit per pixel indicating
       *      which pixels were originally enabled when the thread was
       *      dispatched. This field is only required for the end-of-
       *      thread message and on all dual-source messages."
       */
      const fs_builder ubld = bld.exec_all().group(8, 0);

      brw_reg header = ubld.vgrf(BRW_TYPE_UD, 2);
      if (bld.group() < 16) {
         /* The header starts off as g0 and g1 for the first half */
         ubld.group(16, 0).MOV(header, retype(brw_vec8_grf(0, 0),
                                              BRW_TYPE_UD));
      } else {
         /* The header starts off as g0 and g2 for the second half */
         assert(bld.group() < 32);
         const brw_reg header_sources[2] = {
            retype(brw_vec8_grf(0, 0), BRW_TYPE_UD),
            retype(brw_vec8_grf(2, 0), BRW_TYPE_UD),
         };
         ubld.LOAD_PAYLOAD(header, header_sources, 2, 0);

         /* Gfx12 will require additional fix-ups if we ever hit this path. */
         assert(devinfo->ver < 12);
      }

      uint32_t g00_bits = 0;

      /* Set "Source0 Alpha Present to RenderTarget" bit in message
       * header.
       */
      if (src0_alpha.file != BAD_FILE)
         g00_bits |= 1 << 11;

      /* Set computes stencil to render target */
      if (prog_data->computed_stencil)
         g00_bits |= 1 << 14;

      if (g00_bits) {
         /* OR extra bits into g0.0 */
         ubld.group(1, 0).OR(component(header, 0),
                             retype(brw_vec1_grf(0, 0), BRW_TYPE_UD),
                             brw_imm_ud(g00_bits));
      }

      /* Set the render target index for choosing BLEND_STATE. */
      if (inst->target > 0) {
         ubld.group(1, 0).MOV(component(header, 2), brw_imm_ud(inst->target));
      }

      if (prog_data->uses_kill) {
         ubld.group(1, 0).MOV(retype(component(header, 15), BRW_TYPE_UW),
                              brw_sample_mask_reg(bld));
      }

      assert(length == 0);
      sources[0] = header;
      sources[1] = horiz_offset(header, 8);
      length = 2;
   }
   assert(length == 0 || length == 2);
   header_size = length;

   if (fs_payload.aa_dest_stencil_reg[0]) {
      assert(inst->group < 16);
      sources[length] = brw_vgrf(bld.shader->alloc.allocate(1), BRW_TYPE_F);
      bld.group(8, 0).exec_all().annotate("FB write stencil/AA alpha")
         .MOV(sources[length],
              brw_reg(brw_vec8_grf(fs_payload.aa_dest_stencil_reg[0], 0)));
      length++;
   }

   if (src0_alpha.file != BAD_FILE) {
      for (unsigned i = 0; i < bld.dispatch_width() / 8; i++) {
         const fs_builder &ubld = bld.exec_all().group(8, i)
                                    .annotate("FB write src0 alpha");
         const brw_reg tmp = ubld.vgrf(BRW_TYPE_F);
         ubld.MOV(tmp, horiz_offset(src0_alpha, i * 8));
         setup_color_payload(ubld, key, &sources[length], tmp, 1);
         length++;
      }
   }

   if (sample_mask.file != BAD_FILE) {
      const brw_reg tmp = brw_vgrf(bld.shader->alloc.allocate(reg_unit(devinfo)),
                                  BRW_TYPE_UD);

      /* Hand over gl_SampleMask.  Only the lower 16 bits of each channel are
       * relevant.  Since it's unsigned single words one vgrf is always
       * 16-wide, but only the lower or higher 8 channels will be used by the
       * hardware when doing a SIMD8 write depending on whether we have
       * selected the subspans for the first or second half respectively.
       */
      assert(sample_mask.file != BAD_FILE &&
             brw_type_size_bytes(sample_mask.type) == 4);
      sample_mask.type = BRW_TYPE_UW;
      sample_mask.stride *= 2;

      bld.exec_all().annotate("FB write oMask")
         .MOV(horiz_offset(retype(tmp, BRW_TYPE_UW),
                           inst->group % (16 * reg_unit(devinfo))),
              sample_mask);

      for (unsigned i = 0; i < reg_unit(devinfo); i++)
         sources[length++] = byte_offset(tmp, REG_SIZE * i);
   }

   payload_header_size = length;

   setup_color_payload(bld, key, &sources[length], color0, components);
   length += 4;

   if (color1.file != BAD_FILE) {
      setup_color_payload(bld, key, &sources[length], color1, components);
      length += 4;
   }

   if (src_depth.file != BAD_FILE) {
      sources[length] = src_depth;
      length++;
   }

   if (dst_depth.file != BAD_FILE) {
      sources[length] = dst_depth;
      length++;
   }

   if (src_stencil.file != BAD_FILE) {
      assert(bld.dispatch_width() == 8 * reg_unit(devinfo));

      /* XXX: src_stencil is only available on gfx9+. dst_depth is never
       * available on gfx9+. As such it's impossible to have both enabled at the
       * same time and therefore length cannot overrun the array.
       */
      assert(length < 15 * reg_unit(devinfo));

      sources[length] = bld.vgrf(BRW_TYPE_UD);
      bld.exec_all().annotate("FB write OS")
         .MOV(retype(sources[length], BRW_TYPE_UB),
              subscript(src_stencil, BRW_TYPE_UB, 0));
      length++;
   }

   /* Send from the GRF */
   brw_reg payload = brw_vgrf(-1, BRW_TYPE_F);
   fs_inst *load = bld.LOAD_PAYLOAD(payload, sources, length, payload_header_size);
   payload.nr = bld.shader->alloc.allocate(regs_written(load));
   load->dst = payload;

   uint32_t msg_ctl = brw_fb_write_msg_control(inst, prog_data);

   /* XXX - Bit 13 Per-sample PS enable */
   inst->desc =
      (inst->group / 16) << 11 | /* rt slot group */
      brw_fb_write_desc(devinfo, inst->target, msg_ctl, inst->last_rt,
                        0 /* coarse_rt_write */);

   brw_reg desc = brw_imm_ud(0);
   if (prog_data->coarse_pixel_dispatch == BRW_ALWAYS) {
      inst->desc |= (1 << 18);
   } else if (prog_data->coarse_pixel_dispatch == BRW_SOMETIMES) {
      STATIC_ASSERT(INTEL_MSAA_FLAG_COARSE_RT_WRITES == (1 << 18));
      const fs_builder &ubld = bld.exec_all().group(8, 0);
      desc = ubld.vgrf(BRW_TYPE_UD);
      ubld.AND(desc, dynamic_msaa_flags(prog_data),
               brw_imm_ud(INTEL_MSAA_FLAG_COARSE_RT_WRITES));
      desc = component(desc, 0);
   }

   uint32_t ex_desc = 0;
   if (devinfo->ver >= 20) {
      ex_desc = inst->target << 21 |
                null_rt << 20 |
                (src0_alpha.file != BAD_FILE) << 15 |
                (src_stencil.file != BAD_FILE) << 14 |
                (src_depth.file != BAD_FILE) << 13 |
                (sample_mask.file != BAD_FILE) << 12;
   } else if (devinfo->ver >= 11) {
      /* Set the "Render Target Index" and "Src0 Alpha Present" fields
       * in the extended message descriptor, in lieu of using a header.
       */
      ex_desc = inst->target << 12 |
                null_rt << 20 |
                (src0_alpha.file != BAD_FILE) << 15;
   }
   inst->ex_desc = ex_desc;

   inst->opcode = SHADER_OPCODE_SEND;
   inst->resize_sources(3);
   inst->sfid = GFX6_SFID_DATAPORT_RENDER_CACHE;
   inst->src[0] = desc;
   inst->src[1] = brw_imm_ud(0);
   inst->src[2] = payload;
   inst->mlen = regs_written(load);
   inst->ex_mlen = 0;
   inst->header_size = header_size;
   inst->check_tdr = true;
   inst->send_has_side_effects = true;
}

static void
lower_fb_read_logical_send(const fs_builder &bld, fs_inst *inst,
                           const struct brw_wm_prog_data *wm_prog_data)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const fs_builder &ubld = bld.exec_all().group(8, 0);
   const unsigned length = 2;
   const brw_reg header = ubld.vgrf(BRW_TYPE_UD, length);

   assert(devinfo->ver >= 9 && devinfo->ver < 20);

   if (bld.group() < 16) {
      ubld.group(16, 0).MOV(header, retype(brw_vec8_grf(0, 0),
                                           BRW_TYPE_UD));
   } else {
      assert(bld.group() < 32);
      const brw_reg header_sources[] = {
         retype(brw_vec8_grf(0, 0), BRW_TYPE_UD),
         retype(brw_vec8_grf(2, 0), BRW_TYPE_UD)
      };
      ubld.LOAD_PAYLOAD(header, header_sources, ARRAY_SIZE(header_sources), 0);

      if (devinfo->ver >= 12) {
         /* On Gfx12 the Viewport and Render Target Array Index fields (AKA
          * Poly 0 Info) are provided in r1.1 instead of r0.0, and the render
          * target message header format was updated accordingly -- However
          * the updated format only works for the lower 16 channels in a
          * SIMD32 thread, since the higher 16 channels want the subspan data
          * from r2 instead of r1, so we need to copy over the contents of
          * r1.1 in order to fix things up.
          */
         ubld.group(1, 0).MOV(component(header, 9),
                              retype(brw_vec1_grf(1, 1), BRW_TYPE_UD));
      }
   }

   /* BSpec 12470 (Gfx8-11), BSpec 47842 (Gfx12+) :
    *
    *   "Must be zero for Render Target Read message."
    *
    * For bits :
    *   - 14 : Stencil Present to Render Target
    *   - 13 : Source Depth Present to Render Target
    *   - 12 : oMask to Render Target
    *   - 11 : Source0 Alpha Present to Render Target
    */
   ubld.group(1, 0).AND(component(header, 0),
                        component(header, 0),
                        brw_imm_ud(~INTEL_MASK(14, 11)));

   inst->resize_sources(4);
   inst->opcode = SHADER_OPCODE_SEND;
   inst->src[0] = brw_imm_ud(0);
   inst->src[1] = brw_imm_ud(0);
   inst->src[2] = header;
   inst->src[3] = brw_reg();
   inst->mlen = length;
   inst->header_size = length;
   inst->sfid = GFX6_SFID_DATAPORT_RENDER_CACHE;
   inst->check_tdr = true;
   inst->desc =
      (inst->group / 16) << 11 | /* rt slot group */
      brw_fb_read_desc(devinfo, inst->target,
                       0 /* msg_control */, inst->exec_size,
                       wm_prog_data->persample_dispatch);
}

static bool
is_high_sampler(const struct intel_device_info *devinfo, const brw_reg &sampler)
{
   return sampler.file != IMM || sampler.ud >= 16;
}

static unsigned
sampler_msg_type(const intel_device_info *devinfo,
                 opcode opcode, bool shadow_compare,
                 bool lod_is_zero, bool has_min_lod)
{
   switch (opcode) {
   case SHADER_OPCODE_TEX_LOGICAL:
      if (devinfo->ver >= 20 && has_min_lod) {
         return shadow_compare ? XE2_SAMPLER_MESSAGE_SAMPLE_COMPARE_MLOD :
                                 XE2_SAMPLER_MESSAGE_SAMPLE_MLOD;
      } else {
         return shadow_compare ? GFX5_SAMPLER_MESSAGE_SAMPLE_COMPARE :
                                 GFX5_SAMPLER_MESSAGE_SAMPLE;
      }
   case FS_OPCODE_TXB_LOGICAL:
      return shadow_compare ? GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE :
                              GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS;
   case SHADER_OPCODE_TXL_LOGICAL:
      assert(!has_min_lod);
      if (lod_is_zero) {
         return shadow_compare ? GFX9_SAMPLER_MESSAGE_SAMPLE_C_LZ :
                                 GFX9_SAMPLER_MESSAGE_SAMPLE_LZ;
      }
      return shadow_compare ? GFX5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE :
                              GFX5_SAMPLER_MESSAGE_SAMPLE_LOD;
   case SHADER_OPCODE_TXS_LOGICAL:
   case SHADER_OPCODE_IMAGE_SIZE_LOGICAL:
      assert(!has_min_lod);
      return GFX5_SAMPLER_MESSAGE_SAMPLE_RESINFO;
   case SHADER_OPCODE_TXD_LOGICAL:
      return shadow_compare ? HSW_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE :
                              GFX5_SAMPLER_MESSAGE_SAMPLE_DERIVS;
   case SHADER_OPCODE_TXF_LOGICAL:
      assert(!has_min_lod);
      return lod_is_zero ? GFX9_SAMPLER_MESSAGE_SAMPLE_LD_LZ :
                           GFX5_SAMPLER_MESSAGE_SAMPLE_LD;
   case SHADER_OPCODE_TXF_CMS_W_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL:
      assert(!has_min_lod);
      return GFX9_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W;
   case SHADER_OPCODE_TXF_MCS_LOGICAL:
      assert(!has_min_lod);
      return GFX7_SAMPLER_MESSAGE_SAMPLE_LD_MCS;
   case SHADER_OPCODE_LOD_LOGICAL:
      assert(!has_min_lod);
      return GFX5_SAMPLER_MESSAGE_LOD;
   case SHADER_OPCODE_TG4_LOGICAL:
      assert(!has_min_lod);
      return shadow_compare ? GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_C :
                              GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4;
      break;
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
      assert(!has_min_lod);
      return shadow_compare ? GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C :
                              GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO;
   case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
      assert(!has_min_lod);
      assert(devinfo->ver >= 20);
      return shadow_compare ? XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L_C:
                              XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L;
   case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
      assert(!has_min_lod);
      assert(devinfo->ver >= 20);
      return XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_B;
   case SHADER_OPCODE_TG4_BIAS_LOGICAL:
      assert(!has_min_lod);
      assert(devinfo->ver >= 20);
      return XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B;
   case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
      assert(!has_min_lod);
      assert(devinfo->ver >= 20);
      return shadow_compare ? XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C :
                              XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L;
   case SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL:
      assert(!has_min_lod);
      assert(devinfo->ver >= 20);
      return shadow_compare ? XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C :
                              XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I;
  case SHADER_OPCODE_SAMPLEINFO_LOGICAL:
      assert(!has_min_lod);
      return GFX6_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO;
   default:
      unreachable("not reached");
   }
}

/**
 * Emit a LOAD_PAYLOAD instruction while ensuring the sources are aligned to
 * the given requested_alignment_sz.
 */
static fs_inst *
emit_load_payload_with_padding(const fs_builder &bld, const brw_reg &dst,
                               const brw_reg *src, unsigned sources,
                               unsigned header_size,
                               unsigned requested_alignment_sz)
{
   unsigned length = 0;
   unsigned num_srcs =
      sources * DIV_ROUND_UP(requested_alignment_sz, bld.dispatch_width());
   brw_reg *src_comps = new brw_reg[num_srcs];

   for (unsigned i = 0; i < header_size; i++)
      src_comps[length++] = src[i];

   for (unsigned i = header_size; i < sources; i++) {
      unsigned src_sz =
         retype(dst, src[i].type).component_size(bld.dispatch_width());
      const enum brw_reg_type padding_payload_type =
         brw_type_with_size(BRW_TYPE_UD, brw_type_size_bits(src[i].type));

      src_comps[length++] = src[i];

      /* Expand the real sources if component of requested payload type is
       * larger than real source component.
       */
      if (src_sz < requested_alignment_sz) {
         for (unsigned j = 0; j < (requested_alignment_sz / src_sz) - 1; j++) {
            src_comps[length++] = retype(brw_reg(), padding_payload_type);
         }
      }
   }

   fs_inst *inst = bld.LOAD_PAYLOAD(dst, src_comps, length, header_size);
   delete[] src_comps;

   return inst;
}

static bool
shader_opcode_needs_header(opcode op)
{
   switch (op) {
   case SHADER_OPCODE_TG4_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_BIAS_LOGICAL:
   case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_SAMPLEINFO_LOGICAL:
      return true;
   default:
      break;
   }

   return false;
}

static void
lower_sampler_logical_send(const fs_builder &bld, fs_inst *inst,
                           const brw_reg &coordinate,
                           const brw_reg &shadow_c,
                           brw_reg lod, const brw_reg &lod2,
                           const brw_reg &min_lod,
                           const brw_reg &sample_index,
                           const brw_reg &mcs,
                           const brw_reg &surface,
                           const brw_reg &sampler,
                           const brw_reg &surface_handle,
                           const brw_reg &sampler_handle,
                           const brw_reg &tg4_offset,
                           unsigned payload_type_bit_size,
                           unsigned coord_components,
                           unsigned grad_components,
                           bool residency)
{
   /* We never generate EOT sampler messages */
   assert(!inst->eot);

   const brw_compiler *compiler = bld.shader->compiler;
   const intel_device_info *devinfo = bld.shader->devinfo;
   const enum brw_reg_type payload_type =
      brw_type_with_size(BRW_TYPE_F, payload_type_bit_size);
   const enum brw_reg_type payload_unsigned_type =
      brw_type_with_size(BRW_TYPE_UD, payload_type_bit_size);
   const enum brw_reg_type payload_signed_type =
      brw_type_with_size(BRW_TYPE_D, payload_type_bit_size);
   unsigned header_size = 0, length = 0;
   opcode op = inst->opcode;
   brw_reg sources[1 + MAX_SAMPLER_MESSAGE_SIZE];
   for (unsigned i = 0; i < ARRAY_SIZE(sources); i++)
      sources[i] = bld.vgrf(payload_type);

   /* We must have exactly one of surface/sampler and surface/sampler_handle */
   assert((surface.file == BAD_FILE) != (surface_handle.file == BAD_FILE));
   assert((sampler.file == BAD_FILE) != (sampler_handle.file == BAD_FILE));

   if (shader_opcode_needs_header(op) || inst->offset != 0 ||
       sampler_handle.file != BAD_FILE ||
       is_high_sampler(devinfo, sampler) ||
       residency) {
      /* For general texture offsets (no txf workaround), we need a header to
       * put them in.
       *
       * TG4 needs to place its channel select in the header, for interaction
       * with ARB_texture_swizzle.  The sampler index is only 4-bits, so for
       * larger sampler numbers we need to offset the Sampler State Pointer in
       * the header.
       */
      brw_reg header = retype(sources[0], BRW_TYPE_UD);
      for (header_size = 0; header_size < reg_unit(devinfo); header_size++)
         sources[length++] = byte_offset(header, REG_SIZE * header_size);

      /* If we're requesting fewer than four channels worth of response,
       * and we have an explicit header, we need to set up the sampler
       * writemask.  It's reversed from normal: 1 means "don't write".
       */
      unsigned comps_regs =
         DIV_ROUND_UP(regs_written(inst) - reg_unit(devinfo) * residency,
                      reg_unit(devinfo));
      unsigned comp_regs =
         DIV_ROUND_UP(inst->dst.component_size(inst->exec_size),
                      reg_unit(devinfo) * REG_SIZE);
      if (comps_regs < 4 * comp_regs) {
         assert(comps_regs % comp_regs == 0);
         unsigned mask = ~((1 << (comps_regs / comp_regs)) - 1) & 0xf;
         inst->offset |= mask << 12;
      }

      if (residency)
         inst->offset |= 1 << 23; /* g0.2 bit23 : Pixel Null Mask Enable */

      /* Build the actual header */
      const fs_builder ubld = bld.exec_all().group(8 * reg_unit(devinfo), 0);
      const fs_builder ubld1 = ubld.group(1, 0);
      if (devinfo->ver >= 11)
         ubld.MOV(header, brw_imm_ud(0));
      else
         ubld.MOV(header, retype(brw_vec8_grf(0, 0), BRW_TYPE_UD));
      if (inst->offset) {
         ubld1.MOV(component(header, 2), brw_imm_ud(inst->offset));
      } else if (devinfo->ver < 11 &&
                 bld.shader->stage != MESA_SHADER_VERTEX &&
                 bld.shader->stage != MESA_SHADER_FRAGMENT) {
         /* The vertex and fragment stages have g0.2 set to 0, so
          * header0.2 is 0 when g0 is copied. Other stages may not, so we
          * must set it to 0 to avoid setting undesirable bits in the
          * message.
          */
         ubld1.MOV(component(header, 2), brw_imm_ud(0));
      }

      if (sampler_handle.file != BAD_FILE) {
         /* Bindless sampler handles aren't relative to the sampler state
          * pointer passed into the shader through SAMPLER_STATE_POINTERS_*.
          * Instead, it's an absolute pointer relative to dynamic state base
          * address.
          *
          * Sampler states are 16 bytes each and the pointer we give here has
          * to be 32-byte aligned.  In order to avoid more indirect messages
          * than required, we assume that all bindless sampler states are
          * 32-byte aligned.  This sacrifices a bit of general state base
          * address space but means we can do something more efficient in the
          * shader.
          */
         if (compiler->use_bindless_sampler_offset) {
            assert(devinfo->ver >= 11);
            ubld1.OR(component(header, 3), sampler_handle, brw_imm_ud(1));
         } else {
            ubld1.MOV(component(header, 3), sampler_handle);
         }
      } else if (is_high_sampler(devinfo, sampler)) {
         brw_reg sampler_state_ptr =
            retype(brw_vec1_grf(0, 3), BRW_TYPE_UD);

         /* Gfx11+ sampler message headers include bits in 4:0 which conflict
          * with the ones included in g0.3 bits 4:0.  Mask them out.
          */
         if (devinfo->ver >= 11) {
            sampler_state_ptr = ubld1.vgrf(BRW_TYPE_UD);
            ubld1.AND(sampler_state_ptr,
                      retype(brw_vec1_grf(0, 3), BRW_TYPE_UD),
                      brw_imm_ud(INTEL_MASK(31, 5)));
         }

         if (sampler.file == IMM) {
            assert(sampler.ud >= 16);
            const int sampler_state_size = 16; /* 16 bytes */

            ubld1.ADD(component(header, 3), sampler_state_ptr,
                      brw_imm_ud(16 * (sampler.ud / 16) * sampler_state_size));
         } else {
            brw_reg tmp = ubld1.vgrf(BRW_TYPE_UD);
            ubld1.AND(tmp, sampler, brw_imm_ud(0x0f0));
            ubld1.SHL(tmp, tmp, brw_imm_ud(4));
            ubld1.ADD(component(header, 3), sampler_state_ptr, tmp);
         }
      } else if (devinfo->ver >= 11) {
         /* Gfx11+ sampler message headers include bits in 4:0 which conflict
          * with the ones included in g0.3 bits 4:0.  Mask them out.
          */
         ubld1.AND(component(header, 3),
                   retype(brw_vec1_grf(0, 3), BRW_TYPE_UD),
                   brw_imm_ud(INTEL_MASK(31, 5)));
      }
   }

   const bool lod_is_zero = lod.is_zero();

   /* On Xe2 and newer platforms, min_lod is the first parameter specifically
    * so that a bunch of other, possibly unused, parameters don't need to also
    * be included.
    */
   const unsigned msg_type =
      sampler_msg_type(devinfo, op, inst->shadow_compare, lod_is_zero,
                       min_lod.file != BAD_FILE);

   const bool min_lod_is_first = devinfo->ver >= 20 &&
      (msg_type == XE2_SAMPLER_MESSAGE_SAMPLE_MLOD ||
       msg_type == XE2_SAMPLER_MESSAGE_SAMPLE_COMPARE_MLOD);

   if (min_lod_is_first) {
      assert(min_lod.file != BAD_FILE);
      bld.MOV(sources[length++], min_lod);
   }

   if (shadow_c.file != BAD_FILE) {
      bld.MOV(sources[length], shadow_c);
      length++;
   }

   bool coordinate_done = false;

   /* Set up the LOD info */
   switch (op) {
   case SHADER_OPCODE_TXL_LOGICAL:
      if (lod_is_zero)
         break;
      FALLTHROUGH;
   case FS_OPCODE_TXB_LOGICAL:
   case SHADER_OPCODE_TG4_BIAS_LOGICAL:
   case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
      bld.MOV(sources[length], lod);
      length++;
      break;
   case SHADER_OPCODE_TXD_LOGICAL:
      /* TXD should have been lowered in SIMD16 mode (in SIMD32 mode in
       * Xe2+).
       */
      assert(bld.dispatch_width() == (8 * reg_unit(devinfo)));

      /* Load dPdx and the coordinate together:
       * [hdr], [ref], x, dPdx.x, dPdy.x, y, dPdx.y, dPdy.y, z, dPdx.z, dPdy.z
       */
      for (unsigned i = 0; i < coord_components; i++) {
         bld.MOV(sources[length++], offset(coordinate, bld, i));

         /* For cube map array, the coordinate is (u,v,r,ai) but there are
          * only derivatives for (u, v, r).
          */
         if (i < grad_components) {
            bld.MOV(sources[length++], offset(lod, bld, i));
            bld.MOV(sources[length++], offset(lod2, bld, i));
         }
      }

      coordinate_done = true;
      break;
   case SHADER_OPCODE_TXS_LOGICAL:
      sources[length] = retype(sources[length], payload_unsigned_type);
      bld.MOV(sources[length++], lod);
      break;
   case SHADER_OPCODE_IMAGE_SIZE_LOGICAL:
      /* We need an LOD; just use 0 */
      sources[length] = retype(sources[length], payload_unsigned_type);
      bld.MOV(sources[length++], brw_imm_ud(0));
      break;
   case SHADER_OPCODE_TXF_LOGICAL:
       /* On Gfx9 the parameters are intermixed they are u, v, lod, r. */
      sources[length] = retype(sources[length], payload_signed_type);
      bld.MOV(sources[length++], coordinate);

      if (coord_components >= 2) {
         sources[length] = retype(sources[length], payload_signed_type);
         bld.MOV(sources[length], offset(coordinate, bld, 1));
      } else {
         sources[length] = brw_imm_d(0);
      }
      length++;

      if (!lod_is_zero) {
         sources[length] = retype(sources[length], payload_signed_type);
         bld.MOV(sources[length++], lod);
      }

      for (unsigned i = 2; i < coord_components; i++) {
         sources[length] = retype(sources[length], payload_signed_type);
         bld.MOV(sources[length++], offset(coordinate, bld, i));
      }

      coordinate_done = true;
      break;

   case SHADER_OPCODE_TXF_CMS_W_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL:
      sources[length] = retype(sources[length], payload_unsigned_type);
      bld.MOV(sources[length++], sample_index);

      /* Data from the multisample control surface. */
      for (unsigned i = 0; i < 2; ++i) {
         /* Sampler always writes 4/8 register worth of data but for ld_mcs
          * only valid data is in first two register. So with 16-bit
          * payload, we need to split 2-32bit register into 4-16-bit
          * payload.
          *
          * From the Gfx12HP BSpec: Render Engine - 3D and GPGPU Programs -
          * Shared Functions - 3D Sampler - Messages - Message Format:
          *
          *    ld2dms_w   si  mcs0 mcs1 mcs2  mcs3  u  v  r
          */
         if (op == SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL) {
            brw_reg tmp = offset(mcs, bld, i);
            sources[length] = retype(sources[length], payload_unsigned_type);
            bld.MOV(sources[length++],
                    mcs.file == IMM ? mcs :
                    brw_reg(subscript(tmp, payload_unsigned_type, 0)));

            sources[length] = retype(sources[length], payload_unsigned_type);
            bld.MOV(sources[length++],
                    mcs.file == IMM ? mcs :
                    brw_reg(subscript(tmp, payload_unsigned_type, 1)));
         } else {
            sources[length] = retype(sources[length], payload_unsigned_type);
            bld.MOV(sources[length++],
                    mcs.file == IMM ? mcs : offset(mcs, bld, i));
         }
      }
      FALLTHROUGH;

   case SHADER_OPCODE_TXF_MCS_LOGICAL:
      /* There is no offsetting for this message; just copy in the integer
       * texture coordinates.
       */
      for (unsigned i = 0; i < coord_components; i++) {
         sources[length] = retype(sources[length], payload_signed_type);
         bld.MOV(sources[length++], offset(coordinate, bld, i));
      }

      coordinate_done = true;
      break;
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
      /* More crazy intermixing */
      for (unsigned i = 0; i < 2; i++) /* u, v */
         bld.MOV(sources[length++], offset(coordinate, bld, i));

      for (unsigned i = 0; i < 2; i++) { /* offu, offv */
         sources[length] = retype(sources[length], payload_signed_type);
         bld.MOV(sources[length++], offset(tg4_offset, bld, i));
      }

      if (coord_components == 3) /* r if present */
         bld.MOV(sources[length++], offset(coordinate, bld, 2));

      coordinate_done = true;
      break;
   default:
      break;
   }

   /* Set up the coordinate (except for cases where it was done above) */
   if (!coordinate_done) {
      for (unsigned i = 0; i < coord_components; i++)
         bld.MOV(retype(sources[length++], payload_type),
                 offset(coordinate, bld, i));
   }

   if (min_lod.file != BAD_FILE && !min_lod_is_first) {
      /* Account for all of the missing coordinate sources */
      if (op == FS_OPCODE_TXB_LOGICAL && devinfo->ver >= 20) {
         /* Bspec 64985:
          *
          * For sample_b sampler message format:
          *
          * SIMD16H/SIMD32H
          * Param Number   0     1  2  3  4  5
          * Param          BIAS  U  V  R  Ai MLOD
          *
          * SIMD16/SIMD32
          * Param Number   0        1  2  3  4
          * Param          BIAS_AI  U  V  R  MLOD
          */
         length += 3 - coord_components;
      } else if (op == SHADER_OPCODE_TXD_LOGICAL && devinfo->verx10 >= 125) {
         /* On DG2 and newer platforms, sample_d can only be used with 1D and
          * 2D surfaces, so the maximum number of gradient components is 2.
          * In spite of this limitation, the Bspec lists a mysterious R
          * component before the min_lod, so the maximum coordinate components
          * is 3.
          *
          * See bspec 45942, "Enable new message layout for cube array"
          */
         length += 3 - coord_components;
         length += (2 - grad_components) * 2;
      } else {
         length += 4 - coord_components;
         if (op == SHADER_OPCODE_TXD_LOGICAL)
            length += (3 - grad_components) * 2;
      }

      bld.MOV(sources[length++], min_lod);

      /* Wa_14014595444: Populate MLOD as parameter 5 (twice). */
       if (devinfo->verx10 == 125 && op == FS_OPCODE_TXB_LOGICAL &&
          !inst->shadow_compare)
         bld.MOV(sources[length++], min_lod);
   }

   const brw_reg src_payload =
      brw_vgrf(bld.shader->alloc.allocate(length * bld.dispatch_width() / 8),
               BRW_TYPE_F);
   /* In case of 16-bit payload each component takes one full register in
    * both SIMD8H and SIMD16H modes. In both cases one reg can hold 16
    * elements. In SIMD8H case hardware simply expects the components to be
    * padded (i.e., aligned on reg boundary).
    */
   fs_inst *load_payload_inst =
      emit_load_payload_with_padding(bld, src_payload, sources, length,
                                     header_size, REG_SIZE * reg_unit(devinfo));
   unsigned mlen = load_payload_inst->size_written / REG_SIZE;
   unsigned simd_mode = 0;
   if (devinfo->ver < 20) {
      if (payload_type_bit_size == 16) {
         assert(devinfo->ver >= 11);
         simd_mode = inst->exec_size <= 8 ? GFX10_SAMPLER_SIMD_MODE_SIMD8H :
            GFX10_SAMPLER_SIMD_MODE_SIMD16H;
      } else {
         simd_mode = inst->exec_size <= 8 ? BRW_SAMPLER_SIMD_MODE_SIMD8 :
            BRW_SAMPLER_SIMD_MODE_SIMD16;
      }
   } else {
      if (payload_type_bit_size == 16) {
         simd_mode = inst->exec_size <= 16 ? XE2_SAMPLER_SIMD_MODE_SIMD16H :
            XE2_SAMPLER_SIMD_MODE_SIMD32H;
      } else {
         simd_mode = inst->exec_size <= 16 ? XE2_SAMPLER_SIMD_MODE_SIMD16 :
            XE2_SAMPLER_SIMD_MODE_SIMD32;
      }
   }

   /* Generate the SEND. */
   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = mlen;
   inst->header_size = header_size;
   inst->sfid = BRW_SFID_SAMPLER;
   uint sampler_ret_type = brw_type_size_bits(inst->dst.type) == 16
      ? GFX8_SAMPLER_RETURN_FORMAT_16BITS
      : GFX8_SAMPLER_RETURN_FORMAT_32BITS;
   if (surface.file == IMM &&
       (sampler.file == IMM || sampler_handle.file != BAD_FILE)) {
      inst->desc = brw_sampler_desc(devinfo, surface.ud,
                                    sampler.file == IMM ? sampler.ud % 16 : 0,
                                    msg_type,
                                    simd_mode,
                                    sampler_ret_type);
      inst->src[0] = brw_imm_ud(0);
      inst->src[1] = brw_imm_ud(0);
   } else if (surface_handle.file != BAD_FILE) {
      /* Bindless surface */
      inst->desc = brw_sampler_desc(devinfo,
                                    GFX9_BTI_BINDLESS,
                                    sampler.file == IMM ? sampler.ud % 16 : 0,
                                    msg_type,
                                    simd_mode,
                                    sampler_ret_type);

      /* For bindless samplers, the entire address is included in the message
       * header so we can leave the portion in the message descriptor 0.
       */
      if (sampler_handle.file != BAD_FILE || sampler.file == IMM) {
         inst->src[0] = brw_imm_ud(0);
      } else {
         const fs_builder ubld = bld.group(1, 0).exec_all();
         brw_reg desc = ubld.vgrf(BRW_TYPE_UD);
         ubld.SHL(desc, sampler, brw_imm_ud(8));
         inst->src[0] = component(desc, 0);
      }

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      inst->src[1] = retype(surface_handle, BRW_TYPE_UD);
      inst->send_ex_bso = compiler->extended_bindless_surface_offset;
   } else {
      /* Immediate portion of the descriptor */
      inst->desc = brw_sampler_desc(devinfo,
                                    0, /* surface */
                                    0, /* sampler */
                                    msg_type,
                                    simd_mode,
                                    sampler_ret_type);
      const fs_builder ubld = bld.group(1, 0).exec_all();
      brw_reg desc = ubld.vgrf(BRW_TYPE_UD);
      if (surface.equals(sampler)) {
         /* This case is common in GL */
         ubld.MUL(desc, surface, brw_imm_ud(0x101));
      } else {
         if (sampler_handle.file != BAD_FILE) {
            ubld.MOV(desc, surface);
         } else if (sampler.file == IMM) {
            ubld.OR(desc, surface, brw_imm_ud(sampler.ud << 8));
         } else {
            ubld.SHL(desc, sampler, brw_imm_ud(8));
            ubld.OR(desc, desc, surface);
         }
      }
      ubld.AND(desc, desc, brw_imm_ud(0xfff));

      inst->src[0] = component(desc, 0);
      inst->src[1] = brw_imm_ud(0); /* ex_desc */
   }

   inst->ex_desc = 0;

   inst->src[2] = src_payload;
   inst->resize_sources(3);

   /* Message length > MAX_SAMPLER_MESSAGE_SIZE disallowed by hardware. */
   assert(inst->mlen <= MAX_SAMPLER_MESSAGE_SIZE * reg_unit(devinfo));
}

static unsigned
get_sampler_msg_payload_type_bit_size(const intel_device_info *devinfo,
                                      const fs_inst *inst)
{
   assert(inst);
   const brw_reg *src = inst->src;
   unsigned src_type_size = 0;

   /* All sources need to have the same size, therefore seek the first valid
    * and take the size from there.
    */
   for (unsigned i = 0; i < TEX_LOGICAL_NUM_SRCS; i++) {
      if (src[i].file != BAD_FILE) {
         src_type_size = brw_type_size_bytes(src[i].type);
         break;
      }
   }

   assert(src_type_size == 2 || src_type_size == 4);

#ifndef NDEBUG
   /* Make sure all sources agree. On gfx12 this doesn't hold when sampling
    * compressed multisampled surfaces. There the payload contains MCS data
    * which is already in 16-bits unlike the other parameters that need forced
    * conversion.
    */
   if (inst->opcode != SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL) {
      for (unsigned i = 0; i < TEX_LOGICAL_NUM_SRCS; i++) {
         assert(src[i].file == BAD_FILE ||
                brw_type_size_bytes(src[i].type) == src_type_size);
      }
   }
#endif

   if (devinfo->verx10 < 125)
      return src_type_size * 8;

   /* Force conversion from 32-bit sources to 16-bit payload. From the XeHP Bspec:
    * 3D and GPGPU Programs - Shared Functions - 3D Sampler - Messages - Message
    * Format [GFX12:HAS:1209977870] *
    *
    *  ld2dms_w       SIMD8H and SIMD16H Only
    *  ld_mcs         SIMD8H and SIMD16H Only
    *  ld2dms         REMOVEDBY(GEN:HAS:1406788836)
    */
   if (inst->opcode == SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL ||
       inst->opcode == SHADER_OPCODE_TXF_MCS_LOGICAL)
      src_type_size = 2;

   return src_type_size * 8;
}

static void
lower_sampler_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_reg coordinate = inst->src[TEX_LOGICAL_SRC_COORDINATE];
   const brw_reg shadow_c = inst->src[TEX_LOGICAL_SRC_SHADOW_C];
   const brw_reg lod = inst->src[TEX_LOGICAL_SRC_LOD];
   const brw_reg lod2 = inst->src[TEX_LOGICAL_SRC_LOD2];
   const brw_reg min_lod = inst->src[TEX_LOGICAL_SRC_MIN_LOD];
   const brw_reg sample_index = inst->src[TEX_LOGICAL_SRC_SAMPLE_INDEX];
   const brw_reg mcs = inst->src[TEX_LOGICAL_SRC_MCS];
   const brw_reg surface = inst->src[TEX_LOGICAL_SRC_SURFACE];
   const brw_reg sampler = inst->src[TEX_LOGICAL_SRC_SAMPLER];
   const brw_reg surface_handle = inst->src[TEX_LOGICAL_SRC_SURFACE_HANDLE];
   const brw_reg sampler_handle = inst->src[TEX_LOGICAL_SRC_SAMPLER_HANDLE];
   const brw_reg tg4_offset = inst->src[TEX_LOGICAL_SRC_TG4_OFFSET];
   assert(inst->src[TEX_LOGICAL_SRC_COORD_COMPONENTS].file == IMM);
   const unsigned coord_components = inst->src[TEX_LOGICAL_SRC_COORD_COMPONENTS].ud;
   assert(inst->src[TEX_LOGICAL_SRC_GRAD_COMPONENTS].file == IMM);
   const unsigned grad_components = inst->src[TEX_LOGICAL_SRC_GRAD_COMPONENTS].ud;
   assert(inst->src[TEX_LOGICAL_SRC_RESIDENCY].file == IMM);
   const bool residency = inst->src[TEX_LOGICAL_SRC_RESIDENCY].ud != 0;

   const unsigned msg_payload_type_bit_size =
      get_sampler_msg_payload_type_bit_size(devinfo, inst);

   /* 16-bit payloads are available only on gfx11+ */
   assert(msg_payload_type_bit_size != 16 || devinfo->ver >= 11);

   lower_sampler_logical_send(bld, inst, coordinate,
                              shadow_c, lod, lod2, min_lod,
                              sample_index,
                              mcs, surface, sampler,
                              surface_handle, sampler_handle,
                              tg4_offset,
                              msg_payload_type_bit_size,
                              coord_components, grad_components,
                              residency);
}

/**
 * Predicate the specified instruction on the vector mask.
 */
static void
emit_predicate_on_vector_mask(const fs_builder &bld, fs_inst *inst)
{
   assert(bld.shader->stage == MESA_SHADER_FRAGMENT &&
          bld.group() == inst->group &&
          bld.dispatch_width() == inst->exec_size);

   const fs_builder ubld = bld.exec_all().group(1, 0);

   const fs_visitor &s = *bld.shader;
   const brw_reg vector_mask = ubld.vgrf(BRW_TYPE_UW);
   ubld.UNDEF(vector_mask);
   ubld.emit(SHADER_OPCODE_READ_ARCH_REG, vector_mask, retype(brw_sr0_reg(3),
                                                              BRW_TYPE_UD));
   const unsigned subreg = sample_mask_flag_subreg(s);

   ubld.MOV(brw_flag_subreg(subreg + inst->group / 16), vector_mask);

   if (inst->predicate) {
      assert(inst->predicate == BRW_PREDICATE_NORMAL);
      assert(!inst->predicate_inverse);
      assert(inst->flag_subreg == 0);
      assert(s.devinfo->ver < 20);
      /* Combine the vector mask with the existing predicate by using a
       * vertical predication mode.
       */
      inst->predicate = BRW_PREDICATE_ALIGN1_ALLV;
   } else {
      inst->flag_subreg = subreg;
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->predicate_inverse = false;
   }
}

static void
setup_surface_descriptors(const fs_builder &bld, fs_inst *inst, uint32_t desc,
                          const brw_reg &surface, const brw_reg &surface_handle)
{
   const brw_compiler *compiler = bld.shader->compiler;

   /* We must have exactly one of surface and surface_handle */
   assert((surface.file == BAD_FILE) != (surface_handle.file == BAD_FILE));

   if (surface.file == IMM) {
      inst->desc = desc | (surface.ud & 0xff);
      inst->src[0] = brw_imm_ud(0);
      inst->src[1] = brw_imm_ud(0); /* ex_desc */
   } else if (surface_handle.file != BAD_FILE) {
      /* Bindless surface */
      inst->desc = desc | GFX9_BTI_BINDLESS;
      inst->src[0] = brw_imm_ud(0);

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      inst->src[1] = retype(surface_handle, BRW_TYPE_UD);
      inst->send_ex_bso = compiler->extended_bindless_surface_offset;
   } else {
      inst->desc = desc;
      const fs_builder ubld = bld.exec_all().group(1, 0);
      brw_reg tmp = ubld.vgrf(BRW_TYPE_UD);
      ubld.AND(tmp, surface, brw_imm_ud(0xff));
      inst->src[0] = component(tmp, 0);
      inst->src[1] = brw_imm_ud(0); /* ex_desc */
   }
}

static void
setup_lsc_surface_descriptors(const fs_builder &bld, fs_inst *inst,
                              uint32_t desc, const brw_reg &surface)
{
   const ASSERTED intel_device_info *devinfo = bld.shader->devinfo;
   const brw_compiler *compiler = bld.shader->compiler;

   inst->src[0] = brw_imm_ud(0); /* desc */

   enum lsc_addr_surface_type surf_type = lsc_msg_desc_addr_type(devinfo, desc);
   switch (surf_type) {
   case LSC_ADDR_SURFTYPE_BSS:
      inst->send_ex_bso = compiler->extended_bindless_surface_offset;
      /* fall-through */
   case LSC_ADDR_SURFTYPE_SS:
      assert(surface.file != BAD_FILE);
      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      inst->src[1] = retype(surface, BRW_TYPE_UD);
      break;

   case LSC_ADDR_SURFTYPE_BTI:
      assert(surface.file != BAD_FILE);
      if (surface.file == IMM) {
         inst->src[1] = brw_imm_ud(lsc_bti_ex_desc(devinfo, surface.ud));
      } else {
         const fs_builder ubld = bld.exec_all().group(1, 0);
         brw_reg tmp = ubld.vgrf(BRW_TYPE_UD);
         ubld.SHL(tmp, surface, brw_imm_ud(24));
         inst->src[1] = component(tmp, 0);
      }
      break;

   case LSC_ADDR_SURFTYPE_FLAT:
      inst->src[1] = brw_imm_ud(0);
      break;

   default:
      unreachable("Invalid LSC surface address type");
   }
}

static enum lsc_addr_size
lsc_addr_size_for_type(enum brw_reg_type type)
{
   switch (brw_type_size_bytes(type)) {
   case 2: return LSC_ADDR_SIZE_A16;
   case 4: return LSC_ADDR_SIZE_A32;
   case 8: return LSC_ADDR_SIZE_A64;
   default: unreachable("invalid type size");
   }
}

static void
lower_lsc_memory_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->has_lsc);

   assert(inst->src[MEMORY_LOGICAL_OPCODE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_MODE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_BINDING_TYPE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_COORD_COMPONENTS].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_DATA_SIZE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_FLAGS].file == IMM);

   /* Get the logical send arguments. */
   const enum lsc_opcode op = (lsc_opcode) inst->src[MEMORY_LOGICAL_OPCODE].ud;
   const enum memory_logical_mode mode =
      (enum memory_logical_mode) inst->src[MEMORY_LOGICAL_MODE].ud;
   const enum lsc_addr_surface_type binding_type =
      (enum lsc_addr_surface_type) inst->src[MEMORY_LOGICAL_BINDING_TYPE].ud;
   const brw_reg binding = inst->src[MEMORY_LOGICAL_BINDING];
   const brw_reg addr = inst->src[MEMORY_LOGICAL_ADDRESS];
   const unsigned coord_components =
      inst->src[MEMORY_LOGICAL_COORD_COMPONENTS].ud;
   enum lsc_data_size data_size =
      (enum lsc_data_size) inst->src[MEMORY_LOGICAL_DATA_SIZE].ud;
   const unsigned components = inst->src[MEMORY_LOGICAL_COMPONENTS].ud;
   const enum memory_flags flags =
      (enum memory_flags) inst->src[MEMORY_LOGICAL_FLAGS].ud;
   const bool transpose = flags & MEMORY_FLAG_TRANSPOSE;
   const bool include_helpers = flags & MEMORY_FLAG_INCLUDE_HELPERS;
   const brw_reg data0 = inst->src[MEMORY_LOGICAL_DATA0];
   const brw_reg data1 = inst->src[MEMORY_LOGICAL_DATA1];
   const bool has_side_effects = inst->has_side_effects();

   const uint32_t data_size_B = lsc_data_size_bytes(data_size);
   const enum brw_reg_type data_type =
      brw_type_with_size(data0.type, data_size_B * 8);

   const enum lsc_addr_size addr_size = lsc_addr_size_for_type(addr.type);

   brw_reg payload = addr;

   if (addr.file != VGRF || !addr.is_contiguous()) {
      if (inst->force_writemask_all) {
         const fs_builder dbld = bld.group(bld.shader->dispatch_width, 0);
         payload = dbld.move_to_vgrf(addr, coord_components);
      } else {
         payload = bld.move_to_vgrf(addr, coord_components);
      }
   }

   unsigned ex_mlen = 0;
   brw_reg payload2;
   if (data0.file != BAD_FILE) {
      if (transpose) {
         assert(data1.file == BAD_FILE);

         payload2 = data0;
         ex_mlen = DIV_ROUND_UP(components, 8);
      } else {
         brw_reg data[8];
         unsigned size = 0;

         assert(components < 8);

         for (unsigned i = 0; i < components; i++)
            data[size++] = offset(data0, inst->exec_size, i);

         if (data1.file != BAD_FILE) {
            for (unsigned i = 0; i < components; i++)
               data[size++] = offset(data1, inst->exec_size, i);
         }

         payload2 = bld.vgrf(data0.type, size);
         bld.LOAD_PAYLOAD(payload2, data, size, 0);
         ex_mlen = (size * brw_type_size_bytes(data_type) * inst->exec_size) / REG_SIZE;
      }
   }

   /* Bspec: Atomic instruction -> Cache section:
    *
    *    Atomic messages are always forced to "un-cacheable" in the L1
    *    cache.
    */
   unsigned cache_mode =
      lsc_opcode_is_atomic(op) ? (unsigned) LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
      lsc_opcode_is_store(op)  ? (unsigned) LSC_CACHE(devinfo, STORE, L1STATE_L3MOCS) :
      (unsigned) LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS);

   /* If we're a fragment shader, we have to predicate with the sample mask to
    * avoid helper invocations in instructions with side effects, unless they
    * are explicitly required.  One exception is for scratch writes - even
    * though those have side effects, they represent operations that didn't
    * originally have any.  We want to avoid accessing undefined values from
    * scratch, so we disable helper invocations entirely there.
    *
    * There are also special cases when we actually want to run on helpers
    * (ray queries).
    */
   if (bld.shader->stage == MESA_SHADER_FRAGMENT && !transpose) {
      if (include_helpers)
         emit_predicate_on_vector_mask(bld, inst);
      else if (has_side_effects && mode != MEMORY_MODE_SCRATCH)
         brw_emit_predicate_on_sample_mask(bld, inst);
   }

   switch (mode) {
   case MEMORY_MODE_UNTYPED:
   case MEMORY_MODE_SCRATCH:
      inst->sfid = GFX12_SFID_UGM;
      break;
   case MEMORY_MODE_TYPED:
      inst->sfid = GFX12_SFID_TGM;
      break;
   case MEMORY_MODE_SHARED_LOCAL:
      inst->sfid = GFX12_SFID_SLM;
      break;
   }
   assert(inst->sfid);

   /* Disable LSC data port L1 cache scheme for the TGM load/store for RT
    * shaders. (see HSD 18038444588)
    */
   if (devinfo->ver >= 20 && gl_shader_stage_is_rt(bld.shader->stage) &&
       inst->sfid == GFX12_SFID_TGM &&
       !lsc_opcode_is_atomic(op)) {
      if (lsc_opcode_is_store(op)) {
         cache_mode = (unsigned) LSC_CACHE(devinfo, STORE, L1UC_L3WB);
      } else {
         cache_mode = (unsigned) LSC_CACHE(devinfo, LOAD, L1UC_L3C);
      }
   }

   inst->desc = lsc_msg_desc(devinfo, op, binding_type, addr_size, data_size,
                             lsc_opcode_has_cmask(op) ?
                             (1 << components) - 1 : components,
                             transpose, cache_mode);

   /* Set up extended descriptors, fills src[0] and src[1]. */
   setup_lsc_surface_descriptors(bld, inst, inst->desc, binding);

   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = lsc_msg_addr_len(devinfo, addr_size,
                                 inst->exec_size * coord_components);
   inst->ex_mlen = ex_mlen;
   inst->header_size = 0;
   inst->send_has_side_effects = has_side_effects;
   inst->send_is_volatile = !has_side_effects;

   inst->resize_sources(4);

   /* Finally, the payload */
   inst->src[2] = payload;
   inst->src[3] = payload2;
}

static brw_reg
emit_a64_oword_block_header(const fs_builder &bld, const brw_reg &addr)
{
   const fs_builder ubld = bld.exec_all().group(8, 0);

   assert(brw_type_size_bytes(addr.type) == 8 && addr.stride == 0);

   brw_reg expanded_addr = addr;
   if (addr.file == UNIFORM) {
      /* We can't do stride 1 with the UNIFORM file, it requires stride 0 */
      expanded_addr = ubld.vgrf(BRW_TYPE_UQ);
      expanded_addr.stride = 0;
      ubld.MOV(expanded_addr, retype(addr, BRW_TYPE_UQ));
   }

   brw_reg header = ubld.vgrf(BRW_TYPE_UD);
   ubld.MOV(header, brw_imm_ud(0));

   /* Use a 2-wide MOV to fill out the address */
   brw_reg addr_vec2 = expanded_addr;
   addr_vec2.type = BRW_TYPE_UD;
   addr_vec2.stride = 1;
   ubld.group(2, 0).MOV(header, addr_vec2);

   return header;
}

static void
lower_hdc_memory_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_compiler *compiler = bld.shader->compiler;

   assert(inst->src[MEMORY_LOGICAL_OPCODE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_MODE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_BINDING_TYPE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_COORD_COMPONENTS].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_DATA_SIZE].file == IMM);
   assert(inst->src[MEMORY_LOGICAL_FLAGS].file == IMM);

   /* Get the logical send arguments. */
   const enum lsc_opcode op = (lsc_opcode)inst->src[MEMORY_LOGICAL_OPCODE].ud;
   const enum memory_logical_mode mode =
      (enum memory_logical_mode) inst->src[MEMORY_LOGICAL_MODE].ud;
   enum lsc_addr_surface_type binding_type =
      (enum lsc_addr_surface_type) inst->src[MEMORY_LOGICAL_BINDING_TYPE].ud;
   brw_reg binding = inst->src[MEMORY_LOGICAL_BINDING];
   const brw_reg addr = inst->src[MEMORY_LOGICAL_ADDRESS];
   const unsigned coord_components =
      inst->src[MEMORY_LOGICAL_COORD_COMPONENTS].ud;
   const unsigned alignment = inst->src[MEMORY_LOGICAL_ALIGNMENT].ud;
   const unsigned components = inst->src[MEMORY_LOGICAL_COMPONENTS].ud;
   const enum memory_flags flags =
      (enum memory_flags) inst->src[MEMORY_LOGICAL_FLAGS].ud;
   const bool block = flags & MEMORY_FLAG_TRANSPOSE;
   const bool include_helpers = flags & MEMORY_FLAG_INCLUDE_HELPERS;
   const brw_reg data0 = inst->src[MEMORY_LOGICAL_DATA0];
   const brw_reg data1 = inst->src[MEMORY_LOGICAL_DATA1];
   const bool has_side_effects = inst->has_side_effects();
   const bool has_dest = inst->dst.file != BAD_FILE && !inst->dst.is_null();

   /* Don't predicate scratch writes on the sample mask.  Otherwise,
    * FS helper invocations would load undefined values from scratch memory.
    * And scratch memory load/stores are produced from operations without
    * side-effects, thus they should not have different behavior in the
    * helper invocations.
    */
   bool allow_sample_mask = has_side_effects && mode != MEMORY_MODE_SCRATCH;

   const enum lsc_data_size data_size =
      (enum lsc_data_size) inst->src[MEMORY_LOGICAL_DATA_SIZE].ud;

   /* unpadded data size */
   const uint32_t data_bit_size =
      data_size == LSC_DATA_SIZE_D8U32 ? 8 :
      data_size == LSC_DATA_SIZE_D16U32 ? 16 :
      8 * lsc_data_size_bytes(data_size);

   const bool byte_scattered =
      data_bit_size < 32 || (alignment != 0 && alignment < 4);
   const bool dword_scattered = !byte_scattered && mode == MEMORY_MODE_SCRATCH;
   const bool surface_access = !byte_scattered && !dword_scattered && !block;

   /* SLM block reads must use the 16B-aligned OWord Block Read messages,
    * as the unaligned message doesn't exist for SLM.
    */
   const bool oword_aligned = block && mode == MEMORY_MODE_SHARED_LOCAL;
   assert(!oword_aligned || (alignment % 16) == 0);

   enum lsc_addr_size addr_size = lsc_addr_size_for_type(addr.type);
   unsigned addr_size_B = coord_components * lsc_addr_size_bytes(addr_size);

   brw_reg header;
   fs_builder ubld8 = bld.exec_all().group(8, 0);
   fs_builder ubld1 = ubld8.group(1, 0);
   if (mode == MEMORY_MODE_SCRATCH) {
      header = ubld8.vgrf(BRW_TYPE_UD);
      ubld8.emit(SHADER_OPCODE_SCRATCH_HEADER, header, brw_ud8_grf(0, 0));
   } else if (block) {
      if (addr_size == LSC_ADDR_SIZE_A64) {
         header = emit_a64_oword_block_header(bld, addr);
      } else {
         header = ubld8.vgrf(BRW_TYPE_UD);
         ubld8.MOV(header, brw_imm_ud(0));
         if (oword_aligned)
            ubld1.SHR(component(header, 2), addr, brw_imm_ud(4));
         else
            ubld1.MOV(component(header, 2), addr);
      }
   }

   /* If we're a fragment shader, we have to predicate with the sample mask to
    * avoid helper invocations to avoid helper invocations in instructions
    * with side effects, unless they are explicitly required.
    *
    * There are also special cases when we actually want to run on helpers
    * (ray queries).
    */
   if (bld.shader->stage == MESA_SHADER_FRAGMENT) {
      if (include_helpers)
         emit_predicate_on_vector_mask(bld, inst);
      else if (allow_sample_mask &&
               (header.file == BAD_FILE || !surface_access))
         brw_emit_predicate_on_sample_mask(bld, inst);
   }

   brw_reg payload, payload2;
   unsigned mlen, ex_mlen = 0;

   if (!block) {
      brw_reg data[11];
      unsigned num_sources = 0;
      if (header.file != BAD_FILE)
         data[num_sources++] = header;

      for (unsigned i = 0; i < coord_components; i++)
         data[num_sources++] = offset(addr, inst->exec_size, i);

      if (data0.file != BAD_FILE) {
         for (unsigned i = 0; i < components; i++)
            data[num_sources++] = offset(data0, inst->exec_size, i);
         if (data1.file != BAD_FILE) {
            for (unsigned i = 0; i < components; i++)
               data[num_sources++] = offset(data1, inst->exec_size, i);
         }
      }

      assert(num_sources <= ARRAY_SIZE(data));

      unsigned payload_size_UDs = (header.file != BAD_FILE ? 1 : 0) +
                                  (addr_size_B / 4) +
                                  (lsc_op_num_data_values(op) * components *
                                   lsc_data_size_bytes(data_size) / 4);

      payload = bld.vgrf(BRW_TYPE_UD, payload_size_UDs);
      fs_inst *load_payload =
         emit_load_payload_with_padding(bld, payload, data, num_sources,
                                        header.file != BAD_FILE ? 1 : 0,
                                        REG_SIZE);
      mlen = load_payload->size_written / REG_SIZE;
   } else {
      assert(data1.file == BAD_FILE);

      payload = header;
      mlen = 1;

      if (data0.file != BAD_FILE) {
         payload2 = bld.move_to_vgrf(data0, components);
         ex_mlen = components * sizeof(uint32_t) / REG_SIZE;
      }
   }


   if (mode == MEMORY_MODE_SHARED_LOCAL) {
      binding_type = LSC_ADDR_SURFTYPE_BTI;
      binding = brw_imm_ud(GFX7_BTI_SLM);
   } else if (mode == MEMORY_MODE_SCRATCH) {
      binding_type = LSC_ADDR_SURFTYPE_BTI;
      binding = brw_imm_ud(GFX8_BTI_STATELESS_NON_COHERENT);
   }

   uint32_t sfid, desc;
   if (mode == MEMORY_MODE_TYPED) {
      assert(addr_size == LSC_ADDR_SIZE_A32);
      assert(!block);

      sfid = HSW_SFID_DATAPORT_DATA_CACHE_1;

      if (lsc_opcode_is_atomic(op)) {
         desc = brw_dp_typed_atomic_desc(devinfo, inst->exec_size, inst->group,
                                         lsc_op_to_legacy_atomic(op),
                                         has_dest);
      } else {
         desc = brw_dp_typed_surface_rw_desc(devinfo, inst->exec_size,
                                             inst->group, components, !has_dest);
      }
   } else if (addr_size == LSC_ADDR_SIZE_A64) {
      assert(binding_type == LSC_ADDR_SURFTYPE_FLAT);
      assert(!dword_scattered);

      sfid = HSW_SFID_DATAPORT_DATA_CACHE_1;

      if (lsc_opcode_is_atomic(op)) {
         unsigned aop = lsc_op_to_legacy_atomic(op);
         if (lsc_opcode_is_atomic_float(op)) {
            desc = brw_dp_a64_untyped_atomic_float_desc(devinfo, inst->exec_size,
                                                        data_bit_size, aop,
                                                        has_dest);
         } else {
            desc = brw_dp_a64_untyped_atomic_desc(devinfo, inst->exec_size,
                                                  data_bit_size, aop,
                                                  has_dest);
         }
      } else if (block) {
         desc = brw_dp_a64_oword_block_rw_desc(devinfo, oword_aligned,
                                               components, !has_dest);
      } else if (byte_scattered) {
         desc = brw_dp_a64_byte_scattered_rw_desc(devinfo, inst->exec_size,
                                                  data_bit_size, !has_dest);
      } else {
         desc = brw_dp_a64_untyped_surface_rw_desc(devinfo, inst->exec_size,
                                                   components, !has_dest);
      }
   } else {
      assert(binding_type != LSC_ADDR_SURFTYPE_FLAT);

      sfid = surface_access ? HSW_SFID_DATAPORT_DATA_CACHE_1
                            : GFX7_SFID_DATAPORT_DATA_CACHE;

      if (lsc_opcode_is_atomic(op)) {
         unsigned aop = lsc_op_to_legacy_atomic(op);
         if (lsc_opcode_is_atomic_float(op)) {
            desc = brw_dp_untyped_atomic_float_desc(devinfo, inst->exec_size,
                                                    aop, has_dest);
         } else {
            desc = brw_dp_untyped_atomic_desc(devinfo, inst->exec_size,
                                              aop, has_dest);
         }
      } else if (block) {
         desc = brw_dp_oword_block_rw_desc(devinfo, oword_aligned,
                                           components, !has_dest);
      } else if (byte_scattered) {
         desc = brw_dp_byte_scattered_rw_desc(devinfo, inst->exec_size,
                                              data_bit_size, !has_dest);
      } else if (dword_scattered) {
         desc = brw_dp_dword_scattered_rw_desc(devinfo, inst->exec_size,
                                               !has_dest);
      } else {
         desc = brw_dp_untyped_surface_rw_desc(devinfo, inst->exec_size,
                                               components, !has_dest);
      }
   }

   assert(sfid);

   /* Update the original instruction. */
   inst->opcode = SHADER_OPCODE_SEND;
   inst->sfid = sfid;
   inst->mlen = mlen;
   inst->ex_mlen = ex_mlen;
   inst->header_size = header.file != BAD_FILE ? 1 : 0;
   inst->send_has_side_effects = has_side_effects;
   inst->send_is_volatile = !has_side_effects;

   if (block) {
      assert(inst->force_writemask_all);
      inst->exec_size = components > 8 ? 16 : 8;
   }

   inst->resize_sources(4);

   /* Set up descriptors */
   switch (binding_type) {
   case LSC_ADDR_SURFTYPE_FLAT:
      inst->src[0] = brw_imm_ud(0);
      inst->src[1] = brw_imm_ud(0);
      break;
   case LSC_ADDR_SURFTYPE_BSS:
      inst->send_ex_bso = compiler->extended_bindless_surface_offset;
      /* fall-through */
   case LSC_ADDR_SURFTYPE_SS:
      desc |= GFX9_BTI_BINDLESS;

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      inst->src[0] = brw_imm_ud(0);
      inst->src[1] = binding;
      break;
   case LSC_ADDR_SURFTYPE_BTI:
      if (binding.file == IMM) {
         desc |= binding.ud & 0xff;
         inst->src[0] = brw_imm_ud(0);
         inst->src[1] = brw_imm_ud(0);
      } else {
         brw_reg tmp = ubld1.vgrf(BRW_TYPE_UD);
         ubld1.AND(tmp, binding, brw_imm_ud(0xff));
         inst->src[0] = component(tmp, 0);
         inst->src[1] = brw_imm_ud(0);
      }
      break;
   default:
      unreachable("Unknown surface type");
   }

   inst->desc = desc;

   /* Finally, the payloads */
   inst->src[2] = payload;
   inst->src[3] = payload2;
}

static void
lower_lsc_varying_pull_constant_logical_send(const fs_builder &bld,
                                             fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   ASSERTED const brw_compiler *compiler = bld.shader->compiler;

   brw_reg surface        = inst->src[PULL_VARYING_CONSTANT_SRC_SURFACE];
   brw_reg surface_handle = inst->src[PULL_VARYING_CONSTANT_SRC_SURFACE_HANDLE];
   brw_reg offset_B       = inst->src[PULL_VARYING_CONSTANT_SRC_OFFSET];
   brw_reg alignment_B    = inst->src[PULL_VARYING_CONSTANT_SRC_ALIGNMENT];

   /* We are switching the instruction from an ALU-like instruction to a
    * send-from-grf instruction.  Since sends can't handle strides or
    * source modifiers, we have to make a copy of the offset source.
    */
   brw_reg ubo_offset = bld.move_to_vgrf(offset_B, 1);

   enum lsc_addr_surface_type surf_type =
      surface_handle.file == BAD_FILE ?
      LSC_ADDR_SURFTYPE_BTI : LSC_ADDR_SURFTYPE_BSS;

   assert(alignment_B.file == IMM);
   unsigned alignment = alignment_B.ud;

   inst->opcode = SHADER_OPCODE_SEND;
   inst->sfid = GFX12_SFID_UGM;
   inst->resize_sources(3);
   inst->send_ex_bso = surf_type == LSC_ADDR_SURFTYPE_BSS &&
                       compiler->extended_bindless_surface_offset;

   assert(!compiler->indirect_ubos_use_sampler);

   inst->src[0] = brw_imm_ud(0);
   inst->src[2] = ubo_offset; /* payload */

   if (alignment >= 4) {
      inst->desc =
         lsc_msg_desc(devinfo, LSC_OP_LOAD,
                      surf_type, LSC_ADDR_SIZE_A32,
                      LSC_DATA_SIZE_D32,
                      4 /* num_channels */,
                      false /* transpose */,
                      LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));
      inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, inst->exec_size);

      setup_lsc_surface_descriptors(bld, inst, inst->desc,
                                    surface.file != BAD_FILE ?
                                    surface : surface_handle);
   } else {
      inst->desc =
         lsc_msg_desc(devinfo, LSC_OP_LOAD,
                      surf_type, LSC_ADDR_SIZE_A32,
                      LSC_DATA_SIZE_D32,
                      1 /* num_channels */,
                      false /* transpose */,
                      LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));
      inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, inst->exec_size);

      setup_lsc_surface_descriptors(bld, inst, inst->desc,
                                    surface.file != BAD_FILE ?
                                    surface : surface_handle);

      /* The byte scattered messages can only read one dword at a time so
       * we have to duplicate the message 4 times to read the full vec4.
       * Hopefully, dead code will clean up the mess if some of them aren't
       * needed.
       */
      assert(inst->size_written == 16 * inst->exec_size);
      inst->size_written /= 4;
      for (unsigned c = 1; c < 4; c++) {
         /* Emit a copy of the instruction because we're about to modify
          * it.  Because this loop starts at 1, we will emit copies for the
          * first 3 and the final one will be the modified instruction.
          */
         bld.emit(*inst);

         /* Offset the source */
         inst->src[2] = bld.vgrf(BRW_TYPE_UD);
         bld.ADD(inst->src[2], ubo_offset, brw_imm_ud(c * 4));

         /* Offset the destination */
         inst->dst = offset(inst->dst, bld, 1);
      }
   }
}

static void
lower_varying_pull_constant_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_compiler *compiler = bld.shader->compiler;

   brw_reg surface = inst->src[PULL_VARYING_CONSTANT_SRC_SURFACE];
   brw_reg surface_handle = inst->src[PULL_VARYING_CONSTANT_SRC_SURFACE_HANDLE];
   brw_reg offset_B = inst->src[PULL_VARYING_CONSTANT_SRC_OFFSET];

   /* We are switching the instruction from an ALU-like instruction to a
    * send-from-grf instruction.  Since sends can't handle strides or
    * source modifiers, we have to make a copy of the offset source.
    */
   brw_reg ubo_offset = bld.vgrf(BRW_TYPE_UD);
   bld.MOV(ubo_offset, offset_B);

   assert(inst->src[PULL_VARYING_CONSTANT_SRC_ALIGNMENT].file == IMM);
   unsigned alignment = inst->src[PULL_VARYING_CONSTANT_SRC_ALIGNMENT].ud;

   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = inst->exec_size / 8;
   inst->resize_sources(3);

   /* src[0] & src[1] are filled by setup_surface_descriptors() */
   inst->src[2] = ubo_offset; /* payload */

   if (compiler->indirect_ubos_use_sampler) {
      const unsigned simd_mode =
         inst->exec_size <= 8 ? BRW_SAMPLER_SIMD_MODE_SIMD8 :
                                BRW_SAMPLER_SIMD_MODE_SIMD16;
      const uint32_t desc = brw_sampler_desc(devinfo, 0, 0,
                                             GFX5_SAMPLER_MESSAGE_SAMPLE_LD,
                                             simd_mode, 0);

      inst->sfid = BRW_SFID_SAMPLER;
      setup_surface_descriptors(bld, inst, desc, surface, surface_handle);
   } else if (alignment >= 4) {
      const uint32_t desc =
         brw_dp_untyped_surface_rw_desc(devinfo, inst->exec_size,
                                        4, /* num_channels */
                                        false   /* write */);

      inst->sfid = HSW_SFID_DATAPORT_DATA_CACHE_1;
      setup_surface_descriptors(bld, inst, desc, surface, surface_handle);
   } else {
      const uint32_t desc =
         brw_dp_byte_scattered_rw_desc(devinfo, inst->exec_size,
                                       32,     /* bit_size */
                                       false   /* write */);

      inst->sfid = GFX7_SFID_DATAPORT_DATA_CACHE;
      setup_surface_descriptors(bld, inst, desc, surface, surface_handle);

      /* The byte scattered messages can only read one dword at a time so
       * we have to duplicate the message 4 times to read the full vec4.
       * Hopefully, dead code will clean up the mess if some of them aren't
       * needed.
       */
      assert(inst->size_written == 16 * inst->exec_size);
      inst->size_written /= 4;
      for (unsigned c = 1; c < 4; c++) {
         /* Emit a copy of the instruction because we're about to modify
          * it.  Because this loop starts at 1, we will emit copies for the
          * first 3 and the final one will be the modified instruction.
          */
         bld.emit(*inst);

         /* Offset the source */
         inst->src[2] = bld.vgrf(BRW_TYPE_UD);
         bld.ADD(inst->src[2], ubo_offset, brw_imm_ud(c * 4));

         /* Offset the destination */
         inst->dst = offset(inst->dst, bld, 1);
      }
   }
}

static void
lower_interpolator_logical_send(const fs_builder &bld, fs_inst *inst,
                                const struct brw_wm_prog_key *wm_prog_key,
                                const struct brw_wm_prog_data *wm_prog_data)
{
   const intel_device_info *devinfo = bld.shader->devinfo;

   /* We have to send something */
   brw_reg payload = brw_vec8_grf(0, 0);
   unsigned mlen = 1;

   unsigned mode;
   switch (inst->opcode) {
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
      assert(inst->src[INTERP_SRC_OFFSET].file == BAD_FILE);
      mode = GFX7_PIXEL_INTERPOLATOR_LOC_SAMPLE;
      break;

   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
      assert(inst->src[INTERP_SRC_OFFSET].file == BAD_FILE);
      mode = GFX7_PIXEL_INTERPOLATOR_LOC_SHARED_OFFSET;
      break;

   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      payload = inst->src[INTERP_SRC_OFFSET];
      mlen = 2 * inst->exec_size / 8;
      mode = GFX7_PIXEL_INTERPOLATOR_LOC_PER_SLOT_OFFSET;
      break;

   default:
      unreachable("Invalid interpolator instruction");
   }

   const bool dynamic_mode =
      inst->src[INTERP_SRC_DYNAMIC_MODE].file != BAD_FILE;

   brw_reg desc = inst->src[INTERP_SRC_MSG_DESC];
   uint32_t desc_imm =
      brw_pixel_interp_desc(devinfo,
                            /* Leave the mode at 0 if persample_dispatch is
                             * dynamic, it will be ORed in below.
                             */
                            dynamic_mode ? 0 : mode,
                            inst->pi_noperspective,
                            false /* coarse_pixel_rate */,
                            inst->exec_size, inst->group);

   if (wm_prog_data->coarse_pixel_dispatch == BRW_ALWAYS) {
      desc_imm |= (1 << 15);
   } else if (wm_prog_data->coarse_pixel_dispatch == BRW_SOMETIMES) {
      STATIC_ASSERT(INTEL_MSAA_FLAG_COARSE_PI_MSG == (1 << 15));
      brw_reg orig_desc = desc;
      const fs_builder &ubld = bld.exec_all().group(8, 0);
      desc = ubld.vgrf(BRW_TYPE_UD);
      ubld.AND(desc, dynamic_msaa_flags(wm_prog_data),
               brw_imm_ud(INTEL_MSAA_FLAG_COARSE_PI_MSG));

      /* And, if it's AT_OFFSET, we might have a non-trivial descriptor */
      if (orig_desc.file == IMM) {
         desc_imm |= orig_desc.ud;
      } else {
         ubld.OR(desc, desc, orig_desc);
      }
   }

   /* If persample_dispatch is dynamic, select the interpolation mode
    * dynamically and OR into the descriptor to complete the static part
    * generated by brw_pixel_interp_desc().
    *
    * Why does this work? If you look at the SKL PRMs, Volume 7:
    * 3D-Media-GPGPU, Shared Functions Pixel Interpolater, you'll see that
    *
    *   - "Per Message Offset” Message Descriptor
    *   - “Sample Position Offset” Message Descriptor
    *
    * have different formats. Fortunately, a fragment shader dispatched at
    * pixel rate, will have gl_SampleID = 0 & gl_NumSamples = 1. So the value
    * we pack in “Sample Position Offset” will be a 0 and will cover the X/Y
    * components of "Per Message Offset”, which will give us the pixel offset 0x0.
    */
   if (dynamic_mode) {
      brw_reg orig_desc = desc;
      const fs_builder &ubld = bld.exec_all().group(8, 0);
      desc = ubld.vgrf(BRW_TYPE_UD);

      /* The predicate should have been built in brw_fs_nir.cpp when emitting
       * NIR code. This guarantees that we do not have incorrect interactions
       * with the flag register holding the predication result.
       */
      if (orig_desc.file == IMM) {
         /* Not using SEL here because we would generate an instruction with 2
          * immediate sources which is not supported by HW.
          */
         set_predicate_inv(BRW_PREDICATE_NORMAL, false,
                           ubld.MOV(desc, brw_imm_ud(orig_desc.ud |
                                                     GFX7_PIXEL_INTERPOLATOR_LOC_SAMPLE << 12)));
         set_predicate_inv(BRW_PREDICATE_NORMAL, true,
                           ubld.MOV(desc, brw_imm_ud(orig_desc.ud |
                                                     GFX7_PIXEL_INTERPOLATOR_LOC_SHARED_OFFSET << 12)));
      } else {
         set_predicate_inv(BRW_PREDICATE_NORMAL, false,
                           ubld.OR(desc, orig_desc,
                                   brw_imm_ud(GFX7_PIXEL_INTERPOLATOR_LOC_SAMPLE << 12)));
         set_predicate_inv(BRW_PREDICATE_NORMAL, true,
                           ubld.OR(desc, orig_desc,
                                   brw_imm_ud(GFX7_PIXEL_INTERPOLATOR_LOC_SHARED_OFFSET << 12)));
      }
   }

   inst->opcode = SHADER_OPCODE_SEND;
   inst->sfid = GFX7_SFID_PIXEL_INTERPOLATOR;
   inst->desc = desc_imm;
   inst->ex_desc = 0;
   inst->mlen = mlen;
   inst->ex_mlen = 0;
   inst->send_has_side_effects = false;
   inst->send_is_volatile = false;

   inst->resize_sources(3);
   inst->src[0] = component(desc, 0);
   inst->src[1] = brw_imm_ud(0); /* ex_desc */
   inst->src[2] = payload;
}

static void
lower_btd_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   brw_reg global_addr = inst->src[0];
   const brw_reg btd_record = inst->src[1];

   const unsigned unit = reg_unit(devinfo);
   const unsigned mlen = 2 * unit;
   const fs_builder ubld = bld.exec_all();
   brw_reg header = ubld.vgrf(BRW_TYPE_UD, 2 * unit);

   ubld.MOV(header, brw_imm_ud(0));
   switch (inst->opcode) {
   case SHADER_OPCODE_BTD_SPAWN_LOGICAL:
      assert(brw_type_size_bytes(global_addr.type) == 8 &&
             global_addr.stride == 0);
      global_addr.type = BRW_TYPE_UD;
      global_addr.stride = 1;
      ubld.group(2, 0).MOV(header, global_addr);
      break;

   case SHADER_OPCODE_BTD_RETIRE_LOGICAL:
      /* The bottom bit is the Stack ID release bit */
      ubld.group(1, 0).MOV(header, brw_imm_ud(1));
      break;

   default:
      unreachable("Invalid BTD message");
   }

   /* Stack IDs are always in R1 regardless of whether we're coming from a
    * bindless shader or a regular compute shader.
    */
   brw_reg stack_ids = retype(offset(header, bld, 1), BRW_TYPE_UW);
   bld.exec_all().MOV(stack_ids, retype(brw_vec8_grf(1 * unit, 0),
                                        BRW_TYPE_UW));

   unsigned ex_mlen = 0;
   brw_reg payload;
   if (inst->opcode == SHADER_OPCODE_BTD_SPAWN_LOGICAL) {
      ex_mlen = 2 * (inst->exec_size / 8);
      payload = bld.move_to_vgrf(btd_record, 1);
   } else {
      assert(inst->opcode == SHADER_OPCODE_BTD_RETIRE_LOGICAL);
      /* All these messages take a BTD and things complain if we don't provide
       * one for RETIRE.  However, it shouldn't ever actually get used so fill
       * it with zero.
       */
      ex_mlen = 2 * (inst->exec_size / 8);
      payload = bld.move_to_vgrf(brw_imm_uq(0), 1);
   }

   /* Update the original instruction. */
   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = mlen;
   inst->ex_mlen = ex_mlen;
   inst->header_size = 0; /* HW docs require has_header = false */
   inst->send_has_side_effects = true;
   inst->send_is_volatile = false;

   /* Set up SFID and descriptors */
   inst->sfid = GEN_RT_SFID_BINDLESS_THREAD_DISPATCH;
   inst->desc = brw_btd_spawn_desc(devinfo, inst->exec_size,
                                   GEN_RT_BTD_MESSAGE_SPAWN);
   inst->resize_sources(4);
   inst->src[0] = brw_imm_ud(0); /* desc */
   inst->src[1] = brw_imm_ud(0); /* ex_desc */
   inst->src[2] = header;
   inst->src[3] = payload;
}

static void
lower_trace_ray_logical_send(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   /* The emit_uniformize() in brw_fs_nir.cpp will generate an horizontal
    * stride of 0. Below we're doing a MOV() in SIMD2. Since we can't use UQ/Q
    * types in on Gfx12.5, we need to tweak the stride with a value of 1 dword
    * so that the MOV operates on 2 components rather than twice the same
    * component.
    */
   brw_reg globals_addr = retype(inst->src[RT_LOGICAL_SRC_GLOBALS], BRW_TYPE_UD);
   globals_addr.stride = 1;
   const brw_reg bvh_level =
      inst->src[RT_LOGICAL_SRC_BVH_LEVEL].file == IMM ?
      inst->src[RT_LOGICAL_SRC_BVH_LEVEL] :
      bld.move_to_vgrf(inst->src[RT_LOGICAL_SRC_BVH_LEVEL],
                       inst->components_read(RT_LOGICAL_SRC_BVH_LEVEL));
   const brw_reg trace_ray_control =
      inst->src[RT_LOGICAL_SRC_TRACE_RAY_CONTROL].file == IMM ?
      inst->src[RT_LOGICAL_SRC_TRACE_RAY_CONTROL] :
      bld.move_to_vgrf(inst->src[RT_LOGICAL_SRC_TRACE_RAY_CONTROL],
                       inst->components_read(RT_LOGICAL_SRC_TRACE_RAY_CONTROL));
   const brw_reg synchronous_src = inst->src[RT_LOGICAL_SRC_SYNCHRONOUS];
   assert(synchronous_src.file == IMM);
   const bool synchronous = synchronous_src.ud;

   const unsigned unit = reg_unit(devinfo);
   const unsigned mlen = unit;
   const fs_builder ubld = bld.exec_all();
   brw_reg header = ubld.vgrf(BRW_TYPE_UD);
   ubld.MOV(header, brw_imm_ud(0));
   ubld.group(2, 0).MOV(header, globals_addr);
   if (synchronous)
      ubld.group(1, 0).MOV(byte_offset(header, 16), brw_imm_ud(synchronous));

   const unsigned ex_mlen = inst->exec_size / 8;
   brw_reg payload = bld.vgrf(BRW_TYPE_UD);
   if (bvh_level.file == IMM &&
       trace_ray_control.file == IMM) {
      uint32_t high = devinfo->ver >= 20 ? 10 : 9;
      bld.MOV(payload, brw_imm_ud(SET_BITS(trace_ray_control.ud, high, 8) |
                                  (bvh_level.ud & 0x7)));
   } else {
      bld.SHL(payload, trace_ray_control, brw_imm_ud(8));
      bld.OR(payload, payload, bvh_level);
   }

   /* When doing synchronous traversal, the HW implicitly computes the
    * stack_id using the following formula :
    *
    *    EUID[3:0] & THREAD_ID[2:0] & SIMD_LANE_ID[3:0]
    *
    * Only in the asynchronous case we need to set the stack_id given from the
    * payload register.
    */
   if (!synchronous) {
      bld.AND(subscript(payload, BRW_TYPE_UW, 1),
              retype(brw_vec8_grf(1 * unit, 0), BRW_TYPE_UW),
              brw_imm_uw(0x7ff));
   }

   /* Update the original instruction. */
   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = mlen;
   inst->ex_mlen = ex_mlen;
   inst->header_size = 0; /* HW docs require has_header = false */
   inst->send_has_side_effects = true;
   inst->send_is_volatile = false;

   /* Set up SFID and descriptors */
   inst->sfid = GEN_RT_SFID_RAY_TRACE_ACCELERATOR;
   inst->desc = brw_rt_trace_ray_desc(devinfo, inst->exec_size);
   inst->resize_sources(4);
   inst->src[0] = brw_imm_ud(0); /* desc */
   inst->src[1] = brw_imm_ud(0); /* ex_desc */
   inst->src[2] = header;
   inst->src[3] = payload;
}

static void
lower_get_buffer_size(const fs_builder &bld, fs_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   /* Since we can only execute this instruction on uniform bti/surface
    * handles, brw_fs_nir.cpp should already have limited this to SIMD8.
    */
   assert(inst->exec_size == (devinfo->ver < 20 ? 8 : 16));

   brw_reg surface = inst->src[GET_BUFFER_SIZE_SRC_SURFACE];
   brw_reg surface_handle = inst->src[GET_BUFFER_SIZE_SRC_SURFACE_HANDLE];
   brw_reg lod = inst->src[GET_BUFFER_SIZE_SRC_LOD];

   inst->opcode = SHADER_OPCODE_SEND;
   inst->mlen = inst->exec_size / 8;
   inst->resize_sources(3);
   inst->ex_mlen = 0;
   inst->ex_desc = 0;

   /* src[0] & src[1] are filled by setup_surface_descriptors() */
   inst->src[2] = lod;

   const uint32_t return_format = GFX8_SAMPLER_RETURN_FORMAT_32BITS;

   const uint32_t desc = brw_sampler_desc(devinfo, 0, 0,
                                          GFX5_SAMPLER_MESSAGE_SAMPLE_RESINFO,
                                          BRW_SAMPLER_SIMD_MODE_SIMD8,
                                          return_format);

   inst->dst = retype(inst->dst, BRW_TYPE_UW);
   inst->sfid = BRW_SFID_SAMPLER;
   setup_surface_descriptors(bld, inst, desc, surface, surface_handle);
}

bool
brw_fs_lower_logical_sends(fs_visitor &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe(block, fs_inst, inst, s.cfg) {
      const fs_builder ibld(&s, block, inst);

      switch (inst->opcode) {
      case FS_OPCODE_FB_WRITE_LOGICAL:
         assert(s.stage == MESA_SHADER_FRAGMENT);
         lower_fb_write_logical_send(ibld, inst,
                                     brw_wm_prog_data(s.prog_data),
                                     (const brw_wm_prog_key *)s.key,
                                     s.fs_payload());
         break;

      case FS_OPCODE_FB_READ_LOGICAL:
         lower_fb_read_logical_send(ibld, inst, brw_wm_prog_data(s.prog_data));
         break;

      case SHADER_OPCODE_TEX_LOGICAL:
      case SHADER_OPCODE_TXD_LOGICAL:
      case SHADER_OPCODE_TXF_LOGICAL:
      case SHADER_OPCODE_TXL_LOGICAL:
      case SHADER_OPCODE_TXS_LOGICAL:
      case SHADER_OPCODE_IMAGE_SIZE_LOGICAL:
      case FS_OPCODE_TXB_LOGICAL:
      case SHADER_OPCODE_TXF_CMS_W_LOGICAL:
      case SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL:
      case SHADER_OPCODE_TXF_MCS_LOGICAL:
      case SHADER_OPCODE_LOD_LOGICAL:
      case SHADER_OPCODE_TG4_LOGICAL:
      case SHADER_OPCODE_TG4_BIAS_LOGICAL:
      case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
      case SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL:
      case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
      case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
      case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
      case SHADER_OPCODE_SAMPLEINFO_LOGICAL:
         lower_sampler_logical_send(ibld, inst);
         break;

      case SHADER_OPCODE_GET_BUFFER_SIZE:
         lower_get_buffer_size(ibld, inst);
         break;

      case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
      case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
      case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
         if (devinfo->ver >= 20 ||
             (devinfo->has_lsc &&
              inst->src[MEMORY_LOGICAL_MODE].ud != MEMORY_MODE_TYPED))
            lower_lsc_memory_logical_send(ibld, inst);
         else
            lower_hdc_memory_logical_send(ibld, inst);
         break;

      case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL:
         if (devinfo->has_lsc && !s.compiler->indirect_ubos_use_sampler)
            lower_lsc_varying_pull_constant_logical_send(ibld, inst);
         else
            lower_varying_pull_constant_logical_send(ibld, inst);
         break;

      case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
      case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
      case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
         lower_interpolator_logical_send(ibld, inst,
                                         (const brw_wm_prog_key *)s.key,
                                         brw_wm_prog_data(s.prog_data));
         break;

      case SHADER_OPCODE_BTD_SPAWN_LOGICAL:
      case SHADER_OPCODE_BTD_RETIRE_LOGICAL:
         lower_btd_logical_send(ibld, inst);
         break;

      case RT_OPCODE_TRACE_RAY_LOGICAL:
         lower_trace_ray_logical_send(ibld, inst);
         break;

      case SHADER_OPCODE_URB_READ_LOGICAL:
         if (devinfo->ver < 20)
            lower_urb_read_logical_send(ibld, inst);
         else
            lower_urb_read_logical_send_xe2(ibld, inst);
         break;

      case SHADER_OPCODE_URB_WRITE_LOGICAL:
         if (devinfo->ver < 20)
            lower_urb_write_logical_send(ibld, inst);
         else
            lower_urb_write_logical_send_xe2(ibld, inst);

         break;

      default:
         continue;
      }

      progress = true;
   }

   if (progress)
      s.invalidate_analysis(DEPENDENCY_INSTRUCTIONS | DEPENDENCY_VARIABLES);

   return progress;
}

/**
 * Turns the generic expression-style uniform pull constant load instruction
 * into a hardware-specific series of instructions for loading a pull
 * constant.
 *
 * The expression style allows the CSE pass before this to optimize out
 * repeated loads from the same offset, and gives the pre-register-allocation
 * scheduling full flexibility, while the conversion to native instructions
 * allows the post-register-allocation scheduler the best information
 * possible.
 *
 * Note that execution masking for setting up pull constant loads is special:
 * the channels that need to be written are unrelated to the current execution
 * mask, since a later instruction will use one of the result channels as a
 * source operand for all 8 or 16 of its channels.
 */
bool
brw_fs_lower_uniform_pull_constant_loads(fs_visitor &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst (block, fs_inst, inst, s.cfg) {
      if (inst->opcode != FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD)
         continue;

      const brw_reg surface = inst->src[PULL_UNIFORM_CONSTANT_SRC_SURFACE];
      const brw_reg surface_handle = inst->src[PULL_UNIFORM_CONSTANT_SRC_SURFACE_HANDLE];
      const brw_reg offset_B = inst->src[PULL_UNIFORM_CONSTANT_SRC_OFFSET];
      const brw_reg size_B = inst->src[PULL_UNIFORM_CONSTANT_SRC_SIZE];
      assert(surface.file == BAD_FILE || surface_handle.file == BAD_FILE);
      assert(offset_B.file == IMM);
      assert(size_B.file == IMM);

      if (devinfo->has_lsc) {
         const fs_builder ubld =
            fs_builder(&s, block, inst).group(8, 0).exec_all();

         const brw_reg payload = ubld.vgrf(BRW_TYPE_UD);
         ubld.MOV(payload, offset_B);

         inst->sfid = GFX12_SFID_UGM;
         inst->desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                                   surface_handle.file == BAD_FILE ?
                                   LSC_ADDR_SURFTYPE_BTI :
                                   LSC_ADDR_SURFTYPE_BSS,
                                   LSC_ADDR_SIZE_A32,
                                   LSC_DATA_SIZE_D32,
                                   inst->size_written / 4,
                                   true /* transpose */,
                                   LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));

         /* Update the original instruction. */
         inst->opcode = SHADER_OPCODE_SEND;
         inst->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, 1);
         inst->send_ex_bso = surface_handle.file != BAD_FILE &&
                             s.compiler->extended_bindless_surface_offset;
         inst->ex_mlen = 0;
         inst->header_size = 0;
         inst->send_has_side_effects = false;
         inst->send_is_volatile = true;
         inst->exec_size = 1;

         /* Finally, the payload */

         inst->resize_sources(3);
         setup_lsc_surface_descriptors(ubld, inst, inst->desc,
                                       surface.file != BAD_FILE ?
                                       surface : surface_handle);
         inst->src[2] = payload;

         s.invalidate_analysis(DEPENDENCY_INSTRUCTIONS | DEPENDENCY_VARIABLES);
      } else {
         const fs_builder ubld = fs_builder(&s, block, inst).exec_all();
         brw_reg header = fs_builder(&s, 8).exec_all().vgrf(BRW_TYPE_UD);

         ubld.group(8, 0).MOV(header,
                              retype(brw_vec8_grf(0, 0), BRW_TYPE_UD));
         ubld.group(1, 0).MOV(component(header, 2),
                              brw_imm_ud(offset_B.ud / 16));

         inst->sfid = GFX6_SFID_DATAPORT_CONSTANT_CACHE;
         inst->opcode = SHADER_OPCODE_SEND;
         inst->header_size = 1;
         inst->mlen = 1;

         uint32_t desc =
            brw_dp_oword_block_rw_desc(devinfo, true /* align_16B */,
                                       size_B.ud / 4, false /* write */);

         inst->resize_sources(4);

         setup_surface_descriptors(ubld, inst, desc, surface, surface_handle);

         inst->src[2] = header;
         inst->src[3] = brw_reg(); /* unused for reads */

         s.invalidate_analysis(DEPENDENCY_INSTRUCTIONS | DEPENDENCY_VARIABLES);
      }

      progress = true;
   }

   return progress;
}
