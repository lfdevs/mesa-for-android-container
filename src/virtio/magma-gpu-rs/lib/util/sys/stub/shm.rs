// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::ffi::CStr;

use crate::util::descriptor::AsRawDescriptor;
use crate::util::descriptor::IntoRawDescriptor;
use crate::util::Error;
use crate::util::RawDescriptor;
use crate::util::Result;

pub struct SharedMemory {
    size: u64,
}

impl SharedMemory {
    /// Creates a new shared memory file descriptor with zero size.
    pub fn new(_debug_name: &CStr, _size: u64) -> Result<SharedMemory> {
        Err(Error::Unsupported)
    }

    /// Gets the size in bytes of the shared memory.
    ///
    /// The size returned here does not reflect changes by other interfaces or users of the shared
    /// memory file descriptor..
    pub fn size(&self) -> u64 {
        self.size
    }
}

impl AsRawDescriptor for SharedMemory {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        unimplemented!()
    }
}

impl IntoRawDescriptor for SharedMemory {
    fn into_raw_descriptor(self) -> RawDescriptor {
        unimplemented!()
    }
}

pub fn page_size() -> Result<u64> {
    Err(Error::Unsupported)
}
