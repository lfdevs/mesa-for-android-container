/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_api_version.h"

#include "util/driconf.h"

static const driOptionDescription anv_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_ADAPTIVE_SYNC(true)
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
      DRI_CONF_VK_WSI_DISABLE_UNORDERED_SUBMITS(false)
      DRI_CONF_VK_XWAYLAND_WAIT_READY(false)
      DRI_CONF_ANV_ASSUME_FULL_SUBGROUPS(0)
      DRI_CONF_ANV_ASSUME_FULL_SUBGROUPS_WITH_BARRIER(false)
      DRI_CONF_ANV_ASSUME_FULL_SUBGROUPS_WITH_SHARED_MEMORY(false)
      DRI_CONF_ANV_BARRIER_POST_TYPED_CLEAR_SHADER(false)
      DRI_CONF_ANV_BARRIER_POST_UNTYPED_CLEAR_SHADER(false)
      DRI_CONF_ANV_DISABLE_FCV(false)
      DRI_CONF_ANV_ENABLE_BUFFER_COMP(false)
      DRI_CONF_ANV_DISABLE_DRM_AUX_MODIFIERS(false)
      DRI_CONF_ANV_EXTERNAL_MEMORY_IMPLICIT_SYNC(true)
      DRI_CONF_ANV_FORCE_GUC_LOW_LATENCY(false)
      DRI_CONF_ANV_SAMPLE_MASK_OUT_OPENGL_BEHAVIOUR(false)
      DRI_CONF_ANV_FORCE_FILTER_ADDR_ROUNDING(false)
      DRI_CONF_ANV_FP64_WORKAROUND_ENABLED(false)
      DRI_CONF_ANV_GENERATED_INDIRECT_THRESHOLD(4)
      DRI_CONF_ANV_GENERATED_INDIRECT_RING_THRESHOLD(100)
      DRI_CONF_ANV_PROMOTE_CBV_TO_PUSH_BUFFERS(false)
      DRI_CONF_ANV_STATE_CACHE_PERF_FIX(false)
      DRI_CONF_NO_16BIT(false)
      DRI_CONF_INTEL_BINDING_TABLE_BLOCK_SIZE(BINDING_TABLE_POOL_DEFAULT_BLOCK_SIZE,
                                              1024, 128 * 1024)
      DRI_CONF_INTEL_DISABLE_PUSH_CONSTANT_ALLOC(true)
      DRI_CONF_INTEL_ENABLE_WA_14018912822(false)
      DRI_CONF_INTEL_ENABLE_WA_14024015672_MSAA(false)
      DRI_CONF_INTEL_SAMPLER_ROUTE_TO_LSC(false)
      DRI_CONF_ANV_QUERY_CLEAR_WITH_BLORP_THRESHOLD(6)
      DRI_CONF_ANV_QUERY_COPY_WITH_SHADER_THRESHOLD(6)
      DRI_CONF_ANV_FORCE_INDIRECT_DESCRIPTORS(false)
      DRI_CONF_ANV_DISABLE_LINK_TIME_OPTIMIZATION(false)
      DRI_CONF_ANV_ENABLE_OPT_DIVERGENT_ATOMICS(0)
      DRI_CONF_ANV_BRW_DISABLE_SUBGROUP_SIZE_CONTROL(false)
      DRI_CONF_SHADER_SPILLING_RATE(11)
      DRI_CONFIG_INTEL_FORCE_COMPUTE_SURFACE_PREFETCH(true)
      DRI_CONFIG_INTEL_FORCE_SAMPLER_PREFETCH(false)
      DRI_CONFIG_INTEL_TBIMR(true)
      DRI_CONFIG_INTEL_VF_DISTRIBUTION(true)
      DRI_CONFIG_INTEL_TE_DISTRIBUTION(true)
      DRI_CONFIG_INTEL_STORAGE_CACHE_POLICY_WT(false)
      DRI_CONF_ANV_LARGE_WORKGROUP_NON_COHERENT_IMAGE_WORKAROUND(false)
#if DETECT_OS_ANDROID && ANDROID_API_LEVEL >= 37
      DRI_CONF_ANV_COMPRESSION_CONTROL_ENABLED(true)
#else
      DRI_CONF_ANV_COMPRESSION_CONTROL_ENABLED(false)
#endif
      DRI_CONF_ANV_FAKE_NONLOCAL_MEMORY(false)
      DRI_CONF_OPT_E(intel_stack_id, 512, 256, 2048,
                     "Control the number stackIDs (i.e. number of unique rays in the RT subsytem)",
                     DRI_CONF_ENUM(256,  "256 stackids")
                     DRI_CONF_ENUM(512,  "512 stackids")
                     DRI_CONF_ENUM(1024, "1024 stackids")
                     DRI_CONF_ENUM(2048, "2048 stackids"))
      DRI_CONF_OPT_E(dispatch_timeout_counter, 512, 64, 4096,
                     "Force BTD child dispatches if dispatches do not happen naturally for number of clocks equal to the programmed timeout counter",
                     DRI_CONF_ENUM(64,    "64 clocks")
                     DRI_CONF_ENUM(128,   "128 clocks")
                     DRI_CONF_ENUM(192,   "192 clocks")
                     DRI_CONF_ENUM(256,   "256 clocks")
                     DRI_CONF_ENUM(384,   "384 clocks")
                     DRI_CONF_ENUM(512,   "512 clocks")
                     DRI_CONF_ENUM(640,   "640 clocks")
                     DRI_CONF_ENUM(768,   "768 clocks")
                     DRI_CONF_ENUM(896,   "896 clocks")
                     DRI_CONF_ENUM(1024,  "1024 clocks")
                     DRI_CONF_ENUM(1152,  "1152 clocks")
                     DRI_CONF_ENUM(1280,  "1280 clocks")
                     DRI_CONF_ENUM(1408,  "1408 clocks")
                     DRI_CONF_ENUM(1536,  "1536 clocks")
                     DRI_CONF_ENUM(1664,  "1664 clocks")
                     DRI_CONF_ENUM(1792,  "1792 clocks")
                     DRI_CONF_ENUM(1920,  "1920 clocks")
                     DRI_CONF_ENUM(2048,  "2048 clocks")
                     DRI_CONF_ENUM(4096,  "4096 clocks"))
      DRI_CONF_ANV_UPPER_BOUND_DESCRIPTOR_POOL_SAMPLER(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_ALWAYS_FLUSH_CACHE(false)
      DRI_CONF_VK_LOWER_TERMINATE_TO_DISCARD(false)
      DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(false)
      DRI_CONF_VK_WSI_FORCE_SWAPCHAIN_TO_CURRENT_EXTENT(false)
      DRI_CONF_VK_X11_IGNORE_SUBOPTIMAL(false)
      DRI_CONF_LIMIT_TRIG_INPUT_RANGE(false)
#if DETECT_OS_ANDROID && ANDROID_API_LEVEL >= 35
      DRI_CONF_ANV_EMULATE_READ_WITHOUT_FORMAT(true)
#else
      DRI_CONF_ANV_EMULATE_READ_WITHOUT_FORMAT(false)
#endif
      DRI_CONF_FORCE_VK_VENDOR()
      DRI_CONF_FAKE_SPARSE(false)
      DRI_CONF_CUSTOM_BORDER_COLORS_WITHOUT_FORMAT(!DETECT_OS_ANDROID)
#if DETECT_OS_ANDROID && ANDROID_API_LEVEL >= 34
      DRI_CONF_VK_REQUIRE_ASTC(true)
#else
      DRI_CONF_VK_REQUIRE_ASTC(false)
#endif
      DRI_CONF_ANV_VF_COMPONENT_PACKING(true)
      DRI_CONF_ANV_ENABLE_SCRATCH_PAGE(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_QUALITY
      DRI_CONF_PP_LOWER_DEPTH_RANGE_RATE()
   DRI_CONF_SECTION_END
};

static const struct debug_control debug_control[] = {
   { "bindless",     ANV_DEBUG_BINDLESS},
   { "desc-dirty",   ANV_DEBUG_DESCRIPTOR_DIRTY},
   { "dgc-dump",     ANV_DEBUG_DGC_DUMP},
   { "experimental", ANV_DEBUG_EXPERIMENTAL},
   { "no-gpl",       ANV_DEBUG_NO_GPL},
   { "no-slab",      ANV_DEBUG_NO_SLAB},
   { "no-sparse",    ANV_DEBUG_NO_SPARSE},
   { "sparse-trtt",  ANV_DEBUG_SPARSE_TRTT},
   { "video-decode", ANV_DEBUG_VIDEO_DECODE},
   { "video-encode", ANV_DEBUG_VIDEO_ENCODE},
   { "shader-dump",  ANV_DEBUG_SHADER_DUMP},
   { "shader-hash",  ANV_DEBUG_SHADER_HASH},
   { "shader-print", ANV_DEBUG_SHADER_PRINT},
   { NULL,    0 }
};

enum anv_debug anv_debug;

static void
process_anv_debug_variable_once(void)
{
   anv_debug = parse_debug_string(os_get_option("ANV_DEBUG"), debug_control);
}

VkResult anv_EnumerateInstanceVersion(
    uint32_t*                                   pApiVersion)
{
    *pApiVersion = ANV_API_VERSION;
    return VK_SUCCESS;
}

static const struct vk_instance_extension_table instance_extensions = {
   .KHR_device_group_creation                = true,
   .KHR_external_fence_capabilities          = true,
   .KHR_external_memory_capabilities         = true,
   .KHR_external_semaphore_capabilities      = true,
   .KHR_get_physical_device_properties2      = true,
   .EXT_debug_report                         = true,
   .EXT_debug_utils                          = true,

#ifdef ANV_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2            = true,
   .KHR_surface                              = true,
   .KHR_surface_maintenance1                 = true,
   .KHR_surface_protected_capabilities       = true,
   .EXT_surface_maintenance1                 = true,
   .EXT_swapchain_colorspace                 = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface                      = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface                          = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface                         = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display                 = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display                              = true,
   .KHR_get_display_properties2              = true,
   .EXT_direct_mode_display                  = true,
   .EXT_display_surface_counter              = true,
   .EXT_acquire_drm_display                  = true,
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface                     = true,
#endif
};

VkResult anv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

static void
anv_init_dri_options(struct anv_instance *instance)
{
   driParseOptionInfo(&instance->available_dri_options, anv_dri_options,
                      ARRAY_SIZE(anv_dri_options));
   driParseConfigFiles(&instance->dri_options,
                       &instance->available_dri_options, 0, "anv", NULL, NULL,
                       instance->vk.app_info.app_name,
                       instance->vk.app_info.app_version,
                       instance->vk.app_info.engine_name,
                       instance->vk.app_info.engine_version);

    instance->assume_full_subgroups =
       driQueryOptioni(&instance->dri_options, "anv_assume_full_subgroups");
    instance->assume_full_subgroups_with_barrier =
       driQueryOptionb(&instance->dri_options, "anv_assume_full_subgroups_with_barrier");
    instance->assume_full_subgroups_with_shared_memory =
       driQueryOptionb(&instance->dri_options, "anv_assume_full_subgroups_with_shared_memory");
    instance->limit_trig_input_range =
       driQueryOptionb(&instance->dri_options, "limit_trig_input_range");
    instance->sample_mask_out_opengl_behaviour =
       driQueryOptionb(&instance->dri_options, "anv_sample_mask_out_opengl_behaviour");
    instance->force_filter_addr_rounding =
       driQueryOptionb(&instance->dri_options, "anv_force_filter_addr_rounding");
    instance->promote_cbv_to_push_buffers =
       driQueryOptionb(&instance->dri_options, "anv_promote_cbv_to_push_buffers");
    instance->state_cache_perf_fix =
       driQueryOptionb(&instance->dri_options, "anv_state_cache_perf_fix");
    instance->lower_depth_range_rate =
       driQueryOptionf(&instance->dri_options, "lower_depth_range_rate");
    instance->no_16bit =
       driQueryOptionb(&instance->dri_options, "no_16bit");
    instance->intel_enable_wa_14018912822 =
       driQueryOptionb(&instance->dri_options, "intel_enable_wa_14018912822");
    instance->intel_enable_wa_14024015672_msaa =
       driQueryOptionb(&instance->dri_options, "intel_enable_wa_14024015672_msaa");
    instance->emulate_read_without_format =
       driQueryOptionb(&instance->dri_options, "anv_emulate_read_without_format");
    instance->fp64_workaround_enabled =
       driQueryOptionb(&instance->dri_options, "fp64_workaround_enabled");
    instance->generated_indirect_threshold =
       driQueryOptioni(&instance->dri_options, "generated_indirect_threshold");
    instance->generated_indirect_ring_threshold =
       driQueryOptioni(&instance->dri_options, "generated_indirect_ring_threshold");
    instance->query_clear_with_blorp_threshold =
       driQueryOptioni(&instance->dri_options, "query_clear_with_blorp_threshold");
    instance->query_copy_with_shader_threshold =
       driQueryOptioni(&instance->dri_options, "query_copy_with_shader_threshold");
    instance->force_vk_vendor =
       driQueryOptioni(&instance->dri_options, "force_vk_vendor");
    instance->has_fake_sparse =
       driQueryOptionb(&instance->dri_options, "fake_sparse");
    instance->force_sampler_prefetch =
       driQueryOptionb(&instance->dri_options, "intel_force_sampler_prefetch");
    instance->force_compute_surface_prefetch =
       driQueryOptionb(&instance->dri_options, "intel_force_compute_surface_prefetch");
    instance->enable_tbimr = driQueryOptionb(&instance->dri_options, "intel_tbimr");
    instance->enable_vf_distribution =
       driQueryOptionb(&instance->dri_options, "intel_vf_distribution");
    instance->enable_te_distribution =
       driQueryOptionb(&instance->dri_options, "intel_te_distribution");
    instance->large_workgroup_non_coherent_image_workaround =
       driQueryOptionb(&instance->dri_options, "anv_large_workgroup_non_coherent_image_workaround");
    instance->disable_fcv =
       driQueryOptionb(&instance->dri_options, "anv_disable_fcv");
    instance->enable_buffer_comp =
       driQueryOptionb(&instance->dri_options, "anv_enable_buffer_comp");
    instance->external_memory_implicit_sync =
       driQueryOptionb(&instance->dri_options, "anv_external_memory_implicit_sync");
    instance->compression_control_enabled =
       driQueryOptionb(&instance->dri_options, "compression_control_enabled");
    instance->anv_fake_nonlocal_memory =
       driQueryOptionb(&instance->dri_options, "anv_fake_nonlocal_memory");
    instance->anv_upper_bound_descriptor_pool_sampler =
       driQueryOptionb(&instance->dri_options,
                       "anv_upper_bound_descriptor_pool_sampler");
    instance->custom_border_colors_without_format =
       driQueryOptionb(&instance->dri_options,
                       "custom_border_colors_without_format");
    instance->vf_component_packing =
       driQueryOptionb(&instance->dri_options, "anv_vf_component_packing");
    instance->lower_terminate_to_discard =
       driQueryOptionb(&instance->dri_options, "vk_lower_terminate_to_discard");
    instance->disable_xe2_drm_ccs_modifiers =
       driQueryOptionb(&instance->dri_options, "anv_disable_drm_ccs_modifiers");
    instance->binding_table_block_size = util_next_power_of_two(
       driQueryOptioni(&instance->dri_options, "intel_binding_table_block_size"));
    instance->barrier_post_typed_clear_shader =
       driQueryOptionb(&instance->dri_options, "anv_barrier_post_typed_clear_shader");
    instance->barrier_post_untyped_clear_shader =
       driQueryOptionb(&instance->dri_options, "anv_barrier_post_untyped_clear_shader");
    instance->disable_push_constant_alloc =
       driQueryOptionb(&instance->dri_options, "intel_disable_push_constant_alloc");

    if (instance->vk.app_info.engine_name &&
        !strcmp(instance->vk.app_info.engine_name, "DXVK")) {
        /* Since 2.3.1+, DXVK uses the application version to signal D3D9. */
        const bool is_d3d9 = instance->vk.app_info.app_version & 0x1;

        /* This driconf bit enables D3D10+ behaviour for texture coordinate
         * rounding. As D3D9 wants the Vulkan behaviour instead, apply the
         * workaround only to D3D10+.
         */
        instance->force_filter_addr_rounding &= !is_d3d9;
    }

    instance->disable_lto =
        driQueryOptionb(&instance->dri_options, "anv_disable_link_time_optimization");
    instance->enable_opt_divergent_atomics =
        driQueryOptioni(&instance->dri_options, "anv_enable_opt_divergent_atomics");

    instance->stack_ids = driQueryOptioni(&instance->dri_options, "intel_stack_id");
    switch (instance->stack_ids) {
    case 256:
    case 512:
    case 1024:
    case 2048:
       break;
    default:
       mesa_logw("Invalid value provided for drirc intel_stack_id=%u, reverting to 512.",
                 instance->stack_ids);
       instance->stack_ids = 512;
       break;
    }
    instance->force_guc_low_latency =
       driQueryOptionb(&instance->dri_options, "force_guc_low_latency");

   instance->dispatch_timeout_counter =
      driQueryOptioni(&instance->dri_options, "dispatch_timeout_counter");
   switch(instance->dispatch_timeout_counter) {
   case 64:
   case 128:
   case 192:
   case 256:
   case 384:
   case 512:
   case 640:
   case 768:
   case 896:
   case 1024:
   case 1152:
   case 1280:
   case 1408:
   case 1536:
   case 1664:
   case 1792:
   case 1920:
   case 2048:
   case 4096:
      break;
   default:
       mesa_logw("Invalid value provided for drirc dispatch_timeout_counter=%u, reverting to 512.",
                 instance->dispatch_timeout_counter);
       instance->dispatch_timeout_counter = 512;
       break;
   }
}

VkResult anv_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
   struct anv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator, sizeof(*instance), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &anv_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk, &instance_extensions,
                             &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(NULL, result);
   }

   instance->vk.physical_devices.try_create_for_drm = anv_physical_device_try_create;
   instance->vk.physical_devices.destroy = anv_physical_device_destroy;

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   anv_init_dri_options(instance);

   static once_flag process_anv_debug_variable_flag = ONCE_FLAG_INIT;
   call_once(&process_anv_debug_variable_flag,
             process_anv_debug_variable_once);

   process_intel_debug_variable();
   instance->vk.enable_debug_logging = INTEL_DEBUG(DEBUG_PERF);

   intel_driver_ds_init();

   *pInstance = anv_instance_to_handle(instance);

   return VK_SUCCESS;
}

void anv_DestroyInstance(
    VkInstance                                  _instance,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

PFN_vkVoidFunction anv_GetInstanceProcAddr(
    VkInstance                                  _instance,
    const char*                                 pName)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   return vk_instance_get_proc_addr(instance ? &instance->vk : NULL,
                                    &anv_instance_entrypoints,
                                    pName);
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
   return anv_GetInstanceProcAddr(instance, pName);
}
