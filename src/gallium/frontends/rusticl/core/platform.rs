use crate::api::icd::CLResult;
use crate::api::icd::DISPATCH;
use crate::core::device::*;
use crate::core::version::*;

use mesa_rust_gen::*;
use rusticl_opencl_gen::*;

use std::env;
use std::sync::Arc;
use std::sync::Once;

#[repr(C)]
pub struct Platform {
    dispatch: &'static cl_icd_dispatch,
    pub extensions: [cl_name_version; 2],
    pub devs: Vec<Arc<Device>>,
    pub debug: PlatformDebug,
}

pub struct PlatformDebug {
    pub program: bool,
}

static PLATFORM_ONCE: Once = Once::new();
static mut PLATFORM: Platform = Platform {
    dispatch: &DISPATCH,
    extensions: [
        mk_cl_version_ext(1, 0, 0, "cl_khr_icd"),
        mk_cl_version_ext(1, 0, 0, "cl_khr_il_program"),
    ],
    devs: Vec::new(),
    debug: PlatformDebug { program: false },
};

impl Platform {
    pub fn as_ptr(&self) -> cl_platform_id {
        (self as *const Self) as cl_platform_id
    }

    pub fn get() -> &'static Self {
        // SAFETY: no concurrent static mut access due to std::Once
        PLATFORM_ONCE.call_once(|| unsafe { PLATFORM.init() });
        // SAFETY: no mut references exist at this point
        unsafe { &PLATFORM }
    }

    fn init(&mut self) {
        unsafe {
            glsl_type_singleton_init_or_ref();
        }

        self.devs.extend(Device::all());
        if let Ok(debug_flags) = env::var("RUSTICL_DEBUG") {
            for flag in debug_flags.split(',') {
                match flag {
                    "program" => self.debug.program = true,
                    _ => eprintln!("Unknown RUSTICL_DEBUG flag found: {}", flag),
                }
            }
        }
    }
}

impl Drop for Platform {
    fn drop(&mut self) {
        unsafe {
            glsl_type_singleton_decref();
        }
    }
}

pub trait GetPlatformRef {
    fn get_ref(&self) -> CLResult<&'static Platform>;
}

impl GetPlatformRef for cl_platform_id {
    fn get_ref(&self) -> CLResult<&'static Platform> {
        if !self.is_null() && *self == Platform::get().as_ptr() {
            Ok(Platform::get())
        } else {
            Err(CL_INVALID_PLATFORM)
        }
    }
}
