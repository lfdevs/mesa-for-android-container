// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::ffi::c_char;
use std::ffi::c_void;
use std::ffi::CStr;
use std::panic::catch_unwind;
use std::panic::AssertUnwindSafe;
use std::ptr::null_mut;
use std::slice::from_raw_parts;
use std::slice::from_raw_parts_mut;
use std::sync::Mutex;

use libc::EINVAL;
use libc::ESRCH;
use log::error;
use magma_gpu::util::FromRawDescriptor;
use magma_gpu::util::Handle as MagmaGpuHandle;
use magma_gpu::util::IntoRawDescriptor;
use magma_gpu::util::OwnedDescriptor;
use magma_gpu::util::RawDescriptor;
use magma_gpu::util::Result as MagmaGpuResult;
use magma_gpu::util::DEFAULT_RAW_DESCRIPTOR;
use magma_gpu::virtgpu_kumquat::defines::*;
use magma_gpu::virtgpu_kumquat::VirtGpuKumquat;

const NO_ERROR: i32 = 0;

fn return_result<T>(result: MagmaGpuResult<T>) -> i32 {
    if let Err(e) = result {
        error!("An error occurred: {}", e);
        -EINVAL
    } else {
        NO_ERROR
    }
}

macro_rules! return_on_error {
    ($result:expr) => {
        match $result {
            Ok(t) => t,
            Err(e) => {
                error!("An error occurred: {}", e);
                return -EINVAL;
            }
        }
    };
}

#[expect(non_camel_case_types)]
type virtgpu_kumquat_ffi = Mutex<VirtGpuKumquat>;

// The following structs (in define.rs) must be ABI-compatible with FFI header
// (virtgpu_kumquat_ffi.h).

#[expect(non_camel_case_types)]
type drm_kumquat_getparam = VirtGpuParam;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_unref = VirtGpuResourceUnref;

#[expect(non_camel_case_types)]
type drm_kumquat_get_caps = VirtGpuGetCaps;

#[expect(non_camel_case_types)]
type drm_kumquat_context_init = VirtGpuContextInit;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_create_3d = VirtGpuResourceCreate3D;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_create_blob = VirtGpuResourceCreateBlob;

#[expect(non_camel_case_types)]
type drm_kumquat_transfer_to_host = VirtGpuTransfer;

#[expect(non_camel_case_types)]
type drm_kumquat_transfer_from_host = VirtGpuTransfer;

#[expect(non_camel_case_types)]
type drm_kumquat_execbuffer = VirtGpuExecBuffer;

#[expect(non_camel_case_types)]
type drm_kumquat_wait = VirtGpuWait;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_map = VirtGpuResourceMap;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_export = VirtGpuResourceExport;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_import = VirtGpuResourceImport;

#[expect(non_camel_case_types)]
type drm_kumquat_resource_info = VirtGpuResourceInfo;

// SAFETY:
// The `ptr` must be a valid pointer to a `*mut virtgpu_kumquat_ffi`.
// If `gpu_socket` is `Some`, `gpu_socket` must be a valid, null-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_init(
    ptr: &mut *mut virtgpu_kumquat_ffi,
    gpu_socket: Option<&c_char>,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let gpu_socket_str = match gpu_socket {
            Some(value) => {
                // SAFETY:
                // The API user must pass in a valid C-string.
                let c_str_slice = unsafe { CStr::from_ptr(value) };
                let result = c_str_slice.to_str();
                return_on_error!(result)
            }
            None => "/tmp/kumquat-gpu-0",
        };

        let result = VirtGpuKumquat::new(gpu_socket_str);
        let kmqt = return_on_error!(result);
        *ptr = Box::into_raw(Box::new(Mutex::new(kmqt))) as _;
        NO_ERROR
    }))
    .unwrap_or(-ESRCH)
}

#[no_mangle]
pub extern "C" fn virtgpu_kumquat_finish(ptr: &mut *mut virtgpu_kumquat_ffi) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY:
        // The pointer `*ptr` must have been previously allocated by `virtgpu_kumquat_init`,
        // which uses `Box::into_raw`, and must not have been freed yet.
        let _ = unsafe { Box::from_raw(*ptr) };
        *ptr = null_mut();
        NO_ERROR
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// The `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` struct initialized
// by `virtgpu_kumquat_init`.
// The `cmd` pointer must be valid for writes.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_get_param(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_getparam,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().get_param(cmd);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`. `cmd` must be a valid pointer to a
// `drm_kumquat_get_caps` struct. `cmd.addr` must be a valid pointer to a
// buffer of at least `cmd.size` bytes, or null if `cmd.size` is 0. The
// buffer must be valid for writes.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_get_caps(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &drm_kumquat_get_caps,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let caps_slice = if cmd.size != 0 {
            // SAFETY:
            // The caller must ensure that `cmd.addr` points to a valid, mutable
            // memory block of at least `cmd.size` bytes.
            unsafe { from_raw_parts_mut(cmd.addr as *mut u8, cmd.size as usize) }
        } else {
            &mut []
        };
        let result = ptr.lock().unwrap().get_caps(cmd.cap_set_id, caps_slice);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`. `cmd` must be a valid pointer to a
// `drm_kumquat_context_init` struct. `cmd.ctx_set_params` must be a valid
// pointer to an array of `VirtGpuParam` of size `cmd.num_params`, or null
// if `cmd.num_params` is 0.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_context_init(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &drm_kumquat_context_init,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let context_params: &[VirtGpuParam] = if cmd.num_params != 0 {
            // SAFETY:
            // The caller must ensure that `cmd.ctx_set_params` points to a
            // valid memory block of at least `cmd.num_params` `VirtGpuParam`s.
            unsafe {
                from_raw_parts(
                    cmd.ctx_set_params as *const VirtGpuParam,
                    cmd.num_params as usize,
                )
            }
        } else {
            &[]
        };

        let mut capset_id: u64 = 0;

        for param in context_params {
            #[expect(clippy::single_match)]
            match param.param {
                VIRTGPU_KUMQUAT_CONTEXT_PARAM_CAPSET_ID => {
                    capset_id = param.value;
                }
                _ => (),
            }
        }

        let result = ptr.lock().unwrap().context_create(capset_id, "");
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_resource_create_3d` struct.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_resource_create_3d(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_create_3d,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().resource_create_3d(cmd);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_resource_create_blob` struct.
// `cmd.cmd` must be a valid pointer to a buffer of at least `cmd.cmd_size` bytes,
// or null if `cmd.cmd_size` is 0.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_resource_create_blob(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_create_blob,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let blob_cmd = if cmd.cmd_size != 0 {
            // SAFETY:
            // The caller must ensure that `cmd.cmd` points to a valid memory
            // block of at least `cmd.cmd_size` bytes.
            unsafe { from_raw_parts(cmd.cmd as *const u8, cmd.cmd_size as usize) }
        } else {
            &[]
        };
        let result = ptr.lock().unwrap().resource_create_blob(cmd, blob_cmd);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_resource_unref` struct.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_resource_unref(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_unref,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().resource_unref(cmd.bo_handle);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_resource_map` struct and be valid for writes.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_resource_map(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_map,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().map(cmd.bo_handle);
        let internal_map = return_on_error!(result);
        cmd.ptr = internal_map.ptr as *mut c_void;
        cmd.size = internal_map.size;
        NO_ERROR
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `bo_handle` must be a valid resource handle that has been previously mapped.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_resource_unmap(
    ptr: &mut virtgpu_kumquat_ffi,
    bo_handle: u32,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().unmap(bo_handle);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_transfer_to_host` struct.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_transfer_to_host(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_transfer_to_host,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().transfer_to_host(cmd);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_transfer_from_host` struct.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_transfer_from_host(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_transfer_from_host,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().transfer_from_host(cmd);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_execbuffer` struct.
// If `cmd.num_bo_handles` is not 0, `cmd.bo_handles` must point to a valid
// array of `u32` of at least `cmd.num_bo_handles` elements.
// If `cmd.size` is not 0, `cmd.command` must point to a valid buffer of at
// least `cmd.size` bytes.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_execbuffer(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_execbuffer,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let bo_handles = if cmd.num_bo_handles != 0 {
            // SAFETY:
            // The API user must pass in a valid array of bo_handles with correct size.
            unsafe { from_raw_parts(cmd.bo_handles as *const u32, cmd.num_bo_handles as usize) }
        } else {
            &[]
        };

        let cmd_buf = if cmd.size != 0 {
            // SAFETY:
            // The API user must pass in a valid command buffer with correct size.
            unsafe { from_raw_parts(cmd.command as *const u8, cmd.size as usize) }
        } else {
            &[]
        };

        // TODO
        let in_fences: &[u64] = &[0; 0];

        let mut descriptor: RawDescriptor = DEFAULT_RAW_DESCRIPTOR;
        let result = ptr.lock().unwrap().submit_command(
            cmd.flags,
            bo_handles,
            cmd_buf,
            cmd.ring_idx,
            in_fences,
            &mut descriptor,
        );

        cmd.fence_handle = descriptor as i64;
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_wait` struct.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_wait(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_wait,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().wait(cmd.bo_handle);
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

#[no_mangle]
pub extern "C" fn virtgpu_kumquat_resource_export(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_export,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr
            .get_mut()
            .unwrap()
            .resource_export(cmd.bo_handle, cmd.flags);
        let hnd = return_on_error!(result);

        cmd.handle_type = hnd.handle_type;
        cmd.os_handle = hnd.os_handle.into_raw_descriptor() as i64;
        NO_ERROR
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
// `cmd` must be a valid pointer to a `drm_kumquat_resource_import` struct.
// `cmd.os_handle` must be a valid OS handle, and ownership is transferred to this function.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_resource_import(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_import,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let handle = MagmaGpuHandle {
            // SAFETY:
            // The API user must transfer ownership of a valid OS handle.
            os_handle: unsafe {
                OwnedDescriptor::from_raw_descriptor(cmd.os_handle.into_raw_descriptor())
            },
            handle_type: cmd.handle_type,
        };

        let result = ptr.lock().unwrap().resource_import(
            handle,
            &mut cmd.bo_handle,
            &mut cmd.res_handle,
            &mut cmd.size,
        );

        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

#[no_mangle]
pub extern "C" fn virtgpu_kumquat_resource_info(
    ptr: &mut virtgpu_kumquat_ffi,
    cmd: &mut drm_kumquat_resource_info,
) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().resource_info(cmd.bo_handle);

        let info = return_on_error!(result);
        cmd.vulkan_info = info;
        NO_ERROR
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_snapshot_save(ptr: &mut virtgpu_kumquat_ffi) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().snapshot();
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}

// SAFETY:
// `ptr` must be a valid pointer to a `virtgpu_kumquat_ffi` obtained from
// `virtgpu_kumquat_init`.
#[no_mangle]
pub unsafe extern "C" fn virtgpu_kumquat_snapshot_restore(ptr: &mut virtgpu_kumquat_ffi) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let result = ptr.lock().unwrap().restore();
        return_result(result)
    }))
    .unwrap_or(-ESRCH)
}
