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

/** @file
 *
 * This file supports generating the FS LIR from the GLSL IR.  The LIR
 * makes it easier to do backend-specific optimizations than doing so
 * in the GLSL IR or in the native code.
 */
#include "brw_eu.h"
#include "brw_fs.h"
#include "brw_fs_builder.h"
#include "brw_nir.h"
#include "compiler/glsl_types.h"
#include "dev/intel_device_info.h"

using namespace brw;

void
fs_visitor::emit_urb_writes(const brw_reg &gs_vertex_count)
{
   int slot, urb_offset, length;
   int starting_urb_offset = 0;
   const struct brw_vue_prog_data *vue_prog_data =
      brw_vue_prog_data(this->prog_data);
   const GLbitfield64 psiz_mask =
      VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT | VARYING_BIT_PSIZ | VARYING_BIT_PRIMITIVE_SHADING_RATE;
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
      unreachable("invalid stage");
   }

   const fs_builder bld = fs_builder(this).at_end();

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
          * vue map.  But often none of the special varyings that live there
          * are written and in that case we can skip writing to the vue
          * header, provided the corresponding state properly clamps the
          * values further down the pipeline. */
         if ((vue_map->slots_valid & psiz_mask) == 0) {
            assert(length == 0);
            urb_offset++;
            break;
         }

         brw_reg zero = brw_vgrf(alloc.allocate(dispatch_width / 8),
                                BRW_TYPE_UD);
         bld.MOV(zero, brw_imm_ud(0u));

         if (vue_map->slots_valid & VARYING_BIT_PRIMITIVE_SHADING_RATE &&
             this->outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE].file != BAD_FILE) {
            sources[length++] = this->outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE];
         } else if (devinfo->has_coarse_pixel_primitive_and_cb) {
            uint32_t one_fp16 = 0x3C00;
            brw_reg one_by_one_fp16 = brw_vgrf(alloc.allocate(dispatch_width / 8),
                                              BRW_TYPE_UD);
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
         unreachable("unexpected scalar vs output");
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

      const fs_builder abld = bld.annotate("URB write");

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
         srcs[URB_LOGICAL_SRC_DATA] = brw_vgrf(alloc.allocate((dispatch_width / 8) * length),
                                               BRW_TYPE_F);
         srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(length);
         abld.LOAD_PAYLOAD(srcs[URB_LOGICAL_SRC_DATA], sources, length, 0);

         fs_inst *inst = abld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef,
                                   srcs, ARRAY_SIZE(srcs));

         /* For Wa_1805992985 one needs additional write in the end. */
         if (intel_needs_workaround(devinfo, 1805992985) && stage == MESA_SHADER_TESS_EVAL)
            inst->eot = false;
         else
            inst->eot = slot == last_slot && stage != MESA_SHADER_GEOMETRY;

         inst->offset = urb_offset;
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

      brw_reg uniform_urb_handle = brw_vgrf(alloc.allocate(dispatch_width / 8),
                                           BRW_TYPE_UD);
      brw_reg payload = brw_vgrf(alloc.allocate(dispatch_width / 8),
                                BRW_TYPE_UD);

      bld.exec_all().MOV(uniform_urb_handle, urb_handle);

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = uniform_urb_handle;
      srcs[URB_LOGICAL_SRC_DATA] = payload;
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(1);

      fs_inst *inst = bld.emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef,
                               srcs, ARRAY_SIZE(srcs));
      inst->eot = true;
      inst->offset = 1;
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
      brw_reg uniform_urb_handle = brw_vgrf(alloc.allocate(1), BRW_TYPE_UD);
      brw_reg uniform_mask = brw_vgrf(alloc.allocate(1), BRW_TYPE_UD);
      brw_reg payload = brw_vgrf(alloc.allocate(4), BRW_TYPE_UD);

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
      bld.exec_all().MOV(uniform_mask, brw_imm_ud(0x10000u));
      bld.exec_all().MOV(offset(payload, bld, 0), brw_imm_ud(0u));
      bld.exec_all().MOV(offset(payload, bld, 1), brw_imm_ud(0u));
      bld.exec_all().MOV(offset(payload, bld, 2), brw_imm_ud(0u));
      bld.exec_all().MOV(offset(payload, bld, 3), brw_imm_ud(0u));

      brw_reg srcs[URB_LOGICAL_NUM_SRCS];
      srcs[URB_LOGICAL_SRC_HANDLE] = uniform_urb_handle;
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = uniform_mask;
      srcs[URB_LOGICAL_SRC_DATA] = payload;
      srcs[URB_LOGICAL_SRC_COMPONENTS] = brw_imm_ud(4);

      fs_inst *inst = bld.exec_all().emit(SHADER_OPCODE_URB_WRITE_LOGICAL,
                                          reg_undef, srcs, ARRAY_SIZE(srcs));
      inst->eot = true;
      inst->offset = 0;
   }
}

void
fs_visitor::emit_cs_terminate()
{
   const fs_builder ubld = fs_builder(this).at_end().exec_all();

   /* We can't directly send from g0, since sends with EOT have to use
    * g112-127. So, copy it to a virtual register, The register allocator will
    * make sure it uses the appropriate register range.
    */
   struct brw_reg g0 = retype(brw_vec8_grf(0, 0), BRW_TYPE_UD);
   brw_reg payload = brw_vgrf(alloc.allocate(reg_unit(devinfo)),
                             BRW_TYPE_UD);
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

   brw_reg srcs[4] = {
      brw_imm_ud(desc), /* desc */
      brw_imm_ud(0), /* ex_desc */
      payload,       /* payload */
      brw_reg(),      /* payload2 */
   };

   fs_inst *send = ubld.emit(SHADER_OPCODE_SEND, reg_undef, srcs, 4);

   /* On Alchemist and later, send an EOT message to the message gateway to
    * terminate a compute shader.  For older GPUs, send to the thread spawner.
    */
   send->sfid = devinfo->verx10 >= 125 ? BRW_SFID_MESSAGE_GATEWAY
                                       : BRW_SFID_THREAD_SPAWNER;
   send->mlen = reg_unit(devinfo);
   send->eot = true;
}

fs_visitor::fs_visitor(const struct brw_compiler *compiler,
                       const struct brw_compile_params *params,
                       const brw_base_prog_key *key,
                       struct brw_stage_prog_data *prog_data,
                       const nir_shader *shader,
                       unsigned dispatch_width,
                       bool needs_register_pressure,
                       bool debug_enabled)
   : compiler(compiler), log_data(params->log_data),
     devinfo(compiler->devinfo), nir(shader),
     mem_ctx(params->mem_ctx),
     cfg(NULL), stage(shader->info.stage),
     debug_enabled(debug_enabled),
     key(key), gs_compile(NULL), prog_data(prog_data),
     live_analysis(this), regpressure_analysis(this),
     performance_analysis(this), idom_analysis(this), def_analysis(this),
     needs_register_pressure(needs_register_pressure),
     dispatch_width(dispatch_width),
     max_polygons(0),
     api_subgroup_size(brw_nir_api_subgroup_size(shader, dispatch_width))
{
   init();
}

fs_visitor::fs_visitor(const struct brw_compiler *compiler,
                       const struct brw_compile_params *params,
                       const brw_wm_prog_key *key,
                       struct brw_wm_prog_data *prog_data,
                       const nir_shader *shader,
                       unsigned dispatch_width, unsigned max_polygons,
                       bool needs_register_pressure,
                       bool debug_enabled)
   : compiler(compiler), log_data(params->log_data),
     devinfo(compiler->devinfo), nir(shader),
     mem_ctx(params->mem_ctx),
     cfg(NULL), stage(shader->info.stage),
     debug_enabled(debug_enabled),
     key(&key->base), gs_compile(NULL), prog_data(&prog_data->base),
     live_analysis(this), regpressure_analysis(this),
     performance_analysis(this), idom_analysis(this), def_analysis(this),
     needs_register_pressure(needs_register_pressure),
     dispatch_width(dispatch_width),
     max_polygons(max_polygons),
     api_subgroup_size(brw_nir_api_subgroup_size(shader, dispatch_width))
{
   init();
   assert(api_subgroup_size == 0 ||
          api_subgroup_size == 8 ||
          api_subgroup_size == 16 ||
          api_subgroup_size == 32);
}

fs_visitor::fs_visitor(const struct brw_compiler *compiler,
                       const struct brw_compile_params *params,
                       struct brw_gs_compile *c,
                       struct brw_gs_prog_data *prog_data,
                       const nir_shader *shader,
                       bool needs_register_pressure,
                       bool debug_enabled)
   : compiler(compiler), log_data(params->log_data),
     devinfo(compiler->devinfo), nir(shader),
     mem_ctx(params->mem_ctx),
     cfg(NULL), stage(shader->info.stage),
     debug_enabled(debug_enabled),
     key(&c->key.base), gs_compile(c),
     prog_data(&prog_data->base.base),
     live_analysis(this), regpressure_analysis(this),
     performance_analysis(this), idom_analysis(this), def_analysis(this),
     needs_register_pressure(needs_register_pressure),
     dispatch_width(compiler->devinfo->ver >= 20 ? 16 : 8),
     max_polygons(0),
     api_subgroup_size(brw_nir_api_subgroup_size(shader, dispatch_width))
{
   init();
   assert(api_subgroup_size == 0 ||
          api_subgroup_size == 8 ||
          api_subgroup_size == 16 ||
          api_subgroup_size == 32);
}

void
fs_visitor::init()
{
   this->max_dispatch_width = 32;

   this->failed = false;
   this->fail_msg = NULL;

   this->payload_ = NULL;
   this->source_depth_to_render_target = false;
   this->first_non_payload_grf = 0;

   this->uniforms = 0;
   this->last_scratch = 0;
   this->push_constant_loc = NULL;

   memset(&this->shader_stats, 0, sizeof(this->shader_stats));

   this->grf_used = 0;
   this->spilled_any_registers = false;

   this->phase = BRW_SHADER_PHASE_INITIAL;
}

fs_visitor::~fs_visitor()
{
   delete this->payload_;
}
