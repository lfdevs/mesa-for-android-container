// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use crate::util::AsBorrowedDescriptor;
use crate::util::Error;
use crate::util::Handle;
use crate::util::OwnedDescriptor;
use crate::util::Result as MagmaGpuResult;

pub struct Event;

impl Event {
    pub fn new() -> MagmaGpuResult<Event> {
        Err(Error::Unsupported)
    }

    pub fn add(&mut self, _value: u64) -> MagmaGpuResult<()> {
        Err(Error::Unsupported)
    }

    pub fn signal(&mut self) -> MagmaGpuResult<()> {
        Err(Error::Unsupported)
    }

    pub fn wait(&mut self) -> MagmaGpuResult<u64> {
        Err(Error::Unsupported)
    }

    pub fn try_clone(&self) -> MagmaGpuResult<Event> {
        Err(Error::Unsupported)
    }
}

impl TryFrom<Handle> for Event {
    type Error = Error;
    fn try_from(_handle: Handle) -> Result<Self, Self::Error> {
        Err(Error::Unsupported)
    }
}

impl From<Event> for Handle {
    fn from(_evt: Event) -> Self {
        unimplemented!()
    }
}

impl AsBorrowedDescriptor for Event {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        unimplemented!()
    }
}
