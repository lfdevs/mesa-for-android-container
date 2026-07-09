Rust
====

Rust Subproject Names
---------------------

All Rust subprojects should follow the convention ``NAME-SEMVER-rs``,
where ``SEMVER`` is the version up to and including the first nonzero
component.  For example, ``zerocopy-0.8.13`` becomes ``zerocopy-0.8-rs``,
whereas ``syn-2.0.66`` becomes ``syn-2-rs``.

Rust Update Policy
------------------

Given that for some distributions it's not feasible to keep up with the
pace of Rust, we promise to only bump the minimum required Rust version
following those rules:

-  Only up to the Rust requirement of other major Linux desktop
   components, e.g.:

   -  `Firefox ESR <https://whattrainisitnow.com/release/?version=esr>`__:
      `Minimum Supported Rust Version:
      <https://firefox-source-docs.mozilla.org/writing-rust-code/update-policy.html#schedule>`__

   -  latest `Linux Kernel Rust requirement
      <https://docs.kernel.org/process/changes.html#current-minimal-requirements>`__

-  Only require a newer Rust version than stated by other rules if and only
   if it's required to get around a bug inside rustc.

As bug fixes might run into rustc compiler bugs, a rust version bump _can_
happen on a stable branch as well.

Peripheral Support for crates.io uploads
----------------------------------------
Certain Mesa crates are uploaded to crates.io at the discretion of certain
sub-communities of Mesa.  These crates are:

- https://crates.io/crates/magma-gpu

These crates are used as dependencies to other Rust-based projects, such as:

- https://crates.io/crates/rutabaga_gfx

This is not supported by the "core" Mesa quarterly release cycles, and the
official Mesa maintainers are not liable for use or mis-use of the crates.
Please contact the relevant sub-community before using these crates outside
of Mesa3D.

Official Mesa distribution of crates will likely have wait until improvements
are made in the Meson build system:

- https://github.com/mesonbuild/meson/issues/2173

The Meson build system is the only build system officially supported by
Mesa3D.
