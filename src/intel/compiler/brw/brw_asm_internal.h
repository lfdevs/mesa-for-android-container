/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Assembler internal state and definitions used by the brw_gram/brw_lex. */

#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>

#include "brw_reg.h"
#include "brw_reg_type.h"
#include "brw_eu_defines.h"
#include "brw_eu_inst.h"
#include "brw_eu.h"
#include "dev/intel_device_info.h"
#include "util/list.h"

/* glibc < 2.27 defines OVERFLOW in /usr/include/math.h. */
#undef OVERFLOW

int yyparse(void);
int yylex(void);
char *lex_text(void);

extern struct brw_codegen *p;
extern int errors;
extern bool compaction_warning_given;
extern const char *input_filename;

struct condition {
   unsigned cond_modifier:4;
   unsigned flag_reg_nr:1;
   unsigned flag_subreg_nr:1;
};

struct predicate {
   unsigned pred_control:4;
   unsigned pred_inv:1;
   unsigned flag_reg_nr:1;
   unsigned flag_subreg_nr:1;
};

enum instoption_type {
   INSTOPTION_FLAG,
   INSTOPTION_DEP_INFO,
   INSTOPTION_CHAN_OFFSET,
};

struct instoption {
   enum instoption_type type;
   union {
      unsigned uint_value;
      struct tgl_swsb depinfo_value;
   };
};

struct options {
   uint8_t chan_offset;
   unsigned access_mode:1;
   unsigned compression_control:2;
   unsigned thread_control:2;
   unsigned branch_control:1;
   unsigned no_dd_check:1; // Dependency control
   unsigned no_dd_clear:1; // Dependency control
   unsigned mask_control:1;
   unsigned debug_control:1;
   unsigned acc_wr_control:1;
   unsigned end_of_thread:1;
   unsigned compaction:1;
   unsigned is_compr:1;
   struct tgl_swsb depinfo;
};

struct msgdesc {
   unsigned ex_bso:1;
   unsigned src1_len:5;
};

void brw_asm_label_set(const char *name);
void brw_asm_label_use_jip(const char *name);
void brw_asm_label_use_uip(const char *name);
