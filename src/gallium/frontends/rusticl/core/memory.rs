use crate::api::icd::*;
use crate::api::types::*;
use crate::api::util::*;
use crate::core::context::*;
use crate::core::device::*;
use crate::core::format::*;
use crate::core::queue::*;
use crate::core::util::cl_mem_type_to_texture_target;
use crate::impl_cl_type_trait;

use mesa_rust::pipe::context::*;
use mesa_rust::pipe::resource::*;
use mesa_rust::pipe::screen::ResourceType;
use mesa_rust::pipe::transfer::*;
use mesa_rust_gen::*;
use mesa_rust_util::math::*;
use mesa_rust_util::properties::Properties;
use rusticl_opencl_gen::*;

use std::cmp;
use std::collections::HashMap;
use std::convert::TryInto;
use std::mem::size_of;
use std::ops::AddAssign;
use std::os::raw::c_void;
use std::ptr;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;

struct MappingTransfer {
    tx: PipeTransfer,
    shadow: Option<PipeResource>,
    pending: u32,
}

impl MappingTransfer {
    fn new(tx: PipeTransfer, shadow: Option<PipeResource>) -> Self {
        MappingTransfer {
            tx: tx,
            shadow: shadow,
            pending: 1,
        }
    }
}

struct Mappings {
    tx: HashMap<Arc<Device>, MappingTransfer>,
    maps: HashMap<*mut c_void, u32>,
}

impl Mappings {
    fn new() -> Mutex<Self> {
        Mutex::new(Mappings {
            tx: HashMap::new(),
            maps: HashMap::new(),
        })
    }

    fn mark_pending(&mut self, dev: &Device) {
        self.tx.get_mut(dev).unwrap().pending += 1;
    }

    fn unmark_pending(&mut self, dev: &Device) {
        if let Some(tx) = self.tx.get_mut(dev) {
            tx.pending -= 1;
        }
    }

    fn increase_ref(&mut self, dev: &Device, ptr: *mut c_void) -> bool {
        let res = self.maps.is_empty();
        *self.maps.entry(ptr).or_default() += 1;
        self.unmark_pending(dev);
        res
    }

    fn decrease_ref(&mut self, ptr: *mut c_void, dev: &Device) -> (bool, Option<&PipeResource>) {
        if let Some(r) = self.maps.get_mut(&ptr) {
            *r -= 1;

            if *r == 0 {
                self.maps.remove(&ptr);
            }

            if self.maps.is_empty() {
                let shadow = self.tx.get(dev).and_then(|tx| tx.shadow.as_ref());
                return (true, shadow);
            }
        }
        (false, None)
    }

    fn clean_up_tx(&mut self, dev: &Device, ctx: &PipeContext) {
        if self.maps.is_empty() {
            if let Some(tx) = self.tx.get(dev) {
                if tx.pending == 0 {
                    self.tx.remove(dev).unwrap().tx.with_ctx(ctx);
                }
            }
        }
    }
}

#[repr(C)]
pub struct Mem {
    pub base: CLObjectBase<CL_INVALID_MEM_OBJECT>,
    pub context: Arc<Context>,
    pub parent: Option<Arc<Mem>>,
    pub mem_type: cl_mem_object_type,
    pub flags: cl_mem_flags,
    pub size: usize,
    pub offset: usize,
    pub host_ptr: *mut c_void,
    pub image_format: cl_image_format,
    pub image_desc: cl_image_desc,
    pub image_elem_size: u8,
    pub props: Vec<cl_mem_properties>,
    pub cbs: Mutex<Vec<Box<dyn Fn(cl_mem)>>>,
    res: Option<HashMap<Arc<Device>, Arc<PipeResource>>>,
    maps: Mutex<Mappings>,
}

impl_cl_type_trait!(cl_mem, Mem, CL_INVALID_MEM_OBJECT);

pub trait CLImageDescInfo {
    fn type_info(&self) -> (u8, bool);
    fn pixels(&self) -> usize;
    fn bx(&self) -> CLResult<pipe_box>;
    fn row_pitch(&self) -> CLResult<u32>;
    fn slice_pitch(&self) -> CLResult<u32>;
    fn size(&self) -> CLVec<usize>;
    fn api_size(&self) -> CLVec<usize>;

    fn dims(&self) -> u8 {
        self.type_info().0
    }

    fn has_slice(&self) -> bool {
        self.dims() == 3 || self.is_array()
    }

    fn is_array(&self) -> bool {
        self.type_info().1
    }
}

impl CLImageDescInfo for cl_image_desc {
    fn type_info(&self) -> (u8, bool) {
        match self.image_type {
            CL_MEM_OBJECT_IMAGE1D | CL_MEM_OBJECT_IMAGE1D_BUFFER => (1, false),
            CL_MEM_OBJECT_IMAGE1D_ARRAY => (1, true),
            CL_MEM_OBJECT_IMAGE2D => (2, false),
            CL_MEM_OBJECT_IMAGE2D_ARRAY => (2, true),
            CL_MEM_OBJECT_IMAGE3D => (3, false),
            _ => panic!("unknown image_type {:x}", self.image_type),
        }
    }

    fn pixels(&self) -> usize {
        let mut res = self.image_width;
        let dims = self.dims();

        if dims > 1 {
            res *= self.image_height;
        }

        if dims > 2 {
            res *= self.image_depth;
        }

        if self.is_array() {
            res *= self.image_array_size;
        }

        res
    }

    fn size(&self) -> CLVec<usize> {
        let mut depth = if self.is_array() {
            self.image_array_size
        } else {
            self.image_depth
        };

        let height = cmp::max(self.image_height, 1);
        depth = cmp::max(depth, 1);

        CLVec::new([self.image_width, height, depth])
    }

    fn api_size(&self) -> CLVec<usize> {
        let mut size = self.size();

        if self.is_array() && self.dims() == 1 {
            size[1] = size[2];
            size[2] = 1;
        }

        size
    }

    fn bx(&self) -> CLResult<pipe_box> {
        let size = self.size();

        Ok(pipe_box {
            x: 0,
            y: 0,
            z: 0,
            width: size[0].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            height: size[1].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            depth: size[2].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        })
    }

    fn row_pitch(&self) -> CLResult<u32> {
        self.image_row_pitch
            .try_into()
            .map_err(|_| CL_OUT_OF_HOST_MEMORY)
    }

    fn slice_pitch(&self) -> CLResult<u32> {
        self.image_slice_pitch
            .try_into()
            .map_err(|_| CL_OUT_OF_HOST_MEMORY)
    }
}

fn sw_copy(
    src: *const c_void,
    dst: *mut c_void,
    region: &CLVec<usize>,
    src_origin: &CLVec<usize>,
    src_row_pitch: usize,
    src_slice_pitch: usize,
    dst_origin: &CLVec<usize>,
    dst_row_pitch: usize,
    dst_slice_pitch: usize,
    pixel_size: u8,
) {
    for z in 0..region[2] {
        for y in 0..region[1] {
            unsafe {
                ptr::copy_nonoverlapping(
                    src.add((*src_origin + [0, y, z]) * [1, src_row_pitch, src_slice_pitch]),
                    dst.add((*dst_origin + [0, y, z]) * [1, dst_row_pitch, dst_slice_pitch]),
                    region[0] * pixel_size as usize,
                )
            };
        }
    }
}

fn create_box(
    origin: &CLVec<usize>,
    region: &CLVec<usize>,
    tex_type: cl_mem_object_type,
) -> CLResult<pipe_box> {
    let mut y = 1;
    let mut z = 2;

    // array slice belongs to z/depth
    if tex_type == CL_MEM_OBJECT_IMAGE1D_ARRAY {
        (z, y) = (y, z);
    }

    Ok(pipe_box {
        x: origin[0].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        y: origin[y].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        z: origin[z].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        width: region[0].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        height: region[y].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        depth: region[z].try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
    })
}

fn buffer_offset_size(
    origin: &CLVec<usize>,
    region: &CLVec<usize>,
    row_pitch: usize,
    slice_pitch: usize,
) -> (usize, usize) {
    let pitch = [1, row_pitch, slice_pitch];
    (*origin * pitch, *region * pitch)
}

impl Mem {
    pub fn new_buffer(
        context: Arc<Context>,
        flags: cl_mem_flags,
        size: usize,
        host_ptr: *mut c_void,
        props: Vec<cl_mem_properties>,
    ) -> CLResult<Arc<Mem>> {
        let res_type = if bit_check(flags, CL_MEM_ALLOC_HOST_PTR) {
            ResourceType::Staging
        } else {
            ResourceType::Normal
        };

        let buffer = context.create_buffer(
            size,
            host_ptr,
            bit_check(flags, CL_MEM_COPY_HOST_PTR),
            res_type,
        )?;

        let host_ptr = if bit_check(flags, CL_MEM_USE_HOST_PTR) {
            host_ptr
        } else {
            ptr::null_mut()
        };

        Ok(Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            parent: None,
            mem_type: CL_MEM_OBJECT_BUFFER,
            flags: flags,
            size: size,
            offset: 0,
            host_ptr: host_ptr,
            image_format: cl_image_format::default(),
            image_desc: cl_image_desc::default(),
            image_elem_size: 0,
            props: props,
            cbs: Mutex::new(Vec::new()),
            res: Some(buffer),
            maps: Mappings::new(),
        }))
    }

    pub fn new_sub_buffer(
        parent: Arc<Mem>,
        flags: cl_mem_flags,
        offset: usize,
        size: usize,
    ) -> Arc<Mem> {
        let host_ptr = if parent.host_ptr.is_null() {
            ptr::null_mut()
        } else {
            unsafe { parent.host_ptr.add(offset) }
        };

        Arc::new(Self {
            base: CLObjectBase::new(),
            context: parent.context.clone(),
            parent: Some(parent),
            mem_type: CL_MEM_OBJECT_BUFFER,
            flags: flags,
            size: size,
            offset: offset,
            host_ptr: host_ptr,
            image_format: cl_image_format::default(),
            image_desc: cl_image_desc::default(),
            image_elem_size: 0,
            props: Vec::new(),
            cbs: Mutex::new(Vec::new()),
            res: None,
            maps: Mappings::new(),
        })
    }

    pub fn new_image(
        context: Arc<Context>,
        parent: Option<Arc<Mem>>,
        mem_type: cl_mem_object_type,
        flags: cl_mem_flags,
        image_format: &cl_image_format,
        mut image_desc: cl_image_desc,
        image_elem_size: u8,
        host_ptr: *mut c_void,
        props: Vec<cl_mem_properties>,
    ) -> CLResult<Arc<Mem>> {
        // we have to sanitize the image_desc a little for internal use
        let api_image_desc = image_desc;
        let dims = image_desc.dims();
        let is_array = image_desc.is_array();
        if dims < 3 {
            image_desc.image_depth = 1;
        }
        if dims < 2 {
            image_desc.image_height = 1;
        }
        if !is_array {
            image_desc.image_array_size = 1;
        }

        let res_type = if bit_check(flags, CL_MEM_ALLOC_HOST_PTR) {
            ResourceType::Staging
        } else {
            ResourceType::Normal
        };

        let texture = if parent.is_none() {
            Some(context.create_texture(
                &image_desc,
                image_format,
                host_ptr,
                bit_check(flags, CL_MEM_COPY_HOST_PTR),
                res_type,
            )?)
        } else {
            None
        };

        let host_ptr = if bit_check(flags, CL_MEM_USE_HOST_PTR) {
            host_ptr
        } else {
            ptr::null_mut()
        };

        Ok(Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            parent: parent,
            mem_type: mem_type,
            flags: flags,
            size: image_desc.pixels() * image_format.pixel_size().unwrap() as usize,
            offset: 0,
            host_ptr: host_ptr,
            image_format: *image_format,
            image_desc: api_image_desc,
            image_elem_size: image_elem_size,
            props: props,
            cbs: Mutex::new(Vec::new()),
            res: texture,
            maps: Mappings::new(),
        }))
    }

    pub fn is_buffer(&self) -> bool {
        self.mem_type == CL_MEM_OBJECT_BUFFER
    }

    fn tx_raw(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        mut offset: usize,
        size: usize,
        rw: RWFlags,
    ) -> CLResult<PipeTransfer> {
        let b = self.to_parent(&mut offset);
        let r = b.get_res()?.get(&q.device).unwrap();

        assert!(self.is_buffer());

        Ok(ctx.buffer_map(
            r,
            offset.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            size.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            rw,
            ResourceMapType::Normal,
        ))
    }

    fn tx_raw_async(
        &self,
        q: &Arc<Queue>,
        rw: RWFlags,
    ) -> CLResult<(PipeTransfer, Option<PipeResource>)> {
        let mut offset = 0;
        let b = self.to_parent(&mut offset);
        let r = b.get_res()?.get(&q.device).unwrap();
        let size = self.size.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?;
        let ctx = q.device.helper_ctx();

        assert!(self.is_buffer());

        // don't bother mapping directly if it's not UMA or a staging buffer
        let tx = if q.device.unified_memory() || bit_check(b.flags, CL_MEM_ALLOC_HOST_PTR) {
            ctx.buffer_map_directly(
                r,
                offset.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
                size,
                rw,
            )
        } else {
            None
        };

        if let Some(tx) = tx {
            Ok((tx, None))
        } else {
            let shadow = q
                .device
                .screen()
                .resource_create_buffer(size as u32, ResourceType::Staging)
                .ok_or(CL_OUT_OF_RESOURCES)?;
            let tx = ctx.buffer_map_coherent(&shadow, 0, size, rw);
            Ok((tx, Some(shadow)))
        }
    }

    fn tx<'a>(
        &self,
        q: &Arc<Queue>,
        ctx: &'a PipeContext,
        offset: usize,
        size: usize,
        rw: RWFlags,
    ) -> CLResult<GuardedPipeTransfer<'a>> {
        Ok(self.tx_raw(q, ctx, offset, size, rw)?.with_ctx(ctx))
    }

    fn tx_image_raw(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        bx: &pipe_box,
        rw: RWFlags,
    ) -> CLResult<PipeTransfer> {
        assert!(!self.is_buffer());

        let r = self.get_res()?.get(&q.device).unwrap();
        Ok(ctx.texture_map(r, bx, rw, ResourceMapType::Normal))
    }

    fn tx_image_raw_async(
        &self,
        q: &Arc<Queue>,
        bx: &pipe_box,
        rw: RWFlags,
    ) -> CLResult<(PipeTransfer, Option<PipeResource>)> {
        assert!(!self.is_buffer());

        let r = self.get_res()?.get(&q.device).unwrap();
        let ctx = q.device.helper_ctx();

        // don't bother mapping directly if it's not staging
        let tx = if bit_check(self.flags, CL_MEM_ALLOC_HOST_PTR) {
            ctx.texture_map_directly(r, bx, rw)
        } else {
            None
        };

        if let Some(tx) = tx {
            Ok((tx, None))
        } else {
            let shadow = q
                .device
                .screen()
                .resource_create_texture(
                    r.width(),
                    r.height(),
                    r.depth(),
                    r.array_size(),
                    cl_mem_type_to_texture_target(self.image_desc.image_type),
                    self.image_format.to_pipe_format().unwrap(),
                    ResourceType::Staging,
                )
                .ok_or(CL_OUT_OF_RESOURCES)?;
            let tx = ctx.texture_map_coherent(&shadow, bx, rw);
            Ok((tx, Some(shadow)))
        }
    }

    fn tx_image<'a>(
        &self,
        q: &Arc<Queue>,
        ctx: &'a PipeContext,
        bx: &pipe_box,
        rw: RWFlags,
    ) -> CLResult<GuardedPipeTransfer<'a>> {
        Ok(self.tx_image_raw(q, ctx, bx, rw)?.with_ctx(ctx))
    }

    pub fn has_same_parent(&self, other: &Self) -> bool {
        let a = self.parent.as_ref().map_or(self, |p| p);
        let b = other.parent.as_ref().map_or(other, |p| p);
        ptr::eq(a, b)
    }

    fn get_res(&self) -> CLResult<&HashMap<Arc<Device>, Arc<PipeResource>>> {
        self.parent
            .as_ref()
            .map_or(self, |p| p.as_ref())
            .res
            .as_ref()
            .ok_or(CL_OUT_OF_HOST_MEMORY)
    }

    pub fn get_res_of_dev(&self, dev: &Arc<Device>) -> CLResult<&Arc<PipeResource>> {
        Ok(self.get_res()?.get(dev).unwrap())
    }

    fn to_parent<'a>(&'a self, offset: &mut usize) -> &'a Self {
        if let Some(parent) = &self.parent {
            offset.add_assign(self.offset);
            parent
        } else {
            self
        }
    }

    fn has_user_shadow_buffer(&self, d: &Device) -> CLResult<bool> {
        let r = self.get_res()?.get(d).unwrap();
        Ok(!r.is_user && bit_check(self.flags, CL_MEM_USE_HOST_PTR))
    }

    pub fn read_to_user(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        offset: usize,
        ptr: *mut c_void,
        size: usize,
    ) -> CLResult<()> {
        assert!(self.is_buffer());

        let tx = self.tx(q, ctx, offset, size, RWFlags::RD)?;

        unsafe {
            ptr::copy_nonoverlapping(tx.ptr(), ptr, size);
        }

        Ok(())
    }

    pub fn write_from_user(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        mut offset: usize,
        ptr: *const c_void,
        size: usize,
    ) -> CLResult<()> {
        assert!(self.is_buffer());

        let b = self.to_parent(&mut offset);
        let r = b.get_res()?.get(&q.device).unwrap();
        ctx.buffer_subdata(
            r,
            offset.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            ptr,
            size.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        );
        Ok(())
    }

    pub fn copy_to(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        dst: &Arc<Mem>,
        mut src_origin: CLVec<usize>,
        mut dst_origin: CLVec<usize>,
        region: &CLVec<usize>,
    ) -> CLResult<()> {
        let src = self.to_parent(&mut src_origin[0]);
        let dst = dst.to_parent(&mut dst_origin[0]);

        let src_res = src.get_res()?.get(&q.device).unwrap();
        let dst_res = dst.get_res()?.get(&q.device).unwrap();

        if self.is_buffer() && !dst.is_buffer() || !self.is_buffer() && dst.is_buffer() {
            let tx_src;
            let tx_dst;

            if self.is_buffer() {
                let bpp = dst.image_format.pixel_size().unwrap() as usize;
                tx_src = self.tx(q, ctx, src_origin[0], region.pixels() * bpp, RWFlags::RD)?;
                tx_dst = dst.tx_image(
                    q,
                    ctx,
                    &create_box(&dst_origin, region, dst.mem_type)?,
                    RWFlags::WR,
                )?;

                sw_copy(
                    tx_src.ptr(),
                    tx_dst.ptr(),
                    region,
                    &CLVec::default(),
                    region[0] * bpp,
                    region[0] * region[1] * bpp,
                    &CLVec::default(),
                    tx_dst.row_pitch() as usize,
                    tx_dst.slice_pitch() as usize,
                    bpp as u8,
                )
            } else {
                let bpp = self.image_format.pixel_size().unwrap() as usize;
                tx_src = self.tx_image(
                    q,
                    ctx,
                    &create_box(&src_origin, region, self.mem_type)?,
                    RWFlags::RD,
                )?;
                tx_dst = dst.tx(q, ctx, dst_origin[0], region.pixels() * bpp, RWFlags::WR)?;

                sw_copy(
                    tx_src.ptr(),
                    tx_dst.ptr(),
                    region,
                    &CLVec::default(),
                    tx_src.row_pitch() as usize,
                    tx_src.slice_pitch() as usize,
                    &CLVec::default(),
                    region[0] * bpp,
                    region[0] * region[1] * bpp,
                    bpp as u8,
                )
            }
        } else {
            let bx = create_box(&src_origin, region, self.mem_type)?;
            let mut dst_origin: [u32; 3] = dst_origin.try_into()?;

            if self.mem_type == CL_MEM_OBJECT_IMAGE1D_ARRAY {
                (dst_origin[1], dst_origin[2]) = (dst_origin[2], dst_origin[1]);
            }

            ctx.resource_copy_region(src_res, dst_res, &dst_origin, &bx);
        }
        Ok(())
    }

    pub fn fill(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        pattern: &[u8],
        mut offset: usize,
        size: usize,
    ) -> CLResult<()> {
        assert!(self.is_buffer());

        let b = self.to_parent(&mut offset);
        let res = b.get_res()?.get(&q.device).unwrap();
        ctx.clear_buffer(
            res,
            pattern,
            offset.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            size.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
        );
        Ok(())
    }

    pub fn fill_image(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        pattern: &[u32],
        origin: &CLVec<usize>,
        region: &CLVec<usize>,
    ) -> CLResult<()> {
        assert!(!self.is_buffer());

        let res = self.get_res()?.get(&q.device).unwrap();
        let bx = create_box(origin, region, self.mem_type)?;
        // make sure we allocate multiples of 4 bytes so drivers don't read out of bounds or
        // unaligned.
        // TODO: use div_ceil once it's available
        let size = align(
            self.image_format.pixel_size().unwrap() as usize,
            size_of::<u32>(),
        );
        let mut new_pattern: Vec<u32> = vec![0; size / size_of::<u32>()];

        // we don't support CL_DEPTH for now
        assert!(pattern.len() == 4);

        // SAFETY: pointers have to be valid for read/writes of exactly one pixel of their
        // respective format.
        // `new_pattern` has the correct size due to the `size` above.
        // `pattern` is validated through the CL API and allows undefined behavior if not followed
        // by CL API rules. It's expected to be a 4 component array of 32 bit values, except for
        // CL_DEPTH where it's just one value.
        unsafe {
            util_format_pack_rgba(
                self.image_format.to_pipe_format().unwrap(),
                new_pattern.as_mut_ptr().cast(),
                pattern.as_ptr().cast(),
                1,
            );
        }

        ctx.clear_texture(res, &new_pattern, &bx);

        Ok(())
    }

    pub fn write_from_user_rect(
        &self,
        src: *const c_void,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        src_row_pitch: usize,
        mut src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        if self.is_buffer() {
            let (offset, size) =
                buffer_offset_size(dst_origin, region, dst_row_pitch, dst_slice_pitch);
            let tx = self.tx(q, ctx, offset, size, RWFlags::WR)?;

            sw_copy(
                src,
                tx.ptr(),
                region,
                src_origin,
                src_row_pitch,
                src_slice_pitch,
                &CLVec::default(),
                dst_row_pitch,
                dst_slice_pitch,
                1,
            );
        } else {
            assert!(dst_row_pitch == self.image_desc.image_row_pitch);
            assert!(dst_slice_pitch == self.image_desc.image_slice_pitch);
            assert!(src_origin == &CLVec::default());

            let res = self.get_res()?.get(&q.device).unwrap();
            let bx = create_box(dst_origin, region, self.mem_type)?;

            if self.mem_type == CL_MEM_OBJECT_IMAGE1D_ARRAY {
                src_slice_pitch = src_row_pitch;
            }

            ctx.texture_subdata(
                res,
                &bx,
                src,
                src_row_pitch
                    .try_into()
                    .map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
                src_slice_pitch
                    .try_into()
                    .map_err(|_| CL_OUT_OF_HOST_MEMORY)?,
            );
        }
        Ok(())
    }

    pub fn read_to_user_rect(
        &self,
        dst: *mut c_void,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        mut src_row_pitch: usize,
        mut src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        let tx;
        let pixel_size;

        if self.is_buffer() {
            let (offset, size) =
                buffer_offset_size(src_origin, region, src_row_pitch, src_slice_pitch);
            tx = self.tx(q, ctx, offset, size, RWFlags::RD)?;
            pixel_size = 1;
        } else {
            assert!(dst_origin == &CLVec::default());

            let bx = create_box(src_origin, region, self.mem_type)?;
            tx = self.tx_image(q, ctx, &bx, RWFlags::RD)?;
            src_row_pitch = tx.row_pitch() as usize;
            src_slice_pitch = tx.slice_pitch() as usize;

            pixel_size = self.image_format.pixel_size().unwrap();
        };

        sw_copy(
            tx.ptr(),
            dst,
            region,
            &CLVec::default(),
            src_row_pitch,
            src_slice_pitch,
            dst_origin,
            dst_row_pitch,
            dst_slice_pitch,
            pixel_size,
        );

        Ok(())
    }

    pub fn copy_to_rect(
        &self,
        dst: &Self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        region: &CLVec<usize>,
        src_origin: &CLVec<usize>,
        src_row_pitch: usize,
        src_slice_pitch: usize,
        dst_origin: &CLVec<usize>,
        dst_row_pitch: usize,
        dst_slice_pitch: usize,
    ) -> CLResult<()> {
        assert!(self.is_buffer());
        assert!(dst.is_buffer());

        let (offset, size) = buffer_offset_size(src_origin, region, src_row_pitch, src_slice_pitch);
        let tx_src = self.tx(q, ctx, offset, size, RWFlags::RD)?;

        let (offset, size) = buffer_offset_size(dst_origin, region, dst_row_pitch, dst_slice_pitch);
        let tx_dst = dst.tx(q, ctx, offset, size, RWFlags::WR)?;

        // TODO check to use hw accelerated paths (e.g. resource_copy_region or blits)
        sw_copy(
            tx_src.ptr(),
            tx_dst.ptr(),
            region,
            &CLVec::default(),
            src_row_pitch,
            src_slice_pitch,
            &CLVec::default(),
            dst_row_pitch,
            dst_slice_pitch,
            1,
        );

        Ok(())
    }

    // TODO: only sync on map when the memory is not mapped with discard
    pub fn sync_shadow_buffer(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        ptr: *mut c_void,
    ) -> CLResult<()> {
        let mut lock = self.maps.lock().unwrap();
        if !lock.increase_ref(&q.device, ptr) {
            return Ok(());
        }

        if self.has_user_shadow_buffer(&q.device)? {
            self.read_to_user(q, ctx, 0, self.host_ptr, self.size)
        } else {
            if let Some(shadow) = lock.tx.get(&q.device).and_then(|tx| tx.shadow.as_ref()) {
                let mut offset = 0;
                let b = self.to_parent(&mut offset);
                let res = b.get_res_of_dev(&q.device)?;
                let bx = pipe_box {
                    width: self.size as i32,
                    height: 1,
                    depth: 1,
                    x: offset as i32,
                    ..Default::default()
                };
                ctx.resource_copy_region(res, shadow, &[0; 3], &bx);
            }
            Ok(())
        }
    }

    // TODO: only sync on map when the memory is not mapped with discard
    pub fn sync_shadow_image(
        &self,
        q: &Arc<Queue>,
        ctx: &PipeContext,
        ptr: *mut c_void,
    ) -> CLResult<()> {
        let mut lock = self.maps.lock().unwrap();
        if !lock.increase_ref(&q.device, ptr) {
            return Ok(());
        }

        if self.has_user_shadow_buffer(&q.device)? {
            self.read_to_user_rect(
                self.host_ptr,
                q,
                ctx,
                &self.image_desc.api_size(),
                &CLVec::default(),
                0,
                0,
                &CLVec::default(),
                self.image_desc.image_row_pitch,
                self.image_desc.image_slice_pitch,
            )
        } else {
            if let Some(shadow) = lock.tx.get(&q.device).and_then(|tx| tx.shadow.as_ref()) {
                let res = self.get_res_of_dev(&q.device)?;
                let bx = self.image_desc.bx()?;
                ctx.resource_copy_region(res, shadow, &[0, 0, 0], &bx);
            }
            Ok(())
        }
    }

    /// Maps the queue associated device's resource.
    ///
    /// Mapping resources could have been quite straightforward if OpenCL wouldn't allow for so
    /// called non blocking maps. Non blocking maps shall return a valid pointer to the mapped
    /// region immediately, but should not synchronize data (in case of shadow buffers) until after
    /// the map event is reached in the queue.
    /// This makes it not possible to simply use pipe_transfers as those can't be explicitly synced
    /// by the frontend.
    ///
    /// In order to have a compliant implementation of the mapping API we have to consider the
    /// following cases:
    ///   1. Mapping a cl_mem object with CL_MEM_USE_HOST_PTR: We simply return the host_ptr.
    ///      Synchronization of shadowed host ptrs are done in `sync_shadow_buffer` and
    ///      `sync_shadow_image` on demand.
    ///   2. Mapping linear resources on UMA systems: We simply create the pipe_transfer with
    ///      `PIPE_MAP_DIRECTLY` and `PIPE_MAP_UNSYNCHRONIZED` and return the attached pointer.
    ///   3. On non UMA systems or when 2. fails (e.g. due to the resource being tiled) we
    ///      - create a shadow pipe_resource with `PIPE_USAGE_STAGING`,
    ///        `PIPE_RESOURCE_FLAG_MAP_PERSISTENT` and `PIPE_RESOURCE_FLAG_MAP_COHERENT`
    ///      - create a pipe_transfer with `PIPE_MAP_COHERENT`, `PIPE_MAP_PERSISTENT` and
    ///        `PIPE_MAP_UNSYNCHRONIZED`
    ///      - sync the shadow buffer like a host_ptr shadow buffer in 1.
    ///
    /// Taking this approach we guarentee that we only copy when actually needed while making sure
    /// the content behind the returned pointer is valid until unmapped.
    fn map<'a>(
        &self,
        q: &Arc<Queue>,
        lock: &'a mut MutexGuard<Mappings>,
        rw: RWFlags,
    ) -> CLResult<&'a PipeTransfer> {
        if !lock.tx.contains_key(&q.device) {
            let (tx, res) = if self.is_buffer() {
                self.tx_raw_async(q, rw)?
            } else {
                let bx = self.image_desc.bx()?;
                self.tx_image_raw_async(q, &bx, rw)?
            };

            lock.tx
                .insert(q.device.clone(), MappingTransfer::new(tx, res));
        } else {
            lock.mark_pending(&q.device);
        }

        Ok(&lock.tx.get_mut(&q.device).unwrap().tx)
    }

    pub fn map_buffer(&self, q: &Arc<Queue>, offset: usize, _size: usize) -> CLResult<*mut c_void> {
        assert!(self.is_buffer());

        let mut lock = self.maps.lock().unwrap();
        let ptr = if self.has_user_shadow_buffer(&q.device)? {
            self.host_ptr
        } else {
            let tx = self.map(q, &mut lock, RWFlags::RW)?;
            tx.ptr()
        };

        let ptr = unsafe { ptr.add(offset) };
        Ok(ptr)
    }

    pub fn map_image(
        &self,
        q: &Arc<Queue>,
        origin: &CLVec<usize>,
        _region: &CLVec<usize>,
        row_pitch: &mut usize,
        slice_pitch: &mut usize,
    ) -> CLResult<*mut c_void> {
        assert!(!self.is_buffer());

        let mut lock = self.maps.lock().unwrap();

        // we might have a host_ptr shadow buffer
        let ptr = if self.has_user_shadow_buffer(&q.device)? {
            *row_pitch = self.image_desc.image_row_pitch;
            *slice_pitch = self.image_desc.image_slice_pitch;

            self.host_ptr
        } else {
            let tx = self.map(q, &mut lock, RWFlags::RW)?;

            if self.image_desc.dims() > 1 {
                *row_pitch = tx.row_pitch() as usize;
            }
            if self.image_desc.dims() > 2 || self.image_desc.is_array() {
                *slice_pitch = tx.slice_pitch() as usize;
            }

            tx.ptr()
        };

        let ptr = unsafe {
            ptr.add(
                *origin
                    * [
                        self.image_format.pixel_size().unwrap() as usize,
                        *row_pitch,
                        *slice_pitch,
                    ],
            )
        };

        Ok(ptr)
    }

    pub fn is_mapped_ptr(&self, ptr: *mut c_void) -> bool {
        self.maps.lock().unwrap().maps.contains_key(&ptr)
    }

    // TODO: only sync on unmap when the memory is not mapped for writing
    pub fn unmap(&self, q: &Arc<Queue>, ctx: &PipeContext, ptr: *mut c_void) -> CLResult<()> {
        let mut lock = self.maps.lock().unwrap();
        if !lock.maps.contains_key(&ptr) {
            return Ok(());
        }

        let (needs_sync, shadow) = lock.decrease_ref(ptr, &q.device);
        if needs_sync {
            if let Some(shadow) = shadow {
                let mut offset = 0;
                let b = self.to_parent(&mut offset);
                let res = b.get_res_of_dev(&q.device)?;

                let bx = if b.is_buffer() {
                    pipe_box {
                        width: self.size as i32,
                        height: 1,
                        depth: 1,
                        ..Default::default()
                    }
                } else {
                    self.image_desc.bx()?
                };

                ctx.resource_copy_region(shadow, res, &[offset as u32, 0, 0], &bx);
            } else if self.has_user_shadow_buffer(&q.device)? {
                if self.is_buffer() {
                    self.write_from_user(q, ctx, 0, self.host_ptr, self.size)?;
                } else {
                    self.write_from_user_rect(
                        self.host_ptr,
                        q,
                        ctx,
                        &self.image_desc.api_size(),
                        &CLVec::default(),
                        self.image_desc.image_row_pitch,
                        self.image_desc.image_slice_pitch,
                        &CLVec::default(),
                        self.image_desc.image_row_pitch,
                        self.image_desc.image_slice_pitch,
                    )?;
                }
            }
        }

        lock.clean_up_tx(&q.device, ctx);

        Ok(())
    }
}

impl Drop for Mem {
    fn drop(&mut self) {
        let cl = cl_mem::from_ptr(self);
        self.cbs
            .get_mut()
            .unwrap()
            .iter()
            .rev()
            .for_each(|cb| cb(cl));

        for (d, tx) in self.maps.lock().unwrap().tx.drain() {
            d.helper_ctx().unmap(tx.tx);
        }
    }
}

#[repr(C)]
pub struct Sampler {
    pub base: CLObjectBase<CL_INVALID_SAMPLER>,
    pub context: Arc<Context>,
    pub normalized_coords: bool,
    pub addressing_mode: cl_addressing_mode,
    pub filter_mode: cl_filter_mode,
    pub props: Option<Properties<cl_sampler_properties>>,
}

impl_cl_type_trait!(cl_sampler, Sampler, CL_INVALID_SAMPLER);

impl Sampler {
    pub fn new(
        context: Arc<Context>,
        normalized_coords: bool,
        addressing_mode: cl_addressing_mode,
        filter_mode: cl_filter_mode,
        props: Option<Properties<cl_sampler_properties>>,
    ) -> Arc<Sampler> {
        Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            normalized_coords: normalized_coords,
            addressing_mode: addressing_mode,
            filter_mode: filter_mode,
            props: props,
        })
    }

    pub fn nir_to_cl(
        addressing_mode: u32,
        filter_mode: u32,
        normalized_coords: u32,
    ) -> (cl_addressing_mode, cl_filter_mode, bool) {
        let addr_mode = match addressing_mode {
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_NONE => CL_ADDRESS_NONE,
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_CLAMP_TO_EDGE => {
                CL_ADDRESS_CLAMP_TO_EDGE
            }
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_CLAMP => CL_ADDRESS_CLAMP,
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_REPEAT => CL_ADDRESS_REPEAT,
            cl_sampler_addressing_mode::SAMPLER_ADDRESSING_MODE_REPEAT_MIRRORED => {
                CL_ADDRESS_MIRRORED_REPEAT
            }
            _ => panic!("unkown addressing_mode"),
        };

        let filter = match filter_mode {
            cl_sampler_filter_mode::SAMPLER_FILTER_MODE_NEAREST => CL_FILTER_NEAREST,
            cl_sampler_filter_mode::SAMPLER_FILTER_MODE_LINEAR => CL_FILTER_LINEAR,
            _ => panic!("unkown filter_mode"),
        };

        (addr_mode, filter, normalized_coords != 0)
    }

    pub fn cl_to_pipe(
        (addressing_mode, filter_mode, normalized_coords): (
            cl_addressing_mode,
            cl_filter_mode,
            bool,
        ),
    ) -> pipe_sampler_state {
        let mut res = pipe_sampler_state::default();

        let wrap = match addressing_mode {
            CL_ADDRESS_CLAMP_TO_EDGE => pipe_tex_wrap::PIPE_TEX_WRAP_CLAMP_TO_EDGE,
            CL_ADDRESS_CLAMP => pipe_tex_wrap::PIPE_TEX_WRAP_CLAMP_TO_BORDER,
            CL_ADDRESS_REPEAT => pipe_tex_wrap::PIPE_TEX_WRAP_REPEAT,
            CL_ADDRESS_MIRRORED_REPEAT => pipe_tex_wrap::PIPE_TEX_WRAP_MIRROR_REPEAT,
            // TODO: what's a reasonable default?
            _ => pipe_tex_wrap::PIPE_TEX_WRAP_CLAMP_TO_EDGE,
        };

        let img_filter = match filter_mode {
            CL_FILTER_NEAREST => pipe_tex_filter::PIPE_TEX_FILTER_NEAREST,
            CL_FILTER_LINEAR => pipe_tex_filter::PIPE_TEX_FILTER_LINEAR,
            _ => panic!("unkown filter_mode"),
        };

        res.set_min_img_filter(img_filter);
        res.set_mag_img_filter(img_filter);
        res.set_unnormalized_coords((!normalized_coords).into());
        res.set_wrap_r(wrap);
        res.set_wrap_s(wrap);
        res.set_wrap_t(wrap);

        res
    }

    pub fn pipe(&self) -> pipe_sampler_state {
        Self::cl_to_pipe((
            self.addressing_mode,
            self.filter_mode,
            self.normalized_coords,
        ))
    }
}
