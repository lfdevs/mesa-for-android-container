### v3d_noop backend

This implements the minimum of v3d in order to make shader-db and fossilize work.
The submit ioctl is stubbed out to not execute anything.

Export `MESA_LOADER_DRIVER_OVERRIDE=v3d
LD_PRELOAD=$prefix/lib/libv3d_noop_drm_shim.so`.

By default the emulated GPU will be V3D 7.1 (rpi5). But it is possible
to select a specific V3D GPU model by setting V3D_GPU_ID environment
variable. The accepted values are:

- V3D_GPU_ID=71 (default): this emulates V3D 7.1 (rpi5) device.
- V3D_GPU_ID=42: this emulates V3D 4.2 (rpi4) device.

### vc4_noop backend

This implements the minimum of vc4 in order to make shader-db work.
The submit ioctl is stubbed out to not execute anything.

Export `MESA_LOADER_DRIVER_OVERRIDE=vc4
LD_PRELOAD=$prefix/lib/libvc4_noop_drm_shim.so`.  This will be a VC4
2.1 device.

