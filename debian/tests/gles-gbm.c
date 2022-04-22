// based on
// https://www.khronos.org/registry/EGL/extensions/MESA/EGL_MESA_platform_gbm.txt

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

struct my_display {
   struct gbm_device *gbm;
   EGLDisplay egl;
};

struct my_config {
   struct my_display dpy;
   EGLConfig egl;
};

struct my_window {
   struct my_config config;
   struct gbm_surface *gbm;
   EGLSurface egl;
};

PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplayEXT;
PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC createPlatformWindowSurfaceEXT;

static void
check_extensions(void)
{
   const char *client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

   if (!client_extensions) {
      abort();
   }
   if (!strstr(client_extensions, "EGL_MESA_platform_gbm")) {
      abort();
   }

   if (!strstr(client_extensions, "EGL_EXT_platform_base")) {
      abort();
   }

   getPlatformDisplayEXT =
      (void *) eglGetProcAddress("eglGetPlatformDisplayEXT");
   createPlatformWindowSurfaceEXT =
      (void *) eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");

}

static struct my_display
get_display(void)
{
   struct my_display dpy;
   EGLint major, minor;

   int fd = open("/dev/dri/card0", O_RDWR | FD_CLOEXEC);
   if (fd < 0)
      abort();

   dpy.gbm = gbm_create_device(fd);
   if (!dpy.gbm)
      abort();

   dpy.egl = getPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA, dpy.gbm, NULL);

   if (dpy.egl == EGL_NO_DISPLAY)
      abort();

   if (eglInitialize(dpy.egl, &major, &minor))
      printf ("EGL %d.%d\n", major, minor);
   else
      abort();

   return dpy;
}

static struct my_config
get_config(struct my_display dpy)
{
   struct my_config config = {
      .dpy = dpy,
   };

   EGLint egl_config_attribs[] = {
      EGL_BUFFER_SIZE,        32,
      EGL_DEPTH_SIZE,         EGL_DONT_CARE,
      EGL_STENCIL_SIZE,       EGL_DONT_CARE,
      EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
      EGL_SURFACE_TYPE,       EGL_WINDOW_BIT,
      EGL_NONE,
   };

   EGLint num_configs;
   if (!eglGetConfigs(dpy.egl, NULL, 0, &num_configs))
      abort();

   EGLConfig *configs = malloc(num_configs * sizeof(EGLConfig));
   if (!eglChooseConfig(dpy.egl, egl_config_attribs,
                        configs, num_configs, &num_configs)) {
      abort();
   }
   if (num_configs == 0)
      abort();

   for (int i = 0; i < num_configs; ++i) {
      EGLint gbm_format;
      struct gbm_format_name_desc desc;

      if (!eglGetConfigAttrib(dpy.egl, configs[i],
                              EGL_NATIVE_VISUAL_ID, &gbm_format)) {
         abort();
      }

      printf ("found gbm_format: %s\n", gbm_format_get_name (gbm_format, &desc));
      if (gbm_format == GBM_FORMAT_ARGB8888) {
         config.egl = configs[i];
         free(configs);
         return config;
      }
   }

   // no egl config matching gbm format
   abort();
}

static struct my_window
get_window(struct my_config config)
{
   struct my_window window = {
      .config = config,
   };

   window.gbm = gbm_surface_create(config.dpy.gbm,
                                   256, 256,
                                   GBM_FORMAT_XRGB8888,
                                   GBM_BO_USE_RENDERING);
   if (!window.gbm)
      abort();

   window.egl = createPlatformWindowSurfaceEXT(config.dpy.egl,
                                               config.egl,
                                               window.gbm,
                                               NULL);
   if (window.egl == EGL_NO_SURFACE)
      abort();

   return window;
}

int
main(void)
{
   check_extensions();

   struct my_display dpy = get_display();
   struct my_config config = get_config(dpy);
   struct my_window window = get_window(config);
   EGLContext context;

   context = eglCreateContext(dpy.egl, config.egl, EGL_NO_CONTEXT, NULL);
   eglMakeCurrent(dpy.egl, window.egl, window.egl, context);

   /* just so we have some gles symbols too */
   glClearColor(1.0, 1.0, 1.0, 1.0);
   glClear(GL_COLOR_BUFFER_BIT);
   glFlush();

   return EXIT_SUCCESS;
}
