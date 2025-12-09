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
#include "util/u_math.h"

void
brw_shader::emit_urb_writes(const brw_reg &gs_vertex_count)
{
   int slot, urb_offset, length;
   int starting_urb_offset = 0;
   const struct brw_vue_prog_data *vue_prog_data =
      brw_vue_prog_data(this->prog_data);
   const struct intel_vue_map *vue_map = &vue_prog_data->vue_map;
   bool flush;
   brw_reg sources[8];
   brw_reg urb_handle;

   switch (stage) {
   case MESA_SHADER_VERTEX:
      urb_handle = vs_payload().urb_handles;
      break;
   case MESA_SHADER_TESS_EVAL:
      urb_handle = tes_payload().urb_output;
      break;
   case MESA_SHADER_GEOMETRY:
      urb_handle = gs_payload().urb_handles;
      break;
   default:
      UNREACHABLE("invalid stage");
   }

   const brw_builder bld = brw_builder(this);

   brw_reg per_slot_offsets;

   if (stage == MESA_SHADER_GEOMETRY) {
      const struct brw_gs_prog_data *gs_prog_data =
         brw_gs_prog_data(this->prog_data);

      /* We need to increment the Global Offset to skip over the control data
       * header and the extra "Vertex Count" field (1 HWord) at the beginning
       * of the VUE.  We're counting in OWords, so the units are doubled.
       */
      starting_urb_offset = 2 * gs_prog_data->control_data_header_size_hwords;
      if (gs_prog_data->static_vertex_count == -1)
         starting_urb_offset += 2;

      /* The URB offset is in 128-bit units, so we need to multiply by 2 */
      const int output_vertex_size_owords =
         gs_prog_data->output_vertex_size_hwords * 2;

      /* On Xe2+ platform, LSC can operate on the Dword data element with byte
       * offset granularity, so convert per slot offset in bytes since it's in
       * Owords (16-bytes) unit else keep per slot offset in oword unit for
       * previous platforms.
       */
      const int output_vertex_size = devinfo->ver >= 20 ?
                                     output_vertex_size_owords * 16 :
                                     output_vertex_size_owords;
      if (gs_vertex_count.file == IMM) {
         per_slot_offsets = brw_imm_ud(output_vertex_size *
                                       gs_vertex_count.ud);
      } else {
         per_slot_offsets = bld.vgrf(BRW_TYPE_UD);
         bld.MUL(per_slot_offsets, gs_vertex_count,
                 brw_imm_ud(output_vertex_size));
      }
   }

   length = 0;
   urb_offset = starting_urb_offset;
   flush = false;

   /* SSO shaders can have VUE slots allocated which are never actually
    * written to, so ignore them when looking for the last (written) slot.
    */
   int last_slot = vue_map->num_slots - 1;
   while (last_slot > 0 &&
          (vue_map->slot_to_varying[last_slot] == BRW_VARYING_SLOT_PAD ||
           outputs[vue_map->slot_to_varying[last_slot]].file == BAD_FILE)) {
      last_slot--;
   }

   bool urb_written = false;
   for (slot = 0; slot < vue_map->num_slots; slot++) {
      int varying = vue_map->slot_to_varying[slot];
      switch (varying) {
      case VARYING_SLOT_PSIZ: {
         /* The point size varying slot is the vue header and is always in the
          * vue map. If anything in the header is going to be read back by HW,
          * we need to initialize it, in particular the viewport & layer
          * values.
          *
          * SKL PRMs, Volume 7: 3D-Media-GPGPU, Vertex URB Entry (VUE)
          * Formats:
          *
          *    "VUEs are written in two ways:
          *
          *       - At the top of the 3D Geometry pipeline, the VF's
          *         InputAssembly function creates VUEs and initializes them
          *         from data extracted from Vertex Buffers as well as
          *         internally generated data.
          *
          *       - VS, GS, HS and DS threads can compute, format, and write
          *         new VUEs as thread output."
          *
          *    "Software must ensure that any VUEs subject to readback by the
          *     3D pipeline start with a valid Vertex Header. This extends to
          *     all VUEs with the following exceptions:
          *
          *       - If the VS function is enabled, the VF-written VUEs are not
          *         required to have Vertex Headers, as the VS-incoming
          *         vertices are guaranteed to be consumed by the VS (i.e.,
          *         the VS thread is responsible for overwriting the input
          *         vertex data).
          *
          *       - If the GS FF is enabled, neither VF-written VUEs nor VS
          *         thread-generated VUEs are required to have Vertex Headers,
          *         as the GS will consume all incoming vertices.
          *
          *       - If Rendering is disabled, VertexHeaders are not required
          *         anywhere."
          */
         brw_reg zero =
            retype(brw_allocate_vgrf_units(*this, dispatch_width / 8), BRW_TYPE_UD);
         bld.MOV(zero, brw_imm_ud(0u));

         if (vue_map->slots_valid & VARYING_BIT_PRIMITIVE_SHADING_RATE &&
             this->outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE].file != BAD_FILE) {
            sources[length++] = this->outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE];
         } else if (devinfo->has_coarse_pixel_primitive_and_cb) {
            uint32_t one_fp16 = 0x3C00;
            brw_reg one_by_one_fp16 =
               retype(brw_allocate_vgrf_units(*this, dispatch_width / 8), BRW_TYPE_UD);
            bld.MOV(one_by_one_fp16, brw_imm_ud((one_fp16 << 16) | one_fp16));
            sources[length++] = one_by_one_fp16;
         } else {
            sources[length++] = zero;
         }

         if (vue_map->slots_valid & VARYING_BIT_LAYER)
            sources[length++] = this->outputs[VARYING_SLOT_LAYER];
         else
            sources[length++] = zero;

         if (vue_map->slots_valid & VARYING_BIT_VIEWPORT)
            sources[length++] = this->outputs[VARYING_SLOT_VIEWPORT];
         else
            sources[length++] = zero;

         if (vue_map->slots_valid & VARYING_BIT_PSIZ)
            sources[length++] = this->outputs[VARYING_SLOT_PSIZ];
         else
            sources[length++] = zero;
         break;
      }
      case VARYING_SLOT_EDGE:
         UNREACHABLE("unexpected scalar vs output");
         break;

      default:
         /* gl_Position is always in the vue map, but isn't always written by
          * the shader.  Other varyings (clip distances) get added to the vue
          * map but don't always get written.  In those cases, the
          * corresponding this->output[] slot will be invalid we and can skip
          * the urb write for the varying.  If we've already queued up a vue
          * slot for writing we flush a mlen 5 urb write, otherwise we just
          * advance the urb_offset.
          */
         if (varying == BRW_VARYING_SLOT_PAD ||
             this->outputs[varying].file == BAD_FILE) {
            if (length > 0)
               flush = true;
            else
               urb_offset++;
            break;
         }

         int slot_offset = 0;

         /* When using Primitive Replication, there may be multiple slots
          * assigned to POS.
          */
         if (varying == VARYING_SLOT_POS)
            slot_offset = slot - vue_map->varying_to_slot[VARYING_SLOT_POS];

         for (unsigned i = 0; i < 4; i++) {
            sources[length++] = offset(this->outputs[varying], bld,
                                       i + (slot_offset * 4));
         }
         break;
      }

      const brw_builder abld = bld.annotate("URB write");

      /* If we've queued up 8 registers of payload (2 VUE slots), if this is
       * the last slot or if we need to flush (see BAD_FILE varying case
       * above), emit a URB write send now to flush out the data.
       */
      if (length == 8 || (length > 0 && slot == last_slot))
         flush = true;
      if (flush) {
         brw_reg srcs[URB_LOGICAL_NUM_SRCS];

         srcs[URB_LOGICAL_SRC_HANDLE] = urb_handle;
         srcs[URB_LOGICAL_SRC_PER_SLOT_OFFSETS] = per_slot_offsets;
         srcs[URB_LOGICAL_SRC_DATA] =
            retype(brw_allocate_vgrf_units(*this, (dispatch_width / 8) * length), BRW_TYPE_F);
         abld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], sources, length, 0);

         brw_urb_inst *urb = abld.URB_WRITE(srcs, ARRAY_SIZE(srcs));
         urb->components = length;

         /* For Wa_1805992985 one needs additional write in the end. */
         if (intel_needs_workaround(devinfo, 1805992985) && stage == MESA_SHADER_TESS_EVAL)
            urb->eot = false;
         else
            urb->eot = slot == last_slot && stage != MESA_SHADER_GEOMETRY;

         urb->offset = urb_offset * (devinfo->ver >= 20 ? 16 : 1);
         urb_offset = starting_urb_offset + slot + 1;
         length = 0;
         flush = false;
         urb_written = true;
      }
   }

   /* If we don't have any valid slots to write, just do a minimal urb write
    * send to terminate the shader.  This includes 1 slot of undefined data,
    * because it's invalid to write 0 data:
    *
    * From the Broadwell PRM, Volume 7: 3D Media GPGPU, Shared Functions -
    * Unified Return Buffer (URB) > URB_SIMD8_Write and URB_SIMD8_Read >
    * Write Data Payload:
    *
    *    "The write data payload can be between 1 and 8 message phases long."
    */
   if (!urb_written) {
      /* For GS, just turn EmitVertex() into a no-op.  We don't want it to
       * end the thread, and emit_gs_thread_end() already emits a SEND with
       * EOT at the end of the program for us.
       */
      if (stage == MESA_SHADER_GEOMETRY)
         return;

      brw_reg uniform_urb_handle =
         retype(brw_allocate_vgrf_units(*this, dispatch_width / 8), BRW_TYPE_UD);
      brw_reg payload =
         retype(brw_allocate_vgrf_units(*this, dispatch_width / 8), BRW_TYPE_UD);

      bld.exec_all().MOV(uniform_urb_handle, urb_handle);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = uniform_urb_handle;
      srcs[URB_LOGICAL_SRC_DATA] = payload;

      brw_urb_inst *urb = bld.URB_WRITE(srcs, ARRAY_SIZE(srcs));
      urb->eot = true;
      urb->offset = devinfo->ver >= 20 ? 16 : 1;
      urb->components = 1;
      return;
   }

   /* Wa_1805992985:
    *
    * GPU hangs on one of tessellation vkcts tests with DS not done. The
    * send cycle, which is a urb write with an eot must be 4 phases long and
    * all 8 lanes must valid.
    */
   if (intel_needs_workaround(devinfo, 1805992985) && stage == MESA_SHADER_TESS_EVAL) {
      assert(dispatch_width == 8);
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
   this->source_depth_to_render_target = false;
   this->first_non_payload_grf = 0;

   this->uniforms = this->nir->num_uniforms / 4;
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
brw_barycentric_mode(const struct brw_wm_prog_key *key,
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
   brw_foreach_in_list_reverse(brw_inst, prev, &this->instructions) {
      if (prev->opcode == SHADER_OPCODE_URB_WRITE_LOGICAL) {
         prev->eot = true;

         /* Delete now dead instructions. */
         brw_foreach_in_list_reverse_safe(brw_exec_node, dead, &this->instructions) {
            if (dead == prev)
               break;
            dead->remove();
         }
         return true;
      } else if (prev->is_control_flow() || prev->has_side_effects()) {
         break;
      }
   }

   return false;
}

static unsigned
round_components_to_whole_registers(const intel_device_info *devinfo,
                                    unsigned c)
{
   return DIV_ROUND_UP(c, 8 * reg_unit(devinfo)) * reg_unit(devinfo);
}

void
brw_shader::assign_curb_setup()
{
   unsigned uniform_push_length =
      round_components_to_whole_registers(devinfo, prog_data->nr_params);

   unsigned ubo_push_length = 0;
   unsigned ubo_push_start[4];
   for (int i = 0; i < 4; i++) {
      ubo_push_start[i] = 8 * (ubo_push_length + uniform_push_length);
      ubo_push_length += prog_data->ubo_ranges[i].length;

      assert(ubo_push_start[i] % (8 * reg_unit(devinfo)) == 0);
      assert(ubo_push_length % (1 * reg_unit(devinfo)) == 0);
   }

   prog_data->curb_read_length = uniform_push_length + ubo_push_length;
   if (stage == MESA_SHADER_FRAGMENT &&
       ((struct brw_wm_prog_key *)key)->null_push_constant_tbimr_workaround)
      prog_data->curb_read_length = MAX2(1, prog_data->curb_read_length);

   uint64_t used = 0;
   const bool pull_constants =
      devinfo->verx10 >= 125 &&
      (mesa_shader_stage_is_compute(stage) ||
       mesa_shader_stage_is_mesh(stage)) &&
      uniform_push_length;

   if (pull_constants) {
      const bool pull_constants_a64 =
         (mesa_shader_stage_is_rt(stage) &&
          brw_bs_prog_data(prog_data)->uses_inline_push_addr) ||
         ((mesa_shader_stage_is_compute(stage) ||
           mesa_shader_stage_is_mesh(stage)) &&
          brw_cs_prog_data(prog_data)->uses_inline_push_addr);
      assert(devinfo->has_lsc);
      brw_builder ubld = brw_builder(this, 1).exec_all().at_start(cfg->first_block());

      brw_reg base_addr;
      if (pull_constants_a64) {
         /* The address of the push constants is at offset 0 in the inline
          * parameter.
          */
         base_addr =
            mesa_shader_stage_is_rt(stage) ?
            retype(bs_payload().inline_parameter, BRW_TYPE_UQ) :
            retype(cs_payload().inline_parameter, BRW_TYPE_UQ);
      } else {
         /* The base offset for our push data is passed in as R0.0[31:6]. We
          * have to mask off the bottom 6 bits.
          */
         base_addr = ubld.AND(retype(brw_vec1_grf(0, 0), BRW_TYPE_UD),
                              brw_imm_ud(INTEL_MASK(31, 6)));
      }

      brw_analysis_dependency_class dirty_bits = BRW_DEPENDENCY_INSTRUCTIONS;

      /* On Gfx12-HP we load constants at the start of the program using A32
       * stateless messages.
       */
      for (unsigned i = 0; i < uniform_push_length;) {
         /* Limit ourselves to LSC HW limit of 8 GRFs (256bytes D32V64). */
         unsigned num_regs = MIN2(uniform_push_length - i, 8);
         assert(num_regs > 0);
         num_regs = 1 << util_logbase2(num_regs);

         brw_reg addr;

         if (i != 0) {
            if (pull_constants_a64) {
               dirty_bits |= BRW_DEPENDENCY_VARIABLES;
               /* We need to do the carry manually as when this pass is run,
                * we're not expecting any 64bit ALUs. Unfortunately all the
                * 64bit lowering is done in NIR.
                */
               addr = ubld.vgrf(BRW_TYPE_UQ);
               brw_reg addr_ldw = subscript(addr, BRW_TYPE_UD, 0);
               brw_reg addr_udw = subscript(addr, BRW_TYPE_UD, 1);
               brw_reg base_addr_ldw = subscript(base_addr, BRW_TYPE_UD, 0);
               brw_reg base_addr_udw = subscript(base_addr, BRW_TYPE_UD, 1);
               ubld.ADD(addr_ldw, base_addr_ldw, brw_imm_ud(i * REG_SIZE));
               ubld.CMP(ubld.null_reg_d(), addr_ldw, base_addr_ldw, BRW_CONDITIONAL_L);
               set_predicate(BRW_PREDICATE_NORMAL,
                             ubld.ADD(addr_udw, base_addr_udw, brw_imm_ud(1)));
               set_predicate_inv(BRW_PREDICATE_NORMAL, true,
                                 ubld.MOV(addr_udw, base_addr_udw));
            } else {
               addr = ubld.ADD(base_addr, brw_imm_ud(i * REG_SIZE));
            }
         } else {
            addr = base_addr;
         }

         brw_send_inst *send = ubld.SEND();
         send->dst = retype(brw_vec8_grf(payload().num_regs + i, 0),
                            BRW_TYPE_UD);

         send->src[SEND_SRC_DESC]     = brw_imm_ud(0);
         send->src[SEND_SRC_EX_DESC]  = brw_imm_ud(0);
         send->src[SEND_SRC_PAYLOAD1] = addr;
         send->src[SEND_SRC_PAYLOAD2] = brw_reg();

         send->sfid = BRW_SFID_UGM;
         uint32_t desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                                      LSC_ADDR_SURFTYPE_FLAT,
                                      pull_constants_a64 ?
                                      LSC_ADDR_SIZE_A64 : LSC_ADDR_SIZE_A32,
                                      LSC_DATA_SIZE_D32,
                                      num_regs * 8 /* num_channels */,
                                      true /* transpose */,
                                      LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));
         send->header_size = 0;
         send->mlen = lsc_msg_addr_len(
            devinfo, pull_constants_a64 ?
            LSC_ADDR_SIZE_A64 : LSC_ADDR_SIZE_A32, 1);
         send->size_written =
            lsc_msg_dest_len(devinfo, LSC_DATA_SIZE_D32, num_regs * 8) * REG_SIZE;
         assert((payload().num_regs + i + send->size_written / REG_SIZE) <=
                (payload().num_regs + prog_data->curb_read_length));
         send->is_volatile = true;

         send->src[SEND_SRC_DESC] =
            brw_imm_ud(desc | brw_message_desc(devinfo,
                                               send->mlen,
                                               send->size_written / REG_SIZE,
                                               send->header_size));

         i += num_regs;
      }

      invalidate_analysis(dirty_bits);
   }

   /* Map the offsets in the UNIFORM file to fixed HW regs. */
   foreach_block_and_inst(block, brw_inst, inst, cfg) {
      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == UNIFORM) {
            int uniform_nr = inst->src[i].nr + inst->src[i].offset / 4;
            int constant_nr;
            if (inst->src[i].nr >= UBO_START) {
               /* constant_nr is in 32-bit units, the rest are in bytes */
               constant_nr = ubo_push_start[inst->src[i].nr - UBO_START] +
                             inst->src[i].offset / 4;
            } else if (uniform_nr >= 0 && uniform_nr < (int) uniforms) {
               constant_nr = uniform_nr;
            } else {
               /* Section 5.11 of the OpenGL 4.1 spec says:
                * "Out-of-bounds reads return undefined values, which include
                *  values from other variables of the active program or zero."
                * Just return the first push constant.
                */
               constant_nr = 0;
            }

            assert(constant_nr / 8 < 64);
            used |= BITFIELD64_BIT(constant_nr / 8);

	    struct brw_reg brw_reg = brw_vec1_grf(payload().num_regs +
						  constant_nr / 8,
						  constant_nr % 8);
            brw_reg.abs = inst->src[i].abs;
            brw_reg.negate = inst->src[i].negate;

            /* The combination of is_scalar for load_uniform, copy prop, and
             * lower_btd_logical_send can generate a MOV from a UNIFORM with
             * exec size 2 and stride of 1.
             */
            assert(inst->src[i].stride == 0 || inst->exec_size == 2);
            inst->src[i] = byte_offset(
               retype(brw_reg, inst->src[i].type),
               inst->src[i].offset % 4);
	 }
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
         struct brw_ubo_range *ubo_range = &prog_data->ubo_ranges[i];

         unsigned range_start = ubo_push_start[i] / 8;
         uint64_t want_zero = (used >> range_start) & BITFIELD64_MASK(ubo_range->length);
         if (!want_zero)
            continue;

         const unsigned grf_start = payload().num_regs + range_start;
         const unsigned grf_end = grf_start + ubo_range->length;
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
               and_length = 1u << (util_last_bit(MIN2(grf_end - grf, max_grf_writes)) - 1);

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
   this->first_non_payload_grf = payload().num_regs + prog_data->curb_read_length;

   this->debug_optimizer(this->nir, "assign_curb_setup", 90, 0);
}

/*
 * Build up an array of indices into the urb_setup array that
 * references the active entries of the urb_setup array.
 * Used to accelerate walking the active entries of the urb_setup array
 * on each upload.
 */
void
brw_compute_urb_setup_index(struct brw_wm_prog_data *wm_prog_data)
{
   /* TODO(mesh): Review usage of this in the context of Mesh, we may want to
    * skip per-primitive attributes here.
    */

   /* Make sure uint8_t is sufficient */
   STATIC_ASSERT(VARYING_SLOT_MAX <= 0xff);
   uint8_t index = 0;
   for (uint8_t attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (wm_prog_data->urb_setup[attr] >= 0) {
         wm_prog_data->urb_setup_attribs[index++] = attr;
      }
   }
   wm_prog_data->urb_setup_attribs_count = index;
}

void
brw_shader::convert_attr_sources_to_hw_regs(brw_inst *inst)
{
   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].file == ATTR) {
         assert(inst->src[i].nr == 0);
         int grf = payload().num_regs +
                   prog_data->curb_read_length +
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

int
brw_get_subgroup_id_param_index(const intel_device_info *devinfo,
                                const brw_stage_prog_data *prog_data)
{
   if (prog_data->nr_params == 0)
      return -1;

   if (devinfo->verx10 >= 125)
      return -1;

   /* The local thread id is always the last parameter in the list */
   uint32_t last_param = prog_data->param[prog_data->nr_params - 1];
   if (last_param == BRW_PARAM_BUILTIN_SUBGROUP_ID)
      return prog_data->nr_params - 1;

   return -1;
}

uint32_t
brw_fb_write_msg_control(const brw_inst *inst,
                         const struct brw_wm_prog_data *prog_data)
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

   if (!allocated) {
      if (0) {
         fprintf(stderr, "Spilling - using lowest-pressure mode \"%s\"\n",
                 scheduler_mode_name[pre_modes[best_press_idx]]);
      }
      restore_instruction_order(s, orders[best_press_idx]);
      s.shader_stats.scheduler_mode = scheduler_mode_name[pre_modes[best_press_idx]];

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

#define OPT(pass, ...) ({                                               \
      pass_num++;                                                       \
      bool this_progress = pass(s, ##__VA_ARGS__);                      \
                                                                        \
      if (this_progress)                                                \
         s.debug_optimizer(nir, #pass, iteration, pass_num);            \
                                                                        \
      this_progress;                                                    \
   })

#define OPT_V(pass, ...) do {                                           \
      pass_num++;                                                       \
      pass(s, ##__VA_ARGS__);                                           \
      s.debug_optimizer(nir, #pass, iteration, pass_num);               \
   } while (false)

   int pass_num = 0;
   int iteration = 96;

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

   OPT(brw_lower_scoreboard);
}

#ifndef NDEBUG
void
brw_debug_archive_nir(debug_archiver *archiver, nir_shader *nir,
                      unsigned dispatch_width, const char *step)
{
   if (!archiver)
      return;

   const bool prefix_dispatch_width =
      dispatch_width > 0 && mesa_shader_stage_uses_workgroup(nir->info.stage);
   const char *filename = prefix_dispatch_width ?
         ralloc_asprintf(archiver, "NIR%d/%s", dispatch_width, step) :
         ralloc_asprintf(archiver, "NIR/%s", step);

   FILE *f = debug_archiver_start_file(archiver, filename);
   nir_print_shader(nir, f);
   debug_archiver_finish_file(archiver);
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

   const int simd = brw_simd_select_for_workgroup_size(devinfo, prog_data, sizes);
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
