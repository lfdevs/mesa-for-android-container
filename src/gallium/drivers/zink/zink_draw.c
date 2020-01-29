#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_program.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_state.h"

#include "indices/u_primconvert.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_prim.h"

static VkDescriptorSet
allocate_descriptor_set(struct zink_screen *screen,
                        struct zink_batch *batch,
                        struct zink_gfx_program *prog)
{
   assert(batch->descs_left >= prog->num_descriptors);
   VkDescriptorSetAllocateInfo dsai;
   memset((void *)&dsai, 0, sizeof(dsai));
   dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   dsai.pNext = NULL;
   dsai.descriptorPool = batch->descpool;
   dsai.descriptorSetCount = 1;
   dsai.pSetLayouts = &prog->dsl;

   VkDescriptorSet desc_set;
   if (vkAllocateDescriptorSets(screen->dev, &dsai, &desc_set) != VK_SUCCESS) {
      debug_printf("ZINK: failed to allocate descriptor set :/");
      return VK_NULL_HANDLE;
   }

   batch->descs_left -= prog->num_descriptors;
   return desc_set;
}

static void
zink_bind_vertex_buffers(struct zink_batch *batch, struct zink_context *ctx)
{
   VkBuffer buffers[PIPE_MAX_ATTRIBS];
   VkDeviceSize buffer_offsets[PIPE_MAX_ATTRIBS];
   const struct zink_vertex_elements_state *elems = ctx->element_state;
   for (unsigned i = 0; i < elems->hw_state.num_bindings; i++) {
      struct pipe_vertex_buffer *vb = ctx->buffers + ctx->element_state->binding_map[i];
      assert(vb && vb->buffer.resource);
      struct zink_resource *res = zink_resource(vb->buffer.resource);
      buffers[i] = res->buffer;
      buffer_offsets[i] = vb->buffer_offset;
      zink_batch_reference_resoure(batch, res);
   }

   if (elems->hw_state.num_bindings > 0)
      vkCmdBindVertexBuffers(batch->cmdbuf, 0,
                             elems->hw_state.num_bindings,
                             buffers, buffer_offsets);
}

static struct zink_gfx_program *
get_gfx_program(struct zink_context *ctx)
{
   if (ctx->dirty_program) {
      struct hash_entry *entry = _mesa_hash_table_search(ctx->program_cache,
                                                         ctx->gfx_stages);
      if (!entry) {
         struct zink_gfx_program *prog;
         prog = zink_create_gfx_program(zink_screen(ctx->base.screen),
                                                     ctx->gfx_stages);
         entry = _mesa_hash_table_insert(ctx->program_cache, prog->stages, prog);
         if (!entry)
            return NULL;
      }
      ctx->curr_program = entry->data;
      ctx->dirty_program = false;
   }

   assert(ctx->curr_program);
   return ctx->curr_program;
}

static bool
line_width_needed(enum pipe_prim_type reduced_prim,
                  VkPolygonMode polygon_mode)
{
   switch (reduced_prim) {
   case PIPE_PRIM_POINTS:
      return false;

   case PIPE_PRIM_LINES:
      return true;

   case PIPE_PRIM_TRIANGLES:
      return polygon_mode == VK_POLYGON_MODE_LINE;

   default:
      unreachable("unexpected reduced prim");
   }
}

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_rasterizer_state *rast_state = ctx->rast_state;

   if (dinfo->mode >= PIPE_PRIM_QUADS ||
       dinfo->mode == PIPE_PRIM_LINE_LOOP ||
       dinfo->index_size == 1) {
      if (!u_trim_pipe_prim(dinfo->mode, (unsigned *)&dinfo->count))
         return;

      util_primconvert_save_rasterizer_state(ctx->primconvert, &rast_state->base);
      util_primconvert_draw_vbo(ctx->primconvert, dinfo);
      return;
   }

   struct zink_gfx_program *gfx_program = get_gfx_program(ctx);
   if (!gfx_program)
      return;

   VkPipeline pipeline = zink_get_gfx_pipeline(screen, gfx_program,
                                               &ctx->gfx_pipeline_state,
                                               dinfo->mode);

   enum pipe_prim_type reduced_prim = u_reduced_prim(dinfo->mode);

   bool depth_bias = false;
   switch (reduced_prim) {
   case PIPE_PRIM_POINTS:
      depth_bias = rast_state->offset_point;
      break;

   case PIPE_PRIM_LINES:
      depth_bias = rast_state->offset_line;
      break;

   case PIPE_PRIM_TRIANGLES:
      depth_bias = rast_state->offset_tri;
      break;

   default:
      unreachable("unexpected reduced prim");
   }

   unsigned index_offset = 0;
   struct pipe_resource *index_buffer = NULL;
   if (dinfo->index_size > 0) {
      if (dinfo->has_user_indices) {
         if (!util_upload_index_buffer(pctx, dinfo, &index_buffer, &index_offset, 4)) {
            debug_printf("util_upload_index_buffer() failed\n");
            return;
         }
      } else
         index_buffer = dinfo->index.resource;
   }

   VkWriteDescriptorSet wds[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS + PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   VkDescriptorBufferInfo buffer_infos[PIPE_SHADER_TYPES * PIPE_MAX_CONSTANT_BUFFERS];
   VkDescriptorImageInfo image_infos[PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   int num_wds = 0, num_buffer_info = 0, num_image_info = 0;

   struct zink_resource *transitions[PIPE_SHADER_TYPES * PIPE_MAX_SHADER_SAMPLER_VIEWS];
   int num_transitions = 0;

   for (int i = 0; i < ARRAY_SIZE(ctx->gfx_stages); i++) {
      struct zink_shader *shader = ctx->gfx_stages[i];
      if (!shader)
         continue;

      for (int j = 0; j < shader->num_bindings; j++) {
         int index = shader->bindings[j].index;
         if (shader->bindings[j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            assert(ctx->ubos[i][index].buffer_size > 0);
            assert(ctx->ubos[i][index].buffer_size <= screen->props.limits.maxUniformBufferRange);
            assert(ctx->ubos[i][index].buffer);
            struct zink_resource *res = zink_resource(ctx->ubos[i][index].buffer);
            buffer_infos[num_buffer_info].buffer = res->buffer;
            buffer_infos[num_buffer_info].offset = ctx->ubos[i][index].buffer_offset;
            buffer_infos[num_buffer_info].range  = ctx->ubos[i][index].buffer_size;
            wds[num_wds].pBufferInfo = buffer_infos + num_buffer_info;
            ++num_buffer_info;
         } else {
            struct pipe_sampler_view *psampler_view = ctx->image_views[i][index];
            assert(psampler_view);
            struct zink_sampler_view *sampler_view = zink_sampler_view(psampler_view);

            struct zink_resource *res = zink_resource(psampler_view->texture);
            VkImageLayout layout = res->layout;
            if (layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
                layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                layout != VK_IMAGE_LAYOUT_GENERAL) {
               transitions[num_transitions++] = res;
               layout = VK_IMAGE_LAYOUT_GENERAL;
            }
            image_infos[num_image_info].imageLayout = layout;
            image_infos[num_image_info].imageView = sampler_view->image_view;
            image_infos[num_image_info].sampler = ctx->samplers[i][index];
            wds[num_wds].pImageInfo = image_infos + num_image_info;
            ++num_image_info;
         }

         wds[num_wds].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
         wds[num_wds].pNext = NULL;
         wds[num_wds].dstBinding = shader->bindings[j].binding;
         wds[num_wds].dstArrayElement = 0;
         wds[num_wds].descriptorCount = 1;
         wds[num_wds].descriptorType = shader->bindings[j].type;
         ++num_wds;
      }
   }

   struct zink_batch *batch;
   if (num_transitions > 0) {
      batch = zink_batch_no_rp(ctx);

      for (int i = 0; i < num_transitions; ++i)
         zink_resource_barrier(batch->cmdbuf, transitions[i],
                               transitions[i]->aspect,
                               VK_IMAGE_LAYOUT_GENERAL);
   }

   batch = zink_batch_rp(ctx);

   if (batch->descs_left < gfx_program->num_descriptors) {
      ctx->base.flush(&ctx->base, NULL, 0);
      batch = zink_batch_rp(ctx);
      assert(batch->descs_left >= gfx_program->num_descriptors);
   }

   VkDescriptorSet desc_set = allocate_descriptor_set(screen, batch,
                                                      gfx_program);
   assert(desc_set != VK_NULL_HANDLE);

   for (int i = 0; i < ARRAY_SIZE(ctx->gfx_stages); i++) {
      struct zink_shader *shader = ctx->gfx_stages[i];
      if (!shader)
         continue;

      for (int j = 0; j < shader->num_bindings; j++) {
         int index = shader->bindings[j].index;
         if (shader->bindings[j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            struct zink_resource *res = zink_resource(ctx->ubos[i][index].buffer);
            zink_batch_reference_resoure(batch, res);
         } else {
            struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->image_views[i][index]);
            zink_batch_reference_sampler_view(batch, sampler_view);
         }
      }
   }

   vkCmdSetViewport(batch->cmdbuf, 0, ctx->num_viewports, ctx->viewports);
   if (ctx->rast_state->base.scissor)
      vkCmdSetScissor(batch->cmdbuf, 0, ctx->num_viewports, ctx->scissors);
   else if (ctx->fb_state.width && ctx->fb_state.height) {
      VkRect2D fb_scissor = {};
      fb_scissor.extent.width = ctx->fb_state.width;
      fb_scissor.extent.height = ctx->fb_state.height;
      vkCmdSetScissor(batch->cmdbuf, 0, 1, &fb_scissor);
   }

   if (line_width_needed(reduced_prim, rast_state->hw_state.polygon_mode)) {
      if (screen->feats.wideLines || ctx->line_width == 1.0f)
         vkCmdSetLineWidth(batch->cmdbuf, ctx->line_width);
      else
         debug_printf("BUG: wide lines not supported, needs fallback!");
   }

   vkCmdSetStencilReference(batch->cmdbuf, VK_STENCIL_FACE_FRONT_BIT, ctx->stencil_ref.ref_value[0]);
   vkCmdSetStencilReference(batch->cmdbuf, VK_STENCIL_FACE_BACK_BIT, ctx->stencil_ref.ref_value[1]);

   if (depth_bias)
      vkCmdSetDepthBias(batch->cmdbuf, rast_state->offset_units, rast_state->offset_clamp, rast_state->offset_scale);
   else
      vkCmdSetDepthBias(batch->cmdbuf, 0.0f, 0.0f, 0.0f);

   if (ctx->gfx_pipeline_state.blend_state->need_blend_constants)
      vkCmdSetBlendConstants(batch->cmdbuf, ctx->blend_constants);

   if (num_wds > 0) {
      for (int i = 0; i < num_wds; ++i)
         wds[i].dstSet = desc_set;
      vkUpdateDescriptorSets(screen->dev, num_wds, wds, 0, NULL);
   }

   vkCmdBindPipeline(batch->cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   vkCmdBindDescriptorSets(batch->cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           gfx_program->layout, 0, 1, &desc_set, 0, NULL);
   zink_bind_vertex_buffers(batch, ctx);

   if (dinfo->index_size > 0) {
      assert(dinfo->index_size != 1);
      VkIndexType index_type = dinfo->index_size == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
      struct zink_resource *res = zink_resource(index_buffer);
      vkCmdBindIndexBuffer(batch->cmdbuf, res->buffer, index_offset, index_type);
      zink_batch_reference_resoure(batch, res);
      vkCmdDrawIndexed(batch->cmdbuf,
         dinfo->count, dinfo->instance_count,
         dinfo->start, dinfo->index_bias, dinfo->start_instance);
   } else
      vkCmdDraw(batch->cmdbuf, dinfo->count, dinfo->instance_count, dinfo->start, dinfo->start_instance);

   if (dinfo->index_size > 0 && dinfo->has_user_indices)
      pipe_resource_reference(&index_buffer, NULL);
}
