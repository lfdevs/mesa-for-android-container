/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_COMPILER_H_
#define IR3_COMPILER_H_

#include "compiler/nir/nir.h"
#include "util/disk_cache.h"
#include "util/log.h"
#include "util/perf/cpu_trace.h"

#include "freedreno_dev_info.h"

#include "ir3.h"

BEGINC;

struct ir3_ra_reg_set;
struct ir3_shader;

struct ir3_compiler_options {
   /* If true, promote UBOs (except for constant data) to constants using ldc.k
    * in the preamble. The driver should ignore everything in ubo_state except
    * for the constant data UBO, which is excluded because the command pushing
    * constants for it can be pre-baked when compiling the shader.
    */
   bool push_ubo_with_preamble;

   /* If true, disable the shader cache. The driver is then responsible for
    * caching.
    */
   bool disable_cache;

   /* If >= 0, this specifies the bindless descriptor set + descriptor to use
    * for txf_ms_fb
    */
   int bindless_fb_read_descriptor;
   int bindless_fb_read_slot;

   /* True if 16-bit descriptors are available. */
   bool storage_16bit;
   /* True if 8-bit descriptors are available. */
   bool storage_8bit;

   /* If base_vertex should be lowered in nir */
   bool lower_base_vertex;

   bool shared_push_consts;

   /* "dual_color_blend_by_location" workaround is enabled: */
   bool dual_color_blend_by_location;

   uint64_t uche_trap_base;
};

struct ir3_gpu_profile {
    uint32_t reg_efficiency;
    uint32_t max_sy_inflight;
    uint32_t max_ss_inflight;
    bool force_double_threadsize;
};

struct ir3_compiler {
   struct fd_device *dev;
   const struct fd_dev_id *dev_id;
   uint8_t gen;
   uint32_t shader_count;

   struct disk_cache *disk_cache;

   struct nir_shader_compiler_options nir_options;

   /*
    * Configuration options for things handled differently by turnip vs
    * gallium
    */
   struct ir3_compiler_options options;

   /*
    * Configuration options for things that are handled differently on
    * different generations:
    */

   bool is_64bit;

   /* a4xx (and later) drops SP_FS_FLAT_SHAD_MODE_REG_* for flat-interpolate
    * so we need to use ldlv.u32 to load the varying directly:
    */
   bool flat_bypass;

   /* on a3xx, we need to add one to # of array levels:
    */
   bool levels_add_one;

   /* on a3xx, we need to scale up integer coords for isaml based
    * on LoD:
    */
   bool unminify_coords;

   /* on a3xx do txf_ms w/ isaml and scaled coords: */
   bool txf_ms_with_isaml;

   /* on a4xx, for array textures we need to add 0.5 to the array
    * index coordinate:
    */
   bool array_index_add_half;

   /* on a6xx, rewrite samgp to sequence of samgq0-3 in vertex shaders:
    */
   bool samgq_workaround;

   /* Whether full and half regs are merged. */
   bool mergedregs;

   const struct fd_dev_info *info;

   /* The maximum number of constants, in vec4's, across the entire graphics
    * pipeline.
    */
   uint16_t max_const_pipeline;

   /* The maximum number of constants, in vec4's, for VS+HS+DS+GS. */
   uint16_t max_const_geom;

   /* The maximum number of constants, in vec4's, for FS. */
   uint16_t max_const_frag;

   /* A "safe" max constlen that can be applied to each shader in the
    * pipeline which we guarantee will never exceed any combined limits.
    */
   uint16_t max_const_safe;

   /* The maximum number of constants, in vec4's, for compute shaders. */
   uint16_t max_const_compute;

   /* See freedreno_dev_info::compute_lb_size. */
   uint32_t compute_lb_size;

   /* Number of instructions that the shader's base address and length
    * (instrlen divides instruction count by this) must be aligned to.
    */
   uint32_t instr_align;

   /* on a3xx, the unit of indirect const load is higher than later gens (in
    * vec4 units):
    */
   uint32_t const_upload_unit;

   /* This is theoretical maximum number of vec4 registers that one wave of
    * the base threadsize could use. To get the actual size of the register
    * file in bytes one would need to compute:
    *
    * reg_size_vec4 * threadsize_base * wave_granularity * 16 (bytes per vec4)
    *
    * However this number is more often what we actually need. For example, a
    * max_reg more than half of this will result in a doubled threadsize
    * being impossible (because double-sized waves take up twice as many
    * registers). Also, the formula for the occupancy given a particular
    * register footprint is simpler.
    *
    * It is in vec4 units because the register file is allocated
    * with vec4 granularity, so it's in the same units as max_reg.
    */
   uint32_t reg_size_vec4;

   /* The number of total branch stack entries. */
   uint32_t branchstack_size;

   /* The maximum number of branch stack entries per wave. */
   uint32_t max_branchstack;

   /* The byte increment of MEMSIZEPERITEM, the private memory per-fiber allocation. */
   uint32_t pvtmem_per_fiber_align;

   /* Whether clip+cull distances are supported */
   bool has_clip_cull;

   /* Whether private memory is supported */
   bool has_pvtmem;

   /* Whether SSBOs have descriptors for sampling with ISAM */
   bool has_isam_ssbo;

   /* Is lock/unlock sequence needed for CS? */
   bool cs_lock_unlock_quirk;

   /* True if the shfl instruction is supported. Needed for subgroup rotate and
    * (more efficient) shuffle.
    */
   bool has_shfl;

   /* True if the bitwise triops (sh[lr][gm]/andg) are supported. */
   bool has_bitwise_triops;

   /* Number of available predicate registers (p0.c) */
   uint32_t num_predicates;

   /* True if bitops (and.b, or.b, xor.b, not.b) can write to p0.c */
   bool bitops_can_write_predicates;

   /* True if braa/brao are available. */
   bool has_branch_and_or;

   /* True if predt/predf/prede are supported. */
   bool has_predication;

   /* MAX_COMPUTE_VARIABLE_GROUP_INVOCATIONS_ARB */
   uint32_t max_variable_workgroup_size;

   /* Type to use for 1b nir bools: */
   type_t bool_type;

   /* Whether compute invocation params are passed in via shared regfile or
    * constbuf. a5xx+ has the shared regfile.
    */
   bool has_shared_regfile;

   /* True if preamble instructions (shps, shpe, etc.) are supported */
   bool has_preamble;

   /* Where the shared consts start in constants file, in vec4's. */
   uint16_t shared_consts_base_offset;

   /* The size of shared consts for CS and FS(in vec4's).
    * Also the size that is actually used on geometry stages (on a6xx).
    */
   uint64_t shared_consts_size;

   /* Found on a6xx for geometry stages, that is different from
    * actually used shared consts.
    *
    * TODO: Keep an eye on this for next gens.
    */
   uint64_t geom_shared_consts_size_quirk;

   /* True if (rptN) is supported for bary.f. */
   bool has_rpt_bary_f;

   /* True if alias.tex is supported. */
   bool has_alias_tex;

   bool cat3_rel_offset_0_quirk;

   /*
    * Some low-bandwidth parts benefit from spending a little more constant
    * file space to merge nearby promoted UBO ranges. This reduces the number
    * of memory-backed UBO fetch windows left in hot shader code.
    */
   bool coalesce_ubo_push_ranges;

   struct {
      /* The number of cycles needed for the result of one ALU operation to be
       * available to another ALU operation. Only valid when the halfness of the
       * source and destination match.
       */
      unsigned alu_to_alu;

      /* The number of cycles needed for the result of one instruction to be
       * available to another. Valid for a0.x, a1.x, and p0.c destinations, ALU
       * to non-ALU dependencies, and ALU to ALU dependencies witch mismatched
       * halfness.
       */
      unsigned non_alu;

      /* The number of cycles from the start of the instruction until a cat3
       * instruction reads its 3rd src.
       */
      unsigned cat3_src2_read;
   } delay_slots;
};

void ir3_compiler_destroy(struct ir3_compiler *compiler);
struct ir3_compiler *ir3_compiler_create(struct fd_device *dev,
                                         const struct fd_dev_id *dev_id,
                                         const struct fd_dev_info *dev_info,
                                         const struct ir3_compiler_options *options);

void ir3_disk_cache_init(struct ir3_compiler *compiler);
void ir3_disk_cache_init_shader_key(struct ir3_compiler *compiler,
                                    struct ir3_shader *shader);
struct ir3_shader_variant *ir3_retrieve_variant(struct blob_reader *blob,
                                                struct ir3_compiler *compiler,
                                                void *mem_ctx);
void ir3_store_variant(struct blob *blob, const struct ir3_shader_variant *v);
bool ir3_disk_cache_retrieve(struct ir3_shader *shader,
                             struct ir3_shader_variant *v);
void ir3_disk_cache_store(struct ir3_shader *shader,
                          struct ir3_shader_variant *v);

const nir_shader_compiler_options *
ir3_get_compiler_options(struct ir3_compiler *compiler);

int ir3_compile_shader_nir(struct ir3_compiler *compiler,
                           struct ir3_shader *shader,
                           struct ir3_shader_variant *so);

/* gpu pointer size in units of 32bit registers/slots */
static inline unsigned
ir3_pointer_size(struct ir3_compiler *compiler)
{
   return compiler->is_64bit ? 2 : 1;
}

enum ir3_shader_debug {
   IR3_DBG_SHADER_VS = BITFIELD_BIT(0),
   IR3_DBG_SHADER_TCS = BITFIELD_BIT(1),
   IR3_DBG_SHADER_TES = BITFIELD_BIT(2),
   IR3_DBG_SHADER_GS = BITFIELD_BIT(3),
   IR3_DBG_SHADER_FS = BITFIELD_BIT(4),
   IR3_DBG_SHADER_CS = BITFIELD_BIT(5),
   IR3_DBG_DISASM = BITFIELD_BIT(6),
   IR3_DBG_OPTMSGS = BITFIELD_BIT(7),
   IR3_DBG_FORCES2EN = BITFIELD_BIT(8),
   IR3_DBG_NOUBOOPT = BITFIELD_BIT(9),
   IR3_DBG_NOFP16 = BITFIELD_BIT(10),
   IR3_DBG_NOCACHE = BITFIELD_BIT(11),
   IR3_DBG_SPILLALL = BITFIELD_BIT(12),
   IR3_DBG_NOPREAMBLE = BITFIELD_BIT(13),
   IR3_DBG_SHADER_INTERNAL = BITFIELD_BIT(14),
   IR3_DBG_FULLSYNC = BITFIELD_BIT(15),
   IR3_DBG_FULLNOP = BITFIELD_BIT(16),
   IR3_DBG_NOEARLYPREAMBLE = BITFIELD_BIT(17),
   IR3_DBG_NODESCPREFETCH = BITFIELD_BIT(18),
   IR3_DBG_EXPANDRPT = BITFIELD_BIT(19),
   IR3_DBG_ASM_ROUNDTRIP = BITFIELD_BIT(20),
   IR3_DBG_THREAD64 = BITFIELD_BIT(21),

   /* MESA_DEBUG-only options: */
   IR3_DBG_SCHEDMSGS = BITFIELD_BIT(22),
   IR3_DBG_RAMSGS = BITFIELD_BIT(23),
   IR3_DBG_NOALIASTEX = BITFIELD_BIT(24),
   IR3_DBG_NOALIASRT = BITFIELD_BIT(25),
};

extern enum ir3_shader_debug ir3_shader_debug;
extern const char *ir3_shader_override_path;

static inline bool
shader_debug_enabled(mesa_shader_stage type, bool internal)
{
   if (internal)
      return !!(ir3_shader_debug & IR3_DBG_SHADER_INTERNAL);

   if (ir3_shader_debug & IR3_DBG_DISASM)
      return true;

   switch (type) {
   case MESA_SHADER_VERTEX:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_VS);
   case MESA_SHADER_TESS_CTRL:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_TCS);
   case MESA_SHADER_TESS_EVAL:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_TES);
   case MESA_SHADER_GEOMETRY:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_GS);
   case MESA_SHADER_FRAGMENT:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_FS);
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return !!(ir3_shader_debug & IR3_DBG_SHADER_CS);
   default:
      assert(0);
      return false;
   }
}

static inline void
ir3_debug_print(struct ir3 *ir, const char *when)
{
   if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
      mesa_logi("%s:", when);
      ir3_print(ir);
   }
}

/* Return the debug flags that influence shader codegen and should be included
 * in the hash key. Note that we use a deny list so that we don't accidentally
 * forget to include new flags.
 */
static inline enum ir3_shader_debug
ir3_shader_debug_hash_key()
{
   return (enum ir3_shader_debug)(
      ir3_shader_debug &
      ~(IR3_DBG_SHADER_VS | IR3_DBG_SHADER_TCS | IR3_DBG_SHADER_TES |
        IR3_DBG_SHADER_GS | IR3_DBG_SHADER_FS | IR3_DBG_SHADER_CS |
        IR3_DBG_DISASM | IR3_DBG_OPTMSGS | IR3_DBG_NOCACHE |
        IR3_DBG_SHADER_INTERNAL | IR3_DBG_SCHEDMSGS | IR3_DBG_RAMSGS));
}

/* Returns a pointer to internal static tmp buffer. */
const char *
ir3_shader_debug_as_string(void);

void ir3_shader_bisect_init(void);
bool ir3_shader_bisect_need_shader_key(void);
void ir3_shader_bisect_dump_id(struct ir3_shader_variant *v);
bool ir3_shader_bisect_select(struct ir3_shader_variant *v);
bool ir3_shader_bisect_disasm_select(struct ir3_shader_variant *v);

/* ========== A8XX HELPER FUNCTIONS ========== */

struct ir3_gpu_profile;
struct ir3_gpu_profile ir3_get_gpu_profile(uint32_t chip_id);
uint32_t ir3_effective_reg_size(struct ir3_compiler *compiler);

static inline bool
ir3_force_double_threadsize(struct ir3_compiler *compiler)
{
   if (compiler->gen >= 8) {
      struct ir3_gpu_profile profile = ir3_get_gpu_profile(compiler->dev_id->chip_id);
      return profile.force_double_threadsize;
   }
   return compiler->info->props.supports_double_threadsize;
}

/* Helper function to check if A8XX should use aggressive const limits */
static inline bool
ir3_use_aggressive_const_limits(struct ir3_compiler *compiler)
{
   return compiler->gen >= 8;
}

/* Helper function to get optimal delay slots for current gen */
static inline unsigned
ir3_get_alu_to_alu_delay(struct ir3_compiler *compiler)
{
   if (compiler->gen >= 8)
      return 1;
   if (compiler->gen >= 7)
      return 2;
   return 3;
}

static inline unsigned
ir3_get_non_alu_delay(struct ir3_compiler *compiler)
{
   if (compiler->gen >= 8)
      return 4;
   if (compiler->gen >= 7)
      return 5;
   return 6;
}

/* ========== END A8XX HELPER FUNCTIONS ========== */

ENDC;

#endif /* IR3_COMPILER_H_ */