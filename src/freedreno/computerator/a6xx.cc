/*
 * Copyright © 2020 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ir3/ir3_compiler.h"

#include "util/u_math.h"

#include "fd6_hw.h"

#include "common/freedreno_dev_info.h"
#include "fdl/freedreno_layout.h"

#include "ir3_asm.h"
#include "main.h"

#define FD_BO_NO_HARDPIN 1
#include "common/fd6_pack.h"

struct a6xx_backend {
   struct backend base;

   struct ir3_compiler *compiler;
   struct fd_device *dev;

   const struct fd_dev_info *info;

   unsigned seqno;
   struct fd_bo *control_mem;

   struct fd_bo *query_mem;
   const struct perfcntr *perfcntrs;
   unsigned num_perfcntrs;
};
define_cast(backend, a6xx_backend);

/*
 * Data structures shared with GPU:
 */

/* This struct defines the layout of the fd6_context::control buffer: */
struct fd6_control {
   uint32_t seqno; /* seqno for async CP_EVENT_WRITE, etc */
   uint32_t _pad0;
   volatile uint32_t vsc_overflow;
   uint32_t _pad1;
   /* flag set from cmdstream when VSC overflow detected: */
   uint32_t vsc_scratch;
   uint32_t _pad2;
   uint32_t _pad3;
   uint32_t _pad4;

   /* scratch space for VPC_SO[i].FLUSH_BASE_LO/HI, start on 32 byte boundary. */
   struct {
      uint32_t offset;
      uint32_t pad[7];
   } flush_base[4];
};

#define control_ptr(a6xx_backend, member)                                      \
   (a6xx_backend)->control_mem, offsetof(struct fd6_control, member)

struct PACKED fd6_query_sample {
   uint64_t start;
   uint64_t result;
   uint64_t stop;
};

/* offset of a single field of an array of fd6_query_sample: */
#define query_sample_idx(a6xx_backend, idx, field)                             \
   (a6xx_backend)->query_mem,                                                  \
      (idx * sizeof(struct fd6_query_sample)) +                                \
         offsetof(struct fd6_query_sample, field)

/*
 * Backend implementation:
 */

static struct kernel *
a6xx_assemble(struct backend *b, FILE *in)
{
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(b);
   struct ir3_kernel *ir3_kernel = ir3_asm_assemble(a6xx_backend->compiler, in);
   ir3_kernel->backend = b;
   return &ir3_kernel->base;
}

static void
a6xx_disassemble(struct kernel *kernel, struct ir3_disasm_options *options)
{
   ir3_asm_disassemble(to_ir3_kernel(kernel), options);
}

template<chip CHIP>
static void
cs_restore_emit(fd_cs &cs, struct a6xx_backend *a6xx_backend)
{
   fd_ncrb<CHIP> ncrb(cs, 2 + ARRAY_SIZE(a6xx_backend->info->magic_raw));

   ncrb.add(A6XX_SP_PERFCTR_SHADER_MASK(.cs = true));

   /* KMD programs this (and blocks UMD access) on gen8+: */
   if (CHIP < A8XX)
      ncrb.add(A6XX_SP_NC_MODE_CNTL_2());

   for (size_t i = 0; i < ARRAY_SIZE(a6xx_backend->info->magic_raw); i++) {
      auto magic_reg = a6xx_backend->info->magic_raw[i];
      if (!magic_reg.reg)
         break;

      ncrb.add({magic_reg.reg, magic_reg.value});
   }
}

template<chip CHIP>
static void
cs_program_emit_regs(fd_cs &cs, struct kernel *kernel)
{
   struct ir3_kernel *ir3_kernel = to_ir3_kernel(kernel);
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(ir3_kernel->backend);
   struct ir3_shader_variant *v = ir3_kernel->v;
   const unsigned *local_size = kernel->local_size;
   const struct ir3_info *i = &v->info;
   enum a6xx_threadsize thrsz = i->double_threadsize ? THREAD128 : THREAD64;
   fd_crb crb(cs, 25);

   crb.add(A6XX_SP_MODE_CNTL(
      .constant_demotion_enable = true,
      .isammode = ISAMMODE_GL,
      .shared_consts_enable = false,
   ));

   crb.add(SP_UPDATE_CNTL(CHIP,
      .vs_state = true,
      .hs_state = true,
      .ds_state = true,
      .gs_state = true,
      .fs_state = true,
      .cs_state = true,
      .gfx_uav = true,
   ));

   unsigned constlen = align(v->constlen, 4);
   crb.add(SP_CS_CONST_CONFIG(CHIP, .constlen = constlen, .enabled = true, ));

   crb.add(A6XX_SP_CS_CONFIG(
      .enabled = true,
      .ntex = v->num_samp,
      .nsamp = v->num_samp,
      .nuav = kernel->num_bufs,
   ));
   crb.add(A6XX_SP_CS_INSTR_SIZE(v->instrlen));

   crb.add(A6XX_SP_CS_CNTL_0(
      .halfregfootprint = i->max_half_reg + 1,
      .fullregfootprint = i->max_reg + 1,
      .branchstack = ir3_shader_branchstack_hw(v),
      .threadsize = thrsz,
      .earlypreamble = v->early_preamble,
      .mergedregs = v->mergedregs,
   ));

   if (CHIP >= A7XX) {
      crb.add(SP_PS_WAVE_CNTL(CHIP, .threadsize = THREAD64));

      crb.add(SP_REG_PROG_ID_0(CHIP, .dword = 0xfcfcfcfc));
      crb.add(SP_REG_PROG_ID_1(CHIP, .dword = 0xfcfcfcfc));
      crb.add(SP_REG_PROG_ID_2(CHIP, .dword = 0xfcfcfcfc));
      crb.add(SP_REG_PROG_ID_3(CHIP, .dword = 0x0000fc00));
   }

   uint32_t shared_size = MAX2(((int)v->shared_size - 1) / 1024, 1);
   enum a6xx_const_ram_mode mode =
      v->constlen > 256 ? CONSTLEN_512 :
      (v->constlen > 192 ? CONSTLEN_256 :
      (v->constlen > 128 ? CONSTLEN_192 : CONSTLEN_128));
   crb.add(A6XX_SP_CS_CNTL_1(.shared_size = shared_size, .constantrammode = mode));

   if (CHIP == A6XX && a6xx_backend->info->props.has_lpac) {
      crb.add(HLSQ_CS_CTRL_REG1(CHIP, .shared_size = 1, .constantrammode = mode));
   }

   uint32_t local_invocation_id, work_group_id;
   local_invocation_id =
      ir3_find_sysval_regid(v, SYSTEM_VALUE_LOCAL_INVOCATION_ID);
   work_group_id = ir3_find_sysval_regid(v, SYSTEM_VALUE_WORKGROUP_ID);

   if (CHIP == A6XX) {
      crb.add(SP_CS_CONST_CONFIG_0(CHIP,
         .wgidconstid = work_group_id,
         .wgsizeconstid = INVALID_REG,
         .wgoffsetconstid = INVALID_REG,
         .localidregid = local_invocation_id,
      ));
      crb.add(SP_CS_WGE_CNTL(CHIP,
         .linearlocalidregid = INVALID_REG,
         .threadsize = thrsz,
      ));
   } else {
      unsigned tile_height = (local_size[1] % 8 == 0)   ? 3
                             : (local_size[1] % 4 == 0) ? 5
                             : (local_size[1] % 2 == 0) ? 9
                                                        : 17;

      crb.add(SP_CS_WGE_CNTL(CHIP,
         .linearlocalidregid = INVALID_REG,
         .threadsize = thrsz,
         .workgrouprastorderzfirsten = true,
         .wgtilewidth = 4,
         .wgtileheight = tile_height,
      ));
   }

   if (CHIP >= A7XX || a6xx_backend->info->props.has_lpac) {
      crb.add(A6XX_SP_CS_WIE_CNTL_0(
         .wgidconstid = work_group_id,
         .wgsizeconstid = INVALID_REG,
         .wgoffsetconstid = INVALID_REG,
         .localidregid = local_invocation_id,
      ));

      if (CHIP >= A7XX) {
         /* TODO allow the shader to control the tiling */
         crb.add(SP_CS_WIE_CNTL_1(CHIP,
            .linearlocalidregid = INVALID_REG,
            .threadsize = thrsz,
            .workitemrastorder = WORKITEMRASTORDER_LINEAR,
         ));
      } else {
         crb.add(SP_CS_WIE_CNTL_1(CHIP,
            .linearlocalidregid = INVALID_REG,
            .threadsize = thrsz,
         ));
      }
   }

   crb.attach_bo(v->bo);

   crb.add(A6XX_SP_CS_BASE(v->bo));
   crb.add(A6XX_SP_CS_INSTR_SIZE(v->instrlen));

   if (v->pvtmem_size > 0) {
      uint32_t per_fiber_size = v->pvtmem_size;
      uint32_t per_sp_size =
         align(per_fiber_size * a6xx_backend->info->fibers_per_sp, 1 << 12);
      uint32_t total_size = per_sp_size * a6xx_backend->info->num_sp_cores;

      struct fd_bo *pvtmem = fd_bo_new(a6xx_backend->dev, total_size, 0, "pvtmem");
      crb.add(A6XX_SP_CS_PVT_MEM_PARAM(.memsizeperitem = per_fiber_size));
      crb.add(A6XX_SP_CS_PVT_MEM_BASE(pvtmem));
      crb.add(A6XX_SP_CS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = v->pvtmem_per_wave,
      ));

      crb.add(A6XX_SP_CS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
   }
}

template<chip CHIP>
static void
cs_program_emit(fd_cs &cs, struct kernel *kernel)
{
   struct ir3_kernel *ir3_kernel = to_ir3_kernel(kernel);
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(ir3_kernel->backend);
   struct ir3_shader_variant *v = ir3_kernel->v;

   cs_program_emit_regs<CHIP>(cs, kernel);

   uint32_t shader_preload_size =
      MIN2(v->instrlen, a6xx_backend->info->props.instr_cache_size);

   fd_pkt7(cs, CP_LOAD_STATE6_FRAG, 3)
      .add(CP_LOAD_STATE6_0(
         .state_type = ST6_SHADER,
         .state_src = SS6_INDIRECT,
         .state_block = SB6_CS_SHADER,
         .num_unit = shader_preload_size,
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR(v->bo));
}

template<chip CHIP>
static void
emit_const(fd_cs &cs, uint32_t regid, uint32_t sizedwords, const uint32_t *dwords)
{
   uint32_t zero[4] = {};
   uint32_t align_sz;

   assert((regid % 4) == 0);

   align_sz = align(sizedwords, 4);

   fd_pkt7(cs, CP_LOAD_STATE6_FRAG, 3 + align_sz)
      .add(CP_LOAD_STATE6_0(
         .dst_off = regid / 4,
         .state_type = ST6_CONSTANTS,
         .state_src = SS6_DIRECT,
         .state_block = SB6_CS_SHADER,
         .num_unit = DIV_ROUND_UP(sizedwords, 4)
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR())
      .add(dwords, sizedwords)
      /* Zero-pad to multiple of 4 dwords */
      .add(zero, align_sz - sizedwords);
}

template<chip CHIP>
static void
cs_const_emit(fd_cs &cs, struct kernel *kernel, uint32_t grid[3])
{
   struct ir3_kernel *ir3_kernel = to_ir3_kernel(kernel);
   struct ir3_shader_variant *v = ir3_kernel->v;

   const struct ir3_const_state *const_state = ir3_const_state(v);
   uint32_t base = const_state->allocs.max_const_offset_vec4;
   const struct ir3_imm_const_state *imm_state = &v->imm_state;
   int size = DIV_ROUND_UP(imm_state->count, 4);

   if (ir3_kernel->info.numwg != INVALID_REG) {
      assert((ir3_kernel->info.numwg & 0x3) == 0);
      int idx = ir3_kernel->info.numwg >> 2;
      imm_state->values[idx * 4 + 0] = grid[0];
      imm_state->values[idx * 4 + 1] = grid[1];
      imm_state->values[idx * 4 + 2] = grid[2];
   }

   for (int i = 0; i < MAX_BUFS; i++) {
      if (kernel->buf_addr_regs[i] != INVALID_REG) {
         assert((kernel->buf_addr_regs[i] & 0x3) == 0);
         int idx = kernel->buf_addr_regs[i] >> 2;

         uint64_t iova = fd_bo_get_iova(kernel->bufs[i]);

         imm_state->values[idx * 4 + 1] = iova >> 32;
         imm_state->values[idx * 4 + 0] = (iova << 32) >> 32;
      }
   }

   /* truncate size to avoid writing constants that shader
    * does not use:
    */
   size = MIN2(size + base, v->constlen) - base;

   /* convert out of vec4: */
   base *= 4;
   size *= 4;

   if (size > 0) {
      emit_const<CHIP>(cs, base, size, imm_state->values);
   }
}

static unsigned
kernel_num_bufs(struct kernel *kernel, enum kernel_buf_type buf_type)
{
   unsigned num_bufs = 0;

   for (unsigned i = 0; i < kernel->num_bufs; i++) {
      if (kernel->buf_types[i] == buf_type) {
         num_bufs++;
      }
   }

   return num_bufs;
}

template<chip CHIP>
static void
cs_uav_emit(fd_cs &cs, struct fd_device *dev, struct kernel *kernel)
{
   unsigned num_bufs = kernel_num_bufs(kernel, KERNEL_BUF_UAV);

   if (num_bufs == 0) {
      return;
   }

   struct fd_bo *state = fd_bo_new(dev, kernel->num_bufs * 16 * 4,
                                   FD_BO_GPUREADONLY | FD_BO_HINT_COMMAND,
                                   "tex_desc");
   fd_bo_mark_for_dump(state);

   cs.attach_bo(state);

   uint32_t *buf = (uint32_t *)fd_bo_map(state);

   for (unsigned i = 0; i < kernel->num_bufs; i++) {
      if (kernel->buf_types[i] != KERNEL_BUF_UAV) {
         continue;
      }

      cs.attach_bo(kernel->bufs[i]);

      uint64_t iova = fd_bo_get_iova(kernel->bufs[i]);
      uint32_t descriptor[16];

      static const uint8_t swiz_identity[4] = {
         PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y,
         PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W,
      };

      fdl6_buffer_view_init<CHIP>(descriptor, PIPE_FORMAT_R32_UINT, swiz_identity,
                                  iova, kernel->buf_sizes[i]);

      memcpy(buf, descriptor, 16 * 4);
      buf += 16;
   }

   fd_pkt7(cs, CP_LOAD_STATE6_FRAG, 3)
      .add(CP_LOAD_STATE6_0(
         .state_type = ST6_UAV,
         .state_src = SS6_INDIRECT,
         .state_block = SB6_CS_SHADER,
         .num_unit = num_bufs,
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR(state));

   fd_crb crb(cs, 3);

   crb.add(SP_CS_UAV_BASE(CHIP, state));
   crb.add(A6XX_SP_CS_USIZE(num_bufs));

   fd_bo_del(state);
}

static void
cs_ubo_emit(fd_cs &cs, struct kernel *kernel)
{
   unsigned num_bufs = kernel_num_bufs(kernel, KERNEL_BUF_UBO);

   if (num_bufs == 0) {
      return;
   }

   for (unsigned i = 0, offset = 0; i < kernel->num_bufs; i++) {
      if (kernel->buf_types[i] != KERNEL_BUF_UBO) {
         continue;
      }

      cs.attach_bo(kernel->bufs[i]);

      unsigned size_vec4s = DIV_ROUND_UP(kernel->buf_sizes[i], 4);

      fd_pkt7(cs, CP_LOAD_STATE6_FRAG, 5)
         .add(CP_LOAD_STATE6_0(
            .dst_off = offset,
            .state_type = ST6_UBO,
            .state_src = SS6_DIRECT,
            .state_block = SB6_CS_SHADER,
            .num_unit = 1,
         ))
         .add(CP_LOAD_STATE6_EXT_SRC_ADDR())
         .add(A6XX_UBO_DESC(0, kernel->bufs[i], 0, size_vec4s));

      offset++;
   }
}

template<chip CHIP>
static inline unsigned
event_write(fd_cs &cs, struct kernel *kernel, enum vgt_event_type evt, bool timestamp)
{
   unsigned seqno = 0;
   unsigned len = timestamp ? 4 : 1;

   fd_pkt7 pkt(cs, CP_EVENT_WRITE, len);

   if (CHIP == A6XX) {
      pkt.add(CP_EVENT_WRITE_0_EVENT(evt));
   } else {
      pkt.add(CP_EVENT_WRITE7_0_EVENT(evt) |
              CP_EVENT_WRITE7_0_WRITE_SRC(EV_WRITE_USER_32B) |
              COND(timestamp, CP_EVENT_WRITE7_0_WRITE_ENABLED));
   }

   if (timestamp) {
      struct ir3_kernel *ir3_kernel = to_ir3_kernel(kernel);
      struct a6xx_backend *a6xx_backend = to_a6xx_backend(ir3_kernel->backend);
      seqno = ++a6xx_backend->seqno;
      pkt.add(CP_EVENT_WRITE_ADDR(control_ptr(a6xx_backend, seqno)));
      pkt.add(seqno);
   }

   return seqno;
}

template<chip CHIP>
static inline void
cache_flush(fd_cs &cs, struct kernel *kernel)
{
   struct ir3_kernel *ir3_kernel = to_ir3_kernel(kernel);
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(ir3_kernel->backend);
   unsigned seqno;

   seqno = event_write<CHIP>(cs, kernel, RB_DONE_TS, true);

   fd_pkt7(cs, CP_WAIT_REG_MEM, 6)
      .add(CP_WAIT_REG_MEM_0(.function = WRITE_EQ, .poll = POLL_MEMORY))
      .add(CP_WAIT_REG_MEM_POLL_ADDR(control_ptr(a6xx_backend, seqno)))
      .add(CP_WAIT_REG_MEM_3(.ref = seqno))
      .add(CP_WAIT_REG_MEM_4(.mask = ~0))
      .add(CP_WAIT_REG_MEM_5(.delay_loop_cycles = 16));

   if (CHIP == A6XX) {
      seqno = event_write<CHIP>(cs, kernel, CACHE_FLUSH_TS, true);

      fd_pkt7(cs, CP_WAIT_MEM_GTE, 4)
         .add(CP_WAIT_MEM_GTE_0())
         .add(CP_WAIT_MEM_GTE_POLL_ADDR(control_ptr(a6xx_backend, seqno)))
         .add(CP_WAIT_MEM_GTE_3(.ref = seqno));
   } else {
      event_write<CHIP>(cs, kernel, CACHE_FLUSH7, false);
   }
}

template<chip CHIP>
static void
a6xx_emit_grid(struct kernel *kernel, uint32_t grid[3],
               struct fd_submit *submit)
{
   struct ir3_kernel *ir3_kernel = to_ir3_kernel(kernel);
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(ir3_kernel->backend);
   fd_cs cs(fd_submit_new_ringbuffer(submit, 0,
      (enum fd_ringbuffer_flags)(FD_RINGBUFFER_PRIMARY | FD_RINGBUFFER_GROWABLE)));

   cs.attach_bo(a6xx_backend->control_mem);

   cs_restore_emit<CHIP>(cs, a6xx_backend);
   cs_program_emit<CHIP>(cs, kernel);
   cs_const_emit<CHIP>(cs, kernel, grid);
   cs_uav_emit<CHIP>(cs, a6xx_backend->dev, kernel);
   cs_ubo_emit(cs, kernel);

   fd_pkt7(cs, CP_SET_MARKER, 1)
      .add(A6XX_CP_SET_MARKER_0(.mode = RM6_COMPUTE));

   const unsigned *local_size = kernel->local_size;
   const unsigned *num_groups = grid;

   unsigned work_dim = 0;
   for (int i = 0; i < 3; i++) {
      if (!grid[i])
         break;
      work_dim++;
   }

   with_crb (cs, 11) {
      crb.add(SP_CS_NDRANGE_0(CHIP,
         .kerneldim = work_dim,
         .localsizex = local_size[0] - 1,
         .localsizey = local_size[1] - 1,
         .localsizez = local_size[2] - 1,
      ));

      if (CHIP >= A7XX) {
         crb.add(SP_CS_NDRANGE_7(CHIP,
            .localsizex = local_size[0] - 1,
            .localsizey = local_size[1] - 1,
            .localsizez = local_size[2] - 1,
         ));
      }

      crb.add(SP_CS_NDRANGE_1(CHIP,
         .globalsize_x = local_size[0] * num_groups[0],
      ));
      crb.add(SP_CS_NDRANGE_2(CHIP, 0));
      crb.add(SP_CS_NDRANGE_3(CHIP,
         .globalsize_y = local_size[1] * num_groups[1],
      ));
      crb.add(SP_CS_NDRANGE_4(CHIP, 0));
      crb.add(SP_CS_NDRANGE_5(CHIP,
         .globalsize_z = local_size[2] * num_groups[2],
      ));
      crb.add(SP_CS_NDRANGE_6(CHIP, 0));

      crb.add(SP_CS_KERNEL_GROUP_X(CHIP, 1));
      crb.add(SP_CS_KERNEL_GROUP_Y(CHIP, 1));
      crb.add(SP_CS_KERNEL_GROUP_Z(CHIP, 1));
   }

   if (a6xx_backend->num_perfcntrs > 0) {
      a6xx_backend->query_mem = fd_bo_new(
         a6xx_backend->dev,
         a6xx_backend->num_perfcntrs * sizeof(struct fd6_query_sample), 0, "query");

      /* configure the performance counters to count the requested
       * countables:
       */
      for (unsigned i = 0; i < a6xx_backend->num_perfcntrs; i++) {
         const struct perfcntr *counter = &a6xx_backend->perfcntrs[i];

         fd_pkt4(cs, 1).add({
            .reg = counter->select_reg,
            .value = counter->selector,
         });
      }

      fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

      /* and snapshot the start values: */
      for (unsigned i = 0; i < a6xx_backend->num_perfcntrs; i++) {
         const struct perfcntr *counter = &a6xx_backend->perfcntrs[i];

         fd_pkt7(cs, CP_REG_TO_MEM, 3)
            .add(CP_REG_TO_MEM_0(.reg = counter->counter_reg_lo, ._64b = true))
            .add(A5XX_CP_REG_TO_MEM_DEST(query_sample_idx(a6xx_backend, i, start)));
      }
   }

   fd_pkt7(cs, CP_EXEC_CS, 4)
      .add(CP_EXEC_CS_0())
      .add(CP_EXEC_CS_1(.ngroups_x = grid[0]))
      .add(CP_EXEC_CS_2(.ngroups_y = grid[1]))
      .add(CP_EXEC_CS_3(.ngroups_z = grid[2]));

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   if (a6xx_backend->num_perfcntrs > 0) {
      /* snapshot the end values: */
      for (unsigned i = 0; i < a6xx_backend->num_perfcntrs; i++) {
         const struct perfcntr *counter = &a6xx_backend->perfcntrs[i];

         fd_pkt7(cs, CP_REG_TO_MEM, 3)
            .add(CP_REG_TO_MEM_0(.reg = counter->counter_reg_lo, ._64b = true))
            .add(A5XX_CP_REG_TO_MEM_DEST(query_sample_idx(a6xx_backend, i, stop)));
      }

      /* and compute the result: */
      for (unsigned i = 0; i < a6xx_backend->num_perfcntrs; i++) {
         /* result += stop - start: */
         fd_pkt7(cs, CP_MEM_TO_MEM, 9)
            .add(CP_MEM_TO_MEM_0(.neg_c = true, ._double = true))
            .add(CP_MEM_TO_MEM_DST(query_sample_idx(a6xx_backend, i, result)))
            .add(CP_MEM_TO_MEM_SRC_A(query_sample_idx(a6xx_backend, i, result)))
            .add(CP_MEM_TO_MEM_SRC_B(query_sample_idx(a6xx_backend, i, stop)))
            .add(CP_MEM_TO_MEM_SRC_C(query_sample_idx(a6xx_backend, i, start)));
      }
   }

   cache_flush<CHIP>(cs, kernel);
}

static void
a6xx_set_perfcntrs(struct backend *b, const struct perfcntr *perfcntrs,
                   unsigned num_perfcntrs)
{
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(b);

   a6xx_backend->perfcntrs = perfcntrs;
   a6xx_backend->num_perfcntrs = num_perfcntrs;
}

static void
a6xx_read_perfcntrs(struct backend *b, uint64_t *results)
{
   struct a6xx_backend *a6xx_backend = to_a6xx_backend(b);

   fd_bo_cpu_prep(a6xx_backend->query_mem, NULL, FD_BO_PREP_READ);
   struct fd6_query_sample *samples =
      (struct fd6_query_sample *)fd_bo_map(a6xx_backend->query_mem);

   for (unsigned i = 0; i < a6xx_backend->num_perfcntrs; i++) {
      results[i] = samples[i].result;
   }
}

template<chip CHIP>
struct backend *
a6xx_init(struct fd_device *dev, const struct fd_dev_id *dev_id)
{
   struct a6xx_backend *a6xx_backend =
      (struct a6xx_backend *)calloc(1, sizeof(*a6xx_backend));

   a6xx_backend->base = (struct backend){
      .assemble = a6xx_assemble,
      .disassemble = a6xx_disassemble,
      .emit_grid = a6xx_emit_grid<CHIP>,
      .set_perfcntrs = a6xx_set_perfcntrs,
      .read_perfcntrs = a6xx_read_perfcntrs,
   };

   struct ir3_compiler_options compiler_options = {
      .disable_cache = true,
   };
   a6xx_backend->compiler =
      ir3_compiler_create(dev, dev_id, fd_dev_info_raw(dev_id), &compiler_options);
   a6xx_backend->dev = dev;

   a6xx_backend->info = fd_dev_info_raw(dev_id);

   a6xx_backend->control_mem =
      fd_bo_new(dev, 0x1000, 0, "control");

   return &a6xx_backend->base;
}
FD_GENX(a6xx_init);
