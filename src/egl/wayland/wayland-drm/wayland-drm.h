#ifndef WAYLAND_DRM_H
#define WAYLAND_DRM_H

#include <wayland-server.h>

struct wl_display;
struct wl_drm;
struct wl_resource;

struct wl_drm_buffer {
	struct wl_resource *resource;
	struct wl_drm *drm;
	int32_t width, height;
	uint32_t format;
        const void *driver_format;
        int32_t offset[3];
        int32_t stride[3];
	void *driver_buffer;
};

struct wayland_drm_callbacks {
	int (*authenticate)(void *user_data, uint32_t id);

        void (*reference_buffer)(void *user_data, uint32_t name, int fd,
                                 struct wl_drm_buffer *buffer);

	void (*release_buffer)(void *user_data, struct wl_drm_buffer *buffer);
};

enum { WAYLAND_DRM_PRIME = 0x01 };

struct wl_drm_buffer *
wayland_drm_buffer_get(struct wl_drm *drm, struct wl_resource *resource);

struct wl_drm *
wayland_drm_init(struct wl_display *display, char *device_name,
		 const struct wayland_drm_callbacks *callbacks, void *user_data,
                 uint32_t flags);

void
wayland_drm_uninit(struct wl_drm *drm);

#endif
