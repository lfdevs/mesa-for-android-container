// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::ffi::CStr;

use crate::util::descriptor::AsRawDescriptor;
use crate::util::descriptor::IntoRawDescriptor;
use crate::util::Error;
use crate::util::OwnedDescriptor;
use crate::util::RawDescriptor;
use crate::util::Result;

/// A shared memory file descriptor and its size.
pub struct SharedMemory {
    pub descriptor: OwnedDescriptor,
    pub size: u64,
}

impl SharedMemory {
    /// Creates a new shared memory file mapping with zero size.
    pub fn new(_debug_name: &CStr, _size: u64) -> Result<Self> {
        Err(Error::Unsupported)
    }

    /// Gets the size in bytes of the shared memory.
    ///
    /// The size returned here does not reflect changes by other interfaces or users of the shared
    /// memory file descriptor.
    pub fn size(&self) -> u64 {
        self.size
    }
}

/// USE THIS CAUTIOUSLY. The returned handle is not a file handle and cannot be
/// used as if it were one. It is a handle to a the associated file mapping object
/// and should only be used for memory-mapping the file view.
impl AsRawDescriptor for SharedMemory {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        self.descriptor.as_raw_descriptor()
    }
}

impl IntoRawDescriptor for SharedMemory {
    fn into_raw_descriptor(self) -> RawDescriptor {
        self.descriptor.into_raw_descriptor()
    }
}

pub fn page_size() -> Result<u64> {
    // TODO: rustix ideally should support this using GetSystemInfo(..)
    Err(Error::Unsupported)
}
