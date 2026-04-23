:orphan:

.. _aco-live-var-analysis:

Description of live variable analysis in ACO
============================================

Operand flags
-------------

Operands can have several different flags which are set by live variable analysis:

* ``isKill``: this operand is not live after the instruction
* ``isFirstKill``: this is the first instance of a temporary in the instruction with ``isKill=true``
* ``isLateKill``: this operand cannot use the same registers as any definition
* ``isClobbered``: this operand will use some of the same registers as a definition
* ``isCopyKill``: this operand must use a different register than earlier instances of the same temporary in the
  instruction's operand list

Note that:

* ``isFirstKill=true`` requires that the operand is also ``isKill=true``.
* If ``isKill=true``, then ``isLateKill=true`` requires that the operand is also ``isFirstKill=true`` or
  ``isCopyKill=true``.
* ``isCopyKill=true`` is incompatible with ``isFirstKill=true`` in the same operand.
* ``isLateKill=true`` cannot be used with ``isClobbered=true`` for the same temporary in an instruction.
* If ``isCopyKill=true``, then the operand will also have ``isKill=true``, even if the temporary is live after the
  instruction.

Operands and definitions can be "tied", indicated by ``get_tied_defs()``, meaning that the must use the same registers:

* Operands tied to definitions have ``isClobbered=true``.
* If a clobbered operand has ``isKill=false``, it will be moved to a different register.
* If two operands of the same temporary are tied to different definitions of the same instruction, the second of those
  operands will have ``isCopyKill=true``.

There also exists the ``isVectorAligned`` flag. This can be used to define a vector of operands, starting with
``isVectorAligned=true`` and ending with ``isVectorAligned=false``, which must be placed in consecutive registers.

To prevent issues with register allocation, an operand being part of a vector means:

* Unless a vector is tied to a definition, it will also have ``isLateKill=true``, so that a partially killed vectors
  are not required to share the same space with definitions.
* If two operands of the same temporary are both part of vectors in the same instruction, the second of those operands
  will have ``isCopyKill=true``.

Register demand calculation
---------------------------

``live_in``
   temporaries live before the instruction

``live_out``
   temporaries live after the instruction

``live_through``
   temporaries live both before and after the instruction

``live_definitions``
   temporaries defined and used later (definitions where ``isKill=false``)

``dead_definitions``
   temporaries defined and not used later (definitions where ``isKill=true``)

``early_kill_operands``
   temporaries killed which are not marked late kill (operands where ``isFirstKill=true && isLateKill=false``)

``late_kill_operands``
   temporaries killed which are marked late kill (operands where ``isFirstKill=true && isLateKill=true``)

``first_kill_operands``
   temporaries killed by the instruction (operands where ``isFirstKill=true``)

   ``early_kill_operands + late_kill_operands``

``copied_operands``
   operands which are either clobbered but not killed, or copy-kill (operands where
   ``isCopyKill=true || (isClobbered=true && isKill=false)``)

``early_kill_copies``
   ``copied_operands`` which are not marked late kill (operands where
   ``(isCopyKill=true && isLateKill=false) || (isClobbered=true && isKill=false)``)

``late_kill_copies``
   ``copied_operands`` which are marked late kill (operands where ``(isCopyKill=true && isLateKill=true)``)

``live_out``
   ``live_through + live_definitions``

   ``live_in - first_kill_operands + live_definitions``

``live_in``
   ``live_out - live_definitions + first_kill_operands``

   ``live_through + first_kill_operands``

``live_through``
   ``live_out - live_definitions``

   ``live_in - first_kill_operands``

Breakdown of register demand stages using ``live_in``:

* stage 0: before instruction: ``live_in``
* stage 1:     setup operands: ``live_in + early_kill_copies + late_kill_copies``
* stage 2: during instruction: ``live_in - early_kill_operands + late_kill_copies``
* stage 3:  write definitions: ``live_in - early_kill_operands + late_kill_copies + live_definitions + dead_definitions``
* stage 4:  after instruction: ``live_in - early_kill_operands - late_kill_operands + live_definitions``

Breakdown of register demand stages using ``live_through``:

* stage 0: before instruction: ``live_through + late_kill_operands + early_kill_operands``
* stage 1:     setup operands: ``live_through + late_kill_operands + early_kill_operands + early_kill_copies + late_kill_copies``
* stage 2: during instruction: ``live_through + late_kill_operands + late_kill_copies``
* stage 3:  write definitions: ``live_through + late_kill_operands + late_kill_copies + live_definitions + dead_definitions``
* stage 4:  after instruction: ``live_through + live_definitions``

Breakdown of register demand stages using ``live_out``:

* stage 0: before instruction: ``live_out - live_definitions + late_kill_operands + early_kill_operands``
* stage 1:     setup operands: ``live_out - live_definitions + late_kill_operands + early_kill_operands + early_kill_copies + late_kill_copies``
* stage 2: during instruction: ``live_out - live_definitions + late_kill_operands + late_kill_copies``
* stage 3:  write definitions: ``live_out + dead_definitions + late_kill_operands + late_kill_copies``
* stage 4:  after instruction: ``live_out``

If instruction B immediately follows instruction A, then stage 0 of instruction B equals stage 4 of instruction A.
``Instruction::register_demand`` is ``max(stage1, stage3)``, which is equal to the maximum of all stages.

There are a few helper functions for examining how an instruction changes register demand:

``get_live_changes()``
   This is the register demand change from killed temporaries and live definitions.

   ``live_definitions - first_kill_operands``

   equal to ``live_out - live_in``

``get_temp_registers()``
   This is the temporary increase in register demand needed for copy-kill operands, late-kill operands, clobbered
   operands, and dead definitions.

   ``max(early_kill_operands + late_kill_operands + early_kill_copies + late_kill_copies - live_definitions, late_kill_operands + late_kill_copies + dead_definitions)``

   equal to ``register_demand - live_out``

``get_temp_reg_changes()``
   Since ``register_demand`` is ``max(stage1, stage3)``, this can be used to know what's the effect of marking an
   operand killed will be.

   ``live_definitions + dead_definitions - early_kill_operands - early_kill_copies``

   equal to ``stage3 - stage1``

They can be used as follows:

* ``register_demand(a) = live_in(a) + live_changes(a) + temp_registers(a)``
* ``register_demand(a) = live_out(a) + temp_registers(a)``

Assuming ``stage4(a)==stage0(b)``:

* ``register_demand(a) = register_demand(b) - temp_registers(b) - live_changes(b) + temp_registers(a)``
* ``register_demand(b) = register_demand(a) - temp_registers(a) + live_changes(b) + temp_registers(b)``

(note that ``max(a + b, a + c) - max(b, c) = a`` and ``a + max(b, c) = max(a + b, a + c)``)
