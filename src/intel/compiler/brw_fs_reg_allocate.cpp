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
#include "brw_fs.h"
#include "brw_cfg.h"
#include "util/set.h"
#include "util/register_allocate.h"

using namespace brw;

static void
assign_reg(unsigned *reg_hw_locations, fs_reg *reg)
{
   if (reg->file == VGRF) {
      reg->nr = reg_hw_locations[reg->nr] + reg->offset / REG_SIZE;
      reg->offset %= REG_SIZE;
   }
}

void
fs_visitor::assign_regs_trivial()
{
   unsigned hw_reg_mapping[this->alloc.count + 1];
   unsigned i;
   int reg_width = dispatch_width / 8;

   /* Note that compressed instructions require alignment to 2 registers. */
   hw_reg_mapping[0] = ALIGN(this->first_non_payload_grf, reg_width);
   for (i = 1; i <= this->alloc.count; i++) {
      hw_reg_mapping[i] = (hw_reg_mapping[i - 1] +
			   this->alloc.sizes[i - 1]);
   }
   this->grf_used = hw_reg_mapping[this->alloc.count];

   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      assign_reg(hw_reg_mapping, &inst->dst);
      for (i = 0; i < inst->sources; i++) {
         assign_reg(hw_reg_mapping, &inst->src[i]);
      }
   }

   if (this->grf_used >= max_grf) {
      fail("Ran out of regs on trivial allocator (%d/%d)\n",
	   this->grf_used, max_grf);
   } else {
      this->alloc.count = this->grf_used;
   }

}

/**
 * Size of a register from the aligned_bary_class register class.
 */
static unsigned
aligned_bary_size(unsigned dispatch_width)
{
   return (dispatch_width == 8 ? 2 : 4);
}

static void
brw_alloc_reg_set(struct brw_compiler *compiler, int dispatch_width)
{
   const struct gen_device_info *devinfo = compiler->devinfo;
   int base_reg_count = BRW_MAX_GRF;
   const int index = util_logbase2(dispatch_width / 8);

   if (dispatch_width > 8 && devinfo->gen >= 7) {
      /* For IVB+, we don't need the PLN hacks or the even-reg alignment in
       * SIMD16.  Therefore, we can use the exact same register sets for
       * SIMD16 as we do for SIMD8 and we don't need to recalculate them.
       */
      compiler->fs_reg_sets[index] = compiler->fs_reg_sets[0];
      return;
   }

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
    * Additionally, on gen5 we need aligned pairs of registers for the PLN
    * instruction, and on gen4 we need 8 contiguous regs for workaround simd16
    * texturing.
    */
   const int class_count = MAX_VGRF_SIZE;
   int class_sizes[MAX_VGRF_SIZE];
   for (unsigned i = 0; i < MAX_VGRF_SIZE; i++)
      class_sizes[i] = i + 1;

   memset(compiler->fs_reg_sets[index].class_to_ra_reg_range, 0,
          sizeof(compiler->fs_reg_sets[index].class_to_ra_reg_range));
   int *class_to_ra_reg_range = compiler->fs_reg_sets[index].class_to_ra_reg_range;

   /* Compute the total number of registers across all classes. */
   int ra_reg_count = 0;
   for (int i = 0; i < class_count; i++) {
      if (devinfo->gen <= 5 && dispatch_width >= 16) {
         /* From the G45 PRM:
          *
          * In order to reduce the hardware complexity, the following
          * rules and restrictions apply to the compressed instruction:
          * ...
          * * Operand Alignment Rule: With the exceptions listed below, a
          *   source/destination operand in general should be aligned to
          *   even 256-bit physical register with a region size equal to
          *   two 256-bit physical register
          */
         ra_reg_count += (base_reg_count - (class_sizes[i] - 1)) / 2;
      } else {
         ra_reg_count += base_reg_count - (class_sizes[i] - 1);
      }
      /* Mark the last register. We'll fill in the beginnings later. */
      class_to_ra_reg_range[class_sizes[i]] = ra_reg_count;
   }

   /* Fill out the rest of the range markers */
   for (int i = 1; i < 17; ++i) {
      if (class_to_ra_reg_range[i] == 0)
         class_to_ra_reg_range[i] = class_to_ra_reg_range[i-1];
   }

   uint8_t *ra_reg_to_grf = ralloc_array(compiler, uint8_t, ra_reg_count);
   struct ra_regs *regs = ra_alloc_reg_set(compiler, ra_reg_count, false);
   if (devinfo->gen >= 6)
      ra_set_allocate_round_robin(regs);
   int *classes = ralloc_array(compiler, int, class_count);
   int aligned_bary_class = -1;

   /* Allocate space for q values.  We allocate class_count + 1 because we
    * want to leave room for the aligned barycentric class if we have it.
    */
   unsigned int **q_values = ralloc_array(compiler, unsigned int *,
                                          class_count + 1);
   for (int i = 0; i < class_count + 1; ++i)
      q_values[i] = ralloc_array(q_values, unsigned int, class_count + 1);

   /* Now, add the registers to their classes, and add the conflicts
    * between them and the base GRF registers (and also each other).
    */
   int reg = 0;
   int aligned_bary_base_reg = 0;
   int aligned_bary_reg_count = 0;
   for (int i = 0; i < class_count; i++) {
      int class_reg_count;
      if (devinfo->gen <= 5 && dispatch_width >= 16) {
         class_reg_count = (base_reg_count - (class_sizes[i] - 1)) / 2;

         /* See comment below.  The only difference here is that we are
          * dealing with pairs of registers instead of single registers.
          * Registers of odd sizes simply get rounded up. */
         for (int j = 0; j < class_count; j++)
            q_values[i][j] = (class_sizes[i] + 1) / 2 +
                             (class_sizes[j] + 1) / 2 - 1;
      } else {
         class_reg_count = base_reg_count - (class_sizes[i] - 1);

         /* From register_allocate.c:
          *
          * q(B,C) (indexed by C, B is this register class) in
          * Runeson/Nyström paper.  This is "how many registers of B could
          * the worst choice register from C conflict with".
          *
          * If we just let the register allocation algorithm compute these
          * values, is extremely expensive.  However, since all of our
          * registers are laid out, we can very easily compute them
          * ourselves.  View the register from C as fixed starting at GRF n
          * somwhere in the middle, and the register from B as sliding back
          * and forth.  Then the first register to conflict from B is the
          * one starting at n - class_size[B] + 1 and the last register to
          * conflict will start at n + class_size[B] - 1.  Therefore, the
          * number of conflicts from B is class_size[B] + class_size[C] - 1.
          *
          *   +-+-+-+-+-+-+     +-+-+-+-+-+-+
          * B | | | | | |n| --> | | | | | | |
          *   +-+-+-+-+-+-+     +-+-+-+-+-+-+
          *             +-+-+-+-+-+
          * C           |n| | | | |
          *             +-+-+-+-+-+
          */
         for (int j = 0; j < class_count; j++)
            q_values[i][j] = class_sizes[i] + class_sizes[j] - 1;
      }
      classes[i] = ra_alloc_reg_class(regs);

      /* Save this off for the aligned barycentric class at the end. */
      if (class_sizes[i] == int(aligned_bary_size(dispatch_width))) {
         aligned_bary_base_reg = reg;
         aligned_bary_reg_count = class_reg_count;
      }

      if (devinfo->gen <= 5 && dispatch_width >= 16) {
         for (int j = 0; j < class_reg_count; j++) {
            ra_class_add_reg(regs, classes[i], reg);

            ra_reg_to_grf[reg] = j * 2;

            for (int base_reg = j;
                 base_reg < j + (class_sizes[i] + 1) / 2;
                 base_reg++) {
               ra_add_reg_conflict(regs, base_reg, reg);
            }

            reg++;
         }
      } else {
         for (int j = 0; j < class_reg_count; j++) {
            ra_class_add_reg(regs, classes[i], reg);

            ra_reg_to_grf[reg] = j;

            for (int base_reg = j;
                 base_reg < j + class_sizes[i];
                 base_reg++) {
               ra_add_reg_conflict(regs, base_reg, reg);
            }

            reg++;
         }
      }
   }
   assert(reg == ra_reg_count);

   /* Applying transitivity to all of the base registers gives us the
    * appropreate register conflict relationships everywhere.
    */
   for (int reg = 0; reg < base_reg_count; reg++)
      ra_make_reg_conflicts_transitive(regs, reg);

   /* Add a special class for aligned barycentrics, which we'll put the
    * first source of LINTERP on so that we can do PLN on Gen <= 6.
    */
   if (devinfo->has_pln && (devinfo->gen == 6 ||
                            (dispatch_width == 8 && devinfo->gen <= 5))) {
      aligned_bary_class = ra_alloc_reg_class(regs);

      for (int i = 0; i < aligned_bary_reg_count; i++) {
	 if ((ra_reg_to_grf[aligned_bary_base_reg + i] & 1) == 0) {
	    ra_class_add_reg(regs, aligned_bary_class,
                             aligned_bary_base_reg + i);
	 }
      }

      for (int i = 0; i < class_count; i++) {
         /* These are a little counter-intuitive because the barycentric
          * registers are required to be aligned while the register they are
          * potentially interferring with are not.  In the case where the size
          * is even, the worst-case is that the register is odd-aligned.  In
          * the odd-size case, it doesn't matter.
          */
         q_values[class_count][i] = class_sizes[i] / 2 +
                                    aligned_bary_size(dispatch_width) / 2;
         q_values[i][class_count] = class_sizes[i] +
                                    aligned_bary_size(dispatch_width) - 1;
      }
      q_values[class_count][class_count] = aligned_bary_size(dispatch_width) - 1;
   }

   ra_set_finalize(regs, q_values);

   ralloc_free(q_values);

   compiler->fs_reg_sets[index].regs = regs;
   for (unsigned i = 0; i < ARRAY_SIZE(compiler->fs_reg_sets[index].classes); i++)
      compiler->fs_reg_sets[index].classes[i] = -1;
   for (int i = 0; i < class_count; i++)
      compiler->fs_reg_sets[index].classes[class_sizes[i] - 1] = classes[i];
   compiler->fs_reg_sets[index].ra_reg_to_grf = ra_reg_to_grf;
   compiler->fs_reg_sets[index].aligned_bary_class = aligned_bary_class;
}

void
brw_fs_alloc_reg_sets(struct brw_compiler *compiler)
{
   brw_alloc_reg_set(compiler, 8);
   brw_alloc_reg_set(compiler, 16);
   brw_alloc_reg_set(compiler, 32);
}

static int
count_to_loop_end(const bblock_t *block)
{
   if (block->end()->opcode == BRW_OPCODE_WHILE)
      return block->end_ip;

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
            return block->end_ip;
      }
   }
   unreachable("not reached");
}

void fs_visitor::calculate_payload_ranges(int payload_node_count,
                                          int *payload_last_use_ip) const
{
   int loop_depth = 0;
   int loop_end_ip = 0;

   for (int i = 0; i < payload_node_count; i++)
      payload_last_use_ip[i] = -1;

   int ip = 0;
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_DO:
         loop_depth++;

         /* Since payload regs are deffed only at the start of the shader
          * execution, any uses of the payload within a loop mean the live
          * interval extends to the end of the outermost loop.  Find the ip of
          * the end now.
          */
         if (loop_depth == 1)
            loop_end_ip = count_to_loop_end(block);
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
            int node_nr = inst->src[i].nr;
            if (node_nr >= payload_node_count)
               continue;

            for (unsigned j = 0; j < regs_read(inst, i); j++) {
               payload_last_use_ip[node_nr + j] = use_ip;
               assert(node_nr + j < unsigned(payload_node_count));
            }
         }
      }

      /* Special case instructions which have extra implied registers used. */
      switch (inst->opcode) {
      case CS_OPCODE_CS_TERMINATE:
         payload_last_use_ip[0] = use_ip;
         break;

      default:
         if (inst->eot) {
            /* We could omit this for the !inst->header_present case, except
             * that the simulator apparently incorrectly reads from g0/g1
             * instead of sideband.  It also really freaks out driver
             * developers to see g0 used in unusual places, so just always
             * reserve it.
             */
            payload_last_use_ip[0] = use_ip;
            payload_last_use_ip[1] = use_ip;
         }
         break;
      }

      ip++;
   }
}

class fs_reg_alloc {
public:
   fs_reg_alloc(fs_visitor *fs):
      fs(fs), devinfo(fs->devinfo), compiler(fs->compiler),
      live(fs->live_analysis.require()), g(NULL),
      have_spill_costs(false)
   {
      mem_ctx = ralloc_context(NULL);

      /* Stash the number of instructions so we can sanity check that our
       * counts still match liveness.
       */
      live_instr_count = fs->cfg->last_block()->end_ip + 1;

      spill_insts = _mesa_pointer_set_create(mem_ctx);

      /* Most of this allocation was written for a reg_width of 1
       * (dispatch_width == 8).  In extending to SIMD16, the code was
       * left in place and it was converted to have the hardware
       * registers it's allocating be contiguous physical pairs of regs
       * for reg_width == 2.
       */
      int reg_width = fs->dispatch_width / 8;
      rsi = util_logbase2(reg_width);
      payload_node_count = ALIGN(fs->first_non_payload_grf, reg_width);

      /* Get payload IP information */
      payload_last_use_ip = ralloc_array(mem_ctx, int, payload_node_count);

      node_count = 0;
      first_payload_node = 0;
      first_mrf_hack_node = 0;
      scratch_header_node = 0;
      grf127_send_hack_node = 0;
      first_vgrf_node = 0;
      last_vgrf_node = 0;
      first_spill_node = 0;

      spill_vgrf_ip = NULL;
      spill_vgrf_ip_alloc = 0;
      spill_node_count = 0;
   }

   ~fs_reg_alloc()
   {
      ralloc_free(mem_ctx);
   }

   bool assign_regs(bool allow_spilling, bool spill_all);

private:
   void setup_live_interference(unsigned node,
                                int node_start_ip, int node_end_ip);
   void setup_inst_interference(const fs_inst *inst);

   void build_interference_graph(bool allow_spilling);
   void discard_interference_graph();

   void emit_unspill(const fs_builder &bld, fs_reg dst,
                     uint32_t spill_offset, unsigned count);
   void emit_spill(const fs_builder &bld, fs_reg src,
                   uint32_t spill_offset, unsigned count);

   void set_spill_costs();
   int choose_spill_reg();
   fs_reg alloc_scratch_header();
   fs_reg alloc_spill_reg(unsigned size, int ip);
   void spill_reg(unsigned spill_reg);

   void *mem_ctx;
   fs_visitor *fs;
   const gen_device_info *devinfo;
   const brw_compiler *compiler;
   const fs_live_variables &live;
   int live_instr_count;

   set *spill_insts;

   /* Which compiler->fs_reg_sets[] to use */
   int rsi;

   ra_graph *g;
   bool have_spill_costs;

   int payload_node_count;
   int *payload_last_use_ip;

   int node_count;
   int first_payload_node;
   int first_mrf_hack_node;
   int scratch_header_node;
   int grf127_send_hack_node;
   int first_vgrf_node;
   int last_vgrf_node;
   int first_spill_node;

   int *spill_vgrf_ip;
   int spill_vgrf_ip_alloc;
   int spill_node_count;

   fs_reg scratch_header;
};

/**
 * Sets the mrf_used array to indicate which MRFs are used by the shader IR
 *
 * This is used in assign_regs() to decide which of the GRFs that we use as
 * MRFs on gen7 get normally register allocated, and in register spilling to
 * see if we can actually use MRFs to do spills without overwriting normal MRF
 * contents.
 */
static void
get_used_mrfs(const fs_visitor *v, bool *mrf_used)
{
   int reg_width = v->dispatch_width / 8;

   memset(mrf_used, 0, BRW_MAX_MRF(v->devinfo->gen) * sizeof(bool));

   foreach_block_and_inst(block, fs_inst, inst, v->cfg) {
      if (inst->dst.file == MRF) {
         int reg = inst->dst.nr & ~BRW_MRF_COMPR4;
         mrf_used[reg] = true;
         if (reg_width == 2) {
            if (inst->dst.nr & BRW_MRF_COMPR4) {
               mrf_used[reg + 4] = true;
            } else {
               mrf_used[reg + 1] = true;
            }
         }
      }

      if (inst->mlen > 0) {
	 for (unsigned i = 0; i < inst->implied_mrf_writes(); i++) {
            mrf_used[inst->base_mrf + i] = true;
         }
      }
   }
}

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
   spill_max_size(const backend_shader *s)
   {
      /* FINISHME - On Gen7+ it should be possible to avoid this limit
       *            altogether by spilling directly from the temporary GRF
       *            allocated to hold the result of the instruction (and the
       *            scratch write header).
       */
      /* FINISHME - The shader's dispatch width probably belongs in
       *            backend_shader (or some nonexistent fs_shader class?)
       *            rather than in the visitor class.
       */
      return static_cast<const fs_visitor *>(s)->dispatch_width / 8;
   }

   /**
    * First MRF register available for spilling.
    */
   unsigned
   spill_base_mrf(const backend_shader *s)
   {
      /* We don't use the MRF hack on Gen9+ */
      assert(s->devinfo->gen < 9);
      return BRW_MAX_MRF(s->devinfo->gen) - spill_max_size(s) - 1;
   }
}

void
fs_reg_alloc::setup_live_interference(unsigned node,
                                      int node_start_ip, int node_end_ip)
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
      if (node_start_ip <= payload_last_use_ip[i])
         ra_add_node_interference(g, node, first_payload_node + i);
   }

   /* If we have the MRF hack enabled, mark this node as interfering with all
    * MRF registers.
    */
   if (first_mrf_hack_node >= 0) {
      for (int i = spill_base_mrf(fs); i < BRW_MAX_MRF(devinfo->gen); i++)
         ra_add_node_interference(g, node, first_mrf_hack_node + i);
   }

   /* Everything interferes with the scratch header */
   if (scratch_header_node >= 0)
      ra_add_node_interference(g, node, scratch_header_node);

   /* Add interference with every vgrf whose live range intersects this
    * node's.  We only need to look at nodes below this one as the reflexivity
    * of interference will take care of the rest.
    */
   for (unsigned n2 = first_vgrf_node;
        n2 <= (unsigned)last_vgrf_node && n2 < node; n2++) {
      unsigned vgrf = n2 - first_vgrf_node;
      if (!(node_end_ip <= live.vgrf_start[vgrf] ||
            live.vgrf_end[vgrf] <= node_start_ip))
         ra_add_node_interference(g, node, n2);
   }
}

void
fs_reg_alloc::setup_inst_interference(const fs_inst *inst)
{
   /* Certain instructions can't safely use the same register for their
    * sources and destination.  Add interference.
    */
   if (inst->dst.file == VGRF && inst->has_source_and_destination_hazard()) {
      for (unsigned i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == VGRF) {
            ra_add_node_interference(g, first_vgrf_node + inst->dst.nr,
                                        first_vgrf_node + inst->src[i].nr);
         }
      }
   }

   /* In 16-wide instructions we have an issue where a compressed
    * instruction is actually two instructions executed simultaneously.
    * It's actually ok to have the source and destination registers be
    * the same.  In this case, each instruction over-writes its own
    * source and there's no problem.  The real problem here is if the
    * source and destination registers are off by one.  Then you can end
    * up in a scenario where the first instruction over-writes the
    * source of the second instruction.  Since the compiler doesn't know
    * about this level of granularity, we simply make the source and
    * destination interfere.
    */
   if (inst->exec_size >= 16 && inst->dst.file == VGRF) {
      for (int i = 0; i < inst->sources; ++i) {
         if (inst->src[i].file == VGRF) {
            ra_add_node_interference(g, first_vgrf_node + inst->dst.nr,
                                        first_vgrf_node + inst->src[i].nr);
         }
      }
   }

   if (grf127_send_hack_node >= 0) {
      /* At Intel Broadwell PRM, vol 07, section "Instruction Set Reference",
       * subsection "EUISA Instructions", Send Message (page 990):
       *
       * "r127 must not be used for return address when there is a src and
       * dest overlap in send instruction."
       *
       * We are avoiding using grf127 as part of the destination of send
       * messages adding a node interference to the grf127_send_hack_node.
       * This node has a fixed asignment to grf127.
       *
       * We don't apply it to SIMD16 instructions because previous code avoids
       * any register overlap between sources and destination.
       */
      if (inst->exec_size < 16 && inst->is_send_from_grf() &&
          inst->dst.file == VGRF)
         ra_add_node_interference(g, first_vgrf_node + inst->dst.nr,
                                     grf127_send_hack_node);

      /* Spilling instruction are genereated as SEND messages from MRF but as
       * Gen7+ supports sending from GRF the driver will maps assingn these
       * MRF registers to a GRF. Implementations reuses the dest of the send
       * message as source. So as we will have an overlap for sure, we create
       * an interference between destination and grf127.
       */
      if ((inst->opcode == SHADER_OPCODE_GEN7_SCRATCH_READ ||
           inst->opcode == SHADER_OPCODE_GEN4_SCRATCH_READ) &&
          inst->dst.file == VGRF)
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
   if (devinfo->gen >= 9) {
      if (inst->opcode == SHADER_OPCODE_SEND && inst->ex_mlen > 0 &&
          inst->src[2].file == VGRF && inst->src[3].file == VGRF &&
          inst->src[2].nr != inst->src[3].nr)
         ra_add_node_interference(g, first_vgrf_node + inst->src[2].nr,
                                     first_vgrf_node + inst->src[3].nr);
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
   if (inst->eot) {
      const int vgrf = inst->opcode == SHADER_OPCODE_SEND ?
                       inst->src[2].nr : inst->src[0].nr;
      int size = fs->alloc.sizes[vgrf];
      int reg = compiler->fs_reg_sets[rsi].class_to_ra_reg_range[size] - 1;

      if (first_mrf_hack_node >= 0) {
         /* If something happened to spill, we want to push the EOT send
          * register early enough in the register file that we don't
          * conflict with any used MRF hack registers.
          */
         reg -= BRW_MAX_MRF(devinfo->gen) - spill_base_mrf(fs);
      } else if (grf127_send_hack_node >= 0) {
         /* Avoid r127 which might be unusable if the node was previously
          * written by a SIMD8 SEND message with source/destination overlap.
          */
         reg--;
      }

      ra_set_node_reg(g, first_vgrf_node + vgrf, reg);
   }
}

void
fs_reg_alloc::build_interference_graph(bool allow_spilling)
{
   /* Compute the RA node layout */
   node_count = 0;
   first_payload_node = node_count;
   node_count += payload_node_count;
   if (devinfo->gen >= 7 && devinfo->gen < 9 && allow_spilling) {
      first_mrf_hack_node = node_count;
      node_count += BRW_MAX_GRF - GEN7_MRF_HACK_START;
   } else {
      first_mrf_hack_node = -1;
   }
   if (devinfo->gen >= 8) {
      grf127_send_hack_node = node_count;
      node_count ++;
   } else {
      grf127_send_hack_node = -1;
   }
   first_vgrf_node = node_count;
   node_count += fs->alloc.count;
   last_vgrf_node = node_count - 1;
   if (devinfo->gen >= 9 && allow_spilling) {
      scratch_header_node = node_count++;
   } else {
      scratch_header_node = -1;
   }
   first_spill_node = node_count;

   fs->calculate_payload_ranges(payload_node_count,
                                payload_last_use_ip);

   assert(g == NULL);
   g = ra_alloc_interference_graph(compiler->fs_reg_sets[rsi].regs, node_count);
   ralloc_steal(mem_ctx, g);

   /* Set up the payload nodes */
   for (int i = 0; i < payload_node_count; i++) {
      /* Mark each payload node as being allocated to its physical register.
       *
       * The alternative would be to have per-physical-register classes, which
       * would just be silly.
       */
      if (devinfo->gen <= 5 && fs->dispatch_width >= 16) {
         /* We have to divide by 2 here because we only have even numbered
          * registers.  Some of the payload registers will be odd, but
          * that's ok because their physical register numbers have already
          * been assigned.  The only thing this is used for is interference.
          */
         ra_set_node_reg(g, first_payload_node + i, i / 2);
      } else {
         ra_set_node_reg(g, first_payload_node + i, i);
      }
   }

   if (first_mrf_hack_node >= 0) {
      /* Mark each MRF reg node as being allocated to its physical
       * register.
       *
       * The alternative would be to have per-physical-register classes,
       * which would just be silly.
       */
      for (int i = 0; i < BRW_MAX_MRF(devinfo->gen); i++) {
         ra_set_node_reg(g, first_mrf_hack_node + i,
                            GEN7_MRF_HACK_START + i);
      }
   }

   if (grf127_send_hack_node >= 0)
      ra_set_node_reg(g, grf127_send_hack_node, 127);

   /* Specify the classes of each virtual register. */
   for (unsigned i = 0; i < fs->alloc.count; i++) {
      unsigned size = fs->alloc.sizes[i];

      assert(size <= ARRAY_SIZE(compiler->fs_reg_sets[rsi].classes) &&
             "Register allocation relies on split_virtual_grfs()");

      ra_set_node_class(g, first_vgrf_node + i,
                        compiler->fs_reg_sets[rsi].classes[size - 1]);
   }

   /* Special case: on pre-Gen7 hardware that supports PLN, the second operand
    * of a PLN instruction needs to be an even-numbered register, so we have a
    * special register class aligned_bary_class to handle this case.
    */
   if (compiler->fs_reg_sets[rsi].aligned_bary_class >= 0) {
      foreach_block_and_inst(block, fs_inst, inst, fs->cfg) {
         if (inst->opcode == FS_OPCODE_LINTERP && inst->src[0].file == VGRF &&
             fs->alloc.sizes[inst->src[0].nr] ==
               aligned_bary_size(fs->dispatch_width)) {
            ra_set_node_class(g, first_vgrf_node + inst->src[0].nr,
                              compiler->fs_reg_sets[rsi].aligned_bary_class);
         }
      }
   }

   /* Add interference based on the live range of the register */
   for (unsigned i = 0; i < fs->alloc.count; i++) {
      setup_live_interference(first_vgrf_node + i,
                              live.vgrf_start[i],
                              live.vgrf_end[i]);
   }

   /* Add interference based on the instructions in which a register is used.
    */
   foreach_block_and_inst(block, fs_inst, inst, fs->cfg)
      setup_inst_interference(inst);
}

void
fs_reg_alloc::discard_interference_graph()
{
   ralloc_free(g);
   g = NULL;
   have_spill_costs = false;
}

void
fs_reg_alloc::emit_unspill(const fs_builder &bld, fs_reg dst,
                           uint32_t spill_offset, unsigned count)
{
   const gen_device_info *devinfo = bld.shader->devinfo;
   const unsigned reg_size = dst.component_size(bld.dispatch_width()) /
                             REG_SIZE;
   assert(count % reg_size == 0);

   for (unsigned i = 0; i < count / reg_size; i++) {
      fs_inst *unspill_inst;
      if (devinfo->gen >= 9) {
         fs_reg header = this->scratch_header;
         fs_builder ubld = bld.exec_all().group(1, 0);
         assert(spill_offset % 16 == 0);
         unspill_inst = ubld.MOV(component(header, 2),
                                 brw_imm_ud(spill_offset / 16));
         _mesa_set_add(spill_insts, unspill_inst);

         fs_reg srcs[] = { brw_imm_ud(0), brw_imm_ud(0), header };
         unspill_inst = bld.emit(SHADER_OPCODE_SEND, dst,
                                 srcs, ARRAY_SIZE(srcs));
         unspill_inst->mlen = 1;
         unspill_inst->header_size = 1;
         unspill_inst->size_written = reg_size * REG_SIZE;
         unspill_inst->send_has_side_effects = false;
         unspill_inst->send_is_volatile = true;
         unspill_inst->sfid = GEN7_SFID_DATAPORT_DATA_CACHE;
         unspill_inst->desc =
            brw_dp_read_desc(devinfo, GEN8_BTI_STATELESS_NON_COHERENT,
                             BRW_DATAPORT_OWORD_BLOCK_DWORDS(reg_size * 8),
                             BRW_DATAPORT_READ_MESSAGE_OWORD_BLOCK_READ,
                             BRW_DATAPORT_READ_TARGET_RENDER_CACHE);
      } else if (devinfo->gen >= 7 && spill_offset < (1 << 12) * REG_SIZE) {
         /* The Gen7 descriptor-based offset is 12 bits of HWORD units.
          * Because the Gen7-style scratch block read is hardwired to BTI 255,
          * on Gen9+ it would cause the DC to do an IA-coherent read, what
          * largely outweighs the slight advantage from not having to provide
          * the address as part of the message header, so we're better off
          * using plain old oword block reads.
          */
         unspill_inst = bld.emit(SHADER_OPCODE_GEN7_SCRATCH_READ, dst);
         unspill_inst->offset = spill_offset;
      } else {
         unspill_inst = bld.emit(SHADER_OPCODE_GEN4_SCRATCH_READ, dst);
         unspill_inst->offset = spill_offset;
         unspill_inst->base_mrf = spill_base_mrf(bld.shader);
         unspill_inst->mlen = 1; /* header contains offset */
      }
      _mesa_set_add(spill_insts, unspill_inst);

      dst.offset += reg_size * REG_SIZE;
      spill_offset += reg_size * REG_SIZE;
   }
}

void
fs_reg_alloc::emit_spill(const fs_builder &bld, fs_reg src,
                         uint32_t spill_offset, unsigned count)
{
   const gen_device_info *devinfo = bld.shader->devinfo;
   const unsigned reg_size = src.component_size(bld.dispatch_width()) /
                             REG_SIZE;
   assert(count % reg_size == 0);

   for (unsigned i = 0; i < count / reg_size; i++) {
      fs_inst *spill_inst;
      if (devinfo->gen >= 9) {
         fs_reg header = this->scratch_header;
         fs_builder ubld = bld.exec_all().group(1, 0);
         assert(spill_offset % 16 == 0);
         spill_inst = ubld.MOV(component(header, 2),
                               brw_imm_ud(spill_offset / 16));
         _mesa_set_add(spill_insts, spill_inst);

         fs_reg srcs[] = { brw_imm_ud(0), brw_imm_ud(0), header, src };
         spill_inst = bld.emit(SHADER_OPCODE_SEND, bld.null_reg_f(),
                               srcs, ARRAY_SIZE(srcs));
         spill_inst->mlen = 1;
         spill_inst->ex_mlen = reg_size;
         spill_inst->size_written = 0;
         spill_inst->header_size = 1;
         spill_inst->send_has_side_effects = true;
         spill_inst->send_is_volatile = false;
         spill_inst->sfid = GEN7_SFID_DATAPORT_DATA_CACHE;
         spill_inst->desc =
            brw_dp_write_desc(devinfo, GEN8_BTI_STATELESS_NON_COHERENT,
                              BRW_DATAPORT_OWORD_BLOCK_DWORDS(reg_size * 8),
                              GEN6_DATAPORT_WRITE_MESSAGE_OWORD_BLOCK_WRITE,
                              0 /* not a render target */,
                              false /* send_commit_msg */);
      } else {
         spill_inst = bld.emit(SHADER_OPCODE_GEN4_SCRATCH_WRITE,
                               bld.null_reg_f(), src);
         spill_inst->offset = spill_offset;
         spill_inst->mlen = 1 + reg_size; /* header, value */
         spill_inst->base_mrf = spill_base_mrf(bld.shader);
      }
      _mesa_set_add(spill_insts, spill_inst);

      src.offset += reg_size * REG_SIZE;
      spill_offset += reg_size * REG_SIZE;
   }
}

void
fs_reg_alloc::set_spill_costs()
{
   float block_scale = 1.0;
   float spill_costs[fs->alloc.count];
   bool no_spill[fs->alloc.count];

   for (unsigned i = 0; i < fs->alloc.count; i++) {
      spill_costs[i] = 0.0;
      no_spill[i] = false;
   }

   /* Calculate costs for spilling nodes.  Call it a cost of 1 per
    * spill/unspill we'll have to do, and guess that the insides of
    * loops run 10 times.
    */
   foreach_block_and_inst(block, fs_inst, inst, fs->cfg) {
      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == VGRF)
            spill_costs[inst->src[i].nr] += regs_read(inst, i) * block_scale;
      }

      if (inst->dst.file == VGRF)
         spill_costs[inst->dst.nr] += regs_written(inst) * block_scale;

      /* Don't spill anything we generated while spilling */
      if (_mesa_set_search(spill_insts, inst)) {
         for (unsigned int i = 0; i < inst->sources; i++) {
	    if (inst->src[i].file == VGRF)
               no_spill[inst->src[i].nr] = true;
         }
	 if (inst->dst.file == VGRF)
            no_spill[inst->dst.nr] = true;
      }

      switch (inst->opcode) {

      case BRW_OPCODE_DO:
	 block_scale *= 10;
	 break;

      case BRW_OPCODE_WHILE:
	 block_scale /= 10;
	 break;

      case BRW_OPCODE_IF:
      case BRW_OPCODE_IFF:
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
      if (no_spill[i])
         continue;

      int live_length = live.vgrf_end[i] - live.vgrf_start[i];
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
}

int
fs_reg_alloc::choose_spill_reg()
{
   if (!have_spill_costs)
      set_spill_costs();

   int node = ra_get_best_spill_node(g);
   if (node < 0)
      return -1;

   assert(node >= first_vgrf_node);
   return node - first_vgrf_node;
}

fs_reg
fs_reg_alloc::alloc_scratch_header()
{
   int vgrf = fs->alloc.allocate(1);
   assert(first_vgrf_node + vgrf == scratch_header_node);
   ra_set_node_class(g, scratch_header_node,
                        compiler->fs_reg_sets[rsi].classes[0]);

   setup_live_interference(scratch_header_node, 0, INT_MAX);

   return fs_reg(VGRF, vgrf, BRW_REGISTER_TYPE_UD);
}

fs_reg
fs_reg_alloc::alloc_spill_reg(unsigned size, int ip)
{
   int vgrf = fs->alloc.allocate(size);
   int n = ra_add_node(g, compiler->fs_reg_sets[rsi].classes[size - 1]);
   assert(n == first_vgrf_node + vgrf);
   assert(n == first_spill_node + spill_node_count);

   setup_live_interference(n, ip - 1, ip + 1);

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

   return fs_reg(VGRF, vgrf);
}

void
fs_reg_alloc::spill_reg(unsigned spill_reg)
{
   int size = fs->alloc.sizes[spill_reg];
   unsigned int spill_offset = fs->last_scratch;
   assert(ALIGN(spill_offset, 16) == spill_offset); /* oword read/write req. */

   /* Spills may use MRFs 13-15 in the SIMD16 case.  Our texturing is done
    * using up to 11 MRFs starting from either m1 or m2, and fb writes can use
    * up to m13 (gen6+ simd16: 2 header + 8 color + 2 src0alpha + 2 omask) or
    * m15 (gen4-5 simd16: 2 header + 8 color + 1 aads + 2 src depth + 2 dst
    * depth), starting from m1.  In summary: We may not be able to spill in
    * SIMD16 mode, because we'd stomp the FB writes.
    */
   if (!fs->spilled_any_registers) {
      if (devinfo->gen >= 9) {
         this->scratch_header = alloc_scratch_header();
         fs_builder ubld = fs->bld.exec_all().group(8, 0).at(
            fs->cfg->first_block(), fs->cfg->first_block()->start());
         fs_inst *header_inst = ubld.emit(SHADER_OPCODE_SCRATCH_HEADER,
                                          this->scratch_header);
         _mesa_set_add(spill_insts, header_inst);
      } else {
         bool mrf_used[BRW_MAX_MRF(devinfo->gen)];
         get_used_mrfs(fs, mrf_used);

         for (int i = spill_base_mrf(fs); i < BRW_MAX_MRF(devinfo->gen); i++) {
            if (mrf_used[i]) {
               fs->fail("Register spilling not supported with m%d used", i);
             return;
            }
         }
      }

      fs->spilled_any_registers = true;
   }

   fs->last_scratch += size * REG_SIZE;

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
   foreach_block_and_inst (block, fs_inst, inst, fs->cfg) {
      const fs_builder ibld = fs_builder(fs, block, inst);
      exec_node *before = inst->prev;
      exec_node *after = inst->next;

      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == VGRF &&
             inst->src[i].nr == spill_reg) {
            int count = regs_read(inst, i);
            int subset_spill_offset = spill_offset +
               ROUND_DOWN_TO(inst->src[i].offset, REG_SIZE);
            fs_reg unspill_dst = alloc_spill_reg(count, ip);

            inst->src[i].nr = unspill_dst.nr;
            inst->src[i].offset %= REG_SIZE;

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
             */
            emit_unspill(ibld.exec_all().group(width, 0), unspill_dst,
                         subset_spill_offset, count);
	 }
      }

      if (inst->dst.file == VGRF &&
          inst->dst.nr == spill_reg) {
         int subset_spill_offset = spill_offset +
            ROUND_DOWN_TO(inst->dst.offset, REG_SIZE);
         fs_reg spill_src = alloc_spill_reg(regs_written(inst), ip);

         inst->dst.nr = spill_src.nr;
         inst->dst.offset %= REG_SIZE;

         /* If we're immediately spilling the register, we should not use
          * destination dependency hints.  Doing so will cause the GPU do
          * try to read and write the register at the same time and may
          * hang the GPU.
          */
         inst->no_dd_clear = false;
         inst->no_dd_check = false;

         /* Calculate the execution width of the scratch messages (which work
          * in terms of 32 bit components so we have a fixed number of eight
          * channels per spilled register).  We attempt to write one
          * exec_size-wide component of the variable at a time without
          * exceeding the maximum number of (fake) MRF registers reserved for
          * spills.
          */
         const unsigned width = 8 * MIN2(
            DIV_ROUND_UP(inst->dst.component_size(inst->exec_size), REG_SIZE),
            spill_max_size(fs));

         /* Spills should only write data initialized by the instruction for
          * whichever channels are enabled in the excution mask.  If that's
          * not possible we'll have to emit a matching unspill before the
          * instruction and set force_writemask_all on the spill.
          */
         const bool per_channel =
            inst->dst.is_contiguous() && type_sz(inst->dst.type) == 4 &&
            inst->exec_size == width;

         /* Builder used to emit the scratch messages. */
         const fs_builder ubld = ibld.exec_all(!per_channel).group(width, 0);

	 /* If our write is going to affect just part of the
          * regs_written(inst), then we need to unspill the destination since
          * we write back out all of the regs_written().  If the original
          * instruction had force_writemask_all set and is not a partial
          * write, there should be no need for the unspill since the
          * instruction will be overwriting the whole destination in any case.
	  */
         if (inst->is_partial_write() ||
             (!inst->force_writemask_all && !per_channel))
            emit_unspill(ubld, spill_src, subset_spill_offset,
                         regs_written(inst));

         emit_spill(ubld.at(block, inst->next), spill_src,
                    subset_spill_offset, regs_written(inst));
      }

      for (fs_inst *inst = (fs_inst *)before->next;
           inst != after; inst = (fs_inst *)inst->next)
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
fs_reg_alloc::assign_regs(bool allow_spilling, bool spill_all)
{
   build_interference_graph(fs->spilled_any_registers || spill_all);

   bool spilled = false;
   while (1) {
      /* Debug of register spilling: Go spill everything. */
      if (unlikely(spill_all)) {
         int reg = choose_spill_reg();
         if (reg != -1) {
            spill_reg(reg);
            continue;
         }
      }

      if (ra_allocate(g))
         break;

      if (!allow_spilling)
         return false;

      /* Failed to allocate registers.  Spill a reg, and the caller will
       * loop back into here to try again.
       */
      int reg = choose_spill_reg();
      if (reg == -1)
         return false;

      /* If we're going to spill but we've never spilled before, we need to
       * re-build the interference graph with MRFs enabled to allow spilling.
       */
      if (!fs->spilled_any_registers) {
         discard_interference_graph();
         build_interference_graph(true);
      }

      spilled = true;

      spill_reg(reg);
   }

   if (spilled)
      fs->invalidate_analysis(DEPENDENCY_INSTRUCTIONS | DEPENDENCY_VARIABLES);

   /* Get the chosen virtual registers for each node, and map virtual
    * regs in the register classes back down to real hardware reg
    * numbers.
    */
   unsigned hw_reg_mapping[fs->alloc.count];
   fs->grf_used = fs->first_non_payload_grf;
   for (unsigned i = 0; i < fs->alloc.count; i++) {
      int reg = ra_get_node_reg(g, first_vgrf_node + i);

      hw_reg_mapping[i] = compiler->fs_reg_sets[rsi].ra_reg_to_grf[reg];
      fs->grf_used = MAX2(fs->grf_used,
			  hw_reg_mapping[i] + fs->alloc.sizes[i]);
   }

   foreach_block_and_inst(block, fs_inst, inst, fs->cfg) {
      assign_reg(hw_reg_mapping, &inst->dst);
      for (int i = 0; i < inst->sources; i++) {
         assign_reg(hw_reg_mapping, &inst->src[i]);
      }
   }

   fs->alloc.count = fs->grf_used;

   return true;
}

bool
fs_visitor::assign_regs(bool allow_spilling, bool spill_all)
{
   fs_reg_alloc alloc(this);
   bool success = alloc.assign_regs(allow_spilling, spill_all);
   if (!success && allow_spilling) {
      fail("no register to spill:\n");
      dump_instructions(NULL);
   }
   return success;
}
