/*
 * Copyright © 2012 Intel Corporation
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

#define XXH_INLINE_ALL
#include "util/xxhash.h"

#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_cfg.h"

/** @file
 *
 * Support for SSA-based global Common Subexpression Elimination (CSE).
 */

struct remap_entry {
   brw_inst *inst;
   enum brw_reg_type type;
   unsigned nr;
   bool negate;
   bool still_used;
};

static bool
is_expression(const brw_shader *v, const brw_inst *const inst)
{
   switch (inst->opcode) {
   case BRW_OPCODE_MOV:
   case BRW_OPCODE_SEL:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_AND:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_XOR:
   case BRW_OPCODE_SHR:
   case BRW_OPCODE_SHL:
   case BRW_OPCODE_ASR:
   case BRW_OPCODE_ROR:
   case BRW_OPCODE_ROL:
   case BRW_OPCODE_CMP:
   case BRW_OPCODE_CMPN:
   case BRW_OPCODE_CSEL:
   case BRW_OPCODE_BFREV:
   case BRW_OPCODE_BFE:
   case BRW_OPCODE_BFI1:
   case BRW_OPCODE_BFI2:
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_MUL:
   case SHADER_OPCODE_MULH:
   case BRW_OPCODE_AVG:
   case BRW_OPCODE_FRC:
   case BRW_OPCODE_LZD:
   case BRW_OPCODE_FBH:
   case BRW_OPCODE_FBL:
   case BRW_OPCODE_CBIT:
   case BRW_OPCODE_ADD3:
   case BRW_OPCODE_RNDU:
   case BRW_OPCODE_RNDD:
   case BRW_OPCODE_RNDE:
   case BRW_OPCODE_RNDZ:
   case BRW_OPCODE_LINE:
   case BRW_OPCODE_PLN:
   case BRW_OPCODE_MAD:
   case BRW_OPCODE_LRP:
   case FS_OPCODE_FB_READ_LOGICAL:
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
   case FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL:
   case SHADER_OPCODE_FIND_LIVE_CHANNEL:
   case SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL:
   case SHADER_OPCODE_LOAD_LIVE_CHANNELS:
   case FS_OPCODE_LOAD_LIVE_CHANNELS:
   case SHADER_OPCODE_BROADCAST:
   case SHADER_OPCODE_SHUFFLE:
   case SHADER_OPCODE_QUAD_SWIZZLE:
   case SHADER_OPCODE_CLUSTER_BROADCAST:
   case SHADER_OPCODE_MOV_INDIRECT:
   case SHADER_OPCODE_SAMPLER:
   case FS_OPCODE_PACK:
   case FS_OPCODE_PACK_HALF_2x16_SPLIT:
   case SHADER_OPCODE_RCP:
   case SHADER_OPCODE_RSQ:
   case SHADER_OPCODE_SQRT:
   case SHADER_OPCODE_EXP2:
   case SHADER_OPCODE_LOG2:
   case SHADER_OPCODE_POW:
   case SHADER_OPCODE_INT_QUOTIENT:
   case SHADER_OPCODE_INT_REMAINDER:
   case SHADER_OPCODE_SIN:
   case SHADER_OPCODE_COS:
   case SHADER_OPCODE_LOAD_SUBGROUP_INVOCATION:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      return true;
   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
      return inst->as_mem()->mode == MEMORY_MODE_CONSTANT;
   case SHADER_OPCODE_LOAD_PAYLOAD:
      return !is_coalescing_payload(*v, inst);
   case SHADER_OPCODE_SEND:
   case SHADER_OPCODE_SEND_GATHER: {
      const brw_send_inst *send = inst->as_send();
      return !send->has_side_effects &&
             !send->is_volatile &&
             !send->eot;
   }
   default:
      return false;
   }
}

/**
 * True if the instruction should only be CSE'd within their local block.
 */
bool
local_only(const brw_inst *inst)
{
   switch (inst->opcode) {
   case SHADER_OPCODE_FIND_LIVE_CHANNEL:
   case SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL:
   case SHADER_OPCODE_LOAD_LIVE_CHANNELS:
   case FS_OPCODE_LOAD_LIVE_CHANNELS:
      /* These depend on the current channel enables, so the same opcode
       * in another block will likely return a different value.
       */
      return true;
   case BRW_OPCODE_MOV:
      /* Global CSE of MOVs is likely not worthwhile.  It can increase
       * register pressure by extending the lifetime of simple constants.
       */
      return true;
   case SHADER_OPCODE_LOAD_PAYLOAD:
      /* This is basically a MOV */
      return inst->sources == 1;
   case BRW_OPCODE_CMP:
      /* Seems to increase spilling a lot without much benefit */
      return true;
   default:
      return false;
   }
}

static bool
operands_match(const brw_inst *a, const brw_inst *b, bool *negate)
{
   brw_reg *xs = a->src;
   brw_reg *ys = b->src;

   if (a->opcode == BRW_OPCODE_MAD) {
      return xs[0].equals(ys[0]) &&
             ((xs[1].equals(ys[1]) && xs[2].equals(ys[2])) ||
              (xs[2].equals(ys[1]) && xs[1].equals(ys[2])));
   } else if (a->opcode == BRW_OPCODE_MUL && a->dst.type == BRW_TYPE_F) {
      bool xs0_negate = xs[0].negate;
      bool xs1_negate = xs[1].file == IMM ? xs[1].f < 0.0f
                                          : xs[1].negate;
      bool ys0_negate = ys[0].negate;
      bool ys1_negate = ys[1].file == IMM ? ys[1].f < 0.0f
                                          : ys[1].negate;
      float xs1_imm = xs[1].f;
      float ys1_imm = ys[1].f;

      xs[0].negate = false;
      xs[1].negate = false;
      ys[0].negate = false;
      ys[1].negate = false;
      xs[1].f = fabsf(xs[1].f);
      ys[1].f = fabsf(ys[1].f);

      bool ret = (xs[0].equals(ys[0]) && xs[1].equals(ys[1])) ||
                 (xs[1].equals(ys[0]) && xs[0].equals(ys[1]));

      xs[0].negate = xs0_negate;
      xs[1].negate = xs[1].file == IMM ? false : xs1_negate;
      ys[0].negate = ys0_negate;
      ys[1].negate = ys[1].file == IMM ? false : ys1_negate;
      xs[1].f = xs1_imm;
      ys[1].f = ys1_imm;

      *negate = (xs0_negate != xs1_negate) != (ys0_negate != ys1_negate);
      if (*negate && (a->saturate || b->saturate))
         return false;
      return ret;
   } else if (!a->is_commutative()) {
      bool match = true;
      for (int i = 0; i < a->sources; i++) {
         if (!xs[i].equals(ys[i])) {
            match = false;
            break;
         }
      }
      return match;
   } else if (a->sources == 3) {
      return (xs[0].equals(ys[0]) && xs[1].equals(ys[1]) && xs[2].equals(ys[2])) ||
             (xs[0].equals(ys[0]) && xs[1].equals(ys[2]) && xs[2].equals(ys[1])) ||
             (xs[0].equals(ys[1]) && xs[1].equals(ys[0]) && xs[2].equals(ys[2])) ||
             (xs[0].equals(ys[1]) && xs[1].equals(ys[2]) && xs[2].equals(ys[1])) ||
             (xs[0].equals(ys[2]) && xs[1].equals(ys[0]) && xs[2].equals(ys[1])) ||
             (xs[0].equals(ys[2]) && xs[1].equals(ys[1]) && xs[2].equals(ys[0]));
   } else {
      return (xs[0].equals(ys[0]) && xs[1].equals(ys[1])) ||
             (xs[1].equals(ys[0]) && xs[0].equals(ys[1]));
   }
}

static bool
send_inst_match(brw_send_inst *a, brw_send_inst *b)
{
   return a->mlen == b->mlen &&
          a->ex_mlen == b->ex_mlen &&
          a->sfid == b->sfid &&
          a->header_size == b->header_size &&
          a->desc == b->desc &&
          a->ex_desc == b->ex_desc &&
          a->offset == b->offset &&
          a->send_bits == b->send_bits;
}

static bool
tex_inst_match(brw_tex_inst *a, brw_tex_inst *b)
{
   return a->sampler_opcode == b->sampler_opcode &&
          a->surface_bindless == b->surface_bindless &&
          a->sampler_bindless == b->sampler_bindless &&
          a->residency == b->residency &&
          a->required_params == b->required_params &&
          a->coord_components == b->coord_components &&
          a->gather_component == b->gather_component &&
          a->has_const_offsets == b->has_const_offsets &&
          a->const_offsets[0] == b->const_offsets[0] &&
          a->const_offsets[1] == b->const_offsets[1] &&
          a->const_offsets[2] == b->const_offsets[2];
}

static bool
mem_inst_match(brw_mem_inst *a, brw_mem_inst *b)
{
   return a->lsc_op == b->lsc_op &&
          a->mode == b->mode &&
          a->binding_type == b->binding_type &&
          a->data_size == b->data_size &&
          a->coord_components == b->coord_components &&
          a->components == b->components &&
          a->flags == b->flags &&
          a->alignment == b->alignment &&
          a->address_offset == b->address_offset;
}

static bool
dpas_inst_match(brw_dpas_inst *a, brw_dpas_inst *b)
{
   return a->sdepth == b->sdepth &&
          a->rcount == b->rcount;
}

static bool
load_payload_inst_match(brw_load_payload_inst *a, brw_load_payload_inst *b)
{
   return a->header_size == b->header_size;
}

static bool
urb_inst_match(brw_urb_inst *a, brw_urb_inst *b)
{
   return a->offset == b->offset &&
          a->components == b->components;
}

static bool
fb_write_inst_match(brw_fb_write_inst *a, brw_fb_write_inst *b)
{
   return a->components == b->components &&
          a->target == b->target &&
          a->null_rt == b->null_rt &&
          a->last_rt == b->last_rt;
}

static bool
instructions_match(brw_inst *a, brw_inst *b, bool *negate)
{

   return a->opcode == b->opcode &&
          /* `kind` is derived from opcode, so skipped. */
          (a->kind != BRW_KIND_SEND || send_inst_match(a->as_send(), b->as_send())) &&
          (a->kind != BRW_KIND_TEX || tex_inst_match(a->as_tex(), b->as_tex())) &&
          (a->kind != BRW_KIND_MEM || mem_inst_match(a->as_mem(), b->as_mem())) &&
          (a->kind != BRW_KIND_DPAS || dpas_inst_match(a->as_dpas(), b->as_dpas())) &&
          (a->kind != BRW_KIND_LOAD_PAYLOAD ||
           load_payload_inst_match(a->as_load_payload(), b->as_load_payload())) &&
          (a->kind != BRW_KIND_URB || urb_inst_match(a->as_urb(), b->as_urb())) &&
          (a->kind != BRW_KIND_FB_WRITE || fb_write_inst_match(a->as_fb_write(), b->as_fb_write())) &&
          a->exec_size == b->exec_size &&
          a->group == b->group &&
          a->predicate == b->predicate &&
          a->conditional_mod == b->conditional_mod &&
          a->dst.type == b->dst.type &&
          a->size_written == b->size_written &&
          a->sources == b->sources &&
          a->bits == b->bits &&
          operands_match(a, b, negate);
}

/* -------------------------------------------------------------------- */

#define HASH(hash, data) XXH32(&(data), sizeof(data), hash)

uint32_t
hash_reg(uint32_t hash, const brw_reg &r)
{
   struct {
      uint64_t u64;
      uint32_t u32;
      uint16_t u16a;
      uint16_t u16b;
   } data = {
      .u64 = r.u64, .u32 = r.bits, .u16a = r.offset, .u16b = r.stride
   };
   STATIC_ASSERT(sizeof(data) == 16); /* ensure there's no padding */
   hash = HASH(hash, data);
   return hash;
}

static uint32_t
hash_inst(const void *v)
{
   const brw_inst *inst = static_cast<const brw_inst *>(v);
   uint32_t hash = 0;

   /* Skip dst - that would make nothing ever match */

   /* Skip ir and annotation - we don't care for equivalency purposes. */

   /* Skip `kind` since it is derived from the opcode. */

   const uint8_t u8data[] = {
      inst->sources,
      inst->exec_size,
      inst->group,

      inst->conditional_mod,
      inst->predicate,
   };
   const uint32_t u32data[] = {
      inst->size_written,
      inst->opcode,
      inst->bits,
   };

   hash = HASH(hash, u8data);
   hash = HASH(hash, u32data);

   /* Skip hashing sched - we shouldn't be CSE'ing after that SWSB */

   switch (inst->kind) {
   case BRW_KIND_SEND: {
      const brw_send_inst *send = inst->as_send();
      const uint8_t send_u8data[] = {
         send->mlen,
         send->ex_mlen,
         send->sfid,
         send->send_bits,
         send->header_size,
      };
      const uint32_t send_u32data[] = {
         send->desc,
         send->ex_desc,
         send->offset,
      };
      hash = HASH(hash, send_u8data);
      hash = HASH(hash, send_u32data);
      break;
   }

   case BRW_KIND_TEX: {
      const brw_tex_inst *tex = inst->as_tex();
      const uint8_t tex_u8data[] = {
         tex->sampler_opcode,
         (uint8_t)tex->const_offsets[0],
         (uint8_t)tex->const_offsets[1],
         (uint8_t)tex->const_offsets[2],
      };
      const uint32_t tex_u32data[] = {
         tex->bits,
      };
      hash = HASH(hash, tex_u8data);
      hash = HASH(hash, tex_u32data);
      break;
   }

   case BRW_KIND_MEM: {
      const brw_mem_inst *mem = inst->as_mem();
      const uint8_t mem_u8data[] = {
         mem->lsc_op,
         mem->mode,
         mem->binding_type,
         mem->data_size,
         mem->coord_components,
         mem->components,
         mem->flags,
      };
      const uint32_t mem_u32data[] = {
         (uint32_t)mem->address_offset,
         mem->alignment,
      };
      hash = HASH(hash, mem_u8data);
      hash = HASH(hash, mem_u32data);
      break;
   }

   case BRW_KIND_DPAS: {
      const brw_dpas_inst *dpas = inst->as_dpas();
      const uint8_t dpas_u8data[] = {
         dpas->sdepth,
         dpas->rcount,
      };
      hash = HASH(hash, dpas_u8data);
      break;
   }

   case BRW_KIND_LOAD_PAYLOAD: {
      const brw_load_payload_inst *lp = inst->as_load_payload();
      const uint8_t lp_u8data[] = {
         lp->header_size,
      };
      hash = HASH(hash, lp_u8data);
      break;
   }

   case BRW_KIND_URB: {
      const brw_urb_inst *urb = inst->as_urb();
      const uint8_t urb_u8data[] = {
         urb->components,
      };
      const uint32_t urb_u32data[] = {
         urb->offset,
      };
      hash = HASH(hash, urb_u8data);
      hash = HASH(hash, urb_u32data);
      break;
   }

   case BRW_KIND_FB_WRITE: {
      const brw_fb_write_inst *fb_write = inst->as_fb_write();
      const uint8_t fb_write_u8data[] = {
         fb_write->components,
         fb_write->target,
         fb_write->null_rt,
         fb_write->last_rt,
      };
      hash = HASH(hash, fb_write_u8data);
      break;
   }

   case BRW_KIND_LOGICAL:
   case BRW_KIND_BASE:
      /* Nothing else to do. */
      break;

   case BRW_KIND_SCRATCH:
      UNREACHABLE("Spill and fills should not exist yet.");
   }

   if (inst->opcode == BRW_OPCODE_MAD) {
      /* Commutatively combine the hashes for the multiplicands */
      hash = hash_reg(hash, inst->src[0]);
      uint32_t hash1 = hash_reg(hash, inst->src[1]);
      uint32_t hash2 = hash_reg(hash, inst->src[2]);
      hash = hash1 * hash2;
   } else if (inst->opcode == BRW_OPCODE_MUL &&
              inst->dst.type == BRW_TYPE_F) {
      /* Canonicalize negations on either source (or both) and commutatively
       * combine the hashes for both sources.
       */
      brw_reg src[2] = { inst->src[0], inst->src[1] };
      uint32_t src_hash[2];

      for (int i = 0; i < 2; i++) {
         src[i].negate = false;
         if (src[i].file == IMM)
            src[i].f = fabs(src[i].f);

         src_hash[i] = hash_reg(hash, src[i]);
      }

      hash = src_hash[0] * src_hash[1];
   } else if (inst->is_commutative()) {
      /* Commutatively combine the sources */
      uint32_t hash0 = hash_reg(hash, inst->src[0]);
      uint32_t hash1 = hash_reg(hash, inst->src[1]);
      uint32_t hash2 = inst->sources > 2 ? hash_reg(hash, inst->src[2]) : 1;
      hash = hash0 * hash1 * hash2;
   } else {
      /* Just hash all the sources */
      for (int i = 0; i < inst->sources; i++)
         hash = hash_reg(hash, inst->src[i]);
   }

   return hash;
}

/* -------------------------------------------------------------------- */

static bool
cmp_func(const void *data1, const void *data2)
{
   bool negate;
   return instructions_match((brw_inst *) data1, (brw_inst *) data2, &negate);
}

static bool
remap_sources(brw_shader &s, const brw_def_analysis &defs,
              brw_inst *inst, struct remap_entry *remap_table)
{
   bool progress = false;

   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].file == VGRF &&
          inst->src[i].nr < defs.count() &&
          remap_table[inst->src[i].nr].inst != NULL) {
         const unsigned old_nr = inst->src[i].nr;
         const unsigned new_nr = remap_table[old_nr].nr;
         const bool need_negate = remap_table[old_nr].negate;

         if (need_negate &&
             (remap_table[old_nr].type != inst->src[i].type ||
              !inst->can_do_source_mods(s.devinfo))) {
            remap_table[old_nr].still_used = true;
            continue;
         }

         inst->src[i].nr = new_nr;

         if (!inst->src[i].abs)
            inst->src[i].negate ^= need_negate;

         progress = true;
      }
   }

   return progress;
}

bool
brw_opt_cse_defs(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   const brw_idom_tree &idom = s.idom_analysis.require();
   const brw_def_analysis &defs = s.def_analysis.require();
   bool progress = false;
   bool need_remaps = false;

   struct remap_entry *remap_table = new remap_entry[defs.count()];
   memset(remap_table, 0, defs.count() * sizeof(struct remap_entry));
   struct set *set = _mesa_set_create(NULL, NULL, cmp_func);

   foreach_block(block, s.cfg) {
      brw_inst *last_flag_write = NULL;
      brw_inst *last = NULL;

      foreach_inst_in_block_safe(brw_inst, inst, block) {
         if (need_remaps)
            progress |= remap_sources(s, defs, inst, remap_table);

         /* Updating last_flag_written should be at the bottom of the loop,
          * but doing it this way lets us use "continue" more easily.
          */
         if (last && last->flags_written(devinfo))
            last_flag_write = last;
         last = inst;

         /* Discard jumps aren't represented in the CFG unfortunately, so we need
          * to make sure that they behave as a CSE barrier, since we lack global
          * dataflow information.  This is particularly likely to cause problems
          * with instructions dependent on the current execution mask like
          * SHADER_OPCODE_FIND_LIVE_CHANNEL.
          */
         if (inst->opcode == BRW_OPCODE_HALT ||
             inst->opcode == SHADER_OPCODE_HALT_TARGET) {
            /* Treat each side of the HALT separately for local_only
             * expressions as it's altering the channel enables.
             */
            set_foreach(set, e) {
               brw_inst *match = (brw_inst *) e->key;
               if (match->block == block && local_only(match))
                  _mesa_set_remove(set, e);
            }
         }

         if (inst->dst.is_null()) {
            bool ignored;
            if (last_flag_write && !inst->writes_accumulator &&
                instructions_match(last_flag_write, inst, &ignored)) {
               /* This instruction has no destination but has a flag write
                * which is redundant with the previous flag write in our
                * basic block.  So we can simply remove it.
                */
               inst->remove();
               last = NULL;
               progress = true;
            }
         } else if (is_expression(&s, inst) && defs.get(inst->dst)) {
            assert(!inst->writes_accumulator);
            assert(!inst->reads_accumulator_implicitly());

            uint32_t hash = hash_inst(inst);
            if (inst->flags_read(devinfo)) {
               hash = last_flag_write ? HASH(hash, last_flag_write)
                                      : HASH(hash, block);
            }

            struct set_entry *e =
               _mesa_set_search_or_add_pre_hashed(set, hash, inst, NULL);
            if (!e) goto out; /* out of memory error */
            brw_inst *match = (brw_inst *) e->key;

            /* If there was no match, move on */
            if (match == inst)
               continue;

            bblock_t *def_block = match->block;
            if (block != def_block && (local_only(inst) ||
                !idom.dominates(def_block, block))) {
               /* If `match` doesn't dominate `inst` then remove it from
                * the set and add `inst` instead so future lookups see that.
                */
               e->key = inst;
               continue;
            }

            /* We can replace inst with match or negate(match). */
            bool negate = false;
            if (inst->opcode == BRW_OPCODE_MUL &&
                inst->dst.type == BRW_TYPE_F) {
               /* Determine whether inst is actually negate(match) */
               bool ops_must_match = operands_match(inst, match, &negate);
               assert(ops_must_match);
            }

            /* Some later instruction could depend on the flags written by
             * this instruction. It can only be removed if the previous
             * instruction that write the flags is identical.
             */
            if (inst->flags_written(devinfo)) {
               bool ignored;

               if (last_flag_write == NULL ||
                   !instructions_match(last_flag_write, inst, &ignored)) {
                  continue;
               }
            }

            need_remaps = true;
            remap_table[inst->dst.nr].inst = inst;
            remap_table[inst->dst.nr].type = match->dst.type;
            remap_table[inst->dst.nr].nr = match->dst.nr;
            remap_table[inst->dst.nr].negate = negate;
            remap_table[inst->dst.nr].still_used = false;
         }
      }
   }

   /* Remove instruction now unused */
   for (unsigned i = 0; i < defs.count(); i++) {
      if (!remap_table[i].inst)
         continue;

      if (!remap_table[i].still_used) {
         remap_table[i].inst->remove();
         progress = true;
      }
   }

out:
   delete [] remap_table;
   _mesa_set_destroy(set, NULL);

   if (progress) {
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);
   }

   return progress;
}

#undef HASH
