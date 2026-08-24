KosmicKrisp
###########

KosmicKrisp is a Vulkan conformant implementation for macOS on Apple Silicon
hardware. It is implemented on top of Metal 4, which requires macOS 26 and up.

No iOS support is present as of now. However, iOS was taken into consideration
during development to support A14 Bionic GPUs and upwards.

Building
********

The following build instructions assume Homebrew as the package manager to
install dependencies. Homebrew homepage https://brew.sh/
Homebrew install command line:

.. code-block:: sh

   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

Terminal restart is recommended after Homebrew installation.

Requirements
============

- Xcode and Xcode command line tools
- Homebrew packages
   - meson (1.9.1+, can also be installed as a Python package)
   - cmake
   - pkg-config
   - libclc
   - llvm (20.1.8+, lower versions may work but have not been tested)
   - spirv-llvm-translator
   - spirv-tools (static library preferred which is not included in the package, Vulkan macOS SDK does)

Users might already have llvm installed, run ``llvm-config --version`` and
check that the command works and prints at least ``20.1.8``. Otherwise install
llvm from homebrew and add it to the ``PATH`` (follow homebrew instructions).

- Python
- Python packages
   - mako
   - packaging
   - pyyaml
   - meson (1.9.1+, if not installed through Homebrew)

Since Homebrew manages the Python environment, it is encouraged to create a
Python virtual environment and install all packages in that environment. To
create a Python virtual environment (e.g. ``$HOME/venv_mesa``):

.. code-block:: sh

   python3 -m venv $HOME/venv_mesa

To enable a Python virtual environment:

.. code-block:: sh

   source $HOME/venv_mesa/bin/activate

Build instructions
==================

Out of tree build directory is recommended.

Once all requirements have been installed, the following command line can be
used to create a debug build:

.. code-block:: sh

   meson setup <path/to/mesa> --buildtype=debug -Dplatforms=macos -Dvulkan-drivers=kosmickrisp -Dgallium-drivers= -Dopengl=false -Dzstd=disabled --prefer-static

Environment variables
*********************

KosmicKrisp specific environment variables:

- ``MESA_KK_DEBUG``: ``msl`` to log all generated Metal Shading Language (MSL) shaders. ``force_robustness`` to force robustness on all shaders.
- ``MESA_KK_GPU_CAPTURE``: Starts Metal capture at device create and ends it at device destroy. Set to ``1`` to activate.
- ``MESA_KK_GPU_CAPTURE_DIRECTORY``: Metal capture will be saved to the specified directory. Defaults to Xcode if no path is provided.
- ``MESA_KK_DISABLE_WORKAROUNDS``: Provide ``all`` to disable all workarounds. Otherwise, provide a comma separated list to disable wanted workarounds e.g. ``1,3,4`` to disable workaround 1, 3 and 4.
- ``MESA_KK_EXPERIMENTAL``: Enables advertising experimental features which may not provide full correctness or may negatively impact performance when enabled. Provide ``all`` to advertise all listed features.
  - ``custom_border`` to advertise ``VK_EXT_custom_border_color`` and ``VK_EXT_border_color_swizzle``.
  - ``image_view_min_lod`` to advertise ``VK_EXT_image_view_min_lod``.
  - ``m5_optimize_e5r9g9b9`` to use allowGPUOptimizedContents on M5 with this format, which causes some CTS tests for vkCmdCopyImage() to fail

Metal workarounds
*****************

Different workarounds are applied throughout the project to avoid issues such
as:

- Metal API and Vulkan API discrepancies
- Metal bugs
- MSL compiler bugs
- MSL compiler crashes

These workarounds can be found in:

.. toctree::
   :maxdepth: 1

   kosmickrisp/workarounds
