// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT
//
// This file specifies and encodes the descriptor for storage images
// on Kepler.

use crate::format::Format;
use crate::image::ViewType;
use crate::image::{Image, SampleLayout, View, ViewAccess};
use bitview::{BitMutView, SetField};
use nil_rs_bindings::*;

fn format_info(fmt: Format) -> Option<(u8, u8)> {
    let fmt: pipe_format = fmt.into();
    // First value taken from GK104_IMAGE_FORMAT in
    // envytools/rnndb/g80_defs.xml, it corresponds to
    // internal NVIDIA enumerations.
    // Second value is nve4_su_format_aux_map.unk8
    // | 0..2: comp. size: 0 => 32, 1 => 16, 2 => 8
    // | 2..4: n. comps:   0 => 1, 1 => 2, 2 => 4,
    Some(match fmt {
        PIPE_FORMAT_R32G32B32A32_FLOAT => (0x2, 0x8),
        PIPE_FORMAT_R32G32B32A32_SINT => (0x3, 0x8),
        PIPE_FORMAT_R32G32B32A32_UINT => (0x4, 0x8),

        PIPE_FORMAT_R16G16B16A16_FLOAT => (0xc, 0x9),
        PIPE_FORMAT_R16G16B16A16_UNORM => (0x8, 0x9),
        PIPE_FORMAT_R16G16B16A16_SNORM => (0x9, 0x9),
        PIPE_FORMAT_R16G16B16A16_SINT => (0xa, 0x9),
        PIPE_FORMAT_R16G16B16A16_UINT => (0xb, 0x9),

        PIPE_FORMAT_B8G8R8A8_UNORM => (0x11, 0xa),
        PIPE_FORMAT_R8G8B8A8_UNORM => (0x18, 0xa),
        PIPE_FORMAT_R8G8B8A8_SNORM => (0x1a, 0xa),
        PIPE_FORMAT_R8G8B8A8_SINT => (0x1b, 0xa),
        PIPE_FORMAT_R8G8B8A8_UINT => (0x1c, 0xa),

        PIPE_FORMAT_R11G11B10_FLOAT => (0x24, 0xa),
        PIPE_FORMAT_R10G10B10A2_UNORM => (0x13, 0xa),
        PIPE_FORMAT_R10G10B10A2_UINT => (0x15, 0xa),

        PIPE_FORMAT_R32G32_FLOAT => (0xd, 0x4),
        PIPE_FORMAT_R32G32_SINT => (0xe, 0x4),
        PIPE_FORMAT_R32G32_UINT => (0xf, 0x4),

        // Lower R64 as R32G32
        PIPE_FORMAT_R64_FLOAT => (0xd, 0x4),
        PIPE_FORMAT_R64_SINT => (0xe, 0x4),
        PIPE_FORMAT_R64_UINT => (0xf, 0x4),

        PIPE_FORMAT_R16G16_FLOAT => (0x21, 0x5),
        PIPE_FORMAT_R16G16_UNORM => (0x1d, 0x5),
        PIPE_FORMAT_R16G16_SNORM => (0x1e, 0x5),
        PIPE_FORMAT_R16G16_SINT => (0x1f, 0x5),
        PIPE_FORMAT_R16G16_UINT => (0x20, 0x5),

        PIPE_FORMAT_R8G8_UNORM => (0x2e, 0x6),
        PIPE_FORMAT_R8G8_SNORM => (0x2f, 0x6),
        PIPE_FORMAT_R8G8_SINT => (0x30, 0x6),
        PIPE_FORMAT_R8G8_UINT => (0x31, 0x6),

        PIPE_FORMAT_R32_FLOAT => (0x29, 0x0),
        PIPE_FORMAT_R32_SINT => (0x27, 0x0),
        PIPE_FORMAT_R32_UINT => (0x28, 0x0),

        PIPE_FORMAT_R16_FLOAT => (0x36, 0x1),
        PIPE_FORMAT_R16_UNORM => (0x32, 0x1),
        PIPE_FORMAT_R16_SNORM => (0x33, 0x1),
        PIPE_FORMAT_R16_SINT => (0x34, 0x1),
        PIPE_FORMAT_R16_UINT => (0x35, 0x1),

        PIPE_FORMAT_R8_UNORM => (0x37, 0x2),
        PIPE_FORMAT_R8_SNORM => (0x38, 0x2),
        PIPE_FORMAT_R8_SINT => (0x39, 0x2),
        PIPE_FORMAT_R8_UINT => (0x3a, 0x2),
        _ => return None,
    })
}

/// Descriptor for Kepler storage image functions
///
/// This is never passed directly to hardware, but is used by
/// nak_nir_lower_image_addrs to lower image coordinates into addresses.
#[derive(Clone, Debug, Copy, PartialEq)]
#[repr(C)]
pub struct SuInfo {
    /// Address of the image LOF, shifted by 8
    pub addr_shifted8: u32,

    /// Hardware format bitfield
    ///
    /// See encode_format for a more precise description.
    pub format_info: u32,

    /// suclamp value in the X dimension
    /// for buffers this is the number of elements.
    pub clamp_x: u32,

    /// Bits 0 ..24: Pitch in tiles for block linear or
    ///                    in elements for pitch linear
    /// Bits 24..28: Log2 of sample width in pixels
    /// Bits 28..32: Log2 of sample height in pixels
    pub pitch: u32,

    /// suclamp value in the Y dimension
    /// for buffers this is the size of the element in bytes.
    pub clamp_y: u32,

    /// Array stride in bytes, shifted by 8
    pub array_stride_shifted8: u32,

    /// suclamp value in the Z dimension, or array clamp
    pub clamp_z: u32,

    /// Extra offsetting information
    ///
    /// For multisampled images, this is a map from sample index to
    /// x/y position within a pixel. Each nibble is a sample with
    /// x in the low 2 bits and y in the high 2 bits.
    ///
    /// For buffers (which can never be multisampled), this is
    /// the low 8 bits of the address.
    pub extra: u32,
}

fn encode_clamp_block_linear(
    clamp: u32,
    tile_dim_el_log2: u8,
    el_size_B_log2: u8,
    gobs_per_tile_log2: u8,
) -> u32 {
    let mut info_raw = 0_u32;
    let mut info = BitMutView::new(&mut info_raw);

    assert!(tile_dim_el_log2 < 0x10);
    assert!(el_size_B_log2 < 8);
    assert!(gobs_per_tile_log2 < 8);

    info.set_field(0..20, clamp);

    info.set_bit(21, false); // pitch_linear=false

    // Bits to remove (shift right) from the coord to obtain tile coords.
    info.set_field(22..26, tile_dim_el_log2);
    // Shift to obtain coords inside of GoB
    // always zero except in x, where it's the number of bytes of an element.
    info.set_field(26..29, el_size_B_log2);
    info.set_field(29..32, gobs_per_tile_log2);

    info_raw
}

fn encode_clamp_pitch_linear(clamp: u32, el_size_B_log2: u8) -> u32 {
    let mut info_raw = 0_u32;
    let mut info = BitMutView::new(&mut info_raw);

    assert!(el_size_B_log2 < 8);

    info.set_field(0..20, clamp);
    info.set_bit(21, true); // pitch_linear=true
    info.set_field(26..29, el_size_B_log2);

    info_raw
}

impl Format {
    pub(crate) fn supports_kepler_storage(&self) -> bool {
        format_info(*self).is_some()
    }
}

fn encode_format(format: Format) -> u32 {
    let (fmt, comp_description) =
        format_info(format).expect("Unsupported format");

    let mut raw = 0_u32;
    let mut a = BitMutView::new(&mut raw);
    a.set_field(0..8, fmt);
    a.set_field(8..12, comp_description);
    a.set_field(12..16, 0x4);
    a.set_field(16..20, format.el_size_B().ilog2());
    raw
}

fn compute_ms_table(layout: SampleLayout) -> u32 {
    let samples = layout.samples();
    assert!(samples <= 8);
    let samples = samples as u8;

    let mut map = 0_u32;
    for s in 0..samples {
        let off = layout.sa_offset(s);
        assert!(off.x < 4 && off.y < 4);
        let s_xy = (off.y << 2 | off.x) as u32;
        map |= s_xy << (s * 4);
    }
    map
}

#[unsafe(no_mangle)]
pub extern "C" fn nil_fill_su_info(
    dev: &nil_rs_bindings::nv_device_info,
    image: &Image,
    view: &View,
    base_address: u64,
) -> SuInfo {
    assert_eq!(view.access, ViewAccess::Storage);
    assert!(view.format.supports_storage(dev));
    debug_assert!(image.align_B >= 256);
    let level = &image.levels[view.base_level as usize];

    let layer_offset = if view.view_type == ViewType::_3DSliced {
        assert!(view.num_levels == 1);
        assert!(
            view.base_array_layer + view.array_len <= image.extent_px.depth
        );

        image.level_z_offset_B(view.base_level, view.base_array_layer)
    } else {
        assert!(
            view.base_array_layer + view.array_len <= image.extent_px.array_len
        );
        u64::from(view.base_array_layer) * image.array_stride_B + level.offset_B
    };
    let address = base_address + layer_offset;

    let el_size_B_log2 = view.format.el_size_B().ilog2() as u8;
    let tile_width_B_log2 = level.tiling.extent_B().width.ilog2() as u8;
    let tile_height_B_log2 = level.tiling.extent_B().height.ilog2() as u8;
    let tile_depth_B_log2 = level.tiling.extent_B().depth.ilog2() as u8;

    let extent_px = image.level_extent_px(view.base_level);
    let extent_sa = extent_px.to_sa(image.sample_layout);
    let samples = image.sample_layout.px_extent_sa();

    // We reuse dim_z for both image length and depth since
    // 3d images cannot be arrays.
    let depth = match view.view_type {
        ViewType::_1D | ViewType::_2D | ViewType::_3D | ViewType::Cube => {
            extent_sa.depth
        }
        ViewType::_1DArray
        | ViewType::_2DArray
        | ViewType::_3DSliced
        | ViewType::CubeArray => view.array_len,
    };

    let (clamp_x, clamp_y, clamp_z, raw_pitch) = if level.tiling.is_tiled() {
        (
            encode_clamp_block_linear(
                extent_sa.width - 1,
                tile_width_B_log2 - el_size_B_log2,
                el_size_B_log2,
                level.tiling.x_log2,
            ),
            encode_clamp_block_linear(
                extent_sa.height - 1,
                tile_height_B_log2,
                0,
                level.tiling.y_log2,
            ),
            encode_clamp_block_linear(
                depth - 1,
                tile_depth_B_log2,
                0,
                level.tiling.z_log2,
            ),
            // Pitch in GOB coordinate space
            level.row_stride_B >> tile_width_B_log2,
        )
    } else {
        (
            encode_clamp_pitch_linear(extent_sa.width - 1, el_size_B_log2),
            encode_clamp_pitch_linear(extent_sa.height - 1, 0),
            encode_clamp_pitch_linear(depth - 1, 0),
            // Pitch in pixels
            level.row_stride_B >> el_size_B_log2,
        )
    };

    let pitch = {
        let mut raw = 0_u32;
        let mut a = BitMutView::new(&mut raw);
        a.set_field(0..24, raw_pitch);
        a.set_field(24..28, samples.width.ilog2());
        a.set_field(28..32, samples.height.ilog2());
        raw
    };

    assert!((address & 0xff) == 0);
    SuInfo {
        addr_shifted8: (address >> 8)
            .try_into()
            .expect("surface address overflow"),
        format_info: encode_format(view.format),
        clamp_x,
        clamp_y,
        clamp_z,
        pitch,
        array_stride_shifted8: (image.array_stride_B >> 8)
            .try_into()
            .expect("surface array stride overflow"),
        extra: compute_ms_table(image.sample_layout),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn nil_buffer_fill_su_info(
    _dev: &nil_rs_bindings::nv_device_info,
    base_address: u64,
    format: Format,
    num_elements: u32,
) -> SuInfo {
    assert!(format.supports_buffer());

    SuInfo {
        addr_shifted8: (base_address >> 8)
            .try_into()
            .expect("buffer address overflow"),
        format_info: encode_format(format),
        clamp_x: num_elements,
        clamp_y: 0,
        clamp_z: 0,
        pitch: format.el_size_B(),
        array_stride_shifted8: 0,
        extra: (base_address & 0xff) as u32,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn nil_fill_null_su_info(
    _dev: &nil_rs_bindings::nv_device_info,
) -> SuInfo {
    // We need to make everything fail, but this is not automatic
    // you'd think that nvidia could use some special value to always
    // fail every suclamp right? nope
    // We instead always compare the addr with 0 and mix it in with OOB
    // predicates.
    // For image buffer address calcs it's a bit simpler since we have
    // exclusive range clamps, so with num_elements=0 everything is OOB.
    SuInfo {
        addr_shifted8: 0,
        format_info: 0,
        clamp_x: 0,
        clamp_y: 0,
        clamp_z: 0,
        pitch: 0,
        array_stride_shifted8: 0,
        extra: 0,
    }
}
