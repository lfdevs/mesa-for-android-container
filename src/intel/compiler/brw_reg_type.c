/*
 * Copyright © 2017 Intel Corporation
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

#include "brw_reg.h"
#include "brw_eu_defines.h"
#include "dev/gen_device_info.h"

#define INVALID (-1)

enum hw_reg_type {
   BRW_HW_REG_TYPE_UD  = 0,
   BRW_HW_REG_TYPE_D   = 1,
   BRW_HW_REG_TYPE_UW  = 2,
   BRW_HW_REG_TYPE_W   = 3,
   BRW_HW_REG_TYPE_F   = 7,
   GEN8_HW_REG_TYPE_UQ = 8,
   GEN8_HW_REG_TYPE_Q  = 9,

   BRW_HW_REG_TYPE_UB  = 4,
   BRW_HW_REG_TYPE_B   = 5,
   GEN7_HW_REG_TYPE_DF = 6,
   GEN8_HW_REG_TYPE_HF = 10,

   GEN11_HW_REG_TYPE_UD = 0,
   GEN11_HW_REG_TYPE_D  = 1,
   GEN11_HW_REG_TYPE_UW = 2,
   GEN11_HW_REG_TYPE_W  = 3,
   GEN11_HW_REG_TYPE_UB = 4,
   GEN11_HW_REG_TYPE_B  = 5,
   GEN11_HW_REG_TYPE_UQ = 6,
   GEN11_HW_REG_TYPE_Q  = 7,
   GEN11_HW_REG_TYPE_HF = 8,
   GEN11_HW_REG_TYPE_F  = 9,
   GEN11_HW_REG_TYPE_DF = 10,
   GEN11_HW_REG_TYPE_NF = 11,
};

enum hw_imm_type {
   BRW_HW_IMM_TYPE_UD  = 0,
   BRW_HW_IMM_TYPE_D   = 1,
   BRW_HW_IMM_TYPE_UW  = 2,
   BRW_HW_IMM_TYPE_W   = 3,
   BRW_HW_IMM_TYPE_F   = 7,
   GEN8_HW_IMM_TYPE_UQ = 8,
   GEN8_HW_IMM_TYPE_Q  = 9,

   BRW_HW_IMM_TYPE_UV  = 4,
   BRW_HW_IMM_TYPE_VF  = 5,
   BRW_HW_IMM_TYPE_V   = 6,
   GEN8_HW_IMM_TYPE_DF = 10,
   GEN8_HW_IMM_TYPE_HF = 11,

   GEN11_HW_IMM_TYPE_UD = 0,
   GEN11_HW_IMM_TYPE_D  = 1,
   GEN11_HW_IMM_TYPE_UW = 2,
   GEN11_HW_IMM_TYPE_W  = 3,
   GEN11_HW_IMM_TYPE_UV = 4,
   GEN11_HW_IMM_TYPE_V  = 5,
   GEN11_HW_IMM_TYPE_UQ = 6,
   GEN11_HW_IMM_TYPE_Q  = 7,
   GEN11_HW_IMM_TYPE_HF = 8,
   GEN11_HW_IMM_TYPE_F  = 9,
   GEN11_HW_IMM_TYPE_DF = 10,
   GEN11_HW_IMM_TYPE_VF = 11,
};

#define GEN12_HW_REG_TYPE_UINT(n) (n)
#define GEN12_HW_REG_TYPE_SINT(n) (0x4 | (n))
#define GEN12_HW_REG_TYPE_FLOAT(n) (0x8 | (n))

static const struct hw_type {
   enum hw_reg_type reg_type;
   enum hw_imm_type imm_type;
} gen4_hw_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = {     INVALID, INVALID             },

   [BRW_REGISTER_TYPE_F]  = { BRW_HW_REG_TYPE_F,   BRW_HW_IMM_TYPE_F   },
   [BRW_REGISTER_TYPE_VF] = { INVALID,             BRW_HW_IMM_TYPE_VF  },

   [BRW_REGISTER_TYPE_D]  = { BRW_HW_REG_TYPE_D,   BRW_HW_IMM_TYPE_D   },
   [BRW_REGISTER_TYPE_UD] = { BRW_HW_REG_TYPE_UD,  BRW_HW_IMM_TYPE_UD  },
   [BRW_REGISTER_TYPE_W]  = { BRW_HW_REG_TYPE_W,   BRW_HW_IMM_TYPE_W   },
   [BRW_REGISTER_TYPE_UW] = { BRW_HW_REG_TYPE_UW,  BRW_HW_IMM_TYPE_UW  },
   [BRW_REGISTER_TYPE_B]  = { BRW_HW_REG_TYPE_B,   INVALID             },
   [BRW_REGISTER_TYPE_UB] = { BRW_HW_REG_TYPE_UB,  INVALID             },
   [BRW_REGISTER_TYPE_V]  = { INVALID,             BRW_HW_IMM_TYPE_V   },
}, gen6_hw_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = {     INVALID, INVALID             },

   [BRW_REGISTER_TYPE_F]  = { BRW_HW_REG_TYPE_F,   BRW_HW_IMM_TYPE_F   },
   [BRW_REGISTER_TYPE_VF] = { INVALID,             BRW_HW_IMM_TYPE_VF  },

   [BRW_REGISTER_TYPE_D]  = { BRW_HW_REG_TYPE_D,   BRW_HW_IMM_TYPE_D   },
   [BRW_REGISTER_TYPE_UD] = { BRW_HW_REG_TYPE_UD,  BRW_HW_IMM_TYPE_UD  },
   [BRW_REGISTER_TYPE_W]  = { BRW_HW_REG_TYPE_W,   BRW_HW_IMM_TYPE_W   },
   [BRW_REGISTER_TYPE_UW] = { BRW_HW_REG_TYPE_UW,  BRW_HW_IMM_TYPE_UW  },
   [BRW_REGISTER_TYPE_B]  = { BRW_HW_REG_TYPE_B,   INVALID             },
   [BRW_REGISTER_TYPE_UB] = { BRW_HW_REG_TYPE_UB,  INVALID             },
   [BRW_REGISTER_TYPE_V]  = { INVALID,             BRW_HW_IMM_TYPE_V   },
   [BRW_REGISTER_TYPE_UV] = { INVALID,             BRW_HW_IMM_TYPE_UV  },
}, gen7_hw_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = {     INVALID, INVALID             },

   [BRW_REGISTER_TYPE_DF] = { GEN7_HW_REG_TYPE_DF, INVALID             },
   [BRW_REGISTER_TYPE_F]  = { BRW_HW_REG_TYPE_F,   BRW_HW_IMM_TYPE_F   },
   [BRW_REGISTER_TYPE_VF] = { INVALID,             BRW_HW_IMM_TYPE_VF  },

   [BRW_REGISTER_TYPE_D]  = { BRW_HW_REG_TYPE_D,   BRW_HW_IMM_TYPE_D   },
   [BRW_REGISTER_TYPE_UD] = { BRW_HW_REG_TYPE_UD,  BRW_HW_IMM_TYPE_UD  },
   [BRW_REGISTER_TYPE_W]  = { BRW_HW_REG_TYPE_W,   BRW_HW_IMM_TYPE_W   },
   [BRW_REGISTER_TYPE_UW] = { BRW_HW_REG_TYPE_UW,  BRW_HW_IMM_TYPE_UW  },
   [BRW_REGISTER_TYPE_B]  = { BRW_HW_REG_TYPE_B,   INVALID             },
   [BRW_REGISTER_TYPE_UB] = { BRW_HW_REG_TYPE_UB,  INVALID             },
   [BRW_REGISTER_TYPE_V]  = { INVALID,             BRW_HW_IMM_TYPE_V   },
   [BRW_REGISTER_TYPE_UV] = { INVALID,             BRW_HW_IMM_TYPE_UV  },
}, gen8_hw_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = {     INVALID, INVALID             },

   [BRW_REGISTER_TYPE_DF] = { GEN7_HW_REG_TYPE_DF, GEN8_HW_IMM_TYPE_DF },
   [BRW_REGISTER_TYPE_F]  = { BRW_HW_REG_TYPE_F,   BRW_HW_IMM_TYPE_F   },
   [BRW_REGISTER_TYPE_HF] = { GEN8_HW_REG_TYPE_HF, GEN8_HW_IMM_TYPE_HF },
   [BRW_REGISTER_TYPE_VF] = { INVALID,             BRW_HW_IMM_TYPE_VF  },

   [BRW_REGISTER_TYPE_Q]  = { GEN8_HW_REG_TYPE_Q,  GEN8_HW_IMM_TYPE_Q  },
   [BRW_REGISTER_TYPE_UQ] = { GEN8_HW_REG_TYPE_UQ, GEN8_HW_IMM_TYPE_UQ },
   [BRW_REGISTER_TYPE_D]  = { BRW_HW_REG_TYPE_D,   BRW_HW_IMM_TYPE_D   },
   [BRW_REGISTER_TYPE_UD] = { BRW_HW_REG_TYPE_UD,  BRW_HW_IMM_TYPE_UD  },
   [BRW_REGISTER_TYPE_W]  = { BRW_HW_REG_TYPE_W,   BRW_HW_IMM_TYPE_W   },
   [BRW_REGISTER_TYPE_UW] = { BRW_HW_REG_TYPE_UW,  BRW_HW_IMM_TYPE_UW  },
   [BRW_REGISTER_TYPE_B]  = { BRW_HW_REG_TYPE_B,   INVALID             },
   [BRW_REGISTER_TYPE_UB] = { BRW_HW_REG_TYPE_UB,  INVALID             },
   [BRW_REGISTER_TYPE_V]  = { INVALID,             BRW_HW_IMM_TYPE_V   },
   [BRW_REGISTER_TYPE_UV] = { INVALID,             BRW_HW_IMM_TYPE_UV  },
}, gen11_hw_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = {      INVALID, INVALID              },

   [BRW_REGISTER_TYPE_NF] = { GEN11_HW_REG_TYPE_NF, INVALID              },
   [BRW_REGISTER_TYPE_F]  = { GEN11_HW_REG_TYPE_F,  GEN11_HW_IMM_TYPE_F  },
   [BRW_REGISTER_TYPE_HF] = { GEN11_HW_REG_TYPE_HF, GEN11_HW_IMM_TYPE_HF },
   [BRW_REGISTER_TYPE_VF] = { INVALID,              GEN11_HW_IMM_TYPE_VF },

   [BRW_REGISTER_TYPE_D]  = { GEN11_HW_REG_TYPE_D,  GEN11_HW_IMM_TYPE_D  },
   [BRW_REGISTER_TYPE_UD] = { GEN11_HW_REG_TYPE_UD, GEN11_HW_IMM_TYPE_UD },
   [BRW_REGISTER_TYPE_W]  = { GEN11_HW_REG_TYPE_W,  GEN11_HW_IMM_TYPE_W  },
   [BRW_REGISTER_TYPE_UW] = { GEN11_HW_REG_TYPE_UW, GEN11_HW_IMM_TYPE_UW },
   [BRW_REGISTER_TYPE_B]  = { GEN11_HW_REG_TYPE_B,  INVALID              },
   [BRW_REGISTER_TYPE_UB] = { GEN11_HW_REG_TYPE_UB, INVALID              },
   [BRW_REGISTER_TYPE_V]  = { INVALID,              GEN11_HW_IMM_TYPE_V  },
   [BRW_REGISTER_TYPE_UV] = { INVALID,              GEN11_HW_IMM_TYPE_UV },
}, gen12_hw_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = {            INVALID, INVALID                    },

   [BRW_REGISTER_TYPE_F]  = { GEN12_HW_REG_TYPE_FLOAT(2), GEN12_HW_REG_TYPE_FLOAT(2) },
   [BRW_REGISTER_TYPE_HF] = { GEN12_HW_REG_TYPE_FLOAT(1), GEN12_HW_REG_TYPE_FLOAT(1) },
   [BRW_REGISTER_TYPE_VF] = { INVALID,                    GEN12_HW_REG_TYPE_FLOAT(0) },

   [BRW_REGISTER_TYPE_D]  = { GEN12_HW_REG_TYPE_SINT(2),  GEN12_HW_REG_TYPE_SINT(2)  },
   [BRW_REGISTER_TYPE_UD] = { GEN12_HW_REG_TYPE_UINT(2),  GEN12_HW_REG_TYPE_UINT(2)  },
   [BRW_REGISTER_TYPE_W]  = { GEN12_HW_REG_TYPE_SINT(1),  GEN12_HW_REG_TYPE_SINT(1)  },
   [BRW_REGISTER_TYPE_UW] = { GEN12_HW_REG_TYPE_UINT(1),  GEN12_HW_REG_TYPE_UINT(1)  },
   [BRW_REGISTER_TYPE_B]  = { GEN12_HW_REG_TYPE_SINT(0),  INVALID                    },
   [BRW_REGISTER_TYPE_UB] = { GEN12_HW_REG_TYPE_UINT(0),  INVALID                    },
   [BRW_REGISTER_TYPE_V]  = { INVALID,                    GEN12_HW_REG_TYPE_SINT(0)  },
   [BRW_REGISTER_TYPE_UV] = { INVALID,                    GEN12_HW_REG_TYPE_UINT(0)  },
};

/* SNB adds 3-src instructions (MAD and LRP) that only operate on floats, so
 * the types were implied. IVB adds BFE and BFI2 that operate on doublewords
 * and unsigned doublewords, so a new field is also available in the da3src
 * struct (part of struct brw_instruction.bits1 in brw_structs.h) to select
 * dst and shared-src types.
 *
 * CNL adds support for 3-src instructions in align1 mode, and with it support
 * for most register types.
 */
enum hw_3src_reg_type {
   GEN7_3SRC_TYPE_F  = 0,
   GEN7_3SRC_TYPE_D  = 1,
   GEN7_3SRC_TYPE_UD = 2,
   GEN7_3SRC_TYPE_DF = 3,
   GEN8_3SRC_TYPE_HF = 4,

   /** When ExecutionDatatype is 1: @{ */
   GEN10_ALIGN1_3SRC_REG_TYPE_HF = 0b000,
   GEN10_ALIGN1_3SRC_REG_TYPE_F  = 0b001,
   GEN10_ALIGN1_3SRC_REG_TYPE_DF = 0b010,
   GEN11_ALIGN1_3SRC_REG_TYPE_NF = 0b011,
   /** @} */

   /** When ExecutionDatatype is 0: @{ */
   GEN10_ALIGN1_3SRC_REG_TYPE_UD = 0b000,
   GEN10_ALIGN1_3SRC_REG_TYPE_D  = 0b001,
   GEN10_ALIGN1_3SRC_REG_TYPE_UW = 0b010,
   GEN10_ALIGN1_3SRC_REG_TYPE_W  = 0b011,
   GEN10_ALIGN1_3SRC_REG_TYPE_UB = 0b100,
   GEN10_ALIGN1_3SRC_REG_TYPE_B  = 0b101,
   /** @} */
};

static const struct hw_3src_type {
   enum hw_3src_reg_type reg_type;
   enum gen10_align1_3src_exec_type exec_type;
} gen6_hw_3src_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = { INVALID },

   [BRW_REGISTER_TYPE_F]  = { GEN7_3SRC_TYPE_F  },
}, gen7_hw_3src_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = { INVALID },

   [BRW_REGISTER_TYPE_F]  = { GEN7_3SRC_TYPE_F  },
   [BRW_REGISTER_TYPE_D]  = { GEN7_3SRC_TYPE_D  },
   [BRW_REGISTER_TYPE_UD] = { GEN7_3SRC_TYPE_UD },
   [BRW_REGISTER_TYPE_DF] = { GEN7_3SRC_TYPE_DF },
}, gen8_hw_3src_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = { INVALID },

   [BRW_REGISTER_TYPE_F]  = { GEN7_3SRC_TYPE_F  },
   [BRW_REGISTER_TYPE_D]  = { GEN7_3SRC_TYPE_D  },
   [BRW_REGISTER_TYPE_UD] = { GEN7_3SRC_TYPE_UD },
   [BRW_REGISTER_TYPE_DF] = { GEN7_3SRC_TYPE_DF },
   [BRW_REGISTER_TYPE_HF] = { GEN8_3SRC_TYPE_HF },
}, gen10_hw_3src_align1_type[] = {
#define E(x) BRW_ALIGN1_3SRC_EXEC_TYPE_##x
   [0 ... BRW_REGISTER_TYPE_LAST] = { INVALID },

   [BRW_REGISTER_TYPE_DF] = { GEN10_ALIGN1_3SRC_REG_TYPE_DF, E(FLOAT) },
   [BRW_REGISTER_TYPE_F]  = { GEN10_ALIGN1_3SRC_REG_TYPE_F,  E(FLOAT) },
   [BRW_REGISTER_TYPE_HF] = { GEN10_ALIGN1_3SRC_REG_TYPE_HF, E(FLOAT) },

   [BRW_REGISTER_TYPE_D]  = { GEN10_ALIGN1_3SRC_REG_TYPE_D,  E(INT)   },
   [BRW_REGISTER_TYPE_UD] = { GEN10_ALIGN1_3SRC_REG_TYPE_UD, E(INT)   },
   [BRW_REGISTER_TYPE_W]  = { GEN10_ALIGN1_3SRC_REG_TYPE_W,  E(INT)   },
   [BRW_REGISTER_TYPE_UW] = { GEN10_ALIGN1_3SRC_REG_TYPE_UW, E(INT)   },
   [BRW_REGISTER_TYPE_B]  = { GEN10_ALIGN1_3SRC_REG_TYPE_B,  E(INT)   },
   [BRW_REGISTER_TYPE_UB] = { GEN10_ALIGN1_3SRC_REG_TYPE_UB, E(INT)   },
}, gen11_hw_3src_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = { INVALID },

   [BRW_REGISTER_TYPE_NF] = { GEN11_ALIGN1_3SRC_REG_TYPE_NF, E(FLOAT) },
   [BRW_REGISTER_TYPE_F]  = { GEN10_ALIGN1_3SRC_REG_TYPE_F,  E(FLOAT) },
   [BRW_REGISTER_TYPE_HF] = { GEN10_ALIGN1_3SRC_REG_TYPE_HF, E(FLOAT) },

   [BRW_REGISTER_TYPE_D]  = { GEN10_ALIGN1_3SRC_REG_TYPE_D,  E(INT)   },
   [BRW_REGISTER_TYPE_UD] = { GEN10_ALIGN1_3SRC_REG_TYPE_UD, E(INT)   },
   [BRW_REGISTER_TYPE_W]  = { GEN10_ALIGN1_3SRC_REG_TYPE_W,  E(INT)   },
   [BRW_REGISTER_TYPE_UW] = { GEN10_ALIGN1_3SRC_REG_TYPE_UW, E(INT)   },
   [BRW_REGISTER_TYPE_B]  = { GEN10_ALIGN1_3SRC_REG_TYPE_B,  E(INT)   },
   [BRW_REGISTER_TYPE_UB] = { GEN10_ALIGN1_3SRC_REG_TYPE_UB, E(INT)   },
}, gen12_hw_3src_type[] = {
   [0 ... BRW_REGISTER_TYPE_LAST] = { INVALID },

   [BRW_REGISTER_TYPE_F]  = { GEN12_HW_REG_TYPE_UINT(2),     E(FLOAT), },
   [BRW_REGISTER_TYPE_HF] = { GEN12_HW_REG_TYPE_UINT(1),     E(FLOAT), },

   [BRW_REGISTER_TYPE_D]  = { GEN12_HW_REG_TYPE_SINT(2),     E(INT),  },
   [BRW_REGISTER_TYPE_UD] = { GEN12_HW_REG_TYPE_UINT(2),     E(INT),  },
   [BRW_REGISTER_TYPE_W]  = { GEN12_HW_REG_TYPE_SINT(1),     E(INT),  },
   [BRW_REGISTER_TYPE_UW] = { GEN12_HW_REG_TYPE_UINT(1),     E(INT),  },
   [BRW_REGISTER_TYPE_B]  = { GEN12_HW_REG_TYPE_SINT(0),     E(INT),  },
   [BRW_REGISTER_TYPE_UB] = { GEN12_HW_REG_TYPE_UINT(0),     E(INT),  },
#undef E
};

/**
 * Convert a brw_reg_type enumeration value into the hardware representation.
 *
 * The hardware encoding may depend on whether the value is an immediate.
 */
unsigned
brw_reg_type_to_hw_type(const struct gen_device_info *devinfo,
                        enum brw_reg_file file,
                        enum brw_reg_type type)
{
   const struct hw_type *table;

   if (devinfo->gen >= 12) {
      assert(type < ARRAY_SIZE(gen12_hw_type));
      table = gen12_hw_type;
   } else if (devinfo->gen >= 11) {
      assert(type < ARRAY_SIZE(gen11_hw_type));
      table = gen11_hw_type;
   } else if (devinfo->gen >= 8) {
      assert(type < ARRAY_SIZE(gen8_hw_type));
      table = gen8_hw_type;
   } else if (devinfo->gen >= 7) {
      assert(type < ARRAY_SIZE(gen7_hw_type));
      table = gen7_hw_type;
   } else if (devinfo->gen >= 6) {
      assert(type < ARRAY_SIZE(gen6_hw_type));
      table = gen6_hw_type;
   } else {
      assert(type < ARRAY_SIZE(gen4_hw_type));
      table = gen4_hw_type;
   }

   if (file == BRW_IMMEDIATE_VALUE) {
      assert(table[type].imm_type != (enum hw_imm_type)INVALID);
      return table[type].imm_type;
   } else {
      assert(table[type].reg_type != (enum hw_reg_type)INVALID);
      return table[type].reg_type;
   }
}

/**
 * Convert the hardware representation into a brw_reg_type enumeration value.
 *
 * The hardware encoding may depend on whether the value is an immediate.
 */
enum brw_reg_type
brw_hw_type_to_reg_type(const struct gen_device_info *devinfo,
                        enum brw_reg_file file, unsigned hw_type)
{
   const struct hw_type *table;

   if (devinfo->gen >= 12) {
      table = gen12_hw_type;
   } else if (devinfo->gen >= 11) {
      table = gen11_hw_type;
   } else if (devinfo->gen >= 8) {
      table = gen8_hw_type;
   } else if (devinfo->gen >= 7) {
      table = gen7_hw_type;
   } else if (devinfo->gen >= 6) {
      table = gen6_hw_type;
   } else {
      table = gen4_hw_type;
   }

   if (file == BRW_IMMEDIATE_VALUE) {
      for (enum brw_reg_type i = 0; i <= BRW_REGISTER_TYPE_LAST; i++) {
         if (table[i].imm_type == (enum hw_imm_type)hw_type) {
            return i;
         }
      }
   } else {
      for (enum brw_reg_type i = 0; i <= BRW_REGISTER_TYPE_LAST; i++) {
         if (table[i].reg_type == (enum hw_reg_type)hw_type) {
            return i;
         }
      }
   }
   return INVALID_REG_TYPE;
}

/**
 * Convert a brw_reg_type enumeration value into the hardware representation
 * for a 3-src align16 instruction
 */
unsigned
brw_reg_type_to_a16_hw_3src_type(const struct gen_device_info *devinfo,
                                 enum brw_reg_type type)
{
   const struct hw_3src_type *table;

   if (devinfo->gen >= 8) {
      assert(type < ARRAY_SIZE(gen8_hw_3src_type));
      table = gen8_hw_3src_type;
   } else if (devinfo->gen >= 7) {
      assert(type < ARRAY_SIZE(gen7_hw_3src_type));
      table = gen7_hw_3src_type;
   } else {
      assert(type < ARRAY_SIZE(gen6_hw_3src_type));
      table = gen6_hw_3src_type;
   }

   assert(table[type].reg_type != (enum hw_3src_reg_type)INVALID);
   return table[type].reg_type;
}

/**
 * Convert a brw_reg_type enumeration value into the hardware representation
 * for a 3-src align1 instruction
 */
unsigned
brw_reg_type_to_a1_hw_3src_type(const struct gen_device_info *devinfo,
                                enum brw_reg_type type)
{
   if (devinfo->gen >= 12) {
      assert(type < ARRAY_SIZE(gen12_hw_3src_type));
      return gen12_hw_3src_type[type].reg_type;
   } else if (devinfo->gen >= 11) {
      assert(type < ARRAY_SIZE(gen11_hw_3src_type));
      return gen11_hw_3src_type[type].reg_type;
   } else {
      assert(type < ARRAY_SIZE(gen10_hw_3src_align1_type));
      return gen10_hw_3src_align1_type[type].reg_type;
   }
}

/**
 * Convert the hardware representation for a 3-src align16 instruction into a
 * brw_reg_type enumeration value.
 */
enum brw_reg_type
brw_a16_hw_3src_type_to_reg_type(const struct gen_device_info *devinfo,
                                 unsigned hw_type)
{
   const struct hw_3src_type *table = NULL;

   if (devinfo->gen >= 8) {
      table = gen8_hw_3src_type;
   } else if (devinfo->gen >= 7) {
      table = gen7_hw_3src_type;
   } else if (devinfo->gen >= 6) {
      table = gen6_hw_3src_type;
   }

   for (enum brw_reg_type i = 0; i <= BRW_REGISTER_TYPE_LAST; i++) {
      if (table[i].reg_type == hw_type) {
         return i;
      }
   }
   return INVALID_REG_TYPE;
}

/**
 * Convert the hardware representation for a 3-src align1 instruction into a
 * brw_reg_type enumeration value.
 */
enum brw_reg_type
brw_a1_hw_3src_type_to_reg_type(const struct gen_device_info *devinfo,
                                unsigned hw_type, unsigned exec_type)
{
   const struct hw_3src_type *table = (devinfo->gen >= 12 ? gen12_hw_3src_type :
                                       devinfo->gen >= 11 ? gen11_hw_3src_type :
                                       gen10_hw_3src_align1_type);

   for (enum brw_reg_type i = 0; i <= BRW_REGISTER_TYPE_LAST; i++) {
      if (table[i].reg_type == hw_type &&
          table[i].exec_type == exec_type) {
         return i;
      }
   }
   return INVALID_REG_TYPE;
}

/**
 * Return the element size given a register type.
 */
unsigned
brw_reg_type_to_size(enum brw_reg_type type)
{
   static const unsigned type_size[] = {
      [BRW_REGISTER_TYPE_NF] = 8,
      [BRW_REGISTER_TYPE_DF] = 8,
      [BRW_REGISTER_TYPE_F]  = 4,
      [BRW_REGISTER_TYPE_HF] = 2,
      [BRW_REGISTER_TYPE_VF] = 4,

      [BRW_REGISTER_TYPE_Q]  = 8,
      [BRW_REGISTER_TYPE_UQ] = 8,
      [BRW_REGISTER_TYPE_D]  = 4,
      [BRW_REGISTER_TYPE_UD] = 4,
      [BRW_REGISTER_TYPE_W]  = 2,
      [BRW_REGISTER_TYPE_UW] = 2,
      [BRW_REGISTER_TYPE_B]  = 1,
      [BRW_REGISTER_TYPE_UB] = 1,
      [BRW_REGISTER_TYPE_V]  = 2,
      [BRW_REGISTER_TYPE_UV] = 2,
   };
   if (type >= ARRAY_SIZE(type_size))
      return -1;

   return type_size[type];
}

/**
 * Converts a BRW_REGISTER_TYPE_* enum to a short string (F, UD, and so on).
 *
 * This is different than reg_encoding from brw_disasm.c in that it operates
 * on the abstract enum values, rather than the generation-specific encoding.
 */
const char *
brw_reg_type_to_letters(enum brw_reg_type type)
{
   static const char letters[][3] = {
      [BRW_REGISTER_TYPE_NF] = "NF",
      [BRW_REGISTER_TYPE_DF] = "DF",
      [BRW_REGISTER_TYPE_F]  = "F",
      [BRW_REGISTER_TYPE_HF] = "HF",
      [BRW_REGISTER_TYPE_VF] = "VF",

      [BRW_REGISTER_TYPE_Q]  = "Q",
      [BRW_REGISTER_TYPE_UQ] = "UQ",
      [BRW_REGISTER_TYPE_D]  = "D",
      [BRW_REGISTER_TYPE_UD] = "UD",
      [BRW_REGISTER_TYPE_W]  = "W",
      [BRW_REGISTER_TYPE_UW] = "UW",
      [BRW_REGISTER_TYPE_B]  = "B",
      [BRW_REGISTER_TYPE_UB] = "UB",
      [BRW_REGISTER_TYPE_V]  = "V",
      [BRW_REGISTER_TYPE_UV] = "UV",
   };
   if (type >= ARRAY_SIZE(letters))
      return "INVALID";

   assert(type < ARRAY_SIZE(letters));
   return letters[type];
}
