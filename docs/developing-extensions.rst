Developing Extensions
=====================

Drafting new MESA extensions
----------------------------

The Mesa project works closely with Khronos, the standards body that
governs the OpenGL, OpenCL, EGL, and Vulkan specifications.  Mesa
developers are often spec authors and not just implementors.  There are
several ``GL_MESA_*`` and ``VK_MESA_`` extensions which serve the unique
needs of the Linux graphics ecosystem as well as many other vendor, EXT,
and even KHR extensions extensions with Mesa developers as authors.

There are generally two processes for developing new extensions.  For
developers who work for a Khronos member company, extensions can be
developed via the normal Khronos process.  This means making a merge
request to the Khronos internal repositories, responding to review feedback
from the working group, and participating in the conference calls where
in-development extensions are discussed.  This process happens under the
Khronos NDA and is a little more heavy-weight than most Mesa discussions.
However, this is generally the preferred method from a Khronos perspective
because the NDA gives the hardware company representatives the freedom to be
more involved with extension review.

Alternatively, extensions can be developed in public.  This is done by
opening a pull request against the ``KhronosGroup/Vulkan-Docs`` and related
repositories on GitHub.  The advantage of this process is that it is
publicly visible and more accessible to open-source developers.  This makes
it the preferred process when developing something that needs broad Linux
ecosystem consensus and where we need review from people who aren't under
the Khronos NDA.  The downside is that hardware company representatives are
much less likely to interact.  If the extension exposes hardware and not
software details, it's better to go through the Khronos process.

Regardless of which path is taken, any extensions bearing the MESA prefix,
need to adhere to the following policy:

 1. The extension needs to be accompanied by a Mesa merge request and the
    extension MR needs to link to the Mesa MR.  For public extensions, both
    merge requests should be public and they should link to each other.
    For extensions developed under the Khronos NDA, the Mesa merge request
    can be made in the Khronos-internal Mesa repo.

 2. As with all other extensions, MESA extensions must be accompanied by
    CTS tests to ensure we can regression test them in CI.

 3. Before the extension gets merged and released, the Mesa implementation
    must be complete, passing the relevant CTS tests, and the code
    reviewed.  For extensions developed under the Khronos NDA, further
    review may happen when the implementation goes public.  However,
    developers should make a good-faith effort to get enough relevant
    review while the extension is still in development to find any issues
    which may impact the specification itself.


Implementing new extensions
---------------------------

Updating Khronos XML and headers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When implementing a brand new extension, Mesa's copies of the relevant spec
assets may need to be updated:

- For Vulkan, the headers are placed in ``include/vulkan/`` and the XML
  description is placed in ``src/vulkan/registry/vk.xml``

- For SPIR-V, the headers and JSON files are placed in
  ``src/compiler/spirv/``.

- For OpenGL and OpenGL ES, the headers are placed in ``include/GL/`` or
  ``include/GLES*/`` and the XML description is placed in
  ``src/mesa/glapi/glapi/registry/gl.xml``.

- For EGL, the headers are placed in ``include/EGL/`` and the XML
  description is placed in ``src/egl/generate/egl.xml``.

- For OpenCL, the headers are placed in ``include/CL/``.

All of these must be updated with the ``khronos_update.py`` script in
``bin/``.  They should not be updated manually.  The script updates all
headers and XML/JSON descriptions by default but only the changes for the
relevant API need to be checked in.  When updating XML or headers, provide
a descriptive commit message that specifies the new version.


Implementing Vulkan extensions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As of today, implementing Vulkan extensions is a mostly per-driver process.
Pulling a new XML and headers will automatically update all the dispatch
table and other codegen.  It's then up to the driver to implement and
advertise the extension.  When advertising a new extension, remember that
there are generally three places that need updating:

 - The driver's supported extensions table
 - The driver's supported features table
 - The driver's properties table

The extensions and features tables are split in two: physical device and
instance.  The instance tables are used for instance extensions and the
physical device tables are used for physical device extensions.  Only
physical device extensions have properties.


Implementing SPIR-V capabilities
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

SPIR-V features are typically implemented in terms of capabilities, not
extensions.  While SPIR-V does have extension strings, Mesa generally
ignores them.  When implementing a new SPIR-V feature, the
``implemented_capabilities`` table needs to be updated.  This table only
describes the capabilities implemented by the SPIR-V parser.  There is also
a ``supported_capabilities`` table that gets passed into the SPIR-V parser.

For OpenGL or OpenCL, implementing a new SPIR-V capability requires also
updating the ``supported_capabilities`` in the relevant state tracker.  For
Vulkan, ``supported_capabilities`` is automatically populated based on the
Vulkan device's physical device features.


Implementing OpenGL[ES] extensions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To add a new GL extension to Mesa you have to do at least the following.

-  If ``glext.h`` doesn't define the extension, edit ``include/GL/gl.h``
   and add code like this:

   .. code-block:: c

           #ifndef GL_EXT_the_extension_name
           #define GL_EXT_the_extension_name 1
           /* declare the new enum tokens */
           /* prototype the new functions */
           /* TYPEDEFS for the new functions */
           #endif


-  In the ``src/mesa/glapi/glapi/gen/`` directory, add the new extension
   functions and enums to the ``gl_API.xml`` file. Then, a bunch of
   source files must be regenerated by executing the corresponding
   Python scripts.
-  Add a new entry to the ``gl_extensions`` struct in ``consts_exts.h`` if
   the extension requires driver capabilities not already exposed by
   another extension.
-  Add a new entry to the ``src/mesa/main/extensions_table.h`` file.
-  From this point, the best way to proceed is to find another
   extension, similar to the new one, that's already implemented in Mesa
   and use it as an example.
-  If the new extension adds new GL state, the functions in ``get.c``,
   ``enable.c`` and ``attrib.c`` will most likely require new code.
-  To determine if the new extension is active in the current context,
   use the auto-generated ``_mesa_has_##name_str()`` function defined in
   ``src/mesa/main/extensions.h``.
