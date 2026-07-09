Help Wanted
===========

We can always use more help with the Mesa project. Here are some
specific ideas and areas where help would be appreciated:

#. **Driver testing.** Most developers are working on newer hardware
   on the Mesa main branch, but older hardware often doesn't have
   automated testing or as much testing coverage.  Testing your older
   hardware for regressions and bisecting and fixing problems in those
   drivers is useful for other Linux users.  You can also help by
   doing manual performance (or regression) testing work on performance
   changes being made in Mesa's open merge requests.
#. **Tasks up for grabs.** There are ideas for improvements which other
   developers would like to see someone take on, but are explicitly up for
   grabs, under the `difficulty: easy
   <https://gitlab.freedesktop.org/mesa/mesa/-/work_items?sort=created_date&state=opened&label_name%5B%5D=difficulty%3A%20easy>`__,
   `difficulty: medium
   <https://gitlab.freedesktop.org/mesa/mesa/-/work_items?sort=created_date&state=opened&label_name%5B%5D=difficulty%3A%20medium>`__,
   and `difficulty: hard
   <https://gitlab.freedesktop.org/mesa/mesa/-/work_items?sort=created_date&state=opened&label_name%5B%5D=difficulty%3A%20hard>`__
   labels.
#. **Driver debugging.** There are plenty of open bugs in the `bug
   database <https://gitlab.freedesktop.org/mesa/mesa/-/work_items>`__.
#. **Remove aliasing warnings.** Enable GCC's
   ``-Wstrict-aliasing=2 -fstrict-aliasing`` arguments, and track down
   aliasing issues in the code.
#. **Contribute more tests to**
   `Piglit <https://piglit.freedesktop.org/>`__ that aren't covered by
   VK-GL-CTS.

You can find some further To-do lists here:

**Common To-Do lists:**

-  `features.txt <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/features.txt>`__
   - Status of OpenGL/OpenGLES and Vulkan features in Mesa across drivers.

If you want to do something new in Mesa, first join the Mesa developer's
mailing list. Then post a message to propose what you want to do, just
to make sure there's no issues.

Anyone is welcome to contribute code to the Mesa project. By doing so,
it's assumed that you agree to the code's licensing terms.

Finally, be sure to review the instructions for :doc:`submitting
patches <submittingpatches>`.
