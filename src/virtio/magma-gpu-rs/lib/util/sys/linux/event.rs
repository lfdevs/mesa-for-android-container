// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::os::fd::OwnedFd;

use rustix::event::eventfd;
use rustix::event::EventfdFlags;
use rustix::io::read;
use rustix::io::write;

use crate::util::AsBorrowedDescriptor;
use crate::util::Error;
use crate::util::Handle;
use crate::util::OwnedDescriptor;
use crate::util::Result as MagmaGpuResult;
use crate::util::MAGMA_GPU_HANDLE_TYPE_SIGNAL_EVENT_FD;

pub struct Event {
    descriptor: OwnedDescriptor,
}

impl Event {
    pub fn new() -> MagmaGpuResult<Event> {
        let owned: OwnedFd = eventfd(0, EventfdFlags::empty())?;
        Ok(Event {
            descriptor: owned.into(),
        })
    }

    pub fn add(&mut self, value: u64) -> MagmaGpuResult<()> {
        let _ = write(&self.descriptor, &value.to_le_bytes())?;
        Ok(())
    }

    pub fn signal(&mut self) -> MagmaGpuResult<()> {
        self.add(1)
    }

    pub fn wait(&mut self) -> MagmaGpuResult<u64> {
        let mut buf = [0; 8];
        read(&self.descriptor, &mut buf)?;
        Ok(u64::from_le_bytes(buf))
    }

    pub fn try_clone(&self) -> MagmaGpuResult<Event> {
        let clone = self.descriptor.try_clone()?;
        Ok(Event { descriptor: clone })
    }
}

impl TryFrom<Handle> for Event {
    type Error = Error;
    fn try_from(handle: Handle) -> Result<Self, Self::Error> {
        if handle.handle_type != MAGMA_GPU_HANDLE_TYPE_SIGNAL_EVENT_FD {
            return Err(Error::InvalidMagmaHandle);
        }

        Ok(Event {
            descriptor: handle.os_handle,
        })
    }
}

impl From<Event> for Handle {
    fn from(evt: Event) -> Self {
        Handle {
            os_handle: evt.descriptor,
            handle_type: MAGMA_GPU_HANDLE_TYPE_SIGNAL_EVENT_FD,
        }
    }
}

impl AsBorrowedDescriptor for Event {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        &self.descriptor
    }
}
