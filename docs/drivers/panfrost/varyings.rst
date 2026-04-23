Varyings
========

This is the naming convention followed by the hardware (slightly different
from Vulkan naming):

- "Attribute": per-vertex data
- "Varying": per-fragment data

Thus vertex shaders always interact with attributes and fragment/pixel shaders
only interact with varyings (commonly shortened to "VAR").


Hardware descriptors
--------------------

Before Valhall there were two types of hardware descriptors:

- ``AttributeDescriptor``: Descriptor for a single attribute in a larger buffer,
  contains offset, stride and the in-memory format.
- ``BufferDescriptor``: Buffer, when used for attributes and varyings it
  contains: pointer, size and stride

Midgard (v5)
------------
Attributes are loaded/stored using ``LD_ATTR``/``ST_ATTR``, those query the
attribute descriptors and buffer descriptors to find the real global memory
offset before loading/storing.
All varyings are loaded using ``LD_VAR``, including flat attributes (using a
modifier), and special attributes (position, psiz, ecc...).  Special attributes
require a special descriptor, that is not backed by any buffer, it generates
data on-the-fly.
There is a concept of in-memory format and register format, the first is written
inside of the ``AttributeDescriptor`` while the register format is a modifier of
the instructions, when those two disagree the hardware will try to convert them,
this is only supported between int-int or float-float operations.


Bifrost (v6)
------------
Changes:

- ``ST_ATTR`` is replaced by ``LEA_ATTR`` and ``ST_CVT`` pairs.

  - ``LEA_ATTR`` queries memory address and format from the appropriate
    descriptor.
  - ``ST_CVT`` performs the actual conversion and memory store.
- A new ``LD_VAR_SPECIAL`` is introduced, now special attributes do not need
  special descriptors.
- ``LD_VAR`` only handles interpolated varyings, flat varyings are now handled
  by ``LD_VAR_FLAT``.

Valhall (v9)
------------
Valhall introduced layout-specific instructions, ``LEA_BUF`` and ``LD_VAR_BUF``,
those do not use ``AttributeDescriptor`` but read ``BufferDescriptor`` directly.
These new instructions bake the offset and in-memory format of the attribute
inside of the shader code itself, saving a lookup.  If all shaders in a program
use those instructions, we can remove all ``AttributeDescriptor``.  If the
shaders aren't compiled together, one of the shaders cannot know the data layout,
and it needs to fall back to the old ``AttributeDescriptor``-based path.  We
also have to fall back to ``AttributeDescriptor`` completely if the attribute
offset overflows the immediate field.

``LD_VAR_BUF`` only supports ``.f16`` and ``.f32`` as register modifiers, we can
also load (flat) ints with those.  We need to specify no conversion
(``.f16.src_flat16`` or ``.f32.src_flat32``), but with those we can load both
16-bit and 32-bit integers.

In this architecture we assume the VS always uses IDVS, emitting attribute data
with ``LEA_BUF`` (instead of ``LEA_ATTR``), and never emit varying
``AttributeDescriptor`` for VS.

Challenges
----------
Theoretically, the output types from the VS and input types from the FS should
always agree.  In practice we cannot trust the varyings types given to us,
here are some challenging examples:

* Shaders are allowed to use the same types with different precision modifiers.
* ``nir_opt_varying`` erases highp flat types with "float32" and packs them
  together.
* Some games like to play with ``intBitsToFloat`` / ``floatBitsToInt``, so it's
  possible to have VS write int and FS read float (in that case, we treat the
  underlying bits as float, allowing for NaN normalization)

In practice you have to follow these rules:

* Only FS knows a varying smoothness, VS sees everything as flat.
* When linked together, you cannot trust highp flat types, but you can trust
  precision
* While loading/storing data, mistaking a float for an int is ok, mistaking an
  int for a float seems to be lossy even without bit-size conversions.  Be
  conservative on what you say is a float.
* You cannot lower flat varyings after they passed through ``nir_opt_varying``

Current implementation rules:

* VS decides the memory layout
* Descriptor formats can mismatch between stages (eg. VS int - FS float)
* Descriptor formats always match what the instructions are using


To regain the type information:

* in VS, every highp varying is written as U32
* in FS, highp flat varyings are read as U32, highp smooth are read as F32
* for mediump, we trust the original type
