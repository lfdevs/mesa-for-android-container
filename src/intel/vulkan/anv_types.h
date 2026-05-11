/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define ANV_GRAPHICS_STAGE_BITS  (VK_SHADER_STAGE_ALL_GRAPHICS | \
                                  VK_SHADER_STAGE_MESH_BIT_EXT | \
                                  VK_SHADER_STAGE_TASK_BIT_EXT)

#define ANV_RT_STAGE_BITS (VK_SHADER_STAGE_RAYGEN_BIT_KHR |             \
                           VK_SHADER_STAGE_ANY_HIT_BIT_KHR |            \
                           VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |        \
                           VK_SHADER_STAGE_MISS_BIT_KHR |               \
                           VK_SHADER_STAGE_INTERSECTION_BIT_KHR |       \
                           VK_SHADER_STAGE_CALLABLE_BIT_KHR)

#define ANV_VK_STAGE_MASK (ANV_GRAPHICS_STAGE_BITS |    \
                           ANV_RT_STAGE_BITS |          \
                           VK_SHADER_STAGE_COMPUTE_BIT)

/* 3DSTATE_VERTEX_ELEMENTS supports up to 34 VEs, but our backend compiler
 * only supports the push model of VS inputs, and we only have 128 GRFs,
 * minus the g0 and g1 payload, which gives us a maximum of 31 VEs.  Plus,
 * we use two of them for SGVs.
 */
#define MAX_VES         (31 - 2)

#define MAX_XFB_BUFFERS  4
#define MAX_XFB_STREAMS  4
#define MAX_SETS        32
#define MAX_RTS          8
#define MAX_VIEWPORTS   16
#define MAX_SCISSORS    16
#define MAX_PUSH_CONSTANTS_SIZE 256  /* Minimum requirement as of Vulkan 1.4 */
#define MAX_DYNAMIC_BUFFERS 16
#define MAX_PUSH_DESCRIPTORS 32 /* Minimum requirement */
#define MAX_INLINE_UNIFORM_BLOCK_SIZE 4096
#define MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS 32
#define MAX_EMBEDDED_SAMPLERS 2048
#define MAX_CUSTOM_BORDER_COLORS 4096
#define MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS 256
/* Different SKUs have different maximum values. Make things more consistent
 * across them, by setting a maximum of 48KiB because it's what some of the
 * other vendors report as maximum and also above the required limit from DX
 * (16KiB on "downlevel hardware", 32KiB otherwise).
 */
#define MAX_SLM_SIZE (48 * 1024)
/* We need 16 for UBO block reads to work and 32 for push UBOs. However, we
 * use 64 here to avoid cache issues. This could most likely bring it back to
 * 32 if we had different virtual addresses for the different views on a given
 * GEM object.
 */
#define ANV_UBO_ALIGNMENT 64
#define ANV_UBO_BOUNDS_CHECK_ALIGNMENT 16
#define ANV_SSBO_ALIGNMENT 4
#define ANV_SSBO_BOUNDS_CHECK_ALIGNMENT 4
#define MAX_VIEWS_FOR_PRIMITIVE_REPLICATION 16
#define MAX_SAMPLE_LOCATIONS 16

/* RENDER_SURFACE_STATE is a bit smaller (48b) but since it is aligned to 64
 * and we can't put anything else there we use 64b.
 */
#define ANV_SURFACE_STATE_SIZE (64)

/* From the Skylake PRM Vol. 7 "Binding Table Surface State Model":
 *
 *    "The surface state model is used when a Binding Table Index (specified
 *    in the message descriptor) of less than 240 is specified. In this model,
 *    the Binding Table Index is used to index into the binding table, and the
 *    binding table entry contains a pointer to the SURFACE_STATE."
 *
 * Binding table values above 240 are used for various things in the hardware
 * such as stateless, stateless with incoherent cache, SLM, and bindless.
 */
#define MAX_BINDING_TABLE_SIZE 240

#define HW_MAX_VBS 33

 /* 3DSTATE_VERTEX_BUFFER supports 33 VBs, but these limits are applied on Gen9
  * graphics, where 2 VBs are reserved for base & drawid SGVs.
  */
#define ANV_SVGS_VB_INDEX   (HW_MAX_VBS - 2)
#define ANV_DRAWID_VB_INDEX (ANV_SVGS_VB_INDEX + 1)

#define ANV_GRAPHICS_SHADER_STAGE_COUNT (MESA_SHADER_MESH + 1)
#define ANV_RT_SHADER_STAGE_COUNT       (MESA_SHADER_CALLABLE - MESA_SHADER_RAYGEN + 1)

/* RENDER_SURFACE_STATE is a bit smaller (48b) but since it is aligned to 64
 * and we can't put anything else there we use 64b.
 */
#define ANV_SURFACE_STATE_SIZE (64)
#define ANV_SAMPLER_STATE_SIZE (32)

/* For gfx12 we set the streamout buffers using 4 separate commands
 * (3DSTATE_SO_BUFFER_INDEX_*) instead of 3DSTATE_SO_BUFFER. However the layout
 * of the 3DSTATE_SO_BUFFER_INDEX_* commands is identical to that of
 * 3DSTATE_SO_BUFFER apart from the SOBufferIndex field, so for now we use the
 * 3DSTATE_SO_BUFFER command, but change the 3DCommandSubOpcode.
 * SO_BUFFER_INDEX_0_CMD is actually the 3DCommandSubOpcode for
 * 3DSTATE_SO_BUFFER_INDEX_0.
 */
#define SO_BUFFER_INDEX_0_CMD 0x60

struct anv_push_constants {
   /** Push constant data provided by the client through vkPushConstants */
   uint8_t client_data[MAX_PUSH_CONSTANTS_SIZE];

#define ANV_DESCRIPTOR_SET_DYNAMIC_INDEX_MASK ((uint32_t)ANV_UBO_ALIGNMENT - 1)
#define ANV_DESCRIPTOR_SET_OFFSET_MASK        (~(uint32_t)(ANV_UBO_ALIGNMENT - 1))

   /**
    * Base offsets for descriptor sets from
    *
    * The offset has different meaning depending on a number of factors :
    *
    *    - with descriptor sets (direct or indirect), this relative
    *      pdevice->va.descriptor_pool
    *
    *    - with descriptor buffers on DG2+, relative
    *      device->va.descriptor_buffer_pool
    *
    *    - with descriptor buffers prior to DG2, relative the programmed value
    *      in STATE_BASE_ADDRESS::BindlessSurfaceStateBaseAddress
    */
   uint32_t desc_surface_offsets[MAX_SETS];

   /**
    * Base offsets for descriptor sets from
    */
   uint32_t desc_sampler_offsets[MAX_SETS];

   /** Dynamic offsets for dynamic UBOs and SSBOs */
   uint32_t dynamic_offsets[MAX_DYNAMIC_BUFFERS];

   union {
      /** Surface buffer base offset
       *
       * Only used prior to DG2 with descriptor buffers.
       *
       * (surfaces_base_offset + desc_offsets[set_index]) is relative to
       * device->va.descriptor_buffer_pool and can be used to compute a 64bit
       * address to the descriptor buffer (using load_desc_set_address_intel).
       */
      uint32_t surfaces_base_offset;

      /** Ray query globals
       *
       * Pointer to a couple of RT_DISPATCH_GLOBALS structures (see
       * genX(cmd_buffer_ray_query_globals))
       */
      uint64_t ray_query_globals;
   };

   union {
      struct {
         /** Dynamic MSAA value */
         uint32_t fs_config;

         /** Dynamic TCS/TES configuration */
         uint32_t tess_config;

         /** Robust access pushed registers. */
         uint8_t push_reg_mask[MESA_SHADER_STAGES][4];

         /** Wa_18019110168
          * bits  4:0 : provoking vertex value
          * bits 31:5 : per primitive table remapping offset
          */
#define ANV_WA_18019110168_PROVOKING_VERTEX_MASK                 ((1u << 5) - 1)
#define ANV_WA_18019110168_PER_PRIMITIVE_REMAP_TABLE_OFFSET_MASK (~ANV_WA_18019110168_PROVOKING_VERTEX_MASK)
         uint32_t wa_18019110168;
      } gfx;

      struct {
         /** Base workgroup ID
          *
          * Used for vkCmdDispatchBase.
          */
         uint32_t base_workgroup[3];

         /** gl_NumWorkgroups */
         uint32_t num_workgroups[3];

         uint32_t unaligned_invocations_x;

         /** Subgroup ID
          *
          * This is never set by software but is implicitly filled out when
          * uploading the push constants for compute shaders.
          *
          * This *MUST* be the last field of the anv_push_constants structure.
          */
         uint32_t subgroup_id;
      } cs;
   };
};

#define ANV_DRIVER_PUSH_CONSTANTS_SIZE (sizeof(struct anv_push_constants) - MAX_PUSH_CONSTANTS_SIZE)

#define ANV_INLINE_DWORD_PUSH_ADDRESS_LDW      (UINT8_MAX - 0)
#define ANV_INLINE_DWORD_PUSH_ADDRESS_UDW      (UINT8_MAX - 1)

/* Location of the user visible part of the dynamic state heap (1GiB) */
#define ANV_DYNAMIC_VISIBLE_HEAP_OFFSET (1024 * 1024 * 1024)

/**
 * Stage enum for generated commands
 */
enum anv_dgc_stage {
   ANV_DGC_STAGE_VERTEX = 0,
   ANV_DGC_STAGE_TESS_CTRL,
   ANV_DGC_STAGE_TESS_EVAL,
   ANV_DGC_STAGE_GEOMETRY,
   ANV_DGC_STAGE_FRAGMENT,
   ANV_DGC_STAGE_TASK,
   ANV_DGC_STAGE_MESH,

   ANV_DGC_STAGE_COMPUTE,
   ANV_DGC_STAGE_RT,

   ANV_DGC_STAGES,
};

#define ANV_DGC_N_GFX_STAGES (ANV_DGC_STAGE_MESH + 1)

enum anv_dgc_draw_type {
   ANV_DGC_DRAW_TYPE_SEQUENTIAL,
   ANV_DGC_DRAW_TYPE_INDEXED,
   ANV_DGC_DRAW_TYPE_MESH,
};

#define ANV_DGC_RT_GLOBAL_DISPATCH_SIZE (128)

enum anv_dgc_push_constant_flags {
   ANV_DGC_PUSH_CONSTANTS_CMD_ACTIVE   = BITFIELD_BIT(0),
};

/**
 * This structure represents the indirect data layout (in
 * VkGeneratedCommandsInfoEXT::indirectAddress) for push constants
 */
struct anv_dgc_push_layout {
   struct anv_dgc_push_entry {
      /* Location of the data to copy in the indirect buffer */
      uint32_t seq_offset;

      /* Location where to write the data in anv_push_constants::client_data[]
       */
      uint16_t push_offset;

      /* Size of the data to copy */
      uint16_t size;
   } entries[32];

   uint8_t flags; /* enum anv_dgc_push_constant_flags */

   uint8_t num_entries;
   uint8_t mocs;

   /* Whether the sequence ID is active and at what offset we should write it
    * in the push constant data
    */
   uint16_t seq_id_active;
   uint16_t seq_id_offset;

   /* Offset of the push constant commands in the preprocessed buffer.
    */
   uint16_t cmd_offset;
   uint16_t cmd_size;

   /* Offset of the data in the indirect buffer, relative to
    * VkGeneratedCommandsInfoEXT::indirectAddress
    */
   uint16_t data_offset;
};

/**
 * This structure represents both the data layout (in
 * VkGeneratedCommandsInfoEXT::indirectAddress) and the command layout in the
 * preprocess buffer (in VkGeneratedCommandsInfoEXT::preprocessAddress) for
 * graphics commands
 */
struct anv_dgc_gfx_layout {
   struct anv_dgc_index_buffer {
      uint16_t cmd_offset; /* Offset of 3DSTATE_INDEX_BUFFER */
      uint16_t cmd_size;
      uint16_t seq_offset; /* Offset of VkBindIndexBufferIndirectCommandEXT */
      uint16_t mocs;
      uint32_t u32_value;
      uint32_t u16_value;
      uint32_t u8_value;
   } index_buffer;

   struct {
      struct anv_dgc_vertex_buffer {
         uint16_t seq_offset; /* Offset of VkBindVertexBufferIndirectCommandEXT */
         uint16_t binding;
      } buffers[31];
      uint16_t n_buffers;
      uint16_t mocs;
      uint16_t cmd_offset; /* Offset of 3DSTATE_VERTEX_BUFFERS */
      uint16_t cmd_size;
   } vertex_buffers;

   struct anv_dgc_push_layout push_constants;

   struct {
      uint16_t final_cmds_offset;
      uint16_t final_cmds_size;
      uint32_t active;
   } indirect_set;

   struct {
      uint16_t cmd_offset; /* Offset of 3DPRIMITIVE/3DMESH_3D */
      uint16_t cmd_size;
      uint16_t draw_type; /* anv_dgc_gfx_draw_type */
      uint16_t seq_offset; /* Offset of :
                            *    - VkDrawIndirectCommand
                            *    - VkDrawIndexedIndirectCommand
                            *    - VkDrawMeshTasksIndirectCommandEXT
                            */
   } draw;
};

/**
 * This structure represents both the data layout (in
 * VkGeneratedCommandsInfoEXT::indirectAddress) and the command layout in the
 * preprocess buffer (in VkGeneratedCommandsInfoEXT::preprocessAddress) for
 * compute commands
 */
struct anv_dgc_cs_layout {
   struct anv_dgc_push_layout push_constants;

   /* Location of the indirect execution set index */
   struct {
      uint32_t seq_offset;
      uint16_t data_offset;
      uint16_t active;
   } indirect_set;

   /* Offset of VkDispatchIndirectCommand */
   struct {
      uint32_t seq_offset;
      uint16_t cmd_offset;
      uint16_t pad;
   } dispatch;
};

enum anv_dgc_push_slot_type {
   ANV_DGC_PUSH_SLOT_TYPE_PUSH_CONSTANTS,
   ANV_DGC_PUSH_SLOT_TYPE_OTHER,
};

enum anv_dgc_draw_params {
   ANV_DGC_DRAW_PARAM_BASE_INSTANCE_VERTEX = BITFIELD_BIT(0),
   ANV_DGC_DRAW_PARAM_DRAW_ID              = BITFIELD_BIT(1),
};

/**
 * This structure holds prepacked HW instructions for a set of graphics
 * shaders forming a pipeline . It is part of the command buffer temporary
 * memory.
 */
struct anv_dgc_gfx_descriptor {
   /* Fully packed instructions ready to be copied directly into the
    * preprocess buffer (for workarounds)
    */
   uint32_t final_commands[20];
   uint32_t final_commands_size;

   union {
      /* Gfx12.5 only */
      uint32_t wa_18019110168_remapping_table_offset;
      /* Gfx9 only */
      enum anv_dgc_draw_params draw_params;
   };

   struct {
      struct anv_dgc_push_stage_state {
         union {
            struct {
               struct anv_dgc_push_stage_slot {
                  uint16_t push_data_offset;
                  uint16_t push_data_size;
                  uint32_t type; /* enum anv_dgc_push_slot_type */
               } slots[4];
               uint32_t n_slots;
            } legacy;
            struct anv_dgc_push_bindless_stage {
               uint16_t push_data_offset;
               uint16_t inline_dwords_count;
               uint8_t  inline_dwords[8];
            } bindless;
         };
      } stages[ANV_DGC_N_GFX_STAGES];
      uint32_t active_stages; /* Bitfield of anv_dgc_command_stage */
   } push_constants;
};

/**
 * This structure holds information about the graphics state for generation.
 */
struct anv_dgc_gfx_state {
   struct anv_dgc_gfx_layout layout;

   struct anv_dgc_gfx_descriptor descriptor;

   struct {
      uint64_t addresses[4];
   } push_constants;

   struct {
      uint16_t instance_multiplier;
      uint32_t flags; /* ANV_GENERATED_FLAG_* */
   } draw;
};

/**
 * This structure holds prepacked HW instructions for a compute shader. It is
 * either located in the memory associated with VkIndirectExecutionSetEXT or
 * part of the command buffer temporary memory if indirect execution set is
 * not used.
 */
struct anv_dgc_cs_descriptor {
   union {
      struct {
         uint32_t compute_walker[40];
         uint32_t inline_dwords_count;
         uint8_t  inline_dwords[8];
      } gfx125;

      struct {
         /* Needs to be the first field because
          * MEDIA_INTERFACE_DESCRIPTOR_LOAD::InterfaceDescriptorDataStartAddress
          * needs 64B alignment.
          */
         uint32_t interface_descriptor_data[8];
         uint32_t gpgpu_walker[15];
         uint32_t media_vfe_state[9];

         uint32_t n_threads;
         uint16_t cross_thread_push_size;
         uint8_t per_thread_push_size;
         uint8_t subgroup_id_offset;
      } gfx9;
   };

   uint32_t right_mask;
   uint32_t threads;
   uint32_t simd_size;

   uint32_t push_data_offset;

   /* Align the struct to 64B */
   uint32_t pad[1];
};

/**
 * This structure holds information for a ray tracing pipeline.
 */
struct anv_dgc_rt_indirect_descriptor {
   uint32_t ray_stack_stride;
   uint32_t stack_ids_per_dss;
   uint32_t sw_stack_size;

   uint64_t call_handler;

   uint64_t hit_sbt;
   uint64_t miss_sbt;
   uint64_t callable_sbt;
};
