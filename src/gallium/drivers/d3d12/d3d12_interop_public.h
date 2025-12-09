/*
* Copyright © Microsoft Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include <stdint.h>

#ifndef D3D12_INTEROP_PUBLIC_H
#define D3D12_INTEROP_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct pipe_video_buffer;
struct pipe_context;
struct pipe_resource;

struct d3d12_interop_video_buffer_associated_data
{
   /*
    * Function to get read-only resource from video buffer.
    */
   bool (*get_read_only_resource)(struct pipe_video_buffer *buffer,
                                  struct pipe_context *pipe,
                                  struct pipe_resource **readonly_resource,
                                  uint32_t *subresource_index);
};

struct d3d12_interop_device_info {
   uint64_t adapter_luid;
   ID3D12Device *device;
   ID3D12CommandQueue *queue;
};

struct d3d12_interop_resource_info {
   ID3D12Resource *resource;
   uint64_t buffer_offset;
};

/*
 * Structure that contains information about scheduling priority management
 * for GPU workloads exposed through work queues.
 *
 * Used by gallium frontend and driver to manage scheduling priority
 * of GPU workloads.
 *
 * The frontend passes the [Input] callbacks after context creation
 * and the gallium driver fills the callbacks in this structure
 * annotated as [Output] parameters.
 *
 */
struct d3d12_context_queue_priority_manager
{
   /*
    * [Input] Register a work queue.
    *
    * The driver must call register_work_queue()
    * callback ONCE for every queue created. Multiple registrations of the same
    * queue are idempotent.
    *
    * The callback passed is expected to be thread safe.
    *
    * Parameters:
    * manager: [In] Pointer to the manager structure itself
    * queue: [In] Driver passes the queue to be registered in the frontend
    *
    * return: 0 for success, error code otherwise
    */
   int (*register_work_queue)(struct d3d12_context_queue_priority_manager* manager, ID3D12CommandQueue *queue);

   /*
    * [Input] Unregister a work queue.
    *
    * The driver must call unregister_work_queue()
    * callback ONCE for every queue destroyed
    * that was previously registered by register_work_queue()
    *
    * The callback passed is expected to be thread safe.
    *
    * The driver will call unregister_work_queue() for all registered queues
    * on destruction of the pipe_context for sanity.
    *
    * Parameters:
    * manager: [In] Pointer to the manager structure itself
    * queue: [In] Driver passes the queue to be unregistered in the frontend
    *
    * return: 0 for success, error code otherwise
    */
   int (*unregister_work_queue)(struct d3d12_context_queue_priority_manager* manager, ID3D12CommandQueue *queue);

   /*
    * [Output] Set the scheduling priority of a registered work queue.
    *
    * Frontend can call set_queue_priority() to set the priority of a registered queue.
    *
    * The function returned is expected to be thread safe.
    *
    * Parameters:
    * manager: [In] Pointer to the manager structure itself
    * queue: [In] The frontend sends one of the queues previously
    *               registered by the driver in register_work_queue, representing the
    *               queue to set the priority for
    * global_priority: [In] the global priority to be set. Value castable from D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY
    * local_priority: [In] the local priority to be set. Value castable from D3D12_COMMAND_QUEUE_PROCESS_PRIORITY
    *
    * return: 0 for success, error code otherwise
    */
   int (*set_queue_priority)(struct d3d12_context_queue_priority_manager* manager,
                             ID3D12CommandQueue *queue,
                             const uint32_t* global_priority,
                             const uint32_t* local_priority);

   /*
    * [Output] Get the scheduling priority of a registered work queue.
    *
    * The function returned is expected to be thread safe.
    *
    * Parameters:
    * manager: [In] Pointer to the manager structure itself
    * queue: [In] The frontend sends one of the queues previously
    *               registered by the driver in register_work_queue, representing the
    *               queue to set the priority for
    * global_priority: [Out] the current global priority of the queue. Value castable to D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY
    * local_priority: [Out] the current local priority of the queue. Value castable to D3D12_COMMAND_QUEUE_PROCESS_PRIORITY
    *
    * return: 0 for success, error code otherwise
    */
   int (*get_queue_priority)(struct d3d12_context_queue_priority_manager* manager,
                             ID3D12CommandQueue *queue,
                             uint32_t *global_priority,
                             uint32_t *local_priority);

   struct pipe_context *context;
};

struct d3d12_interop_device_info1 {
   uint64_t adapter_luid;
   ID3D12Device *device;
   ID3D12CommandQueue *queue;

   /*
    * Function pointer to set a queue priority manager for a context.
    * If this function is NULL, the driver does not support queue priority management.
    *
    * The lifetime of the d3d12_context_queue_priority_manager is managed by the caller,
    * and it must be valid for the duration of the context's usage.
    * The caller is responsible for destroying and cleaning up any previously
    * set manager before calling this function.
    *
    * Any objects created by pipe_context that also create work queues
    * such as pipe_video_codec, must also use the d3d12_context_queue_priority_manager,
    * and unregister any queues on destruction of such children objects.
    *
    * The driver must call unregister_work_queue() for each registered queue
    * on destruction of the pipe_context for sanity.
    *
    * Parameters:
    *   - pipe_context*: context to configure
    *   - d3d12_context_queue_priority_manager*: manager to use
    *
    * Returns int (0 for success, error code otherwise)
    */
   int (*set_context_queue_priority_manager)(struct pipe_context *context, struct d3d12_context_queue_priority_manager *manager);

   /*
    * Function pointer to set the maximum queue async depth for video encode work queues.
    * If this function is NULL, the driver does not support setting max queue depth.
    * Some frontends that have modes where they limit the number of frames in flight
    * and this function allows the frontend to communicate that to the driver.
    * That way the driver can allocate less command allocators and resources for
    * video in flight frames and reduce memory usage.
    *
    * A call to this function alters the behavior of pipe_context::create_video_codec
    * and any video codec created AFTER a call to this function will have the specified
    * max async queue depth. Created video codecs previous to calling this function are not affected.
    *
    * Parameters:
    *   - pipe_context*: context to configure
    *   - unsigned int: maximum queue depth to set
    *
    * Returns int (0 for success, error code otherwise)
    */
   int (*set_video_encoder_max_async_queue_depth)(struct pipe_context *context, uint32_t max_async_queue_depth);

   /*
   * Function pointer to get the last slice completion fence for a video encoder,
   * which may happen before the entire frame is complete, including the stats.
   * If this function is NULL, the driver does not support getting the last slice completion fence.
   *
   * Parameters:
   *   - pipe_video_codec*: codec to query
   *   - void*: feedback data to provide to the driver to indicate the frame feedback
   *              from which to get the last slice completion fence returned by
   *              pipe_video_codec::encode_bitstream/encode_bitstream_sliced.
   *   - pipe_fence_handle**: pointer to a fence handle to be filled in by the driver
   * 
   * The caller must call pipe_video_codec::destroy_fence to destroy the returned fence handle
   *
   * Returns int (0 for success, error code otherwise)
   */
   int (*get_video_enc_last_slice_completion_fence)(struct pipe_video_codec *codec,
                                                    void *feedback,
                                                    struct pipe_fence_handle **fence);
};

#ifdef __cplusplus
}
#endif

#endif /* D3D12_INTEROP_PUBLIC_H */
