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
#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_sampler.h"

#include "util/bitpack_helpers.h"

static inline brw_send_inst *
brw_transform_inst_to_send(const brw_builder &bld, brw_inst *inst)
{
   return brw_transform_inst(bld, inst, SHADER_OPCODE_SEND)->as_send();
}

static void
lower_urb_read_logical_send(const brw_builder &bld, brw_urb_inst *urb)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const bool per_slot_present =
      urb->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS].file != BAD_FILE;

   assert(urb->size_written % REG_SIZE == 0);

   brw_reg payload_sources[2];
   unsigned header_size = 0;
   payload_sources[header_size++] = urb->src[URB_LOGICAL_SRC_HANDLE];
   if (per_slot_present)
      payload_sources[header_size++] = urb->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];

   brw_reg payload = retype(brw_allocate_vgrf_units(*bld.shader, header_size), BRW_TYPE_F);
   bld.LOAD_PAYLOAD(payload, payload_sources, header_size, header_size);

   const uint32_t offset = urb->offset;

   brw_send_inst *send = brw_transform_inst_to_send(bld, urb);
   urb = NULL;

   send->header_size = header_size;

   send->sfid = BRW_SFID_URB;
   send->desc = brw_urb_desc(devinfo,
                             GFX8_URB_OPCODE_SIMD8_READ,
                             per_slot_present,
                             false,
                             offset);

   send->mlen = header_size;
   send->ex_desc = 0;
   send->ex_mlen = 0;
   send->is_volatile = true;

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
}

static void
lower_urb_read_logical_send_xe2(const brw_builder &bld, brw_urb_inst *urb)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->has_lsc);

   assert(urb->size_written % (REG_SIZE * reg_unit(devinfo)) == 0);

   /* Get the logical send arguments. */
   const brw_reg handle = urb->src[URB_LOGICAL_SRC_HANDLE];

   /* Calculate the total number of components of the payload. */
   const unsigned dst_comps = urb->size_written / (REG_SIZE * reg_unit(devinfo));

   brw_reg payload = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(payload, handle);

   /* The low 24-bits of the URB handle is a byte offset into the URB area.
    * Add the (OWord) offset of the write to this value.
    */
   if (urb->offset) {
      bld.ADD(payload, payload, brw_imm_ud(urb->offset));
      urb->offset = 0;
   }

   brw_reg offsets = urb->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];
   if (offsets.file != BAD_FILE) {
      bld.ADD(payload, payload, offsets);
   }

   brw_send_inst *send = brw_transform_inst_to_send(bld, urb);
   urb = NULL;

   send->sfid = BRW_SFID_URB;

   assert((dst_comps >= 1 && dst_comps <= 4) || dst_comps == 8);

   send->desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                             LSC_ADDR_SURFTYPE_FLAT, LSC_ADDR_SIZE_A32,
                             LSC_DATA_SIZE_D32, dst_comps /* num_channels */,
                             false /* transpose */,
                             LSC_CACHE(devinfo, LOAD, L1UC_L3UC));


   send->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, send->exec_size);
   send->ex_mlen = 0;
   send->header_size = 0;
   send->has_side_effects = true;
   send->is_volatile = false;

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
}

static void
lower_urb_write_logical_send(const brw_builder &bld, brw_urb_inst *urb)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const bool per_slot_present =
      urb->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS].file != BAD_FILE;
   const bool channel_mask_present =
      urb->src[URB_LOGICAL_SRC_CHANNEL_MASK].file != BAD_FILE;

   const unsigned length = 1 + per_slot_present + channel_mask_present +
                           urb->components_read(URB_LOGICAL_SRC_DATA);

   brw_reg *payload_sources = new brw_reg[length];
   brw_reg payload = retype(brw_allocate_vgrf_units(*bld.shader, length),
                            BRW_TYPE_F);

   unsigned header_size = 0;
   payload_sources[header_size++] = urb->src[URB_LOGICAL_SRC_HANDLE];
   if (per_slot_present)
      payload_sources[header_size++] = urb->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];

   if (channel_mask_present) {
      payload_sources[header_size++] =
         urb->src[URB_LOGICAL_SRC_CHANNEL_MASK].file == IMM ?
         brw_imm_ud(urb->src[URB_LOGICAL_SRC_CHANNEL_MASK].ud << 16) :
         bld.SHL(retype(urb->src[URB_LOGICAL_SRC_CHANNEL_MASK], BRW_TYPE_UD),
                 brw_imm_ud(16));
   }

   for (unsigned i = header_size, j = 0; i < length; i++, j++)
      payload_sources[i] = offset(urb->src[URB_LOGICAL_SRC_DATA], bld, j);

   bld.LOAD_PAYLOAD(payload, payload_sources, length, header_size);

   delete [] payload_sources;

   const uint32_t offset = urb->offset;

   brw_send_inst *send = brw_transform_inst_to_send(bld, urb);
   urb = NULL;

   send->header_size = header_size;
   send->dst = brw_null_reg();

   send->sfid = BRW_SFID_URB;
   send->desc = brw_urb_desc(devinfo,
                             GFX8_URB_OPCODE_SIMD8_WRITE,
                             per_slot_present,
                             channel_mask_present,
                             offset);

   send->mlen = length;
   send->ex_desc = 0;
   send->ex_mlen = 0;
   send->has_side_effects = true;

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
}

static void
lower_urb_write_logical_send_xe2(const brw_builder &bld, brw_urb_inst *urb)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->has_lsc);

   /* Get the logical send arguments. */
   const brw_reg handle = urb->src[URB_LOGICAL_SRC_HANDLE];
   const brw_reg src = urb->components_read(URB_LOGICAL_SRC_DATA) ?
      urb->src[URB_LOGICAL_SRC_DATA] : brw_reg(brw_imm_ud(0));
   assert(brw_type_size_bytes(src.type) == 4);

   /* Calculate the total number of components of the payload. */
   const unsigned src_comps = MAX2(1, urb->components_read(URB_LOGICAL_SRC_DATA));
   const unsigned src_sz = brw_type_size_bytes(src.type);

   brw_reg payload = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(payload, handle);

   /* The low 24-bits of the URB handle is a byte offset into the URB area.
    * Add the (OWord) offset of the write to this value.
    */
   if (urb->offset) {
      bld.ADD(payload, payload, brw_imm_ud(urb->offset));
      urb->offset = 0;
   }

   brw_reg offsets = urb->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS];
   if (offsets.file != BAD_FILE) {
      bld.ADD(payload, payload, offsets);
   }

   unsigned num_channels_or_cmask = src_comps;

   const brw_reg cmask = urb->src[URB_LOGICAL_SRC_CHANNEL_MASK];
   brw_reg desc = brw_imm_ud(0);
   if (cmask.file == IMM) {
      assert(cmask.type == BRW_TYPE_UD);
      num_channels_or_cmask = cmask.ud;
   } else if (cmask.file != BAD_FILE) {
      const brw_builder &ubld = bld.exec_all().group(8, 0);
      desc = component(ubld.SHL(retype(cmask, BRW_TYPE_UD), brw_imm_ud(12)), 0);
      num_channels_or_cmask = 0;
   }

   brw_reg payload2 = bld.move_to_vgrf(src, src_comps);
   const unsigned ex_mlen = (src_comps * src_sz * urb->exec_size) / REG_SIZE;

   brw_send_inst *send = brw_transform_inst_to_send(bld, urb);
   urb = NULL;

   send->sfid = BRW_SFID_URB;

   enum lsc_opcode op = cmask.file != BAD_FILE ? LSC_OP_STORE_CMASK : LSC_OP_STORE;
   send->desc = lsc_msg_desc(devinfo, op,
                             LSC_ADDR_SURFTYPE_FLAT, LSC_ADDR_SIZE_A32,
                             LSC_DATA_SIZE_D32,
                             num_channels_or_cmask,
                             false /* transpose */,
                             LSC_CACHE(devinfo, STORE, L1UC_L3UC));


   send->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, send->exec_size);
   send->ex_mlen = ex_mlen;
   send->header_size = 0;
   send->has_side_effects = true;
   send->is_volatile = false;

   send->src[SEND_SRC_DESC]     = desc;
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = payload2;
}

static void
setup_color_payload(const brw_builder &bld, brw_reg *dst, brw_reg color,
                    unsigned components)
{
   for (unsigned i = 0; i < components; i++)
      dst[i] = offset(color, bld, i);
}

static void
lower_fb_write_logical_send(const brw_builder &bld, brw_fb_write_inst *write,
                            const struct brw_wm_prog_data *prog_data,
                            const brw_wm_prog_key *key,
                            const brw_fs_thread_payload &fs_payload)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_reg color0 = write->src[FB_WRITE_LOGICAL_SRC_COLOR0];
   const brw_reg color1 = write->src[FB_WRITE_LOGICAL_SRC_COLOR1];
   const brw_reg src0_alpha = write->src[FB_WRITE_LOGICAL_SRC_SRC0_ALPHA];
   const brw_reg src_depth = write->src[FB_WRITE_LOGICAL_SRC_SRC_DEPTH];
   const brw_reg src_stencil = write->src[FB_WRITE_LOGICAL_SRC_SRC_STENCIL];
   brw_reg sample_mask = write->src[FB_WRITE_LOGICAL_SRC_OMASK];
   const unsigned components = write->components;
   const unsigned target = write->target;
   const bool null_rt = write->null_rt;
   const bool last_rt = write->last_rt;

   assert(target != 0 || src0_alpha.file == BAD_FILE);

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
      const brw_builder ubld = bld.exec_all().group(8, 0);

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
      if (target > 0) {
         ubld.group(1, 0).MOV(component(header, 2), brw_imm_ud(target));
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
      assert(write->group < 16);
      sources[length] = retype(brw_allocate_vgrf_units(*bld.shader, 1), BRW_TYPE_F);
      bld.group(8, 0).exec_all().annotate("FB write stencil/AA alpha")
         .MOV(sources[length],
              brw_reg(brw_vec8_grf(fs_payload.aa_dest_stencil_reg[0], 0)));
      length++;
   }

   if (src0_alpha.file != BAD_FILE) {
      for (unsigned i = 0; i < bld.dispatch_width() / 8; i++) {
         const brw_builder &ubld = bld.exec_all().group(8, i)
                                      .annotate("FB write src0 alpha");
         const brw_reg tmp = ubld.vgrf(BRW_TYPE_F);
         ubld.MOV(tmp, horiz_offset(src0_alpha, i * 8));
         setup_color_payload(ubld, &sources[length], tmp, 1);
         length++;
      }
   }

   if (sample_mask.file != BAD_FILE) {
      const brw_reg tmp = retype(brw_allocate_vgrf_units(*bld.shader, reg_unit(devinfo)),
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
                           write->group % (16 * reg_unit(devinfo))),
              sample_mask);

      for (unsigned i = 0; i < reg_unit(devinfo); i++)
         sources[length++] = byte_offset(tmp, REG_SIZE * i);
   }

   payload_header_size = length;

   setup_color_payload(bld, &sources[length], color0, components);
   length += 4;

   if (color1.file != BAD_FILE) {
      setup_color_payload(bld, &sources[length], color1, components);
      length += 4;
   }

   if (src_depth.file != BAD_FILE) {
      sources[length] = src_depth;
      length++;
   }

   if (src_stencil.file != BAD_FILE) {
      assert(bld.dispatch_width() == 8 * reg_unit(devinfo));

      sources[length] = bld.vgrf(BRW_TYPE_UD);
      bld.exec_all().annotate("FB write OS")
         .MOV(retype(sources[length], BRW_TYPE_UB),
              subscript(src_stencil, BRW_TYPE_UB, 0));
      length++;
   }

   /* Send from the GRF */
   brw_reg payload = brw_vgrf(-1, BRW_TYPE_F);
   brw_inst *load = bld.LOAD_PAYLOAD(payload, sources, length, payload_header_size);
   payload.nr = brw_allocate_vgrf_units(*bld.shader, regs_written(load)).nr;
   load->dst = payload;

   uint32_t msg_ctl = brw_fb_write_msg_control(write, prog_data);

   /* XXX - Bit 13 Per-sample PS enable */
   uint32_t desc =
      (write->group / 16) << 11 | /* rt slot group */
      brw_fb_write_desc(devinfo, target, msg_ctl, last_rt,
                        0 /* coarse_rt_write */);

   const bool double_rt_writes = devinfo->verx10 == 110 &&
      prog_data->coarse_pixel_dispatch == INTEL_SOMETIMES;

   brw_reg desc_reg = brw_imm_ud(0);
   if (prog_data->coarse_pixel_dispatch == INTEL_SOMETIMES) {
      assert(devinfo->ver >= 11);
      if (!double_rt_writes) {
         const brw_builder &ubld =
            bld.scalar_group().annotate("Coarse bit");
         brw_reg coarse_bit =
            ubld.AND(brw_dynamic_msaa_flags(prog_data),
                     brw_imm_ud(INTEL_MSAA_FLAG_COARSE_RT_WRITES));
         desc_reg = component(coarse_bit, 0);
      }
   } else {
      desc |= prog_data->coarse_pixel_dispatch == INTEL_ALWAYS ? (1 << 18) : 0;
   }

   uint32_t ex_desc = 0;
   if (devinfo->ver >= 20) {
      ex_desc = target << 21 |
                null_rt << 20 |
                (src0_alpha.file != BAD_FILE) << 15 |
                (src_stencil.file != BAD_FILE) << 14 |
                (src_depth.file != BAD_FILE) << 13 |
                (sample_mask.file != BAD_FILE) << 12;
   } else if (devinfo->ver >= 11) {
      /* Set the "Render Target Index" and "Src0 Alpha Present" fields
       * in the extended message descriptor, in lieu of using a header.
       */
      ex_desc = target << 12 |
                null_rt << 20 |
                (src0_alpha.file != BAD_FILE) << 15;
   }

   brw_send_inst *send = brw_transform_inst_to_send(bld, write);
   write = NULL;

   send->desc = desc;
   send->ex_desc = ex_desc;

   send->sfid = BRW_SFID_RENDER_CACHE;

   send->src[SEND_SRC_DESC] = desc_reg;
   send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
   send->mlen = regs_written(load);
   send->ex_mlen = 0;
   send->header_size = header_size;
   send->check_tdr = true;
   send->has_side_effects = true;

   if (double_rt_writes) {
      brw_check_dynamic_msaa_flag(bld, prog_data,
                                  INTEL_MSAA_FLAG_COARSE_RT_WRITES);
      bld.IF(BRW_PREDICATE_NORMAL);
      {
         brw_send_inst *coarse_inst = brw_clone_inst(*bld.shader, send)->as_send();
         coarse_inst->desc |= brw_fb_write_desc(devinfo, target, msg_ctl, last_rt,
                                                true);
         bld.emit(coarse_inst);
      }
      bld.ELSE();
      {
         bld.emit(brw_clone_inst(*bld.shader, send));
      }
      bld.ENDIF();
      send->remove();
   }
}

static void
lower_fb_read_logical_send(const brw_builder &bld, brw_inst *inst,
                           const struct brw_wm_prog_data *wm_prog_data)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_builder &ubld = bld.exec_all().group(8, 0);
   const unsigned length = 2;
   const brw_reg header = ubld.vgrf(BRW_TYPE_UD, length);
   assert(inst->src[0].file == IMM);
   unsigned target = inst->src[0].ud;

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

   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = header;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
   send->mlen = length;
   send->header_size = length;
   send->sfid = BRW_SFID_RENDER_CACHE;
   send->check_tdr = true;
   send->desc =
      (send->group / 16) << 11 | /* rt slot group */
      brw_fb_read_desc(devinfo, target,
                       0 /* msg_control */, send->exec_size,
                       wm_prog_data->persample_dispatch);
}

static bool
is_high_sampler(const struct intel_device_info *devinfo, const brw_reg &sampler)
{
   return sampler.file != IMM || sampler.ud >= 16;
}

/**
 * Emit a LOAD_PAYLOAD instruction while ensuring the sources are aligned to
 * the given requested_alignment_sz.
 */
static brw_inst *
emit_load_payload_with_padding(const brw_builder &bld, const brw_reg &dst,
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

   brw_inst *inst = bld.LOAD_PAYLOAD(dst, src_comps, length, header_size);
   delete[] src_comps;

   return inst;
}

static bool
sampler_op_needs_header(enum brw_sampler_opcode op,
                        const struct intel_device_info *devinfo)
{
   switch (op) {
   case BRW_SAMPLER_OPCODE_GATHER4:
   case BRW_SAMPLER_OPCODE_GATHER4_B:
   case BRW_SAMPLER_OPCODE_GATHER4_C:
   case BRW_SAMPLER_OPCODE_GATHER4_I:
   case BRW_SAMPLER_OPCODE_GATHER4_I_C:
   case BRW_SAMPLER_OPCODE_GATHER4_L:
   case BRW_SAMPLER_OPCODE_GATHER4_L_C:
   case BRW_SAMPLER_OPCODE_GATHER4_PO:
   case BRW_SAMPLER_OPCODE_GATHER4_PO_PACKED:
   case BRW_SAMPLER_OPCODE_GATHER4_PO_B:
   case BRW_SAMPLER_OPCODE_GATHER4_PO_C:
   case BRW_SAMPLER_OPCODE_GATHER4_PO_C_PACKED:
   case BRW_SAMPLER_OPCODE_GATHER4_PO_L:
   case BRW_SAMPLER_OPCODE_GATHER4_PO_L_C:
   case BRW_SAMPLER_OPCODE_SAMPLEINFO:
      return true;
   case BRW_SAMPLER_OPCODE_LD:
   case BRW_SAMPLER_OPCODE_LD_LZ:
      /* Xe3 HW does not seem to work unless we force a header. */
      return devinfo->ver >= 30;
   default:
      break;
   }

   return false;
}

static bool
sampler_opcode_uses_sampler_state(enum brw_sampler_opcode op)
{
   switch (op) {
   case BRW_SAMPLER_OPCODE_RESINFO:
   case BRW_SAMPLER_OPCODE_SAMPLEINFO:
   case BRW_SAMPLER_OPCODE_LD:
   case BRW_SAMPLER_OPCODE_LD_LZ:
      return false;

   default:
      return true;
   }
}

static unsigned
get_sampler_msg_payload_type_bit_size(const intel_device_info *devinfo,
                                      const brw_tex_inst *inst)
{
   assert(inst);
   const brw_reg *src = inst->src;
   unsigned src_type_size = 4; /* SAMPLEINFO has no payload source */

   /* All sources need to have the same size, therefore seek the first valid
    * and take the size from there.
    */
   for (unsigned i = TEX_LOGICAL_SRC_PAYLOAD0; i < inst->sources; i++) {
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
   for (unsigned i = TEX_LOGICAL_SRC_PAYLOAD0; i < inst->sources; i++) {
      assert(src[i].file == BAD_FILE ||
             brw_type_size_bytes(src[i].type) == src_type_size);
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
   if (inst->sampler_opcode == BRW_SAMPLER_OPCODE_LD2DMS_W_GFX125 ||
       inst->sampler_opcode == BRW_SAMPLER_OPCODE_LD_MCS)
      src_type_size = 2;

   return src_type_size * 8;
}

static void
lower_sampler_logical_send(const brw_builder &bld, brw_tex_inst *tex)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const brw_compiler *compiler = bld.shader->compiler;

   const enum brw_sampler_opcode op = tex->sampler_opcode;
   const bool surface_bindless = tex->surface_bindless;
   const bool sampler_bindless = tex->sampler_bindless;
   const brw_reg surface = tex->src[TEX_LOGICAL_SRC_SURFACE];
   const brw_reg sampler = tex->src[TEX_LOGICAL_SRC_SAMPLER];
   /* Xe2+ should never used packed offsets since it has enough opcodes to
    * handle any programmable offset.
    */
   const brw_reg packed_offsets = tex->src[TEX_LOGICAL_SRC_PACKED_OFFSETS];
   assert(packed_offsets.file == BAD_FILE || devinfo->ver < 20);

   const unsigned payload_type_bit_size =
      get_sampler_msg_payload_type_bit_size(devinfo, tex);

   /* 16-bit payloads are available only on gfx11+ */
   assert(payload_type_bit_size != 16 || devinfo->ver >= 11);

   /* We never generate EOT sampler messages */
   assert(!tex->eot);

   const enum brw_reg_type payload_type =
      brw_type_with_size(BRW_TYPE_UD, payload_type_bit_size);

   const bool needs_header =
      sampler_op_needs_header(op, devinfo) ||
      tex->has_const_offsets ||
      packed_offsets.file != BAD_FILE ||
      sampler_bindless || is_high_sampler(devinfo, sampler) ||
      tex->residency;

   unsigned header_size = needs_header ? reg_unit(devinfo) : 0, length = 0;
   brw_reg sources[1 + MAX_SAMPLER_MESSAGE_SIZE];

   for (unsigned i = 0; i < ARRAY_SIZE(sources); i++)
      sources[i] = bld.vgrf((i == 0 && needs_header) ? BRW_TYPE_UD : payload_type);

   if (needs_header) {
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

      uint32_t g0_2 = 0;
      if (tex->gather_component)
         g0_2 |= tex->gather_component << 16;
      if (tex->residency)
         g0_2 |= 1 << 23; /* g0.2 bit23 : Pixel Null Mask Enable */

      /* If we're requesting fewer than four channels worth of response,
       * and we have an explicit header, we need to set up the sampler
       * writemask.  It's reversed from normal: 1 means "don't write".
       */
      unsigned comps_regs =
         DIV_ROUND_UP(regs_written(tex) - reg_unit(devinfo) * tex->residency,
                      reg_unit(devinfo));
      unsigned comp_regs =
         DIV_ROUND_UP(tex->dst.component_size(tex->exec_size),
                      reg_unit(devinfo) * REG_SIZE);
      if (comps_regs < 4 * comp_regs) {
         assert(comps_regs % comp_regs == 0);
         unsigned mask = ~((1 << (comps_regs / comp_regs)) - 1) & 0xf;
         g0_2 |= mask << 12;
      }

      if (tex->has_const_offsets) {
         g0_2 |= ((tex->const_offsets[2] & 0xf) << 0) |
                 ((tex->const_offsets[1] & 0xf) << 4) |
                 ((tex->const_offsets[0] & 0xf) << 8);
      }

      /* Build the actual header */
      const brw_builder ubld = bld.exec_all().group(8 * reg_unit(devinfo), 0);
      const brw_builder ubld1 = ubld.group(1, 0);
      if (devinfo->ver >= 11)
         ubld.MOV(header, brw_imm_ud(0));
      else
         ubld.MOV(header, retype(brw_vec8_grf(0, 0), BRW_TYPE_UD));

      if (packed_offsets.file != BAD_FILE) {
         ubld1.OR(retype(component(header, 2), BRW_TYPE_UD),
                  retype(component(packed_offsets, 0), BRW_TYPE_UD),
                  brw_imm_ud(g0_2));
      } else if (g0_2) {
         ubld1.MOV(component(header, 2), brw_imm_ud(g0_2));
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

      if (sampler_bindless) {
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
            ubld1.OR(component(header, 3), sampler, brw_imm_ud(1));
         } else {
            ubld1.MOV(component(header, 3), sampler);
         }
      } else if (is_high_sampler(devinfo, sampler)) {
         brw_reg sampler_state_ptr =
            retype(brw_vec1_grf(0, 3), BRW_TYPE_UD);

         /* Gfx11+ sampler message headers include bits in 4:0 which conflict
          * with the ones included in g0.3 bits 4:0.  Mask them out.
          */
         if (devinfo->ver >= 11) {
            sampler_state_ptr = ubld.vgrf(BRW_TYPE_UD);
            ubld.AND(sampler_state_ptr,
                     retype(brw_vec1_grf(0, 3), BRW_TYPE_UD),
                     brw_imm_ud(INTEL_MASK(31, 5)));
         }

         if (sampler.file == IMM) {
            assert(sampler.ud >= 16);
            const int sampler_state_size = 16; /* 16 bytes */

            ubld1.ADD(component(header, 3), sampler_state_ptr,
                      brw_imm_ud(16 * (sampler.ud / 16) * sampler_state_size));
         } else {
            brw_reg tmp = ubld.vgrf(BRW_TYPE_UD);
            ubld.AND(tmp, component(sampler, 0), brw_imm_ud(0x0f0));
            ubld.SHL(tmp, tmp, brw_imm_ud(4));
            ubld1.ADD(component(header, 3), sampler_state_ptr, tmp);
         }
      } else if (devinfo->ver >= 11 && sampler_opcode_uses_sampler_state(op)) {
         /* Gfx11+ sampler message headers include bits in 4:0 which conflict
          * with the ones included in g0.3 bits 4:0.  Mask them out.
          */
         ubld1.AND(component(header, 3),
                   retype(brw_vec1_grf(0, 3), BRW_TYPE_UD),
                   brw_imm_ud(INTEL_MASK(31, 5)));
      }
   }

   const unsigned msg_type = brw_get_sampler_hw_opcode(op);

   for (uint32_t i = TEX_LOGICAL_SRC_PAYLOAD0; i < tex->sources; i++)
      bld.MOV(retype(sources[length++], payload_type), retype(tex->src[i], payload_type));

   const brw_reg src_payload =
      retype(brw_allocate_vgrf_units(*bld.shader, length * bld.dispatch_width() / 8),
             BRW_TYPE_UD);
   /* In case of 16-bit payload each component takes one full register in
    * both SIMD8H and SIMD16H modes. In both cases one reg can hold 16
    * elements. In SIMD8H case hardware simply expects the components to be
    * padded (i.e., aligned on reg boundary).
    */
   brw_inst *load_payload_inst =
      emit_load_payload_with_padding(bld, src_payload, sources, length,
                                     header_size, REG_SIZE * reg_unit(devinfo));
   unsigned mlen = load_payload_inst->size_written / REG_SIZE;
   unsigned simd_mode = 0;
   if (devinfo->ver < 20) {
      if (payload_type_bit_size == 16) {
         assert(devinfo->ver >= 11);
         simd_mode = tex->exec_size <= 8 ? GFX10_SAMPLER_SIMD_MODE_SIMD8H :
            GFX10_SAMPLER_SIMD_MODE_SIMD16H;
      } else {
         simd_mode = tex->exec_size <= 8 ? BRW_SAMPLER_SIMD_MODE_SIMD8 :
            BRW_SAMPLER_SIMD_MODE_SIMD16;
      }
   } else {
      if (payload_type_bit_size == 16) {
         simd_mode = tex->exec_size <= 16 ? XE2_SAMPLER_SIMD_MODE_SIMD16H :
            XE2_SAMPLER_SIMD_MODE_SIMD32H;
      } else {
         simd_mode = tex->exec_size <= 16 ? XE2_SAMPLER_SIMD_MODE_SIMD16 :
            XE2_SAMPLER_SIMD_MODE_SIMD32;
      }
   }

   const bool fused_eu_disable = tex->fused_eu_disable;

   brw_send_inst *send = brw_transform_inst_to_send(bld, tex);
   tex = NULL;

   send->fused_eu_disable = fused_eu_disable;
   send->mlen = mlen;
   send->header_size = header_size;
   send->sfid = BRW_SFID_SAMPLER;
   send->bindless_surface = surface_bindless;
   uint sampler_ret_type = brw_type_size_bits(send->dst.type) == 16
      ? GFX8_SAMPLER_RETURN_FORMAT_16BITS
      : GFX8_SAMPLER_RETURN_FORMAT_32BITS;
   if (!surface_bindless && surface.file == IMM &&
       (sampler.file == IMM || sampler_bindless)) {
      send->desc = brw_sampler_desc(devinfo, surface.ud,
                                    (sampler.file == IMM && !sampler_bindless) ?
                                    sampler.ud % 16 : 0,
                                    msg_type,
                                    simd_mode,
                                    sampler_ret_type);
      send->src[SEND_SRC_DESC]    = brw_imm_ud(0);
      send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
   } else if (surface_bindless) {
      /* Bindless surface */
      send->desc = brw_sampler_desc(devinfo,
                                    GFX9_BTI_BINDLESS,
                                    (sampler.file == IMM && !sampler_bindless) ?
                                    sampler.ud % 16 : 0,
                                    msg_type,
                                    simd_mode,
                                    sampler_ret_type);

      /* For bindless samplers, the entire address is included in the message
       * header so we can leave the portion in the message descriptor 0.
       */
      if (sampler_bindless || sampler.file == IMM) {
         send->src[SEND_SRC_DESC] = brw_imm_ud(0);
      } else {
         const brw_builder ubld = bld.uniform();
         brw_reg desc = ubld.vgrf(BRW_TYPE_UD);
         ubld.SHL(desc, sampler, brw_imm_ud(8));
         send->src[SEND_SRC_DESC] = component(desc, 0);
      }

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      send->src[SEND_SRC_EX_DESC] = retype(surface, BRW_TYPE_UD);
   } else {
      /* Immediate portion of the descriptor */
      send->desc = brw_sampler_desc(devinfo,
                                    0, /* surface */
                                    0, /* sampler */
                                    msg_type,
                                    simd_mode,
                                    sampler_ret_type);
      const brw_builder ubld = bld.uniform();
      brw_reg desc = ubld.vgrf(BRW_TYPE_UD);
      if (surface.equals(sampler)) {
         /* This case is common in GL */
         ubld.MUL(desc, surface, brw_imm_ud(0x101));
      } else {
         if (sampler_bindless) {
            ubld.MOV(desc, surface);
         } else if (!sampler_bindless && sampler.file == IMM) {
            ubld.OR(desc, surface, brw_imm_ud(sampler.ud << 8));
         } else {
            ubld.SHL(desc, sampler, brw_imm_ud(8));
            ubld.OR(desc, desc, surface);
         }
      }
      ubld.AND(desc, desc, brw_imm_ud(0xfff));

      send->src[SEND_SRC_DESC]    = component(desc, 0);
      send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
   }

   send->ex_desc = 0;

   send->src[SEND_SRC_PAYLOAD1] = src_payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();

   /* Message length > MAX_SAMPLER_MESSAGE_SIZE disallowed by hardware. */
   assert(send->mlen <= MAX_SAMPLER_MESSAGE_SIZE * reg_unit(devinfo));
}

/**
 * Predicate the specified instruction on the vector mask.
 */
static void
emit_predicate_on_vector_mask(const brw_builder &bld, brw_inst *inst)
{
   assert(bld.shader->stage == MESA_SHADER_FRAGMENT &&
          bld.group() == inst->group &&
          bld.dispatch_width() == inst->exec_size);

   const brw_builder ubld = bld.uniform();

   const brw_shader &s = *bld.shader;
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
setup_surface_descriptors(const brw_builder &bld, brw_send_inst *send, uint32_t desc,
                          const brw_reg &surface, const brw_reg &surface_handle)
{
   /* We must have exactly one of surface and surface_handle */
   assert((surface.file == BAD_FILE) != (surface_handle.file == BAD_FILE));

   if (surface.file == IMM) {
      send->desc = desc | (surface.ud & 0xff);
      send->src[SEND_SRC_DESC]    = brw_imm_ud(0);
      send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
   } else if (surface_handle.file != BAD_FILE) {
      /* Bindless surface */
      send->desc = desc | GFX9_BTI_BINDLESS;
      send->src[SEND_SRC_DESC] = brw_imm_ud(0);

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      send->src[SEND_SRC_EX_DESC] = retype(surface_handle, BRW_TYPE_UD);
   } else {
      send->desc = desc;
      const brw_builder ubld = bld.uniform();
      brw_reg tmp = ubld.vgrf(BRW_TYPE_UD);
      ubld.AND(tmp, surface, brw_imm_ud(0xff));
      send->src[SEND_SRC_DESC] = component(tmp, 0);
      send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
   }

   send->bindless_surface = surface_handle.file != BAD_FILE;
}

static void
setup_lsc_surface_descriptors(const brw_builder &bld, brw_send_inst *send,
                              uint32_t desc, const brw_reg &surface,
                              int32_t base_offset)
{
   const ASSERTED intel_device_info *devinfo = bld.shader->devinfo;

   assert(base_offset == 0 || devinfo->ver >= 20);

   send->src[SEND_SRC_DESC] = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);

   enum lsc_addr_surface_type surf_type = lsc_msg_desc_addr_type(devinfo, desc);

   unsigned max_imm_bits = brw_max_immediate_offset_bits(surf_type);
   assert(base_offset >= u_intN_min(max_imm_bits));
   assert(base_offset <= u_intN_max(max_imm_bits));

   const unsigned base_offset_bits =
      util_bitpack_sint(base_offset, 0, max_imm_bits - 1);

   /* On Gfx20+ UGM always uses ExBSO which implies bindless. */
   send->bindless_surface =
      surf_type == LSC_ADDR_SURFTYPE_BSS ||
      (devinfo->ver >= 20 && surf_type == LSC_ADDR_SURFTYPE_SS);

   switch (surf_type) {
   case LSC_ADDR_SURFTYPE_BSS:
   case LSC_ADDR_SURFTYPE_SS:
      assert(surface.file != BAD_FILE);
      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      send->src[SEND_SRC_EX_DESC] = retype(surface, BRW_TYPE_UD);

      /* We're already using the extended descriptor to hold the surface
       * handle. But now the immediate extended descriptor bits in the
       * instruction are unused, so this is where the HW design team thought
       * it would be a good idea to store the immediate offset.
       *
       * This doesn't play well with the rest of our compiler that considers
       * there is only one value for the extended descriptor. So here we stash
       * the base offset in brw_inst::offset and flag the instruction for the
       * generator to do the right thing with it.
       */
      if (base_offset) {
         send->ex_desc_imm = true;
         send->offset = SET_BITS(GET_BITS(base_offset_bits, 16, 4), 31, 19) |
                        SET_BITS(GET_BITS(base_offset_bits, 3, 0), 15, 12);
      }
      break;

   case LSC_ADDR_SURFTYPE_BTI:
      assert(surface.file != BAD_FILE);
      if (surface.file == IMM) {
         send->src[SEND_SRC_EX_DESC] =
            brw_imm_ud(lsc_bti_ex_desc(devinfo, surface.ud, base_offset_bits));
      } else {
         assert(base_offset == 0);
         const brw_builder ubld = bld.uniform();
         brw_reg tmp = ubld.SHL(surface, brw_imm_ud(24));
         send->src[SEND_SRC_EX_DESC] = component(tmp, 0);
      }
      break;

   case LSC_ADDR_SURFTYPE_FLAT:
      send->src[SEND_SRC_EX_DESC] =
         brw_imm_ud(lsc_flat_ex_desc(devinfo, base_offset_bits));
      break;

   default:
      UNREACHABLE("Invalid LSC surface address type");
   }
}

static enum lsc_addr_size
lsc_addr_size_for_type(enum brw_reg_type type)
{
   switch (brw_type_size_bytes(type)) {
   case 2: return LSC_ADDR_SIZE_A16;
   case 4: return LSC_ADDR_SIZE_A32;
   case 8: return LSC_ADDR_SIZE_A64;
   default: UNREACHABLE("invalid type size");
   }
}

static void
lower_lsc_memory_logical_send(const brw_builder &bld, brw_mem_inst *mem)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   assert(devinfo->has_lsc);

   /* Get the logical send arguments. */
   const brw_reg binding = mem->src[MEMORY_LOGICAL_BINDING];
   const brw_reg addr = mem->src[MEMORY_LOGICAL_ADDRESS];
   const brw_reg data0 = mem->src[MEMORY_LOGICAL_DATA0];
   const brw_reg data1 = mem->src[MEMORY_LOGICAL_DATA1];

   const enum lsc_opcode op = mem->lsc_op;
   const enum memory_logical_mode mode = mem->mode;
   const enum lsc_addr_surface_type binding_type = mem->binding_type;
   const unsigned coord_components = mem->coord_components;
   enum lsc_data_size data_size = mem->data_size;
   const unsigned components = mem->components;
   const bool transpose = mem->flags & MEMORY_FLAG_TRANSPOSE;
   const bool include_helpers = mem->flags & MEMORY_FLAG_INCLUDE_HELPERS;
   const bool volatile_access = mem->flags & MEMORY_FLAG_VOLATILE_ACCESS;
   const bool coherent_access = mem->flags & MEMORY_FLAG_COHERENT_ACCESS;
   const bool has_side_effects = mem->has_side_effects();
   const bool fused_eu_disable = mem->flags & MEMORY_FLAG_FUSED_EU_DISABLE;

   const uint32_t data_size_B = lsc_data_size_bytes(data_size);
   const enum brw_reg_type data_type =
      brw_type_with_size(data0.type, data_size_B * 8);

   const enum lsc_addr_size addr_size = lsc_addr_size_for_type(addr.type);
   const int32_t base_offset = mem->address_offset;

   /**
    * TGM messages cannot have a base offset
    */
   if (mode == MEMORY_MODE_TYPED)
      assert(base_offset == 0);

   brw_reg payload = addr;

   if (addr.file != VGRF || !addr.is_contiguous()) {
      if (mem->force_writemask_all) {
         const brw_builder dbld =
            mem->exec_size == 1 ?
            bld.scalar_group() :
            bld.group(bld.shader->dispatch_width, 0);
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
            data[size++] = offset(data0, bld, i);

         if (data1.file != BAD_FILE) {
            for (unsigned i = 0; i < components; i++)
               data[size++] = offset(data1, bld, i);
         }

         payload2 = bld.vgrf(data0.type, size);
         bld.LOAD_PAYLOAD(payload2, data, size, 0);
         ex_mlen = (size * brw_type_size_bytes(data_type) * mem->exec_size) / REG_SIZE;
      }
   }

   /* Bspec: Atomic instruction -> Cache section:
    *
    *    Atomic messages are always forced to "un-cacheable" in the L1
    *    cache.
    *
    * Bspec: Overview of memory Access:
    *
    *   If a read from a Null tile gets a cache-hit in a virtually-addressed
    *   GPU cache, then the read may not return zeroes.
    *
    * If a shader writes to a null tile and wants to be able to read it back
    * as zero, it will use the 'volatile' decoration for the access, otherwise
    * the compiler may choose to optimize things out, breaking the
    * residencyNonResidentStrict guarantees. Due to the above, we need to make
    * these operations uncached.
    */
   unsigned cache_mode =
      lsc_opcode_is_atomic(op) ? LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
      volatile_access ?
         (devinfo->ver >= 20 ?
            /* Xe2 has a better L3 that can deal with null tiles.*/
            (lsc_opcode_is_store(op) ?
               LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
               LSC_CACHE(devinfo, LOAD, L1UC_L3C)) :
            /* On older platforms, all caches have to be bypassed. */
            (lsc_opcode_is_store(op) ?
               LSC_CACHE(devinfo, STORE, L1UC_L3UC) :
               LSC_CACHE(devinfo, LOAD, L1UC_L3UC))) :
      /* Skip L1 for coherent accesses */
      coherent_access ? (lsc_opcode_is_store(op) ?
                         LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
                         LSC_CACHE(devinfo, LOAD, L1UC_L3C)) :
      lsc_opcode_is_store(op) ? LSC_CACHE(devinfo, STORE, L1STATE_L3MOCS) :
      LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS);

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
         emit_predicate_on_vector_mask(bld, mem);
      else if (has_side_effects && mode != MEMORY_MODE_SCRATCH)
         brw_emit_predicate_on_sample_mask(bld, mem);
   }

   brw_send_inst *send = brw_transform_inst_to_send(bld, mem);
   mem = NULL;

   switch (mode) {
   case MEMORY_MODE_UNTYPED:
   case MEMORY_MODE_CONSTANT:
   case MEMORY_MODE_SCRATCH:
      send->sfid = BRW_SFID_UGM;
      break;
   case MEMORY_MODE_TYPED:
      send->sfid = BRW_SFID_TGM;
      break;
   case MEMORY_MODE_SHARED_LOCAL:
      send->sfid = BRW_SFID_SLM;
      break;
   }
   assert(send->sfid);

   /* Disable LSC data port L1 cache scheme for the TGM load/store for RT
    * shaders. (see HSD 18038444588)
    */
   if (devinfo->ver >= 20 && mesa_shader_stage_is_rt(bld.shader->stage) &&
       send->sfid == BRW_SFID_TGM &&
       !lsc_opcode_is_atomic(op)) {
      if (lsc_opcode_is_store(op)) {
         cache_mode = (unsigned) LSC_CACHE(devinfo, STORE, L1UC_L3WB);
      } else {
         cache_mode = (unsigned) LSC_CACHE(devinfo, LOAD, L1UC_L3C);
      }
   }

   send->desc = lsc_msg_desc(devinfo, op, binding_type, addr_size, data_size,
                             lsc_opcode_has_cmask(op) ?
                             (1 << components) - 1 : components,
                             transpose, cache_mode);

   setup_lsc_surface_descriptors(bld, send, send->desc, binding, base_offset);


   send->mlen = lsc_msg_addr_len(devinfo, addr_size,
                                 send->exec_size * coord_components);
   send->ex_mlen = ex_mlen;
   send->header_size = 0;
   send->has_side_effects = has_side_effects;
   send->is_volatile = !has_side_effects || volatile_access;
   send->fused_eu_disable = fused_eu_disable;

   /* Finally, the payload */
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = payload2;
}

static brw_reg
emit_a64_oword_block_header(const brw_builder &bld, const brw_reg &addr)
{
   const brw_builder ubld = bld.exec_all().group(8, 0);

   assert(brw_type_size_bytes(addr.type) == 8 && addr.stride == 0);

   brw_reg expanded_addr = addr;
   if (addr.file == UNIFORM) {
      /* We can't do stride 1 with the UNIFORM file, it requires stride 0 */
      brw_builder ubld1 = ubld.group(1, 0);

      brw_reg tmp = ubld1.vgrf(BRW_TYPE_UQ);
      ubld1.UNDEF(tmp);

      expanded_addr = component(tmp, 0);
      ubld1.MOV(expanded_addr, retype(addr, BRW_TYPE_UQ));
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
lower_hdc_memory_logical_send(const brw_builder &bld, brw_mem_inst *mem)
{
   const intel_device_info *devinfo = bld.shader->devinfo;

   /* Get the logical send arguments. */
   brw_reg binding = mem->src[MEMORY_LOGICAL_BINDING];
   const brw_reg addr = mem->src[MEMORY_LOGICAL_ADDRESS];
   const brw_reg data0 = mem->src[MEMORY_LOGICAL_DATA0];
   const brw_reg data1 = mem->src[MEMORY_LOGICAL_DATA1];

   const enum lsc_opcode op = mem->lsc_op;
   const enum memory_logical_mode mode = mem->mode;
   enum lsc_addr_surface_type binding_type = mem->binding_type;
   const unsigned coord_components = mem->coord_components;
   const unsigned alignment = mem->alignment;
   const unsigned components = mem->components;
   const bool block = mem->flags & MEMORY_FLAG_TRANSPOSE;
   const bool include_helpers = mem->flags & MEMORY_FLAG_INCLUDE_HELPERS;
   const bool volatile_access = mem->flags & MEMORY_FLAG_VOLATILE_ACCESS;
   const bool fused_eu_disable = mem->flags & MEMORY_FLAG_FUSED_EU_DISABLE;
   const bool has_side_effects = mem->has_side_effects();
   const bool has_dest = mem->dst.file != BAD_FILE && !mem->dst.is_null();
   assert(mem->address_offset == 0);

   /* Don't predicate scratch writes on the sample mask.  Otherwise,
    * FS helper invocations would load undefined values from scratch memory.
    * And scratch memory load/stores are produced from operations without
    * side-effects, thus they should not have different behavior in the
    * helper invocations.
    */
   bool allow_sample_mask = has_side_effects && mode != MEMORY_MODE_SCRATCH;

   const enum lsc_data_size data_size = mem->data_size;

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
   assert(!block || (alignment % 4) == 0);

   enum lsc_addr_size addr_size = lsc_addr_size_for_type(addr.type);
   unsigned addr_size_B = coord_components * lsc_addr_size_bytes(addr_size);

   brw_reg header;
   brw_builder ubld8 = bld.exec_all().group(8, 0);
   brw_builder ubld1 = ubld8.group(1, 0);
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
         emit_predicate_on_vector_mask(bld, mem);
      else if (allow_sample_mask &&
               (header.file == BAD_FILE || !surface_access))
         brw_emit_predicate_on_sample_mask(bld, mem);
   }

   brw_reg payload, payload2;
   unsigned mlen, ex_mlen = 0;

   if (!block) {
      brw_reg data[11];
      unsigned num_sources = 0;
      if (header.file != BAD_FILE)
         data[num_sources++] = header;

      for (unsigned i = 0; i < coord_components; i++)
         data[num_sources++] = offset(addr, bld, i);

      if (data0.file != BAD_FILE) {
         for (unsigned i = 0; i < components; i++)
            data[num_sources++] = offset(data0, bld, i);
         if (data1.file != BAD_FILE) {
            for (unsigned i = 0; i < components; i++)
               data[num_sources++] = offset(data1, bld, i);
         }
      }

      assert(num_sources <= ARRAY_SIZE(data));

      unsigned payload_size_UDs = (header.file != BAD_FILE ? 1 : 0) +
                                  (addr_size_B / 4) +
                                  (lsc_op_num_data_values(op) * components *
                                   lsc_data_size_bytes(data_size) / 4);

      payload = bld.vgrf(BRW_TYPE_UD, payload_size_UDs);
      brw_inst *load_payload =
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

      sfid = BRW_SFID_HDC1;

      if (lsc_opcode_is_atomic(op)) {
         desc = brw_dp_typed_atomic_desc(devinfo, mem->exec_size, mem->group,
                                         lsc_op_to_legacy_atomic(op),
                                         has_dest);
      } else {
         desc = brw_dp_typed_surface_rw_desc(devinfo, mem->exec_size,
                                             mem->group, components, !has_dest);
      }
   } else if (mode == MEMORY_MODE_CONSTANT) {
      assert(block); /* non-block loads not yet handled */

      sfid = BRW_SFID_HDC_READ_ONLY;
      desc = brw_dp_oword_block_rw_desc(devinfo, false, components, !has_dest);
   } else if (addr_size == LSC_ADDR_SIZE_A64) {
      assert(binding_type == LSC_ADDR_SURFTYPE_FLAT);
      assert(!dword_scattered);

      sfid = BRW_SFID_HDC1;

      if (lsc_opcode_is_atomic(op)) {
         unsigned aop = lsc_op_to_legacy_atomic(op);
         if (lsc_opcode_is_atomic_float(op)) {
            desc = brw_dp_a64_untyped_atomic_float_desc(devinfo, mem->exec_size,
                                                        data_bit_size, aop,
                                                        has_dest);
         } else {
            desc = brw_dp_a64_untyped_atomic_desc(devinfo, mem->exec_size,
                                                  data_bit_size, aop,
                                                  has_dest);
         }
      } else if (block) {
         desc = brw_dp_a64_oword_block_rw_desc(devinfo, oword_aligned,
                                               components, !has_dest);
      } else if (byte_scattered) {
         desc = brw_dp_a64_byte_scattered_rw_desc(devinfo, mem->exec_size,
                                                  data_bit_size, !has_dest);
      } else {
         desc = brw_dp_a64_untyped_surface_rw_desc(devinfo, mem->exec_size,
                                                   components, !has_dest);
      }
   } else {
      assert(binding_type != LSC_ADDR_SURFTYPE_FLAT);

      sfid = surface_access ? BRW_SFID_HDC1 : BRW_SFID_HDC0;

      if (lsc_opcode_is_atomic(op)) {
         unsigned aop = lsc_op_to_legacy_atomic(op);
         if (lsc_opcode_is_atomic_float(op)) {
            desc = brw_dp_untyped_atomic_float_desc(devinfo, mem->exec_size,
                                                    aop, has_dest);
         } else {
            desc = brw_dp_untyped_atomic_desc(devinfo, mem->exec_size,
                                              aop, has_dest);
         }
      } else if (block) {
         desc = brw_dp_oword_block_rw_desc(devinfo, oword_aligned,
                                           components, !has_dest);
      } else if (byte_scattered) {
         desc = brw_dp_byte_scattered_rw_desc(devinfo, mem->exec_size,
                                              data_bit_size, !has_dest);
      } else if (dword_scattered) {
         desc = brw_dp_dword_scattered_rw_desc(devinfo, mem->exec_size,
                                               !has_dest);
      } else {
         desc = brw_dp_untyped_surface_rw_desc(devinfo, mem->exec_size,
                                               components, !has_dest);
      }
   }

   assert(sfid);

   brw_send_inst *send = brw_transform_inst_to_send(bld, mem);
   mem = NULL;

   send->sfid = sfid;
   send->mlen = mlen;
   send->ex_mlen = ex_mlen;
   send->header_size = header.file != BAD_FILE ? 1 : 0;
   send->has_side_effects = has_side_effects;
   send->is_volatile = !has_side_effects || volatile_access;
   send->fused_eu_disable = fused_eu_disable;

   if (block) {
      assert(send->force_writemask_all);
      send->exec_size = components > 8 ? 16 : 8;
   }

   send->bindless_surface = binding_type == LSC_ADDR_SURFTYPE_BSS;

   /* Set up descriptors */
   switch (binding_type) {
   case LSC_ADDR_SURFTYPE_FLAT:
      send->src[SEND_SRC_DESC]    = brw_imm_ud(0);
      send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
      break;
   case LSC_ADDR_SURFTYPE_BSS:
   case LSC_ADDR_SURFTYPE_SS:
      desc |= GFX9_BTI_BINDLESS;

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      send->src[SEND_SRC_DESC]    = brw_imm_ud(0);
      send->src[SEND_SRC_EX_DESC] = binding;
      break;
   case LSC_ADDR_SURFTYPE_BTI:
      if (binding.file == IMM) {
         desc |= binding.ud & 0xff;
         send->src[SEND_SRC_DESC]    = brw_imm_ud(0);
         send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
      } else {
         brw_reg tmp = ubld1.vgrf(BRW_TYPE_UD);
         ubld1.AND(tmp, binding, brw_imm_ud(0xff));
         send->src[SEND_SRC_DESC]    = component(tmp, 0);
         send->src[SEND_SRC_EX_DESC] = brw_imm_ud(0);
      }
      break;
   default:
      UNREACHABLE("Unknown surface type");
   }

   send->desc = desc;

   /* Finally, the payloads */
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = payload2;
}

static void
lower_lsc_varying_pull_constant_logical_send(const brw_builder &bld,
                                             brw_inst *inst)
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

   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->sfid = BRW_SFID_UGM;
   send->bindless_surface =
      surf_type == LSC_ADDR_SURFTYPE_BSS ||
      (devinfo->ver >= 20 && surf_type == LSC_ADDR_SURFTYPE_SS);

   assert(!compiler->indirect_ubos_use_sampler);

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = ubo_offset;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();

   send->desc =
      lsc_msg_desc(devinfo, LSC_OP_LOAD,
                   surf_type, LSC_ADDR_SIZE_A32,
                   LSC_DATA_SIZE_D32,
                   alignment >= 4 ? 4 : 1 /* num_channels */,
                   false /* transpose */,
                   LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));
   send->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, send->exec_size);

   setup_lsc_surface_descriptors(bld, send, send->desc,
                                 surface.file != BAD_FILE ?
                                 surface : surface_handle, 0);

   if (alignment < 4) {
      /* The byte scattered messages can only read one dword at a time so
       * we have to duplicate the message 4 times to read the full vec4.
       * Hopefully, dead code will clean up the mess if some of them aren't
       * needed.
       */
      assert(send->size_written == 16 * send->exec_size);
      send->size_written /= 4;
      for (unsigned c = 1; c < 4; c++) {
         /* Emit a copy of the instruction because we're about to modify
          * it.  Because this loop starts at 1, we will emit copies for the
          * first 3 and the final one will be the modified instruction.
          */
         bld.emit(brw_clone_inst(*bld.shader, send));

         /* Offset the source */
         send->src[SEND_SRC_PAYLOAD1] = bld.vgrf(BRW_TYPE_UD);
         bld.ADD(send->src[SEND_SRC_PAYLOAD1], ubo_offset, brw_imm_ud(c * 4));

         /* Offset the destination */
         send->dst = offset(send->dst, bld, 1);
      }
   }
}

static void
lower_varying_pull_constant_logical_send(const brw_builder &bld, brw_inst *inst)
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

   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->mlen = send->exec_size / 8;

   /* src[SEND_SRC_DESC/EX_DESC] are filled by setup_surface_descriptors() */
   send->src[SEND_SRC_PAYLOAD1] = ubo_offset;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();

   if (compiler->indirect_ubos_use_sampler) {
      const unsigned simd_mode =
         send->exec_size <= 8 ? BRW_SAMPLER_SIMD_MODE_SIMD8 :
                                BRW_SAMPLER_SIMD_MODE_SIMD16;
      const uint32_t desc = brw_sampler_desc(devinfo, 0, 0,
                                             GFX5_SAMPLER_MESSAGE_SAMPLE_LD,
                                             simd_mode, 0);

      send->sfid = BRW_SFID_SAMPLER;
      setup_surface_descriptors(bld, send, desc, surface, surface_handle);
   } else if (alignment >= 4) {
      const uint32_t desc =
         brw_dp_untyped_surface_rw_desc(devinfo, send->exec_size,
                                        4, /* num_channels */
                                        false   /* write */);

      send->sfid = BRW_SFID_HDC1;
      setup_surface_descriptors(bld, send, desc, surface, surface_handle);
   } else {
      const uint32_t desc =
         brw_dp_byte_scattered_rw_desc(devinfo, send->exec_size,
                                       32,     /* bit_size */
                                       false   /* write */);

      send->sfid = BRW_SFID_HDC0;
      setup_surface_descriptors(bld, send, desc, surface, surface_handle);

      /* The byte scattered messages can only read one dword at a time so
       * we have to duplicate the message 4 times to read the full vec4.
       * Hopefully, dead code will clean up the mess if some of them aren't
       * needed.
       */
      assert(send->size_written == 16 * send->exec_size);
      send->size_written /= 4;
      for (unsigned c = 1; c < 4; c++) {
         /* Emit a copy of the instruction because we're about to modify
          * it.  Because this loop starts at 1, we will emit copies for the
          * first 3 and the final one will be the modified instruction.
          */
         bld.emit(brw_clone_inst(*bld.shader, send));

         /* Offset the source */
         send->src[SEND_SRC_PAYLOAD1] = bld.vgrf(BRW_TYPE_UD);
         bld.ADD(send->src[SEND_SRC_PAYLOAD1], ubo_offset, brw_imm_ud(c * 4));

         /* Offset the destination */
         send->dst = offset(send->dst, bld, 1);
      }
   }
}

static void
lower_interpolator_logical_send(const brw_builder &bld, brw_inst *inst,
                                const struct brw_wm_prog_key *wm_prog_key,
                                const struct brw_wm_prog_data *wm_prog_data)
{
   assert(inst->src[INTERP_SRC_NOPERSPECTIVE].file == IMM);

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
      payload = bld.move_to_vgrf(inst->src[INTERP_SRC_OFFSET], 2);
      mlen = 2 * inst->exec_size / 8;
      mode = GFX7_PIXEL_INTERPOLATOR_LOC_PER_SLOT_OFFSET;
      break;

   default:
      UNREACHABLE("Invalid interpolator instruction");
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
                            inst->src[INTERP_SRC_NOPERSPECTIVE].ud,
                            false /* coarse_pixel_rate */,
                            inst->exec_size, inst->group);

   if (wm_prog_data->coarse_pixel_dispatch == INTEL_ALWAYS) {
      desc_imm |= (1 << 15);
   } else if (wm_prog_data->coarse_pixel_dispatch == INTEL_SOMETIMES) {
      STATIC_ASSERT(INTEL_MSAA_FLAG_COARSE_PI_MSG == (1 << 15));
      brw_reg orig_desc = desc;
      const brw_builder &ubld = bld.exec_all().group(8, 0);
      desc = ubld.vgrf(BRW_TYPE_UD);
      ubld.AND(desc, brw_dynamic_msaa_flags(wm_prog_data),
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
      const brw_builder &ubld = bld.exec_all().group(8, 0);
      desc = ubld.vgrf(BRW_TYPE_UD);

      /* The predicate should have been built in brw_from_nir.cpp when emitting
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

   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->sfid = BRW_SFID_PIXEL_INTERPOLATOR;
   send->desc = desc_imm;
   send->ex_desc = 0;
   send->mlen = mlen;
   send->ex_mlen = 0;
   send->has_side_effects = false;
   send->is_volatile = false;

   send->src[SEND_SRC_DESC]     = component(desc, 0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
}

static void
lower_btd_logical_send(const brw_builder &bld, brw_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const unsigned unit = reg_unit(devinfo);
   const unsigned mlen = 2 * unit;
   const brw_builder ubld = bld.exec_all();
   brw_reg header = ubld.vgrf(BRW_TYPE_UD, 2 * unit);

   ubld.MOV(header, brw_imm_ud(0));
   switch (inst->opcode) {
   case SHADER_OPCODE_BTD_SPAWN_LOGICAL: {
      brw_reg global_addr = inst->src[0];
      assert(brw_type_size_bytes(global_addr.type) == 8 &&
             global_addr.stride == 0);
      global_addr.type = BRW_TYPE_UD;
      global_addr.stride = 1;
      ubld.group(2, 0).MOV(header, global_addr);

      /* XXX - There is a Registers Per Thread field in the BTD spawn
       *       header starting on Xe3, it doesn't appear to be needed
       *       by the hardware so we don't set it.  If it's ever
       *       needed though we will need some sort of reloc since
       *       we'll have to initialize it based on the prog_data
       *       structure of the callee.
       */
      break;
   }

   case SHADER_OPCODE_BTD_RETIRE_LOGICAL:
      /* The bottom bit is the Stack ID release bit */
      ubld.group(1, 0).MOV(header, brw_imm_ud(1));
      break;

   default:
      UNREACHABLE("Invalid BTD message");
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
      const brw_reg btd_record = inst->src[1];
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

   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->mlen = mlen;
   send->ex_mlen = ex_mlen;
   send->header_size = 0; /* HW docs require has_header = false */
   send->has_side_effects = true;
   send->is_volatile = false;

   /* Set up SFID and descriptors */
   send->sfid = BRW_SFID_BINDLESS_THREAD_DISPATCH;
   send->desc = brw_btd_spawn_desc(devinfo, send->exec_size,
                                   GEN_RT_BTD_MESSAGE_SPAWN);

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = header;
   send->src[SEND_SRC_PAYLOAD2] = payload;
}

static void
lower_trace_ray_logical_send(const brw_builder &bld, brw_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   /* The emit_uniformize() in brw_from_nir.cpp will generate an horizontal
    * stride of 0. Below we're doing a MOV() in SIMD2. Since we can't use UQ/Q
    * types in on Gfx12.5, we need to tweak the stride with a value of 1 dword
    * so that the MOV operates on 2 components rather than twice the same
    * component.
    */
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
   const brw_builder ubld = bld.exec_all();
   brw_reg header = ubld.vgrf(BRW_TYPE_UD);
   ubld.MOV(header, brw_imm_ud(0));

   const brw_reg globals_addr = inst->src[RT_LOGICAL_SRC_GLOBALS];
   if (globals_addr.file != UNIFORM) {
      brw_reg addr_ud = retype(globals_addr, BRW_TYPE_UD);
      addr_ud.stride = 1;
      ubld.group(2, 0).MOV(header, addr_ud);
   } else {
      /* If the globals address comes from a uniform, do not do the SIMD2
       * optimization. This occurs in many Vulkan CTS tests.
       *
       * Many places in the late compiler, including but not limited to an
       * assertion in brw_shader::assign_curb_setup, assume that all uses of a
       * UNIFORM will be uniform (i.e., <0,1,0>). The clever SIMD2
       * optimization violates that assumption.
       */
      ubld.group(1, 0).MOV(byte_offset(header, 0),
                           subscript(globals_addr, BRW_TYPE_UD, 0));
      ubld.group(1, 0).MOV(byte_offset(header, 4),
                           subscript(globals_addr, BRW_TYPE_UD, 1));
   }

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
      /* For Xe2+, Bspec 64643:
       * "StackID": The maximum number of StackIDs can be 2^12- 1.
       *
       * For platforms < Xe2, The maximum number of StackIDs can be 2^11 - 1.
       */
      brw_reg stack_id_mask = devinfo->ver >= 20 ?
                              brw_imm_uw(0xfff) :
                              brw_imm_uw(0x7ff);
      bld.AND(subscript(payload, BRW_TYPE_UW, 1),
              retype(brw_vec8_grf(1 * unit, 0), BRW_TYPE_UW),
              stack_id_mask);
   }

   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->mlen = mlen;
   send->ex_mlen = ex_mlen;
   send->header_size = 0; /* HW docs require has_header = false */
   send->has_side_effects = true;
   send->is_volatile = false;

   /* Set up SFID and descriptors */
   send->sfid = BRW_SFID_RAY_TRACE_ACCELERATOR;
   send->desc = brw_rt_trace_ray_desc(devinfo, send->exec_size);

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = header;
   send->src[SEND_SRC_PAYLOAD2] = payload;
}

static void
lower_lsc_memory_fence_and_interlock(const brw_builder &bld, struct brw_send_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const bool interlock = inst->opcode == SHADER_OPCODE_INTERLOCK;

   assert(inst->src[1].file == IMM);
   const bool commit_enable = inst->src[1].ud;
   assert(commit_enable); /* always used */

   brw_reg header = inst->src[0];

   assert(inst->size_written == reg_unit(devinfo) * REG_SIZE);

   const uint32_t intrinsic_desc = inst->desc;
   const uint8_t sfid = inst->sfid;
   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->sfid = sfid;
   send->check_tdr = interlock;
   send->has_side_effects = true;

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = retype(vec1(header), BRW_TYPE_UD);
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
   send->mlen = reg_unit(devinfo);
   send->ex_mlen = 0;

   /* On Gfx12.5 URB is not listed as port usable for fences with the LSC (see
    * BSpec 53578 for Gfx12.5, BSpec 57330 for Gfx20), so we completely ignore
    * the descriptor value and rebuild a legacy URB fence descriptor.
    */
   if (send->sfid == BRW_SFID_URB && devinfo->ver < 20) {
      send->desc = brw_urb_fence_desc(devinfo);
      send->header_size = 1;
   } else {
      enum lsc_fence_scope scope =
         lsc_fence_msg_desc_scope(devinfo, intrinsic_desc);
      enum lsc_flush_type flush_type =
         lsc_fence_msg_desc_flush_type(devinfo, intrinsic_desc);

      /* Wa_14012437816:
       *
       *   "For any fence greater than local scope, always set flush type to
       *    at least invalidate so that fence goes on properly."
       *
       *   "The bug is if flush_type is 'None', the scope is always downgraded
       *    to 'local'."
       *
       * Here set scope to NONE_6 instead of NONE, which has the same effect
       * as NONE but avoids the downgrade to scope LOCAL.
       */
      if (intel_needs_workaround(devinfo, 14012437816) &&
          scope > LSC_FENCE_LOCAL &&
          flush_type == LSC_FLUSH_TYPE_NONE) {
         flush_type = LSC_FLUSH_TYPE_NONE_6;
      }

      send->desc = lsc_fence_msg_desc(devinfo, scope, flush_type, false);
   }
}

static void
lower_hdc_memory_fence_and_interlock(const brw_builder &bld, brw_send_inst *inst)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const bool interlock = inst->opcode == SHADER_OPCODE_INTERLOCK;

   assert(inst->src[1].file == IMM);

   brw_reg header = inst->src[0];
   const bool commit_enable = inst->src[1].ud;

   bool slm = false;

   if (inst->sfid == BRW_SFID_SLM) {
      assert(devinfo->ver >= 11);

      /* This SFID doesn't exist on Gfx11-12.0, but we use it to represent
       * SLM fences, and map back here to the way Gfx11 represented that:
       * a special "SLM" binding table index and the data cache SFID.
       */
      inst->sfid = BRW_SFID_HDC0;
      slm = true;
   }

   assert(inst->size_written == (commit_enable ? REG_SIZE : 0));

   const uint8_t sfid = inst->sfid;
   brw_send_inst *send = brw_transform_inst_to_send(bld, inst);
   inst = NULL;

   send->sfid = sfid;
   send->check_tdr = interlock;
   send->has_side_effects = true;

   send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = retype(vec1(header), BRW_TYPE_UD);
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();
   send->mlen = reg_unit(devinfo);
   send->ex_mlen = 0;
   send->header_size = 1;

   const unsigned msg_type =
      send->sfid == BRW_SFID_RENDER_CACHE ? GFX7_DATAPORT_RC_MEMORY_FENCE :
                                            GFX7_DATAPORT_DC_MEMORY_FENCE;

   send->desc = brw_dp_desc(devinfo, slm ? GFX7_BTI_SLM : 0, msg_type,
                            commit_enable ? 1 << 5 : 0);
}

bool
brw_lower_logical_sends(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      const brw_builder ibld(inst);

      switch (inst->opcode) {
      case FS_OPCODE_FB_WRITE_LOGICAL:
         assert(s.stage == MESA_SHADER_FRAGMENT);
         lower_fb_write_logical_send(ibld, inst->as_fb_write(),
                                     brw_wm_prog_data(s.prog_data),
                                     (const brw_wm_prog_key *)s.key,
                                     s.fs_payload());
         break;

      case FS_OPCODE_FB_READ_LOGICAL:
         lower_fb_read_logical_send(ibld, inst, brw_wm_prog_data(s.prog_data));
         break;

      case SHADER_OPCODE_SAMPLER:
         lower_sampler_logical_send(ibld, inst->as_tex());
         break;

      case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
      case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
      case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL: {
         brw_mem_inst *mem = inst->as_mem();
         if (devinfo->ver >= 20 ||
             (devinfo->has_lsc && mem->mode != MEMORY_MODE_TYPED))
            lower_lsc_memory_logical_send(ibld, mem);
         else
            lower_hdc_memory_logical_send(ibld, mem);
         break;
      }

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
            lower_urb_read_logical_send(ibld, inst->as_urb());
         else
            lower_urb_read_logical_send_xe2(ibld, inst->as_urb());
         break;

      case SHADER_OPCODE_URB_WRITE_LOGICAL:
         if (devinfo->ver < 20)
            lower_urb_write_logical_send(ibld, inst->as_urb());
         else
            lower_urb_write_logical_send_xe2(ibld, inst->as_urb());

         break;

      case SHADER_OPCODE_MEMORY_FENCE:
      case SHADER_OPCODE_INTERLOCK:
         if (devinfo->has_lsc)
            lower_lsc_memory_fence_and_interlock(ibld, inst->as_send());
         else
            lower_hdc_memory_fence_and_interlock(ibld, inst->as_send());
         break;

      default:
         continue;
      }

      progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

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
brw_lower_uniform_pull_constant_loads(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe (block, brw_inst, inst, s.cfg) {
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
         const brw_builder ubld = brw_builder(inst).group(8, 0).exec_all();

         const brw_reg payload = ubld.vgrf(BRW_TYPE_UD);
         ubld.MOV(payload, offset_B);

         brw_send_inst *send = brw_transform_inst_to_send(ubld, inst);
         inst = NULL;

         send->sfid = BRW_SFID_UGM;
         send->desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                                   surface_handle.file == BAD_FILE ?
                                   LSC_ADDR_SURFTYPE_BTI :
                                   LSC_ADDR_SURFTYPE_BSS,
                                   LSC_ADDR_SIZE_A32,
                                   LSC_DATA_SIZE_D32,
                                   send->size_written / 4,
                                   true /* transpose */,
                                   LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));

         send->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, 1);
         send->bindless_surface = surface_handle.file != BAD_FILE;
         send->ex_mlen = 0;
         send->header_size = 0;
         send->has_side_effects = false;
         send->is_volatile = true;
         send->exec_size = 1;

         /* Finally, the payload */
         setup_lsc_surface_descriptors(ubld, send, send->desc,
                                       surface.file != BAD_FILE ?
                                       surface : surface_handle, 0);
         send->src[SEND_SRC_PAYLOAD1] = payload;
         send->src[SEND_SRC_PAYLOAD2] = brw_reg();

         s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                               BRW_DEPENDENCY_VARIABLES);
      } else {
         const brw_builder ubld = brw_builder(inst).exec_all();
         brw_reg header = brw_builder(&s, 8).exec_all().vgrf(BRW_TYPE_UD);

         ubld.group(8, 0).MOV(header,
                              retype(brw_vec8_grf(0, 0), BRW_TYPE_UD));
         ubld.group(1, 0).MOV(component(header, 2),
                              brw_imm_ud(offset_B.ud / 16));

         brw_send_inst *send = brw_transform_inst_to_send(ubld, inst);
         inst = NULL;

         send->sfid = BRW_SFID_HDC_READ_ONLY;
         send->header_size = 1;
         send->mlen = 1;

         uint32_t desc =
            brw_dp_oword_block_rw_desc(devinfo, true /* align_16B */,
                                       size_B.ud / 4, false /* write */);

         setup_surface_descriptors(ubld, send, desc, surface, surface_handle);

         send->src[SEND_SRC_PAYLOAD1] = header;
         send->src[SEND_SRC_PAYLOAD2] = brw_reg(); /* unused for reads */

         s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                               BRW_DEPENDENCY_VARIABLES);
      }

      progress = true;
   }

   return progress;
}

bool
brw_lower_send_descriptors(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst (block, brw_inst, inst, s.cfg) {
      if (inst->opcode != SHADER_OPCODE_SEND &&
          inst->opcode != SHADER_OPCODE_SEND_GATHER)
         continue;

      brw_send_inst *send = inst->as_send();

      const brw_builder ubld = brw_builder(send).uniform();

      /* Descriptor */
      const unsigned rlen = send->dst.is_null() ? 0 : send->size_written / REG_SIZE;
      unsigned mlen = send->mlen;
      if (send->opcode == SHADER_OPCODE_SEND_GATHER) {
         assert(send->sources >= 3);
         mlen = (send->sources - 3) * reg_unit(devinfo);
      }

      uint32_t desc_imm = send->desc |
         brw_message_desc(devinfo, mlen, rlen, send->header_size);

      assert(send->src[SEND_SRC_DESC].file != BAD_FILE);
      assert(send->src[SEND_SRC_EX_DESC].file != BAD_FILE);

      brw_reg desc = send->src[SEND_SRC_DESC];
      if (desc.file == IMM) {
         send->src[SEND_SRC_DESC] = brw_imm_ud(desc.ud | desc_imm);
      } else {
         brw_reg addr_reg = ubld.vaddr(BRW_TYPE_UD,
                                       BRW_ADDRESS_SUBREG_INDIRECT_DESC);
         ubld.OR(addr_reg, desc, brw_imm_ud(desc_imm));
         send->src[SEND_SRC_DESC] = addr_reg;
      }

      /* Extended descriptor */
      brw_reg ex_desc = send->src[SEND_SRC_EX_DESC];
      uint32_t ex_desc_imm = send->ex_desc |
         brw_message_ex_desc(devinfo, send->ex_mlen);

      if (ex_desc.file == IMM && !send->bindless_surface)
         ex_desc_imm |= ex_desc.ud;

      bool needs_addr_reg = false;
      if (ex_desc.file != IMM || send->bindless_surface)
         needs_addr_reg = true;
      if (devinfo->ver < 12 && ex_desc.file == IMM &&
          (ex_desc_imm & INTEL_MASK(15, 12)) != 0)
         needs_addr_reg = true;

      if (send->bindless_surface &&
          s.compiler->extended_bindless_surface_offset) {
         needs_addr_reg = true;
         /* When using the extended bindless offset, the whole extended
          * descriptor is the surface handle.
          */
         ex_desc_imm = 0;
      } else {
         if (needs_addr_reg)
            ex_desc_imm |=  send->sfid | send->eot << 5;
      }

      if (needs_addr_reg) {
         brw_reg addr_reg = ubld.vaddr(BRW_TYPE_UD,
                                       BRW_ADDRESS_SUBREG_INDIRECT_EX_DESC);
         if (ex_desc.file == IMM && !send->bindless_surface)
            ubld.MOV(addr_reg, brw_imm_ud(ex_desc_imm));
         else if (ex_desc_imm == 0)
            ubld.MOV(addr_reg, ex_desc);
         else
            ubld.OR(addr_reg, ex_desc, brw_imm_ud(ex_desc_imm));
         send->src[SEND_SRC_EX_DESC] = addr_reg;
      } else {
         send->src[SEND_SRC_EX_DESC] = brw_imm_ud(ex_desc_imm);
      }

      progress = true;
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS | BRW_DEPENDENCY_VARIABLES);
   }

   return progress;
}
