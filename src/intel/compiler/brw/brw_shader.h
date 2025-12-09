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

#pragma once

#include "brw_analysis.h"
#include "brw_cfg.h"
#include "brw_compiler.h"
#include "brw_inst.h"
#include "compiler/nir/nir.h"
#include "brw_analysis.h"
#include "brw_thread_payload.h"

#define UBO_START ((1 << 16) - 4)

struct brw_shader_stats {
   const char *scheduler_mode;
   unsigned promoted_constants;
   unsigned spill_count;
   unsigned fill_count;
   unsigned max_register_pressure;
   unsigned non_ssa_registers_after_nir;
};

enum brw_shader_phase {
   BRW_SHADER_PHASE_INITIAL = 0,
   BRW_SHADER_PHASE_AFTER_NIR,
   BRW_SHADER_PHASE_AFTER_OPT_LOOP,
   BRW_SHADER_PHASE_AFTER_EARLY_LOWERING,
   BRW_SHADER_PHASE_AFTER_MIDDLE_LOWERING,
   BRW_SHADER_PHASE_AFTER_LATE_LOWERING,
   BRW_SHADER_PHASE_AFTER_REGALLOC,

   /* Larger value than any other phase. */
   BRW_SHADER_PHASE_INVALID,
};

struct brw_shader_params
{
   const struct brw_compiler *compiler;
   void *mem_ctx;

   const nir_shader *nir;
   const brw_base_prog_key *key;
   brw_stage_prog_data *prog_data;

   unsigned dispatch_width;

   /* Fragment shader. */
   unsigned num_polygons;
   const int *per_primitive_offsets;

   bool needs_register_pressure;
   void *log_data;
   bool debug_enabled;

   debug_archiver *archiver;
};

struct brw_shader
{
public:
   brw_shader(const brw_shader_params *params);
   ~brw_shader();

   void assign_curb_setup();
   void convert_attr_sources_to_hw_regs(brw_inst *inst);
   void calculate_payload_ranges(bool allow_spilling,
                                 unsigned payload_node_count,
                                 int *payload_last_use_ip) const;
   void invalidate_analysis(brw_analysis_dependency_class c);

   void vfail(const char *msg, va_list args);
   void fail(const char *msg, ...);
   void limit_dispatch_width(unsigned n, const char *msg);

   void emit_urb_writes(const brw_reg &gs_vertex_count = brw_reg());
   void emit_gs_control_data_bits(const brw_reg &vertex_count);
   brw_reg gs_urb_channel_mask(const brw_reg &dword_index);
   brw_reg gs_urb_per_slot_dword_index(const brw_reg &vertex_count);
   bool mark_last_urb_write_with_eot();
   void emit_cs_terminate();

   const struct brw_compiler *compiler;
   void *log_data; /* Passed to compiler->*_log functions */

   const struct intel_device_info * const devinfo;
   const nir_shader *nir;

   /** ralloc context for temporary data used during compile */
   void *mem_ctx;

   /** List of brw_inst. */
   brw_exec_list instructions;

   cfg_t *cfg;

   mesa_shader_stage stage;
   bool debug_enabled;

   /* VGRF allocation. */
   struct {
      /** Array of sizes for each allocation, in REG_SIZE units. */
      unsigned *sizes;

      /** Total number of VGRFs allocated. */
      unsigned count;

      unsigned capacity;
   } alloc;

   const brw_base_prog_key *const key;

   struct brw_stage_prog_data *prog_data;

   brw_analysis<brw_live_variables, brw_shader> live_analysis;
   brw_analysis<brw_register_pressure, brw_shader> regpressure_analysis;
   brw_analysis<brw_performance, brw_shader> performance_analysis;
   brw_analysis<brw_idom_tree, brw_shader> idom_analysis;
   brw_analysis<brw_def_analysis, brw_shader> def_analysis;
   brw_analysis<brw_ip_ranges, brw_shader> ip_ranges_analysis;

   /** Number of uniform variable components visited. */
   unsigned uniforms;

   /** Byte-offset for the next available spot in the scratch space buffer. */
   unsigned last_scratch;

   brw_reg frag_depth;
   brw_reg frag_stencil;
   brw_reg sample_mask;
   brw_reg outputs[VARYING_SLOT_MAX];
   brw_reg dual_src_output;
   /* This includes HW thread payload + push constants + URB(after brw_assign_xs_urb_setup()) */
   int first_non_payload_grf;

   enum brw_shader_phase phase;

   bool failed;
   char *fail_msg;

   /* Use the vs_payload(), fs_payload(), etc. to access the right payload. */
   brw_thread_payload *payload_;

#define DEFINE_PAYLOAD_ACCESSOR(TYPE, NAME, ASSERTION)   \
   TYPE &NAME() {                                        \
      assert(ASSERTION);                                 \
      return *static_cast<TYPE *>(this->payload_);       \
   }                                                     \
   const TYPE &NAME() const {                            \
      assert(ASSERTION);                                 \
      return *static_cast<const TYPE *>(this->payload_); \
   }

   DEFINE_PAYLOAD_ACCESSOR(brw_thread_payload,     payload,     true);
   DEFINE_PAYLOAD_ACCESSOR(brw_vs_thread_payload,  vs_payload,  stage == MESA_SHADER_VERTEX);
   DEFINE_PAYLOAD_ACCESSOR(brw_tcs_thread_payload, tcs_payload, stage == MESA_SHADER_TESS_CTRL);
   DEFINE_PAYLOAD_ACCESSOR(brw_tes_thread_payload, tes_payload, stage == MESA_SHADER_TESS_EVAL);
   DEFINE_PAYLOAD_ACCESSOR(brw_gs_thread_payload,  gs_payload,  stage == MESA_SHADER_GEOMETRY);
   DEFINE_PAYLOAD_ACCESSOR(brw_fs_thread_payload,  fs_payload,  stage == MESA_SHADER_FRAGMENT);
   DEFINE_PAYLOAD_ACCESSOR(brw_cs_thread_payload,  cs_payload,
                           mesa_shader_stage_uses_workgroup(stage));
   DEFINE_PAYLOAD_ACCESSOR(brw_task_mesh_thread_payload, task_mesh_payload,
                           stage == MESA_SHADER_TASK || stage == MESA_SHADER_MESH);
   DEFINE_PAYLOAD_ACCESSOR(brw_bs_thread_payload, bs_payload,
                           stage >= MESA_SHADER_RAYGEN && stage <= MESA_SHADER_CALLABLE);

   bool source_depth_to_render_target;

   brw_reg uw_pixel_x;
   brw_reg uw_pixel_y;
   brw_reg pixel_z;
   brw_reg wpos_w;
   brw_reg pixel_w;
   brw_reg delta_xy[INTEL_BARYCENTRIC_MODE_COUNT];
   brw_reg final_gs_vertex_count;
   brw_reg control_data_bits;
   brw_reg invocation_id;

   struct {
      unsigned control_data_bits_per_vertex;
      unsigned control_data_header_size_bits;
   } gs;

   struct {
      /* Offset of per-primitive locations in bytes */
      int per_primitive_offsets[VARYING_SLOT_MAX];
   } fs;

   unsigned grf_used;
   bool spilled_any_registers;
   bool needs_register_pressure;

   const unsigned dispatch_width; /**< 8, 16 or 32 */
   const unsigned max_polygons;
   unsigned max_dispatch_width;

   /* The API selected subgroup size */
   unsigned api_subgroup_size; /**< 0, 8, 16, 32 */

   unsigned next_address_register_nr;

   struct brw_shader_stats shader_stats;

   debug_archiver *archiver;

   void debug_optimizer(const nir_shader *nir,
                        const char *pass_name,
                        int iteration, int pass_num) const;

   /* Used to allocate instructions, see brw_new_inst() and brw_clone_inst(). */
   struct {
      void *mem_ctx;
      unsigned cap;
      char *beg;
      char *end;

      unsigned total_cap;
   } inst_arena;
};

void brw_print_instructions(const brw_shader &s, FILE *file = stderr);

void brw_print_instruction(const brw_shader &s, const brw_inst *inst,
                           FILE *file = stderr,
                           const brw_def_analysis *defs = nullptr);

void brw_print_swsb(FILE *f, const struct intel_device_info *devinfo, const tgl_swsb swsb);

static inline bool
brw_can_coherent_fb_fetch(const struct intel_device_info *devinfo)
{
   /* Not functional after Gfx20 */
   return devinfo->ver >= 9 && devinfo->ver < 20;
}

/**
 * Return the flag register used in fragment shaders to keep track of live
 * samples.  On Gfx7+ we use f1.0-f1.1 to allow discard jumps in SIMD32
 * dispatch mode.
 */
static inline unsigned
sample_mask_flag_subreg(const brw_shader &s)
{
   assert(s.stage == MESA_SHADER_FRAGMENT);
   return 2;
}

inline brw_reg
brw_dynamic_msaa_flags(const struct brw_wm_prog_data *wm_prog_data)
{
   return brw_uniform_reg(wm_prog_data->msaa_flags_param, BRW_TYPE_UD);
}

inline brw_reg
brw_dynamic_per_primitive_remap(const struct brw_wm_prog_data *wm_prog_data)
{
   return brw_uniform_reg(wm_prog_data->per_primitive_remap_param, BRW_TYPE_UD);
}

enum intel_barycentric_mode brw_barycentric_mode(const struct brw_wm_prog_key *key,
                                                 nir_intrinsic_instr *intr);

uint32_t brw_fb_write_msg_control(const brw_inst *inst,
                                  const struct brw_wm_prog_data *prog_data);

void brw_compute_urb_setup_index(struct brw_wm_prog_data *wm_prog_data);

int brw_get_subgroup_id_param_index(const intel_device_info *devinfo,
                                    const brw_stage_prog_data *prog_data);

void brw_from_nir(brw_shader *s);

void brw_shader_phase_update(brw_shader &s, enum brw_shader_phase phase);

#ifndef NDEBUG
void brw_validate(const brw_shader &s);
#else
static inline void brw_validate(const brw_shader &s) {}
#endif

void brw_calculate_cfg(brw_shader &s);

void brw_optimize(brw_shader &s);

enum brw_instruction_scheduler_mode {
   BRW_SCHEDULE_PRE_LATENCY,
   BRW_SCHEDULE_PRE,
   BRW_SCHEDULE_PRE_NON_LIFO,
   BRW_SCHEDULE_PRE_LIFO,
   BRW_SCHEDULE_POST,
   BRW_SCHEDULE_NONE,
};

class brw_instruction_scheduler;

brw_instruction_scheduler *brw_prepare_scheduler(brw_shader &s, void *mem_ctx);
void brw_schedule_instructions_pre_ra(brw_shader &s, brw_instruction_scheduler *sched,
                                      brw_instruction_scheduler_mode mode);
void brw_schedule_instructions_post_ra(brw_shader &s);

void brw_allocate_registers(brw_shader &s, bool allow_spilling);
bool brw_assign_regs(brw_shader &s, bool allow_spilling, bool spill_all);
void brw_assign_regs_trivial(brw_shader &s);

bool brw_lower_3src_null_dest(brw_shader &s);
bool brw_lower_alu_restrictions(brw_shader &s);
bool brw_lower_barycentrics(brw_shader &s);
bool brw_lower_bfloat_conversion(brw_shader &s, brw_inst *inst);
bool brw_lower_constant_loads(brw_shader &s);
bool brw_lower_csel(brw_shader &s);
bool brw_lower_derivatives(brw_shader &s);
bool brw_lower_dpas(brw_shader &s);
bool brw_lower_fill_and_spill(brw_shader &s);
bool brw_lower_find_live_channel(brw_shader &s);
bool brw_lower_indirect_mov(brw_shader &s);
bool brw_lower_integer_multiplication(brw_shader &s);
bool brw_lower_load_payload(brw_shader &s);
bool brw_lower_load_subgroup_invocation(brw_shader &s);
bool brw_lower_logical_sends(brw_shader &s);
bool brw_lower_pack(brw_shader &s);
bool brw_lower_regioning(brw_shader &s);
bool brw_lower_scalar_fp64_MAD(brw_shader &s);
bool brw_lower_scoreboard(brw_shader &s);
bool brw_lower_send_descriptors(brw_shader &s);
bool brw_lower_send_gather(brw_shader &s);
bool brw_lower_sends_overlapping_payload(brw_shader &s);
bool brw_lower_simd_width(brw_shader &s);
bool brw_lower_src_modifiers(brw_shader &s, brw_inst *inst, unsigned i);
bool brw_lower_sub_sat(brw_shader &s);
bool brw_lower_subgroup_ops(brw_shader &s);
bool brw_lower_uniform_pull_constant_loads(brw_shader &s);
void brw_lower_vgrfs_to_fixed_grfs(brw_shader &s);
brw_reg brw_lower_vgrf_to_fixed_grf(const struct intel_device_info *devinfo,
                                    const brw_inst *inst, const brw_reg &reg);

bool brw_opt_address_reg_load(brw_shader &s);
bool brw_opt_algebraic(brw_shader &s);
bool brw_opt_bank_conflicts(brw_shader &s);
bool brw_opt_cmod_propagation(brw_shader &s);
bool brw_opt_combine_constants(brw_shader &s);
bool brw_opt_combine_convergent_txf(brw_shader &s);
bool brw_opt_compact_virtual_grfs(brw_shader &s);
bool brw_opt_constant_fold_instruction(brw_shader &s, brw_inst *inst);
bool brw_opt_copy_propagation(brw_shader &s);
bool brw_opt_copy_propagation_defs(brw_shader &s);
bool brw_opt_cse_defs(brw_shader &s);
bool brw_opt_dead_code_eliminate(brw_shader &s);
bool brw_opt_eliminate_find_live_channel(brw_shader &s);
bool brw_opt_fill_and_spill(brw_shader &s);
bool brw_opt_register_coalesce(brw_shader &s);
bool brw_opt_remove_extra_rounding_modes(brw_shader &s);
bool brw_opt_remove_redundant_halts(brw_shader &s);
bool brw_opt_saturate_propagation(brw_shader &s);
bool brw_opt_send_gather_to_send(brw_shader &s);
bool brw_opt_send_to_send_gather(brw_shader &s);
bool brw_opt_split_sends(brw_shader &s);
bool brw_opt_split_virtual_grfs(brw_shader &s);
bool brw_opt_zero_samples(brw_shader &s);

bool brw_workaround_emit_dummy_mov_instruction(brw_shader &s);
bool brw_workaround_memory_fence_before_eot(brw_shader &s);
bool brw_workaround_nomask_control_flow(brw_shader &s);
bool brw_workaround_source_arf_before_eot(brw_shader &s);

/* Helpers. */
unsigned brw_get_lowered_simd_width(const brw_shader *shader,
                                    const brw_inst *inst);

brw_reg brw_allocate_vgrf(brw_shader &s, brw_reg_type type, unsigned count);
brw_reg brw_allocate_vgrf_units(brw_shader &s, unsigned units_of_REGSIZE);

bool brw_insert_load_reg(brw_shader &s);
bool brw_lower_load_reg(brw_shader &s);

brw_inst *brw_new_inst(brw_shader &s, enum opcode opcode, unsigned exec_size,
                       const brw_reg &dst, unsigned num_srcs);
brw_inst *brw_clone_inst(brw_shader &s, const brw_inst *inst);

/* Transform the opcode/num_sources of an instruction.  All the fields in
 * brw_inst are maintained and any previous sources still visible.  Additional
 * sources will be uninitialized.
 *
 * All instructions can be transformed to an instruction of BASE kind.
 * All non-BASE instructions can be transformed to an instruction of SEND kind.
 *
 * If new_num_srcs is UINT_MAX a default will be picked based on the opcode.
 * Not all opcodes have a default.
 */
brw_inst *brw_transform_inst(brw_shader &s, brw_inst *inst, enum opcode new_opcode,
                             unsigned new_num_srcs = UINT_MAX);
