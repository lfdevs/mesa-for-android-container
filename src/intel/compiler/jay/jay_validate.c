/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

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
              (jay_op_is_control_flow(I->op) && I->op != JAY_OPCODE_ELSE)) {
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
get_src_words(struct validate_state *validate, jay_inst *I, unsigned s)
{
   if (I->op == JAY_OPCODE_EXPAND_QUAD) {
      return 4;
   }

   bool vectorized = I->dst.file == UGPR &&
                     jay_num_values(I->dst) > jay_type_vector_length(I->type) &&
                     I->op != JAY_OPCODE_SEND &&
                     jay_num_values(I->src[s]) > 1;

   unsigned elsize = jay_type_vector_length(jay_src_type(I, s));
   unsigned words = elsize * (vectorized ? jay_num_values(I->dst) : 1);

   if (vectorized && I->src[s].file == GPR) {
      CHECK(words == validate->func->shader->dispatch_width);
      return 1;
   } else {
      return words;
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
validate_def(struct validate_state *validate, jay_def def, const char *kind)
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

   validate_def(validate, I->dst, "dst");
   validate_def(validate, I->cond_flag, "cond_flag");

   jay_foreach_src(I, s) {
      validate_def(validate, I->src[s], "source");
   }

   if (!validate->post_ra) {
      validate_ssa(validate, I);
   }

   CHECK(I->num_srcs <= JAY_MAX_SRCS);

   validate_flagness(validate, I->dst, I->type, "destination");
   validate_flagness(validate, I->cond_flag, JAY_TYPE_U1, "cond_flag");

   CHECK(!I->conditional_mod ||
         !jay_is_null(I->cond_flag) ||
         I->op == JAY_OPCODE_CSEL);

   /* These assumptions are baked into the definition of broadcast_flag and
    * required to ensure correctness with the lane masking.
    */
   CHECK(!I->broadcast_flag ||
         (!jay_is_null(I->cond_flag) &&
          jay_is_null(I->dst) &&
          I->cond_flag.file == FLAG &&
          (I->op == JAY_OPCODE_CMP || I->op == JAY_OPCODE_MOV)));

   /* Standard modifiers only allowed on some instructions */
   CHECK(!I->conditional_mod || opinfo->cmod || I->op == JAY_OPCODE_CSEL);
   CHECK(!I->saturate || opinfo->sat);

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
         if (I->op != JAY_OPCODE_SEND || s < 2) {
            CHECK(expected == words);
         }

         validate_flagness(validate, I->src[s], jay_src_type(I, s), "source");
      }

      CHECK(!I->src[s].negate || jay_has_src_mods(I, s));
   }

   if (I->op == JAY_OPCODE_SEL) {
      CHECK(jay_is_flag(I->src[2]) && "SEL src[2] (selector) must be a flag");
   }
}

static void
jay_validate_function(struct validate_state *validate)
{
   validate->defs = BITSET_CALLOC(validate->func->ssa_alloc);
   validate->files =
      calloc(validate->func->ssa_alloc, sizeof(validate->files[0]));

   jay_foreach_block(validate->func, block) {
      validate->block = block;
      validate->I = NULL;

      CHECK(block->successors[0] || !block->successors[1]);

      /* Post-RA we can remove physical jumps though they exist logically */
      if (block->successors[1] && !validate->post_ra) {
         CHECK(jay_block_ending_jump(block) != NULL);
      }

      /* If a block has multiple successors, and one of them has multiple
       * predecessors, then we've detected a critical edge.
       */
      if (jay_num_successors(block) > 1 && !validate->post_ra) {
         jay_foreach_successor(block, succ) {
            if (jay_num_predecessors(succ) > 1) {
               chirp(validate, "Critical edge (B%u -> B%u) is not allowed",
                     block->index, succ->index);
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
