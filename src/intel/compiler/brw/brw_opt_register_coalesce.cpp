/*
 * Copyright © 2012 Intel Corporation
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

/** @file
 *
 * Implements register coalescing: Checks if the two registers involved in a
 * raw move don't interfere, in which case they can both be stored in the same
 * place and the MOV removed.
 *
 * To do this, all uses of the source of the MOV in the shader are replaced
 * with the destination of the MOV. For example:
 *
 * add vgrf3:F, vgrf1:F, vgrf2:F
 * mov vgrf4:F, vgrf3:F
 * mul vgrf5:F, vgrf5:F, vgrf4:F
 *
 * becomes
 *
 * add vgrf4:F, vgrf1:F, vgrf2:F
 * mul vgrf5:F, vgrf5:F, vgrf4:F
 */

#include "brw_analysis.h"
#include "brw_shader.h"
#include "brw_cfg.h"

static bool
is_nop_mov(const brw_inst *inst)
{
   if (inst->opcode == SHADER_OPCODE_LOAD_PAYLOAD) {
      const brw_load_payload_inst *lp = inst->as_load_payload();

      brw_reg dst = lp->dst;
      for (int i = 0; i < lp->sources; i++) {
         if (!dst.equals(lp->src[i])) {
            return false;
         }
         dst.offset += (i < lp->header_size ? REG_SIZE :
                        lp->exec_size * dst.stride *
                        brw_type_size_bytes(lp->src[i].type));
      }
      return true;
   } else if (inst->opcode == BRW_OPCODE_MOV) {
      return inst->dst.equals(inst->src[0]);
   }

   return false;
}

static bool
is_coalesce_candidate(const brw_shader *v, const brw_inst *inst)
{
   if ((inst->opcode != BRW_OPCODE_MOV &&
        inst->opcode != SHADER_OPCODE_LOAD_PAYLOAD) ||
       inst->is_partial_write() ||
       inst->saturate ||
       inst->src[0].file != VGRF ||
       inst->src[0].negate ||
       inst->src[0].abs ||
       !inst->src[0].is_contiguous() ||
       inst->dst.file != VGRF ||
       inst->dst.type != inst->src[0].type) {
      return false;
   }

   if (v->alloc.sizes[inst->src[0].nr] >
       v->alloc.sizes[inst->dst.nr])
      return false;

   if (inst->opcode == SHADER_OPCODE_LOAD_PAYLOAD) {
      if (!is_coalescing_payload(*v, inst)) {
         return false;
      }
   }

   return true;
}

static bool
can_coalesce_vars(const intel_device_info *devinfo,
                  const brw_live_variables &live,
                  const brw_ip_ranges &ips,
                  const cfg_t *cfg,
                  const brw_inst *inst,
                  int dst_var, int src_var)
{
   if (!live.vars_interfere(src_var, dst_var))
      return true;

   brw_range dst_range = live.vars_range[dst_var];
   brw_range src_range = live.vars_range[src_var];

   /* Variables interfere and one live range isn't a subset of the other. */
   if (!dst_range.contains(src_range) &&
       !src_range.contains(dst_range))
      return false;

   /* Check for a write to either register in the intersection of their live
    * ranges.
    */
   brw_range intersection = intersect(dst_range, src_range);
   assert(!intersection.is_empty());

   foreach_block(scan_block, cfg) {
      if (ips.range(scan_block).last() < intersection.start)
         continue;

      int scan_ip = ips.range(scan_block).start - 1;

      bool seen_src_write = false;
      bool seen_copy = false;
      foreach_inst_in_block(brw_inst, scan_inst, scan_block) {
         scan_ip++;

         /* Ignore anything before the intersection of the live ranges */
         if (scan_ip < intersection.start)
            continue;

         /* Ignore the copying instruction itself */
         if (scan_inst == inst) {
            seen_copy = true;
            continue;
         }

         if (scan_ip > intersection.last())
            return true; /* registers do not interfere */

         if (seen_src_write && !seen_copy) {
            /* In order to satisfy the guarantee of register coalescing, we
             * must ensure that the two registers always have the same value
             * during the intersection of their live ranges.  One way to do
             * this is to simply ensure that neither is ever written apart
             * from the one copy which syncs up the two registers.  However,
             * this can be overly conservative and only works in the case
             * where the destination live range is entirely contained in the
             * source live range.
             *
             * To handle the other case where the source is contained in the
             * destination, we allow writes to the source register as long as
             * they happen before the copy, in the same block as the copy, and
             * the destination is never read between first such write and the
             * copy.  This effectively moves the write from the copy up.
             */
            for (int j = 0; j < scan_inst->sources; j++) {
               if (regions_overlap(scan_inst->src[j], scan_inst->size_read(devinfo, j),
                                   inst->dst, inst->size_written))
                  return false; /* registers interfere */
            }
         }

         /* The MOV being coalesced had better be the only instruction which
          * writes to the coalesce destination in the intersection.
          */
         if (regions_overlap(scan_inst->dst, scan_inst->size_written,
                             inst->dst, inst->size_written))
            return false; /* registers interfere */

         /* See the big comment above */
         if (regions_overlap(scan_inst->dst, scan_inst->size_written,
                             inst->src[0], inst->size_read(devinfo, 0))) {
            if (seen_copy || scan_block != inst->block ||
                (scan_inst->force_writemask_all && !inst->force_writemask_all))
               return false;
            seen_src_write = true;
         }
      }
   }

   return true;
}

/**
 * Check if coalescing this register would expand the size of the last
 * SEND instruction's payload to more than would fit in g112-g127.
 */
static bool
would_violate_eot_restriction(brw_shader &s,
                              const cfg_t *cfg,
                              unsigned dst_reg, unsigned src_reg)
{
   if (s.alloc.sizes[dst_reg] > s.alloc.sizes[src_reg]) {
      foreach_block_reverse(block, cfg) {
         bool done = false;

         foreach_inst_in_block_reverse(brw_inst, send, block) {
            if (send->is_control_flow())
               continue;

            if (send->opcode != SHADER_OPCODE_SEND || !send->eot) {
               /* We're done on the first block who's last instruction isn't a
                * SEND EOT because the only reason we generate more than one
                * EOT is lower_fb_write_logical_send() for Gfx11's broken
                * indirect descriptor. The only pattern where we can have 2
                * EOTs is like this :
                *
                * ...
                * if ...
                *    send eot
                * else
                *    send eot
                * endif
                * <--- end of shader
                */
               done = true;
               break;
            }

            if ((send->src[SEND_SRC_PAYLOAD1].file == VGRF &&
                 send->src[SEND_SRC_PAYLOAD1].nr == src_reg) ||
                (send->src[SEND_SRC_PAYLOAD2].file == VGRF &&
                 send->src[SEND_SRC_PAYLOAD2].nr == src_reg)) {
               const unsigned p1 =
                  send->src[SEND_SRC_PAYLOAD1].file == VGRF ?
                  s.alloc.sizes[send->src[SEND_SRC_PAYLOAD1].nr] : 0;
               const unsigned p2 = send->sources >= 4 &&
                  send->src[SEND_SRC_PAYLOAD2].file == VGRF ?
                  s.alloc.sizes[send->src[SEND_SRC_PAYLOAD2].nr] : 0;

               const unsigned increase =
                  s.alloc.sizes[dst_reg] - s.alloc.sizes[src_reg];

               if (p1 + p2 + increase > 15)
                  return true;
            }
            break;
         }

         if (done)
            break;
      }
   }

   return false;
}

bool
brw_opt_register_coalesce(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;

   bool progress = false;
   brw_live_variables &live = s.live_analysis.require();
   brw_ip_ranges &ips = s.ip_ranges_analysis.require();
   int src_size = 0;
   int channels_remaining = 0;
   unsigned src_reg = ~0u, dst_reg = ~0u;
   int *dst_reg_offset = new int[live.max_vgrf_size];
   brw_inst **mov = new brw_inst *[live.max_vgrf_size];
   int *dst_var = new int[live.max_vgrf_size];
   int *src_var = new int[live.max_vgrf_size];
   const brw_def_analysis &defs = s.def_analysis.require();

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (!is_coalesce_candidate(&s, inst))
         continue;

      if (is_nop_mov(inst)) {
         inst = brw_transform_inst(s, inst, BRW_OPCODE_NOP);
         progress = true;
         continue;
      }

      /* Do not allow register coalescing of a value that was generated by a
       * LOAD_REG. Register coalesce works by making the destination of the
       * original instruction (in this case the LOAD_REG) be the same as the
       * destination of the MOV.
       *
       * If the MOV result is not a def (due to multiple writes or being used
       * outside the body of a loop), this will cause the LOAD_REG to also not
       * be a def. That violates the requirement of the LOAD_REG, and it will
       * fail validation.
       */
      const brw_inst *const def = defs.get(inst->src[0]);
      if (def && def->opcode == SHADER_OPCODE_LOAD_REG)
         continue;

      if (src_reg != inst->src[0].nr) {
         src_reg = inst->src[0].nr;

         src_size = s.alloc.sizes[inst->src[0].nr];
         assert(src_size <= (int) live.max_vgrf_size);

         channels_remaining = src_size;
         memset(mov, 0, sizeof(*mov) * live.max_vgrf_size);

         dst_reg = inst->dst.nr;
      }

      if (dst_reg != inst->dst.nr)
         continue;

      if (inst->opcode == SHADER_OPCODE_LOAD_PAYLOAD) {
         for (int i = 0; i < src_size; i++) {
            dst_reg_offset[i] = inst->dst.offset / REG_SIZE + i;
            mov[i] = inst;
         }
         channels_remaining -= regs_written(inst);
      } else {
         const int offset = inst->src[0].offset / REG_SIZE;
         if (mov[offset]) {
            /* This is the second time that this offset in the register has
             * been set.  This means, in particular, that inst->dst was
             * live before this instruction and that the live ranges of
             * inst->dst and inst->src[0] overlap and we can't coalesce the
             * two variables.  Let's ensure that doesn't happen.
             */
            channels_remaining = -1;
            continue;
         }
         for (unsigned i = 0; i < MAX2(inst->size_written / REG_SIZE, 1); i++) {
            dst_reg_offset[offset + i] = inst->dst.offset / REG_SIZE + i;
            mov[offset + i] = inst;
         }
         channels_remaining -= regs_written(inst);
      }

      if (channels_remaining)
         continue;

      bool can_coalesce = true;
      for (int i = 0; i < src_size; i++) {
         if (dst_reg_offset[i] != dst_reg_offset[0] + i) {
            /* Registers are out-of-order. */
            can_coalesce = false;
            src_reg = ~0u;
            break;
         }

         dst_var[i] = live.var_from_vgrf[dst_reg] + dst_reg_offset[i];
         src_var[i] = live.var_from_vgrf[src_reg] + i;

         if (!can_coalesce_vars(devinfo, live, ips, s.cfg, mov[i], dst_var[i], src_var[i]) ||
             would_violate_eot_restriction(s, s.cfg, dst_reg, src_reg)) {
            can_coalesce = false;
            src_reg = ~0u;
            break;
         }
      }

      if (!can_coalesce)
         continue;

      progress = true;

      for (int i = 0; i < src_size; i += regs_written(mov[i])) {
         if (!mov[i])
            continue;

         if (mov[i]->conditional_mod == BRW_CONDITIONAL_NONE) {
            mov[i] = brw_transform_inst(s, mov[i], BRW_OPCODE_NOP);
            mov[i]->dst = reg_undef;
            for (int j = 0; j < mov[i]->sources; j++) {
               mov[i]->src[j] = reg_undef;
            }
         } else {
            /* If we have a conditional modifier, rewrite the MOV to be a
             * MOV.cmod from the coalesced register.  Hopefully, cmod
             * propagation will clean this up and move it to the instruction
             * that writes the register.  If not, this keeps things correct
             * while still letting us coalesce.
             */
            assert(mov[i]->opcode == BRW_OPCODE_MOV);
            assert(mov[i]->sources == 1);
            mov[i]->src[0] = mov[i]->dst;
            mov[i]->dst = retype(brw_null_reg(), mov[i]->dst.type);
         }
      }

      foreach_block_and_inst(block, brw_inst, scan_inst, s.cfg) {
         if (scan_inst->dst.file == VGRF &&
             scan_inst->dst.nr == src_reg) {
            scan_inst->dst.nr = dst_reg;
            scan_inst->dst.offset = scan_inst->dst.offset % REG_SIZE +
               dst_reg_offset[scan_inst->dst.offset / REG_SIZE] * REG_SIZE;
         }

         for (int j = 0; j < scan_inst->sources; j++) {
            if (scan_inst->src[j].file == VGRF &&
                scan_inst->src[j].nr == src_reg) {
               scan_inst->src[j].nr = dst_reg;
               scan_inst->src[j].offset = scan_inst->src[j].offset % REG_SIZE +
                  dst_reg_offset[scan_inst->src[j].offset / REG_SIZE] * REG_SIZE;
            }
         }
      }

      for (int i = 0; i < src_size; i++) {
         live.vars_range[dst_var[i]] = merge(live.vars_range[dst_var[i]],
                                             live.vars_range[src_var[i]]);
      }
      src_reg = ~0u;
   }

   if (progress) {
      foreach_block_and_inst_safe (block, brw_inst, inst, s.cfg) {
         if (inst->opcode == BRW_OPCODE_NOP) {
            inst->remove();
         }
      }

      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);
   }

   delete[] src_var;
   delete[] dst_var;
   delete[] mov;
   delete[] dst_reg_offset;

   return progress;
}
