// Copyright 2023 Antonio Gomes.
// SPDX-License-Identifier: MIT

use crate::api::icd::*;
use crate::api::types::*;
use crate::core::context::*;
use crate::core::device::*;
use crate::core::format::*;
use crate::core::memory::*;
use crate::core::queue::*;
use crate::core::util::*;

use libc_rust_gen::{close, dlsym};
use mesa_rust::pipe::context::RWFlags;
use mesa_rust_gen::pipe_fd_type;
use rusticl_opencl_gen::*;

use mesa_rust::pipe::fence::*;
use mesa_rust::pipe::resource::*;
use mesa_rust::pipe::screen::*;

use std::collections::HashMap;
use std::ffi::CStr;
use std::ffi::CString;
use std::mem;
use std::os::raw::c_void;
use std::ptr;
use std::sync::Arc;

type CLGLMappings = Option<HashMap<PipeResourceOwned, PipeResourceOwned>>;

pub struct XPlatManager {
    #[cfg(glx)]
    glx_get_proc_addr: PFNGLXGETPROCADDRESSPROC,
    egl_get_proc_addr: PFNEGLGETPROCADDRESSPROC,
}

impl Default for XPlatManager {
    fn default() -> Self {
        Self::new()
    }
}

impl XPlatManager {
    pub fn new() -> Self {
        Self {
            #[cfg(glx)]
            glx_get_proc_addr: Self::get_proc_address_func(c"glXGetProcAddress"),
            egl_get_proc_addr: Self::get_proc_address_func(c"eglGetProcAddress"),
        }
    }

    fn get_proc_address_func<T>(name: &CStr) -> T {
        unsafe {
            let pfn = dlsym(ptr::null_mut(), name.as_ptr());
            mem::transmute_copy(&pfn)
        }
    }

    #[cfg(glx)]
    unsafe fn get_func_glx(&self, cname: &CStr) -> CLResult<__GLXextFuncPtr> {
        unsafe {
            Ok(self
                .glx_get_proc_addr
                .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?(
                cname.as_ptr().cast(),
            ))
        }
    }

    // in theory it should return CLResult<__GLXextFuncPtr> but luckily it's identical
    #[cfg(not(glx))]
    unsafe fn get_func_glx(&self, _: &CStr) -> CLResult<__eglMustCastToProperFunctionPointerType> {
        Err(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)
    }

    fn get_func<T>(&self, name: &str) -> CLResult<T> {
        let cname = CString::new(name).unwrap();
        unsafe {
            let raw_func = if name.starts_with("glX") {
                self.get_func_glx(&cname)?
            } else if name.starts_with("egl") {
                self.egl_get_proc_addr
                    .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?(
                    cname.as_ptr().cast()
                )
            } else {
                panic!();
            };

            Ok(mem::transmute_copy(&raw_func))
        }
    }

    #[allow(non_snake_case)]
    pub fn MesaGLInteropEGLQueryDeviceInfo(
        &self,
    ) -> CLResult<PFNMESAGLINTEROPEGLQUERYDEVICEINFOPROC> {
        self.get_func::<PFNMESAGLINTEROPEGLQUERYDEVICEINFOPROC>("eglGLInteropQueryDeviceInfoMESA")
    }

    #[allow(non_snake_case)]
    pub fn MesaGLInteropGLXQueryDeviceInfo(
        &self,
    ) -> CLResult<PFNMESAGLINTEROPGLXQUERYDEVICEINFOPROC> {
        self.get_func::<PFNMESAGLINTEROPGLXQUERYDEVICEINFOPROC>("glXGLInteropQueryDeviceInfoMESA")
    }

    #[allow(non_snake_case)]
    pub fn MesaGLInteropEGLExportObject(&self) -> CLResult<PFNMESAGLINTEROPEGLEXPORTOBJECTPROC> {
        self.get_func::<PFNMESAGLINTEROPEGLEXPORTOBJECTPROC>("eglGLInteropExportObjectMESA")
    }

    #[allow(non_snake_case)]
    pub fn MesaGLInteropGLXExportObject(&self) -> CLResult<PFNMESAGLINTEROPGLXEXPORTOBJECTPROC> {
        self.get_func::<PFNMESAGLINTEROPGLXEXPORTOBJECTPROC>("glXGLInteropExportObjectMESA")
    }

    #[allow(non_snake_case)]
    pub fn MesaGLInteropEGLFlushObjects(&self) -> CLResult<PFNMESAGLINTEROPEGLFLUSHOBJECTSPROC> {
        self.get_func::<PFNMESAGLINTEROPEGLFLUSHOBJECTSPROC>("eglGLInteropFlushObjectsMESA")
    }

    #[allow(non_snake_case)]
    pub fn MesaGLInteropGLXFlushObjects(&self) -> CLResult<PFNMESAGLINTEROPGLXFLUSHOBJECTSPROC> {
        self.get_func::<PFNMESAGLINTEROPGLXFLUSHOBJECTSPROC>("glXGLInteropFlushObjectsMESA")
    }
}

#[allow(clippy::upper_case_acronyms)]
#[derive(PartialEq, Eq)]
enum GLCtx {
    EGL(EGLDisplay, EGLContext),
    GLX(*mut _XDisplay, *mut __GLXcontextRec),
}

pub struct GLCtxManager {
    pub interop_dev_info: mesa_glinterop_device_info,
    pub xplat_manager: XPlatManager,
    gl_ctx: GLCtx,
}

// SAFETY: We do have a few pointers inside [GLCtxManager], but nothing really relevant here:
//  * pointers of the GLX/EGL context and _XDisplay/EGLDisplay, but we don't do much with them
//    except calling into our mesa internal GL sharing extension, which properly locks data.
//  * pointer to the _XDisplay/EGLDisplay
unsafe impl Send for GLCtxManager {}
unsafe impl Sync for GLCtxManager {}

impl GLCtxManager {
    pub fn new(
        gl_context: *mut c_void,
        glx_display: *mut _XDisplay,
        egl_display: EGLDisplay,
    ) -> CLResult<Option<Self>> {
        let mut info = mesa_glinterop_device_info {
            version: 4,
            ..Default::default()
        };
        let xplat_manager = XPlatManager::new();

        // More than one of the attributes CL_CGL_SHAREGROUP_KHR, CL_EGL_DISPLAY_KHR,
        // CL_GLX_DISPLAY_KHR, and CL_WGL_HDC_KHR is set to a non-default value.
        if !egl_display.is_null() && !glx_display.is_null() {
            return Err(CL_INVALID_OPERATION);
        }

        if gl_context.is_null() {
            return Ok(None);
        }

        if !egl_display.is_null() {
            let egl_query_device_info_func = xplat_manager
                .MesaGLInteropEGLQueryDeviceInfo()?
                .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?;

            let err = unsafe {
                egl_query_device_info_func(egl_display.cast(), gl_context.cast(), &mut info)
            };

            if err != MESA_GLINTEROP_SUCCESS as i32 {
                return Err(interop_to_cl_error(err));
            }

            Ok(Some(GLCtxManager {
                gl_ctx: GLCtx::EGL(egl_display.cast(), gl_context),
                interop_dev_info: info,
                xplat_manager: xplat_manager,
            }))
        } else if !glx_display.is_null() && cfg!(glx) {
            let glx_query_device_info_func = xplat_manager
                .MesaGLInteropGLXQueryDeviceInfo()?
                .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?;

            let err = unsafe {
                glx_query_device_info_func(glx_display.cast(), gl_context.cast(), &mut info)
            };

            if err != MESA_GLINTEROP_SUCCESS as i32 {
                return Err(interop_to_cl_error(err));
            }

            Ok(Some(GLCtxManager {
                gl_ctx: GLCtx::GLX(glx_display.cast(), gl_context.cast()),
                interop_dev_info: info,
                xplat_manager: xplat_manager,
            }))
        } else {
            Err(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)
        }
    }

    fn do_flush(&self, exports_in: &mut [mesa_glinterop_export_in]) -> CLResult<Option<FenceFd>> {
        let mut fd = -1;
        let mut flush_out = mesa_glinterop_flush_out {
            version: 1,
            fence_fd: &mut fd,
            ..Default::default()
        };

        let err = match self.gl_ctx {
            GLCtx::EGL(disp, ctx) => {
                let egl_flush_objects_func = self
                    .xplat_manager
                    .MesaGLInteropEGLFlushObjects()?
                    .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?;

                unsafe {
                    egl_flush_objects_func(
                        disp.cast(),
                        ctx.cast(),
                        exports_in.len() as u32,
                        exports_in.as_mut_ptr(),
                        &mut flush_out,
                    )
                }
            }
            GLCtx::GLX(disp, ctx) => {
                let glx_flush_objects_func = self
                    .xplat_manager
                    .MesaGLInteropGLXFlushObjects()?
                    .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?;

                unsafe {
                    glx_flush_objects_func(
                        disp.cast(),
                        ctx.cast(),
                        exports_in.len() as u32,
                        exports_in.as_mut_ptr(),
                        &mut flush_out,
                    )
                }
            }
        };

        if err != 0 {
            return Err(interop_to_cl_error(err));
        }

        if fd != -1 {
            Ok(Some(FenceFd { fd: fd }))
        } else {
            Ok(None)
        }
    }

    pub fn export_object(
        &self,
        cl_ctx: &Arc<Context>,
        target: cl_GLenum,
        flags: u32,
        miplevel: cl_GLint,
        texture: cl_GLuint,
    ) -> CLResult<GLExportManager> {
        let xplat_manager = &self.xplat_manager;
        let mut export_in = mesa_glinterop_export_in {
            version: 2,
            target: target,
            obj: texture,
            miplevel: miplevel as u32,
            access: cl_to_interop_flags(flags),
            ..Default::default()
        };

        let mut export_out = mesa_glinterop_export_out {
            version: 2,
            ..Default::default()
        };

        if let Some(fence_fd) = self.do_flush(&mut [export_in])? {
            for dev in &cl_ctx.devs {
                let fence = dev
                    .helper_ctx()
                    .import_fence(&fence_fd, pipe_fd_type::PIPE_FD_TYPE_NATIVE_SYNC)?;
                fence.wait();
            }
        }

        let err = unsafe {
            match &self.gl_ctx {
                GLCtx::EGL(disp, ctx) => {
                    let egl_export_object_func = xplat_manager
                        .MesaGLInteropEGLExportObject()?
                        .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?;

                    egl_export_object_func(disp.cast(), ctx.cast(), &mut export_in, &mut export_out)
                }
                GLCtx::GLX(disp, ctx) => {
                    let glx_export_object_func = xplat_manager
                        .MesaGLInteropGLXExportObject()?
                        .ok_or(CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR)?;

                    glx_export_object_func(disp.cast(), ctx.cast(), &mut export_in, &mut export_out)
                }
            }
        };

        if err != MESA_GLINTEROP_SUCCESS as i32 {
            return Err(interop_to_cl_error(err));
        }

        // CL_INVALID_GL_OBJECT if bufobj is not a GL buffer object or is a GL buffer
        // object but does not have an existing data store or the size of the buffer is 0.
        if [GL_ARRAY_BUFFER, GL_TEXTURE_BUFFER].contains(&target) && export_out.buf_size == 0 {
            return Err(CL_INVALID_GL_OBJECT);
        }

        Ok(GLExportManager {
            export_in: export_in,
            export_out: export_out,
        })
    }

    pub fn flush(&self, mem_objects: &[Mem]) -> CLResult<Option<FenceFd>> {
        let mut exports_in = mem_objects
            .iter()
            .filter_map(|m| m.gl_obj.as_ref())
            .map(|gl_obj| gl_obj.props)
            .collect::<Vec<_>>();

        self.do_flush(&mut exports_in)
    }
}

#[derive(Clone)]
pub struct GLMemProps {
    pub height: u32,
    pub depth: u16,
    pub width: u32,
    pub offset: u32,
    pub array_size: u16,
    pub pixel_size: u8,
    pub stride: u32,
}

impl GLMemProps {
    pub fn size(&self) -> usize {
        self.height as usize
            * self.depth as usize
            * self.array_size as usize
            * self.width as usize
            * self.pixel_size as usize
    }
}

pub struct GLExportManager {
    pub export_in: mesa_glinterop_export_in,
    pub export_out: mesa_glinterop_export_out,
}

impl GLExportManager {
    pub fn get_gl_mem_props(&self) -> CLResult<GLMemProps> {
        let pixel_size = if self.is_gl_buffer() {
            1
        } else {
            format_from_gl(self.export_out.internal_format)
                .ok_or(CL_OUT_OF_HOST_MEMORY)?
                .pixel_size()
                .unwrap()
        };

        let mut height = self.export_out.height;
        let mut depth = self.export_out.depth as u16;
        let mut width = self.export_out.width;
        let mut array_size = 1;
        let mut offset = 0;

        // some fixups
        match self.export_in.target {
            GL_TEXTURE_1D_ARRAY => {
                array_size = height as u16;
                height = 1;
                depth = 1;
            }
            GL_TEXTURE_2D_ARRAY => {
                array_size = depth;
                depth = 1;
            }
            GL_ARRAY_BUFFER | GL_TEXTURE_BUFFER => {
                array_size = 1;
                width = self.export_out.buf_size as u32;
                offset = self.export_out.buf_offset as u32;
                height = 1;
                depth = 1;
            }
            _ => {}
        }
        if is_cube_map_face(self.export_in.target) {
            array_size = 6;
        }

        Ok(GLMemProps {
            height: height,
            depth: depth,
            width: width,
            offset: offset,
            array_size: array_size,
            pixel_size: pixel_size,
            stride: self.export_out.stride,
        })
    }

    pub fn is_gl_buffer(&self) -> bool {
        self.export_out.internal_format == GL_NONE
    }
}

impl Drop for GLExportManager {
    fn drop(&mut self) {
        unsafe {
            close(self.export_out.dmabuf_fd);
        }
    }
}

pub struct GLObject {
    pub props: mesa_glinterop_export_in,
    pub shadow_map: CLGLMappings,
}

// SAFETY: We never use the pointer inside mesa_glinterop_export_in for anything.
unsafe impl Send for GLObject {}
unsafe impl Sync for GLObject {}

impl GLObject {
    pub fn cl_gl_type(&self) -> CLResult<cl_gl_object_type> {
        Ok(match self.props.target {
            GL_ARRAY_BUFFER => CL_GL_OBJECT_BUFFER,
            GL_TEXTURE_BUFFER => CL_GL_OBJECT_TEXTURE_BUFFER,
            GL_RENDERBUFFER => CL_GL_OBJECT_RENDERBUFFER,
            GL_TEXTURE_1D => CL_GL_OBJECT_TEXTURE1D,
            GL_TEXTURE_1D_ARRAY => CL_GL_OBJECT_TEXTURE1D_ARRAY,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_X
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
            | GL_TEXTURE_CUBE_MAP_POSITIVE_X
            | GL_TEXTURE_CUBE_MAP_POSITIVE_Y
            | GL_TEXTURE_CUBE_MAP_POSITIVE_Z
            | GL_TEXTURE_2D
            | GL_TEXTURE_RECTANGLE => CL_GL_OBJECT_TEXTURE2D,
            GL_TEXTURE_2D_ARRAY => CL_GL_OBJECT_TEXTURE2D_ARRAY,
            GL_TEXTURE_3D => CL_GL_OBJECT_TEXTURE3D,
            _ => return Err(CL_INVALID_VALUE),
        })
    }

    pub fn cl_mem_type(&self) -> CLResult<cl_mem_object_type> {
        mem_type_from_gl(self.props.target)
    }

    pub fn name(&self) -> cl_GLuint {
        self.props.obj
    }

    pub fn target(&self) -> cl_GLuint {
        self.props.target
    }
}

pub fn create_shadow_slice(
    cube_map: &HashMap<&'static Device, PipeResourceOwned>,
    image_format: cl_image_format,
) -> CLResult<HashMap<&'static Device, PipeResourceOwned>> {
    let mut slice = HashMap::new();

    for (dev, imported_gl_res) in cube_map {
        let width = imported_gl_res.width();
        let height = imported_gl_res.height();

        let shadow = dev
            .screen()
            .resource_create_texture(
                width,
                height,
                1,
                1,
                cl_mem_type_to_texture_target(CL_MEM_OBJECT_IMAGE2D),
                image_format.to_pipe_format().unwrap(),
                ResourceType::Normal,
                false,
            )
            .ok_or(CL_OUT_OF_HOST_MEMORY)?;

        slice.insert(*dev, shadow);
    }

    Ok(slice)
}

pub fn copy_cube_to_slice(ctx: &QueueContext, mem_objects: &[Mem]) -> CLResult<()> {
    for mem in mem_objects {
        let Mem::Image(image) = mem else {
            continue;
        };
        let gl_obj = image.gl_obj.as_ref().unwrap();
        if !is_cube_map_face(gl_obj.target()) {
            continue;
        }
        let width = image.image_desc.image_width;
        let height = image.image_desc.image_height;

        // Fill in values for doing the copy
        let idx = get_array_slice_idx(gl_obj.target());
        let src_origin = CLVec::<usize>::new([0, 0, idx]);
        let dst_offset: [u32; 3] = [0, 0, 0];
        let region = CLVec::<usize>::new([width, height, 1]);
        let src_bx = create_pipe_box(src_origin, region, CL_MEM_OBJECT_IMAGE2D_ARRAY)?;

        let cl_res = image.get_res_for_access(ctx, RWFlags::WR)?;
        let gl_res = gl_obj.shadow_map.as_ref().unwrap().get(cl_res).unwrap();

        ctx.resource_copy_texture(gl_res, cl_res, &dst_offset, &src_bx);
    }

    Ok(())
}

pub fn copy_slice_to_cube(ctx: &QueueContext, mem_objects: &[Mem]) -> CLResult<()> {
    for mem in mem_objects {
        let Mem::Image(image) = mem else {
            continue;
        };
        let gl_obj = image.gl_obj.as_ref().unwrap();
        if !is_cube_map_face(gl_obj.target()) {
            continue;
        }
        let width = image.image_desc.image_width;
        let height = image.image_desc.image_height;

        // Fill in values for doing the copy
        let idx = get_array_slice_idx(gl_obj.target()) as u32;
        let src_origin = CLVec::<usize>::new([0, 0, 0]);
        let dst_offset: [u32; 3] = [0, 0, idx];
        let region = CLVec::<usize>::new([width, height, 1]);
        let src_bx = create_pipe_box(src_origin, region, CL_MEM_OBJECT_IMAGE2D_ARRAY)?;

        let cl_res = image.get_res_for_access(ctx, RWFlags::WR)?;
        let gl_res = gl_obj.shadow_map.as_ref().unwrap().get(cl_res).unwrap();

        ctx.resource_copy_texture(cl_res, gl_res, &dst_offset, &src_bx);
    }

    Ok(())
}

pub fn interop_to_cl_error(error: i32) -> CLError {
    match error.try_into().unwrap() {
        MESA_GLINTEROP_OUT_OF_RESOURCES => CL_OUT_OF_RESOURCES,
        MESA_GLINTEROP_OUT_OF_HOST_MEMORY => CL_OUT_OF_HOST_MEMORY,
        MESA_GLINTEROP_INVALID_OPERATION => CL_INVALID_OPERATION,
        MESA_GLINTEROP_INVALID_CONTEXT | MESA_GLINTEROP_INVALID_DISPLAY => {
            CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR
        }
        MESA_GLINTEROP_INVALID_TARGET | MESA_GLINTEROP_INVALID_OBJECT => CL_INVALID_GL_OBJECT,
        MESA_GLINTEROP_INVALID_MIP_LEVEL => CL_INVALID_MIP_LEVEL,
        _ => CL_OUT_OF_HOST_MEMORY,
    }
}

pub fn cl_to_interop_flags(flags: u32) -> u32 {
    match flags {
        CL_MEM_READ_WRITE => MESA_GLINTEROP_ACCESS_READ_WRITE,
        CL_MEM_READ_ONLY => MESA_GLINTEROP_ACCESS_READ_ONLY,
        CL_MEM_WRITE_ONLY => MESA_GLINTEROP_ACCESS_WRITE_ONLY,
        _ => 0,
    }
}

pub fn mem_type_from_gl(target: u32) -> CLResult<cl_mem_object_type> {
    Ok(match target {
        GL_ARRAY_BUFFER => CL_MEM_OBJECT_BUFFER,
        GL_TEXTURE_BUFFER => CL_MEM_OBJECT_IMAGE1D_BUFFER,
        GL_TEXTURE_1D => CL_MEM_OBJECT_IMAGE1D,
        GL_TEXTURE_1D_ARRAY => CL_MEM_OBJECT_IMAGE1D_ARRAY,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X
        | GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
        | GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
        | GL_TEXTURE_CUBE_MAP_POSITIVE_X
        | GL_TEXTURE_CUBE_MAP_POSITIVE_Y
        | GL_TEXTURE_CUBE_MAP_POSITIVE_Z
        | GL_TEXTURE_2D
        | GL_TEXTURE_RECTANGLE
        | GL_RENDERBUFFER => CL_MEM_OBJECT_IMAGE2D,
        GL_TEXTURE_2D_ARRAY => CL_MEM_OBJECT_IMAGE2D_ARRAY,
        GL_TEXTURE_3D => CL_MEM_OBJECT_IMAGE3D,
        _ => return Err(CL_INVALID_VALUE),
    })
}

pub fn is_valid_gl_texture(target: u32) -> bool {
    matches!(
        target,
        GL_TEXTURE_1D
            | GL_TEXTURE_1D_ARRAY
            | GL_TEXTURE_BUFFER
            | GL_TEXTURE_2D_ARRAY
            | GL_TEXTURE_3D
    ) || is_valid_gl_texture_2d(target)
}

pub fn is_valid_gl_texture_2d(target: u32) -> bool {
    matches!(
        target,
        GL_TEXTURE_2D
            | GL_TEXTURE_RECTANGLE
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_X
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
            | GL_TEXTURE_CUBE_MAP_POSITIVE_X
            | GL_TEXTURE_CUBE_MAP_POSITIVE_Y
            | GL_TEXTURE_CUBE_MAP_POSITIVE_Z
    )
}

pub fn get_array_slice_idx(target: u32) -> usize {
    match target {
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X
        | GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
        | GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
        | GL_TEXTURE_CUBE_MAP_POSITIVE_X
        | GL_TEXTURE_CUBE_MAP_POSITIVE_Y
        | GL_TEXTURE_CUBE_MAP_POSITIVE_Z => (target - GL_TEXTURE_CUBE_MAP_POSITIVE_X) as usize,
        _ => 0,
    }
}

pub fn is_cube_map_face(target: u32) -> bool {
    matches!(
        target,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
            | GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
            | GL_TEXTURE_CUBE_MAP_POSITIVE_X
            | GL_TEXTURE_CUBE_MAP_POSITIVE_Y
            | GL_TEXTURE_CUBE_MAP_POSITIVE_Z
    )
}
