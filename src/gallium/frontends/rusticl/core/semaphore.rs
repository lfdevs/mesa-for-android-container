// Copyright 2025 Red Hat.
// SPDX-License-Identifier: MIT

use {
    crate::{
        api::icd::{CLObjectBase, CLResult, RusticlTypes},
        core::{
            context::Context,
            device::{Device, HelperContextWrapper},
            event::EventSig,
            queue::Queue,
        },
        impl_cl_type_trait,
    },
    mesa_rust::pipe::{
        context::PipeContext,
        fence::{FenceFd, PipeFence},
    },
    mesa_rust_gen::pipe_fd_type,
    mesa_rust_util::properties::MultiValProperties,
    rusticl_opencl_gen::*,
    std::{
        ffi::c_int,
        sync::{Arc, Condvar, Mutex, MutexGuard, Weak},
    },
};

pub enum SemaphoreHandle {
    SyncFD(c_int),
}

struct RealFence {
    fence: PipeFence,
    is_signalled: bool,
}

enum Fence {
    AlwaysSignalled,
    Fence(RealFence),
}

impl Fence {
    fn from_fence(fence: PipeFence) -> Self {
        Self::Fence(RealFence {
            fence: fence,
            is_signalled: false,
        })
    }
}

struct SemaphoreState {
    fence: Fence,
    last_queue: Option<Weak<Queue>>,
}

enum SemaphoreWaitAction {
    Signal,
    Wait,
    WaitImported,
}

impl SemaphoreState {
    fn gpu_signal(&mut self, ctx: &PipeContext) {
        match &mut self.fence {
            Fence::AlwaysSignalled => {}
            Fence::Fence(fence) => {
                fence.fence.gpu_signal(ctx);
                fence.is_signalled = true;
            }
        }
    }

    fn gpu_wait(&mut self, ctx: &PipeContext) {
        match &mut self.fence {
            Fence::AlwaysSignalled => {}
            Fence::Fence(fence) => {
                fence.fence.gpu_wait(ctx);
                fence.is_signalled = false
            }
        }
    }

    fn is_signalled(&self) -> bool {
        match &self.fence {
            Fence::AlwaysSignalled => true,
            Fence::Fence(fence) => fence.is_signalled,
        }
    }

    fn do_action(
        mut this: MutexGuard<Self>,
        cv: &Condvar,
        action: SemaphoreWaitAction,
        ctx: &PipeContext,
    ) -> CLResult<()> {
        // If we are always signalled we can just skip this call.
        if matches!(this.fence, Fence::AlwaysSignalled) {
            return Ok(());
        }

        // On imported waits we ignore the state of this fence as it might have been waited on
        // somewhere else and this is out of our control.
        if !matches!(action, SemaphoreWaitAction::WaitImported) {
            // We need to wait until is_signalled gets set to the proper state.
            let is_signalled_expected = !matches!(action, SemaphoreWaitAction::Signal);
            this = cv
                .wait_while(this, |state| state.is_signalled() != is_signalled_expected)
                .or(Err(CL_OUT_OF_HOST_MEMORY))?;
        }

        match action {
            // If this semaphore was already signalled, we need to wait until something
            // successfully waited on it before we can re-signal.
            SemaphoreWaitAction::Signal => this.gpu_signal(ctx),

            // We need to wait until something signals this semaphore before we can wait on it.
            SemaphoreWaitAction::Wait | SemaphoreWaitAction::WaitImported => this.gpu_wait(ctx),
        }

        drop(this);
        cv.notify_all();
        Ok(())
    }
}

/// Object representing a GPU semaphore that can be signalled and waited on. A semaphore is either
/// in a signalled or reset state and can only be waited on by a single consumer after which it
/// needs to be reset by a new call to gpu_signal.
pub struct Semaphore {
    pub base: CLObjectBase<CL_INVALID_SEMAPHORE_KHR>,
    pub ctx: Arc<Context>,
    pub props: MultiValProperties<cl_semaphore_properties_khr>,
    pub dev: &'static Device,
    pub handle_type: Option<cl_external_semaphore_handle_type_khr>,
    pub imported: bool,
    state: Mutex<SemaphoreState>,
    /// Condition Variable used for waiting on a gpu_signal or gpu_wait operation to executed on the
    /// queue thread.
    signal_cv: Condvar,
}

impl_cl_type_trait!(cl_semaphore_khr, Semaphore, CL_INVALID_SEMAPHORE_KHR);

impl Semaphore {
    fn create_semaphore(dev: &Device, handle: Option<SemaphoreHandle>) -> CLResult<Fence> {
        Ok(match handle {
            None => Fence::from_fence(
                dev.screen()
                    .create_semaphore()
                    .ok_or(CL_OUT_OF_HOST_MEMORY)?,
            ),
            Some(SemaphoreHandle::SyncFD(fd)) => {
                // The special value -1 for fd is treated like a valid sync file descriptor
                // referring to an object that has already signaled. The import operation will
                // succeed and the semaphore will have a temporarily imported payload as if a valid
                // file descriptor had been provided.
                if fd == -1 {
                    Fence::AlwaysSignalled
                } else {
                    // Importing a semaphore payload from a file descriptor transfers ownership of
                    // the file descriptor from the application to the OpenCL implementation. The
                    // application must not perform any operations on the file descriptor after a
                    // successful import.
                    let fence_fd = FenceFd { fd: fd };
                    let fence = dev
                        .helper_ctx()
                        .import_fence(&fence_fd, pipe_fd_type::PIPE_FD_TYPE_NATIVE_SYNC)?;
                    Fence::from_fence(fence)
                }
            }
        })
    }

    pub fn new(
        ctx: Arc<Context>,
        props: MultiValProperties<cl_semaphore_properties_khr>,
        dev: &'static Device,
        export_handle_type: Option<cl_external_semaphore_handle_type_khr>,
        handle: Option<SemaphoreHandle>,
    ) -> CLResult<Arc<Self>> {
        Ok(Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Semaphore),
            ctx: ctx,
            props: props,
            dev: dev,
            handle_type: export_handle_type,
            imported: handle.is_some(),
            state: Mutex::new(SemaphoreState {
                fence: Self::create_semaphore(dev, handle)?,
                last_queue: None,
            }),
            signal_cv: Condvar::new(),
        }))
    }

    pub fn fd(&self) -> CLResult<c_int> {
        let mut state = self.state();
        if !state.is_signalled() {
            // Some drivers might block until the semaphore was signalled, so we need to flush
            // the last queue the semaphore was used on.
            if let Some(queue) = state.last_queue.take() {
                if let Some(queue) = queue.upgrade() {
                    queue.flush(false).ok().ok_or(CL_OUT_OF_RESOURCES)?;
                }
            }

            // Now we need to wait until the semaphore gets signalled.
            state = self
                .signal_cv
                .wait_while(state, |state| !state.is_signalled())
                .or(Err(CL_OUT_OF_HOST_MEMORY))?;
        }

        Ok(match &mut state.fence {
            Fence::Fence(fence) => {
                // Do the spinny
                loop {
                    let fd = fence.fence.export_fd();
                    if fd != -1 {
                        // After exporting a semaphore it's safe to re-signal it.
                        fence.is_signalled = false;
                        break fd;
                    }
                }
            }
            Fence::AlwaysSignalled => -1,
        })
    }

    /// Makes the GPU signal the semaphore.
    pub fn gpu_signal(semas: Vec<Arc<Self>>, q: &Arc<Queue>) -> EventSig {
        for sema in &semas {
            let mut state = sema.state();
            if !matches!(state.fence, Fence::AlwaysSignalled) {
                state.last_queue = Some(Arc::downgrade(q));
            }
        }

        Box::new(move |_, ctx| {
            for sema in semas {
                SemaphoreState::do_action(
                    sema.state(),
                    &sema.signal_cv,
                    SemaphoreWaitAction::Signal,
                    ctx,
                )?;
            }
            Ok(())
        })
    }

    /// Makes the GPU wait on the semaphore to be signalled.
    pub fn gpu_wait(&self, ctx: &PipeContext) -> CLResult<()> {
        SemaphoreState::do_action(
            self.state(),
            &self.signal_cv,
            if self.imported {
                SemaphoreWaitAction::WaitImported
            } else {
                SemaphoreWaitAction::Wait
            },
            ctx,
        )
    }

    pub fn is_signalled(&self) -> bool {
        self.state().is_signalled()
    }

    pub fn reimport(&self, fd: c_int) -> CLResult<()> {
        let new_fence = Self::create_semaphore(self.dev, Some(SemaphoreHandle::SyncFD(fd)))?;
        self.state().fence = new_fence;
        Ok(())
    }

    fn state(&self) -> MutexGuard<'_, SemaphoreState> {
        self.state.lock().unwrap()
    }
}
