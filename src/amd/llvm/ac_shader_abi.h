/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SHADER_ABI_H
#define AC_SHADER_ABI_H

#include "ac_shader_util.h"
#include "compiler/shader_enums.h"
#include <llvm-c/Core.h>

#include <assert.h>

#define AC_LLVM_MAX_OUTPUTS (VARYING_SLOT_VAR31 + 1)

/* Document the shader ABI during compilation. This is what allows radeonsi and
 * radv to share a compiler backend.
 */
struct ac_shader_abi {
   /* Each entry is a pointer to a f32 or a f16 value (only possible for FS) */
   LLVMValueRef outputs[AC_LLVM_MAX_OUTPUTS * 4];
   bool is_16bit[AC_LLVM_MAX_OUTPUTS * 4];

   /* The result must be either scalar or vector i16 or i32. */
   LLVMValueRef (*load_tess_varyings)(struct ac_shader_abi *abi, unsigned num_components,
                                      unsigned bit_size, gl_varying_slot slot, unsigned component,
                                      bool high_16bits);

   LLVMValueRef (*load_ubo)(struct ac_shader_abi *abi, LLVMValueRef index);

   /**
    * Load the descriptor for the given buffer.
    *
    * \param buffer the buffer as presented in NIR: this is the descriptor
    *               in Vulkan, and the buffer index in OpenGL/Gallium
    * \param write whether buffer contents will be written
    * \param non_uniform whether the buffer descriptor is not assumed to be uniform
    */
   LLVMValueRef (*load_ssbo)(struct ac_shader_abi *abi, LLVMValueRef buffer, bool write, bool non_uniform);

   /**
    * Load a descriptor associated to a sampler.
    *
    * \param index of the descriptor
    * \param desc_type the type of descriptor to load
    */
   LLVMValueRef (*load_sampler_desc)(struct ac_shader_abi *abi, LLVMValueRef index,
                                     enum ac_descriptor_type desc_type);

   /* Whether to clamp the shadow reference value to [0,1]on GFX8. Radeonsi currently
    * uses it due to promoting D16 to D32, but radv needs it off. */
   bool clamp_shadow_reference;

   /* Whether bounds checks are required */
   bool robust_buffer_access;

   /* Check for Inf interpolation coeff */
   bool kill_ps_if_inf_interp;

   /* Clamp div by 0 (so it won't produce NaN) */
   bool clamp_div_by_zero;

   /* Whether to disable anisotropic filtering. */
   bool disable_aniso_single_level;
};

#endif /* AC_SHADER_ABI_H */
