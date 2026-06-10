Mesh Shading Notes
=============================================

Mesh shaders support is present on Turing and later.
It reuses the 3D engine and regular graphics stages.

Draw
""""

When performing a draw with a mesh shader bound, ``SET_DRAW_CONTROL_A`` needs to
be used while ignoring the base indices.

This can be done with the following commands:
.. code-block::

   NVC597_SET_VERTEX_ID_BASE
      .V = (0x0)
   NVC597_SET_DRAW_CONTROL_A
      .TOPOLOGY = POINTS
      .PRIMITIVE_ID = FIRST
      .INSTANCE_ID = FIRST
      .SPLIT_MODE = NORMAL_BEGIN_NORMAL_END
      .INSTANCE_ITERATE_ENABLE = FALSE
      .IGNORE_GLOBAL_BASE_VERTEX_INDEX = TRUE
      .IGNORE_GLOBAL_BASE_INSTANCE_INDEX = TRUE
   NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_A
      .START = (0x0)
   NVC597_DRAW_VERTEX_ARRAY_BEGIN_END_B
      .COUNT = ($groupCount)


Where:

- ``$groupCount`` is the number of local workgroups to dispatch.

**NOTE**: The topology of ``SET_DRAW_CONTROL_A`` will be ignored and
``SET_MESH_SHADER_A`` topology will be used.

Stages
""""""

Before binding a task or mesh shader, ``SET_MESH_CONTROL`` needs to be set.

Shared Memory
^^^^^^^^^^^^^

Shared memory is allocated in the output :doc:`ISBE Attribute region <isbe_layout>` after all attributes.

Part of the shared memory can be made accessible to the next stage
as part of the input :doc:`ISBE Attribute region <isbe_layout>` using ``.OUTPUT_TO_M_S_LINES``.

As such, the task payload is the first part of the shared memory on the task stage.


Task
^^^^

The task shader is always bound as a vertex shader.

Additionally, ``SET_MESH_INIT_SHADER`` is used to set the number of local invocations
to use and the size used for shared memory.

This can be done with the following command:
::

   NVC597_SET_MESH_INIT_SHADER
      .THREAD_COUNT = ($thread_count)
      .LOCAL_BUFFER_LINES = ($local_buffer_lines)
      .OUTPUT_TO_M_S_LINES = ($output_to_m_s_lines)

Where:

- ``$thread_count`` is the number of local invocations (up to 32).
- ``$local_buffer_lines`` is the total amount of shared memory lines (including task payload)
  to allocate in the output :doc:`ISBE Attribute region <isbe_layout>` after all attributes.
- ``$output_to_m_s_lines`` is the total amount of shared memory lines that will be available
  to the next stage.

**NOTE**: The size of a memory line is 128 bytes.

The workgroup_index is implemented using the ``VERTEX_ID`` read from the input :doc:`ISBE Attribute region <isbe_layout>` (with SKEW applied).

``EmitMeshTasksEXT`` is lowered to the equivalent pseudo code:
::

   void EmitMeshTasksEXT(uint x, uint y, uint z) {
      uint taskCount = x * y * z;

      ISBEWR.O.ATTR.32 [0x04], taskCount;
      ISBEWR.O.ATTR.32 [0x08], x;
      ISBEWR.O.ATTR.32 [0x0C], y;
      ISBEWR.O.ATTR.32 [0x10], z;
   }

Mesh
^^^^

The mesh shader is bound to the vertex stage if no task shader is present
or the tess control stage otherwise.

Attributes are stored in the output :doc:`ISBE Attribute region <isbe_layout>` **with SKEW applied**.

If any per primitive attributes are in use, they are stored after all per vertex
attributes and the geometry stage will be enabled in passthrough mode with only
its program header present.

For more details about the ISBE Attribute or Map layout, see the dedicated :doc:`ISBE <isbe_layout>` page.

Additionally, ``SET_MESH_SHADER_A`` and ``SET_MESH_SHADER_B`` are used
to set the number of local invocations to use, the size used for shared memory
and topology details.

This can be done with the following command:
::

   NVC597_SET_MESH_SHADER_A
      .OUTPUT_TOPOLOGY = $topology
      .MAX_VERTEX = ($max_vertex)
      .MAX_PRIMITIVE = ($max_primitive)
   NVC597_SET_MESH_SHADER_B
      .SHARED_MEM_LINES = ($shared_mem_lines)
      .THREAD_COUNT = ($thread_count)

Where:

- ``$topology`` is the topology to use.
- ``$max_vertex`` is the max count of vertices used by the mesh shader.
- ``$max_primitive``  is the max count primitives used by the mesh shader.
- ``$shared_mem_lines`` is the total amount of shared memory line to allocate
  in the output :doc:`ISBE Attribute region <isbe_layout>` after all attributes.
- ``$thread_count`` is the number of local invocations (up to 32).

**NOTE**: The size of a memory line is 128 bytes.

The workgroup_index is implemented in the following way:

- If a task shader is present, the value is read from the input :doc:`ISBE Attribute region <isbe_layout>` **without SKEW applied**.
- Otherwise, it is implemented using the ``VERTEX_ID`` read from the input :doc:`ISBE Attribute region <isbe_layout>` **with SKEW applied**.

``SetMeshOutputsEXT`` is lowered to the equivalent pseudo code:
::

   void SetMeshOutputsEXT(uint vertexCount, uint primitiveCount) {
      // vertexCount is unused
      ISBEWR.O.MAP.32 [0x3], primitiveCount;
   }

**NOTE**: This is effectively a write to offset 0x0 but the output :doc:`ISBE Map region <isbe_layout>` process writes in reverse.

All primitive indices are stored as 8-bit indices starting at offset 0x4 in the ouptut :doc:`ISBE Map region <isbe_layout>`.


Hardware limitations
""""""""""""""""""""

* Only up to 32 local invocations are supported.
* The shared memory being part of the :doc:`ISBE Attribute region <isbe_layout>` makes it that
  we do not have any atomics for it.
* Task / mesh invocations need to be counted inside the shader.
