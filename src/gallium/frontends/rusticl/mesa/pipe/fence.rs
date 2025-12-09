// Copyright 2022 Red Hat.
// SPDX-License-Identifier: MIT

use crate::pipe::{context::PipeContext, screen::*};

use libc_rust_gen::close;
use mesa_rust_gen::*;
use mesa_rust_util::ptr::ThreadSafeCPtr;

use std::ffi::c_int;

pub struct FenceFd {
    pub fd: i32,
}

impl Drop for FenceFd {
    fn drop(&mut self) {
        unsafe {
            close(self.fd);
        }
    }
}

pub struct PipeFence {
    fence: ThreadSafeCPtr<pipe_fence_handle>,
    screen: PipeScreenOwned,
}

unsafe impl Send for PipeFence {}

impl PipeFence {
    pub fn new(fence: *mut pipe_fence_handle, screen: &PipeScreen) -> Option<Self> {
        Some(Self {
            fence: unsafe { ThreadSafeCPtr::new(fence)? },
            screen: screen.to_owned(),
        })
    }

    pub fn export_fd(&self) -> c_int {
        self.screen.fence_get_fd(self.fence.as_ptr())
    }

    pub fn gpu_signal(&self, ctx: &PipeContext) {
        debug_assert!(ctx.has_fence_server());
        unsafe {
            ctx.pipe().as_ref().fence_server_signal.unwrap()(
                ctx.pipe().as_ptr(),
                self.fence.as_ptr(),
                0,
            );
        }
    }

    pub fn gpu_wait(&self, ctx: &PipeContext) {
        debug_assert!(ctx.has_fence_server());
        unsafe {
            ctx.pipe().as_ref().fence_server_sync.unwrap()(
                ctx.pipe().as_ptr(),
                self.fence.as_ptr(),
                0,
            );
        }
    }

    /// Returns false on errors.
    ///
    /// TODO: should be a Result.
    pub fn wait(&self) -> bool {
        self.screen.fence_finish(self.fence.as_ptr())
    }
}

impl Drop for PipeFence {
    fn drop(&mut self) {
        self.screen.unref_fence(self.fence.as_ptr());
    }
}
