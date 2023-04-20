Intel Surface Layout (ISL)
==========================

The Intel Surface Layout library (**ISL**) is a subproject in Mesa for doing
surface layout calculations for Intel graphics drivers.  It was originally
written by Lina Versace and is now maintained by Faith Ekstrand and Nanley
Chery.

.. toctree::
   :maxdepth: 2

   units
   formats
   tiling
   aux-surf-comp
   ccs
   hiz

The core representation of a surface in ISL is :cpp:struct:`isl_surf`.

.. doxygenstruct:: isl_surf
   :members:
