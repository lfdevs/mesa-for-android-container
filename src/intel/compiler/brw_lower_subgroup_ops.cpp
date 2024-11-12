/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "util/half_float.h"

#include "brw_fs.h"
#include "brw_fs_builder.h"

using namespace brw;

struct brw_reduction_info {
   brw_reg             identity;
   enum opcode         op;
   brw_conditional_mod cond_mod;
};

static brw_reduction_info
brw_get_reduction_info(brw_reduce_op red_op, brw_reg_type type)
{
   struct brw_reduction_info info;

   info.op = BRW_OPCODE_SEL;
   info.cond_mod = BRW_CONDITIONAL_NONE;

   switch (red_op) {
   case BRW_REDUCE_OP_ADD: info.op = BRW_OPCODE_ADD;           break;
   case BRW_REDUCE_OP_MUL: info.op = BRW_OPCODE_MUL;           break;
   case BRW_REDUCE_OP_AND: info.op = BRW_OPCODE_AND;           break;
   case BRW_REDUCE_OP_OR:  info.op = BRW_OPCODE_OR;            break;
   case BRW_REDUCE_OP_XOR: info.op = BRW_OPCODE_XOR;           break;
   case BRW_REDUCE_OP_MIN: info.cond_mod = BRW_CONDITIONAL_L;  break;
   case BRW_REDUCE_OP_MAX: info.cond_mod = BRW_CONDITIONAL_GE; break;
   default:
      unreachable("invalid reduce op");
   }

   switch (red_op) {
   case BRW_REDUCE_OP_ADD:
   case BRW_REDUCE_OP_XOR:
   case BRW_REDUCE_OP_OR:
      info.identity = retype(brw_imm_u64(0), type);
      return info;
   case BRW_REDUCE_OP_AND:
      info.identity = retype(brw_imm_u64(~0ull), type);
      return info;
   default:
      /* Continue below. */
      break;
   }

   brw_reg id;
   const unsigned size = brw_type_size_bytes(type);

   switch (red_op) {
   case BRW_REDUCE_OP_MUL: {
      if (brw_type_is_int(type)) {
         id = size < 4  ? brw_imm_uw(1) :
              size == 4 ? brw_imm_ud(1) :
                          brw_imm_u64(1);
      } else {
         assert(brw_type_is_float(type));
         id = size == 2 ? brw_imm_uw(_mesa_float_to_half(1.0)) :
              size == 4 ? brw_imm_f(1.0) :
                          brw_imm_df(1.0);
      }
      break;
   }

   case BRW_REDUCE_OP_MIN: {
      if (brw_type_is_uint(type)) {
         id = brw_imm_u64(~0ull);
      } else if (brw_type_is_sint(type)) {
         id = size == 1 ? brw_imm_w(INT8_MAX) :
              size == 2 ? brw_imm_w(INT16_MAX) :
              size == 4 ? brw_imm_d(INT32_MAX) :
                          brw_imm_q(INT64_MAX);
      } else {
         assert(brw_type_is_float(type));
         id = size == 2 ? brw_imm_uw(_mesa_float_to_half(INFINITY)) :
              size == 4 ? brw_imm_f(INFINITY) :
                          brw_imm_df(INFINITY);
      }
      break;
   }

   case BRW_REDUCE_OP_MAX: {
      if (brw_type_is_uint(type)) {
         id = brw_imm_u64(0);
      } else if (brw_type_is_sint(type)) {
         id = size == 1 ? brw_imm_w(INT8_MIN) :
              size == 2 ? brw_imm_w(INT16_MIN) :
              size == 4 ? brw_imm_d(INT32_MIN) :
                          brw_imm_q(INT64_MIN);
      } else {
         assert(brw_type_is_float(type));
         id = size == 2 ? brw_imm_uw(_mesa_float_to_half(-INFINITY)) :
              size == 4 ? brw_imm_f(-INFINITY) :
                          brw_imm_df(-INFINITY);
      }
      break;
   }

   default:
      unreachable("invalid reduce op");
   }

   /* For some cases above (e.g. all bits zeros, all bits ones, first bit one)
    * either the size or the signedness was ignored, so adjust the final type
    * now.
    *
    * B/UB types can't have immediates, so used W/UW above and here.
    */
   if      (type == BRW_TYPE_UB) type = BRW_TYPE_UW;
   else if (type == BRW_TYPE_B)  type = BRW_TYPE_W;

   info.identity = retype(id, type);

   return info;
}

static void
brw_emit_scan_step(const fs_builder &bld, enum opcode opcode, brw_conditional_mod mod,
                   const brw_reg &tmp,
                   unsigned left_offset, unsigned left_stride,
                   unsigned right_offset, unsigned right_stride)
{
   brw_reg left, right;
   left = horiz_stride(horiz_offset(tmp, left_offset), left_stride);
   right = horiz_stride(horiz_offset(tmp, right_offset), right_stride);
   if ((tmp.type == BRW_TYPE_Q || tmp.type == BRW_TYPE_UQ) &&
       (!bld.shader->devinfo->has_64bit_int || bld.shader->devinfo->ver >= 20)) {
      switch (opcode) {
      case BRW_OPCODE_MUL:
         /* This will get lowered by integer MUL lowering */
         set_condmod(mod, bld.emit(opcode, right, left, right));
         break;

      case BRW_OPCODE_SEL: {
         /* In order for the comparisons to work out right, we need our
          * comparisons to be strict.
          */
         assert(mod == BRW_CONDITIONAL_L || mod == BRW_CONDITIONAL_GE);
         if (mod == BRW_CONDITIONAL_GE)
            mod = BRW_CONDITIONAL_G;

         /* We treat the bottom 32 bits as unsigned regardless of
          * whether or not the integer as a whole is signed.
          */
         brw_reg right_low = subscript(right, BRW_TYPE_UD, 0);
         brw_reg left_low = subscript(left, BRW_TYPE_UD, 0);

         /* The upper bits get the same sign as the 64-bit type */
         brw_reg_type type32 = brw_type_with_size(tmp.type, 32);
         brw_reg right_high = subscript(right, type32, 1);
         brw_reg left_high = subscript(left, type32, 1);

         /* Build up our comparison:
          *
          *   l_hi < r_hi || (l_hi == r_hi && l_low < r_low)
          */
         bld.CMP(bld.null_reg_ud(), retype(left_low, BRW_TYPE_UD),
                            retype(right_low, BRW_TYPE_UD), mod);
         set_predicate(BRW_PREDICATE_NORMAL,
                       bld.CMP(bld.null_reg_ud(), left_high, right_high,
                           BRW_CONDITIONAL_EQ));
         set_predicate_inv(BRW_PREDICATE_NORMAL, true,
                           bld.CMP(bld.null_reg_ud(), left_high, right_high, mod));

         /* We could use selects here or we could use predicated MOVs
          * because the destination and second source (if it were a SEL)
          * are the same.
          */
         set_predicate(BRW_PREDICATE_NORMAL, bld.MOV(right_low, left_low));
         set_predicate(BRW_PREDICATE_NORMAL, bld.MOV(right_high, left_high));
         break;
      }

      default:
         unreachable("Unsupported 64-bit scan op");
      }
   } else {
      set_condmod(mod, bld.emit(opcode, right, left, right));
   }
}

static void
brw_emit_scan(const fs_builder &bld, enum opcode opcode, const brw_reg &tmp,
              unsigned cluster_size, brw_conditional_mod mod)
{
   unsigned dispatch_width = bld.dispatch_width();
   assert(dispatch_width >= 8);

   /* The instruction splitting code isn't advanced enough to split
    * these so we need to handle that ourselves.
    */
   if (dispatch_width * brw_type_size_bytes(tmp.type) > 2 * REG_SIZE) {
      const unsigned half_width = dispatch_width / 2;
      const fs_builder ubld = bld.exec_all().group(half_width, 0);
      brw_reg left = tmp;
      brw_reg right = horiz_offset(tmp, half_width);
      brw_emit_scan(ubld, opcode, left, cluster_size, mod);
      brw_emit_scan(ubld, opcode, right, cluster_size, mod);
      if (cluster_size > half_width) {
         brw_emit_scan_step(ubld, opcode, mod, tmp,
                            half_width - 1, 0, half_width, 1);
      }
      return;
   }

   if (cluster_size > 1) {
      const fs_builder ubld = bld.exec_all().group(dispatch_width / 2, 0);
      brw_emit_scan_step(ubld, opcode, mod, tmp, 0, 2, 1, 2);
   }

   if (cluster_size > 2) {
      if (brw_type_size_bytes(tmp.type) <= 4) {
         const fs_builder ubld =
            bld.exec_all().group(dispatch_width / 4, 0);
         brw_emit_scan_step(ubld, opcode, mod, tmp, 1, 4, 2, 4);
         brw_emit_scan_step(ubld, opcode, mod, tmp, 1, 4, 3, 4);
      } else {
         /* For 64-bit types, we have to do things differently because
          * the code above would land us with destination strides that
          * the hardware can't handle.  Fortunately, we'll only be
          * 8-wide in that case and it's the same number of
          * instructions.
          */
         const fs_builder ubld = bld.exec_all().group(2, 0);
         for (unsigned i = 0; i < dispatch_width; i += 4)
            brw_emit_scan_step(ubld, opcode, mod, tmp, i + 1, 0, i + 2, 1);
      }
   }

   for (unsigned i = 4;
        i < MIN2(cluster_size, dispatch_width);
        i *= 2) {
      const fs_builder ubld = bld.exec_all().group(i, 0);
      brw_emit_scan_step(ubld, opcode, mod, tmp, i - 1, 0, i, 1);

      if (dispatch_width > i * 2)
         brw_emit_scan_step(ubld, opcode, mod, tmp, i * 3 - 1, 0, i * 3, 1);

      if (dispatch_width > i * 4) {
         brw_emit_scan_step(ubld, opcode, mod, tmp, i * 5 - 1, 0, i * 5, 1);
         brw_emit_scan_step(ubld, opcode, mod, tmp, i * 7 - 1, 0, i * 7, 1);
      }
   }
}

static bool
brw_lower_reduce(fs_visitor &s, bblock_t *block, fs_inst *inst)
{
   const fs_builder bld(&s, block, inst);

   assert(inst->dst.type == inst->src[0].type);
   brw_reg dst = inst->dst;
   brw_reg src = inst->src[0];

   assert(inst->src[1].file == IMM);
   enum brw_reduce_op op = (enum brw_reduce_op)inst->src[1].ud;

   assert(inst->src[2].file == IMM);
   unsigned cluster_size = inst->src[2].ud;

   assert(cluster_size > 0);
   assert(cluster_size <= s.dispatch_width);

   struct brw_reduction_info info = brw_get_reduction_info(op, src.type);

   /* Set up a register for all of our scratching around and initialize it
    * to reduction operation's identity value.
    */
   brw_reg scan = bld.vgrf(src.type);
   bld.exec_all().emit(SHADER_OPCODE_SEL_EXEC, scan, src, info.identity);

   brw_emit_scan(bld, info.op, scan, cluster_size, info.cond_mod);

   if (cluster_size * brw_type_size_bytes(src.type) >= REG_SIZE * 2) {
      /* In this case, CLUSTER_BROADCAST instruction isn't needed because
       * the distance between clusters is at least 2 GRFs.  In this case,
       * we don't need the weird striding of the CLUSTER_BROADCAST
       * instruction and can just do regular MOVs.
       */
      assert((cluster_size * brw_type_size_bytes(src.type)) % (REG_SIZE * 2) == 0);
      const unsigned groups =
         (s.dispatch_width * brw_type_size_bytes(src.type)) / (REG_SIZE * 2);
      const unsigned group_size = s.dispatch_width / groups;
      for (unsigned i = 0; i < groups; i++) {
         const unsigned cluster = (i * group_size) / cluster_size;
         const unsigned comp = cluster * cluster_size + (cluster_size - 1);
         bld.group(group_size, i).MOV(horiz_offset(dst, i * group_size),
                                      component(scan, comp));
      }
   } else {
      bld.emit(SHADER_OPCODE_CLUSTER_BROADCAST, dst, scan,
               brw_imm_ud(cluster_size - 1), brw_imm_ud(cluster_size));
   }
   inst->remove(block);
   return true;
}

static bool
brw_lower_scan(fs_visitor &s, bblock_t *block, fs_inst *inst)
{
   const fs_builder bld(&s, block, inst);

   assert(inst->dst.type == inst->src[0].type);
   brw_reg dst = inst->dst;
   brw_reg src = inst->src[0];

   assert(inst->src[1].file == IMM);
   enum brw_reduce_op op = (enum brw_reduce_op)inst->src[1].ud;

   struct brw_reduction_info info = brw_get_reduction_info(op, src.type);

   /* Set up a register for all of our scratching around and initialize it
    * to reduction operation's identity value.
    */
   brw_reg scan = bld.vgrf(src.type);
   const fs_builder ubld = bld.exec_all();
   ubld.emit(SHADER_OPCODE_SEL_EXEC, scan, src, info.identity);

   if (inst->opcode == SHADER_OPCODE_EXCLUSIVE_SCAN) {
      /* Exclusive scan is a bit harder because we have to do an annoying
       * shift of the contents before we can begin.  To make things worse,
       * we can't do this with a normal stride; we have to use indirects.
       */
      brw_reg shifted = bld.vgrf(src.type);
      brw_reg idx = bld.vgrf(BRW_TYPE_W);

      ubld.ADD(idx, bld.LOAD_SUBGROUP_INVOCATION(), brw_imm_w(-1));
      ubld.emit(SHADER_OPCODE_SHUFFLE, shifted, scan, idx);
      ubld.group(1, 0).MOV(horiz_offset(shifted, 0), info.identity);
      scan = shifted;
   }

   brw_emit_scan(bld, info.op, scan, s.dispatch_width, info.cond_mod);

   bld.MOV(dst, scan);

   inst->remove(block);
   return true;
}

static brw_reg
brw_fill_flag(const fs_builder &bld, unsigned v)
{
   const fs_builder ubld1 = bld.exec_all().group(1, 0);
   brw_reg flag = brw_flag_reg(0, 0);

   if (bld.shader->dispatch_width == 32) {
      /* For SIMD32, we use a UD type so we fill both f0.0 and f0.1. */
      flag = retype(flag, BRW_TYPE_UD);
      ubld1.MOV(flag, brw_imm_ud(v));
   } else {
      ubld1.MOV(flag, brw_imm_uw(v & 0xFFFF));
   }

   return flag;
}

static void
brw_lower_dispatch_width_vote(const fs_builder &bld, enum opcode opcode, brw_reg dst, brw_reg src)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const unsigned dispatch_width = bld.shader->dispatch_width;

   assert(opcode == SHADER_OPCODE_VOTE_ANY ||
          opcode == SHADER_OPCODE_VOTE_ALL ||
          opcode == SHADER_OPCODE_VOTE_EQUAL);

   const bool any   = opcode == SHADER_OPCODE_VOTE_ANY;
   const bool equal = opcode == SHADER_OPCODE_VOTE_EQUAL;

   const brw_reg ref = equal ? bld.emit_uniformize(src) : brw_imm_d(0);

   /* The any/all predicates do not consider channel enables. To prevent
    * dead channels from affecting the result, we initialize the flag with
    * with the identity value for the logical operation.
    */
   brw_fill_flag(bld, any ? 0 : 0xFFFFFFFF);
   bld.CMP(bld.null_reg_d(), src, ref, equal ? BRW_CONDITIONAL_Z
                                             : BRW_CONDITIONAL_NZ);

   /* For some reason, the any/all predicates don't work properly with
    * SIMD32.  In particular, it appears that a SEL with a QtrCtrl of 2H
    * doesn't read the correct subset of the flag register and you end up
    * getting garbage in the second half.  Work around this by using a pair
    * of 1-wide MOVs and scattering the result.
    *
    * TODO: Check if we still need this for newer platforms.
    */
   const fs_builder ubld = devinfo->ver >= 20 ? bld.exec_all()
                                              : bld.exec_all().group(1, 0);
   brw_reg res1 = ubld.MOV(brw_imm_d(0));

   enum brw_predicate pred;
   if (any) {
      pred = devinfo->ver >= 20   ? XE2_PREDICATE_ANY :
             dispatch_width == 8  ? BRW_PREDICATE_ALIGN1_ANY8H :
             dispatch_width == 16 ? BRW_PREDICATE_ALIGN1_ANY16H :
                                    BRW_PREDICATE_ALIGN1_ANY32H;
   } else {
      pred = devinfo->ver >= 20   ? XE2_PREDICATE_ALL :
             dispatch_width == 8  ? BRW_PREDICATE_ALIGN1_ALL8H :
             dispatch_width == 16 ? BRW_PREDICATE_ALIGN1_ALL16H :
                                    BRW_PREDICATE_ALIGN1_ALL32H;
   }
   set_predicate(pred, ubld.MOV(res1, brw_imm_d(-1)));

   bld.MOV(retype(dst, BRW_TYPE_D), component(res1, 0));
}

static void
brw_lower_quad_vote_gfx9(const fs_builder &bld, enum opcode opcode, brw_reg dst, brw_reg src)
{
   assert(opcode == SHADER_OPCODE_VOTE_ANY || opcode == SHADER_OPCODE_VOTE_ALL);
   const bool any = opcode == SHADER_OPCODE_VOTE_ANY;

   /* The any/all predicates do not consider channel enables. To prevent
    * dead channels from affecting the result, we initialize the flag with
    * with the identity value for the logical operation.
    */
   brw_fill_flag(bld, any ? 0 : 0xFFFFFFFF);
   bld.CMP(bld.null_reg_ud(), src, brw_imm_ud(0u), BRW_CONDITIONAL_NZ);
   bld.exec_all().MOV(retype(dst, BRW_TYPE_UD), brw_imm_ud(0));

   /* Before Xe2, we can use specialized predicates. */
   const enum brw_predicate pred = any ? BRW_PREDICATE_ALIGN1_ANY4H
                                       : BRW_PREDICATE_ALIGN1_ALL4H;

   fs_inst *mov = bld.MOV(retype(dst, BRW_TYPE_D), brw_imm_d(-1));
   set_predicate(pred, mov);
}

static void
brw_lower_quad_vote_gfx20(const fs_builder &bld, enum opcode opcode, brw_reg dst, brw_reg src)
{
   assert(opcode == SHADER_OPCODE_VOTE_ANY || opcode == SHADER_OPCODE_VOTE_ALL);
   const bool any = opcode == SHADER_OPCODE_VOTE_ANY;

   /* This code is going to manipulate the results of flag mask, so clear it to
    * avoid any residual value from disabled channels.
    */
   brw_reg flag = brw_fill_flag(bld, 0);

   /* Mask of invocations where condition is true, note that mask is
    * replicated to each invocation.
    */
   bld.CMP(bld.null_reg_ud(), src, brw_imm_ud(0u), BRW_CONDITIONAL_NZ);
   brw_reg cond_mask = bld.vgrf(BRW_TYPE_UD);
   bld.MOV(cond_mask, flag);

   /* Mask of invocations in the quad, each invocation will get
    * all the bits set for their quad, i.e. invocations 0-3 will have
    * 0b...1111, invocations 4-7 will have 0b...11110000 and so on.
    */
   brw_reg invoc_ud = bld.vgrf(BRW_TYPE_UD);
   bld.MOV(invoc_ud, bld.LOAD_SUBGROUP_INVOCATION());
   brw_reg quad_mask =
      bld.SHL(brw_imm_ud(0xF), bld.AND(invoc_ud, brw_imm_ud(0xFFFFFFFC)));

   /* An invocation will have bits set for each quad that passes the
    * condition.  This is uniform among each quad.
    */
   brw_reg tmp = bld.AND(cond_mask, quad_mask);

   if (any) {
      bld.CMP(retype(dst, BRW_TYPE_UD), tmp, brw_imm_ud(0), BRW_CONDITIONAL_NZ);
   } else {
      /* Filter out quad_mask to include only active channels. */
      brw_reg active = bld.vgrf(BRW_TYPE_UD);
      bld.exec_all().emit(SHADER_OPCODE_LOAD_LIVE_CHANNELS, active);
      bld.MOV(active, brw_reg(component(active, 0)));
      bld.AND(quad_mask, quad_mask, active);

      bld.CMP(retype(dst, BRW_TYPE_UD), tmp, quad_mask, BRW_CONDITIONAL_Z);
   }
}

static bool
brw_lower_vote(fs_visitor &s, bblock_t *block, fs_inst *inst)
{
   const fs_builder bld(&s, block, inst);

   brw_reg dst = inst->dst;
   brw_reg src = inst->src[0];

   unsigned cluster_size;
   if (inst->sources > 1) {
      assert(inst->src[1].file == IMM);
      cluster_size = inst->src[1].ud;
   } else {
      cluster_size = s.dispatch_width;
   }

   if (cluster_size == s.dispatch_width) {
      brw_lower_dispatch_width_vote(bld, inst->opcode, dst, src);
   } else {
      assert(cluster_size == 4);
      if (s.devinfo->ver < 20)
         brw_lower_quad_vote_gfx9(bld, inst->opcode, dst, src);
      else
         brw_lower_quad_vote_gfx20(bld, inst->opcode, dst, src);
   }

   inst->remove(block);
   return true;
}

bool
brw_fs_lower_subgroup_ops(fs_visitor &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, fs_inst, inst, s.cfg) {
      switch (inst->opcode) {
      case SHADER_OPCODE_REDUCE:
         progress |= brw_lower_reduce(s, block, inst);
         break;

      case SHADER_OPCODE_INCLUSIVE_SCAN:
      case SHADER_OPCODE_EXCLUSIVE_SCAN:
         progress |= brw_lower_scan(s, block, inst);
         break;

      case SHADER_OPCODE_VOTE_ANY:
      case SHADER_OPCODE_VOTE_ALL:
      case SHADER_OPCODE_VOTE_EQUAL:
         progress |= brw_lower_vote(s, block, inst);
         break;

      default:
         /* Nothing to do. */
         break;
      }
   }

   if (progress)
      s.invalidate_analysis(DEPENDENCY_INSTRUCTIONS | DEPENDENCY_VARIABLES);

   return progress;
}
