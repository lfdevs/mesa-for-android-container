/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */


#ifndef BRW_VS_H
#define BRW_VS_H


#include "brw_vec4.h"

/**
 * The VF can't natively handle certain types of attributes, such as GL_FIXED
 * or most 10_10_10_2 types.  These flags enable various VS workarounds to
 * "fix" attributes at the beginning of shaders.
 */
#define BRW_ATTRIB_WA_COMPONENT_MASK    7  /* mask for GL_FIXED scale channel count */
#define BRW_ATTRIB_WA_NORMALIZE     8   /* normalize in shader */
#define BRW_ATTRIB_WA_BGRA          16  /* swap r/b channels in shader */
#define BRW_ATTRIB_WA_SIGN          32  /* interpret as signed in shader */
#define BRW_ATTRIB_WA_SCALE         64  /* interpret as scaled in shader */

#ifdef __cplusplus
extern "C" {
#endif

void brw_vs_debug_recompile(struct brw_context *brw,
                            struct gl_shader_program *prog,
                            const struct brw_vs_prog_key *key);

void
brw_upload_vs_prog(struct brw_context *brw);

bool
brw_codegen_vs_prog(struct brw_context *brw,
                    struct gl_shader_program *prog,
                    struct brw_vertex_program *vp,
                    struct brw_vs_prog_key *key);

#ifdef __cplusplus
} /* extern "C" */


namespace brw {

class vec4_vs_visitor : public vec4_visitor
{
public:
   vec4_vs_visitor(const struct brw_compiler *compiler,
                   void *log_data,
                   const struct brw_vs_prog_key *key,
                   struct brw_vs_prog_data *vs_prog_data,
                   const nir_shader *shader,
                   gl_clip_plane *clip_planes,
                   void *mem_ctx,
                   int shader_time_index,
                   bool use_legacy_snorm_formula);

protected:
   virtual dst_reg *make_reg_for_system_value(int location);
   virtual void setup_payload();
   virtual void emit_prolog();
   virtual void emit_thread_end();
   virtual void emit_urb_write_header(int mrf);
   virtual void emit_urb_slot(dst_reg reg, int varying);
   virtual vec4_instruction *emit_urb_write_opcode(bool complete);

private:
   int setup_attributes(int payload_reg);
   void setup_uniform_clipplane_values();
   void emit_clip_distances(dst_reg reg, int offset);

   const struct brw_vs_prog_key *const key;
   struct brw_vs_prog_data * const vs_prog_data;

   gl_clip_plane *clip_planes;

   bool use_legacy_snorm_formula;
};

} /* namespace brw */


#endif

#endif
