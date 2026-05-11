/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/****************
* VPE OP Codes
****************/
enum VPE_CMD_OPCODE {
    VPE_CMD_OPCODE_NOP              = 0x0,
    VPE_CMD_OPCODE_VPE_DESC         = 0x1,
    VPE_CMD_OPCODE_PLANE_CFG        = 0x2,
    VPE_CMD_OPCODE_VPEP_CFG         = 0x3,
    VPE_CMD_OPCODE_INDIRECT_BUFFER  = 0x4,
    VPE_CMD_OPCODE_FENCE            = 0x5,
    VPE_CMD_OPCODE_TRAP             = 0x6,
    VPE_CMD_OPCODE_REG_WRITE        = 0x7,
    VPE_CMD_OPCODE_POLL_REGMEM      = 0x8,
    VPE_CMD_OPCODE_COND_EXE         = 0x9,
    VPE_CMD_OPCODE_ATOMIC           = 0xA,
    VPE_CMD_OPCODE_PLANE_FILL       = 0xB,
    VPE_CMD_OPCODE_COLLABORATE_SYNC = 0xC,
    VPE_CMD_OPCODE_TIMESTAMP        = 0xD,
    VPE_CMD_OPCODE_QUERY_RESOLVE    = 0xF,
    VPE_CMD_OPCODE_SET_PREDICATION  = 0x9
};

/** Generic Command Header
 * Generic Commands include:
 *  Noop, Fence, Trap,
 *  RegisterWrite, PollRegisterWriteMemory,
 *  SetLocalTimestamp, GetLocalTimestamp
 *  GetGlobalGPUTimestamp */
#define VPE_HEADER_SUB_OPCODE__SHIFT 8
#define VPE_HEADER_SUB_OPCODE_MASK   0x0000FF00
#define VPE_HEADER_OPCODE__SHIFT     0
#define VPE_HEADER_OPCODE_MASK       0x000000FF

#define VPE_CMD_HEADER(op, subop)                                                                  \
    (((subop << VPE_HEADER_SUB_OPCODE__SHIFT) & VPE_HEADER_SUB_OPCODE_MASK) |                      \
        ((op << VPE_HEADER_OPCODE__SHIFT) & VPE_HEADER_OPCODE_MASK))

#define VPE_PREDICATION_SUB_OPCODE     1
#define VPE_PREDICATION_CMD_SIZE       16
#define VPE_PREDICATION_POLARITY_SHIFT 31
#define VPE_PREDICATION_ADDR_SHIFT     32
#define VPE_PREDICATION_HIGH_ADDR_MASK 0xFFFFFFFF00000000
#define VPE_PREDICATION_LOW_ADDR_MASK  0x00000000FFFFFFFF

#define VPE_TIMESTAMP_SUB_OPCODE     2
#define VPE_TIMESTAMP_CMD_SIZE       12
#define VPE_TIMESTAMP_ADDR_SHIFT     32
#define VPE_TIMESTAMP_HIGH_ADDR_MASK 0xFFFFFFFF00000000
#define VPE_TIMESTAMP_LOW_ADDR_MASK  0x00000000FFFFFFFF

#define VPE_RESOLVE_QUERY_SUB_OPCODE     0
#define VPE_RESOLVE_QUERY_CMD_SIZE       24
#define VPE_RESOLVE_QUERY_ADDR_SHIFT     32
#define VPE_RESOLVE_QUERY_HIGH_ADDR_MASK 0xFFFFFFFF00000000
#define VPE_RESOLVE_QUERY_LOW_ADDR_MASK  0x00000000FFFFFFFF

/************************
 * VPEP Config
 ************************/
enum VPE_VPEP_CFG_SUBOP {
    VPE_VPEP_CFG_SUBOP_DIR_CFG   = 0x0,
    VPE_VPEP_CFG_SUBOP_IND_CFG   = 0x1,
    VPE_VPEP_CFG_SUBOP_3DLUT_CFG = 0x2,
};

// Direct Config Command Header
#define VPE_DIR_CFG_HEADER_ARRAY_SIZE__SHIFT 16
#define VPE_DIR_CFG_HEADER_ARRAY_SIZE_MASK   0xFFFF0000

#define VPE_DIR_CFG_CMD_HEADER(arr_sz)                                                             \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_VPEP_CFG, VPE_VPEP_CFG_SUBOP_DIR_CFG) |                         \
        (((arr_sz) << VPE_DIR_CFG_HEADER_ARRAY_SIZE__SHIFT) & VPE_DIR_CFG_HEADER_ARRAY_SIZE_MASK))
#define VPE_DIR_CFG_PKT_REGISTER_OFFSET__SHIFT 2
#define VPE_DIR_CFG_PKT_REGISTER_OFFSET_MASK   0x000FFFFC

#define VPE_DIR_CFG_PKT_DATA_SIZE__SHIFT 20
#define VPE_DIR_CFG_PKT_DATA_SIZE_MASK   0xFFF00000

// InDirect Config Command Header
#define VPE_IND_CFG_HEADER_NUM_DST__SHIFT 28
#define VPE_IND_CFG_HEADER_NUM_DST_MASK   0xF0000000

#define VPE_IND_CFG_CMD_HEADER(num_dst)                                                            \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_VPEP_CFG, VPE_VPEP_CFG_SUBOP_IND_CFG) |                         \
        ((((uint32_t)num_dst) << VPE_IND_CFG_HEADER_NUM_DST__SHIFT) &                              \
            VPE_IND_CFG_HEADER_NUM_DST_MASK))

#define VPE_IND_CFG_DATA_ARRAY_SIZE__SHIFT 0
#define VPE_IND_CFG_DATA_ARRAY_SIZE_MASK   0x0007FFFF

#define VPE_IND_CFG_PKT_REGISTER_OFFSET__SHIFT 2
#define VPE_IND_CFG_PKT_REGISTER_OFFSET_MASK   0x000FFFFC

// VPEP 3D LUT Config Command Header
#define VPE_3DLUT_CFG_HEADER_ADDR_MOD__SHIFT 31
#define VPE_3DLUT_CFG_HEADER_ADDR_MOD_MASK   0x80000000
#define VPE_3DLUT_CFG_HEADER_PITCH_MOD__SHIFT 30
#define VPE_3DLUT_CFG_HEADER_PITCH_MOD_MASK   0x40000000

#define VPE_3DLUT_CFG_CMD_HEADER(addr_mode, mem_align)                                             \
    (VPE_CMD_HEADER(VPE_CMD_OPCODE_VPEP_CFG, VPE_VPEP_CFG_SUBOP_3DLUT_CFG) |                       \
        ((((uint32_t)addr_mode) << VPE_3DLUT_CFG_HEADER_ADDR_MOD__SHIFT) &                         \
            VPE_3DLUT_CFG_HEADER_ADDR_MOD_MASK) |                                                  \
        ((((uint32_t)mem_align) << VPE_3DLUT_CFG_HEADER_PITCH_MOD__SHIFT) &                        \
            VPE_3DLUT_CFG_HEADER_PITCH_MOD_MASK))

#define VPE_3DLUT_CFG_COMP_MODE__SHIFT 5
#define VPE_3DLUT_CFG_COMP_MODE_MASK   0x20

/**************************
* Poll Reg/Mem Sub-OpCode
**************************/
enum VPE_POLL_REGMEM_SUBOP {
    VPE_POLL_REGMEM_SUBOP_REGMEM = 0x0,
    VPE_POLL_REGMEM_SUBOP_REGMEM_WRITE = 0x1,
};

#ifdef __cplusplus
}
#endif
