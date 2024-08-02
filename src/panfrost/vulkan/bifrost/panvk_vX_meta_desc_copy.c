/*
 * Copyright © 2024 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>

#include "genxml/gen_macros.h"

#include "nir.h"
#include "nir_builder.h"

#include "pan_encoder.h"
#include "pan_shader.h"

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_shader.h"

struct pan_nir_desc_copy_info {
   mali_ptr sets[MAX_SETS];
   mali_ptr tables[PANVK_BIFROST_DESC_TABLE_COUNT];
   mali_ptr img_attrib_table;
   struct {
      mali_ptr table;
      uint32_t limits[PANVK_BIFROST_DESC_TABLE_COUNT];
      uint32_t attrib_buf_idx_offset;
   } desc_copy;
   uint32_t set_desc_counts[MAX_SETS];
};

#define get_input_field(b, name)                                               \
   nir_load_push_constant(                                                     \
      b, 1, sizeof(((struct pan_nir_desc_copy_info *)0)->name) * 8,            \
      nir_imm_int(b, 0),                                                       \
      .base = offsetof(struct pan_nir_desc_copy_info, name),                   \
      .range = sizeof(((struct pan_nir_desc_copy_info *)0)->name))

#define get_input_array_slot(b, name, index)                                   \
   nir_load_push_constant(                                                     \
      b, 1, sizeof(((struct pan_nir_desc_copy_info *)0)->name[0]) * 8,         \
      nir_imul_imm(b, index,                                                   \
                   sizeof(((struct pan_nir_desc_copy_info *)0)->name[0])),     \
      .base = offsetof(struct pan_nir_desc_copy_info, name),                   \
      .range = sizeof(((struct pan_nir_desc_copy_info *)0)->name))

static void
extract_desc_info_from_handle(nir_builder *b, nir_def *handle, nir_def **table,
                              nir_def **desc_idx)
{
   *table = nir_ushr_imm(b, handle, 28);
   *desc_idx = nir_iand_imm(b, handle, 0xfffffff);
}

static void
set_to_table_copy(nir_builder *b, nir_def *set_ptr, nir_def *set_desc_count,
                  nir_def *src_desc_idx, nir_def *table_ptr,
                  nir_def *dst_desc_idx, unsigned element_size)
{
   /* The last binding can have
    * VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT set, we need to make
    * we don't do an out-of-bound access on the source set. */
   nir_def *dst_offset =
      nir_u2u64(b, nir_imul_imm(b, dst_desc_idx, element_size));

   nir_push_if(b, nir_ult(b, src_desc_idx, set_desc_count));
   {
      nir_def *src_offset =
         nir_u2u64(b, nir_imul_imm(b, src_desc_idx, PANVK_DESCRIPTOR_SIZE));
      nir_def *desc = nir_load_global(b, nir_iadd(b, set_ptr, src_offset),
                                      element_size, element_size / 4, 32);
      nir_store_global(b, nir_iadd(b, table_ptr, dst_offset), element_size,
                       desc, ~0);
   }
   nir_push_else(b, NULL);
   {
      nir_const_value v[] = {
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
      };

      nir_def *desc = nir_build_imm(b, element_size / 4, 32, v);
      nir_store_global(b, nir_iadd(b, table_ptr, dst_offset), element_size,
                       desc, ~0);
   }
   nir_pop_if(b, NULL);
}

static void
set_to_table_img_copy(nir_builder *b, nir_def *set_ptr, nir_def *set_desc_count,
                      nir_def *src_desc_idx, nir_def *attrib_table_ptr,
                      nir_def *attrib_buf_table_ptr, nir_def *dst_desc_idx)
{
   /* The last binding can have
    * VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT set, we need to make
    * sure we don't do an out-of-bound access on the source set. */
   const unsigned element_size = pan_size(ATTRIBUTE_BUFFER) * 2;
   const unsigned attrib_buf_comps = element_size / 4;
   const unsigned attrib_comps = pan_size(ATTRIBUTE) / 4;
   nir_def *attrib_offset =
      nir_u2u64(b, nir_imul_imm(b, dst_desc_idx, pan_size(ATTRIBUTE)));
   nir_def *attrib_buf_offset =
      nir_u2u64(b, nir_imul_imm(b, dst_desc_idx, element_size));

   nir_push_if(b, nir_ult(b, src_desc_idx, set_desc_count));
   {
      nir_def *attr_buf_idx_offset =
         get_input_field(b, desc_copy.attrib_buf_idx_offset);
      nir_def *src_offset =
         nir_u2u64(b, nir_imul_imm(b, src_desc_idx, PANVK_DESCRIPTOR_SIZE));
      nir_def *src_desc = nir_load_global(b, nir_iadd(b, set_ptr, src_offset),
                                          element_size, element_size / 4, 32);
      nir_def *fmt = nir_iand_imm(b, nir_channel(b, src_desc, 2), 0xfffffc00);

      /* Each image descriptor takes two attribute buffer slots, and we need
       * to add the attribute buffer offset to have images working with vertex
       * shader. */
      nir_def *buf_idx =
         nir_iadd(b, nir_imul_imm(b, dst_desc_idx, 2), attr_buf_idx_offset);

      nir_def *attrib_w1 = nir_ior(b, buf_idx, fmt);

      nir_def *attrib_desc = nir_vec2(b, attrib_w1, nir_imm_int(b, 0));

      nir_store_global(b, nir_iadd(b, attrib_table_ptr, attrib_offset),
                       pan_size(ATTRIBUTE), attrib_desc,
                       nir_component_mask(attrib_comps));

      nir_def *attrib_buf_desc = nir_vec8(
         b, nir_channel(b, src_desc, 0), nir_channel(b, src_desc, 1),
         nir_iand_imm(b, nir_channel(b, src_desc, 2), BITFIELD_MASK(10)),
         nir_channel(b, src_desc, 3), nir_channel(b, src_desc, 4),
         nir_channel(b, src_desc, 5), nir_channel(b, src_desc, 6),
         nir_channel(b, src_desc, 7));
      nir_store_global(b, nir_iadd(b, attrib_buf_table_ptr, attrib_buf_offset),
                       element_size, attrib_buf_desc,
                       nir_component_mask(attrib_buf_comps));
   }
   nir_push_else(b, NULL);
   {
      nir_const_value v[] = {
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
         nir_const_value_for_uint(0, 32), nir_const_value_for_uint(0, 32),
      };

      nir_def *desc =
         nir_build_imm(b, MAX2(attrib_buf_comps, attrib_comps), 32, v);

      nir_store_global(b, nir_iadd(b, attrib_buf_table_ptr, attrib_buf_offset),
                       pan_size(ATTRIBUTE), desc,
                       nir_component_mask(attrib_buf_comps));
      nir_store_global(b, nir_iadd(b, attrib_table_ptr, attrib_offset),
                       element_size, desc, nir_component_mask(attrib_comps));
   }
   nir_pop_if(b, NULL);
}

static void
single_desc_copy(nir_builder *b, nir_def *desc_copy_idx)
{
   nir_def *desc_copy_offset = nir_imul_imm(b, desc_copy_idx, sizeof(uint32_t));
   nir_def *desc_copy_ptr = nir_iadd(b, get_input_field(b, desc_copy.table),
                                     nir_u2u64(b, desc_copy_offset));
   nir_def *src_copy_handle = nir_load_global(b, desc_copy_ptr, 4, 1, 32);

   nir_def *set_idx, *src_desc_idx;
   extract_desc_info_from_handle(b, src_copy_handle, &set_idx, &src_desc_idx);

   nir_def *set_ptr = get_input_array_slot(b, sets, set_idx);
   nir_def *set_desc_count = get_input_array_slot(b, set_desc_counts, set_idx);
   nir_def *ubo_end =
      get_input_field(b, desc_copy.limits[PANVK_BIFROST_DESC_TABLE_UBO]);
   nir_def *img_end =
      get_input_field(b, desc_copy.limits[PANVK_BIFROST_DESC_TABLE_IMG]);
   nir_def *tex_end =
      get_input_field(b, desc_copy.limits[PANVK_BIFROST_DESC_TABLE_TEXTURE]);
   nir_def *sampler_end =
      get_input_field(b, desc_copy.limits[PANVK_BIFROST_DESC_TABLE_SAMPLER]);

   nir_push_if(b, nir_ult(b, desc_copy_idx, ubo_end));
   {
      nir_def *table_ptr =
         get_input_field(b, tables[PANVK_BIFROST_DESC_TABLE_UBO]);

      set_to_table_copy(b, set_ptr, set_desc_count, src_desc_idx, table_ptr,
                        desc_copy_idx, sizeof(struct mali_attribute_packed));
   }
   nir_push_else(b, NULL);
   {
      nir_push_if(b, nir_ult(b, desc_copy_idx, img_end));
      {
         nir_def *table_ptr =
            get_input_field(b, tables[PANVK_BIFROST_DESC_TABLE_IMG]);
         nir_def *attrib_table_ptr = get_input_field(b, img_attrib_table);
         nir_def *attrib_buf_table_ptr = table_ptr;

         set_to_table_img_copy(b, set_ptr, set_desc_count, src_desc_idx,
                               attrib_table_ptr, attrib_buf_table_ptr,
                               nir_isub(b, desc_copy_idx, ubo_end));
      }
      nir_push_else(b, NULL);
      {
         nir_push_if(b, nir_ult(b, desc_copy_idx, tex_end));
         {
            nir_def *table_ptr =
               get_input_field(b, tables[PANVK_BIFROST_DESC_TABLE_TEXTURE]);

            set_to_table_copy(b, set_ptr, set_desc_count, src_desc_idx,
                              table_ptr, nir_isub(b, desc_copy_idx, img_end),
                              sizeof(struct mali_texture_packed));
         }
         nir_push_else(b, NULL);
         {
            nir_push_if(b, nir_ult(b, desc_copy_idx, sampler_end));
            {
               nir_def *table_ptr =
                  get_input_field(b, tables[PANVK_BIFROST_DESC_TABLE_SAMPLER]);

               set_to_table_copy(b, set_ptr, set_desc_count, src_desc_idx,
                                 table_ptr, nir_isub(b, desc_copy_idx, tex_end),
                                 sizeof(struct mali_sampler_packed));
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static mali_ptr
panvk_meta_desc_copy_shader(struct panvk_device *dev,
                            struct pan_shader_info *shader_info)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, GENX(pan_shader_get_compiler_options)(), "%s",
      "desc_copy");

   /* We actually customize that at execution time to issue the
    * exact number of jobs. */
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   nir_def *desc_copy_id =
      nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   single_desc_copy(&b, desc_copy_id);

   struct panfrost_compile_inputs inputs = {
      .gpu_id = phys_dev->kmod.props.gpu_prod_id,
      .no_ubo_to_push = true,
   };
   struct util_dynarray binary;

   util_dynarray_init(&binary, NULL);
   pan_shader_preprocess(b.shader, inputs.gpu_id);
   GENX(pan_shader_compile)(b.shader, &inputs, &binary, shader_info);
   ralloc_free(b.shader);

   shader_info->push.count =
      DIV_ROUND_UP(sizeof(struct pan_nir_desc_copy_info), 4);

   mali_ptr shader = pan_pool_upload_aligned(&dev->meta.bin_pool.base,
                                             binary.data, binary.size, 128);

   util_dynarray_fini(&binary);
   return shader;
}

void
panvk_per_arch(meta_desc_copy_init)(struct panvk_device *dev)
{
   struct pan_shader_info shader_info;

   mali_ptr shader = panvk_meta_desc_copy_shader(dev, &shader_info);
   struct panfrost_ptr rsd =
      pan_pool_alloc_desc(&dev->meta.desc_pool.base, RENDERER_STATE);

   pan_pack(rsd.cpu, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(&shader_info, shader, &cfg);
   }

   dev->meta.desc_copy.rsd = rsd.gpu;
}

struct panfrost_ptr
panvk_per_arch(meta_get_copy_desc_job)(
   struct panvk_device *dev, struct pan_pool *desc_pool,
   const struct panvk_shader *shader,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_state *shader_desc_state,
   uint32_t attrib_buf_idx_offset)
{
   if (!shader)
      return (struct panfrost_ptr){0};

   mali_ptr copy_table = panvk_priv_mem_dev_addr(shader->desc_info.others.map);
   if (!copy_table)
      return (struct panfrost_ptr){0};

   struct pan_nir_desc_copy_info copy_info = {
      .img_attrib_table = shader_desc_state->img_attrib_table,
      .desc_copy = {
         .table = copy_table,
         .attrib_buf_idx_offset = attrib_buf_idx_offset,
      },
   };

   for (uint32_t i = 0; i < ARRAY_SIZE(copy_info.desc_copy.limits); i++)
      copy_info.desc_copy.limits[i] =
         shader->desc_info.others.count[i] +
         (i > 0 ? copy_info.desc_copy.limits[i - 1] : 0);

   for (uint32_t i = 0; i < ARRAY_SIZE(desc_state->sets); i++) {
      const struct panvk_descriptor_set *set = desc_state->sets[i];

      if (!set)
         continue;

      copy_info.sets[i] = set->descs.dev;
      copy_info.set_desc_counts[i] = set->desc_count;
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      uint32_t desc_count = shader->desc_info.others.count[i];

      if (!desc_count)
         continue;

      copy_info.tables[i] = shader_desc_state->tables[i];
   }

   mali_ptr push_uniforms =
      pan_pool_upload_aligned(desc_pool, &copy_info, sizeof(copy_info), 16);

   struct panfrost_ptr job = pan_pool_alloc_desc(desc_pool, COMPUTE_JOB);

   /* Given the per-stage max descriptors limit, we should never
    * reach the workgroup dimension limit. */
   uint32_t copy_count =
      copy_info.desc_copy.limits[PANVK_BIFROST_DESC_TABLE_COUNT - 1];

   assert(copy_count - 1 < BITFIELD_MASK(10));

   panfrost_pack_work_groups_compute(
      pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), 1, 1, 1, copy_count, 1,
      1, false, false);

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(copy_count + 1) +
                           util_logbase2_ceil(1 + 1) +
                           util_logbase2_ceil(1 + 1);
   }

   struct pan_tls_info tlsinfo = {0};
   struct panfrost_ptr tls = pan_pool_alloc_desc(desc_pool, LOCAL_STORAGE);

   GENX(pan_emit_tls)(&tlsinfo, tls.cpu);

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = dev->meta.desc_copy.rsd;
      cfg.push_uniforms = push_uniforms;
      cfg.thread_storage = tls.gpu;
   }

   return job;
}
