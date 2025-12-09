// Copyright 2025 Seán de Búrca.
// SPDX-License-Identifier: MIT

//! An abstraction of mesa's `util_queue` and related primitives for
//! asynchronous, queue-based execution of arbitrary jobs.

use std::{cell::UnsafeCell, ffi::CStr, pin::Pin};

use mesa_rust_gen::{
    util_queue, util_queue_add_job, util_queue_destroy, util_queue_fence, util_queue_fence_destroy,
    util_queue_fence_init, util_queue_fence_wait, util_queue_finish, util_queue_init,
};

/// A threaded job queue.
pub struct Queue {
    inner: Pin<Box<UnsafeCell<util_queue>>>,
}

// SAFETY: `util_queue` doesn't use any thread-local storage.
unsafe impl Send for Queue {}

// SAFETY: `util_queue` functionality is mediated by a mutex.
unsafe impl Sync for Queue {}

impl Queue {
    /// Creates a new job queue.
    ///
    /// The name of the queue will be truncated to 14 bytes.
    pub fn new(name: &CStr, max_jobs: u32, num_threads: u32) -> Self {
        // SAFETY: `queue` and `name` are both valid pointers of the appropriate
        // type, and `global_data` may be null per implementation.
        let queue = Box::pin(UnsafeCell::new(util_queue::default()));
        unsafe {
            util_queue_init(
                queue.get(),
                name.as_ptr(),
                max_jobs,
                num_threads,
                0,
                std::ptr::null_mut(),
            )
        };

        Self { inner: queue }
    }

    /// Adds a job to the queue to be executed asynchronously.
    pub fn add_job<F>(&self, func: F)
    where
        F: FnMut() + Send + Sync + 'static,
    {
        // SAFETY: The fence parameter may be a null pointer.
        unsafe { self.do_add_job(func, std::ptr::null_mut()) };
    }

    /// Adds a job to the queue to be executed synchronously.
    ///
    /// When `func` finishes executing, the returned fence will be signaled.
    pub fn add_job_sync<F>(&self, func: F) -> Fence
    where
        F: FnMut() + Send + Sync + 'static,
    {
        let fence = Fence::new();

        // SAFETY: `fence` is a valid pointer to a fence.
        unsafe { self.do_add_job(func, fence.inner.get()) };

        fence
    }

    /// Adds a job to the queue.
    ///
    /// # Safety
    ///
    /// `fence` must either be a null pointer or a valid pointer to a fence.
    unsafe fn do_add_job<F>(&self, func: F, fence: *mut util_queue_fence)
    where
        F: FnMut() + Send + Sync + 'static,
    {
        // SAFETY: The queue is valid so long as it is only destroyed on drop.
        // We uphold the safety requirements of `exec_rust_job` by specifying
        // `F` matching the type of `func` and passing `func` as a raw pointer
        // to it. `fence` cannot be dropped without first being signaled,
        // meaning it will be valid for the life of the job in the queue.
        unsafe {
            util_queue_add_job(
                self.inner.get(),
                Box::into_raw(Box::new(func)).cast(),
                fence,
                Some(exec_rust_job::<F>),
                None,
                0,
            )
        };
    }
}

impl Drop for Queue {
    fn drop(&mut self) {
        let inner = self.inner.get();

        // SAFETY: `inner` is a valid pointer to a queue so long as no other
        // code destroys it.
        unsafe { util_queue_finish(inner) };
        unsafe { util_queue_destroy(inner) };
    }
}

/// Executes a Rust closure as a job in a worker queue.
///
/// Not intended for general use. See [`Queue::add_job`].
///
/// # Safety
///
/// `data` must be a valid pointer to a Rust closure of type `F`.
unsafe extern "C" fn exec_rust_job<F>(data: *mut std::ffi::c_void, _: *mut std::ffi::c_void, _: i32)
where
    F: FnMut() + Send + Sync + 'static,
{
    // SAFETY: The caller must uphold that `data` is valid for casting to `F`.
    let func: &mut F = unsafe { &mut *(data.cast()) };

    func();
}

// SAFETY: `util_queue_fence` value is read atomically by the appropriate
// functions. Its value _must not_ be read directly.
unsafe impl Send for Fence {}
unsafe impl Sync for Fence {}

/// A fence for signaling or awaiting asynchronous jobs.
#[clippy::has_significant_drop]
pub struct Fence {
    inner: Pin<Box<UnsafeCell<util_queue_fence>>>,
}

impl Fence {
    /// Creates a new fence.
    ///
    /// A `Fence` _must_ be signaled before it is dropped, otherwise droppign
    /// will block forever. This protects against use-after-free if the `Fence`
    /// is passed to a [`Queue`].
    #[must_use = "fences must be signaled before dropping"]
    fn new() -> Self {
        // SAFETY: `fence` is a valid pointer to a `util_queue_fence`.
        let fence = Box::pin(UnsafeCell::new(util_queue_fence::default()));
        unsafe { util_queue_fence_init(fence.get()) };

        Self { inner: fence }
    }

    /// Waits synchronously for the fence to be signaled.
    pub fn wait(&self) {
        // SAFETY: `inner` is a valid pointer to a fence so long as it is only
        // destroyed on drop.
        unsafe { util_queue_fence_wait(self.inner.get()) };
    }
}

impl Drop for Fence {
    fn drop(&mut self) {
        // Ensure that a fence can't be dropped without first being signaled.
        // This prevents use-after-free if the fence is passed to a queue.
        self.wait();

        // SAFETY: `inner` is a valid pointer to a fence so long as no other
        // code destroys it.
        unsafe { util_queue_fence_destroy(self.inner.get()) };
    }
}
