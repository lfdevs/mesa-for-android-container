termux-va bridge
================

The termux-va bridge forwards VA-API video decoding from this Mesa build
(the container side) to the `termux-va` daemon running in Termux on the
Android host, which decodes with the Android MediaCodec API in hardware
and returns NV12 frames.  Applications inside a Linux container that
shares Termux's tmp directory (``proot-distro ... --shared-tmp``) get
hardware decoding through the standard VA-API without any modification.

The porting model follows anland-termux: a Termux daemon, a Unix socket
placed in the shared tmp directory, and a bridge on the container side.
The daemon lives in the `termux-va` repository; the wire protocol is
byte-compatible with droidspaces-media-decode protocol v3.

Supported codecs: H.264 (Constrained Baseline / Main / High) and VP9 Profile 0, outputting NV12 progressive frames. HEVC parsing is present in the frontend, but VPS/SPS/PPS synthesis is not complete, so HEVC is not advertised yet. Profiles are advertised to libva through the underlying screen; encode and other codecs are not provided.

Building
--------

Build with ``-Dgallium-va=enabled -Dtermux-va-bridge=enabled`` and at
least one of ``h264dec``, ``h265dec``, ``vp9dec`` in ``video-codecs``
(for example ``-Dvideo-codecs=all``).  The megadriver is additionally
exposed as ``termuxva_drv_video.so`` so libva can select it with
``LIBVA_DRIVER_NAME=termuxva``.  Native Wayland Chromium support also
requires ``-Dtermux-va-wayland-shim=enabled``; this installs the
process-local DRM compatibility shim described below.

Activation
----------

The bridge is runtime-gated; a Mesa build with the bridge behaves exactly
like an unmodified one until activation:

- ``TERMUX_VA_BRIDGE=1`` forces the bridge on, ``0`` forces it off.
- Unset: the bridge activates when ``TERMUX_VA_SOCKET`` /
  ``TERMUX_VA_SOCKET_DIR`` is set, or when the default endpoint exists as
  a socket.

When the bridge is active but the daemon is unreachable, driver init
fails cleanly and applications fall back to software decoding.

Socket location
---------------

Default endpoint (container view): ``/tmp/termux-va/termux-va.sock`` -
the same directory as the Termux daemon's ``$TMPDIR/termux-va/`` through
the shared tmp mount.  Both ends understand ``TERMUX_VA_SOCKET`` (full
socket path) and ``TERMUX_VA_SOCKET_DIR`` (directory), so one setting
covers the daemon and the bridge; Android system properties are accepted
as a fallback through Mesa's ``os_get_option``.

Underlying screen
-----------------

The decode surfaces live on a screen created by the bridge before the
frontend asks for one.  ``TERMUX_VA_GPU_BACKEND`` selects how:

``auto`` (default) tries the stock loader first and falls back to llvmpipe. It does not try the KGSL alias automatically because environments that expose a display DRM node may not have a usable Vulkan or stock DRM path. ``kgsl`` explicitly selects the fork's KGSL Freedreno alias: GPU submission goes to ``/dev/kgsl-3d0`` while the handed fd stays the control/identity fd. The same selection is propagated to Mesa's EGL and GLX loaders, so callers do not need to add ``MESA_LOADER_DRIVER_OVERRIDE=kgsl`` or ``FD_FORCE_KGSL=1``. ``sw`` forces llvmpipe for setups without GPU access; the VA decode paths used by vainfo and ffmpeg work without a GPU. ``drm`` selects the stock loader only.

PRoot containers
----------------

A PRoot container can expose ``/dev/kgsl-3d0`` while exposing no usable DRM
render node.  With ``TERMUX_VA_GPU_BACKEND=kgsl`` the bridge opens KGSL itself
when the display backend supplies no fd.  It also uses one linear dma-buf for
both NV12 planes when no DRM render node is present, because Chromium's native
pixmap importer currently accepts only one dma-buf for this format.  Override
this choice with ``DMD_VA_CONTIGUOUS_DMABUF`` or
``TERMUX_VA_CONTIGUOUS_DMABUF`` when needed.

Chromium's native Wayland Ozone backend normally requires a DRM render node
for its GPU process, independently of VA-API.  For a DRM-less PRoot desktop,
enable ``-Dtermux-va-wayland-shim=enabled`` and load the installed
``libtva_drm_shim_wayland.so`` into Chromium with ``LD_PRELOAD``.  The shim
maps Chromium's DRM discovery calls to ``/dev/kgsl-3d0``; it does not replace
the Mesa KGSL backend or create a DRM device for other applications.  Launch
Chromium with the native Wayland platform, ``--render-node-override=/dev/kgsl-3d0``
and ``--hardware-video-device-path=/dev/kgsl-3d0``:

.. code-block:: sh

   export WAYLAND_DISPLAY=wayland-0
   export XDG_RUNTIME_DIR=/run/user/$(id -u)
   export LIBVA_DRIVER_NAME=termuxva
   export TERMUX_VA_BRIDGE=1
   export TERMUX_VA_GPU_BACKEND=kgsl
   export LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtva_drm_shim_wayland.so
   google-chrome --ozone-platform=wayland --no-sandbox --use-gl=angle \
     --use-angle=gles --render-node-override=/dev/kgsl-3d0 \
     --hardware-video-device-path=/dev/kgsl-3d0 \
     --enable-features=VaapiIgnoreDriverChecks,AcceleratedVideoDecoder,AcceleratedVideoDecodeLinuxGL,AcceleratedVideoDecodeLinuxZeroCopyGL

The standard libva DRM backend also rejects a KGSL fd; use this Mesa build's
bridge driver or an equivalent KGSL-aware compatibility layer.

Data path
---------

- vaRenderPicture: the frontend parses the VA buffers and hands the
  bridge slice data that already carries H.264/HEVC start codes
  (parameter sets arrive as slice data buffers).
- vaEndPicture: the bridge sends the complete access unit as one daemon
  input unit and associates one pending fence with the picture.  The normal
  pending depth defaults to 6 and may grow while the decoder reorders output;
  SHM mode clamps it to 8 to stay within the daemon's slot pool.
- vaSyncSurface: the bridge waits for the frame tagged with the
  picture's unit index, stages it, and copies the visible (cropped)
  region into the surface's plane resources, honoring the decoder's
  stride/slice-height geometry (Venus aligns buffers to 128x32).

Frames come back inline on the socket or zero-copy through a memfd slot
pool handed over via SCM_RIGHTS (disable with ``DMD_WANT_SHM=0``).
``vaDeriveImage`` is not available (plane resources are separate
textures); CPU consumers can use ``vaGetImage``.

Troubleshooting
---------------

- vainfo shows no profiles: the daemon is not running, or the consumer
  environment lacks ``LIBVA_DRIVER_NAME=termuxva`` / bridge activation.
- "endpoint inode mismatch": the socket path resolves to a stale socket
  (a single socket FILE was bind-mounted and the daemon restarted).
  Mount the socket DIRECTORY instead - the daemon replaces the socket
  file on every start, only the directory inode is stable.
- Black frames after a seek: a drain was triggered; should not happen in
  steady playback - reproduce with ``DMD_VA_LOG=1`` and the daemon's
  ``-v`` log.
