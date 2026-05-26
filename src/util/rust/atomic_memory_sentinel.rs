// Copyright 2026 Red Hat, Inc.
// SPDX-License-Identifier: MIT

//! AtomicMemorySentinel - Cross-platform atomic memory synchronization primitive.
//!
//! Provides access to futex and WaitOnAddress-like APIs for atomic memory operations.

use std::mem::size_of;
use std::sync::atomic::{AtomicU32, Ordering};

use crate::sys::platform::atomic_memory_sentinel;
use crate::MappedRegion;
use crate::MemoryMapping;
use crate::MesaError;
use crate::MesaResult;

/// A sentinel that provides access to atomic memory.
/// Safe to share across threads.
pub struct AtomicMemorySentinel {
    _memory_mapping: MemoryMapping,
    atomic_ptr: *mut u32,
}

// SAFETY: AtomicMemorySentinel can be sent across threads.
unsafe impl Send for AtomicMemorySentinel {}
// SAFETY: AtomicMemorySentinel can be shared across threads.
unsafe impl Sync for AtomicMemorySentinel {}

impl AtomicMemorySentinel {
    /// Create a new AtomicMemorySentinel from a memory mapping.
    pub fn new(memory_mapping: MemoryMapping) -> MesaResult<Self> {
        if memory_mapping.size() < size_of::<u32>() {
            return Err(MesaError::WithContext(
                "memory mapping too small for AtomicU32",
            ));
        }

        let atomic_ptr = memory_mapping.as_ptr() as *mut u32;

        Ok(Self {
            _memory_mapping: memory_mapping,
            atomic_ptr,
        })
    }

    /// Signal that the atomic memory has changed.
    ///
    /// This will wake up waiters on this sentinel.
    pub fn signal(&self) -> MesaResult<()> {
        // SAFETY: self.atomic_ptr is valid for the lifetime of the MemoryMapping,
        // properly aligned, and we have exclusive ownership via the MemoryMapping.
        let atomic_val = unsafe { AtomicU32::from_ptr(self.atomic_ptr) };
        atomic_memory_sentinel::wake_bitset(atomic_val, i32::MAX, 1);
        Ok(())
    }

    /// Load the current value from atomic memory.
    pub fn load(&self) -> u32 {
        // SAFETY: self.atomic_ptr is valid for the lifetime of the MemoryMapping,
        // properly aligned, and we have exclusive ownership via the MemoryMapping.
        let atomic_val = unsafe { AtomicU32::from_ptr(self.atomic_ptr) };
        atomic_val.load(Ordering::SeqCst)
    }

    /// Store a value to atomic memory.
    pub fn store(&self, val: u32) {
        // SAFETY: self.atomic_ptr is valid for the lifetime of the MemoryMapping,
        // properly aligned, and we have exclusive ownership via the MemoryMapping.
        let atomic_val = unsafe { AtomicU32::from_ptr(self.atomic_ptr) };
        atomic_val.store(val, Ordering::SeqCst);
    }

    /// Wake all threads waiting on this sentinel.
    pub fn wake_all(&self) {
        // SAFETY: self.atomic_ptr is valid for the lifetime of the MemoryMapping,
        // properly aligned, and we have exclusive ownership via the MemoryMapping.
        let atomic_val = unsafe { AtomicU32::from_ptr(self.atomic_ptr) };
        atomic_memory_sentinel::wake_all(atomic_val);
    }

    /// Blocks until the value changes from `val` or is woken up.
    pub fn wait(&self, val: u32) {
        // SAFETY: self.atomic_ptr is valid for the lifetime of the MemoryMapping,
        // properly aligned, and we have exclusive ownership via the MemoryMapping.
        let atomic_val = unsafe { AtomicU32::from_ptr(self.atomic_ptr) };
        atomic_memory_sentinel::wait_bitset(atomic_val, val, 1);
    }
}
