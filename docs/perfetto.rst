Perfetto Tracing
================

Mesa has experimental support for `Perfetto <https://perfetto.dev>`__ for
GPU performance monitoring.  Perfetto supports multiple
`producers <https://perfetto.dev/docs/concepts/service-model>`__ each with
one or more data-sources.  Perfetto already provides various producers and
data-sources for things like:

- CPU scheduling events (``linux.ftrace``)
- CPU frequency scaling (``linux.ftrace``)
- System calls (``linux.ftrace``)
- Process memory utilization (``linux.process_stats``)

As well as various domain specific producers.

The mesa Perfetto support adds additional producers, to allow for visualizing
GPU performance (frequency, utilization, performance counters, etc) on the
same timeline, to better understand and tune/debug system level performance:

- pps-producer: A systemwide daemon that can collect global performance
  counters.
- mesa: Per-process producer within mesa to capture render-stage traces
  on the GPU timeline, track events on the CPU timeline, etc.

The exact supported features vary per driver:

.. list-table:: Supported data-sources
   :header-rows: 1

   * - Driver
     - PPS Counters
     - Render Stages
   * - Freedreno
     - ``gpu.counters.msm``
     - ``gpu.renderstages.msm``
   * - Turnip
     - ``gpu.counters.msm``
     - ``gpu.renderstages.msm``
   * - Intel
     - ``gpu.counters.i915``
     - ``gpu.renderstages.intel``
   * - Panfrost
     - ``gpu.counters.panfrost``
     -
   * - PanVK
     - ``gpu.counters.panfrost``
     - ``gpu.renderstages.panfrost``
   * - V3D
     - ``gpu.counters.v3d``
     -
   * - V3DV
     - ``gpu.counters.v3d``
     -

Run
---

To capture a trace with Perfetto you need to take the following steps:

1. Build Mesa with perfetto enabled.

.. code-block:: sh

   # Configure Mesa with perfetto
   mesa $ meson . build -Dperfetto=true -Dvulkan-drivers=intel,broadcom -Dgallium-drivers=
   # Build mesa
   mesa $ meson compile -C build

2. Build Perfetto from sources available at ``subprojects/perfetto``.

.. code-block:: sh

   # Within the Mesa repo, build perfetto
   mesa $ cd subprojects/perfetto
   perfetto $ ./tools/install-build-deps
   perfetto $ ./tools/gn gen --args='is_debug=false' out/linux
   perfetto $ ./tools/ninja -C out/linux

   # Example arm64 cross compile instead
   perfetto $ ./tools/install-build-deps --linux-arm
   perfetto $ ./tools/gn gen --args='is_debug=false target_cpu="arm64"' out/linux-arm64

More build options can be found in `this guide <https://perfetto.dev/docs/quickstart/linux-tracing>`__.

3. Select a `trace config <https://perfetto.dev/docs/concepts/config>`__, likely
   ``src/tool/pps/cfg/system.cfg`` which does whole-system including GPU
   profiling for any supported GPUs).  Other configs are available in that
   directory for CPU-only or GPU-only tracing, and more examples of config files
   can be found in ``subprojects/perfetto/test/configs``.

4. Start the PPS producer to capture GPU performance counters.

.. code-block:: sh

   mesa $ sudo meson devenv -C build pps-producer

5. Start your application (and any other GPU-using system components) you want
   to trace using the perfetto-enabled Mesa build.

.. code-block:: sh

   mesa $ meson devenv -C build vkcube

6. Capture a perfetto trace using ``tracebox``.

.. code-block:: sh

   mesa $ sudo ./subprojects/perfetto/out/linux/tracebox --system-sockets --txt -c src/tool/pps/cfg/system.cfg -o vkcube.trace

7.  Go to `ui.perfetto.dev <https://ui.perfetto.dev>`__ and upload
    ``vkcube.trace`` by clicking on **Open trace file**.

8.  Alternatively you can open the trace in `AGI <https://gpuinspector.dev/>`__
    (which despite the name can be used to view non-android traces).

CPU Tracing
~~~~~~~~~~~

Mesa's CPU tracepoints (``MESA_TRACE_*``) use Perfetto track events when
Perfetto is enabled.  They use ``mesa.default`` and ``mesa.slow`` categories.

Currently, only EGL and the following drivers have CPU tracepoints.

- Freedreno
- Panfrost
- Turnip
- V3D
- VC4
- V3DV

Render stage data sources
~~~~~~~~~~~~~~~~~~~~~~~~~

The render stage data sources are the driver-specific traces of command buffer
execution on the GPU.

The Vulkan API gives the application control over recording of command buffers
as well as when they are submitted to the hardware, and command buffers can be
recorded once and reused repeatedly.  Trace commands are normally only recorded
into a command buffer when a perfetto trace is active.  Most applications don't
reuse command buffers, so you'll see traces appear shortly after the trace was
started, but if you have one of the rare applications that reuses command
buffers, you'll need to set the :envvar:`MESA_GPU_TRACES` environment variable
before starting a Vulkan application :

.. code-block:: sh

   MESA_GPU_TRACES=perfetto ./build/my_vulkan_app

Driver Specifics
~~~~~~~~~~~~~~~~

Below is driver specific information/instructions for the PPS producer.

Freedreno / Turnip
^^^^^^^^^^^^^^^^^^

The Freedreno PPS driver needs root access to read system-wide
performance counters, so you can simply run it with sudo:

.. code-block:: sh

   sudo ./build/src/tool/pps/pps-producer

Intel
^^^^^

The Intel PPS driver needs root access to read system-wide
`RenderBasic <https://www.intel.com/content/www/us/en/docs/vtune-profiler/user-guide/2023-0/gpu-metrics-reference.html>`__
performance counters, so you can simply run it with sudo:

.. code-block:: sh

   sudo ./build/src/tool/pps/pps-producer

Another option to enable access wide data without root permissions would be running the following:

.. code-block:: sh

   sudo sysctl dev.i915.perf_stream_paranoid=0

Alternatively using the ``CAP_PERFMON`` permission on the binary should work too.

A particular metric set can also be selected to capture a different
set of HW counters :

.. code-block:: sh

   INTEL_PERFETTO_METRIC_SET=RasterizerAndPixelBackend ./build/src/tool/pps/pps-producer

Vulkan applications can also be instrumented to be Perfetto producers.
To enable this for given application, set the environment variable as
follow :

.. code-block:: sh

   PERFETTO_TRACE=1 my_vulkan_app

Panfrost
^^^^^^^^

The Panfrost PPS driver uses unstable ioctls that behave correctly on
kernel version `5.4.23+ <https://lwn.net/Articles/813601/>`__ and
`5.5.7+ <https://lwn.net/Articles/813600/>`__.

To run the producer, follow these two simple steps:

1. Enable Panfrost unstable ioctls via kernel parameter:

   .. code-block:: sh

      modprobe panfrost unstable_ioctls=1

   Alternatively you could add ``panfrost.unstable_ioctls=1`` to your kernel command line, or ``echo 1 > /sys/module/panfrost/parameters/unstable_ioctls``.

2. Run the producer:

   .. code-block:: sh

      ./build/pps-producer

V3D / V3DV
^^^^^^^^^^

As we can only have one performance monitor active at a given time, we can only monitor
32 performance counters. There is a need to define the performance counters of interest
for pps_producer using the environment variable ``V3D_DS_COUNTER``.

.. code-block:: sh

   V3D_DS_COUNTER=cycle-count,CLE-bin-thread-active-cycles,CLE-render-thread-active-cycles,QPU-total-uniform-cache-hit ./src/tool/pps/pps-producer

Troubleshooting
---------------

Missing counter names
~~~~~~~~~~~~~~~~~~~~~

If the trace viewer shows a list of counters with a description like
``gpu_counter(#)`` instead of their proper names, maybe you had a data loss due
to the trace buffer being full and wrapped.

In order to prevent this loss of data you can tweak the trace config file in
two different ways:

- Increase the size of the buffer in use:

  .. code-block:: javascript

      buffers {
          size_kb: 2048,
          fill_policy: RING_BUFFER,
      }

- Periodically flush the trace buffer into the output file:

  .. code-block:: javascript

      write_into_file: true
      file_write_period_ms: 250


- Discard new traces when the buffer fills:

  .. code-block:: javascript

      buffers {
          size_kb: 2048,
          fill_policy: DISCARD,
      }
