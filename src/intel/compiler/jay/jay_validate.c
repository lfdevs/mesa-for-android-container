/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

#ifndef NDEBUG

enum validate_block_state {
   STATE_PHI_DST,
   STATE_NORMAL,
   STATE_LATE,
};

struct validate_state {
   bool failed;
   bool post_ra;
   const char *when;
   jay_inst *I;
   jay_block *block;
   jay_function *func;
   BITSET_WORD *defs;
   enum jay_file *files;
   enum validate_block_state block_state;
};

static enum validate_block_state
block_state_for_inst(jay_inst *I)
{
   if (I->op == JAY_OPCODE_PHI_DST || I->op == JAY_OPCODE_PRELOAD) {
      return STATE_PHI_DST;
   } else if (I->op == JAY_OPCODE_PHI_SRC ||
              (jay_op_is_control_flow(I->op) &&
               I->op != JAY_OPCODE_ELSE &&
               I->op != JAY_OPCODE_HALT_TARGET)) {
      return STATE_LATE;
   } else {
      return STATE_NORMAL;
   }
}

static void
chirp(struct validate_state *validate, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);

   if (!validate->failed) {
      fprintf(stderr, "jay shader validation failed (after %s):\n",
              validate->when);
      validate->failed = true;
   }
   if (validate->I) {
      fprintf(stderr,
              "   invalid instruction in block %d: ", validate->block->index);
      jay_print_inst(stderr, validate->I);
   }
   fprintf(stderr, "   ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n\n");

   va_end(args);
}

#define CHECK(cond)                                                            \
   if (!(cond)) {                                                              \
      chirp(validate, "assertion failed at %s:%u\n   %s", __FILE__, __LINE__,  \
            #cond);                                                            \
   }

static void
validate_flagness(struct validate_state *validate,
                  jay_def def,
                  enum jay_type type,
                  const char *name)
{
   CHECK(type != JAY_TYPE_U1 || jay_is_flag(def) || jay_is_null(def));
}

static unsigned
adjust_width_for_type(unsigned width, enum jay_type type)
{
   return (width * jay_type_size_bits(type)) / 32;
}

static unsigned
get_src_words(struct validate_state *validate, jay_inst *I, unsigned s)
{
   jay_shader *shader = validate->func->shader;

   /* TODO: I think this can be simplified */
   if (I->op == JAY_OPCODE_EXPAND_QUAD) {
      return 4;
   }

   if (I->op == JAY_OPCODE_OFFSET_PACKED_PIXEL_COORDS && s == 1) {
      return 8;
   }

   if (I->op == JAY_OPCODE_ZIP_UGPR16) {
      return jay_ugpr_per_grf(shader);
   }

   if (I->op == JAY_OPCODE_DPAS) {
      const unsigned dpas_exec_size = 8 * reg_unit(shader->devinfo);
      const unsigned grf_size = shader->devinfo->grf_size;
      const unsigned acc_size_B = jay_type_size_bits(jay_dpas_acc_type(I)) / 8;

      unsigned bytes;
      switch (s) {
      case 0:
         bytes = jay_dpas_rcount(I) * dpas_exec_size * acc_size_B;
         break;
      case 1:
         bytes = jay_dpas_sdepth(I) * grf_size;
         break;
      case 2:
         bytes = jay_dpas_rcount(I) * jay_dpas_sdepth(I) * 4;
         break;
      default:
         UNREACHABLE("invalid DPAS source");
      }

      return bytes / (shader->dispatch_width * 4);
   }

   if (I->op == JAY_OPCODE_SLICE_REPACK && !jay_slice_repack_unpack(I))
      return 1 << jay_slice_repack_factor_log2(I);

   unsigned simd_width = jay_simd_width_logical(validate->func->shader, I);
   unsigned elsize = jay_type_vector_length(jay_src_type(I, s));

   if (I->src[s].file == GPR && I->dst.file == UGPR) {
      CHECK(jay_num_values(I->dst) ==
               adjust_width_for_type(simd_width, I->type) ||
            I->op == JAY_OPCODE_SEND);

      return 1;
   } else if (I->src[s].file == UGPR && jay_num_values(I->src[s]) > elsize) {
      return adjust_width_for_type(simd_width, jay_src_type(I, s));
   } else {
      return elsize;
   }
}

/*
 * Validate the fundamental invariants of static single assignment form.
 */
static void
validate_ssa(struct validate_state *validate, jay_inst *I)
{
   jay_foreach_src_index(I, src_index, _, ssa_index) {
      CHECK(BITSET_TEST(validate->defs, ssa_index) && "defs dominate uses");
      CHECK(validate->files[ssa_index] == I->src[src_index].file &&
            "consistent files");
   }

   jay_foreach_dst_index(I, d, ssa_index) {
      CHECK(!BITSET_TEST(validate->defs, ssa_index) && "single definition");
      BITSET_SET(validate->defs, ssa_index);
      validate->files[ssa_index] = d.file;
   }
}

/*
 * Validate the invariants of jay_def.
 */
static void
validate_def(struct validate_state *validate,
             jay_inst *I,
             jay_def def,
             const char *kind)
{
   CHECK(!jay_is_null(def) || !def.reg);

   if (def.collect) {
      CHECK(jay_num_values(def) >= 2);
      CHECK(def.file == GPR || def.file == UGPR);

      bool contiguous = true;
      jay_foreach_comp(def, c) {
         uint32_t index = jay_channel(def, c);
         contiguous &= index == (jay_channel(def, 0) + c);
         CHECK(index != JAY_SENTINEL);
      }

      CHECK(!contiguous);
   } else if (def.file == J_IMM) {
      CHECK(!def.reg);
      CHECK(!def.num_values_m1);
      CHECK(!def.negate);
      CHECK(!def.abs);
   } else if (def.file == ACCUM || def.file == UACCUM || def.hi) {
      CHECK(validate->post_ra);
   } else {
      CHECK(jay_base_index(def) != JAY_SENTINEL || validate->post_ra);
   }

   if (jay_is_ssa(def) && jay_channel(def, 0) != JAY_SENTINEL) {
      jay_foreach_comp(def, c) {
         CHECK(jay_channel(def, c) < validate->func->ssa_alloc);
      }
   }

   CHECK(jay_num_values(def) == 1 || !jay_is_flag(def));

   /* With some exceptions we cannot access GPRs from SIMD1 instructions */
   CHECK((jay_is_null(def) ||
          jay_is_uniform(def) ||
          def.file == FLAG ||
          jay_simd_width_logical(validate->func->shader, I) > 1) ||
         I->op == JAY_OPCODE_SHUFFLE ||
         I->op == JAY_OPCODE_VECTOR_EXTRACT ||
         I->op == JAY_OPCODE_BROADCAST_IMM);
}

/**
 * Validate an instruction.
 */
static void
validate_inst(struct validate_state *validate, jay_inst *I)
{
   validate->I = I;

   /* Block states are monotonic. */
   enum validate_block_state state = block_state_for_inst(I);
   CHECK(state >= validate->block_state);
   validate->block_state = state;

   const struct jay_opcode_info *opinfo = &jay_opcode_infos[I->op];

   validate_def(validate, I, I->dst, "dst");
   validate_def(validate, I, I->cond_flag, "cond_flag");

   jay_foreach_src(I, s) {
      validate_def(validate, I, I->src[s], "source");
   }

   if (!validate->post_ra) {
      validate_ssa(validate, I);
   }

   CHECK(I->num_srcs <= JAY_MAX_SRCS);

   validate_flagness(validate, I->dst, I->type, "destination");
   validate_flagness(validate, I->cond_flag, JAY_TYPE_U1, "cond_flag");

   /* These assumptions are baked into the definition of broadcast_flag and
    * required to ensure correctness with the lane masking.
    */
   CHECK(!I->broadcast_flag ||
         (!jay_is_null(I->cond_flag) &&
          jay_is_null(I->dst) &&
          I->cond_flag.file == UFLAG &&
          (I->op == JAY_OPCODE_CMP || I->op == JAY_OPCODE_MOV)));

   /* We cannot mix uniformness */
   CHECK(I->cond_flag.file != UFLAG || I->dst.file != GPR);
   CHECK(I->cond_flag.file != FLAG || I->dst.file != UGPR);

   /* Standard modifiers only allowed on some instructions */
   CHECK(!I->saturate || opinfo->sat);
   CHECK(!I->conditional_mod ||
         (I->op == JAY_OPCODE_CSEL || I->op == JAY_OPCODE_DEMOTE) ||
         (!jay_is_null(I->cond_flag) && opinfo->cmod));

   unsigned num_srcs = I->num_srcs;

   if (I->predication) {
      CHECK(num_srcs >= I->predication);

      if (jay_inst_has_default(I)) {
         jay_def dst = jay_is_null(I->dst) ? I->cond_flag : I->dst;
         CHECK(jay_inst_get_default(I)->file == dst.file);
      }

      CHECK(jay_is_flag(*jay_inst_get_predicate(I)));
      CHECK(!jay_is_null(*jay_inst_get_predicate(I)));

      num_srcs -= I->predication;
   }

   if (validate->post_ra) {
      CHECK(jay_simd_width_logical(validate->func->shader, I) > 0);
      CHECK(jay_simd_width_physical(validate->func->shader, I) > 0);
   }

   /* Number of sources should match for our opcode.  If opinfo->num_srcs
    * is zero, then it may actually take a variable number of sources.
    */
   CHECK(num_srcs == opinfo->num_srcs || opinfo->num_srcs == 0);

   for (unsigned s = 0; s < num_srcs; s++) {
      if (jay_is_ssa(I->src[s]) && !jay_is_null(I->src[s])) {
         unsigned expected = get_src_words(validate, I, s);
         unsigned words = jay_num_values(I->src[s]);
         if ((I->op != JAY_OPCODE_SEND || s < 2) &&
             I->op != JAY_OPCODE_VECTOR_EXTRACT) {
            CHECK(expected == words);
         }

         validate_flagness(validate, I->src[s], jay_src_type(I, s), "source");
      }

      CHECK(!I->src[s].negate || jay_has_src_mods(I, s));
   }

   if (I->op == JAY_OPCODE_DPAS) {
      CHECK(jay_num_values(I->dst) == get_src_words(validate, I, 0));
   } else if (I->op == JAY_OPCODE_SEL) {
      CHECK(jay_is_flag(I->src[2]) && "SEL src[2] (selector) must be a flag");
   } else if (I->op == JAY_OPCODE_SYNC) {
      CHECK(validate->post_ra && "SYNC does not exist while scheduling");
   } else if (I->op == JAY_OPCODE_ZIP_UGPR16) {
      CHECK(I->dst.file == GPR);
      CHECK(I->src[0].file == UGPR && I->src[1].file == UGPR);
      CHECK(jay_num_values(I->src[0]) == 16);
      CHECK(jay_num_values(I->src[1]) == 16);
      CHECK(jay_grf_per_gpr(validate->func->shader) == 2);
   } else if (I->op == JAY_OPCODE_SLICE_REPACK) {
      const bool unpack = jay_slice_repack_unpack(I);
      const unsigned pf = 1 << jay_slice_repack_factor_log2(I);
      CHECK(pf == 1 || pf == 2 || pf == 4);
      CHECK(jay_num_values(I->dst) == (unpack ? pf : 1));
   }
}

static void
jay_validate_function(struct validate_state *validate)
{
   validate->defs = BITSET_CALLOC(validate->func->ssa_alloc);
   validate->files =
      calloc(validate->func->ssa_alloc, sizeof(validate->files[0]));

   BITSET_WORD *blocks = BITSET_CALLOC(validate->func->num_blocks);
   unsigned min_block = 0;

   jay_foreach_block(validate->func, block) {
      validate->block = block;
      validate->I = NULL;

      CHECK(block->logical_succs[0] || !block->logical_succs[1]);
      CHECK(block->index < validate->func->num_blocks);

      /* Post-RA we can remove physical jumps though they exist logically */
      if (block->logical_succs[1] && !validate->post_ra) {
         CHECK(jay_block_ending_jump(block) != NULL);
      }

      /* Loop headers have a single forward edge and a single back edge. There
       * are no other back edges.
       */
      if (block->loop_header) {
         CHECK(jay_num_predecessors(block, GPR) == 2);
         CHECK(jay_num_predecessors(block, UGPR) == 2);
         jay_block **preds = jay_predecessors(block, GPR)->data;
         CHECK(BITSET_TEST(blocks, preds[0]->index));
         CHECK(!BITSET_TEST(blocks, preds[1]->index));
         CHECK(block->physical_loop_header);
      } else {
         jay_foreach_predecessor(block, pred, UGPR) {
            CHECK(BITSET_TEST(blocks, (*pred)->index));
         }
      }

      BITSET_SET(blocks, block->index);

      /* Check blocks are monotonic pre-RA (not always true post-RA) */
      if (!validate->post_ra) {
         CHECK(block->index >= min_block);
         min_block = block->index + 1;
      }

      bool uniform_phi = false;
      jay_foreach_phi_src_in_block(block, phi) {
         uniform_phi |= jay_is_uniform(phi->src[0]);
      }

      /* If a block has multiple successors, and one of them has multiple
       * predecessors, then we've detected a critical edge.
       */
      for (enum jay_file file = GPR; file <= (uniform_phi ? UGPR : GPR);
           ++file) {
         if (jay_num_successors(block, file) > 1 && !validate->post_ra) {
            jay_foreach_successor(block, succ, file) {
               if (jay_num_predecessors(succ, file) > 1) {
                  chirp(validate, "%s critical edge (B%u -> B%u)",
                        file == GPR ? "Logical" : "Physical", block->index,
                        succ->index);
               }
            }
         }
      }

      validate->block_state = 0;
      jay_foreach_inst_in_block(block, inst) {
         validate_inst(validate, inst);
      }
   }

   /* Validate that there are no dead phis. RA relies on this. */
   if (!validate->post_ra) {
      jay_foreach_block(validate->func, block) {
         jay_foreach_phi_src_in_block(block, phi) {
            CHECK(BITSET_TEST(validate->defs, jay_phi_src_index(phi)));
         }
      }
   }

   free(validate->defs);
   free(validate->files);
   free(blocks);
}

void
jay_validate(jay_shader *s, const char *when)
{
   struct validate_state validate = { .when = when, .post_ra = s->post_ra };

   jay_foreach_function(s, f) {
      validate.func = f;
      jay_validate_function(&validate);
   }

   if (validate.failed) {
      fprintf(stderr, "jay shader that failed validation:\n");
      jay_print(stderr, s);
      abort();
   }
}

#endif
