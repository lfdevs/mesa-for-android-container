/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2015 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ILO_STATE_SOL_H
#define ILO_STATE_SOL_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

/*
 * From the Ivy Bridge PRM, volume 2 part 1, page 193:
 *
 *     "Incoming topologies are tagged with a 2-bit StreamID."
 */
#define ILO_STATE_SOL_MAX_STREAM_COUNT 4

/*
 * From the Ivy Bridge PRM, volume 2 part 1, page 195:
 *
 *     "Up to four SO buffers are supported."
 */
#define ILO_STATE_SOL_MAX_BUFFER_COUNT 4

/*
 * From the Ivy Bridge PRM, volume 2 part 1, page 201:
 *
 *     "All 128 decls..."
 */
#define ILO_STATE_SOL_MAX_DECL_COUNT 128

/**
 * Output a vertex attribute.
 */
struct ilo_state_sol_decl_info {
   /* select an attribute from read ones */
   uint8_t attr;
   bool is_hole;

   /* which components to write */
   uint8_t component_base;
   uint8_t component_count;

   /* destination buffer */
   uint8_t buffer;
};

struct ilo_state_sol_stream_info {
   /* which VUE attributes to read */
   uint8_t cv_vue_attr_count;
   uint8_t vue_read_base;
   uint8_t vue_read_count;

   uint8_t decl_count;
   const struct ilo_state_sol_decl_info *decls;
};

struct ilo_state_sol_info {
   void *data;
   size_t data_size;

   bool sol_enable;
   bool stats_enable;
   enum gen_reorder_mode tristrip_reorder;

   bool render_disable;
   /* ignored when SOL is disabled */
   uint8_t render_stream;

   /* a buffer is disabled when its stride is zero */
   uint16_t buffer_strides[ILO_STATE_SOL_MAX_BUFFER_COUNT];

   struct ilo_state_sol_stream_info streams[ILO_STATE_SOL_MAX_STREAM_COUNT];
};

struct ilo_state_sol {
   uint32_t streamout[2];
   uint16_t strides[4];

   uint32_t so_decl[2];
   uint32_t (*decl)[2];
   uint8_t decl_count;
};

struct ilo_vma;

struct ilo_state_sol_buffer_info {
   const struct ilo_vma *vma;
   uint32_t offset;
   uint32_t size;

   /* Gen8+ only; at least sizeof(uint32_t) bytes */
   const struct ilo_vma *write_offset_vma;
   uint32_t write_offset_offset;

   bool write_offset_load;
   bool write_offset_save;

   bool write_offset_imm_enable;
   uint32_t write_offset_imm;
};

struct ilo_state_sol_buffer {
   uint32_t so_buf[5];

   const struct ilo_vma *vma;
   const struct ilo_vma *write_offset_vma;
};

static inline size_t
ilo_state_sol_data_size(const struct ilo_dev *dev, uint8_t max_decl_count)
{
   const struct ilo_state_sol *so = NULL;
   return (ilo_dev_gen(dev) >= ILO_GEN(7)) ?
      sizeof(so->decl[0]) * max_decl_count : 0;
}

bool
ilo_state_sol_init(struct ilo_state_sol *sol,
                   const struct ilo_dev *dev,
                   const struct ilo_state_sol_info *info);

bool
ilo_state_sol_init_disabled(struct ilo_state_sol *sol,
                            const struct ilo_dev *dev,
                            bool render_disable);

uint32_t
ilo_state_sol_buffer_size(const struct ilo_dev *dev, uint32_t size,
                          uint32_t *alignment);

bool
ilo_state_sol_buffer_init(struct ilo_state_sol_buffer *sb,
                          const struct ilo_dev *dev,
                          const struct ilo_state_sol_buffer_info *info);

bool
ilo_state_sol_buffer_init_disabled(struct ilo_state_sol_buffer *sb,
                                   const struct ilo_dev *dev);

#endif /* ILO_STATE_SOL_H */
