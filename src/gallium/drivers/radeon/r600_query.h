/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *  Nicolai Hähnle <nicolai.haehnle@amd.com>
 *
 */

#ifndef R600_QUERY_H
#define R600_QUERY_H

#include "pipe/p_defines.h"
#include "util/list.h"

struct pipe_context;
struct pipe_query;

struct r600_common_context;
struct r600_common_screen;
struct r600_query;
struct r600_query_hw;
struct r600_resource;

#define R600_QUERY_DRAW_CALLS		(PIPE_QUERY_DRIVER_SPECIFIC + 0)
#define R600_QUERY_REQUESTED_VRAM	(PIPE_QUERY_DRIVER_SPECIFIC + 1)
#define R600_QUERY_REQUESTED_GTT	(PIPE_QUERY_DRIVER_SPECIFIC + 2)
#define R600_QUERY_BUFFER_WAIT_TIME	(PIPE_QUERY_DRIVER_SPECIFIC + 3)
#define R600_QUERY_NUM_CS_FLUSHES	(PIPE_QUERY_DRIVER_SPECIFIC + 4)
#define R600_QUERY_NUM_BYTES_MOVED	(PIPE_QUERY_DRIVER_SPECIFIC + 5)
#define R600_QUERY_VRAM_USAGE		(PIPE_QUERY_DRIVER_SPECIFIC + 6)
#define R600_QUERY_GTT_USAGE		(PIPE_QUERY_DRIVER_SPECIFIC + 7)
#define R600_QUERY_GPU_TEMPERATURE	(PIPE_QUERY_DRIVER_SPECIFIC + 8)
#define R600_QUERY_CURRENT_GPU_SCLK	(PIPE_QUERY_DRIVER_SPECIFIC + 9)
#define R600_QUERY_CURRENT_GPU_MCLK	(PIPE_QUERY_DRIVER_SPECIFIC + 10)
#define R600_QUERY_GPU_LOAD		(PIPE_QUERY_DRIVER_SPECIFIC + 11)
#define R600_QUERY_NUM_COMPILATIONS	(PIPE_QUERY_DRIVER_SPECIFIC + 12)
#define R600_QUERY_NUM_SHADERS_CREATED	(PIPE_QUERY_DRIVER_SPECIFIC + 13)
#define R600_QUERY_GPIN_ASIC_ID		(PIPE_QUERY_DRIVER_SPECIFIC + 14)
#define R600_QUERY_GPIN_NUM_SIMD	(PIPE_QUERY_DRIVER_SPECIFIC + 15)
#define R600_QUERY_GPIN_NUM_RB		(PIPE_QUERY_DRIVER_SPECIFIC + 16)
#define R600_QUERY_GPIN_NUM_SPI		(PIPE_QUERY_DRIVER_SPECIFIC + 17)
#define R600_QUERY_GPIN_NUM_SE		(PIPE_QUERY_DRIVER_SPECIFIC + 18)
#define R600_QUERY_FIRST_PERFCOUNTER	(PIPE_QUERY_DRIVER_SPECIFIC + 100)

enum {
	R600_QUERY_GROUP_GPIN = 0,
	R600_NUM_SW_QUERY_GROUPS
};

struct r600_query_ops {
	void (*destroy)(struct r600_common_context *, struct r600_query *);
	boolean (*begin)(struct r600_common_context *, struct r600_query *);
	void (*end)(struct r600_common_context *, struct r600_query *);
	boolean (*get_result)(struct r600_common_context *,
			      struct r600_query *, boolean wait,
			      union pipe_query_result *result);
};

struct r600_query {
	struct r600_query_ops *ops;

	/* The type of query */
	unsigned type;
};

enum {
	R600_QUERY_HW_FLAG_NO_START = (1 << 0),
	R600_QUERY_HW_FLAG_TIMER = (1 << 1),
	R600_QUERY_HW_FLAG_PREDICATE = (1 << 2),
};

struct r600_query_hw_ops {
	void (*prepare_buffer)(struct r600_common_context *,
			       struct r600_query_hw *,
			       struct r600_resource *);
	void (*emit_start)(struct r600_common_context *,
			   struct r600_query_hw *,
			   struct r600_resource *buffer, uint64_t va);
	void (*emit_stop)(struct r600_common_context *,
			  struct r600_query_hw *,
			  struct r600_resource *buffer, uint64_t va);
	void (*clear_result)(struct r600_query_hw *, union pipe_query_result *);
	void (*add_result)(struct r600_common_context *ctx,
			   struct r600_query_hw *, void *buffer,
			   union pipe_query_result *result);
};

struct r600_query_buffer {
	/* The buffer where query results are stored. */
	struct r600_resource		*buf;
	/* Offset of the next free result after current query data */
	unsigned			results_end;
	/* If a query buffer is full, a new buffer is created and the old one
	 * is put in here. When we calculate the result, we sum up the samples
	 * from all buffers. */
	struct r600_query_buffer	*previous;
};

struct r600_query_hw {
	struct r600_query b;
	struct r600_query_hw_ops *ops;
	unsigned flags;

	/* The query buffer and how many results are in it. */
	struct r600_query_buffer buffer;
	/* Size of the result in memory for both begin_query and end_query,
	 * this can be one or two numbers, or it could even be a size of a structure. */
	unsigned result_size;
	/* The number of dwords for begin_query or end_query. */
	unsigned num_cs_dw_begin;
	unsigned num_cs_dw_end;
	/* Linked list of queries */
	struct list_head list;
	/* For transform feedback: which stream the query is for */
	unsigned stream;
};

boolean r600_query_hw_init(struct r600_common_context *rctx,
			   struct r600_query_hw *query);
void r600_query_hw_destroy(struct r600_common_context *rctx,
			   struct r600_query *rquery);
boolean r600_query_hw_begin(struct r600_common_context *rctx,
			    struct r600_query *rquery);
void r600_query_hw_end(struct r600_common_context *rctx,
		       struct r600_query *rquery);
boolean r600_query_hw_get_result(struct r600_common_context *rctx,
				 struct r600_query *rquery,
				 boolean wait,
				 union pipe_query_result *result);

/* Performance counters */
enum {
	/* This block is part of the shader engine */
	R600_PC_BLOCK_SE = (1 << 0),

	/* Expose per-instance groups instead of summing all instances (within
	 * an SE). */
	R600_PC_BLOCK_INSTANCE_GROUPS = (1 << 1),

	/* Expose per-SE groups instead of summing instances across SEs. */
	R600_PC_BLOCK_SE_GROUPS = (1 << 2),

	/* Shader block */
	R600_PC_BLOCK_SHADER = (1 << 3),

	/* Non-shader block with perfcounters windowed by shaders. */
	R600_PC_BLOCK_SHADER_WINDOWED = (1 << 4),
};

/* Describes a hardware block with performance counters. Multiple instances of
 * each block, possibly per-SE, may exist on the chip. Depending on the block
 * and on the user's configuration, we either
 *  (a) expose every instance as a performance counter group,
 *  (b) expose a single performance counter group that reports the sum over all
 *      instances, or
 *  (c) expose one performance counter group per instance, but summed over all
 *      shader engines.
 */
struct r600_perfcounter_block {
	const char *basename;
	unsigned flags;
	unsigned num_counters;
	unsigned num_selectors;
	unsigned num_instances;

	unsigned num_groups;
	char *group_names;
	unsigned group_name_stride;

	char *selector_names;
	unsigned selector_name_stride;

	void *data;
};

struct r600_perfcounters {
	unsigned num_groups;
	unsigned num_blocks;
	struct r600_perfcounter_block *blocks;

	unsigned num_start_cs_dwords;
	unsigned num_stop_cs_dwords;
	unsigned num_instance_cs_dwords;
	unsigned num_shaders_cs_dwords;

	unsigned num_shader_types;
	const char * const *shader_type_suffixes;
	const unsigned *shader_type_bits;

	void (*get_size)(struct r600_perfcounter_block *,
			 unsigned count, unsigned *selectors,
			 unsigned *num_select_dw, unsigned *num_read_dw);

	void (*emit_instance)(struct r600_common_context *,
			      int se, int instance);
	void (*emit_shaders)(struct r600_common_context *, unsigned shaders);
	void (*emit_select)(struct r600_common_context *,
			    struct r600_perfcounter_block *,
			    unsigned count, unsigned *selectors);
	void (*emit_start)(struct r600_common_context *,
			  struct r600_resource *buffer, uint64_t va);
	void (*emit_stop)(struct r600_common_context *,
			  struct r600_resource *buffer, uint64_t va);
	void (*emit_read)(struct r600_common_context *,
			  struct r600_perfcounter_block *,
			  unsigned count, unsigned *selectors,
			  struct r600_resource *buffer, uint64_t va);

	void (*cleanup)(struct r600_common_screen *);

	boolean separate_se;
	boolean separate_instance;
};

struct pipe_query *r600_create_batch_query(struct pipe_context *ctx,
					   unsigned num_queries,
					   unsigned *query_types);

int r600_get_perfcounter_info(struct r600_common_screen *,
			      unsigned index,
			      struct pipe_driver_query_info *info);
int r600_get_perfcounter_group_info(struct r600_common_screen *,
				    unsigned index,
				    struct pipe_driver_query_group_info *info);

boolean r600_perfcounters_init(struct r600_perfcounters *, unsigned num_blocks);
void r600_perfcounters_add_block(struct r600_common_screen *,
				 struct r600_perfcounters *,
				 const char *name, unsigned flags,
				 unsigned counters, unsigned selectors,
				 unsigned instances, void *data);
void r600_perfcounters_do_destroy(struct r600_perfcounters *);

#endif /* R600_QUERY_H */
