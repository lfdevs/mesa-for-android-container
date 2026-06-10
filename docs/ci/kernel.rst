Upreving Linux Kernel
=====================

Occasionally, the GitLab CI needs a Linux Kernel update to enable new kernel
features, device drivers, bug fixes etc to CI jobs.
Kernel uprevs in GitLab CI are relatively simple, but prone to lots of
side-effects since many devices from different platforms are involved in the
pipeline.

Kernel repository
-----------------

The Linux Kernel used in the GitLab CI is stored at the following repository:
https://gitlab.freedesktop.org/gfx-ci/linux

It is common that Mesa kernel brings some patches that were not merged on the
Linux mainline, that is why Mesa has its own kernel version which should be used
as the base for newer kernels.

So, one should base the kernel uprev from the last tag used in the Mesa CI,
please refer to ``.gitlab-ci/image-tags.yml`` ``KERNEL_TAG`` variable.
Every tag has a standard naming: ``vX.YZ-for-mesa-ci-<commit_short_SHA>``, which
can be created via the command:

:code:`git tag vX.YZ-for-mesa-ci-$(git rev-parse --short HEAD)`

Building Kernel
---------------

The kernel files are loaded from the artifacts uploaded to S3 from gfx-ci/linux.

Updating Kconfigs
^^^^^^^^^^^^^^^^^

When a Kernel uprev happens, it is worth compiling and cross-compiling the
Kernel locally, in order to update the Kconfigs accordingly.  Remember that the
resulting Kconfig is a merge between *Mesa CI Kconfig* and *Linux tree
defconfig* made via ``merge_config.sh`` script located at Linux Kernel tree.

Kconfigs location
"""""""""""""""""

+------------+------------------------------------------------------+-------------------------------------+
| Platform   | Mesa CI Kconfig location                             | Linux tree defconfig                |
+============+======================================================+=====================================+
| arm        | kernel/configs/mesa3d-ci_arm.config\@gfx-ci/linux    | arch/arm/configs/multi_v7_defconfig |
+------------+------------------------------------------------------+-------------------------------------+
| arm64      | kernel/configs/mesa3d-ci_arm64.config\@gfx-ci/linux  | arch/arm64/configs/defconfig        |
+------------+------------------------------------------------------+-------------------------------------+
| x86-64     | kernel/configs/mesa3d-ci_x86_64.config\@gfx-ci/linux | arch/x86/configs/x86_64_defconfig   |
+------------+------------------------------------------------------+-------------------------------------+

Updating image tags
-------------------

Every kernel uprev should update the following tag:

:code:`.gitlab-ci/image-tags.yml` tag
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
- **KERNEL_TAG** to use the new kernel

Development routine
-------------------

1. Compile the newer kernel locally for each platform.
2. Compile device trees for ARM platforms
3. Update Kconfigs. Are new Kconfigs necessary? Is CONFIG_XYZ_BLA deprecated? Does the ``merge_config.sh`` override an important config?
4. Push a new development branch to `Kernel repository`_ based on the latest kernel tag used in GitLab CI
5. Hack ``build-kernel.sh`` script to clone kernel from your development branch
6. Update image tags. See `Updating image tags`_
7. Run the entire CI pipeline, all the automatic jobs should be green. If some job is red or taking too long, you will need to investigate it and probably ask for help.

When the Kernel uprev is stable
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

1. Push a new tag to Mesa CI `Kernel repository`_
2. Update KERNEL_URL ``debian/x86_test-gl`` job definition
3. Open a merge request, if it is not opened yet

Tips and Tricks
---------------

Compare pipelines
^^^^^^^^^^^^^^^^^

To have the most confidence that a kernel uprev does not break anything in Mesa,
it is suggested that one runs the entire CI pipeline to check if the update affected the manual CI jobs.

Step-by-step
""""""""""""

1. Create a local branch in the same git ref (should be the main branch) before branching to the kernel uprev kernel.
2. Push this test branch
3. Run the entire pipeline against the test branch, even the manual jobs
4. Now do the same for the kernel uprev branch
5. Compare the job results. If a CI job turned red on your uprev branch, it means that the kernel update broke the test. Otherwise, it should be fine.

Bare-metal custom kernels
^^^^^^^^^^^^^^^^^^^^^^^^^

Some CI jobs have support to plug in a custom kernel by simply changing a variable.
This is great, since rebuilding the kernel and rootfs may takes dozens of minutes.

For example, Freedreno jobs ``gitlab.yml`` manifest support a variable named
``BM_KERNEL``. If one puts a gz-compressed kernel URL there, the job will use that
kernel to boot the Freedreno bare-metal devices. The same works for ``BM_DTB`` in
the case of device tree binaries.

Careful reading of the job logs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Sometimes a job may turn to red for reasons unrelated to the kernel update, e.g.
LAVA ``tftp`` timeout, problems with the freedesktop servers etc.
So it is important to see the reason why the job turned red, and retry it if an
infrastructure error has happened.

Try your own kernel out
^^^^^^^^^^^^^^^^^^^^^^^

Trying out your own kernel is pretty straightforward. First, take the tree you
want to test, and cherry-pick the closest -for-mesa-ci branch from gfx-ci/linux.
For example, if you're based against 7.1 and the newest gfx-ci kernel is 6.19,
you'll want to cherry-pick every commit in the v6.19-for-mesa-ci branch that
isn't in the stable kernels. These commits are a mix of adding CI pipeline
support, and fixups required to run on our devices. Push this to your fork on
gitlab.freedesktop.org. Push a unique tag as well, e.g. fix-gpu-reset-abc1234.
The tag is important so that you can iterate without getting defeated by
caching.

Once your kernel is built (i.e. the pipeline in your branch has completed),
change ``KERNEL_TAG`` and ``KERNEL_REPO`` in Mesa's
``.gitlab-ci/image-tags.yml`` to refer to this, e.g.
``KERNEL_TAG: "fix-gpu-reset-abc1234"`` and
``KERNEL_REPO: "hopeless-optimist/linux"``.

Push this Mesa branch, and run your pipeline as usual.
