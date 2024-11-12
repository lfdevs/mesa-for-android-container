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

use std::io;
use std::ptr;
use std::ptr::NonNull;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

unsafe fn is_nvidia_device(dev: drmDevicePtr) -> bool {
    match (*dev).bustype as u32 {
        DRM_BUS_PCI => {
            let pci = &*(*dev).deviceinfo.pci;
            pci.vendor_id == (NVIDIA_VENDOR_ID as u16)
        }
        _ => false,
    }
}

#[repr(C)]
pub struct CB0 {
    pub data_addr_lo: u32,
    pub data_addr_hi: u32,
    pub data_stride: u32,
    pub invocations: u32,
}

struct BO<'a> {
    run: &'a Runner,
    bo: NonNull<nouveau_ws_bo>,
    pub addr: u64,
    pub map: *mut std::os::raw::c_void,
}

impl<'a> BO<'a> {
    fn new(run: &'a Runner, size: u64) -> io::Result<BO<'a>> {
        let size = size.next_multiple_of(4096);

        let mut map: *mut std::os::raw::c_void = std::ptr::null_mut();
        let bo = unsafe {
            nouveau_ws_bo_new_mapped(
                run.dev.as_ptr(),
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

        let addr = run.next_addr.fetch_add(size, Ordering::Relaxed);
        assert!(addr % 4096 == 0);

        unsafe {
            nouveau_ws_bo_bind_vma(
                run.dev.as_ptr(),
                bo.as_ptr(),
                addr,
                size,
                0, // bo_offset
                0, // pte_kind
            );
        }

        Ok(BO { run, bo, addr, map })
    }
}

impl Drop for BO<'_> {
    fn drop(&mut self) {
        unsafe {
            nouveau_ws_bo_unbind_vma(
                self.run.dev.as_ptr(),
                self.addr,
                self.bo.as_ref().size,
            );
            nouveau_ws_bo_destroy(self.bo.as_ptr());
        }
    }
}

pub struct Runner {
    dev: NonNull<nouveau_ws_device>,
    ctx: NonNull<nouveau_ws_context>,
    syncobj: u32,
    sync_value: Mutex<u64>,
    next_addr: AtomicU64,
}

impl<'a> Runner {
    pub fn new(dev_id: Option<usize>) -> Runner {
        unsafe {
            let mut drm_devices: [drmDevicePtr; 16] = std::mem::zeroed();
            let num_drm_devices = drmGetDevices(
                drm_devices.as_mut_ptr(),
                drm_devices.len().try_into().unwrap(),
            );

            assert!(num_drm_devices >= 0, "Failed to enumerate DRM devices");
            let num_drm_devices: usize = num_drm_devices.try_into().unwrap();

            let drm_dev = if let Some(dev_id) = dev_id {
                assert!(dev_id < num_drm_devices, "Unknown device {dev_id}");
                assert!(
                    is_nvidia_device(drm_devices[dev_id]),
                    "Device {dev_id} is not an NVIDIA device",
                );
                drm_devices[dev_id]
            } else {
                *drm_devices
                    .iter()
                    .find(|dev| is_nvidia_device(**dev))
                    .expect("Failed to find an NVIDIA device")
            };

            let dev = nouveau_ws_device_new(drm_dev);
            let dev =
                NonNull::new(dev).expect("Failed to create nouveau device");

            drmFreeDevices(
                drm_devices.as_mut_ptr(),
                num_drm_devices.try_into().unwrap(),
            );

            let mut ctx: *mut nouveau_ws_context = std::ptr::null_mut();
            let err = nouveau_ws_context_create(
                dev.as_ptr(),
                NOUVEAU_WS_ENGINE_COMPUTE,
                &mut ctx,
            );
            assert!(err == 0, "Failed to create nouveau context");
            let ctx = NonNull::new(ctx).unwrap();

            let mut syncobj = 0_u32;
            let err = drmSyncobjCreate(dev.as_ref().fd, 0, &mut syncobj);
            assert!(err == 0, "Failed to create syncobj");

            Runner {
                dev,
                ctx,
                syncobj,
                sync_value: Mutex::new(0),
                next_addr: AtomicU64::new(1 << 16),
            }
        }
    }

    pub fn dev_info(&self) -> &nv_device_info {
        unsafe { &self.dev.as_ref().info }
    }

    fn exec(&self, addr: u64, len: u16) -> io::Result<()> {
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
                self.dev.as_ref().fd,
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
                self.dev.as_ref().fd,
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
                self.dev.as_ref().fd,
                DRM_RS_IOCTL_NOUVEAU_EXEC.into(),
                ptr::from_mut(&mut exec).cast(),
            );
            if err != 0 {
                return Err(io::Error::last_os_error());
            }
        }

        Ok(())
    }

    pub unsafe fn run_raw(
        &self,
        shader: &nak_shader_bin,
        invocations: u32,
        data_stride: u32,
        data: *mut std::os::raw::c_void,
        data_size: usize,
    ) -> io::Result<()> {
        assert!(shader.info.stage == MESA_SHADER_COMPUTE);
        let cs_info = &shader.info.__bindgen_anon_1.cs;
        assert!(cs_info.local_size[1] == 1 && cs_info.local_size[2] == 1);
        let local_size = cs_info.local_size[0];

        // Compute the needed size of the buffer
        let mut size = 0_usize;

        const MAX_PUSH_DW: usize = 256;
        let push_offset = size;
        size = push_offset + 4 * MAX_PUSH_DW;

        const QMD_SIZE: usize = 64 * 4;
        let qmd_offset = size.next_multiple_of(0x100);
        size = qmd_offset + 4 * QMD_SIZE;

        let shader_offset = size.next_multiple_of(0x80);
        size = shader_offset + usize::try_from(shader.code_size).unwrap();

        let cb0_offset = size.next_multiple_of(256);
        size = cb0_offset + std::mem::size_of::<CB0>();

        let data_offset = size.next_multiple_of(256);
        size = data_offset + data_size;

        let bo = BO::new(self, size.try_into().unwrap())?;

        // Copy the data from the caller into our BO
        let data_addr = bo.addr + u64::try_from(data_offset).unwrap();
        let data_map = bo.map.byte_offset(data_offset.try_into().unwrap());
        std::ptr::copy(data, data_map, data_size);

        // Fill out cb0
        let cb0_addr = bo.addr + u64::try_from(cb0_offset).unwrap();
        let cb0_map = bo.map.byte_offset(cb0_offset.try_into().unwrap());
        cb0_map.cast::<CB0>().write(CB0 {
            data_addr_lo: data_addr as u32,
            data_addr_hi: (data_addr >> 32) as u32,
            data_stride,
            invocations,
        });

        // Upload the shader
        let shader_addr = bo.addr + u64::try_from(shader_offset).unwrap();
        let shader_map = bo.map.byte_offset(shader_offset.try_into().unwrap());
        std::ptr::copy(
            shader.code,
            shader_map,
            shader.code_size.try_into().unwrap(),
        );

        // Populate and upload the QMD
        let mut qmd_cbufs: [nak_qmd_cbuf; 8] = unsafe { std::mem::zeroed() };
        qmd_cbufs[0] = nak_qmd_cbuf {
            index: 0,
            size: std::mem::size_of::<CB0>()
                .next_multiple_of(256)
                .try_into()
                .unwrap(),
            addr: cb0_addr,
        };
        let qmd_info = nak_qmd_info {
            // Pre-Volta, we set the program region to the start of the bo
            addr: if self.dev_info().cls_compute < VOLTA_COMPUTE_A {
                shader_offset.try_into().unwrap()
            } else {
                shader_addr
            },
            smem_size: 0,
            smem_max: 48 * 1024,
            global_size: [invocations.div_ceil(local_size.into()), 1, 1],
            num_cbufs: 1,
            cbufs: qmd_cbufs,
        };

        let qmd_addr = bo.addr + u64::try_from(qmd_offset).unwrap();
        let qmd_map = bo.map.byte_offset(qmd_offset.try_into().unwrap());
        nak_fill_qmd(
            self.dev_info(),
            &shader.info,
            &qmd_info,
            qmd_map,
            QMD_SIZE,
        );

        // Fill out the pushbuf
        let mut p = NvPush::new();

        p.push_method(cla0c0::SetObject {
            class_id: self.dev_info().cls_compute.into(),
            engine_id: 0,
        });
        if self.dev_info().cls_compute < VOLTA_COMPUTE_A {
            p.push_method(cla0c0::SetProgramRegionA {
                address_upper: (bo.addr >> 32) as u32,
            });
            p.push_method(cla0c0::SetProgramRegionB {
                address_lower: bo.addr as u32,
            });
        }

        let smem_base_addr = 0xfe000000_u32;
        let lmem_base_addr = 0xff000000_u32;
        if self.dev_info().cls_compute >= VOLTA_COMPUTE_A {
            p.push_method(clc3c0::SetShaderSharedMemoryWindowA {
                base_address_upper: 0,
            });
            p.push_method(clc3c0::SetShaderSharedMemoryWindowB {
                base_address: smem_base_addr,
            });

            p.push_method(clc3c0::SetShaderLocalMemoryWindowA {
                base_address_upper: 0,
            });
            p.push_method(clc3c0::SetShaderLocalMemoryWindowB {
                base_address: lmem_base_addr,
            });
        } else {
            p.push_method(cla0c0::SetShaderSharedMemoryWindow {
                base_address: smem_base_addr,
            });
            p.push_method(cla0c0::SetShaderLocalMemoryWindow {
                base_address: lmem_base_addr,
            });
        }

        if self.dev_info().cls_compute >= MAXWELL_COMPUTE_B {
            p.push_method(clb1c0::InvalidateSkedCaches { v: 0 });
        }

        p.push_method(cla0c0::SendPcasA {
            qmd_address_shifted8: (qmd_addr >> 8) as u32,
        });
        if self.dev_info().cls_compute >= AMPERE_COMPUTE_A {
            p.push_method(clc6c0::SendSignalingPcas2B {
                pcas_action: clc6c0::SendSignalingPcas2BPcasAction::InvalidateCopySchedule,
            });
        } else {
            p.push_method(cla0c0::SendSignalingPcasB {
                invalidate: true,
                schedule: true,
            });
        }

        let push_addr = bo.addr + u64::try_from(push_offset).unwrap();
        let push_map = bo.map.byte_offset(push_offset.try_into().unwrap());
        std::ptr::copy(p.as_ptr(), push_map.cast(), p.len());

        let res = self.exec(push_addr, (p.len() * 4).try_into().unwrap());

        // Always copy the data back to the caller, even if exec fails
        let data_map = bo.map.byte_offset(data_offset.try_into().unwrap());
        std::ptr::copy(data_map, data, data_size);

        res
    }

    pub fn run<T>(
        &self,
        shader: &nak_shader_bin,
        data: &mut [T],
    ) -> io::Result<()> {
        unsafe {
            let stride = std::mem::size_of::<T>();
            self.run_raw(
                shader,
                data.len().try_into().unwrap(),
                stride.try_into().unwrap(),
                data.as_mut_ptr().cast(),
                data.len() * stride,
            )
        }
    }
}

unsafe impl Sync for Runner {}
unsafe impl Send for Runner {}
