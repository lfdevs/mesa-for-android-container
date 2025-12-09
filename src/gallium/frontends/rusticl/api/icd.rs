// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

#![allow(non_snake_case)]

use crate::api::context::*;
use crate::api::device::*;
use crate::api::event::*;
use crate::api::kernel::*;
use crate::api::memory::*;
use crate::api::platform;
use crate::api::platform::*;
use crate::api::program::*;
use crate::api::queue::*;
use crate::api::semaphore::*;
use crate::api::types::*;
use crate::api::util::*;
use crate::core::platform::*;

use mesa_rust_util::ptr::*;
use rusticl_opencl_gen::*;

use std::ffi::c_char;
use std::ffi::c_void;
use std::ffi::CStr;
use std::mem;
use std::ptr;
use std::sync::Arc;

// somehow bindgen ignores those
#[cfg(target_pointer_width = "32")]
const CL_ICD2_TAG_KHR: isize = 0x434C3331;

#[cfg(target_pointer_width = "64")]
const CL_ICD2_TAG_KHR: isize = 0x4F50454E434C3331;

const fn cl_icd_dispatch_default() -> cl_icd_dispatch {
    const ELEMS: usize = size_of::<cl_icd_dispatch>() / size_of::<Option<fn()>>();

    // SAFETY: cl_icd_dispatch is a list of function pointers and we set them all to None
    unsafe { mem::transmute([None::<fn()>; ELEMS]) }
}

macro_rules! cl_dispatch {
    ([$($func:ident: Some($val:tt)),*$(,)?]) => {
        pub static DISPATCH: cl_icd_dispatch = cl_icd_dispatch {
            anon_1: _cl_icd_dispatch__bindgen_ty_1 {
                clGetPlatformIDs_icd2_tag: CL_ICD2_TAG_KHR,
            },
            anon_2: _cl_icd_dispatch__bindgen_ty_2 {
                clUnloadCompiler_icd2_tag: CL_ICD2_TAG_KHR,
            },
            $($func: Some($val)),*,
            ..cl_icd_dispatch_default()
        };

        unsafe extern "C" fn clIcdGetFunctionAddressForPlatformKHR(
            platform: cl_platform_id,
            func_name: *const c_char,
        ) -> *mut c_void {
            // A return value of NULL indicates that [..] platform is not a valid platform for the
            // implementation.
            if platform.get_ref().is_err() {
                return ptr::null_mut();
            };

            // SAFETY: func_name is a proper UTF-8 encoded nul terminated string.
            let Ok(func_name) = unsafe { CStr::from_ptr(func_name) }.to_str() else {
                return ptr::null_mut();
            };

            match func_name {
                $(stringify!($func) => $val as _,)*
                // A return value of NULL indicates that the specified function does not exist for
                // platform.
                _ => ptr::null_mut()
            }
        }
    };
}

cl_dispatch!([
    clGetPlatformInfo: Some(clGetPlatformInfo),
    clGetDeviceIDs: Some(clGetDeviceIDs),
    clGetDeviceInfo: Some(clGetDeviceInfo),
    clCreateContext: Some(clCreateContext),
    clCreateContextFromType: Some(clCreateContextFromType),
    clRetainContext: Some(clRetainContext),
    clReleaseContext: Some(clReleaseContext),
    clGetContextInfo: Some(clGetContextInfo),
    clCreateCommandQueue: Some(clCreateCommandQueue),
    clRetainCommandQueue: Some(clRetainCommandQueue),
    clReleaseCommandQueue: Some(clReleaseCommandQueue),
    clGetCommandQueueInfo: Some(clGetCommandQueueInfo),
    clSetCommandQueueProperty: Some(clSetCommandQueueProperty),
    clCreateBuffer: Some(clCreateBuffer),
    clCreateImage2D: Some(clCreateImage2D),
    clCreateImage3D: Some(clCreateImage3D),
    clRetainMemObject: Some(clRetainMemObject),
    clReleaseMemObject: Some(clReleaseMemObject),
    clGetSupportedImageFormats: Some(clGetSupportedImageFormats),
    clGetMemObjectInfo: Some(clGetMemObjectInfo),
    clGetImageInfo: Some(clGetImageInfo),
    clCreateSampler: Some(clCreateSampler),
    clRetainSampler: Some(clRetainSampler),
    clReleaseSampler: Some(clReleaseSampler),
    clGetSamplerInfo: Some(clGetSamplerInfo),
    clCreateProgramWithSource: Some(clCreateProgramWithSource),
    clCreateProgramWithBinary: Some(clCreateProgramWithBinary),
    clRetainProgram: Some(clRetainProgram),
    clReleaseProgram: Some(clReleaseProgram),
    clBuildProgram: Some(clBuildProgram),
    clGetProgramInfo: Some(clGetProgramInfo),
    clGetProgramBuildInfo: Some(clGetProgramBuildInfo),
    clCreateKernel: Some(clCreateKernel),
    clCreateKernelsInProgram: Some(clCreateKernelsInProgram),
    clRetainKernel: Some(clRetainKernel),
    clReleaseKernel: Some(clReleaseKernel),
    clSetKernelArg: Some(clSetKernelArg),
    clGetKernelInfo: Some(clGetKernelInfo),
    clGetKernelWorkGroupInfo: Some(clGetKernelWorkGroupInfo),
    clWaitForEvents: Some(clWaitForEvents),
    clGetEventInfo: Some(clGetEventInfo),
    clRetainEvent: Some(clRetainEvent),
    clReleaseEvent: Some(clReleaseEvent),
    clGetEventProfilingInfo: Some(clGetEventProfilingInfo),
    clFlush: Some(clFlush),
    clFinish: Some(clFinish),
    clEnqueueReadBuffer: Some(clEnqueueReadBuffer),
    clEnqueueWriteBuffer: Some(clEnqueueWriteBuffer),
    clEnqueueCopyBuffer: Some(clEnqueueCopyBuffer),
    clEnqueueReadImage: Some(clEnqueueReadImage),
    clEnqueueWriteImage: Some(clEnqueueWriteImage),
    clEnqueueCopyImage: Some(clEnqueueCopyImage),
    clEnqueueCopyImageToBuffer: Some(clEnqueueCopyImageToBuffer),
    clEnqueueCopyBufferToImage: Some(clEnqueueCopyBufferToImage),
    clEnqueueMapBuffer: Some(clEnqueueMapBuffer),
    clEnqueueMapImage: Some(clEnqueueMapImage),
    clEnqueueUnmapMemObject: Some(clEnqueueUnmapMemObject),
    clEnqueueNDRangeKernel: Some(clEnqueueNDRangeKernel),
    clEnqueueTask: Some(clEnqueueTask),
    // clEnqueueNativeKernel: None,
    clEnqueueMarker: Some(clEnqueueMarker),
    // clEnqueueWaitForEvents: None,
    clEnqueueBarrier: Some(clEnqueueBarrier),
    clGetExtensionFunctionAddress: Some(clGetExtensionFunctionAddress),
    clCreateFromGLBuffer: Some(clCreateFromGLBuffer),
    clCreateFromGLTexture2D: Some(clCreateFromGLTexture2D),
    clCreateFromGLTexture3D: Some(clCreateFromGLTexture3D),
    clCreateFromGLRenderbuffer: Some(clCreateFromGLRenderbuffer),
    clGetGLObjectInfo: Some(clGetGLObjectInfo),
    clGetGLTextureInfo: Some(clGetGLTextureInfo),
    clEnqueueAcquireGLObjects: Some(clEnqueueAcquireGLObjects),
    clEnqueueReleaseGLObjects: Some(clEnqueueReleaseGLObjects),
    clGetGLContextInfoKHR: Some(clGetGLContextInfoKHR),
    clSetEventCallback: Some(clSetEventCallback),
    clCreateSubBuffer: Some(clCreateSubBuffer),
    clSetMemObjectDestructorCallback: Some(clSetMemObjectDestructorCallback),
    clCreateUserEvent: Some(clCreateUserEvent),
    clSetUserEventStatus: Some(clSetUserEventStatus),
    clEnqueueReadBufferRect: Some(clEnqueueReadBufferRect),
    clEnqueueWriteBufferRect: Some(clEnqueueWriteBufferRect),
    clEnqueueCopyBufferRect: Some(clEnqueueCopyBufferRect),
    // clCreateSubDevicesEXT: None,
    // clRetainDeviceEXT: None,
    // clReleaseDeviceEXT: None,
    // clCreateEventFromGLsyncKHR: None,
    clCreateSubDevices: Some(clCreateSubDevices),
    clRetainDevice: Some(clRetainDevice),
    clReleaseDevice: Some(clReleaseDevice),
    clCreateImage: Some(clCreateImage),
    // clCreateProgramWithBuiltInKernels: None,
    clCompileProgram: Some(clCompileProgram),
    clLinkProgram: Some(clLinkProgram),
    clUnloadPlatformCompiler: Some(clUnloadPlatformCompiler),
    clGetKernelArgInfo: Some(clGetKernelArgInfo),
    clEnqueueFillBuffer: Some(clEnqueueFillBuffer),
    clEnqueueFillImage: Some(clEnqueueFillImage),
    clEnqueueMigrateMemObjects: Some(clEnqueueMigrateMemObjects),
    clEnqueueMarkerWithWaitList: Some(clEnqueueMarkerWithWaitList),
    clEnqueueBarrierWithWaitList: Some(clEnqueueBarrierWithWaitList),
    clGetExtensionFunctionAddressForPlatform: Some(clGetExtensionFunctionAddressForPlatform),
    clCreateFromGLTexture: Some(clCreateFromGLTexture),
    // clCreateFromEGLImageKHR: None,
    // clEnqueueAcquireEGLObjectsKHR: None,
    // clEnqueueReleaseEGLObjectsKHR: None,
    // clCreateEventFromEGLSyncKHR: None,
    clCreateCommandQueueWithProperties: Some(clCreateCommandQueueWithProperties),
    clCreatePipe: Some(clCreatePipe),
    clGetPipeInfo: Some(clGetPipeInfo),
    clSVMAlloc: Some(clSVMAlloc),
    clSVMFree: Some(clSVMFree),
    clEnqueueSVMFree: Some(clEnqueueSVMFree),
    clEnqueueSVMMemcpy: Some(clEnqueueSVMMemcpy),
    clEnqueueSVMMemFill: Some(clEnqueueSVMMemFill),
    clEnqueueSVMMap: Some(clEnqueueSVMMap),
    clEnqueueSVMUnmap: Some(clEnqueueSVMUnmap),
    clCreateSamplerWithProperties: Some(clCreateSamplerWithProperties),
    clSetKernelArgSVMPointer: Some(clSetKernelArgSVMPointer),
    clSetKernelExecInfo: Some(clSetKernelExecInfo),
    clGetKernelSubGroupInfoKHR: Some(clGetKernelSubGroupInfo),
    clCloneKernel: Some(clCloneKernel),
    clCreateProgramWithIL: Some(clCreateProgramWithIL),
    clEnqueueSVMMigrateMem: Some(clEnqueueSVMMigrateMem),
    clGetDeviceAndHostTimer: Some(clGetDeviceAndHostTimer),
    clGetHostTimer: Some(clGetHostTimer),
    clGetKernelSubGroupInfo: Some(clGetKernelSubGroupInfo),
    clSetDefaultDeviceCommandQueue: Some(clSetDefaultDeviceCommandQueue),
    clSetProgramReleaseCallback: Some(clSetProgramReleaseCallback),
    clSetProgramSpecializationConstant: Some(clSetProgramSpecializationConstant),
    clCreateBufferWithProperties: Some(clCreateBufferWithProperties),
    clCreateImageWithProperties: Some(clCreateImageWithProperties),
    clSetContextDestructorCallback: Some(clSetContextDestructorCallback),
]);

pub type CLError = cl_int;
pub type CLResult<T> = Result<T, CLError>;

#[derive(Clone, Copy, PartialEq)]
#[repr(u32)]
pub enum RusticlTypes {
    // random number
    Buffer = 0xec4cf9a9,
    Context,
    Device,
    Event,
    Image,
    Kernel,
    Program,
    Queue,
    Sampler,
    Semaphore,
}

impl RusticlTypes {
    pub const fn u32(&self) -> u32 {
        *self as u32
    }

    pub const fn from_u32(val: u32) -> Option<Self> {
        let result = match val {
            0xec4cf9a9 => Self::Buffer,
            0xec4cf9aa => Self::Context,
            0xec4cf9ab => Self::Device,
            0xec4cf9ac => Self::Event,
            0xec4cf9ad => Self::Image,
            0xec4cf9ae => Self::Kernel,
            0xec4cf9af => Self::Program,
            0xec4cf9b0 => Self::Queue,
            0xec4cf9b1 => Self::Sampler,
            0xec4cf9b2 => Self::Semaphore,
            _ => return None,
        };
        debug_assert!(result.u32() == val);
        Some(result)
    }
}

#[repr(C)]
pub struct CLObjectBase<const ERR: i32> {
    dispatch: &'static cl_icd_dispatch,
    pub dispatch_data: usize,
    rusticl_type: u32,
}

impl<const ERR: i32> CLObjectBase<ERR> {
    pub fn new(t: RusticlTypes) -> Self {
        Self {
            dispatch: &DISPATCH,
            dispatch_data: Platform::get().dispatch_data,
            rusticl_type: t.u32(),
        }
    }

    pub fn new_no_dispatch(t: RusticlTypes) -> Self {
        Self {
            dispatch: &DISPATCH,
            dispatch_data: 0,
            rusticl_type: t.u32(),
        }
    }

    pub fn check_ptr(ptr: *const Self) -> CLResult<RusticlTypes> {
        if ptr.is_null() {
            return Err(ERR);
        }

        unsafe {
            if !::std::ptr::eq((*ptr).dispatch, &DISPATCH) {
                return Err(ERR);
            }

            let Some(ty) = RusticlTypes::from_u32((*ptr).rusticl_type) else {
                return Err(ERR);
            };

            Ok(ty)
        }
    }

    pub fn get_type(&self) -> CLResult<RusticlTypes> {
        RusticlTypes::from_u32(self.rusticl_type).ok_or(ERR)
    }
}

pub trait ReferenceCountedAPIPointer<T, const ERR: i32> {
    fn get_ptr(&self) -> CLResult<*const T>;

    // TODO:  I can't find a trait that would let me say T: pointer so that
    // I can do the cast in the main trait implementation.  So we need to
    // implement that as part of the macro where we know the real type.
    fn from_ptr(ptr: *const T) -> Self;
}

pub trait BaseCLObject<'a, const ERR: i32, CL: ReferenceCountedAPIPointer<Self, ERR> + 'a>:
    Sized
{
    fn ref_from_raw(obj: CL) -> CLResult<&'a Self> {
        let obj = obj.get_ptr()?;
        // SAFETY: `get_ptr` already checks if it's one of our pointers and not null
        Ok(unsafe { &*obj })
    }

    fn refs_from_arr(objs: *const CL, count: u32) -> CLResult<Vec<&'a Self>>
    where
        CL: Copy,
    {
        // CL spec requires validation for obj arrays, both values have to make sense
        if objs.is_null() && count > 0 || !objs.is_null() && count == 0 {
            return Err(CL_INVALID_VALUE);
        }

        let mut res = Vec::new();
        if objs.is_null() || count == 0 {
            return Ok(res);
        }

        for i in 0..count as usize {
            res.push(Self::ref_from_raw(unsafe { *objs.add(i) })?);
        }
        Ok(res)
    }
}

pub trait ArcedCLObject<'a, const ERR: i32, CL: ReferenceCountedAPIPointer<Self, ERR> + 'a>:
    Sized + BaseCLObject<'a, ERR, CL>
{
    /// Note: this operation increases the internal ref count as `ref_from_raw` is the better option
    /// when an Arc is not needed.
    fn arc_from_raw(ptr: CL) -> CLResult<Arc<Self>> {
        let ptr = ptr.get_ptr()?;
        // SAFETY: `get_ptr` already checks if it's one of our pointers.
        Ok(unsafe {
            Arc::increment_strong_count(ptr);
            Arc::from_raw(ptr)
        })
    }

    fn arcs_from_arr(objs: *const CL, count: u32) -> CLResult<Vec<Arc<Self>>>
    where
        CL: Copy,
    {
        // CL spec requires validation for obj arrays, both values have to make sense
        if objs.is_null() && count > 0 || !objs.is_null() && count == 0 {
            return Err(CL_INVALID_VALUE);
        }

        let mut res = Vec::new();
        if objs.is_null() || count == 0 {
            return Ok(res);
        }

        for i in 0..count as usize {
            unsafe {
                res.push(Self::arc_from_raw(*objs.add(i))?);
            }
        }
        Ok(res)
    }

    fn refcnt(ptr: CL) -> CLResult<u32> {
        let ptr = ptr.get_ptr()?;
        // SAFETY: `get_ptr` already checks if it's one of our pointers.
        let arc = unsafe { Arc::from_raw(ptr) };
        let res = Arc::strong_count(&arc);
        // leak the arc again, so we don't reduce the refcount by dropping `arc`
        let _ = Arc::into_raw(arc);
        Ok(res as u32)
    }

    fn into_cl(self: Arc<Self>) -> CL {
        CL::from_ptr(Arc::into_raw(self))
    }

    fn release(ptr: CL) -> CLResult<()> {
        let ptr = ptr.get_ptr()?;
        // SAFETY: `get_ptr` already checks if it's one of our pointers.
        unsafe { Arc::decrement_strong_count(ptr) };
        Ok(())
    }

    fn retain(ptr: CL) -> CLResult<()> {
        let ptr = ptr.get_ptr()?;
        // SAFETY: `get_ptr` already checks if it's one of our pointers.
        unsafe { Arc::increment_strong_count(ptr) };
        Ok(())
    }
}

#[macro_export]
macro_rules! impl_cl_type_trait_base {
    (@BASE $cl: ident, $t: ident, [$($types: ident),+], $err: ident, $($field:ident).+) => {
        impl $crate::api::icd::ReferenceCountedAPIPointer<$t, $err> for $cl {
            fn get_ptr(&self) -> $crate::api::icd::CLResult<*const $t> {
                type Base = $crate::api::icd::CLObjectBase<$err>;
                let t = Base::check_ptr(self.cast())?;
                if ![$($crate::api::icd::RusticlTypes::$types),+].contains(&t) {
                    return Err($err);
                }

                let offset = ::std::mem::offset_of!($t, $($field).+);
                // SAFETY: We offset the pointer back from the ICD specified base type to our
                //         internal type.
                let obj_ptr: *const $t = unsafe { self.byte_sub(offset) }.cast();

                // Check at compile-time that we indeed got the right path
                unsafe { let _: &Base = &(*obj_ptr).$($field).+; }

                Ok(obj_ptr)
            }

            fn from_ptr(ptr: *const $t) -> Self {
                if ptr.is_null() {
                    return std::ptr::null_mut();
                }
                let offset = ::std::mem::offset_of!($t, $($field).+);
                // SAFETY: The resulting pointer is safe as we simply offset into the ICD specified
                //         base type.
                unsafe { ptr.byte_add(offset) as Self }
            }
        }

        impl $crate::api::icd::BaseCLObject<'_, $err, $cl> for $t {}

        impl $t {
            fn _ensure_send_sync(&self) -> impl Send + Sync + '_ {
                self
            }
        }

        // there are two reason to implement those traits for all objects
        //   1. it speeds up operations
        //   2. we want to check for real equality more explicit to stay conformant with the API
        //      and to not break in subtle ways e.g. using CL objects as keys in HashMaps.
        impl std::cmp::Eq for $t {}
        impl std::cmp::PartialEq for $t {
            fn eq(&self, other: &Self) -> bool {
                std::ptr::addr_eq(self, other)
            }
        }

        impl std::hash::Hash for $t {
            fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
                std::ptr::from_ref(self).hash(state);
            }
        }
    };

    ($cl: ident, $t: ident, [$($types: ident),+], $err: ident, $($field:ident).+) => {
        $crate::impl_cl_type_trait_base!(@BASE $cl, $t, [$($types),+], $err, $($field).+);
    };

    ($cl: ident, $t: ident, [$($types: ident),+], $err: ident) => {
        $crate::impl_cl_type_trait_base!($cl, $t, [$($types),+], $err, base);
    };
}

#[macro_export]
macro_rules! impl_cl_type_trait {
    ($cl: ident, $t: ident, $err: ident, $($field:ident).+) => {
        $crate::impl_cl_type_trait_base!(@BASE $cl, $t, [$t], $err, $($field).+);
        impl $crate::api::icd::ArcedCLObject<'_, $err, $cl> for $t {}
    };

    ($cl: ident, $t: ident, $err: ident) => {
        $crate::impl_cl_type_trait!($cl, $t, $err, base);
    };
}

// SAFETY: The OpenCL spec demands this function to be exported with its plain C name
#[unsafe(no_mangle)]
pub unsafe extern "C" fn clGetPlatformInfo(
    platform: cl_platform_id,
    param_name: cl_platform_info,
    param_value_size: usize,
    param_value: *mut ::std::ffi::c_void,
    param_value_size_ret: *mut usize,
) -> cl_int {
    unsafe {
        platform::clGetPlatformInfo(
            platform,
            param_name,
            param_value_size,
            param_value,
            param_value_size_ret,
        )
    }
}

// SAFETY: The OpenCL spec demands this function to be exported with its plain C name
#[unsafe(no_mangle)]
pub unsafe extern "C" fn clIcdGetPlatformIDsKHR(
    num_entries: cl_uint,
    platforms: *mut cl_platform_id,
    num_platforms: *mut cl_uint,
) -> cl_int {
    unsafe { clGetPlatformIDs(num_entries, platforms, num_platforms) }
}

macro_rules! cl_ext_func {
    ($func:ident: $api_type:ident) => {{
        let _func: $api_type = Some($func);
        $func as *mut ::std::ffi::c_void
    }};
}

#[rustfmt::skip]
// SAFETY: The OpenCL spec demands this function to be exported with its plain C name
#[unsafe(no_mangle)]
pub extern "C" fn clGetExtensionFunctionAddress(
    function_name: *const c_char,
) -> *mut c_void {
    if function_name.is_null() {
        return ptr::null_mut();
    }
    match unsafe { CStr::from_ptr(function_name) }.to_str().unwrap() {
        // cl_khr_create_command_queue
        "clCreateCommandQueueWithPropertiesKHR" => cl_ext_func!(clCreateCommandQueueWithProperties: clCreateCommandQueueWithPropertiesKHR_fn),

        // cl_khr_external_memory
        "clEnqueueAcquireExternalMemObjectsKHR" => cl_ext_func!(clEnqueueAcquireExternalMemObjectsKHR: clEnqueueAcquireExternalMemObjectsKHR_fn),
        "clEnqueueReleaseExternalMemObjectsKHR" => cl_ext_func!(clEnqueueReleaseExternalMemObjectsKHR: clEnqueueReleaseExternalMemObjectsKHR_fn),

        // cl_khr_external_semaphore
        "clGetSemaphoreHandleForTypeKHR" => cl_ext_func!(clGetSemaphoreHandleForTypeKHR: clGetSemaphoreHandleForTypeKHR_fn),

        // cl_khr_external_semaphore_sync_fd
        "clReImportSemaphoreSyncFdKHR" => cl_ext_func!(clReImportSemaphoreSyncFdKHR: clReImportSemaphoreSyncFdKHR_fn),

        // cl_khr_gl_sharing
        "clCreateFromGLBuffer" => cl_ext_func!(clCreateFromGLBuffer: clCreateFromGLBuffer_fn),
        "clCreateFromGLRenderbuffer" => cl_ext_func!(clCreateFromGLRenderbuffer: clCreateFromGLRenderbuffer_fn),
        "clCreateFromGLTexture" => cl_ext_func!(clCreateFromGLTexture: clCreateFromGLTexture_fn),
        "clCreateFromGLTexture2D" => cl_ext_func!(clCreateFromGLTexture2D: clCreateFromGLTexture2D_fn),
        "clCreateFromGLTexture3D" => cl_ext_func!(clCreateFromGLTexture3D: clCreateFromGLTexture3D_fn),
        "clEnqueueAcquireGLObjects" => cl_ext_func!(clEnqueueAcquireGLObjects: clEnqueueAcquireGLObjects_fn),
        "clEnqueueReleaseGLObjects" => cl_ext_func!(clEnqueueReleaseGLObjects: clEnqueueReleaseGLObjects_fn),
        "clGetGLContextInfoKHR" => cl_ext_func!(clGetGLContextInfoKHR: clGetGLContextInfoKHR_fn),
        "clGetGLObjectInfo" => cl_ext_func!(clGetGLObjectInfo: clGetGLObjectInfo_fn),
        "clGetGLTextureInfo" => cl_ext_func!(clGetGLTextureInfo: clGetGLTextureInfo_fn),

        // cl_khr_icd
        "clGetPlatformInfo" => cl_ext_func!(clGetPlatformInfo: clGetPlatformInfo_fn),
        "clIcdGetPlatformIDsKHR" => cl_ext_func!(clIcdGetPlatformIDsKHR: clIcdGetPlatformIDsKHR_fn),
        "clIcdGetFunctionAddressForPlatformKHR" => cl_ext_func!(clIcdGetFunctionAddressForPlatformKHR: clIcdGetFunctionAddressForPlatformKHR_fn),
        "clIcdSetPlatformDispatchDataKHR" => cl_ext_func!(clIcdSetPlatformDispatchDataKHR: clIcdSetPlatformDispatchDataKHR_fn),

        // cl_khr_il_program
        "clCreateProgramWithILKHR" => cl_ext_func!(clCreateProgramWithIL: clCreateProgramWithILKHR_fn),

        // cl_khr_semaphore
        "clCreateSemaphoreWithPropertiesKHR" => cl_ext_func!(clCreateSemaphoreWithPropertiesKHR: clCreateSemaphoreWithPropertiesKHR_fn),
        "clEnqueueSignalSemaphoresKHR" => cl_ext_func!(clEnqueueSignalSemaphoresKHR: clEnqueueSignalSemaphoresKHR_fn),
        "clEnqueueWaitSemaphoresKHR" => cl_ext_func!(clEnqueueWaitSemaphoresKHR: clEnqueueWaitSemaphoresKHR_fn),
        "clGetSemaphoreInfoKHR" => cl_ext_func!(clGetSemaphoreInfoKHR: clGetSemaphoreInfoKHR_fn),
        "clReleaseSemaphoreKHR" => cl_ext_func!(clReleaseSemaphoreKHR: clReleaseSemaphoreKHR_fn),
        "clRetainSemaphoreKHR" => cl_ext_func!(clRetainSemaphoreKHR: clRetainSemaphoreKHR_t),

        // cl_khr_suggested_local_work_size
        "clGetKernelSuggestedLocalWorkSizeKHR" => cl_ext_func!(clGetKernelSuggestedLocalWorkSizeKHR: clGetKernelSuggestedLocalWorkSizeKHR_fn),

        // cl_ext_buffer_device_address
        "clSetKernelArgDevicePointerEXT" => cl_ext_func!(clSetKernelArgDevicePointerEXT: clSetKernelArgDevicePointerEXT_fn),

        // cl_arm_shared_virtual_memory
        "clEnqueueSVMFreeARM" => cl_ext_func!(clEnqueueSVMFreeARM: clEnqueueSVMFreeARM_fn),
        "clEnqueueSVMMapARM" => cl_ext_func!(clEnqueueSVMMapARM: clEnqueueSVMMapARM_fn),
        "clEnqueueSVMMemcpyARM" => cl_ext_func!(clEnqueueSVMMemcpyARM: clEnqueueSVMMemcpyARM_fn),
        "clEnqueueSVMMemFillARM" => cl_ext_func!(clEnqueueSVMMemFillARM: clEnqueueSVMMemFillARM_fn),
        "clEnqueueSVMUnmapARM" => cl_ext_func!(clEnqueueSVMUnmapARM: clEnqueueSVMUnmapARM_fn),
        "clSetKernelArgSVMPointerARM" => cl_ext_func!(clSetKernelArgSVMPointer: clSetKernelArgSVMPointerARM_fn),
        "clSetKernelExecInfoARM" => cl_ext_func!(clSetKernelExecInfo: clSetKernelExecInfoARM_fn),
        "clSVMAllocARM" => cl_ext_func!(clSVMAlloc: clSVMAllocARM_fn),
        "clSVMFreeARM" => cl_ext_func!(clSVMFree: clSVMFreeARM_fn),

        // DPCPP bug https://github.com/intel/llvm/issues/9964
        "clSetProgramSpecializationConstant" => cl_ext_func!(clSetProgramSpecializationConstant: clSetProgramSpecializationConstant_fn),

        _ => ptr::null_mut(),
    }
}

extern "C" fn clLinkProgram(
    context: cl_context,
    num_devices: cl_uint,
    device_list: *const cl_device_id,
    options: *const ::std::os::raw::c_char,
    num_input_programs: cl_uint,
    input_programs: *const cl_program,
    pfn_notify: Option<FuncProgramCB>,
    user_data: *mut ::std::os::raw::c_void,
    errcode_ret: *mut cl_int,
) -> cl_program {
    let (ptr, err) = match link_program(
        context,
        num_devices,
        device_list,
        options,
        num_input_programs,
        input_programs,
        pfn_notify,
        user_data,
    ) {
        Ok((prog, code)) => (prog, code),
        Err(e) => (ptr::null_mut(), e),
    };

    // Correct behavior when `errcode_ret` is null is unspecified, but by
    // analogy, we fail silently in that case.
    // SAFETY: Caller is responsible for providing a pointer valid for a write
    // of `size_of::<cl_int>()`.
    unsafe { errcode_ret.write_checked(err) };

    ptr
}

extern "C" fn clGetExtensionFunctionAddressForPlatform(
    _platform: cl_platform_id,
    function_name: *const ::std::os::raw::c_char,
) -> *mut ::std::os::raw::c_void {
    clGetExtensionFunctionAddress(function_name)
}

extern "C" fn clSVMAlloc(
    context: cl_context,
    flags: cl_svm_mem_flags,
    size: usize,
    alignment: ::std::os::raw::c_uint,
) -> *mut ::std::os::raw::c_void {
    svm_alloc(context, flags, size, alignment).unwrap_or(ptr::null_mut())
}

extern "C" fn clSVMFree(context: cl_context, svm_pointer: *mut ::std::os::raw::c_void) {
    svm_free(context, svm_pointer as usize).ok();
}

unsafe extern "C" fn clGetKernelSubGroupInfo(
    kernel: cl_kernel,
    device: cl_device_id,
    param_name: cl_kernel_sub_group_info,
    input_value_size: usize,
    input_value: *const ::std::os::raw::c_void,
    param_value_size: usize,
    param_value: *mut ::std::os::raw::c_void,
    param_value_size_ret: *mut usize,
) -> cl_int {
    match unsafe {
        kernel.get_info_obj(
            (device, input_value_size, input_value, param_value_size),
            param_name,
            param_value_size,
            param_value,
            param_value_size_ret,
        )
    } {
        Ok(_) => CL_SUCCESS as cl_int,
        Err(e) => e,
    }
}
