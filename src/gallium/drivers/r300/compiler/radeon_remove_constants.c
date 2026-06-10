/*
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "radeon_remove_constants.h"
#include <stdbool.h>
#include <stdlib.h>
#include "util/bitscan.h"
#include "radeon_code.h"
#include "radeon_dataflow.h"

struct const_remap_state {
   /* Used when emiting shaders constants. */
   struct const_remap *remap_table;
   /* Used when rewritign registers */
   struct const_remap *inv_remap_table;
   /* Old costant layout. */
   struct rc_constant *constants;
   /* New constant layout. */
   struct rc_constant_list new_constants;
   /* Marks immediates that are used as a vector. Those will be just copied. */
   uint8_t *is_used_as_vector;
   bool has_rel_addr;
   bool are_externals_remapped;
   bool is_identity;
};

static void
remap_regs(struct rc_instruction *inst, struct const_remap *inv_remap_table)
{
   const struct rc_opcode_info *opcode = rc_get_opcode_info(inst->U.I.Opcode);
   for (unsigned src = 0; src < opcode->NumSrcRegs; ++src) {
      if (inst->U.I.SrcReg[src].File != RC_FILE_CONSTANT)
         continue;
      unsigned old_index = inst->U.I.SrcReg[src].Index;
      for (unsigned chan = 0; chan < 4; chan++) {
         unsigned old_swz = GET_SWZ(inst->U.I.SrcReg[src].Swizzle, chan);
         if (old_swz <= RC_SWIZZLE_W) {
            inst->U.I.SrcReg[src].Index = inv_remap_table[old_index].index[old_swz];
            SET_SWZ(inst->U.I.SrcReg[src].Swizzle, chan,
                    inv_remap_table[old_index].swizzle[old_swz]);
            if (inv_remap_table[old_index].negate[old_swz]) {
               assert(!inst->U.I.SrcReg[src].Abs);
               inst->U.I.SrcReg[src].Negate ^= 1 << chan;
            }
         }
      }
   }
}

static void
mark_used(void *userdata, struct rc_instruction *inst, struct rc_src_register *src)
{
   struct const_remap_state *d = userdata;

   if (src->File == RC_FILE_CONSTANT) {
      uint8_t mask = 0;
      if (src->RelAddr) {
         d->has_rel_addr = true;
      } else {
         for (unsigned chan = 0; chan < 4; chan++) {
            char swz = GET_SWZ(src->Swizzle, chan);
            if (swz > RC_SWIZZLE_W)
               continue;
            mask |= 1 << swz;
         }
      }
      d->constants[src->Index].UseMask |= mask;
      if (d->constants[src->Index].Type == RC_CONSTANT_IMMEDIATE && util_bitcount(mask) > 1) {
         d->is_used_as_vector[src->Index] |= mask;
      }
   }
}

static void
place_constant_in_free_slot(struct const_remap_state *s, unsigned i)
{
   unsigned count = s->new_constants.Count;
   for (unsigned chan = 0; chan < 4; chan++) {
      s->inv_remap_table[i].index[chan] = count;
      s->inv_remap_table[i].swizzle[chan] = chan;
      if (s->constants[i].UseMask & (1 << chan)) {
         s->remap_table[count].index[chan] = i;
         s->remap_table[count].swizzle[chan] = chan;
      }
   }
   s->new_constants.Constants[count] = s->constants[i];

   if (count != i) {
      if (s->constants[i].Type == RC_CONSTANT_EXTERNAL)
         s->are_externals_remapped = true;
      s->is_identity = false;
   }
   s->new_constants.Count++;
}

static void
place_immediate_in_free_slot(struct const_remap_state *s, unsigned i)
{
   assert(util_bitcount(s->is_used_as_vector[i]) > 1);

   unsigned count = s->new_constants.Count;
   bool remapped = count != i;
   s->new_constants.Constants[count] = s->constants[i];
   s->new_constants.Constants[count].UseMask = 0;

   /* Deduplicate repeated values within the immediate, leaving
    * free channels for later merging via try_merge_vec_immediate. */
   for (unsigned chan = 0; chan < 4; chan++) {
      if (!(s->is_used_as_vector[i] & (1 << chan)))
         continue;
      float val = s->constants[i].u.Immediate[chan];
      bool found = false;
      for (unsigned slot_chan = 0; slot_chan < 4; slot_chan++) {
         if ((s->new_constants.Constants[count].UseMask & (1 << slot_chan)) &&
             s->new_constants.Constants[count].u.Immediate[slot_chan] == val) {
            s->inv_remap_table[i].index[chan] = count;
            s->inv_remap_table[i].swizzle[chan] = slot_chan;
            remapped |= slot_chan != chan;
            found = true;
            break;
         }
      }
      if (!found) {
         unsigned new_chan = ffs(~s->new_constants.Constants[count].UseMask) - 1;
         s->new_constants.Constants[count].u.Immediate[new_chan] = val;
         s->new_constants.Constants[count].UseMask |= (1 << new_chan);
         s->inv_remap_table[i].index[chan] = count;
         s->inv_remap_table[i].swizzle[chan] = new_chan;
         remapped |= new_chan != chan;
      }
   }
   if (remapped)
      s->is_identity = false;
   s->new_constants.Count++;
}

/* Try to merge a vec-used immediate into an already-placed slot by matching
 * values and filling free channels. */
static bool
try_merge_vec_immediate(struct const_remap_state *s, unsigned i)
{
   uint8_t vec_mask = s->is_used_as_vector[i];

   for (unsigned j = 0; j < s->new_constants.Count; j++) {
      if (s->new_constants.Constants[j].Type != RC_CONSTANT_IMMEDIATE)
         continue;

      /* Work on a local copy so we don't corrupt state on failure. */
      uint8_t new_chan[4];
      uint8_t slot_used = s->new_constants.Constants[j].UseMask;
      float slot_vals[4];
      memcpy(slot_vals, s->new_constants.Constants[j].u.Immediate, sizeof(slot_vals));

      bool ok = true;
      for (unsigned chan = 0; chan < 4; chan++) {
         new_chan[chan] = 4;
         if (!(vec_mask & (1 << chan)))
            continue;

         float val = s->constants[i].u.Immediate[chan];

         /* First look for an existing (or tentatively placed) channel with
          * the same value. */
         bool found = false;
         for (unsigned slot_chan = 0; slot_chan < 4; slot_chan++) {
            if ((slot_used & (1 << slot_chan)) && slot_vals[slot_chan] == val) {
               new_chan[chan] = slot_chan;
               found = true;
               break;
            }
         }
         if (!found) {
            /* Put the value in a free channel. */
            uint8_t free_chan = ffs(~slot_used) - 1;
            if (free_chan > 3) {
               ok = false;
               break;
            }

            new_chan[chan] = free_chan;
            slot_vals[free_chan] = val;
            slot_used |= (1 << free_chan);
         }
      }

      if (!ok)
         continue;

      /* Write newly-claimed channels and update remap tables. */
      for (unsigned chan = 0; chan < 4; chan++) {
         if (!(vec_mask & (1 << chan)))
            continue;
         if (!(s->new_constants.Constants[j].UseMask & (1 << new_chan[chan]))) {
            s->new_constants.Constants[j].u.Immediate[new_chan[chan]] =
               s->constants[i].u.Immediate[chan];
            s->new_constants.Constants[j].UseMask |= (1 << new_chan[chan]);
         }
         s->inv_remap_table[i].index[chan] = j;
         s->inv_remap_table[i].swizzle[chan] = new_chan[chan];
      }
      s->is_identity = false;
      return true;
   }
   return false;
}

static void
try_merge_constants_external(struct const_remap_state *s, unsigned i)
{
   assert(util_bitcount(s->constants[i].UseMask) == 1);
   for (unsigned j = 0; j < s->new_constants.Count; j++) {
      for (unsigned chan = 0; chan < 4; chan++) {
         if (s->remap_table[j].swizzle[chan] == RC_SWIZZLE_UNUSED) {
            /* Writemask to swizzle */
            unsigned swizzle = 0;
            for (; swizzle < 4; swizzle++)
               if (s->constants[i].UseMask >> swizzle == 1)
                  break;
            /* Update the remap tables. */
            s->remap_table[j].index[chan] = i;
            s->remap_table[j].swizzle[chan] = swizzle;
            s->inv_remap_table[i].index[swizzle] = j;
            s->inv_remap_table[i].swizzle[swizzle] = chan;
            s->are_externals_remapped = true;
            s->is_identity = false;
            return;
         }
      }
   }
   place_constant_in_free_slot(s, i);
}

static void
init_constant_remap_state(struct radeon_compiler *c, struct const_remap_state *s)
{
   s->is_identity = true;
   s->is_used_as_vector = malloc(c->Program.Constants.Count);
   s->new_constants.Constants = malloc(sizeof(struct rc_constant) * c->Program.Constants.Count);
   s->new_constants._Reserved = c->Program.Constants.Count;
   s->constants = c->Program.Constants.Constants;
   memset(s->is_used_as_vector, 0, c->Program.Constants.Count);

   s->remap_table = calloc(c->Program.Constants.Count, sizeof(struct const_remap));
   s->inv_remap_table = calloc(c->Program.Constants.Count, sizeof(struct const_remap));
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      /* Clear the UseMask, we will update it later. */
      s->constants[i].UseMask = 0;
      for (unsigned swz = 0; swz < 4; swz++) {
         s->remap_table[i].index[swz] = -1;
         s->remap_table[i].swizzle[swz] = RC_SWIZZLE_UNUSED;
      }
   }
}

void
rc_remove_unused_constants(struct radeon_compiler *c, void *user)
{
   struct const_remap **out_remap_table = (struct const_remap **)user;
   struct rc_constant *constants = c->Program.Constants.Constants;
   struct const_remap_state remap_state = {};
   struct const_remap_state *s = &remap_state;

   if (!c->Program.Constants.Count) {
      *out_remap_table = NULL;
      return;
   }

   init_constant_remap_state(c, s);

   /* Pass 1: Mark used constants. */
   for (struct rc_instruction *inst = c->Program.Instructions.Next;
        inst != &c->Program.Instructions; inst = inst->Next) {
      rc_for_all_reads_src(inst, mark_used, s);
   }

   /* Pass 2: If there is relative addressing or dead constant elimination
    * is disabled, mark all externals as used. */
   if (s->has_rel_addr || !c->remove_unused_constants) {
      for (unsigned i = 0; i < c->Program.Constants.Count; i++)
         if (constants[i].Type == RC_CONSTANT_EXTERNAL)
            s->constants[i].UseMask = RC_MASK_XYZW;
   }

   /* Pass 3: Make the remapping table and remap constants.
    * First iterate over used vec2, vec3 and vec4 externals and place them in a free
    * slots. While we could in theory merge 2 vec2 together, its not worth it
    * as we would have to a) check that the swizzle is valid, b) transforming
    * xy to zw would mean we need rgb and alpha source slot, thus it would hurt
    * us potentially during pair scheduling. */
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      if (constants[i].Type != RC_CONSTANT_EXTERNAL)
         continue;
      if (util_bitcount(s->constants[i].UseMask) > 1) {
         place_constant_in_free_slot(s, i);
      }
   }

   /* Now iterate over scalar externals and put them into empty slots. */
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      if (constants[i].Type != RC_CONSTANT_EXTERNAL)
         continue;
      if (util_bitcount(s->constants[i].UseMask) == 1)
         try_merge_constants_external(s, i);
   }

   /* Place state constants before immediates so the immediate-packing budget
    * accounts for state slots that cannot be packed with immediates. */
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      if (constants[i].Type != RC_CONSTANT_STATE)
         continue;
      if (util_bitcount(s->constants[i].UseMask) > 0) {
         place_constant_in_free_slot(s, i);
      }
   }

   /* Count vec-used immediates to estimate whether aggressive packing (specifically
    * packing which can produce invalid swizzles) is needed. */
   unsigned num_vec_imm = 0;
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      if (constants[i].Type == RC_CONSTANT_IMMEDIATE &&
          util_bitcount(s->constants[i].UseMask) > 0 &&
          util_bitcount(s->is_used_as_vector[i]) > 0)
         num_vec_imm++;
   }
   bool aggressive = c->type == RC_VERTEX_PROGRAM ||
                     (!c->is_r500 &&
                      s->new_constants.Count + num_vec_imm > R300_PFS_NUM_CONST_REGS);

   /* Place vec-used immediates first. Place_immediate_in_free_slot deduplicates
    * repeated values within the immediate, leaving free channels in the new slot.
    * Subsequent vec immediates can then merge into those free channels via
    * try_merge_vec_immediate, naturally building a shared value palette. */
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      if (constants[i].Type == RC_CONSTANT_IMMEDIATE &&
          util_bitcount(s->constants[i].UseMask) > 0 &&
          util_bitcount(s->is_used_as_vector[i]) > 0) {
         if (aggressive && try_merge_vec_immediate(s, i))
            continue;
         place_immediate_in_free_slot(s, i);
      }
   }

   /* Scalar-only channels fill the remaining free channels of already-placed
    * slots or create new ones via rc_constants_add_immediate_scalar. */
   for (unsigned i = 0; i < c->Program.Constants.Count; i++) {
      if (constants[i].Type != RC_CONSTANT_IMMEDIATE)
         continue;
      for (unsigned chan = 0; chan < 4; chan++) {
         if ((s->constants[i].UseMask) & (1 << chan) &&
             (~(s->is_used_as_vector[i]) & (1 << chan))) {
            unsigned swz, neg;
            s->inv_remap_table[i].index[chan] = rc_constants_add_immediate_scalar(
               &s->new_constants, constants[i].u.Immediate[chan], &swz, &neg);
            s->inv_remap_table[i].swizzle[chan] = GET_SWZ(swz, 0);
            s->inv_remap_table[i].negate[chan] = GET_BIT(neg, 0);
            s->is_identity = false;
         }
      }
   }

   /*  is_identity ==> new_count == old_count
    * !is_identity ==> new_count <  old_count */
   assert(!((s->has_rel_addr || !c->remove_unused_constants) && s->are_externals_remapped));

   /* Pass 4: Redirect reads of all constants to their new locations. */
   if (!s->is_identity) {
      for (struct rc_instruction *inst = c->Program.Instructions.Next;
           inst != &c->Program.Instructions; inst = inst->Next) {
         remap_regs(inst, s->inv_remap_table);
      }
   }

   /* Set the new constant count. Note that new_count may be less than
    * Count even though the remapping function is identity. In that case,
    * the constants have been removed at the end of the array. */
   rc_constants_destroy(&c->Program.Constants);
   c->Program.Constants = s->new_constants;

   if (c->Debug & RC_DBG_LOG)
      rc_constants_print(&c->Program.Constants, s->remap_table);

   if (s->are_externals_remapped) {
      *out_remap_table = s->remap_table;
   } else {
      *out_remap_table = NULL;
      free(s->remap_table);
   }

   free(s->inv_remap_table);
   free(s->is_used_as_vector);
}
