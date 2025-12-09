/*
 * Copyright © 2010 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_cfg.h"
#include "dev/intel_debug.h"
#include "util/set.h"
#include "util/register_allocate.h"

static void
assign_reg(const struct intel_device_info *devinfo,
           unsigned *reg_hw_locations, brw_reg *reg)
{
   if (reg->file == VGRF) {
      reg->nr = reg_unit(devinfo) * reg_hw_locations[reg->nr] + reg->offset / REG_SIZE;
      reg->offset %= REG_SIZE;
   }
}

void
brw_assign_regs_trivial(brw_shader &s)
{
   const struct intel_device_info *devinfo = s.devinfo;
   unsigned *hw_reg_mapping = ralloc_array(NULL, unsigned, s.alloc.count + 1);
   unsigned i;
   int reg_width = s.dispatch_width / 8;

   /* Note that compressed instructions require alignment to 2 registers. */
   hw_reg_mapping[0] = align(s.first_non_payload_grf, reg_width);
   for (i = 1; i <= s.alloc.count; i++) {
      hw_reg_mapping[i] = (hw_reg_mapping[i - 1] +
                           DIV_ROUND_UP(s.alloc.sizes[i - 1],
                                        reg_unit(devinfo)));
   }
   s.grf_used = hw_reg_mapping[s.alloc.count];

   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      assign_reg(devinfo, hw_reg_mapping, &inst->dst);
      for (i = 0; i < inst->sources; i++) {
         assign_reg(devinfo, hw_reg_mapping, &inst->src[i]);
      }
   }

   if (s.grf_used >= BRW_MAX_GRF) {
      s.fail("Ran out of regs on trivial allocator (%d/%d)\n",
	     s.grf_used, BRW_MAX_GRF);
   } else {
      s.alloc.count = s.grf_used;
   }

   ralloc_free(hw_reg_mapping);
}

extern "C" void
brw_alloc_reg_sets(struct brw_compiler *compiler)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   int base_reg_count = (devinfo->ver >= 30 && !INTEL_DEBUG(DEBUG_NO_VRT) ?
                         XE3_MAX_GRF / reg_unit(devinfo) :
                         BRW_MAX_GRF);

   /* The registers used to make up almost all values handled in the compiler
    * are a scalar value occupying a single register (or 2 registers in the
    * case of SIMD16, which is handled by dividing base_reg_count by 2 and
    * multiplying allocated register numbers by 2).  Things that were
    * aggregates of scalar values at the GLSL level were split to scalar
    * values by split_virtual_grfs().
    *
    * However, texture SEND messages return a series of contiguous registers
    * to write into.  We currently always ask for 4 registers, but we may
    * convert that to use less some day.
    *
    * Additionally, on gfx5 we need aligned pairs of registers for the PLN
    * instruction, and on gfx4 we need 8 contiguous regs for workaround simd16
    * texturing.
    */
   assert(REG_CLASS_COUNT == MAX_VGRF_SIZE(devinfo) / reg_unit(devinfo));
   int class_sizes[REG_CLASS_COUNT];
   for (unsigned i = 0; i < REG_CLASS_COUNT; i++)
      class_sizes[i] = i + 1;

   struct ra_regs *regs = ra_alloc_reg_set(compiler, base_reg_count, false);
   if (devinfo->ver < 30)
      ra_set_allocate_round_robin(regs);
   struct ra_class **classes = ralloc_array(compiler, struct ra_class *,
                                            REG_CLASS_COUNT);

   /* Now, make the register classes for each size of contiguous register
    * allocation we might need to make.
    */
   for (int i = 0; i < REG_CLASS_COUNT; i++) {
      classes[i] = ra_alloc_contig_reg_class(regs, class_sizes[i]);

      for (int reg = 0; reg <= base_reg_count - class_sizes[i]; reg++)
         ra_class_add_reg(classes[i], reg);

      if (devinfo->ver >= 30 && !INTEL_DEBUG(DEBUG_NO_VRT))
         ra_class_override_p(classes[i], 96 - class_sizes[i] + 1);
   }

   ra_set_finalize(regs, NULL);

   compiler->reg_set.regs = regs;
   for (unsigned i = 0; i < ARRAY_SIZE(compiler->reg_set.classes); i++)
      compiler->reg_set.classes[i] = NULL;
   for (int i = 0; i < REG_CLASS_COUNT; i++)
      compiler->reg_set.classes[class_sizes[i] - 1] = classes[i];
}

static int
count_to_loop_end(const bblock_t *block, const brw_ip_ranges &ips)
{
   if (block->end()->opcode == BRW_OPCODE_WHILE)
      return ips.range(block).last();

   int depth = 1;
   /* Skip the first block, since we don't want to count the do the calling
    * function found.
    */
   for (block = block->next();
        depth > 0;
        block = block->next()) {
      if (block->start()->opcode == BRW_OPCODE_DO)
         depth++;
      if (block->end()->opcode == BRW_OPCODE_WHILE) {
         depth--;
         if (depth == 0)
            return ips.range(block).last();
      }
   }
   UNREACHABLE("not reached");
}

void brw_shader::calculate_payload_ranges(bool allow_spilling,
                                          unsigned payload_node_count,
                                          int *payload_last_use_ip) const
{
   const brw_ip_ranges &ips = this->ip_ranges_analysis.require();

   int loop_depth = 0;
   int loop_end_ip = 0;

   for (unsigned i = 0; i < payload_node_count; i++)
      payload_last_use_ip[i] = -1;

   int ip = 0;
   foreach_block_and_inst(block, brw_inst, inst, cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_DO:
         loop_depth++;

         /* Since payload regs are deffed only at the start of the shader
          * execution, any uses of the payload within a loop mean the live
          * interval extends to the end of the outermost loop.  Find the ip of
          * the end now.
          */
         if (loop_depth == 1)
            loop_end_ip = count_to_loop_end(block, ips);
         break;
      case BRW_OPCODE_WHILE:
         loop_depth--;
         break;
      default:
         break;
      }

      int use_ip;
      if (loop_depth > 0)
         use_ip = loop_end_ip;
      else
         use_ip = ip;

      /* Note that UNIFORM args have been turned into FIXED_GRF by
       * assign_curbe_setup(), and interpolation uses fixed hardware regs from
       * the start (see interp_reg()).
       */
      for (int i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == FIXED_GRF) {
            unsigned reg_nr = inst->src[i].nr;
            if (reg_nr / reg_unit(devinfo) >= payload_node_count)
               continue;

            for (unsigned j = reg_nr / reg_unit(devinfo);
                 j < DIV_ROUND_UP(reg_nr + regs_read(devinfo, inst, i),
                                  reg_unit(devinfo));
                 j++) {
               payload_last_use_ip[j] = use_ip;
               assert(j < payload_node_count);
            }
         }
      }

      if (inst->dst.file == FIXED_GRF) {
         unsigned reg_nr = inst->dst.nr;
         if (reg_nr / reg_unit(devinfo) < payload_node_count) {
            for (unsigned j = reg_nr / reg_unit(devinfo);
                 j < DIV_ROUND_UP(reg_nr + regs_written(inst),
                                  reg_unit(devinfo));
                 j++) {
               payload_last_use_ip[j] = use_ip;
               assert(j < payload_node_count);
            }
         }
      }

      ip++;
   }

   /* g0 is needed to construct scratch headers for spilling.  While we could
    * extend its live range each time we spill a register, and update the
    * interference graph accordingly, this would get pretty messy.  Instead,
    * simply consider g0 live for the whole program if spilling is required.
    */
   if (allow_spilling)
      payload_last_use_ip[0] = ip - 1;
}

class brw_reg_alloc {
public:
   brw_reg_alloc(brw_shader *fs):
      fs(fs), devinfo(fs->devinfo), compiler(fs->compiler),
      live(fs->live_analysis.require()), g(NULL),
      have_spill_costs(false)
   {
      mem_ctx = ralloc_context(NULL);

      /* Stash the number of instructions so we can sanity check that our
       * counts still match liveness.
       */
      live_instr_count = fs->cfg->total_instructions;

      spill_insts = _mesa_pointer_set_create(mem_ctx);

      /* Most of this allocation was written for a reg_width of 1
       * (dispatch_width == 8).  In extending to SIMD16, the code was
       * left in place and it was converted to have the hardware
       * registers it's allocating be contiguous physical pairs of regs
       * for reg_width == 2.
       */
      int reg_width = fs->dispatch_width / 8;
      payload_node_count = align(fs->first_non_payload_grf, reg_width);

      /* Get payload IP information */
      payload_last_use_ip = ralloc_array(mem_ctx, int, payload_node_count);

      node_count = 0;
      first_payload_node = 0;
      grf127_send_hack_node = 0;
      first_vgrf_node = 0;
      last_vgrf_node = 0;
      first_spill_node = 0;

      spill_vgrf_ip = NULL;
      spill_vgrf_ip_alloc = 0;
      spill_node_count = 0;

      eot_reg = -1;
   }

   ~brw_reg_alloc()
   {
      ralloc_free(mem_ctx);
   }

   bool assign_regs(bool allow_spilling, bool spill_all);

private:
   void setup_live_interference(unsigned node, brw_range ip_range);
   void setup_inst_interference(const brw_inst *inst);

   bool build_interference_graph(bool allow_spilling);

   brw_reg build_lane_offsets(const brw_builder &bld,
                              uint32_t spill_offset, int ip);
   brw_reg build_single_offset(const brw_builder &bld,
                              uint32_t spill_offset, int ip);
   brw_reg build_legacy_scratch_header(const brw_builder &bld,
                                       uint32_t spill_offset, int ip);

   void emit_unspill(const brw_builder &bld, struct brw_shader_stats *stats,
                     brw_reg dst, uint32_t spill_offset, unsigned count, int ip);
   void emit_spill(const brw_builder &bld, struct brw_shader_stats *stats,
                   brw_reg src, uint32_t spill_offset, unsigned count, int ip);

   void set_spill_costs();
   int choose_spill_reg();
   brw_reg alloc_spill_reg(unsigned size, int ip);
   void spill_reg(unsigned spill_reg);

   void *mem_ctx;
   brw_shader *fs;
   const intel_device_info *devinfo;
   const brw_compiler *compiler;
   const brw_live_variables &live;
   int live_instr_count;

   set *spill_insts;

   int eot_reg;

   ra_graph *g;
   bool have_spill_costs;

   int payload_node_count;
   int *payload_last_use_ip;

   int node_count;
   int first_payload_node;
   int grf127_send_hack_node;
   int first_vgrf_node;
   int last_vgrf_node;
   int first_spill_node;

   int *spill_vgrf_ip;
   int spill_vgrf_ip_alloc;
   int spill_node_count;
};

namespace {
   /**
    * Maximum spill block size we expect to encounter in 32B units.
    *
    * This is somewhat arbitrary and doesn't necessarily limit the maximum
    * variable size that can be spilled -- A higher value will allow a
    * variable of a given size to be spilled more efficiently with a smaller
    * number of scratch messages, but will increase the likelihood of a
    * collision between the MRFs reserved for spilling and other MRFs used by
    * the program (and possibly increase GRF register pressure on platforms
    * without hardware MRFs), what could cause register allocation to fail.
    *
    * For the moment reserve just enough space so a register of 32 bit
    * component type and natural region width can be spilled without splitting
    * into multiple (force_writemask_all) scratch messages.
    */
   unsigned
   spill_max_size(const brw_shader *s)
   {
      /* LSC is limited to SIMD16 sends (SIMD32 on Xe2) */
      if (s->devinfo->has_lsc)
         return 2 * reg_unit(s->devinfo);

      /* FINISHME - On Gfx7+ it should be possible to avoid this limit
       *            altogether by spilling directly from the temporary GRF
       *            allocated to hold the result of the instruction (and the
       *            scratch write header).
       */
      return s->dispatch_width / 8;
   }
}

void
brw_reg_alloc::setup_live_interference(unsigned node, brw_range ip_range)
{
   /* Mark any virtual grf that is live between the start of the program and
    * the last use of a payload node interfering with that payload node.
    */
   for (int i = 0; i < payload_node_count; i++) {
      if (payload_last_use_ip[i] == -1)
         continue;

      /* Note that we use a <= comparison, unlike vgrfs_interfere(),
       * in order to not have to worry about the uniform issue described in
       * calculate_live_intervals().
       */
      if (ip_range.start <= payload_last_use_ip[i])
         ra_add_node_interference(g, node, first_payload_node + i);
   }

   const brw_range clipped_ip_range = clip_end(ip_range, 1);

   /* Add interference with every vgrf whose live range intersects this
    * node's.  We only need to look at nodes below this one as the reflexivity
    * of interference will take care of the rest.
    */
   for (unsigned n2 = first_vgrf_node;
        n2 <= (unsigned)last_vgrf_node && n2 < node; n2++) {
      unsigned vgrf = n2 - first_vgrf_node;

      /* Clip the ranges so the end of a live range can overlap with
       * the start of another live range.  See details in vgrfs_interfere().
       */
      if (overlaps(clip_end(live.vgrf_range[vgrf], 1),
                   clipped_ip_range))
         ra_add_node_interference(g, node, n2);
   }
}

/**
 * Returns true if this instruction's sources and destinations cannot
 * safely be the same register.
 *
 * In most cases, a register can be written over safely by the same
 * instruction that is its last use.  For a single instruction, the
 * sources are dereferenced before writing of the destination starts
 * (naturally).
 *
 * However, there are a few cases where this can be problematic:
 *
 * - Virtual opcodes that translate to multiple instructions in the
 *   code generator: if src == dst and one instruction writes the
 *   destination before a later instruction reads the source, then
 *   src will have been clobbered.
 *
 * - SIMD16 compressed instructions with certain regioning (see below).
 *
 * The register allocator uses this information to set up conflicts between
 * GRF sources and the destination.
 */
static bool
brw_inst_has_source_and_destination_hazard(const struct intel_device_info *devinfo,
                                           const brw_inst *inst, unsigned src)
{
   switch (inst->opcode) {
   case FS_OPCODE_PACK_HALF_2x16_SPLIT:
      /* Multiple partial writes to the destination */
      return true;
   case SHADER_OPCODE_BROADCAST:
      /* This instruction returns an arbitrary channel from the source. If the
       * source is 64-bits and the platform does not support 64-bit integers,
       * the instruction will be split in to multiple instructionsin the
       * generator.  It's possible that one of the instructions will read from
       * a channel corresponding to an earlier instruction.
       *
       * Otherwise, if the SIMD size is larger than the fundamental SIMD size
       * of the platform, the instruction will be implicitly SIMD split. It is
       * possible for earlier "instructions" to overwrite the source needed
       * later.
       */
      return inst->exec_size > 8 * reg_unit(devinfo) ||
             (brw_type_size_bytes(inst->src[src].type) > 4 && !devinfo->has_64bit_int);
   case SHADER_OPCODE_SHUFFLE:
      /* This instruction returns an arbitrary channel from the source and
       * gets split into smaller instructions in the generator.  It's possible
       * that one of the instructions will read from a channel corresponding
       * to an earlier instruction.
       */
   case SHADER_OPCODE_SEL_EXEC:
      /* This is implemented as
       *
       * mov(16)      g4<1>D      0D            { align1 WE_all 1H };
       * mov(16)      g4<1>D      g5<8,8,1>D    { align1 1H }
       *
       * Because the source is only read in the second instruction, the first
       * may stomp all over it.
       */
      return true;
   case SHADER_OPCODE_QUAD_SWIZZLE:
      switch (inst->src[1].ud) {
      case BRW_SWIZZLE_XXXX:
      case BRW_SWIZZLE_YYYY:
      case BRW_SWIZZLE_ZZZZ:
      case BRW_SWIZZLE_WWWW:
      case BRW_SWIZZLE_XXZZ:
      case BRW_SWIZZLE_YYWW:
      case BRW_SWIZZLE_XYXY:
      case BRW_SWIZZLE_ZWZW:
         /* These can be implemented as a single Align1 region on all
          * platforms, so there's never a hazard between source and
          * destination.  C.f. brw_generator::generate_quad_swizzle().
          */
         return false;
      default:
         return !is_uniform(inst->src[0]);
      }
   case BRW_OPCODE_DPAS:
      /* This is overly conservative. The actual hazard is more complicated to
       * describe. When the repeat count is N, the single instruction behaves
       * like N instructions with a repeat count of one, but the destination
       * and source registers are incremented (in somewhat complex ways) for
       * each instruction.
       *
       * This means the source and destination register is actually a range of
       * registers. The hazard exists of an earlier iteration would write a
       * register that should be read by a later iteration.
       *
       * There may be some advantage to properly modeling this, but for now,
       * be overly conservative.
       */
      return inst->as_dpas()->rcount > 1;

   case SHADER_OPCODE_LSC_FILL:
      return false;

   default:
      /* The SIMD16 compressed instruction
       *
       * add(16)      g4<1>F      g4<8,8,1>F   g6<8,8,1>F
       *
       * is actually decoded in hardware as:
       *
       * add(8)       g4<1>F      g4<8,8,1>F   g6<8,8,1>F
       * add(8)       g5<1>F      g5<8,8,1>F   g7<8,8,1>F
       *
       * Which is safe.  However, if we have uniform accesses
       * happening, we get into trouble:
       *
       * add(8)       g4<1>F      g4<0,1,0>F   g6<8,8,1>F
       * add(8)       g5<1>F      g4<0,1,0>F   g7<8,8,1>F
       *
       * Now our destination for the first instruction overwrote the
       * second instruction's src0, and we get garbage for those 8
       * pixels.
       */
      if (inst->exec_size > 8 * reg_unit(devinfo)) {
         if (inst->src[src].file == VGRF && (inst->src[src].stride == 0 ||
                                             inst->src[src].type == BRW_TYPE_UW ||
                                             inst->src[src].type == BRW_TYPE_W ||
                                             inst->src[src].type == BRW_TYPE_UB ||
                                             inst->src[src].type == BRW_TYPE_B)) {
            return true;
         }
      }
      return false;
   }
}

void
brw_reg_alloc::setup_inst_interference(const brw_inst *inst)
{
   /* Certain instructions can't safely use the same register for their
    * sources and destination.  Add interference.
    */
   if (inst->dst.file == VGRF) {
      for (unsigned i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == VGRF &&
             brw_inst_has_source_and_destination_hazard(devinfo, inst, i)) {
            ra_add_node_interference(g, first_vgrf_node + inst->dst.nr,
                                        first_vgrf_node + inst->src[i].nr);
         }
      }
   }

   /* A compressed instruction is actually two instructions executed
    * simultaneously. If the source and destination registers are the same,
    * each instruction overwrites its own source, and there's no problem. The
    * real problem here is if the source and destination registers are off by
    * one. Then you can end up in a scenario where the first instruction
    * overwrites the source of the second instruction. Consider this
    * instruction:
    *
    *    and(16)         g17<1>UD        g16<1,1,0>UD    g13<1,1,0>UD
    *
    * The EU processes this as
    *
    *    and(8)          g17<1>UD        g16<1,1,0>UD    g13<1,1,0>UD
    *    and(8)          g18<1>UD        g17<1,1,0>UD    g14<1,1,0>UD
    *
    * The first SIMD8 part of the instruction overwrites the source used in
    * the second SIMD8 part. Since there's no way to tell the register
    * allocator "the destination register number can be src, but it can't be
    * src+1," simply make the source and destination interfere.
    *
    * Theoretically, the register_coalesce passes should have done the dest ==
    * src merging.
    */
   if (inst->dst.component_size(inst->exec_size) > (reg_unit(devinfo) * REG_SIZE) &&
       inst->dst.file == VGRF) {
      for (int i = 0; i < inst->sources; ++i) {
         if (inst->src[i].file == VGRF) {
            ra_add_node_interference(g, first_vgrf_node + inst->dst.nr,
                                        first_vgrf_node + inst->src[i].nr);
         }
      }
   }

   if (grf127_send_hack_node >= 0) {
      /* Bspec says:
       *
       *    [Pre-CNL] r127 must not be used for return address when there is a
       *    src and dest overlap in send instruction.
       *
       * The Intel Broadwell PRM, vol 07, section "Instruction Set Reference",
       * subsection "EUISA Instructions", Send Message (page 990) contains the
       * same text.
       *
       * We are avoiding using grf127 as part of the destination of send
       * messages adding a node interference to the grf127_send_hack_node.
       * This node has a fixed assignment to grf127.
       *
       * We don't apply it to SIMD16 instructions because previous code avoids
       * any register overlap between sources and destination. Some care is
       * taken to detect when interference may not have been added between
       * source and destination. This can occur in SIMD16 with UW
       * destination. See also gitlab issue #14171.
       */
      if (inst->opcode == SHADER_OPCODE_SEND && inst->dst.file == VGRF &&
          (inst->exec_size < 16 || brw_type_size_bytes(inst->dst.type) < 4))
         ra_add_node_interference(g, first_vgrf_node + inst->dst.nr,
                                     grf127_send_hack_node);
   }

   /* From the Skylake PRM Vol. 2a docs for sends:
    *
    *    "It is required that the second block of GRFs does not overlap with
    *    the first block."
    *
    * Normally, this is taken care of by fixup_sends_duplicate_payload() but
    * in the case where one of the registers is an undefined value, the
    * register allocator may decide that they don't interfere even though
    * they're used as sources in the same instruction.  We also need to add
    * interference here.
    */
   if (inst->opcode == SHADER_OPCODE_SEND && inst->as_send()->ex_mlen > 0 &&
       inst->src[SEND_SRC_PAYLOAD1].file == VGRF &&
       inst->src[SEND_SRC_PAYLOAD2].file == VGRF &&
       inst->src[SEND_SRC_PAYLOAD1].nr != inst->src[SEND_SRC_PAYLOAD2].nr) {
      ra_add_node_interference(g,
         first_vgrf_node + inst->src[SEND_SRC_PAYLOAD1].nr,
         first_vgrf_node + inst->src[SEND_SRC_PAYLOAD2].nr);
   } else if (inst->opcode == SHADER_OPCODE_LSC_SPILL &&
              inst->src[SPILL_SRC_PAYLOAD1].file == VGRF &&
              inst->src[SPILL_SRC_PAYLOAD2].file == VGRF &&
              inst->src[SPILL_SRC_PAYLOAD1].nr != inst->src[SPILL_SRC_PAYLOAD2].nr) {
      ra_add_node_interference(g,
         first_vgrf_node + inst->src[SPILL_SRC_PAYLOAD1].nr,
         first_vgrf_node + inst->src[SPILL_SRC_PAYLOAD2].nr);
   }

   /* When we do send-from-GRF for FB writes, we need to ensure that the last
    * write instruction sends from a high register.  This is because the
    * vertex fetcher wants to start filling the low payload registers while
    * the pixel data port is still working on writing out the memory.  If we
    * don't do this, we get rendering artifacts.
    *
    * We could just do "something high".  Instead, we just pick the highest
    * register that works.
    */
   if (inst->eot && devinfo->ver < 30) {
      assert(inst->opcode == SHADER_OPCODE_SEND);
      const brw_reg srcs[2] = {
         inst->src[SEND_SRC_PAYLOAD1],
         inst->as_send()->ex_mlen > 0 ? inst->src[SEND_SRC_PAYLOAD2] : brw_reg(),
      };
      const unsigned sizes[2] = {
         DIV_ROUND_UP(fs->alloc.sizes[srcs[0].nr], reg_unit(devinfo)),
         DIV_ROUND_UP(srcs[1].file != BAD_FILE ?
                      fs->alloc.sizes[srcs[1].nr] : 0, reg_unit(devinfo)),
      };
      int regs[2] = { BRW_MAX_GRF, BRW_MAX_GRF };

      /* If we haven´t yet allocated a register for the EOT sources, allocate
       * one large enough. Otherwise the assumption is that the backend
       * generates EOT messages that consummes the same amount of source
       * register so we can reuse the existing allocation.
       */
      regs[0] = BRW_MAX_GRF - sizes[0];
      if (grf127_send_hack_node >= 0) {
         /* Avoid r127 which might be unusable if the node was previously
          * written by a SIMD8 SEND message with source/destination overlap.
          */
         regs[0]--;
      }
      assert(regs[0] >= 112);

      ra_set_node_reg(g, first_vgrf_node + srcs[0].nr, regs[0]);
      if (sizes[1] > 0) {
         regs[1] = regs[0] - sizes[1];
         assert(regs[1] >= 112);
         ra_set_node_reg(g, first_vgrf_node + srcs[1].nr, regs[1]);
      }

      /* All the EOT messages should have the same amount of payload as we
       * only use this for last render target write on Gfx11.
       *
       * Unfortunately opt_register_coalesce can merge 2 registers of
       * different sizes (effectively increasing the allocated size of one of
       * the EOT SEND sources). If that register gets spilled, the spiller
       * might reduce the size of the register since it sees some of it is
       * unused. This leads to the computed sizes[] values to change as we
       * register allocate the program.
       *
       * Here we stick to the largest EOT size found during register
       * allocation.
       */
      assert(eot_reg == -1 || eot_reg <= MIN2(regs[0], regs[1]));
      eot_reg = MIN3(eot_reg == -1 ? BRW_MAX_GRF : eot_reg, regs[0], regs[1]);
   }
}

bool
brw_reg_alloc::build_interference_graph(bool allow_spilling)
{
   /* Compute the RA node layout */
   node_count = 0;
   first_payload_node = node_count;
   node_count += payload_node_count;

   /* Bspec says:
    *
    *    [Pre-CNL] r127 must not be used for return address when there is a
    *    src and dest overlap in send instruction.
    *
    * The Intel Broadwell PRM, vol 07, section "Instruction Set Reference",
    * subsection "EUISA Instructions", Send Message (page 990) contains the
    * same text.
    *
    * The workaround will only be applied to Gfx9.
    */
   if (devinfo->ver < 10)
      grf127_send_hack_node = node_count++;
   else
      grf127_send_hack_node = -1;

   first_vgrf_node = node_count;
   node_count += fs->alloc.count;
   last_vgrf_node = node_count - 1;
   first_spill_node = node_count;

   fs->calculate_payload_ranges(allow_spilling, payload_node_count,
                                payload_last_use_ip);

   assert(g == NULL);
   g = ra_alloc_interference_graph(compiler->reg_set.regs, node_count);
   ralloc_steal(mem_ctx, g);

   /* Set up the payload nodes */
   for (int i = 0; i < payload_node_count; i++)
      ra_set_node_reg(g, first_payload_node + i, i);

   if (grf127_send_hack_node >= 0)
      ra_set_node_reg(g, grf127_send_hack_node, 127);

   /* Specify the classes of each virtual register. */
   for (unsigned i = 0; i < fs->alloc.count; i++) {
      unsigned size = DIV_ROUND_UP(fs->alloc.sizes[i], reg_unit(devinfo));

#ifndef NDEBUG
      assert(size <= ARRAY_SIZE(compiler->reg_set.classes) &&
             "Register allocation relies on split_virtual_grfs()");
#else
      if (size > ARRAY_SIZE(compiler->reg_set.classes))
         return false;
#endif

      ra_set_node_class(g, first_vgrf_node + i,
                        compiler->reg_set.classes[size - 1]);
   }

   /* Add interference based on the live range of the register */
   for (unsigned i = 0; i < fs->alloc.count; i++)
      setup_live_interference(first_vgrf_node + i, live.vgrf_range[i]);

   /* Add interference based on the instructions in which a register is used.
    */
   foreach_block_and_inst(block, brw_inst, inst, fs->cfg)
      setup_inst_interference(inst);

   return true;
}

brw_reg
brw_reg_alloc::build_single_offset(const brw_builder &bld, uint32_t spill_offset, int ip)
{
   brw_reg offset = retype(alloc_spill_reg(1, ip), BRW_TYPE_UD);
   brw_inst *inst = bld.MOV(offset, brw_imm_ud(spill_offset));
   _mesa_set_add(spill_insts, inst);
   return offset;
}

brw_reg
brw_reg_alloc::build_lane_offsets(const brw_builder &bld, uint32_t spill_offset, int ip)
{
   assert(bld.dispatch_width() <= 16 * reg_unit(bld.shader->devinfo));

   const brw_builder ubld = bld.exec_all();
   const unsigned reg_count = ubld.dispatch_width() / 8;

   brw_reg offset = retype(alloc_spill_reg(reg_count, ip), BRW_TYPE_UD);
   brw_inst *inst;

   /* Build an offset per lane in SIMD8 */
   inst = ubld.group(8, 0).MOV(retype(offset, BRW_TYPE_UW),
                               brw_imm_uv(0x76543210));
   _mesa_set_add(spill_insts, inst);

   if (spill_offset > 0 && spill_offset <= 0xffffu) {
      inst = ubld.group(8, 0).MAD(offset,
                                  brw_imm_uw(spill_offset),
                                  retype(offset, BRW_TYPE_UW),
                                  brw_imm_uw(4));
      _mesa_set_add(spill_insts, inst);
   } else {
      /* Make the offset a dword */
      inst = ubld.group(8, 0).SHL(offset, retype(offset, BRW_TYPE_UW), brw_imm_uw(2));
      _mesa_set_add(spill_insts, inst);

      /* Add the base offset */
      if (spill_offset) {
         inst = ubld.group(8, 0).ADD(offset, offset, brw_imm_ud(spill_offset));
         _mesa_set_add(spill_insts, inst);
      }
   }

   /* Build offsets in the upper 8 lanes of SIMD16 */
   if (ubld.dispatch_width() > 8) {
      inst = ubld.group(8, 0).ADD(
         byte_offset(offset, REG_SIZE),
         byte_offset(offset, 0),
         brw_imm_ud(8 << 2));
      _mesa_set_add(spill_insts, inst);
   }

   /* Build offsets in the upper 16 lanes of SIMD32 */
   if (ubld.dispatch_width() > 16) {
      inst = ubld.group(16, 0).ADD(
         byte_offset(offset, 2 * REG_SIZE),
         byte_offset(offset, 0),
         brw_imm_ud(16 << 2));
      _mesa_set_add(spill_insts, inst);
   }

   return offset;
}

/**
 * Generate a scratch header for pre-LSC platforms.
 */
brw_reg
brw_reg_alloc::build_legacy_scratch_header(const brw_builder &bld,
                                          uint32_t spill_offset, int ip)
{
   const brw_builder ubld8 = bld.exec_all().group(8, 0);
   const brw_builder ubld1 = bld.exec_all().group(1, 0);

   /* Allocate a spill header and make it interfere with g0 */
   brw_reg header = retype(alloc_spill_reg(1, ip), BRW_TYPE_UD);
   ra_add_node_interference(g, first_vgrf_node + header.nr, first_payload_node);

   brw_inst *inst =
      ubld8.emit(SHADER_OPCODE_SCRATCH_HEADER, header, brw_ud8_grf(0, 0));
   _mesa_set_add(spill_insts, inst);

   /* Write the scratch offset */
   assert(spill_offset % 16 == 0);
   inst = ubld1.MOV(component(header, 2), brw_imm_ud(spill_offset / 16));
   _mesa_set_add(spill_insts, inst);

   return header;
}

void
brw_reg_alloc::emit_unspill(const brw_builder &bld,
                           struct brw_shader_stats *stats,
                           brw_reg dst,
                           uint32_t spill_offset, unsigned count, int ip)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const unsigned reg_size = dst.component_size(bld.dispatch_width()) /
                             REG_SIZE;

   for (unsigned i = 0; i < DIV_ROUND_UP(count, reg_size); i++) {
      ++stats->fill_count;

      if (devinfo->verx10 >= 125) {
         /* LSC is limited to SIMD16 (SIMD32 on Xe2) load/store but we can
          * load more using transpose messages.
          */
         const bool use_transpose =
            bld.dispatch_width() > 16 * reg_unit(devinfo) ||
            bld.has_writemask_all();
         const brw_builder ubld = use_transpose ? bld.uniform() : bld;
         brw_reg offset;
         if (use_transpose) {
            offset = build_single_offset(ubld, spill_offset, ip);
         } else {
            offset = build_lane_offsets(ubld, spill_offset, ip);
         }

         const bool exec_all = use_transpose || bld.has_writemask_all();
         brw_scratch_inst *unspill_inst = bld.exec_all(exec_all).FILL();
         unspill_inst->dst = dst;

         unspill_inst->src[FILL_SRC_PAYLOAD1] = offset;

         unspill_inst->offset = spill_offset;
         unspill_inst->use_transpose = use_transpose;
         unspill_inst->size_written =
            lsc_msg_dest_len(devinfo, LSC_DATA_SIZE_D32, bld.dispatch_width()) * REG_SIZE;
         assert(unspill_inst->size_written == (reg_size * REG_SIZE));

         _mesa_set_add(spill_insts, unspill_inst);
         assert(unspill_inst->force_writemask_all || count % reg_size == 0);
      } else {
         brw_reg header = build_legacy_scratch_header(bld, spill_offset, ip);

         const unsigned bti = GFX8_BTI_STATELESS_NON_COHERENT;

         brw_send_inst *unspill_inst = bld.SEND();
         unspill_inst->dst = dst;

         unspill_inst->src[SEND_SRC_DESC]     = brw_imm_ud(0);
         unspill_inst->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
         unspill_inst->src[SEND_SRC_PAYLOAD1] = header;
         unspill_inst->src[SEND_SRC_PAYLOAD2] = brw_reg();

         unspill_inst->mlen = 1;
         unspill_inst->header_size = 1;
         unspill_inst->size_written = reg_size * REG_SIZE;
         unspill_inst->has_side_effects = false;
         unspill_inst->is_volatile = true;
         unspill_inst->sfid = BRW_SFID_HDC0;

         unspill_inst->src[0] = brw_imm_ud(
            brw_dp_desc(devinfo, bti,
                        BRW_DATAPORT_READ_MESSAGE_OWORD_BLOCK_READ,
                        BRW_DATAPORT_OWORD_BLOCK_DWORDS(reg_size * 8)) |
            brw_message_desc(devinfo,
                             unspill_inst->mlen,
                             unspill_inst->size_written / REG_SIZE,
                             unspill_inst->header_size));

         _mesa_set_add(spill_insts, unspill_inst);
         assert(unspill_inst->force_writemask_all || count % reg_size == 0);
      }

      dst.offset += reg_size * REG_SIZE;
      spill_offset += reg_size * REG_SIZE;
   }
}

void
brw_reg_alloc::emit_spill(const brw_builder &bld,
                         struct brw_shader_stats *stats,
                         brw_reg src,
                         uint32_t spill_offset, unsigned count, int ip)
{
   const intel_device_info *devinfo = bld.shader->devinfo;
   const unsigned reg_size = src.component_size(bld.dispatch_width()) /
                             REG_SIZE;

   for (unsigned i = 0; i < DIV_ROUND_UP(count, reg_size); i++) {
      ++stats->spill_count;

      if (devinfo->verx10 >= 125) {
         brw_reg offset = build_lane_offsets(bld, spill_offset, ip);

         brw_scratch_inst *spill_inst = bld.SPILL();
         spill_inst->dst = bld.null_reg_f();

         spill_inst->src[SPILL_SRC_PAYLOAD1] = offset;
         spill_inst->src[SPILL_SRC_PAYLOAD2] = src;

         spill_inst->offset = spill_offset;
         spill_inst->use_transpose = false;

         _mesa_set_add(spill_insts, spill_inst);
         assert(spill_inst->force_writemask_all || count % reg_size == 0);
      } else {
         brw_reg header = build_legacy_scratch_header(bld, spill_offset, ip);

         const unsigned bti = GFX8_BTI_STATELESS_NON_COHERENT;

         brw_send_inst *spill_inst = bld.SEND();
         spill_inst->dst = bld.null_reg_f();

         spill_inst->src[SEND_SRC_DESC]     = brw_imm_ud(0);
         spill_inst->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
         spill_inst->src[SEND_SRC_PAYLOAD1] = header;
         spill_inst->src[SEND_SRC_PAYLOAD2] = src;

         spill_inst->mlen = 1;
         spill_inst->ex_mlen = reg_size;
         spill_inst->size_written = 0;
         spill_inst->header_size = 1;
         spill_inst->has_side_effects = true;
         spill_inst->is_volatile = false;
         spill_inst->sfid = BRW_SFID_HDC0;

         spill_inst->src[0] = brw_imm_ud(
            brw_dp_desc(devinfo, bti,
                        GFX6_DATAPORT_WRITE_MESSAGE_OWORD_BLOCK_WRITE,
                        BRW_DATAPORT_OWORD_BLOCK_DWORDS(reg_size * 8)) |
            brw_message_desc(devinfo,
                             spill_inst->mlen,
                             spill_inst->size_written / REG_SIZE,
                             spill_inst->header_size));
         spill_inst->src[1] = brw_imm_ud(
            brw_message_ex_desc(devinfo, spill_inst->ex_mlen));

         _mesa_set_add(spill_insts, spill_inst);
         assert(spill_inst->force_writemask_all || count % reg_size == 0);
      }

      src.offset += reg_size * REG_SIZE;
      spill_offset += reg_size * REG_SIZE;
   }
}

void
brw_reg_alloc::set_spill_costs()
{
   float block_scale = 1.0;
   float *spill_costs = rzalloc_array(NULL, float, fs->alloc.count);

   /* Calculate costs for spilling nodes.  Call it a cost of 1 per
    * spill/unspill we'll have to do, and guess that the insides of
    * loops run 10 times.
    */
   foreach_block_and_inst(block, brw_inst, inst, fs->cfg) {
      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == VGRF)
            spill_costs[inst->src[i].nr] += regs_read(devinfo, inst, i) * block_scale;
      }

      if (inst->dst.file == VGRF)
         spill_costs[inst->dst.nr] += regs_written(inst) * block_scale;

      /* Don't spill anything we generated while spilling */
      if (_mesa_set_search(spill_insts, inst)) {
         for (unsigned int i = 0; i < inst->sources; i++) {
	    if (inst->src[i].file == VGRF)
               spill_costs[inst->src[i].nr] = INFINITY;
         }
	 if (inst->dst.file == VGRF)
            spill_costs[inst->dst.nr] = INFINITY;
      }

      switch (inst->opcode) {

      case BRW_OPCODE_DO:
	 block_scale *= 10;
	 break;

      case BRW_OPCODE_WHILE:
	 block_scale /= 10;
	 break;

      case BRW_OPCODE_IF:
         block_scale *= 0.5;
         break;

      case BRW_OPCODE_ENDIF:
         block_scale /= 0.5;
         break;

      default:
	 break;
      }
   }

   for (unsigned i = 0; i < fs->alloc.count; i++) {
      /* Do the no_spill check first.  Registers that are used as spill
       * temporaries may have been allocated after we calculated liveness so
       * we shouldn't look their liveness up.  Fortunately, they're always
       * used in SCRATCH_READ/WRITE instructions so they'll always be flagged
       * no_spill.
       */
      if (isinf(spill_costs[i]))
         continue;

      int live_length = live.vgrf_range[i].last() - live.vgrf_range[i].start;
      if (live_length <= 0)
         continue;

      /* Divide the cost (in number of spills/fills) by the log of the length
       * of the live range of the register.  This will encourage spill logic
       * to spill long-living things before spilling short-lived things where
       * spilling is less likely to actually do us any good.  We use the log
       * of the length because it will fall off very quickly and not cause us
       * to spill medium length registers with more uses.
       */
      float adjusted_cost = spill_costs[i] / logf(live_length);
      ra_set_node_spill_cost(g, first_vgrf_node + i, adjusted_cost);
   }

   have_spill_costs = true;

   ralloc_free(spill_costs);
}

int
brw_reg_alloc::choose_spill_reg()
{
   if (!have_spill_costs)
      set_spill_costs();

   int node = ra_get_best_spill_node(g);
   if (node < 0)
      return -1;

   assert(node >= first_vgrf_node);
   return node - first_vgrf_node;
}

brw_reg
brw_reg_alloc::alloc_spill_reg(unsigned size, int ip)
{
   int vgrf = brw_allocate_vgrf_units(*fs, align(size, reg_unit(devinfo))).nr;
   int class_idx = DIV_ROUND_UP(size, reg_unit(devinfo)) - 1;
   int n = ra_add_node(g, compiler->reg_set.classes[class_idx]);
   assert(n == first_vgrf_node + vgrf);
   assert(n == first_spill_node + spill_node_count);

   brw_range spill_reg_range{ ip - 1, ip + 2 };
   setup_live_interference(n, spill_reg_range);

   /* Add interference between this spill node and any other spill nodes for
    * the same instruction.
    */
   for (int s = 0; s < spill_node_count; s++) {
      if (spill_vgrf_ip[s] == ip)
         ra_add_node_interference(g, n, first_spill_node + s);
   }

   /* Add this spill node to the list for next time */
   if (spill_node_count >= spill_vgrf_ip_alloc) {
      if (spill_vgrf_ip_alloc == 0)
         spill_vgrf_ip_alloc = 16;
      else
         spill_vgrf_ip_alloc *= 2;
      spill_vgrf_ip = reralloc(mem_ctx, spill_vgrf_ip, int,
                               spill_vgrf_ip_alloc);
   }
   spill_vgrf_ip[spill_node_count++] = ip;

   return brw_vgrf(vgrf, BRW_TYPE_F);
}

void
brw_reg_alloc::spill_reg(unsigned spill_reg)
{
   int size = fs->alloc.sizes[spill_reg];
   unsigned int spill_offset = fs->last_scratch;
   assert(align(spill_offset, 16) == spill_offset); /* oword read/write req. */

   fs->spilled_any_registers = true;

   fs->last_scratch += align(size * REG_SIZE, REG_SIZE * reg_unit(devinfo));

   /* We're about to replace all uses of this register.  It no longer
    * conflicts with anything so we can get rid of its interference.
    */
   ra_set_node_spill_cost(g, first_vgrf_node + spill_reg, 0);
   ra_reset_node_interference(g, first_vgrf_node + spill_reg);

   /* Generate spill/unspill instructions for the objects being
    * spilled.  Right now, we spill or unspill the whole thing to a
    * virtual grf of the same size.  For most instructions, though, we
    * could just spill/unspill the GRF being accessed.
    */
   int ip = 0;
   foreach_block_and_inst (block, brw_inst, inst, fs->cfg) {
      const brw_builder ibld = brw_builder(inst);
      brw_exec_node *before = inst->prev;
      brw_exec_node *after = inst->next;

      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == VGRF &&
             inst->src[i].nr == spill_reg) {
            /* Count registers needed in units of physical registers */
            int count = align(regs_read(devinfo, inst, i), reg_unit(devinfo));
            /* Align the spilling offset the physical register size */
            int subset_spill_offset = spill_offset +
               ROUND_DOWN_TO(inst->src[i].offset, REG_SIZE * reg_unit(devinfo));
            brw_reg unspill_dst = alloc_spill_reg(count, ip);

            inst->src[i].nr = unspill_dst.nr;
            /* The unspilled register is aligned to physical register, so
             * adjust the offset to the remaining within the physical register
             * size.
             */
            inst->src[i].offset %= REG_SIZE * reg_unit(devinfo);

            /* We read the largest power-of-two divisor of the register count
             * (because only POT scratch read blocks are allowed by the
             * hardware) up to the maximum supported block size.
             */
            const unsigned width =
               MIN2(32, 1u << (ffs(MAX2(1, count) * 8) - 1));

            /* Set exec_all() on unspill messages under the (rather
             * pessimistic) assumption that there is no one-to-one
             * correspondence between channels of the spilled variable in
             * scratch space and the scratch read message, which operates on
             * 32 bit channels.  It shouldn't hurt in any case because the
             * unspill destination is a block-local temporary.
             *
             * FINIHSME: However, this will prevent brw_opt_fill_and_spill
             * from making progress if the lsc_fill is NoMask and the
             * lsc_spill is not.
             */
            emit_unspill(ibld.exec_all().group(width, 0), &fs->shader_stats,
                         unspill_dst, subset_spill_offset, count, ip);
	 }
      }

      if (inst->dst.file == VGRF &&
          inst->dst.nr == spill_reg &&
          inst->opcode != SHADER_OPCODE_UNDEF) {
         /* Count registers needed in units of physical registers */
         int count = align(regs_written(inst), reg_unit(devinfo));
         /* Align the spilling offset the physical register size */
         int subset_spill_offset = spill_offset +
            ROUND_DOWN_TO(inst->dst.offset, reg_unit(devinfo) * REG_SIZE);
         brw_reg spill_src = alloc_spill_reg(count, ip);

         inst->dst.nr = spill_src.nr;
         /* The spilled register is aligned to physical register, so adjust
          * the offset to the remaining within the physical register size.
          */
         inst->dst.offset %= REG_SIZE * reg_unit(devinfo);

         /* Calculate the execution width of the scratch messages (which work
          * in terms of 32 bit components so we have a fixed number of eight
          * channels per spilled register).  We attempt to write one
          * exec_size-wide component of the variable at a time without
          * exceeding the maximum number of (fake) MRF registers reserved for
          * spills.
          */
         const unsigned width = 8 * reg_unit(devinfo) *
            DIV_ROUND_UP(MIN2(inst->dst.component_size(inst->exec_size),
                              spill_max_size(fs) * REG_SIZE),
                         reg_unit(devinfo) * REG_SIZE);

         /* Spills should only write data initialized by the instruction for
          * whichever channels are enabled in the execution mask.  If that's
          * not possible we'll have to emit a matching unspill before the
          * instruction and set force_writemask_all on the spill.
          */
         const bool per_channel =
            inst->dst.is_contiguous() &&
            brw_type_size_bytes(inst->dst.type) == 4 &&
            inst->exec_size == width;

         /* Builder used to emit the scratch messages. */
         const brw_builder ubld = ibld.exec_all(!per_channel).group(width, 0);

	 /* If our write is going to affect just part of the
          * regs_written(inst), then we need to unspill the destination since
          * we write back out all of the regs_written().  If the original
          * instruction had force_writemask_all set and is not a partial
          * write, there should be no need for the unspill since the
          * instruction will be overwriting the whole destination in any case.
	  */
         if (inst->is_partial_write(reg_unit(devinfo) * REG_SIZE) ||
             (!inst->force_writemask_all && !per_channel))
            emit_unspill(ubld, &fs->shader_stats, spill_src,
                         subset_spill_offset, regs_written(inst), ip);

         emit_spill(ubld.after(inst), &fs->shader_stats, spill_src,
                    subset_spill_offset, regs_written(inst), ip);
      }

      for (brw_inst *inst = (brw_inst *)before->next;
           inst != after; inst = (brw_inst *)inst->next)
         setup_inst_interference(inst);

      /* We don't advance the ip for scratch read/write instructions
       * because we consider them to have the same ip as instruction we're
       * spilling around for the purposes of interference.  Also, we're
       * inserting spill instructions without re-running liveness analysis
       * and we don't want to mess up our IPs.
       */
      if (!_mesa_set_search(spill_insts, inst))
         ip++;
   }

   assert(ip == live_instr_count);
}

bool
brw_reg_alloc::assign_regs(bool allow_spilling, bool spill_all)
{
   if (!build_interference_graph(allow_spilling))
      return false;

   unsigned spilled = 0;
   while (1) {
      /* Debug of register spilling: Go spill everything. */
      if (unlikely(spill_all)) {
         int reg = choose_spill_reg();
         if (reg != -1) {
            spill_reg(reg);
            spilled++;
            continue;
         }
      }

      if (ra_allocate(g))
         break;

      if (!allow_spilling)
         return false;

      /* Failed to allocate registers.  Spill some regs, and the caller will
       * loop back into here to try again.
       */
      unsigned nr_spills = 1;
      if (compiler->spilling_rate)
         nr_spills = MAX2(1, spilled / compiler->spilling_rate);

      for (unsigned j = 0; j < nr_spills; j++) {
         int reg = choose_spill_reg();
         if (reg == -1) {
            if (j == 0)
               return false; /* Nothing to spill */
            break;
         }

         spill_reg(reg);
         spilled++;
      }
   }

   if (spilled)
      fs->invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                              BRW_DEPENDENCY_VARIABLES);

   /* Get the chosen virtual registers for each node, and map virtual
    * regs in the register classes back down to real hardware reg
    * numbers.
    */
   unsigned *hw_reg_mapping = ralloc_array(NULL, unsigned, fs->alloc.count);
   fs->grf_used = fs->first_non_payload_grf;
   for (unsigned i = 0; i < fs->alloc.count; i++) {
      int reg = ra_get_node_reg(g, first_vgrf_node + i);

      hw_reg_mapping[i] = reg;
      fs->grf_used = MAX2(fs->grf_used,
			  hw_reg_mapping[i] + DIV_ROUND_UP(fs->alloc.sizes[i],
                                                           reg_unit(devinfo)));
   }

   foreach_block_and_inst(block, brw_inst, inst, fs->cfg) {
      assign_reg(devinfo, hw_reg_mapping, &inst->dst);
      for (int i = 0; i < inst->sources; i++) {
         assign_reg(devinfo, hw_reg_mapping, &inst->src[i]);
      }
   }

   fs->alloc.count = fs->grf_used;

   ralloc_free(hw_reg_mapping);

   return true;
}

bool
brw_assign_regs(brw_shader &s, bool allow_spilling, bool spill_all)
{
   /* Ensure that g0 is reserved for spilling since there is going to be
    * spilling.
    */
   if (spill_all)
      allow_spilling = true;

   brw_reg_alloc alloc(&s);
   bool success = alloc.assign_regs(allow_spilling, spill_all);
   if (!success && allow_spilling) {
      s.fail("no register to spill:\n");
      brw_print_instructions(s);
   }
   return success;
}
