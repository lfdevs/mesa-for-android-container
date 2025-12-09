/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

/* This file primarily concerns itself with mapping from the NIR (and Vulkan)
 * model of I/O to the Metal one. */

#include "msl_private.h"

#include "nir_builder.h"

/* Mapping from alu type to Metal scalar type */
static const char *
alu_type_to_string(nir_alu_type type)
{
   switch (type) {
   case nir_type_uint8:
      return "uchar";
   case nir_type_uint16:
      return "ushort";
   case nir_type_uint32:
      return "uint";
   case nir_type_uint64:
      return "ulong";
   case nir_type_int8:
      return "char";
   case nir_type_int16:
      return "short";
   case nir_type_int32:
      return "int";
   case nir_type_int64:
      return "long";
   case nir_type_float16:
      return "half";
   case nir_type_float32:
      return "float";
   case nir_type_bool8:
      return "bool";
   default:
      UNREACHABLE("Unsupported nir_alu_type");
   }
};

/* Type suffix for a vector of a given size. */
static const char *vector_suffixes[] = {
   [1] = "",
   [2] = "2",
   [3] = "3",
   [4] = "4",
};

/* The type names of the generated output structs */
static const char *VERTEX_OUTPUT_TYPE = "VertexOut";
static const char *FRAGMENT_OUTPUT_TYPE = "FragmentOut";

/* Mapping from NIR's varying slots to the generated struct member name */
static const struct {
   const char *name;
   bool user;
   bool scalarized;
} VARYING_SLOT_INFO[NUM_TOTAL_VARYING_SLOTS] = {
   [VARYING_SLOT_POS] = {"position"},
   [VARYING_SLOT_PSIZ] = {"point_size"},
   [VARYING_SLOT_PRIMITIVE_ID] = {"primitive_id"},
   [VARYING_SLOT_LAYER] = {"render_target_array_index"},
   [VARYING_SLOT_VIEWPORT] = {"viewport_array_index"},
   [VARYING_SLOT_CLIP_DIST0] = {"clip_0", .user = true, .scalarized = true},
   [VARYING_SLOT_CLIP_DIST1] = {"clip_1", .user = true, .scalarized = true},
   [VARYING_SLOT_VAR0] = {"vary_00", .user = true},
   [VARYING_SLOT_VAR1] = {"vary_01", .user = true},
   [VARYING_SLOT_VAR2] = {"vary_02", .user = true},
   [VARYING_SLOT_VAR3] = {"vary_03", .user = true},
   [VARYING_SLOT_VAR4] = {"vary_04", .user = true},
   [VARYING_SLOT_VAR5] = {"vary_05", .user = true},
   [VARYING_SLOT_VAR6] = {"vary_06", .user = true},
   [VARYING_SLOT_VAR7] = {"vary_07", .user = true},
   [VARYING_SLOT_VAR8] = {"vary_08", .user = true},
   [VARYING_SLOT_VAR9] = {"vary_09", .user = true},
   [VARYING_SLOT_VAR10] = {"vary_10", .user = true},
   [VARYING_SLOT_VAR11] = {"vary_11", .user = true},
   [VARYING_SLOT_VAR12] = {"vary_12", .user = true},
   [VARYING_SLOT_VAR13] = {"vary_13", .user = true},
   [VARYING_SLOT_VAR14] = {"vary_14", .user = true},
   [VARYING_SLOT_VAR15] = {"vary_15", .user = true},
   [VARYING_SLOT_VAR16] = {"vary_16", .user = true},
   [VARYING_SLOT_VAR17] = {"vary_17", .user = true},
   [VARYING_SLOT_VAR18] = {"vary_18", .user = true},
   [VARYING_SLOT_VAR19] = {"vary_19", .user = true},
   [VARYING_SLOT_VAR20] = {"vary_20", .user = true},
   [VARYING_SLOT_VAR21] = {"vary_21", .user = true},
   [VARYING_SLOT_VAR22] = {"vary_22", .user = true},
   [VARYING_SLOT_VAR23] = {"vary_23", .user = true},
   [VARYING_SLOT_VAR24] = {"vary_24", .user = true},
   [VARYING_SLOT_VAR25] = {"vary_25", .user = true},
   [VARYING_SLOT_VAR26] = {"vary_26", .user = true},
   [VARYING_SLOT_VAR27] = {"vary_27", .user = true},
   [VARYING_SLOT_VAR28] = {"vary_28", .user = true},
   [VARYING_SLOT_VAR29] = {"vary_29", .user = true},
   [VARYING_SLOT_VAR30] = {"vary_30", .user = true},
   [VARYING_SLOT_VAR31] = {"vary_31", .user = true},
};

static void
varying_slot_name(struct nir_to_msl_ctx *ctx, unsigned location,
                  unsigned component)
{
   if (VARYING_SLOT_INFO[location].scalarized) {
      P(ctx, "%s_%c", VARYING_SLOT_INFO[location].name, "xyzw"[component]);
   } else {
      P(ctx, "%s", VARYING_SLOT_INFO[location].name);
   }
}

static void
varying_slot_semantic(struct nir_to_msl_ctx *ctx, unsigned location,
                      unsigned component)
{
   if (VARYING_SLOT_INFO[location].user) {
      P(ctx, "[[user(");
      varying_slot_name(ctx, location, component);
      P(ctx, ")]]");
   } else {
      P(ctx, "[[");
      varying_slot_name(ctx, location, component);
      P(ctx, "]]");
   }
}

/* Mapping from NIR fragment output slot to MSL struct member name */
static const char *FS_OUTPUT_NAME[] = {
   [FRAG_RESULT_DEPTH] = "depth_out",
   [FRAG_RESULT_STENCIL] = "stencil_out",
   [FRAG_RESULT_SAMPLE_MASK] = "sample_mask_out",
   [FRAG_RESULT_DATA0] = "color_0",
   [FRAG_RESULT_DATA1] = "color_1",
   [FRAG_RESULT_DATA2] = "color_2",
   [FRAG_RESULT_DATA3] = "color_3",
   [FRAG_RESULT_DATA4] = "color_4",
   [FRAG_RESULT_DATA5] = "color_5",
   [FRAG_RESULT_DATA6] = "color_6",
   [FRAG_RESULT_DATA7] = "color_7",
};

/* Mapping from NIR fragment output slot to MSL struct member attribute */
static const char *FS_OUTPUT_SEMANTIC[] = {
   [FRAG_RESULT_DEPTH] = "", // special case, depends on depth layout
   [FRAG_RESULT_STENCIL] = "stencil", [FRAG_RESULT_SAMPLE_MASK] = "sample_mask",
   [FRAG_RESULT_DATA0] = "color(0)",  [FRAG_RESULT_DATA1] = "color(1)",
   [FRAG_RESULT_DATA2] = "color(2)",  [FRAG_RESULT_DATA3] = "color(3)",
   [FRAG_RESULT_DATA4] = "color(4)",  [FRAG_RESULT_DATA5] = "color(5)",
   [FRAG_RESULT_DATA6] = "color(6)",  [FRAG_RESULT_DATA7] = "color(7)",
};

const char *depth_layout_arg[8] = {
   [FRAG_DEPTH_LAYOUT_ANY] = "any",
   [FRAG_DEPTH_LAYOUT_GREATER] = "greater",
   [FRAG_DEPTH_LAYOUT_LESS] = "less",
   [FRAG_DEPTH_LAYOUT_UNCHANGED] = "any",
};

/* Generate the struct definition for the vertex shader return value */
static void
vs_output_block(nir_shader *shader, struct nir_to_msl_ctx *ctx)
{
   P(ctx, "struct %s {\n", VERTEX_OUTPUT_TYPE);
   ctx->indentlevel++;
   u_foreach_bit64(location, shader->info.outputs_written) {
      struct io_slot_info info = ctx->outputs_info[location];
      bool scalarized = VARYING_SLOT_INFO[location].scalarized;
      const char *type = alu_type_to_string(info.type);
      const char *vector_suffix =
         scalarized ? "" : vector_suffixes[info.num_components];
      unsigned components = scalarized ? info.num_components : 1;
      for (int c = 0; c < components; c++) {
         P_IND(ctx, "%s%s ", type, vector_suffix);
         varying_slot_name(ctx, location, c);
         P(ctx, " ");
         varying_slot_semantic(ctx, location, c);
         P(ctx, ";\n");
      }
   }

   if (shader->info.clip_distance_array_size)
      P_IND(ctx, "float gl_ClipDistance [[clip_distance]] [%d];",
        shader->info.clip_distance_array_size);
   ctx->indentlevel--;
   P(ctx, "};\n");
}

/* Generate the struct definition for the fragment shader input argument */
static void
fs_input_block(nir_shader *shader, struct nir_to_msl_ctx *ctx)
{
   P(ctx, "struct FragmentIn {\n");
   ctx->indentlevel++;
   u_foreach_bit64(location, shader->info.inputs_read) {
      struct io_slot_info info = ctx->inputs_info[location];
      bool scalarized = VARYING_SLOT_INFO[location].scalarized;
      const char *type = alu_type_to_string(info.type);
      const char *vector_suffix =
         scalarized ? "" : vector_suffixes[info.num_components];
      const char *interp = "";
      switch (info.interpolation) {
      case INTERP_MODE_NOPERSPECTIVE:
         if (info.centroid)
            interp = "[[centroid_no_perspective]]";
         else if (info.sample)
            interp = "[[sample_no_perspective]]";
         else
            interp = "[[center_no_perspective]]";
         break;
      case INTERP_MODE_FLAT:
         interp = "[[flat]]";
         break;
      default:
         if (info.centroid)
            interp = "[[centroid_perspective]]";
         else if (info.sample)
            interp = "[[sample_perspective]]";
         break;
      }
      unsigned components = scalarized ? info.num_components : 1;
      for (int c = 0; c < components; c++) {
         P_IND(ctx, "%s%s ", type, vector_suffix);
         varying_slot_name(ctx, location, c);
         P(ctx, " ");
         varying_slot_semantic(ctx, location, c);
         P(ctx, " %s;\n", interp);
      }
   }

   /* Enable reading from framebuffer */
   u_foreach_bit64(location, shader->info.outputs_read) {
      struct io_slot_info info = ctx->outputs_info[location];
      const char *type = alu_type_to_string(info.type);
      const char *vector_suffix = vector_suffixes[info.num_components];
      P_IND(ctx, "%s%s ", type, vector_suffix);
      P(ctx, "%s [[%s, raster_order_group(0)]];\n", FS_OUTPUT_NAME[location],
        FS_OUTPUT_SEMANTIC[location]);
   }

   ctx->indentlevel--;
   P(ctx, "};\n");
}

/* Generate the struct definition for the fragment shader return value */
static void
fs_output_block(nir_shader *shader, struct nir_to_msl_ctx *ctx)
{
   P_IND(ctx, "struct %s {\n", FRAGMENT_OUTPUT_TYPE);
   ctx->indentlevel++;
   u_foreach_bit64(location, shader->info.outputs_written) {
      struct io_slot_info info = ctx->outputs_info[location];
      const char *type = alu_type_to_string(info.type);
      const char *vector_suffix = vector_suffixes[info.num_components];
      P_IND(ctx, "%s%s ", type, vector_suffix);
      if (location == FRAG_RESULT_DEPTH) {
         enum gl_frag_depth_layout depth_layout = shader->info.fs.depth_layout;
         assert(depth_layout_arg[depth_layout]);
         P(ctx, "%s [[depth(%s)]];\n", FS_OUTPUT_NAME[location],
           depth_layout_arg[depth_layout]);
      } else {
         // TODO: scalarized fs outputs
         P(ctx, "%s [[%s]];\n", FS_OUTPUT_NAME[location],
           FS_OUTPUT_SEMANTIC[location]);
      }
   }
   ctx->indentlevel--;
   P_IND(ctx, "};\n")
}

struct gather_ctx {
   struct io_slot_info *input;
   struct io_slot_info *output;
};

static bool
msl_nir_gather_io_info(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   struct gather_ctx *ctx = (struct gather_ctx *)data;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_interpolated_input: {
      unsigned component = nir_intrinsic_component(intrin);
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
      assert(io.num_slots == 1u && "We don't support arrays");

      unsigned location = nir_src_as_uint(intrin->src[1u]) + io.location;
      ctx->input[location].type = nir_intrinsic_dest_type(intrin);
      ctx->input[location].num_components =
         MAX2(ctx->input[location].num_components,
              intrin->num_components + component);
      assert(ctx->input[location].num_components <= 4u &&
             "Cannot have more than a vec4");

      nir_intrinsic_instr *interp_intrin =
         nir_src_as_intrinsic(intrin->src[0u]);
      ctx->input[location].interpolation =
         nir_intrinsic_interp_mode(interp_intrin);
      ctx->input[location].centroid =
         interp_intrin->intrinsic == nir_intrinsic_load_barycentric_centroid;
      ctx->input[location].sample =
         interp_intrin->intrinsic == nir_intrinsic_load_barycentric_sample;
      break;
   }
   case nir_intrinsic_load_input: {
      unsigned component = nir_intrinsic_component(intrin);
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
      assert(io.num_slots == 1u && "We don't support arrays");

      unsigned location = nir_src_as_uint(intrin->src[0u]) + io.location;
      ctx->input[location].type = nir_intrinsic_dest_type(intrin);
      ctx->input[location].interpolation = INTERP_MODE_FLAT;
      ctx->input[location].num_components =
         MAX2(ctx->input[location].num_components,
              intrin->num_components + component);
      assert(ctx->input[location].num_components <= 4u &&
             "Cannot have more than a vec4");
      break;
   }
   case nir_intrinsic_load_output: {
      unsigned component = nir_intrinsic_component(intrin);
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
      assert(io.num_slots == 1u && "We don't support arrays");

      unsigned location = nir_src_as_uint(intrin->src[0u]) + io.location;
      ctx->output[location].type = nir_intrinsic_dest_type(intrin);
      ctx->output[location].num_components =
         MAX2(ctx->output[location].num_components,
              intrin->num_components + component);
      assert(ctx->output[location].num_components <= 4u &&
             "Cannot have more than a vec4");
      break;
   }
   case nir_intrinsic_store_output: {
      unsigned component = nir_intrinsic_component(intrin);
      unsigned write_mask = nir_intrinsic_write_mask(intrin);
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
      assert(io.num_slots == 1u && "We don't support arrays");

      /* Due to nir_lower_blend that doesn't generate intrinsics with the same
       * num_components as destination, we need to compute current store's
       * num_components using offset and mask. */
      unsigned num_components = component + 1u;
      unsigned mask_left_most_index = 0u;
      for (unsigned i = 0u; i < intrin->num_components; ++i) {
         if ((write_mask >> i) & 1u)
            mask_left_most_index = i;
      }
      num_components += mask_left_most_index;
      unsigned location = nir_src_as_uint(intrin->src[1u]) + io.location;
      ctx->output[location].type = nir_intrinsic_src_type(intrin);
      ctx->output[location].num_components =
         MAX3(ctx->output[location].num_components, num_components,
              intrin->num_components);
      assert(ctx->output[location].num_components <= 4u &&
             "Cannot have more than a vec4");
      break;
   }
   default:
      break;
   }

   return false;
}

void
msl_gather_io_info(struct nir_to_msl_ctx *ctx,
                   struct io_slot_info *info_array_input,
                   struct io_slot_info *info_array_output)
{
   struct gather_ctx gather_ctx = {
      .input = info_array_input,
      .output = info_array_output,
   };
   nir_shader_intrinsics_pass(ctx->shader, msl_nir_gather_io_info,
                              nir_metadata_all, &gather_ctx);
}

/* Generate all the struct definitions needed for shader I/O */
void
msl_emit_io_blocks(struct nir_to_msl_ctx *ctx, nir_shader *shader)
{
   switch (ctx->shader->info.stage) {
   case MESA_SHADER_VERTEX:
      vs_output_block(shader, ctx);
      break;
   case MESA_SHADER_FRAGMENT:
      fs_input_block(shader, ctx);
      fs_output_block(shader, ctx);
      break;
   case MESA_SHADER_COMPUTE:
      break;
   default:
      assert(0);
   }
   // TODO_KOSMICKRISP This should not exist. We need to create input structs in
   // nir that will later be translated
   P(ctx, "struct Buffer {\n");
   ctx->indentlevel++;
   P_IND(ctx, "uint64_t contents[1];\n"); // TODO_KOSMICKRISP This should not be
                                          // a cpu pointer
   ctx->indentlevel--;
   P(ctx, "};\n")

   P(ctx, "struct SamplerTable {\n");
   ctx->indentlevel++;
   P_IND(ctx, "sampler handles[1024];\n");
   ctx->indentlevel--;
   P(ctx, "};\n")
}

void
msl_emit_output_var(struct nir_to_msl_ctx *ctx, nir_shader *shader)
{
   switch (shader->info.stage) {
   case MESA_SHADER_VERTEX:
      P_IND(ctx, "%s out = {};\n", VERTEX_OUTPUT_TYPE);
      break;
   case MESA_SHADER_FRAGMENT:
      P_IND(ctx, "%s out = {};\n", FRAGMENT_OUTPUT_TYPE);

      /* Load inputs to output */
      u_foreach_bit64(location, shader->info.outputs_read) {
         P_IND(ctx, "out.%s = in.%s;\n", FS_OUTPUT_NAME[location],
               FS_OUTPUT_NAME[location]);
      }
      break;
   default:
      break;
   }
}

void
msl_output_name(struct nir_to_msl_ctx *ctx, unsigned location,
                unsigned component)
{
   P(ctx, "out.")
   switch (ctx->shader->info.stage) {
   case MESA_SHADER_VERTEX:
      varying_slot_name(ctx, location, component);
      break;
   case MESA_SHADER_FRAGMENT:
      P(ctx, "%s", FS_OUTPUT_NAME[location]);
      break;
   default:
      UNREACHABLE("Invalid shader stage");
   }
}

void
msl_input_name(struct nir_to_msl_ctx *ctx, unsigned location,
               unsigned component)
{
   P(ctx, "in.");
   switch (ctx->shader->info.stage) {
   case MESA_SHADER_FRAGMENT:
      varying_slot_name(ctx, location, component);
      break;
   default:
      UNREACHABLE("Invalid shader stage");
   }
}

uint32_t
msl_input_num_components(struct nir_to_msl_ctx *ctx, uint32_t location)
{
   if (ctx->shader->info.stage == MESA_SHADER_FRAGMENT &&
       VARYING_SLOT_INFO[location].scalarized)
      return 1;
   else
      return ctx->inputs_info[location].num_components;
}

uint32_t
msl_output_num_components(struct nir_to_msl_ctx *ctx, uint32_t location)
{
   if (ctx->shader->info.stage == MESA_SHADER_VERTEX &&
       VARYING_SLOT_INFO[location].scalarized)
      return 1;
   else
      return ctx->outputs_info[location].num_components;
}
