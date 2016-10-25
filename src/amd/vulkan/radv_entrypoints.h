/* This file generated from vk_gen.py, don't edit directly. */

struct radv_dispatch_table {
   union {
      void *entrypoints[169];
      struct {
         PFN_vkCreateInstance CreateInstance;
         PFN_vkDestroyInstance DestroyInstance;
         PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
         PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
         PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
         PFN_vkGetPhysicalDeviceImageFormatProperties GetPhysicalDeviceImageFormatProperties;
         PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
         PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
         PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
         PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
         PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
         PFN_vkCreateDevice CreateDevice;
         PFN_vkDestroyDevice DestroyDevice;
         PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
         PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;
         PFN_vkEnumerateInstanceLayerProperties EnumerateInstanceLayerProperties;
         PFN_vkEnumerateDeviceLayerProperties EnumerateDeviceLayerProperties;
         PFN_vkGetDeviceQueue GetDeviceQueue;
         PFN_vkQueueSubmit QueueSubmit;
         PFN_vkQueueWaitIdle QueueWaitIdle;
         PFN_vkDeviceWaitIdle DeviceWaitIdle;
         PFN_vkAllocateMemory AllocateMemory;
         PFN_vkFreeMemory FreeMemory;
         PFN_vkMapMemory MapMemory;
         PFN_vkUnmapMemory UnmapMemory;
         PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges;
         PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges;
         PFN_vkGetDeviceMemoryCommitment GetDeviceMemoryCommitment;
         PFN_vkBindBufferMemory BindBufferMemory;
         PFN_vkBindImageMemory BindImageMemory;
         PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
         PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
         PFN_vkGetImageSparseMemoryRequirements GetImageSparseMemoryRequirements;
         PFN_vkGetPhysicalDeviceSparseImageFormatProperties GetPhysicalDeviceSparseImageFormatProperties;
         PFN_vkQueueBindSparse QueueBindSparse;
         PFN_vkCreateFence CreateFence;
         PFN_vkDestroyFence DestroyFence;
         PFN_vkResetFences ResetFences;
         PFN_vkGetFenceStatus GetFenceStatus;
         PFN_vkWaitForFences WaitForFences;
         PFN_vkCreateSemaphore CreateSemaphore;
         PFN_vkDestroySemaphore DestroySemaphore;
         PFN_vkCreateEvent CreateEvent;
         PFN_vkDestroyEvent DestroyEvent;
         PFN_vkGetEventStatus GetEventStatus;
         PFN_vkSetEvent SetEvent;
         PFN_vkResetEvent ResetEvent;
         PFN_vkCreateQueryPool CreateQueryPool;
         PFN_vkDestroyQueryPool DestroyQueryPool;
         PFN_vkGetQueryPoolResults GetQueryPoolResults;
         PFN_vkCreateBuffer CreateBuffer;
         PFN_vkDestroyBuffer DestroyBuffer;
         PFN_vkCreateBufferView CreateBufferView;
         PFN_vkDestroyBufferView DestroyBufferView;
         PFN_vkCreateImage CreateImage;
         PFN_vkDestroyImage DestroyImage;
         PFN_vkGetImageSubresourceLayout GetImageSubresourceLayout;
         PFN_vkCreateImageView CreateImageView;
         PFN_vkDestroyImageView DestroyImageView;
         PFN_vkCreateShaderModule CreateShaderModule;
         PFN_vkDestroyShaderModule DestroyShaderModule;
         PFN_vkCreatePipelineCache CreatePipelineCache;
         PFN_vkDestroyPipelineCache DestroyPipelineCache;
         PFN_vkGetPipelineCacheData GetPipelineCacheData;
         PFN_vkMergePipelineCaches MergePipelineCaches;
         PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
         PFN_vkCreateComputePipelines CreateComputePipelines;
         PFN_vkDestroyPipeline DestroyPipeline;
         PFN_vkCreatePipelineLayout CreatePipelineLayout;
         PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
         PFN_vkCreateSampler CreateSampler;
         PFN_vkDestroySampler DestroySampler;
         PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
         PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
         PFN_vkCreateDescriptorPool CreateDescriptorPool;
         PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
         PFN_vkResetDescriptorPool ResetDescriptorPool;
         PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
         PFN_vkFreeDescriptorSets FreeDescriptorSets;
         PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
         PFN_vkCreateFramebuffer CreateFramebuffer;
         PFN_vkDestroyFramebuffer DestroyFramebuffer;
         PFN_vkCreateRenderPass CreateRenderPass;
         PFN_vkDestroyRenderPass DestroyRenderPass;
         PFN_vkGetRenderAreaGranularity GetRenderAreaGranularity;
         PFN_vkCreateCommandPool CreateCommandPool;
         PFN_vkDestroyCommandPool DestroyCommandPool;
         PFN_vkResetCommandPool ResetCommandPool;
         PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
         PFN_vkFreeCommandBuffers FreeCommandBuffers;
         PFN_vkBeginCommandBuffer BeginCommandBuffer;
         PFN_vkEndCommandBuffer EndCommandBuffer;
         PFN_vkResetCommandBuffer ResetCommandBuffer;
         PFN_vkCmdBindPipeline CmdBindPipeline;
         PFN_vkCmdSetViewport CmdSetViewport;
         PFN_vkCmdSetScissor CmdSetScissor;
         PFN_vkCmdSetLineWidth CmdSetLineWidth;
         PFN_vkCmdSetDepthBias CmdSetDepthBias;
         PFN_vkCmdSetBlendConstants CmdSetBlendConstants;
         PFN_vkCmdSetDepthBounds CmdSetDepthBounds;
         PFN_vkCmdSetStencilCompareMask CmdSetStencilCompareMask;
         PFN_vkCmdSetStencilWriteMask CmdSetStencilWriteMask;
         PFN_vkCmdSetStencilReference CmdSetStencilReference;
         PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
         PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
         PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;
         PFN_vkCmdDraw CmdDraw;
         PFN_vkCmdDrawIndexed CmdDrawIndexed;
         PFN_vkCmdDrawIndirect CmdDrawIndirect;
         PFN_vkCmdDrawIndexedIndirect CmdDrawIndexedIndirect;
         PFN_vkCmdDispatch CmdDispatch;
         PFN_vkCmdDispatchIndirect CmdDispatchIndirect;
         PFN_vkCmdCopyBuffer CmdCopyBuffer;
         PFN_vkCmdCopyImage CmdCopyImage;
         PFN_vkCmdBlitImage CmdBlitImage;
         PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
         PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
         PFN_vkCmdUpdateBuffer CmdUpdateBuffer;
         PFN_vkCmdFillBuffer CmdFillBuffer;
         PFN_vkCmdClearColorImage CmdClearColorImage;
         PFN_vkCmdClearDepthStencilImage CmdClearDepthStencilImage;
         PFN_vkCmdClearAttachments CmdClearAttachments;
         PFN_vkCmdResolveImage CmdResolveImage;
         PFN_vkCmdSetEvent CmdSetEvent;
         PFN_vkCmdResetEvent CmdResetEvent;
         PFN_vkCmdWaitEvents CmdWaitEvents;
         PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
         PFN_vkCmdBeginQuery CmdBeginQuery;
         PFN_vkCmdEndQuery CmdEndQuery;
         PFN_vkCmdResetQueryPool CmdResetQueryPool;
         PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
         PFN_vkCmdCopyQueryPoolResults CmdCopyQueryPoolResults;
         PFN_vkCmdPushConstants CmdPushConstants;
         PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
         PFN_vkCmdNextSubpass CmdNextSubpass;
         PFN_vkCmdEndRenderPass CmdEndRenderPass;
         PFN_vkCmdExecuteCommands CmdExecuteCommands;
         PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
         PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
         PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
         PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
         PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
         PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
         PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
         PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
         PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
         PFN_vkQueuePresentKHR QueuePresentKHR;
         PFN_vkGetPhysicalDeviceDisplayPropertiesKHR GetPhysicalDeviceDisplayPropertiesKHR;
         PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR GetPhysicalDeviceDisplayPlanePropertiesKHR;
         PFN_vkGetDisplayPlaneSupportedDisplaysKHR GetDisplayPlaneSupportedDisplaysKHR;
         PFN_vkGetDisplayModePropertiesKHR GetDisplayModePropertiesKHR;
         PFN_vkCreateDisplayModeKHR CreateDisplayModeKHR;
         PFN_vkGetDisplayPlaneCapabilitiesKHR GetDisplayPlaneCapabilitiesKHR;
         PFN_vkCreateDisplayPlaneSurfaceKHR CreateDisplayPlaneSurfaceKHR;
         PFN_vkCreateSharedSwapchainsKHR CreateSharedSwapchainsKHR;
#ifdef VK_USE_PLATFORM_XLIB_KHR
         PFN_vkCreateXlibSurfaceKHR CreateXlibSurfaceKHR;
#else
         void *CreateXlibSurfaceKHR;
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
         PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR GetPhysicalDeviceXlibPresentationSupportKHR;
#else
         void *GetPhysicalDeviceXlibPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
         PFN_vkCreateXcbSurfaceKHR CreateXcbSurfaceKHR;
#else
         void *CreateXcbSurfaceKHR;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
         PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR GetPhysicalDeviceXcbPresentationSupportKHR;
#else
         void *GetPhysicalDeviceXcbPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
         PFN_vkCreateWaylandSurfaceKHR CreateWaylandSurfaceKHR;
#else
         void *CreateWaylandSurfaceKHR;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
         PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR GetPhysicalDeviceWaylandPresentationSupportKHR;
#else
         void *GetPhysicalDeviceWaylandPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_MIR_KHR
         PFN_vkCreateMirSurfaceKHR CreateMirSurfaceKHR;
#else
         void *CreateMirSurfaceKHR;
#endif
#ifdef VK_USE_PLATFORM_MIR_KHR
         PFN_vkGetPhysicalDeviceMirPresentationSupportKHR GetPhysicalDeviceMirPresentationSupportKHR;
#else
         void *GetPhysicalDeviceMirPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
         PFN_vkCreateAndroidSurfaceKHR CreateAndroidSurfaceKHR;
#else
         void *CreateAndroidSurfaceKHR;
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
         PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR;
#else
         void *CreateWin32SurfaceKHR;
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
         PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR GetPhysicalDeviceWin32PresentationSupportKHR;
#else
         void *GetPhysicalDeviceWin32PresentationSupportKHR;
#endif
         PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallbackEXT;
         PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallbackEXT;
         PFN_vkDebugReportMessageEXT DebugReportMessageEXT;
      };

   };

};

VkResult radv_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance);
void radv_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator);
VkResult radv_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
void radv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures);
void radv_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties);
VkResult radv_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties);
void radv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
void radv_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties);
void radv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties);
PFN_vkVoidFunction radv_GetInstanceProcAddr(VkInstance instance, const char* pName);
PFN_vkVoidFunction radv_GetDeviceProcAddr(VkDevice device, const char* pName);
VkResult radv_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
void radv_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);
VkResult radv_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
VkResult radv_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
VkResult radv_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties);
VkResult radv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties);
void radv_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);
VkResult radv_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
VkResult radv_QueueWaitIdle(VkQueue queue);
VkResult radv_DeviceWaitIdle(VkDevice device);
VkResult radv_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
void radv_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator);
VkResult radv_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);
void radv_UnmapMemory(VkDevice device, VkDeviceMemory memory);
VkResult radv_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges);
VkResult radv_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges);
void radv_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes);
VkResult radv_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VkResult radv_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
void radv_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements);
void radv_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements);
void radv_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements);
void radv_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties);
VkResult radv_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence);
VkResult radv_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence);
void radv_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator);
VkResult radv_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences);
VkResult radv_GetFenceStatus(VkDevice device, VkFence fence);
VkResult radv_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout);
VkResult radv_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
void radv_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent);
void radv_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator);
VkResult radv_GetEventStatus(VkDevice device, VkEvent event);
VkResult radv_SetEvent(VkDevice device, VkEvent event);
VkResult radv_ResetEvent(VkDevice device, VkEvent event);
VkResult radv_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool);
void radv_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator);
VkResult radv_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags);
VkResult radv_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer);
void radv_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView);
void radv_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
void radv_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator);
void radv_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout);
VkResult radv_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView);
void radv_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule);
void radv_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache);
void radv_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator);
VkResult radv_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData);
VkResult radv_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches);
VkResult radv_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
VkResult radv_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
void radv_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout);
void radv_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler);
void radv_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout);
void radv_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool);
void radv_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator);
VkResult radv_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags);
VkResult radv_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets);
VkResult radv_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets);
void radv_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies);
VkResult radv_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer);
void radv_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator);
VkResult radv_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
void radv_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator);
void radv_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity);
VkResult radv_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);
void radv_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator);
VkResult radv_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
VkResult radv_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers);
void radv_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);
VkResult radv_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
VkResult radv_EndCommandBuffer(VkCommandBuffer commandBuffer);
VkResult radv_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
void radv_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
void radv_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);
void radv_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
void radv_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth);
void radv_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor);
void radv_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]);
void radv_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds);
void radv_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask);
void radv_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask);
void radv_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference);
void radv_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
void radv_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
void radv_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
void radv_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void radv_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void radv_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void radv_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
void radv_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z);
void radv_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset);
void radv_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions);
void radv_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions);
void radv_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter);
void radv_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions);
void radv_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
void radv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const uint32_t* pData);
void radv_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data);
void radv_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);
void radv_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);
void radv_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects);
void radv_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions);
void radv_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
void radv_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask);
void radv_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
void radv_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
void radv_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags);
void radv_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query);
void radv_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
void radv_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query);
void radv_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags);
void radv_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues);
void radv_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
void radv_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents);
void radv_CmdEndRenderPass(VkCommandBuffer commandBuffer);
void radv_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);
void radv_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator);
VkResult radv_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported);
VkResult radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
VkResult radv_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats);
VkResult radv_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes);
VkResult radv_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
void radv_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator);
VkResult radv_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages);
VkResult radv_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
VkResult radv_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
VkResult radv_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties);
VkResult radv_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties);
VkResult radv_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays);
VkResult radv_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties);
VkResult radv_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR*pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode);
VkResult radv_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities);
VkResult radv_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
VkResult radv_CreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains);
#ifdef VK_USE_PLATFORM_XLIB_KHR
VkResult radv_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
VkBool32 radv_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID);
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
VkResult radv_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
VkBool32 radv_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id);
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkResult radv_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkBool32 radv_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display);
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkResult radv_CreateMirSurfaceKHR(VkInstance instance, const VkMirSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkBool32 radv_GetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, MirConnection* connection);
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
VkResult radv_CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkResult radv_CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkBool32 radv_GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex);
#endif // VK_USE_PLATFORM_WIN32_KHR
VkResult radv_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback);
void radv_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator);
void radv_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage);
