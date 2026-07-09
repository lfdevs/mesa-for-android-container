/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdbool.h>

/*
 * QRisc disasm / asm helpers
 */

unsigned qrisc_control_reg(const char *name);
char * qrisc_control_reg_name(unsigned id);

unsigned qrisc_sqe_reg(const char *name);
char * qrisc_sqe_reg_name(unsigned id);

unsigned qrisc_pipe_reg(const char *name);
char * qrisc_pipe_reg_name(unsigned id);
bool qrisc_pipe_reg_is_void(unsigned id);

unsigned qrisc_gpu_reg(const char *name);
char * qrisc_gpu_reg_name(unsigned id);

unsigned qrisc_gpr_reg(const char *name);

int qrisc_pm4_id(const char *name);
const char * qrisc_pm_id_name(unsigned id);

enum qrisc_color {
   QRISC_ERR,
   QRISC_LBL,
};

void qrisc_printc(enum qrisc_color c, const char *fmt, ...);

enum qrisc_fwid {
   QRISC_GEN80000 = 0x500,
   QRISC_GEN80100 = 0x700,
   QRISC_GEN80200 = 0x510,

   QRISC_A730 = 0x730,
   QRISC_A740 = 0x740,
   QRISC_GEN70500 = 0x512,
   QRISC_A750 = 0x520,

   QRISC_A630 = 0x6ee,
   QRISC_A650 = 0x6dc,
   QRISC_A660 = 0x6dd,

   QRISC_A530 = 0x5ff,
};

static inline enum qrisc_fwid
qrisc_get_fwid(uint32_t first_dword)
{
   /* The firmware ID is in bits 12-24 of the first dword */
   return (first_dword >> 12) & 0xfff;
}

int qrisc_util_init(enum qrisc_fwid fw_id, int *gpuver, bool colors);

#endif /* _UTIL_H_ */
