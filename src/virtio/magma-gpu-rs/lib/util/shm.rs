// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::ffi::CString;

use crate::util::sys::platform::page_size;
use crate::util::sys::platform::SharedMemory as PlatformSharedMemory;
use crate::util::AsRawDescriptor;
use crate::util::Error;
use crate::util::FromRawDescriptor;
use crate::util::IntoRawDescriptor;
use crate::util::OwnedDescriptor;
use crate::util::RawDescriptor;
use crate::util::Result;

pub struct SharedMemory(pub(crate) PlatformSharedMemory);
impl SharedMemory {
    /// Creates a new shared memory object of the given size.
    ///
    /// |name| is purely for debugging purposes. It does not need to be unique, and it does
    /// not affect any non-debugging related properties of the constructed shared memory.
    pub fn new<T: Into<Vec<u8>>>(debug_name: T, size: u64) -> Result<SharedMemory> {
        let debug_name = CString::new(debug_name)?;
        PlatformSharedMemory::new(&debug_name, size).map(SharedMemory)
    }

    pub fn size(&self) -> u64 {
        self.0.size()
    }
}

impl AsRawDescriptor for SharedMemory {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        self.0.as_raw_descriptor()
    }
}

impl IntoRawDescriptor for SharedMemory {
    fn into_raw_descriptor(self) -> RawDescriptor {
        self.0.into_raw_descriptor()
    }
}

impl From<SharedMemory> for OwnedDescriptor {
    fn from(sm: SharedMemory) -> OwnedDescriptor {
        // SAFETY:
        // Safe because we own the SharedMemory at this point.
        unsafe { OwnedDescriptor::from_raw_descriptor(sm.into_raw_descriptor()) }
    }
}

/// Uses the system's page size in bytes to round the given value up to the nearest page boundary.
pub fn round_up_to_page_size(v: u64) -> Result<u64> {
    v.checked_next_multiple_of(page_size()? as _)
        .ok_or(Error::WithContext("rounding up caused overflow"))
}
