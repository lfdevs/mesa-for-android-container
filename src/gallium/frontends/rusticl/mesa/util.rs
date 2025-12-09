// Copyright 2022 Red Hat.
// SPDX-License-Identifier: MIT

use mesa_rust_gen::util_get_cpu_caps;

pub mod disk_cache;
pub mod queue;
pub mod vm;

/// Gets the number of currently-online CPUs available to mesa.
pub fn cpu_count() -> u32 {
    // SAFETY: `util_get_cpu_caps()` always returns a valid set of CPU caps.
    let caps = unsafe { &*util_get_cpu_caps() };
    debug_assert!(caps.nr_cpus > 0);

    caps.nr_cpus as u32
}
