// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

//! Abstraction layer on top of pan_kmod and libdrm.
//!
//! TODO docs

use kraid_hw_runner_bindings::*;

use std::ffi::CStr;
use std::ops::Range;
use std::os::fd::{FromRawFd, OwnedFd};
use std::os::raw::c_void;
use std::ptr::{NonNull, null, null_mut};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;
use std::{fmt, io};

const MAP_FAILED: *mut c_void = -1isize as *mut _;

#[derive(Debug)]
pub enum HwError {
    OsError(io::Error),
    DeviceLost {
        fatal: bool,
        timeout: bool,
        innocent: bool,
        fatal_queues: u32,
    },
}

impl fmt::Display for HwError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            HwError::OsError(error) => error.fmt(f)?,
            HwError::DeviceLost {
                fatal,
                timeout,
                innocent,
                fatal_queues,
            } => {
                write!(f, "Device lost: ")?;
                if *fatal {
                    write!(f, "fatal, ")?;
                }
                if *timeout {
                    write!(f, "timeout, ")?;
                }
                if *innocent {
                    write!(f, "innocent, ")?;
                }
                write!(f, "fatal queues: {}", *fatal_queues)?;
            }
        };
        Ok(())
    }
}

impl std::error::Error for HwError {}

impl From<io::Error> for HwError {
    fn from(value: io::Error) -> Self {
        HwError::OsError(value)
    }
}

pub struct Device {
    dev: NonNull<pan_kmod_dev>,
    debug: Option<NonNull<pandecode_context>>,
}

impl Device {
    pub fn new(debug: bool) -> io::Result<Arc<Self>> {
        unsafe {
            let drm_fd = drmOpenWithType(
                c"panthor".as_ptr(),
                null(),
                DRM_NODE_RENDER as i32,
            );

            if drm_fd < 0 {
                return Err(io::Error::new(
                    io::ErrorKind::NotFound,
                    "Failed to find a panthor device",
                ));
            }

            let dev =
                pan_kmod_dev_create(drm_fd, PAN_KMOD_DEV_FLAG_OWNS_FD, null());

            let Some(dev) = NonNull::new(dev) else {
                drop(OwnedFd::from_raw_fd(drm_fd));
                return Err(io::Error::last_os_error());
            };

            let debug = debug.then(|| {
                NonNull::new(pandecode_create_context(true))
                    .expect("Failed to create debug context")
            });

            Ok(Arc::new(Device { dev, debug }))
        }
    }

    unsafe fn io_ctl(&self, request: u64, arg: *mut c_void) -> i32 {
        unsafe { drmIoctl(self.fd(), request, arg) }
    }

    pub fn user_va_range(&self) -> Range<u64> {
        let res = unsafe { pan_kmod_dev_query_user_va_range(self.kdev()) };

        res.start..(res.start + res.size)
    }

    pub fn props(&self) -> &pan_kmod_dev_props {
        unsafe { &self.dev.as_ref().props }
    }

    pub fn fd(&self) -> i32 {
        unsafe { self.dev.as_ref().fd }
    }

    pub fn kdev(&self) -> *mut pan_kmod_dev {
        self.dev.as_ptr()
    }
}

impl Drop for Device {
    fn drop(&mut self) {
        unsafe {
            if let Some(ctx) = self.debug {
                pandecode_destroy_context(ctx.as_ptr());
            }
            pan_kmod_dev_destroy(self.kdev());
        }
    }
}

pub struct VirtualMemory {
    dev: Arc<Device>,
    handle: NonNull<pan_kmod_vm>,
    start: u64,
    end: u64,
    next_addr: AtomicU64,
}

impl VirtualMemory {
    pub fn new(dev: Arc<Device>) -> io::Result<Self> {
        let usable_range = dev.user_va_range();

        let start = usable_range.start.max(32 * 1024 * 1024);
        let end = usable_range.end.min(1 << 32);
        let handle =
            unsafe { pan_kmod_vm_create(dev.kdev(), 0, start, end - start) };

        let Some(handle) = NonNull::new(handle) else {
            return Err(io::Error::last_os_error());
        };

        let next_addr = AtomicU64::new(start.next_multiple_of(0x1000));

        let mem = VirtualMemory {
            dev,
            handle,
            start,
            end,
            next_addr,
        };
        Ok(mem)
    }

    pub fn allocate_buffer(
        self: &Arc<VirtualMemory>,
        size: u64,
        label: &CStr,
        device_flags: u32,
    ) -> io::Result<MemoryBuffer> {
        let size = size.next_multiple_of(0x1000);

        let addr = self.next_addr.fetch_add(size, Ordering::Relaxed);
        assert!(self.start <= addr && addr < self.end);

        let mut buf = MemoryBuffer::new(self.clone(), size, device_flags)?;
        buf.set_label(label);
        buf.cpu_mmap((PROT_READ | PROT_WRITE) as i32, MAP_SHARED as i32)?;
        buf.gpu_bind_immediate(addr)?;
        buf.inject_debug(label);

        Ok(buf)
    }

    fn kvm(&self) -> *mut pan_kmod_vm {
        self.handle.as_ptr()
    }

    pub fn dev(&self) -> &Arc<Device> {
        &self.dev
    }

    pub fn handle(&self) -> u32 {
        unsafe { self.handle.as_ref().handle }
    }
}

impl Drop for VirtualMemory {
    fn drop(&mut self) {
        unsafe {
            pan_kmod_vm_destroy(self.kvm());
        }
    }
}

pub struct MemoryBuffer {
    mem: Arc<VirtualMemory>,
    handle: NonNull<pan_kmod_bo>,
    device_addr: u64,
    host_addr: *mut c_void,
    debug_inj: bool,
}

impl MemoryBuffer {
    pub fn new(
        mem: Arc<VirtualMemory>,
        size: u64,
        flags: u32,
    ) -> io::Result<Self> {
        let handle = unsafe {
            pan_kmod_bo_alloc(mem.dev.kdev(), mem.kvm(), size, flags)
        };
        let Some(handle) = NonNull::new(handle) else {
            return Err(io::Error::last_os_error());
        };

        Ok(Self {
            mem,
            handle,
            device_addr: 0,
            host_addr: null_mut(),
            debug_inj: false,
        })
    }

    fn set_label(&mut self, label: &CStr) {
        unsafe {
            pan_kmod_set_bo_label(
                self.mem.dev().kdev(),
                self.kbo(),
                label.as_ptr(),
            );
        }
    }

    fn cpu_mmap(&mut self, prot: i32, flags: i32) -> io::Result<()> {
        assert!(self.host_addr.is_null(), "Already mapped");
        let res =
            unsafe { pan_kmod_bo_mmap(self.kbo(), prot, flags, null_mut()) };
        if res == MAP_FAILED {
            return Err(io::Error::new(
                io::ErrorKind::OutOfMemory,
                "Failed to mmap BO",
            ));
        }
        self.host_addr = res;
        Ok(())
    }

    fn cpu_unmap(&mut self) {
        if !self.host_addr.is_null() {
            let res = unsafe { munmap(self.host_addr, self.size() as usize) };
            debug_assert_eq!(res, 0, "Failed to munmap BO");
            self.host_addr = null_mut();
        }
    }

    fn gpu_bind_immediate(&mut self, addr: u64) -> io::Result<()> {
        assert!(self.device_addr == 0, "Already bound");

        let vm_ops = [pan_kmod_vm_op {
            type_: PAN_KMOD_VM_OP_TYPE_MAP,
            va: pan_kmod_vm_op__bindgen_ty_1 {
                start: addr,
                size: self.size(),
            },
            __bindgen_anon_1: pan_kmod_vm_op__bindgen_ty_2 {
                map: pan_kmod_vm_op__bindgen_ty_2__bindgen_ty_1 {
                    bo: self.kbo(),
                    bo_offset: 0,
                },
            },
            signal: Default::default(),
            wait: Default::default(),
            flags: 0,
        }];

        let res = unsafe {
            pan_kmod_vm_bind(
                self.mem.kvm(),
                PAN_KMOD_VM_OP_MODE_IMMEDIATE,
                &vm_ops as *const _ as *mut _,
                vm_ops.len().try_into().unwrap(),
            )
        };

        if res != 0 {
            return Err(io::Error::new(
                io::ErrorKind::OutOfMemory,
                "Failed to bind BO",
            ));
        }
        self.device_addr = addr;
        Ok(())
    }

    fn gpu_unbind_immediate(&mut self) {
        if self.device_addr == 0 {
            return;
        }

        let vm_ops = [pan_kmod_vm_op {
            type_: PAN_KMOD_VM_OP_TYPE_UNMAP,
            va: pan_kmod_vm_op__bindgen_ty_1 {
                start: self.device_addr,
                size: self.size(),
            },
            __bindgen_anon_1: Default::default(),
            signal: Default::default(),
            wait: Default::default(),
            flags: 0,
        }];

        unsafe {
            let res = pan_kmod_vm_bind(
                self.mem.kvm(),
                PAN_KMOD_VM_OP_MODE_IMMEDIATE,
                &vm_ops as *const _ as *mut _,
                vm_ops.len().try_into().unwrap(),
            );
            debug_assert_eq!(res, 0, "Failed to unbind BO");
        }
        self.device_addr = 0;
    }

    fn inject_debug(&mut self, name: &CStr) {
        assert!(self.host_addr != null_mut() && self.device_addr != 0);
        if let Some(dctx) = &self.mem.dev().debug {
            unsafe {
                pandecode_inject_mmap(
                    dctx.as_ptr(),
                    self.device_addr,
                    self.host_addr,
                    self.size() as u32,
                    name.as_ptr(),
                );
            }
            self.debug_inj = true;
        }
    }

    pub fn sync(&self) {
        unsafe {
            pan_kmod_queue_bo_map_sync(
                self.kbo(),
                0,
                self.host_addr,
                self.size(),
                PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE,
            );
            pan_kmod_flush_bo_map_syncs(self.mem.dev().kdev());
        }
    }

    pub fn host_addr(&self) -> *mut c_void {
        self.host_addr
    }

    pub fn device_addr(&self) -> u64 {
        self.device_addr
    }

    pub fn size(&self) -> u64 {
        unsafe { self.handle.as_ref().size }
    }

    fn kbo(&self) -> *mut pan_kmod_bo {
        self.handle.as_ptr()
    }
}

impl Drop for MemoryBuffer {
    fn drop(&mut self) {
        unsafe {
            if self.debug_inj {
                let dctx = &self.mem.dev().debug.unwrap();

                pandecode_inject_free(
                    dctx.as_ptr(),
                    self.device_addr,
                    self.size() as u32,
                );
            }

            self.gpu_unbind_immediate();
            self.cpu_unmap();

            pan_kmod_bo_put(self.kbo());
        }
    }
}

fn drm_array<T>(arr: &[T]) -> drm_panthor_obj_array {
    drm_panthor_obj_array {
        stride: size_of::<T>().try_into().unwrap(),
        count: arr.len().try_into().unwrap(),
        array: arr.as_ptr() as u64,
    }
}

/* Both a CSF Group and a submit queue actually */
pub struct SubmitGroup {
    vm: Arc<VirtualMemory>,
    handle: u32,
}

impl SubmitGroup {
    pub fn new(vm: &Arc<VirtualMemory>) -> io::Result<Self> {
        let vm = vm.clone();
        let shader_present = vm.dev.props().shader_present;

        let queues = [drm_panthor_queue_create {
            priority: 1,
            ringbuf_size: 64 * 1024,
            ..Default::default()
        }];

        let mut args = drm_panthor_group_create {
            queues: drm_array(&queues),
            max_compute_cores: shader_present.count_ones().try_into().unwrap(),
            max_fragment_cores: 0,
            max_tiler_cores: 0,
            priority: PANTHOR_GROUP_PRIORITY_MEDIUM as u8,
            compute_core_mask: shader_present,
            fragment_core_mask: 0,
            tiler_core_mask: 0,
            vm_id: vm.handle(),
            group_handle: 0,
            ..Default::default()
        };

        let res = unsafe {
            let io_args = &mut args as *mut _ as *mut c_void;
            vm.dev()
                .io_ctl(DRM_IOCTL_PANTHOR_GROUP_CREATE.into(), io_args)
        };
        assert_eq!(res, 0, "Failed to initialize Group");
        let handle = args.group_handle;

        Ok(SubmitGroup { vm, handle })
    }

    pub fn check_state(&self) -> Result<(), HwError> {
        let mut args = drm_panthor_group_get_state {
            group_handle: self.handle,
            ..Default::default()
        };
        let res = unsafe {
            let kargs = &mut args as *mut _ as *mut c_void;
            self.vm
                .dev()
                .io_ctl(DRM_IOCTL_PANTHOR_GROUP_GET_STATE.into(), kargs)
        };
        if res == 0 && args.state == 0 {
            return Ok(());
        }
        if res != 0 {
            return Err(io::Error::last_os_error().into());
        }
        let fatal = (args.state & DRM_PANTHOR_GROUP_STATE_FATAL_FAULT) != 0;
        let timeout = (args.state & DRM_PANTHOR_GROUP_STATE_TIMEDOUT) != 0;
        let innocent = (args.state & DRM_PANTHOR_GROUP_STATE_INNOCENT) != 0;

        Err(HwError::DeviceLost {
            fatal,
            timeout,
            innocent,
            fatal_queues: args.fatal_queues,
        })
    }

    pub fn submit(
        &self,
        stream_addr: u64,
        stream_size: u32,
        syncs: &[drm_panthor_sync_op],
    ) -> io::Result<()> {
        let submits = [drm_panthor_queue_submit {
            queue_index: 0,
            stream_addr,
            stream_size,
            latest_flush: 0,
            syncs: drm_array(syncs),
            ..Default::default()
        }];

        let mut args = drm_panthor_group_submit {
            group_handle: self.handle,
            queue_submits: drm_array(&submits),
            ..Default::default()
        };
        if let Some(dctx) = self.vm.dev.debug {
            unsafe {
                eprintln!("\nCS binary:\n");
                pandecode_cs_binary(
                    dctx.as_ptr(),
                    stream_addr,
                    stream_size,
                    self.vm.dev.props().gpu_id,
                );
            }
        }
        let res = unsafe {
            let kargs = &mut args as *mut _ as *mut c_void;
            self.vm
                .dev()
                .io_ctl(DRM_IOCTL_PANTHOR_GROUP_SUBMIT.into(), kargs)
        };
        if res != 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }
}

impl Drop for SubmitGroup {
    fn drop(&mut self) {
        let mut args = drm_panthor_group_destroy {
            group_handle: self.handle,
            pad: 0,
        };
        unsafe {
            let kargs = &mut args as *mut _ as *mut c_void;
            let res = self
                .vm
                .dev()
                .io_ctl(DRM_IOCTL_PANTHOR_GROUP_DESTROY.into(), kargs);
            assert_eq!(res, 0, "Failed to destroy Group");
        }
    }
}

pub struct TimelineSyncobj {
    dev: Arc<Device>,
    handle: u32,
    value: AtomicU64,
}

impl TimelineSyncobj {
    pub fn new(dev: Arc<Device>) -> io::Result<Self> {
        let mut handle = 0u32;
        let res = unsafe {
            drmSyncobjCreate(dev.fd(), DRM_SYNCOBJ_CREATE_SIGNALED, &mut handle)
        };
        if res != 0 {
            return Err(io::Error::last_os_error());
        };
        Ok(TimelineSyncobj {
            dev,
            handle,
            value: AtomicU64::new(0),
        })
    }

    pub fn signal(&self) -> TimelineMoment<'_> {
        TimelineMoment {
            timeline: self,
            value: self.value.fetch_add(1, Ordering::SeqCst) + 1,
        }
    }
}

#[derive(Clone, Copy)]
pub struct TimelineMoment<'a> {
    timeline: &'a TimelineSyncobj,
    value: u64,
}

impl TimelineMoment<'_> {
    pub fn wait(&self, timeout: Duration) -> io::Result<()> {
        let res = unsafe {
            drmSyncobjTimelineWait(
                self.timeline.dev.fd(),
                &self.timeline.handle as *const _ as *mut _,
                &self.value as *const _ as *mut _,
                1,
                timeout.as_nanos().min(i64::MAX as u128) as i64,
                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                null_mut(),
            )
        };
        if res == -(ETIME as i32) {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                "Timeline syncobj timed out",
            ));
        }
        if res < 0 {
            return Err(io::Error::last_os_error());
        };
        Ok(())
    }
}

impl From<TimelineMoment<'_>> for drm_panthor_sync_op {
    fn from(value: TimelineMoment) -> Self {
        let flags = (DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
            | DRM_PANTHOR_SYNC_OP_SIGNAL) as u32;

        drm_panthor_sync_op {
            flags,
            handle: value.timeline.handle,
            timeline_value: value.value,
        }
    }
}

impl Drop for TimelineSyncobj {
    fn drop(&mut self) {
        let res = unsafe { drmSyncobjDestroy(self.dev.fd(), self.handle) };
        assert_eq!(res, 0, "Failed to destroy syncobj")
    }
}
