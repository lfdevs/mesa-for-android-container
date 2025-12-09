#include "panvk_queue.h"

VkResult
panvk_per_arch(create_bind_queue)(
   struct panvk_device *device, const VkDeviceQueueCreateInfo *create_info,
   uint32_t queue_idx, struct vk_queue **out_queue)
{
   return VK_ERROR_INITIALIZATION_FAILED;
}

void
panvk_per_arch(destroy_bind_queue)(struct vk_queue *vk_queue)
{
   UNREACHABLE("");
}

VkResult
panvk_per_arch(bind_queue_submit)(struct vk_queue *vk_queue,
                                  struct vk_queue_submit *vk_submit)
{
   UNREACHABLE("");
}

VkResult
panvk_per_arch(bind_queue_check_status)(struct vk_queue *vk_queue)
{
   UNREACHABLE("");
}
