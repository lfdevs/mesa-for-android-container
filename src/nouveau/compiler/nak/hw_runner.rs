// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::bindings::*;
use nak_bindings::*;
use nv_push_rs::Push as NvPush;
use nvidia_headers::classes::cla0c0::mthd as cla0c0;
use nvidia_headers::classes::clb1c0::mthd as clb1c0;
use nvidia_headers::classes::clb1c0::MAXWELL_COMPUTE_B;
use nvidia_headers::classes::clc3c0::mthd as clc3c0;
use nvidia_headers::classes::clc3c0::VOLTA_COMPUTE_A;
use nvidia_headers::classes::clc6c0::mthd as clc6c0;
use nvidia_headers::classes::clc6c0::AMPERE_COMPUTE_A;
use nvidia_headers::classes::clcbc0::HOPPER_COMPUTE_A;

use std::ffi::c_void;
use std::io;
use std::ops::Deref;
use std::ptr;
use std::ptr::NonNull;
use std::slice;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

unsafe fn is_nvidia_device(dev: drmDevicePtr) -> bool {
    unsafe {
        match (*dev).bustype as u32 {
            DRM_BUS_PCI => {
                let pci = (*dev).deviceinfo.pci.as_ref().unwrap();
                pci.vendor_id == (NVIDIA_VENDOR_ID as u16)
            }
            _ => false,
        }
    }
}

#[repr(C)]
pub struct CB0 {
    pub data_addr_lo: u32,
    pub data_addr_hi: u32,
    pub data_stride: u32,
    pub invocations: u32,
}

struct DrmDevices {
    num_devices: usize,
    devices: [drmDevicePtr; 16],
}

impl DrmDevices {
    fn get() -> io::Result<Self> {
        unsafe {
            let mut devices: [drmDevicePtr; 16] = [std::ptr::null_mut(); 16];
            let num_devices = drmGetDevices(
                devices.as_mut_ptr(),
                devices.len().try_into().unwrap(),
            );
            if num_devices < 0 {
                return Err(io::Error::last_os_error());
            }
            Ok(DrmDevices {
                num_devices: num_devices.try_into().unwrap(),
                devices,
            })
        }
    }

    fn iter(&self) -> slice::Iter<'_, drmDevicePtr> {
        self.devices[..self.num_devices].iter()
    }
}

impl Deref for DrmDevices {
    type Target = [drmDevicePtr];

    fn deref(&self) -> &[drmDevicePtr] {
        &self.devices[..self.num_devices]
    }
}

impl Drop for DrmDevices {
    fn drop(&mut self) {
        unsafe {
            drmFreeDevices(
                self.devices.as_mut_ptr(),
                self.num_devices.try_into().unwrap(),
            );
        }
    }
}

pub struct Device {
    dev: NonNull<nouveau_ws_device>,
    next_addr: AtomicU64,
}

impl Device {
    pub fn new(dev_id: Option<usize>) -> io::Result<Arc<Self>> {
        let drm_devices = DrmDevices::get()?;
        unsafe {
            let drm_dev = if let Some(dev_id) = dev_id {
                if dev_id >= drm_devices.len() {
                    return Err(io::Error::new(
                        io::ErrorKind::NotFound,
                        "Unknown device {dev_id}",
                    ));
                }
                drm_devices[dev_id]
            } else {
                if let Some(dev) =
                    drm_devices.iter().find(|dev| is_nvidia_device(**dev))
                {
                    *dev
                } else {
                    return Err(io::Error::new(
                        io::ErrorKind::NotFound,
                        "Failed to find an NVIDIA device",
                    ));
                }
            };

            let dev = nouveau_ws_device_new(drm_dev);
            let Some(dev) = NonNull::new(dev) else {
                return Err(io::Error::last_os_error());
            };

            Ok(Arc::new(Device {
                dev,
                next_addr: AtomicU64::new(1 << 16),
            }))
        }
    }

    pub fn dev_info(&self) -> &nv_device_info {
        unsafe { &self.dev.as_ref().info }
    }

    fn fd(&self) -> i32 {
        unsafe { self.dev.as_ref().fd }
    }

    fn ws_dev(&self) -> *mut nouveau_ws_device {
        self.dev.as_ptr()
    }
}

impl Drop for Device {
    fn drop(&mut self) {
        unsafe { nouveau_ws_device_destroy(self.ws_dev()) }
    }
}

pub struct Context {
    dev: Arc<Device>,
    ctx: NonNull<nouveau_ws_context>,
    syncobj: u32,
    sync_value: Mutex<u64>,
}

impl Context {
    pub fn new(dev: Arc<Device>) -> io::Result<Self> {
        unsafe {
            let mut ctx: *mut nouveau_ws_context = std::ptr::null_mut();
            let err = nouveau_ws_context_create(
                dev.ws_dev(),
                NOUVEAU_WS_ENGINE_COMPUTE,
                &mut ctx,
            );
            if err != 0 {
                return Err(io::Error::last_os_error());
            }
            let ctx = NonNull::new(ctx).unwrap();

            let mut syncobj = 0_u32;
            let err = drmSyncobjCreate(dev.fd(), 0, &mut syncobj);
            if err != 0 {
                nouveau_ws_context_destroy(ctx.as_ptr());
                return Err(io::Error::last_os_error());
            }

            Ok(Context {
                dev,
                ctx,
                syncobj,
                sync_value: Mutex::new(0),
            })
        }
    }

    pub fn exec(&self, addr: u64, len: u16) -> io::Result<()> {
        let sync_value = unsafe {
            let mut sync_value = self.sync_value.lock().unwrap();
            *sync_value += 1;

            let push = drm_nouveau_exec_push {
                va: addr,
                va_len: len.into(),
                flags: 0,
            };
            let sig = drm_nouveau_sync {
                flags: DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ,
                handle: self.syncobj,
                timeline_value: *sync_value,
            };
            let exec = drm_nouveau_exec {
                channel: self.ctx.as_ref().channel as u32,
                wait_count: 0,
                wait_ptr: 0,
                push_count: 1,
                push_ptr: &push as *const _ as u64,
                sig_count: 1,
                sig_ptr: &sig as *const _ as u64,
            };
            let err = drmIoctl(
                self.dev.fd(),
                DRM_RS_IOCTL_NOUVEAU_EXEC.into(),
                &exec as *const _ as *mut std::os::raw::c_void,
            );
            if err != 0 {
                return Err(io::Error::last_os_error());
            }
            *sync_value
        };
        // The close of this unsafe { } drops the lock

        unsafe {
            let err = drmSyncobjTimelineWait(
                self.dev.fd(),
                &self.syncobj as *const _ as *mut _,
                &sync_value as *const _ as *mut _,
                1,        // num_handles
                i64::MAX, // timeout_nsec
                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                std::ptr::null_mut(),
            );
            if err != 0 {
                return Err(io::Error::last_os_error());
            }

            // Exec again to check for errors
            let mut exec = drm_nouveau_exec {
                channel: self.ctx.as_ref().channel as u32,
                wait_count: 0,
                wait_ptr: 0,
                push_count: 0,
                push_ptr: 0,
                sig_count: 0,
                sig_ptr: 0,
            };
            let err = drmIoctl(
                self.dev.fd(),
                DRM_RS_IOCTL_NOUVEAU_EXEC.into(),
                ptr::from_mut(&mut exec).cast(),
            );
            if err != 0 {
                return Err(io::Error::last_os_error());
            }
        }

        Ok(())
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            drmSyncobjDestroy(self.dev.fd(), self.syncobj);
            nouveau_ws_context_destroy(self.ctx.as_ptr());
        }
    }
}

pub struct BO {
    dev: Arc<Device>,
    bo: NonNull<nouveau_ws_bo>,
    pub addr: u64,
    pub map: *mut std::os::raw::c_void,
}

impl BO {
    pub fn new(dev: Arc<Device>, size: u64) -> io::Result<BO> {
        let size = size.next_multiple_of(4096);

        let mut map: *mut std::os::raw::c_void = std::ptr::null_mut();
        let bo = unsafe {
            nouveau_ws_bo_new_mapped(
                dev.ws_dev(),
                size,
                0, // align
                NOUVEAU_WS_BO_GART,
                NOUVEAU_WS_BO_RDWR,
                ptr::from_mut(&mut map),
            )
        };
        let Some(bo) = NonNull::new(bo) else {
            return Err(io::Error::last_os_error());
        };
        assert!(!map.is_null());

        let addr = dev.next_addr.fetch_add(size, Ordering::Relaxed);
        assert!(addr % 4096 == 0);

        unsafe {
            nouveau_ws_bo_bind_vma(
                dev.ws_dev(),
                bo.as_ptr(),
                addr,
                size,
                0, // bo_offset
                0, // pte_kind
            );
        }

        Ok(BO { dev, bo, addr, map })
    }
}

impl Drop for BO {
    fn drop(&mut self) {
        unsafe {
            nouveau_ws_bo_unbind_vma(
                self.dev.dev.as_ptr(),
                self.addr,
                self.bo.as_ref().size,
            );
            nouveau_ws_bo_destroy(self.bo.as_ptr());
        }
    }
}

struct QMDHeapImpl {
    dev: Arc<Device>,
    bos: Vec<BO>,
    last_offset: u32,
    free: Vec<(u64, *mut std::os::raw::c_void)>,
}

pub struct QMDHeap {
    pub qmd_size_B: u32,
    imp: Mutex<QMDHeapImpl>,
}

pub struct QMD<'a> {
    heap: &'a QMDHeap,
    pub addr: u64,
    pub map: *mut std::os::raw::c_void,
}

impl Drop for QMD<'_> {
    fn drop(&mut self) {
        let mut heap = self.heap.imp.lock().unwrap();
        heap.free.push((self.addr, self.map));
    }
}

impl QMDHeap {
    const BO_SIZE: u32 = 1 << 16;

    fn new(dev: Arc<Device>) -> Self {
        let qmd_size_B = unsafe { nak_qmd_size_B(dev.dev_info()) };
        Self {
            qmd_size_B,
            imp: Mutex::new(QMDHeapImpl {
                dev,
                bos: Vec::new(),
                last_offset: Self::BO_SIZE,
                free: Vec::new(),
            }),
        }
    }

    fn alloc_qmd<'a>(&'a self) -> io::Result<QMD<'a>> {
        let mut imp = self.imp.lock().unwrap();
        if let Some((addr, map)) = imp.free.pop() {
            return Ok(QMD {
                heap: self,
                addr,
                map,
            });
        }

        if imp.last_offset >= Self::BO_SIZE {
            let dev = imp.dev.clone();
            imp.bos.push(BO::new(dev, Self::BO_SIZE.into())?);
            imp.last_offset = 0;
        }

        let bo = imp.bos.last().unwrap();
        let addr = bo.addr + u64::from(imp.last_offset);
        let map =
            unsafe { bo.map.byte_offset(imp.last_offset.try_into().unwrap()) };
        imp.last_offset += self.qmd_size_B.next_multiple_of(NAK_QMD_ALIGN_B);

        Ok(QMD {
            heap: self,
            addr,
            map,
        })
    }

    pub fn qmd_size(&self) -> usize {
        self.qmd_size_B.try_into().unwrap()
    }
}

pub struct Runner {
    dev: Arc<Device>,
    ctx: Context,
    qmd_heap: QMDHeap,
}

pub struct RunConfig<'n, 'q> {
    shader: &'n nak_shader_bin,
    invocations: u32,
    data_stride: u32,
    data: *mut std::os::raw::c_void,
    data_size: usize,

    bo_shader_offset: usize,
    bo_data_offset: usize,
    bo_cb0_offset: usize,
    qmd: Option<QMD<'q>>,
}

impl<'n, 'q> RunConfig<'n, 'q> {
    pub fn new(
        shader: &'n nak_shader_bin,
        invocations: u32,
        data_stride: u32,
        data: *mut std::os::raw::c_void,
        data_size: usize,
    ) -> Self {
        Self {
            shader,
            invocations,
            data_stride,
            data,
            data_size,
            bo_shader_offset: 0,
            bo_data_offset: 0,
            bo_cb0_offset: 0,
            qmd: None,
        }
    }
}

pub struct PushBuilder {
    bo: BO,
    p: NvPush,
    push_size: usize,
    bo_alloced: usize,
}

impl PushBuilder {
    pub fn new(
        dev: Arc<Device>,
        bo_size: usize,
        push_size: usize,
    ) -> io::Result<Self> {
        let cls_compute = dev.dev_info().cls_compute;
        let bo = BO::new(dev, u64::try_from(bo_size + 4 * push_size).unwrap())?;
        let mut p = NvPush::new();

        p.push_mthd(cla0c0::SetObject {
            class_id: cls_compute.into(),
            engine_id: 0,
        });
        if cls_compute < VOLTA_COMPUTE_A {
            p.push_mthd(cla0c0::SetProgramRegionA {
                address_upper: (bo.addr >> 32) as u32,
            });
            p.push_mthd(cla0c0::SetProgramRegionB {
                address_lower: bo.addr as u32,
            });
        }

        let mut smem_base_addr = 0xfe000000_u64;
        if cls_compute >= HOPPER_COMPUTE_A {
            // This needs to be 4GB aligned on Hopper+
            smem_base_addr <<= 8;
        }

        let lmem_base_addr = 0xff000000_u32;
        if cls_compute >= VOLTA_COMPUTE_A {
            p.push_mthd(clc3c0::SetShaderSharedMemoryWindowA {
                base_address_upper: (smem_base_addr >> 32) as u32,
            });
            p.push_mthd(clc3c0::SetShaderSharedMemoryWindowB {
                base_address: smem_base_addr as u32,
            });

            p.push_mthd(clc3c0::SetShaderLocalMemoryWindowA {
                base_address_upper: 0,
            });
            p.push_mthd(clc3c0::SetShaderLocalMemoryWindowB {
                base_address: lmem_base_addr,
            });
        } else {
            p.push_mthd(cla0c0::SetShaderSharedMemoryWindow {
                base_address: smem_base_addr as u32,
            });
            p.push_mthd(cla0c0::SetShaderLocalMemoryWindow {
                base_address: lmem_base_addr,
            });
        }

        if cls_compute >= MAXWELL_COMPUTE_B {
            p.push_mthd(clb1c0::InvalidateSkedCaches { v: 0 });
        }

        Ok(Self {
            bo,
            p,
            push_size,
            bo_alloced: 0,
        })
    }

    pub fn submit(&self, ctx: &Context) -> io::Result<()> {
        assert!(self.p.size() <= self.push_size);

        let push_addr = self.bo.addr;
        unsafe {
            let push_map = self.bo.map;
            std::ptr::copy(self.p.as_ptr(), push_map.cast(), self.p.len());
        }

        ctx.exec(push_addr, (self.p.len() * 4).try_into().unwrap())
    }

    pub fn data(&self, offset: usize) -> *mut c_void {
        unsafe {
            self.bo.map.byte_offset(
                isize::try_from(self.push_size * 4 + offset).unwrap(),
            )
        }
    }

    pub fn bo_offset(&self, offset: usize) -> usize {
        self.push_size * 4 + offset
    }

    pub fn data_gpu_addr(&self, offset: usize) -> u64 {
        self.bo.addr + u64::try_from(self.push_size * 4 + offset).unwrap()
    }

    /// Returns the gpu address
    pub fn copy_shader(&mut self, offset: usize, shader: &nak_shader_bin) {
        let shader_map = self.data(offset);
        unsafe {
            std::ptr::copy(
                shader.code,
                shader_map,
                shader.code_size.try_into().unwrap(),
            );
        };
    }

    /// Returns the address of cb0
    pub fn copy_data_and_fill_cb0(
        &mut self,
        data_offset: usize,
        cb0_offset: usize,
        data: *const c_void,
        data_size: usize,
        data_stride: u32,
        invocations: u32,
    ) {
        let data_addr = self.data_gpu_addr(data_offset);
        let data_map = self.data(data_offset);
        let cb0_map = self.data(cb0_offset);

        unsafe {
            if data_size > 0 {
                std::ptr::copy(data, data_map, data_size);
            }

            cb0_map.cast::<CB0>().write(CB0 {
                data_addr_lo: data_addr as u32,
                data_addr_hi: (data_addr >> 32) as u32,
                data_stride: data_stride,
                invocations: invocations,
            });
        }
    }

    pub fn create_qmd_with_dependence<'a>(
        &self,
        qmd_heap: &'a QMDHeap,
        shader: &nak_shader_bin,
        shader_offset: usize,
        cb0_offset: usize,
        invocations: u32,
        dependence_counter: u16,
        hw_dependence_counter: u16,
    ) -> io::Result<QMD<'a>> {
        let dev_info = self.bo.dev.dev_info();
        let mut qmd_cbufs: [nak_qmd_cbuf; 8] = Default::default();

        let cs_info = unsafe {
            assert!(shader.info.stage == MESA_SHADER_COMPUTE);
            &shader.info.__bindgen_anon_1.cs
        };
        assert!(cs_info.local_size[1] == 1 && cs_info.local_size[2] == 1);
        let local_size = cs_info.local_size[0];

        qmd_cbufs[0] = nak_qmd_cbuf {
            index: 0,
            size: size_of::<CB0>().next_multiple_of(256).try_into().unwrap(),
            addr: self.data_gpu_addr(cb0_offset).try_into().unwrap(),
        };

        let qmd_info = nak_qmd_info {
            // Pre-Volta, we set the program region to the start of the bo
            addr: if dev_info.cls_compute < VOLTA_COMPUTE_A {
                self.bo_offset(shader_offset).try_into().unwrap()
            } else {
                self.data_gpu_addr(shader_offset)
            },
            smem_size: unsafe { shader.info.__bindgen_anon_1.cs }
                .smem_size
                .into(),
            global_size: [invocations.div_ceil(local_size.into()), 1, 1],
            num_cbufs: 1,
            cbufs: qmd_cbufs,
            dependence_counter,
            hw_dependence_counter,
        };

        let qmd = qmd_heap.alloc_qmd()?;
        unsafe {
            nak_fill_qmd(
                dev_info,
                &shader.info,
                &qmd_info,
                qmd.map,
                qmd_heap.qmd_size_B.try_into().unwrap(),
            );
        }

        Ok(qmd)
    }

    pub fn create_qmd<'a>(
        &self,
        qmd_heap: &'a QMDHeap,
        shader: &nak_shader_bin,
        shader_offset: usize,
        cb0_offset: usize,
        invocations: u32,
    ) -> io::Result<QMD<'a>> {
        self.create_qmd_with_dependence(
            qmd_heap,
            shader,
            shader_offset,
            cb0_offset,
            invocations,
            0,
            0,
        )
    }

    pub fn dispatch_qmd(&mut self, qmd: &QMD) {
        self.p.push_mthd(cla0c0::SendPcasA {
            qmd_address_shifted8: (qmd.addr >> 8) as u32,
        });
        if self.bo.dev.dev_info().cls_compute >= AMPERE_COMPUTE_A {
            self.p.push_mthd(clc6c0::SendSignalingPcas2B {
                    pcas_action: clc6c0::SendSignalingPcas2BPcasAction::InvalidateCopySchedule,
                });
        } else {
            self.p.push_mthd(cla0c0::SendSignalingPcasB {
                invalidate: true,
                schedule: true,
            });
        }
    }

    pub fn qmd_decrement_dependence(&mut self, qmd: &QMD) {
        assert!(self.bo.dev.dev_info().cls_compute >= AMPERE_COMPUTE_A);
        self.p.push_mthd(cla0c0::SendPcasA {
            qmd_address_shifted8: (qmd.addr >> 8) as u32,
        });
        self.p.push_mthd(clc6c0::SendSignalingPcas2B {
            pcas_action:
                clc6c0::SendSignalingPcas2BPcasAction::DecrementDependence,
        });
    }

    pub fn bo_alloc(&mut self, size: usize, align: usize) -> usize {
        self.bo_alloced = self.bo_alloced.next_multiple_of(align);
        let alloc = self.bo_alloced;
        self.bo_alloced += size;
        alloc
    }
}

impl Runner {
    pub fn new(dev_id: Option<usize>) -> Runner {
        let dev = Device::new(dev_id).expect("Failed to create nouveau device");
        let ctx = Context::new(dev.clone()).expect("Failed to create context");
        let qmd_heap = QMDHeap::new(dev.clone());
        Runner { dev, ctx, qmd_heap }
    }

    pub fn device(&self) -> Arc<Device> {
        self.dev.clone()
    }

    pub fn dev_info(&self) -> &nv_device_info {
        self.dev.dev_info()
    }

    pub fn qmd_heap(&self) -> &QMDHeap {
        &self.qmd_heap
    }

    pub fn ctx(&self) -> &Context {
        &self.ctx
    }

    pub unsafe fn run_raw<'q, 'n, const N: usize>(
        &'q self,
        mut configs: [RunConfig<'n, 'q>; N],
    ) -> io::Result<()> {
        // Compute the needed size of the buffer
        let mut size = 0_usize;
        for config in &mut configs {
            config.bo_shader_offset = size.next_multiple_of(0x80);
            size = config.bo_shader_offset
                + usize::try_from(config.shader.code_size).unwrap();

            config.bo_cb0_offset = size.next_multiple_of(256);
            size = config.bo_cb0_offset + size_of::<CB0>();

            config.bo_data_offset = size.next_multiple_of(256);
            size = config.bo_data_offset + config.data_size;
        }

        let mut pb =
            PushBuilder::new(self.dev.clone(), size.try_into().unwrap(), 256)?;

        for config in &mut configs {
            let data_offset = config.bo_data_offset;
            let cb0_offset = config.bo_cb0_offset;
            let shader = config.shader;

            // Fill out cb0
            pb.copy_data_and_fill_cb0(
                data_offset,
                cb0_offset,
                config.data,
                config.data_size,
                config.data_stride,
                config.invocations,
            );

            // Upload the shader
            pb.copy_shader(config.bo_shader_offset, shader);

            // Populate and upload the QMD
            let qmd = pb.create_qmd(
                &self.qmd_heap,
                config.shader,
                config.bo_shader_offset,
                cb0_offset,
                config.invocations,
            )?;
            pb.dispatch_qmd(&qmd);
            config.qmd = Some(qmd);
        }

        let res = pb.submit(&self.ctx);

        for config in configs {
            // Always copy the data back to the caller, even if exec fails
            unsafe {
                let data_map = pb.data(config.bo_data_offset);
                if config.data_size > 0 {
                    std::ptr::copy(data_map, config.data, config.data_size);
                }
            }
        }

        res
    }

    pub fn run<T>(
        &self,
        shader: &nak_shader_bin,
        data: &mut [T],
    ) -> io::Result<()> {
        unsafe {
            let stride = size_of::<T>();
            self.run_raw([RunConfig::new(
                shader,
                data.len().try_into().unwrap(),
                stride.try_into().unwrap(),
                data.as_mut_ptr().cast(),
                size_of_val(data),
            )])
        }
    }

    pub fn run_multi<T, const N: usize>(
        &self,
        configs: [(&nak_shader_bin, &mut [T]); N],
    ) -> io::Result<()> {
        let configs = configs.map(|(shader, data)| {
            let stride = size_of::<T>();
            RunConfig::new(
                shader,
                data.len().try_into().unwrap(),
                stride.try_into().unwrap(),
                data.as_mut_ptr().cast(),
                size_of_val(data),
            )
        });

        unsafe { self.run_raw(configs) }
    }
}

unsafe impl Sync for Runner {}
unsafe impl Send for Runner {}
