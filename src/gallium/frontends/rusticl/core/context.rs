// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use crate::api::icd::*;
use crate::api::types::DeleteContextCB;
use crate::api::util::bit_check;
use crate::core::device::*;
use crate::core::format::*;
use crate::core::gl::*;
use crate::core::memory::*;
use crate::core::queue::*;
use crate::core::util::*;
use crate::impl_cl_type_trait;

use mesa_rust::pipe::context::RWFlags;
use mesa_rust::pipe::fence::FenceFd;
use mesa_rust::pipe::resource::*;
use mesa_rust::pipe::screen::ResourceType;
use mesa_rust::util;
use mesa_rust_gen::*;
use mesa_rust_util::conversion::*;
use mesa_rust_util::properties::Properties;
use mesa_rust_util::ptr::AllocSize;
use mesa_rust_util::ptr::TrackedPointers;
use rusticl_opencl_gen::*;

use std::alloc;
use std::alloc::Layout;
use std::cmp;
use std::collections::HashMap;
use std::convert::TryInto;
use std::ffi::c_int;
use std::mem;
use std::num::NonZeroU64;
use std::os::raw::c_void;
use std::ptr;
use std::slice;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::Weak;

use super::platform::Platform;

struct TrackedBDAAlloc {
    buffer: Weak<Buffer>,
    size: cl_mem_device_address_ext,
}

impl AllocSize<cl_mem_device_address_ext> for TrackedBDAAlloc {
    fn size(&self) -> cl_mem_device_address_ext {
        self.size
    }
}

struct SVMAlloc {
    layout: Layout,
    vma: Option<NonZeroU64>,
    alloc: Arc<Allocation>,
}

impl SVMAlloc {
    pub fn size(&self) -> usize {
        self.layout.size()
    }
}

impl Drop for SVMAlloc {
    fn drop(&mut self) {
        if let Some(vma) = self.vma {
            let address = vma.get() as usize as *mut c_void;
            unsafe {
                let ret = munmap(address, self.size());
                debug_assert_eq!(0, ret);
            }

            for (dev, res) in &self.alloc.get_real_resource().res {
                if !dev.system_svm_supported() {
                    dev.screen().resource_assign_vma(res, 0);
                }
            }

            Platform::get()
                .vm
                .as_ref()
                .unwrap()
                .lock()
                .unwrap()
                .free(vma, NonZeroU64::new(self.size() as u64).unwrap());
        } else {
            // SAFETY: we make sure that svm_pointer is a valid allocation and reuse the same layout
            // from the allocation
            unsafe {
                alloc::dealloc(self.alloc.host_ptr().cast(), self.layout);
            }
        }
    }
}

impl AllocSize<usize> for SVMAlloc {
    fn size(&self) -> usize {
        SVMAlloc::size(self)
    }
}

struct SVMContext {
    svm_ptrs: TrackedPointers<usize, SVMAlloc>,
}

pub struct Context {
    pub base: CLObjectBase<CL_INVALID_CONTEXT>,
    pub devs: Vec<&'static Device>,
    pub properties: Properties<cl_context_properties>,
    pub dtors: Mutex<Vec<DeleteContextCB>>,
    // we track the pointers per device for quick access in hot paths.
    bda_ptrs: Mutex<
        HashMap<&'static Device, TrackedPointers<cl_mem_device_address_ext, TrackedBDAAlloc>>,
    >,
    svm: Mutex<SVMContext>,
    pub gl_ctx_manager: Option<GLCtxManager>,
    pub worker_queue: util::queue::Queue,
}

impl_cl_type_trait!(cl_context, Context, CL_INVALID_CONTEXT);

impl Context {
    pub fn new(
        devs: Vec<&'static Device>,
        properties: Properties<cl_context_properties>,
        gl_ctx_manager: Option<GLCtxManager>,
    ) -> Arc<Context> {
        let worker_count = u32::max(util::cpu_count() / 2, 1);
        let max_job_count = worker_count * 8;

        Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Context),
            devs: devs,
            properties: properties,
            dtors: Mutex::new(Vec::new()),
            bda_ptrs: Mutex::new(HashMap::new()),
            svm: Mutex::new(SVMContext {
                svm_ptrs: TrackedPointers::new(),
            }),
            gl_ctx_manager: gl_ctx_manager,
            worker_queue: util::queue::Queue::new(c"clctxworker", max_job_count, worker_count),
        })
    }

    pub fn create_buffer(
        &self,
        size: usize,
        user_ptr: *mut c_void,
        copy: bool,
        bda: bool,
        res_type: ResourceType,
    ) -> CLResult<HashMap<&'static Device, PipeResourceOwned>> {
        let adj_size: u32 = size.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;
        let mut res = HashMap::new();
        let mut pipe_flags = 0;

        if bda {
            pipe_flags |= PIPE_RESOURCE_FLAG_FIXED_ADDRESS;
        }

        for &dev in &self.devs {
            let mut resource = None;

            if !user_ptr.is_null() && !copy {
                resource = dev.screen().resource_create_buffer_from_user(
                    adj_size,
                    user_ptr,
                    PIPE_BIND_GLOBAL,
                    pipe_flags,
                )
            }

            if resource.is_none() {
                resource = dev.screen().resource_create_buffer(
                    adj_size,
                    res_type,
                    PIPE_BIND_GLOBAL,
                    pipe_flags,
                )
            }

            let resource = resource.ok_or(CL_OUT_OF_RESOURCES);
            res.insert(dev, resource?);
        }

        if !user_ptr.is_null() {
            res.iter()
                .filter(|(_, r)| copy || !r.is_user())
                .map(|(d, r)| {
                    d.helper_ctx()
                        .exec(|ctx| ctx.buffer_subdata(r, 0, user_ptr, size.try_into().unwrap()))
                })
                .for_each(|f| {
                    f.wait();
                });
        }

        Ok(res)
    }

    pub fn create_texture(
        &self,
        desc: &cl_image_desc,
        format: &cl_image_format,
        user_ptr: *mut c_void,
        copy: bool,
        res_type: ResourceType,
    ) -> CLResult<HashMap<&'static Device, PipeResourceOwned>> {
        let pipe_format = format.to_pipe_format().unwrap();

        let width = desc.image_width.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;
        let height = desc.image_height.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;
        let depth = desc.image_depth.try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;
        let array_size = desc
            .image_array_size
            .try_into_with_err(CL_OUT_OF_HOST_MEMORY)?;
        let target = cl_mem_type_to_texture_target(desc.image_type);

        let mut res = HashMap::new();
        for &dev in &self.devs {
            let mut resource = None;
            let enable_bind_as_image =
                (dev.formats[format][&desc.image_type] as u32 & CL_MEM_WRITE_ONLY) != 0;

            // we can't specify custom pitches/slices, so this won't work for non 1D images
            if !user_ptr.is_null() && !copy && desc.image_type == CL_MEM_OBJECT_IMAGE1D {
                resource = dev.screen().resource_create_texture_from_user(
                    width,
                    height,
                    depth,
                    array_size,
                    target,
                    pipe_format,
                    user_ptr,
                    enable_bind_as_image,
                )
            }

            if resource.is_none() {
                resource = dev.screen().resource_create_texture(
                    width,
                    height,
                    depth,
                    array_size,
                    target,
                    pipe_format,
                    res_type,
                    enable_bind_as_image,
                )
            }

            let resource = resource.ok_or(CL_OUT_OF_RESOURCES);
            res.insert(dev, resource?);
        }

        if !user_ptr.is_null() {
            let bx = desc.bx()?;
            let stride = desc.row_pitch()?;
            let layer_stride = desc.slice_pitch();

            res.iter()
                .filter(|(_, r)| copy || !r.is_user())
                .map(|(d, r)| {
                    d.helper_ctx()
                        .exec(|ctx| ctx.texture_subdata(r, &bx, user_ptr, stride, layer_stride))
                })
                .for_each(|f| {
                    f.wait();
                });
        }

        Ok(res)
    }

    /// Returns the max allocation size supported by all devices
    pub fn max_mem_alloc(&self) -> u64 {
        self.devs
            .iter()
            .map(|dev| dev.max_mem_alloc())
            .min()
            .unwrap()
    }

    pub fn has_svm_devs(&self) -> bool {
        self.devs.iter().any(|dev| dev.api_svm_supported())
    }

    pub fn alloc_svm_ptr(
        &self,
        size: NonZeroU64,
        mut alignment: NonZeroU64,
    ) -> CLResult<*mut c_void> {
        // TODO: choose better alignment in regards to huge pages
        alignment = cmp::max(alignment, NonZeroU64::new(0x1000).unwrap());

        // clSVMAlloc will fail if alignment is not a power of two.
        // `from_size_align()` verifies this condition is met.
        let layout = Layout::from_size_align(size.get() as usize, alignment.get() as usize)
            .or(Err(CL_INVALID_VALUE))?;

        // clSVMAlloc will fail if size is 0 or > CL_DEVICE_MAX_MEM_ALLOC_SIZE value
        // for any device in context.
        // Verify that the requested size, once adjusted to be a multiple of
        // alignment, fits within the maximum allocation size. While
        // `from_size_align()` ensures that the allocation will fit in host memory,
        // the maximum allocation may be smaller due to limitations from gallium or
        // devices.
        // let size_aligned = layout.pad_to_align().size();

        // allocate a vma if one of the devices doesn't support system SVM
        let vma = if let Some(vm) = &Platform::get().vm {
            Some(
                vm.lock()
                    .unwrap()
                    .alloc(size, alignment)
                    .ok_or(CL_OUT_OF_RESOURCES)?,
            )
        } else {
            None
        };

        let ptr: *mut c_void = if let Some(vma) = &vma {
            #[cfg(target_os = "linux")]
            fn os_flags() -> u32 {
                // MAP_FIXED_NOREPLACE needs 4.17
                MAP_FIXED_NOREPLACE | MAP_NORESERVE
            }

            #[cfg(target_os = "freebsd")]
            fn os_flags() -> u32 {
                MAP_FIXED | MAP_EXCL
            }

            #[cfg(not(any(target_os = "linux", target_os = "freebsd")))]
            fn os_flags() -> u32 {
                unreachable!("SVM supported only on Linux")
            }

            let mut res = unsafe {
                mmap(
                    vma.get() as usize as *mut c_void,
                    size.get() as usize,
                    (PROT_READ | PROT_WRITE) as c_int,
                    (MAP_PRIVATE | MAP_ANONYMOUS | os_flags()) as c_int,
                    -1,
                    0,
                )
            };

            // mmap returns MAP_FAILED on error which is -1
            if res as usize == usize::MAX {
                return Err(CL_OUT_OF_HOST_MEMORY);
            }

            if res as usize != vma.get() as usize {
                unsafe {
                    let ret = munmap(res, size.get() as usize);
                    debug_assert_eq!(0, ret);
                }
                res = ptr::null_mut();
            }

            res.cast()
        } else {
            unsafe { alloc::alloc(layout) }.cast()
        };

        if ptr.is_null() {
            return Err(CL_OUT_OF_HOST_MEMORY);
        }

        let address = ptr as u64;
        let mut buffers = HashMap::new();
        for &dev in &self.devs {
            let size: u32 = size.get().try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?;

            // For system SVM devices we simply create a userptr resource.
            let res = if dev.system_svm_supported() {
                dev.screen()
                    .resource_create_buffer_from_user(size, ptr, PIPE_BIND_GLOBAL, 0)
            } else {
                dev.screen().resource_create_buffer(
                    size,
                    ResourceType::Normal,
                    PIPE_BIND_GLOBAL,
                    PIPE_RESOURCE_FLAG_FRONTEND_VM,
                )
            };

            let res = res.ok_or(CL_OUT_OF_RESOURCES)?;
            if !dev.system_svm_supported() {
                if Platform::dbg().memory {
                    eprintln!("assigning {address:x} to {res:?}");
                }

                if !dev.screen().resource_assign_vma(&res, address) {
                    return Err(CL_OUT_OF_RESOURCES);
                }
            }

            buffers.insert(dev, res);
        }

        self.svm.lock().unwrap().svm_ptrs.insert(
            ptr as usize,
            SVMAlloc {
                layout: layout,
                vma,
                alloc: Arc::new(Allocation::new(buffers, 0, ptr)),
            },
        );

        Ok(ptr)
    }

    pub fn copy_svm_to_dev(
        &self,
        ctx: &QueueContext,
        ptr: usize,
    ) -> CLResult<Option<PipeResourceOwned>> {
        let svm = self.svm.lock().unwrap();

        let Some(alloc) = svm.svm_ptrs.find_alloc_precise(ptr) else {
            return Ok(None);
        };

        Ok(Some(
            alloc.alloc.get_res_for_access(ctx, RWFlags::RW)?.new_ref(),
        ))
    }

    pub fn copy_svm_to_host(
        &self,
        ctx: &QueueContext,
        svm_ptr: usize,
        flags: cl_map_flags,
    ) -> CLResult<()> {
        // no need to copy
        if bit_check(flags, CL_MAP_WRITE_INVALIDATE_REGION) {
            return Ok(());
        }

        let svm = self.svm.lock().unwrap();
        let Some((_, alloc)) = svm.svm_ptrs.find_alloc(svm_ptr) else {
            return Ok(());
        };

        alloc.alloc.migrate_to_hostptr(ctx, RWFlags::RW)
    }

    pub fn copy_svm(
        &self,
        ctx: &QueueContext,
        src_addr: usize,
        dst_addr: usize,
        size: usize,
    ) -> CLResult<()> {
        let svm = self.svm.lock().unwrap();
        let src = svm.svm_ptrs.find_alloc(src_addr);
        let dst = svm.svm_ptrs.find_alloc(dst_addr);

        #[allow(clippy::collapsible_else_if)]
        if let Some((src_base, src_alloc)) = src {
            let src_res = src_alloc.alloc.get_res_for_access(ctx, RWFlags::RD)?;
            let src_offset = src_addr - src_base;

            if let Some((dst_base, dst_alloc)) = dst {
                let dst_res = dst_alloc.alloc.get_res_for_access(ctx, RWFlags::WR)?;
                let dst_offset = dst_addr - dst_base;

                ctx.resource_copy_buffer(
                    src_res,
                    src_offset as i32,
                    dst_res,
                    dst_offset as u32,
                    size as i32,
                );
            } else {
                let map = ctx
                    .buffer_map(src_res, src_offset as i32, size as i32, RWFlags::RD)
                    .ok_or(CL_OUT_OF_HOST_MEMORY)?;
                unsafe {
                    ptr::copy_nonoverlapping(map.ptr(), dst_addr as *mut c_void, size);
                }
            }
        } else {
            if let Some((dst_base, dst_alloc)) = dst {
                let dst_res = dst_alloc.alloc.get_res_for_access(ctx, RWFlags::WR)?;
                let dst_offset = dst_addr - dst_base;

                ctx.buffer_subdata(
                    dst_res,
                    dst_offset as u32,
                    src_addr as *const c_void,
                    size as u32,
                );
            } else {
                unsafe {
                    ptr::copy(src_addr as *const c_void, dst_addr as *mut c_void, size);
                }
            }
        }

        Ok(())
    }

    pub fn clear_svm<const T: usize>(
        &self,
        ctx: &QueueContext,
        svm_ptr: usize,
        size: usize,
        pattern: [u8; T],
    ) -> CLResult<()> {
        let svm = self.svm.lock().unwrap();

        if let Some((base, alloc)) = svm.svm_ptrs.find_alloc(svm_ptr) {
            let res = alloc.alloc.get_res_for_access(ctx, RWFlags::WR)?;
            let offset = svm_ptr - base;
            ctx.clear_buffer(res, &pattern, offset as u32, size as u32);
        } else {
            let slice = unsafe {
                slice::from_raw_parts_mut(svm_ptr as *mut _, size / size_of_val(&pattern))
            };

            slice.fill(pattern);
        }

        Ok(())
    }

    pub fn migrate_svm(
        &self,
        ctx: &QueueContext,
        pointers: Vec<usize>,
        sizes: Vec<usize>,
        to_device: bool,
        content_undefined: bool,
    ) -> CLResult<()> {
        let svm = self.svm.lock().unwrap();

        if ctx.dev.system_svm_supported() {
            ctx.svm_migrate(&pointers, &sizes, to_device, content_undefined);
        } else {
            for ptr in pointers {
                let Some((_, alloc)) = svm.svm_ptrs.find_alloc(ptr) else {
                    continue;
                };

                // we assume it's only read, so it remains valid on the host until future commands
                // have different needs.
                if to_device {
                    alloc.alloc.get_res_for_access(ctx, RWFlags::RD)?;
                } else {
                    alloc.alloc.migrate_to_hostptr(ctx, RWFlags::RD)?;
                }
            }
        }
        Ok(())
    }

    pub fn get_svm_alloc(&self, ptr: usize) -> Option<(*mut c_void, Arc<Allocation>)> {
        self.svm
            .lock()
            .unwrap()
            .svm_ptrs
            .find_alloc(ptr)
            .map(|(base, alloc)| (base as *mut c_void, Arc::clone(&alloc.alloc)))
    }

    pub fn find_svm_alloc(&self, ptr: usize) -> Option<(*mut c_void, usize)> {
        self.svm
            .lock()
            .unwrap()
            .svm_ptrs
            .find_alloc(ptr)
            .map(|(ptr, alloc)| (ptr as _, alloc.size()))
    }

    pub fn remove_svm_ptr(&self, ptr: usize) {
        self.svm.lock().unwrap().svm_ptrs.remove(ptr);
    }

    pub fn add_bda_ptr(&self, buffer: &Arc<Buffer>) {
        if let Some(iter) = buffer.dev_addresses() {
            let mut bda_ptrs = self.bda_ptrs.lock().unwrap();

            for (dev, address) in iter {
                let Some(address) = address else {
                    continue;
                };

                bda_ptrs.entry(dev).or_default().insert(
                    address.get(),
                    TrackedBDAAlloc {
                        buffer: Arc::downgrade(buffer),
                        size: buffer.size as _,
                    },
                );
            }
        }
    }

    pub fn find_bda_alloc(
        &self,
        dev: &Device,
        ptr: cl_mem_device_address_ext,
    ) -> Option<Arc<Buffer>> {
        let lock = self.bda_ptrs.lock().unwrap();
        let (_, mem) = lock.get(dev)?.find_alloc(ptr)?;
        mem.buffer.upgrade()
    }

    pub fn remove_bda(&self, buf: &Buffer) {
        let mut bda_ptrs = self.bda_ptrs.lock().unwrap();

        for (dev, bdas) in bda_ptrs.iter_mut() {
            if let Some(address) = buf.dev_address(dev) {
                bdas.remove(address.get());
            }
        }
    }

    pub fn import_gl_buffer(
        &self,
        handle: u32,
        modifier: u64,
        image_type: cl_mem_object_type,
        gl_target: cl_GLenum,
        format: cl_image_format,
        gl_props: GLMemProps,
    ) -> CLResult<HashMap<&'static Device, PipeResourceOwned>> {
        let mut res = HashMap::new();
        let target = cl_mem_type_to_texture_target_gl(image_type, gl_target);
        let pipe_format = if image_type == CL_MEM_OBJECT_BUFFER {
            pipe_format::PIPE_FORMAT_NONE
        } else {
            format.to_pipe_format().unwrap()
        };

        for dev in &self.devs {
            let enable_bind_as_image = if target != pipe_texture_target::PIPE_BUFFER {
                dev.formats[&format][&image_type] as u32 & CL_MEM_WRITE_ONLY != 0
            } else {
                false
            };

            let resource = dev
                .screen()
                .resource_import_dmabuf(
                    handle,
                    modifier,
                    target,
                    pipe_format,
                    gl_props.stride,
                    gl_props.width,
                    gl_props.height,
                    gl_props.depth,
                    gl_props.array_size,
                    enable_bind_as_image,
                )
                .ok_or(CL_OUT_OF_RESOURCES)?;

            res.insert(*dev, resource);
        }

        Ok(res)
    }

    pub fn flush_gl_mem_objects(&self, mem_objects: &[Mem]) -> CLResult<Option<FenceFd>> {
        let gl_ctx = self.gl_ctx_manager.as_ref().ok_or(CL_INVALID_CONTEXT)?;
        gl_ctx.flush(mem_objects)
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        let cbs = mem::take(self.dtors.get_mut().unwrap());
        for cb in cbs.into_iter().rev() {
            cb.call(self);
        }
    }
}
