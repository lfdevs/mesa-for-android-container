// Copyright © 2024 Valve Corp. and Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::extent::{units, Extent4D, Offset4D};
use crate::tiling::{GOBType, Tiling};

use std::ffi::c_void;
use std::ops::Range;

// This file is dedicated to the internal tiling layout, mainly in the context
// of CPU-based tiled memcpy implementations (and helpers) for
// VK_EXT_host_image_copy
//
// Details on the NVIDIA tiling format can be found in the documentaiton for
// the [`Tiling`] struct.
//
// The way our implementation will work is by splitting an image into tiles,
// then each tile will be broken into its GOBs, and finally each GOB into 16B
// or 8B sectors, where each sector will be copied into its position.
//
// For code sharing and cleanliness, we write everything to be very generic,
// so as to be shared between Linear <-> Tiled and Tiled <-> Linear paths, and
// (ab)use Rust's traits to specialize the last level (copy_gob/copy_whole_gob)
// for a particular direction.
//
// The copy_x and copy_whole_x distinction is made because if we can guarantee
// that tiles/gobs are whole and aligned, we can skip all bounds checking and
// copy things in fast and tight loops

/// Copies a GOB
///
/// This trait should be implemented twice for each GOB type, once for
/// tiled-to-linear and once for linear-to-tiled.  This allows to implement
/// the rest of tiled copies in a generic way.
trait CopyGOB {
    const GOB_EXTENT_B: Extent4D<units::Bytes>;
    const X_DIVISOR: u32;

    unsafe fn copy_gob(
        tiled: usize,
        linear: LinearPointer,
        start: Offset4D<units::Bytes>,
        end: Offset4D<units::Bytes>,
    );

    // No bounding box for this one
    unsafe fn copy_whole_gob(tiled: usize, linear: LinearPointer) {
        unsafe {
            Self::copy_gob(
                tiled,
                linear,
                Offset4D::new(0, 0, 0, 0),
                Offset4D::new(0, 0, 0, 0) + Self::GOB_EXTENT_B,
            );
        }
    }
}

/// An often simpler trait to implement than CopyGOB
trait CopyGOBLines {
    const GOB_EXTENT_B: Extent4D<units::Bytes>;
    const LINE_WIDTH_B: u32;
    const X_DIVISOR: u32;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize);
    unsafe fn copy_whole_line(tiled: *mut u8, linear: *mut u8);

    /// Iterate over the lines of a GOB, calling `f` once for each line.  The
    /// first parameter to f is the offset in the GOB in bytes.  The following
    /// parameters are an (x, y, z) coordinate.
    fn for_each_gob_line(f: impl FnMut(u32, u32, u32, u32));
}

impl<C: CopyGOBLines> CopyGOB for C {
    const GOB_EXTENT_B: Extent4D<units::Bytes> = C::GOB_EXTENT_B;
    const X_DIVISOR: u32 = C::X_DIVISOR;

    unsafe fn copy_gob(
        tiled: usize,
        linear: LinearPointer,
        start: Offset4D<units::Bytes>,
        end: Offset4D<units::Bytes>,
    ) {
        C::for_each_gob_line(|offset, x, y, z| {
            if y >= start.y && y < end.y && z >= start.z && z < end.z {
                let tiled = tiled + (offset as usize);
                let linear = linear.at(Offset4D::new(x, y, z, 0));
                if x >= start.x && x + C::LINE_WIDTH_B <= end.x {
                    unsafe {
                        C::copy_whole_line(tiled as *mut _, linear as *mut _);
                    }
                } else if x + C::LINE_WIDTH_B >= start.x && x < end.x {
                    let start = (std::cmp::max(x, start.x) - x) as usize;
                    let end =
                        std::cmp::min(end.x - x, C::LINE_WIDTH_B) as usize;
                    unsafe {
                        C::copy(
                            (tiled + start) as *mut _,
                            (linear + start) as *mut _,
                            end - start,
                        );
                    }
                }
            }
        });
    }

    unsafe fn copy_whole_gob(tiled: usize, linear: LinearPointer) {
        Self::for_each_gob_line(|offset, x, y, z| {
            let tiled = tiled + (offset as usize);
            let linear = linear.at(Offset4D::new(x, y, z, 0));
            unsafe {
                C::copy_whole_line(tiled as *mut _, linear as *mut _);
            }
        });
    }
}

/// Copies at most 16B of data to/from linear
trait CopyBytes {
    const X_DIVISOR: u32;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize);
    unsafe fn copy_16b(tiled: *mut [u8; 16], linear: *mut [u8; 16]) {
        unsafe {
            Self::copy(tiled as *mut _, linear as *mut _, 16);
        }
    }
    unsafe fn copy_8b(tiled: *mut [u8; 8], linear: *mut [u8; 8]) {
        unsafe {
            Self::copy(tiled as *mut _, linear as *mut _, 8);
        }
    }
}

/// Implements copies for [`GOBType::FermiColor`]
struct CopyGOBFermi<C: CopyBytes> {
    phantom: std::marker::PhantomData<C>,
}

impl<C: CopyBytes> CopyGOBLines for CopyGOBFermi<C> {
    const GOB_EXTENT_B: Extent4D<units::Bytes> = Extent4D::new(64, 8, 1, 1);
    const LINE_WIDTH_B: u32 = 16;
    const X_DIVISOR: u32 = C::X_DIVISOR;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        unsafe {
            C::copy(tiled, linear, bytes);
        }
    }

    unsafe fn copy_whole_line(tiled: *mut u8, linear: *mut u8) {
        unsafe {
            C::copy_16b(tiled as *mut _, linear as *mut _);
        }
    }

    #[inline(always)]
    fn for_each_gob_line(mut f: impl FnMut(u32, u32, u32, u32)) {
        for i in 0..2 {
            f(i * 0x100 + 0x00, i * 32 + 0, 2, 0);
            f(i * 0x100 + 0x10, i * 32 + 0, 3, 0);
            f(i * 0x100 + 0x20, i * 32 + 0, 0, 0);
            f(i * 0x100 + 0x30, i * 32 + 0, 1, 0);

            f(i * 0x100 + 0x40, i * 32 + 16, 0, 0);
            f(i * 0x100 + 0x50, i * 32 + 16, 1, 0);
            f(i * 0x100 + 0x60, i * 32 + 16, 2, 0);
            f(i * 0x100 + 0x70, i * 32 + 16, 3, 0);

            f(i * 0x100 + 0xc0, i * 32 + 0, 6, 0);
            f(i * 0x100 + 0xd0, i * 32 + 0, 7, 0);
            f(i * 0x100 + 0xe0, i * 32 + 0, 4, 0);
            f(i * 0x100 + 0xf0, i * 32 + 0, 5, 0);

            f(i * 0x100 + 0x80, i * 32 + 16, 4, 0);
            f(i * 0x100 + 0x90, i * 32 + 16, 5, 0);
            f(i * 0x100 + 0xa0, i * 32 + 16, 6, 0);
            f(i * 0x100 + 0xb0, i * 32 + 16, 7, 0);
        }
    }
}

/// Implements copies for [`GOBType::TegraColor`]
struct CopyGOBTegra<C: CopyBytes> {
    phantom: std::marker::PhantomData<C>,
}

impl<C: CopyBytes> CopyGOBLines for CopyGOBTegra<C> {
    const GOB_EXTENT_B: Extent4D<units::Bytes> = Extent4D::new(64, 8, 1, 1);
    const LINE_WIDTH_B: u32 = 16;
    const X_DIVISOR: u32 = C::X_DIVISOR;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        unsafe {
            C::copy(tiled, linear, bytes);
        }
    }

    unsafe fn copy_whole_line(tiled: *mut u8, linear: *mut u8) {
        unsafe {
            C::copy_16b(tiled as *mut _, linear as *mut _);
        }
    }

    #[inline(always)]
    fn for_each_gob_line(mut f: impl FnMut(u32, u32, u32, u32)) {
        for i in 0..2 {
            f(i * 0x100 + 0x00, i * 32 + 0, 0, 0);
            f(i * 0x100 + 0x10, i * 32 + 0, 1, 0);
            f(i * 0x100 + 0x20, i * 32 + 16, 0, 0);
            f(i * 0x100 + 0x30, i * 32 + 16, 1, 0);

            f(i * 0x100 + 0x40, i * 32 + 0, 2, 0);
            f(i * 0x100 + 0x50, i * 32 + 0, 3, 0);
            f(i * 0x100 + 0x60, i * 32 + 16, 2, 0);
            f(i * 0x100 + 0x70, i * 32 + 16, 3, 0);

            f(i * 0x100 + 0x80, i * 32 + 0, 4, 0);
            f(i * 0x100 + 0x90, i * 32 + 0, 5, 0);
            f(i * 0x100 + 0xa0, i * 32 + 16, 4, 0);
            f(i * 0x100 + 0xb0, i * 32 + 16, 5, 0);

            f(i * 0x100 + 0xc0, i * 32 + 0, 6, 0);
            f(i * 0x100 + 0xd0, i * 32 + 0, 7, 0);
            f(i * 0x100 + 0xe0, i * 32 + 16, 6, 0);
            f(i * 0x100 + 0xf0, i * 32 + 16, 7, 0);
        }
    }
}

/// Implements copies for [`GOBType::TuringColor2D`]
struct CopyGOBTuring2D<C: CopyBytes> {
    phantom: std::marker::PhantomData<C>,
}

impl<C: CopyBytes> CopyGOBLines for CopyGOBTuring2D<C> {
    const GOB_EXTENT_B: Extent4D<units::Bytes> = Extent4D::new(64, 8, 1, 1);
    const LINE_WIDTH_B: u32 = 16;
    const X_DIVISOR: u32 = C::X_DIVISOR;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        unsafe {
            C::copy(tiled, linear, bytes);
        }
    }

    unsafe fn copy_whole_line(tiled: *mut u8, linear: *mut u8) {
        unsafe {
            C::copy_16b(tiled as *mut _, linear as *mut _);
        }
    }

    #[inline(always)]
    fn for_each_gob_line(mut f: impl FnMut(u32, u32, u32, u32)) {
        for i in 0..2 {
            f(i * 0x100 + 0x00, i * 32 + 0, 0, 0);
            f(i * 0x100 + 0x10, i * 32 + 0, 1, 0);
            f(i * 0x100 + 0x20, i * 32 + 0, 2, 0);
            f(i * 0x100 + 0x30, i * 32 + 0, 3, 0);

            f(i * 0x100 + 0x40, i * 32 + 16, 0, 0);
            f(i * 0x100 + 0x50, i * 32 + 16, 1, 0);
            f(i * 0x100 + 0x60, i * 32 + 16, 2, 0);
            f(i * 0x100 + 0x70, i * 32 + 16, 3, 0);

            f(i * 0x100 + 0x80, i * 32 + 0, 4, 0);
            f(i * 0x100 + 0x90, i * 32 + 0, 5, 0);
            f(i * 0x100 + 0xa0, i * 32 + 0, 6, 0);
            f(i * 0x100 + 0xb0, i * 32 + 0, 7, 0);

            f(i * 0x100 + 0xc0, i * 32 + 16, 4, 0);
            f(i * 0x100 + 0xd0, i * 32 + 16, 5, 0);
            f(i * 0x100 + 0xe0, i * 32 + 16, 6, 0);
            f(i * 0x100 + 0xf0, i * 32 + 16, 7, 0);
        }
    }
}

/// Implements copies for [`GOBType::Blackwell16Bit`]
struct CopyGOBBlackwell2D2BPP<C: CopyBytes> {
    phantom: std::marker::PhantomData<C>,
}

impl<C: CopyBytes> CopyGOBLines for CopyGOBBlackwell2D2BPP<C> {
    const GOB_EXTENT_B: Extent4D<units::Bytes> = Extent4D::new(64, 8, 1, 1);
    const LINE_WIDTH_B: u32 = 16;
    const X_DIVISOR: u32 = C::X_DIVISOR;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        unsafe {
            C::copy(tiled, linear, bytes);
        }
    }

    unsafe fn copy_whole_line(tiled: *mut u8, linear: *mut u8) {
        unsafe {
            C::copy_16b(tiled as *mut _, linear as *mut _);
        }
    }

    #[inline(always)]
    fn for_each_gob_line(mut f: impl FnMut(u32, u32, u32, u32)) {
        for i in 0..2 {
            f(i * 0x100 + 0x00, i * 32 + 0, 0, 0);
            f(i * 0x100 + 0x10, i * 32 + 0, 1, 0);
            f(i * 0x100 + 0x20, i * 32 + 0, 2, 0);
            f(i * 0x100 + 0x30, i * 32 + 0, 3, 0);

            f(i * 0x100 + 0x80, i * 32 + 16, 0, 0);
            f(i * 0x100 + 0x90, i * 32 + 16, 1, 0);
            f(i * 0x100 + 0xa0, i * 32 + 16, 2, 0);
            f(i * 0x100 + 0xb0, i * 32 + 16, 3, 0);

            f(i * 0x100 + 0x40, i * 32 + 0, 4, 0);
            f(i * 0x100 + 0x50, i * 32 + 0, 5, 0);
            f(i * 0x100 + 0x60, i * 32 + 0, 6, 0);
            f(i * 0x100 + 0x70, i * 32 + 0, 7, 0);

            f(i * 0x100 + 0xc0, i * 32 + 16, 4, 0);
            f(i * 0x100 + 0xd0, i * 32 + 16, 5, 0);
            f(i * 0x100 + 0xe0, i * 32 + 16, 6, 0);
            f(i * 0x100 + 0xf0, i * 32 + 16, 7, 0);
        }
    }
}

/// Implements copies for [`GOBType::Blackwell8Bit`]
struct CopyGOBBlackwell2D1BPP<C: CopyBytes> {
    phantom: std::marker::PhantomData<C>,
}

impl<C: CopyBytes> CopyGOBLines for CopyGOBBlackwell2D1BPP<C> {
    const GOB_EXTENT_B: Extent4D<units::Bytes> = Extent4D::new(64, 8, 1, 1);
    const LINE_WIDTH_B: u32 = 8;
    const X_DIVISOR: u32 = C::X_DIVISOR;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        unsafe {
            C::copy(tiled, linear, bytes);
        }
    }

    unsafe fn copy_whole_line(tiled: *mut u8, linear: *mut u8) {
        unsafe {
            C::copy_8b(tiled as *mut _, linear as *mut _);
        }
    }

    #[inline(always)]
    fn for_each_gob_line(mut f: impl FnMut(u32, u32, u32, u32)) {
        for x in 0..8 {
            for y in 0..8 {
                f(x * 0x40 + y * 0x8, x * 0x8, y, 0);
            }
        }
    }
}

fn aligned_range(start: u32, end: u32, align: u32) -> Range<u32> {
    debug_assert!(align.is_power_of_two());
    let align_1 = align - 1;
    (start & !align_1)..((end + align_1) & !align_1)
}

fn chunk_range(
    whole: Range<u32>,
    chunk_start: u32,
    chunk_len: u32,
) -> Range<u32> {
    debug_assert!(chunk_start < whole.end);
    let start = whole.start.saturating_sub(chunk_start);
    let end = std::cmp::min(whole.end - chunk_start, chunk_len);
    start..end
}

fn for_each_extent4d<U>(
    start: Offset4D<U>,
    end: Offset4D<U>,
    chunk: Extent4D<U>,
    mut f: impl FnMut(Offset4D<U>, Offset4D<U>, Offset4D<U>),
) {
    debug_assert!(chunk.width.is_power_of_two());
    debug_assert!(chunk.height.is_power_of_two());
    debug_assert!(chunk.depth.is_power_of_two());
    debug_assert!(chunk.array_len == 1);

    debug_assert!(start.a == 0);
    debug_assert!(end.a == 1);

    let x_range = aligned_range(start.x, end.x, chunk.width);
    let y_range = aligned_range(start.y, end.y, chunk.height);
    let z_range = aligned_range(start.z, end.z, chunk.depth);

    for z in z_range.step_by(chunk.depth as usize) {
        let chunk_z = chunk_range(start.z..end.z, z, chunk.depth);
        for y in y_range.clone().step_by(chunk.height as usize) {
            let chunk_y = chunk_range(start.y..end.y, y, chunk.height);
            for x in x_range.clone().step_by(chunk.width as usize) {
                let chunk_x = chunk_range(start.x..end.x, x, chunk.width);
                let chunk_start = Offset4D::new(x, y, z, start.a);
                let start = Offset4D::new(
                    chunk_x.start,
                    chunk_y.start,
                    chunk_z.start,
                    start.a,
                );
                let end =
                    Offset4D::new(chunk_x.end, chunk_y.end, chunk_z.end, end.a);
                f(chunk_start, start, end);
            }
        }
    }
}

fn for_each_extent4d_aligned<U>(
    start: Offset4D<U>,
    end: Offset4D<U>,
    chunk: Extent4D<U>,
    mut f: impl FnMut(Offset4D<U>),
) {
    debug_assert!(start.x % chunk.width == 0);
    debug_assert!(start.y % chunk.height == 0);
    debug_assert!(start.z % chunk.depth == 0);
    debug_assert!(start.a == 0);

    debug_assert!(end.x % chunk.width == 0);
    debug_assert!(end.y % chunk.height == 0);
    debug_assert!(end.z % chunk.depth == 0);
    debug_assert!(end.a == 1);

    debug_assert!(chunk.width.is_power_of_two());
    debug_assert!(chunk.height.is_power_of_two());
    debug_assert!(chunk.depth.is_power_of_two());
    debug_assert!(chunk.array_len == 1);

    for z in (start.z..end.z).step_by(chunk.depth as usize) {
        for y in (start.y..end.y).step_by(chunk.height as usize) {
            for x in (start.x..end.x).step_by(chunk.width as usize) {
                f(Offset4D::new(x, y, z, start.a));
            }
        }
    }
}

struct BlockPointer {
    pointer: usize,
    x_mul: usize,
    y_mul: usize,
    z_mul: usize,
    #[cfg(debug_assertions)]
    bl_extent: Extent4D<units::Bytes>,
}

impl BlockPointer {
    fn new(
        pointer: usize,
        bl_extent: Extent4D<units::Bytes>,
        extent: Extent4D<units::Bytes>,
    ) -> BlockPointer {
        debug_assert!(bl_extent.array_len == 1);

        debug_assert!(extent.width % bl_extent.width == 0);
        debug_assert!(extent.height % bl_extent.height == 0);
        debug_assert!(extent.depth % bl_extent.depth == 0);
        debug_assert!(extent.array_len == 1);

        BlockPointer {
            pointer,
            // We assume that offsets passed to at() are aligned to bl_extent so
            //
            //    x_bl * bl_size_B
            //  = (x / bl_extent.width) * bl_size_B
            //  = x * (bl_size_B / bl_extent.width)
            //  = x * bl_extent.height * bl_extent.depth
            x_mul: (bl_extent.height as usize) * (bl_extent.depth as usize),

            //   y_bl * width_bl * bl_size_B
            //   (y / bl_extent.height) * width_bl * bl_size_B
            // = y * (bl_size_B / bl_extent.height) * width_bl
            // = y * bl_extent.width * bl_extent.depth * width_bl
            // = y * (width_bl * bl_extent.width) * bl_extent.depth
            // = x * extent.width * bl_extent.depth
            y_mul: (extent.width as usize) * (bl_extent.depth as usize),

            //   z_bl * width_bl * height_bl * bl_size_B
            // = (z / bl_extent.depth) * width_bl * height_bl * bl_size_B
            // = z * (bl_size_B / bl_extent.depth) * width_bl * height_bl
            // = z * (bl_extent.width * bl_extent.height) * width_bl * height_bl
            // = z * width_bl * bl_extent.width * height_bl * bl_extent.height
            // = z * extent.width * extent.height
            z_mul: (extent.width as usize) * (extent.height as usize),

            #[cfg(debug_assertions)]
            bl_extent,
        }
    }

    #[inline]
    fn at(&self, offset: Offset4D<units::Bytes>) -> usize {
        #[cfg(debug_assertions)]
        {
            debug_assert!(offset.x % self.bl_extent.width == 0);
            debug_assert!(offset.y % self.bl_extent.height == 0);
            debug_assert!(offset.z % self.bl_extent.depth == 0);
            debug_assert!(offset.a == 0);
        }

        self.pointer
            + (offset.z as usize) * self.z_mul
            + (offset.y as usize) * self.y_mul
            + (offset.x as usize) * self.x_mul
    }
}

#[derive(Copy, Clone)]
struct LinearPointer {
    pointer: usize,
    x_shift: u32,
    row_stride_B: usize,
    plane_stride_B: usize,
}

impl LinearPointer {
    fn new(
        pointer: usize,
        x_divisor: u32,
        row_stride_B: usize,
        plane_stride_B: usize,
    ) -> LinearPointer {
        debug_assert!(x_divisor.is_power_of_two());
        LinearPointer {
            pointer,
            x_shift: x_divisor.ilog2(),
            row_stride_B,
            plane_stride_B,
        }
    }

    fn x_divisor(&self) -> u32 {
        1 << self.x_shift
    }

    #[inline]
    fn reverse(self, offset: Offset4D<units::Bytes>) -> LinearPointer {
        debug_assert!(offset.x % (1 << self.x_shift) == 0);
        debug_assert!(offset.a == 0);
        LinearPointer {
            pointer: self
                .pointer
                .wrapping_sub((offset.z as usize) * self.plane_stride_B)
                .wrapping_sub((offset.y as usize) * self.row_stride_B)
                .wrapping_sub((offset.x >> self.x_shift) as usize),
            x_shift: self.x_shift,
            row_stride_B: self.row_stride_B,
            plane_stride_B: self.plane_stride_B,
        }
    }

    #[inline]
    fn at(self, offset: Offset4D<units::Bytes>) -> usize {
        debug_assert!(offset.x % (1 << self.x_shift) == 0);
        debug_assert!(offset.a == 0);
        self.pointer
            .wrapping_add((offset.z as usize) * self.plane_stride_B)
            .wrapping_add((offset.y as usize) * self.row_stride_B)
            .wrapping_add((offset.x >> self.x_shift) as usize)
    }

    #[inline]
    fn offset(self, offset: Offset4D<units::Bytes>) -> LinearPointer {
        LinearPointer {
            pointer: self.at(offset),
            x_shift: self.x_shift,
            row_stride_B: self.row_stride_B,
            plane_stride_B: self.plane_stride_B,
        }
    }
}

unsafe fn copy_tile<CG: CopyGOB>(
    tiling: Tiling,
    tile_ptr: usize,
    linear: LinearPointer,
    start: Offset4D<units::Bytes>,
    end: Offset4D<units::Bytes>,
) {
    debug_assert!(linear.x_divisor() == CG::X_DIVISOR);
    debug_assert!(tiling.gob_type.extent_B() == CG::GOB_EXTENT_B);

    let tile_extent_B = tiling.extent_B();
    let tile_ptr = BlockPointer::new(tile_ptr, CG::GOB_EXTENT_B, tile_extent_B);

    if start.is_aligned_to(CG::GOB_EXTENT_B)
        && end.is_aligned_to(CG::GOB_EXTENT_B)
    {
        for_each_extent4d_aligned(start, end, CG::GOB_EXTENT_B, |gob| unsafe {
            CG::copy_whole_gob(tile_ptr.at(gob), linear.offset(gob));
        });
    } else {
        for_each_extent4d(start, end, CG::GOB_EXTENT_B, |gob, start, end| {
            let tiled = tile_ptr.at(gob);
            let linear = linear.offset(gob);
            if start == Offset4D::new(0, 0, 0, 0)
                && end == Offset4D::new(0, 0, 0, 0) + CG::GOB_EXTENT_B
            {
                unsafe {
                    CG::copy_whole_gob(tiled, linear);
                }
            } else {
                unsafe {
                    CG::copy_gob(tiled, linear, start, end);
                }
            }
        });
    }
}

unsafe fn copy_tiled<CG: CopyGOB>(
    tiling: Tiling,
    level_extent_B: Extent4D<units::Bytes>,
    level_tiled_ptr: usize,
    linear: LinearPointer,
    start: Offset4D<units::Bytes>,
    end: Offset4D<units::Bytes>,
) {
    let tile_extent_B = tiling.extent_B();
    let level_extent_B = level_extent_B.align(&tile_extent_B);

    // Back up the linear pointer so it also points at the start of the level.
    // This way, every step of the iteration can assume that both pointers
    // point to the start chunk of the level, tile, or GOB.
    let linear = linear.reverse(start);

    let level_tiled_ptr =
        BlockPointer::new(level_tiled_ptr, tile_extent_B, level_extent_B);

    for_each_extent4d(start, end, tile_extent_B, |tile, start, end| {
        let tile_ptr = level_tiled_ptr.at(tile);
        let linear = linear.offset(tile);
        unsafe {
            copy_tile::<CG>(tiling, tile_ptr, linear, start, end);
        }
    });
}

struct RawCopyToTiled {}

impl CopyBytes for RawCopyToTiled {
    const X_DIVISOR: u32 = 1;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        // This is backwards from memcpy
        unsafe {
            std::ptr::copy_nonoverlapping(linear, tiled, bytes);
        }
    }
}

struct RawCopyToLinear {}

impl CopyBytes for RawCopyToLinear {
    const X_DIVISOR: u32 = 1;

    unsafe fn copy(tiled: *mut u8, linear: *mut u8, bytes: usize) {
        // This is backwards from memcpy
        unsafe {
            std::ptr::copy_nonoverlapping(tiled, linear, bytes);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn nil_copy_linear_to_tiled(
    tiled_dst: *mut c_void,
    level_extent_B: Extent4D<units::Bytes>,
    linear_src: *const c_void,
    linear_row_stride_B: usize,
    linear_plane_stride_B: usize,
    offset_B: Offset4D<units::Bytes>,
    extent_B: Extent4D<units::Bytes>,
    tiling: &Tiling,
) {
    let end_B = offset_B + extent_B;

    let linear_src = linear_src as usize;
    let tiled_dst = tiled_dst as usize;
    let linear_pointer = LinearPointer::new(
        linear_src,
        1,
        linear_row_stride_B,
        linear_plane_stride_B,
    );

    unsafe {
        match tiling.gob_type {
            GOBType::Blackwell16Bit => {
                copy_tiled::<CopyGOBBlackwell2D2BPP<RawCopyToTiled>>(
                    *tiling,
                    level_extent_B,
                    tiled_dst,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::Blackwell8Bit => {
                copy_tiled::<CopyGOBBlackwell2D1BPP<RawCopyToTiled>>(
                    *tiling,
                    level_extent_B,
                    tiled_dst,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::TuringColor2D => {
                copy_tiled::<CopyGOBTuring2D<RawCopyToTiled>>(
                    *tiling,
                    level_extent_B,
                    tiled_dst,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::TegraColor => {
                copy_tiled::<CopyGOBTegra<RawCopyToTiled>>(
                    *tiling,
                    level_extent_B,
                    tiled_dst,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::FermiColor => {
                copy_tiled::<CopyGOBFermi<RawCopyToTiled>>(
                    *tiling,
                    level_extent_B,
                    tiled_dst,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            _ => panic!("Unsupported GOB type"),
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn nil_copy_tiled_to_linear(
    linear_dst: *mut c_void,
    linear_row_stride_B: usize,
    linear_plane_stride_B: usize,
    tiled_src: *const c_void,
    level_extent_B: Extent4D<units::Bytes>,
    offset_B: Offset4D<units::Bytes>,
    extent_B: Extent4D<units::Bytes>,
    tiling: &Tiling,
) {
    let mut end_B = offset_B + extent_B;
    end_B.a = 1;
    let linear_dst = linear_dst as usize;
    let tiled_src = tiled_src as usize;
    let linear_pointer = LinearPointer::new(
        linear_dst,
        1,
        linear_row_stride_B,
        linear_plane_stride_B,
    );

    unsafe {
        match tiling.gob_type {
            GOBType::Blackwell16Bit => {
                copy_tiled::<CopyGOBBlackwell2D2BPP<RawCopyToLinear>>(
                    *tiling,
                    level_extent_B,
                    tiled_src,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::Blackwell8Bit => {
                copy_tiled::<CopyGOBBlackwell2D1BPP<RawCopyToLinear>>(
                    *tiling,
                    level_extent_B,
                    tiled_src,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::TuringColor2D => {
                copy_tiled::<CopyGOBTuring2D<RawCopyToLinear>>(
                    *tiling,
                    level_extent_B,
                    tiled_src,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::TegraColor => {
                copy_tiled::<CopyGOBTegra<RawCopyToLinear>>(
                    *tiling,
                    level_extent_B,
                    tiled_src,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            GOBType::FermiColor => {
                copy_tiled::<CopyGOBFermi<RawCopyToLinear>>(
                    *tiling,
                    level_extent_B,
                    tiled_src,
                    linear_pointer,
                    offset_B,
                    end_B,
                );
            }
            _ => panic!("Unsupported GOB type"),
        }
    }
}
