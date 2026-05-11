/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_SHADER_H
#define RADV_SHADER_H

#include "util/mesa-blake3.h"
#include "util/shader_stats.h"
#include "util/u_math.h"
#include "vulkan/vulkan.h"
#include "ac_binary.h"
#include "ac_shader_util.h"
#include "amd_family.h"
#include "radv_constants.h"
#include "radv_shader_args.h"
#include "radv_shader_info.h"
#include "vk_nir_lower_descriptor_heaps.h"
#include "vk_pipeline_cache.h"

#include "nir/nir_shader_compiler_options.h"
#include "spirv/spirv_info.h"

#include "aco_shader_info.h"

struct radv_physical_device;
struct radv_device;
struct radv_pipeline;
struct radv_ray_tracing_pipeline;
struct radv_shader_args;
struct radv_shader_args;
struct radv_serialized_shader_arena_block;
struct vk_pipeline_robustness_state;
struct nir_parameter;
typedef struct nir_parameter nir_parameter;

#define RADV_GRAPHICS_STAGE_BITS                                                                                       \
   (VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT)
#define RADV_RT_STAGE_BITS                                                                                             \
   (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |           \
    VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR)

#define RADV_STAGE_MASK ((1 << MESA_VULKAN_SHADER_STAGES) - 1)

#define radv_foreach_stage(stage, stage_bits)                                                                          \
   for (mesa_shader_stage stage, __tmp = (mesa_shader_stage)((stage_bits) & RADV_STAGE_MASK);                          \
        stage = ffs(__tmp) - 1, __tmp; __tmp &= ~(1 << (stage)))

enum radv_nggc_settings {
   radv_nggc_none = 0,
   radv_nggc_front_face = 1 << 0,
   radv_nggc_back_face = 1 << 1,
   radv_nggc_face_is_ccw = 1 << 2,
   radv_nggc_small_primitives = 1 << 3,
};

enum radv_shader_query_state {
   radv_shader_query_none = 0,
   radv_shader_query_pipeline_stat = 1 << 0,
   radv_shader_query_prim_gen = 1 << 1,
   radv_shader_query_prim_xfb = 1 << 2,
};

enum radv_required_subgroup_size {
   RADV_REQUIRED_NONE = 0,
   RADV_REQUIRED_WAVE32 = 1,
   RADV_REQUIRED_WAVE64 = 2,
};

struct radv_shader_stage_key {
   uint8_t subgroup_required_size : 2; /* radv_required_subgroup_size */
   uint8_t subgroup_require_full : 1;  /* whether full subgroups are required */
   uint8_t subgroup_allow_varying : 1; /* whether subgroup size can differ from the api constant */

   uint8_t storage_robustness2 : 1;
   uint8_t uniform_robustness2 : 1;
   uint8_t vertex_robustness1 : 1;

   uint8_t optimisations_disabled : 1;
   uint8_t keep_statistic_info : 1;
   uint8_t keep_executable_info : 1;
   uint8_t view_index_from_device_index : 1;
   uint8_t descriptor_heap : 1;

   /* Shader version (up to 8) to force re-compilation when RADV_BUILD_ID_OVERRIDE is enabled. */
   uint8_t version : 3;

   /* Whether the mesh shader is used with a task shader. */
   uint8_t has_task_shader : 1;

   /* Whether the shader is used with indirect pipeline binds. */
   uint8_t indirect_bindable : 1;

   uint32_t reserved : 17;
};

struct radv_ps_epilog_key {
   uint32_t spi_shader_col_format;
   uint32_t spi_shader_z_format;

   /* Bitmasks, each bit represents one of the 8 MRTs. */
   uint8_t color_is_int8;
   uint8_t color_is_int10;
   uint8_t enable_mrt_output_nan_fixup;
   uint8_t no_signed_zero;

   uint32_t colors_needed;

   uint32_t colors_written;
   uint8_t color_map[MAX_RTS];
   bool mrt0_is_dual_src;
   bool export_depth;
   bool export_stencil;
   bool export_sample_mask;
   bool alpha_to_coverage_via_mrtz;
   bool alpha_to_one;

   uint16_t reserved;
};

struct radv_spirv_to_nir_options {
   uint32_t lower_view_index_to_zero : 1;
   uint32_t lower_view_index_to_device_index : 1;
};

struct radv_graphics_state_key {
   uint32_t lib_flags : 4; /* VkGraphicsPipelineLibraryFlagBitsEXT */

   uint32_t has_multiview_view_index : 1;
   uint32_t adjust_frag_coord_z : 1;
   uint32_t dynamic_rasterization_samples : 1;
   uint32_t dynamic_provoking_vtx_mode : 1;
   uint32_t dynamic_line_rast_mode : 1;
   uint32_t enable_remove_point_size : 1;
   uint32_t unknown_rast_prim : 1;
   uint32_t dcc_decompress_gfx11 : 1;
   uint32_t reserved : 12;

   struct {
      uint8_t topology;
   } ia;

   struct {
      uint32_t attributes_valid;
      uint32_t instance_rate_inputs;
      uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];
      uint8_t vertex_attribute_formats[MAX_VERTEX_ATTRIBS];
      uint32_t vertex_attribute_bindings[MAX_VERTEX_ATTRIBS];
      uint32_t vertex_attribute_offsets[MAX_VERTEX_ATTRIBS];
      uint32_t vertex_attribute_strides[MAX_VERTEX_ATTRIBS];
      uint8_t vertex_binding_align[MAX_VBS];
   } vi;

   struct {
      unsigned patch_control_points;
   } ts;

   struct {
      uint32_t provoking_vtx_last : 1;
      uint32_t cull_mode : 2;
   } rs;

   struct {
      bool sample_shading_enable;
      bool alpha_to_coverage_via_mrtz; /* GFX11+ */
      uint8_t rasterization_samples;
   } ms;

   struct vs {
      bool has_prolog;
   } vs;

   struct {
      struct radv_ps_epilog_key epilog;
      bool force_vrs_enabled;
      bool exports_mrtz_via_epilog;
      bool has_epilog;
   } ps;
};

struct radv_graphics_pipeline_key {
   struct radv_graphics_state_key gfx_state;

   struct radv_shader_stage_key stage_info[MESA_VULKAN_SHADER_STAGES];
};

struct radv_llvm_compiler_options {
   const struct ac_compiler_info *compiler_info;
   enum radeon_family family;
   uint32_t address32_hi;
   bool robust_buffer_access;
   bool dump_shader;
   bool dump_preoptir;
   bool record_ir;
   bool check_ir;
   bool wgp_mode;
};

#define SET_SGPR_FIELD(field, value) (((unsigned)(value) & field##__MASK) << field##__SHIFT)

#define TCS_OFFCHIP_LAYOUT_NUM_PATCHES__SHIFT           0
#define TCS_OFFCHIP_LAYOUT_NUM_PATCHES__MASK            0x7f
#define TCS_OFFCHIP_LAYOUT_PATCH_VERTICES_IN__SHIFT     7
#define TCS_OFFCHIP_LAYOUT_PATCH_VERTICES_IN__MASK      0x1f
#define TCS_OFFCHIP_LAYOUT_TCS_MEM_ATTRIB_STRIDE__SHIFT 12
#define TCS_OFFCHIP_LAYOUT_TCS_MEM_ATTRIB_STRIDE__MASK  0x1f
#define TCS_OFFCHIP_LAYOUT_NUM_LS_OUTPUTS__SHIFT        17
#define TCS_OFFCHIP_LAYOUT_NUM_LS_OUTPUTS__MASK         0x3f
#define TCS_OFFCHIP_LAYOUT_NUM_HS_OUTPUTS__SHIFT        23
#define TCS_OFFCHIP_LAYOUT_NUM_HS_OUTPUTS__MASK         0x3f
#define TCS_OFFCHIP_LAYOUT_PRIMITIVE_MODE__SHIFT        29
#define TCS_OFFCHIP_LAYOUT_PRIMITIVE_MODE__MASK         0x03
#define TCS_OFFCHIP_LAYOUT_TES_READS_TF__SHIFT          31
#define TCS_OFFCHIP_LAYOUT_TES_READS_TF__MASK           0x01

#define TES_STATE_NUM_PATCHES__SHIFT      0
#define TES_STATE_NUM_PATCHES__MASK       0xff
#define TES_STATE_TCS_VERTICES_OUT__SHIFT 8
#define TES_STATE_TCS_VERTICES_OUT__MASK  0xff
#define TES_STATE_NUM_TCS_OUTPUTS__SHIFT  16
#define TES_STATE_NUM_TCS_OUTPUTS__MASK   0xff

#define NGG_LDS_LAYOUT_GS_OUT_VERTEX_BASE__SHIFT 0
#define NGG_LDS_LAYOUT_GS_OUT_VERTEX_BASE__MASK  0xffff

#define NGG_STATE_NUM_VERTS_PER_PRIM__SHIFT 0
#define NGG_STATE_NUM_VERTS_PER_PRIM__MASK  0x7
#define NGG_STATE_PROVOKING_VTX__SHIFT      3
#define NGG_STATE_PROVOKING_VTX__MASK       0x7
#define NGG_STATE_QUERY__SHIFT              6
#define NGG_STATE_QUERY__MASK               0x7

#define PS_STATE_NUM_SAMPLES__SHIFT    0
#define PS_STATE_NUM_SAMPLES__MASK     0xf
#define PS_STATE_LINE_RAST_MODE__SHIFT 4
#define PS_STATE_LINE_RAST_MODE__MASK  0x3
#define PS_STATE_PS_ITER_MASK__SHIFT   6
#define PS_STATE_PS_ITER_MASK__MASK    0xffff
#define PS_STATE_RAST_PRIM__SHIFT      22
#define PS_STATE_RAST_PRIM__MASK       0x3

struct radv_shader_layout {
   uint32_t num_sets;

   struct {
      struct radv_descriptor_set_layout *layout;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t dynamic_offset_count;
   bool use_dynamic_descriptors;

   bool independent_sets;

   const VkShaderDescriptorSetAndBindingMappingInfoEXT *mapping;

   struct vk_sampler_state_array embedded_samplers;
};

struct radv_shader_stage {
   mesa_shader_stage stage;
   mesa_shader_stage next_stage;

   struct {
      const struct vk_object_base *object;
      const char *data;
      uint32_t size;
   } spirv;

   const char *entrypoint;
   const VkSpecializationInfo *spec_info;

   unsigned char shader_blake3[BLAKE3_KEY_LEN];

   nir_shader *nir;
   nir_shader *gs_copy_shader;
   nir_shader *internal_nir; /* meta shaders */

   struct radv_shader_info info;
   struct radv_shader_args args;
   struct radv_shader_stage_key key;

   VkPipelineCreationFeedback feedback;

   struct radv_shader_layout layout;
};

static inline bool
radv_is_last_vgt_stage(const struct radv_shader_stage *stage)
{
   return (stage->info.stage == MESA_SHADER_VERTEX || stage->info.stage == MESA_SHADER_TESS_EVAL ||
           stage->info.stage == MESA_SHADER_GEOMETRY || stage->info.stage == MESA_SHADER_MESH) &&
          (stage->info.next_stage == MESA_SHADER_FRAGMENT || stage->info.next_stage == MESA_SHADER_NONE);
}

struct radv_vs_prolog_key {
   /* All the fields are pre-masked with BITFIELD_MASK(num_attributes).
    * Some of the fields are pre-masked by other conditions. See lookup_vs_prolog.
    */
   uint32_t instance_rate_inputs;
   uint32_t nontrivial_divisors;
   uint32_t zero_divisors;
   uint32_t post_shuffle;
   /* Having two separate fields instead of a single uint64_t makes it easier to remove attributes
    * using bitwise arithmetic.
    */
   uint32_t alpha_adjust_lo;
   uint32_t alpha_adjust_hi;
   uint8_t formats[MAX_VERTEX_ATTRIBS];
   unsigned num_attributes;
   uint32_t misaligned_mask;
   uint32_t unaligned_mask;
   bool as_ls;
   bool is_ngg;
   bool wave32;
   mesa_shader_stage next_stage;
};

enum radv_shader_binary_type { RADV_BINARY_TYPE_LEGACY, RADV_BINARY_TYPE_RTLD };

struct radv_shader_binary {
   uint32_t type; /* enum radv_shader_binary_type */

   struct ac_shader_config config;
   struct radv_shader_info info;

   /* Self-referential size so we avoid consistency issues. */
   uint32_t total_size;
};

struct radv_shader_binary_legacy {
   struct radv_shader_binary base;
   uint32_t code_size;
   uint32_t exec_size;
   uint32_t ir_size;
   uint32_t disasm_size;
   uint32_t stats_size;
   uint32_t debug_info_size;

   /* data has size of stats_size + code_size + ir_size + disasm_size + 2,
    * where the +2 is for 0 of the ir strings. */
   uint8_t data[0];
};
static_assert(sizeof(struct radv_shader_binary_legacy) == offsetof(struct radv_shader_binary_legacy, data),
              "Unexpected padding");

struct radv_shader_binary_rtld {
   struct radv_shader_binary base;
   unsigned elf_size;
   unsigned llvm_ir_size;
   uint8_t data[0];
};

struct radv_shader_part_binary {
   struct {
      uint32_t spi_shader_col_format;
      uint32_t cb_shader_mask;
      uint32_t spi_shader_z_format;
   } info;

   uint8_t num_sgprs;
   uint8_t num_vgprs;
   unsigned code_size;
   unsigned disasm_size;

   /* Self-referential size so we avoid consistency issues. */
   uint32_t total_size;

   uint8_t data[0];
};

enum radv_shader_arena_type { RADV_SHADER_ARENA_DEFAULT, RADV_SHADER_ARENA_REPLAYABLE, RADV_SHADER_ARENA_REPLAYED };

struct radv_shader_arena {
   struct list_head list;
   struct list_head entries;
   uint32_t size;
   struct radeon_winsys_bo *bo;
   char *ptr;
   enum radv_shader_arena_type type;
};

union radv_shader_arena_block {
   struct list_head pool;
   struct {
      /* List of blocks in the arena, sorted by address. */
      struct list_head list;
      /* For holes, a list_head for the free-list. For allocations, freelist.prev=NULL and
       * freelist.next is a pointer associated with the allocation.
       */
      struct list_head freelist;
      struct radv_shader_arena *arena;
      uint32_t offset;
      uint32_t size;
   };
};

struct radv_shader_free_list {
   uint8_t size_mask;
   struct list_head free_lists[RADV_SHADER_ALLOC_NUM_FREE_LISTS];
};

struct radv_serialized_shader_arena_block {
   uint32_t offset;
   uint32_t size;
   uint64_t arena_va;
   uint32_t arena_size;
};

struct radv_shader_debug_info {
   bool dump_shader;
   uint32_t stages; /* mesa_shader_stage */
   char *spirv;
   uint32_t spirv_size;
   char *nir_string;
   char *disasm_string;
   char *ir_string;
   char *args_string;
   struct amd_stats *statistics;
   struct ac_shader_debug_info *debug_info;
   uint32_t debug_info_count;
};

struct radv_shader {
   struct vk_pipeline_cache_object base;

   simple_mtx_t replay_mtx;
   bool has_replay_alloc;

   struct radeon_winsys_bo *bo;
   union radv_shader_arena_block *alloc;
   uint64_t va;

   uint64_t upload_seq;

   struct ac_shader_config config;
   uint32_t code_size;
   uint32_t exec_size;
   struct radv_shader_info info;
   struct radv_shader_regs regs;
   uint32_t max_waves;

   blake3_hash hash;
   void *code;

   struct radv_shader_debug_info dbg;
};

struct radv_shader_part {
   uint32_t ref_count;

   union {
      struct radv_vs_prolog_key vs;
      struct radv_ps_epilog_key ps;
   } key;

   uint64_t va;

   struct radeon_winsys_bo *bo;
   union radv_shader_arena_block *alloc;
   uint32_t code_size;
   uint32_t rsrc1;
   bool nontrivial_divisors;
   uint32_t spi_shader_col_format;
   uint32_t cb_shader_mask;
   uint32_t spi_shader_z_format;
   uint64_t upload_seq;

   /* debug only */
   char *disasm_string;
};

struct radv_shader_part_cache_ops {
   uint32_t (*hash)(const void *key);
   bool (*equals)(const void *a, const void *b);
   struct radv_shader_part *(*create)(struct radv_device *device, const void *key);
};

struct radv_shader_part_cache {
   simple_mtx_t lock;
   struct radv_shader_part_cache_ops *ops;
   struct set entries;
};

struct radv_shader_dma_submission {
   struct list_head list;

   struct radv_cmd_stream *cs;
   struct radeon_winsys_bo *bo;
   uint64_t bo_size;
   char *ptr;

   /* The semaphore value to wait for before reusing this submission. */
   uint64_t seq;
};

struct radv_compiler_info {
   /* Hardware info */
   const struct ac_compiler_info *ac;

   struct {
      uint32_t address32_hi;
      uint32_t rbplus_allowed : 1;
      uint32_t address_prt_wa_control_bit : 8;
      uint32_t padding : 23;
   } hw;

   /* Misc values included as part of the cache key */
   struct {
      /* Shader features */
      uint32_t use_llvm : 1;
      uint32_t use_ngg : 1;
      uint32_t use_ngg_culling : 1;
      uint32_t nggc_max_ps_params : 4;
      uint32_t no_ngg_gs : 1;
      uint32_t load_grid_size_from_user_sgpr : 1;
      uint32_t emulate_ngg_gs_query_pipeline_stat : 1;
      uint32_t primitives_generated_query : 1;
      uint32_t mesh_shader_queries : 1;
      uint32_t image_2d_view_of_3d : 1;
      uint32_t use_fmask : 1;
      uint32_t robust_buffer_access : 1; /* Only used by LLVM. */
      uint32_t mitigate_smem_oob : 1;
      uint32_t mitigate_smem_with_null_prt : 1;
      uint32_t bvh8 : 1;
      uint32_t no_rt : 1;
      uint32_t rt_cps : 1;
      uint32_t clear_lds : 1;
      uint32_t disable_aniso_single_level : 1;
      uint32_t disable_shrink_image_store : 1;
      uint32_t disable_sinking_load_input_fs : 1;
      uint32_t disable_trunc_coord : 1;
      uint32_t enable_mrt_output_nan_fixup : 1;
      uint32_t emulate_rt : 1;
      uint32_t invariant_geom : 1;
      uint32_t split_fma : 1;
      uint32_t ssbo_non_uniform : 1;
      uint32_t tex_non_uniform : 1;
      uint32_t lower_terminate_to_discard : 1;
      uint32_t no_implicit_varying_subgroup_size : 1;
      uint32_t padding : 31;

      int32_t force_aniso;

      uint32_t family; /* CHIP_UNKNOWN unless LLVM is used. */

      /* Wave/subgroup sizes */
      uint8_t ge_wave_size;
      uint8_t ps_wave_size;
      uint8_t cs_wave_size;
      uint8_t rt_wave_size;
   } key;

   /* Debug/tracing */
   struct {
      bool dump_spirv;
      bool dump_backend_ir;
      bool dump_preopt_ir;
      bool dump_nir;
      bool dump_asm;
      bool dump_meta_shaders;
      bool dump_shader_stats;
      bool nir_debug_info;
      VkShaderStageFlags dump_shaders;
      bool check_ir;
      bool printf_enabled;
      bool trap_enabled;
      uint64_t trap_excp_flags;
      struct vk_debug_report *debug_report;
      struct radv_debug_nir *debug_nir;
      simple_mtx_t *shader_dump_mtx;
      bool keep_shader_info;
      bool capture_shaders;
      bool capture_shader_stats;
      uint32_t family; /* For ACO disassembly only */
   } debug;

   struct radv_rra_trace_data *rra_trace;

   /* Cache */
   bool cache_disabled;
   bool enable_nir_cache;
   struct vk_pipeline_cache *mem_cache;
   uint8_t override_compute_shader_version;
   uint8_t override_graphics_shader_version;
   uint8_t override_ray_tracing_shader_version;

   /* Descriptors */
   uint8_t sampled_image_desc_size;
   uint8_t combined_image_sampler_desc_size;
   uint8_t combined_image_sampler_offset;
   uint32_t sampler_descriptor_size;
   uint32_t sampler_descriptor_alignment;
   uint32_t image_descriptor_size;
   uint32_t image_descriptor_alignment;
   uint32_t buffer_descriptor_size;
   uint32_t buffer_descriptor_alignment;

   /* Shader features, included as part of the pipeline key */
   const struct vk_pipeline_robustness_state *device_robustness_state;
   bool smooth_lines;
   bool force_vrs_enabled;

   /* Wave/subgroup sizes */
   uint32_t subgroup_size;
   uint32_t min_subgroup_size;
   uint32_t max_subgroup_size;

   /* NIR/SPIR-V */
   struct spirv_capabilities spirv_caps;
   nir_shader_compiler_options nir_options[MESA_VULKAN_SHADER_STAGES];
};
struct radv_shader_stage;

void radv_optimize_nir(struct nir_shader *shader, bool optimize_conservatively);
void radv_optimize_nir_algebraic_early(nir_shader *shader);
void radv_optimize_nir_algebraic_late(nir_shader *shader);
void radv_optimize_nir_algebraic(nir_shader *shader, bool opt_offsets, bool opt_mqsad,
                                 enum amd_gfx_level gfx_level);


struct radv_shader_stage;

nir_shader *radv_shader_spirv_to_nir(const struct radv_compiler_info *compiler_info, struct radv_shader_stage *stage,
                                     const struct radv_spirv_to_nir_options *options, bool is_internal);

void radv_init_shader_arenas(struct radv_device *device);
void radv_destroy_shader_arenas(struct radv_device *device);
VkResult radv_init_shader_upload_queue(struct radv_device *device);
void radv_destroy_shader_upload_queue(struct radv_device *device);

struct radv_shader_args;

VkResult radv_parse_binary_debug_info(const struct radv_compiler_info *compiler_info, const struct radv_shader_binary *binary,
                                      struct radv_shader_debug_info *dbg);

VkResult radv_shader_create_uncached(struct radv_device *device, const struct radv_shader_binary *binary,
                                     bool replayable, struct radv_serialized_shader_arena_block *replay_block,
                                     struct radv_shader_debug_info *dbg, struct radv_shader **out_shader);

struct radv_shader_binary *radv_shader_nir_to_asm(const struct radv_compiler_info *compiler_info,
                                                  struct radv_shader_stage *pl_stage, struct nir_shader *const *shaders,
                                                  int shader_count, const struct radv_graphics_state_key *gfx_state,
                                                  bool keep_shader_info, bool keep_statistic_info);

void radv_shader_dump_asm(const struct radv_compiler_info *compiler_info, const struct radv_shader_debug_info *debug,
                          const struct radv_shader_info *info);

char *radv_dump_nir_shaders(const struct radv_compiler_info *compiler_info, struct nir_shader *const *shaders, int shader_count);

VkResult radv_shader_wait_for_upload(struct radv_device *device, uint64_t seq);

struct radv_shader_dma_submission *radv_shader_dma_pop_submission(struct radv_device *device);

void radv_shader_dma_push_submission(struct radv_device *device, struct radv_shader_dma_submission *submission,
                                     uint64_t seq);

struct radv_shader_dma_submission *
radv_shader_dma_get_submission(struct radv_device *device, struct radeon_winsys_bo *bo, uint64_t va, uint64_t size);

bool radv_shader_dma_submit(struct radv_device *device, struct radv_shader_dma_submission *submission,
                            uint64_t *upload_seq_out);

union radv_shader_arena_block *radv_alloc_shader_memory(struct radv_device *device, uint32_t size, bool replayable,
                                                        void *ptr);

union radv_shader_arena_block *radv_replay_shader_arena_block(struct radv_device *device,
                                                              const struct radv_serialized_shader_arena_block *src,
                                                              void *ptr);

void radv_free_shader_memory(struct radv_device *device, union radv_shader_arena_block *alloc);

struct radv_shader *radv_create_trap_handler_shader(struct radv_device *device);

struct radv_shader *radv_compile_rt_prolog(struct radv_device *device, struct radv_shader_stage *stage,
                                           struct radv_shader_debug_info *debug);

struct radv_shader_part *radv_shader_part_create(struct radv_device *device, struct radv_shader_part_binary *binary,
                                                 unsigned wave_size);

struct radv_shader_part *radv_create_vs_prolog(struct radv_device *device, const struct radv_vs_prolog_key *key);

struct radv_shader_part *radv_create_ps_epilog(struct radv_device *device, const struct radv_ps_epilog_key *key,
                                               struct radv_shader_part_binary **binary_out);

void radv_shader_part_destroy(struct radv_device *device, struct radv_shader_part *shader_part);

void radv_shader_part_cache_init(struct radv_shader_part_cache *cache, struct radv_shader_part_cache_ops *ops);
void radv_shader_part_cache_finish(struct radv_device *device, struct radv_shader_part_cache *cache);
struct radv_shader_part *radv_shader_part_cache_get(struct radv_device *device, struct radv_shader_part_cache *cache,
                                                    struct set *local_entries, const void *key);

uint64_t radv_shader_get_va(const struct radv_shader *shader);
struct radv_shader *radv_find_shader(struct radv_device *device, uint64_t pc);

unsigned radv_get_max_waves(const struct radv_device *device, const struct ac_shader_config *conf,
                            const struct radv_shader_info *info);

unsigned radv_get_max_scratch_waves(const struct radv_device *device, struct radv_shader *shader);

const char *radv_get_shader_name(const struct radv_shader_info *info, mesa_shader_stage stage);

unsigned radv_compute_spi_ps_input(enum amd_gfx_level gfx_level, const struct radv_graphics_state_key *gfx_state,
                                   const struct radv_shader_info *info);

bool radv_is_traversal_shader(nir_shader *nir);

bool radv_can_dump_shader(const struct radv_compiler_info *compiler_info, nir_shader *nir);

bool radv_can_dump_shader_stats(const struct radv_compiler_info *compiler_info, nir_shader *nir);

VkResult radv_dump_shader_stats(struct radv_device *device, struct radv_pipeline *pipeline, struct radv_shader *shader,
                                FILE *output);

/* Returns true on success and false on failure */
bool radv_shader_reupload(struct radv_device *device, struct radv_shader *shader);

extern const struct vk_pipeline_cache_object_ops radv_shader_ops;

static inline struct radv_shader *
radv_shader_ref(struct radv_shader *shader)
{
   vk_pipeline_cache_object_ref(&shader->base);
   return shader;
}

static inline void
radv_shader_unref(struct radv_device *device, struct radv_shader *shader)
{
   vk_pipeline_cache_object_unref((struct vk_device *)device, &shader->base);
}

static inline struct radv_shader_part *
radv_shader_part_ref(struct radv_shader_part *shader_part)
{
   assert(shader_part && shader_part->ref_count >= 1);
   p_atomic_inc(&shader_part->ref_count);
   return shader_part;
}

static inline void
radv_shader_part_unref(struct radv_device *device, struct radv_shader_part *shader_part)
{
   assert(shader_part && shader_part->ref_count >= 1);
   if (p_atomic_dec_zero(&shader_part->ref_count))
      radv_shader_part_destroy(device, shader_part);
}

static inline struct radv_shader_part *
radv_shader_part_from_cache_entry(const void *key)
{
   return container_of(key, struct radv_shader_part, key);
}

static inline unsigned
get_tcs_input_vertex_stride(unsigned tcs_num_inputs)
{
   unsigned stride = tcs_num_inputs * 16;

   /* Add 1 dword to reduce LDS bank conflicts. */
   if (stride)
      stride += 4;

   return stride;
}

void radv_get_tess_wg_info(const struct radv_compiler_info *compiler_info, const ac_nir_tess_io_info *io_info,
                           unsigned tcs_vertices_out, unsigned tcs_num_input_vertices, unsigned tcs_num_lds_inputs,
                           unsigned *num_patches_per_wg, unsigned *lds_size);

void radv_lower_ngg(const struct radv_compiler_info *compiler_info, struct radv_shader_stage *ngg_stage,
                    const struct radv_graphics_state_key *gfx_state);

bool radv_consider_culling(const struct radv_compiler_info *compiler_info, struct nir_shader *nir,
                           uint64_t ps_inputs_read, unsigned num_vertices_per_primitive,
                           const struct radv_shader_info *info);

void radv_get_nir_options(struct radv_compiler_info *compiler_info);

enum radv_rt_lowering_mode {
   RADV_RT_LOWERING_MODE_MONOLITHIC,
   RADV_RT_LOWERING_MODE_CPS,
   RADV_RT_LOWERING_MODE_FUNCTION_CALLS,
};

struct radv_shader_layout;
enum radv_pipeline_type;

void radv_shader_combine_cfg_vs_tcs(const struct radv_shader *vs, const struct radv_shader *tcs, uint32_t *rsrc1_out,
                                    uint32_t *rsrc2_out);

void radv_shader_combine_cfg_vs_gs(const struct radv_device *device, const struct radv_shader *vs,
                                   const struct radv_shader *gs, uint32_t *rsrc1_out, uint32_t *rsrc2_out);

void radv_shader_combine_cfg_tes_gs(const struct radv_device *device, const struct radv_shader *tes,
                                    const struct radv_shader *gs, uint32_t *rsrc1_out, uint32_t *rsrc2_out);

const struct radv_userdata_info *radv_get_user_sgpr_info(const struct radv_shader *shader, int idx);

uint32_t radv_get_user_sgpr_loc(const struct radv_shader *shader, int idx);

uint32_t radv_get_user_sgpr(const struct radv_shader *shader, int idx);

static inline bool
radv_shader_need_indirect_descriptors(const struct radv_shader *shader)
{
   const struct radv_userdata_info *loc = radv_get_user_sgpr_info(shader, AC_UD_INDIRECT_DESCRIPTORS);
   return loc->sgpr_idx != -1;
}

static inline bool
radv_shader_need_push_constants_upload(const struct radv_shader *shader)
{
   const struct radv_userdata_info *loc = radv_get_user_sgpr_info(shader, AC_UD_PUSH_CONSTANTS);
   return loc->sgpr_idx != -1;
}

static inline bool
radv_shader_need_dynamic_descriptors_offset_addr(const struct radv_shader *shader)
{
   const struct radv_userdata_info *loc = radv_get_user_sgpr_info(shader, AC_UD_DYNAMIC_DESCRIPTORS_OFFSET_ADDR);
   return loc->sgpr_idx != -1;
}

void radv_precompute_registers_hw_gs(struct radv_device *device, const struct radv_shader_info *es_info,
                                     struct radv_shader *shader);

void radv_precompute_registers_hw_ngg(struct radv_device *device, struct radv_shader *shader);

void radv_set_stage_key_robustness(const struct vk_pipeline_robustness_state *rs, mesa_shader_stage stage,
                                   struct radv_shader_stage_key *key);

#endif /* RADV_SHADER_H */
