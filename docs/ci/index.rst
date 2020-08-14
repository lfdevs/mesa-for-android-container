Continuous Integration
======================

GitLab CI
---------

GitLab provides a convenient framework for running commands in response to git pushes.
We use it to test merge requests (MRs) before merging them (pre-merge testing),
as well as post-merge testing, for everything that hits ``master``
(this is necessary because we still allow commits to be pushed outside of MRs,
and even then the MR CI runs in the forked repository, which might have been
modified and thus is unreliable).

The CI runs a number of tests, from trivial build-testing to complex GPU rendering:

- Build testing for a number of build systems, configurations and platforms
- Sanity checks (``meson test`` & ``scons check``)
- Some drivers (softpipe, llvmpipe, freedreno and panfrost) are also tested
  using `VK-GL-CTS <https://github.com/KhronosGroup/VK-GL-CTS>`__
- Replay of application traces

A typical run takes between 20 and 30 minutes, although it can go up very quickly
if the GitLab runners are overwhelmed, which happens sometimes. When it does happen,
not much can be done besides waiting it out, or cancel it.

Due to limited resources, we currently do not run the CI automatically
on every push; instead, we only run it automatically once the MR has
been assigned to ``Marge``, our merge bot.

If you're interested in the details, the main configuration file is ``.gitlab-ci.yml``,
and it references a number of other files in ``.gitlab-ci/``.

If the GitLab CI doesn't seem to be running on your fork (or MRs, as they run
in the context of your fork), you should check the "Settings" of your fork.
Under "CI / CD" → "General pipelines", make sure "Custom CI config path" is
empty (or set to the default ``.gitlab-ci.yml``), and that the
"Public pipelines" box is checked.

If you're having issues with the GitLab CI, your best bet is to ask
about it on ``#freedesktop`` on Freenode and tag `Daniel Stone
<https://gitlab.freedesktop.org/daniels>`__ (``daniels`` on IRC) or
`Eric Anholt <https://gitlab.freedesktop.org/anholt>`__ (``anholt`` on
IRC).

The three gitlab CI systems currently integrated are:


.. toctree::
   :maxdepth: 1

   bare-metal
   LAVA
   docker

Intel CI
--------

The Intel CI is not yet integrated into the GitLab CI.
For now, special access must be manually given (file a issue in
`the Intel CI configuration repo <https://gitlab.freedesktop.org/Mesa_CI/mesa_jenkins>`__
if you think you or Mesa would benefit from you having access to the Intel CI).
Results can be seen on `mesa-ci.01.org <https://mesa-ci.01.org>`__
if you are *not* an Intel employee, but if you are you
can access a better interface on
`mesa-ci-results.jf.intel.com <http://mesa-ci-results.jf.intel.com>`__.

The Intel CI runs a much larger array of tests, on a number of generations
of Intel hardware and on multiple platforms (x11, wayland, drm & android),
with the purpose of detecting regressions.
Tests include
`Crucible <https://gitlab.freedesktop.org/mesa/crucible>`__,
`VK-GL-CTS <https://github.com/KhronosGroup/VK-GL-CTS>`__,
`dEQP <https://android.googlesource.com/platform/external/deqp>`__,
`Piglit <https://gitlab.freedesktop.org/mesa/piglit>`__,
`Skia <https://skia.googlesource.com/skia>`__,
`VkRunner <https://github.com/Igalia/vkrunner>`__,
`WebGL <https://github.com/KhronosGroup/WebGL>`__,
and a few other tools.
A typical run takes between 30 minutes and an hour.

If you're having issues with the Intel CI, your best bet is to ask about
it on ``#dri-devel`` on Freenode and tag `Clayton Craft
<https://gitlab.freedesktop.org/craftyguy>`__ (``craftyguy`` on IRC) or
`Nico Cortes <https://gitlab.freedesktop.org/ngcortes>`__ (``ngcortes``
on IRC).

.. _CI-farm-expectations:

CI farm expectations
--------------------

To make sure that testing of one vendor's drivers doesn't block
unrelated work by other vendors, we require that a given driver's test
farm produces a spurious failure no more than once a week.  If every
driver had CI and failed once a week, we would be seeing someone's
code getting blocked on a spurious failure daily, which is an
unacceptable cost to the project.

Additionally, the test farm needs to be able to provide a short enough
turnaround time that we can get our MRs through marge-bot without the
pipeline backing up.  As a result, we require that the test farm be
able to handle a whole pipeline's worth of jobs in less than 15 minutes
(to compare, the build stage is about 10 minutes).

If a test farm is short the HW to provide these guarantees, consider
dropping tests to reduce runtime.
``VK-GL-CTS/scripts/log/bottleneck_report.py`` can help you find what
tests were slow in a ``results.qpa`` file.  Or, you can have a job with
no ``parallel`` field set and:

.. code-block:: yaml

    variables:
      CI_NODE_INDEX: 1
      CI_NODE_TOTAL: 10

to just run 1/10th of the test list.

If a HW CI farm goes offline (network dies and all CI pipelines end up
stalled) or its runners are consistently spuriously failing (disk
full?), and the maintainer is not immediately available to fix the
issue, please push through an MR disabling that farm's jobs by adding
'.' to the front of the jobs names until the maintainer can bring
things back up.  If this happens, the farm maintainer should provide a
report to mesa-dev@lists.freedesktop.org after the fact explaining
what happened and what the mitigation plan is for that failure next
time.

Personal runners
----------------

Mesa's CI is currently run primarily on packet.net's m1xlarge nodes
(2.2Ghz Sandybridge), with each job getting 8 cores allocated.  You
can speed up your personal CI builds (and marge-bot merges) by using a
faster personal machine as a runner.  You can find the gitlab-runner
package in debian, or use gitlab's own builds.

To do so, follow `gitlab's instructions
<https://docs.gitlab.com/ce/ci/runners/#create-a-specific-runner>`__ to
register your personal gitlab runner in your Mesa fork.  Then, tell
Mesa how many jobs it should serve (``concurrent=``) and how many
cores those jobs should use (``FDO_CI_CONCURRENT=``) by editing these
lines in ``/etc/gitlab-runner/config.toml``, for example::

  concurrent = 2

  [[runners]]
    environment = ["FDO_CI_CONCURRENT=16"]


Docker caching
--------------

The CI system uses docker images extensively to cache
infrequently-updated build content like the CTS.  The `freedesktop.org
CI templates
<https://gitlab.freedesktop.org/freedesktop/ci-templates/>`_ help us
manage the building of the images to reduce how frequently rebuilds
happen, and trim down the images (stripping out manpages, cleaning the
apt cache, and other such common pitfalls of building docker images).

When running a container job, the templates will look for an existing
build of that image in the container registry under
``FDO_DISTRIBUTION_TAG``.  If it's found it will be reused, and if
not, the associated `.gitlab-ci/containers/<jobname>.sh`` will be run
to build it.  So, when developing any change to container build
scripts, you need to update the associated ``FDO_DISTRIBUTION_TAG`` to
a new unique string.  We recommend using the current date plus some
string related to your branch (so that if you rebase on someone else's
container update from the same day, you will get a git conflict
instead of silently reusing their container)

When developing a given change to your docker image, you would have to
bump the tag on each ``git commit --amend`` to your development
branch, which can get tedious.  Instead, you can navigate to the
`container registry
<https://gitlab.freedesktop.org/mesa/mesa/container_registry>`_ for
your repository and delete the tag to force a rebuild.  When your code
is eventually merged to master, a full image rebuild will occur again
(forks inherit images from the main repo, but MRs don't propagate
images from the fork into the main repo's registry).
