/*
 * Copyright (c) 2017 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>

#include "util/build_id.h"
#include "util/mesa-sha1.h"

#include "brw_context.h"
#include "brw_program.h"

static uint8_t driver_sha1[20];

void
brw_program_binary_init(unsigned device_id)
{
   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(brw_program_binary_init);
   assert(note);

   /**
    * With Mesa's megadrivers, taking the sha1 of i965_dri.so may not be
    * unique. Therefore, we make a sha1 of the "i965" string and the sha1
    * build id from i965_dri.so.
    */
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);
   char renderer[10];
   assert(device_id < 0x10000);
   int len = snprintf(renderer, sizeof(renderer), "i965_%04x", device_id);
   assert(len == sizeof(renderer) - 1);
   _mesa_sha1_update(&ctx, renderer, len);
   _mesa_sha1_update(&ctx, build_id_data(note), build_id_length(note));
   _mesa_sha1_final(&ctx, driver_sha1);
}

void
brw_get_program_binary_driver_sha1(struct gl_context *ctx, uint8_t *sha1)
{
   memcpy(sha1, driver_sha1, sizeof(uint8_t) * 20);
}

/* This is just a wrapper around brw_program_deserialize_nir() as i965
 * doesn't need gl_shader_program like other drivers do.
 */
void
brw_deserialize_program_binary(struct gl_context *ctx,
                               struct gl_shader_program *shProg,
                               struct gl_program *prog)
{
   brw_program_deserialize_nir(ctx, prog, prog->info.stage);
}
