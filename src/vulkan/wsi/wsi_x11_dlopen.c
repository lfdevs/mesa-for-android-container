/*
 * Copyright © 2026 Mesa contributors
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#include "c11/threads.h"
#include "util/log.h"
#include "util/macros.h"
#include "wsi_common_private.h"

static const char *const wsi_x11_libs[] = {
   "libxcb.so.1",
   "libX11-xcb.so.1",
   "libxcb-dri3.so.0",
   "libxcb-present.so.0",
   "libxcb-shm.so.0",
   "libxcb-sync.so.1",
   "libxcb-xfixes.so.0",
   "libxcb-randr.so.0",
   "libxshmfence.so.1",
};

#define WSI_X11_SYMS(S) \
   S(xcb_change_property_checked) \
   S(xcb_create_gc) \
   S(xcb_delete_property_checked) \
   S(xcb_depth_next) \
   S(xcb_depth_visuals_iterator) \
   S(xcb_discard_reply) \
   S(xcb_flush) \
   S(xcb_free_pixmap) \
   S(xcb_generate_id) \
   S(xcb_get_geometry) \
   S(xcb_get_geometry_reply) \
   S(xcb_get_geometry_unchecked) \
   S(xcb_get_maximum_request_length) \
   S(xcb_get_setup) \
   S(xcb_get_window_attributes) \
   S(xcb_get_window_attributes_reply) \
   S(xcb_intern_atom) \
   S(xcb_intern_atom_reply) \
   S(xcb_put_image) \
   S(xcb_query_extension) \
   S(xcb_query_extension_reply) \
   S(xcb_query_tree) \
   S(xcb_query_tree_reply) \
   S(xcb_register_for_special_xge) \
   S(xcb_request_check) \
   S(xcb_screen_allowed_depths_iterator) \
   S(xcb_screen_next) \
   S(xcb_setup_roots_iterator) \
   S(xcb_translate_coordinates_reply) \
   S(xcb_translate_coordinates_unchecked) \
   S(xcb_unregister_for_special_event) \
   S(xcb_visualtype_next) \
   S(xcb_wait_for_special_event) \
   S(XGetXCBConnection) \
   S(xcb_dri3_fence_from_fd) \
   S(xcb_dri3_free_syncobj) \
   S(xcb_dri3_get_supported_modifiers) \
   S(xcb_dri3_get_supported_modifiers_reply) \
   S(xcb_dri3_get_supported_modifiers_screen_modifiers) \
   S(xcb_dri3_get_supported_modifiers_window_modifiers) \
   S(xcb_dri3_import_syncobj_checked) \
   S(xcb_dri3_open) \
   S(xcb_dri3_open_reply) \
   S(xcb_dri3_open_reply_fds) \
   S(xcb_dri3_pixmap_from_buffer_checked) \
   S(xcb_dri3_pixmap_from_buffers_checked) \
   S(xcb_dri3_query_version) \
   S(xcb_dri3_query_version_reply) \
   S(xcb_present_pixmap) \
   S(xcb_present_pixmap_synced) \
   S(xcb_present_query_capabilities) \
   S(xcb_present_query_capabilities_reply) \
   S(xcb_present_query_version) \
   S(xcb_present_query_version_reply) \
   S(xcb_present_select_input) \
   S(xcb_present_select_input_checked) \
   S(xcb_shm_attach) \
   S(xcb_shm_attach_checked) \
   S(xcb_shm_create_pixmap_checked) \
   S(xcb_shm_detach) \
   S(xcb_shm_detach_checked) \
   S(xcb_sync_destroy_fence) \
   S(xcb_xfixes_create_region) \
   S(xcb_xfixes_destroy_region) \
   S(xcb_xfixes_query_version) \
   S(xcb_xfixes_query_version_reply) \
   S(xcb_xfixes_set_region) \
   S(xcb_randr_get_crtc_info_reply) \
   S(xcb_randr_get_crtc_info_unchecked) \
   S(xcb_randr_get_output_info) \
   S(xcb_randr_get_output_info_name) \
   S(xcb_randr_get_output_info_reply) \
   S(xcb_randr_get_screen_resources_current_crtcs) \
   S(xcb_randr_get_screen_resources_current_modes) \
   S(xcb_randr_get_screen_resources_current_outputs) \
   S(xcb_randr_get_screen_resources_current_reply) \
   S(xcb_randr_get_screen_resources_current_unchecked) \
   S(xcb_randr_query_version_reply) \
   S(xcb_randr_query_version_unchecked) \
   S(xshmfence_alloc_shm) \
   S(xshmfence_await) \
   S(xshmfence_map_shm) \
   S(xshmfence_reset) \
   S(xshmfence_trigger) \
   S(xshmfence_unmap_shm)

xcb_extension_t xcb_present_id = { "Present", 0 };

static void
wsi_x11_dl_unloaded(void)
{
   mesa_loge("wsi/x11: X11 function called before the X11 libraries were loaded");
   abort();
}

#define S(sym) void *wsi_x11_dl_##sym = (void *)wsi_x11_dl_unloaded;
WSI_X11_SYMS(S)
#undef S

#define S(sym)                                                   \
   __asm__(".text\n"                                             \
           ".globl " #sym "\n"                                   \
           ".hidden " #sym "\n"                                  \
           ".type " #sym ", %function\n"                         \
           ".p2align 2\n"                                        \
           #sym ":\n"                                            \
           "  adrp x16, wsi_x11_dl_" #sym "\n"                   \
           "  ldr x16, [x16, :lo12:wsi_x11_dl_" #sym "]\n"       \
           "  br x16\n"                                          \
           ".size " #sym ", . - " #sym "\n");
WSI_X11_SYMS(S)
#undef S

static once_flag wsi_x11_dl_once = ONCE_FLAG_INIT;
static bool wsi_x11_dl_ok;

static void *
wsi_x11_dl_lookup(void *const *handles, const char *name)
{
   for (unsigned i = 0; i < ARRAY_SIZE(wsi_x11_libs); i++) {
      void *p = dlsym(handles[i], name);
      if (p)
         return p;
   }
   mesa_loge("wsi/x11: missing symbol %s", name);
   return NULL;
}

static void
wsi_x11_dl_load(void)
{
   void *handles[ARRAY_SIZE(wsi_x11_libs)];

   for (unsigned i = 0; i < ARRAY_SIZE(wsi_x11_libs); i++) {
      handles[i] = dlopen(wsi_x11_libs[i], RTLD_NOW | RTLD_LOCAL);
      if (!handles[i]) {
         mesa_loge("wsi/x11: dlopen(%s) failed: %s", wsi_x11_libs[i], dlerror());
         return;
      }
   }

#define S(sym)                                                   \
   void *p_##sym = wsi_x11_dl_lookup(handles, #sym);             \
   if (!p_##sym)                                                 \
      return;
   WSI_X11_SYMS(S)
#undef S

#define S(sym) wsi_x11_dl_##sym = p_##sym;
   WSI_X11_SYMS(S)
#undef S

   wsi_x11_dl_ok = true;
}

bool
wsi_x11_dlopen(void)
{
   call_once(&wsi_x11_dl_once, wsi_x11_dl_load);
   return wsi_x11_dl_ok;
}
