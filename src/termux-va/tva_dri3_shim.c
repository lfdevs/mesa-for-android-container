/*
 * Copyright © 2026 lfdevs
 * SPDX-License-Identifier: MIT
 *
 * Termux:X11 supports importing pixmaps through DRI3, but has no DRM device
 * to return from DRI3 Open. Chromium requires that reply to initialize GBM.
 * With an explicit opt-in, replace only a failed Open reply with a real KGSL
 * descriptor; successful replies and all other X11 requests stay unchanged.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

static unsigned (*real_send)(xcb_connection_t *, int, struct iovec *, const xcb_protocol_request_t *);
static uint64_t (*real_send64)(xcb_connection_t *, int, struct iovec *, const xcb_protocol_request_t *);
static int (*real_poll)(xcb_connection_t *, unsigned, void **, xcb_generic_error_t **);
static int (*real_poll64)(xcb_connection_t *, uint64_t, void **, xcb_generic_error_t **);
static void *(*real_wait)(xcb_connection_t *, unsigned, xcb_generic_error_t **);
static void *(*real_wait64)(xcb_connection_t *, uint64_t, xcb_generic_error_t **);
static void (*real_disconnect)(xcb_connection_t *);
static pthread_once_t symbols_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;
static xcb_extension_t dri3_extension = {"DRI3", 0};

static struct {
   xcb_connection_t *connection;
   uint64_t sequence;
   uint8_t opcode;
} pending[64];

static void
load_symbols(void)
{
   real_send = dlsym(RTLD_NEXT, "xcb_send_request");
   real_send64 = dlsym(RTLD_NEXT, "xcb_send_request64");
   real_poll = dlsym(RTLD_NEXT, "xcb_poll_for_reply");
   real_poll64 = dlsym(RTLD_NEXT, "xcb_poll_for_reply64");
   real_wait = dlsym(RTLD_NEXT, "xcb_wait_for_reply");
   real_wait64 = dlsym(RTLD_NEXT, "xcb_wait_for_reply64");
   real_disconnect = dlsym(RTLD_NEXT, "xcb_disconnect");
}

static bool
enabled(void)
{
   const char *value = getenv("TERMUX_VA_X11_DRI3");
   return value && (!strcmp(value, "1") || !strcasecmp(value, "true") ||
                    !strcasecmp(value, "on"));
}

static uint8_t
open_opcode(xcb_connection_t *connection, int flags, struct iovec *vector,
            const xcb_protocol_request_t *request)
{
   if (!enabled() || !(flags & XCB_REQUEST_CHECKED) ||
       !(flags & XCB_REQUEST_REPLY_FDS) || !request || request->isvoid ||
       !request->count || !vector || !vector[0].iov_base || vector[0].iov_len < 4)
      return 0;

   const uint8_t *header = vector[0].iov_base;
   if (flags & XCB_REQUEST_RAW) {
      uint16_t length;
      memcpy(&length, header + 2, sizeof(length));
      if (header[0] < 128 || header[1] != 1 || length != 3)
         return 0;
   } else if (!request->ext || strcmp(request->ext->name, "DRI3") ||
              request->opcode != 1) {
      return 0;
   }

   /* Query before sending the application request. XCB caches this per
    * connection; its internal QueryExtension request has no reply fds and
    * therefore cannot recurse through this candidate filter. */
   const xcb_query_extension_reply_t *extension =
      xcb_get_extension_data(connection, &dri3_extension);
   if (!extension || !extension->present ||
       ((flags & XCB_REQUEST_RAW) && header[0] != extension->major_opcode))
      return 0;
   return extension->major_opcode;
}

static void
track_open(xcb_connection_t *connection, uint64_t sequence, uint8_t opcode)
{
   if (!sequence || !opcode)
      return;
   pthread_mutex_lock(&pending_mutex);
   for (unsigned i = 0; i < sizeof(pending) / sizeof(pending[0]); i++) {
      if (!pending[i].connection) {
         pending[i].connection = connection;
         pending[i].sequence = sequence;
         pending[i].opcode = opcode;
         break;
      }
   }
   pthread_mutex_unlock(&pending_mutex);
}

static bool
is_pending_open(xcb_connection_t *connection, uint64_t sequence)
{
   if (!enabled())
      return false;
   bool found = false;
   pthread_mutex_lock(&pending_mutex);
   for (unsigned i = 0; i < sizeof(pending) / sizeof(pending[0]); i++) {
      if (pending[i].connection == connection && pending[i].sequence == sequence) {
         found = true;
         break;
      }
   }
   pthread_mutex_unlock(&pending_mutex);
   return found;
}

static void
complete_open(xcb_connection_t *connection, uint64_t sequence, void **reply,
              xcb_generic_error_t **error)
{
   if (!enabled())
      return;
   uint8_t opcode = 0;
   pthread_mutex_lock(&pending_mutex);
   for (unsigned i = 0; i < sizeof(pending) / sizeof(pending[0]); i++) {
      if (pending[i].connection == connection && pending[i].sequence == sequence) {
         opcode = pending[i].opcode;
         pending[i].connection = NULL;
         break;
      }
   }
   pthread_mutex_unlock(&pending_mutex);
   if (!opcode || !reply || *reply || !error || !*error ||
       (*error)->error_code != XCB_MATCH || (*error)->major_code != opcode ||
       (*error)->minor_code != 1)
      return;

   int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
   if (fd < 0)
      return;
   /* XCB stores received descriptors immediately after the wire reply.
    * xcb_get_reply_fds() and Chromium's ReadBuffer both use this layout. */
   uint8_t *replacement = calloc(1, 32 + sizeof(fd));
   if (!replacement) {
      close(fd);
      return;
   }
   replacement[0] = 1;
   replacement[1] = 1; /* nfd */
   uint16_t sequence16 = sequence;
   memcpy(replacement + 2, &sequence16, sizeof(sequence16));
   memcpy(replacement + 32, &fd, sizeof(fd));
   setenv("FD_KGSL_ENABLE_DMABUF", "1", 0);
   free(*error);
   *error = NULL;
   *reply = replacement;
   if (getenv("TERMUX_VA_DRM_SHIM_LOG"))
      fprintf(stderr, "tva-drm-shim: DRI3 Open BadMatch fallback to KGSL fd=%d\n", fd);
}

#define SEND_WRAPPER(name, type, real_function) \
type name(xcb_connection_t *connection, int flags, struct iovec *vector, \
          const xcb_protocol_request_t *request) \
{ \
   pthread_once(&symbols_once, load_symbols); \
   uint8_t opcode = open_opcode(connection, flags, vector, request); \
   type sequence = real_function(connection, flags, vector, request); \
   track_open(connection, sequence, opcode); \
   return sequence; \
}
SEND_WRAPPER(xcb_send_request, unsigned, real_send)
SEND_WRAPPER(xcb_send_request64, uint64_t, real_send64)

/* Some callers (including KWin) discard checked errors by passing NULL.
 * Capture those errors only for tracked Open requests so the fallback still
 * runs, then release any error the caller did not request. */
#define POLL_WRAPPER(name, type, real_function) \
int name(xcb_connection_t *connection, type sequence, void **reply, \
         xcb_generic_error_t **error) \
{ \
   pthread_once(&symbols_once, load_symbols); \
   xcb_generic_error_t *ignored_error = NULL; \
   xcb_generic_error_t **reply_error = error; \
   if (!error && is_pending_open(connection, sequence)) \
      reply_error = &ignored_error; \
   int ready = real_function(connection, sequence, reply, reply_error); \
   if (ready) \
      complete_open(connection, sequence, reply, reply_error); \
   free(ignored_error); \
   return ready; \
}
POLL_WRAPPER(xcb_poll_for_reply, unsigned, real_poll)
POLL_WRAPPER(xcb_poll_for_reply64, uint64_t, real_poll64)

#define WAIT_WRAPPER(name, type, real_function) \
void *name(xcb_connection_t *connection, type sequence, xcb_generic_error_t **error) \
{ \
   pthread_once(&symbols_once, load_symbols); \
   xcb_generic_error_t *ignored_error = NULL; \
   xcb_generic_error_t **reply_error = error; \
   if (!error && is_pending_open(connection, sequence)) \
      reply_error = &ignored_error; \
   void *reply = real_function(connection, sequence, reply_error); \
   complete_open(connection, sequence, &reply, reply_error); \
   free(ignored_error); \
   return reply; \
}
WAIT_WRAPPER(xcb_wait_for_reply, unsigned, real_wait)
WAIT_WRAPPER(xcb_wait_for_reply64, uint64_t, real_wait64)

void
xcb_disconnect(xcb_connection_t *connection)
{
   pthread_once(&symbols_once, load_symbols);
   pthread_mutex_lock(&pending_mutex);
   for (unsigned i = 0; i < sizeof(pending) / sizeof(pending[0]); i++)
      if (pending[i].connection == connection)
         pending[i].connection = NULL;
   pthread_mutex_unlock(&pending_mutex);
   real_disconnect(connection);
}
