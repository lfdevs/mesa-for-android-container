// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use crate::compiler::nir::*;
use crate::pipe::fence::*;
use crate::pipe::resource::*;
use crate::pipe::screen::*;
use crate::pipe::transfer::*;

use mesa_rust_gen::*;
use mesa_rust_util::has_required_feature;

use std::os::raw::*;
use std::ptr;
use std::ptr::*;

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PipeContextPrio {
    Low = PIPE_CONTEXT_LOW_PRIORITY,
    Med = 0,
    High = PIPE_CONTEXT_HIGH_PRIORITY,
}

impl From<PipeContextPrio> for u32 {
    fn from(value: PipeContextPrio) -> Self {
        value as _
    }
}

pub struct PipeContext {
    pipe: NonNull<pipe_context>,
    pub prio: PipeContextPrio,
}

unsafe impl Send for PipeContext {}

#[derive(Clone, Copy)]
#[repr(u32)]
pub enum RWFlags {
    RD = pipe_map_flags::PIPE_MAP_READ.0,
    WR = pipe_map_flags::PIPE_MAP_WRITE.0,
    RW = pipe_map_flags::PIPE_MAP_READ_WRITE.0,
}

impl From<RWFlags> for pipe_map_flags {
    fn from(rw: RWFlags) -> Self {
        pipe_map_flags(rw as u32)
    }
}

impl PipeContext {
    pub fn new(prio: PipeContextPrio, screen: &PipeScreen) -> Option<Self> {
        let screen = screen.to_owned();
        let context = screen.create_context(prio);
        let s = Self {
            pipe: NonNull::new(context)?,
            prio: prio,
        };

        if !has_required_cbs(unsafe { s.pipe.as_ref() }) {
            assert!(false, "Context missing features. This should never happen!");
            return None;
        }

        // As the raw type already stores the pointer we can safely leak it here and turn it back
        // into the owned wrapper on `drop`.
        let ptr = screen.into_raw();
        assert_eq!(ptr, s.screen().pipe());
        Some(s)
    }

    pub(crate) fn pipe(&self) -> NonNull<pipe_context> {
        self.pipe
    }

    pub fn screen(&self) -> &PipeScreen {
        // SAFETY: self.pipe() is a valid pointer.
        PipeScreen::from_raw(&unsafe { self.pipe().as_ref() }.screen)
    }

    pub fn buffer_subdata(
        &self,
        res: &PipeResourceOwned,
        offset: c_uint,
        data: *const c_void,
        size: c_uint,
    ) {
        unsafe {
            self.pipe.as_ref().buffer_subdata.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                pipe_map_flags::PIPE_MAP_WRITE.0, // TODO PIPE_MAP_x
                offset,
                size,
                data,
            )
        }
    }

    pub fn texture_subdata(
        &self,
        res: &PipeResourceOwned,
        bx: &pipe_box,
        data: *const c_void,
        stride: u32,
        layer_stride: usize,
    ) {
        unsafe {
            self.pipe.as_ref().texture_subdata.unwrap()(
                self.pipe.as_ptr(),
                res.pipe(),
                0,
                pipe_map_flags::PIPE_MAP_WRITE.0, // TODO PIPE_MAP_x
                bx,
                data,
                stride,
                layer_stride,
            )
        }
    }

    pub fn clear_buffer(&self, res: &PipeResourceOwned, pattern: &[u8], offset: u32, size: u32) {
        unsafe {
            self.pipe
                .as_ref()
                .clear_buffer
                .unwrap_or(u_default_clear_buffer)(
                self.pipe.as_ptr(),
                res.pipe(),
                offset,
                size,
                pattern.as_ptr().cast(),
                pattern.len() as i32,
            )
        }
    }

    pub fn clear_image_buffer(
        &self,
        res: &PipeResourceOwned,
        pattern: &[u32],
        offset_bytes: u32,
        region: &[usize; 3],
        strides: (usize, usize),
        pixel_size: usize,
    ) {
        let (row_pitch, slice_pitch) = strides;
        for z in 0..region[2] {
            for y in 0..region[1] {
                let pitch = [pixel_size, row_pitch, slice_pitch];
                // Convoluted way of doing [0, y, z] * pitch
                let offset: u32 = (0..3).map(|i| ([0, y, z][i] * pitch[i]) as u32).sum();

                unsafe {
                    self.pipe.as_ref().clear_buffer.unwrap()(
                        self.pipe.as_ptr(),
                        res.pipe(),
                        offset + offset_bytes,
                        (region[0] * pixel_size) as u32,
                        pattern.as_ptr().cast(),
                        pixel_size as i32,
                    )
                };
            }
        }
    }

    pub fn clear_texture(&self, res: &PipeResourceOwned, pattern: &[u32], bx: &pipe_box) {
        unsafe {
            let clear_texture = self
                .pipe
                .as_ref()
                .clear_texture
                .unwrap_or(u_default_clear_texture);
            clear_texture(
                self.pipe.as_ptr(),
                res.pipe(),
                0,
                bx,
                pattern.as_ptr().cast(),
            )
        }
    }

    fn resource_copy_region(
        &self,
        src: &PipeResourceOwned,
        dst: &PipeResourceOwned,
        dst_offset: &[u32; 3],
        bx: &pipe_box,
    ) {
        unsafe {
            self.pipe.as_ref().resource_copy_region.unwrap()(
                self.pipe.as_ptr(),
                dst.pipe(),
                0,
                dst_offset[0],
                dst_offset[1],
                dst_offset[2],
                src.pipe(),
                0,
                bx,
            )
        }
    }

    pub fn resource_copy_buffer(
        &self,
        src: &PipeResourceOwned,
        src_offset: i32,
        dst: &PipeResourceOwned,
        dst_offset: u32,
        width: i32,
    ) {
        debug_assert!(src.is_buffer());
        debug_assert!(dst.is_buffer());

        let bx = pipe_box {
            x: src_offset,
            width: width,
            height: 1,
            depth: 1,
            ..Default::default()
        };

        self.resource_copy_region(src, dst, &[dst_offset, 0, 0], &bx)
    }

    pub fn resource_copy_texture(
        &self,
        src: &PipeResourceOwned,
        dst: &PipeResourceOwned,
        dst_offset: &[u32; 3],
        bx: &pipe_box,
    ) {
        debug_assert!(!src.is_buffer());
        debug_assert!(!dst.is_buffer());

        self.resource_copy_region(src, dst, dst_offset, bx)
    }

    pub fn has_buffer_texture_copies(&self) -> bool {
        unsafe { self.pipe.as_ref() }.image_copy_buffer.is_some()
    }

    /// Copies between a buffer and a texture if supported by the context
    /// ([Self::has_buffer_texture_copies]).
    pub fn resource_copy_buffer_texture(
        &self,
        src: &PipeResourceOwned,
        dst: &PipeResourceOwned,
        buffer_offset: u32,
        bx: &pipe_box,
    ) {
        debug_assert_ne!(src.is_buffer(), dst.is_buffer());

        unsafe {
            self.pipe.as_ref().image_copy_buffer.unwrap()(
                self.pipe.as_ptr(),
                dst.pipe(),
                src.pipe(),
                buffer_offset,
                0,
                0,
                0,
                bx,
            );
        }
    }

    fn resource_map(
        &self,
        res: &PipeResourceOwned,
        bx: &pipe_box,
        flags: pipe_map_flags,
        is_buffer: bool,
    ) -> Option<PipeTransfer<'_>> {
        let mut out: *mut pipe_transfer = ptr::null_mut();

        let ptr = unsafe {
            let func = if is_buffer {
                self.pipe.as_ref().buffer_map
            } else {
                self.pipe.as_ref().texture_map
            };

            func.unwrap()(self.pipe.as_ptr(), res.pipe(), 0, flags.0, bx, &mut out)
        };

        if ptr.is_null() {
            None
        } else {
            Some(PipeTransfer::new(self, is_buffer, out, ptr))
        }
    }

    pub fn buffer_map_flags(
        &self,
        res: &PipeResourceOwned,
        offset: i32,
        size: i32,
        flags: pipe_map_flags,
    ) -> Option<PipeTransfer<'_>> {
        let b = pipe_box {
            x: offset,
            width: size,
            height: 1,
            depth: 1,
            ..Default::default()
        };

        self.resource_map(res, &b, flags, true)
    }

    pub fn buffer_map(
        &self,
        res: &PipeResourceOwned,
        offset: i32,
        size: i32,
        rw: RWFlags,
    ) -> Option<PipeTransfer<'_>> {
        self.buffer_map_flags(res, offset, size, rw.into())
    }

    pub(super) fn buffer_unmap(&self, tx: *mut pipe_transfer) {
        unsafe { self.pipe.as_ref().buffer_unmap.unwrap()(self.pipe.as_ptr(), tx) };
    }

    pub fn texture_map_flags(
        &self,
        res: &PipeResourceOwned,
        bx: &pipe_box,
        flags: pipe_map_flags,
    ) -> Option<PipeTransfer<'_>> {
        self.resource_map(res, bx, flags, false)
    }

    pub fn texture_map(
        &self,
        res: &PipeResourceOwned,
        bx: &pipe_box,
        rw: RWFlags,
    ) -> Option<PipeTransfer<'_>> {
        self.texture_map_flags(res, bx, rw.into())
    }

    pub(super) fn texture_unmap(&self, tx: *mut pipe_transfer) {
        unsafe { self.pipe.as_ref().texture_unmap.unwrap()(self.pipe.as_ptr(), tx) };
    }

    pub fn create_compute_state(&self, nir: &NirShader, static_local_mem: u32) -> *mut c_void {
        let state = pipe_compute_state {
            ir_type: pipe_shader_ir::PIPE_SHADER_IR_NIR,
            prog: nir.dup_for_driver().cast(),
            static_shared_mem: static_local_mem,
        };
        unsafe { self.pipe.as_ref().create_compute_state.unwrap()(self.pipe.as_ptr(), &state) }
    }

    /// # Safety
    ///
    /// The state pointer needs to point to valid memory until a new one is set.
    pub unsafe fn bind_compute_state(&self, state: *mut c_void) {
        unsafe { self.pipe.as_ref().bind_compute_state.unwrap()(self.pipe.as_ptr(), state) }
    }

    pub fn delete_compute_state(&self, state: *mut c_void) {
        unsafe { self.pipe.as_ref().delete_compute_state.unwrap()(self.pipe.as_ptr(), state) }
    }

    pub fn compute_state_info(&self, state: *mut c_void) -> pipe_compute_state_object_info {
        let mut info = pipe_compute_state_object_info::default();
        unsafe {
            self.pipe.as_ref().get_compute_state_info.unwrap()(self.pipe.as_ptr(), state, &mut info)
        }
        info
    }

    pub fn compute_state_subgroup_size(&self, state: *mut c_void, block: &[u32; 3]) -> u32 {
        unsafe {
            if let Some(cb) = self.pipe.as_ref().get_compute_state_subgroup_size {
                cb(self.pipe.as_ptr(), state, block)
            } else {
                0
            }
        }
    }

    pub fn is_create_fence_fd_supported(&self) -> bool {
        unsafe { self.pipe.as_ref().create_fence_fd.is_some() }
    }

    pub fn create_sampler_state(&self, state: &pipe_sampler_state) -> *mut c_void {
        unsafe { self.pipe.as_ref().create_sampler_state.unwrap()(self.pipe.as_ptr(), state) }
    }

    pub fn bind_sampler_states(&self, samplers: &[*mut c_void]) {
        let mut samplers = samplers.to_owned();
        unsafe {
            self.pipe.as_ref().bind_sampler_states.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                0,
                samplers.len() as u32,
                samplers.as_mut_ptr(),
            )
        }
    }

    pub fn clear_sampler_states(&self, count: u32) {
        let mut samplers = vec![ptr::null_mut(); count as usize];
        unsafe {
            self.pipe.as_ref().bind_sampler_states.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                0,
                count,
                samplers.as_mut_ptr(),
            )
        }
    }

    pub fn delete_sampler_state(&self, ptr: *mut c_void) {
        unsafe { self.pipe.as_ref().delete_sampler_state.unwrap()(self.pipe.as_ptr(), ptr) }
    }

    pub fn bind_constant_buffer(&self, idx: u32, res: &PipeResourceOwned) {
        let cb = pipe_constant_buffer {
            buffer: res.pipe(),
            buffer_offset: 0,
            buffer_size: res.width(),
            user_buffer: ptr::null(),
        };
        unsafe {
            self.pipe.as_ref().set_constant_buffer.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                idx,
                &cb,
            )
        }
    }

    pub fn set_constant_buffer(&self, idx: u32, data: &[u8]) {
        let cb = pipe_constant_buffer {
            buffer: ptr::null_mut(),
            buffer_offset: 0,
            buffer_size: data.len() as u32,
            user_buffer: data.as_ptr().cast(),
        };
        unsafe {
            self.pipe.as_ref().set_constant_buffer.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                idx,
                if data.is_empty() { ptr::null() } else { &cb },
            )
        }
    }

    /// returns false when failing to allocate GPU memory.
    #[must_use]
    pub fn set_constant_buffer_stream(&self, idx: u32, data: &[u8]) -> bool {
        let mut cb = pipe_constant_buffer {
            buffer: ptr::null_mut(),
            buffer_offset: 0,
            buffer_size: data.len() as u32,
            user_buffer: ptr::null_mut(),
        };

        unsafe {
            let stream = self.pipe.as_ref().stream_uploader;
            let mut releasebuf = ptr::null_mut();
            u_upload_data(
                stream,
                0,
                data.len() as u32,
                size_of::<[u64; 16]>() as u32,
                data.as_ptr().cast(),
                &mut cb.buffer_offset,
                &mut cb.buffer,
                &mut releasebuf,
            );
            u_upload_unmap(stream);

            if cb.buffer.is_null() {
                return false;
            }

            self.pipe.as_ref().set_constant_buffer.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                idx,
                &cb,
            );

            pipe_resource_release(self.pipe.as_ptr(), releasebuf);

            true
        }
    }

    pub fn launch_grid(
        &self,
        work_dim: u32,
        block: [u32; 3],
        grid: [u32; 3],
        variable_local_mem: u32,
        globals: &[&PipeResourceOwned],
    ) {
        let mut globals: Vec<*mut pipe_resource> = globals.iter().map(|res| res.pipe()).collect();
        let info = pipe_grid_info {
            variable_shared_mem: variable_local_mem,
            work_dim: work_dim,
            block: block,
            grid: grid,
            globals: globals.as_mut_ptr(),
            num_globals: globals.len() as u32,
            ..Default::default()
        };
        unsafe { self.pipe.as_ref().launch_grid.unwrap()(self.pipe.as_ptr(), &info) }
    }

    pub fn set_global_binding(&self, res: &mut [&PipeResource], out: &mut [*mut u32]) {
        let len = res.len();
        let res = PipeResource::slice_as_mut_ptr_slice(res);
        // SAFETY: We can safely cast the *mut *const pointer to *mut *mut as drivers aren't going
        //         to change any of the pipe_resource fields, but merely allows them to change
        //         fields of their own subclass.
        unsafe {
            self.pipe.as_ref().set_global_binding.unwrap()(
                self.pipe.as_ptr(),
                0,
                len as u32,
                res.cast(),
                out.as_mut_ptr(),
            )
        }
    }

    pub fn clear_global_binding(&self, count: u32) {
        unsafe {
            self.pipe.as_ref().set_global_binding.unwrap()(
                self.pipe.as_ptr(),
                0,
                count,
                ptr::null_mut(),
                ptr::null_mut(),
            )
        }
    }

    pub fn set_sampler_views(&self, views: &mut [PipeSamplerView], unbind_trailing: u32) {
        unsafe {
            self.pipe.as_ref().set_sampler_views.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                0,
                views.len() as u32,
                unbind_trailing,
                PipeSamplerView::as_pipe(views),
            );
        }
    }

    pub fn clear_sampler_views(&self, count: u32) {
        let mut samplers = vec![ptr::null_mut(); count as usize];
        unsafe {
            self.pipe.as_ref().set_sampler_views.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                0,
                count,
                0,
                samplers.as_mut_ptr(),
            )
        }
    }

    pub fn set_shader_images(&self, images: &[PipeImageView], unbind_trailing: u32) {
        let images = PipeImageView::slice_to_pipe(images);
        unsafe {
            self.pipe.as_ref().set_shader_images.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                0,
                images.len() as u32,
                unbind_trailing,
                images.as_ptr(),
            )
        }
    }

    pub fn clear_shader_images(&self, count: u32) {
        unsafe {
            self.pipe.as_ref().set_shader_images.unwrap()(
                self.pipe.as_ptr(),
                mesa_shader_stage::MESA_SHADER_COMPUTE,
                0,
                count,
                0,
                ptr::null_mut(),
            )
        }
    }

    pub(crate) fn create_query(&self, query_type: c_uint, index: c_uint) -> *mut pipe_query {
        unsafe { self.pipe.as_ref().create_query.unwrap()(self.pipe.as_ptr(), query_type, index) }
    }

    /// # Safety
    ///
    /// usual rules on raw mut pointers apply, specifically no concurrent access
    pub(crate) unsafe fn end_query(&self, pq: *mut pipe_query) -> bool {
        unsafe { self.pipe.as_ref().end_query.unwrap()(self.pipe.as_ptr(), pq) }
    }

    /// # Safety
    ///
    /// usual rules on raw mut pointers apply, specifically no concurrent access
    pub(crate) unsafe fn get_query_result(
        &self,
        pq: *mut pipe_query,
        wait: bool,
        pqr: *mut pipe_query_result,
    ) -> bool {
        unsafe { self.pipe.as_ref().get_query_result.unwrap()(self.pipe.as_ptr(), pq, wait, pqr) }
    }

    /// # Safety
    ///
    /// usual rules on raw mut pointers apply, specifically no concurrent access
    pub(crate) unsafe fn destroy_query(&self, pq: *mut pipe_query) {
        unsafe { self.pipe.as_ref().destroy_query.unwrap()(self.pipe.as_ptr(), pq) }
    }

    pub fn memory_barrier(&self, barriers: u32) {
        unsafe { self.pipe.as_ref().memory_barrier.unwrap()(self.pipe.as_ptr(), barriers) }
    }

    pub fn flush(&self) -> PipeFence {
        unsafe {
            let mut fence = ptr::null_mut();
            self.pipe.as_ref().flush.unwrap()(self.pipe.as_ptr(), &mut fence, 0);
            // TODO: handle properly
            PipeFence::new(fence, self.screen()).unwrap()
        }
    }

    pub fn import_fence(&self, fence_fd: &FenceFd, fence_type: pipe_fd_type) -> Option<PipeFence> {
        unsafe {
            let mut fence = ptr::null_mut();
            self.pipe.as_ref().create_fence_fd.unwrap()(
                self.pipe.as_ptr(),
                &mut fence,
                fence_fd.fd,
                fence_type,
            );
            PipeFence::new(fence, self.screen())
        }
    }

    pub fn svm_migrate(
        &self,
        ptrs: &[usize],
        sizes: &[usize],
        to_device: bool,
        content_undefined: bool,
    ) {
        assert_eq!(ptrs.len(), sizes.len());
        unsafe {
            if let Some(cb) = self.pipe.as_ref().svm_migrate {
                cb(
                    self.pipe.as_ptr(),
                    ptrs.len() as u32,
                    ptrs.as_ptr().cast(),
                    sizes.as_ptr(),
                    to_device,
                    content_undefined,
                );
            }
        }
    }

    pub fn device_reset_status(&self) -> pipe_reset_status {
        unsafe {
            if let Some(get_device_reset_status) = self.pipe.as_ref().get_device_reset_status {
                get_device_reset_status(self.pipe.as_ptr())
            } else {
                pipe_reset_status::PIPE_NO_RESET
            }
        }
    }

    pub fn has_fence_server(&self) -> bool {
        let pipe = unsafe { self.pipe().as_ref() };
        pipe.fence_server_signal.is_some() && pipe.fence_server_sync.is_some()
    }
}

impl Drop for PipeContext {
    fn drop(&mut self) {
        self.flush().wait();
        let screen = self.screen().pipe();
        unsafe {
            self.pipe.as_ref().destroy.unwrap()(self.pipe.as_ptr());
        }

        // In new we check that screen is identical to the pointer we retrieved from into_raw. We
        // convert the pointer back to an owned reference so we release our reference to prevent
        // leaking memory.
        unsafe { PipeScreenOwned::from_raw(screen) };
    }
}

fn has_required_cbs(context: &pipe_context) -> bool {
    // Use '&' to evaluate all features and to not stop
    // on first missing one to list all missing features.
    has_required_feature!(context, destroy)
        & has_required_feature!(context, bind_compute_state)
        & has_required_feature!(context, bind_sampler_states)
        & has_required_feature!(context, buffer_map)
        & has_required_feature!(context, buffer_subdata)
        & has_required_feature!(context, buffer_unmap)
        & has_required_feature!(context, create_compute_state)
        & has_required_feature!(context, create_query)
        & has_required_feature!(context, delete_compute_state)
        & has_required_feature!(context, delete_sampler_state)
        & has_required_feature!(context, destroy_query)
        & has_required_feature!(context, end_query)
        & has_required_feature!(context, flush)
        & has_required_feature!(context, get_compute_state_info)
        & has_required_feature!(context, launch_grid)
        & has_required_feature!(context, memory_barrier)
        & has_required_feature!(context, resource_copy_region)
        // implicitly used through pipe_sampler_view_release
        & has_required_feature!(context, sampler_view_destroy)
        & has_required_feature!(context, set_constant_buffer)
        & has_required_feature!(context, set_global_binding)
        & has_required_feature!(context, set_sampler_views)
        & has_required_feature!(context, sampler_view_release)
        & has_required_feature!(context, set_shader_images)
        & has_required_feature!(context, texture_map)
        & has_required_feature!(context, texture_subdata)
        & has_required_feature!(context, texture_unmap)
}
