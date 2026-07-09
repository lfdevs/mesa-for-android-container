// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use crate::util::Error;
use crate::util::OwnedDescriptor;
use crate::util::Result;
use crate::util::WaitEvent;
use crate::util::WaitTimeout;

pub struct WaitContext;

impl WaitContext {
    pub fn new() -> Result<WaitContext> {
        Err(Error::Unsupported)
    }

    pub fn add(&mut self, _connection_id: u64, _descriptor: &OwnedDescriptor) -> Result<()> {
        Err(Error::Unsupported)
    }

    pub fn wait(&mut self, _timeout: WaitTimeout) -> Result<Vec<WaitEvent>> {
        Err(Error::Unsupported)
    }

    pub fn delete(&mut self, _descriptor: &OwnedDescriptor) -> Result<()> {
        Err(Error::Unsupported)
    }
}
