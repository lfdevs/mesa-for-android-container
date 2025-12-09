SPIR-V Debugging
================

SPIR-V is a binary shader intermediate representation used extensively by
Vulkan applications. Mesa translates SPIR-V shaders into its internal NIR
representation using the `spirv_to_nir` function.

.. _spirv_capture:

SPIR-V Shader Capture
~~~~~~~~~~~~~~~~~~~~~
Set the environment variable `MESA_SPIRV_DUMP_PATH` to a directory where
the SPIR-V shaders will be captured. This is useful for debugging purposes,
allowing developers to inspect the SPIR-V shaders provided by applications.

Shader capture filenames are a hexadecimal representation of the shader's
BLAKE3 hash. The file extension corresponds to the shader stage, e.g.
`.vert` for vertex shaders or `.frag` for fragment shaders.

It is usually necessary to disable the shader cache to activate the dump logic.
This can be done by setting the `MESA_SHADER_CACHE_DISABLE` environment variable to `1`.

.. code-block:: sh

   MESA_SPIRV_DUMP_PATH=$(pwd) MESA_SHADER_CACHE_DISABLE=1 vkcube

.. _spirv_replacement:

SPIR-V Shader Replacement
~~~~~~~~~~~~~~~~~~~~~~~~~
SPIR-V shaders can be replaced with alternative SPIR-V shaders
during runtime. This is useful for debugging or testing purposes, allowing
developers to swap out shaders without modifying the application.

To enable shader replacement, set the environment variable
`MESA_SPIRV_READ_PATH` to the directory containing the replacement SPIR-V shaders.
The filenames of the replacement shaders must match the original shader's BLAKE3
hash and the extensions must match the original shader stages (e.g., `.vert`, `.frag`).

It is usually necessary to disable the shader cache to ensure that
the replacement shaders are used. This can be done by setting the
`MESA_SHADER_CACHE_DISABLE` environment variable to `1`.

When a shader is replaced, Mesa will log the replacement action, including
the original shader's BLAKE3 hash and the filename of the replacement shader.
Set `MESA_SPIRV_LOG_LEVEL=info` to see these log messages printed to `stderr`.

.. code-block:: sh

   MESA_SPIRV_READ_PATH=$(pwd)/replace MESA_SPIRV_LOG_LEVEL=info MESA_SHADER_CACHE_DISABLE=1 vkcube

To replace a shader with a modified version of a captured shader, you can
convert the captured SPIR-V shader to GLSL, edit as needed, and then
recompile it back to SPIR-V.

.. code-block:: sh

   # Capture a SPIR-V shader
   MESA_SPIRV_DUMP_PATH=$(pwd) MESA_SHADER_CACHE_DISABLE=1 vkcube

   # Convert a captured SPIR-V shader to Vulkan-flavored GLSL
   # spriv-cross is bundled with the Vulkan SDK and also generally available via package managers
   spirv-cross -V 0x36c56e0f736238.frag --output 0x36c56e0f736238.frag.glsl

   # <Edit the GLSL file as needed>

   # Recompile the GLSL shader to SPIR-V
   # Some shaders may require additional flags, such as "--auto-map-locations"
   glslang -V 0x36c56e0f736238.frag.glsl -o replace/0x36c56e0f736238.frag

   # run the application with the replacement shader
   MESA_SPIRV_READ_PATH=$(pwd)/replace MESA_SPIRV_LOG_LEVEL=info MESA_SHADER_CACHE_DISABLE=1 vkcube

