#include "util/detect_os.h"

#include "target-helpers/drm_helper.h"
#include "target-helpers/sw_helper.h"

#include "dri_screen.h"

#define DEFINE_LOADER_DRM_ENTRYPOINT(drivername)                          \
const __DRIextension **__driDriverGetExtensions_##drivername(void);       \
PUBLIC const __DRIextension **__driDriverGetExtensions_##drivername(void) \
{                                                                         \
   return galliumdrm_driver_extensions;                                   \
}

#if defined(HAVE_SWRAST)

const __DRIextension **__driDriverGetExtensions_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_swrast(void)
{
   return galliumsw_driver_extensions;
}

#if defined(HAVE_LIBDRM)

const __DRIextension **__driDriverGetExtensions_kms_swrast(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_kms_swrast(void)
{
   return dri_swrast_kms_driver_extensions;
}

#endif
#endif

#if defined(GALLIUM_I915)
DEFINE_LOADER_DRM_ENTRYPOINT(i915)
#endif

#if defined(GALLIUM_IRIS)
DEFINE_LOADER_DRM_ENTRYPOINT(iris)
#endif

#if defined(GALLIUM_CROCUS)
DEFINE_LOADER_DRM_ENTRYPOINT(crocus)
#endif

#if defined(GALLIUM_NOUVEAU)
DEFINE_LOADER_DRM_ENTRYPOINT(nouveau)
#endif

#if defined(GALLIUM_R300)
DEFINE_LOADER_DRM_ENTRYPOINT(r300)
#endif

#if defined(GALLIUM_R600)
DEFINE_LOADER_DRM_ENTRYPOINT(r600)
#endif

#if defined(GALLIUM_RADEONSI)
DEFINE_LOADER_DRM_ENTRYPOINT(radeonsi)
#endif

#if defined(GALLIUM_VMWGFX)
DEFINE_LOADER_DRM_ENTRYPOINT(vmwgfx)
#endif

#if defined(GALLIUM_FREEDRENO)
DEFINE_LOADER_DRM_ENTRYPOINT(msm)
DEFINE_LOADER_DRM_ENTRYPOINT(kgsl)
#endif

#if defined(GALLIUM_VIRGL) || (defined(GALLIUM_FREEDRENO) && !defined(PIPE_LOADER_DYNAMIC))
DEFINE_LOADER_DRM_ENTRYPOINT(virtio_gpu)
#endif

#if defined(GALLIUM_V3D)
DEFINE_LOADER_DRM_ENTRYPOINT(v3d)
#endif

#if defined(GALLIUM_VC4)
DEFINE_LOADER_DRM_ENTRYPOINT(vc4)
#endif

#if defined(GALLIUM_PANFROST)
DEFINE_LOADER_DRM_ENTRYPOINT(panfrost)
DEFINE_LOADER_DRM_ENTRYPOINT(panthor)
#endif

#if defined(GALLIUM_ASAHI)
DEFINE_LOADER_DRM_ENTRYPOINT(asahi)
#endif

#if defined(GALLIUM_ETNAVIV)
DEFINE_LOADER_DRM_ENTRYPOINT(etnaviv)
#endif

#if defined(GALLIUM_TEGRA)
DEFINE_LOADER_DRM_ENTRYPOINT(tegra);
#endif

#if defined(GALLIUM_KMSRO)
DEFINE_LOADER_DRM_ENTRYPOINT(armada_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(exynos)
DEFINE_LOADER_DRM_ENTRYPOINT(gm12u320)
DEFINE_LOADER_DRM_ENTRYPOINT(hdlcd)
DEFINE_LOADER_DRM_ENTRYPOINT(hx8357d)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9163)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9225)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9341)
DEFINE_LOADER_DRM_ENTRYPOINT(ili9486)
DEFINE_LOADER_DRM_ENTRYPOINT(imx_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(imx_dcss)
DEFINE_LOADER_DRM_ENTRYPOINT(imx_lcdif)
DEFINE_LOADER_DRM_ENTRYPOINT(ingenic_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(kirin)
DEFINE_LOADER_DRM_ENTRYPOINT(komeda)
DEFINE_LOADER_DRM_ENTRYPOINT(mali_dp)
DEFINE_LOADER_DRM_ENTRYPOINT(mcde)
DEFINE_LOADER_DRM_ENTRYPOINT(mediatek)
DEFINE_LOADER_DRM_ENTRYPOINT(meson)
DEFINE_LOADER_DRM_ENTRYPOINT(mi0283qt)
DEFINE_LOADER_DRM_ENTRYPOINT(mxsfb_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(panel_mipi_dbi)
DEFINE_LOADER_DRM_ENTRYPOINT(pl111)
DEFINE_LOADER_DRM_ENTRYPOINT(rcar_du)
DEFINE_LOADER_DRM_ENTRYPOINT(repaper)
DEFINE_LOADER_DRM_ENTRYPOINT(rockchip)
DEFINE_LOADER_DRM_ENTRYPOINT(rzg2l_du)
DEFINE_LOADER_DRM_ENTRYPOINT(ssd130x)
DEFINE_LOADER_DRM_ENTRYPOINT(st7586)
DEFINE_LOADER_DRM_ENTRYPOINT(st7735r)
DEFINE_LOADER_DRM_ENTRYPOINT(sti)
DEFINE_LOADER_DRM_ENTRYPOINT(stm)
DEFINE_LOADER_DRM_ENTRYPOINT(sun4i_drm)
DEFINE_LOADER_DRM_ENTRYPOINT(udl)
DEFINE_LOADER_DRM_ENTRYPOINT(vkms)
DEFINE_LOADER_DRM_ENTRYPOINT(zynqmp_dpsub)
#endif

#if defined(GALLIUM_LIMA)
DEFINE_LOADER_DRM_ENTRYPOINT(lima)
#endif

#if defined(GALLIUM_ZINK)
#if DETECT_OS_ANDROID
DEFINE_LOADER_DRM_ENTRYPOINT(zink);
#else
const __DRIextension **__driDriverGetExtensions_zink(void);

PUBLIC const __DRIextension **__driDriverGetExtensions_zink(void)
{
   return debug_get_bool_option("LIBGL_KOPPER_DISABLE", false) ? galliumdrm_driver_extensions : galliumvk_driver_extensions;
}
#endif
#endif

#if defined(GALLIUM_D3D12)
DEFINE_LOADER_DRM_ENTRYPOINT(d3d12);
#endif

const __DRIextension **
dri_loader_get_extensions(const char *driver_name);

PUBLIC const __DRIextension **
dri_loader_get_extensions(const char *driver_name)
{
#if defined(HAVE_I915)
   if (!strcmp(driver_name, "i915"))
      return __driDriverGetExtensions_i915();
#endif

#if defined(HAVE_IRIS)
   if (!strcmp(driver_name, "iris"))
      return __driDriverGetExtensions_iris();
#endif

#if defined(HAVE_CROCUS)
   if (!strcmp(driver_name, "crocus"))
      return __driDriverGetExtensions_crocus();
#endif

#if defined(HAVE_NOUVEAU)
   if (!strcmp(driver_name, "nouveau"))
      return __driDriverGetExtensions_nouveau();
#endif

#if defined(HAVE_R300)
   if (!strcmp(driver_name, "r300"))
      return __driDriverGetExtensions_r300();
#endif

#if defined(HAVE_R600)
   if (!strcmp(driver_name, "r600"))
      return __driDriverGetExtensions_r600();
#endif

#if defined(HAVE_RADEONSI)
   if (!strcmp(driver_name, "radeonsi"))
      return __driDriverGetExtensions_radeonsi();
#endif

#if defined(HAVE_SVGA)
   if (!strcmp(driver_name, "vmwgfx"))
      return __driDriverGetExtensions_vmwgfx();
#endif

#if defined(HAVE_FREEDRENO)
   if (!strcmp(driver_name, "msm"))
      return __driDriverGetExtensions_msm();
   if (!strcmp(driver_name, "kgsl"))
      return __driDriverGetExtensions_kgsl();
#endif

#if defined(HAVE_VIRGL) || (defined(HAVE_FREEDRENO) && !defined(PIPE_LOADER_DYNAMIC))
   if (!strcmp(driver_name, "virtio_gpu"))
      return __driDriverGetExtensions_virtio_gpu();
#endif

#if defined(HAVE_V3D)
   if (!strcmp(driver_name, "v3d"))
      return __driDriverGetExtensions_v3d();
#endif

#if defined(HAVE_VC4)
   if (!strcmp(driver_name, "vc4"))
      return __driDriverGetExtensions_vc4();
#endif

#if defined(HAVE_PANFROST)
   if (!strcmp(driver_name, "panfrost"))
      return __driDriverGetExtensions_panfrost();
   if (!strcmp(driver_name, "panthor"))
      return __driDriverGetExtensions_panthor();
#endif

#if defined(HAVE_ASAHI)
   if (!strcmp(driver_name, "asahi"))
      return __driDriverGetExtensions_asahi();
#endif

#if defined(HAVE_ETNAVIV)
   if (!strcmp(driver_name, "etnaviv"))
      return __driDriverGetExtensions_etnaviv();
#endif

#if defined(HAVE_TEGRA)
   if (!strcmp(driver_name, "tegra"))
      return __driDriverGetExtensions_tegra();
#endif

#if defined(GALLIUM_KMSRO)
   if (!strcmp(driver_name, "armada-drm"))
      return __driDriverGetExtensions_armada_drm();
   if (!strcmp(driver_name, "exynos"))
      return __driDriverGetExtensions_exynos();
   if (!strcmp(driver_name, "gm12u320"))
      return __driDriverGetExtensions_gm12u320();
   if (!strcmp(driver_name, "hdlcd"))
      return __driDriverGetExtensions_hdlcd();
   if (!strcmp(driver_name, "hx8357d"))
      return __driDriverGetExtensions_hx8357d();
   if (!strcmp(driver_name, "ili9163"))
      return __driDriverGetExtensions_ili9163();
   if (!strcmp(driver_name, "ili9225"))
      return __driDriverGetExtensions_ili9225();
   if (!strcmp(driver_name, "ili9341"))
      return __driDriverGetExtensions_ili9341();
   if (!strcmp(driver_name, "ili9486"))
      return __driDriverGetExtensions_ili9486();
   if (!strcmp(driver_name, "imx-drm"))
      return __driDriverGetExtensions_imx_drm();
   if (!strcmp(driver_name, "imx-dcss"))
      return __driDriverGetExtensions_imx_dcss();
   if (!strcmp(driver_name, "imx-lcdif"))
      return __driDriverGetExtensions_imx_lcdif();
   if (!strcmp(driver_name, "ingenic-drm"))
      return __driDriverGetExtensions_ingenic_drm();
   if (!strcmp(driver_name, "kirin"))
      return __driDriverGetExtensions_kirin();
   if (!strcmp(driver_name, "komeda"))
      return __driDriverGetExtensions_komeda();
   if (!strcmp(driver_name, "mali-dp"))
      return __driDriverGetExtensions_mali_dp();
   if (!strcmp(driver_name, "mcde"))
      return __driDriverGetExtensions_mcde();
   if (!strcmp(driver_name, "mediatek"))
      return __driDriverGetExtensions_mediatek();
   if (!strcmp(driver_name, "meson"))
      return __driDriverGetExtensions_meson();
   if (!strcmp(driver_name, "mi0283qt"))
      return __driDriverGetExtensions_mi0283qt();
   if (!strcmp(driver_name, "mxsfb-drm"))
      return __driDriverGetExtensions_mxsfb_drm();
   if (!strcmp(driver_name, "panel-mipi-dbi"))
      return __driDriverGetExtensions_panel_mipi_dbi();
   if (!strcmp(driver_name, "pl111"))
      return __driDriverGetExtensions_pl111();
   if (!strcmp(driver_name, "rcar-du"))
      return __driDriverGetExtensions_rcar_du();
   if (!strcmp(driver_name, "repaper"))
      return __driDriverGetExtensions_repaper();
   if (!strcmp(driver_name, "rockchip"))
      return __driDriverGetExtensions_rockchip();
   if (!strcmp(driver_name, "rzg2l-du"))
      return __driDriverGetExtensions_rzg2l_du();
   if (!strcmp(driver_name, "ssd130x"))
      return __driDriverGetExtensions_ssd130x();
   if (!strcmp(driver_name, "st7586"))
      return __driDriverGetExtensions_st7586();
   if (!strcmp(driver_name, "st7735r"))
      return __driDriverGetExtensions_st7735r();
   if (!strcmp(driver_name, "sti"))
      return __driDriverGetExtensions_sti();
   if (!strcmp(driver_name, "stm"))
      return __driDriverGetExtensions_stm();
   if (!strcmp(driver_name, "sun4i-drm"))
      return __driDriverGetExtensions_sun4i_drm();
   if (!strcmp(driver_name, "udl"))
      return __driDriverGetExtensions_udl();
   if (!strcmp(driver_name, "vkms"))
      return __driDriverGetExtensions_vkms();
   if (!strcmp(driver_name, "zynqmp-dpsub"))
      return __driDriverGetExtensions_zynqmp_dpsub();
#endif

#if defined(HAVE_LIMA)
   if (!strcmp(driver_name, "lima"))
      return __driDriverGetExtensions_lima();
#endif

#if defined(HAVE_ZINK)
   if (!strcmp(driver_name, "zink"))
      return __driDriverGetExtensions_zink();
#endif

#if defined(HAVE_D3D12)
   if (!strcmp(driver_name, "d3d12"))
      return __driDriverGetExtensions_d3d12();
#endif

#if defined(HAVE_SWRAST)
   if (!strcmp(driver_name, "swrast"))
      return __driDriverGetExtensions_swrast();

#if defined(HAVE_LIBDRM)
   if (!strcmp(driver_name, "kms_swrast"))
      return __driDriverGetExtensions_kms_swrast();
#endif
#endif

   return NULL;
}
