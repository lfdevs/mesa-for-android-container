/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "brw_shader.h"

/* Translates BRW IR to actual EU assembly code. */
class brw_generator
{
public:
   brw_generator(const struct brw_compiler *compiler,
                const struct brw_compile_params *params,
                struct brw_stage_prog_data *prog_data,
                mesa_shader_stage stage);
   ~brw_generator();

   void enable_debug(const char *shader_name);
   int generate_code(const brw_shader &s,
                     struct genisa_stats *stats);
   void add_const_data(void *data, unsigned size);
   void add_resume_sbt(unsigned num_resume_shaders, uint64_t *sbt);
   const unsigned *get_assembly();

private:
   void generate_send(brw_send_inst *inst,
                      struct brw_reg dst,
                      struct brw_reg desc,
                      struct brw_reg ex_desc,
                      struct brw_reg payload,
                      struct brw_reg payload2,
                      bool ex_bso);
   void generate_barrier(brw_inst *inst, struct brw_reg src);
   void generate_ddx(const brw_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_ddy(const brw_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_scratch_header(brw_inst *inst,
                                struct brw_reg dst, struct brw_reg src);

   void generate_halt(brw_inst *inst);

   void generate_mov_indirect(brw_inst *inst,
                              struct brw_reg dst,
                              struct brw_reg reg,
                              struct brw_reg indirect_byte_offset);

   void generate_shuffle(brw_inst *inst,
                         struct brw_reg dst,
                         struct brw_reg src,
                         struct brw_reg idx);

   void generate_quad_swizzle(const brw_inst *inst,
                              struct brw_reg dst, struct brw_reg src,
                              unsigned swiz);

   bool patch_halt_jumps();

   const struct brw_compiler *compiler;
   const struct brw_compile_params *params;

   const struct intel_device_info *devinfo;

   struct brw_codegen *p;
   struct brw_stage_prog_data * const prog_data;

   unsigned dispatch_width; /**< 8, 16 or 32 */

   brw_exec_list discard_halt_patches;
   bool debug_flag;
   const char *shader_name;
   mesa_shader_stage stage;
   void *mem_ctx;
};

void brw_prog_data_init(struct brw_stage_prog_data *prog_data,
                        const struct brw_compile_params *params);
