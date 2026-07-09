// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::path::Path;

use crate::util::AsBorrowedDescriptor;
use crate::util::Error;
use crate::util::OwnedDescriptor;
use crate::util::Result as MagmaGpuResult;
use crate::util::TubeType;

pub struct Tube;
pub struct Listener;

impl TryFrom<OwnedDescriptor> for Tube {
    type Error = Error;

    fn try_from(_socket: OwnedDescriptor) -> Result<Self, Self::Error> {
        Err(Error::Unsupported)
    }
}

impl Tube {
    pub fn new<P: AsRef<Path>>(_path: P, _kind: TubeType) -> MagmaGpuResult<Tube> {
        Err(Error::Unsupported)
    }

    pub fn send(
        &self,
        _opaque_data: &[u8],
        _descriptors: &[OwnedDescriptor],
    ) -> MagmaGpuResult<usize> {
        Err(Error::Unsupported)
    }

    pub fn receive(
        &self,
        _opaque_data: &mut [u8],
    ) -> MagmaGpuResult<(usize, Vec<OwnedDescriptor>)> {
        Err(Error::Unsupported)
    }
}

impl AsBorrowedDescriptor for Tube {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        unimplemented!()
    }
}

impl Listener {
    /// Creates a new `Listener` bound to the given path.
    pub fn bind<P: AsRef<Path>>(_path: P) -> MagmaGpuResult<Listener> {
        Err(Error::Unsupported)
    }

    pub fn accept(&self) -> MagmaGpuResult<Tube> {
        Err(Error::Unsupported)
    }
}

impl AsBorrowedDescriptor for Listener {
    fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        unimplemented!()
    }
}
