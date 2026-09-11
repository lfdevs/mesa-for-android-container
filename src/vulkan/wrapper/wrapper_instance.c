#include "wrapper_private.h"
#include "wrapper_log.h"
#include "graphicsenv_hook.hpp"
#include "wrapper_entrypoints.h"
#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_dispatch_table.h"
#include "vk_extensions.h"

const struct vk_instance_extension_table wrapper_instance_extensions = {
   .KHR_get_surface_capabilities2 = true,
   .EXT_surface_maintenance1 = true,
   .KHR_surface_protected_capabilities = true,
   .KHR_surface = true,
   .EXT_swapchain_colorspace = true,
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   .KHR_android_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .KHR_get_display_properties2 = true,
   .EXT_display_surface_counter = true,
   .EXT_acquire_drm_display = true,
   .EXT_direct_mode_display = true,
#endif
   .EXT_headless_surface = true,
};

static const char *layers[] = {
   "VK_LAYER_KHRONOS_validation"
};


static void *vulkan_library_handle;
static PFN_vkCreateInstance create_instance;
static PFN_vkGetInstanceProcAddr get_instance_proc_addr;
static PFN_vkEnumerateInstanceVersion enumerate_instance_version;
static PFN_vkEnumerateInstanceExtensionProperties enumerate_instance_extension_properties;
static PFN_vkEnumerateInstanceLayerProperties enumerate_instance_layer_properties;
static PFN_vkCreateDebugUtilsMessengerEXT create_debug_utils_messenger;
static PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug_utils_messenger;
VkDebugUtilsMessengerEXT debugUtilsMessenger;
static struct vk_instance_extension_table *supported_instance_extensions;

bool has_intercepted_layer_paths = false;

#ifdef __LP64__
#define DEFAULT_VULKAN_PATH "/system/lib64/libvulkan.so"
#else
#define DEFAULT_VULKAN_PATH "/system/lib/libvulkan.so"
#endif

#include <dlfcn.h>

static void init_debug_messenger(VkInstance instance)
{
  create_debug_utils_messenger = (PFN_vkCreateDebugUtilsMessengerEXT)get_instance_proc_addr(instance, "vkCreateDebugUtilsMessengerEXT");
  destroy_debug_utils_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)get_instance_proc_addr(instance, "vkDestroyDebugUtilsMessengerEXT");

  if (create_debug_utils_messenger && destroy_debug_utils_messenger) {
     WRAPPER_LOG(info, "Creating debug messenger");
     VkDebugUtilsMessengerCreateInfoEXT messengerInfo;
     const VkDebugUtilsMessageSeverityFlagsEXT kSeveritiesToLog =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

     const VkDebugUtilsMessageTypeFlagsEXT kMessagesToLog =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

     messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
     messengerInfo.pNext = NULL;
     messengerInfo.flags = 0;
     messengerInfo.messageSeverity = kSeveritiesToLog;
     messengerInfo.messageType = kMessagesToLog;
     messengerInfo.pfnUserCallback = &wrapper_debug_utils_messenger;
     messengerInfo.pUserData = NULL;

     create_debug_utils_messenger(instance, &messengerInfo, NULL, &debugUtilsMessenger);
   }

}

static void *get_vulkan_handle()
{
   init_wrapper_logging();

   if (WRAPPER_LOG_LEVEL(validation)) {
      has_intercepted_layer_paths = set_layer_paths();
   }

   const char *path = getenv("WRAPPER_VULKAN_PATH");
   return dlopen(path ? path : DEFAULT_VULKAN_PATH, RTLD_NOW | RTLD_LOCAL);
}


static bool vulkan_library_init()
{
   if (vulkan_library_handle)
      return true;

   vulkan_library_handle = get_vulkan_handle();

   if (vulkan_library_handle) {
      create_instance = dlsym(vulkan_library_handle, "vkCreateInstance");
      get_instance_proc_addr = dlsym(vulkan_library_handle,
                                     "vkGetInstanceProcAddr");
      enumerate_instance_version = dlsym(vulkan_library_handle,
                                         "vkEnumerateInstanceVersion");
      enumerate_instance_extension_properties =
         dlsym(vulkan_library_handle, "vkEnumerateInstanceExtensionProperties");
      enumerate_instance_layer_properties =
         dlsym(vulkan_library_handle, "vkEnumerateInstanceLayerProperties");
   }
   else {
      fprintf(stderr, "%s", dlerror());
   }

   return vulkan_library_handle ? true : false;
}

static VkResult wrapper_vulkan_init()
{
   VkExtensionProperties props[VK_INSTANCE_EXTENSION_COUNT];
   uint32_t prop_count = VK_INSTANCE_EXTENSION_COUNT;
   VkResult result;

   if (supported_instance_extensions)
      return VK_SUCCESS;

   if (!vulkan_library_init())
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   result = enumerate_instance_extension_properties(NULL, &prop_count, props);
   if (result != VK_SUCCESS)
      return result;

   supported_instance_extensions = malloc(sizeof(*supported_instance_extensions));
   if (!supported_instance_extensions)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   *supported_instance_extensions = wrapper_instance_extensions;

   for(int i = 0; i < prop_count; i++) {
      int idx;
      for (idx = 0; idx < VK_INSTANCE_EXTENSION_COUNT; idx++) {
         if (strcmp(vk_instance_extensions[idx].extensionName,
                    props[i].extensionName) == 0)
            break;
      }

      if (idx >= VK_INSTANCE_EXTENSION_COUNT)
         continue;

      supported_instance_extensions->extensions[idx] = true;
   }

   supported_instance_extensions->EXT_debug_utils = false;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_EnumerateInstanceVersion(uint32_t* pApiVersion)
{

   if (!vulkan_library_init())
      return vk_error(NULL, VK_ERROR_INCOMPATIBLE_DRIVER);

   return enumerate_instance_version(pApiVersion);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_EnumerateInstanceExtensionProperties(const char* pLayerName,
                                             uint32_t* pPropertyCount,
                                             VkExtensionProperties* pProperties)
{
   VkResult result;

   result = wrapper_vulkan_init();
   if (result != VK_SUCCESS)
      return vk_error(NULL, result);

   return vk_enumerate_instance_extension_properties(supported_instance_extensions,
                                                     pPropertyCount,
                                                     pProperties);
}

static inline void
set_wrapper_required_extensions(const struct vk_instance *instance,
                                uint32_t *enable_extension_count,
                                const char **enable_extensions)
{
   uint32_t count = *enable_extension_count;
#define REQUIRED_EXTENSION(name) \
   assert (count < VK_INSTANCE_EXTENSION_COUNT); \
   if (supported_instance_extensions->name) { \
      enable_extensions[count++] = "VK_" #name; \
   }
   REQUIRED_EXTENSION(KHR_get_physical_device_properties2);
   REQUIRED_EXTENSION(KHR_surface);
   REQUIRED_EXTENSION(KHR_external_fence_capabilities);
   REQUIRED_EXTENSION(KHR_external_memory_capabilities);
   REQUIRED_EXTENSION(KHR_external_semaphore_capabilities);
#undef REQUIRED_EXTENSION
   *enable_extension_count = count;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkInstance *pInstance)
{
   const char *wrapper_enable_extensions[VK_INSTANCE_EXTENSION_COUNT];
   uint32_t wrapper_enable_extension_count = 0;
   VkApplicationInfo wrapper_application_info = {};
   VkInstanceCreateInfo wrapper_create_info = *pCreateInfo;
   struct vk_instance_dispatch_table dispatch_table;
   struct wrapper_instance *instance;
   VkResult result;

   result = wrapper_vulkan_init();
   if (result != VK_SUCCESS)
      return vk_error(NULL, result);

   instance = vk_zalloc2(vk_default_allocator(), pAllocator, sizeof(*instance),
                         8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wrapper_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk, supported_instance_extensions,
                             &dispatch_table, pCreateInfo,
                             pAllocator ? pAllocator : vk_default_allocator());

   if (result != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed to init instance, res %d", result);
      vk_free2(vk_default_allocator(), pAllocator, instance);
      return vk_error(NULL, result);
   }

   instance->vk.physical_devices.enumerate = enumerate_physical_device;
   instance->vk.physical_devices.destroy = destroy_physical_device;

   for (int idx = 0; idx < pCreateInfo->enabledExtensionCount; idx++) {
      if (wrapper_instance_extensions.extensions[idx])
         continue;

      if (!instance->vk.enabled_extensions.extensions[idx])
         continue;

      wrapper_enable_extensions[wrapper_enable_extension_count++] =
         vk_instance_extensions[idx].extensionName;
   }

   set_wrapper_required_extensions(&instance->vk,
                                   &wrapper_enable_extension_count,
                                   wrapper_enable_extensions);

   if (wrapper_create_info.pApplicationInfo) {
      wrapper_application_info = *wrapper_create_info.pApplicationInfo;
   } else {
      wrapper_application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
      wrapper_application_info.pApplicationName = "wrapper";
      wrapper_application_info.pEngineName = "wrapper";
      enumerate_instance_version(&wrapper_application_info.apiVersion);
   }

   wrapper_create_info.pApplicationInfo = &wrapper_application_info;

   if (WRAPPER_LOG_LEVEL(validation)) {
      if (!has_intercepted_layer_paths)
         return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

      uint32_t layer_count = 0;
      enumerate_instance_layer_properties(&layer_count, NULL);

      if (layer_count == 0) {
        WRAPPER_LOG(error, "Failed to find Vulkan Validation layer");
	return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
      }

      VkLayerProperties layer_props[layer_count];
      enumerate_instance_layer_properties(&layer_count, layer_props);

      for (int i = 0; i < layer_count; i++) {
         if (!strcmp(layer_props[i].layerName, layers[0])) {
            wrapper_create_info.enabledLayerCount = 1;
            wrapper_create_info.ppEnabledLayerNames = layers;
            const VkBool32 settings_validate_best_practices_arm = VK_TRUE;
            const VkLayerSettingEXT layer_settings[] = {
               {layers[0], "validate_best_practices_arm", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &settings_validate_best_practices_arm},
               {layers[0], "validate_best_practices", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &settings_validate_best_practices_arm}
            };
            VkLayerSettingsCreateInfoEXT layer_settings_info = {
               VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, NULL, 2, layer_settings,
            };
            layer_settings_info.pNext = wrapper_create_info.pNext;
            wrapper_create_info.pNext = &layer_settings_info;
         }
      }
   }

   wrapper_create_info.enabledExtensionCount = wrapper_enable_extension_count;
   wrapper_create_info.ppEnabledExtensionNames = wrapper_enable_extensions;

   result = create_instance(&wrapper_create_info, pAllocator,
                            &instance->dispatch_handle);
   if (result != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed driver createInstance, res %d", result);
      vk_instance_finish(&instance->vk);
      vk_free2(vk_default_allocator(), pAllocator, instance);
      return vk_error(NULL, result);
   }

   if (WRAPPER_LOG_LEVEL(validation)) {
      init_debug_messenger(instance->dispatch_handle);
   }

   vk_instance_dispatch_table_load(&instance->dispatch_table,
                                   get_instance_proc_addr,
                                   instance->dispatch_handle);

   *pInstance = wrapper_instance_to_handle(instance);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyInstance(VkInstance _instance,
                        const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(wrapper_instance, instance, _instance);

   if (destroy_debug_utils_messenger)
      destroy_debug_utils_messenger(instance->dispatch_handle, debugUtilsMessenger, pAllocator);

   instance->dispatch_table.DestroyInstance(instance->dispatch_handle,
                                            pAllocator);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
wrapper_GetInstanceProcAddr(VkInstance _instance,
                            const char *pName)
{
   VK_FROM_HANDLE(wrapper_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk,
                                    &wrapper_instance_entrypoints,
                                    pName);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char *pName);


PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char *pName)
{
   return wrapper_GetInstanceProcAddr(instance, pName);
}
