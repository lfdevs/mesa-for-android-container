#include "target-helpers/drm_helper.h"
#include "target-helpers/sw_helper.h"

#include "dri_screen.h"

#if defined(GALLIUM_SOFTPIPE)

const __DRIextension **__driDriverGetExtensions_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_swrast(void)
{
   globalDriverAPI = &galliumsw_driver_api;
   return galliumsw_driver_extensions;
}

#if defined(HAVE_LIBDRM)

const __DRIextension **__driDriverGetExtensions_kms_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_kms_swrast(void)
{
   globalDriverAPI = &dri_kms_driver_api;
   return galliumdrm_driver_extensions;
}

#endif
#endif

#if defined(GALLIUM_I915)

const __DRIextension **__driDriverGetExtensions_i915(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_i915(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_ILO)

const __DRIextension **__driDriverGetExtensions_i965(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_i965(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_NOUVEAU)

const __DRIextension **__driDriverGetExtensions_nouveau(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_nouveau(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_R300)

const __DRIextension **__driDriverGetExtensions_r300(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_r300(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_R600)

const __DRIextension **__driDriverGetExtensions_r600(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_r600(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_RADEONSI)

const __DRIextension **__driDriverGetExtensions_radeonsi(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_radeonsi(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_VMWGFX)

const __DRIextension **__driDriverGetExtensions_vmwgfx(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_vmwgfx(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_FREEDRENO)

const __DRIextension **__driDriverGetExtensions_msm(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_msm(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}

const __DRIextension **__driDriverGetExtensions_kgsl(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_kgsl(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_VIRGL)

const __DRIextension **__driDriverGetExtensions_virtio_gpu(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_virtio_gpu(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif

#if defined(GALLIUM_VC4)

const __DRIextension **__driDriverGetExtensions_vc4(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_vc4(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}

#if defined(USE_VC4_SIMULATOR)
const __DRIextension **__driDriverGetExtensions_i965(void);

/**
 * When building using the simulator (on x86), we advertise ourselves as the
 * i965 driver so that you can just make a directory with a link from
 * i965_dri.so to the built vc4_dri.so, and point LIBGL_DRIVERS_PATH to that
 * on your i965-using host to run the driver under simulation.
 *
 * This is, of course, incompatible with building with the ilo driver, but you
 * shouldn't be building that anyway.
 */
PUBLIC const __DRIextension **__driDriverGetExtensions_i965(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif
#endif

#if defined(GALLIUM_ETNAVIV)

const __DRIextension **__driDriverGetExtensions_imx_drm(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_imx_drm(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}

const __DRIextension **__driDriverGetExtensions_etnaviv(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_etnaviv(void)
{
   globalDriverAPI = &galliumdrm_driver_api;
   return galliumdrm_driver_extensions;
}
#endif
