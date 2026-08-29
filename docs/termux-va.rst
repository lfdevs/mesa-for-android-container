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

Supported codecs: H.264 (Constrained Baseline / Main / High), HEVC Main
and VP9 Profile 0, outputting NV12 progressive frames.  Profiles are
advertised to libva through the wrapped screen; encode and other codecs
are not provided.

Building
--------

Build with ``-Dgallium-va=enabled -Dtermux-va-bridge=enabled`` and at
least one of ``h264dec``, ``h265dec``, ``vp9dec`` in ``video-codecs``
(for example ``-Dvideo-codecs=all``).  The megadriver is additionally
exposed as ``termuxva_drv_video.so`` so libva can select it with
``LIBVA_DRIVER_NAME=termuxva``.

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

Data path
---------

- vaRenderPicture: the frontend parses the VA buffers and hands the
  bridge slice data that already carries H.264/HEVC start codes
  (parameter sets arrive as slice data buffers).
- vaEndPicture: the bridge splits the picture into Annex B units (one
  NALU per daemon length prefix), sends them, and returns; the pending
  pipeline depth is capped at 6 to stay within the daemon's 8-slot SHM
  pool.
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
