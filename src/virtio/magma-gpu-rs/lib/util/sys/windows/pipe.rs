// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use crate::util::AsBorrowedDescriptor;
use crate::util::AsRawDescriptor;
use crate::util::Error;
use crate::util::OwnedDescriptor;
use crate::util::RawDescriptor;
use crate::util::Result;

pub struct ReadPipe;
pub struct WritePipe;

pub fn create_pipe() -> Result<(ReadPipe, WritePipe)> {
    Err(Error::Unsupported)
}

impl ReadPipe {
    pub fn read(&self, _data: &mut [u8]) -> Result<usize> {
        Err(Error::Unsupported)
    }
}

impl AsBorrowedDescriptor for ReadPipe {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        unimplemented!()
    }
}

impl WritePipe {
    pub fn new(_descriptor: RawDescriptor) -> WritePipe {
        unimplemented!()
    }

    pub fn write(&self, _data: &[u8]) -> Result<usize> {
        Err(Error::Unsupported)
    }
}

impl AsBorrowedDescriptor for WritePipe {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        unimplemented!()
    }
}

impl AsRawDescriptor for WritePipe {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        unimplemented!()
    }
}
