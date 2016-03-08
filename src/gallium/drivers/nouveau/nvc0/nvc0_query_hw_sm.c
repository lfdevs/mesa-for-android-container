/*
 * Copyright 2011 Christoph Bumiller
 * Copyright 2015 Samuel Pitoiset
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#define NVC0_PUSH_EXPLICIT_SPACE_CHECKING

#include "nvc0/nvc0_context.h"
#include "nvc0/nvc0_query_hw_sm.h"

#include "nv_object.xml.h"
#include "nvc0/nve4_compute.xml.h"
#include "nvc0/nvc0_compute.xml.h"

/* === PERFORMANCE MONITORING COUNTERS for NVE4+ === */

/* NOTE: intentionally using the same names as NV */
static const char *nve4_hw_sm_query_names[] =
{
   /* MP counters */
   "active_cycles",
   "active_warps",
   "atom_cas_count",
   "atom_count",
   "branch",
   "divergent_branch",
   "gld_request",
   "global_ld_mem_divergence_replays",
   "global_store_transaction",
   "global_st_mem_divergence_replays",
   "gred_count",
   "gst_request",
   "inst_executed",
   "inst_issued1",
   "inst_issued2",
   "l1_global_load_hit",
   "l1_global_load_miss",
   "__l1_global_load_transactions",
   "__l1_global_store_transactions",
   "l1_local_load_hit",
   "l1_local_load_miss",
   "l1_local_store_hit",
   "l1_local_store_miss",
   "l1_shared_load_transactions",
   "l1_shared_store_transactions",
   "local_load",
   "local_load_transactions",
   "local_store",
   "local_store_transactions",
   "prof_trigger_00",
   "prof_trigger_01",
   "prof_trigger_02",
   "prof_trigger_03",
   "prof_trigger_04",
   "prof_trigger_05",
   "prof_trigger_06",
   "prof_trigger_07",
   "shared_load",
   "shared_load_replay",
   "shared_store",
   "shared_store_replay",
   "sm_cta_launched",
   "threads_launched",
   "uncached_global_load_transaction",
   "warps_launched",
};

/* Code to read out MP counters: They are accessible via mmio, too, but let's
 * just avoid mapping registers in userspace. We'd have to know which MPs are
 * enabled/present, too, and that information is not presently exposed.
 * We could add a kernel interface for it, but reading the counters like this
 * has the advantage of being async (if get_result isn't called immediately).
 */
static const uint64_t nve4_read_hw_sm_counters_code[] =
{
   /* sched 0x20 0x20 0x20 0x20 0x20 0x20 0x20
    * mov b32 $r8 $tidx
    * mov b32 $r12 $physid
    * mov b32 $r0 $pm0
    * mov b32 $r1 $pm1
    * mov b32 $r2 $pm2
    * mov b32 $r3 $pm3
    * mov b32 $r4 $pm4
    * sched 0x20 0x20 0x23 0x04 0x20 0x04 0x2b
    * mov b32 $r5 $pm5
    * mov b32 $r6 $pm6
    * mov b32 $r7 $pm7
    * set $p0 0x1 eq u32 $r8 0x0
    * mov b32 $r10 c0[0x0]
    * ext u32 $r8 $r12 0x414
    * mov b32 $r11 c0[0x4]
    * sched 0x04 0x2e 0x04 0x20 0x20 0x28 0x04
    * ext u32 $r9 $r12 0x208
    * (not $p0) exit
    * set $p1 0x1 eq u32 $r9 0x0
    * mul $r8 u32 $r8 u32 96
    * mul $r12 u32 $r9 u32 16
    * mul $r13 u32 $r9 u32 4
    * add b32 $r9 $r8 $r13
    * sched 0x28 0x04 0x2c 0x04 0x2c 0x04 0x2c
    * add b32 $r8 $r8 $r12
    * mov b32 $r12 $r10
    * add b32 $r10 $c $r10 $r8
    * mov b32 $r13 $r11
    * add b32 $r11 $r11 0x0 $c
    * add b32 $r12 $c $r12 $r9
    * st b128 wt g[$r10d] $r0q
    * sched 0x4 0x2c 0x20 0x04 0x2e 0x00 0x00
    * mov b32 $r0 c0[0x8]
    * add b32 $r13 $r13 0x0 $c
    * $p1 st b128 wt g[$r12d+0x40] $r4q
    * st b32 wt g[$r12d+0x50] $r0
    * exit */
   0x2202020202020207ULL,
   0x2c00000084021c04ULL,
   0x2c0000000c031c04ULL,
   0x2c00000010001c04ULL,
   0x2c00000014005c04ULL,
   0x2c00000018009c04ULL,
   0x2c0000001c00dc04ULL,
   0x2c00000020011c04ULL,
   0x22b0420042320207ULL,
   0x2c00000024015c04ULL,
   0x2c00000028019c04ULL,
   0x2c0000002c01dc04ULL,
   0x190e0000fc81dc03ULL,
   0x2800400000029de4ULL,
   0x7000c01050c21c03ULL,
   0x280040001002dde4ULL,
   0x204282020042e047ULL,
   0x7000c00820c25c03ULL,
   0x80000000000021e7ULL,
   0x190e0000fc93dc03ULL,
   0x1000000180821c02ULL,
   0x1000000040931c02ULL,
   0x1000000010935c02ULL,
   0x4800000034825c03ULL,
   0x22c042c042c04287ULL,
   0x4800000030821c03ULL,
   0x2800000028031de4ULL,
   0x4801000020a29c03ULL,
   0x280000002c035de4ULL,
   0x0800000000b2dc42ULL,
   0x4801000024c31c03ULL,
   0x9400000000a01fc5ULL,
   0x200002e04202c047ULL,
   0x2800400020001de4ULL,
   0x0800000000d35c42ULL,
   0x9400000100c107c5ULL,
   0x9400000140c01f85ULL,
   0x8000000000001de7ULL
};

/* For simplicity, we will allocate as many group slots as we allocate counter
 * slots. This means that a single counter which wants to source from 2 groups
 * will have to be declared as using 2 counter slots. This shouldn't really be
 * a problem because such queries don't make much sense ... (unless someone is
 * really creative).
 */
struct nvc0_hw_sm_counter_cfg
{
   uint32_t func    : 16; /* mask or 4-bit logic op (depending on mode) */
   uint32_t mode    : 4;  /* LOGOP,B6,LOGOP_B6(_PULSE) */
   uint32_t sig_dom : 1;  /* if 0, MP_PM_A (per warp-sched), if 1, MP_PM_B */
   uint32_t sig_sel : 8;  /* signal group */
   uint32_t src_mask;     /* mask for signal selection (only for NVC0:NVE4) */
   uint32_t src_sel;      /* signal selection for up to 4 sources */
};

struct nvc0_hw_sm_query_cfg
{
   struct nvc0_hw_sm_counter_cfg ctr[8];
   uint8_t num_counters;
   uint8_t norm[2]; /* normalization num,denom */
};

#define _Q1A(n, f, m, g, s, nu, dn) [NVE4_HW_SM_QUERY_##n] = { { { f, NVE4_COMPUTE_MP_PM_FUNC_MODE_##m, 0, NVE4_COMPUTE_MP_PM_A_SIGSEL_##g, 0, s }, {}, {}, {} }, 1, { nu, dn } }
#define _Q1B(n, f, m, g, s, nu, dn) [NVE4_HW_SM_QUERY_##n] = { { { f, NVE4_COMPUTE_MP_PM_FUNC_MODE_##m, 1, NVE4_COMPUTE_MP_PM_B_SIGSEL_##g, 0, s }, {}, {}, {} }, 1, { nu, dn } }

/* NOTES:
 * active_warps: bit 0 alternates btw 0 and 1 for odd nr of warps
 * inst_executed etc.: we only count a single warp scheduler
 */
static const struct nvc0_hw_sm_query_cfg nve4_hw_sm_queries[] =
{
   _Q1B(ACTIVE_CYCLES, 0x0001, B6, WARP, 0x00000000, 1, 1),
   _Q1B(ACTIVE_WARPS,  0x003f, B6, WARP, 0x31483104, 2, 1),
   _Q1A(ATOM_CAS_COUNT, 0x0001, B6, BRANCH, 0x000000004, 1, 1),
   _Q1A(ATOM_COUNT, 0x0001, B6, BRANCH, 0x00000000, 1, 1),
   _Q1A(BRANCH,           0x0001, B6, BRANCH, 0x0000000c, 1, 1),
   _Q1A(DIVERGENT_BRANCH, 0x0001, B6, BRANCH, 0x00000010, 1, 1),
   _Q1A(GLD_REQUEST, 0x0001, B6, LDST, 0x00000010, 1, 1),
   _Q1B(GLD_MEM_DIV_REPLAY, 0x0001, B6, REPLAY, 0x00000010, 1, 1),
   _Q1B(GST_TRANSACTIONS,          0x0001, B6, MEM, 0x00000004, 1, 1),
   _Q1B(GST_MEM_DIV_REPLAY, 0x0001, B6, REPLAY, 0x00000014, 1, 1),
   _Q1A(GRED_COUNT, 0x0001, B6, BRANCH, 0x00000008, 1, 1),
   _Q1A(GST_REQUEST, 0x0001, B6, LDST, 0x00000014, 1, 1),
   _Q1A(INST_EXECUTED, 0x0003, B6, EXEC,  0x00000398, 1, 1),
   _Q1A(INST_ISSUED1,  0x0001, B6, ISSUE, 0x00000004, 1, 1),
   _Q1A(INST_ISSUED2,  0x0001, B6, ISSUE, 0x00000008, 1, 1),
   _Q1B(L1_GLD_HIT,  0x0001, B6, L1, 0x00000010, 1, 1),
   _Q1B(L1_GLD_MISS, 0x0001, B6, L1, 0x00000014, 1, 1),
   _Q1B(L1_GLD_TRANSACTIONS,  0x0001, B6, UNK0F, 0x00000000, 1, 1),
   _Q1B(L1_GST_TRANSACTIONS,  0x0001, B6, UNK0F, 0x00000004, 1, 1),
   _Q1B(L1_LOCAL_LD_HIT,   0x0001, B6, L1, 0x00000000, 1, 1),
   _Q1B(L1_LOCAL_LD_MISS,  0x0001, B6, L1, 0x00000004, 1, 1),
   _Q1B(L1_LOCAL_ST_HIT,  0x0001, B6, L1, 0x00000008, 1, 1),
   _Q1B(L1_LOCAL_ST_MISS, 0x0001, B6, L1, 0x0000000c, 1, 1),
   _Q1B(L1_SHARED_LD_TRANSACTIONS, 0x0001, B6, TRANSACTION, 0x00000008, 1, 1),
   _Q1B(L1_SHARED_ST_TRANSACTIONS, 0x0001, B6, TRANSACTION, 0x0000000c, 1, 1),
   _Q1A(LOCAL_LD,    0x0001, B6, LDST, 0x00000008, 1, 1),
   _Q1B(LOCAL_LD_TRANSACTIONS, 0x0001, B6, TRANSACTION, 0x00000000, 1, 1),
   _Q1A(LOCAL_ST,    0x0001, B6, LDST, 0x0000000c, 1, 1),
   _Q1B(LOCAL_ST_TRANSACTIONS, 0x0001, B6, TRANSACTION, 0x00000004, 1, 1),
   _Q1A(PROF_TRIGGER_0, 0x0001, B6, USER, 0x00000000, 1, 1),
   _Q1A(PROF_TRIGGER_1, 0x0001, B6, USER, 0x00000004, 1, 1),
   _Q1A(PROF_TRIGGER_2, 0x0001, B6, USER, 0x00000008, 1, 1),
   _Q1A(PROF_TRIGGER_3, 0x0001, B6, USER, 0x0000000c, 1, 1),
   _Q1A(PROF_TRIGGER_4, 0x0001, B6, USER, 0x00000010, 1, 1),
   _Q1A(PROF_TRIGGER_5, 0x0001, B6, USER, 0x00000014, 1, 1),
   _Q1A(PROF_TRIGGER_6, 0x0001, B6, USER, 0x00000018, 1, 1),
   _Q1A(PROF_TRIGGER_7, 0x0001, B6, USER, 0x0000001c, 1, 1),
   _Q1A(SHARED_LD,   0x0001, B6, LDST, 0x00000000, 1, 1),
   _Q1B(SHARED_LD_REPLAY, 0x0001, B6, REPLAY, 0x00000008, 1, 1),
   _Q1A(SHARED_ST,   0x0001, B6, LDST, 0x00000004, 1, 1),
   _Q1B(SHARED_ST_REPLAY, 0x0001, B6, REPLAY, 0x0000000c, 1, 1),
   _Q1B(SM_CTA_LAUNCHED,      0x0001, B6, WARP, 0x0000001c, 1, 1),
   _Q1A(THREADS_LAUNCHED,  0x003f, B6, LAUNCH, 0x398a4188, 1, 1),
   _Q1B(UNCACHED_GLD_TRANSACTIONS, 0x0001, B6, MEM, 0x00000000, 1, 1),
   _Q1A(WARPS_LAUNCHED,    0x0001, B6, LAUNCH, 0x00000004, 1, 1),
};

#undef _Q1A
#undef _Q1B

/* === PERFORMANCE MONITORING COUNTERS for NVC0:NVE4 === */
/* NOTES:
 * - MP counters on GF100/GF110 (compute capability 2.0) are buggy
 *   because there is a context-switch problem that we need to fix.
 *   Results might be wrong sometimes, be careful!
 */
static const char *nvc0_hw_sm_query_names[] =
{
   /* MP counters */
   "active_cycles",
   "active_warps",
   "atom_count",
   "branch",
   "divergent_branch",
   "gld_request",
   "gred_count",
   "gst_request",
   "inst_executed",
   "inst_issued",
   "inst_issued1_0",
   "inst_issued1_1",
   "inst_issued2_0",
   "inst_issued2_1",
   "local_load",
   "local_store",
   "prof_trigger_00",
   "prof_trigger_01",
   "prof_trigger_02",
   "prof_trigger_03",
   "prof_trigger_04",
   "prof_trigger_05",
   "prof_trigger_06",
   "prof_trigger_07",
   "shared_load",
   "shared_store",
   "threads_launched",
   "thread_inst_executed_0",
   "thread_inst_executed_1",
   "thread_inst_executed_2",
   "thread_inst_executed_3",
   "warps_launched",
};

static const uint64_t nvc0_read_hw_sm_counters_code[] =
{
   /* mov b32 $r8 $tidx
    * mov b32 $r9 $physid
    * mov b32 $r0 $pm0
    * mov b32 $r1 $pm1
    * mov b32 $r2 $pm2
    * mov b32 $r3 $pm3
    * mov b32 $r4 $pm4
    * mov b32 $r5 $pm5
    * mov b32 $r6 $pm6
    * mov b32 $r7 $pm7
    * set $p0 0x1 eq u32 $r8 0x0
    * mov b32 $r10 c0[0x0]
    * mov b32 $r11 c0[0x4]
    * ext u32 $r8 $r9 0x414
    * (not $p0) exit
    * mul $r8 u32 $r8 u32 48
    * add b32 $r10 $c $r10 $r8
    * add b32 $r11 $r11 0x0 $c
    * mov b32 $r8 c0[0x8]
    * st b128 wt g[$r10d+0x00] $r0q
    * st b128 wt g[$r10d+0x10] $r4q
    * st b32 wt g[$r10d+0x20] $r8
    * exit */
   0x2c00000084021c04ULL,
   0x2c0000000c025c04ULL,
   0x2c00000010001c04ULL,
   0x2c00000014005c04ULL,
   0x2c00000018009c04ULL,
   0x2c0000001c00dc04ULL,
   0x2c00000020011c04ULL,
   0x2c00000024015c04ULL,
   0x2c00000028019c04ULL,
   0x2c0000002c01dc04ULL,
   0x190e0000fc81dc03ULL,
   0x2800400000029de4ULL,
   0x280040001002dde4ULL,
   0x7000c01050921c03ULL,
   0x80000000000021e7ULL,
   0x10000000c0821c02ULL,
   0x4801000020a29c03ULL,
   0x0800000000b2dc42ULL,
   0x2800400020021de4ULL,
   0x9400000000a01fc5ULL,
   0x9400000040a11fc5ULL,
   0x9400000080a21f85ULL,
   0x8000000000001de7ULL
};

#define _C(f, o, g, m, s) { f, NVC0_COMPUTE_MP_PM_OP_MODE_##o, 0, g, m, s }
#define _Q(n, c) [NVC0_HW_SM_QUERY_##n] = c

/* ==== Compute capability 2.0 (GF100/GF110) ==== */
static const struct nvc0_hw_sm_query_cfg
sm20_active_cycles =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x11, 0x000000ff, 0x00000000),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_active_warps =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x24, 0x000000ff, 0x00000010),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x24, 0x000000ff, 0x00000020),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0x24, 0x000000ff, 0x00000030),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0x24, 0x000000ff, 0x00000040),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0x24, 0x000000ff, 0x00000050),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0x24, 0x000000ff, 0x00000060),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_atom_count =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x63, 0x000000ff, 0x00000030),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_branch =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x1a, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x1a, 0x000000ff, 0x00000010),
   .num_counters = 2,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_divergent_branch =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x19, 0x000000ff, 0x00000020),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x19, 0x000000ff, 0x00000030),
   .num_counters = 2,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_gld_request =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x64, 0x000000ff, 0x00000030),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_gred_count =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x63, 0x000000ff, 0x00000040),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_gst_request =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x64, 0x000000ff, 0x00000060),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_inst_executed =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x2d, 0x0000ffff, 0x00001000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x2d, 0x0000ffff, 0x00001010),
   .num_counters = 2,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_inst_issued =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x27, 0x0000ffff, 0x00007060),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x27, 0x0000ffff, 0x00007070),
   .num_counters = 2,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_local_ld =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x64, 0x000000ff, 0x00000020),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_local_st =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x64, 0x000000ff, 0x00000050),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_0 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000000),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_1 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000010),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_2 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000020),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_3 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000030),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_4 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000040),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_5 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000050),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_6 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000060),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_prof_trigger_7 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x01, 0x000000ff, 0x00000070),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_shared_ld =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x64, 0x000000ff, 0x00000010),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_shared_st =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x64, 0x000000ff, 0x00000040),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_threads_launched =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000010),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000020),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000030),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000040),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000050),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000060),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_th_inst_executed_0 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x2f, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x2f, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0x2f, 0x000000ff, 0x00000020),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0x2f, 0x000000ff, 0x00000030),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0x2f, 0x000000ff, 0x00000040),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0x2f, 0x000000ff, 0x00000050),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_th_inst_executed_1 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x30, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x30, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0x30, 0x000000ff, 0x00000020),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0x30, 0x000000ff, 0x00000030),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0x30, 0x000000ff, 0x00000040),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0x30, 0x000000ff, 0x00000050),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm20_warps_launched =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x26, 0x000000ff, 0x00000000),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg *sm20_hw_sm_queries[] =
{
   _Q(ACTIVE_CYCLES,       &sm20_active_cycles),
   _Q(ACTIVE_WARPS,        &sm20_active_warps),
   _Q(ATOM_COUNT,          &sm20_atom_count),
   _Q(BRANCH,              &sm20_branch),
   _Q(DIVERGENT_BRANCH,    &sm20_divergent_branch),
   _Q(GLD_REQUEST,         &sm20_gld_request),
   _Q(GRED_COUNT,          &sm20_gred_count),
   _Q(GST_REQUEST,         &sm20_gst_request),
   _Q(INST_EXECUTED,       &sm20_inst_executed),
   _Q(INST_ISSUED,         &sm20_inst_issued),
   _Q(INST_ISSUED1_0,      NULL),
   _Q(INST_ISSUED1_1,      NULL),
   _Q(INST_ISSUED2_0,      NULL),
   _Q(INST_ISSUED2_1,      NULL),
   _Q(LOCAL_LD,            &sm20_local_ld),
   _Q(LOCAL_ST,            &sm20_local_st),
   _Q(PROF_TRIGGER_0,      &sm20_prof_trigger_0),
   _Q(PROF_TRIGGER_1,      &sm20_prof_trigger_1),
   _Q(PROF_TRIGGER_2,      &sm20_prof_trigger_2),
   _Q(PROF_TRIGGER_3,      &sm20_prof_trigger_3),
   _Q(PROF_TRIGGER_4,      &sm20_prof_trigger_4),
   _Q(PROF_TRIGGER_5,      &sm20_prof_trigger_5),
   _Q(PROF_TRIGGER_6,      &sm20_prof_trigger_6),
   _Q(PROF_TRIGGER_7,      &sm20_prof_trigger_7),
   _Q(SHARED_LD,           &sm20_shared_ld),
   _Q(SHARED_ST,           &sm20_shared_st),
   _Q(THREADS_LAUNCHED,    &sm20_threads_launched),
   _Q(TH_INST_EXECUTED_0,  &sm20_th_inst_executed_0),
   _Q(TH_INST_EXECUTED_1,  &sm20_th_inst_executed_1),
   _Q(TH_INST_EXECUTED_2,  NULL),
   _Q(TH_INST_EXECUTED_3,  NULL),
   _Q(WARPS_LAUNCHED,      &sm20_warps_launched),
};

/* ==== Compute capability 2.1 (GF108+ except GF110) ==== */
static const struct nvc0_hw_sm_query_cfg
sm21_inst_executed =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x2d, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0x2d, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0x2d, 0x000000ff, 0x00000020),
   .num_counters = 3,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_inst_issued1_0 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x7e, 0x000000ff, 0x00000010),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_inst_issued1_1 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x7e, 0x000000ff, 0x00000040),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_inst_issued2_0 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x7e, 0x000000ff, 0x00000020),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_inst_issued2_1 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0x7e, 0x000000ff, 0x00000050),
   .num_counters = 1,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_th_inst_executed_0 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0xa3, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0xa3, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0xa3, 0x000000ff, 0x00000020),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0xa3, 0x000000ff, 0x00000030),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0xa3, 0x000000ff, 0x00000040),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0xa3, 0x000000ff, 0x00000050),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_th_inst_executed_1 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0xa5, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0xa5, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0xa5, 0x000000ff, 0x00000020),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0xa5, 0x000000ff, 0x00000030),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0xa5, 0x000000ff, 0x00000040),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0xa5, 0x000000ff, 0x00000050),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_th_inst_executed_2 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0xa4, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0xa4, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0xa4, 0x000000ff, 0x00000020),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0xa4, 0x000000ff, 0x00000030),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0xa4, 0x000000ff, 0x00000040),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0xa4, 0x000000ff, 0x00000050),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg
sm21_th_inst_executed_3 =
{
   .ctr[0]       = _C(0xaaaa, LOGOP, 0xa6, 0x000000ff, 0x00000000),
   .ctr[1]       = _C(0xaaaa, LOGOP, 0xa6, 0x000000ff, 0x00000010),
   .ctr[2]       = _C(0xaaaa, LOGOP, 0xa6, 0x000000ff, 0x00000020),
   .ctr[3]       = _C(0xaaaa, LOGOP, 0xa6, 0x000000ff, 0x00000030),
   .ctr[4]       = _C(0xaaaa, LOGOP, 0xa6, 0x000000ff, 0x00000040),
   .ctr[5]       = _C(0xaaaa, LOGOP, 0xa6, 0x000000ff, 0x00000050),
   .num_counters = 6,
   .norm         = { 1, 1 },
};

static const struct nvc0_hw_sm_query_cfg *sm21_hw_sm_queries[] =
{
   _Q(ACTIVE_CYCLES,       &sm20_active_cycles),
   _Q(ACTIVE_WARPS,        &sm20_active_warps),
   _Q(ATOM_COUNT,          &sm20_atom_count),
   _Q(BRANCH,              &sm20_branch),
   _Q(DIVERGENT_BRANCH,    &sm20_divergent_branch),
   _Q(GLD_REQUEST,         &sm20_gld_request),
   _Q(GRED_COUNT,          &sm20_gred_count),
   _Q(GST_REQUEST,         &sm20_gst_request),
   _Q(INST_EXECUTED,       &sm21_inst_executed),
   _Q(INST_ISSUED,         NULL),
   _Q(INST_ISSUED1_0,      &sm21_inst_issued1_0),
   _Q(INST_ISSUED1_1,      &sm21_inst_issued1_1),
   _Q(INST_ISSUED2_0,      &sm21_inst_issued2_0),
   _Q(INST_ISSUED2_1,      &sm21_inst_issued2_1),
   _Q(LOCAL_LD,            &sm20_local_ld),
   _Q(LOCAL_ST,            &sm20_local_st),
   _Q(PROF_TRIGGER_0,      &sm20_prof_trigger_0),
   _Q(PROF_TRIGGER_1,      &sm20_prof_trigger_1),
   _Q(PROF_TRIGGER_2,      &sm20_prof_trigger_2),
   _Q(PROF_TRIGGER_3,      &sm20_prof_trigger_3),
   _Q(PROF_TRIGGER_4,      &sm20_prof_trigger_4),
   _Q(PROF_TRIGGER_5,      &sm20_prof_trigger_5),
   _Q(PROF_TRIGGER_6,      &sm20_prof_trigger_6),
   _Q(PROF_TRIGGER_7,      &sm20_prof_trigger_7),
   _Q(SHARED_LD,           &sm20_shared_ld),
   _Q(SHARED_ST,           &sm20_shared_st),
   _Q(THREADS_LAUNCHED,    &sm20_threads_launched),
   _Q(TH_INST_EXECUTED_0,  &sm21_th_inst_executed_0),
   _Q(TH_INST_EXECUTED_1,  &sm21_th_inst_executed_1),
   _Q(TH_INST_EXECUTED_2,  &sm21_th_inst_executed_2),
   _Q(TH_INST_EXECUTED_3,  &sm21_th_inst_executed_3),
   _Q(WARPS_LAUNCHED,      &sm20_warps_launched),
};

#undef _Q
#undef _C

static inline const struct nvc0_hw_sm_query_cfg **
nvc0_hw_sm_get_queries(struct nvc0_screen *screen)
{
   struct nouveau_device *dev = screen->base.device;

   if (dev->chipset == 0xc0 || dev->chipset == 0xc8)
      return sm20_hw_sm_queries;
   return sm21_hw_sm_queries;
}

static const struct nvc0_hw_sm_query_cfg *
nvc0_hw_sm_query_get_cfg(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_screen *screen = nvc0->screen;
   struct nvc0_query *q = &hq->base;

   if (screen->base.class_3d >= NVE4_3D_CLASS)
      return &nve4_hw_sm_queries[q->type - PIPE_QUERY_DRIVER_SPECIFIC];

   if (q->type >= NVC0_HW_SM_QUERY(0) && q->type <= NVC0_HW_SM_QUERY_LAST) {
      const struct nvc0_hw_sm_query_cfg **queries =
         nvc0_hw_sm_get_queries(screen);
      return queries[q->type - NVC0_HW_SM_QUERY(0)];
   }
   debug_printf("invalid query type: %d\n", q->type);
   return NULL;
}

static void
nvc0_hw_sm_destroy_query(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_query *q = &hq->base;
   nvc0_hw_query_allocate(nvc0, q, 0);
   nouveau_fence_ref(NULL, &hq->fence);
   FREE(hq);
}

static boolean
nve4_hw_sm_begin_query(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_screen *screen = nvc0->screen;
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nvc0_hw_sm_query *hsq = nvc0_hw_sm_query(hq);
   const struct nvc0_hw_sm_query_cfg *cfg;
   unsigned i, c;
   unsigned num_ab[2] = { 0, 0 };

   cfg = nvc0_hw_sm_query_get_cfg(nvc0, hq);

   /* check if we have enough free counter slots */
   for (i = 0; i < cfg->num_counters; ++i)
      num_ab[cfg->ctr[i].sig_dom]++;

   if (screen->pm.num_hw_sm_active[0] + num_ab[0] > 4 ||
       screen->pm.num_hw_sm_active[1] + num_ab[1] > 4) {
      NOUVEAU_ERR("Not enough free MP counter slots !\n");
      return false;
   }

   assert(cfg->num_counters <= 4);
   PUSH_SPACE(push, 4 * 8 * + 6);

   if (!screen->pm.mp_counters_enabled) {
      screen->pm.mp_counters_enabled = true;
      BEGIN_NVC0(push, SUBC_SW(0x06ac), 1);
      PUSH_DATA (push, 0x1fcb);
   }

   /* set sequence field to 0 (used to check if result is available) */
   for (i = 0; i < screen->mp_count; ++i)
      hq->data[i * 10 + 10] = 0;
   hq->sequence++;

   for (i = 0; i < cfg->num_counters; ++i) {
      const unsigned d = cfg->ctr[i].sig_dom;

      if (!screen->pm.num_hw_sm_active[d]) {
         uint32_t m = (1 << 22) | (1 << (7 + (8 * !d)));
         if (screen->pm.num_hw_sm_active[!d])
            m |= 1 << (7 + (8 * d));
         BEGIN_NVC0(push, SUBC_SW(0x0600), 1);
         PUSH_DATA (push, m);
      }
      screen->pm.num_hw_sm_active[d]++;

      for (c = d * 4; c < (d * 4 + 4); ++c) {
         if (!screen->pm.mp_counter[c]) {
            hsq->ctr[i] = c;
            screen->pm.mp_counter[c] = hsq;
            break;
         }
      }
      assert(c <= (d * 4 + 3)); /* must succeed, already checked for space */

      /* configure and reset the counter(s) */
     if (d == 0)
        BEGIN_NVC0(push, NVE4_COMPUTE(MP_PM_A_SIGSEL(c & 3)), 1);
     else
        BEGIN_NVC0(push, NVE4_COMPUTE(MP_PM_B_SIGSEL(c & 3)), 1);
     PUSH_DATA (push, cfg->ctr[i].sig_sel);
     BEGIN_NVC0(push, NVE4_COMPUTE(MP_PM_SRCSEL(c)), 1);
     PUSH_DATA (push, cfg->ctr[i].src_sel + 0x2108421 * (c & 3));
     BEGIN_NVC0(push, NVE4_COMPUTE(MP_PM_FUNC(c)), 1);
     PUSH_DATA (push, (cfg->ctr[i].func << 4) | cfg->ctr[i].mode);
     BEGIN_NVC0(push, NVE4_COMPUTE(MP_PM_SET(c)), 1);
     PUSH_DATA (push, 0);
   }
   return true;
}

static boolean
nvc0_hw_sm_begin_query(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_screen *screen = nvc0->screen;
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nvc0_hw_sm_query *hsq = nvc0_hw_sm_query(hq);
   const struct nvc0_hw_sm_query_cfg *cfg;
   unsigned i, c;

   if (screen->base.class_3d >= NVE4_3D_CLASS)
      return nve4_hw_sm_begin_query(nvc0, hq);

   cfg = nvc0_hw_sm_query_get_cfg(nvc0, hq);

   /* check if we have enough free counter slots */
   if (screen->pm.num_hw_sm_active[0] + cfg->num_counters > 8) {
      NOUVEAU_ERR("Not enough free MP counter slots !\n");
      return false;
   }

   assert(cfg->num_counters <= 8);
   PUSH_SPACE(push, 8 * 8 + 2);

   /* set sequence field to 0 (used to check if result is available) */
   for (i = 0; i < screen->mp_count; ++i) {
      const unsigned b = (0x30 / 4) * i;
      hq->data[b + 8] = 0;
   }
   hq->sequence++;

   for (i = 0; i < cfg->num_counters; ++i) {
      uint32_t mask_sel = 0x00000000;

      if (!screen->pm.num_hw_sm_active[0]) {
         BEGIN_NVC0(push, SUBC_SW(0x0600), 1);
         PUSH_DATA (push, 0x80000000);
      }
      screen->pm.num_hw_sm_active[0]++;

      for (c = 0; c < 8; ++c) {
         if (!screen->pm.mp_counter[c]) {
            hsq->ctr[i] = c;
            screen->pm.mp_counter[c] = hsq;
            break;
         }
      }

      /* Oddly-enough, the signal id depends on the slot selected on Fermi but
       * not on Kepler. Fortunately, the signal ids are just offseted by the
       * slot id! */
      mask_sel |= c;
      mask_sel |= (c << 8);
      mask_sel |= (c << 16);
      mask_sel |= (c << 24);
      mask_sel &= cfg->ctr[i].src_mask;

      /* configure and reset the counter(s) */
      BEGIN_NVC0(push, NVC0_COMPUTE(MP_PM_SIGSEL(c)), 1);
      PUSH_DATA (push, cfg->ctr[i].sig_sel);
      BEGIN_NVC0(push, NVC0_COMPUTE(MP_PM_SRCSEL(c)), 1);
      PUSH_DATA (push, cfg->ctr[i].src_sel | mask_sel);
      BEGIN_NVC0(push, NVC0_COMPUTE(MP_PM_OP(c)), 1);
      PUSH_DATA (push, (cfg->ctr[i].func << 4) | cfg->ctr[i].mode);
      BEGIN_NVC0(push, NVC0_COMPUTE(MP_PM_SET(c)), 1);
      PUSH_DATA (push, 0);
   }
   return true;
}

static void
nvc0_hw_sm_end_query(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_screen *screen = nvc0->screen;
   struct pipe_context *pipe = &nvc0->base.pipe;
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   const bool is_nve4 = screen->base.class_3d >= NVE4_3D_CLASS;
   struct nvc0_hw_sm_query *hsq = nvc0_hw_sm_query(hq);
   struct pipe_grid_info info = {};
   uint32_t mask;
   uint32_t input[3];
   const uint block[3] = { 32, is_nve4 ? 4 : 1, 1 };
   const uint grid[3] = { screen->mp_count, screen->gpc_count, 1 };
   unsigned c, i;

   if (unlikely(!screen->pm.prog)) {
      struct nvc0_program *prog = CALLOC_STRUCT(nvc0_program);
      prog->type = PIPE_SHADER_COMPUTE;
      prog->translated = true;
      prog->parm_size = 12;
      if (is_nve4) {
         prog->code = (uint32_t *)nve4_read_hw_sm_counters_code;
         prog->code_size = sizeof(nve4_read_hw_sm_counters_code);
         prog->num_gprs = 14;
      } else {
         prog->code = (uint32_t *)nvc0_read_hw_sm_counters_code;
         prog->code_size = sizeof(nvc0_read_hw_sm_counters_code);
         prog->num_gprs = 12;
      }
      screen->pm.prog = prog;
   }

   /* disable all counting */
   PUSH_SPACE(push, 8);
   for (c = 0; c < 8; ++c)
      if (screen->pm.mp_counter[c]) {
         if (is_nve4) {
            IMMED_NVC0(push, NVE4_COMPUTE(MP_PM_FUNC(c)), 0);
         } else {
            IMMED_NVC0(push, NVC0_COMPUTE(MP_PM_OP(c)), 0);
         }
      }
   /* release counters for this query */
   for (c = 0; c < 8; ++c) {
      if (screen->pm.mp_counter[c] == hsq) {
         uint8_t d = is_nve4 ? c / 4 : 0; /* only one domain for NVC0:NVE4 */
         screen->pm.num_hw_sm_active[d]--;
         screen->pm.mp_counter[c] = NULL;
      }
   }

   BCTX_REFN_bo(nvc0->bufctx_cp, CP_QUERY, NOUVEAU_BO_GART | NOUVEAU_BO_WR,
                hq->bo);

   PUSH_SPACE(push, 1);
   IMMED_NVC0(push, SUBC_COMPUTE(NV50_GRAPH_SERIALIZE), 0);

   pipe->bind_compute_state(pipe, screen->pm.prog);
   input[0] = (hq->bo->offset + hq->base_offset);
   input[1] = (hq->bo->offset + hq->base_offset) >> 32;
   input[2] = hq->sequence;

   for (i = 0; i < 3; i++) {
      info.block[i] = block[i];
      info.grid[i] = grid[i];
   }
   info.pc = 0;
   info.input = input;
   pipe->launch_grid(pipe, &info);

   nouveau_bufctx_reset(nvc0->bufctx_cp, NVC0_BIND_CP_QUERY);

   /* re-activate other counters */
   PUSH_SPACE(push, 16);
   mask = 0;
   for (c = 0; c < 8; ++c) {
      const struct nvc0_hw_sm_query_cfg *cfg;
      unsigned i;

      hsq = screen->pm.mp_counter[c];
      if (!hsq)
         continue;

      cfg = nvc0_hw_sm_query_get_cfg(nvc0, &hsq->base);
      for (i = 0; i < cfg->num_counters; ++i) {
         if (mask & (1 << hsq->ctr[i]))
            break;
         mask |= 1 << hsq->ctr[i];
         if (is_nve4) {
            BEGIN_NVC0(push, NVE4_COMPUTE(MP_PM_FUNC(hsq->ctr[i])), 1);
         } else {
            BEGIN_NVC0(push, NVC0_COMPUTE(MP_PM_OP(hsq->ctr[i])), 1);
         }
         PUSH_DATA (push, (cfg->ctr[i].func << 4) | cfg->ctr[i].mode);
      }
   }
}

static inline bool
nvc0_hw_sm_query_read_data(uint32_t count[32][8],
                           struct nvc0_context *nvc0, bool wait,
                           struct nvc0_hw_query *hq,
                           const struct nvc0_hw_sm_query_cfg *cfg,
                           unsigned mp_count)
{
   struct nvc0_hw_sm_query *hsq = nvc0_hw_sm_query(hq);
   unsigned p, c;

   for (p = 0; p < mp_count; ++p) {
      const unsigned b = (0x30 / 4) * p;

      for (c = 0; c < cfg->num_counters; ++c) {
         if (hq->data[b + 8] != hq->sequence) {
            if (!wait)
               return false;
            if (nouveau_bo_wait(hq->bo, NOUVEAU_BO_RD, nvc0->base.client))
               return false;
         }
         count[p][c] = hq->data[b + hsq->ctr[c]] * (1 << c);
      }
   }
   return true;
}

static inline bool
nve4_hw_sm_query_read_data(uint32_t count[32][8],
                           struct nvc0_context *nvc0, bool wait,
                           struct nvc0_hw_query *hq,
                           const struct nvc0_hw_sm_query_cfg *cfg,
                           unsigned mp_count)
{
   struct nvc0_hw_sm_query *hsq = nvc0_hw_sm_query(hq);
   unsigned p, c, d;

   for (p = 0; p < mp_count; ++p) {
      const unsigned b = (0x60 / 4) * p;

      for (c = 0; c < cfg->num_counters; ++c) {
         count[p][c] = 0;
         for (d = 0; d < ((hsq->ctr[c] & ~3) ? 1 : 4); ++d) {
            if (hq->data[b + 20 + d] != hq->sequence) {
               if (!wait)
                  return false;
               if (nouveau_bo_wait(hq->bo, NOUVEAU_BO_RD, nvc0->base.client))
                  return false;
            }
            if (hsq->ctr[c] & ~0x3)
               count[p][c] = hq->data[b + 16 + (hsq->ctr[c] & 3)];
            else
               count[p][c] += hq->data[b + d * 4 + hsq->ctr[c]];
         }
      }
   }
   return true;
}

static boolean
nvc0_hw_sm_get_query_result(struct nvc0_context *nvc0, struct nvc0_hw_query *hq,
                            boolean wait, union pipe_query_result *result)
{
   uint32_t count[32][8];
   uint64_t value = 0;
   unsigned mp_count = MIN2(nvc0->screen->mp_count_compute, 32);
   unsigned p, c;
   const struct nvc0_hw_sm_query_cfg *cfg;
   bool ret;

   cfg = nvc0_hw_sm_query_get_cfg(nvc0, hq);

   if (nvc0->screen->base.class_3d >= NVE4_3D_CLASS)
      ret = nve4_hw_sm_query_read_data(count, nvc0, wait, hq, cfg, mp_count);
   else
      ret = nvc0_hw_sm_query_read_data(count, nvc0, wait, hq, cfg, mp_count);
   if (!ret)
      return false;

   for (c = 0; c < cfg->num_counters; ++c)
      for (p = 0; p < mp_count; ++p)
         value += count[p][c];
   value = (value * cfg->norm[0]) / cfg->norm[1];

   *(uint64_t *)result = value;
   return true;
}

static const struct nvc0_hw_query_funcs hw_sm_query_funcs = {
   .destroy_query = nvc0_hw_sm_destroy_query,
   .begin_query = nvc0_hw_sm_begin_query,
   .end_query = nvc0_hw_sm_end_query,
   .get_query_result = nvc0_hw_sm_get_query_result,
};

struct nvc0_hw_query *
nvc0_hw_sm_create_query(struct nvc0_context *nvc0, unsigned type)
{
   struct nvc0_screen *screen = nvc0->screen;
   struct nvc0_hw_sm_query *hsq;
   struct nvc0_hw_query *hq;
   unsigned space;

   if (nvc0->screen->base.drm->version < 0x01000101)
      return NULL;

   if ((type < NVE4_HW_SM_QUERY(0) || type > NVE4_HW_SM_QUERY_LAST) &&
       (type < NVC0_HW_SM_QUERY(0) || type > NVC0_HW_SM_QUERY_LAST))
      return NULL;

   hsq = CALLOC_STRUCT(nvc0_hw_sm_query);
   if (!hsq)
      return NULL;

   hq = &hsq->base;
   hq->funcs = &hw_sm_query_funcs;
   hq->base.type = type;

   if (screen->base.class_3d >= NVE4_3D_CLASS) {
       /* for each MP:
        * [00] = WS0.C0
        * [04] = WS0.C1
        * [08] = WS0.C2
        * [0c] = WS0.C3
        * [10] = WS1.C0
        * [14] = WS1.C1
        * [18] = WS1.C2
        * [1c] = WS1.C3
        * [20] = WS2.C0
        * [24] = WS2.C1
        * [28] = WS2.C2
        * [2c] = WS2.C3
        * [30] = WS3.C0
        * [34] = WS3.C1
        * [38] = WS3.C2
        * [3c] = WS3.C3
        * [40] = MP.C4
        * [44] = MP.C5
        * [48] = MP.C6
        * [4c] = MP.C7
        * [50] = WS0.sequence
        * [54] = WS1.sequence
        * [58] = WS2.sequence
        * [5c] = WS3.sequence
        */
       space = (4 * 4 + 4 + 4) * nvc0->screen->mp_count * sizeof(uint32_t);
   } else {
      /*
       * Note that padding is used to align memory access to 128 bits.
       *
       * for each MP:
       * [00] = MP.C0
       * [04] = MP.C1
       * [08] = MP.C2
       * [0c] = MP.C3
       * [10] = MP.C4
       * [14] = MP.C5
       * [18] = MP.C6
       * [1c] = MP.C7
       * [20] = MP.sequence
       * [24] = padding
       * [28] = padding
       * [2c] = padding
       */
      space = (8 + 1 + 3) * nvc0->screen->mp_count * sizeof(uint32_t);
   }

   if (!nvc0_hw_query_allocate(nvc0, &hq->base, space)) {
      FREE(hq);
      return NULL;
   }

   return hq;
}

static int
nvc0_hw_sm_get_next_query_id(const struct nvc0_hw_sm_query_cfg **queries,
                             unsigned id)
{
   unsigned i, next = 0;

   for (i = 0; i < NVC0_HW_SM_QUERY_COUNT; i++) {
      if (!queries[i]) {
         next++;
      } else
      if (i >= id && queries[id + next]) {
         break;
      }
   }
   return id + next;
}

int
nvc0_hw_sm_get_driver_query_info(struct nvc0_screen *screen, unsigned id,
                                 struct pipe_driver_query_info *info)
{
   int count = 0;

   if (screen->base.drm->version >= 0x01000101) {
      if (screen->compute) {
         if (screen->base.class_3d == NVE4_3D_CLASS) {
            count += NVE4_HW_SM_QUERY_COUNT;
         } else
         if (screen->base.class_3d < NVE4_3D_CLASS) {
            const struct nvc0_hw_sm_query_cfg **queries =
               nvc0_hw_sm_get_queries(screen);
            unsigned i;

            for (i = 0; i < NVC0_HW_SM_QUERY_COUNT; i++) {
               if (queries[i])
                  count++;
            }
         }
      }
   }

   if (!info)
      return count;

   if (id < count) {
      if (screen->compute) {
         if (screen->base.class_3d == NVE4_3D_CLASS) {
            info->name = nve4_hw_sm_query_names[id];
            info->query_type = NVE4_HW_SM_QUERY(id);
            info->group_id = NVC0_HW_SM_QUERY_GROUP;
            return 1;
         } else
         if (screen->base.class_3d < NVE4_3D_CLASS) {
            const struct nvc0_hw_sm_query_cfg **queries =
               nvc0_hw_sm_get_queries(screen);

            id = nvc0_hw_sm_get_next_query_id(queries, id);
            info->name = nvc0_hw_sm_query_names[id];
            info->query_type = NVC0_HW_SM_QUERY(id);
            info->group_id = NVC0_HW_SM_QUERY_GROUP;
            return 1;
         }
      }
   }
   return 0;
}
