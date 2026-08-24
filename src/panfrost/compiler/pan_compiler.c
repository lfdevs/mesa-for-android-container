/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_compiler.h"
#include "nir_xfb_info.h"
#include "pan_nir.h"

#include "bifrost/bi_debug.h"
#include "bifrost/bifrost_compile.h"
#include "bifrost/bifrost/disassemble.h"
#include "bifrost/valhall/disassemble.h"
#include "midgard/disassemble.h"
#include "midgard/midgard_compile.h"
#include "kraid/kraid.h"

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

#ifdef WITH_PANFROST_RUST
#define USE_KRAID_CS (1ull << 0)
#define USE_KRAID_FS (1ull << 1)
#define USE_KRAID_VS (1ull << 2)
#define USE_KRAID_INTERNAL (1ull << 3)
#define USE_KRAID_ALL 0xf

static const struct debug_named_value pan_use_kraid_flags[] = {
   { "cs", USE_KRAID_CS, "Use Kraid for compute shaders" },
   { "fs", USE_KRAID_FS, "Use Kraid for fragment shaders" },
   { "vs", USE_KRAID_VS, "Use Kraid for vertex shaders" },
   { "internal", USE_KRAID_INTERNAL, "Use Kraid for internal shaders" },
   { "all", USE_KRAID_ALL, "Use Kraid for all shader stages" },
   DEBUG_NAMED_VALUE_END,
};

DEBUG_GET_ONCE_FLAGS_OPTION(use_kraid, "PAN_USE_KRAID",
                            pan_use_kraid_flags, 0)
#endif

bool
pan_use_kraid(unsigned arch, mesa_shader_stage stage, bool internal)
{
#ifdef WITH_PANFROST_RUST
   if (arch < 9)
      return false;

   uint64_t use_kraid = debug_get_option_use_kraid();
   if (internal && !(use_kraid & USE_KRAID_INTERNAL))
      return false;

   switch (stage) {
   case MESA_SHADER_VERTEX:
      return use_kraid & USE_KRAID_VS;
   case MESA_SHADER_FRAGMENT:
      return use_kraid & USE_KRAID_FS;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return use_kraid & USE_KRAID_CS;
   default:
      return false;
   }
#else
   return false;
#endif
}

/**
 * Returns a set of flags which may affect the output of the compiler, used
 * to invalidate caches.  This should be passed into disk_cache_create()
 * and may also be used with Vulkan pipeline caches or other shader caches
 * to ensure environment variables are taken into account, even when shaders
 * are pulled from the cache.
 */
uint32_t
pan_get_compiler_flags(unsigned arch)
{
   if (arch >= 6) {
#ifdef WITH_PANFROST_RUST
      const uint32_t use_kraid = debug_get_option_use_kraid();
      const uint32_t kraid_flags = kraid_get_compiler_flags();
#else
      const uint32_t use_kraid = 0, kraid_flags = 0;
#endif
      const uint32_t bi_flags = bifrost_get_compiler_flags();

      assert(bi_flags <= (1ull << 18));
      assert(use_kraid <= (1ull << 4));
      assert(kraid_flags <= (1ull << 10));

      return bi_flags | (use_kraid << 18) | (kraid_flags << 22);
   } else {
      return midgard_get_compiler_flags();
   }
}

const nir_shader_compiler_options *
pan_get_nir_shader_compiler_options(unsigned arch,
                                    mesa_shader_stage stage,
                                    bool merge_wg)
{
#ifdef WITH_PANFROST_RUST
   /* Only return the Kraid options if we're also using it for internal
    * shaders.  We have no internal/external flag here so we have to assume
    * the worst case.  Kraid can generally handle Bifrost NIR but Bifrost
    * can't handle Kraid NIR.
    */
   if (pan_use_kraid(arch, stage, false) && pan_use_kraid(arch, stage, true))
      return kraid_get_nir_shader_compiler_options(arch, merge_wg);
#endif

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
   case 14:
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
pan_lookup_pushed_ubo(const struct pan_fau_layout *fau,
                      unsigned ubo, unsigned offs)
{
   struct pan_ubo_relocation word = {.ubo = ubo, .offset = offs};

   pan_fau_foreach_reloc(fau, i) {
      if (memcmp(fau->words + i, &word, sizeof(word)) == 0)
         return i;
   }

   UNREACHABLE("UBO not pushed");
}

int
pan_lookup_pushed_imm(const struct pan_fau_layout *fau, uint32_t imm)
{
   pan_fau_foreach_imm(fau, i) {
      if (fau->words[i].constant == imm)
         return i;
   }

   return -1;
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

static uint64_t
pan_fixed_varying_mask(nir_shader *nir)
{
   uint64_t mask = 0;

   assert(nir->info.stage == MESA_SHADER_FRAGMENT ||
          nir->info.stage == MESA_SHADER_VERTEX);

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   assert(impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_variable_mode modes = nir_var_shader_in | nir_var_shader_out;
         nir_variable_mode mode;
         nir_intrinsic_instr *intr = nir_get_io_intrinsic(instr, modes, &mode);
         if (!intr)
            continue;

         bool is_varying = !(nir->info.stage == MESA_SHADER_VERTEX &&
                             mode == nir_var_shader_in) &&
                           !(nir->info.stage == MESA_SHADER_FRAGMENT &&
                             mode == nir_var_shader_out);

         nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

         if (!is_varying || sem.location < VARYING_SLOT_VAR0)
            continue;

         nir_alu_type type = nir_intrinsic_has_src_type(intr) ?
            nir_intrinsic_src_type(intr) : nir_intrinsic_dest_type(intr);
         bool is_float = nir_alu_type_get_base_type(type) == nir_type_float;

         /* Only lower mediump floats, they must agree on ALL load/stores */
         if (!(sem.medium_precision && is_float)) {
            mask |= BITFIELD64_RANGE(sem.location, sem.num_slots);
         }
      }
   }

   return mask;
}

static bool
clear_flat_mediump_io_flag(struct nir_builder *b, nir_intrinsic_instr *intr,
                           void *data)
{
   /* The mediump flag must be preserved for XFB, we can remove it for all other
    * flat IO.  It is still useful for interpolated input because of
    * pan_nir_fuse_io_16.
    */
   bool is_flat = intr->intrinsic == nir_intrinsic_load_input ||
                  (intr->intrinsic == nir_intrinsic_store_output &&
                   b->shader->info.stage == MESA_SHADER_VERTEX);

   if (nir_intrinsic_has_io_semantics(intr) &&
       !nir_instr_xfb_write_mask(intr) && is_flat) {
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

      if (sem.medium_precision) {
         sem.medium_precision = 0;
         nir_intrinsic_set_io_semantics(intr, sem);
         return true;
      }
   }
   return false;
}

static bool
is_mediump_varying_instr(const nir_intrinsic_instr *intr, const void *data)
{
   if (!nir_intrinsic_has_io_semantics(intr))
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   uint64_t loc_mask = *(uint64_t *)data;

   if (sem.location < VARYING_SLOT_VAR0 || sem.location > VARYING_SLOT_VAR31)
      return false;

   return loc_mask & BITFIELD64_RANGE(sem.location, sem.num_slots);
}

/* Lower VS/FS varyings early for linked shader, this permits us to do crazy
 * compactions in nir_opt_varyings (and might save us from lots of bugs).
 * Used by lower_mediump_io
 */
void
pan_nir_lower_mediump_io(nir_shader *nir)
{
   /* I don't want to get headaches, XFB gets slowed down */
   if (nir->info.prev_stage_has_xfb ||
       nir->info.has_transform_feedback_varyings)
      return;

   nir_variable_mode modes = 0;

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
      modes = nir_var_shader_out;
      break;
   case MESA_SHADER_FRAGMENT:
      modes = nir_var_shader_in;
      break;
   default:
      assert(!"Unsupported shader");
      return;
   }

   uint64_t lower_mask = ~pan_fixed_varying_mask(nir);

   /* nir_opt_varyings can see thorugh vecs but not through f2f16 of vecs, i.e.
    * it can see a vec2(x, 1.0) but not through f2f16(vec2(x, 1.0)), if we
    * scalarize it will only see f2f16(x) and f2f16(1.0).  nir_opt_varyings will
    * scalarize IO internally anyways.
    */
   NIR_PASS(_, nir, nir_lower_io_to_scalar, modes, is_mediump_varying_instr,
            &lower_mask);

   NIR_PASS(_, nir, nir_lower_mediump_io, modes, lower_mask, false);

   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_constant_folding);

   /* By shrinking vectors we help nir_opt_varyings DCE unused FS loads even
    * in the VS, it also helps collect a smaller varying layout in the future
    */
   NIR_PASS(_, nir, nir_opt_shrink_vectors, false);

   NIR_PASS(_, nir, nir_shader_intrinsics_pass,
            clear_flat_mediump_io_flag, nir_metadata_all, NULL);
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
