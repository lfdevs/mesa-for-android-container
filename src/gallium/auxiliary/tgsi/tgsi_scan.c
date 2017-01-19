/**************************************************************************
 * 
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 * Copyright 2008 VMware, Inc.  All rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

/**
 * TGSI program scan utility.
 * Used to determine which registers and instructions are used by a shader.
 *
 * Authors:  Brian Paul
 */


#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_util.h"
#include "tgsi/tgsi_scan.h"


static bool
is_memory_file(unsigned file)
{
   return file == TGSI_FILE_SAMPLER ||
          file == TGSI_FILE_SAMPLER_VIEW ||
          file == TGSI_FILE_IMAGE ||
          file == TGSI_FILE_BUFFER;
}


static bool
is_mem_query_inst(unsigned opcode)
{
   return opcode == TGSI_OPCODE_RESQ ||
          opcode == TGSI_OPCODE_TXQ ||
          opcode == TGSI_OPCODE_TXQS ||
          opcode == TGSI_OPCODE_TXQ_LZ ||
          opcode == TGSI_OPCODE_LODQ;
}

/**
 * Is the opcode a "true" texture instruction which samples from a
 * texture map?
 */
static bool
is_texture_inst(unsigned opcode)
{
   return (!is_mem_query_inst(opcode) &&
           tgsi_get_opcode_info(opcode)->is_tex);
}


/**
 * Is the opcode an instruction which computes a derivative explicitly or
 * implicitly?
 */
static bool
computes_derivative(unsigned opcode)
{
   if (tgsi_get_opcode_info(opcode)->is_tex) {
      return opcode != TGSI_OPCODE_TG4 &&
             opcode != TGSI_OPCODE_TXD &&
             opcode != TGSI_OPCODE_TXF &&
             opcode != TGSI_OPCODE_TXL &&
             opcode != TGSI_OPCODE_TXL2 &&
             opcode != TGSI_OPCODE_TXQ &&
             opcode != TGSI_OPCODE_TXQ_LZ &&
             opcode != TGSI_OPCODE_TXQS;
   }

   return opcode == TGSI_OPCODE_DDX || opcode == TGSI_OPCODE_DDX_FINE ||
          opcode == TGSI_OPCODE_DDY || opcode == TGSI_OPCODE_DDY_FINE ||
          opcode == TGSI_OPCODE_SAMPLE ||
          opcode == TGSI_OPCODE_SAMPLE_B ||
          opcode == TGSI_OPCODE_SAMPLE_C;
}


static void
scan_src_operand(struct tgsi_shader_info *info,
                 const struct tgsi_full_instruction *fullinst,
                 const struct tgsi_full_src_register *src,
                 unsigned src_index,
                 unsigned usage_mask,
                 bool is_interp_instruction,
                 bool *is_mem_inst)
{
   int ind = src->Register.Index;

   /* Mark which inputs are effectively used */
   if (src->Register.File == TGSI_FILE_INPUT) {
      if (src->Register.Indirect) {
         for (ind = 0; ind < info->num_inputs; ++ind) {
            info->input_usage_mask[ind] |= usage_mask;
         }
      } else {
         assert(ind >= 0);
         assert(ind < PIPE_MAX_SHADER_INPUTS);
         info->input_usage_mask[ind] |= usage_mask;
      }

      if (info->processor == PIPE_SHADER_FRAGMENT) {
         unsigned name, index, input;

         if (src->Register.Indirect && src->Indirect.ArrayID)
            input = info->input_array_first[src->Indirect.ArrayID];
         else
            input = src->Register.Index;

         name = info->input_semantic_name[input];
         index = info->input_semantic_index[input];

         if (name == TGSI_SEMANTIC_POSITION &&
             (src->Register.SwizzleX == TGSI_SWIZZLE_Z ||
              src->Register.SwizzleY == TGSI_SWIZZLE_Z ||
              src->Register.SwizzleZ == TGSI_SWIZZLE_Z ||
              src->Register.SwizzleW == TGSI_SWIZZLE_Z))
            info->reads_z = TRUE;

         if (name == TGSI_SEMANTIC_COLOR) {
            unsigned mask =
               (1 << src->Register.SwizzleX) |
               (1 << src->Register.SwizzleY) |
               (1 << src->Register.SwizzleZ) |
               (1 << src->Register.SwizzleW);

            info->colors_read |= mask << (index * 4);
         }

         /* Process only interpolated varyings. Don't include POSITION.
          * Don't include integer varyings, because they are not
          * interpolated. Don't process inputs interpolated by INTERP
          * opcodes. Those are tracked separately.
          */
         if ((!is_interp_instruction || src_index != 0) &&
             (name == TGSI_SEMANTIC_GENERIC ||
              name == TGSI_SEMANTIC_TEXCOORD ||
              name == TGSI_SEMANTIC_COLOR ||
              name == TGSI_SEMANTIC_BCOLOR ||
              name == TGSI_SEMANTIC_FOG ||
              name == TGSI_SEMANTIC_CLIPDIST)) {
            switch (info->input_interpolate[input]) {
            case TGSI_INTERPOLATE_COLOR:
            case TGSI_INTERPOLATE_PERSPECTIVE:
               switch (info->input_interpolate_loc[input]) {
               case TGSI_INTERPOLATE_LOC_CENTER:
                  info->uses_persp_center = TRUE;
                  break;
               case TGSI_INTERPOLATE_LOC_CENTROID:
                  info->uses_persp_centroid = TRUE;
                  break;
               case TGSI_INTERPOLATE_LOC_SAMPLE:
                  info->uses_persp_sample = TRUE;
                  break;
               }
               break;
            case TGSI_INTERPOLATE_LINEAR:
               switch (info->input_interpolate_loc[input]) {
               case TGSI_INTERPOLATE_LOC_CENTER:
                  info->uses_linear_center = TRUE;
                  break;
               case TGSI_INTERPOLATE_LOC_CENTROID:
                  info->uses_linear_centroid = TRUE;
                  break;
               case TGSI_INTERPOLATE_LOC_SAMPLE:
                  info->uses_linear_sample = TRUE;
                  break;
               }
               break;
               /* TGSI_INTERPOLATE_CONSTANT doesn't do any interpolation. */
            }
         }
      }
   }

   /* check for indirect register reads */
   if (src->Register.Indirect) {
      info->indirect_files |= (1 << src->Register.File);
      info->indirect_files_read |= (1 << src->Register.File);

      /* record indirect constant buffer indexing */
      if (src->Register.File == TGSI_FILE_CONSTANT) {
         if (src->Register.Dimension) {
            if (src->Dimension.Indirect)
               info->const_buffers_indirect = info->const_buffers_declared;
            else
               info->const_buffers_indirect |= 1u << src->Dimension.Index;
         } else {
            info->const_buffers_indirect |= 1;
         }
      }
   }

   if (src->Register.Dimension && src->Dimension.Indirect)
      info->dim_indirect_files |= 1u << src->Register.File;

   /* Texture samplers */
   if (src->Register.File == TGSI_FILE_SAMPLER) {
      const unsigned index = src->Register.Index;

      assert(fullinst->Instruction.Texture);
      assert(index < ARRAY_SIZE(info->is_msaa_sampler));
      assert(index < PIPE_MAX_SAMPLERS);

      if (is_texture_inst(fullinst->Instruction.Opcode)) {
         const unsigned target = fullinst->Texture.Texture;
         assert(target < TGSI_TEXTURE_UNKNOWN);
         /* for texture instructions, check that the texture instruction
          * target matches the previous sampler view declaration (if there
          * was one.)
          */
         if (info->sampler_targets[index] == TGSI_TEXTURE_UNKNOWN) {
            /* probably no sampler view declaration */
            info->sampler_targets[index] = target;
         } else {
            /* Make sure the texture instruction's sampler/target info
             * agrees with the sampler view declaration.
             */
            assert(info->sampler_targets[index] == target);
         }
         /* MSAA samplers */
         if (target == TGSI_TEXTURE_2D_MSAA ||
             target == TGSI_TEXTURE_2D_ARRAY_MSAA) {
            info->is_msaa_sampler[src->Register.Index] = TRUE;
         }
      }
   }

   if (is_memory_file(src->Register.File) &&
       !is_mem_query_inst(fullinst->Instruction.Opcode)) {
      *is_mem_inst = true;

      if (tgsi_get_opcode_info(fullinst->Instruction.Opcode)->is_store) {
         info->writes_memory = TRUE;

         if (src->Register.File == TGSI_FILE_IMAGE) {
            if (src->Register.Indirect)
               info->images_writemask = info->images_declared;
            else
               info->images_writemask |= 1 << src->Register.Index;
         } else if (src->Register.File == TGSI_FILE_BUFFER) {
            if (src->Register.Indirect)
               info->shader_buffers_atomic = info->shader_buffers_declared;
            else
               info->shader_buffers_atomic |= 1 << src->Register.Index;
         }
      } else {
         if (src->Register.File == TGSI_FILE_BUFFER) {
            if (src->Register.Indirect)
               info->shader_buffers_load = info->shader_buffers_declared;
            else
               info->shader_buffers_load |= 1 << src->Register.Index;
         }
      }
   }
}


static void
scan_instruction(struct tgsi_shader_info *info,
                 const struct tgsi_full_instruction *fullinst,
                 unsigned *current_depth)
{
   unsigned i;
   bool is_mem_inst = false;
   bool is_interp_instruction = false;

   assert(fullinst->Instruction.Opcode < TGSI_OPCODE_LAST);
   info->opcode_count[fullinst->Instruction.Opcode]++;

   switch (fullinst->Instruction.Opcode) {
   case TGSI_OPCODE_IF:
   case TGSI_OPCODE_UIF:
   case TGSI_OPCODE_BGNLOOP:
      (*current_depth)++;
      info->max_depth = MAX2(info->max_depth, *current_depth);
      break;
   case TGSI_OPCODE_ENDIF:
   case TGSI_OPCODE_ENDLOOP:
      (*current_depth)--;
      break;
   default:
      break;
   }

   if (fullinst->Instruction.Opcode == TGSI_OPCODE_INTERP_CENTROID ||
       fullinst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
       fullinst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
      const struct tgsi_full_src_register *src0 = &fullinst->Src[0];
      unsigned input;

      is_interp_instruction = true;

      if (src0->Register.Indirect && src0->Indirect.ArrayID)
         input = info->input_array_first[src0->Indirect.ArrayID];
      else
         input = src0->Register.Index;

      /* For the INTERP opcodes, the interpolation is always
       * PERSPECTIVE unless LINEAR is specified.
       */
      switch (info->input_interpolate[input]) {
      case TGSI_INTERPOLATE_COLOR:
      case TGSI_INTERPOLATE_CONSTANT:
      case TGSI_INTERPOLATE_PERSPECTIVE:
         switch (fullinst->Instruction.Opcode) {
         case TGSI_OPCODE_INTERP_CENTROID:
            info->uses_persp_opcode_interp_centroid = TRUE;
            break;
         case TGSI_OPCODE_INTERP_OFFSET:
            info->uses_persp_opcode_interp_offset = TRUE;
            break;
         case TGSI_OPCODE_INTERP_SAMPLE:
            info->uses_persp_opcode_interp_sample = TRUE;
            break;
         }
         break;

      case TGSI_INTERPOLATE_LINEAR:
         switch (fullinst->Instruction.Opcode) {
         case TGSI_OPCODE_INTERP_CENTROID:
            info->uses_linear_opcode_interp_centroid = TRUE;
            break;
         case TGSI_OPCODE_INTERP_OFFSET:
            info->uses_linear_opcode_interp_offset = TRUE;
            break;
         case TGSI_OPCODE_INTERP_SAMPLE:
            info->uses_linear_opcode_interp_sample = TRUE;
            break;
         }
         break;
      }
   }

   if (fullinst->Instruction.Opcode >= TGSI_OPCODE_F2D &&
       fullinst->Instruction.Opcode <= TGSI_OPCODE_DSSG)
      info->uses_doubles = TRUE;

   for (i = 0; i < fullinst->Instruction.NumSrcRegs; i++) {
      scan_src_operand(info, fullinst, &fullinst->Src[i], i,
                       tgsi_util_get_inst_usage_mask(fullinst, i),
                       is_interp_instruction, &is_mem_inst);
   }

   if (fullinst->Instruction.Texture) {
      for (i = 0; i < fullinst->Texture.NumOffsets; i++) {
         struct tgsi_full_src_register src = {{0}};

         src.Register.File = fullinst->TexOffsets[i].File;
         src.Register.Index = fullinst->TexOffsets[i].Index;
         src.Register.SwizzleX = fullinst->TexOffsets[i].SwizzleX;
         src.Register.SwizzleY = fullinst->TexOffsets[i].SwizzleY;
         src.Register.SwizzleZ = fullinst->TexOffsets[i].SwizzleZ;

         /* The usage mask is suboptimal but should be safe. */
         scan_src_operand(info, fullinst, &src, 0, TGSI_WRITEMASK_XYZ,
                          false, &is_mem_inst);
      }
   }

   /* check for indirect register writes */
   for (i = 0; i < fullinst->Instruction.NumDstRegs; i++) {
      const struct tgsi_full_dst_register *dst = &fullinst->Dst[i];
      if (dst->Register.Indirect) {
         info->indirect_files |= (1 << dst->Register.File);
         info->indirect_files_written |= (1 << dst->Register.File);
      }

      if (dst->Register.Dimension && dst->Dimension.Indirect)
         info->dim_indirect_files |= 1u << dst->Register.File;

      if (is_memory_file(dst->Register.File)) {
         assert(fullinst->Instruction.Opcode == TGSI_OPCODE_STORE);

         is_mem_inst = true;
         info->writes_memory = TRUE;

         if (dst->Register.File == TGSI_FILE_IMAGE) {
            if (dst->Register.Indirect)
               info->images_writemask = info->images_declared;
            else
               info->images_writemask |= 1 << dst->Register.Index;
         } else if (dst->Register.File == TGSI_FILE_BUFFER) {
            if (dst->Register.Indirect)
               info->shader_buffers_store = info->shader_buffers_declared;
            else
               info->shader_buffers_store |= 1 << dst->Register.Index;
         }
      }
   }

   if (is_mem_inst)
      info->num_memory_instructions++;

   if (computes_derivative(fullinst->Instruction.Opcode))
      info->uses_derivatives = true;

   info->num_instructions++;
}
     

static void
scan_declaration(struct tgsi_shader_info *info,
                 const struct tgsi_full_declaration *fulldecl)
{
   const uint file = fulldecl->Declaration.File;
   const unsigned procType = info->processor;
   uint reg;

   if (fulldecl->Declaration.Array) {
      unsigned array_id = fulldecl->Array.ArrayID;

      switch (file) {
      case TGSI_FILE_INPUT:
         assert(array_id < ARRAY_SIZE(info->input_array_first));
         info->input_array_first[array_id] = fulldecl->Range.First;
         info->input_array_last[array_id] = fulldecl->Range.Last;
         break;
      case TGSI_FILE_OUTPUT:
         assert(array_id < ARRAY_SIZE(info->output_array_first));
         info->output_array_first[array_id] = fulldecl->Range.First;
         info->output_array_last[array_id] = fulldecl->Range.Last;
         break;
      }
      info->array_max[file] = MAX2(info->array_max[file], array_id);
   }

   for (reg = fulldecl->Range.First; reg <= fulldecl->Range.Last; reg++) {
      unsigned semName = fulldecl->Semantic.Name;
      unsigned semIndex = fulldecl->Semantic.Index +
         (reg - fulldecl->Range.First);
      int buffer;
      unsigned index, target, type;

      /* only first 32 regs will appear in this bitfield */
      info->file_mask[file] |= (1 << reg);
      info->file_count[file]++;
      info->file_max[file] = MAX2(info->file_max[file], (int)reg);

      switch (file) {
      case TGSI_FILE_CONSTANT:
         buffer = 0;

         if (fulldecl->Declaration.Dimension)
            buffer = fulldecl->Dim.Index2D;

         info->const_file_max[buffer] =
            MAX2(info->const_file_max[buffer], (int)reg);
         info->const_buffers_declared |= 1u << buffer;
         break;

      case TGSI_FILE_IMAGE:
         info->images_declared |= 1u << reg;
         if (fulldecl->Image.Resource == TGSI_TEXTURE_BUFFER)
            info->images_buffers |= 1 << reg;
         break;

      case TGSI_FILE_BUFFER:
         info->shader_buffers_declared |= 1u << reg;
         break;

      case TGSI_FILE_INPUT:
         info->input_semantic_name[reg] = (ubyte) semName;
         info->input_semantic_index[reg] = (ubyte) semIndex;
         info->input_interpolate[reg] = (ubyte)fulldecl->Interp.Interpolate;
         info->input_interpolate_loc[reg] = (ubyte)fulldecl->Interp.Location;
         info->input_cylindrical_wrap[reg] = (ubyte)fulldecl->Interp.CylindricalWrap;

         /* Vertex shaders can have inputs with holes between them. */
         info->num_inputs = MAX2(info->num_inputs, reg + 1);

         if (semName == TGSI_SEMANTIC_PRIMID)
            info->uses_primid = TRUE;
         else if (procType == PIPE_SHADER_FRAGMENT) {
            if (semName == TGSI_SEMANTIC_POSITION)
               info->reads_position = TRUE;
            else if (semName == TGSI_SEMANTIC_FACE)
               info->uses_frontface = TRUE;
         }
         break;

      case TGSI_FILE_SYSTEM_VALUE:
         index = fulldecl->Range.First;

         info->system_value_semantic_name[index] = semName;
         info->num_system_values = MAX2(info->num_system_values, index + 1);

         switch (semName) {
         case TGSI_SEMANTIC_INSTANCEID:
            info->uses_instanceid = TRUE;
            break;
         case TGSI_SEMANTIC_VERTEXID:
            info->uses_vertexid = TRUE;
            break;
         case TGSI_SEMANTIC_VERTEXID_NOBASE:
            info->uses_vertexid_nobase = TRUE;
            break;
         case TGSI_SEMANTIC_BASEVERTEX:
            info->uses_basevertex = TRUE;
            break;
         case TGSI_SEMANTIC_PRIMID:
            info->uses_primid = TRUE;
            break;
         case TGSI_SEMANTIC_INVOCATIONID:
            info->uses_invocationid = TRUE;
            break;
         case TGSI_SEMANTIC_POSITION:
            info->reads_position = TRUE;
            break;
         case TGSI_SEMANTIC_FACE:
            info->uses_frontface = TRUE;
            break;
         case TGSI_SEMANTIC_SAMPLEMASK:
            info->reads_samplemask = TRUE;
            break;
         }
         break;

      case TGSI_FILE_OUTPUT:
         info->output_semantic_name[reg] = (ubyte) semName;
         info->output_semantic_index[reg] = (ubyte) semIndex;
         info->output_usagemask[reg] |= fulldecl->Declaration.UsageMask;
         info->num_outputs = MAX2(info->num_outputs, reg + 1);

         if (fulldecl->Declaration.UsageMask & TGSI_WRITEMASK_X) {
            info->output_streams[reg] |= (ubyte)fulldecl->Semantic.StreamX;
            info->num_stream_output_components[fulldecl->Semantic.StreamX]++;
         }
         if (fulldecl->Declaration.UsageMask & TGSI_WRITEMASK_Y) {
            info->output_streams[reg] |= (ubyte)fulldecl->Semantic.StreamY << 2;
            info->num_stream_output_components[fulldecl->Semantic.StreamY]++;
         }
         if (fulldecl->Declaration.UsageMask & TGSI_WRITEMASK_Z) {
            info->output_streams[reg] |= (ubyte)fulldecl->Semantic.StreamZ << 4;
            info->num_stream_output_components[fulldecl->Semantic.StreamZ]++;
         }
         if (fulldecl->Declaration.UsageMask & TGSI_WRITEMASK_W) {
            info->output_streams[reg] |= (ubyte)fulldecl->Semantic.StreamW << 6;
            info->num_stream_output_components[fulldecl->Semantic.StreamW]++;
         }

         switch (semName) {
         case TGSI_SEMANTIC_PRIMID:
            info->writes_primid = true;
            break;
         case TGSI_SEMANTIC_VIEWPORT_INDEX:
            info->writes_viewport_index = true;
            break;
         case TGSI_SEMANTIC_LAYER:
            info->writes_layer = true;
            break;
         case TGSI_SEMANTIC_PSIZE:
            info->writes_psize = true;
            break;
         case TGSI_SEMANTIC_CLIPVERTEX:
            info->writes_clipvertex = true;
            break;
         case TGSI_SEMANTIC_COLOR:
            info->colors_written |= 1 << semIndex;
            break;
         case TGSI_SEMANTIC_STENCIL:
            info->writes_stencil = true;
            break;
         case TGSI_SEMANTIC_SAMPLEMASK:
            info->writes_samplemask = true;
            break;
         case TGSI_SEMANTIC_EDGEFLAG:
            info->writes_edgeflag = true;
            break;
         case TGSI_SEMANTIC_POSITION:
            if (procType == PIPE_SHADER_FRAGMENT)
               info->writes_z = true;
            else
               info->writes_position = true;
            break;
         }
         break;

      case TGSI_FILE_SAMPLER:
         STATIC_ASSERT(sizeof(info->samplers_declared) * 8 >= PIPE_MAX_SAMPLERS);
         info->samplers_declared |= 1u << reg;
         break;

      case TGSI_FILE_SAMPLER_VIEW:
         target = fulldecl->SamplerView.Resource;
         type = fulldecl->SamplerView.ReturnTypeX;

         assert(target < TGSI_TEXTURE_UNKNOWN);
         if (info->sampler_targets[reg] == TGSI_TEXTURE_UNKNOWN) {
            /* Save sampler target for this sampler index */
            info->sampler_targets[reg] = target;
            info->sampler_type[reg] = type;
         } else {
            /* if previously declared, make sure targets agree */
            assert(info->sampler_targets[reg] == target);
            assert(info->sampler_type[reg] == type);
         }
         break;
      }
   }
}


static void
scan_immediate(struct tgsi_shader_info *info)
{
   uint reg = info->immediate_count++;
   uint file = TGSI_FILE_IMMEDIATE;

   info->file_mask[file] |= (1 << reg);
   info->file_count[file]++;
   info->file_max[file] = MAX2(info->file_max[file], (int)reg);
}


static void
scan_property(struct tgsi_shader_info *info,
              const struct tgsi_full_property *fullprop)
{
   unsigned name = fullprop->Property.PropertyName;
   unsigned value = fullprop->u[0].Data;

   assert(name < ARRAY_SIZE(info->properties));
   info->properties[name] = value;

   switch (name) {
   case TGSI_PROPERTY_NUM_CLIPDIST_ENABLED:
      info->num_written_clipdistance = value;
      info->clipdist_writemask |= (1 << value) - 1;
      break;
   case TGSI_PROPERTY_NUM_CULLDIST_ENABLED:
      info->num_written_culldistance = value;
      info->culldist_writemask |= (1 << value) - 1;
      break;
   }
}


/**
 * Scan the given TGSI shader to collect information such as number of
 * registers used, special instructions used, etc.
 * \return info  the result of the scan
 */
void
tgsi_scan_shader(const struct tgsi_token *tokens,
                 struct tgsi_shader_info *info)
{
   uint procType, i;
   struct tgsi_parse_context parse;
   unsigned current_depth = 0;

   memset(info, 0, sizeof(*info));
   for (i = 0; i < TGSI_FILE_COUNT; i++)
      info->file_max[i] = -1;
   for (i = 0; i < ARRAY_SIZE(info->const_file_max); i++)
      info->const_file_max[i] = -1;
   info->properties[TGSI_PROPERTY_GS_INVOCATIONS] = 1;
   for (i = 0; i < ARRAY_SIZE(info->sampler_targets); i++)
      info->sampler_targets[i] = TGSI_TEXTURE_UNKNOWN;

   /**
    ** Setup to begin parsing input shader
    **/
   if (tgsi_parse_init( &parse, tokens ) != TGSI_PARSE_OK) {
      debug_printf("tgsi_parse_init() failed in tgsi_scan_shader()!\n");
      return;
   }
   procType = parse.FullHeader.Processor.Processor;
   assert(procType == PIPE_SHADER_FRAGMENT ||
          procType == PIPE_SHADER_VERTEX ||
          procType == PIPE_SHADER_GEOMETRY ||
          procType == PIPE_SHADER_TESS_CTRL ||
          procType == PIPE_SHADER_TESS_EVAL ||
          procType == PIPE_SHADER_COMPUTE);
   info->processor = procType;

   /**
    ** Loop over incoming program tokens/instructions
    */
   while (!tgsi_parse_end_of_tokens(&parse)) {
      info->num_tokens++;

      tgsi_parse_token( &parse );

      switch( parse.FullToken.Token.Type ) {
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         scan_instruction(info, &parse.FullToken.FullInstruction,
                          &current_depth);
         break;
      case TGSI_TOKEN_TYPE_DECLARATION:
         scan_declaration(info, &parse.FullToken.FullDeclaration);
         break;
      case TGSI_TOKEN_TYPE_IMMEDIATE:
         scan_immediate(info);
         break;
      case TGSI_TOKEN_TYPE_PROPERTY:
         scan_property(info, &parse.FullToken.FullProperty);
         break;
      default:
         assert(!"Unexpected TGSI token type");
      }
   }

   info->uses_kill = (info->opcode_count[TGSI_OPCODE_KILL_IF] ||
                      info->opcode_count[TGSI_OPCODE_KILL]);

   /* The dimensions of the IN decleration in geometry shader have
    * to be deduced from the type of the input primitive.
    */
   if (procType == PIPE_SHADER_GEOMETRY) {
      unsigned input_primitive =
            info->properties[TGSI_PROPERTY_GS_INPUT_PRIM];
      int num_verts = u_vertices_per_prim(input_primitive);
      int j;
      info->file_count[TGSI_FILE_INPUT] = num_verts;
      info->file_max[TGSI_FILE_INPUT] =
            MAX2(info->file_max[TGSI_FILE_INPUT], num_verts - 1);
      for (j = 0; j < num_verts; ++j) {
         info->file_mask[TGSI_FILE_INPUT] |= (1 << j);
      }
   }

   tgsi_parse_free(&parse);
}

/**
 * Collect information about the arrays of a given register file.
 *
 * @param tokens TGSI shader
 * @param file the register file to scan through
 * @param max_array_id number of entries in @p arrays; should be equal to the
 *                     highest array id, i.e. tgsi_shader_info::array_max[file].
 * @param arrays info for array of each ID will be written to arrays[ID - 1].
 */
void
tgsi_scan_arrays(const struct tgsi_token *tokens,
                 unsigned file,
                 unsigned max_array_id,
                 struct tgsi_array_info *arrays)
{
   struct tgsi_parse_context parse;

   if (tgsi_parse_init(&parse, tokens) != TGSI_PARSE_OK) {
      debug_printf("tgsi_parse_init() failed in tgsi_scan_arrays()!\n");
      return;
   }

   memset(arrays, 0, sizeof(arrays[0]) * max_array_id);

   while (!tgsi_parse_end_of_tokens(&parse)) {
      struct tgsi_full_instruction *inst;

      tgsi_parse_token(&parse);

      if (parse.FullToken.Token.Type == TGSI_TOKEN_TYPE_DECLARATION) {
         struct tgsi_full_declaration *decl = &parse.FullToken.FullDeclaration;

         if (decl->Declaration.Array && decl->Declaration.File == file &&
             decl->Array.ArrayID > 0 && decl->Array.ArrayID <= max_array_id) {
            struct tgsi_array_info *array = &arrays[decl->Array.ArrayID - 1];
            assert(!array->declared);
            array->declared = true;
            array->range = decl->Range;
         }
      }

      if (parse.FullToken.Token.Type != TGSI_TOKEN_TYPE_INSTRUCTION)
         continue;

      inst = &parse.FullToken.FullInstruction;
      for (unsigned i = 0; i < inst->Instruction.NumDstRegs; i++) {
         const struct tgsi_full_dst_register *dst = &inst->Dst[i];
         if (dst->Register.File != file)
            continue;

         if (dst->Register.Indirect) {
            if (dst->Indirect.ArrayID > 0 &&
                dst->Indirect.ArrayID <= max_array_id) {
               arrays[dst->Indirect.ArrayID - 1].writemask |= dst->Register.WriteMask;
            } else {
               /* Indirect writes without an ArrayID can write anywhere. */
               for (unsigned j = 0; j < max_array_id; ++j)
                  arrays[j].writemask |= dst->Register.WriteMask;
            }
         } else {
            /* Check whether the write falls into any of the arrays anyway. */
            for (unsigned j = 0; j < max_array_id; ++j) {
               struct tgsi_array_info *array = &arrays[j];
               if (array->declared &&
                   dst->Register.Index >= array->range.First &&
                   dst->Register.Index <= array->range.Last)
                  array->writemask |= dst->Register.WriteMask;
            }
         }
      }
   }

   tgsi_parse_free(&parse);

   return;
}


/**
 * Check if the given shader is a "passthrough" shader consisting of only
 * MOV instructions of the form:  MOV OUT[n], IN[n]
 *  
 */
boolean
tgsi_is_passthrough_shader(const struct tgsi_token *tokens)
{
   struct tgsi_parse_context parse;

   /**
    ** Setup to begin parsing input shader
    **/
   if (tgsi_parse_init(&parse, tokens) != TGSI_PARSE_OK) {
      debug_printf("tgsi_parse_init() failed in tgsi_is_passthrough_shader()!\n");
      return FALSE;
   }

   /**
    ** Loop over incoming program tokens/instructions
    */
   while (!tgsi_parse_end_of_tokens(&parse)) {

      tgsi_parse_token(&parse);

      switch (parse.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_INSTRUCTION:
         {
            struct tgsi_full_instruction *fullinst =
               &parse.FullToken.FullInstruction;
            const struct tgsi_full_src_register *src =
               &fullinst->Src[0];
            const struct tgsi_full_dst_register *dst =
               &fullinst->Dst[0];

            /* Do a whole bunch of checks for a simple move */
            if (fullinst->Instruction.Opcode != TGSI_OPCODE_MOV ||
                (src->Register.File != TGSI_FILE_INPUT &&
                 src->Register.File != TGSI_FILE_SYSTEM_VALUE) ||
                dst->Register.File != TGSI_FILE_OUTPUT ||
                src->Register.Index != dst->Register.Index ||

                src->Register.Negate ||
                src->Register.Absolute ||

                src->Register.SwizzleX != TGSI_SWIZZLE_X ||
                src->Register.SwizzleY != TGSI_SWIZZLE_Y ||
                src->Register.SwizzleZ != TGSI_SWIZZLE_Z ||
                src->Register.SwizzleW != TGSI_SWIZZLE_W ||

                dst->Register.WriteMask != TGSI_WRITEMASK_XYZW)
            {
               tgsi_parse_free(&parse);
               return FALSE;
            }
         }
         break;

      case TGSI_TOKEN_TYPE_DECLARATION:
         /* fall-through */
      case TGSI_TOKEN_TYPE_IMMEDIATE:
         /* fall-through */
      case TGSI_TOKEN_TYPE_PROPERTY:
         /* fall-through */
      default:
         ; /* no-op */
      }
   }

   tgsi_parse_free(&parse);

   /* if we get here, it's a pass-through shader */
   return TRUE;
}
