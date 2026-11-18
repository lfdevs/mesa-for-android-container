#include "nir.h"
#include "nir_builder.h"

#include "util/u_dynarray.h"

struct robust_ssbo_load {
   nir_intrinsic_instr *instr;
   nir_def *resource;
   nir_def *base;
   uint32_t offset;
   uint32_t scale;
   bool handled;
};

static bool
parse_offset_scaled(nir_def *offset, uint32_t scale, nir_def **base,
                    uint32_t *add, uint32_t *out_scale);

static bool
const_u32(nir_def *def, uint32_t *value)
{
   nir_const_value *constant = nir_src_as_const_value(nir_src_for_ssa(def));
   if (!constant || def->bit_size != 32 || def->num_components != 1)
      return false;

   *value = constant->u32;
   return true;
}

static bool
const_src_u32(const nir_alu_src *src, uint32_t *value)
{
   if (src->swizzle[0] != 0 || !src->src.ssa ||
       src->src.ssa->num_components != 1)
      return false;

   return const_u32(src->src.ssa, value);
}

static bool
parse_offset_src(const nir_alu_src *src, uint32_t scale, nir_def **base,
                 uint32_t *add, uint32_t *out_scale)
{
   if (src->swizzle[0] != 0 || !src->src.ssa ||
       src->src.ssa->num_components != 1)
      return false;

   return parse_offset_scaled(src->src.ssa, scale, base, add, out_scale);
}

static bool
parse_offset_scaled(nir_def *offset, uint32_t scale, nir_def **base,
                    uint32_t *add, uint32_t *out_scale)
{
   uint32_t constant;
   if (const_u32(offset, &constant)) {
      *base = NULL;
      *add = constant * scale;
      *out_scale = scale;
      return true;
   }

   if (offset->bit_size != 32 || offset->num_components != 1 ||
       !nir_def_is_alu(offset)) {
      *base = offset;
      *add = 0;
      *out_scale = scale;
      return offset->bit_size == 32 && offset->num_components == 1;
   }

   nir_alu_instr *alu = nir_def_as_alu(offset);
   uint32_t c0, c1;
   switch (alu->op) {
   case nir_op_iadd:
      if (const_src_u32(&alu->src[0], &c0) &&
          !const_src_u32(&alu->src[1], &c1) &&
          parse_offset_src(&alu->src[1], scale, base, add, out_scale)) {
         *add += c0 * scale;
         return true;
      }
      if (const_src_u32(&alu->src[1], &c1) &&
          !const_src_u32(&alu->src[0], &c0) &&
          parse_offset_src(&alu->src[0], scale, base, add, out_scale)) {
         *add += c1 * scale;
         return true;
      }
      if (const_src_u32(&alu->src[0], &c0) &&
          const_src_u32(&alu->src[1], &c1)) {
         *base = NULL;
         *add = (c0 + c1) * scale;
         *out_scale = scale;
         return true;
      }
      break;

   case nir_op_imul:
      if (const_src_u32(&alu->src[0], &c0) &&
          !const_src_u32(&alu->src[1], &c1))
         return parse_offset_src(&alu->src[1], scale * c0, base, add,
                                 out_scale);
      if (const_src_u32(&alu->src[1], &c1) &&
          !const_src_u32(&alu->src[0], &c0))
         return parse_offset_src(&alu->src[0], scale * c1, base, add,
                                 out_scale);
      break;

   case nir_op_ishl:
      if (const_src_u32(&alu->src[1], &c1) &&
          !const_src_u32(&alu->src[0], &c0) && c1 < 32)
         return parse_offset_src(&alu->src[0], scale << c1, base, add,
                                 out_scale);
      break;

   default:
      break;
   }

   *base = offset;
   *add = 0;
   *out_scale = scale;
   return true;
}

static bool
parse_offset(nir_def *offset, nir_def **base, uint32_t *add,
             uint32_t *scale)
{
   return parse_offset_scaled(offset, 1, base, add, scale);
}

static bool
has_unsafe_memory(nir_shader *shader)
{
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (nir_intrinsic_writes_external_memory(intr) ||
                intr->intrinsic == nir_intrinsic_barrier)
               return true;
         }
      }
   }

   return false;
}

static bool
is_eligible(const struct robust_ssbo_load *load)
{
   nir_intrinsic_instr *instr = load->instr;
   if (instr->intrinsic != nir_intrinsic_load_ssbo ||
       instr->def.bit_size != 32 || instr->def.num_components != 1 ||
       nir_intrinsic_offset_shift(instr) != 0 ||
       nir_intrinsic_align(instr) < 4 ||
       (load->scale != 1 && load->scale != 4))
      return false;

   enum gl_access_qualifier access = nir_intrinsic_access(instr);
   return (access & (ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER)) ==
             (ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER) &&
          !(access & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_ATOMIC |
                      ACCESS_KEEP_SCALAR));
}

static bool
same_group(const struct robust_ssbo_load *a, const struct robust_ssbo_load *b)
{
   return a->instr->instr.block == b->instr->instr.block &&
          a->resource == b->resource && a->base == b->base &&
          a->scale == b->scale &&
          nir_intrinsic_access(a->instr) == nir_intrinsic_access(b->instr);
}

static bool
def_dominates_instr(nir_def *def, nir_instr *instr)
{
   nir_instr *def_instr = nir_def_instr(def);
   if (!nir_block_dominates(def_instr->block, instr->block))
      return false;
   return def_instr->block != instr->block ||
          def_instr == instr || nir_instr_is_before(def_instr, instr);
}

static bool
find_group(struct robust_ssbo_load *loads, unsigned count,
           struct robust_ssbo_load *group[4], unsigned *group_count)
{
   for (unsigned i = 0; i < count; i++) {
      if (loads[i].handled || !is_eligible(&loads[i]))
         continue;

      if (loads[i].offset >= 4) {
         bool has_previous = false;
         for (unsigned j = 0; j < count; j++) {
            if (!loads[j].handled && is_eligible(&loads[j]) &&
                same_group(&loads[i], &loads[j]) &&
                loads[j].offset == loads[i].offset - 4) {
               has_previous = true;
               break;
            }
         }
         if (has_previous)
            continue;
      }

      unsigned best = 0;
      struct robust_ssbo_load *candidate[4] = { NULL };
      for (unsigned n = 2; n <= 4; n++) {
         if (loads[i].offset > UINT32_MAX - 4 * (n - 1))
            break;

         bool found_all = true;
         for (unsigned c = 0; c < n; c++) {
            candidate[c] = NULL;
            uint32_t wanted = loads[i].offset + 4 * c;
            for (unsigned j = 0; j < count; j++) {
               if (!loads[j].handled && is_eligible(&loads[j]) &&
                   same_group(&loads[i], &loads[j]) &&
                   loads[j].offset == wanted) {
                  candidate[c] = &loads[j];
                  break;
               }
            }
            if (!candidate[c]) {
               found_all = false;
               break;
            }
         }

         if (!found_all)
            break;
         best = n;
         for (unsigned c = 0; c < n; c++)
            group[c] = candidate[c];
      }

      if (best) {
         *group_count = best;
         return true;
      }
   }

   return false;
}

static bool
transform_group(nir_shader *shader, struct robust_ssbo_load *group[4],
                unsigned count)
{
   struct robust_ssbo_load *first = group[0];
   nir_intrinsic_instr *earliest = first->instr;
   for (unsigned i = 1; i < count; i++) {
      if (nir_instr_is_before(&group[i]->instr->instr, &earliest->instr))
         earliest = group[i]->instr;
   }

   if ((first->base && !def_dominates_instr(first->base, &earliest->instr)) ||
       !def_dominates_instr(first->resource, &earliest->instr))
      return false;

   nir_builder b = nir_builder_create(nir_cf_node_get_function(&earliest->instr.block->cf_node));
   b.cursor = nir_before_instr(&earliest->instr);

   nir_def *low = first->base;
   if (!low)
      low = nir_imm_int(&b, first->offset);
   else {
      if (first->scale != 1)
         low = nir_imul_imm(&b, low, first->scale);
      if (first->offset)
         low = nir_iadd_imm(&b, low, first->offset);
   }

   unsigned bytes = 4 * count;
   nir_def *size = nir_get_ssbo_size(&b, 32, first->resource,
                                     .access = nir_intrinsic_access(first->instr));
   nir_def *size_minus_bytes = nir_iadd_imm(&b, size, -bytes);
   nir_def *valid = nir_iand(&b, nir_uge_imm(&b, size, bytes),
                             nir_uge(&b, size_minus_bytes, low));
   nir_if *if_stmt = nir_push_if(&b, valid);
   nir_def *wide = nir_load_ssbo(&b, count, 32, first->resource, low,
                                 .access = nir_intrinsic_access(first->instr),
                                 .align_mul = 4, .align_offset = 0,
                                 .offset_shift = 0);
   nir_def *wide_channels[4];
   for (unsigned i = 0; i < count; i++)
      wide_channels[i] = nir_channel(&b, wide, i);

   nir_push_else(&b, if_stmt);
   nir_def *scalar[4];
   for (unsigned i = 0; i < count; i++) {
      nir_intrinsic_instr *clone = nir_instr_as_intrinsic(
         nir_instr_clone(shader, &group[i]->instr->instr));
      nir_def *offset = group[i]->base;
      if (!offset)
         offset = nir_imm_int(&b, group[i]->offset);
      else {
         if (group[i]->scale != 1)
            offset = nir_imul_imm(&b, offset, group[i]->scale);
         if (group[i]->offset)
            offset = nir_iadd_imm(&b, offset, group[i]->offset);
      }
      clone->src[1] = nir_src_for_ssa(offset);
      nir_builder_instr_insert(&b, &clone->instr);
      scalar[i] = &clone->def;
   }
   nir_pop_if(&b, NULL);

   for (unsigned i = 0; i < count; i++) {
      nir_def *value = nir_if_phi(&b, wide_channels[i], scalar[i]);
      nir_def_rewrite_uses(&group[i]->instr->def, value);
      nir_instr_remove(&group[i]->instr->instr);
      group[i]->handled = true;
   }

   return true;
}

bool
nir_opt_vectorize_robust_ssbo_loads(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_FRAGMENT || has_unsafe_memory(shader))
      return false;

   bool progress = false;
   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_dominance);
      struct util_dynarray candidates;
      util_dynarray_init(&candidates, shader);

      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_load_ssbo ||
                intr->def.bit_size != 32 || intr->def.num_components != 1)
               continue;
            if (!block->cf_node.parent ||
                block->cf_node.parent->type != nir_cf_node_function)
               continue;

            nir_def *base;
            uint32_t offset, scale;
            if (!parse_offset(intr->src[1].ssa, &base, &offset, &scale))
               continue;

            struct robust_ssbo_load *load = util_dynarray_grow(
               &candidates, struct robust_ssbo_load, 1);
            *load = (struct robust_ssbo_load){
               .instr = intr,
               .resource = intr->src[0].ssa,
               .base = base,
               .offset = offset,
               .scale = scale,
            };
         }
      }

      struct robust_ssbo_load *loads = util_dynarray_begin(&candidates);
      unsigned count = util_dynarray_num_elements(&candidates,
                                                  struct robust_ssbo_load);
      struct robust_ssbo_load *group[4];
      unsigned group_count;
      while (find_group(loads, count, group, &group_count)) {
         if (transform_group(shader, group, group_count)) {
            progress = true;
            nir_metadata_invalidate(shader);
            nir_metadata_require(impl, nir_metadata_dominance);
         } else {
            for (unsigned i = 0; i < group_count; i++)
               group[i]->handled = true;
         }

         for (unsigned i = 0; i < count; i++) {
            if (loads[i].handled)
               continue;
            loads[i].resource = loads[i].instr->src[0].ssa;
            if (!parse_offset(loads[i].instr->src[1].ssa, &loads[i].base,
                              &loads[i].offset, &loads[i].scale))
               loads[i].handled = true;
         }
      }
      util_dynarray_fini(&candidates);
   }

   if (progress)
      nir_metadata_invalidate(shader);
   return progress;
}
