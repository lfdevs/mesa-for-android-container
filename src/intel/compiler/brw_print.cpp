/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_cfg.h"
#include "brw_disasm.h"
#include "brw_fs.h"
#include "brw_private.h"
#include "dev/intel_debug.h"
#include "util/half_float.h"

using namespace brw;

void
brw_print_instructions_to_file(const fs_visitor &s, FILE *file)
{
   if (s.cfg && s.grf_used == 0) {
      const brw::def_analysis &defs = s.def_analysis.require();
      const register_pressure *rp =
         INTEL_DEBUG(DEBUG_REG_PRESSURE) ? &s.regpressure_analysis.require() : NULL;

      unsigned ip = 0, max_pressure = 0;
      unsigned cf_count = 0;
      foreach_block(block, s.cfg) {
         fprintf(file, "START B%d", block->num);
         foreach_list_typed(bblock_link, link, link, &block->parents) {
            fprintf(file, " <%cB%d",
                    link->kind == bblock_link_logical ? '-' : '~',
                    link->block->num);
         }
         fprintf(file, "\n");

         foreach_inst_in_block(fs_inst, inst, block) {
            if (inst->is_control_flow_end())
               cf_count -= 1;

            if (rp) {
               max_pressure = MAX2(max_pressure, rp->regs_live_at_ip[ip]);
               fprintf(file, "{%3d} ", rp->regs_live_at_ip[ip]);
            }

            for (unsigned i = 0; i < cf_count; i++)
               fprintf(file, "  ");
            brw_print_instruction(s, inst, file, &defs);
            ip++;

            if (inst->is_control_flow_begin())
               cf_count += 1;
         }

         fprintf(file, "END B%d", block->num);
         foreach_list_typed(bblock_link, link, link, &block->children) {
            fprintf(file, " %c>B%d",
                    link->kind == bblock_link_logical ? '-' : '~',
                    link->block->num);
         }
         fprintf(file, "\n");
      }
      if (rp)
         fprintf(file, "Maximum %3d registers live at once.\n", max_pressure);
   } else if (s.cfg && exec_list_is_empty(&s.instructions)) {
      foreach_block_and_inst(block, fs_inst, inst, s.cfg) {
         brw_print_instruction(s, inst, file);
      }
   } else {
      foreach_in_list(fs_inst, inst, &s.instructions) {
         brw_print_instruction(s, inst, file);
      }
   }
}

void
brw_print_instructions(const fs_visitor &s, const char *name)
{
   FILE *file = stderr;
   if (name && __normal_user()) {
      file = fopen(name, "w");
      if (!file)
         file = stderr;
   }

   brw_print_instructions_to_file(s, file);

   if (file != stderr) {
      fclose(file);
   }
}

static const char *
brw_instruction_name(const struct brw_isa_info *isa, enum opcode op)
{
   const struct intel_device_info *devinfo = isa->devinfo;

   switch (op) {
   case 0 ... NUM_BRW_OPCODES - 1:
      /* The DO instruction doesn't exist on Gfx9+, but we use it to mark the
       * start of a loop in the IR.
       */
      if (op == BRW_OPCODE_DO)
         return "do";

      /* DPAS instructions may transiently exist on platforms that do not
       * support DPAS. They will eventually be lowered, but in the meantime it
       * must be possible to query the instruction name.
       */
      if (devinfo->verx10 < 125 && op == BRW_OPCODE_DPAS)
         return "dpas";

      assert(brw_opcode_desc(isa, op)->name);
      return brw_opcode_desc(isa, op)->name;
   case FS_OPCODE_FB_WRITE_LOGICAL:
      return "fb_write_logical";
   case FS_OPCODE_FB_READ_LOGICAL:
      return "fb_read_logical";

   case SHADER_OPCODE_RCP:
      return "rcp";
   case SHADER_OPCODE_RSQ:
      return "rsq";
   case SHADER_OPCODE_SQRT:
      return "sqrt";
   case SHADER_OPCODE_EXP2:
      return "exp2";
   case SHADER_OPCODE_LOG2:
      return "log2";
   case SHADER_OPCODE_POW:
      return "pow";
   case SHADER_OPCODE_INT_QUOTIENT:
      return "int_quot";
   case SHADER_OPCODE_INT_REMAINDER:
      return "int_rem";
   case SHADER_OPCODE_SIN:
      return "sin";
   case SHADER_OPCODE_COS:
      return "cos";

   case SHADER_OPCODE_SEND:
      return "send";

   case SHADER_OPCODE_UNDEF:
      return "undef";

   case SHADER_OPCODE_TEX_LOGICAL:
      return "tex_logical";
   case SHADER_OPCODE_TXD_LOGICAL:
      return "txd_logical";
   case SHADER_OPCODE_TXF_LOGICAL:
      return "txf_logical";
   case SHADER_OPCODE_TXL_LOGICAL:
      return "txl_logical";
   case SHADER_OPCODE_TXS_LOGICAL:
      return "txs_logical";
   case FS_OPCODE_TXB_LOGICAL:
      return "txb_logical";
   case SHADER_OPCODE_TXF_CMS_W_LOGICAL:
      return "txf_cms_w_logical";
   case SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL:
      return "txf_cms_w_gfx12_logical";
   case SHADER_OPCODE_TXF_MCS_LOGICAL:
      return "txf_mcs_logical";
   case SHADER_OPCODE_LOD_LOGICAL:
      return "lod_logical";
   case SHADER_OPCODE_TG4_LOGICAL:
      return "tg4_logical";
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
      return "tg4_offset_logical";
   case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
      return "tg4_offset_lod_logical";
   case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
      return "tg4_offset_bias_logical";
   case SHADER_OPCODE_TG4_BIAS_LOGICAL:
      return "tg4_b_logical";
   case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
      return "tg4_l_logical";
   case SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL:
      return "tg4_i_logical";
   case SHADER_OPCODE_SAMPLEINFO_LOGICAL:
      return "sampleinfo_logical";

   case SHADER_OPCODE_IMAGE_SIZE_LOGICAL:
      return "image_size_logical";

   case SHADER_OPCODE_MEMORY_FENCE:
      return "memory_fence";
   case FS_OPCODE_SCHEDULING_FENCE:
      return "scheduling_fence";
   case SHADER_OPCODE_INTERLOCK:
      /* For an interlock we actually issue a memory fence via sendc. */
      return "interlock";

   case SHADER_OPCODE_LOAD_PAYLOAD:
      return "load_payload";
   case FS_OPCODE_PACK:
      return "pack";

   case SHADER_OPCODE_SCRATCH_HEADER:
      return "scratch_header";

   case SHADER_OPCODE_URB_WRITE_LOGICAL:
      return "urb_write_logical";
   case SHADER_OPCODE_URB_READ_LOGICAL:
      return "urb_read_logical";

   case SHADER_OPCODE_FIND_LIVE_CHANNEL:
      return "find_live_channel";
   case SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL:
      return "find_last_live_channel";
   case SHADER_OPCODE_LOAD_LIVE_CHANNELS:
      return "load_live_channels";
   case FS_OPCODE_LOAD_LIVE_CHANNELS:
      return "fs_load_live_channels";

   case SHADER_OPCODE_BROADCAST:
      return "broadcast";
   case SHADER_OPCODE_SHUFFLE:
      return "shuffle";
   case SHADER_OPCODE_SEL_EXEC:
      return "sel_exec";
   case SHADER_OPCODE_QUAD_SWIZZLE:
      return "quad_swizzle";
   case SHADER_OPCODE_CLUSTER_BROADCAST:
      return "cluster_broadcast";

   case SHADER_OPCODE_GET_BUFFER_SIZE:
      return "get_buffer_size";

   case FS_OPCODE_DDX_COARSE:
      return "ddx_coarse";
   case FS_OPCODE_DDX_FINE:
      return "ddx_fine";
   case FS_OPCODE_DDY_COARSE:
      return "ddy_coarse";
   case FS_OPCODE_DDY_FINE:
      return "ddy_fine";

   case FS_OPCODE_PIXEL_X:
      return "pixel_x";
   case FS_OPCODE_PIXEL_Y:
      return "pixel_y";

   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
      return "uniform_pull_const";
   case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL:
      return "varying_pull_const_logical";

   case FS_OPCODE_PACK_HALF_2x16_SPLIT:
      return "pack_half_2x16_split";

   case SHADER_OPCODE_HALT_TARGET:
      return "halt_target";

   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
      return "interp_sample";
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
      return "interp_shared_offset";
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      return "interp_per_slot_offset";

   case SHADER_OPCODE_BARRIER:
      return "barrier";
   case SHADER_OPCODE_MULH:
      return "mulh";
   case SHADER_OPCODE_ISUB_SAT:
      return "isub_sat";
   case SHADER_OPCODE_USUB_SAT:
      return "usub_sat";
   case SHADER_OPCODE_MOV_INDIRECT:
      return "mov_indirect";
   case SHADER_OPCODE_MOV_RELOC_IMM:
      return "mov_reloc_imm";

   case RT_OPCODE_TRACE_RAY_LOGICAL:
      return "rt_trace_ray_logical";

   case SHADER_OPCODE_RND_MODE:
      return "rnd_mode";
   case SHADER_OPCODE_FLOAT_CONTROL_MODE:
      return "float_control_mode";
   case SHADER_OPCODE_BTD_SPAWN_LOGICAL:
      return "btd_spawn_logical";
   case SHADER_OPCODE_BTD_RETIRE_LOGICAL:
      return "btd_retire_logical";
   case SHADER_OPCODE_READ_ARCH_REG:
      return "read_arch_reg";
   case SHADER_OPCODE_LOAD_SUBGROUP_INVOCATION:
      return "load_subgroup_invocation";
   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
      return "memory_load";
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
      return "memory_store";
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
      return "memory_atomic";
   case SHADER_OPCODE_REDUCE:
      return "reduce";
   case SHADER_OPCODE_INCLUSIVE_SCAN:
      return "inclusive_scan";
   case SHADER_OPCODE_EXCLUSIVE_SCAN:
      return "exclusive_scan";
   case SHADER_OPCODE_VOTE_ANY:
      return "vote_any";
   case SHADER_OPCODE_VOTE_ALL:
      return "vote_all";
   case SHADER_OPCODE_VOTE_EQUAL:
      return "vote_equal";
   }

   unreachable("not reached");
}

/**
 * Pretty-print a source for a SHADER_OPCODE_MEMORY_LOGICAL instruction.
 *
 * Returns true if the value is fully printed (i.e. an enum) and false if
 * we only printed a label, and the actual source value still needs printing.
 */
static bool
print_memory_logical_source(FILE *file, const fs_inst *inst, unsigned i)
{
   if (inst->is_control_source(i)) {
      assert(inst->src[i].file == IMM && inst->src[i].type == BRW_TYPE_UD);
      assert(!inst->src[i].negate);
      assert(!inst->src[i].abs);
   }

   switch (i) {
   case MEMORY_LOGICAL_OPCODE:
      fprintf(file, " %s", brw_lsc_op_to_string(inst->src[i].ud));
      return true;
   case MEMORY_LOGICAL_MODE: {
      static const char *modes[] = {
         [MEMORY_MODE_TYPED]        = "typed",
         [MEMORY_MODE_UNTYPED]      = "untyped",
         [MEMORY_MODE_SHARED_LOCAL] = "shared",
         [MEMORY_MODE_SCRATCH]      = "scratch",
      };
      assert(inst->src[i].ud < ARRAY_SIZE(modes));
      fprintf(file, " %s", modes[inst->src[i].ud]);
      return true;
   }
   case MEMORY_LOGICAL_BINDING_TYPE:
      fprintf(file, " %s", brw_lsc_addr_surftype_to_string(inst->src[i].ud));
      if (inst->src[i].ud != LSC_ADDR_SURFTYPE_FLAT)
         fprintf(file, ":");
      return true;
   case MEMORY_LOGICAL_BINDING:
      return inst->src[i].file == BAD_FILE;
   case MEMORY_LOGICAL_ADDRESS:
      fprintf(file, " addr: ");
      return false;
   case MEMORY_LOGICAL_COORD_COMPONENTS:
      fprintf(file, " coord_comps:");
      return false;
   case MEMORY_LOGICAL_ALIGNMENT:
      fprintf(file, " align:");
      return false;
   case MEMORY_LOGICAL_DATA_SIZE:
      fprintf(file, " %s", brw_lsc_data_size_to_string(inst->src[i].ud));
      return true;
   case MEMORY_LOGICAL_COMPONENTS:
      fprintf(file, " comps:");
      return false;
   case MEMORY_LOGICAL_FLAGS:
      if (inst->src[i].ud & MEMORY_FLAG_TRANSPOSE)
         fprintf(file, " transpose");
      if (inst->src[i].ud & MEMORY_FLAG_INCLUDE_HELPERS)
         fprintf(file, " helpers");
      return true;
   case MEMORY_LOGICAL_DATA0:
      fprintf(file, " data0: ");
      return false;
   case MEMORY_LOGICAL_DATA1:
      if (inst->src[i].file == BAD_FILE)
         return true;
      fprintf(file, " data1: ");
      return false;
   default:
      unreachable("invalid source");
   }
}

void
brw_print_instruction_to_file(const fs_visitor &s, const fs_inst *inst, FILE *file, const brw::def_analysis *defs)
{
   if (inst->predicate) {
      fprintf(file, "(%cf%d.%d) ",
              inst->predicate_inverse ? '-' : '+',
              inst->flag_subreg / 2,
              inst->flag_subreg % 2);
   }

   fprintf(file, "%s", brw_instruction_name(&s.compiler->isa, inst->opcode));
   if (inst->saturate)
      fprintf(file, ".sat");
   if (inst->conditional_mod) {
      fprintf(file, "%s", conditional_modifier[inst->conditional_mod]);
      if (!inst->predicate &&
          (inst->opcode != BRW_OPCODE_SEL &&
           inst->opcode != BRW_OPCODE_CSEL &&
           inst->opcode != BRW_OPCODE_IF &&
           inst->opcode != BRW_OPCODE_WHILE)) {
         fprintf(file, ".f%d.%d", inst->flag_subreg / 2,
                 inst->flag_subreg % 2);
      }
   }
   fprintf(file, "(%d) ", inst->exec_size);

   if (inst->mlen) {
      fprintf(file, "(mlen: %d) ", inst->mlen);
   }

   if (inst->ex_mlen) {
      fprintf(file, "(ex_mlen: %d) ", inst->ex_mlen);
   }

   if (inst->eot) {
      fprintf(file, "(EOT) ");
   }

   switch (inst->dst.file) {
   case VGRF:
      if (defs && defs->get(inst->dst))
         fprintf(file, "%%%d", inst->dst.nr);
      else
         fprintf(file, "v%d", inst->dst.nr);
      break;
   case FIXED_GRF:
      fprintf(file, "g%d", inst->dst.nr);
      if (inst->dst.subnr != 0)
         fprintf(file, ".%d", inst->dst.subnr / brw_type_size_bytes(inst->dst.type));
      break;
   case BAD_FILE:
      fprintf(file, "(null)");
      break;
   case UNIFORM:
      fprintf(file, "***u%d***", inst->dst.nr);
      break;
   case ATTR:
      fprintf(file, "***attr%d***", inst->dst.nr);
      break;
   case ARF:
      switch (inst->dst.nr & 0xF0) {
      case BRW_ARF_NULL:
         fprintf(file, "null");
         break;
      case BRW_ARF_ADDRESS:
         fprintf(file, "a0.%d", inst->dst.subnr);
         break;
      case BRW_ARF_ACCUMULATOR:
         if (inst->dst.subnr == 0)
            fprintf(file, "acc%d", inst->dst.nr & 0x0F);
         else
            fprintf(file, "acc%d.%d", inst->dst.nr & 0x0F, inst->dst.subnr);

         break;
      case BRW_ARF_FLAG:
         fprintf(file, "f%d.%d", inst->dst.nr & 0xf, inst->dst.subnr);
         break;
      default:
         fprintf(file, "arf%d.%d", inst->dst.nr & 0xf, inst->dst.subnr);
         break;
      }
      break;
   case IMM:
      unreachable("not reached");
   }

   if (inst->dst.offset ||
       (!s.grf_used && inst->dst.file == VGRF &&
        s.alloc.sizes[inst->dst.nr] * REG_SIZE != inst->size_written)) {
      const unsigned reg_size = (inst->dst.file == UNIFORM ? 4 : REG_SIZE);
      fprintf(file, "+%d.%d", inst->dst.offset / reg_size,
              inst->dst.offset % reg_size);
   }

   if (inst->dst.stride != 1)
      fprintf(file, "<%u>", inst->dst.stride);
   fprintf(file, ":%s", brw_reg_type_to_letters(inst->dst.type));

   for (int i = 0; i < inst->sources; i++) {
      if (inst->opcode == SHADER_OPCODE_MEMORY_LOAD_LOGICAL ||
          inst->opcode == SHADER_OPCODE_MEMORY_STORE_LOGICAL ||
          inst->opcode == SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL) {
         if (print_memory_logical_source(file, inst, i))
            continue;
      } else {
         fprintf(file, ", ");
      }

      if (inst->src[i].negate)
         fprintf(file, "-");
      if (inst->src[i].abs)
         fprintf(file, "|");
      switch (inst->src[i].file) {
      case VGRF:
         if (defs && defs->get(inst->src[i]))
            fprintf(file, "%%%d", inst->src[i].nr);
         else
            fprintf(file, "v%d", inst->src[i].nr);
         break;
      case FIXED_GRF:
         fprintf(file, "g%d", inst->src[i].nr);
         break;
      case ATTR:
         fprintf(file, "attr%d", inst->src[i].nr);
         break;
      case UNIFORM:
         fprintf(file, "u%d", inst->src[i].nr);
         break;
      case BAD_FILE:
         fprintf(file, "(null)");
         break;
      case IMM:
         switch (inst->src[i].type) {
         case BRW_TYPE_HF:
            fprintf(file, "%-ghf", _mesa_half_to_float(inst->src[i].ud & 0xffff));
            break;
         case BRW_TYPE_F:
            fprintf(file, "%-gf", inst->src[i].f);
            break;
         case BRW_TYPE_DF:
            fprintf(file, "%fdf", inst->src[i].df);
            break;
         case BRW_TYPE_W:
            fprintf(file, "%dw", (int)(int16_t)inst->src[i].d);
            break;
         case BRW_TYPE_D:
            fprintf(file, "%dd", inst->src[i].d);
            break;
         case BRW_TYPE_UW:
            fprintf(file, "%duw", inst->src[i].ud & 0xffff);
            break;
         case BRW_TYPE_UD:
            fprintf(file, "%uu", inst->src[i].ud);
            break;
         case BRW_TYPE_Q:
            fprintf(file, "%" PRId64 "q", inst->src[i].d64);
            break;
         case BRW_TYPE_UQ:
            fprintf(file, "%" PRIu64 "uq", inst->src[i].u64);
            break;
         case BRW_TYPE_VF:
            fprintf(file, "[%-gF, %-gF, %-gF, %-gF]",
                    brw_vf_to_float((inst->src[i].ud >>  0) & 0xff),
                    brw_vf_to_float((inst->src[i].ud >>  8) & 0xff),
                    brw_vf_to_float((inst->src[i].ud >> 16) & 0xff),
                    brw_vf_to_float((inst->src[i].ud >> 24) & 0xff));
            break;
         case BRW_TYPE_V:
         case BRW_TYPE_UV:
            fprintf(file, "%08x%s", inst->src[i].ud,
                    inst->src[i].type == BRW_TYPE_V ? "V" : "UV");
            break;
         default:
            fprintf(file, "???");
            break;
         }
         break;
      case ARF:
         switch (inst->src[i].nr & 0xF0) {
         case BRW_ARF_NULL:
            fprintf(file, "null");
            break;
         case BRW_ARF_ADDRESS:
            fprintf(file, "a0.%d", inst->src[i].subnr);
            break;
         case BRW_ARF_ACCUMULATOR:
            if (inst->src[i].subnr == 0)
               fprintf(file, "acc%d", inst->src[i].nr & 0x0F);
            else
               fprintf(file, "acc%d.%d", inst->src[i].nr & 0x0F, inst->src[i].subnr);

            break;
         case BRW_ARF_FLAG:
            fprintf(file, "f%d.%d", inst->src[i].nr & 0xf, inst->src[i].subnr);
            break;
         default:
            fprintf(file, "arf%d.%d", inst->src[i].nr & 0xf, inst->src[i].subnr);
            break;
         }
         break;
      }

      if (inst->src[i].file == FIXED_GRF && inst->src[i].subnr != 0) {
         assert(inst->src[i].offset == 0);

         fprintf(file, ".%d", inst->src[i].subnr / brw_type_size_bytes(inst->src[i].type));
      } else if (inst->src[i].offset ||
          (!s.grf_used && inst->src[i].file == VGRF &&
           s.alloc.sizes[inst->src[i].nr] * REG_SIZE != inst->size_read(i))) {
         const unsigned reg_size = (inst->src[i].file == UNIFORM ? 4 : REG_SIZE);
         fprintf(file, "+%d.%d", inst->src[i].offset / reg_size,
                 inst->src[i].offset % reg_size);
      }

      if (inst->src[i].abs)
         fprintf(file, "|");

      if (inst->src[i].file != IMM) {
         unsigned stride;
         if (inst->src[i].file == ARF || inst->src[i].file == FIXED_GRF) {
            unsigned hstride = inst->src[i].hstride;
            stride = (hstride == 0 ? 0 : (1 << (hstride - 1)));
         } else {
            stride = inst->src[i].stride;
         }
         if (stride != 1)
            fprintf(file, "<%u>", stride);

         fprintf(file, ":%s", brw_reg_type_to_letters(inst->src[i].type));
      }
   }

   fprintf(file, " ");

   if (inst->force_writemask_all)
      fprintf(file, "NoMask ");

   if (inst->exec_size != s.dispatch_width)
      fprintf(file, "group%d ", inst->group);

   if (inst->has_no_mask_send_params)
      fprintf(file, "NoMaskParams ");

   if (inst->sched.pipe != TGL_PIPE_NONE) {
      fprintf(file, "{ ");
      brw_print_swsb(file, s.devinfo, inst->sched);
      fprintf(file, " } ");
   }

   fprintf(file, "\n");
}


void
brw_print_swsb(FILE *f, const struct intel_device_info *devinfo, const tgl_swsb swsb)
{
   if (swsb.pipe == TGL_PIPE_NONE)
      return;

   if (swsb.regdist) {
      fprintf(f, "%s@%d",
              (devinfo && devinfo->verx10 < 125 ? "" :
               swsb.pipe == TGL_PIPE_FLOAT ? "F" :
               swsb.pipe == TGL_PIPE_INT ? "I" :
               swsb.pipe == TGL_PIPE_LONG ? "L" :
               swsb.pipe == TGL_PIPE_ALL ? "A"  :
               swsb.pipe == TGL_PIPE_MATH ? "M" : "" ),
              swsb.regdist);
   }

   if (swsb.mode) {
      if (swsb.regdist)
          fprintf(f, " ");

      fprintf(f, "$%d%s", swsb.sbid,
              (swsb.mode & TGL_SBID_SET ? "" :
               swsb.mode & TGL_SBID_DST ? ".dst" : ".src"));
   }
}

