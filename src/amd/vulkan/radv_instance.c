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

#include "tools/radv_debug.h"
#include "tools/radv_debug_hang.h"
#include "radv_entrypoints.h"
#include "radv_instance.h"
#include "radv_wsi.h"

#include "vk_instance.h"
#include "vk_log.h"

static const struct debug_control radv_debug_options[] = {
   {"nofastclears", RADV_DEBUG_NO_FAST_CLEARS},
   {"nodcc", RADV_DEBUG_NO_DCC},
   {"shaders", RADV_DEBUG_DUMP_SHADERS},
   {"nocachecompat", RADV_DEBUG_NO_CACHE_COMPAT},
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
   {"dumpibs", RADV_DEBUG_DUMP_IBS},
   {"vm", RADV_DEBUG_VM},
   {"nosmemmitigation", RADV_DEBUG_NO_SMEM_MITIGATION},
   {"fullsync", RADV_DEBUG_FULL_SYNC},
   {"notmz", RADV_DEBUG_NO_TMZ},
   {"noheap", RADV_DEBUG_NO_HEAP},
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
   {"video_encode", RADV_PERFTEST_VIDEO_ENCODE},
   {"nogttspill", RADV_PERFTEST_NO_GTT_SPILL},
   {"hic", RADV_PERFTEST_HIC},
   {"sparse", RADV_PERFTEST_SPARSE},
   {"rtcps", RADV_PERFTEST_RT_CPS},
   {"bfloat16", RADV_PERFTEST_BFLOAT16},
   {"lowlatencydec", RADV_PERFTEST_LOWLATENCYDEC},
   {"lowlatencyenc", RADV_PERFTEST_LOWLATENCYENC},
   {NULL, 0},
};

static const struct debug_control radv_experimental_options[] = {
   {"emulate_rt", RADV_EXPERIMENTAL_EMULATE_RT},
   {"video_decode", RADV_EXPERIMENTAL_VIDEO_DECODE},
   {"transfer_queue", RADV_EXPERIMENTAL_TRANSFER_QUEUE},
   {"video_encode", RADV_EXPERIMENTAL_VIDEO_ENCODE},
   {"hic", RADV_EXPERIMENTAL_HIC},
   {"sparse", RADV_EXPERIMENTAL_SPARSE},
   {"bfloat16", RADV_EXPERIMENTAL_BFLOAT16},
   {"msrtss", RADV_EXPERIMENTAL_MSRTSS},
   {NULL, 0},
};

static const struct debug_control radv_trap_excp_options[] = {
   {"mem_viol", RADV_TRAP_EXCP_MEM_VIOL},
   {"float_div_by_zero", RADV_TRAP_EXCP_FLOAT_DIV_BY_ZERO},
   {"float_overflow", RADV_TRAP_EXCP_FLOAT_OVERFLOW},
   {"float_underflow", RADV_TRAP_EXCP_FLOAT_UNDERFLOW},
   {NULL, 0},
};

// clang-format off
static const struct debug_control radv_queue_disable_options[] = {
   {"gfx", RADV_QUEUE_DISABLE_GENERAL},
   {"compute", RADV_QUEUE_DISABLE_COMPUTE},
   {"vdec", RADV_QUEUE_DISABLE_VIDEO_DEC},
   {"venc", RADV_QUEUE_DISABLE_VIDEO_ENC},
   {"transfer", RADV_QUEUE_DISABLE_TRANSFER},
   {"sparse", RADV_QUEUE_DISABLE_SPARSE},
};
// clang-format on

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
   {"gamma", RADV_TRACE_MODE_GAMMA},
   {"ctxroll", RADV_TRACE_MODE_CTX_ROLLS},
   {"ranges", RADV_TRACE_MODE_RANGES},
   {NULL, 0},
};

static void
radv_init_dri_options(struct radv_instance *instance)
{
   struct radv_drirc *drirc = &instance->drirc;

   radv_parse_dri_options(drirc, &(driConfigFileParseParams){
                                    .driverName = "radv",
                                    .applicationName = instance->vk.app_info.app_name,
                                    .applicationVersion = instance->vk.app_info.app_version,
                                    .engineName = instance->vk.app_info.engine_name,
                                    .engineVersion = instance->vk.app_info.engine_version,
                                 });

   if (instance->vk.app_info.engine_name && !strcmp(instance->vk.app_info.engine_name, "DXVK")) {
      /* Since 2.3.1+, DXVK uses the application version to notify the driver about D3D9. */
      const bool is_d3d9 = instance->vk.app_info.app_version & 0x1;

      drirc->debug.disable_trunc_coord &= !is_d3d9;
   }
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

static void
radv_convert_perftest_to_experimental(struct radv_instance *instance)
{
#define CONVERT(name, flag)                                                                                            \
   if (instance->perftest_flags & RADV_PERFTEST_##flag) {                                                              \
      fprintf(stderr, "radv: RADV_PERFTEST=" #name " is deprecated and will be removed in future Mesa releases. "      \
                      "Please use RADV_EXPERIMENTAL=" #name " instead.\n");                                            \
      instance->experimental_flags |= RADV_EXPERIMENTAL_##flag;                                                        \
   }

   CONVERT(emulate_rt, EMULATE_RT);
   CONVERT(video_decode, VIDEO_DECODE);
   CONVERT(video_encode, VIDEO_ENCODE);
   CONVERT(transfer_queue, TRANSFER_QUEUE);
   CONVERT(hic, HIC);
   CONVERT(sparse, SPARSE);
   CONVERT(bfloat16, BFLOAT16);

#undef CONVERT
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                    VkInstance *pInstance)
{
   struct radv_instance *instance;
   VkResult result;

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
   instance->experimental_flags = parse_debug_string(os_get_option("RADV_EXPERIMENTAL"), radv_experimental_options);
   instance->trap_excp_flags = parse_debug_string(os_get_option("RADV_TRAP_HANDLER_EXCP"), radv_trap_excp_options);
   instance->profile_pstate = radv_parse_pstate(debug_get_option("RADV_PROFILE_PSTATE", "peak"));
   instance->queue_disable_flags = parse_debug_string(os_get_option("RADV_QUEUE_DISABLE"), radv_queue_disable_options);

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

   radv_convert_perftest_to_experimental(instance);

   if (instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE) {
      fprintf(stderr, "radv: RADV_DEBUG=nocompute is deprecated and will be removed in future Mesa Releases.\n"
                      "Please use RADV_QUEUE_DISABLE=compute instead.\n");
      instance->queue_disable_flags |= RADV_QUEUE_DISABLE_COMPUTE;
   }

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
