use crate::api::icd::*;
use crate::api::types::*;
use crate::api::util::*;
use crate::core::context::*;
use crate::core::device::get_devs_for_type;
use crate::core::platform::*;

use mesa_rust_util::properties::Properties;
use rusticl_opencl_gen::*;
use rusticl_proc_macros::cl_entrypoint;
use rusticl_proc_macros::cl_info_entrypoint;

use std::collections::HashSet;
use std::iter::FromIterator;
use std::mem::MaybeUninit;
use std::slice;

#[cl_info_entrypoint(cl_get_context_info)]
impl CLInfo<cl_context_info> for cl_context {
    fn query(&self, q: cl_context_info, _: &[u8]) -> CLResult<Vec<MaybeUninit<u8>>> {
        let ctx = self.get_ref()?;
        Ok(match q {
            CL_CONTEXT_DEVICES => cl_prop::<Vec<cl_device_id>>(
                ctx.devs
                    .iter()
                    .map(|&d| cl_device_id::from_ptr(d))
                    .collect(),
            ),
            CL_CONTEXT_NUM_DEVICES => cl_prop::<cl_uint>(ctx.devs.len() as u32),
            CL_CONTEXT_PROPERTIES => cl_prop::<&Properties<cl_context_properties>>(&ctx.properties),
            CL_CONTEXT_REFERENCE_COUNT => cl_prop::<cl_uint>(self.refcnt()?),
            // CL_INVALID_VALUE if param_name is not one of the supported values
            _ => return Err(CL_INVALID_VALUE),
        })
    }
}

#[cl_entrypoint]
fn create_context(
    properties: *const cl_context_properties,
    num_devices: cl_uint,
    devices: *const cl_device_id,
    pfn_notify: Option<FuncCreateContextCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<cl_context> {
    // TODO: Actually hook this callback up so it gets called when appropriate.
    // SAFETY: The requirements on `CreateContextCB::try_new` match the requirements
    // imposed by the OpenCL specification. It is the caller's duty to uphold them.
    let _cb_opt = unsafe { CreateContextCB::try_new(pfn_notify, user_data)? };

    // CL_INVALID_VALUE if devices is NULL.
    if devices.is_null() {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_VALUE if num_devices is equal to zero.
    if num_devices == 0 {
        return Err(CL_INVALID_VALUE);
    }

    // CL_INVALID_PROPERTY [...] if the same property name is specified more than once.
    let props = Properties::from_ptr(properties).ok_or(CL_INVALID_PROPERTY)?;
    for p in &props.props {
        match p.0 as u32 {
            // CL_INVALID_PLATFORM [...] if platform value specified in properties is not a valid platform.
            CL_CONTEXT_PLATFORM => {
                (p.1 as cl_platform_id).get_ref()?;
            }
            CL_CONTEXT_INTEROP_USER_SYNC => {
                check_cl_bool(p.1).ok_or(CL_INVALID_PROPERTY)?;
            }
            // CL_INVALID_PROPERTY if context property name in properties is not a supported property name
            _ => return Err(CL_INVALID_PROPERTY),
        }
    }

    // Duplicate devices specified in devices are ignored.
    let set: HashSet<_> =
        HashSet::from_iter(unsafe { slice::from_raw_parts(devices, num_devices as usize) }.iter());
    let devs: Result<_, _> = set.into_iter().map(cl_device_id::get_ref).collect();

    Ok(cl_context::from_arc(Context::new(devs?, props)))
}

#[cl_entrypoint]
fn create_context_from_type(
    properties: *const cl_context_properties,
    device_type: cl_device_type,
    pfn_notify: Option<FuncCreateContextCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<cl_context> {
    // CL_INVALID_DEVICE_TYPE if device_type is not a valid value.
    check_cl_device_type(device_type)?;

    let devs: Vec<_> = get_devs_for_type(device_type)
        .into_iter()
        .map(|d| cl_device_id::from_ptr(d))
        .collect();

    // CL_DEVICE_NOT_FOUND if no devices that match device_type and property values specified in properties were found.
    if devs.is_empty() {
        return Err(CL_DEVICE_NOT_FOUND);
    }

    // errors are essentially the same and we will always pass in a valid
    // device list, so that's fine as well.
    create_context(
        properties,
        devs.len() as u32,
        devs.as_ptr(),
        pfn_notify,
        user_data,
    )
}

#[cl_entrypoint]
fn retain_context(context: cl_context) -> CLResult<()> {
    context.retain()
}

#[cl_entrypoint]
fn release_context(context: cl_context) -> CLResult<()> {
    context.release()
}

#[cl_entrypoint]
fn set_context_destructor_callback(
    context: cl_context,
    pfn_notify: ::std::option::Option<FuncDeleteContextCB>,
    user_data: *mut ::std::os::raw::c_void,
) -> CLResult<()> {
    let c = context.get_ref()?;

    // SAFETY: The requirements on `DeleteContextCB::new` match the requirements
    // imposed by the OpenCL specification. It is the caller's duty to uphold them.
    let cb = unsafe { DeleteContextCB::new(pfn_notify, user_data)? };

    c.dtors.lock().unwrap().push(cb);
    Ok(())
}
