#ifndef _DRM_HELPER_PUBLIC_H
#define _DRM_HELPER_PUBLIC_H


struct pipe_screen;

struct pipe_screen *
pipe_i915_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_ilo_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_nouveau_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_r300_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_r600_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_radeonsi_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_vmwgfx_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_freedreno_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_virgl_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_vc4_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_pl111_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_etna_create_screen(int fd, unsigned flags);

struct pipe_screen *
pipe_imx_drm_create_screen(int fd, unsigned flags);

#endif /* _DRM_HELPER_PUBLIC_H */
