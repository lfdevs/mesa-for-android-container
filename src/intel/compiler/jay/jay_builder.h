/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "jay_ir.h"
#include "jay_opcodes.h"

/* Like in NIR, for use with the builder */
enum jay_cursor_option {
   jay_cursor_after_block,
   jay_cursor_before_inst,
   jay_cursor_after_inst
};

typedef struct PACKED {
   union {
      jay_block *block;
      jay_inst *inst;
   };

   enum jay_cursor_option option;
} jay_cursor;

static inline bool
jay_cursors_equal(jay_cursor a, jay_cursor b)
{
   return !memcmp(&a, &b, sizeof(a));
}

static inline jay_cursor
jay_after_block(jay_block *block)
{
   return (jay_cursor) { .block = block, .option = jay_cursor_after_block };
}

static inline jay_cursor
jay_before_inst(jay_inst *I)
{
   return (jay_cursor) { .inst = I, .option = jay_cursor_before_inst };
}

static inline jay_cursor
jay_after_inst(jay_inst *I)
{
   return (jay_cursor) { .inst = I, .option = jay_cursor_after_inst };
}

static inline jay_cursor
jay_before_block(jay_block *block)
{
   jay_foreach_inst_in_block(block, I) {
      if (I->op != JAY_OPCODE_PHI_DST &&
          I->op != JAY_OPCODE_PRELOAD &&
          I->op != JAY_OPCODE_ELSE)
         return jay_before_inst(I);
   }

   /* Whole block is phis, so insert at the end */
   return jay_after_block(block);
}

static inline jay_cursor
jay_after_block_logical(jay_block *block)
{
   jay_foreach_inst_in_block_rev(block, I) {
      if (I->op != JAY_OPCODE_PHI_SRC && !jay_op_is_control_flow(I->op))
         return jay_after_inst(I);
   }

   /* Whole block is phis, so insert at the start */
   return jay_before_block(block);
}

static inline jay_cursor
jay_before_jump(jay_block *block)
{
   jay_inst *jump = jay_block_ending_jump(block);
   return jump ? jay_before_inst(jump) : jay_after_block(block);
}

/* Get a cursor at the start of a function, after any preloads */
static inline jay_cursor
jay_before_function(jay_function *f)
{
   jay_block *block = jay_first_block(f);

   jay_foreach_inst_in_block(block, I) {
      if (I->op != JAY_OPCODE_PRELOAD)
         return jay_before_inst(I);
   }

   /* The whole block is preloads, so insert at the end */
   return jay_after_block(block);
}

/*
 * Map a control flow edge to a block. If the block has one successor, the
 * predecessor is unique. Else, the successor is unique; the successor must not
 * have other predecessorss since there are no critical edges.
 */
static inline jay_block *
jay_edge_to_block(jay_block *pred, jay_block *succ, enum jay_file file)
{
   assert(jay_num_successors(pred, file) == 1 ||
          jay_num_predecessors(succ, file) == 1);

   return jay_num_successors(pred, file) == 1 ? pred : succ;
}

/*
 * Get a cursor to insert along a control flow edge: either at the start of
 * the successor or the end of the predecessor. This relies on the control
 * flow graph having no critical edges.
 */
static inline jay_cursor
jay_along_edge(jay_block *pred, jay_block *succ, enum jay_file file)
{
   jay_block *to = jay_edge_to_block(pred, succ, file);

   if (to == pred)
      return jay_after_block_logical(pred);
   else
      return jay_before_block(succ);
}

typedef struct {
   jay_shader *shader;
   jay_function *func;
   jay_cursor cursor;
} jay_builder;

static inline jay_builder
jay_init_builder(jay_function *f, jay_cursor cursor)
{
   return (jay_builder) { .shader = f->shader, .func = f, .cursor = cursor };
}

static inline void
jay_builder_insert(jay_builder *b, jay_inst *I)
{
   jay_cursor *cursor = &b->cursor;

   if (cursor->option == jay_cursor_after_inst) {
      list_add(&I->link, &cursor->inst->link);
   } else if (cursor->option == jay_cursor_after_block) {
      list_addtail(&I->link, &cursor->block->instructions);
   } else {
      assert(cursor->option == jay_cursor_before_inst);
      list_addtail(&I->link, &cursor->inst->link);
   }

   cursor->option = jay_cursor_after_inst;
   cursor->inst = I;
}

static inline jay_def
jay_alloc_def(jay_builder *b, enum jay_file file, unsigned size)
{
   unsigned idx = b->func->ssa_alloc;
   b->func->ssa_alloc += size;
   return jay_contiguous_def(file, idx, size);
}

/*
 * Collect SSA indices into a source. If the indices are not contiguous, this
 * uses a heap-allocated collect. Otherwise, a contiguous def is used.
 */
static inline jay_def
jay_collect(jay_builder *b,
            enum jay_file file,
            const uint32_t *indices,
            unsigned nr)
{
   if (nr == 0)
      return jay_null();

   for (unsigned i = 1; i < nr; ++i) {
      if (indices[i] != (indices[0] + i)) {
         static_assert(sizeof(uintptr_t) <= sizeof(uint64_t),
                       "sorry, no Morello support");
         void *dup =
            linear_memdup(b->shader->lin_ctx, indices, sizeof(uint32_t) * nr);
         uint64_t payload = (uintptr_t) dup;

         /* We require pointers to fit within (32+JAY_REG_BITS) bits. Luckily
          * this will always be the case on common architectures.
          */
         assert(payload < (1ull << (32 + JAY_REG_BITS)));

         return (jay_def) {
            ._payload = (uint32_t) payload,
            .reg = (uint32_t) (payload >> 32),
            .file = file,
            .num_values_m1 = nr - 1,
            .collect = true,
         };
      }
   }

   return jay_contiguous_def(file, indices[0], nr);
}

/*
 * Set the n'th channel of a def to index. This requires a copy-on-write.
 *
 * This implementation could likely be optimized.
 */
static inline void
jay_insert_channel(jay_builder *b, jay_def *d, unsigned c, jay_def scalar)
{
   uint32_t indices[JAY_MAX_DEF_LENGTH];
   uint32_t count = jay_num_values(*d);

   assert(scalar.file == d->file && !scalar.negate && !scalar.abs);
   assert(c < count && count <= ARRAY_SIZE(indices));

   /* First, decompress the def. */
   jay_foreach_comp(*d, i) {
      indices[i] = jay_channel(*d, i);
   }

   /* Next, update the indices in place */
   indices[c] = jay_index(scalar);

   /* Now collect it back. */
   jay_replace_src(d, jay_collect(b, d->file, indices, count));
}

/*
 * Concatenate a list of vectors, collecting all the indices in order.
 */
static inline jay_def
jay_collect_vectors(jay_builder *b, jay_def *vecs, uint32_t nr)
{
   uint32_t indices[JAY_MAX_DEF_LENGTH];
   uint32_t nr_indices = 0;

   for (unsigned i = 0; i < nr; ++i) {
      assert(vecs[i].file == vecs[0].file && jay_is_ssa(vecs[i]));
      assert(!vecs[i].negate && !vecs[i].abs);

      jay_foreach_comp(vecs[i], c) {
         assert(nr_indices < ARRAY_SIZE(indices));
         indices[nr_indices++] = jay_channel(vecs[i], c);
      }
   }

   return jay_collect(b, vecs[0].file, indices, nr_indices);
}

static inline jay_def
jay_collect_two(jay_builder *b, jay_def u, jay_def v)
{
   jay_def vecs[] = { u, v };
   return jay_collect_vectors(b, vecs, 2);
}

static inline jay_inst *
jay_alloc_inst(jay_builder *b,
               enum jay_opcode op,
               uint8_t num_srcs,
               unsigned extra_bytes)
{
   const size_t size =
      offsetof(jay_inst, src) + num_srcs * sizeof(jay_def) + extra_bytes;

   jay_inst *I = (jay_inst *) linear_zalloc_child(b->shader->lin_ctx, size);
   I->op = op;
   I->num_srcs = num_srcs;
   I->dst = jay_null();
   I->cond_flag = jay_null();

   return I;
}

static inline void
jay_shrink_sources(jay_inst *I, uint8_t new_num_srcs)
{
   assert(new_num_srcs < I->num_srcs);
   unsigned info_size = jay_inst_info_size(I);

   memmove(&I->src[new_num_srcs], &I->src[I->num_srcs], info_size);
   I->num_srcs = new_num_srcs;
}

static inline jay_inst *
jay_clone_inst(jay_builder *b, jay_inst *I, uint8_t new_num_srcs)
{
   assert(new_num_srcs >= I->num_srcs);
   unsigned info_size = jay_inst_info_size(I);

   jay_inst *clone = jay_alloc_inst(b, I->op, new_num_srcs, info_size);

   memcpy((uint8_t *) clone + sizeof(struct list_head),
          (uint8_t *) I + sizeof(struct list_head),
          sizeof(jay_inst) - sizeof(struct list_head));

   clone->num_srcs = new_num_srcs;

   memcpy(clone->src, I->src, I->num_srcs * sizeof(jay_def));
   memcpy(&clone->src[new_num_srcs], &I->src[I->num_srcs], info_size);
   return clone;
}

static inline jay_inst *
jay_grow_sources(jay_builder *b, jay_inst *I, uint8_t new_num_srcs)
{
   jay_inst *clone = jay_clone_inst(b, I, new_num_srcs);

   if ((b->cursor.option == jay_cursor_before_inst ||
        b->cursor.option == jay_cursor_after_inst) &&
       b->cursor.inst == I) {

      b->cursor.inst = clone;
   }

   jay_builder b_ = jay_init_builder(b->func, jay_before_inst(I));
   jay_builder_insert(&b_, clone);
   jay_remove_instruction(I);
   return clone;
}

static inline jay_inst *
jay_add_predicate_else(jay_builder *b,
                       jay_inst *I,
                       jay_def predicate,
                       jay_def default_value)
{
   assert(!I->predication && "pre-condition");
   assert(jay_is_flag(predicate) && jay_is_ssa(default_value));

   unsigned pred_index = I->num_srcs;
   I = jay_grow_sources(b, I, pred_index + 2);
   I->src[pred_index] = predicate;
   I->src[pred_index + 1] = default_value;
   I->predication = JAY_PREDICATED_DEFAULT;
   return I;
}

static inline jay_inst *
jay_add_predicate(jay_builder *b, jay_inst *I, jay_def predicate)
{
   assert(!I->predication && "pre-condition");
   assert(jay_is_flag(predicate));

   unsigned pred_index = I->num_srcs;
   I = jay_grow_sources(b, I, pred_index + 1);
   I->src[pred_index] = predicate;
   I->predication = JAY_PREDICATED;
   return I;
}

static inline jay_inst *
jay_set_cond_flag(jay_builder *b, jay_inst *I, jay_def cond_flag)
{
   assert(jay_is_flag(cond_flag) && jay_is_null(I->cond_flag));

   I->cond_flag = cond_flag;
   return I;
}

static inline jay_inst *
jay_set_conditional_mod(jay_builder *b,
                        jay_inst *I,
                        jay_def cond_flag,
                        enum jay_conditional_mod cmod)
{
   I->conditional_mod = cmod;
   return jay_set_cond_flag(b, I, cond_flag);
}

static inline jay_def
jay_identity_def(jay_def x)
{
   return x;
}

#ifdef __cplusplus
static inline jay_def
JAY_BUILD_SRC(jay_def x)
{
   return x;
}
static inline jay_def
JAY_BUILD_SRC(uint32_t x)
{
   return jay_imm(x);
}
#else
#define JAY_BUILD_SRC(X)                                                       \
   _Generic((X),                                                               \
      jay_def: jay_identity_def,                                               \
      uint32_t: jay_imm,                                                       \
      int32_t: jay_imm,                                                        \
      uint8_t: jay_imm)(X)
#endif

/* Include generated builder helpers */
#include "jay_builder_opcodes.h"

static inline jay_inst *
_jay_CMP(jay_builder *b,
         enum jay_type src_type,
         enum jay_conditional_mod cmod,
         jay_def dst,
         jay_def src0,
         jay_def src1)
{
   jay_inst *I = jay_alloc_inst(b, JAY_OPCODE_CMP, 2, 0);
   I->type = src_type;
   I->src[0] = src0;
   I->src[1] = src1;

   /* Even if we want to write a 32-bit 0/~0 result, we still need to
    * register-allocate a flag, since the hardware will implicitly clobber one
    * regardless.
    */
   if (!jay_is_flag(dst)) {
      I->dst = dst;
      dst = jay_alloc_def(b, dst.file == UGPR ? UFLAG : FLAG, 1);
   }

   jay_set_conditional_mod(b, I, dst, cmod);
   jay_builder_insert(b, I);
   return I;
}

#define jay_CMP(b, st, cmod, dst, src0, src1)                                  \
   _jay_CMP(b, st, cmod, dst, JAY_BUILD_SRC(src0), JAY_BUILD_SRC(src1))

struct jayb_send_params {
   enum brw_sfid sfid;
   uint64_t msg_desc;
   jay_def dst;
   jay_def header;
   jay_def *srcs;
   jay_def desc, ex_desc;
   enum jay_type type;
   enum jay_type src_type[2];
   unsigned nr_srcs;
   uint32_t ex_desc_imm;
   bool eot;
   bool check_tdr;
   bool uniform;
   bool bindless;
};

static inline jay_inst *
_jay_SEND(jay_builder *b, const struct jayb_send_params p)
{
   const struct intel_device_info *devinfo = b->shader->devinfo;
   jay_inst *I = jay_alloc_inst(b, JAY_OPCODE_SEND, 4, sizeof(jay_send_info));
   jay_send_info *info = jay_get_send_info(I);
   bool has_header = !jay_is_null(p.header);

   I->dst = p.dst;
   I->type = p.type;

   assert(I->type);
   info->type_0 = p.src_type[0] ? p.src_type[0] : I->type;
   info->type_1 = p.src_type[1] ? p.src_type[1] : info->type_0;

   if (has_header) {
      assert(p.nr_srcs == 1 || info->type_0 == info->type_1);

      /* If there is a message header, split the send into <header> and
       * <payload> since the header is UGPR but the payload is GPR.
       */
      I->src[2] = p.header;
      I->src[3] = jay_collect_vectors(b, &p.srcs[0], p.nr_srcs);
      info->type_1 = info->type_0;
      info->type_0 = JAY_TYPE_U32 /* header type */;
   } else if (jay_type_size_bits(info->type_0) == 16 &&
              !p.uniform &&
              b->shader->dispatch_width == 32) {
      /* Pack 16-bit vectors to match the hardware with the data model.
       *
       * XXX: This is a hack. Move to NIR for better
       * codegen in tests like
       * dEQP-GLES31.functional.texture.multisample.samples_4.use_texture_int_2d_array.
       */
      assert(info->type_0 == info->type_1);
      jay_def srcs[8];
      unsigned n = 0, i;
      for (i = 0; i + 2 <= p.nr_srcs; i += 2) {
         assert(p.srcs[i].file == p.srcs[i + 1].file);
         assert(jay_num_values(p.srcs[i]) == jay_num_values(p.srcs[i + 1]));

         for (unsigned c = 1; c < jay_num_values(p.srcs[i]); ++c) {
            assert(jay_channel(p.srcs[i], c) == 0);
            assert(jay_channel(p.srcs[i + 1], c) == 0);
         }

         jay_def lo = jay_extract(p.srcs[i], 0),
                 hi = jay_extract(p.srcs[i + 1], 0);
         jay_def bfi = jay_BFI2_u32(b, 0xffff0000, hi, lo);

         if (p.srcs[i].file == UGPR) {
            uint32_t defs[16] = { jay_index(bfi) };
            srcs[n++] = jay_collect(b, UGPR, defs, jay_ugpr_per_grf(b->shader));
         } else {
            srcs[n++] = bfi;
         }
      }
      if (i < p.nr_srcs) {
         srcs[n++] = p.srcs[i++];
      }
      assert(i == p.nr_srcs);

      I->src[2] = jay_collect_vectors(b, srcs, n);
      I->src[3] = jay_null();
   } else if (p.nr_srcs <= 2) {
      /* Easy case: keep everything scalar */
      I->src[2] = p.nr_srcs > 0 ? p.srcs[0] : jay_null();
      I->src[3] = p.nr_srcs > 1 ? p.srcs[1] : jay_null();
   } else {
      /* Otherwise, we need to pick a point to split at.
       *
       * Heuristic: don't split render targer writes becuase RA gets confused
       * with the EOT requirements. Split everything else in half.
       *
       * TODO: Come up with a better heuristic.
       */
      assert(info->type_0 == info->type_1);
      unsigned split = !p.check_tdr ? (p.nr_srcs / 2) : p.nr_srcs;
      I->src[2] = jay_collect_vectors(b, &p.srcs[0], split);
      I->src[3] = jay_collect_vectors(b, &p.srcs[split], p.nr_srcs - split);
   }

   /* For message headers we pack a UGPR vector as a single GRF */
   unsigned lens[3];
   for (unsigned i = 0; i < 3; ++i) {
      jay_def x = i == 0 ? I->dst : I->src[1 + i];
      lens[i] = jay_num_values(x);

      /* XXX: For the non-transpose uniform case, do we need to pad out
       * with undefs for correctness so we don't fall off the side of the
       * regfile? for sends like:
       *
       * (1&W)       mov.u32 u10.0, u0.8                                 | A@1
         (1&W)       mov.u32 u10.1, u0.9                                 | A@1
         (1&W)       send.u32 u12, g10, _, 0x04403580, 0x00000000
                     ugm MsgDesc: ( load, a64, d32, V4, L1STATE_L3MOCS dst_len =
       4, src0_len = 2, src1_len = 0 flat )  base_offset 0  | A@1 $0

        * We don't care what's in g11, but it has to *exist*. But that is
        * probably implicitly correct as long as the reg file ends with GRFs.
        * Which it has to <Xe3 because of EOT. So no code change needed but I
        * need to document this.
       */
      if (x.file == UGPR) {
         lens[i] = DIV_ROUND_UP(lens[i], jay_ugpr_per_grf(b->shader));
      } else {
         lens[i] *= jay_grf_per_gpr(b->shader);
      }

      lens[i] *= reg_unit(devinfo);
   }

   info->sfid = p.sfid;
   info->eot = p.eot;
   info->check_tdr = p.check_tdr;
   info->uniform = p.uniform;
   info->bindless = p.bindless;
   info->ex_desc_imm = p.ex_desc_imm;
   info->ex_mlen = lens[2];
   I->src[0] = jay_imm(((uint32_t) p.msg_desc) |
                       brw_message_desc(devinfo, lens[1], lens[0], has_header));

   if (!jay_is_null(p.desc)) {
      jay_def a = jay_alloc_def(b, J_ADDRESS, 1);
      jay_OR(b, JAY_TYPE_U32, a, p.desc, I->src[0]);
      I->src[0] = a;
   }

   if (jay_is_null(p.ex_desc)) {
      I->src[1] =
         jay_imm(brw_message_ex_desc(devinfo, lens[2]) | (p.msg_desc >> 32));
   } else if (p.ex_desc.file == J_ADDRESS) {
      I->src[1] = p.ex_desc;
   } else {
      I->src[1] = jay_alloc_def(b, J_ADDRESS, 1);
      if (info->bindless) {
         jay_MOV(b, I->src[1], p.ex_desc);
      } else {
         jay_OR(b, JAY_TYPE_U32, I->src[1], p.ex_desc,
                brw_message_ex_desc(devinfo, info->ex_mlen));
      }
   }

   assert(!info->uniform || jay_is_null(I->dst) || I->dst.file == UGPR);
   jay_builder_insert(b, I);
   return I;
}

#define jay_SEND(b, ...) _jay_SEND(b, (struct jayb_send_params) { __VA_ARGS__ })

static inline void
jay_copy_strided(jay_builder *b, jay_def dst, jay_def src, bool src_strided)
{
   assert(!jay_is_null(src));

   unsigned src_stride = src_strided ? jay_ugpr_per_grf(b->shader) : 1;
   uint32_t n = MIN2(jay_num_values(dst), jay_num_values(src) / src_stride);

   for (unsigned i = 0; i < n; ++i) {
      jay_MOV(b, jay_extract(dst, i), jay_extract(src, i * src_stride));
   }
}

static inline void
jay_copy(jay_builder *b, jay_def dst, jay_def src)
{
   jay_copy_strided(b, dst, src, false);
}

static inline jay_def
jay_as_gpr(jay_builder *b, jay_def src)
{
   if (src.file == GPR || jay_is_null(src))
      return src;

   jay_def def = jay_alloc_def(b, GPR, jay_num_values(src));
   jay_copy(b, def, src);
   return def;
}

static inline void
jay_i2i32(jay_builder *b, jay_def dst, unsigned src_bits, jay_def src)
{
   if (src_bits < 32) {
      jay_CVT(b, JAY_TYPE_S32, dst, src, jay_type(JAY_TYPE_S, src_bits),
              JAY_ROUND, 0);
   } else if (src_bits == 32) {
      jay_MOV(b, dst, src);
   } else {
      assert(src.reg == 0 && ".reg not preserved in this path but that's OK");
      jay_MOV(b, dst, jay_extract(src, 0));
   }
}
