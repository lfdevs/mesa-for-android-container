// Copyright 2026 Red Hat, Inc.
// SPDX-License-Identifier: MIT

//! Stub atomic memory synchronization implementation.
//!
//! The Linux backend uses futexes for atomic memory synchronization.
//! This stub implementation allows compilation on platforms without native support.

use std::sync::atomic::AtomicU32;

/// Stub implementation - no-op on non-Linux platforms.
pub fn wait_bitset(_atomic_val: &AtomicU32, _val: u32, _bitset: u32) {
    todo!("atomic memory synchronization not implemented on this platform")
}

/// Stub implementation - no-op on non-Linux platforms.
pub fn wake_bitset(_atomic_val: &AtomicU32, _val: i32, _bitset: u32) {
    todo!("atomic memory synchronization not implemented on this platform")
}

/// Stub implementation - no-op on non-Linux platforms.
pub fn wake_all(_atomic_val: &AtomicU32) {
    todo!("atomic memory synchronization not implemented on this platform")
}
