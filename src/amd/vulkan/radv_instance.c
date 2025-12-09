/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef HAVE_VALGRIND
#include <memcheck.h>
#include <valgrind.h>
#define VG(x) x
#else
#define VG(x) ((void)0)
#endif

#include "radv_debug.h"
#include "radv_entrypoints.h"
#include "radv_instance.h"
#include "radv_wsi.h"

#include "util/driconf.h"

#include "vk_instance.h"
#include "vk_log.h"
#include "vk_util.h"

static const struct debug_control radv_debug_options[] = {
   {"nofastclears", RADV_DEBUG_NO_FAST_CLEARS},
   {"nodcc", RADV_DEBUG_NO_DCC},
   {"shaders", RADV_DEBUG_DUMP_SHADERS},
   {"nocache", RADV_DEBUG_NO_CACHE},
   {"shaderstats", RADV_DEBUG_DUMP_SHADER_STATS},
   {"nohiz", RADV_DEBUG_NO_HIZ},
   {"nocompute", RADV_DEBUG_NO_COMPUTE_QUEUE},
   {"allbos", RADV_DEBUG_ALL_BOS},
   {"noibchaining", RADV_DEBUG_NO_IB_CHAINING},
   {"spirv", RADV_DEBUG_DUMP_SPIRV},
   {"zerovram", RADV_DEBUG_ZERO_VRAM},
   {"syncshaders", RADV_DEBUG_SYNC_SHADERS},
   {"preoptir", RADV_DEBUG_DUMP_PREOPT_IR},
   {"info", RADV_DEBUG_INFO},
   {"startup", RADV_DEBUG_STARTUP},
   {"checkir", RADV_DEBUG_CHECKIR},
   {"nobinning", RADV_DEBUG_NOBINNING},
   {"nongg", RADV_DEBUG_NO_NGG},
   {"metashaders", RADV_DEBUG_DUMP_META_SHADERS},
   {"llvm", RADV_DEBUG_LLVM},
   {"forcecompress", RADV_DEBUG_FORCE_COMPRESS},
   {"hang", RADV_DEBUG_HANG},
   {"img", RADV_DEBUG_IMG},
   {"noumr", RADV_DEBUG_NO_UMR},
   {"nodisplaydcc", RADV_DEBUG_NO_DISPLAY_DCC},
   {"notccompatcmask", RADV_DEBUG_NO_TC_COMPAT_CMASK},
   {"novrsflatshading", RADV_DEBUG_NO_VRS_FLAT_SHADING},
   {"noatocdithering", RADV_DEBUG_NO_ATOC_DITHERING},
   {"nonggc", RADV_DEBUG_NO_NGGC},
   {"prologs", RADV_DEBUG_DUMP_PROLOGS},
   {"nodma", RADV_DEBUG_NO_DMA_BLIT},
   {"epilogs", RADV_DEBUG_DUMP_EPILOGS},
   {"nofmask", RADV_DEBUG_NO_FMASK},
   {"shadowregs", RADV_DEBUG_SHADOW_REGS},
   {"extra_md", RADV_DEBUG_EXTRA_MD},
   {"nogpl", RADV_DEBUG_NO_GPL},
   {"videoarraypath", RADV_DEBUG_VIDEO_ARRAY_PATH},
   {"nort", RADV_DEBUG_NO_RT},
   {"nomeshshader", RADV_DEBUG_NO_MESH_SHADER},
   {"noeso", RADV_DEBUG_NO_ESO},
   {"psocachestats", RADV_DEBUG_PSO_CACHE_STATS},
   {"nirdebuginfo", RADV_DEBUG_NIR_DEBUG_INFO},
   {"dump_trap_handler", RADV_DEBUG_DUMP_TRAP_HANDLER},
   {"vs", RADV_DEBUG_DUMP_VS},
   {"tcs", RADV_DEBUG_DUMP_TCS},
   {"tes", RADV_DEBUG_DUMP_TES},
   {"gs", RADV_DEBUG_DUMP_GS},
   {"ps", RADV_DEBUG_DUMP_PS},
   {"task", RADV_DEBUG_DUMP_TASK},
   {"mesh", RADV_DEBUG_DUMP_MESH},
   {"cs", RADV_DEBUG_DUMP_CS},
   {"nir", RADV_DEBUG_DUMP_NIR},
   {"asm", RADV_DEBUG_DUMP_ASM},
   {"ir", RADV_DEBUG_DUMP_BACKEND_IR},
   {"pso_history", RADV_DEBUG_PSO_HISTORY},
   {"bvh4", RADV_DEBUG_BVH4},
   {"novideo", RADV_DEBUG_NO_VIDEO},
   {"validatevas", RADV_DEBUG_VALIDATE_VAS},
   {"bo_history", RADV_DEBUG_DUMP_BO_HISTORY},
   {"nobolist", RADV_DEBUG_NO_BO_LIST},
   {"dumpibs", RADV_DEBUG_DUMP_IBS},
   {NULL, 0},
};

const char *
radv_get_debug_option_name(int id)
{
   assert(id < ARRAY_SIZE(radv_debug_options));
   for (uint32_t i = 0; i < ARRAY_SIZE(radv_debug_options); i++) {
      if (radv_debug_options[i].flag == (1ull << id))
         return radv_debug_options[i].string;
   }
   return NULL;
}

static const struct debug_control radv_perftest_options[] = {
   {"localbos", RADV_PERFTEST_LOCAL_BOS},
   {"dccmsaa", RADV_PERFTEST_DCC_MSAA},
   {"cswave32", RADV_PERFTEST_CS_WAVE_32},
   {"pswave32", RADV_PERFTEST_PS_WAVE_32},
   {"gewave32", RADV_PERFTEST_GE_WAVE_32},
   {"nosam", RADV_PERFTEST_NO_SAM},
   {"sam", RADV_PERFTEST_SAM},
   {"nggc", RADV_PERFTEST_NGGC},
   {"emulate_rt", RADV_PERFTEST_EMULATE_RT},
   {"rtwave64", RADV_PERFTEST_RT_WAVE_64},
   {"video_decode", RADV_PERFTEST_VIDEO_DECODE},
   {"dmashaders", RADV_PERFTEST_DMA_SHADERS},
   {"transfer_queue", RADV_PERFTEST_TRANSFER_QUEUE},
   {"nircache", RADV_PERFTEST_NIR_CACHE},
   {"rtwave32", RADV_PERFTEST_RT_WAVE_32},
   {"video_encode", RADV_PERFTEST_VIDEO_ENCODE},
   {"nogttspill", RADV_PERFTEST_NO_GTT_SPILL},
   {"hic", RADV_PERFTEST_HIC},
   {"sparse", RADV_PERFTEST_SPARSE},
   {NULL, 0},
};

static const struct debug_control radv_trap_excp_options[] = {
   {"mem_viol", RADV_TRAP_EXCP_MEM_VIOL},
   {"float_div_by_zero", RADV_TRAP_EXCP_FLOAT_DIV_BY_ZERO},
   {"float_overflow", RADV_TRAP_EXCP_FLOAT_OVERFLOW},
   {"float_underflow", RADV_TRAP_EXCP_FLOAT_UNDERFLOW},
   {NULL, 0},
};

const char *
radv_get_perftest_option_name(int id)
{
   assert(id < ARRAY_SIZE(radv_perftest_options));
   for (uint32_t i = 0; i < ARRAY_SIZE(radv_perftest_options); i++) {
      if (radv_perftest_options[i].flag == (1ull << id))
         return radv_perftest_options[i].string;
   }
   return NULL;
}

static const struct debug_control trace_options[] = {
   {"rgp", RADV_TRACE_MODE_RGP},
   {"rra", RADV_TRACE_MODE_RRA},
   {"ctxroll", RADV_TRACE_MODE_CTX_ROLLS},
   {NULL, 0},
};

// clang-format off
static const driOptionDescription radv_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_ADAPTIVE_SYNC(true)
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
      DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(false)
      DRI_CONF_VK_XWAYLAND_WAIT_READY(false)
      DRI_CONF_RADV_REPORT_LLVM9_VERSION_STRING(false)
      DRI_CONF_RADV_ENABLE_MRT_OUTPUT_NAN_FIXUP(false)
      DRI_CONF_RADV_DISABLE_SHRINK_IMAGE_STORE(false)
      DRI_CONF_RADV_NO_DYNAMIC_BOUNDS(false)
      DRI_CONF_RADV_OVERRIDE_UNIFORM_OFFSET_ALIGNMENT(0)
      DRI_CONF_RADV_CLEAR_LDS(false)
      DRI_CONF_RADV_DISABLE_NGG_GS(false)
      DRI_CONF_RADV_GFX12_HIZ_WA()
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_OVERRIDE_VRAM_SIZE()
      DRI_CONF_VK_LOWER_TERMINATE_TO_DISCARD(false)
      DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(false)
      DRI_CONF_VK_WSI_FORCE_SWAPCHAIN_TO_CURRENT_EXTENT(false)
      DRI_CONF_VK_WSI_DISABLE_UNORDERED_SUBMITS(false)
      DRI_CONF_VK_X11_IGNORE_SUBOPTIMAL(false)
      DRI_CONF_VK_REQUIRE_ETC2(false)
      DRI_CONF_VK_REQUIRE_ASTC(false)
      DRI_CONF_RADV_ZERO_VRAM(false)
      DRI_CONF_RADV_INVARIANT_GEOM(false)
      DRI_CONF_RADV_SPLIT_FMA(false)
      DRI_CONF_RADV_DISABLE_TC_COMPAT_HTILE_GENERAL(false)
      DRI_CONF_RADV_DISABLE_DCC(false)
      DRI_CONF_RADV_DISABLE_DCC_MIPS(false)
      DRI_CONF_RADV_DISABLE_DCC_STORES(false)
      DRI_CONF_RADV_DISABLE_ANISO_SINGLE_LEVEL(false)
      DRI_CONF_RADV_DISABLE_TRUNC_COORD(false)
      DRI_CONF_RADV_DISABLE_SINKING_LOAD_INPUT_FS(false)
      DRI_CONF_RADV_DISABLE_DEPTH_STORAGE(false)
      DRI_CONF_RADV_FLUSH_BEFORE_QUERY_COPY(false)
      DRI_CONF_RADV_ENABLE_UNIFIED_HEAP_ON_APU(false)
      DRI_CONF_RADV_TEX_NON_UNIFORM(false)
      DRI_CONF_RADV_FLUSH_BEFORE_TIMESTAMP_WRITE(false)
      DRI_CONF_RADV_RT_WAVE64(false)
      DRI_CONF_RADV_OVERRIDE_GRAPHICS_SHADER_VERSION(0)
      DRI_CONF_RADV_OVERRIDE_COMPUTE_SHADER_VERSION(0)
      DRI_CONF_RADV_OVERRIDE_RAY_TRACING_SHADER_VERSION(0)
      DRI_CONF_RADV_SSBO_NON_UNIFORM(false)
      DRI_CONF_RADV_APP_LAYER()
      DRI_CONF_RADV_EMULATE_RT(false)
      DRI_CONF_RADV_ENABLE_FLOAT16_GFX8(false)
      DRI_CONF_RADV_COOPERATIVE_MATRIX2_NV(false)
      DRI_CONF_RADV_WAIT_FOR_VM_MAP_UPDATES(false)
      DRI_CONF_RADV_NO_IMPLICIT_VARYING_SUBGROUP_SIZE(false)
      DRI_CONF_RADV_HIDE_REBAR_ON_DGPU(false)
   DRI_CONF_SECTION_END
};
// clang-format on

static void
radv_init_dri_debug_options(struct radv_instance *instance)
{
   struct radv_drirc *drirc = &instance->drirc;

   drirc->debug.disable_aniso_single_level = driQueryOptionb(&drirc->options, "radv_disable_aniso_single_level");
   drirc->debug.disable_dcc = driQueryOptionb(&drirc->options, "radv_disable_dcc");
   drirc->debug.disable_dcc_mips = driQueryOptionb(&drirc->options, "radv_disable_dcc_mips");
   drirc->debug.disable_dcc_stores = driQueryOptionb(&drirc->options, "radv_disable_dcc_stores");
   drirc->debug.disable_depth_storage = driQueryOptionb(&drirc->options, "radv_disable_depth_storage");
   drirc->debug.disable_shrink_image_store = driQueryOptionb(&drirc->options, "radv_disable_shrink_image_store");
   drirc->debug.disable_sinking_load_input_fs = driQueryOptionb(&drirc->options, "radv_disable_sinking_load_input_fs");
   drirc->debug.disable_tc_compat_htile_in_general =
      driQueryOptionb(&drirc->options, "radv_disable_tc_compat_htile_general");

   drirc->debug.disable_trunc_coord = driQueryOptionb(&drirc->options, "radv_disable_trunc_coord");
   if (instance->vk.app_info.engine_name && !strcmp(instance->vk.app_info.engine_name, "DXVK")) {
      /* Since 2.3.1+, DXVK uses the application version to notify the driver about D3D9. */
      const bool is_d3d9 = instance->vk.app_info.app_version & 0x1;

      drirc->debug.disable_trunc_coord &= !is_d3d9;
   }

   drirc->debug.enable_mrt_output_nan_fixup = driQueryOptionb(&drirc->options, "radv_enable_mrt_output_nan_fixup");
   drirc->debug.flush_before_query_copy = driQueryOptionb(&drirc->options, "radv_flush_before_query_copy");
   drirc->debug.flush_before_timestamp_write = driQueryOptionb(&drirc->options, "radv_flush_before_timestamp_write");
   drirc->debug.invariant_geom = driQueryOptionb(&drirc->options, "radv_invariant_geom");
   drirc->debug.lower_terminate_to_discard = driQueryOptionb(&drirc->options, "vk_lower_terminate_to_discard");
   drirc->debug.no_dynamic_bounds = driQueryOptionb(&drirc->options, "radv_no_dynamic_bounds");
   drirc->debug.split_fma = driQueryOptionb(&drirc->options, "radv_split_fma");
   drirc->debug.ssbo_non_uniform = driQueryOptionb(&drirc->options, "radv_ssbo_non_uniform");
   drirc->debug.tex_non_uniform = driQueryOptionb(&drirc->options, "radv_tex_non_uniform");
   drirc->debug.wait_for_vm_map_updates = driQueryOptionb(&drirc->options, "radv_wait_for_vm_map_updates");
   drirc->debug.zero_vram = driQueryOptionb(&drirc->options, "radv_zero_vram");
   drirc->debug.no_implicit_varying_subgroup_size =
      driQueryOptionb(&drirc->options, "radv_no_implicit_varying_subgroup_size");
   drirc->debug.app_layer = driQueryOptionstr(&drirc->options, "radv_app_layer");

   drirc->debug.override_uniform_offset_alignment =
      driQueryOptioni(&drirc->options, "radv_override_uniform_offset_alignment");

   drirc->debug.rt_wave64 = driQueryOptionb(&drirc->options, "radv_rt_wave64");
   drirc->debug.hide_rebar_on_dgpu = driQueryOptionb(&drirc->options, "radv_hide_rebar_on_dgpu");
}

static void
radv_init_dri_performance_options(struct radv_instance *instance)
{
   struct radv_drirc *drirc = &instance->drirc;

   drirc->performance.disable_ngg_gs = driQueryOptionb(&drirc->options, "radv_disable_ngg_gs");
   drirc->performance.enable_unified_heap_on_apu = driQueryOptionb(&drirc->options, "radv_enable_unified_heap_on_apu");
   drirc->performance.report_llvm9_version_string =
      driQueryOptionb(&drirc->options, "radv_report_llvm9_version_string");
   drirc->performance.gfx12_hiz_wa = driQueryOptionstr(&drirc->options, "radv_gfx12_hiz_wa");
}

static void
radv_init_dri_features_options(struct radv_instance *instance)
{
   struct radv_drirc *drirc = &instance->drirc;

   drirc->features.cooperative_matrix2_nv = driQueryOptionb(&drirc->options, "radv_cooperative_matrix2_nv");
   drirc->features.emulate_rt = driQueryOptionb(&drirc->options, "radv_emulate_rt");
   drirc->features.expose_float16_gfx8 = driQueryOptionb(&drirc->options, "radv_enable_float16_gfx8");
   drirc->features.vk_require_etc2 = driQueryOptionb(&drirc->options, "vk_require_etc2");
   drirc->features.vk_require_astc = driQueryOptionb(&drirc->options, "vk_require_astc");
}

static void
radv_init_dri_misc_options(struct radv_instance *instance)
{
   struct radv_drirc *drirc = &instance->drirc;

   drirc->misc.clear_lds = driQueryOptionb(&drirc->options, "radv_clear_lds");
   drirc->misc.override_vram_size = driQueryOptioni(&drirc->options, "override_vram_size");
   drirc->misc.override_graphics_shader_version =
      driQueryOptioni(&drirc->options, "radv_override_graphics_shader_version");
   drirc->misc.override_compute_shader_version =
      driQueryOptioni(&drirc->options, "radv_override_compute_shader_version");
   drirc->misc.override_ray_tracing_shader_version =
      driQueryOptioni(&drirc->options, "radv_override_ray_tracing_shader_version");
}

static void
radv_init_dri_options(struct radv_instance *instance)
{
   struct radv_drirc *drirc = &instance->drirc;

   driParseOptionInfo(&drirc->available_options, radv_dri_options, ARRAY_SIZE(radv_dri_options));
   driParseConfigFiles(&drirc->options, &drirc->available_options, 0, "radv", NULL, NULL,
                       instance->vk.app_info.app_name, instance->vk.app_info.app_version,
                       instance->vk.app_info.engine_name, instance->vk.app_info.engine_version);

   radv_init_dri_debug_options(instance);
   radv_init_dri_performance_options(instance);
   radv_init_dri_features_options(instance);
   radv_init_dri_misc_options(instance);
}

bool
radv_is_rt_wave64_enabled(const struct radv_instance *instance)
{
   return instance->perftest_flags & RADV_PERFTEST_RT_WAVE_64 || instance->drirc.debug.rt_wave64;
}

static const struct vk_instance_extension_table radv_instance_extensions_supported = {
   .KHR_device_group_creation = true,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,

#ifdef RADV_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2 = true,
   .KHR_surface = true,
   .KHR_surface_maintenance1 = true,
   .KHR_surface_protected_capabilities = true,
   .EXT_surface_maintenance1 = true,
   .EXT_swapchain_colorspace = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .KHR_get_display_properties2 = true,
   .EXT_direct_mode_display = true,
   .EXT_display_surface_counter = true,
   .EXT_acquire_drm_display = true,
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface = true,
#endif
};

static enum radeon_ctx_pstate
radv_parse_pstate(const char *str)
{
   if (!strcmp(str, "peak")) {
      return RADEON_CTX_PSTATE_PEAK;
   } else if (!strcmp(str, "standard")) {
      return RADEON_CTX_PSTATE_STANDARD;
   } else if (!strcmp(str, "min_sclk")) {
      return RADEON_CTX_PSTATE_MIN_SCLK;
   } else if (!strcmp(str, "min_mclk")) {
      return RADEON_CTX_PSTATE_MIN_MCLK;
   } else {
      return RADEON_CTX_PSTATE_NONE;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                    VkInstance *pInstance)
{
   struct radv_instance *instance;
   VkResult result;

   /* Report RADV_FORCE_FAMILY as deprecated for one or two release cycles. */
   if (os_get_option("RADV_FORCE_FAMILY")) {
      fprintf(stderr, "radv: RADV_FORCE_FAMILY=<family> has been removed. Please use AMDGPU drm-shim now.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (!pAllocator)
      pAllocator = vk_default_allocator();

   instance = vk_zalloc(pAllocator, sizeof(*instance), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &radv_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table, &wsi_instance_entrypoints, false);

   result =
      vk_instance_init(&instance->vk, &radv_instance_extensions_supported, &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(NULL, result);
   }

   vk_instance_add_driver_trace_modes(&instance->vk, trace_options);

   simple_mtx_init(&instance->shader_dump_mtx, mtx_plain);

   instance->debug_flags = parse_debug_string(os_get_option("RADV_DEBUG"), radv_debug_options);
   instance->perftest_flags = parse_debug_string(os_get_option("RADV_PERFTEST"), radv_perftest_options);
   instance->trap_excp_flags = parse_debug_string(os_get_option("RADV_TRAP_HANDLER_EXCP"), radv_trap_excp_options);
   instance->profile_pstate = radv_parse_pstate(debug_get_option("RADV_PROFILE_PSTATE", "peak"));

   const uint64_t shader_stage_flags = RADV_DEBUG_DUMP_VS | RADV_DEBUG_DUMP_TCS | RADV_DEBUG_DUMP_TES |
                                       RADV_DEBUG_DUMP_GS | RADV_DEBUG_DUMP_PS | RADV_DEBUG_DUMP_TASK |
                                       RADV_DEBUG_DUMP_MESH | RADV_DEBUG_DUMP_CS;

   const uint64_t compilation_stage_flags = RADV_DEBUG_DUMP_SPIRV | RADV_DEBUG_DUMP_NIR | RADV_DEBUG_DUMP_PREOPT_IR |
                                            RADV_DEBUG_DUMP_BACKEND_IR | RADV_DEBUG_DUMP_ASM;

   if ((instance->debug_flags & shader_stage_flags) && !(instance->debug_flags & compilation_stage_flags)) {
      /* When shader stages are specified but compilation stages aren't:
       * use a default set of compilation stages.
       */
      instance->debug_flags |= RADV_DEBUG_DUMP_NIR | RADV_DEBUG_DUMP_BACKEND_IR | RADV_DEBUG_DUMP_ASM;
   } else if (!(instance->debug_flags & shader_stage_flags) && (instance->debug_flags & compilation_stage_flags)) {
      /* When compilation stages are specified but shader stages aren't:
       * dump all shader stages.
       */
      instance->debug_flags |= shader_stage_flags;
   }

   if (instance->debug_flags & RADV_DEBUG_PSO_HISTORY) {
      const char *filename = "/tmp/radv_pso_history.log";

      instance->pso_history_logfile = fopen(filename, "w");
      if (!instance->pso_history_logfile)
         fprintf(stderr, "radv: Failed to open log file: %s.\n", filename);
   }

   instance->vk.physical_devices.try_create_for_drm = create_drm_physical_device;
   instance->vk.physical_devices.destroy = radv_physical_device_destroy;

   if (instance->debug_flags & RADV_DEBUG_STARTUP)
      fprintf(stderr, "radv: info: Created an instance.\n");

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   radv_init_dri_options(instance);

   *pInstance = radv_instance_to_handle(instance);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyInstance(VkInstance _instance, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   if (instance->pso_history_logfile)
      fclose(instance->pso_history_logfile);

   simple_mtx_destroy(&instance->shader_dump_mtx);

   driDestroyOptionCache(&instance->drirc.options);
   driDestroyOptionInfo(&instance->drirc.available_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pPropertyCount,
                                          VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(&radv_instance_extensions_supported, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   *pApiVersion = RADV_API_VERSION;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
radv_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   return vk_instance_get_proc_addr(instance, &radv_instance_entrypoints, pName);
}

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return radv_GetInstanceProcAddr(instance, pName);
}
