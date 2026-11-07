/*
 * Copyright 2023 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 */

#include "u_gpuvis.h"

#include <threads.h>

#include "util/log.h"
#include "util/rand_xor.h"
#include "util/u_dynarray.h"

#define GPUVIS_TRACE_IMPLEMENTATION
#include "gpuvis_trace_utils.h"

static once_flag gpuvis_once_flag = ONCE_FLAG_INIT;
static bool gpuvis_initialized = false;
static tss_t gpuvis_tls;

struct gpuvis_thread_state {
   struct util_dynarray stack;
   uint64_t seed[2];
};

static void
util_gpuvis_thread_state_fini(struct gpuvis_thread_state *state)
{
   util_dynarray_fini(&state->stack);
   free(state);
}

static void
util_gpuvis_thread_state_dtor(void *data)
{
   struct gpuvis_thread_state *state = data;
   if (!state)
      return;

   /* Pop all remaining contexts to mark ends at thread exit. */
   while (state->stack.size >= sizeof(uint64_t)) {
      uint64_t ctx = util_dynarray_pop(&state->stack, uint64_t);
      gpuvis_trace_end_ctx_printf(ctx, "%s", "");
   }

   util_gpuvis_thread_state_fini(state);
}

static void
util_gpuvis_init_once()
{
   if (gpuvis_trace_init() < 0) {
      mesa_logw("util_gpuvis: Failed to initialize tracing.\n");
      return;
   }

   if (tss_create(&gpuvis_tls, util_gpuvis_thread_state_dtor) !=
       thrd_success) {
      mesa_logw("util_gpuvis: Failed to create TLS key.\n");
      return;
   }

   gpuvis_initialized = true;
}

static struct gpuvis_thread_state *
util_gpuvis_thread_get(void)
{
   call_once(&gpuvis_once_flag, util_gpuvis_init_once);
   if (!gpuvis_initialized)
      return NULL;

   struct gpuvis_thread_state *gpuvis_thread = tss_get(gpuvis_tls);
   if (gpuvis_thread)
      return gpuvis_thread;

   gpuvis_thread = calloc(1, sizeof(*gpuvis_thread));
   if (!gpuvis_thread) {
      mesa_logw("util_gpuvis: Failed to allocate thread state.\n");
      return NULL;
   }

   util_dynarray_init(&gpuvis_thread->stack, NULL);
   s_rand_xorshift128plus(gpuvis_thread->seed, true);

   if (tss_set(gpuvis_tls, gpuvis_thread) != thrd_success) {
      mesa_logw("util_gpuvis: Failed to set thread state in TLS.\n");
      util_gpuvis_thread_state_fini(gpuvis_thread);
      return NULL;
   }

   return gpuvis_thread;
}

static uint64_t
util_gpuvis_thread_push_ctx(void)
{
   struct gpuvis_thread_state *gpuvis_thread = util_gpuvis_thread_get();
   if (!gpuvis_thread)
      return 0;

   uint64_t ctx;
   do {
      ctx = rand_xorshift128plus(gpuvis_thread->seed);
   } while (ctx == 0); /* 0 is reserved. */
   util_dynarray_append(&gpuvis_thread->stack, ctx);

   return ctx;
}

static uint64_t
util_gpuvis_thread_pop_ctx(void)
{
   struct gpuvis_thread_state *gpuvis_thread = util_gpuvis_thread_get();
   if (!gpuvis_thread || gpuvis_thread->stack.size < sizeof(uint64_t))
      return 0;

   return util_dynarray_pop(&gpuvis_thread->stack, uint64_t);
}

void
util_gpuvis_init(void)
{
   call_once(&gpuvis_once_flag, util_gpuvis_init_once);
}

void
util_gpuvis_begin(const char *name)
{
   uint64_t ctx = util_gpuvis_thread_push_ctx();
   if (ctx == 0)
      return;

   gpuvis_trace_begin_ctx_printf(ctx, "mesa:%s", name);
}

void
util_gpuvis_end(void)
{
   uint64_t ctx = util_gpuvis_thread_pop_ctx();
   if (ctx == 0)
      return;

   /* Use an empty string to avoid warnings about an empty format string. */
   gpuvis_trace_end_ctx_printf(ctx, "%s", "");
}
