/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <new>
#include "perf/intel_perf_metrics_library.h"
#include "metrics_library/metrics_library_api_1_0.h"

#include "perf/intel_perf.h"

using namespace MetricsLibraryApi;

static ClientGen get_client_gen_from_devinfo(const struct intel_device_info *devinfo)
{
   switch (devinfo->verx10) {
   case 90:
      return ClientGen::Gen9;
   case 110:
      return ClientGen::Gen11;
   case 120:
      return ClientGen::Gen12;
   case 125:
      return ClientGen::XeHPG;
   case 200:
      return ClientGen::Xe2HPG;
   case 300:
      return ClientGen::Xe3;
   default:
      return ClientGen::Unknown;
   }
}

static uint32_t intel_metrics_library_get_query_report_size(struct intel_perf_config *perf, bool gpu_report)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api)
      return 0;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->GetParameter)
      return 0;

   TypedValue_1_0 value = {};

   ParameterType parameter = gpu_report ? ParameterType::QueryHwCountersReportGpuSize : ParameterType::QueryHwCountersReportApiSize;

   StatusCode status = api->GetParameter(parameter, &value.Type, &value);
   if (status != StatusCode::Success || value.Type != ValueType::Uint32)
      return 0;

   return value.ValueUInt32;
}

bool intel_perf_init_metrics_library(struct intel_perf_config *perf, int fd)
{
   const char *libstr = "libigdml.so.1";
   void *metrics_library = dlopen(libstr, RTLD_NOW | RTLD_LOCAL);

   if (!metrics_library)
      return false;

   ContextCreateFunction_1_0 create_context = (ContextCreateFunction_1_0)dlsym(metrics_library, METRICS_LIBRARY_CONTEXT_CREATE_1_0);
   ContextDeleteFunction_1_0 destroy_context = (ContextDeleteFunction_1_0)dlsym(metrics_library, METRICS_LIBRARY_CONTEXT_DELETE_1_0);

   if (!create_context || !destroy_context) {
      dlclose(metrics_library);
      return false;
   }

   StatusCode status;
   ContextHandle_1_0 context_handle;
   ClientType_1_0 client_type = {};
   ClientData_1_0 client_data = {};
   ClientDataLinuxAdapter_1_0 adapter_data = {};
   ContextCreateData_1_0 create_data = {};
   ClientOptionsData_1_0 client_options[3] = {};

   client_options[0].Type = ClientOptionsType::SubDevice;
   client_options[0].SubDevice.Enabled = false;
   client_options[1].Type = ClientOptionsType::SubDeviceIndex;
   client_options[1].SubDeviceIndex.Index = 0;
   client_options[2].Type = ClientOptionsType::SubDeviceCount;
   client_options[2].SubDeviceCount.Count = 1;

   client_type.Gen = get_client_gen_from_devinfo(perf->devinfo);
   client_type.Api = ClientApi::Vulkan;

   client_data.Linux.Adapter = &adapter_data;
   client_data.Linux.Adapter->Type = LinuxAdapterType::DrmFileDescriptor;
   client_data.Linux.Adapter->DrmFileDescriptor = fd;
   client_data.ClientOptions = client_options;
   client_data.ClientOptionsCount = sizeof(client_options) / sizeof(client_options[0]);

   perf->metrics_library.api = new (std::nothrow) Interface_1_0();
   perf->metrics_library.callbacks = new (std::nothrow) ClientCallbacks_1_0();

   create_data.Api = (Interface_1_0*)perf->metrics_library.api;
   create_data.ClientCallbacks = (ClientCallbacks_1_0*)perf->metrics_library.callbacks;
   create_data.ClientData = &client_data;

   status = create_context(client_type, &create_data, &context_handle);
   if (status != StatusCode::Success || !context_handle.data) {
      delete (Interface_1_0*)perf->metrics_library.api;
      perf->metrics_library.api = nullptr;
      delete (ClientCallbacks_1_0*)perf->metrics_library.callbacks;
      perf->metrics_library.callbacks = nullptr;
      dlclose(metrics_library);
      return false;
   }

   perf->metrics_library.lib = metrics_library;
   perf->metrics_library.context = (void*)context_handle.data;
   perf->metrics_library.destroy_context_func = (void*)destroy_context;
   perf->metrics_library.gpu_report_size = intel_metrics_library_get_query_report_size(perf, true);
   perf->metrics_library.api_report_size = intel_metrics_library_get_query_report_size(perf, false);

   return true;
}

bool intel_perf_deinit_metrics_library(struct intel_perf_config *perf)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.destroy_context_func)
      return false;

   ContextHandle_1_0 context_handle = {};
   context_handle.data = perf->metrics_library.context;

   ContextDeleteFunction_1_0 destroy_context = (ContextDeleteFunction_1_0)perf->metrics_library.destroy_context_func;
   StatusCode status = destroy_context(context_handle);
   if (status != StatusCode::Success)
      return false;

   delete (Interface_1_0*)perf->metrics_library.api;
   perf->metrics_library.api = nullptr;
   delete (ClientCallbacks_1_0*)perf->metrics_library.callbacks;
   perf->metrics_library.callbacks = nullptr;
   dlclose(perf->metrics_library.lib);
   perf->metrics_library.lib = nullptr;
   perf->metrics_library.context = nullptr;
   perf->metrics_library.destroy_context_func = nullptr;

   return true;
}

uint64_t intel_perf_metrics_library_create_configuration(struct intel_perf_config *perf)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api)
      return 0;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->ConfigurationCreate)
      return 0;

   ConfigurationHandle_1_0 config_handle = {};
   ConfigurationCreateData_1_0 create_data = {};

   create_data.HandleContext.data = perf->metrics_library.context;
   create_data.Type = ObjectType::ConfigurationHwCountersOa;

   StatusCode status = api->ConfigurationCreate(&create_data, &config_handle);
   if (status != StatusCode::Success || !config_handle.data)
      return 0;

   /* Assuming the configuration handle's data can be interpreted as a uint64_t ID. */
   return reinterpret_cast<uint64_t>(config_handle.data);
}

bool intel_perf_metrics_library_destroy_configuration(struct intel_perf_config *perf, uint64_t config_id)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api)
      return false;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->ConfigurationDelete)
      return false;

   ConfigurationHandle_1_0 config_handle = {};
   config_handle.data = reinterpret_cast<void*>(config_id);

   StatusCode status = api->ConfigurationDelete(config_handle);
   return status == StatusCode::Success;
}

void* intel_perf_metrics_library_create_query_pool(struct intel_perf_config *perf, uint32_t query_count)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api)
      return nullptr;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->QueryCreate)
      return nullptr;

   QueryCreateData_1_0 create_data = {};
   create_data.HandleContext.data = perf->metrics_library.context;
   create_data.Type = ObjectType::QueryHwCounters;
   create_data.Slots = query_count;

   QueryHandle_1_0 query_handle = {};
   StatusCode status = api->QueryCreate(&create_data, &query_handle);
   if (status != StatusCode::Success || !query_handle.data)
      return nullptr;

   return query_handle.data;
}

bool intel_perf_metrics_library_destroy_query_pool(struct intel_perf_config *perf, void* query_pool)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api)
      return false;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->QueryDelete)
      return false;

   QueryHandle_1_0 query_handle = {};
   query_handle.data = query_pool;

   StatusCode status = api->QueryDelete(query_handle);
   return status == StatusCode::Success;
}

bool intel_perf_metrics_library_get_query_results(struct intel_perf_config *perf, void* query_pool, void* data, uint32_t query_index, bool* write_results)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api || !write_results)
      return false;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->GetData)
      return false;

   GetReportData_1_0 report_data = {};
   report_data.Type = ObjectType::QueryHwCounters;
   report_data.Query.Handle.data = query_pool;
   report_data.Query.Slot = query_index;
   report_data.Query.SlotsCount = 1;
   report_data.Query.DataSize = perf->metrics_library.api_report_size;
   report_data.Query.Data = data;

   StatusCode status = api->GetData(&report_data);

   if (status == StatusCode::Success) {
      *write_results = true;
      return true;
   } else if (status == StatusCode::ReportNotReady) {
      *write_results = false;
      return true;
   } else {
      *write_results = false;
      return false;
   }
}

bool intel_perf_metrics_library_activate_configuration(struct intel_perf_config *perf, uint64_t config_id)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api)
      return false;

   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;
   if (!api->ConfigurationActivate)
      return false;

   ConfigurationHandle_1_0 config_handle = {};
   config_handle.data = reinterpret_cast<void*>(config_id);

   ConfigurationActivateData_1_0 activate_data = {};
   activate_data.Type = GpuConfigurationActivationType::Tbs;

   StatusCode status = api->ConfigurationActivate(config_handle, &activate_data);
   return status == StatusCode::Success;
}

bool intel_perf_metrics_library_get_stream_marker_cmds(struct intel_perf_config *perf, uint32_t marker_value, void* cmds, uint32_t* cmds_size)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api || !cmds_size)
      return false;

   StatusCode status = StatusCode::Failed;
   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;

   if (!cmds || *cmds_size == 0)
   {
      if (!api->CommandBufferGetSize)
         return false;

      CommandBufferData_1_0 data = {};
      CommandBufferSize_1_0 size = {};

      data.HandleContext.data = perf->metrics_library.context;
      data.CommandsType = ObjectType::MarkerStreamUser;
      data.Type = GpuCommandBufferType::Render;
      data.MarkerStreamUser.Value = marker_value;

      status = api->CommandBufferGetSize(&data, &size);

      if (status != StatusCode::Success)
         return false;

      *cmds_size = size.GpuMemorySize;
      return true;
   }

   if (cmds && *cmds_size > 0)
   {
      CommandBufferData_1_0 data = {};
      data.HandleContext.data = perf->metrics_library.context;
      data.CommandsType = ObjectType::MarkerStreamUser;
      data.Type = GpuCommandBufferType::Render;
      data.MarkerStreamUser.Value = marker_value;
      data.Data = cmds;
      data.Size = *cmds_size;

      status = api->CommandBufferGet(&data);

      if (status != StatusCode::Success)
         return false;

      return true;
   }

   return false;
}

bool intel_perf_metrics_library_get_perf_query_cmds(
   struct intel_perf_config *perf,
   void* metrics_library_query_pool,
   uint64_t gpu_memory_offset,
   void* cpu_memory_offset,
   uint32_t query_index,
   uint64_t perf_marker,
   bool begin,
   void* cmds,
   uint32_t* cmds_size)
{
   if (!perf->metrics_library.lib || !perf->metrics_library.context || !perf->metrics_library.api || !cmds_size || !metrics_library_query_pool)
      return false;

   StatusCode status = StatusCode::Failed;
   Interface_1_0* api = (Interface_1_0*)perf->metrics_library.api;

   if (!cmds || *cmds_size == 0)
   {
      if (!api->CommandBufferGetSize)
         return false;

      CommandBufferData_1_0 data = {};
      CommandBufferSize_1_0 size = {};

      data.HandleContext.data = perf->metrics_library.context;
      data.CommandsType = ObjectType::QueryHwCounters;
      data.Type = GpuCommandBufferType::Render;
      data.QueryHwCounters.Handle.data = metrics_library_query_pool;
      data.QueryHwCounters.Slot = query_index;
      data.QueryHwCounters.MarkerUser = perf_marker;
      data.QueryHwCounters.Begin = begin;
      data.Allocation.GpuAddress = gpu_memory_offset;
      data.Allocation.CpuAddress = cpu_memory_offset;

      status = api->CommandBufferGetSize(&data, &size);

      if (status != StatusCode::Success)
         return false;

      *cmds_size = size.GpuMemorySize;
      return true;
   }

   if (cmds && *cmds_size > 0)
   {
      CommandBufferData_1_0 data = {};
      data.HandleContext.data = perf->metrics_library.context;
      data.CommandsType = ObjectType::QueryHwCounters;
      data.Type = GpuCommandBufferType::Render;
      data.QueryHwCounters.Handle.data = metrics_library_query_pool;
      data.QueryHwCounters.Slot = query_index;
      data.QueryHwCounters.MarkerUser = perf_marker;
      data.QueryHwCounters.Begin = begin;
      data.Allocation.GpuAddress = gpu_memory_offset;
      data.Allocation.CpuAddress = cpu_memory_offset;
      data.Data = cmds;
      data.Size = *cmds_size;

      status = api->CommandBufferGet(&data);

      if (status != StatusCode::Success)
         return false;

      return true;
   }

   return false;
}
