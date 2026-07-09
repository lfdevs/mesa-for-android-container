// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::os::fd::AsFd;

use rustix::io::read;
use rustix::io::write;
use rustix::pipe::pipe;

use crate::util::AsBorrowedDescriptor;
use crate::util::AsRawDescriptor;
use crate::util::FromRawDescriptor;
use crate::util::OwnedDescriptor;
use crate::util::RawDescriptor;
use crate::util::Result;

pub struct ReadPipe {
    descriptor: OwnedDescriptor,
}

pub struct WritePipe {
    descriptor: OwnedDescriptor,
}

pub fn create_pipe() -> Result<(ReadPipe, WritePipe)> {
    let (read_pipe, write_pipe) = pipe()?;
    Ok((
        ReadPipe {
            descriptor: read_pipe.into(),
        },
        WritePipe {
            descriptor: write_pipe.into(),
        },
    ))
}

impl ReadPipe {
    pub fn read(&self, data: &mut [u8]) -> Result<usize> {
        let bytes_read = read(&self.descriptor, data)?;
        Ok(bytes_read)
    }
}

impl AsBorrowedDescriptor for ReadPipe {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        &self.descriptor
    }
}

impl WritePipe {
    pub fn new(descriptor: RawDescriptor) -> WritePipe {
        // SAFETY: Safe because we know the underlying OS descriptor is valid and
        // owned by us.
        let owned = unsafe { OwnedDescriptor::from_raw_descriptor(descriptor) };
        WritePipe { descriptor: owned }
    }

    pub fn write(&self, data: &[u8]) -> Result<usize> {
        let bytes_written = write(self.descriptor.as_fd(), data)?;
        Ok(bytes_written)
    }
}

impl AsBorrowedDescriptor for WritePipe {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        &self.descriptor
    }
}

impl AsRawDescriptor for WritePipe {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        self.descriptor.as_raw_descriptor()
    }
}
