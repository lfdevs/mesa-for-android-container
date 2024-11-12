/*
 * Copyright © 2010 Intel Corporation
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

/** @file
 *
 * This file drives the GLSL IR -> LIR translation, contains the
 * optimizations on the LIR, and drives the generation of native code
 * from the LIR.
 */

#include "brw_eu.h"
#include "brw_fs.h"
#include "brw_fs_builder.h"
#include "brw_fs_live_variables.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"
#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_math.h"

using namespace brw;

static void
initialize_sources(fs_inst *inst, const brw_reg src[], uint8_t num_sources);

void
fs_inst::init(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
              const brw_reg *src, unsigned sources)
{
   memset((void*)this, 0, sizeof(*this));

   initialize_sources(this, src, sources);

   for (unsigned i = 0; i < sources; i++)
      this->src[i] = src[i];

   this->opcode = opcode;
   this->dst = dst;
   this->exec_size = exec_size;

   assert(dst.file != IMM && dst.file != UNIFORM);

   assert(this->exec_size != 0);

   this->conditional_mod = BRW_CONDITIONAL_NONE;

   /* This will be the case for almost all instructions. */
   switch (dst.file) {
   case VGRF:
   case ARF:
   case FIXED_GRF:
   case ATTR:
      this->size_written = dst.component_size(exec_size);
      break;
   case BAD_FILE:
      this->size_written = 0;
      break;
   case IMM:
   case UNIFORM:
      unreachable("Invalid destination register file");
   }

   this->writes_accumulator = false;
}

fs_inst::fs_inst()
{
   init(BRW_OPCODE_NOP, 8, dst, NULL, 0);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size)
{
   init(opcode, exec_size, reg_undef, NULL, 0);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst)
{
   init(opcode, exec_size, dst, NULL, 0);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
                 const brw_reg &src0)
{
   const brw_reg src[1] = { src0 };
   init(opcode, exec_size, dst, src, 1);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
                 const brw_reg &src0, const brw_reg &src1)
{
   const brw_reg src[2] = { src0, src1 };
   init(opcode, exec_size, dst, src, 2);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
                 const brw_reg &src0, const brw_reg &src1, const brw_reg &src2)
{
   const brw_reg src[3] = { src0, src1, src2 };
   init(opcode, exec_size, dst, src, 3);
}

fs_inst::fs_inst(enum opcode opcode, uint8_t exec_width, const brw_reg &dst,
                 const brw_reg src[], unsigned sources)
{
   init(opcode, exec_width, dst, src, sources);
}

fs_inst::fs_inst(const fs_inst &that)
{
   memcpy((void*)this, &that, sizeof(that));
   initialize_sources(this, that.src, that.sources);
}

fs_inst::~fs_inst()
{
   if (this->src != this->builtin_src)
      delete[] this->src;
}

static void
initialize_sources(fs_inst *inst, const brw_reg src[], uint8_t num_sources)
{
   if (num_sources > ARRAY_SIZE(inst->builtin_src))
      inst->src = new brw_reg[num_sources];
   else
      inst->src = inst->builtin_src;

   for (unsigned i = 0; i < num_sources; i++)
      inst->src[i] = src[i];

   inst->sources = num_sources;
}

void
fs_inst::resize_sources(uint8_t num_sources)
{
   if (this->sources == num_sources)
      return;

   brw_reg *old_src = this->src;
   brw_reg *new_src;

   const unsigned builtin_size = ARRAY_SIZE(this->builtin_src);

   if (old_src == this->builtin_src) {
      if (num_sources > builtin_size) {
         new_src = new brw_reg[num_sources];
         for (unsigned i = 0; i < this->sources; i++)
            new_src[i] = old_src[i];

      } else {
         new_src = old_src;
      }
   } else {
      if (num_sources <= builtin_size) {
         new_src = this->builtin_src;
         assert(this->sources > num_sources);
         for (unsigned i = 0; i < num_sources; i++)
            new_src[i] = old_src[i];

      } else if (num_sources < this->sources) {
         new_src = old_src;

      } else {
         new_src = new brw_reg[num_sources];
         for (unsigned i = 0; i < num_sources; i++)
            new_src[i] = old_src[i];
      }

      if (old_src != new_src)
         delete[] old_src;
   }

   this->sources = num_sources;
   this->src = new_src;
}

bool
fs_inst::is_send_from_grf() const
{
   switch (opcode) {
   case SHADER_OPCODE_SEND:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
   case SHADER_OPCODE_INTERLOCK:
   case SHADER_OPCODE_MEMORY_FENCE:
   case SHADER_OPCODE_BARRIER:
      return true;
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
      return src[1].file == VGRF;
   default:
      return false;
   }
}

bool
fs_inst::is_control_source(unsigned arg) const
{
   switch (opcode) {
   case FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD:
      return arg == 0;

   case SHADER_OPCODE_BROADCAST:
   case SHADER_OPCODE_SHUFFLE:
   case SHADER_OPCODE_QUAD_SWIZZLE:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      return arg == 1;

   case SHADER_OPCODE_MOV_INDIRECT:
   case SHADER_OPCODE_CLUSTER_BROADCAST:
      return arg == 1 || arg == 2;

   case SHADER_OPCODE_SEND:
      return arg == 0 || arg == 1;

   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
      return arg != MEMORY_LOGICAL_BINDING &&
             arg != MEMORY_LOGICAL_ADDRESS &&
             arg != MEMORY_LOGICAL_DATA0 &&
             arg != MEMORY_LOGICAL_DATA1;

   default:
      return false;
   }
}

bool
fs_inst::is_payload(unsigned arg) const
{
   switch (opcode) {
   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
   case SHADER_OPCODE_INTERLOCK:
   case SHADER_OPCODE_MEMORY_FENCE:
   case SHADER_OPCODE_BARRIER:
      return arg == 0;

   case SHADER_OPCODE_SEND:
      return arg == 2 || arg == 3;

   default:
      return false;
   }
}

bool
fs_inst::can_do_source_mods(const struct intel_device_info *devinfo) const
{
   if (is_send_from_grf())
      return false;

   /* From TGL PRM Vol 2a Pg. 1053 and Pg. 1069 MAD and MUL Instructions:
    *
    * "When multiplying a DW and any lower precision integer, source modifier
    *  is not supported."
    */
   if (devinfo->ver >= 12 && (opcode == BRW_OPCODE_MUL ||
                              opcode == BRW_OPCODE_MAD)) {
      const brw_reg_type exec_type = get_exec_type(this);
      const unsigned min_brw_type_size_bytes = opcode == BRW_OPCODE_MAD ?
         MIN2(brw_type_size_bytes(src[1].type), brw_type_size_bytes(src[2].type)) :
         MIN2(brw_type_size_bytes(src[0].type), brw_type_size_bytes(src[1].type));

      if (brw_type_is_int(exec_type) &&
          brw_type_size_bytes(exec_type) >= 4 &&
          brw_type_size_bytes(exec_type) != min_brw_type_size_bytes)
         return false;
   }

   switch (opcode) {
   case BRW_OPCODE_ADDC:
   case BRW_OPCODE_BFE:
   case BRW_OPCODE_BFI1:
   case BRW_OPCODE_BFI2:
   case BRW_OPCODE_BFREV:
   case BRW_OPCODE_CBIT:
   case BRW_OPCODE_FBH:
   case BRW_OPCODE_FBL:
   case BRW_OPCODE_ROL:
   case BRW_OPCODE_ROR:
   case BRW_OPCODE_SUBB:
   case BRW_OPCODE_DP4A:
   case BRW_OPCODE_DPAS:
   case SHADER_OPCODE_BROADCAST:
   case SHADER_OPCODE_CLUSTER_BROADCAST:
   case SHADER_OPCODE_MOV_INDIRECT:
   case SHADER_OPCODE_SHUFFLE:
   case SHADER_OPCODE_INT_QUOTIENT:
   case SHADER_OPCODE_INT_REMAINDER:
   case SHADER_OPCODE_REDUCE:
   case SHADER_OPCODE_INCLUSIVE_SCAN:
   case SHADER_OPCODE_EXCLUSIVE_SCAN:
   case SHADER_OPCODE_VOTE_ANY:
   case SHADER_OPCODE_VOTE_ALL:
   case SHADER_OPCODE_VOTE_EQUAL:
      return false;
   default:
      return true;
   }
}

bool
fs_inst::can_do_cmod() const
{
   switch (opcode) {
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_ADD3:
   case BRW_OPCODE_ADDC:
   case BRW_OPCODE_AND:
   case BRW_OPCODE_ASR:
   case BRW_OPCODE_AVG:
   case BRW_OPCODE_CMP:
   case BRW_OPCODE_CMPN:
   case BRW_OPCODE_DP2:
   case BRW_OPCODE_DP3:
   case BRW_OPCODE_DP4:
   case BRW_OPCODE_DPH:
   case BRW_OPCODE_FRC:
   case BRW_OPCODE_LINE:
   case BRW_OPCODE_LRP:
   case BRW_OPCODE_LZD:
   case BRW_OPCODE_MAC:
   case BRW_OPCODE_MACH:
   case BRW_OPCODE_MAD:
   case BRW_OPCODE_MOV:
   case BRW_OPCODE_MUL:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_PLN:
   case BRW_OPCODE_RNDD:
   case BRW_OPCODE_RNDE:
   case BRW_OPCODE_RNDU:
   case BRW_OPCODE_RNDZ:
   case BRW_OPCODE_SHL:
   case BRW_OPCODE_SHR:
   case BRW_OPCODE_SUBB:
   case BRW_OPCODE_XOR:
      break;
   default:
      return false;
   }

   /* The accumulator result appears to get used for the conditional modifier
    * generation.  When negating a UD value, there is a 33rd bit generated for
    * the sign in the accumulator value, so now you can't check, for example,
    * equality with a 32-bit value.  See piglit fs-op-neg-uvec4.
    */
   for (unsigned i = 0; i < sources; i++) {
      if (brw_type_is_uint(src[i].type) && src[i].negate)
         return false;
   }

   return true;
}

bool
fs_inst::can_change_types() const
{
   return dst.type == src[0].type &&
          !src[0].abs && !src[0].negate && !saturate && src[0].file != ATTR &&
          (opcode == BRW_OPCODE_MOV ||
           (opcode == SHADER_OPCODE_LOAD_PAYLOAD && sources == 1) ||
           (opcode == BRW_OPCODE_SEL &&
            dst.type == src[1].type &&
            predicate != BRW_PREDICATE_NONE &&
            !src[1].abs && !src[1].negate && src[1].file != ATTR));
}

bool
brw_reg::equals(const brw_reg &r) const
{
   return brw_regs_equal(this, &r);
}

bool
brw_reg::negative_equals(const brw_reg &r) const
{
   return brw_regs_negative_equal(this, &r);
}

bool
brw_reg::is_contiguous() const
{
   switch (file) {
   case ARF:
   case FIXED_GRF:
      return hstride == BRW_HORIZONTAL_STRIDE_1 &&
             vstride == width + hstride;
   case VGRF:
   case ATTR:
      return stride == 1;
   case UNIFORM:
   case IMM:
   case BAD_FILE:
      return true;
   }

   unreachable("Invalid register file");
}

unsigned
brw_reg::component_size(unsigned width) const
{
   if (file == ARF || file == FIXED_GRF) {
      const unsigned w = MIN2(width, 1u << this->width);
      const unsigned h = width >> this->width;
      const unsigned vs = vstride ? 1 << (vstride - 1) : 0;
      const unsigned hs = hstride ? 1 << (hstride - 1) : 0;
      assert(w > 0);
      /* Note this rounds up to next horizontal stride to be consistent with
       * the VGRF case below.
       */
      return ((MAX2(1, h) - 1) * vs + MAX2(w * hs, 1)) * brw_type_size_bytes(type);
   } else {
      return MAX2(width * stride, 1) * brw_type_size_bytes(type);
   }
}

void
fs_visitor::vfail(const char *format, va_list va)
{
   char *msg;

   if (failed)
      return;

   failed = true;

   msg = ralloc_vasprintf(mem_ctx, format, va);
   msg = ralloc_asprintf(mem_ctx, "SIMD%d %s compile failed: %s\n",
         dispatch_width, _mesa_shader_stage_to_abbrev(stage), msg);

   this->fail_msg = msg;

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "%s",  msg);
   }
}

void
fs_visitor::fail(const char *format, ...)
{
   va_list va;

   va_start(va, format);
   vfail(format, va);
   va_end(va);
}

/**
 * Mark this program as impossible to compile with dispatch width greater
 * than n.
 *
 * During the SIMD8 compile (which happens first), we can detect and flag
 * things that are unsupported in SIMD16+ mode, so the compiler can skip the
 * SIMD16+ compile altogether.
 *
 * During a compile of dispatch width greater than n (if one happens anyway),
 * this just calls fail().
 */
void
fs_visitor::limit_dispatch_width(unsigned n, const char *msg)
{
   if (dispatch_width > n) {
      fail("%s", msg);
   } else {
      max_dispatch_width = MIN2(max_dispatch_width, n);
      brw_shader_perf_log(compiler, log_data,
                          "Shader dispatch width limited to SIMD%d: %s\n",
                          n, msg);
   }
}

/**
 * Returns true if the instruction has a flag that means it won't
 * update an entire destination register.
 *
 * For example, dead code elimination and live variable analysis want to know
 * when a write to a variable screens off any preceding values that were in
 * it.
 */
bool
fs_inst::is_partial_write() const
{
   if (this->predicate && !this->predicate_trivial &&
       this->opcode != BRW_OPCODE_SEL)
      return true;

   if (!this->dst.is_contiguous())
      return true;

   if (this->dst.offset % REG_SIZE != 0)
      return true;

   return this->size_written % REG_SIZE != 0;
}

unsigned
fs_inst::components_read(unsigned i) const
{
   /* Return zero if the source is not present. */
   if (src[i].file == BAD_FILE)
      return 0;

   switch (opcode) {
   case BRW_OPCODE_PLN:
      return i == 0 ? 1 : 2;

   case FS_OPCODE_PIXEL_X:
   case FS_OPCODE_PIXEL_Y:
      assert(i < 2);
      if (i == 0)
         return 2;
      else
         return 1;

   case FS_OPCODE_FB_WRITE_LOGICAL:
      assert(src[FB_WRITE_LOGICAL_SRC_COMPONENTS].file == IMM);
      /* First/second FB write color. */
      if (i < 2)
         return src[FB_WRITE_LOGICAL_SRC_COMPONENTS].ud;
      else
         return 1;

   case SHADER_OPCODE_TEX_LOGICAL:
   case SHADER_OPCODE_TXD_LOGICAL:
   case SHADER_OPCODE_TXF_LOGICAL:
   case SHADER_OPCODE_TXL_LOGICAL:
   case SHADER_OPCODE_TXS_LOGICAL:
   case SHADER_OPCODE_IMAGE_SIZE_LOGICAL:
   case FS_OPCODE_TXB_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_W_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL:
   case SHADER_OPCODE_TXF_MCS_LOGICAL:
   case SHADER_OPCODE_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
   case SHADER_OPCODE_TG4_BIAS_LOGICAL:
   case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
   case SHADER_OPCODE_SAMPLEINFO_LOGICAL:
      assert(src[TEX_LOGICAL_SRC_COORD_COMPONENTS].file == IMM &&
             src[TEX_LOGICAL_SRC_GRAD_COMPONENTS].file == IMM &&
             src[TEX_LOGICAL_SRC_RESIDENCY].file == IMM);
      /* Texture coordinates. */
      if (i == TEX_LOGICAL_SRC_COORDINATE)
         return src[TEX_LOGICAL_SRC_COORD_COMPONENTS].ud;
      /* Texture derivatives. */
      else if ((i == TEX_LOGICAL_SRC_LOD || i == TEX_LOGICAL_SRC_LOD2) &&
               opcode == SHADER_OPCODE_TXD_LOGICAL)
         return src[TEX_LOGICAL_SRC_GRAD_COMPONENTS].ud;
      /* Texture offset. */
      else if (i == TEX_LOGICAL_SRC_TG4_OFFSET)
         return 2;
      /* MCS */
      else if (i == TEX_LOGICAL_SRC_MCS) {
         if (opcode == SHADER_OPCODE_TXF_CMS_W_LOGICAL)
            return 2;
         else if (opcode == SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL)
            return 4;
         else
            return 1;
      } else
         return 1;

   case SHADER_OPCODE_MEMORY_LOAD_LOGICAL:
      if (i == MEMORY_LOGICAL_DATA0 || i == MEMORY_LOGICAL_DATA0)
         return 0;
      /* fallthrough */
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
      if (i == MEMORY_LOGICAL_DATA1)
         return 0;
      /* fallthrough */
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
      if (i == MEMORY_LOGICAL_DATA0 || i == MEMORY_LOGICAL_DATA1)
         return src[MEMORY_LOGICAL_COMPONENTS].ud;
      else if (i == MEMORY_LOGICAL_ADDRESS)
         return src[MEMORY_LOGICAL_COORD_COMPONENTS].ud;
      else
         return 1;

   case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET:
      return (i == 0 ? 2 : 1);

   case SHADER_OPCODE_URB_WRITE_LOGICAL:
      assert(src[URB_LOGICAL_SRC_COMPONENTS].file == IMM);

      if (i == URB_LOGICAL_SRC_DATA)
         return src[URB_LOGICAL_SRC_COMPONENTS].ud;
      else
         return 1;

   case BRW_OPCODE_DPAS:
      unreachable("Do not use components_read() for DPAS.");

   default:
      return 1;
   }
}

unsigned
fs_inst::size_read(int arg) const
{
   switch (opcode) {
   case SHADER_OPCODE_SEND:
      if (arg == 2) {
         return mlen * REG_SIZE;
      } else if (arg == 3) {
         return ex_mlen * REG_SIZE;
      }
      break;

   case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
   case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
      if (arg == 0)
         return mlen * REG_SIZE;
      break;

   case BRW_OPCODE_PLN:
      if (arg == 0)
         return 16;
      break;

   case SHADER_OPCODE_LOAD_PAYLOAD:
      if (arg < this->header_size)
         return retype(src[arg], BRW_TYPE_UD).component_size(8);
      break;

   case SHADER_OPCODE_BARRIER:
      return REG_SIZE;

   case SHADER_OPCODE_MOV_INDIRECT:
      if (arg == 0) {
         assert(src[2].file == IMM);
         return src[2].ud;
      }
      break;

   case BRW_OPCODE_DPAS: {
      /* This is a little bit sketchy. There's no way to get at devinfo from
       * here, so the regular reg_unit() cannot be used. However, on
       * reg_unit() == 1 platforms, DPAS exec_size must be 8, and on known
       * reg_unit() == 2 platforms, DPAS exec_size must be 16. This is not a
       * coincidence, so this isn't so bad.
       */
      const unsigned reg_unit = this->exec_size / 8;

      switch (arg) {
      case 0:
         if (src[0].type == BRW_TYPE_HF) {
            return rcount * reg_unit * REG_SIZE / 2;
         } else {
            return rcount * reg_unit * REG_SIZE;
         }
      case 1:
         return sdepth * reg_unit * REG_SIZE;
      case 2:
         /* This is simpler than the formula described in the Bspec, but it
          * covers all of the cases that we support. Each inner sdepth
          * iteration of the DPAS consumes a single dword for int8, uint8, or
          * float16 types. These are the one source types currently
          * supportable through Vulkan. This is independent of reg_unit.
          */
         return rcount * sdepth * 4;
      default:
         unreachable("Invalid source number.");
      }
      break;
   }

   default:
      break;
   }

   switch (src[arg].file) {
   case UNIFORM:
   case IMM:
      return components_read(arg) * brw_type_size_bytes(src[arg].type);
   case BAD_FILE:
   case ARF:
   case FIXED_GRF:
   case VGRF:
   case ATTR:
      return components_read(arg) * src[arg].component_size(exec_size);
   }
   return 0;
}

namespace {
   unsigned
   predicate_width(const intel_device_info *devinfo, brw_predicate predicate)
   {
      if (devinfo->ver >= 20) {
         return 1;
      } else {
         switch (predicate) {
         case BRW_PREDICATE_NONE:            return 1;
         case BRW_PREDICATE_NORMAL:          return 1;
         case BRW_PREDICATE_ALIGN1_ANY2H:    return 2;
         case BRW_PREDICATE_ALIGN1_ALL2H:    return 2;
         case BRW_PREDICATE_ALIGN1_ANY4H:    return 4;
         case BRW_PREDICATE_ALIGN1_ALL4H:    return 4;
         case BRW_PREDICATE_ALIGN1_ANY8H:    return 8;
         case BRW_PREDICATE_ALIGN1_ALL8H:    return 8;
         case BRW_PREDICATE_ALIGN1_ANY16H:   return 16;
         case BRW_PREDICATE_ALIGN1_ALL16H:   return 16;
         case BRW_PREDICATE_ALIGN1_ANY32H:   return 32;
         case BRW_PREDICATE_ALIGN1_ALL32H:   return 32;
         default: unreachable("Unsupported predicate");
         }
      }
   }
}

unsigned
fs_inst::flags_read(const intel_device_info *devinfo) const
{
   if (devinfo->ver < 20 && (predicate == BRW_PREDICATE_ALIGN1_ANYV ||
                             predicate == BRW_PREDICATE_ALIGN1_ALLV)) {
      /* The vertical predication modes combine corresponding bits from
       * f0.0 and f1.0 on Gfx7+.
       */
      const unsigned shift = 4;
      return brw_fs_flag_mask(this, 1) << shift | brw_fs_flag_mask(this, 1);
   } else if (predicate) {
      return brw_fs_flag_mask(this, predicate_width(devinfo, predicate));
   } else {
      unsigned mask = 0;
      for (int i = 0; i < sources; i++) {
         mask |= brw_fs_flag_mask(src[i], size_read(i));
      }
      return mask;
   }
}

unsigned
fs_inst::flags_written(const intel_device_info *devinfo) const
{
   if (conditional_mod && (opcode != BRW_OPCODE_SEL &&
                           opcode != BRW_OPCODE_CSEL &&
                           opcode != BRW_OPCODE_IF &&
                           opcode != BRW_OPCODE_WHILE)) {
      return brw_fs_flag_mask(this, 1);
   } else if (opcode == FS_OPCODE_LOAD_LIVE_CHANNELS) {
      return brw_fs_flag_mask(this, 32);
   } else {
      return brw_fs_flag_mask(dst, size_written);
   }
}

bool
fs_inst::has_sampler_residency() const
{
   switch (opcode) {
   case SHADER_OPCODE_TEX_LOGICAL:
   case FS_OPCODE_TXB_LOGICAL:
   case SHADER_OPCODE_TXL_LOGICAL:
   case SHADER_OPCODE_TXD_LOGICAL:
   case SHADER_OPCODE_TXF_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_W_GFX12_LOGICAL:
   case SHADER_OPCODE_TXF_CMS_W_LOGICAL:
   case SHADER_OPCODE_TXS_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOGICAL:
   case SHADER_OPCODE_TG4_LOGICAL:
   case SHADER_OPCODE_TG4_BIAS_LOGICAL:
   case SHADER_OPCODE_TG4_EXPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_IMPLICIT_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_LOD_LOGICAL:
   case SHADER_OPCODE_TG4_OFFSET_BIAS_LOGICAL:
      assert(src[TEX_LOGICAL_SRC_RESIDENCY].file == IMM);
      return src[TEX_LOGICAL_SRC_RESIDENCY].ud != 0;
   default:
      return false;
   }
}

/* \sa inst_is_raw_move in brw_eu_validate. */
bool
fs_inst::is_raw_move() const
{
   if (opcode != BRW_OPCODE_MOV)
      return false;

   if (src[0].file == IMM) {
      if (brw_type_is_vector_imm(src[0].type))
         return false;
   } else if (src[0].negate || src[0].abs) {
      return false;
   }

   if (saturate)
      return false;

   return src[0].type == dst.type ||
          (brw_type_is_int(src[0].type) &&
           brw_type_is_int(dst.type) &&
           brw_type_size_bits(src[0].type) == brw_type_size_bits(dst.type));
}

/* For SIMD16, we need to follow from the uniform setup of SIMD8 dispatch.
 * This brings in those uniform definitions
 */
void
fs_visitor::import_uniforms(fs_visitor *v)
{
   this->push_constant_loc = v->push_constant_loc;
   this->uniforms = v->uniforms;
}

enum brw_barycentric_mode
brw_barycentric_mode(const struct brw_wm_prog_key *key,
                     nir_intrinsic_instr *intr)
{
   const glsl_interp_mode mode =
      (enum glsl_interp_mode) nir_intrinsic_interp_mode(intr);

   /* Barycentric modes don't make sense for flat inputs. */
   assert(mode != INTERP_MODE_FLAT);

   unsigned bary;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_at_offset:
      /* When per sample interpolation is dynamic, assume sample
       * interpolation. We'll dynamically remap things so that the FS thread
       * payload is not affected.
       */
      bary = key->persample_interp == BRW_SOMETIMES ?
             BRW_BARYCENTRIC_PERSPECTIVE_SAMPLE :
             BRW_BARYCENTRIC_PERSPECTIVE_PIXEL;
      break;
   case nir_intrinsic_load_barycentric_centroid:
      bary = BRW_BARYCENTRIC_PERSPECTIVE_CENTROID;
      break;
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
      bary = BRW_BARYCENTRIC_PERSPECTIVE_SAMPLE;
      break;
   default:
      unreachable("invalid intrinsic");
   }

   if (mode == INTERP_MODE_NOPERSPECTIVE)
      bary += 3;

   return (enum brw_barycentric_mode) bary;
}

/**
 * Walk backwards from the end of the program looking for a URB write that
 * isn't in control flow, and mark it with EOT.
 *
 * Return true if successful or false if a separate EOT write is needed.
 */
bool
fs_visitor::mark_last_urb_write_with_eot()
{
   foreach_in_list_reverse(fs_inst, prev, &this->instructions) {
      if (prev->opcode == SHADER_OPCODE_URB_WRITE_LOGICAL) {
         prev->eot = true;

         /* Delete now dead instructions. */
         foreach_in_list_reverse_safe(exec_node, dead, &this->instructions) {
            if (dead == prev)
               break;
            dead->remove();
         }
         return true;
      } else if (prev->is_control_flow() || prev->has_side_effects()) {
         break;
      }
   }

   return false;
}

static unsigned
round_components_to_whole_registers(const intel_device_info *devinfo,
                                    unsigned c)
{
   return DIV_ROUND_UP(c, 8 * reg_unit(devinfo)) * reg_unit(devinfo);
}

void
fs_visitor::assign_curb_setup()
{
   unsigned uniform_push_length =
      round_components_to_whole_registers(devinfo, prog_data->nr_params);

   unsigned ubo_push_length = 0;
   unsigned ubo_push_start[4];
   for (int i = 0; i < 4; i++) {
      ubo_push_start[i] = 8 * (ubo_push_length + uniform_push_length);
      ubo_push_length += prog_data->ubo_ranges[i].length;

      assert(ubo_push_start[i] % (8 * reg_unit(devinfo)) == 0);
      assert(ubo_push_length % (1 * reg_unit(devinfo)) == 0);
   }

   prog_data->curb_read_length = uniform_push_length + ubo_push_length;
   if (stage == MESA_SHADER_FRAGMENT &&
       ((struct brw_wm_prog_key *)key)->null_push_constant_tbimr_workaround)
      prog_data->curb_read_length = MAX2(1, prog_data->curb_read_length);

   uint64_t used = 0;
   bool is_compute = gl_shader_stage_is_compute(stage);

   if (is_compute && devinfo->verx10 >= 125 && uniform_push_length > 0) {
      assert(devinfo->has_lsc);
      fs_builder ubld = fs_builder(this, 1).exec_all().at(
         cfg->first_block(), cfg->first_block()->start());

      /* The base offset for our push data is passed in as R0.0[31:6]. We have
       * to mask off the bottom 6 bits.
       */
      brw_reg base_addr =
         ubld.AND(retype(brw_vec1_grf(0, 0), BRW_TYPE_UD),
                  brw_imm_ud(INTEL_MASK(31, 6)));

      /* On Gfx12-HP we load constants at the start of the program using A32
       * stateless messages.
       */
      for (unsigned i = 0; i < uniform_push_length;) {
         /* Limit ourselves to LSC HW limit of 8 GRFs (256bytes D32V64). */
         unsigned num_regs = MIN2(uniform_push_length - i, 8);
         assert(num_regs > 0);
         num_regs = 1 << util_logbase2(num_regs);

         /* This pass occurs after all of the optimization passes, so don't
          * emit an 'ADD addr, base_addr, 0' instruction.
          */
         brw_reg addr = i == 0 ? base_addr :
            ubld.ADD(base_addr, brw_imm_ud(i * REG_SIZE));

         brw_reg srcs[4] = {
            brw_imm_ud(0), /* desc */
            brw_imm_ud(0), /* ex_desc */
            addr,          /* payload */
            brw_reg(),      /* payload2 */
         };

         brw_reg dest = retype(brw_vec8_grf(payload().num_regs + i, 0),
                              BRW_TYPE_UD);
         fs_inst *send = ubld.emit(SHADER_OPCODE_SEND, dest, srcs, 4);

         send->sfid = GFX12_SFID_UGM;
         send->desc = lsc_msg_desc(devinfo, LSC_OP_LOAD,
                                   LSC_ADDR_SURFTYPE_FLAT,
                                   LSC_ADDR_SIZE_A32,
                                   LSC_DATA_SIZE_D32,
                                   num_regs * 8 /* num_channels */,
                                   true /* transpose */,
                                   LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS));
         send->header_size = 0;
         send->mlen = lsc_msg_addr_len(devinfo, LSC_ADDR_SIZE_A32, 1);
         send->size_written =
            lsc_msg_dest_len(devinfo, LSC_DATA_SIZE_D32, num_regs * 8) * REG_SIZE;
         send->send_is_volatile = true;

         i += num_regs;
      }

      invalidate_analysis(DEPENDENCY_INSTRUCTIONS);
   }

   /* Map the offsets in the UNIFORM file to fixed HW regs. */
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      for (unsigned int i = 0; i < inst->sources; i++) {
	 if (inst->src[i].file == UNIFORM) {
            int uniform_nr = inst->src[i].nr + inst->src[i].offset / 4;
            int constant_nr;
            if (inst->src[i].nr >= UBO_START) {
               /* constant_nr is in 32-bit units, the rest are in bytes */
               constant_nr = ubo_push_start[inst->src[i].nr - UBO_START] +
                             inst->src[i].offset / 4;
            } else if (uniform_nr >= 0 && uniform_nr < (int) uniforms) {
               constant_nr = push_constant_loc[uniform_nr];
            } else {
               /* Section 5.11 of the OpenGL 4.1 spec says:
                * "Out-of-bounds reads return undefined values, which include
                *  values from other variables of the active program or zero."
                * Just return the first push constant.
                */
               constant_nr = 0;
            }

            assert(constant_nr / 8 < 64);
            used |= BITFIELD64_BIT(constant_nr / 8);

	    struct brw_reg brw_reg = brw_vec1_grf(payload().num_regs +
						  constant_nr / 8,
						  constant_nr % 8);
            brw_reg.abs = inst->src[i].abs;
            brw_reg.negate = inst->src[i].negate;

            assert(inst->src[i].stride == 0);
            inst->src[i] = byte_offset(
               retype(brw_reg, inst->src[i].type),
               inst->src[i].offset % 4);
	 }
      }
   }

   uint64_t want_zero = used & prog_data->zero_push_reg;
   if (want_zero) {
      fs_builder ubld = fs_builder(this, 8).exec_all().at(
         cfg->first_block(), cfg->first_block()->start());

      /* push_reg_mask_param is in 32-bit units */
      unsigned mask_param = prog_data->push_reg_mask_param;
      struct brw_reg mask = brw_vec1_grf(payload().num_regs + mask_param / 8,
                                                              mask_param % 8);

      brw_reg b32;
      for (unsigned i = 0; i < 64; i++) {
         if (i % 16 == 0 && (want_zero & BITFIELD64_RANGE(i, 16))) {
            brw_reg shifted = ubld.vgrf(BRW_TYPE_W, 2);
            ubld.SHL(horiz_offset(shifted, 8),
                     byte_offset(retype(mask, BRW_TYPE_W), i / 8),
                     brw_imm_v(0x01234567));
            ubld.SHL(shifted, horiz_offset(shifted, 8), brw_imm_w(8));

            fs_builder ubld16 = ubld.group(16, 0);
            b32 = ubld16.vgrf(BRW_TYPE_D);
            ubld16.group(16, 0).ASR(b32, shifted, brw_imm_w(15));
         }

         if (want_zero & BITFIELD64_BIT(i)) {
            assert(i < prog_data->curb_read_length);
            struct brw_reg push_reg =
               retype(brw_vec8_grf(payload().num_regs + i, 0), BRW_TYPE_D);

            ubld.AND(push_reg, push_reg, component(b32, i % 16));
         }
      }

      invalidate_analysis(DEPENDENCY_INSTRUCTIONS);
   }

   /* This may be updated in assign_urb_setup or assign_vs_urb_setup. */
   this->first_non_payload_grf = payload().num_regs + prog_data->curb_read_length;
}

/*
 * Build up an array of indices into the urb_setup array that
 * references the active entries of the urb_setup array.
 * Used to accelerate walking the active entries of the urb_setup array
 * on each upload.
 */
void
brw_compute_urb_setup_index(struct brw_wm_prog_data *wm_prog_data)
{
   /* TODO(mesh): Review usage of this in the context of Mesh, we may want to
    * skip per-primitive attributes here.
    */

   /* Make sure uint8_t is sufficient */
   STATIC_ASSERT(VARYING_SLOT_MAX <= 0xff);
   uint8_t index = 0;
   for (uint8_t attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (wm_prog_data->urb_setup[attr] >= 0) {
         wm_prog_data->urb_setup_attribs[index++] = attr;
      }
   }
   wm_prog_data->urb_setup_attribs_count = index;
}

void
fs_visitor::convert_attr_sources_to_hw_regs(fs_inst *inst)
{
   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].file == ATTR) {
         assert(inst->src[i].nr == 0);
         int grf = payload().num_regs +
                   prog_data->curb_read_length +
                   inst->src[i].offset / REG_SIZE;

         /* As explained at brw_reg_from_fs_reg, From the Haswell PRM:
          *
          * VertStride must be used to cross GRF register boundaries. This
          * rule implies that elements within a 'Width' cannot cross GRF
          * boundaries.
          *
          * So, for registers that are large enough, we have to split the exec
          * size in two and trust the compression state to sort it out.
          */
         unsigned total_size = inst->exec_size *
                               inst->src[i].stride *
                               brw_type_size_bytes(inst->src[i].type);

         assert(total_size <= 2 * REG_SIZE);
         const unsigned exec_size =
            (total_size <= REG_SIZE) ? inst->exec_size : inst->exec_size / 2;

         unsigned width = inst->src[i].stride == 0 ? 1 : exec_size;
         struct brw_reg reg =
            stride(byte_offset(retype(brw_vec8_grf(grf, 0), inst->src[i].type),
                               inst->src[i].offset % REG_SIZE),
                   exec_size * inst->src[i].stride,
                   width, inst->src[i].stride);
         reg.abs = inst->src[i].abs;
         reg.negate = inst->src[i].negate;

         inst->src[i] = reg;
      }
   }
}

int
brw_get_subgroup_id_param_index(const intel_device_info *devinfo,
                                const brw_stage_prog_data *prog_data)
{
   if (prog_data->nr_params == 0)
      return -1;

   if (devinfo->verx10 >= 125)
      return -1;

   /* The local thread id is always the last parameter in the list */
   uint32_t last_param = prog_data->param[prog_data->nr_params - 1];
   if (last_param == BRW_PARAM_BUILTIN_SUBGROUP_ID)
      return prog_data->nr_params - 1;

   return -1;
}

/**
 * Assign UNIFORM file registers to either push constants or pull constants.
 *
 * We allow a fragment shader to have more than the specified minimum
 * maximum number of fragment shader uniform components (64).  If
 * there are too many of these, they'd fill up all of register space.
 * So, this will push some of them out to the pull constant buffer and
 * update the program to load them.
 */
void
fs_visitor::assign_constant_locations()
{
   /* Only the first compile gets to decide on locations. */
   if (push_constant_loc)
      return;

   push_constant_loc = ralloc_array(mem_ctx, int, uniforms);
   for (unsigned u = 0; u < uniforms; u++)
      push_constant_loc[u] = u;

   /* Now that we know how many regular uniforms we'll push, reduce the
    * UBO push ranges so we don't exceed the 3DSTATE_CONSTANT limits.
    *
    * If changing this value, note the limitation about total_regs in
    * brw_curbe.c/crocus_state.c
    */
   const unsigned max_push_length = 64;
   unsigned push_length =
      round_components_to_whole_registers(devinfo, prog_data->nr_params);
   for (int i = 0; i < 4; i++) {
      struct brw_ubo_range *range = &prog_data->ubo_ranges[i];

      if (push_length + range->length > max_push_length)
         range->length = max_push_length - push_length;

      push_length += range->length;

      assert(push_length % (1 * reg_unit(devinfo)) == 0);

   }
   assert(push_length <= max_push_length);
}

bool
fs_visitor::get_pull_locs(const brw_reg &src,
                          unsigned *out_surf_index,
                          unsigned *out_pull_index)
{
   assert(src.file == UNIFORM);

   if (src.nr < UBO_START)
      return false;

   const struct brw_ubo_range *range =
      &prog_data->ubo_ranges[src.nr - UBO_START];

   /* If this access is in our (reduced) range, use the push data. */
   if (src.offset / 32 < range->length)
      return false;

   *out_surf_index = range->block;
   *out_pull_index = (32 * range->start + src.offset) / 4;

   prog_data->has_ubo_pull = true;

   return true;
}

/**
 * Get the mask of SIMD channels enabled during dispatch and not yet disabled
 * by discard.  Due to the layout of the sample mask in the fragment shader
 * thread payload, \p bld is required to have a dispatch_width() not greater
 * than 16 for fragment shaders.
 */
brw_reg
brw_sample_mask_reg(const fs_builder &bld)
{
   const fs_visitor &s = *bld.shader;

   if (s.stage != MESA_SHADER_FRAGMENT) {
      return brw_imm_ud(0xffffffff);
   } else if (s.devinfo->ver >= 20 ||
              brw_wm_prog_data(s.prog_data)->uses_kill) {
      return brw_flag_subreg(sample_mask_flag_subreg(s) + bld.group() / 16);
   } else {
      assert(bld.dispatch_width() <= 16);
      assert(s.devinfo->ver < 20);
      return retype(brw_vec1_grf((bld.group() >= 16 ? 2 : 1), 7),
                    BRW_TYPE_UW);
   }
}

uint32_t
brw_fb_write_msg_control(const fs_inst *inst,
                         const struct brw_wm_prog_data *prog_data)
{
   uint32_t mctl;

   if (prog_data->dual_src_blend) {
      assert(inst->exec_size < 32);

      if (inst->group % 16 == 0)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN01;
      else if (inst->group % 16 == 8)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN23;
      else
         unreachable("Invalid dual-source FB write instruction group");
   } else {
      assert(inst->group == 0 || (inst->group == 16 && inst->exec_size == 16));

      if (inst->exec_size == 16)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE;
      else if (inst->exec_size == 8)
         mctl = BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_SINGLE_SOURCE_SUBSPAN01;
      else if (inst->exec_size == 32)
         mctl = XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD32_SINGLE_SOURCE;
      else
         unreachable("Invalid FB write execution size");
   }

   return mctl;
}

 /**
 * Predicate the specified instruction on the sample mask.
 */
void
brw_emit_predicate_on_sample_mask(const fs_builder &bld, fs_inst *inst)
{
   assert(bld.shader->stage == MESA_SHADER_FRAGMENT &&
          bld.group() == inst->group &&
          bld.dispatch_width() == inst->exec_size);

   const fs_visitor &s = *bld.shader;
   const brw_reg sample_mask = brw_sample_mask_reg(bld);
   const unsigned subreg = sample_mask_flag_subreg(s);

   if (s.devinfo->ver >= 20 || brw_wm_prog_data(s.prog_data)->uses_kill) {
      assert(sample_mask.file == ARF &&
             sample_mask.nr == brw_flag_subreg(subreg).nr &&
             sample_mask.subnr == brw_flag_subreg(
                subreg + inst->group / 16).subnr);
   } else {
      bld.group(1, 0).exec_all()
         .MOV(brw_flag_subreg(subreg + inst->group / 16), sample_mask);
   }

   if (inst->predicate) {
      assert(inst->predicate == BRW_PREDICATE_NORMAL);
      assert(!inst->predicate_inverse);
      assert(inst->flag_subreg == 0);
      assert(s.devinfo->ver < 20);
      /* Combine the sample mask with the existing predicate by using a
       * vertical predication mode.
       */
      inst->predicate = BRW_PREDICATE_ALIGN1_ALLV;
   } else {
      inst->flag_subreg = subreg;
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->predicate_inverse = false;
   }
}

brw::register_pressure::register_pressure(const fs_visitor *v)
{
   const fs_live_variables &live = v->live_analysis.require();
   const unsigned num_instructions = v->cfg->num_blocks ?
      v->cfg->blocks[v->cfg->num_blocks - 1]->end_ip + 1 : 0;

   regs_live_at_ip = new unsigned[num_instructions]();

   for (unsigned reg = 0; reg < v->alloc.count; reg++) {
      for (int ip = live.vgrf_start[reg]; ip <= live.vgrf_end[reg]; ip++)
         regs_live_at_ip[ip] += v->alloc.sizes[reg];
   }

   const unsigned payload_count = v->first_non_payload_grf;

   int *payload_last_use_ip = new int[payload_count];
   v->calculate_payload_ranges(true, payload_count, payload_last_use_ip);

   for (unsigned reg = 0; reg < payload_count; reg++) {
      for (int ip = 0; ip < payload_last_use_ip[reg]; ip++)
         ++regs_live_at_ip[ip];
   }

   delete[] payload_last_use_ip;
}

brw::register_pressure::~register_pressure()
{
   delete[] regs_live_at_ip;
}

void
fs_visitor::invalidate_analysis(brw::analysis_dependency_class c)
{
   live_analysis.invalidate(c);
   regpressure_analysis.invalidate(c);
   idom_analysis.invalidate(c);
   def_analysis.invalidate(c);
}

void
fs_visitor::debug_optimizer(const nir_shader *nir,
                            const char *pass_name,
                            int iteration, int pass_num) const
{
   if (!brw_should_print_shader(nir, DEBUG_OPTIMIZER))
      return;

   char *filename;
   int ret = asprintf(&filename, "%s/%s%d-%s-%02d-%02d-%s",
                      debug_get_option("INTEL_SHADER_OPTIMIZER_PATH", "./"),
                      _mesa_shader_stage_to_abbrev(stage), dispatch_width, nir->info.name,
                      iteration, pass_num, pass_name);
   if (ret == -1)
      return;
   brw_print_instructions(*this, filename);
   free(filename);
}

static uint32_t
brw_compute_max_register_pressure(fs_visitor &s)
{
   const register_pressure &rp = s.regpressure_analysis.require();
   uint32_t ip = 0, max_pressure = 0;
   foreach_block_and_inst(block, fs_inst, inst, s.cfg) {
      max_pressure = MAX2(max_pressure, rp.regs_live_at_ip[ip]);
      ip++;
   }
   return max_pressure;
}

static fs_inst **
save_instruction_order(const struct cfg_t *cfg)
{
   /* Before we schedule anything, stash off the instruction order as an array
    * of fs_inst *.  This way, we can reset it between scheduling passes to
    * prevent dependencies between the different scheduling modes.
    */
   int num_insts = cfg->last_block()->end_ip + 1;
   fs_inst **inst_arr = new fs_inst * [num_insts];

   int ip = 0;
   foreach_block_and_inst(block, fs_inst, inst, cfg) {
      assert(ip >= block->start_ip && ip <= block->end_ip);
      inst_arr[ip++] = inst;
   }
   assert(ip == num_insts);

   return inst_arr;
}

static void
restore_instruction_order(struct cfg_t *cfg, fs_inst **inst_arr)
{
   ASSERTED int num_insts = cfg->last_block()->end_ip + 1;

   int ip = 0;
   foreach_block (block, cfg) {
      block->instructions.make_empty();

      assert(ip == block->start_ip);
      for (; ip <= block->end_ip; ip++)
         block->instructions.push_tail(inst_arr[ip]);
   }
   assert(ip == num_insts);
}

/* Per-thread scratch space is a power-of-two multiple of 1KB. */
static inline unsigned
brw_get_scratch_size(int size)
{
   return MAX2(1024, util_next_power_of_two(size));
}

void
brw_allocate_registers(fs_visitor &s, bool allow_spilling)
{
   const struct intel_device_info *devinfo = s.devinfo;
   const nir_shader *nir = s.nir;
   bool allocated;

   static const enum instruction_scheduler_mode pre_modes[] = {
      SCHEDULE_PRE,
      SCHEDULE_PRE_NON_LIFO,
      SCHEDULE_NONE,
      SCHEDULE_PRE_LIFO,
   };

   static const char *scheduler_mode_name[] = {
      [SCHEDULE_PRE] = "top-down",
      [SCHEDULE_PRE_NON_LIFO] = "non-lifo",
      [SCHEDULE_PRE_LIFO] = "lifo",
      [SCHEDULE_POST] = "post",
      [SCHEDULE_NONE] = "none",
   };

   uint32_t best_register_pressure = UINT32_MAX;
   enum instruction_scheduler_mode best_sched = SCHEDULE_NONE;

   brw_fs_opt_compact_virtual_grfs(s);

   if (s.needs_register_pressure)
      s.shader_stats.max_register_pressure = brw_compute_max_register_pressure(s);

   s.debug_optimizer(nir, "pre_register_allocate", 90, 90);

   bool spill_all = allow_spilling && INTEL_DEBUG(DEBUG_SPILL_FS);

   /* Before we schedule anything, stash off the instruction order as an array
    * of fs_inst *.  This way, we can reset it between scheduling passes to
    * prevent dependencies between the different scheduling modes.
    */
   fs_inst **orig_order = save_instruction_order(s.cfg);
   fs_inst **best_pressure_order = NULL;

   void *scheduler_ctx = ralloc_context(NULL);
   instruction_scheduler *sched = brw_prepare_scheduler(s, scheduler_ctx);

   /* Try each scheduling heuristic to see if it can successfully register
    * allocate without spilling.  They should be ordered by decreasing
    * performance but increasing likelihood of allocating.
    */
   for (unsigned i = 0; i < ARRAY_SIZE(pre_modes); i++) {
      enum instruction_scheduler_mode sched_mode = pre_modes[i];

      brw_schedule_instructions_pre_ra(s, sched, sched_mode);
      s.shader_stats.scheduler_mode = scheduler_mode_name[sched_mode];

      s.debug_optimizer(nir, s.shader_stats.scheduler_mode, 95, i);

      if (0) {
         brw_assign_regs_trivial(s);
         allocated = true;
         break;
      }

      /* We should only spill registers on the last scheduling. */
      assert(!s.spilled_any_registers);

      allocated = brw_assign_regs(s, false, spill_all);
      if (allocated)
         break;

      /* Save the maximum register pressure */
      uint32_t this_pressure = brw_compute_max_register_pressure(s);

      if (0) {
         fprintf(stderr, "Scheduler mode \"%s\" spilled, max pressure = %u\n",
                 scheduler_mode_name[sched_mode], this_pressure);
      }

      if (this_pressure < best_register_pressure) {
         best_register_pressure = this_pressure;
         best_sched = sched_mode;
         delete[] best_pressure_order;
         best_pressure_order = save_instruction_order(s.cfg);
      }

      /* Reset back to the original order before trying the next mode */
      restore_instruction_order(s.cfg, orig_order);
      s.invalidate_analysis(DEPENDENCY_INSTRUCTIONS);
   }

   ralloc_free(scheduler_ctx);

   if (!allocated) {
      if (0) {
         fprintf(stderr, "Spilling - using lowest-pressure mode \"%s\"\n",
                 scheduler_mode_name[best_sched]);
      }
      restore_instruction_order(s.cfg, best_pressure_order);
      s.shader_stats.scheduler_mode = scheduler_mode_name[best_sched];

      allocated = brw_assign_regs(s, allow_spilling, spill_all);
   }

   delete[] orig_order;
   delete[] best_pressure_order;

   if (!allocated) {
      s.fail("Failure to register allocate.  Reduce number of "
           "live scalar values to avoid this.");
   } else if (s.spilled_any_registers) {
      brw_shader_perf_log(s.compiler, s.log_data,
                          "%s shader triggered register spilling.  "
                          "Try reducing the number of live scalar "
                          "values to improve performance.\n",
                          _mesa_shader_stage_to_string(s.stage));
   }

   if (s.failed)
      return;

   s.debug_optimizer(nir, "post_ra_alloc", 96, 0);

   brw_fs_opt_bank_conflicts(s);

   s.debug_optimizer(nir, "bank_conflict", 96, 1);

   brw_schedule_instructions_post_ra(s);

   s.debug_optimizer(nir, "post_ra_alloc_scheduling", 96, 2);

   /* Lowering VGRF to FIXED_GRF is currently done as a separate pass instead
    * of part of assign_regs since both bank conflicts optimization and post
    * RA scheduling take advantage of distinguishing references to registers
    * that were allocated from references that were already fixed.
    *
    * TODO: Change the passes above, then move this lowering to be part of
    * assign_regs.
    */
   brw_fs_lower_vgrfs_to_fixed_grfs(s);

   s.debug_optimizer(nir, "lowered_vgrfs_to_fixed_grfs", 96, 3);

   brw_shader_phase_update(s, BRW_SHADER_PHASE_AFTER_REGALLOC);

   if (s.last_scratch > 0) {
      /* We currently only support up to 2MB of scratch space.  If we
       * need to support more eventually, the documentation suggests
       * that we could allocate a larger buffer, and partition it out
       * ourselves.  We'd just have to undo the hardware's address
       * calculation by subtracting (FFTID * Per Thread Scratch Space)
       * and then add FFTID * (Larger Per Thread Scratch Space).
       *
       * See 3D-Media-GPGPU Engine > Media GPGPU Pipeline >
       * Thread Group Tracking > Local Memory/Scratch Space.
       */
      if (s.last_scratch <= devinfo->max_scratch_size_per_thread) {
         /* Take the max of any previously compiled variant of the shader. In the
          * case of bindless shaders with return parts, this will also take the
          * max of all parts.
          */
         s.prog_data->total_scratch = MAX2(brw_get_scratch_size(s.last_scratch),
                                           s.prog_data->total_scratch);
      } else {
         s.fail("Scratch space required is larger than supported");
      }
   }

   if (s.failed)
      return;

   brw_fs_lower_scoreboard(s);
}

/**
 * Move load_interpolated_input with simple (payload-based) barycentric modes
 * to the top of the program so we don't emit multiple PLNs for the same input.
 *
 * This works around CSE not being able to handle non-dominating cases
 * such as:
 *
 *    if (...) {
 *       interpolate input
 *    } else {
 *       interpolate the same exact input
 *    }
 *
 * This should be replaced by global value numbering someday.
 */
bool
brw_nir_move_interpolation_to_top(nir_shader *nir)
{
   bool progress = false;

   nir_foreach_function_impl(impl, nir) {
      nir_block *top = nir_start_block(impl);
      nir_cursor cursor = nir_before_instr(nir_block_first_instr(top));
      bool impl_progress = false;

      for (nir_block *block = nir_block_cf_tree_next(top);
           block != NULL;
           block = nir_block_cf_tree_next(block)) {

         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_interpolated_input)
               continue;
            nir_intrinsic_instr *bary_intrinsic =
               nir_instr_as_intrinsic(intrin->src[0].ssa->parent_instr);
            nir_intrinsic_op op = bary_intrinsic->intrinsic;

            /* Leave interpolateAtSample/Offset() where they are. */
            if (op == nir_intrinsic_load_barycentric_at_sample ||
                op == nir_intrinsic_load_barycentric_at_offset)
               continue;

            nir_instr *move[3] = {
               &bary_intrinsic->instr,
               intrin->src[1].ssa->parent_instr,
               instr
            };

            for (unsigned i = 0; i < ARRAY_SIZE(move); i++) {
               if (move[i]->block != top) {
                  nir_instr_move(cursor, move[i]);
                  impl_progress = true;
               }
            }
         }
      }

      progress = progress || impl_progress;

      nir_metadata_preserve(impl, impl_progress ? nir_metadata_control_flow
                                                : nir_metadata_all);
   }

   return progress;
}

unsigned
brw_cs_push_const_total_size(const struct brw_cs_prog_data *cs_prog_data,
                             unsigned threads)
{
   assert(cs_prog_data->push.per_thread.size % REG_SIZE == 0);
   assert(cs_prog_data->push.cross_thread.size % REG_SIZE == 0);
   return cs_prog_data->push.per_thread.size * threads +
          cs_prog_data->push.cross_thread.size;
}

static bool
filter_simd(const nir_instr *instr, const void * /* options */)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   switch (nir_instr_as_intrinsic(instr)->intrinsic) {
   case nir_intrinsic_load_simd_width_intel:
   case nir_intrinsic_load_subgroup_id:
      return true;

   default:
      return false;
   }
}

static nir_def *
lower_simd(nir_builder *b, nir_instr *instr, void *options)
{
   uintptr_t simd_width = (uintptr_t)options;

   switch (nir_instr_as_intrinsic(instr)->intrinsic) {
   case nir_intrinsic_load_simd_width_intel:
      return nir_imm_int(b, simd_width);

   case nir_intrinsic_load_subgroup_id:
      /* If the whole workgroup fits in one thread, we can lower subgroup_id
       * to a constant zero.
       */
      if (!b->shader->info.workgroup_size_variable) {
         unsigned local_workgroup_size = b->shader->info.workgroup_size[0] *
                                         b->shader->info.workgroup_size[1] *
                                         b->shader->info.workgroup_size[2];
         if (local_workgroup_size <= simd_width)
            return nir_imm_int(b, 0);
      }
      return NULL;

   default:
      return NULL;
   }
}

bool
brw_nir_lower_simd(nir_shader *nir, unsigned dispatch_width)
{
   return nir_shader_lower_instructions(nir, filter_simd, lower_simd,
                                 (void *)(uintptr_t)dispatch_width);
}

struct intel_cs_dispatch_info
brw_cs_get_dispatch_info(const struct intel_device_info *devinfo,
                         const struct brw_cs_prog_data *prog_data,
                         const unsigned *override_local_size)
{
   struct intel_cs_dispatch_info info = {};

   const unsigned *sizes =
      override_local_size ? override_local_size :
                            prog_data->local_size;

   const int simd = brw_simd_select_for_workgroup_size(devinfo, prog_data, sizes);
   assert(simd >= 0 && simd < 3);

   info.group_size = sizes[0] * sizes[1] * sizes[2];
   info.simd_size = 8u << simd;
   info.threads = DIV_ROUND_UP(info.group_size, info.simd_size);

   const uint32_t remainder = info.group_size & (info.simd_size - 1);
   if (remainder > 0)
      info.right_mask = ~0u >> (32 - remainder);
   else
      info.right_mask = ~0u >> (32 - info.simd_size);

   return info;
}

void
brw_shader_phase_update(fs_visitor &s, enum brw_shader_phase phase)
{
   assert(phase == s.phase + 1);
   s.phase = phase;
   brw_fs_validate(s);
}

bool brw_should_print_shader(const nir_shader *shader, uint64_t debug_flag)
{
   return INTEL_DEBUG(debug_flag) && (!shader->info.internal || NIR_DEBUG(PRINT_INTERNAL));
}

namespace brw {
   brw_reg
   fetch_payload_reg(const brw::fs_builder &bld, uint8_t regs[2],
                     brw_reg_type type, unsigned n)
   {
      if (!regs[0])
         return brw_reg();

      if (bld.dispatch_width() > 16) {
         const brw_reg tmp = bld.vgrf(type, n);
         const brw::fs_builder hbld = bld.exec_all().group(16, 0);
         const unsigned m = bld.dispatch_width() / hbld.dispatch_width();
         brw_reg *const components = new brw_reg[m * n];

         for (unsigned c = 0; c < n; c++) {
            for (unsigned g = 0; g < m; g++)
               components[c * m + g] =
                  offset(retype(brw_vec8_grf(regs[g], 0), type), hbld, c);
         }

         hbld.LOAD_PAYLOAD(tmp, components, m * n, 0);

         delete[] components;
         return tmp;

      } else {
         return brw_reg(retype(brw_vec8_grf(regs[0], 0), type));
      }
   }

   brw_reg
   fetch_barycentric_reg(const brw::fs_builder &bld, uint8_t regs[2])
   {
      if (!regs[0])
         return brw_reg();
      else if (bld.shader->devinfo->ver >= 20)
         return fetch_payload_reg(bld, regs, BRW_TYPE_F, 2);

      const brw_reg tmp = bld.vgrf(BRW_TYPE_F, 2);
      const brw::fs_builder hbld = bld.exec_all().group(8, 0);
      const unsigned m = bld.dispatch_width() / hbld.dispatch_width();
      brw_reg *const components = new brw_reg[2 * m];

      for (unsigned c = 0; c < 2; c++) {
         for (unsigned g = 0; g < m; g++)
            components[c * m + g] = offset(brw_vec8_grf(regs[g / 2], 0),
                                           hbld, c + 2 * (g % 2));
      }

      hbld.LOAD_PAYLOAD(tmp, components, 2 * m, 0);

      delete[] components;
      return tmp;
   }

   void
   check_dynamic_msaa_flag(const fs_builder &bld,
                           const struct brw_wm_prog_data *wm_prog_data,
                           enum intel_msaa_flags flag)
   {
      fs_inst *inst = bld.AND(bld.null_reg_ud(),
                              dynamic_msaa_flags(wm_prog_data),
                              brw_imm_ud(flag));
      inst->conditional_mod = BRW_CONDITIONAL_NZ;
   }
}

