/*
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define CMD_MI				(0x0 << 29)
#define CMD_2D				(0x2 << 29)
#define CMD_3D				(0x3 << 29)

#define MI_NOOP				(CMD_MI | 0)

#define MI_BATCH_BUFFER_END		(CMD_MI | 0xA << 23)

#define MI_FLUSH			(CMD_MI | (4 << 23))
#define FLUSH_MAP_CACHE				(1 << 0)
#define INHIBIT_FLUSH_RENDER_CACHE		(1 << 2)

#define MI_STORE_DATA_IMM		(CMD_MI | (0x20 << 23))
#define MI_LOAD_REGISTER_IMM		(CMD_MI | (0x22 << 23))
#define MI_LOAD_REGISTER_REG		(CMD_MI | (0x2A << 23))

#define MI_FLUSH_DW			(CMD_MI | (0x26 << 23) | 2)

#define MI_STORE_REGISTER_MEM		(CMD_MI | (0x24 << 23))
# define MI_STORE_REGISTER_MEM_USE_GGTT		(1 << 22)
# define MI_STORE_REGISTER_MEM_PREDICATE	(1 << 21)

/* Load a value from memory into a register.  Only available on Gen7+. */
#define GEN7_MI_LOAD_REGISTER_MEM	(CMD_MI | (0x29 << 23))
# define MI_LOAD_REGISTER_MEM_USE_GGTT		(1 << 22)
/* Haswell RS control */
#define MI_RS_CONTROL                   (CMD_MI | (0x6 << 23))
#define MI_RS_STORE_DATA_IMM            (CMD_MI | (0x2b << 23))

/* Manipulate the predicate bit based on some register values. Only on Gen7+ */
#define GEN7_MI_PREDICATE		(CMD_MI | (0xC << 23))
# define MI_PREDICATE_LOADOP_KEEP		(0 << 6)
# define MI_PREDICATE_LOADOP_LOAD		(2 << 6)
# define MI_PREDICATE_LOADOP_LOADINV		(3 << 6)
# define MI_PREDICATE_COMBINEOP_SET		(0 << 3)
# define MI_PREDICATE_COMBINEOP_AND		(1 << 3)
# define MI_PREDICATE_COMBINEOP_OR		(2 << 3)
# define MI_PREDICATE_COMBINEOP_XOR		(3 << 3)
# define MI_PREDICATE_COMPAREOP_TRUE		(0 << 0)
# define MI_PREDICATE_COMPAREOP_FALSE		(1 << 0)
# define MI_PREDICATE_COMPAREOP_SRCS_EQUAL	(2 << 0)
# define MI_PREDICATE_COMPAREOP_DELTAS_EQUAL	(3 << 0)

#define HSW_MI_MATH			(CMD_MI | (0x1a << 23))

#define MI_MATH_ALU2(opcode, operand1, operand2) \
   ( ((MI_MATH_OPCODE_##opcode) << 20) | ((MI_MATH_OPERAND_##operand1) << 10) | \
     ((MI_MATH_OPERAND_##operand2) << 0) )

#define MI_MATH_ALU1(opcode, operand1) \
   ( ((MI_MATH_OPCODE_##opcode) << 20) | ((MI_MATH_OPERAND_##operand1) << 10) )

#define MI_MATH_ALU0(opcode) \
   ( ((MI_MATH_OPCODE_##opcode) << 20) )

#define MI_MATH_OPCODE_NOOP      0x000
#define MI_MATH_OPCODE_LOAD      0x080
#define MI_MATH_OPCODE_LOADINV   0x480
#define MI_MATH_OPCODE_LOAD0     0x081
#define MI_MATH_OPCODE_LOAD1     0x481
#define MI_MATH_OPCODE_ADD       0x100
#define MI_MATH_OPCODE_SUB       0x101
#define MI_MATH_OPCODE_AND       0x102
#define MI_MATH_OPCODE_OR        0x103
#define MI_MATH_OPCODE_XOR       0x104
#define MI_MATH_OPCODE_STORE     0x180
#define MI_MATH_OPCODE_STOREINV  0x580

#define MI_MATH_OPERAND_R0   0x00
#define MI_MATH_OPERAND_R1   0x01
#define MI_MATH_OPERAND_R2   0x02
#define MI_MATH_OPERAND_R3   0x03
#define MI_MATH_OPERAND_R4   0x04
#define MI_MATH_OPERAND_SRCA 0x20
#define MI_MATH_OPERAND_SRCB 0x21
#define MI_MATH_OPERAND_ACCU 0x31
#define MI_MATH_OPERAND_ZF   0x32
#define MI_MATH_OPERAND_CF   0x33

/** @{
 *
 * PIPE_CONTROL operation, a combination MI_FLUSH and register write with
 * additional flushing control.
 */
#define _3DSTATE_PIPE_CONTROL		(CMD_3D | (3 << 27) | (2 << 24))
#define PIPE_CONTROL_CS_STALL		(1 << 20)
#define PIPE_CONTROL_GLOBAL_SNAPSHOT_COUNT_RESET	(1 << 19)
#define PIPE_CONTROL_TLB_INVALIDATE	(1 << 18)
#define PIPE_CONTROL_SYNC_GFDT		(1 << 17)
#define PIPE_CONTROL_MEDIA_STATE_CLEAR	(1 << 16)
#define PIPE_CONTROL_NO_WRITE		(0 << 14)
#define PIPE_CONTROL_WRITE_IMMEDIATE	(1 << 14)
#define PIPE_CONTROL_WRITE_DEPTH_COUNT	(2 << 14)
#define PIPE_CONTROL_WRITE_TIMESTAMP	(3 << 14)
#define PIPE_CONTROL_DEPTH_STALL	(1 << 13)
#define PIPE_CONTROL_RENDER_TARGET_FLUSH (1 << 12)
#define PIPE_CONTROL_INSTRUCTION_INVALIDATE (1 << 11)
#define PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE	(1 << 10) /* GM45+ only */
#define PIPE_CONTROL_ISP_DIS		(1 << 9)
#define PIPE_CONTROL_INTERRUPT_ENABLE	(1 << 8)
#define PIPE_CONTROL_FLUSH_ENABLE	(1 << 7) /* Gen7+ only */
/* GT */
#define PIPE_CONTROL_DATA_CACHE_FLUSH   	(1 << 5)
#define PIPE_CONTROL_VF_CACHE_INVALIDATE	(1 << 4)
#define PIPE_CONTROL_CONST_CACHE_INVALIDATE	(1 << 3)
#define PIPE_CONTROL_STATE_CACHE_INVALIDATE	(1 << 2)
#define PIPE_CONTROL_STALL_AT_SCOREBOARD	(1 << 1)
#define PIPE_CONTROL_DEPTH_CACHE_FLUSH		(1 << 0)
#define PIPE_CONTROL_PPGTT_WRITE	(0 << 2)
#define PIPE_CONTROL_GLOBAL_GTT_WRITE	(1 << 2)

/** @} */

#define XY_SETUP_BLT_CMD		(CMD_2D | (0x01 << 22))

#define XY_COLOR_BLT_CMD		(CMD_2D | (0x50 << 22))

#define XY_SRC_COPY_BLT_CMD             (CMD_2D | (0x53 << 22))

#define XY_FAST_COPY_BLT_CMD             (CMD_2D | (0x42 << 22))

#define XY_TEXT_IMMEDIATE_BLIT_CMD	(CMD_2D | (0x31 << 22))
# define XY_TEXT_BYTE_PACKED		(1 << 16)

/* BR00 */
#define XY_BLT_WRITE_ALPHA	(1 << 21)
#define XY_BLT_WRITE_RGB	(1 << 20)
#define XY_SRC_TILED		(1 << 15)
#define XY_DST_TILED		(1 << 11)

/* BR00 */
#define XY_FAST_SRC_TILED_64K        (3 << 20)
#define XY_FAST_SRC_TILED_Y          (2 << 20)
#define XY_FAST_SRC_TILED_X          (1 << 20)

#define XY_FAST_DST_TILED_64K        (3 << 13)
#define XY_FAST_DST_TILED_Y          (2 << 13)
#define XY_FAST_DST_TILED_X          (1 << 13)

/* BR13 */
#define BR13_8			(0x0 << 24)
#define BR13_565		(0x1 << 24)
#define BR13_8888		(0x3 << 24)
#define BR13_16161616		(0x4 << 24)
#define BR13_32323232		(0x5 << 24)

#define XY_FAST_SRC_TRMODE_YF        (1 << 31)
#define XY_FAST_DST_TRMODE_YF        (1 << 30)

/* Pipeline Statistics Counter Registers */
#define IA_VERTICES_COUNT               0x2310
#define IA_PRIMITIVES_COUNT             0x2318
#define VS_INVOCATION_COUNT             0x2320
#define HS_INVOCATION_COUNT             0x2300
#define DS_INVOCATION_COUNT             0x2308
#define GS_INVOCATION_COUNT             0x2328
#define GS_PRIMITIVES_COUNT             0x2330
#define CL_INVOCATION_COUNT             0x2338
#define CL_PRIMITIVES_COUNT             0x2340
#define PS_INVOCATION_COUNT             0x2348
#define CS_INVOCATION_COUNT             0x2290
#define PS_DEPTH_COUNT                  0x2350

#define GEN6_SO_PRIM_STORAGE_NEEDED     0x2280
#define GEN7_SO_PRIM_STORAGE_NEEDED(n)  (0x5240 + (n) * 8)

#define GEN6_SO_NUM_PRIMS_WRITTEN       0x2288
#define GEN7_SO_NUM_PRIMS_WRITTEN(n)    (0x5200 + (n) * 8)

#define GEN7_SO_WRITE_OFFSET(n)         (0x5280 + (n) * 4)

#define TIMESTAMP                       0x2358

#define BCS_SWCTRL                      0x22200
# define BCS_SWCTRL_SRC_Y               (1 << 0)
# define BCS_SWCTRL_DST_Y               (1 << 1)

#define OACONTROL                       0x2360
# define OACONTROL_COUNTER_SELECT_SHIFT  2
# define OACONTROL_ENABLE_COUNTERS       (1 << 0)

/* Auto-Draw / Indirect Registers */
#define GEN7_3DPRIM_END_OFFSET          0x2420
#define GEN7_3DPRIM_START_VERTEX        0x2430
#define GEN7_3DPRIM_VERTEX_COUNT        0x2434
#define GEN7_3DPRIM_INSTANCE_COUNT      0x2438
#define GEN7_3DPRIM_START_INSTANCE      0x243C
#define GEN7_3DPRIM_BASE_VERTEX         0x2440

/* Auto-Compute / Indirect Registers */
#define GEN7_GPGPU_DISPATCHDIMX         0x2500
#define GEN7_GPGPU_DISPATCHDIMY         0x2504
#define GEN7_GPGPU_DISPATCHDIMZ         0x2508

#define GEN7_CACHE_MODE_1               0x7004
# define GEN8_HIZ_NP_PMA_FIX_ENABLE        (1 << 11)
# define GEN8_HIZ_NP_EARLY_Z_FAILS_DISABLE (1 << 13)
# define GEN9_PARTIAL_RESOLVE_DISABLE_IN_VC (1 << 1)
# define GEN8_HIZ_PMA_MASK_BITS \
   REG_MASK(GEN8_HIZ_NP_PMA_FIX_ENABLE | GEN8_HIZ_NP_EARLY_Z_FAILS_DISABLE)

/* Predicate registers */
#define MI_PREDICATE_SRC0               0x2400
#define MI_PREDICATE_SRC1               0x2408
#define MI_PREDICATE_DATA               0x2410
#define MI_PREDICATE_RESULT             0x2418
#define MI_PREDICATE_RESULT_1           0x241C
#define MI_PREDICATE_RESULT_2           0x2214

#define HSW_CS_GPR(n) (0x2600 + (n) * 8)

/* L3 cache control registers. */
#define GEN7_L3SQCREG1                     0xb010
/* L3SQ general and high priority credit initialization. */
# define IVB_L3SQCREG1_SQGHPCI_DEFAULT     0x00730000
# define VLV_L3SQCREG1_SQGHPCI_DEFAULT     0x00d30000
# define HSW_L3SQCREG1_SQGHPCI_DEFAULT     0x00610000
# define GEN7_L3SQCREG1_CONV_DC_UC         (1 << 24)
# define GEN7_L3SQCREG1_CONV_IS_UC         (1 << 25)
# define GEN7_L3SQCREG1_CONV_C_UC          (1 << 26)
# define GEN7_L3SQCREG1_CONV_T_UC          (1 << 27)

#define GEN7_L3CNTLREG2                    0xb020
# define GEN7_L3CNTLREG2_SLM_ENABLE        (1 << 0)
# define GEN7_L3CNTLREG2_URB_ALLOC_SHIFT   1
# define GEN7_L3CNTLREG2_URB_ALLOC_MASK    INTEL_MASK(6, 1)
# define GEN7_L3CNTLREG2_URB_LOW_BW        (1 << 7)
# define GEN7_L3CNTLREG2_ALL_ALLOC_SHIFT   8
# define GEN7_L3CNTLREG2_ALL_ALLOC_MASK    INTEL_MASK(13, 8)
# define GEN7_L3CNTLREG2_RO_ALLOC_SHIFT    14
# define GEN7_L3CNTLREG2_RO_ALLOC_MASK     INTEL_MASK(19, 14)
# define GEN7_L3CNTLREG2_RO_LOW_BW         (1 << 20)
# define GEN7_L3CNTLREG2_DC_ALLOC_SHIFT    21
# define GEN7_L3CNTLREG2_DC_ALLOC_MASK     INTEL_MASK(26, 21)
# define GEN7_L3CNTLREG2_DC_LOW_BW         (1 << 27)

#define GEN7_L3CNTLREG3                    0xb024
# define GEN7_L3CNTLREG3_IS_ALLOC_SHIFT    1
# define GEN7_L3CNTLREG3_IS_ALLOC_MASK     INTEL_MASK(6, 1)
# define GEN7_L3CNTLREG3_IS_LOW_BW         (1 << 7)
# define GEN7_L3CNTLREG3_C_ALLOC_SHIFT     8
# define GEN7_L3CNTLREG3_C_ALLOC_MASK      INTEL_MASK(13, 8)
# define GEN7_L3CNTLREG3_C_LOW_BW          (1 << 14)
# define GEN7_L3CNTLREG3_T_ALLOC_SHIFT     15
# define GEN7_L3CNTLREG3_T_ALLOC_MASK      INTEL_MASK(20, 15)
# define GEN7_L3CNTLREG3_T_LOW_BW          (1 << 21)

#define HSW_SCRATCH1                       0xb038
#define HSW_SCRATCH1_L3_ATOMIC_DISABLE     (1 << 27)

#define HSW_ROW_CHICKEN3                   0xe49c
#define HSW_ROW_CHICKEN3_L3_ATOMIC_DISABLE (1 << 6)

#define GEN8_L3CNTLREG                     0x7034
# define GEN8_L3CNTLREG_SLM_ENABLE         (1 << 0)
# define GEN8_L3CNTLREG_URB_ALLOC_SHIFT    1
# define GEN8_L3CNTLREG_URB_ALLOC_MASK     INTEL_MASK(7, 1)
# define GEN8_L3CNTLREG_RO_ALLOC_SHIFT     11
# define GEN8_L3CNTLREG_RO_ALLOC_MASK      INTEL_MASK(17, 11)
# define GEN8_L3CNTLREG_DC_ALLOC_SHIFT     18
# define GEN8_L3CNTLREG_DC_ALLOC_MASK      INTEL_MASK(24, 18)
# define GEN8_L3CNTLREG_ALL_ALLOC_SHIFT    25
# define GEN8_L3CNTLREG_ALL_ALLOC_MASK     INTEL_MASK(31, 25)
