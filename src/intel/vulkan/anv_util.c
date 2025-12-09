/*
 * Copyright © 2015 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "util/u_printf.h"
#include <sys/stat.h>

#include "anv_private.h"
#include "vk_enum_to_str.h"

#include "compiler/brw/brw_nir_rt.h"

#ifdef NO_REGEX
typedef int regex_t;
#define REG_EXTENDED 0
#define REG_NOSUB 0
#define REG_NOMATCH 1
static inline int regcomp(regex_t *r, const char *s, int f) { return 0; }
static inline int regexec(regex_t *r, const char *s, int n, void *p, int f) { return REG_NOMATCH; }
static inline void regfree(regex_t* r) {}
#else
#include <regex.h>
#endif
#include "util/u_process.h"

void
__anv_perf_warn(struct anv_device *device,
                const struct vk_object_base *object,
                const char *file, int line, const char *format, ...)
{
   va_list ap;
   char buffer[256];

   va_start(ap, format);
   vsnprintf(buffer, sizeof(buffer), format, ap);
   va_end(ap);

   if (object) {
      __vk_log(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
               VK_LOG_OBJS(object), file, line,
               "PERF: %s", buffer);
   } else {
      __vk_log(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
               VK_LOG_NO_OBJS(device->physical->instance), file, line,
               "PERF: %s", buffer);
   }
}

void
anv_cmd_buffer_pending_pipe_debug(struct anv_cmd_buffer *cmd_buffer,
                                  enum anv_pipe_bits bits,
                                  const char* reason)
{
   if (bits == 0)
      return;

   struct log_stream *stream = mesa_log_streami();

   mesa_log_stream_printf(stream, "acc: ");

   mesa_log_stream_printf(stream, "bits: ");
   anv_dump_pipe_bits(bits, stream);
   mesa_log_stream_printf(stream, "reason: %s", reason);

   mesa_log_stream_printf(stream, "\n");

   mesa_log_stream_destroy(stream);
}

void
anv_dump_pipe_bits(enum anv_pipe_bits bits, struct log_stream *stream)
{
   if (bits & ANV_PIPE_DEPTH_CACHE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+depth_flush ");
   if (bits & ANV_PIPE_DATA_CACHE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+dc_flush ");
   if (bits & ANV_PIPE_HDC_PIPELINE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+hdc_flush ");
   if (bits & ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+rt_flush ");
   if (bits & ANV_PIPE_TILE_CACHE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+tile_flush ");
   if (bits & ANV_PIPE_L3_FABRIC_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+l3_fabric_flush ");
   if (bits & ANV_PIPE_STATE_CACHE_INVALIDATE_BIT)
      mesa_log_stream_printf(stream, "+state_inval ");
   if (bits & ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT)
      mesa_log_stream_printf(stream, "+const_inval ");
   if (bits & ANV_PIPE_VF_CACHE_INVALIDATE_BIT)
      mesa_log_stream_printf(stream, "+vf_inval ");
   if (bits & ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT)
      mesa_log_stream_printf(stream, "+tex_inval ");
   if (bits & ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT)
      mesa_log_stream_printf(stream, "+ic_inval ");
   if (bits & ANV_PIPE_STALL_AT_SCOREBOARD_BIT)
      mesa_log_stream_printf(stream, "+pb_stall ");
   if (bits & ANV_PIPE_PSS_STALL_SYNC_BIT)
      mesa_log_stream_printf(stream, "+pss_stall ");
   if (bits & ANV_PIPE_DEPTH_STALL_BIT)
      mesa_log_stream_printf(stream, "+depth_stall ");
   if (bits & ANV_PIPE_CS_STALL_BIT ||
       bits & ANV_PIPE_END_OF_PIPE_SYNC_BIT)
      mesa_log_stream_printf(stream, "+cs_stall ");
   if (bits & ANV_PIPE_UNTYPED_DATAPORT_CACHE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+utdp_flush ");
   if (bits & ANV_PIPE_CCS_CACHE_FLUSH_BIT)
      mesa_log_stream_printf(stream, "+ccs_flush ");
}

const char *
anv_gfx_state_bit_to_str(enum anv_gfx_state_bits state)
{
#define NAME(name) case ANV_GFX_STATE_##name: return #name;
   switch (state) {
      NAME(URB);
      NAME(VF_STATISTICS);
      NAME(VF_SGVS);
      NAME(VF_SGVS_2);
      NAME(VF_SGVS_INSTANCING);
      NAME(PRIMITIVE_REPLICATION);
      NAME(MULTISAMPLE);
      NAME(SBE);
      NAME(SBE_SWIZ);
      NAME(SO_DECL_LIST);
      NAME(VS);
      NAME(HS);
      NAME(DS);
      NAME(GS);
      NAME(PS);
      NAME(PS_EXTRA);
      NAME(SBE_MESH);
      NAME(CLIP_MESH);
      NAME(MESH_CONTROL);
      NAME(MESH_SHADER);
      NAME(MESH_DISTRIB);
      NAME(TASK_CONTROL);
      NAME(TASK_SHADER);
      NAME(TASK_REDISTRIB);
      NAME(CLIP);
      NAME(CC_STATE);
      NAME(CPS);
      NAME(DEPTH_BOUNDS);
      NAME(INDEX_BUFFER);
      NAME(LINE_STIPPLE);
      NAME(PS_BLEND);
      NAME(RASTER);
      NAME(SAMPLE_MASK);
      NAME(SAMPLE_PATTERN);
      NAME(SCISSOR);
      NAME(SF);
      NAME(STREAMOUT);
      NAME(TE);
      NAME(VERTEX_INPUT);
      NAME(VF);
      NAME(VF_TOPOLOGY);
      NAME(VFG);
      NAME(VIEWPORT_CC);
      NAME(VIEWPORT_SF_CLIP);
      NAME(WM);
      NAME(WM_DEPTH_STENCIL);
      NAME(PMA_FIX);
      NAME(WA_18019816803);
      NAME(WA_14018283232);
      NAME(TBIMR_TILE_PASS_INFO);
      NAME(FS_MSAA_FLAGS);
      NAME(TESS_CONFIG);
      NAME(MESH_PROVOKING_VERTEX);
   default: UNREACHABLE("invalid state");
   }
}

VkResult
anv_device_print_init(struct anv_device *device)
{
   struct anv_bo *bo;
   VkResult result =
      anv_device_alloc_bo(device, "printf",
                          anv_printf_buffer_size(),
                          ANV_BO_ALLOC_CAPTURE |
                          ANV_BO_ALLOC_MAPPED |
                          ANV_BO_ALLOC_HOST_COHERENT |
                          ANV_BO_ALLOC_NO_LOCAL_MEM,
                          0 /* explicit_address */,
                          &bo);
   if (result != VK_SUCCESS)
      return result;

   u_printf_init(&device->printf, bo, (uint32_t*)bo->map);
   return VK_SUCCESS;
}

void
anv_device_print_fini(struct anv_device *device)
{
   anv_device_release_bo(device, device->printf.bo);
   u_printf_destroy(&device->printf);
}

static void
create_directory(const char *dir, const char *sub_dir)
{
   char full_path[PATH_MAX];
   snprintf(full_path, sizeof(full_path), "%s/%s", dir, sub_dir);

   if (mkdir(dir, 0777) == -1 && errno != EEXIST) {
      perror("Error creating directory");
      return;
   }

   if (mkdir(full_path, 0777) == -1 && errno != EEXIST) {
      perror("Error creating sub directory");
      return;
   }
}

static void
create_bvh_dump_file(struct anv_bvh_dump *bvh)
{
   if (bvh == NULL) {
      fprintf(stderr, "Error: BVH DUMP structure is NULL\n");
      return;
   }

   char file_name[256];
   const char *dump_directory = "bvh_dump";
   const char *dump_sub_directory = NULL;

   switch (bvh->dump_type) {
   case BVH_ANV:
      dump_sub_directory = "BVH_ANV";
      break;
   case BVH_IR_HDR:
      dump_sub_directory = "BVH_IR_HDR";
      break;
   case BVH_IR_AS:
      dump_sub_directory = "BVH_IR_AS";
      break;
   default:
      UNREACHABLE("invalid dump type");
   }

   create_directory(dump_directory, dump_sub_directory);

   snprintf(file_name, sizeof(file_name),
            bvh->geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR
               ? "%s/%s/tlas_%d.txt"
               : "%s/%s/blas_%d.txt",
            dump_directory, dump_sub_directory, bvh->bvh_id);

   FILE *file = fopen(file_name, "w");
   if (file == NULL) {
      perror("Error creating file");
      return;
   }

   fprintf(stderr, "BVH Dump File created: %s\n", file_name);

   uint8_t *addr = (uint8_t *)(bvh->bo->map);
   /* Dump every bytes like this: B0 B1 B2 B3 ... B15 */
   for (uint64_t i = 0; i < bvh->dump_size; i++) {
      uint8_t result = *(volatile uint8_t *)((uint8_t *)addr + i);
      fprintf(file, "%02" PRIx8 " ", result);
      if ((i + 1) % 16 == 0) {
         fprintf(file, "\n");
      }
   }

   fclose(file);
}

void anv_dump_bvh_to_files(struct anv_device *device)
{
   /* device->mutex is acquired in anv_queue_submit, so no need to lock here. */
   list_for_each_entry_safe(struct anv_bvh_dump, bvh_dump, &device->bvh_dumps,
                            link) {
      create_bvh_dump_file(bvh_dump);

      anv_device_release_bo(device, bvh_dump->bo);
      list_del(&bvh_dump->link);
      free(bvh_dump);
   }
}

DEBUG_GET_ONCE_OPTION(anv_debug_wait_for_attach, "ANV_DEBUG_WAIT_FOR_ATTACH", NULL);

void anv_wait_for_attach() {
   const char *attach_regex =
      debug_get_option_anv_debug_wait_for_attach();
   if (unlikely(attach_regex != NULL)) {
      bool wait_for_attach = false;
      const char *exec_name = util_get_process_name();
      regex_t re;
      int compile_res = regcomp(&re, attach_regex, REG_EXTENDED|REG_NOSUB);
      if (compile_res != 0) {
         char compile_err[256];
         regerror(compile_res, &re, compile_err, 256);
         fprintf(stderr, "ANV_DEBUG_WAIT_FOR_ATTACH regex compile fail: %s\n",
                 compile_err);
      } else {
         wait_for_attach = (regexec(&re, exec_name, 0, NULL, 0) == 0);
         regfree(&re);
      }

      if (wait_for_attach) {
         fprintf(stderr, "Sleeping 30 seconds for debugger attach...\n");
         fprintf(stderr, "PID for debugger: %d\n", getpid());
         sleep(30);
      }
   }
}

VkResult
anv_device_init_rt_shaders(struct anv_device *device)
{
   if (!device->vk.enabled_extensions.KHR_ray_tracing_pipeline)
      return VK_SUCCESS;

   bool cache_hit;

   struct anv_push_descriptor_info empty_push_desc_info = {};
   struct anv_pipeline_bind_map empty_bind_map = {};
   struct brw_rt_trampoline {
      char name[16];
      struct brw_cs_prog_key key;
   } trampoline_key = {
      .name = "rt-trampoline",
   };
   device->rt_trampoline =
      anv_device_search_for_kernel(device, device->internal_cache,
                                   &trampoline_key, sizeof(trampoline_key),
                                   &cache_hit);
   if (device->rt_trampoline == NULL) {

      void *tmp_ctx = ralloc_context(NULL);
      nir_shader *trampoline_nir =
         brw_nir_create_raygen_trampoline(device->physical->compiler, tmp_ctx);

      unsigned require_size = device->info->ver >= 20 ? 16 : 8;
      trampoline_nir->info.api_subgroup_size = require_size;
      trampoline_nir->info.max_subgroup_size = require_size;
      trampoline_nir->info.min_subgroup_size = require_size;

      struct brw_cs_prog_data trampoline_prog_data = {
         .uses_btd_stack_ids = true,
      };
      struct brw_compile_cs_params params = {
         .base = {
            .nir = trampoline_nir,
            .log_data = device,
            .mem_ctx = tmp_ctx,
         },
         .key = &trampoline_key.key,
         .prog_data = &trampoline_prog_data,
      };
      const unsigned *tramp_data =
         brw_compile_cs(device->physical->compiler, &params);

      struct anv_shader_upload_params upload_params = {
         .stage               = MESA_SHADER_COMPUTE,
         .key_data            = &trampoline_key,
         .key_size            = sizeof(trampoline_key),
         .kernel_data         = tramp_data,
         .kernel_size         = trampoline_prog_data.base.program_size,
         .prog_data           = &trampoline_prog_data.base,
         .prog_data_size      = sizeof(trampoline_prog_data),
         .bind_map            = &empty_bind_map,
         .push_desc_info      = &empty_push_desc_info,
      };

      device->rt_trampoline =
         anv_device_upload_kernel(device, device->internal_cache,
                                  &upload_params);

      ralloc_free(tmp_ctx);

      if (device->rt_trampoline == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_internal_unref(device, device->rt_trampoline);

   struct brw_rt_trivial_return {
      char name[16];
      struct brw_bs_prog_key key;
   } return_key = {
      .name = "rt-trivial-ret",
   };
   device->rt_trivial_return =
      anv_device_search_for_kernel(device, device->internal_cache,
                                   &return_key, sizeof(return_key),
                                   &cache_hit);
   if (device->rt_trivial_return == NULL) {
      void *tmp_ctx = ralloc_context(NULL);
      nir_shader *trivial_return_nir =
         brw_nir_create_trivial_return_shader(device->physical->compiler, tmp_ctx);

      NIR_PASS(_, trivial_return_nir, brw_nir_lower_rt_intrinsics,
                 &return_key.key.base, device->info);

      struct brw_bs_prog_data return_prog_data = { 0, };
      struct brw_compile_bs_params params = {
         .base = {
            .nir = trivial_return_nir,
            .log_data = device,
            .mem_ctx = tmp_ctx,
         },
         .key = &return_key.key,
         .prog_data = &return_prog_data,
      };
      const unsigned *return_data =
         brw_compile_bs(device->physical->compiler, &params);

      struct anv_shader_upload_params upload_params = {
         .stage               = MESA_SHADER_CALLABLE,
         .key_data            = &return_key,
         .key_size            = sizeof(return_key),
         .kernel_data         = return_data,
         .kernel_size         = return_prog_data.base.program_size,
         .prog_data           = &return_prog_data.base,
         .prog_data_size      = sizeof(return_prog_data),
         .bind_map            = &empty_bind_map,
         .push_desc_info      = &empty_push_desc_info,
      };

      device->rt_trivial_return =
         anv_device_upload_kernel(device, device->internal_cache,
                                  &upload_params);

      ralloc_free(tmp_ctx);

      if (device->rt_trivial_return == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_internal_unref(device, device->rt_trivial_return);

   struct brw_rt_null_ahs {
      char name[16];
      struct brw_bs_prog_key key;
   } null_return_key = {
      .name = "rt-null-ahs",
   };
   device->rt_null_ahs =
      anv_device_search_for_kernel(device, device->internal_cache,
                                   &null_return_key, sizeof(null_return_key),
                                   &cache_hit);
   if (device->rt_null_ahs == NULL) {
      void *tmp_ctx = ralloc_context(NULL);
      nir_shader *null_ahs_nir =
         brw_nir_create_null_ahs_shader(device->physical->compiler, tmp_ctx);

      NIR_PASS(_, null_ahs_nir, brw_nir_lower_rt_intrinsics,
                 &null_return_key.key.base, device->info);

      struct brw_bs_prog_data return_prog_data = { 0, };
      struct brw_compile_bs_params params = {
         .base = {
            .nir = null_ahs_nir,
            .log_data = device,
            .mem_ctx = tmp_ctx,
         },
         .key = &null_return_key.key,
         .prog_data = &return_prog_data,
      };
      const unsigned *return_data =
         brw_compile_bs(device->physical->compiler, &params);

      struct anv_shader_upload_params upload_params = {
         .stage               = MESA_SHADER_CALLABLE,
         .key_data            = &null_return_key,
         .key_size            = sizeof(null_return_key),
         .kernel_data         = return_data,
         .kernel_size         = return_prog_data.base.program_size,
         .prog_data           = &return_prog_data.base,
         .prog_data_size      = sizeof(return_prog_data),
         .bind_map            = &empty_bind_map,
         .push_desc_info      = &empty_push_desc_info,
      };

      device->rt_null_ahs =
         anv_device_upload_kernel(device, device->internal_cache,
                                  &upload_params);

      ralloc_free(tmp_ctx);

      if (device->rt_null_ahs == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_internal_unref(device, device->rt_null_ahs);

   return VK_SUCCESS;
}

void
anv_device_finish_rt_shaders(struct anv_device *device)
{
   if (!device->vk.enabled_extensions.KHR_ray_tracing_pipeline)
      return;
}
