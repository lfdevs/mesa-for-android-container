/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/brw/brw_eu_defines.h"
#include "util/lut.h"
#include "util/macros.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

#define ENUM_TO_STR(x, arr)                                                    \
   ({                                                                          \
      assert(x < ARRAY_SIZE(arr));                                             \
      arr[x];                                                                  \
   })

static const char *jay_conditional_mod_str[] = {
   [JAY_CONDITIONAL_EQ] = ".eq", [JAY_CONDITIONAL_NE] = ".ne",
   [JAY_CONDITIONAL_GT] = ".gt", [JAY_CONDITIONAL_LT] = ".lt",
   [JAY_CONDITIONAL_GE] = ".ge", [JAY_CONDITIONAL_LE] = ".le",
   [JAY_CONDITIONAL_OV] = ".ov", [JAY_CONDITIONAL_NAN] = ".nan",
};

static const char *tgl_pipe_str[] = {
   [TGL_PIPE_NONE] = "",  [TGL_PIPE_FLOAT] = "F", [TGL_PIPE_INT] = "I",
   [TGL_PIPE_LONG] = "L", [TGL_PIPE_MATH] = "M",  [TGL_PIPE_SCALAR] = "S",
   [TGL_PIPE_ALL] = "A",
};

static const char *jay_arf_str[] = {
   [JAY_ARF_NULL] = "_",
   [JAY_ARF_MASK] = "mask",
   [JAY_ARF_CONTROL] = "ctrl",
   [JAY_ARF_TIMESTAMP] = "timestamp",
};

static const char *jay_file_str[JAY_FILE_LAST + 1] = {
   [GPR] = "r",       [UGPR] = "u",      [FLAG] = "f",      [UFLAG] = "uf",
   [J_ADDRESS] = "a", [ACCUM] = "acc",   [UACCUM] = "uacc", [J_ARF] = "arf",
   [MEM] = "m",       [TEST_FILE] = "t",
};

static const char *jay_base_types[] = {
   [JAY_TYPE_U] = "u", [JAY_TYPE_S] = "s", [JAY_TYPE_F] = "f", [JAY_TYPE_BF] = "bf"
};

void
jay_print_type(FILE *fp, enum jay_type t)
{
   fprintf(fp, ".%s%u", ENUM_TO_STR(jay_base_type(t), jay_base_types),
           jay_type_size_bits(t));
}

const char *
jay_file_prefix(enum jay_file file)
{
   return ENUM_TO_STR(file, jay_file_str);
}

static void
jay_print_def(FILE *fp, const jay_inst *I, int src)
{
   jay_def def = src == -2 ? I->cond_flag : src == -1 ? I->dst : I->src[src];
   unsigned len = jay_num_values(def);
   const char *file = jay_file_prefix(def.file);
   bool has_lu = jay_is_ssa(def) && !jay_is_null(def) && src >= 0;
   unsigned lu_bit = has_lu ? jay_source_last_use_bit(I->src, src) : 0;

   bool has_index = jay_channel(def, 0) != JAY_SENTINEL;
   bool has_reg = (!def.collect && def.reg && def.file != J_ARF) || !has_index;

   if (jay_is_null(def)) {
      has_reg = false;
      fprintf(fp, "_");
   } else if (def.file == J_ARF) {
      fputs(ENUM_TO_STR(jay_base_index(def), jay_arf_str), fp);
   } else if (def.collect) {
      assert(has_index && "else would be contiguous");
      fprintf(fp, "(");
      for (unsigned i = 0; i < len; ++i) {
         if (i)
            fprintf(fp, ", ");

         if (jay_channel(def, i)) {
            if (has_lu && BITSET_TEST(I->last_use, lu_bit))
               fprintf(fp, "*");

            fprintf(fp, "%s%u", file, jay_channel(def, i));
            ++lu_bit;
         } else {
            fprintf(fp, "_");
         }
      }
      fprintf(fp, ")");
   } else if (has_index) {
      fprintf(fp, "%s%s%u",
              has_lu && BITSET_TEST(I->last_use, lu_bit) ? "*" : "", file,
              jay_channel(def, 0));
      if (len > 1) {
         fprintf(fp, ":%s%u", file, jay_channel(def, len - 1));
      }
   }

   if (has_reg) {
      if (has_index)
         fprintf(fp, "(");

      fprintf(fp, "%s%u%s", file, def.reg, def.hi ? "h" : "");
      if (len > 1) {
         fprintf(fp, ":%s%u", file, def.reg + len - 1);
      }

      if (has_index)
         fprintf(fp, ")");
   }
}

static void
jay_print_src(FILE *fp, jay_inst *I, unsigned s)
{
   jay_def src = I->src[s];
   fprintf(fp, "%s%s", src.negate ? "-" : "", src.abs ? "(abs)" : "");

   if (jay_is_imm(src)) {
      fprintf(fp, "0x%X", jay_as_uint(src));
      if (util_is_probably_float(jay_as_uint(src))) {
         float f = uif(jay_as_uint(src));
         fprintf(fp, fabs(f) >= 1000000.0 ? " (%e)" : " (%f)", f);
      }
   } else {
      jay_print_def(fp, I, s);
   }
}

void
jay_print_inst(FILE *fp, jay_inst *I)
{
   const char *sep = "";

   if (!jay_is_null(I->dst)) {
      jay_print_def(fp, I, -1);
      sep = ", ";
   }

   if (!jay_is_null(I->cond_flag)) {
      fprintf(fp, "%s", sep);
      jay_print_def(fp, I, -2);
   }

   if (!jay_is_null(I->dst) || !jay_is_null(I->cond_flag)) {
      fprintf(fp, " = ");
   }

   if (I->predication) {
      fprintf(fp, "(");
      jay_print_src(fp, I, jay_inst_get_predicate(I) - I->src);

      if (jay_inst_has_default(I)) {
         fprintf(fp, "/");
         jay_print_src(fp, I, jay_inst_get_default(I) - I->src);
      }

      fprintf(fp, ")");
   }

   if (I->op == JAY_OPCODE_MATH) {
      jay_print_inst_info(fp, I, "");
   } else {
      fprintf(fp, "%s", jay_opcode_infos[I->op].name);
   }

   if (I->type != JAY_TYPE_UNTYPED) {
      jay_print_type(fp, I->type);
   }

   if (I->op == JAY_OPCODE_BFN) {
      fprintf(fp, ".(%s)", util_lut3_to_str[jay_bfn_ctrl(I)]);
   }

   const char *cmod = ENUM_TO_STR(I->conditional_mod, jay_conditional_mod_str);
   fprintf(fp, "%s%s ", I->saturate ? ".sat" : "", cmod ? cmod : "");
   sep = "";

   for (unsigned i = 0; i < I->num_srcs - I->predication; i++) {
      fprintf(fp, "%s", sep);
      jay_print_src(fp, I, i);

      enum jay_type T = jay_src_type(I, i);
      if (T != I->type && !(T == JAY_TYPE_U1 && jay_is_flag(I->src[i]))) {
         jay_print_type(fp, T);
      }

      sep = ", ";
   }

   if (I->op != JAY_OPCODE_MATH) {
      sep = jay_print_inst_info(fp, I, sep);
   }

   /* Software scoreboard dependency info */
   if (I->dep.regdist || I->dep.mode) {
      fprintf(fp, "%s%s%s", strlen(sep) ? " {" : "{",
              I->replicate_dep ? "*" : "", I->decrement_dep ? "+" : "");
      sep = "";

      if (I->dep.regdist) {
         fprintf(fp, "%s@%d", ENUM_TO_STR(I->dep.pipe, tgl_pipe_str),
                 I->dep.regdist);
         sep = " ";
      }

      if (I->dep.mode) {
         fprintf(fp, "%s$%d%s", sep, I->dep.sbid,
                 (I->dep.mode & TGL_SBID_SET ? "" :
                  I->dep.mode & TGL_SBID_DST ? ".dst" :
                                               ".src"));
      }

      fprintf(fp, "}");
   }

   fprintf(fp, "\n");
}

static inline void
indent(FILE *fp, jay_block *block, bool interior)
{
   for (unsigned i = 0; i < block->indent + interior; i++)
      fprintf(fp, "   ");
}

static void
comma_separate(FILE *fp, jay_block *block, bool *first)
{
   if (*first) {
      indent(fp, block, true);
      *first = false;
   } else {
      fprintf(fp, ", ");
   }
}

void
jay_print_block(FILE *fp, jay_block *block)
{
   indent(fp, block, false);
   fprintf(fp, "B%d%s%s", block->index, block->uniform ? " [uniform]" : "",
           block->loop_header ? " [loop header]" : "");
   bool first = true;
   jay_foreach_predecessor(block, p, GPR) {
      fprintf(fp, "%s B%d", first ? " <-" : "", (*p)->index);
      first = false;
   }
   first = true;
   jay_foreach_predecessor(block, p, UGPR) {
      fprintf(fp, "%s B%d", first ? " <=" : "", (*p)->index);
      first = false;
   }
   fprintf(fp, " {\n");

   /* We group phi destinations/sources for legibility */
   first = true;
   jay_foreach_phi_dst_in_block(block, phi) {
      comma_separate(fp, block, &first);
      jay_print_def(fp, phi, -1);
   }
   fprintf(fp, "%s", first ? "" : " = 𝜙\n");

   jay_foreach_inst_in_block(block, inst) {
      if (inst->op != JAY_OPCODE_PHI_DST && inst->op != JAY_OPCODE_PHI_SRC) {
         indent(fp, block, true);
         jay_print_inst(fp, inst);
      }
   }

   first = true;
   jay_foreach_phi_src_in_block(block, phi) {
      comma_separate(fp, block, &first);
      fprintf(fp, "𝜙%u = ", jay_phi_src_index(phi));
      jay_print_def(fp, phi, 0);
   }
   fprintf(fp, "%s", first ? "" : "\n");

   indent(fp, block, false);
   fprintf(fp, "}");
   first = true;
   jay_foreach_successor(block, succ, GPR) {
      fprintf(fp, "%s B%d", first ? " ->" : "", succ->index);
      first = false;
   }
   first = true;
   jay_foreach_successor(block, succ, UGPR) {
      fprintf(fp, "%s B%d", first ? " =>" : "", succ->index);
      first = false;
   }
   fprintf(fp, "\n\n");
}

void
jay_print_func(FILE *fp, jay_function *f)
{
   fprintf(fp, "Jay function: \n\n");
   jay_foreach_block(f, block) {
      jay_print_block(fp, block);
   }
}

void
jay_print(FILE *fp, jay_shader *s)
{
   jay_foreach_function(s, f) {
      jay_print_func(fp, f);
   }
}
