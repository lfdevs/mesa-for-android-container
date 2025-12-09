KosmicKrisp workarounds
#######################

This file documents the relevant issues found in either Metal, the MSL
compiler or any other component we have no control over that needed to be
worked around to accomplish Vulkan conformance.

All workarounds must be documented here and no code comment info should be
provided other than the name ``KK_WORKAROUND_#``.

Once a workaround was removed from the code, the code comment will be
removed but the documentation here will be kept.

Template
========

Use the following template to create documentation for a new workaround:

.. code-block::

   KK_WORKAROUND_#
   ---------------
   | macOS version:
   | Metal ticket:
   | Metal ticket status:
   | CTS test failure/crash:
   | Comments:
   | Log:

``macOS version`` needs to have the OS version with which it was found.

``Metal ticket`` needs to be either the Metal ticket number with the GitLab
handle of the user that reported the ticket or ``Unreported``.

``Metal ticket status`` needs to be either ``Fixed in macOS # (Build hash)``,

``Waiting resolution`` or empty if unreported. If Apple reported that the issue
was fixed, but no user has verified the fix, append ``[Untested]``.
``CTS test failure/crash`` (remove ``failure`` or ``crash`` based on test
behavior) needs to be the name of the test or test family the issue can be
reproduced with.

``Comments`` needs to include as much information on the issue and how the
workaround fixes it.

``Log`` needs to have the dates (yyyy-mm-dd, the only correct date format) with
info on what was updated.

Workarounds
===========

KK_WORKAROUND_4
---------------
| macOS version: 26.0.1
| Metal ticket: FB21124215 (@aitor)
| Metal ticket status: Waiting resolution
| CTS test failure: ``dEQP-VK.draw.renderpass.shader_invocation.helper_invocation*`` and few others
| Comments:

``simd_is_helper_thread()`` will always return true if the shader was started
as a non-helper thread, even after ``discard_fragment()`` is called. The
workaround is to have a variable tracking this state and update it when the
fragment is discarded. This issue is present in M1 and M2 chips.

| Log:
| 2025-11-22: Workaround implemented and reported to Apple

KK_WORKAROUND_3
---------------
| macOS version: 15.4.x
| Metal ticket: FB20113490 (@aitor)
| Metal ticket status: Waiting resolution
| CTS test failure: ``dEQP-VK.subgroups.ballot_other.*.subgroupballotfindlsb``
| Comments:

``simd_is_first`` does not seem to behave as documented in the MSL
specification. The following code snippet misbehaves:

.. code-block:: c

   if (simd_is_first())
      temp = 3u;
   else
      temp = simd_ballot(true); /* <- This will return all active threads... */

The way to fix this is by changing the conditional to:

.. code-block:: c

   if (simd_is_first() && (ulong)simd_ballot(true))
      temp = 3u;
   else
      temp = (ulong)simd_ballot(true);

| Log:
| 2025-09-09: Workaround implemented and reported to Apple

KK_WORKAROUND_2
---------------
| macOS version: 15.4.x
| Metal ticket: Not reported
| Metal ticket status:
| CTS test crash: ``dEQP-VK.graphicsfuzz.cov-nested-loops-never-change-array-element-one`` and ``dEQP-VK.graphicsfuzz.disc-and-add-in-func-in-loop``
| Comments:

We need to loop to infinite since MSL compiler crashes if we have something
like (simplified version):

.. code-block:: c

   while (true) {
      if (some_conditional) {
         break_loop = true;
      } else {
         break_loop = false;
      }
      if (break_loop) {
         break;
      }
   }

The issue I believe is that ``some_conditional`` wouldn't change the value no
matter in which iteration we are (something like fetching the same value from
a buffer) and the MSL compiler doesn't seem to like that much to the point it
crashes.

The implemented solution is to change the ``while(true)`` loop with
``for (uint64_t no_crash = 0u; no_crash < UINT64_MAX; ++no_crash)``, which
tricks the MSL compiler into believing we are not doing an infinite loop
(wink wink).

| Log:
| 2025-09-08: Workaround implemented

KK_WORKAROUND_1
---------------
| macOS version: 15.4.x
| Metal ticket: FB17604106 (@aitor)
| Metal ticket status: [Untested] Fixed in macOS 26 Beta (25A5279m)
| CTS test crash: ``dEQP-VK.glsl.indexing.tmp_array.vec3_dynamic_loop_write_dynamic_loop_read_fragment``
| Comments:

Uninitialized local scratch variable causes the MSL compiler to crash.
Initialize scratch to avoid issue.

| Log:
| 2025-05-14: Workaround implemented and reported to Apple
| 2025-06-14: Apple reported back saying it is now fixed in macOS 26 Beta (Build 25A5279m)



