/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_analysis.h"
#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_rt.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"
#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"
#include "util/bitscan.h"
#include "util/u_math.h"

void
brw_assign_urb_setup(brw_shader &s)
{
   struct brw_vue_prog_data *vue_prog_data = brw_vue_prog_data(s.prog_data);
   const unsigned verts =
      s.stage == MESA_SHADER_GEOMETRY ? s.nir->info.gs.vertices_in : 1;

   s.first_non_payload_grf += 8 * vue_prog_data->urb_read_length * verts;

   /* Rewrite all ATTR file references to HW_REGs. */
   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      s.convert_attr_sources_to_hw_regs(inst);
   }
}

void
brw_shader::emit_tes_terminate()
{
   assert(stage == MESA_SHADER_TESS_EVAL);

   /* Wa_1805992985:
    *
    * GPU hangs on one of tessellation vkcts tests with DS not done. The
    * send cycle, which is a urb write with an eot must be 4 phases long and
    * all 8 lanes must valid.
    */
   if (intel_needs_workaround(devinfo, 1805992985)) {
      assert(dispatch_width == 8);
      const brw_builder bld = brw_builder(this);
      brw_reg urb_handle = tes_payload().urb_output;
      brw_reg uniform_urb_handle = retype(brw_allocate_vgrf_units(*this, 1), BRW_TYPE_UD);
      brw_reg uniform_mask = retype(brw_allocate_vgrf_units(*this, 1), BRW_TYPE_UD);
      brw_reg payload = retype(brw_allocate_vgrf_units(*this, 4), BRW_TYPE_UD);

      /* Workaround requires all 8 channels (lanes) to be valid. This is
       * understood to mean they all need to be alive. First trick is to find
       * a live channel and copy its urb handle for all the other channels to
       * make sure all handles are valid.
       */
      bld.exec_all().MOV(uniform_urb_handle, bld.emit_uniformize(urb_handle));

      /* Second trick is to use masked URB write where one can tell the HW to
       * actually write data only for selected channels even though all are
       * active.
       * Third trick is to take advantage of the must-be-zero (MBZ) area in
       * the very beginning of the URB.
       *
       * One masks data to be written only for the first channel and uses
       * offset zero explicitly to land data to the MBZ area avoiding trashing
       * any other part of the URB.
       *
       * Since the WA says that the write needs to be 4 phases long one uses
       * 4 slots data. All are explicitly zeros in order to to keep the MBZ
       * area written as zeros.
       */
      bld.exec_all().MOV(uniform_mask, brw_imm_ud(0x1u));
      bld.exec_all().MOV(offset(payload, bld, 0), brw_imm_ud(0u));
      bld.exec_all().MOV(offset(payload, bld, 1), brw_imm_ud(0u));
      bld.exec_all().MOV(offset(payload, bld, 2), brw_imm_ud(0u));
      bld.exec_all().MOV(offset(payload, bld, 3), brw_imm_ud(0u));

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = uniform_urb_handle;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = uniform_mask;
      srcs[URB_LOGICAL_SRC_DATA] = payload;

      brw_urb_inst *urb = bld.exec_all().URB_WRITE(srcs, ARRAY_SIZE(srcs));
      urb->eot = true;
      urb->offset = 0;
      urb->components = 4;
   } else {
      ASSERTED bool eot = mark_last_urb_write_with_eot();
      assert(eot);
   }
}

void
brw_shader::emit_cs_terminate()
{
   const brw_builder ubld = brw_builder(this).exec_all();

   /* We can't directly send from g0, since sends with EOT have to use
    * g112-127. So, copy it to a virtual register, The register allocator will
    * make sure it uses the appropriate register range.
    */
   struct brw_reg g0 = retype(brw_vec8_grf(0, 0), BRW_TYPE_UD);
   brw_reg payload =
      retype(brw_allocate_vgrf_units(*this, reg_unit(devinfo)), BRW_TYPE_UD);
   ubld.group(8 * reg_unit(devinfo), 0).MOV(payload, g0);

   /* Set the descriptor to "Dereference Resource" and "Root Thread" */
   unsigned desc = 0;

   /* Set Resource Select to "Do not dereference URB" on Gfx < 11.
    *
    * Note that even though the thread has a URB resource associated with it,
    * we set the "do not dereference URB" bit, because the URB resource is
    * managed by the fixed-function unit, so it will free it automatically.
    */
   if (devinfo->ver < 11)
      desc |= (1 << 4); /* Do not dereference URB */

   brw_send_inst *send = ubld.SEND();
   send->dst = reg_undef;
   send->src[SEND_SRC_DESC]     = brw_imm_ud(desc);
   send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
   send->src[SEND_SRC_PAYLOAD1] = payload;
   send->src[SEND_SRC_PAYLOAD2] = brw_reg();

   /* On Alchemist and later, send an EOT message to the message gateway to
    * terminate a compute shader.  For older GPUs, send to the thread spawner.
    */
   send->sfid = devinfo->verx10 >= 125 ? BRW_SFID_MESSAGE_GATEWAY
                                       : BRW_SFID_THREAD_SPAWNER;
   send->mlen = reg_unit(devinfo);
   send->eot = true;
}

brw_shader::brw_shader(const brw_shader_params *params)
   : compiler(params->compiler),
     log_data(params->log_data),
     devinfo(params->compiler->devinfo),
     nir(params->nir),
     mem_ctx(params->mem_ctx),
     cfg(NULL),
     stage(params->nir->info.stage),
     debug_enabled(params->debug_enabled),
     key(params->key),
     prog_data(params->prog_data),
     live_analysis(this),
     regpressure_analysis(this),
     performance_analysis(this),
     idom_analysis(this),
     def_analysis(this),
     ip_ranges_analysis(this),
     needs_register_pressure(params->needs_register_pressure),
     dispatch_width(params->dispatch_width),
     max_polygons(params->num_polygons),
     api_subgroup_size(brw_nir_api_subgroup_size(params->nir, dispatch_width)),
     archiver(params->archiver)
{
   assert(api_subgroup_size == 0 ||
         api_subgroup_size == 8 ||
         api_subgroup_size == 16 ||
         api_subgroup_size == 32);

   this->max_dispatch_width = 32;

   this->failed = false;
   this->fail_msg = NULL;

   this->payload_ = NULL;
   this->first_non_payload_grf = 0;

   this->last_scratch = 0;

   memset(&this->shader_stats, 0, sizeof(this->shader_stats));

   this->grf_used = 0;
   this->spilled_any_registers = false;

   this->phase = BRW_SHADER_PHASE_INITIAL;

   this->next_address_register_nr = 1;

   this->alloc.capacity = 0;
   this->alloc.sizes = NULL;
   this->alloc.count = 0;

   this->gs.control_data_bits_per_vertex = 0;
   this->gs.control_data_header_size_bits = 0;

   if (params->per_primitive_offsets) {
      assert(stage == MESA_SHADER_FRAGMENT);
      memcpy(this->fs.per_primitive_offsets, params->per_primitive_offsets,
             sizeof(this->fs.per_primitive_offsets));
   }

   {
      unsigned inst_count = 0;
      if (nir_shader_get_entrypoint(nir)) {
         nir_foreach_block(block, nir_shader_get_entrypoint(nir)) {
            nir_foreach_instr(instr, block)
               inst_count++;
         }
      }

      const unsigned estimate = inst_count * (sizeof(brw_inst) + 2 * sizeof(brw_reg));

      inst_arena.mem_ctx = ralloc_context(NULL);
      inst_arena.cap = estimate;
      inst_arena.beg = (char *) ralloc_size(mem_ctx, inst_arena.cap);
      inst_arena.end = inst_arena.beg + inst_arena.cap;
      inst_arena.total_cap = inst_arena.cap;
   }
}

brw_shader::~brw_shader()
{
   delete this->payload_;
   ralloc_free(inst_arena.mem_ctx);
}

void
brw_shader::vfail(const char *format, va_list va)
{
   char *msg;

   if (failed)
      return;

   failed = true;

   msg = ralloc_vasprintf(mem_ctx, format, va);
   msg = ralloc_asprintf(mem_ctx, "SIMD%d %s compile failed: %s\n",
         dispatch_width, _mesa_shader_stage_to_abbrev(stage), msg);

   this->fail_msg = msg;

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "%s",  msg);
   }
}

void
brw_shader::fail(const char *format, ...)
{
   va_list va;

   va_start(va, format);
   vfail(format, va);
   va_end(va);
}

/**
 * Mark this program as impossible to compile with dispatch width greater
 * than n.
 *
 * During the SIMD8 compile (which happens first), we can detect and flag
 * things that are unsupported in SIMD16+ mode, so the compiler can skip the
 * SIMD16+ compile altogether.
 *
 * During a compile of dispatch width greater than n (if one happens anyway),
 * this just calls fail().
 */
void
brw_shader::limit_dispatch_width(unsigned n, const char *msg)
{
   if (dispatch_width > n) {
      fail("%s", msg);
   } else {
      max_dispatch_width = MIN2(max_dispatch_width, n);
      brw_shader_perf_log(compiler, log_data,
                          "Shader dispatch width limited to SIMD%d: %s\n",
                          n, msg);
   }
}

enum intel_barycentric_mode
brw_barycentric_mode(const struct brw_fs_prog_key *key,
                     nir_intrinsic_instr *intr)
{
   const glsl_interp_mode mode =
      (enum glsl_interp_mode) nir_intrinsic_interp_mode(intr);

   /* Barycentric modes don't make sense for flat inputs. */
   assert(mode != INTERP_MODE_FLAT);

   unsigned bary;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_at_offset:
      /* When per sample interpolation is dynamic, assume sample
       * interpolation. We'll dynamically remap things so that the FS thread
       * payload is not affected.
       */
      bary = key->persample_interp == INTEL_SOMETIMES ?
             INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE :
             INTEL_BARYCENTRIC_PERSPECTIVE_PIXEL;
      break;
   case nir_intrinsic_load_barycentric_centroid:
      bary = INTEL_BARYCENTRIC_PERSPECTIVE_CENTROID;
      break;
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
      bary = INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE;
      break;
   default:
      UNREACHABLE("invalid intrinsic");
   }

   if (mode == INTERP_MODE_NOPERSPECTIVE)
      bary += 3;

   return (enum intel_barycentric_mode) bary;
}

/**
 * Walk backwards from the end of the program looking for a URB write that
 * isn't in control flow, and mark it with EOT.
 *
 * Return true if successful or false if a separate EOT write is needed.
 */
bool
brw_shader::mark_last_urb_write_with_eot()
{
   brw_inst *inst = cfg->last_block()->end();
   if (inst && inst->opcode == SHADER_OPCODE_URB_WRITE_LOGICAL) {
      inst->eot = true;
      return true;
   }
   return false;
}

void
brw_shader::assign_curb_setup()
{
   uint32_t ranges_start[4];
   this->push_data_size = 0;
   for (uint32_t i = 0; i < 4; i++) {
      ranges_start[i] = this->push_data_size / REG_SIZE;
      assert(prog_data->push_sizes[i] == align(prog_data->push_sizes[i], REG_SIZE));
      this->push_data_size += prog_data->push_sizes[i];
   }

   /* Map the offsets in the UNIFORM file to fixed HW regs. */
   uint64_t used = 0;
   foreach_block_and_inst(block, brw_inst, inst, cfg) {
      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file != UNIFORM)
            continue;

         struct brw_reg brw_reg;
         if (inst->src[i].nr == BRW_INLINE_PARAM_REG) {
            brw_reg = cs_payload().inline_parameter;
         } else {
            assert(inst->src[i].nr < 64);
            used |= BITFIELD64_BIT(inst->src[i].nr);

            assert(inst->src[i].nr < this->push_data_size);

            brw_reg = brw_vec1_grf(payload().num_regs + inst->src[i].nr, 0);
         }

         brw_reg.abs = inst->src[i].abs;
         brw_reg.negate = inst->src[i].negate;

         /* The combination of is_scalar for load_uniform, copy prop, and
          * lower_btd_logical_send can generate a MOV from a UNIFORM with exec
          * size 2 and stride of 1.
          */
         assert(inst->src[i].stride == 0 || inst->exec_size == 2);
         inst->src[i] = byte_offset(
            retype(brw_reg, inst->src[i].type),
            inst->src[i].offset);
      }
   }

   if (prog_data->robust_ubo_ranges) {
      brw_builder ubld = brw_builder(this, 8).exec_all().at_start(cfg->first_block());
      /* At most we can write 2 GRFs (HW limit), the SIMD width matching the
       * HW generation depends on the size of the physical register.
       */
      const unsigned max_grf_writes = 2 * reg_unit(devinfo);
      assert(max_grf_writes <= 4);

      /* push_reg_mask_param is in 32-bit units */
      unsigned mask_param = prog_data->push_reg_mask_param;
      brw_reg mask = retype(brw_vec1_grf(payload().num_regs + mask_param / 8,
                                         mask_param % 8), BRW_TYPE_UB);

      /* For each 16bit lane, generate an offset in unit of 16B */
      brw_reg offset_base = ubld.vgrf(BRW_TYPE_UW, max_grf_writes);
      ubld.MOV(offset_base, brw_imm_uv(0x76543210));
      ubld.MOV(horiz_offset(offset_base, 8), brw_imm_uv(0xFEDCBA98));
      if (max_grf_writes > 2)
         ubld.group(16, 0).ADD(horiz_offset(offset_base, 16), offset_base, brw_imm_uw(16));

      u_foreach_bit(i, prog_data->robust_ubo_ranges) {
         const unsigned range_length =
            DIV_ROUND_UP(prog_data->push_sizes[i], REG_SIZE);

         const unsigned range_start = ranges_start[i];
         uint64_t want_zero = (used >> range_start) & BITFIELD64_MASK(range_length);
         if (!want_zero)
            continue;

         const unsigned grf_start = payload().num_regs + range_start;
         const unsigned grf_end = grf_start + range_length;
         const unsigned max_grf_mask = max_grf_writes * 4;
         unsigned grf = grf_start;

         do {
            unsigned mask_length = MIN2(grf_end - grf, max_grf_mask);
            unsigned simd_width_mask = 1 << util_last_bit(mask_length * 2 - 1);

            if (!(want_zero & BITFIELD64_RANGE(grf - grf_start, mask_length))) {
               grf += max_grf_mask;
               continue;
            }

            /* Prepare section of mask, at 1/4 size */
            brw_builder ubld_mask = ubld.group(simd_width_mask, 0);
            brw_reg offset_reg = ubld_mask.vgrf(BRW_TYPE_UW);
            unsigned mask_start = grf, mask_end = grf + mask_length;
            ubld_mask.ADD(offset_reg, offset_base, brw_imm_uw((mask_start - grf_start) * 2));
            /* Compare the 16B increments with the value coming from push
             * constants and store the result into a dword. This expands a
             * comparison between 2 values in 16B increments into a 32bit mask
             * where each bit covers 4bits of data in the payload.
             *
             * This expension works because of the sign extension guaranteed
             * by the HW.
             *
             * SKL PRMs, Volume 7: 3D-Media-GPGPU, Execution Data Type:
             *
             *   "The following rules explain the conversion of multiple
             *   source operand types, possibly a mix of different types, to
             *   one common execution type:
             *      - ...
             *      - Unsigned integers are converted to signed integers.
             *      - Byte (B) or Unsigned Byte (UB) values are converted to a Word
             *        or wider integer execution type.
             *      - If source operands have different integer widths, use
             *        the widest width specified to choose the signed integer
             *        execution type."
             */
            brw_reg mask_reg = ubld_mask.vgrf(BRW_TYPE_UD);
            ubld_mask.CMP(mask_reg, byte_offset(mask, i), offset_reg, BRW_CONDITIONAL_G);

            for (unsigned and_length; grf < mask_end; grf += and_length) {
               /* We can't have a destination/source register spanning more
                * than 2 GRFs which would be the case on Xe2+ if we try to
                * mask a payload register not aligned to 64B with a SIMD32
                * instruction. In such case just reduce the SIMDness for the
                * unaligned masking.
                */
               unsigned max_write = grf % reg_unit(devinfo) != 0 ? max_grf_writes / 2 : max_grf_writes;

               and_length = 1u << (util_last_bit(MIN2(grf_end - grf, max_write)) - 1);

               if (!(want_zero & BITFIELD64_RANGE(grf - grf_start, and_length)))
                  continue;

               brw_reg push_reg = retype(brw_vec8_grf(grf, 0), BRW_TYPE_D);

               /* Expand the masking bits one more time (1bit -> 4bit because
                * UB -> UD) so that now each 8bits of mask cover 32bits of
                * data to mask, while doing the masking in the payload data.
                */
               ubld.group(and_length * 8, 0).AND(
                  push_reg,
                  byte_offset(retype(mask_reg, BRW_TYPE_B),
                              (grf - mask_start) * 8),
                  push_reg);
            }
         } while (grf < grf_end);
      }

      invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS | BRW_DEPENDENCY_VARIABLES);
   }

   /* This may be updated in assign_urb_setup or assign_vs_urb_setup. */
   this->first_non_payload_grf = payload().num_regs +
                                 DIV_ROUND_UP(align(this->push_data_size,
                                                    REG_SIZE * reg_unit(devinfo)),
                                              REG_SIZE);

   this->debug_optimizer(this->nir, "assign_curb_setup", 90, 0);
}

/*
 * Build up an array of indices into the urb_setup array that
 * references the active entries of the urb_setup array.
 * Used to accelerate walking the active entries of the urb_setup array
 * on each upload.
 */
void
brw_compute_urb_setup_index(struct brw_fs_prog_data *fs_prog_data)
{
   /* TODO(mesh): Review usage of this in the context of Mesh, we may want to
    * skip per-primitive attributes here.
    */

   /* Make sure uint8_t is sufficient */
   STATIC_ASSERT(VARYING_SLOT_MAX <= 0xff);
   uint8_t index = 0;
   for (uint8_t attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (fs_prog_data->urb_setup[attr] >= 0) {
         fs_prog_data->urb_setup_attribs[index++] = attr;
      }
   }
   fs_prog_data->urb_setup_attribs_count = index;
}

void
brw_shader::convert_attr_sources_to_hw_regs(brw_inst *inst)
{
   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].file == ATTR) {
         assert(inst->src[i].nr == 0);
         int grf = payload().num_regs +
                   DIV_ROUND_UP(
                      align(this->push_data_size, REG_SIZE * reg_unit(devinfo)),
                      REG_SIZE) +
                   inst->src[i].offset / REG_SIZE;

         /* As explained at brw_lower_vgrf_to_fixed_grf, From the Haswell PRM:
          *
          * VertStride must be used to cross GRF register boundaries. This
          * rule implies that elements within a 'Width' cannot cross GRF
          * boundaries.
          *
          * So, for registers that are large enough, we have to split the exec
          * size in two and trust the compression state to sort it out.
          */
         unsigned total_size = inst->exec_size *
                               inst->src[i].stride *
                               brw_type_size_bytes(inst->src[i].type);

         assert(total_size <= 2 * REG_SIZE);
         const unsigned exec_size =
            (total_size <= REG_SIZE) ? inst->exec_size : inst->exec_size / 2;

         unsigned width = inst->src[i].stride == 0 ? 1 : exec_size;
         struct brw_reg reg =
            stride(byte_offset(retype(brw_vec8_grf(grf, 0), inst->src[i].type),
                               inst->src[i].offset % REG_SIZE),
                   exec_size * inst->src[i].stride,
                   width, inst->src[i].stride);
         reg.abs = inst->src[i].abs;
         reg.negate = inst->src[i].negate;

         inst->src[i] = reg;
      }
   }
}

uint32_t
brw_fb_write_msg_control(const brw_inst *inst,
                         const struct brw_fs_prog_data *prog_data)
{
   uint32_t mctl;

   if (prog_data->dual_src_blend) {
      assert(inst->exec_size < 32);

      if (inst->group % 16 == 0)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN01;
      else if (inst->group % 16 == 8)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN23;
      else
         UNREACHABLE("Invalid dual-source FB write instruction group");
   } else {
      assert(inst->group == 0 || (inst->group == 16 && inst->exec_size == 16));

      if (inst->exec_size == 16)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE;
      else if (inst->exec_size == 8)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_SINGLE_SOURCE_SUBSPAN01;
      else if (inst->exec_size == 32)
         mctl = XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD32_SINGLE_SOURCE;
      else
         UNREACHABLE("Invalid FB write execution size");
   }

   return mctl;
}

void
brw_shader::invalidate_analysis(brw_analysis_dependency_class c)
{
   live_analysis.invalidate(c);
   regpressure_analysis.invalidate(c);
   performance_analysis.invalidate(c);
   idom_analysis.invalidate(c);
   def_analysis.invalidate(c);
   ip_ranges_analysis.invalidate(c);
}

void
brw_shader::debug_optimizer(const nir_shader *nir,
                            const char *pass_name,
                            int iteration, int pass_num) const
{
   if (!archiver)
      return;

   /* TODO: Add replacement for INTEL_SHADER_OPTIMIZER_PATH. */
   const char *filename =
      ralloc_asprintf(mem_ctx, "BRW%d/%02d-%02d-%s",
                      dispatch_width,
                      iteration, pass_num, pass_name);

   FILE *f = debug_archiver_start_file(archiver, filename);
   brw_print_instructions(*this, f);
   debug_archiver_finish_file(archiver);
}

static uint32_t
brw_compute_max_register_pressure(brw_shader &s)
{
   const brw_register_pressure &rp = s.regpressure_analysis.require();
   uint32_t ip = 0, max_pressure = 0;
   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      max_pressure = MAX2(max_pressure, rp.regs_live_at_ip[ip]);
      ip++;
   }
   return max_pressure;
}

static brw_inst **
save_instruction_order(const struct cfg_t *cfg)
{
   /* Before we schedule anything, stash off the instruction order as an array
    * of brw_inst *.  This way, we can reset it between scheduling passes to
    * prevent dependencies between the different scheduling modes.
    */
   int num_insts = cfg->total_instructions;
   brw_inst **inst_arr = new brw_inst * [num_insts];

   int ip = 0;
   foreach_block_and_inst(block, brw_inst, inst, cfg) {
      inst_arr[ip++] = inst;
   }
   assert(ip == num_insts);

   return inst_arr;
}

static void
restore_instruction_order(brw_shader &s, brw_inst **inst_arr)
{
   ASSERTED int num_insts = s.cfg->total_instructions;

   int ip = 0;
   foreach_block (block, s.cfg) {
      block->instructions.make_empty();

      for (unsigned i = 0; i < block->num_instructions; i++)
         block->instructions.push_tail(inst_arr[ip++]);
   }
   assert(ip == num_insts);

   s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                         BRW_DEPENDENCY_VARIABLES);
}

/* Per-thread scratch space is a power-of-two multiple of 1KB. */
static inline unsigned
brw_get_scratch_size(int size)
{
   return MAX2(1024, util_next_power_of_two(size));
}

void
brw_allocate_registers(brw_shader &s, bool allow_spilling)
{
   const struct intel_device_info *devinfo = s.devinfo;
   const nir_shader *nir = s.nir;
   bool allocated;

   static const enum brw_instruction_scheduler_mode pre_modes[] = {
      BRW_SCHEDULE_PRE_LATENCY,
      BRW_SCHEDULE_PRE,
      BRW_SCHEDULE_PRE_NON_LIFO,
      BRW_SCHEDULE_NONE,
      BRW_SCHEDULE_PRE_LIFO,
   };

   static const char *scheduler_mode_name[] = {
      [BRW_SCHEDULE_PRE_LATENCY] = "latency-sensitive",
      [BRW_SCHEDULE_PRE] = "top-down",
      [BRW_SCHEDULE_PRE_NON_LIFO] = "non-lifo",
      [BRW_SCHEDULE_PRE_LIFO] = "lifo",
      [BRW_SCHEDULE_POST] = "post",
      [BRW_SCHEDULE_NONE] = "none",
   };

   uint32_t best_register_pressure = UINT32_MAX;
   float best_perf = -INFINITY;
   unsigned best_press_idx = 0;
   unsigned best_perf_idx = 0;

   brw_opt_compact_virtual_grfs(s);

   if (s.needs_register_pressure)
      s.shader_stats.max_register_pressure = brw_compute_max_register_pressure(s);

   s.debug_optimizer(nir, "pre_register_allocate", 90, 90);

   bool spill_all = allow_spilling && INTEL_DEBUG(DEBUG_SPILL_FS) &&
      !s.nir->info.internal;

   /* Before we schedule anything, stash off the instruction order as an array
    * of brw_inst *.  This way, we can reset it between scheduling passes to
    * prevent dependencies between the different scheduling modes.
    */
   brw_inst **orig_order = save_instruction_order(s.cfg);
   brw_inst **orders[ARRAY_SIZE(pre_modes)] = {};

   void *scheduler_ctx = ralloc_context(NULL);
   brw_instruction_scheduler *sched = brw_prepare_scheduler(s, scheduler_ctx);

   /* Try each scheduling heuristic to choose the one one with the
    * best trade-off between latency and register pressure, which on
    * xe3+ is dependent on the thread parallelism that can be achieved
    * at the GRF register requirement of each ordering of the program
    * (note that the register requirement of the program can only be
    * estimated at this point prior to register allocation).
    */
   for (unsigned i = 0; i < ARRAY_SIZE(pre_modes); i++) {
      enum brw_instruction_scheduler_mode sched_mode = pre_modes[i];

      /* Only use the PRE heuristic on pre-xe3 platforms during the
       * first pass, since the trade-off between EU thread count and
       * GRF use isn't a concern on platforms that don't support VRT.
       */
      if (devinfo->ver < 30 && sched_mode != BRW_SCHEDULE_PRE)
         continue;

      /* These don't appear to provide much benefit on xe3+.
       */
      if (devinfo->ver >= 30 && (sched_mode == BRW_SCHEDULE_PRE_LIFO ||
                                 sched_mode == BRW_SCHEDULE_NONE))
         continue;

      brw_schedule_instructions_pre_ra(s, sched, sched_mode);
      s.shader_stats.scheduler_mode = scheduler_mode_name[sched_mode];
      s.debug_optimizer(nir, s.shader_stats.scheduler_mode, 95, i);
      orders[i] = save_instruction_order(s.cfg);

      const unsigned press = brw_compute_max_register_pressure(s);
      if (press < best_register_pressure) {
         best_register_pressure = press;
         best_press_idx = i;
      }

      const brw_performance &perf = s.performance_analysis.require();
      if (perf.throughput > best_perf) {
         best_perf = perf.throughput;
         best_perf_idx = i;
      }

      if (i + 1 < ARRAY_SIZE(pre_modes)) {
         /* Reset back to the original order before trying the next mode */
         restore_instruction_order(s, orig_order);
      }
   }

   restore_instruction_order(s, orders[best_perf_idx]);
   s.shader_stats.scheduler_mode = scheduler_mode_name[pre_modes[best_perf_idx]];
   allocated = brw_assign_regs(s, false, spill_all);

   if (!allocated) {
      /* Try each scheduling heuristic to see if it can successfully register
       * allocate without spilling.  They should be ordered by decreasing
       * performance but increasing likelihood of allocating.
       */
      for (unsigned i = 0; i < ARRAY_SIZE(pre_modes); i++) {
         enum brw_instruction_scheduler_mode sched_mode = pre_modes[i];

         /* The latency-sensitive heuristic is unlikely to be helpful
          * if we failed to register-allocate.
          */
         if (sched_mode == BRW_SCHEDULE_PRE_LATENCY)
            continue;

         /* Already tried to register-allocate this. */
         if (i == best_perf_idx)
            continue;

         if (orders[i]) {
            /* We already scheduled the program with this mode. */
            restore_instruction_order(s, orders[i]);
         } else {
            restore_instruction_order(s, orig_order);
            brw_schedule_instructions_pre_ra(s, sched, sched_mode);
            s.shader_stats.scheduler_mode = scheduler_mode_name[sched_mode];
            s.debug_optimizer(nir, s.shader_stats.scheduler_mode, 95, i);
            orders[i] = save_instruction_order(s.cfg);

            const unsigned press = brw_compute_max_register_pressure(s);
            if (press < best_register_pressure) {
               best_register_pressure = press;
               best_press_idx = i;
            }
         }

         s.shader_stats.scheduler_mode = scheduler_mode_name[sched_mode];

         /* We should only spill registers on the last scheduling. */
         assert(!s.spilled_any_registers);

         allocated = brw_assign_regs(s, false, spill_all);
         if (allocated)
            break;
      }
   }

   ralloc_free(scheduler_ctx);

#define OPT(pass, ...) ({                                               \
      pass_num++;                                                       \
      bool this_progress = pass(s, ##__VA_ARGS__);                      \
                                                                        \
      if (this_progress)                                                \
         s.debug_optimizer(nir, #pass, iteration, pass_num);            \
                                                                        \
      this_progress;                                                    \
   })

   int pass_num = 0;
   int iteration = 95;

   if (!allocated) {
      if (0) {
         fprintf(stderr, "Spilling - using lowest-pressure mode \"%s\"\n",
                 scheduler_mode_name[pre_modes[best_press_idx]]);
      }
      restore_instruction_order(s, orders[best_press_idx]);
      s.shader_stats.scheduler_mode = scheduler_mode_name[pre_modes[best_press_idx]];

      if (OPT(brw_opt_cmod_propagation))
         OPT(brw_opt_dead_code_eliminate);

      if (OPT(brw_opt_cmp_flag_destination,
               /* We want something like
                * brw_wm_prog_data(s.prog_data)->uses_kill, but there are
                * other things that can cause the sample_mask_flag_subreg to
                * be used.
                */
              s.stage == MESA_SHADER_FRAGMENT)) {
         OPT(brw_opt_cmod_propagation);
         OPT(brw_opt_dead_code_eliminate);
      }

      allocated = brw_assign_regs(s, allow_spilling, spill_all);
   }

   delete[] orig_order;
   for (unsigned i = 0; i < ARRAY_SIZE(orders); i++)
      delete[] orders[i];

   if (!allocated) {
      s.fail("Failure to register allocate.  Reduce number of "
           "live scalar values to avoid this.");
   } else if (s.spilled_any_registers) {
      brw_shader_perf_log(s.compiler, s.log_data,
                          "%s shader triggered register spilling.  "
                          "Try reducing the number of live scalar "
                          "values to improve performance.\n",
                          _mesa_shader_stage_to_string(s.stage));
   }

   if (s.failed)
      return;

#define OPT_V(pass, ...) do {                                           \
      pass_num++;                                                       \
      pass(s, ##__VA_ARGS__);                                           \
      s.debug_optimizer(nir, #pass, iteration, pass_num);               \
   } while (false)

   pass_num = 0;
   iteration++;

   s.debug_optimizer(nir, "post_ra_alloc", iteration, pass_num);

   if (s.spilled_any_registers) {
      if (!INTEL_DEBUG(DEBUG_NO_FILL_OPT))
         OPT(brw_opt_fill_and_spill);

      OPT(brw_lower_fill_and_spill);
   }

   OPT(brw_opt_bank_conflicts);
   OPT_V(brw_schedule_instructions_post_ra);

   /* Lowering VGRF to FIXED_GRF is currently done as a separate pass instead
    * of part of assign_regs since both bank conflicts optimization and post
    * RA scheduling take advantage of distinguishing references to registers
    * that were allocated from references that were already fixed.
    *
    * TODO: Change the passes above, then move this lowering to be part of
    * assign_regs.
    */
   OPT_V(brw_lower_vgrfs_to_fixed_grfs);

   /* brw_opt_dead_code_eliminate cannot be run after
    * brw_lower_vgrfs_to_fixed_grfs as it depends on VGRFs. cmod propagation
    * mostly cleans up after itself. The only thing DCE could do would be to
    * eliminate writes to registers that are unread. Since register allocation
    * and final scheduling has already happend, this won't help.
    */
   OPT(brw_opt_cmod_propagation);

   if (OPT(brw_opt_cmp_flag_destination,
           /* We want something like brw_wm_prog_data(s.prog_data)->uses_kill,
            * but there are other things that can cause the
            * sample_mask_flag_subreg to be used.
            */
           s.stage == MESA_SHADER_FRAGMENT)) {
      OPT(brw_opt_cmod_propagation);
   }

   if (s.devinfo->ver >= 30)
      OPT(brw_lower_send_gather);

   brw_shader_phase_update(s, BRW_SHADER_PHASE_AFTER_REGALLOC);

   if (s.last_scratch > 0) {
      /* We currently only support up to 2MB of scratch space.  If we
       * need to support more eventually, the documentation suggests
       * that we could allocate a larger buffer, and partition it out
       * ourselves.  We'd just have to undo the hardware's address
       * calculation by subtracting (FFTID * Per Thread Scratch Space)
       * and then add FFTID * (Larger Per Thread Scratch Space).
       *
       * See 3D-Media-GPGPU Engine > Media GPGPU Pipeline >
       * Thread Group Tracking > Local Memory/Scratch Space.
       */
      if (s.last_scratch <= devinfo->max_scratch_size_per_thread) {
         /* Take the max of any previously compiled variant of the shader. In the
          * case of bindless shaders with return parts, this will also take the
          * max of all parts.
          */
         s.prog_data->total_scratch = MAX2(brw_get_scratch_size(s.last_scratch),
                                           s.prog_data->total_scratch);
      } else {
         s.fail("Scratch space required is larger than supported");
      }
   }

   if (s.failed)
      return;

   brw_workaround_emit_dummy_mov_mulmac(s);

   OPT(brw_lower_scoreboard);
}

#ifndef NDEBUG
void
brw_pass_tracker_archive(brw_pass_tracker *pt, const char *pass_name)
{
   if (!pt->archiver)
      return;

   const char *filename =
      ralloc_asprintf(pt->archiver, "NIR%d/%03d-%s",
                      pt->dispatch_width, pt->pass_num, pass_name);

   FILE *f = debug_archiver_start_file(pt->archiver, filename);
   nir_print_shader(pt->nir, f);
   debug_archiver_finish_file(pt->archiver);
}
#endif

unsigned
brw_cs_push_const_total_size(const struct brw_cs_prog_data *cs_prog_data,
                             unsigned threads)
{
   assert(cs_prog_data->push.per_thread.size % REG_SIZE == 0);
   assert(cs_prog_data->push.cross_thread.size % REG_SIZE == 0);
   return cs_prog_data->push.per_thread.size * threads +
          cs_prog_data->push.cross_thread.size;
}

struct intel_cs_dispatch_info
brw_cs_get_dispatch_info(const struct intel_device_info *devinfo,
                         const struct brw_cs_prog_data *prog_data,
                         const unsigned *override_local_size)
{
   struct intel_cs_dispatch_info info = {};

   const unsigned *sizes =
      override_local_size ? override_local_size :
                            prog_data->local_size;

   int simd = -1;
   if (intel_use_jay(devinfo, MESA_SHADER_COMPUTE)) {
      /* Currently Jay compiles only a single binary, just select that. In the
       * future this needs to get smarter.
       */
      assert(util_is_power_of_two_nonzero(prog_data->prog_mask));
      simd = util_logbase2(prog_data->prog_mask);
   } else {
      simd = brw_simd_select_for_workgroup_size(devinfo, prog_data, sizes);
   }

   assert(simd >= 0 && simd < 3);

   info.group_size = sizes[0] * sizes[1] * sizes[2];
   info.simd_size = 8u << simd;
   info.threads = DIV_ROUND_UP(info.group_size, info.simd_size);

   const uint32_t remainder = info.group_size & (info.simd_size - 1);
   if (remainder > 0)
      info.right_mask = ~0u >> (32 - remainder);
   else
      info.right_mask = ~0u >> (32 - info.simd_size);

   return info;
}

void
brw_shader_phase_update(brw_shader &s, enum brw_shader_phase phase)
{
   assert(phase == s.phase + 1);
   s.phase = phase;
   brw_validate(s);
}

bool brw_should_print_shader(const nir_shader *shader, uint64_t debug_flag, uint32_t source_hash)
{
   if (intel_shader_dump_filter && intel_shader_dump_filter != source_hash) {
      return false;
   }

   return INTEL_DEBUG(debug_flag) && (!shader->info.internal || NIR_DEBUG(PRINT_INTERNAL));
}

static unsigned
brw_allocate_vgrf_number(brw_shader &s, unsigned size_in_REGSIZE_units)
{
   assert(size_in_REGSIZE_units > 0);

   if (s.alloc.capacity <= s.alloc.count) {
      unsigned new_cap = MAX2(16, s.alloc.capacity * 2);
      s.alloc.sizes = rerzalloc(s.mem_ctx, s.alloc.sizes, unsigned,
                                s.alloc.capacity, new_cap);
      s.alloc.capacity = new_cap;
   }

   s.alloc.sizes[s.alloc.count] = size_in_REGSIZE_units;

   return s.alloc.count++;
}

brw_reg
brw_allocate_vgrf(brw_shader &s, brw_reg_type type, unsigned count)
{
   const unsigned unit = reg_unit(s.devinfo);
   const unsigned size = DIV_ROUND_UP(count * brw_type_size_bytes(type),
                                      unit * REG_SIZE) * unit;
   return retype(brw_allocate_vgrf_units(s, size), type);
}

brw_reg
brw_allocate_vgrf_units(brw_shader &s, unsigned units_of_REGSIZE)
{
   return brw_vgrf(brw_allocate_vgrf_number(s, units_of_REGSIZE), BRW_TYPE_UD);
}
