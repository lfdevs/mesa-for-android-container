/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vulkan/vulkan_core.h"
#include "msl_private.h"

typedef enum ti_type {
   /* We haven't been able to assign a type yet */
   TYPE_NONE = 0,
   /* All we know is that this is used in I/O, we
    * can treat it as an opaque value (i.e. uint) */
   TYPE_GENERIC_DATA,
   /* A generic int used in ALU operations but also can a bool for bitwise ops */
   TYPE_GENERIC_INT_OR_BOOL,
   /* A generic int used in ALU operations that can be int or uint */
   TYPE_GENERIC_INT,
   /* These are actual concrete types. */
   TYPE_INT,
   TYPE_UINT,
   TYPE_BOOL,
   TYPE_FLOAT,
   TYPE_SAMPLER,
} ti_type;

static ti_type
unify_types(ti_type t1, ti_type t2)
{
   ti_type generic = MIN2(t1, t2);
   ti_type specific = MAX2(t1, t2);
   if (t1 == t2)
      return TYPE_NONE;
   // NONE or GENERIC_DATA can be upgraded into any concrete type
   if (generic == TYPE_GENERIC_DATA || generic == TYPE_NONE)
      return specific;
   if ((generic == TYPE_GENERIC_INT_OR_BOOL) &&
       ((specific == TYPE_INT) || (specific == TYPE_UINT) ||
        (specific == TYPE_BOOL)))
      return specific;
   if ((generic == TYPE_GENERIC_INT) &&
       ((specific == TYPE_INT) || (specific == TYPE_UINT)))
      return specific;
   return TYPE_NONE;
}

static ti_type
ti_type_from_nir(nir_alu_type nir_type)
{
   switch (nir_alu_type_get_base_type(nir_type)) {
   case nir_type_int:
      return TYPE_INT;
   case nir_type_uint:
      return TYPE_UINT;
   case nir_type_float:
      return TYPE_FLOAT;
   case nir_type_bool:
      return TYPE_BOOL;
   default:
      assert(0);
      return TYPE_NONE;
   }
}

static ti_type
ti_type_from_pipe_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R16_FLOAT:
   case PIPE_FORMAT_R32_FLOAT:
      return TYPE_FLOAT;
   case PIPE_FORMAT_R8_UINT:
   case PIPE_FORMAT_R16_UINT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R64_UINT:
      return TYPE_UINT;
   case PIPE_FORMAT_R8_SINT:
   case PIPE_FORMAT_R16_SINT:
   case PIPE_FORMAT_R32_SINT:
   case PIPE_FORMAT_R64_SINT:
      return TYPE_INT;
   default:
      assert(0);
      return 0u;
   }
}

static void
set_type(struct hash_table *types, void *key, ti_type type)
{
   // convert nir_type
   _mesa_hash_table_insert(types, key, (void *)type);
}

static ti_type
get_type(struct hash_table *types, void *key)
{
   struct hash_entry *entry = _mesa_hash_table_search(types, key);
   if (!entry)
      return TYPE_NONE;
   return (ti_type)(intptr_t)(entry->data);
}

static bool
update_instr_type(struct hash_table *types, nir_instr *instr, ti_type type)
{
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      switch (alu->op) {
      case nir_op_iadd:
      case nir_op_isub:
      case nir_op_ishl:
      case nir_op_iand:
      case nir_op_ior:
      case nir_op_ixor:
         set_type(types, &alu->def, type);
         set_type(types, &alu->src[0].src, type);
         set_type(types, &alu->src[1].src, type);
         return true;
      case nir_op_inot:
         set_type(types, &alu->def, type);
         set_type(types, &alu->src[0].src, type);
         return true;
      case nir_op_ieq:
      case nir_op_ine:
         set_type(types, &alu->src[0].src, type);
         set_type(types, &alu->src[1].src, type);
         return true;
      case nir_op_bcsel:
         set_type(types, &alu->def, type);
         set_type(types, &alu->src[1].src, type);
         set_type(types, &alu->src[2].src, type);
         return true;
      case nir_op_mov:
      case nir_op_vec2:
      case nir_op_vec3:
      case nir_op_vec4:
         set_type(types, &alu->def, type);
         for (int i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
            set_type(types, &alu->src[i].src, type);
         return true;
      default:
         return false;
      }
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      nir_intrinsic_info info = nir_intrinsic_infos[intr->intrinsic];
      switch (intr->intrinsic) {
      case nir_intrinsic_load_reg:
         set_type(types, &intr->def, type);
         set_type(types, &intr->src[0], type);
         return true;
      case nir_intrinsic_store_reg:
         set_type(types, &intr->src[0], type);
         set_type(types, &intr->src[1], type);
         return true;
      case nir_intrinsic_decl_reg:
         set_type(types, &intr->def, type);
         return true;
      case nir_intrinsic_load_global:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_global_constant_bounded:
      case nir_intrinsic_load_global_constant_offset:
      case nir_intrinsic_load_push_constant:
         set_type(types, &intr->def, type);
         return true;
      /* Scratch and shared are always UINT */
      case nir_intrinsic_load_scratch:
      case nir_intrinsic_store_scratch:
      case nir_intrinsic_load_shared:
      case nir_intrinsic_store_shared:
         return false;
      case nir_intrinsic_store_global:
         set_type(types, &intr->src[0], type);
         return true;
      case nir_intrinsic_read_first_invocation:
      case nir_intrinsic_read_invocation:
      case nir_intrinsic_quad_broadcast:
      case nir_intrinsic_quad_swap_horizontal:
      case nir_intrinsic_quad_swap_vertical:
      case nir_intrinsic_quad_swap_diagonal:
      case nir_intrinsic_shuffle:
      case nir_intrinsic_shuffle_down:
      case nir_intrinsic_shuffle_up:
      case nir_intrinsic_shuffle_xor:
         set_type(types, &intr->src[0], type);
         set_type(types, &intr->def, type);
         return true;
      default:
         if (info.has_dest && info.num_srcs == 0) {
            set_type(types, &intr->def, type);
            return true;
         }
         return false;
      }
   } else
      return false;
}

static void
infer_types_from_alu(struct hash_table *types, nir_alu_instr *alu)
{
   // for most types, we infer the type from the nir_op_info,
   // but some ALU instructions are the same for int and uint. Those
   // have their sources and defs get marked by TYPE_GENERIC_INT.
   switch (alu->op) {
   case nir_op_iadd:
   case nir_op_isub:
   case nir_op_ishl:
      // (N, N) -> N
      set_type(types, &alu->def, TYPE_GENERIC_INT);
      set_type(types, &alu->src[0].src, TYPE_GENERIC_INT);
      set_type(types, &alu->src[1].src, TYPE_GENERIC_INT);
      break;
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
      set_type(types, &alu->def, TYPE_GENERIC_INT_OR_BOOL);
      set_type(types, &alu->src[0].src, TYPE_GENERIC_INT_OR_BOOL);
      set_type(types, &alu->src[1].src, TYPE_GENERIC_INT_OR_BOOL);
      break;
   case nir_op_inot:
      // N -> N
      set_type(types, &alu->def, TYPE_GENERIC_INT_OR_BOOL);
      set_type(types, &alu->src[0].src, TYPE_GENERIC_INT_OR_BOOL);
      break;
   case nir_op_ieq:
   case nir_op_ine:
      // (N, N) -> bool
      set_type(types, &alu->def, TYPE_BOOL);
      set_type(types, &alu->src[0].src, TYPE_GENERIC_INT_OR_BOOL);
      set_type(types, &alu->src[1].src, TYPE_GENERIC_INT_OR_BOOL);
      break;
   case nir_op_bcsel:
      // (bool, T, T) -> T
      set_type(types, &alu->def, TYPE_GENERIC_DATA);
      set_type(types, &alu->src[0].src, TYPE_BOOL);
      set_type(types, &alu->src[1].src, TYPE_GENERIC_DATA);
      set_type(types, &alu->src[2].src, TYPE_GENERIC_DATA);
      break;
   // These don't provide any type information, we rely on type propagation
   // to fill in the type data
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
      break;
   /* We don't have 32-bit width boolean, those are uints. */
   case nir_op_b2b32:
      set_type(types, &alu->def, TYPE_UINT);
      set_type(types, &alu->src[0].src, TYPE_UINT);
      break;

   default: {
      // set type for def
      const nir_op_info *info = &nir_op_infos[alu->op];
      set_type(types, &alu->def, ti_type_from_nir(info->output_type));
      for (int i = 0; i < info->num_inputs; i++) {
         // set type for src
         set_type(types, &alu->src[i].src,
                  ti_type_from_nir(info->input_types[i]));
      }
   }
   }
}

static void
infer_types_from_intrinsic(struct hash_table *types, nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_output: {
      ti_type ty = ti_type_from_nir(nir_intrinsic_dest_type(instr));
      set_type(types, &instr->def, ty);
      break;
   }
   case nir_intrinsic_load_global_constant:
      set_type(types, &instr->def, TYPE_GENERIC_DATA);
      set_type(types, &instr->src[0], TYPE_UINT);
      break;
   case nir_intrinsic_load_global_constant_bounded:
      set_type(types, &instr->def, TYPE_GENERIC_DATA);
      set_type(types, &instr->src[0], TYPE_UINT);
      set_type(types, &instr->src[1], TYPE_UINT);
      set_type(types, &instr->src[2], TYPE_UINT);
      break;
   case nir_intrinsic_load_global_constant_offset:
      set_type(types, &instr->def, TYPE_GENERIC_DATA);
      set_type(types, &instr->src[0], TYPE_UINT);
      set_type(types, &instr->src[1], TYPE_UINT);
      break;
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_push_constant:
      set_type(types, &instr->def, TYPE_GENERIC_DATA);
      set_type(types, &instr->src[0], TYPE_UINT);
      break;

   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap: {
      ti_type type =
         ti_type_from_nir(nir_atomic_op_type(nir_intrinsic_atomic_op(instr)));
      set_type(types, &instr->def, type);
      set_type(types, &instr->src[0], TYPE_UINT);
      set_type(types, &instr->src[1], type);
      set_type(types, &instr->src[2], type);
      break;
   }
   case nir_intrinsic_store_global:
      set_type(types, &instr->src[0], TYPE_GENERIC_DATA);
      set_type(types, &instr->src[1], TYPE_UINT);
      break;
   case nir_intrinsic_store_output: {
      ti_type ty = ti_type_from_nir(nir_intrinsic_src_type(instr));
      set_type(types, &instr->src[0], ty);
      break;
   }
   case nir_intrinsic_decl_reg:
      if (nir_intrinsic_bit_size(instr) == 1)
         set_type(types, &instr->def, TYPE_BOOL);
      else
         set_type(types, &instr->def, TYPE_NONE);
      break;
   case nir_intrinsic_store_reg:
      set_type(types, &instr->src[0], TYPE_NONE);
      set_type(types, &instr->src[1], TYPE_NONE);
      break;
   case nir_intrinsic_load_reg:
      set_type(types, &instr->src[0], TYPE_NONE);
      set_type(types, &instr->def, TYPE_NONE);
      break;
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_shared:
      set_type(types, &instr->def, TYPE_UINT);
      set_type(types, &instr->src[0], TYPE_UINT);
      break;
   case nir_intrinsic_store_scratch:
   case nir_intrinsic_store_shared:
      set_type(types, &instr->src[0], TYPE_UINT);
      set_type(types, &instr->src[1], TYPE_UINT);
      break;
   case nir_intrinsic_load_workgroup_id:
   case nir_intrinsic_load_subgroup_id:
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_global_invocation_id:
   case nir_intrinsic_load_num_workgroups:
   case nir_intrinsic_load_num_subgroups:
   case nir_intrinsic_load_subgroup_size:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_mask:
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_amplification_id_kk:
      set_type(types, &instr->def, TYPE_UINT);
      break;
   case nir_intrinsic_load_vulkan_descriptor:
      set_type(types, &instr->src[0], TYPE_UINT);
      set_type(types, &instr->def, TYPE_UINT);
      break;
   case nir_intrinsic_load_buffer_ptr_kk:
      set_type(types, &instr->def, TYPE_UINT);
      break;
   // The defs of these instructions don't participate in type inference
   // but their sources are pointers (i.e. uints).
   case nir_intrinsic_load_texture_handle_kk:
   case nir_intrinsic_load_depth_texture_kk:
      set_type(types, &instr->src[0], TYPE_UINT);
      break;
   case nir_intrinsic_load_sampler_handle_kk:
      set_type(types, &instr->def, TYPE_SAMPLER);
      break;
   case nir_intrinsic_store_clip_distance_kk:
      set_type(types, &instr->src[0], TYPE_FLOAT);
      break;
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddy_coarse:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddy_fine:
      set_type(types, &instr->src[0], TYPE_FLOAT);
      set_type(types, &instr->def, TYPE_FLOAT);
      break;
   case nir_intrinsic_load_point_coord:
      set_type(types, &instr->def, TYPE_FLOAT);
      break;
   case nir_intrinsic_load_front_face:
   case nir_intrinsic_elect:
   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_is_helper_invocation:
      set_type(types, &instr->def, TYPE_BOOL);
      break;
   case nir_intrinsic_load_constant_agx:
      set_type(types, &instr->src[0], TYPE_UINT);
      set_type(types, &instr->src[1], TYPE_UINT);
      set_type(types, &instr->def,
               ti_type_from_pipe_format(nir_intrinsic_format(instr)));
      break;
   case nir_intrinsic_bindless_image_load:
      set_type(types, &instr->def,
               ti_type_from_nir(nir_intrinsic_dest_type(instr)));
      set_type(types, &instr->src[1], TYPE_UINT); // coords
      set_type(types, &instr->src[3], TYPE_UINT); // level
      break;
   case nir_intrinsic_bindless_image_store:
      set_type(types, &instr->src[1], TYPE_UINT); // coords
      set_type(types, &instr->src[3],
               ti_type_from_nir(nir_intrinsic_src_type(instr)));
      set_type(types, &instr->src[4], TYPE_UINT); // level
      break;
   case nir_intrinsic_demote_if:
   case nir_intrinsic_terminate_if:
      set_type(types, &instr->src[0], TYPE_BOOL);
      break;
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap: {
      set_type(types, &instr->src[1], TYPE_UINT); // coords
      set_type(types, &instr->src[2], TYPE_UINT); // level
      ti_type type =
         ti_type_from_nir(nir_atomic_op_type(nir_intrinsic_atomic_op(instr)));
      set_type(types, &instr->src[3], type);
      if (instr->intrinsic == nir_intrinsic_bindless_image_atomic_swap)
         set_type(types, &instr->src[4], type);
      set_type(types, &instr->def, type);
      break;
   }
   case nir_intrinsic_ballot:
      set_type(types, &instr->src[0], TYPE_BOOL);
      set_type(types, &instr->def, TYPE_UINT);
      break;
   case nir_intrinsic_vote_all:
   case nir_intrinsic_vote_any:
      set_type(types, &instr->src[0], TYPE_BOOL);
      set_type(types, &instr->def, TYPE_BOOL);
      break;
   case nir_intrinsic_read_first_invocation:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
      set_type(types, &instr->src[0], TYPE_GENERIC_DATA);
      set_type(types, &instr->def, TYPE_GENERIC_DATA);
      break;
   case nir_intrinsic_read_invocation:
   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_down:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_xor:
      set_type(types, &instr->src[0], TYPE_GENERIC_DATA);
      set_type(types, &instr->def, TYPE_GENERIC_DATA);
      set_type(types, &instr->src[1], TYPE_UINT);
      break;
   case nir_intrinsic_reduce:
      switch (nir_intrinsic_reduction_op(instr)) {
      case nir_op_iand:
      case nir_op_ior:
      case nir_op_ixor:
      case nir_op_iadd:
      case nir_op_imul:
         set_type(types, &instr->src[0], TYPE_GENERIC_INT);
         set_type(types, &instr->def, TYPE_GENERIC_INT);
         break;
      case nir_op_imax:
      case nir_op_imin:
         set_type(types, &instr->src[0], TYPE_INT);
         set_type(types, &instr->def, TYPE_INT);
         break;
      case nir_op_umax:
      case nir_op_umin:
         set_type(types, &instr->src[0], TYPE_UINT);
         set_type(types, &instr->def, TYPE_UINT);
         break;
      case nir_op_fadd:
      case nir_op_fmax:
      case nir_op_fmin:
      case nir_op_fmul:
         set_type(types, &instr->src[0], TYPE_FLOAT);
         set_type(types, &instr->def, TYPE_FLOAT);
         break;
      default:
         break;
      }
      break;
   default:
      break;
   }
}

static void
infer_types_from_tex(struct hash_table *types, nir_tex_instr *tex)
{
   set_type(types, &tex->def, ti_type_from_nir(tex->dest_type));
   for (int i = 0; i < tex->num_srcs; i++) {
      nir_src *src = &tex->src[i].src;
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
         if (tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms)
            set_type(types, src, TYPE_UINT);
         else
            set_type(types, src, TYPE_FLOAT);
         break;
      case nir_tex_src_comparator:
         set_type(types, src, TYPE_FLOAT);
         break;
      case nir_tex_src_offset:
         set_type(types, src, TYPE_INT);
         break;
      case nir_tex_src_bias:
         set_type(types, src, TYPE_FLOAT);
         break;
      case nir_tex_src_lod:
         if (tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms ||
             tex->op == nir_texop_txs)
            set_type(types, src, TYPE_UINT);
         else
            set_type(types, src, TYPE_FLOAT);
         break;
      case nir_tex_src_min_lod:
         set_type(types, src, TYPE_FLOAT);
         break;
      case nir_tex_src_ms_index:
         set_type(types, src, TYPE_UINT);
         break;
      case nir_tex_src_ddx:
      case nir_tex_src_ddy:
         set_type(types, src, TYPE_FLOAT);
         break;
      default:
         break;
      }
   }
}

static void
infer_types_from_instr(struct hash_table *types, nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      infer_types_from_alu(types, nir_instr_as_alu(instr));
      return;
   case nir_instr_type_intrinsic:
      infer_types_from_intrinsic(types, nir_instr_as_intrinsic(instr));
      return;
   case nir_instr_type_tex:
      infer_types_from_tex(types, nir_instr_as_tex(instr));
      break;
   default:
      break;
   }
}

static bool
propagate_types(struct hash_table *types, nir_instr *instr)
{
   bool progress = false;
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      nir_op_info info = nir_op_infos[alu->op];
      for (int i = 0; i < info.num_inputs; i++) {
         ti_type src_type = get_type(types, &alu->src[i].src);
         ti_type def_type = get_type(types, alu->src[i].src.ssa);
         ti_type unified_type = unify_types(src_type, def_type);
         nir_instr *parent_instr = nir_def_instr(alu->src[i].src.ssa);
         if (unified_type > src_type) {
            progress |= update_instr_type(types, instr, unified_type);
         } else if (unified_type > def_type) {
            progress |= update_instr_type(types, parent_instr, unified_type);
         }
      }
      break;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      nir_intrinsic_info info = nir_intrinsic_infos[intr->intrinsic];
      for (int i = 0; i < info.num_srcs; i++) {
         ti_type src_type = get_type(types, &intr->src[i]);
         ti_type def_type = get_type(types, intr->src[i].ssa);
         ti_type unified_type = unify_types(src_type, def_type);
         nir_instr *parent_instr = nir_def_instr(intr->src[i].ssa);
         if (unified_type > src_type) {
            progress |= update_instr_type(types, instr, unified_type);
         } else if (unified_type > def_type) {
            progress |= update_instr_type(types, parent_instr, unified_type);
         }
      }
      break;
   }
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      for (int i = 0; i < tex->num_srcs; i++) {
         ti_type src_type = get_type(types, &tex->src[i].src);
         ti_type def_type = get_type(types, tex->src[i].src.ssa);
         ti_type unified_type = unify_types(src_type, def_type);
         if (src_type == 0)
            continue;
         nir_instr *parent_instr = nir_def_instr(tex->src[i].src.ssa);
         if (unified_type > def_type) {
            progress |= update_instr_type(types, parent_instr, unified_type);
         }
      }
      break;
   }
   default:
      break;
   }
   return progress;
}

static const char *float_names[] = {"float", "float2", "float3", "float4"};
static const char *half_names[] = {"half", "half2", "half3", "half4"};
static const char *bool_names[] = {"bool", "bool2", "bool3", "bool4"};
static const char *int8_names[] = {"char", "char2", "char3", "char4"};
static const char *uint8_names[] = {"uchar", "uchar2", "uchar3", "uchar4"};
static const char *int16_names[] = {"short", "short2", "short3", "short4"};
static const char *uint16_names[] = {"ushort", "ushort2", "ushort3", "ushort4"};
static const char *int32_names[] = {"int", "int2", "int3", "int4"};
static const char *uint32_names[] = {"uint", "uint2", "uint3", "uint4"};
static const char *int64_names[] = {"long", "long2", "long3", "long4"};
static const char *uint64_names[] = {"ulong", "ulong2", "ulong3", "ulong4"};

static const char *
ti_type_to_msl_type(ti_type type, uint8_t bit_width, uint8_t num_components)
{
   switch (type) {
   case TYPE_GENERIC_DATA:
   case TYPE_GENERIC_INT:
   case TYPE_GENERIC_INT_OR_BOOL:
   case TYPE_UINT:
      switch (bit_width) {
      case 1:
         return bool_names[num_components - 1];
      case 8:
         return uint8_names[num_components - 1];
      case 16:
         return uint16_names[num_components - 1];
      case 32:
         return uint32_names[num_components - 1];
      case 64:
         return uint64_names[num_components - 1];
      default:
         assert(!"Bad uint length");
      }
      break;
   case TYPE_BOOL:
      return bool_names[num_components - 1];
   case TYPE_INT:
      switch (bit_width) {
      case 8:
         return int8_names[num_components - 1];
      case 16:
         return int16_names[num_components - 1];
      case 32:
         return int32_names[num_components - 1];
      case 64:
         return int64_names[num_components - 1];
      default:
         assert(!"Bad uint length");
      }
      break;
   case TYPE_FLOAT:
      switch (bit_width) {
      case 16:
         return half_names[num_components - 1];
      case 32:
         return float_names[num_components - 1];
      default:
         assert(!"Bad float length");
      }
      break;
   case TYPE_SAMPLER:
      return "sampler";
   default:
      return NULL;
   }

   return NULL;
}

const char *
msl_uint_type(uint8_t bit_size, uint8_t num_components)
{
   return ti_type_to_msl_type(TYPE_UINT, bit_size, num_components);
}

const char *
msl_type_for_def(struct hash_table *types, nir_def *def)
{
   ti_type type = get_type(types, def);
   return ti_type_to_msl_type(type, def->bit_size, def->num_components);
}

const char *
msl_type_for_src(struct hash_table *types, nir_src *src)
{
   ti_type type = get_type(types, src);
   // This won't necessarily work for alu srcs but for intrinsics it's fine.
   return ti_type_to_msl_type(type, src->ssa->bit_size,
                              src->ssa->num_components);
}

const char *
msl_bitcast_for_src(struct hash_table *types, nir_src *src)
{
   ti_type src_type = get_type(types, src);
   ti_type def_type = get_type(types, src->ssa);
   if (nir_src_is_if(src))
      return NULL;
   if (src_type != def_type) {
      /* bool types cannot use as_type casting */
      if (src_type == TYPE_BOOL || def_type == TYPE_BOOL)
         return NULL;

      // produce bitcast _into_ src_type
      return ti_type_to_msl_type(src_type, src->ssa->bit_size,
                                 src->ssa->num_components);
   } else {
      return NULL;
   }
}

static void
emit_src_component(struct nir_to_msl_ctx *ctx, nir_src *src, unsigned comp)
{
   ti_type type = get_type(ctx->types, src);
   switch (type) {
   case TYPE_FLOAT: {
      double v = nir_src_comp_as_float(*src, comp);
      if (isinf(v)) {
         P(ctx, "(INFINITY");
      } else if (isnan(v)) {
         P(ctx, "(NAN");
      } else {
         /* Building the types explicitly is required since the MSL compiler is
          * too dumb to understand that "max(as_type<int>(t53), -2147483648)" is
          * not ambiguous since both are ints and there's no room for longs.
          * From CTS test:
          * dEQP-VK.renderpass.suballocation.multisample.r32_sint.samples_2 */
         if (src->ssa->bit_size == 16) {
            P(ctx, "half(");
         } else {
            P(ctx, "float(");
         }
         P(ctx, "%.*le", DBL_DECIMAL_DIG, nir_src_comp_as_float(*src, comp));
      }
      break;
   }
   case TYPE_BOOL:
      P(ctx, "bool(%d", nir_src_comp_as_bool(*src, comp));
      break;
   case TYPE_INT:
      switch (src->ssa->bit_size) {
      case 8:
         P(ctx, "char(");
         break;
      case 16:
         P(ctx, "short(");
         break;
      case 32:
         P(ctx, "int(");
         break;
      case 64:
         P(ctx, "long(");
         break;
      default:
         UNREACHABLE("Incorrect bit_size for TYPE_INT");
      }
      P(ctx, "%" PRId64, nir_src_comp_as_int(*src, comp));
      break;
   case TYPE_UINT:
   case TYPE_GENERIC_DATA:
   case TYPE_GENERIC_INT:
   case TYPE_GENERIC_INT_OR_BOOL:
      switch (src->ssa->bit_size) {
      case 8:
         P(ctx, "uchar(");
         break;
      case 16:
         P(ctx, "ushort(");
         break;
      case 32:
         P(ctx, "uint(");
         break;
      case 64:
         P(ctx, "ulong(");
         break;
      default:
         UNREACHABLE("Incorrect bit_size for TYPE_UINT");
      }
      P(ctx, "%" PRIu64 "u", nir_src_comp_as_uint(*src, comp));
      break;
   case TYPE_NONE:
      assert(0);
      P(ctx, "UNTYPED!");
      break;
   default:
      return;
   }
   P(ctx, ")");
}

void
msl_src_as_const(struct nir_to_msl_ctx *ctx, nir_src *src)
{
   ti_type type = get_type(ctx->types, src);
   if (src->ssa->num_components == 1) {
      emit_src_component(ctx, src, 0);
   } else {
      P(ctx, "%s(",
        ti_type_to_msl_type(type, src->ssa->bit_size,
                            src->ssa->num_components));
      for (int i = 0; i < src->ssa->num_components; i++) {
         if (i)
            P(ctx, ", ");
         emit_src_component(ctx, src, i);
      }
      P(ctx, ")");
   }
}

struct hash_table *
msl_infer_types(nir_shader *shader)
{
   struct hash_table *types = _mesa_pointer_hash_table_create(NULL);
   bool progress = false;
   // First, seed the types for every instruction for every source and def
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            infer_types_from_instr(types, instr);
         }
      }
   }

   do {
      progress = false;
      nir_foreach_function_impl(impl, shader) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               progress |= propagate_types(types, instr);
            }
         }
      }
   } while (progress);
   return types;
}

bool
msl_src_is_float(struct nir_to_msl_ctx *ctx, nir_src *src)
{
   return get_type(ctx->types, src) == TYPE_FLOAT;
}

bool
msl_def_is_sampler(struct nir_to_msl_ctx *ctx, nir_def *def)
{
   return get_type(ctx->types, def) == TYPE_SAMPLER;
}
