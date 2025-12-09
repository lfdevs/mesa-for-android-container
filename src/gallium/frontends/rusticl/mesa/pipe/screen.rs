// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use crate::compiler::nir::NirShader;
use crate::pipe::context::*;
use crate::pipe::device::*;
use crate::pipe::fence::PipeFence;
use crate::pipe::resource::*;
use crate::util::disk_cache::*;

use mesa_rust_gen::*;
use mesa_rust_util::has_required_feature;
use mesa_rust_util::ptr::ThreadSafeCPtr;
use mesa_rust_util::static_assert;

use std::borrow::Borrow;
use std::ffi::c_int;
use std::ffi::CStr;
use std::mem;
use std::num::NonZeroU64;
use std::ops::Deref;
use std::os::raw::c_schar;
use std::os::raw::c_uchar;
use std::os::raw::c_void;
use std::ptr;
use std::ptr::NonNull;
use std::sync::atomic::AtomicI32;
use std::sync::atomic::Ordering;

#[derive(PartialEq)]
pub struct PipeScreenWithLdev {
    ldev: PipeLoaderDevice,
    screen: PipeScreenOwned,
}

#[derive(PartialEq)]
#[repr(transparent)]
pub struct PipeScreenOwned {
    screen: ThreadSafeCPtr<pipe_screen>,
}

#[repr(transparent)]
pub struct PipeScreen(pipe_screen);

pub const UUID_SIZE: usize = PIPE_UUID_SIZE as usize;
const LUID_SIZE: usize = PIPE_LUID_SIZE as usize;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ResourceType {
    Immutable,
    Normal,
    Staging,
}

impl ResourceType {
    fn apply(&self, tmpl: &mut pipe_resource) {
        match self {
            Self::Staging => {
                tmpl.set_usage(pipe_resource_usage::PIPE_USAGE_STAGING);
                tmpl.flags |= PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT;
                tmpl.bind |= PIPE_BIND_LINEAR;
            }
            Self::Normal => {}
            Self::Immutable => {
                tmpl.set_usage(pipe_resource_usage::PIPE_USAGE_IMMUTABLE);
            }
        }
    }
}

pub struct ScreenVMAllocation<'a> {
    screen: &'a PipeScreen,
    alloc: NonNull<pipe_vm_allocation>,
}

impl Drop for ScreenVMAllocation<'_> {
    fn drop(&mut self) {
        if let Some(free_vm) = self.screen.screen().free_vm {
            unsafe {
                free_vm(self.screen.pipe(), self.alloc.as_ptr());
            }
        }
    }
}

/// A PipeScreen wrapper also containing an owned PipeLoaderDevice reference.
///
/// TODO: This exist purely for convenience reasons and we might want to split those objects
/// properly.
impl PipeScreenWithLdev {
    pub(super) fn new(ldev: PipeLoaderDevice, screen: *mut pipe_screen) -> Option<Self> {
        if screen.is_null() || !has_required_cbs(screen) {
            return None;
        }

        let screen = Self {
            ldev: ldev,
            // SAFETY: `pipe_screen` is considered a thread-safe type
            screen: PipeScreenOwned {
                screen: unsafe { ThreadSafeCPtr::new(screen)? },
            },
        };

        // We use SeqCst here as refcnt might be accessed behind a mutex.
        screen.refcnt().store(1, Ordering::SeqCst);

        Some(screen)
    }

    pub fn driver_name(&self) -> &CStr {
        self.ldev.driver_name()
    }

    pub fn device_type(&self) -> pipe_loader_device_type {
        self.ldev.device_type()
    }
}

impl Deref for PipeScreenWithLdev {
    type Target = PipeScreenOwned;

    fn deref(&self) -> &Self::Target {
        &self.screen
    }
}

impl Borrow<PipeScreen> for PipeScreenOwned {
    fn borrow(&self) -> &PipeScreen {
        // SAFETY: PipeScreen is transparent over pipe_screen, so we can convert a &pipe_screen to
        //         &PipeScreen.
        unsafe { mem::transmute(self.screen) }
    }
}

impl Deref for PipeScreenOwned {
    type Target = PipeScreen;

    fn deref(&self) -> &Self::Target {
        self.borrow()
    }
}

impl Drop for PipeScreenOwned {
    fn drop(&mut self) {
        if self.refcnt().fetch_sub(1, Ordering::SeqCst) == 1 {
            unsafe { self.screen().destroy.unwrap()(self.pipe()) }
        }
    }
}

impl PipeScreenOwned {
    /// Turns a raw pointer into an owned reference.
    ///
    /// # Safety
    ///
    /// `screen` must be equivalent to a pointer retrieved via [PipeScreenOwned::into_raw].
    /// This function does not increase reference count; use with a pointer not accounted
    /// for in the reference count could lead to undefined behavior.
    pub(super) unsafe fn from_raw<'s>(screen: *mut pipe_screen) -> Self {
        // SAFETY: PipeScreenOwned is transparent over *mut pipe_screen
        unsafe { mem::transmute(screen) }
    }

    /// Turns self into a raw pointer leaking the reference count.
    pub(super) fn into_raw(self) -> *mut pipe_screen {
        // SAFETY: PipeScreenOwned is transparent over *mut pipe_screen
        unsafe { mem::transmute(self) }
    }
}

impl PipeScreen {
    fn screen(&self) -> &pipe_screen {
        &self.0
    }

    pub(super) fn pipe(&self) -> *mut pipe_screen {
        // screen methods are all considered thread safe, so we can just pass the mut pointer
        // around.
        ((&self.0) as *const pipe_screen).cast_mut()
    }

    fn refcnt(&self) -> &AtomicI32 {
        static_assert!(mem::align_of::<i32>() >= mem::align_of::<AtomicI32>());

        let refcnt: *const _ = &self.screen().refcnt;

        // SAFETY: refcnt is supposed to be atomically accessed
        unsafe { AtomicI32::from_ptr(refcnt.cast_mut()) }
    }

    pub(super) fn from_raw<'s>(screen: &'s *mut pipe_screen) -> &'s Self {
        unsafe { mem::transmute(*screen) }
    }

    pub fn caps(&self) -> &pipe_caps {
        &self.screen().caps
    }

    pub(super) fn create_context(&self, prio: PipeContextPrio) -> *mut pipe_context {
        let flags: u32 = prio.into();
        unsafe {
            self.screen().context_create.unwrap()(
                self.pipe(),
                ptr::null_mut(),
                flags | PIPE_CONTEXT_COMPUTE_ONLY | PIPE_CONTEXT_NO_LOD_BIAS,
            )
        }
    }

    pub fn alloc_vm(&self, start: NonZeroU64, size: NonZeroU64) -> Option<ScreenVMAllocation<'_>> {
        let alloc = unsafe { self.screen().alloc_vm?(self.pipe(), start.get(), size.get()) };
        Some(ScreenVMAllocation {
            screen: self,
            alloc: NonNull::new(alloc)?,
        })
    }

    pub fn resource_assign_vma(&self, res: &PipeResourceOwned, address: u64) -> bool {
        if let Some(resource_assign_vma) = self.screen().resource_assign_vma {
            // Validate that we already acquired the vm range
            if cfg!(debug_assertions) {
                if let Some(address) = NonZeroU64::new(address) {
                    debug_assert!(self
                        .alloc_vm(address, NonZeroU64::new(1).unwrap())
                        .is_none());
                }
            }
            unsafe { resource_assign_vma(self.pipe(), res.pipe(), address) }
        } else {
            false
        }
    }

    fn resource_create(&self, tmpl: &pipe_resource) -> Option<PipeResourceOwned> {
        PipeResourceOwned::new(
            unsafe { self.screen().resource_create.unwrap()(self.pipe(), tmpl) },
            false,
        )
    }

    fn resource_create_from_user(
        &self,
        tmpl: &pipe_resource,
        mem: *mut c_void,
    ) -> Option<PipeResourceOwned> {
        PipeResourceOwned::new(
            unsafe { self.screen().resource_from_user_memory?(self.pipe(), tmpl, mem) },
            true,
        )
    }

    pub fn resource_create_buffer(
        &self,
        size: u32,
        res_type: ResourceType,
        pipe_bind: u32,
        pipe_flags: u32,
    ) -> Option<PipeResourceOwned> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(pipe_texture_target::PIPE_BUFFER);
        tmpl.width0 = size;
        tmpl.height0 = 1;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = pipe_bind;
        tmpl.flags = pipe_flags;

        res_type.apply(&mut tmpl);

        self.resource_create(&tmpl)
    }

    pub fn resource_create_buffer_from_user(
        &self,
        size: u32,
        mem: *mut c_void,
        pipe_bind: u32,
        pipe_flags: u32,
    ) -> Option<PipeResourceOwned> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(pipe_texture_target::PIPE_BUFFER);
        tmpl.width0 = size;
        tmpl.height0 = 1;
        tmpl.depth0 = 1;
        tmpl.array_size = 1;
        tmpl.bind = pipe_bind;
        tmpl.flags = pipe_flags;

        self.resource_create_from_user(&tmpl, mem)
    }

    pub fn resource_create_texture(
        &self,
        width: u32,
        height: u32,
        depth: u16,
        array_size: u16,
        target: pipe_texture_target,
        format: pipe_format,
        res_type: ResourceType,
        support_image: bool,
    ) -> Option<PipeResourceOwned> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;
        tmpl.bind = PIPE_BIND_SAMPLER_VIEW;

        if support_image {
            tmpl.bind |= PIPE_BIND_SHADER_IMAGE;
        }

        res_type.apply(&mut tmpl);

        self.resource_create(&tmpl)
    }

    pub fn resource_create_texture_from_user(
        &self,
        width: u32,
        height: u32,
        depth: u16,
        array_size: u16,
        target: pipe_texture_target,
        format: pipe_format,
        mem: *mut c_void,
        support_image: bool,
    ) -> Option<PipeResourceOwned> {
        let mut tmpl = pipe_resource::default();

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;
        tmpl.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_LINEAR;

        if support_image {
            tmpl.bind |= PIPE_BIND_SHADER_IMAGE;
        }

        self.resource_create_from_user(&tmpl, mem)
    }

    pub fn resource_import_dmabuf(
        &self,
        handle: u32,
        modifier: u64,
        target: pipe_texture_target,
        format: pipe_format,
        stride: u32,
        width: u32,
        height: u32,
        depth: u16,
        array_size: u16,
        support_image: bool,
    ) -> Option<PipeResourceOwned> {
        let mut tmpl = pipe_resource::default();
        let mut handle = winsys_handle {
            type_: WINSYS_HANDLE_TYPE_FD,
            handle: handle,
            modifier: modifier,
            format: format as u64,
            stride: stride,
            ..Default::default()
        };

        tmpl.set_target(target);
        tmpl.set_format(format);
        tmpl.width0 = width;
        tmpl.height0 = height;
        tmpl.depth0 = depth;
        tmpl.array_size = array_size;

        if target == pipe_texture_target::PIPE_BUFFER {
            tmpl.bind = PIPE_BIND_GLOBAL
        } else {
            tmpl.bind = PIPE_BIND_SAMPLER_VIEW;
            if support_image {
                tmpl.bind |= PIPE_BIND_SHADER_IMAGE;
            }
        }

        unsafe {
            PipeResourceOwned::new(
                self.screen().resource_from_handle.unwrap()(self.pipe(), &tmpl, &mut handle, 0),
                false,
            )
        }
    }

    pub fn shader_caps(&self, t: mesa_shader_stage) -> &pipe_shader_caps {
        &self.screen().shader_caps[t as usize]
    }

    pub fn compute_caps(&self) -> &pipe_compute_caps {
        &self.screen().compute_caps
    }

    pub fn name(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.screen().get_name.unwrap()(self.pipe())) }
    }

    pub fn device_node_mask(&self) -> Option<u32> {
        unsafe { Some(self.screen().get_device_node_mask?(self.pipe())) }
    }

    pub fn device_uuid(&self) -> Option<[c_uchar; UUID_SIZE]> {
        let mut uuid = [0; UUID_SIZE];
        let ptr = uuid.as_mut_ptr();
        unsafe {
            self.screen().get_device_uuid?(self.pipe(), ptr.cast());
        }

        Some(uuid)
    }

    pub fn device_luid(&self) -> Option<[c_uchar; LUID_SIZE]> {
        let mut luid = [0; LUID_SIZE];
        let ptr = luid.as_mut_ptr();
        unsafe { self.screen().get_device_luid?(self.pipe(), ptr.cast()) }

        Some(luid)
    }

    pub fn device_vendor(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.screen().get_device_vendor.unwrap()(self.pipe())) }
    }

    pub fn driver_uuid(&self) -> Option<[c_schar; UUID_SIZE]> {
        let mut uuid = [0; UUID_SIZE];
        let ptr = uuid.as_mut_ptr();
        unsafe {
            self.screen().get_driver_uuid?(self.pipe(), ptr.cast());
        }

        Some(uuid)
    }

    pub fn cl_cts_version(&self) -> &CStr {
        unsafe {
            let ptr = self
                .screen()
                .get_cl_cts_version
                .map_or(ptr::null(), |get_cl_cts_version| {
                    get_cl_cts_version(self.pipe())
                });
            if ptr.is_null() {
                // this string is good enough to pass the CTS
                c"v0000-01-01-00"
            } else {
                CStr::from_ptr(ptr)
            }
        }
    }

    pub fn is_format_supported(
        &self,
        format: pipe_format,
        target: pipe_texture_target,
        bindings: u32,
    ) -> bool {
        unsafe {
            self.screen().is_format_supported.unwrap()(self.pipe(), format, target, 0, 0, bindings)
        }
    }

    pub fn get_timestamp(&self) -> u64 {
        unsafe {
            self.screen()
                .get_timestamp
                .unwrap_or(u_default_get_timestamp)(self.pipe())
        }
    }

    pub fn is_res_handle_supported(&self) -> bool {
        self.screen().resource_from_handle.is_some() && self.screen().resource_get_handle.is_some()
    }

    pub fn is_fixed_address_supported(&self) -> bool {
        self.screen().resource_get_address.is_some()
    }

    pub fn is_vm_supported(&self) -> bool {
        self.screen().resource_assign_vma.is_some()
            && self.screen().alloc_vm.is_some()
            && self.screen().free_vm.is_some()
    }

    pub fn nir_shader_compiler_options(
        &self,
        shader: mesa_shader_stage,
    ) -> *const nir_shader_compiler_options {
        self.screen().nir_options[shader as usize]
    }

    pub fn shader_cache(&self) -> Option<DiskCacheBorrowed> {
        let ptr = unsafe { self.screen().get_disk_shader_cache?(self.pipe()) };

        DiskCacheBorrowed::from_ptr(ptr)
    }

    /// returns true if finalize_nir was called
    pub fn finalize_nir(&self, nir: &NirShader) -> bool {
        if let Some(func) = self.screen().finalize_nir {
            unsafe {
                func(self.pipe(), nir.get_nir().cast(), true);
            }
            true
        } else {
            false
        }
    }

    pub fn create_semaphore(&self) -> Option<PipeFence> {
        let fence = unsafe { self.screen().semaphore_create.unwrap()(self.pipe()) };
        PipeFence::new(fence, self)
    }

    pub(super) fn unref_fence(&self, mut fence: *mut pipe_fence_handle) {
        unsafe {
            self.screen().fence_reference.unwrap()(self.pipe(), &mut fence, ptr::null_mut());
        }
    }

    pub(super) fn fence_finish(&self, fence: *mut pipe_fence_handle) -> bool {
        unsafe {
            self.screen().fence_finish.unwrap()(
                self.pipe(),
                ptr::null_mut(),
                fence,
                OS_TIMEOUT_INFINITE as u64,
            )
        }
    }

    pub(super) fn fence_get_fd(&self, fence: *mut pipe_fence_handle) -> c_int {
        unsafe { self.screen().fence_get_fd.unwrap()(self.pipe(), fence) }
    }

    pub fn query_memory_info(&self) -> Option<pipe_memory_info> {
        let mut info = pipe_memory_info::default();
        unsafe {
            self.screen().query_memory_info?(self.pipe(), &mut info);
        }
        Some(info)
    }

    pub fn has_fence_get_fd(&self) -> bool {
        self.screen().fence_get_fd.is_some()
    }

    pub fn has_semaphore_create(&self) -> bool {
        self.screen().semaphore_create.is_some()
    }
}

impl ToOwned for PipeScreen {
    type Owned = PipeScreenOwned;

    fn to_owned(&self) -> Self::Owned {
        let refcnt = self.refcnt().fetch_add(1, Ordering::SeqCst);

        // refcnt is not supposed to be 0 at any given point in time.
        assert!(refcnt > 0, "Reference count underflow detected!");

        PipeScreenOwned {
            // SAFETY: self.pipe() is a valid pointer.
            screen: unsafe { ThreadSafeCPtr::new(self.pipe()).unwrap() },
        }
    }
}

fn has_required_cbs(screen: *mut pipe_screen) -> bool {
    let screen = unsafe { *screen };
    // Use '&' to evaluate all features and to not stop
    // on first missing one to list all missing features.
    has_required_feature!(screen, context_create)
        & has_required_feature!(screen, destroy)
        & has_required_feature!(screen, fence_finish)
        & has_required_feature!(screen, fence_reference)
        & has_required_feature!(screen, get_name)
        & has_required_feature!(screen, is_format_supported)
        & has_required_feature!(screen, resource_create)
}
