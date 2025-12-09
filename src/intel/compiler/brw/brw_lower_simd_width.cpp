/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_builder.h"

static bool
is_mixed_float_with_fp32_dst(const brw_inst *inst)
{
   if (inst->dst.type != BRW_TYPE_F)
      return false;

   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].type == BRW_TYPE_HF)
         return true;
   }

   return false;
}

static bool
is_mixed_float_with_packed_fp16_dst(const brw_inst *inst)
{
   if (inst->dst.type != BRW_TYPE_HF || inst->dst.stride != 1)
      return false;

   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].type == BRW_TYPE_F)
         return true;
   }

   return false;
}

/**
 * Get the closest allowed SIMD width for instruction \p inst accounting for
 * some common regioning and execution control restrictions that apply to FPU
 * instructions.  These restrictions don't necessarily have any relevance to
 * instructions not executed by the FPU pipeline like extended math, control
 * flow or send message instructions.
 *
 * For virtual opcodes it's really up to the instruction -- In some cases
 * (e.g. where a virtual instruction unrolls into a simple sequence of FPU
 * instructions) it may simplify virtual instruction lowering if we can
 * enforce FPU-like regioning restrictions already on the virtual instruction,
 * in other cases (e.g. virtual send-like instructions) this may be
 * excessively restrictive.
 */
static unsigned
get_fpu_lowered_simd_width(const brw_shader *shader,
                           const brw_inst *inst)
{
   const struct brw_compiler *compiler = shader->compiler;
   const struct intel_device_info *devinfo = compiler->devinfo;

   /* Maximum execution size representable in the instruction controls. */
   unsigned max_width = MIN2(32, inst->exec_size);

   /* Number of channels per polygon handled by a multipolygon PS shader. */
   const unsigned poly_width = shader->dispatch_width /
                               MAX2(1, shader->max_polygons);

   /* Number of registers that will be read by an ATTR source if
    * present for multipolygon PS shaders, since the PS vertex setup
    * data for each polygon is stored in different contiguous GRFs.
    */
   const unsigned attr_reg_count = (shader->stage != MESA_SHADER_FRAGMENT ||
                                    shader->max_polygons < 2 ? 0 :
                                    DIV_ROUND_UP(inst->exec_size,
                                                 poly_width) * reg_unit(devinfo));

   /* According to the PRMs:
    *  "A. In Direct Addressing mode, a source cannot span more than 2
    *      adjacent GRF registers.
    *   B. A destination cannot span more than 2 adjacent GRF registers."
    *
    * Look for the source or destination with the largest register region
    * which is the one that is going to limit the overall execution size of
    * the instruction due to this rule.
    */
   unsigned reg_count = DIV_ROUND_UP(inst->size_written, REG_SIZE);

   for (unsigned i = 0; i < inst->sources; i++)
      reg_count = MAX3(reg_count, DIV_ROUND_UP(inst->size_read(devinfo, i), REG_SIZE),
                       (inst->src[i].file == ATTR ? attr_reg_count : 0));

   /* Calculate the maximum execution size of the instruction based on the
    * factor by which it goes over the hardware limit of 2 GRFs.
    */
   const unsigned max_reg_count = 2 * reg_unit(devinfo);
   if (reg_count > max_reg_count)
      max_width = MIN2(max_width, inst->exec_size / DIV_ROUND_UP(reg_count, max_reg_count));

   /* From the IVB PRMs (applies to HSW too):
    *  "Instructions with condition modifiers must not use SIMD32."
    *
    * From the BDW PRMs (applies to later hardware too):
    *  "Ternary instruction with condition modifiers must not use SIMD32."
    */
   if (inst->conditional_mod && inst->is_3src(compiler) && devinfo->ver < 12)
      max_width = MIN2(max_width, 16);

   /* From the IVB PRMs (applies to other devices that don't have the
    * intel_device_info::supports_simd16_3src flag set):
    *  "In Align16 access mode, SIMD16 is not allowed for DW operations and
    *   SIMD8 is not allowed for DF operations."
    */
   if (inst->is_3src(compiler) && !devinfo->supports_simd16_3src)
      max_width = MIN2(max_width, inst->exec_size / reg_count);

   if (has_bfloat_operands(inst))
      max_width = MIN2(max_width, devinfo->ver < 20 ? 8 : 16);

   if (inst->opcode != BRW_OPCODE_MOV) {
      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "No SIMD16 in mixed mode when destination is f32. Instruction
       *     execution size must be no more than 8."
       *
       * Testing indicates that this restriction does not apply to MOVs.
       */
      if (is_mixed_float_with_fp32_dst(inst) && devinfo->ver < 20)
         max_width = MIN2(max_width, 8);

      /* From the SKL PRM, Special Restrictions for Handling Mixed Mode
       * Float Operations:
       *
       *    "No SIMD16 in mixed mode when destination is packed f16 for both
       *     Align1 and Align16."
       */
      if (is_mixed_float_with_packed_fp16_dst(inst) && devinfo->ver < 20)
         max_width = MIN2(max_width, 8);
   }

   /* Only power-of-two execution sizes are representable in the instruction
    * control fields.
    */
   return 1 << util_logbase2(max_width);
}

/**
 * Get the maximum allowed SIMD width for instruction \p inst accounting for
 * various payload size restrictions that apply to sampler message
 * instructions.
 *
 * This is only intended to provide a maximum theoretical bound for the
 * execution size of the message based on the number of argument components
 * alone, which in most cases will determine whether the SIMD8 or SIMD16
 * variant of the message can be used, though some messages may have
 * additional restrictions not accounted for here (e.g. pre-ILK hardware uses
 * the message length to determine the exact SIMD width and argument count,
 * which makes a number of sampler message combinations impossible to
 * represent).
 *
 * Note: Platforms with monolithic SIMD16 double the possible SIMD widths
 * change from (SIMD8, SIMD16) to (SIMD16, SIMD32).
 */
static unsigned
get_sampler_lowered_simd_width(const struct intel_device_info *devinfo,
                               const brw_tex_inst *tex)
{
   /* TXD is unsupported in SIMD16 mode previous to Xe2. SIMD32 is still
    * unsuppported on Xe2.
    */
   if (tex->sampler_opcode == BRW_SAMPLER_OPCODE_SAMPLE_D ||
       tex->sampler_opcode == BRW_SAMPLER_OPCODE_SAMPLE_D_REDUCED ||
       tex->sampler_opcode == BRW_SAMPLER_OPCODE_SAMPLE_D_C ||
       tex->sampler_opcode == BRW_SAMPLER_OPCODE_SAMPLE_D_C_PACKED)
      return devinfo->ver < 20 ? 8 : 16;

   const unsigned max_payload_size =
      MAX_SAMPLER_MESSAGE_SIZE *
      (reg_unit(devinfo) * 8) /* min SIMD */ *
      4 /* dword */;
   const unsigned payload_param_size =
      brw_type_size_bytes(tex->src[TEX_LOGICAL_SRC_PAYLOAD0].type);
   unsigned payload_size =
      (tex->sources - TEX_LOGICAL_SRC_PAYLOAD0) *
      tex->exec_size *
      payload_param_size;

   unsigned simd_width = tex->exec_size;
   while (payload_size > max_payload_size) {
      payload_size /= 2;
      simd_width /= 2;
   }

   const unsigned max_hw_simd = devinfo->ver < 20 ? 16 : 32;

   return MIN2(simd_width, max_hw_simd);
}

static bool
is_half_float_src_dst(const brw_inst *inst)
{
   if (inst->dst.type == BRW_TYPE_HF)
      return true;

   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].type == BRW_TYPE_HF)
         return true;
   }

   return false;
}

/**
 * Send instructions are writing physical registers so it's important to
 * allocate physically aligned register size when lowering. With types >=
 * 4bytes this is always the case but with fp16 sampler loads it's not.
 */
static bool
is_send_inst(const brw_inst *inst)
{
   switch (inst->opcode) {
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
   case FS_OPCODE_FB_WRITE_LOGICAL:
   case FS_OPCODE_FB_READ_LOGICAL:
   case SHADER_OPCODE_SAMPLER:
   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
   case SHADER_OPCODE_URB_READ_LOGICAL:
   case SHADER_OPCODE_URB_WRITE_LOGICAL:
      return true;

   default:
      return false;
   }
}

/**
 * Get the closest native SIMD width supported by the hardware for instruction
 * \p inst.  The instruction will be left untouched by
 * brw_shader::lower_simd_width() if the returned value is equal to the
 * original execution size.
 */
unsigned
brw_get_lowered_simd_width(const brw_shader *shader, const brw_inst *inst)
{
   const struct brw_compiler *compiler = shader->compiler;
   const struct intel_device_info *devinfo = compiler->devinfo;

   switch (inst->opcode) {
   case BRW_OPCODE_DP4A:
   case BRW_OPCODE_MOV:
   case BRW_OPCODE_SEL:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_AND:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_XOR:
   case BRW_OPCODE_SHR:
   case BRW_OPCODE_SHL:
   case BRW_OPCODE_ASR:
   case BRW_OPCODE_ROR:
   case BRW_OPCODE_ROL:
   case BRW_OPCODE_CMPN:
   case BRW_OPCODE_CSEL:
   case BRW_OPCODE_BFREV:
   case BRW_OPCODE_BFE:
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_MUL:
   case BRW_OPCODE_AVG:
   case BRW_OPCODE_FRC:
   case BRW_OPCODE_RNDU:
   case BRW_OPCODE_RNDD:
   case BRW_OPCODE_RNDE:
   case BRW_OPCODE_RNDZ:
   case BRW_OPCODE_LZD:
   case BRW_OPCODE_FBH:
   case BRW_OPCODE_FBL:
   case BRW_OPCODE_CBIT:
   case BRW_OPCODE_MAD:
   case BRW_OPCODE_LRP:
   case BRW_OPCODE_ADD3:
   case FS_OPCODE_PACK:
   case SHADER_OPCODE_SEL_EXEC:
   case SHADER_OPCODE_CLUSTER_BROADCAST:
   case SHADER_OPCODE_MOV_RELOC_IMM:
   case BRW_OPCODE_CMP:
   case BRW_OPCODE_BFI1:
   case BRW_OPCODE_BFI2:
   case BRW_OPCODE_BFN:
      return get_fpu_lowered_simd_width(shader, inst);

   case SHADER_OPCODE_RCP:
   case SHADER_OPCODE_RSQ:
   case SHADER_OPCODE_SQRT:
   case SHADER_OPCODE_EXP2:
   case SHADER_OPCODE_LOG2:
   case SHADER_OPCODE_SIN:
   case SHADER_OPCODE_COS: {
      /* Xe2+: BSpec 56797
       *
       * Math operation rules when half-floats are used on both source and
       * destination operands and both source and destinations are packed.
       *
       * The execution size must be 16.
       */
      if (is_half_float_src_dst(inst))
         return devinfo->ver < 20 ? MIN2(8,  inst->exec_size) :
                                    MIN2(16, inst->exec_size);
      if (devinfo->ver < 20)
         return MIN2(16, inst->exec_size);

      return inst->exec_size;
   }

   case SHADER_OPCODE_POW: {
      /* SIMD16 is only allowed on Gfx7+. Extended Math Function is limited
       * to SIMD8 with half-float
       */
      if (is_half_float_src_dst(inst))
        return MIN2(8,  inst->exec_size);
      return MIN2(16, inst->exec_size);
   }

   case SHADER_OPCODE_USUB_SAT:
   case SHADER_OPCODE_ISUB_SAT:
      return get_fpu_lowered_simd_width(shader, inst);

   case SHADER_OPCODE_INT_QUOTIENT:
   case SHADER_OPCODE_INT_REMAINDER:
      /* Integer division is limited to SIMD8 on all generations. */
      return MIN2(8, inst->exec_size);

   case BRW_OPCODE_PLN:
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
   case FS_OPCODE_PACK_HALF_2x16_SPLIT:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
   case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL:
   case FS_OPCODE_DDX_COARSE:
   case FS_OPCODE_DDX_FINE:
   case FS_OPCODE_DDY_COARSE:
   case FS_OPCODE_DDY_FINE:
      return MIN2(16, inst->exec_size);

   case SHADER_OPCODE_MULH:
      /* MULH is lowered to the MUL/MACH sequence using the accumulator, which
       * is 8-wide on Gfx7+.
       */
      return devinfo->ver >= 20 ? 16 : 8;

   case FS_OPCODE_FB_WRITE_LOGICAL:
      if (devinfo->ver >= 20) {
         /* Dual-source FB writes are unsupported in SIMD32 mode. */
         return (inst->src[FB_WRITE_LOGICAL_SRC_COLOR1].file != BAD_FILE ?
                 16 : MIN2(32, inst->exec_size));
      } else {
         /* Dual-source FB writes are unsupported in SIMD16 mode. */
         return (inst->src[FB_WRITE_LOGICAL_SRC_COLOR1].file != BAD_FILE ?
                 8 : MIN2(16, inst->exec_size));
      }

   case FS_OPCODE_FB_READ_LOGICAL:
      return MIN2(16, inst->exec_size);

   case SHADER_OPCODE_SAMPLER:
      return get_sampler_lowered_simd_width(devinfo, inst->as_tex());

   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL: {
      const brw_mem_inst *mem = inst->as_mem();

      if (devinfo->ver >= 20)
         return mem->exec_size;

      if (mem->mode == MEMORY_MODE_TYPED)
         return 8;

      /* HDC A64 atomics are limited to SIMD8 */
      if (!devinfo->has_lsc &&
          mem->binding_type == LSC_ADDR_SURFTYPE_FLAT &&
          lsc_opcode_is_atomic(mem->lsc_op))
         return 8;

      return MIN2(16, mem->exec_size);
   }

   case SHADER_OPCODE_URB_READ_LOGICAL:
   case SHADER_OPCODE_URB_WRITE_LOGICAL:
      return MIN2(devinfo->ver < 20 ? 8 : 16, inst->exec_size);

   case SHADER_OPCODE_QUAD_SWIZZLE: {
      const unsigned swiz = inst->src[1].ud;
      return (is_uniform(inst->src[0]) ?
                 get_fpu_lowered_simd_width(shader, inst) :
              devinfo->ver < 11 && brw_type_size_bytes(inst->src[0].type) == 4 ? 8 :
              swiz == BRW_SWIZZLE_XYXY || swiz == BRW_SWIZZLE_ZWZW ? 4 :
              get_fpu_lowered_simd_width(shader, inst));
   }
   case SHADER_OPCODE_MOV_INDIRECT:
   case SHADER_OPCODE_LOAD_ATTRIBUTE_PAYLOAD: {
      /* From IVB and HSW PRMs:
       *
       * "2.When the destination requires two registers and the sources are
       *  indirect, the sources must use 1x1 regioning mode.
       *
       * In case of DF instructions in HSW/IVB, the exec_size is limited by
       * the EU decompression logic not handling VxH indirect addressing
       * correctly.
       */
      const unsigned max_size = 2 * REG_SIZE;
      /* Prior to Broadwell, we only have 8 address subregisters. */
      return MIN3(16,
                  max_size / (inst->dst.stride * brw_type_size_bytes(inst->dst.type)),
                  inst->exec_size);
   }

   case SHADER_OPCODE_LOAD_PAYLOAD: {
      const brw_load_payload_inst *lp = inst->as_load_payload();

      const unsigned reg_count =
         DIV_ROUND_UP(lp->dst.component_size(lp->exec_size),
                      REG_SIZE * reg_unit(devinfo));

      if (reg_count > 2) {
         /* Only LOAD_PAYLOAD instructions with per-channel destination region
          * can be easily lowered (which excludes headers and heterogeneous
          * types).
          */
         assert(!lp->header_size);
         for (unsigned i = 0; i < lp->sources; i++)
            assert(brw_type_size_bits(lp->dst.type) == brw_type_size_bits(lp->src[i].type) ||
                   lp->src[i].file == BAD_FILE);

         return lp->exec_size / DIV_ROUND_UP(reg_count, 2);
      } else {
         return lp->exec_size;
      }
   }
   default:
      return inst->exec_size;
   }
}

/**
 * Return true if splitting out the group of channels of instruction \p inst
 * given by lbld.group() requires allocating a temporary for the i-th source
 * of the lowered instruction.
 */
static inline bool
needs_src_copy(const brw_builder &lbld, const brw_inst *inst, unsigned i)
{
   /* The indirectly indexed register stays the same even if we split the
    * instruction.
    */
   if (inst->opcode == SHADER_OPCODE_MOV_INDIRECT && i == 0)
      return false;

   return !inst->src[i].is_scalar &&
          (!(is_periodic(inst->src[i], lbld.dispatch_width()) ||
             (inst->components_read(i) == 1 &&
              lbld.dispatch_width() <= inst->exec_size)) ||
           (inst->flags_written(lbld.shader->devinfo) &
            brw_flag_mask(inst->src[i], brw_type_size_bytes(inst->src[i].type))));
}

/**
 * Extract the data that would be consumed by the channel group given by
 * lbld.group() from the i-th source region of instruction \p inst and return
 * it as result in packed form.
 */
static brw_reg
emit_unzip(const brw_builder &lbld, brw_inst *inst, unsigned i)
{
   assert(lbld.group() >= inst->group);

   /* Specified channel group from the source region. */
   const brw_reg src = horiz_offset(inst->src[i], lbld.group() - inst->group);

   if (needs_src_copy(lbld, inst, i)) {
      const unsigned num_components = inst->components_read(i);
      const brw_reg tmp = lbld.vgrf(inst->src[i].type, num_components);

      assert(num_components <= NIR_MAX_VEC_COMPONENTS);
      brw_reg comps[NIR_MAX_VEC_COMPONENTS];

      for (unsigned k = 0; k < num_components; ++k)
         comps[k] = offset(src, inst->exec_size, k);
      lbld.VEC(tmp, comps, num_components);

      return tmp;
   } else if (is_periodic(inst->src[i], lbld.dispatch_width()) ||
              (inst->opcode == SHADER_OPCODE_MOV_INDIRECT && i == 0) ||
              inst->src[i].is_scalar) {
      /* The source is invariant for all dispatch_width-wide groups of the
       * original region.
       *
       * The src[0] of MOV_INDIRECT is invariant regardless of the execution
       * size.
       */
      return inst->src[i];

   } else {
      /* We can just point the lowered instruction at the right channel group
       * from the original region.
       */
      return src;
   }
}

/**
 * Return true if splitting out the group of channels of instruction \p inst
 * given by lbld.group() requires allocating a temporary for the destination
 * of the lowered instruction and copying the data back to the original
 * destination region.
 */
static inline bool
needs_dst_copy(const brw_builder &lbld, const brw_inst *inst)
{
   if (inst->dst.is_null())
      return false;

   /* If we have a SIMD16 SEND message with a destination format like this :
    *
    *   g0 : |hf15|hf14|hf13|       ...      |hf7|hf6|hf5|hf4|hf3|hf2|hf1|hf0|
    *
    * and we have to lower to SIMD8, the lowered format will be this :
    *
    *   g0 : |           unused              |hf7|hf6|hf5|hf4|hf3|hf2|hf1|hf0|
    *
    * Since SEND messages operate on physical register, we need a copy of the
    * destination because the second lowered SIMD8 message cannot write to the
    * upper unused part of the register.
    */
   if (is_send_inst(inst) &&
       (inst->dst.component_size(lbld.dispatch_width()) %
        (reg_unit(lbld.shader->devinfo) * REG_SIZE)) != 0)
      return true;

   /* If the instruction writes more than one component we'll have to shuffle
    * the results of multiple lowered instructions in order to make sure that
    * they end up arranged correctly in the original destination region.
    */
   if (inst->size_written > inst->dst.component_size(inst->exec_size))
      return true;

   for (unsigned i = 0; i < inst->sources; i++) {
      /* If we already made a copy of the source for other reasons there won't
       * be any overlap with the destination.
       */
      if (needs_src_copy(lbld, inst, i))
         continue;

      /* In order to keep the logic simple we emit a copy whenever the
       * destination region doesn't exactly match an overlapping source, which
       * may point at the source and destination not being aligned group by
       * group which could cause one of the lowered instructions to overwrite
       * the data read from the same source by other lowered instructions.
       */
      if (regions_overlap(inst->dst, inst->size_written,
                          inst->src[i], inst->size_read(lbld.shader->devinfo, i)) &&
          !inst->dst.equals(inst->src[i]))
        return true;
   }

   return false;
}

/**
 * Insert data from a packed temporary into the channel group given by
 * lbld.group() of the destination region of instruction \p inst and return
 * the temporary as result.  Any copy instructions that are required for
 * unzipping the previous value (in the case of partial writes) will be
 * inserted using \p lbld_before and any copy instructions required for
 * zipping up the destination of \p inst will be inserted using \p lbld_after.
 */
static brw_reg
emit_zip(const brw_builder &lbld_before, const brw_builder &lbld_after,
         brw_inst *inst)
{
   assert(lbld_before.dispatch_width() == lbld_after.dispatch_width());
   assert(lbld_before.group() == lbld_after.group());
   assert(lbld_after.group() >= inst->group);

   const struct intel_device_info *devinfo = lbld_before.shader->devinfo;

   /* Specified channel group from the destination region. */
   const brw_reg dst = horiz_offset(inst->dst, lbld_after.group() - inst->group);

   if (!needs_dst_copy(lbld_after, inst)) {
      /* No need to allocate a temporary for the lowered instruction, just
       * take the right group of channels from the original region.
       */
      return dst;
   }

   /* Deal with the residency data part later */
   const unsigned residency_size = inst->has_sampler_residency() ?
      (reg_unit(devinfo) * REG_SIZE) : 0;
   const unsigned dst_size = (inst->size_written - residency_size) /
      inst->dst.component_size(inst->exec_size);
   unsigned tmp_comp_size;
   brw_reg tmp;

   if (is_send_inst(inst)) {
      /* For SEND messages, align the allocation to physical registers */
      tmp_comp_size =
         align(inst->dst.component_size(lbld_after.dispatch_width()),
               reg_unit(devinfo) * REG_SIZE);
      const unsigned tmp_size =
         (dst_size * tmp_comp_size + residency_size) / REG_SIZE;
      tmp = retype(brw_allocate_vgrf_units(*lbld_after.shader, tmp_size),
                   inst->dst.type);
   } else {
      tmp_comp_size = inst->dst.component_size(lbld_after.dispatch_width());
      tmp = lbld_after.vgrf(inst->dst.type, dst_size +
         inst->has_sampler_residency() * reg_unit(devinfo));
   }

   if (inst->predicate) {
      /* Handle predication by copying the original contents of the
       * destination into the temporary before emitting the lowered
       * instruction.
       */
      for (unsigned k = 0; k < dst_size; ++k) {
         lbld_before.MOV(byte_offset(tmp, tmp_comp_size * k),
                         offset(dst, inst->exec_size, k));
      }
   }

   for (unsigned k = 0; k < dst_size; ++k) {
      /* Copy the (split) temp into the original (larger) destination */
      lbld_after.MOV(offset(dst, inst->exec_size, k),
                     byte_offset(tmp, tmp_comp_size * k));
   }

   if (inst->has_sampler_residency()) {
      /* Sampler messages with residency need a special attention. In the
       * first lane of the last component are located the Pixel Null Mask
       * (bits 0:15) & some upper bits we need to discard (bits 16:31). We
       * have to build a single 32bit value for the SIMD32 message out of 2
       * SIMD16 16 bit values.
       */
      const brw_builder rbld = lbld_after.uniform();
      brw_reg local_res_reg = component(
         retype(offset(tmp, lbld_before, dst_size),
                BRW_TYPE_UW), 0);
      brw_reg final_res_reg =
         retype(byte_offset(inst->dst,
                            inst->size_written - residency_size +
                            lbld_after.group() / 8), BRW_TYPE_UW);
      rbld.MOV(final_res_reg, local_res_reg);
   }

   return tmp;
}

bool
brw_lower_simd_width(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      const unsigned lower_width = brw_get_lowered_simd_width(&s, inst);

      /* No splitting required */
      if (lower_width == inst->exec_size)
         continue;

      assert(lower_width < inst->exec_size);

      /* Builder matching the original instruction. */
      const brw_builder bld = brw_builder(&s);
      const brw_builder ibld =
         bld.before(inst).exec_all(inst->force_writemask_all)
            .group(inst->exec_size, inst->group / inst->exec_size);

      /* Split the copies in chunks of the execution width of either the
       * original or the lowered instruction, whichever is lower.
       */
      const unsigned n = DIV_ROUND_UP(inst->exec_size, lower_width);
      const unsigned residency_size = inst->has_sampler_residency() ?
         (reg_unit(s.devinfo) * REG_SIZE) : 0;
      const unsigned dst_size =
         (inst->size_written - residency_size) /
         inst->dst.component_size(inst->exec_size);

      if (const brw_send_inst *send = inst->as_send())
         assert(!send->mlen);

      assert(!inst->writes_accumulator);

      /* Inserting the zip, unzip, and duplicated instructions in all of
       * the right spots is somewhat tricky.  All of the unzip and any
       * instructions from the zip which unzip the destination prior to
       * writing need to happen before all of the per-group instructions
       * and the zip instructions need to happen after.  In order to sort
       * this all out, we insert the unzip instructions before \p inst,
       * insert the per-group instructions after \p inst (i.e. before
       * inst->next), and insert the zip instructions before the
       * instruction after \p inst.  Since we are inserting instructions
       * after \p inst, inst->next is a moving target and we need to save
       * it off here so that we insert the zip instructions in the right
       * place.
       *
       * Since we're inserting split instructions after after_inst, the
       * instructions will end up in the reverse order that we insert them.
       * However, certain render target writes require that the low group
       * instructions come before the high group.  From the Ivy Bridge PRM
       * Vol. 4, Pt. 1, Section 3.9.11:
       *
       *    "If multiple SIMD8 Dual Source messages are delivered by the
       *    pixel shader thread, each SIMD8_DUALSRC_LO message must be
       *    issued before the SIMD8_DUALSRC_HI message with the same Slot
       *    Group Select setting."
       *
       * And, from Section 3.9.11.1 of the same PRM:
       *
       *    "When SIMD32 or SIMD16 PS threads send render target writes
       *    with multiple SIMD8 and SIMD16 messages, the following must
       *    hold:
       *
       *    All the slots (as described above) must have a corresponding
       *    render target write irrespective of the slot's validity. A slot
       *    is considered valid when at least one sample is enabled. For
       *    example, a SIMD16 PS thread must send two SIMD8 render target
       *    writes to cover all the slots.
       *
       *    PS thread must send SIMD render target write messages with
       *    increasing slot numbers. For example, SIMD16 thread has
       *    Slot[15:0] and if two SIMD8 render target writes are used, the
       *    first SIMD8 render target write must send Slot[7:0] and the
       *    next one must send Slot[15:8]."
       *
       * In order to make low group instructions come before high group
       * instructions (this is required for some render target writes), we
       * split from the highest group to lowest.
       */
      const brw_builder zip_bld = ibld.after(inst);
      for (int i = n - 1; i >= 0; i--) {
         /* Emit a copy of the original instruction with the lowered width.
          * If the EOT flag was set throw it away except for the last
          * instruction to avoid killing the thread prematurely.
          */
         brw_inst *split_inst = brw_clone_inst(s, inst);
         split_inst->exec_size = lower_width;
         split_inst->eot = inst->eot && i == int(n - 1);

         /* Select the correct channel enables for the i-th group, then
          * transform the sources and destination and emit the lowered
          * instruction.
          */
         const brw_builder lbld = ibld.group(lower_width, i);
         const brw_builder lbld_after = zip_bld.group(lower_width, i);

         for (unsigned j = 0; j < inst->sources; j++)
            split_inst->src[j] = emit_unzip(lbld.before(inst), inst, j);

         split_inst->dst = emit_zip(lbld.before(inst),
                                   lbld_after, inst);
         unsigned comp_size =
            split_inst->dst.component_size(lower_width);

         /* For SEND messages, align the data size to physical registers */
         if (is_send_inst(split_inst))
            comp_size = align(comp_size, REG_SIZE * reg_unit(s.devinfo));

         split_inst->size_written = comp_size * dst_size + residency_size;

         lbld.after(inst).emit(split_inst);
      }

      inst->remove();
      progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}
