drm-shim CI Reproduction
========================

Overview
--------

``drm-shim`` is a user-space library that intercepts ioctl calls using ``LD_PRELOAD``.
It emulates a hardware DRM device node, bypassing the actual kernel driver.
This allows drivers (like ``radeonsi``, ``radv``, ``turnip``, or ``freedreno``)
to initialize as if real hardware is present.

All rendering execution is no-oped (unless using a shim that executes on a simulator),
but tests will often run to completion, which is usually enough to reproduce
crashes and assertion failures.

The shim libraries are built alongside the corresponding driver when you
configure your build with ``-Dtools=drm-shim``.

Running these tests **must** be done inside a ``meson devenv`` shell.
This automatically sets up necessary environment variables like
``VK_DRIVER_FILES`` to point to your newly built drivers, and adds the
``drm-shim`` wrapper script (located at ``bin/drm-shim.py``) to your ``PATH``.

The CI jobs test various GPU targets using the following test suites:

- GL CTS: ``-DDEQP_TARGET=surfaceless``
- VK CTS: ``-DDEQP_TARGET=vulkan_headless``
- Piglit: Use ``PIGLIT_PLATFORM=gbm``

Once inside the ``meson devenv`` shell, CI jobs can be reproduced
using the drm-shim wrapper. For example, to reproduce the
``zink-anv-tgl`` job:

.. code-block:: shell

   drm-shim zink-anv-tgl ./deqp-gles2



AMD CI Jobs
----------------------------


=========  =============================  ==============================================================================================================  ====================================================================
CI job     Driver maintainers             CI job maintainers                                                                                              Build Options
=========  =============================  ==============================================================================================================  ====================================================================
Stoney     Marek Olšák (@mareko, mareko)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``



Raven      Marek Olšák (@mareko, mareko)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``



Cezanne    Marek Olšák (@mareko, mareko)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``



Mendocino  Marek Olšák (@mareko, mareko)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``



Vangogh    Marek Olšák (@mareko, mareko)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``



Navi21     Marek Olšák (@mareko, mareko)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``


Navi31     Marek Olšák (@mareko, mareko)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``



GFX1201    Marek Olšák (@mareko, mareko)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=radeonsi -Dvulkan-drivers=amd -Dtools=drm-shim``


=========  =============================  ==============================================================================================================  ====================================================================


Intel CI Jobs
----------------------------



=================  ===========================================================================================================  ==============================================================================================================  ========================================================================
CI job             Driver maintainers                                                                                           CI job maintainers                                                                                              Build Options
=================  ===========================================================================================================  ==============================================================================================================  ========================================================================
APL (Apollo Lake)  Kenneth Graunke, (@kwg, Kayden), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)              Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dtools=drm-shim,intel``



GLK (Gemini Lake)  Kenneth Graunke, (@kwg, Kayden), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)              Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dtools=drm-shim,intel``



KBL (Kaby Lake)    Kenneth Graunke, (@kwg, Kayden), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)              Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dtools=drm-shim,intel``



JSL (Jasper Lake)  Lionel Landwerlin, (@llandwerlin, dj-death), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dvulkan-drivers=intel -Dtools=drm-shim,intel``



TGL (Tiger Lake)   Lionel Landwerlin, (@llandwerlin, dj-death), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dvulkan-drivers=intel -Dtools=drm-shim,intel``



ADL (Alder Lake)   Lionel Landwerlin, (@llandwerlin, dj-death), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dvulkan-drivers=intel -Dtools=drm-shim,intel``



RPL (Raptor Lake)  Lionel Landwerlin, (@llandwerlin, dj-death), Caio Oliveira, (@cmarcelo, cmarcelo), Ian Romanick (@idr, idr)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=iris -Dvulkan-drivers=intel -Dtools=drm-shim,intel``


=================  ===========================================================================================================  ==============================================================================================================  ========================================================================


ARM CI Jobs
----------------------------


=============  ===============================================  ==============================================================================================================  =========================================================================
CI job         Driver maintainers                               CI job maintainers                                                                                              Build Options
=============  ===============================================  ==============================================================================================================  =========================================================================
lima-mali450   Your Name Here (gitlab:@username, irc:username)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=lima -Dtools=drm-shim``



panfrost-g52   Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=panfrost -Dvulkan-drivers=panfrost -Dtools=drm-shim``



panfrost-g57   Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=panfrost -Dtools=drm-shim``



panfrost-g72   Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=panfrost -Dtools=drm-shim``



panfrost-g610  Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=panfrost -Dvulkan-drivers=panfrost -Dtools=drm-shim``



panfrost-g925  Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=panfrost -Dvulkan-drivers=panfrost -Dtools=drm-shim``


=============  ===============================================  ==============================================================================================================  =========================================================================


Broadcom CI Jobs
----------------------------


======  ===============================================  ===============================================  ====================================================================
CI job  Driver maintainers                               CI job maintainers                               Build Options
======  ===============================================  ===============================================  ====================================================================
rpi3    Your Name Here (gitlab:@username, irc:username)  Your Name Here (gitlab:@username, irc:username)  ``-Dgallium-drivers=vc4 -Dtools=drm-shim``



rpi4    Your Name Here (gitlab:@username, irc:username)  Your Name Here (gitlab:@username, irc:username)  ``-Dgallium-drivers=v3d -Dvulkan-drivers=broadcom -Dtools=drm-shim``



rpi5    Your Name Here (gitlab:@username, irc:username)  Your Name Here (gitlab:@username, irc:username)  ``-Dgallium-drivers=v3d -Dvulkan-drivers=broadcom -Dtools=drm-shim``


======  ===============================================  ===============================================  ====================================================================


Freedreno CI Jobs
----------------------------------------


======  ===============================================  ==============================================================================================================  ===========================================================================
CI job  Driver maintainers                               CI job maintainers                                                                                              Build Options
======  ===============================================  ==============================================================================================================  ===========================================================================
a306    Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=freedreno -Dvulkan-drivers=freedreno -Dtools=drm-shim``



a530    Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=freedreno -Dvulkan-drivers=freedreno -Dtools=drm-shim``



a618    Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=freedreno -Dvulkan-drivers=freedreno -Dtools=drm-shim``



a660    Your Name Here (gitlab:@username, irc:username)  Sergi Blanch Torne (@sergi, sergi), Valentine Burley (@Valentine, valentine), Daniel Stone (@daniels, daniels)  ``-Dgallium-drivers=freedreno -Dvulkan-drivers=freedreno -Dtools=drm-shim``



a750    Your Name Here (gitlab:@username, irc:username)  Your Name Here (gitlab:@username, irc:username)                                                                 ``-Dgallium-drivers=freedreno -Dvulkan-drivers=freedreno -Dtools=drm-shim``


======  ===============================================  ==============================================================================================================  ===========================================================================
