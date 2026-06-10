/*
 * Copyright © 2006 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Intel funded Tungsten Graphics to develop this 3D driver.
 * File originally authored by: Keith Whitwell <keithw@vmware.com>
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>

#include "brw_eu_defines.h"
#include "brw_eu.h"
#include "brw_private.h"
#include "intel_gfx_ver_enum.h"
#include "dev/intel_debug.h"

#include "util/u_debug.h"
#include "util/ralloc.h"

/* Returns a conditional modifier that negates the condition. */
enum brw_conditional_mod
brw_negate_cmod(enum brw_conditional_mod cmod)
{
   switch (cmod) {
   case BRW_CONDITIONAL_Z:
      return BRW_CONDITIONAL_NZ;
   case BRW_CONDITIONAL_NZ:
      return BRW_CONDITIONAL_Z;
   case BRW_CONDITIONAL_G:
      return BRW_CONDITIONAL_LE;
   case BRW_CONDITIONAL_GE:
      return BRW_CONDITIONAL_L;
   case BRW_CONDITIONAL_L:
      return BRW_CONDITIONAL_GE;
   case BRW_CONDITIONAL_LE:
      return BRW_CONDITIONAL_G;
   default:
      UNREACHABLE("Can't negate this cmod");
   }
}

/* Returns the corresponding conditional mod for swapping src0 and
 * src1 in e.g. CMP.
 */
enum brw_conditional_mod
brw_swap_cmod(enum brw_conditional_mod cmod)
{
   switch (cmod) {
   case BRW_CONDITIONAL_Z:
   case BRW_CONDITIONAL_NZ:
      return cmod;
   case BRW_CONDITIONAL_G:
      return BRW_CONDITIONAL_L;
   case BRW_CONDITIONAL_GE:
      return BRW_CONDITIONAL_LE;
   case BRW_CONDITIONAL_L:
      return BRW_CONDITIONAL_G;
   case BRW_CONDITIONAL_LE:
      return BRW_CONDITIONAL_GE;
   default:
      return BRW_CONDITIONAL_NONE;
   }
}

/**
 * Get the least significant bit offset of the i+1-th component of immediate
 * type \p type.  For \p i equal to the two's complement of j, return the
 * offset of the j-th component starting from the end of the vector.  For
 * scalar register types return zero.
 */
static unsigned
imm_shift(enum brw_reg_type type, unsigned i)
{
   assert(type != BRW_TYPE_UV && type != BRW_TYPE_V &&
          "Not implemented.");

   if (type == BRW_TYPE_VF)
      return 8 * (i & 3);
   else
      return 0;
}

/**
 * Swizzle an arbitrary immediate \p x of the given type according to the
 * permutation specified as \p swz.
 */
uint32_t
brw_swizzle_immediate(enum brw_reg_type type, uint32_t x, unsigned swz)
{
   if (imm_shift(type, 1)) {
      const unsigned n = 32 / imm_shift(type, 1);
      uint32_t y = 0;

      for (unsigned i = 0; i < n; i++) {
         /* Shift the specified component all the way to the right and left to
          * discard any undesired L/MSBs, then shift it right into component i.
          */
         y |= x >> imm_shift(type, (i & ~3) + BRW_GET_SWZ(swz, i & 3))
                << imm_shift(type, ~0u)
                >> imm_shift(type, ~0u - i);
      }

      return y;
   } else {
      return x;
   }
}

DEBUG_GET_ONCE_OPTION(shader_bin_dump_path, "INTEL_SHADER_BIN_DUMP_PATH", NULL);

bool brw_should_dump_shader_bin(void)
{
   return debug_get_option_shader_bin_dump_path() != NULL;
}

void brw_dump_shader_bin(void *assembly, int start_offset, int end_offset,
                         const char *identifier)
{
   char *name = ralloc_asprintf(NULL, "%s/%s.bin",
                                debug_get_option_shader_bin_dump_path(),
                                identifier);

   int fd = open(name, O_CREAT | O_WRONLY | O_TRUNC, 0644);
   ralloc_free(name);

   if (fd < 0)
      return;

   struct stat sb;
   if (fstat(fd, &sb) != 0 || (!S_ISREG(sb.st_mode))) {
      close(fd);
      return;
   }

   size_t to_write = end_offset - start_offset;
   void *write_ptr = assembly + start_offset;

   while (to_write) {
      ssize_t ret = write(fd, write_ptr, to_write);

      if (ret <= 0) {
         close(fd);
         return;
      }

      to_write -= ret;
      write_ptr += ret;
   }

   close(fd);
}

static const struct opcode_desc opcode_descs[] = {
   /* IR,                 HW,  name,      nsrc, ndst, gfx_vers assuming Gfx9+ */
   { BRW_OPCODE_ILLEGAL,  0,   "illegal", 0,    0,    GFX_ALL },
   { BRW_OPCODE_SYNC,     1,   "sync",    1,    0,    GFX_GE(GFX12) },
   { BRW_OPCODE_MOV,      1,   "mov",     1,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_MOV,      97,  "mov",     1,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_SEL,      2,   "sel",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SEL,      98,  "sel",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_MOVI,     3,   "movi",    2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_MOVI,     99,  "movi",    2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_NOT,      4,   "not",     1,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_NOT,      100, "not",     1,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_AND,      5,   "and",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_AND,      101, "and",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_OR,       6,   "or",      2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_OR,       102, "or",      2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_XOR,      7,   "xor",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_XOR,      103, "xor",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_BFN,      107, "bfn",     3,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_SHR,      8,   "shr",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SHR,      104, "shr",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_SHL,      9,   "shl",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SHL,      105, "shl",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_SMOV,     10,  "smov",    0,    0,    GFX_LT(GFX12) },
   { BRW_OPCODE_SMOV,     106, "smov",    0,    0,    GFX_GE(GFX12) },
   { BRW_OPCODE_ASR,      12,  "asr",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_ASR,      108, "asr",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_ROR,      14,  "ror",     2,    1,    GFX11 },
   { BRW_OPCODE_ROR,      110, "ror",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_ROL,      15,  "rol",     2,    1,    GFX11 },
   { BRW_OPCODE_ROL,      111, "rol",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_CMP,      16,  "cmp",     2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_CMP,      112, "cmp",     2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_CMPN,     17,  "cmpn",    2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_CMPN,     113, "cmpn",    2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_CSEL,     18,  "csel",    3,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_CSEL,     114, "csel",    3,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_BFREV,    23,  "bfrev",   1,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_BFREV,    119, "bfrev",   1,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_BFE,      24,  "bfe",     3,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_BFE,      120, "bfe",     3,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_BFI1,     25,  "bfi1",    2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_BFI1,     121, "bfi1",    2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_BFI2,     26,  "bfi2",    3,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_BFI2,     122, "bfi2",    3,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_JMPI,     32,  "jmpi",    0,    0,    GFX_ALL },
   { BRW_OPCODE_BRD,      33,  "brd",     0,    0,    GFX_ALL },
   { BRW_OPCODE_IF,       34,  "if",      0,    0,    GFX_ALL },
   { BRW_OPCODE_BRC,      35,  "brc",     0,    0,    GFX_ALL },
   { BRW_OPCODE_ELSE,     36,  "else",    0,    0,    GFX_ALL },
   { BRW_OPCODE_ENDIF,    37,  "endif",   0,    0,    GFX_ALL },
   { BRW_OPCODE_DO,       38,  "do",      0,    0,    0 }, /* Pseudo opcode. */
   { BRW_OPCODE_WHILE,    39,  "while",   0,    0,    GFX_ALL },
   { BRW_OPCODE_BREAK,    40,  "break",   0,    0,    GFX_ALL },
   { BRW_OPCODE_CONTINUE, 41,  "cont",    0,    0,    GFX_ALL },
   { BRW_OPCODE_HALT,     42,  "halt",    0,    0,    GFX_ALL },
   { BRW_OPCODE_CALLA,    43,  "calla",   0,    0,    GFX_ALL },
   { BRW_OPCODE_CALL,     44,  "call",    0,    0,    GFX_ALL },
   { BRW_OPCODE_RET,      45,  "ret",     0,    0,    GFX_ALL },
   { BRW_OPCODE_GOTO,     46,  "goto",    0,    0,    GFX_ALL },
   { BRW_OPCODE_JOIN,     47,  "join",    0,    0,    GFX_ALL },
   { BRW_OPCODE_WAIT,     48,  "wait",    0,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SEND,     49,  "send",    1,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SENDC,    50,  "sendc",   1,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SEND,     49,  "send",    2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_SENDC,    50,  "sendc",   2,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_SENDS,    51,  "sends",   2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_SENDSC,   52,  "sendsc",  2,    1,    GFX_LT(GFX12) },
   { BRW_OPCODE_MATH,     56,  "math",    2,    1,    GFX_ALL },
   { BRW_OPCODE_ADD,      64,  "add",     2,    1,    GFX_ALL },
   { BRW_OPCODE_MUL,      65,  "mul",     2,    1,    GFX_ALL },
   { BRW_OPCODE_AVG,      66,  "avg",     2,    1,    GFX_ALL },
   { BRW_OPCODE_FRC,      67,  "frc",     1,    1,    GFX_ALL },
   { BRW_OPCODE_RNDU,     68,  "rndu",    1,    1,    GFX_ALL },
   { BRW_OPCODE_RNDD,     69,  "rndd",    1,    1,    GFX_ALL },
   { BRW_OPCODE_RNDE,     70,  "rnde",    1,    1,    GFX_ALL },
   { BRW_OPCODE_RNDZ,     71,  "rndz",    1,    1,    GFX_ALL },
   { BRW_OPCODE_MAC,      72,  "mac",     2,    1,    GFX_ALL },
   { BRW_OPCODE_MACH,     73,  "mach",    2,    1,    GFX_ALL },
   { BRW_OPCODE_LZD,      74,  "lzd",     1,    1,    GFX_ALL },
   { BRW_OPCODE_FBH,      75,  "fbh",     1,    1,    GFX_ALL },
   { BRW_OPCODE_FBL,      76,  "fbl",     1,    1,    GFX_ALL },
   { BRW_OPCODE_CBIT,     77,  "cbit",    1,    1,    GFX_ALL },
   { BRW_OPCODE_ADDC,     78,  "addc",    2,    1,    GFX_ALL },
   { BRW_OPCODE_SUBB,     79,  "subb",    2,    1,    GFX_ALL },
   { BRW_OPCODE_ADD3,     82,  "add3",    3,    1,    GFX_GE(GFX125) },
   { BRW_OPCODE_MACL,     83,  "macl",    2,    1,    GFX_GE(XE2) },
   { BRW_OPCODE_DP4,      84,  "dp4",     2,    1,    GFX_LT(GFX11) },
   { BRW_OPCODE_SRND,     84,  "srnd",    2,    1,    GFX_GE(XE2) },
   { BRW_OPCODE_DPH,      85,  "dph",     2,    1,    GFX_LT(GFX11) },
   { BRW_OPCODE_DP3,      86,  "dp3",     2,    1,    GFX_LT(GFX11) },
   { BRW_OPCODE_DP2,      87,  "dp2",     2,    1,    GFX_LT(GFX11) },
   { BRW_OPCODE_DP4A,     88,  "dp4a",    3,    1,    GFX_GE(GFX12) },
   { BRW_OPCODE_LINE,     89,  "line",    2,    1,    GFX9 },
   { BRW_OPCODE_DPAS,     89,  "dpas",    3,    1,    GFX_GE(GFX125) },
   { BRW_OPCODE_PLN,      90,  "pln",     2,    1,    GFX9 },
   { BRW_OPCODE_MAD,      91,  "mad",     3,    1,    GFX_ALL },
   { BRW_OPCODE_LRP,      92,  "lrp",     3,    1,    GFX9 },
   { BRW_OPCODE_MADM,     93,  "madm",    3,    1,    GFX_ALL },
   { BRW_OPCODE_NOP,      126, "nop",     0,    0,    GFX_LT(GFX12) },
   { BRW_OPCODE_NOP,      96,  "nop",     0,    0,    GFX_GE(GFX12) }
};

void
brw_init_isa_info(struct brw_isa_info *isa,
                  const struct intel_device_info *devinfo)
{
   assert(devinfo->ver >= 9);

   isa->devinfo = devinfo;

   enum gfx_ver ver = gfx_ver_from_devinfo(devinfo);

   memset(isa->ir_to_descs, 0, sizeof(isa->ir_to_descs));
   memset(isa->hw_to_descs, 0, sizeof(isa->hw_to_descs));

   for (unsigned i = 0; i < ARRAY_SIZE(opcode_descs); i++) {
      if (opcode_descs[i].gfx_vers & ver) {
         const unsigned e = opcode_descs[i].ir;
         const unsigned h = opcode_descs[i].hw;
         assert(e < ARRAY_SIZE(isa->ir_to_descs) && !isa->ir_to_descs[e]);
         assert(h < ARRAY_SIZE(isa->hw_to_descs) && !isa->hw_to_descs[h]);
         isa->ir_to_descs[e] = &opcode_descs[i];
         isa->hw_to_descs[h] = &opcode_descs[i];
      }
   }
}

/**
 * Return the matching opcode_desc for the specified IR opcode and hardware
 * generation, or NULL if the opcode is not supported by the device.
 */
const struct opcode_desc *
brw_opcode_desc(const struct brw_isa_info *isa, enum opcode op)
{
   return op < ARRAY_SIZE(isa->ir_to_descs) ? isa->ir_to_descs[op] : NULL;
}

/**
 * Return the matching opcode_desc for the specified HW opcode and hardware
 * generation, or NULL if the opcode is not supported by the device.
 */
const struct opcode_desc *
brw_opcode_desc_from_hw(const struct brw_isa_info *isa, unsigned hw)
{
   return hw < ARRAY_SIZE(isa->hw_to_descs) ? isa->hw_to_descs[hw] : NULL;
}
