/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_compiler.h"
#include "pan_nir.h"

#include "bifrost/bi_debug.h"
#include "bifrost/bifrost_compile.h"
#include "bifrost/bifrost/disassemble.h"
#include "bifrost/valhall/disassemble.h"
#include "midgard/disassemble.h"
#include "midgard/midgard_compile.h"

#include "panfrost/model/pan_model.h"

bool
pan_will_dump_shaders(unsigned arch)
{
   if (arch >= 6)
      return bifrost_will_dump_shaders();
   else
      return midgard_will_dump_shaders();
}

bool
pan_want_debug_info(unsigned arch)
{
   if (arch >= 6)
      return bifrost_want_debug_info();
   else
      return false;
}

const nir_shader_compiler_options *
pan_get_nir_shader_compiler_options(unsigned arch, bool merge_wg)
{
   switch (arch) {
   case 4:
   case 5:
      assert(!merge_wg);
      return &midgard_nir_options;
   case 6:
   case 7:
      assert(!merge_wg);
      return &bifrost_nir_options_v6;
   case 9:
   case 10:
      return merge_wg ? &bifrost_nir_options_v9_merge_wg :
                        &bifrost_nir_options_v9;
   case 11:
   case 12:
   case 13:
      return merge_wg ? &bifrost_nir_options_v11_merge_wg :
                        &bifrost_nir_options_v11;
   default:
      assert(!"Unsupported arch");
      return NULL;
   }
}

void
pan_preprocess_nir(nir_shader *nir, uint64_t gpu_id)
{
   if (pan_arch(gpu_id) >= 6)
      bifrost_preprocess_nir(nir, gpu_id);
   else
      midgard_preprocess_nir(nir, gpu_id);

   /* Lower textures early */
   nir_lower_tex_options lower_tex_options = {
      .lower_txs_lod = true,
      .lower_txp = ~0,
      .lower_tg4_offsets = true,
      .lower_tg4_broadcom_swizzle = true,
      .lower_txd = pan_arch(gpu_id) < 6,
      .lower_txd_cube_map = true,
      .lower_invalid_implicit_lod = true,
   };

   NIR_PASS(_, nir, nir_lower_tex, &lower_tex_options);
}

void
pan_postprocess_nir(nir_shader *nir, const struct pan_compile_inputs *inputs,
                    struct pan_shader_info *info)
{
   memset(info, 0, sizeof(*info));

   if (pan_arch(inputs->gpu_id) >= 6)
      bifrost_postprocess_nir(nir, inputs, info);
   else
      midgard_postprocess_nir(nir, inputs->gpu_id);
}

/** Converts a per-component mask to a byte mask */
uint16_t
pan_to_bytemask(unsigned bytes, unsigned mask)
{
   switch (bytes) {
   case 0:
      assert(mask == 0);
      return 0;

   case 8:
      return mask;

   case 16: {
      unsigned space =
         (mask & 0x1) | ((mask & 0x2) << (2 - 1)) | ((mask & 0x4) << (4 - 2)) |
         ((mask & 0x8) << (6 - 3)) | ((mask & 0x10) << (8 - 4)) |
         ((mask & 0x20) << (10 - 5)) | ((mask & 0x40) << (12 - 6)) |
         ((mask & 0x80) << (14 - 7));

      return space | (space << 1);
   }

   case 32: {
      unsigned space = (mask & 0x1) | ((mask & 0x2) << (4 - 1)) |
                       ((mask & 0x4) << (8 - 2)) | ((mask & 0x8) << (12 - 3));

      return space | (space << 1) | (space << 2) | (space << 3);
   }

   case 64: {
      unsigned A = (mask & 0x1) ? 0xFF : 0x00;
      unsigned B = (mask & 0x2) ? 0xFF : 0x00;
      return A | (B << 8);
   }

   default:
      UNREACHABLE("Invalid register mode");
   }
}

/* Could optimize with a better data structure if anyone cares, TODO: profile */
unsigned
pan_lookup_pushed_ubo(struct pan_ubo_push *push, unsigned ubo, unsigned offs)
{
   struct pan_ubo_word word = {.ubo = ubo, .offset = offs};

   for (unsigned i = 0; i < push->count; ++i) {
      if (memcmp(push->words + i, &word, sizeof(word)) == 0)
         return i;
   }

   UNREACHABLE("UBO not pushed");
}

void
pan_shader_update_info(struct pan_shader_info *info, nir_shader *s,
                       const struct pan_compile_inputs *inputs)
{
   unsigned arch = pan_arch(inputs->gpu_id);

   info->stage = s->info.stage;
   info->contains_barrier =
      s->info.uses_memory_barrier || s->info.uses_control_barrier;
   info->separable = s->info.separate_shader;

   switch (info->stage) {
   case MESA_SHADER_VERTEX:
      info->attributes_read = s->info.inputs_read;
      info->attributes_read_count = util_bitcount64(info->attributes_read);
      info->attribute_count = info->attributes_read_count;

      if (arch <= 5) {
         if (info->midgard.vs.reads_raw_vertex_id)
            info->attribute_count =
               MAX2(info->attribute_count, PAN_VERTEX_ID + 1);

         bool instance_id =
            BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
         if (instance_id)
            info->attribute_count =
               MAX2(info->attribute_count, PAN_INSTANCE_ID + 1);
      }

      info->vs.writes_point_size =
         s->info.outputs_written & VARYING_BIT_PSIZ;
      break;
   case MESA_SHADER_FRAGMENT:
      if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         info->fs.writes_depth = true;
      if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
         info->fs.writes_stencil = true;
      if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK))
         info->fs.writes_coverage = true;

      info->fs.outputs_read = s->info.outputs_read;

      info->fs.sample_shading = s->info.fs.uses_sample_shading;
      info->fs.untyped_color_outputs = s->info.fs.untyped_color_outputs;

      info->fs.can_discard = s->info.fs.uses_discard;
      info->fs.early_fragment_tests = s->info.fs.early_fragment_tests;

      /* List of reasons we need to execute frag shaders when things
       * are masked off */

      info->fs.sidefx = s->info.writes_memory || s->info.fs.uses_discard;

      /* With suitable ZSA/blend, is early-z possible? */
      info->fs.can_early_z = !info->fs.sidefx && !info->fs.writes_depth &&
                             !info->fs.writes_stencil &&
                             !info->fs.writes_coverage;

      /* Similiarly with suitable state, is FPK possible? */
      info->fs.can_fpk = !info->fs.writes_depth && !info->fs.writes_stencil &&
                         !info->fs.writes_coverage && !info->fs.can_discard &&
                         !info->fs.outputs_read;

      /* Requires the same hardware guarantees, so grouped as one bit
       * in the hardware.
       */
      info->contains_barrier |= s->info.fs.needs_coarse_quad_helper_invocations;

      info->fs.reads_frag_coord =
         (s->info.inputs_read & VARYING_BIT_POS) ||
         BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_FRAG_COORD);
      info->fs.reads_primitive_id =
         (s->info.inputs_read & VARYING_BIT_PRIMITIVE_ID) ||
         BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);
      info->fs.reads_point_coord =
         s->info.inputs_read & VARYING_BIT_PNTC;
      info->fs.reads_face =
         (s->info.inputs_read & VARYING_BIT_FACE) ||
         BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_FRONT_FACE);
      break;
   default:
      /* Everything else treated as compute */
      info->wls_size = s->info.shared_size;
      break;
   }

   info->outputs_written = s->info.outputs_written;
   info->images_used =
      s->info.images_used[0] | ((uint64_t)s->info.images_used[1]) << 32;
   info->attribute_count += BITSET_LAST_BIT(s->info.images_used);
   if (arch >= 6 && arch < 9)
      info->attribute_count += BITSET_LAST_BIT(s->info.texture_buffers);
   info->writes_global = s->info.writes_memory;
   info->ubo_count = s->info.num_ubos;

   info->sampler_count = info->texture_count =
      BITSET_LAST_BIT(s->info.textures_used);

   unsigned execution_mode = s->info.float_controls_execution_mode;
   info->ftz_fp16 = nir_is_denorm_flush_to_zero(execution_mode, 16);
   info->ftz_fp32 = nir_is_denorm_flush_to_zero(execution_mode, 32);

   if (arch >= 9) {
      /* Valhall hardware doesn't have a "flush FP16, preserve FP32" mode, and
       * we don't advertise independent FP16/FP32 denorm modes in panvk, but
       * it's still possible to have shaders that don't specify any denorm mode
       * for FP32. In that case, default to flush FP32. */
      if (info->ftz_fp16 && !info->ftz_fp32) {
         assert(!nir_is_denorm_preserve(execution_mode, 32));
         info->ftz_fp32 = true;
      }
   }
}

void
pan_shader_compile(nir_shader *s, struct pan_compile_inputs *inputs,
                   struct util_dynarray *binary, struct pan_shader_info *info)
{
   unsigned arch = pan_arch(inputs->gpu_id);

   NIR_PASS(_, s, nir_inline_sysval, nir_intrinsic_load_printf_buffer_size,
            PAN_PRINTF_BUFFER_SIZE - 8);

   if (arch >= 6) {
      bifrost_compile_shader_nir(s, inputs, binary, info);
      /* pan_shader_update_info done in the compile */
   } else {
      midgard_compile_shader_nir(s, inputs, binary, info);
      pan_shader_update_info(info, s, inputs);
   }
}

void
pan_disassemble(FILE *fp, const void *code, size_t size, uint64_t gpu_id,
                bool verbose)
{
   if (pan_arch(gpu_id) >= 9)
      disassemble_valhall(fp, (const uint64_t *)code, size, verbose);
   else if (pan_arch(gpu_id) >= 6)
      disassemble_bifrost(fp, code, size, verbose);
   else
      disassemble_midgard(fp, code, size, gpu_id, verbose);
}
