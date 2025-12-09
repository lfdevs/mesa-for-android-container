/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef SCRIPT_H_
#define SCRIPT_H_

#include <stdint.h>

// XXX make script support optional
#define ENABLE_SCRIPTING 1

#ifdef ENABLE_SCRIPTING

struct rnn;
struct rnndomain;

/* called at start to load the script: */
int script_load(const char *file);
/* called at start to load internal pkt handlers: */
void internal_lua_pkt_handler_load(void);
void internal_lua_pkt_handler_init_rnn(struct rnn *rnn);

/* called at start of each cmdstream file: */
void script_start_cmdstream(const char *name);

/* called at each DRAW_INDX, calls script drawidx fxn to process
 * the current state
 */
__attribute__((weak))
void script_draw(const char *primtype, uint32_t nindx);

__attribute__((weak))
void script_packet(uint32_t *dwords, uint32_t sizedwords,
                   struct rnn *rnn,
                   struct rnndomain *dom);

__attribute__((weak))
const char * internal_packet(uint32_t *dwords, uint32_t sizedwords,
                             struct rnn *rnn,
                             struct rnndomain *dom);

/* maybe at some point it is interesting to add additional script
 * hooks for CP_EVENT_WRITE, etc?
 */

/* called at end of each cmdstream file: */
void script_end_cmdstream(void);

void script_start_submit(void);
void script_end_submit(void);

/* called after last cmdstream file: */
void script_finish(void);
void internal_lua_pkt_handler_finish(void);

#else
// TODO no-op stubs..
#endif

#endif /* SCRIPT_H_ */
