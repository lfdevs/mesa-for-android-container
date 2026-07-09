// Copyright 2026 Red Hat, Inc.
// SPDX-License-Identifier: MIT

//! Linux futex wrappers for cross-domain synchronization.

use std::num::NonZeroU32;
use std::sync::atomic::AtomicU32;

use rustix::thread::futex;

/// Wait on a futex with a bitset mask.
///
/// Blocks until the futex is woken up or the value changes from `val`.
pub fn wait_bitset(atomic_val: &AtomicU32, val: u32, bitset: u32) {
    let flags = futex::Flags::PRIVATE;
    let timeout = None;
    let val3 = NonZeroU32::new(bitset).unwrap_or(NonZeroU32::new(1).unwrap());

    // Ignore errors - futex::wait_bitset returns an error if the value
    // has already changed or if interrupted, both are expected
    let _ = futex::wait_bitset(atomic_val, flags, val, timeout, val3);
}

/// Wake threads waiting on a futex with a bitset mask.
///
/// Only wakes threads whose bitset matches the provided mask.
pub fn wake_bitset(atomic_val: &AtomicU32, val: i32, bitset: u32) {
    let flags = futex::Flags::PRIVATE;
    let val_u32 = if val < 0 { i32::MAX as u32 } else { val as u32 };
    let val3 = NonZeroU32::new(bitset).unwrap_or(NonZeroU32::new(1).unwrap());

    let _ = futex::wake_bitset(atomic_val, flags, val_u32, val3);
}

/// Wake all threads waiting on a futex.
pub fn wake_all(atomic_val: &AtomicU32) {
    let flags = futex::Flags::PRIVATE;
    // u32::MAX means wake all waiters
    let _ = futex::wake(atomic_val, flags, u32::MAX);
}
