use std::ffi::c_void;
use std::io;
use std::ptr::null_mut;
use std::sync::Arc;

use crate::device::{MemoryBuffer, VirtualMemory};
use kraid_hw_runner_bindings::{
    hw_runner_invocation_info, hw_runner_layout_info, hw_runner_new_cmd_stream,
    hw_runner_shader_args,
};

pub struct InvocationInfo<'a> {
    pub code: &'a [u8],
    pub register_count: u8,
    pub register_preload: u64,
    pub invocations: u32,
    pub fau: &'a [u32],
    // Offset of ShaderArgs in fau
    pub fau_args_offset: usize,
    pub data: &'a mut [u8],
    pub data_stride: u32,
}

pub struct InvocationCmdStream {
    pub descr_buf: MemoryBuffer,
    pub data_buf: MemoryBuffer,
    // Offset of the command stream to run
    pub cs_offset: u64,
    pub cs_len: u64,
    data_len: usize,
}

impl InvocationCmdStream {
    pub fn cs_device_addr(&self) -> u64 {
        self.descr_buf.device_addr() + self.cs_offset
    }

    pub fn read_host_data(&self) -> &[u8] {
        self.data_buf.sync();

        unsafe {
            let addr = self.data_buf.host_addr() as *mut u8;
            std::slice::from_raw_parts(addr, self.data_len)
        }
    }
}

pub fn new_invocation_cs(
    mem: &Arc<VirtualMemory>,
    info: &InvocationInfo,
) -> io::Result<InvocationCmdStream> {
    const SHADER_ARGS_FAU_ENTRIES: usize =
        size_of::<hw_runner_shader_args>() / size_of::<u32>();
    assert!(info.fau_args_offset + SHADER_ARGS_FAU_ENTRIES <= info.fau.len());
    assert!(
        (info.data_stride * u32::from(info.invocations)) as usize
            <= info.data.len()
    );

    let mut invoc_data = hw_runner_invocation_info {
        // Initialized later
        descr_bo_device_ptr: 0,
        descr_bo_host_ptr: null_mut(),
        data_bo_device_ptr: 0,
        data_bo_host_ptr: null_mut(),

        code_ptr: info.code.as_ptr() as *mut c_void,
        code_size_B: info.code.len() as u64,
        fau_ptr: info.fau.as_ptr() as *mut c_void,
        fau_size_B: (info.fau.len() * size_of::<u32>()) as u64,
        args_fau_offset: info.fau_args_offset as u64,
        data_ptr: info.data.as_ptr() as *mut c_void,
        data_size_B: info.data.len() as u64,
        data_stride_B: info.data_stride,
        register_preload: info.register_preload,
        register_count: info.register_count,
        invocations: info.invocations,
    };

    let mut layout_info = hw_runner_layout_info {
        descr_bo_size_B: 0,
        cs_offset: 0,
        cs_size_B: 0,
    };
    unsafe {
        hw_runner_new_cmd_stream(
            mem.dev().kdev(),
            &mut invoc_data,
            &mut layout_info,
        )
    };

    let descr_buf = mem.allocate_buffer(
        layout_info.descr_bo_size_B,
        c"hw_runner invoc data",
        1,
    )?;
    let data_buf = mem.allocate_buffer(
        info.data.len() as u64,
        c"hw_runner writable data",
        0,
    )?;
    invoc_data.descr_bo_device_ptr = descr_buf.device_addr();
    invoc_data.descr_bo_host_ptr = descr_buf.host_addr();
    invoc_data.data_bo_device_ptr = data_buf.device_addr();
    invoc_data.data_bo_host_ptr = data_buf.host_addr();

    unsafe {
        hw_runner_new_cmd_stream(
            mem.dev().kdev(),
            &mut invoc_data,
            &mut layout_info,
        )
    };

    descr_buf.sync();
    data_buf.sync();

    Ok(InvocationCmdStream {
        descr_buf,
        data_buf,
        cs_offset: layout_info.cs_offset,
        cs_len: layout_info.cs_size_B,
        data_len: info.data.len(),
    })
}
