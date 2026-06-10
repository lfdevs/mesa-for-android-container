ISBE Layout
=============================================

The ISBE is a piece of staging memory shared between the :abbr:`PE (Primitive Engine)`
and :abbr:`SM (Streaming Multiprocessor)` containing the attributes for
:abbr:`VTG (Vertex, Tesselation, Geometry)` stages.

In normal cases, no shader needs to access it and instead use ``ALD`` or ``AST``
but in some some cases (like ``ALD`` vtx handling or mesh shaders) direct access to
it is necessary.

This document the observations made about the layout of it.

The ISBE "space" is separated in multiple regions (map, patch, primitive and attribute)
and can be accessed with ``ISBERD`` and ``ISBEWR`` instructions.

There is always two ISBE "space" present: one for inputs and one for outputs.
Input and output ISBE memory can be shared by setting bit 25 of the
:abbr:`SPH (Shader Program Header)`.

**NOTE**: Before Turing, only ``ISBERD`` was supported with the ``.MAP`` flag.

Map region
""""""""""

The Map region contains all the vertex indices for all primitives.

When used as output, it starts with the primitives count.

==================================================== ====================  ===================================================
Byte Range                                           Name                  Note
==================================================== ====================  ===================================================
0x0                                                  primitive_count       32-bit, Only present as output ISBE
0x0?..(0x0? + primitive_count * $vtx_per_prim_count) primitive_indices[i]  8-bit, Offset is 0x04 as output ISBE otherwise 0x00
==================================================== ====================  ===================================================

Attribute region
""""""""""""""""

The Attribute region contains all attributes and is allocated based on the
:abbr:`SPH (Shader Program Header)` input/output mask and :doc:`mesh shader methods <mesh_shading_notes>`.

With the ``SKEW`` flag applied on ``ISBERD`` and ``ISBEWR``, each type of attribute is packed with
32 values of the same type forming "memory lines" of 128 bytes each.

Additionally, the order of the attribute packs is determined by the unique attribute id.
(see `nak_attr` or :abbr:`SPH (Shader Program Header)` headers definitions for the values)

Finally, if more than 32 values are needed, the layout repeat itself.

Here is an example for 256 vertices being defined with ``ATTR_POINT_SIZE`` (0x6C) and
``ATTR_POSITION_X`` (0x70) active:

==================================================== ========================
Byte Range                                           Name                
==================================================== ========================
0x000..0x080                                         ATTR_POINT_SIZE[0..31]
0x000..0x100                                         ATTR_POSITION_X[0..31]
0x180..0x200                                         ATTR_POINT_SIZE[32..63]
0x200..0x280                                         ATTR_POSITION_X[32..63]
0x280..0x300                                         ATTR_POINT_SIZE[63..95]
0x300..0x380                                         ATTR_POSITION_X[63..95]
...                                                  ...
0x700..0x780                                         ATTR_POINT_SIZE[224..255]
0x780..0x800                                         ATTR_POSITION_X[224..255]
==================================================== ========================
