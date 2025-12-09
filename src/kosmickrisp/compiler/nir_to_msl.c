/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "nir_to_msl.h"
#include "msl_private.h"
#include "nir.h"

static const char *
get_stage_string(mesa_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
      return "vertex";
   case MESA_SHADER_FRAGMENT:
      return "fragment";
   case MESA_SHADER_COMPUTE:
      return "kernel";
   default:
      assert(0);
      return "";
   }
}

static const char *
get_entrypoint_name(nir_shader *shader)
{
   return nir_shader_get_entrypoint(shader)->function->name;
}

static const char *sysval_table[SYSTEM_VALUE_MAX] = {
   [SYSTEM_VALUE_SUBGROUP_SIZE] =
      "uint gl_SubGroupSize [[threads_per_simdgroup]]",
   [SYSTEM_VALUE_SUBGROUP_INVOCATION] =
      "uint gl_SubGroupInvocation [[thread_index_in_simdgroup]]",
   [SYSTEM_VALUE_NUM_SUBGROUPS] =
      "uint gl_NumSubGroups [[simdgroups_per_threadgroup]]",
   [SYSTEM_VALUE_SUBGROUP_ID] =
      "uint gl_SubGroupID [[simdgroup_index_in_threadgroup]]",
   [SYSTEM_VALUE_WORKGROUP_ID] =
      "uint3 gl_WorkGroupID [[threadgroup_position_in_grid]]",
   [SYSTEM_VALUE_LOCAL_INVOCATION_ID] =
      "uint3 gl_LocalInvocationID [[thread_position_in_threadgroup]]",
   [SYSTEM_VALUE_GLOBAL_INVOCATION_ID] =
      "uint3 gl_GlobalInvocationID [[thread_position_in_grid]]",
   [SYSTEM_VALUE_NUM_WORKGROUPS] =
      "uint3 gl_NumWorkGroups [[threadgroups_per_grid]]",
   [SYSTEM_VALUE_LOCAL_INVOCATION_INDEX] =
      "uint gl_LocalInvocationIndex [[thread_index_in_threadgroup]]",
   [SYSTEM_VALUE_VERTEX_ID] = "uint gl_VertexID [[vertex_id]]",
   [SYSTEM_VALUE_INSTANCE_ID] = "uint gl_InstanceID [[instance_id]]",
   [SYSTEM_VALUE_BASE_INSTANCE] = "uint gl_BaseInstance [[base_instance]]",
   [SYSTEM_VALUE_FRAG_COORD] = "float4 gl_FragCoord [[position]]",
   [SYSTEM_VALUE_POINT_COORD] = "float2 gl_PointCoord [[point_coord]]",
   [SYSTEM_VALUE_FRONT_FACE] = "bool gl_FrontFacing [[front_facing]]",
   [SYSTEM_VALUE_LAYER_ID] = "uint gl_Layer [[render_target_array_index]]",
   [SYSTEM_VALUE_SAMPLE_ID] = "uint gl_SampleID [[sample_id]]",
   [SYSTEM_VALUE_SAMPLE_MASK_IN] = "uint gl_SampleMask [[sample_mask]]",
   [SYSTEM_VALUE_AMPLIFICATION_ID_KK] =
      "uint mtl_AmplificationID [[amplification_id]]",
   [SYSTEM_VALUE_FIRST_VERTEX] = "uint gl_FirstVertex [[base_vertex]]",
   /* These are functions and not shader input variables */
   [SYSTEM_VALUE_HELPER_INVOCATION] = "",
};

static void
emit_sysvals(struct nir_to_msl_ctx *ctx, nir_shader *shader)
{
   unsigned i;
   BITSET_FOREACH_SET(i, shader->info.system_values_read, SYSTEM_VALUE_MAX) {
      assert(sysval_table[i]);
      if (sysval_table[i] && sysval_table[i][0])
         P_IND(ctx, "%s,\n", sysval_table[i]);
   }
}

static void
emit_inputs(struct nir_to_msl_ctx *ctx, nir_shader *shader)
{
   switch (shader->info.stage) {
   case MESA_SHADER_FRAGMENT:
      P_IND(ctx, "FragmentIn in [[stage_in]],\n");
      break;
   default:
      break;
   }
   P_IND(ctx, "constant Buffer &buf0 [[buffer(0)]],\n");
   P_IND(ctx, "constant SamplerTable &sampler_table [[buffer(1)]]\n");
}

static const char *
output_type(nir_shader *shader)
{
   switch (shader->info.stage) {
   case MESA_SHADER_VERTEX:
      return "VertexOut";
   case MESA_SHADER_FRAGMENT:
      return "FragmentOut";
   default:
      return "void";
   }
}

static void
emit_local_vars(struct nir_to_msl_ctx *ctx, nir_shader *shader)
{
   if (shader->info.shared_size) {
      P_IND(ctx, "threadgroup char shared_data[%d];\n",
            shader->info.shared_size);
   }
   if (shader->scratch_size) {
      /* KK_WORKAROUND_1 */
      if (ctx->disabled_workarounds & BITFIELD64_BIT(1)) {
         P_IND(ctx, "uchar scratch[%d];\n", shader->scratch_size);
      } else {
         P_IND(ctx, "uchar scratch[%d] = {0};\n", shader->scratch_size);
      }
   }
}

static bool
is_register(nir_def *def)
{
   return ((nir_def_is_intrinsic(def)) &&
           (nir_def_as_intrinsic(def)->intrinsic == nir_intrinsic_load_reg));
}

static void
writemask_to_msl(struct nir_to_msl_ctx *ctx, unsigned write_mask,
                 unsigned num_components)
{
   if (num_components != util_bitcount(write_mask)) {
      P(ctx, ".");
      for (unsigned i = 0; i < num_components; i++)
         if ((write_mask >> i) & 1)
            P(ctx, "%c", "xyzw"[i]);
   }
}

static void
src_to_msl(struct nir_to_msl_ctx *ctx, nir_src *src)
{
   /* Pointer types cannot use as_type casting */
   const char *bitcast = msl_bitcast_for_src(ctx->types, src);
   if (nir_src_is_const(*src)) {
      msl_src_as_const(ctx, src);
      return;
   }
   if (nir_src_is_undef(*src)) {
      if (src->ssa->num_components == 1) {
         P(ctx, "00");
      } else {
         P(ctx, "%s(", msl_type_for_src(ctx->types, src));
         for (int i = 0; i < src->ssa->num_components; i++) {
            if (i)
               P(ctx, ", ");
            P(ctx, "00");
         }
         P(ctx, ")");
      }
      return;
   }

   if (bitcast)
      P(ctx, "as_type<%s>(", bitcast);
   if (is_register(src->ssa)) {
      nir_intrinsic_instr *instr = nir_def_as_intrinsic(src->ssa);
      if (src->ssa->bit_size != 1u) {
         P(ctx, "as_type<%s>(r%d)", msl_type_for_def(ctx->types, src->ssa),
           instr->src[0].ssa->index);
      } else {
         P(ctx, "%s(r%d)", msl_type_for_def(ctx->types, src->ssa),
           instr->src[0].ssa->index);
      }
   } else if (nir_src_is_const(*src)) {
      msl_src_as_const(ctx, src);
   } else {
      P(ctx, "t%d", src->ssa->index);
   }
   if (bitcast)
      P(ctx, ")");
}

static void
alu_src_to_msl(struct nir_to_msl_ctx *ctx, nir_alu_instr *instr, int srcn)
{
   nir_alu_src *src = &instr->src[srcn];
   src_to_msl(ctx, &src->src);
   if (!nir_alu_src_is_trivial_ssa(instr, srcn) &&
       src->src.ssa->num_components > 1) {
      int num_components = nir_src_num_components(src->src);
      assert(num_components <= 4);

      P(ctx, ".");
      for (int i = 0; i < NIR_MAX_VEC_COMPONENTS; i++) {
         if (!nir_alu_instr_channel_used(instr, srcn, i))
            continue;
         P(ctx, "%c", "xyzw"[src->swizzle[i]]);
      }
   }
}

static void
alu_funclike(struct nir_to_msl_ctx *ctx, nir_alu_instr *instr, const char *name)
{
   const nir_op_info *info = &nir_op_infos[instr->op];
   P(ctx, "%s(", name);
   for (int i = 0; i < info->num_inputs; i++) {
      alu_src_to_msl(ctx, instr, i);
      if (i < info->num_inputs - 1)
         P(ctx, ", ");
   }
   P(ctx, ")");
}

static void
alu_to_msl(struct nir_to_msl_ctx *ctx, nir_alu_instr *instr)
{

#define ALU_BINOP(op)                                                          \
   do {                                                                        \
      alu_src_to_msl(ctx, instr, 0);                                           \
      P(ctx, " %s ", op);                                                      \
      alu_src_to_msl(ctx, instr, 1);                                           \
   } while (0);

   switch (instr->op) {
   case nir_op_isign:
      alu_src_to_msl(ctx, instr, 0);
      P(ctx, " == 0 ? 0.0 : ((");
      alu_src_to_msl(ctx, instr, 0);
      P(ctx, " < 0) ? -1 : 1)");
      break;
   case nir_op_iadd:
   case nir_op_fadd:
      ALU_BINOP("+");
      break;
   case nir_op_uadd_sat:
   case nir_op_iadd_sat:
      alu_funclike(ctx, instr, "addsat");
      break;
   case nir_op_isub:
   case nir_op_fsub:
      ALU_BINOP("-");
      break;
   case nir_op_imul:
   case nir_op_fmul:
      ALU_BINOP("*");
      break;
   case nir_op_idiv:
   case nir_op_udiv:
   case nir_op_fdiv:
      ALU_BINOP("/");
      break;
   case nir_op_irem:
      ALU_BINOP("%");
      break;
   case nir_op_ishl:
      ALU_BINOP("<<");
      break;
   case nir_op_ishr:
   case nir_op_ushr:
      ALU_BINOP(">>");
      break;
   case nir_op_ige:
   case nir_op_uge:
   case nir_op_fge:
      ALU_BINOP(">=");
      break;
   case nir_op_ilt:
   case nir_op_ult:
   case nir_op_flt:
      ALU_BINOP("<")
      break;
   case nir_op_iand:
      ALU_BINOP("&");
      break;
   case nir_op_ior:
      ALU_BINOP("|");
      break;
   case nir_op_ixor:
      ALU_BINOP("^");
      break;
   case nir_op_bitfield_insert:
      alu_funclike(ctx, instr, "insert_bits");
      break;
   case nir_op_ibitfield_extract:
   case nir_op_ubitfield_extract:
      alu_funclike(ctx, instr, "extract_bits");
      break;
   case nir_op_bitfield_reverse:
      alu_funclike(ctx, instr, "reverse_bits");
      break;
   case nir_op_bit_count:
      alu_funclike(ctx, instr, "popcount");
      break;
   case nir_op_uclz:
      alu_funclike(ctx, instr, "clz");
      break;
   case nir_op_ieq:
   case nir_op_feq:
      ALU_BINOP("==");
      break;
   case nir_op_ine:
   case nir_op_fneu:
      ALU_BINOP("!=");
      break;
   case nir_op_umax:
   case nir_op_imax:
      alu_funclike(ctx, instr, "max");
      break;
   case nir_op_umin:
   case nir_op_imin:
      alu_funclike(ctx, instr, "min");
      break;
   case nir_op_umod:
   case nir_op_imod:
      ALU_BINOP("%");
      break;
   case nir_op_imul_high:
   case nir_op_umul_high:
      alu_funclike(ctx, instr, "mulhi");
      break;
   case nir_op_usub_sat:
      alu_funclike(ctx, instr, "subsat");
      break;
   case nir_op_fsat:
      alu_funclike(ctx, instr, "saturate");
      break;
   /* Functions from <metal_relational> */
   case nir_op_fisfinite:
      alu_funclike(ctx, instr, "isfinite");
      break;
   case nir_op_fisnormal:
      alu_funclike(ctx, instr, "isnormal");
      break;
   /* Functions from <metal_math> */
   case nir_op_iabs:
   case nir_op_fabs:
      alu_funclike(ctx, instr, "abs");
      break;
   case nir_op_fceil:
      alu_funclike(ctx, instr, "ceil");
      break;
   case nir_op_fcos:
      alu_funclike(ctx, instr, "cos");
      break;
   case nir_op_fdot2:
   case nir_op_fdot3:
   case nir_op_fdot4:
      alu_funclike(ctx, instr, "dot");
      break;
   case nir_op_fexp2:
      alu_funclike(ctx, instr, "exp2");
      break;
   case nir_op_ffloor:
      alu_funclike(ctx, instr, "floor");
      break;
   case nir_op_ffma:
      alu_funclike(ctx, instr, "fma");
      break;
   case nir_op_ffract:
      alu_funclike(ctx, instr, "fract");
      break;
   case nir_op_flog2:
      alu_funclike(ctx, instr, "log2");
      break;
   case nir_op_flrp:
      alu_funclike(ctx, instr, "mix");
      break;
   case nir_op_fmax:
      alu_funclike(ctx, instr, "fmax");
      break;
   case nir_op_fmin:
      alu_funclike(ctx, instr, "fmin");
      break;
   case nir_op_frem:
      alu_funclike(ctx, instr, "fmod");
      break;
   case nir_op_fpow:
      alu_funclike(ctx, instr, "pow");
      break;
   case nir_op_fround_even:
      alu_funclike(ctx, instr, "rint");
      break;
   case nir_op_frsq:
      alu_funclike(ctx, instr, "rsqrt");
      break;
   case nir_op_fsign:
      alu_funclike(ctx, instr, "sign");
      break;
   case nir_op_fsqrt:
      alu_funclike(ctx, instr, "sqrt");
      break;
   case nir_op_fsin:
      alu_funclike(ctx, instr, "sin");
      break;
   case nir_op_ldexp:
      alu_funclike(ctx, instr, "ldexp");
      break;
   case nir_op_ftrunc:
      alu_funclike(ctx, instr, "trunc");
      break;
   case nir_op_pack_snorm_4x8:
      alu_funclike(ctx, instr, "pack_float_to_snorm4x8");
      break;
   case nir_op_pack_unorm_4x8:
      alu_funclike(ctx, instr, "pack_float_to_unorm4x8");
      break;
   case nir_op_pack_snorm_2x16:
      alu_funclike(ctx, instr, "pack_float_to_snorm2x16");
      break;
   case nir_op_pack_unorm_2x16:
      alu_funclike(ctx, instr, "pack_float_to_unorm2x16");
      break;
   case nir_op_unpack_snorm_4x8:
      alu_funclike(ctx, instr, "unpack_snorm4x8_to_float");
      break;
   case nir_op_unpack_unorm_4x8:
      alu_funclike(ctx, instr, "unpack_unorm4x8_to_float");
      break;
   case nir_op_unpack_snorm_2x16:
      alu_funclike(ctx, instr, "unpack_snorm2x16_to_float");
      break;
   case nir_op_unpack_unorm_2x16:
      alu_funclike(ctx, instr, "unpack_unorm2x16_to_float");
      break;
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_b2b1:
   case nir_op_b2b32:
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
   case nir_op_b2f16:
   case nir_op_i2f16:
   case nir_op_u2f16:
   case nir_op_i2f32:
   case nir_op_u2f32:
   case nir_op_i2i8:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_i2i64:
   case nir_op_f2i8:
   case nir_op_f2i16:
   case nir_op_f2i32:
   case nir_op_f2i64:
   case nir_op_f2u8:
   case nir_op_f2u16:
   case nir_op_f2u32:
   case nir_op_f2u64:
   case nir_op_u2u8:
   case nir_op_u2u16:
   case nir_op_u2u32:
   case nir_op_u2u64:
   case nir_op_f2f16:
   case nir_op_f2f16_rtne:
   case nir_op_f2f32:
      alu_funclike(ctx, instr, msl_type_for_def(ctx->types, &instr->def));
      break;
   case nir_op_unpack_half_2x16_split_x:
      P(ctx, "float(as_type<half>(ushort(t%d & 0x0000ffff)))",
        instr->src[0].src.ssa->index);
      break;
   case nir_op_frcp:
      P(ctx, "1/");
      alu_src_to_msl(ctx, instr, 0);
      break;
   case nir_op_inot:
      if (instr->src[0].src.ssa->bit_size == 1) {
         P(ctx, "!");
      } else {
         P(ctx, "~");
      }
      alu_src_to_msl(ctx, instr, 0);
      break;
   case nir_op_ineg:
   case nir_op_fneg:
      P(ctx, "-");
      alu_src_to_msl(ctx, instr, 0);
      break;
   case nir_op_mov:
      alu_src_to_msl(ctx, instr, 0);
      break;
   case nir_op_b2f32:
      alu_src_to_msl(ctx, instr, 0);
      P(ctx, " ? 1.0 : 0.0");
      break;
   case nir_op_bcsel:
      alu_src_to_msl(ctx, instr, 0);
      P(ctx, " ? ");
      alu_src_to_msl(ctx, instr, 1);
      P(ctx, " : ");
      alu_src_to_msl(ctx, instr, 2);
      break;
   default:
      P(ctx, "ALU %s", nir_op_infos[instr->op].name);
   }
}

static const char *
texture_dim(enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
      return "1d";
   case GLSL_SAMPLER_DIM_2D:
      return "2d";
   case GLSL_SAMPLER_DIM_3D:
      return "3d";
   case GLSL_SAMPLER_DIM_CUBE:
      return "cube";
   case GLSL_SAMPLER_DIM_BUF:
      return "_buffer";
   case GLSL_SAMPLER_DIM_MS:
      return "2d_ms";
   default:
      fprintf(stderr, "Bad texture dim %d\n", dim);
      assert(!"Bad texture dimension");
      return "BAD";
   }
}

static const char *
tex_type_name(nir_alu_type ty)
{
   switch (ty) {
   case nir_type_int16:
      return "short";
   case nir_type_int32:
      return "int";
   case nir_type_uint16:
      return "ushort";
   case nir_type_uint32:
      return "uint";
   case nir_type_float16:
      return "half";
   case nir_type_float32:
      return "float";
   default:
      return "BAD";
   }
}

static bool
instrinsic_needs_dest_type(nir_intrinsic_instr *instr)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[instr->intrinsic];
   nir_intrinsic_op op = instr->intrinsic;
   if (op == nir_intrinsic_decl_reg || op == nir_intrinsic_load_reg ||
       op == nir_intrinsic_load_texture_handle_kk ||
       op == nir_intrinsic_load_depth_texture_kk ||
       /* Atomic swaps have a custom codegen */
       op == nir_intrinsic_global_atomic_swap ||
       op == nir_intrinsic_shared_atomic_swap ||
       op == nir_intrinsic_bindless_image_atomic_swap)
      return false;
   return info->has_dest;
}

static const char *
msl_pipe_format_to_msl_type(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R16_FLOAT:
      return "half";
   case PIPE_FORMAT_R32_FLOAT:
      return "float";
   case PIPE_FORMAT_R8_UINT:
      return "uchar";
   case PIPE_FORMAT_R16_UINT:
      return "ushort";
   case PIPE_FORMAT_R32_UINT:
      return "uint";
   case PIPE_FORMAT_R64_UINT:
      return "unsigned long";
   case PIPE_FORMAT_R8_SINT:
      return "char";
   case PIPE_FORMAT_R16_SINT:
      return "short";
   case PIPE_FORMAT_R32_SINT:
      return "int";
   case PIPE_FORMAT_R64_SINT:
      return "long";
   default:
      assert(0);
      return "";
   }
}

static const char *
component_str(uint8_t num_components)
{
   switch (num_components) {
   default:
   case 1:
      return "";
   case 2:
      return "2";
   case 3:
      return "3";
   case 4:
      return "4";
   }
}

static void
round_src_component_to_uint(struct nir_to_msl_ctx *ctx, nir_src *src,
                            char component)
{
   bool is_float = msl_src_is_float(ctx, src);
   if (is_float) {
      P(ctx, "uint(max(int(rint(");
   }
   src_to_msl(ctx, src);
   P(ctx, ".%c", component);
   if (is_float) {
      P(ctx, ")), int(0)))");
   }
}

static void
texture_src_coord_swizzle(struct nir_to_msl_ctx *ctx, nir_src *coord,
                          uint32_t num_components, bool is_cube, bool is_array)
{
   src_to_msl(ctx, coord);

   uint32_t coord_components =
      num_components - (uint32_t)is_array - (uint32_t)is_cube;
   if (coord_components < coord->ssa->num_components) {
      const char *swizzle = "xyzw";
      uint32_t i = 0;
      P(ctx, ".");
      for (i = 0; i < coord_components; i++)
         P(ctx, "%c", swizzle[i]);

      if (is_cube) {
         P(ctx, ", ");
         round_src_component_to_uint(ctx, coord, swizzle[i++]);
      }
      if (is_array) {
         P(ctx, ", ");
         round_src_component_to_uint(ctx, coord, swizzle[i++]);
      }
   }
}

static void
image_coord_swizzle(struct nir_to_msl_ctx *ctx, nir_intrinsic_instr *instr)
{
   unsigned comps = 0;
   bool is_array = nir_intrinsic_image_array(instr);
   bool is_cube = false;
   switch (nir_intrinsic_image_dim(instr)) {
   case GLSL_SAMPLER_DIM_BUF:
   case GLSL_SAMPLER_DIM_1D:
      comps = 1;
      break;
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_MS:
      comps = 2;
      break;
   case GLSL_SAMPLER_DIM_3D:
      comps = 3;
      break;
   case GLSL_SAMPLER_DIM_CUBE:
      comps = 3;
      is_cube = true;
      break;
   default:
      assert(!"Bad dimension for image");
      break;
   }
   if (is_array)
      comps += 1;

   texture_src_coord_swizzle(ctx, &instr->src[1], comps, is_cube, is_array);
}

/* Non-packed types have stricter alignment requirements that packed types.
 * This helps us build a packed format for storage.
 */
static void
src_to_packed(struct nir_to_msl_ctx *ctx, nir_src *src, const char *type,
              uint32_t component_count)
{
   if (component_count == 1) {
      P(ctx, "%s(", type);
   } else {
      P(ctx, "packed_%s(", type);
   }
   src_to_msl(ctx, src);
   P(ctx, ")");
}

/* Non-packed types have stricter alignment requirements that packed types.
 * This helps us cast the pointer to a packed type and then it builds the
 * non-packed type for Metal usage.
 */
static void
src_to_packed_load(struct nir_to_msl_ctx *ctx, nir_src *src,
                   const char *addressing, const char *type,
                   uint32_t component_count)
{
   if (component_count == 1) {
      P(ctx, "*(%s %s*)(", addressing, type);
   } else {
      P(ctx, "%s(*(%s packed_%s*)", type, addressing, type);
   }
   src_to_msl(ctx, src);
   P(ctx, ")");
}

/* Non-packed types have stricter alignment requirements that packed types.
 * This helps us cast the pointer to a packed type and then it builds the
 * non-packed type for Metal usage.
 */
static void
src_to_packed_load_offset(struct nir_to_msl_ctx *ctx, nir_src *src,
                          nir_src *offset, const char *addressing,
                          const char *type, uint32_t component_count)
{
   if (component_count == 1) {
      P(ctx, "*(%s %s*)((", addressing, type);
   } else {
      P(ctx, "%s(*(%s packed_%s*)(", type, addressing, type);
   }
   src_to_msl(ctx, src);
   P(ctx, " + ");
   src_to_msl(ctx, offset);
   P(ctx, "))");
}

/* Non-packed types have stricter alignment requirements that packed types.
 * This helps us cast the pointer to a packed type for storage.
 */
static void
src_to_packed_store(struct nir_to_msl_ctx *ctx, nir_src *src,
                    const char *addressing, const char *type,
                    uint32_t num_components)
{
   if (num_components == 1) {
      P_IND(ctx, "*(%s %s*)", addressing, type);
   } else {
      P_IND(ctx, "*(%s packed_%s*)", addressing, type);
   }
   src_to_msl(ctx, src);
}

static const char *
atomic_op_to_msl(nir_atomic_op op)
{
   switch (op) {
   case nir_atomic_op_iadd:
   case nir_atomic_op_fadd:
      return "atomic_fetch_add";
   case nir_atomic_op_umin:
   case nir_atomic_op_imin:
   case nir_atomic_op_fmin:
      return "atomic_fetch_min";
   case nir_atomic_op_umax:
   case nir_atomic_op_imax:
   case nir_atomic_op_fmax:
      return "atomic_fetch_max";
   case nir_atomic_op_iand:
      return "atomic_fetch_and";
   case nir_atomic_op_ior:
      return "atomic_fetch_or";
   case nir_atomic_op_ixor:
      return "atomic_fetch_xor";
   case nir_atomic_op_xchg:
      return "atomic_exchange";
   case nir_atomic_op_cmpxchg:
   case nir_atomic_op_fcmpxchg:
      return "atomic_compare_exchange_weak";
   default:
      UNREACHABLE("Unhandled atomic op");
   }
}

static void
atomic_to_msl(struct nir_to_msl_ctx *ctx, nir_intrinsic_instr *instr,
              const char *scope, bool shared)
{
   const char *atomic_op = atomic_op_to_msl(nir_intrinsic_atomic_op(instr));
   const char *mem_order = "memory_order_relaxed";

   P(ctx, "%s_explicit((%s atomic_%s*)", atomic_op, scope,
     msl_type_for_def(ctx->types, &instr->def));
   if (shared)
      P(ctx, "&shared_data[");
   src_to_msl(ctx, &instr->src[0]);
   if (shared)
      P(ctx, "]");
   P(ctx, ", ");
   src_to_msl(ctx, &instr->src[1]);
   P(ctx, ", %s", mem_order);
   P(ctx, ");\n");
}

static void
atomic_swap_to_msl(struct nir_to_msl_ctx *ctx, nir_intrinsic_instr *instr,
                   const char *scope, bool shared)
{
   const char *atomic_op = atomic_op_to_msl(nir_intrinsic_atomic_op(instr));
   const char *mem_order = "memory_order_relaxed";
   const char *type = msl_type_for_def(ctx->types, &instr->def);

   P_IND(ctx, "%s ta%d = ", type, instr->def.index);
   src_to_msl(ctx, &instr->src[1]);
   P(ctx, "; %s_explicit((%s atomic_%s*)", atomic_op, scope, type);
   if (shared)
      P(ctx, "&shared_data[");
   src_to_msl(ctx, &instr->src[0]);
   if (shared)
      P(ctx, "]");
   P(ctx, ", ");
   P(ctx, "&ta%d, ", instr->def.index);
   src_to_msl(ctx, &instr->src[2]);
   P(ctx, ", %s, %s);", mem_order, mem_order);
   P(ctx, "%s t%d = ta%d;\n", type, instr->def.index, instr->def.index);
}

static void
memory_modes_to_msl(struct nir_to_msl_ctx *ctx, nir_variable_mode modes)
{
   bool requires_or = false;
   u_foreach_bit(i, modes) {
      nir_variable_mode single_mode = (1 << i);
      if (requires_or)
         P(ctx, " | ");
      switch (single_mode) {
      case nir_var_image:
         P(ctx, "mem_flags::mem_texture");
         break;
      case nir_var_mem_ssbo:
      case nir_var_mem_global:
         P(ctx, "mem_flags::mem_device");
         break;
      case nir_var_function_temp:
         P(ctx, "mem_flags::mem_none");
         break;
      case nir_var_mem_shared:
         P(ctx, "mem_flags::mem_threadgroup");
         break;
      default:
         UNREACHABLE("bad_memory_mode");
      }
      requires_or = true;
   }
   if (!requires_or)
      P(ctx, "mem_flags::mem_none");
}

static void
intrinsic_to_msl(struct nir_to_msl_ctx *ctx, nir_intrinsic_instr *instr)
{
   /* These instructions are only used to understand interpolation modes, they
    * don't generate any code. */
   if (instr->intrinsic == nir_intrinsic_load_barycentric_pixel ||
       instr->intrinsic == nir_intrinsic_load_barycentric_centroid ||
       instr->intrinsic == nir_intrinsic_load_barycentric_sample)
      return;

   const nir_intrinsic_info *info = &nir_intrinsic_infos[instr->intrinsic];
   if (instrinsic_needs_dest_type(instr)) {
      P_IND(ctx, "t%d = ", instr->def.index);
   }
   switch (instr->intrinsic) {
   case nir_intrinsic_decl_reg: {
      const char *reg_type = msl_uint_type(nir_intrinsic_bit_size(instr),
                                           nir_intrinsic_num_components(instr));
      P_IND(ctx, "%s r%d = %s(0);\n", reg_type, instr->def.index, reg_type);
   } break;
   case nir_intrinsic_load_reg:
      // register loads get inlined into the uses
      break;
   case nir_intrinsic_store_reg:
      P_IND(ctx, "r%d", instr->src[1].ssa->index);
      writemask_to_msl(ctx, nir_intrinsic_write_mask(instr),
                       instr->num_components);
      /* Registers don't store the component count, so get it from the value we
       * are assigning */
      if (instr->src[0].ssa->bit_size == 1u) {
         P(ctx, " = bool%s((", component_str(instr->num_components));
      } else if (nir_src_is_const(instr->src[0])) {
         /* Const vector types already build the type */
         if (instr->src[0].ssa->num_components > 1) {
            P(ctx, " = as_type<%s>((",
              msl_uint_type(instr->src[0].ssa->bit_size,
                            instr->src[0].ssa->num_components));
         } else {
            P(ctx, " = as_type<%s>(%s(",
              msl_uint_type(instr->src[0].ssa->bit_size,
                            instr->src[0].ssa->num_components),
              msl_type_for_src(ctx->types, &instr->src[0]));
         }
      } else {
         P(ctx, " = as_type<%s>((",
           msl_uint_type(instr->src[0].ssa->bit_size,
                         instr->src[0].ssa->num_components));
      }
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, "));\n");
      break;
   case nir_intrinsic_load_subgroup_size:
      P(ctx, "gl_SubGroupSize;\n");
      break;
   case nir_intrinsic_load_subgroup_invocation:
      P(ctx, "gl_SubGroupInvocation;\n");
      break;
   case nir_intrinsic_load_num_subgroups:
      P(ctx, "gl_NumSubGroups;\n");
      break;
   case nir_intrinsic_load_subgroup_id:
      P(ctx, "gl_SubGroupID;\n");
      break;
   case nir_intrinsic_load_workgroup_id:
      P(ctx, "gl_WorkGroupID;\n");
      break;
   case nir_intrinsic_load_local_invocation_id:
      P(ctx, "gl_LocalInvocationID;\n");
      break;
   case nir_intrinsic_load_global_invocation_id:
      P(ctx, "gl_GlobalInvocationID;\n");
      break;
   case nir_intrinsic_load_num_workgroups:
      P(ctx, "gl_NumWorkGroups;\n");
      break;
   case nir_intrinsic_load_local_invocation_index:
      P(ctx, "gl_LocalInvocationIndex;\n");
      break;
   case nir_intrinsic_load_frag_coord:
      P(ctx, "gl_FragCoord;\n");
      break;
   case nir_intrinsic_load_point_coord:
      P(ctx, "gl_PointCoord;\n");
      break;
   case nir_intrinsic_load_first_vertex:
      P(ctx, "gl_FirstVertex;\n");
      break;
   case nir_intrinsic_load_vertex_id:
      P(ctx, "gl_VertexID;\n");
      break;
   case nir_intrinsic_load_instance_id:
      P(ctx, "gl_InstanceID;\n");
      break;
   case nir_intrinsic_load_base_instance:
      P(ctx, "gl_BaseInstance;\n");
      break;
   case nir_intrinsic_load_helper_invocation:
      P(ctx, "simd_is_helper_thread();\n");
      break;
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddx_fine:
      P(ctx, "dfdx(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_coarse:
   case nir_intrinsic_ddy_fine:
      P(ctx, "dfdy(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_load_front_face:
      P(ctx, "gl_FrontFacing;\n");
      break;
   case nir_intrinsic_load_layer_id:
      P(ctx, "gl_Layer;\n");
      break;
   case nir_intrinsic_load_sample_id:
      P(ctx, "gl_SampleID;\n");
      break;
   case nir_intrinsic_load_sample_mask_in:
      P(ctx, "gl_SampleMask;\n");
      break;
   case nir_intrinsic_load_amplification_id_kk:
      P(ctx, "mtl_AmplificationID;\n");
      break;
   case nir_intrinsic_load_interpolated_input: {
      unsigned idx = nir_src_as_uint(instr->src[1u]);
      nir_io_semantics io = nir_intrinsic_io_semantics(instr);
      uint32_t component = nir_intrinsic_component(instr);
      uint32_t location = io.location + idx;

      msl_input_name(ctx, location, component);
      if (instr->num_components < msl_input_num_components(ctx, location)) {
         P(ctx, ".");
         for (unsigned i = 0; i < instr->num_components; i++)
            P(ctx, "%c", "xyzw"[component + i]);
      }
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_load_input: {
      unsigned idx = nir_src_as_uint(instr->src[0u]);
      nir_io_semantics io = nir_intrinsic_io_semantics(instr);
      uint32_t component = nir_intrinsic_component(instr);
      uint32_t location = io.location + idx;

      msl_input_name(ctx, location, component);
      if (instr->num_components < msl_input_num_components(ctx, location)) {
         P(ctx, ".");
         for (unsigned i = 0; i < instr->num_components; i++)
            P(ctx, "%c", "xyzw"[component + i]);
      }
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_load_output: {
      unsigned idx = nir_src_as_uint(instr->src[0]);
      nir_io_semantics io = nir_intrinsic_io_semantics(instr);
      msl_output_name(ctx, io.location + idx, 0);
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_store_output: {
      uint32_t idx = nir_src_as_uint(instr->src[1]);
      nir_io_semantics io = nir_intrinsic_io_semantics(instr);
      uint32_t location = io.location + idx;
      uint32_t write_mask = nir_intrinsic_write_mask(instr);
      uint32_t component = nir_intrinsic_component(instr);
      uint32_t dst_num_components = msl_output_num_components(ctx, location);
      uint32_t num_components = instr->num_components;

      P_IND(ctx, "%s", "");
      msl_output_name(ctx, location, component);
      if (dst_num_components > 1u) {
         P(ctx, ".");
         for (unsigned i = 0; i < num_components; i++)
            if ((write_mask >> i) & 1)
               P(ctx, "%c", "xyzw"[component + i]);
      }
      P(ctx, " = ");
      src_to_msl(ctx, &instr->src[0]);
      if (num_components > 1u) {
         P(ctx, ".");
         for (unsigned i = 0; i < num_components; i++)
            if ((write_mask >> i) & 1)
               P(ctx, "%c", "xyzw"[i]);
      }
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_load_push_constant: {
      const char *ty = msl_type_for_def(ctx->types, &instr->def);
      assert(nir_intrinsic_base(instr) == 0);
      P(ctx, "*((constant %s*)&buf.push_consts[", ty);
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, "]);\n");
      break;
   }
   case nir_intrinsic_load_buffer_ptr_kk:
      P(ctx, "(ulong)&buf%d.contents[0];\n", nir_intrinsic_binding(instr));
      break;
   case nir_intrinsic_load_global: {
      enum gl_access_qualifier access = nir_intrinsic_access(instr);
      const char *type = msl_type_for_def(ctx->types, &instr->def);
      const char *addressing =
         access & ACCESS_COHERENT ? "coherent device" : "device";
      if (access & ACCESS_ATOMIC) {
         assert(instr->num_components == 1u &&
                "We can only do single component with atomics");
         P(ctx, "atomic_load_explicit((%s atomic_%s*)", addressing, type);
         src_to_msl(ctx, &instr->src[0]);
         P(ctx, ", memory_order_relaxed);\n");
      } else {
         src_to_packed_load(ctx, &instr->src[0], addressing, type,
                            instr->def.num_components);
         P(ctx, ";\n");
      }
      break;
   }
   case nir_intrinsic_load_global_constant: {
      src_to_packed_load(ctx, &instr->src[0], "constant",
                         msl_type_for_def(ctx->types, &instr->def),
                         instr->def.num_components);
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_load_global_constant_bounded: {
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, " < ");
      src_to_msl(ctx, &instr->src[2]);
      P(ctx, " ? ");
      src_to_packed_load_offset(ctx, &instr->src[0], &instr->src[1], "constant",
                                msl_type_for_def(ctx->types, &instr->def),
                                instr->def.num_components);
      P(ctx, " : 0;\n");
      break;
   }
   case nir_intrinsic_load_global_constant_offset: {
      enum gl_access_qualifier access = nir_intrinsic_access(instr);
      const char *type = msl_type_for_def(ctx->types, &instr->def);
      const char *addressing =
         access & ACCESS_COHERENT ? "coherent device" : "device";
      if (access & ACCESS_ATOMIC) {
         assert(instr->num_components == 1u &&
                "We can only do single component with atomics");
         P(ctx, "atomic_load_explicit((%s atomic_%s*)(", addressing, type);
         src_to_msl(ctx, &instr->src[0]);
         P(ctx, "+");
         src_to_msl(ctx, &instr->src[1]);
         P(ctx, ", memory_order_relaxed);\n");
      } else {
         src_to_packed_load_offset(ctx, &instr->src[0], &instr->src[1],
                                   addressing,
                                   msl_type_for_def(ctx->types, &instr->def),
                                   instr->def.num_components);
      }
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_global_atomic:
      atomic_to_msl(ctx, instr, "device", false);
      break;
   case nir_intrinsic_global_atomic_swap:
      atomic_swap_to_msl(ctx, instr, "device", false);
      break;
   case nir_intrinsic_shared_atomic:
      atomic_to_msl(ctx, instr, "threadgroup", true);
      break;
   case nir_intrinsic_shared_atomic_swap:
      atomic_swap_to_msl(ctx, instr, "threadgroup", true);
      break;
   case nir_intrinsic_store_global: {
      enum gl_access_qualifier access = nir_intrinsic_access(instr);
      const char *type = msl_type_for_src(ctx->types, &instr->src[0]);
      const char *addressing =
         access & ACCESS_COHERENT ? "coherent device" : "device";
      if (access & ACCESS_ATOMIC) {
         assert(instr->num_components == 1u &&
                "We can only do single component with atomics");
         P_IND(ctx, "atomic_store_explicit((%s atomic_%s*)", addressing, type);
         src_to_msl(ctx, &instr->src[1]);
         P(ctx, ", ")
         src_to_packed(ctx, &instr->src[0], type,
                       instr->src[0].ssa->num_components);
         P(ctx, ", memory_order_relaxed);\n");
      } else {
         src_to_packed_store(ctx, &instr->src[1], addressing, type,
                             instr->src[0].ssa->num_components);
         writemask_to_msl(ctx, nir_intrinsic_write_mask(instr),
                          instr->num_components);
         P(ctx, " = ")
         src_to_packed(ctx, &instr->src[0], type,
                       instr->src[0].ssa->num_components);
         P(ctx, ";\n");
      }
      break;
   }
   case nir_intrinsic_barrier: {
      mesa_scope execution_scope = nir_intrinsic_execution_scope(instr);
      nir_variable_mode memory_modes = nir_intrinsic_memory_modes(instr);
      if (execution_scope == SCOPE_SUBGROUP) {
         P_IND(ctx, "simdgroup_barrier(");
         memory_modes_to_msl(ctx, memory_modes);
      } else if (execution_scope == SCOPE_WORKGROUP) {
         P_IND(ctx, "threadgroup_barrier(");
         memory_modes_to_msl(ctx, memory_modes);
      } else if (execution_scope == SCOPE_NONE) {
         /* Empty barrier */
         if (memory_modes == 0u)
            break;

         P_IND(ctx, "atomic_thread_fence(");
         memory_modes_to_msl(ctx, memory_modes);
         P(ctx, ", memory_order_seq_cst, ");
         switch (nir_intrinsic_memory_scope(instr)) {
         case SCOPE_SUBGROUP:
            P(ctx, "thread_scope::thread_scope_simdgroup");
            break;
         case SCOPE_WORKGROUP:
            /* TODO_KOSMICKRISP This if case should not be needed but we fail
             * the following CTS tests otherwise:
             * dEQP-VK.memory_model.*.ext.u32.*coherent.*.atomicwrite.workgroup.payload_*local.*.guard_local.*.comp
             * The last two wild cards being either 'workgroup' or 'physbuffer'
             */
            if (memory_modes &
                (nir_var_mem_global | nir_var_mem_ssbo | nir_var_image)) {
               P(ctx, "thread_scope::thread_scope_device");
            } else {
               P(ctx, "thread_scope::thread_scope_threadgroup");
            }

            break;
         case SCOPE_QUEUE_FAMILY:
         case SCOPE_DEVICE:
            P(ctx, "thread_scope::thread_scope_device");
            break;
         default:
            P(ctx, "bad_scope");
            assert(!"bad scope");
            break;
         }
      } else {
         UNREACHABLE("bad_execution scope");
      }
      P(ctx, ");\n");
      break;
   }
   case nir_intrinsic_demote:
      P_IND(ctx, "discard_fragment();\n");
      break;
   case nir_intrinsic_demote_if:
      P_IND(ctx, "if (")
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ")\n");
      ctx->indentlevel++;
      P_IND(ctx, "discard_fragment();\n");
      ctx->indentlevel--;
      break;
   case nir_intrinsic_terminate:
      P_IND(ctx, "discard_fragment();\n");
      P_IND(ctx, "return {};\n");
      break;
   case nir_intrinsic_terminate_if:
      P_IND(ctx, "if (")
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ") {\n");
      ctx->indentlevel++;
      P_IND(ctx, "discard_fragment();\n");
      P_IND(ctx, "return {};\n");
      ctx->indentlevel--;
      P_IND(ctx, "}\n");
      break;
   case nir_intrinsic_load_shared:
      assert(nir_intrinsic_base(instr) == 0);
      P(ctx, "*(threadgroup %s*)&shared_data[",
        msl_type_for_def(ctx->types, &instr->def));
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, "];\n");
      break;
   case nir_intrinsic_store_shared:
      assert(nir_intrinsic_base(instr) == 0);
      P_IND(ctx, "(*(threadgroup %s*)&shared_data[",
            msl_type_for_src(ctx->types, &instr->src[0]));
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, "])");
      writemask_to_msl(ctx, nir_intrinsic_write_mask(instr),
                       instr->num_components);
      P(ctx, " = ");
      src_to_msl(ctx, &instr->src[0]);
      if (instr->src[0].ssa->num_components > 1)
         writemask_to_msl(ctx, nir_intrinsic_write_mask(instr),
                          instr->num_components);
      P(ctx, ";\n");
      break;
   case nir_intrinsic_load_scratch:
      P(ctx, "*(thread %s*)&scratch[",
        msl_type_for_def(ctx->types, &instr->def));
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, "];\n");
      break;
   case nir_intrinsic_store_scratch:
      P_IND(ctx, "(*(thread %s*)&scratch[",
            msl_type_for_src(ctx->types, &instr->src[0]));
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, "])");
      writemask_to_msl(ctx, nir_intrinsic_write_mask(instr),
                       instr->num_components);
      P(ctx, " = ");
      src_to_msl(ctx, &instr->src[0]);
      if (instr->src[0].ssa->num_components > 1)
         writemask_to_msl(ctx, nir_intrinsic_write_mask(instr),
                          instr->num_components);
      P(ctx, ";\n");
      break;
   case nir_intrinsic_load_texture_handle_kk: {
      const char *access = "";
      const char *coherent = nir_intrinsic_access(instr) & ACCESS_COHERENT
                                ? ", memory_coherence_device"
                                : "";
      switch (nir_intrinsic_flags(instr)) {
      case MSL_ACCESS_READ:
         access = ", access::read";
         break;
      case MSL_ACCESS_WRITE:
         access = ", access::write";
         break;
      case MSL_ACCESS_READ_WRITE:
         access = ", access::read_write";
         /* TODO_KOSMICKRISP We shouldn't need this line below but it doesn't
          * seem we get the correct access values for what should be device
          * coherent textures from NIR. Example test:
          * dEQP-VK.memory_model.message_passing.ext.u32.coherent.fence_fence.atomicwrite.device.payload_local.image.guard_local.buffer.comp
          * This test declares the texture as devicecoherent, but in NIR it
          * appears as resctrict only with no coherent.
          */
         coherent = ", memory_coherence_device";
         break;
      }
      P_IND(ctx, "texture%s%s<%s%s%s> t%d = *(constant texture%s%s<%s%s%s>*)",
            texture_dim(nir_intrinsic_image_dim(instr)),
            nir_intrinsic_image_array(instr) ? "_array" : "",
            tex_type_name(nir_intrinsic_dest_type(instr)), access, coherent,
            instr->def.index, texture_dim(nir_intrinsic_image_dim(instr)),
            nir_intrinsic_image_array(instr) ? "_array" : "",
            tex_type_name(nir_intrinsic_dest_type(instr)), access, coherent);
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ";\n");
      break;
   }
   case nir_intrinsic_load_depth_texture_kk:
      P_IND(ctx, "depth%s%s<float> t%d = *(constant depth%s%s<float>*)",
            texture_dim(nir_intrinsic_image_dim(instr)),
            nir_intrinsic_image_array(instr) ? "_array" : "", instr->def.index,
            texture_dim(nir_intrinsic_image_dim(instr)),
            nir_intrinsic_image_array(instr) ? "_array" : "");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ";\n");
      break;
   case nir_intrinsic_load_sampler_handle_kk:
      P(ctx, "sampler_table.handles[");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, "];\n");
      break;
   case nir_intrinsic_load_constant_agx: {
      const char *type = msl_type_for_def(ctx->types, &instr->def);
      const char *no_component_type =
         msl_pipe_format_to_msl_type(nir_intrinsic_format(instr));
      if (instr->def.num_components == 1) {
         P(ctx, "(*(((constant %s*)", type);
      } else {
         P(ctx, "%s(*(constant packed_%s*)(((constant %s*)", type, type,
           no_component_type);
      }
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ") + ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, "));\n");
      break;
   }
   case nir_intrinsic_bindless_image_load:
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ".read(");
      image_coord_swizzle(ctx, instr);
      if (nir_intrinsic_image_dim(instr) != GLSL_SAMPLER_DIM_BUF) {
         P(ctx, ", ");
         src_to_msl(ctx, &instr->src[3]);
      }
      /* read will always return vec4 and we may try to assign that to an uint
       * which is illegal. */
      P(ctx, ").");
      for (uint32_t i = 0u; i < instr->def.num_components; ++i) {
         P(ctx, "%c", "xyzw"[i]);
      }
      P(ctx, ";\n");
      break;
   case nir_intrinsic_bindless_image_store:
      P_INDENT(ctx);
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ".write(");
      src_to_msl(ctx, &instr->src[3]);
      P(ctx, ", ");
      image_coord_swizzle(ctx, instr);
      if (nir_intrinsic_image_dim(instr) != GLSL_SAMPLER_DIM_BUF) {
         P(ctx, ", ");
         src_to_msl(ctx, &instr->src[4]);
      }
      P(ctx, ");\n");
      break;
   case nir_intrinsic_bindless_image_atomic:
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ".%s(", atomic_op_to_msl(nir_intrinsic_atomic_op(instr)));
      image_coord_swizzle(ctx, instr);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[3]);
      P(ctx, ").x;\n");
      break;
   case nir_intrinsic_bindless_image_atomic_swap: {
      const char *type = msl_type_for_def(ctx->types, &instr->def);
      P_IND(ctx, "%s4 ta%d = ", type, instr->def.index);
      src_to_msl(ctx, &instr->src[3]);
      P(ctx, "; ");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ".%s(", atomic_op_to_msl(nir_intrinsic_atomic_op(instr)));
      image_coord_swizzle(ctx, instr);
      P(ctx, ", &ta%d, ", instr->def.index);
      src_to_msl(ctx, &instr->src[4]);
      P(ctx, "); %s t%d = ta%d.x;\n", type, instr->def.index, instr->def.index);
      break;
   }
   case nir_intrinsic_ballot:
      P(ctx, "(ulong)simd_ballot(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_elect:
      /* KK_WORKAROUND_3 */
      if (ctx->disabled_workarounds & BITFIELD64_BIT(3)) {
         P(ctx, "simd_is_first();\n");
      } else {
         P(ctx, "simd_is_first() && (ulong)simd_ballot(true);\n");
      }
      break;
   case nir_intrinsic_read_first_invocation:
      P(ctx, "simd_broadcast_first(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_read_invocation:
      P(ctx, "simd_broadcast(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, ");");
      break;
   case nir_intrinsic_shuffle:
      P(ctx, "simd_shuffle(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_shuffle_xor:
      P(ctx, "simd_shuffle_xor(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_shuffle_up:
      P(ctx, "simd_shuffle_up(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_shuffle_down:
      P(ctx, "simd_shuffle_down(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, ");\n");
      break;

   case nir_intrinsic_vote_all:
      P(ctx, "simd_all(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_vote_any:
      P(ctx, "simd_any(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_quad_broadcast:
      P(ctx, "quad_broadcast(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", ");
      src_to_msl(ctx, &instr->src[1]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_quad_swap_horizontal:
      P(ctx, "quad_shuffle_xor(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", 1);\n");
      break;
   case nir_intrinsic_quad_swap_vertical:
      P(ctx, "quad_shuffle_xor(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", 2);\n");
      break;
   case nir_intrinsic_quad_swap_diagonal:
      P(ctx, "quad_shuffle_xor(");
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ", 3);\n");
      break;
   case nir_intrinsic_reduce:
      switch (nir_intrinsic_reduction_op(instr)) {
      case nir_op_iadd:
      case nir_op_fadd:
         P(ctx, "simd_sum(");
         break;
      case nir_op_imul:
      case nir_op_fmul:
         P(ctx, "simd_product(");
         break;
      case nir_op_imin:
      case nir_op_umin:
      case nir_op_fmin:
         P(ctx, "simd_min(");
         break;
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_fmax:
         P(ctx, "simd_max(");
         break;
      case nir_op_iand:
         P(ctx, "simd_and(");
         break;
      case nir_op_ior:
         P(ctx, "simd_or(");
         break;
      case nir_op_ixor:
         P(ctx, "simd_xor(");
         break;
      default:
         UNREACHABLE("Bad reduction op");
      }

      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ");\n");
      break;
   case nir_intrinsic_store_clip_distance_kk:
      P_IND(ctx, "out.gl_ClipDistance[%d] = ", nir_intrinsic_base(instr));
      src_to_msl(ctx, &instr->src[0]);
      P(ctx, ";\n");
      break;
   default:
      P_IND(ctx, "Unknown intrinsic %s\n", info->name);
   }
}

static nir_src *
nir_tex_get_src(struct nir_tex_instr *tex, nir_tex_src_type type)
{
   int idx = nir_tex_instr_src_index(tex, type);
   if (idx == -1)
      return NULL;
   return &tex->src[idx].src;
}

static void
tex_coord_swizzle(struct nir_to_msl_ctx *ctx, nir_tex_instr *tex)
{
   texture_src_coord_swizzle(ctx, nir_tex_get_src(tex, nir_tex_src_coord),
                             tex->coord_components, false, tex->is_array);
}

static void
tex_to_msl(struct nir_to_msl_ctx *ctx, nir_tex_instr *tex)
{
   nir_src *texhandle = nir_tex_get_src(tex, nir_tex_src_texture_handle);
   nir_src *sampler = nir_tex_get_src(tex, nir_tex_src_sampler_handle);
   // Projectors have to be lowered away to regular arithmetic
   assert(!nir_tex_get_src(tex, nir_tex_src_projector));

   P_IND(ctx, "t%d = ", tex->def.index);

   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd: {
      nir_src *bias = nir_tex_get_src(tex, nir_tex_src_bias);
      nir_src *lod = nir_tex_get_src(tex, nir_tex_src_lod);
      nir_src *ddx = nir_tex_get_src(tex, nir_tex_src_ddx);
      nir_src *ddy = nir_tex_get_src(tex, nir_tex_src_ddy);
      nir_src *min_lod_clamp = nir_tex_get_src(tex, nir_tex_src_min_lod);
      nir_src *offset = nir_tex_get_src(tex, nir_tex_src_offset);
      nir_src *comparator = nir_tex_get_src(tex, nir_tex_src_comparator);
      src_to_msl(ctx, texhandle);
      if (comparator) {
         P(ctx, ".sample_compare(");
      } else {
         P(ctx, ".sample(");
      }
      src_to_msl(ctx, sampler);
      P(ctx, ", ");
      tex_coord_swizzle(ctx, tex);
      if (comparator) {
         P(ctx, ", ");
         src_to_msl(ctx, comparator);
      }
      if (bias) {
         P(ctx, ", bias(");
         src_to_msl(ctx, bias);
         P(ctx, ")");
      }
      if (lod) {
         P(ctx, ", level(");
         src_to_msl(ctx, lod);
         P(ctx, ")");
      }
      if (ddx) {
         P(ctx, ", gradient%s(", texture_dim(tex->sampler_dim));
         src_to_msl(ctx, ddx);
         P(ctx, ", ");
         src_to_msl(ctx, ddy);
         P(ctx, ")");
      }
      if (min_lod_clamp) {
         P(ctx, ", min_lod_clamp(");
         src_to_msl(ctx, min_lod_clamp);
         P(ctx, ")");
      }
      if (offset) {
         P(ctx, ", ");
         src_to_msl(ctx, offset);
      }
      P(ctx, ");\n");
      break;
   }
   case nir_texop_txf: {
      src_to_msl(ctx, texhandle);
      P(ctx, ".read(");
      tex_coord_swizzle(ctx, tex);
      nir_src *lod = nir_tex_get_src(tex, nir_tex_src_lod);
      if (lod) {
         P(ctx, ", ");
         src_to_msl(ctx, lod);
      }
      P(ctx, ");\n");
      break;
   }
   case nir_texop_txf_ms:
      src_to_msl(ctx, texhandle);
      P(ctx, ".read(");
      tex_coord_swizzle(ctx, tex);
      P(ctx, ", ");
      src_to_msl(ctx, nir_tex_get_src(tex, nir_tex_src_ms_index));
      P(ctx, ");\n");
      break;
   case nir_texop_txs: {
      nir_src *lod = nir_tex_get_src(tex, nir_tex_src_lod);
      if (tex->def.num_components > 1u) {
         P(ctx, "%s%d(", tex_type_name(tex->dest_type),
           tex->def.num_components);
      } else {
         P(ctx, "%s(", tex_type_name(tex->dest_type));
      }
      src_to_msl(ctx, texhandle);
      P(ctx, ".get_width(")
      if (lod && tex->sampler_dim != GLSL_SAMPLER_DIM_MS &&
          tex->sampler_dim != GLSL_SAMPLER_DIM_BUF)
         src_to_msl(ctx, lod);
      P(ctx, ")");
      if (tex->sampler_dim != GLSL_SAMPLER_DIM_1D &&
          tex->sampler_dim != GLSL_SAMPLER_DIM_BUF) {
         P(ctx, ", ");
         src_to_msl(ctx, texhandle);
         P(ctx, ".get_height(");
         if (lod && tex->sampler_dim != GLSL_SAMPLER_DIM_MS &&
             tex->sampler_dim != GLSL_SAMPLER_DIM_BUF)
            src_to_msl(ctx, lod);
         P(ctx, ")");
      }
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_3D) {
         P(ctx, ", ");
         src_to_msl(ctx, texhandle);
         P(ctx, ".get_depth(");
         if (lod)
            src_to_msl(ctx, lod);
         P(ctx, ")");
      }
      if (tex->is_array) {
         P(ctx, ", ");
         src_to_msl(ctx, texhandle);
         P(ctx, ".get_array_size()");
      }
      P(ctx, ");\n")
      break;
   }
   case nir_texop_query_levels:
      src_to_msl(ctx, texhandle);
      P(ctx, ".get_num_mip_levels();\n");
      break;
   case nir_texop_tg4: {
      nir_src *offset = nir_tex_get_src(tex, nir_tex_src_offset);
      nir_src *comparator = nir_tex_get_src(tex, nir_tex_src_comparator);
      src_to_msl(ctx, texhandle);
      if (comparator) {
         P(ctx, ".gather_compare(");
      } else {
         P(ctx, ".gather(");
      }
      src_to_msl(ctx, sampler);
      P(ctx, ", ");
      tex_coord_swizzle(ctx, tex);
      if (comparator) {
         P(ctx, ", ");
         src_to_msl(ctx, comparator);
      }

      /* SPIR-V does not allow offsets for cube samplers (neither does MSL).
       * nir_opt_constant_folding will remove offsets that are 0, so we need to
       * add them here. */
      if (offset) {
         P(ctx, ", ");
         src_to_msl(ctx, offset);
      } else if (tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE) {
         P(ctx, ", int2(0)");
      }

      /* Non-depth textures require component */
      if (!comparator) {
         P(ctx, ", component::%c", "xyzw"[tex->component]);
      }

      P(ctx, ");\n");
      break;
   }

   case nir_texop_texture_samples:
      src_to_msl(ctx, texhandle);
      P(ctx, ".get_num_samples();\n");
      break;
   case nir_texop_lod: {
      nir_src *coord = nir_tex_get_src(tex, nir_tex_src_coord);
      nir_src *bias = nir_tex_get_src(tex, nir_tex_src_bias);
      nir_src *min = nir_tex_get_src(tex, nir_tex_src_min_lod);
      nir_src *max = nir_tex_get_src(tex, nir_tex_src_max_lod_kk);
      P(ctx, "float2(round(clamp(")
      src_to_msl(ctx, texhandle);
      P(ctx, ".calculate_unclamped_lod(");
      src_to_msl(ctx, sampler);
      P(ctx, ", ");
      src_to_msl(ctx, coord);
      P(ctx, ") + ");
      src_to_msl(ctx, bias);
      P(ctx, ", ");
      src_to_msl(ctx, min);
      P(ctx, ", ");
      src_to_msl(ctx, max);
      P(ctx, ")), ");
      src_to_msl(ctx, texhandle);
      P(ctx, ".calculate_unclamped_lod(");
      src_to_msl(ctx, sampler);
      P(ctx, ", ");
      src_to_msl(ctx, coord);
      P(ctx, ")");
      P(ctx, ");\n");
      break;
   }
   default:
      assert(!"Unsupported texture op");
   }
}

static void
jump_instr_to_msl(struct nir_to_msl_ctx *ctx, nir_jump_instr *jump)
{
   switch (jump->type) {
   case nir_jump_halt:
      P_IND(ctx, "TODO: halt\n");
      assert(!"Unimplemented");
      break;
   case nir_jump_break:
      P_IND(ctx, "break;\n");
      break;
   case nir_jump_continue:
      P_IND(ctx, "continue;\n");
      break;
   case nir_jump_return:
      assert(!"functions should have been inlined by now");
      break;
   case nir_jump_goto:
   case nir_jump_goto_if:
      assert(!"Unstructured control flow not supported");
      break;
   }
}

static void
instr_to_msl(struct nir_to_msl_ctx *ctx, nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      P_IND(ctx, "t%d = ", alu->def.index);
      alu_to_msl(ctx, alu);
      P(ctx, ";\n");
      break;
   }
   case nir_instr_type_deref:
      assert(!"We should have lowered derefs by now");
      break;
   case nir_instr_type_call:
   case nir_instr_type_cmat_call:
      assert(!"We should have inlined all functions by now");
      break;
   case nir_instr_type_tex:
      tex_to_msl(ctx, nir_instr_as_tex(instr));
      break;
   case nir_instr_type_intrinsic:
      intrinsic_to_msl(ctx, nir_instr_as_intrinsic(instr));
      break;
   case nir_instr_type_load_const:
      // consts get inlined into their uses
      break;
   case nir_instr_type_jump:
      jump_instr_to_msl(ctx, nir_instr_as_jump(instr));
      break;
   case nir_instr_type_undef:
      // undefs get inlined into their uses (and we shouldn't see them hopefully)
      break;
   case nir_instr_type_phi:
      assert(!"NIR should be taken out of SSA");
      break;
   }
}

static void
cf_node_to_metal(struct nir_to_msl_ctx *ctx, nir_cf_node *node)
{
   switch (node->type) {
   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(node);
      nir_foreach_instr(instr, block) {
         instr_to_msl(ctx, instr);
      }
      break;
   }
   case nir_cf_node_if: {
      nir_if *ifnode = nir_cf_node_as_if(node);
      P_IND(ctx, "if (");
      src_to_msl(ctx, &ifnode->condition);
      P(ctx, ") {\n");
      ctx->indentlevel++;
      foreach_list_typed(nir_cf_node, node, node, &ifnode->then_list) {
         cf_node_to_metal(ctx, node);
      }
      ctx->indentlevel--;
      if (!nir_cf_list_is_empty_block(&ifnode->else_list)) {
         P_IND(ctx, "} else {\n");
         ctx->indentlevel++;
         foreach_list_typed(nir_cf_node, node, node, &ifnode->else_list) {
            cf_node_to_metal(ctx, node);
         }
         ctx->indentlevel--;
      }
      P_IND(ctx, "}\n");
      break;
   }
   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(node);
      assert(!nir_loop_has_continue_construct(loop));
      /* KK_WORKAROUND_2 */
      if (ctx->disabled_workarounds & BITFIELD64_BIT(2)) {
         P_IND(ctx, "while (true) {\n");
      } else {
         P_IND(ctx,
               "for (uint64_t no_crash = 0u; no_crash < %" PRIu64
               "; ++no_crash) {\n",
               UINT64_MAX);
      }
      ctx->indentlevel++;
      foreach_list_typed(nir_cf_node, node, node, &loop->body) {
         cf_node_to_metal(ctx, node);
      }
      ctx->indentlevel--;
      P_IND(ctx, "}\n");
      break;
   }
   case nir_cf_node_function:
      assert(!"All functions are supposed to be inlined");
   }
}

static void
emit_output_return(struct nir_to_msl_ctx *ctx, nir_shader *shader)
{
   if (shader->info.stage == MESA_SHADER_VERTEX ||
       shader->info.stage == MESA_SHADER_FRAGMENT)
      P_IND(ctx, "return out;\n");
}

static void
rename_main_entrypoint(struct nir_shader *nir)
{
   /* Rename entrypoint to avoid MSL limitations after we've removed all others.
    * We don't really care what it's named as long as it's not "main"
    */
   const char *entrypoint_name = "main_entrypoint";
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
   struct nir_function *function = entrypoint->function;
   ralloc_free((void *)function->name);
   function->name = ralloc_strdup(function, entrypoint_name);
}

static bool
kk_scalarize_filter(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_alu)
      return false;
   return true;
}

void
msl_preprocess_nir(struct nir_shader *nir)
{
   /* First, inline away all the functions */
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   NIR_PASS(_, nir, nir_opt_deref);
   nir_remove_non_entrypoints(nir);

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_struct_vars, nir_var_function_temp);
   NIR_PASS(_, nir, nir_split_array_vars, nir_var_function_temp);
   NIR_PASS(_, nir, nir_split_per_member_structs);
   NIR_PASS(_, nir, nir_lower_continue_constructs);

   NIR_PASS(_, nir, nir_lower_frexp);

   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);

   /* lower_system_values needs to go before is_helper_invocation since it will
    * generate discards. */
   NIR_PASS(_, nir, nir_lower_system_values);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      nir_input_attachment_options input_attachment_options = {};
      NIR_PASS(_, nir, nir_lower_input_attachments, &input_attachment_options);
   }
   NIR_PASS(_, nir, nir_opt_combine_barriers, NULL, NULL);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_split_var_copies);

   NIR_PASS(_, nir, nir_split_array_vars,
            nir_var_function_temp | nir_var_shader_in | nir_var_shader_out);
   NIR_PASS(_, nir, nir_lower_alu_to_scalar, kk_scalarize_filter, NULL);

   NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_shader_in | nir_var_shader_out, UINT32_MAX);
   NIR_PASS(_, nir, nir_lower_vars_to_scratch, nir_var_function_temp, 0,
            glsl_get_natural_size_align_bytes,
            glsl_get_natural_size_align_bytes);

   nir_lower_compute_system_values_options csv_options = {
      .has_base_global_invocation_id = 0,
      .has_base_workgroup_id = true,
   };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &csv_options);

   msl_nir_lower_subgroups(nir);
}

bool
msl_optimize_nir(struct nir_shader *nir)
{
   bool progress;
   NIR_PASS(_, nir, nir_lower_int64);
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_split_var_copies);
      NIR_PASS(progress, nir, nir_split_struct_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_lower_var_copies);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);
      NIR_PASS(progress, nir, nir_opt_combine_stores, nir_var_all);
      NIR_PASS(progress, nir, nir_remove_dead_variables, nir_var_function_temp,
               NULL);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_if, 0);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_loop);
      NIR_PASS(progress, nir, nir_lower_pack);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, kk_scalarize_filter,
               NULL);
   } while (progress);
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, msl_nir_lower_algebraic_late);
   NIR_PASS(_, nir, nir_convert_from_ssa, true, false);
   nir_trivialize_registers(nir);
   NIR_PASS(_, nir, nir_opt_copy_prop);

   return progress;
}

/* Scalarize stores to CLIP_DIST* varyings */
static bool
scalarize_clip_distance_filter(const nir_intrinsic_instr *intrin,
                               UNUSED const void *_data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;
   nir_io_semantics semantics = nir_intrinsic_io_semantics(intrin);
   return semantics.location == VARYING_SLOT_CLIP_DIST0 ||
          semantics.location == VARYING_SLOT_CLIP_DIST1;
}

void
msl_lower_nir_late(nir_shader *nir)
{
   NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out,
            scalarize_clip_distance_filter, NULL);
   NIR_PASS(_, nir, msl_nir_lower_clip_distance);
}

static void
msl_gather_info(struct nir_to_msl_ctx *ctx)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(ctx->shader);
   ctx->types = msl_infer_types(ctx->shader);

   /* TODO_KOSMICKRISP
    * Reindex blocks and ssa. This allows us to optimize things we don't at the
    * moment. */
   nir_index_blocks(impl);
   nir_index_ssa_defs(impl);

   if (ctx->shader->info.stage == MESA_SHADER_VERTEX ||
       ctx->shader->info.stage == MESA_SHADER_FRAGMENT) {
      msl_gather_io_info(ctx, ctx->inputs_info, ctx->outputs_info);
   }
}

static void
predeclare_ssa_values(struct nir_to_msl_ctx *ctx, nir_function_impl *impl)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_def *def;
         switch (instr->type) {
         case nir_instr_type_alu: {
            nir_alu_instr *alu = nir_instr_as_alu(instr);
            def = &alu->def;
            break;
         }
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (!instrinsic_needs_dest_type(intr))
               continue;
            def = &intr->def;
            break;
         }
         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            def = &tex->def;
            break;
         }
         default:
            continue;
         }
         const char *type = msl_type_for_def(ctx->types, def);
         if (!type)
            continue;
         if (msl_def_is_sampler(ctx, def)) {
            P_IND(ctx, "%s t%u;\n", type, def->index);
         } else
            P_IND(ctx, "%s t%u = %s(0);\n", type, def->index, type);
      }
   }
}

char *
nir_to_msl(nir_shader *shader, void *mem_ctx, uint64_t disabled_workarounds)
{
   /* Need to rename the entrypoint here since hardcoded shaders used by vk_meta
    * don't go through the preprocess step since we are the ones creating them.
    */
   rename_main_entrypoint(shader);

   struct nir_to_msl_ctx ctx = {
      .shader = shader,
      .text = _mesa_string_buffer_create(mem_ctx, 1024),
      .disabled_workarounds = disabled_workarounds,
   };
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   msl_gather_info(&ctx);

   P(&ctx, "// Generated by Mesa compiler\n");
   if (shader->info.stage == MESA_SHADER_COMPUTE)
      P(&ctx, "#include <metal_compute>\n");
   P(&ctx, "#include <metal_stdlib>\n");
   P(&ctx, "using namespace metal;\n");

   msl_emit_io_blocks(&ctx, shader);
   if (shader->info.stage == MESA_SHADER_FRAGMENT &&
       shader->info.fs.early_fragment_tests)
      P(&ctx, "[[early_fragment_tests]]\n");
   P(&ctx, "%s %s %s(\n", get_stage_string(shader->info.stage),
     output_type(shader), get_entrypoint_name(shader));
   ctx.indentlevel++;
   emit_sysvals(&ctx, shader);
   emit_inputs(&ctx, shader);
   ctx.indentlevel--;
   P(&ctx, ")\n");
   P(&ctx, "{\n");
   ctx.indentlevel++;
   msl_emit_output_var(&ctx, shader);
   emit_local_vars(&ctx, shader);
   predeclare_ssa_values(&ctx, impl);
   foreach_list_typed(nir_cf_node, node, node, &impl->body) {
      cf_node_to_metal(&ctx, node);
   }
   emit_output_return(&ctx, shader);
   ctx.indentlevel--;
   P(&ctx, "}\n");
   char *ret = ctx.text->buf;
   _mesa_hash_table_destroy(ctx.types, NULL);
   ralloc_steal(mem_ctx, ctx.text->buf);
   ralloc_free(ctx.text);
   return ret;
}
