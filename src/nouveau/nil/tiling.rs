// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::extent::{units, Extent4D};
use crate::format::Format;
use crate::image::{
    ImageDim, ImageUsageFlags, SampleLayout, IMAGE_USAGE_2D_VIEW_BIT,
    IMAGE_USAGE_LINEAR_BIT,
};
use crate::ILog2Ceil;

use nil_rs_bindings::*;
use nvidia_headers::classes::{cl9097, clc597, clcd97};

#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum GOBType {
    #[default]
    /// Indicates a linear (not tiled) image
    Linear,

    /// A grab-bag GOB format for all depth/stencil surfaces
    FermiZS,

    /// The Fermi GOB format for color images
    ///
    /// A `FermiColor` GOB is 512 bytes, arranged in a 64x8 layout and split
    /// into Sectors. Each Sector is 32 Bytes, and the Sectors in a GOB are
    /// arranged in a 16x2 layout (i.e., two 16B lines on top of each other).
    /// It's then arranged into two columns that are 2 sectors by 4, leading to
    /// a 4x4 grid of sectors:
    ///
    /// |           |           |           |           |
    /// |-----------|-----------|-----------|-----------|
    /// | Sector  1 | Sector  2 | Sector  9 | Sector 10 |
    /// | Sector  0 | Sector  3 | Sector  8 | Sector 11 |
    /// | Sector  5 | Sector  6 | Sector 13 | Sector 14 |
    /// | Sector  4 | Sector  7 | Sector 12 | Sector 15 |
    ///
    /// `CopyGOBFermi` implements CPU copies for Fermi color GOBs.
    FermiColor,

    /// The Tegra GOB format for color images
    ///
    /// A `TegraColor` GOB is 512 bytes, arranged in a 64x8 layout and split
    /// into Sectors. Each Sector is 32 Bytes, and the Sectors in a GOB are
    /// arranged in a 16x2 layout (i.e., two 16B lines on top of each other).
    /// It's then arranged into two columns that are 2 sectors by 4, leading to
    /// a 4x4 grid of sectors:
    ///
    /// |           |           |           |           |
    /// |-----------|-----------|-----------|-----------|
    /// | Sector  0 | Sector  1 | Sector  8 | Sector  9 |
    /// | Sector  2 | Sector  3 | Sector 10 | Sector 11 |
    /// | Sector  4 | Sector  5 | Sector 12 | Sector 13 |
    /// | Sector  6 | Sector  7 | Sector 14 | Sector 15 |
    ///
    /// `CopyGOBTegra` implements CPU copies for Tegra color GOBs.
    TegraColor,

    /// The Turing 2D GOB format for color images
    ///
    /// A `TuringColor2D` GOB is 512 bytes, arranged in a 64x8 layout and split
    /// into Sectors. Each Sector is 32 Bytes, and the Sectors in a GOB are
    /// arranged in a 16x2 layout (i.e., two 16B lines on top of each other).
    /// It's then arranged into two columns that are 2 sectors by 4, leading to
    /// a 4x4 grid of sectors:
    ///
    /// |           |           |           |           |
    /// |-----------|-----------|-----------|-----------|
    /// | Sector  0 | Sector  2 | Sector  8 | Sector 10 |
    /// | Sector  1 | Sector  3 | Sector  9 | Sector 11 |
    /// | Sector  4 | Sector  6 | Sector 12 | Sector 14 |
    /// | Sector  5 | Sector  7 | Sector 13 | Sector 15 |
    ///
    /// `CopyGOBTuring2D` implements CPU copies for Turing color 2D GOBs.
    TuringColor2D,

    /// The Blackwell+ GOB format for 8bit images
    ///
    /// A `Blackwell8Bit` GOB is 512 bytes, arranged in a 64x8 layout and split
    /// into Sectors. Each Sector is 32 Bytes, and the Sectors in a GOB are
    /// arranged in a 8x4 layout (i.e., four 8B lines on top of each other).
    /// It's then arranged into two columns that are 4 sectors by 2, leading to
    /// a 2x8 grid of sectors:
    ///
    /// |           |           |           |           |           |           |           |           |
    /// |-----------|-----------|-----------|-----------|-----------|-----------|-----------|-----------|
    /// | Sector  0 | Sector  2 | Sector  4 | Sector  6 | Sector  8 | Sector 10 | Sector 12 | Sector 14 |
    /// | Sector  1 | Sector  3 | Sector  5 | Sector  7 | Sector  9 | Sector 11 | Sector 13 | Sector 15 |
    ///
    /// `CopyGOBBlackwell2D1BPP` implements CPU copies for 8-bit Blackwell
    /// color 2D GOBs.
    Blackwell8Bit,

    /// The Blackwell+ GOB format for 16bit images
    ///
    /// A `Blackwell16Bit` GOB is 512 bytes, arranged in a 64x8 layout and split
    /// into Sectors. Each Sector is 32 Bytes, and the Sectors in a GOB are
    /// arranged in a 16x2 layout (i.e., two 16B lines on top of each other).
    /// It's then arranged into two columns that are 2 sectors by 4, leading to
    /// a 4x4 grid of sectors:
    ///
    /// |           |           |           |           |
    /// |-----------|-----------|-----------|-----------|
    /// | Sector  0 | Sector  1 | Sector  8 | Sector  9 |
    /// | Sector  2 | Sector  3 | Sector 10 | Sector 11 |
    /// | Sector  4 | Sector  5 | Sector 12 | Sector 13 |
    /// | Sector  6 | Sector  7 | Sector 14 | Sector 15 |
    ///
    /// `CopyGOBBlackwell2D2BPP` implements CPU copies for 16-bit Blackwell
    /// color 2D GOBs.
    Blackwell16Bit,

    /// The Blackwell+ GOB format for 24-bit depth images
    BlackwellZ24,
}

impl GOBType {
    pub fn choose(
        dev: &nil_rs_bindings::nv_device_info,
        format: Format,
    ) -> GOBType {
        if dev.cls_eng3d >= clcd97::BLACKWELL_A {
            match pipe_format::from(format) {
                PIPE_FORMAT_Z24X8_UNORM
                | PIPE_FORMAT_X8Z24_UNORM
                | PIPE_FORMAT_Z24_UNORM_S8_UINT
                | PIPE_FORMAT_S8_UINT_Z24_UNORM
                | PIPE_FORMAT_X24S8_UINT
                | PIPE_FORMAT_S8X24_UINT => GOBType::BlackwellZ24,
                _ => match format.el_size_B() {
                    1 => GOBType::Blackwell8Bit,
                    2 => GOBType::Blackwell16Bit,
                    _ => GOBType::TuringColor2D,
                },
            }
        } else if dev.cls_eng3d >= clc597::TURING_A {
            if format.is_depth_or_stencil() {
                GOBType::FermiZS
            } else {
                GOBType::TuringColor2D
            }
        } else if dev.cls_eng3d >= cl9097::FERMI_A {
            if format.is_depth_or_stencil() {
                GOBType::FermiZS
            } else if dev.type_ == NV_DEVICE_TYPE_SOC {
                GOBType::TegraColor
            } else {
                GOBType::FermiColor
            }
        } else {
            panic!("Unsupported 3d engine class")
        }
    }

    pub fn extent_B(&self) -> Extent4D<units::Bytes> {
        match self {
            GOBType::Linear => Extent4D::new(1, 1, 1, 1),
            GOBType::FermiZS
            | GOBType::FermiColor
            | GOBType::TegraColor
            | GOBType::TuringColor2D
            | GOBType::Blackwell8Bit
            | GOBType::Blackwell16Bit
            | GOBType::BlackwellZ24 => Extent4D::new(64, 8, 1, 1),
        }
    }

    #[no_mangle]
    pub extern "C" fn nil_gob_type_height(self) -> u32 {
        self.extent_B().height
    }
}

/// NVIDIA hardware employs a semi-programmable multi-tier image tiling scheme.
///
/// ## Images as arrays of tiles (or blocks)
///
/// Images are first tiled in a grid of rows of tiles (which NVIDIA calls
/// "Blocks"), with one or more columns:
///
/// |         |         |         |         |
/// |---------|---------|---------|---------|
/// | Tile 0  | Tile 1  | Tile 2  | Tile 3  |
/// | Tile 4  | Tile 5  | Tile 6  | Tile 7  |
/// | Tile 8  | Tile 9  | Tile 10 | Tile 11 |
///
/// The tiles (or blocks) themselves are ordered linearly as can be seen above,
/// which is where the "Block Linear" naming comes from for NVIDIA's tiling
/// scheme.
///
/// For 3D images, each tile continues in the Z direction such that tiles
/// contain multiple Z slices. If the image depth is longer than the tile depth,
/// there will be more than one layer of tiles, where a layer is made up of 1 or
/// more Z slices. For example, if the above tile pattern was the first layer of
/// a multilayer arrangement, the second layer would be:
///
/// |         |         |         |         |
/// |---------|---------|---------|---------|
/// | Tile 12 | Tile 13 | Tile 14 | Tile 15 |
/// | Tile 16 | Tile 17 | Tile 18 | Tile 19 |
/// | Tile 20 | Tile 21 | Tile 22 | Tile 23 |
///
/// The number of rows, columns, and layers of tiles can thus be deduced to be:
/// ```
///    rows    = ceiling(image_height / tile_height)
///    columns = ceiling(image_width  / tile_width)
///    layers  = ceiling(image_depth  / tile_depth)
/// ```
/// Where `tile_width`, `tile_height`, and `tile_depth` come from
/// [`Tiling::extent_B`].
///
/// ## Tiles as arrays of GOBs
///
/// Now comes the second tier. Each tile (or block) is composed of GOBs (Groups
/// of Bytes) arranged in linear order, just like tiles are within the image
/// itself.  In the common case, each tile is just vertical column of GOBs.
/// However, for 3D or sparse images, a tile may be fully 3-dimensional.
///
/// The number of GOBs per tiles is controllable by software.  Image
/// descriptors, color target bind methods, and DMA methods all allow
/// programming tile dimensions in units of a power of two number of GOBs.  In
/// NIL, these dimensions are given by [`Tiling::x_log2'], [`Tiling::y_log2'],
/// and [`Tiling::y_log2'].
///
/// ## GOBs as arrays of bytes
///
/// The data may be further swizzled within a GOB.  The swizzling of data within
/// a GOB is determined by the [`GOBType`].
#[derive(Clone, Debug, Default, Copy, PartialEq)]
#[repr(C)]
pub struct Tiling {
    /// GOB type
    pub gob_type: GOBType,
    /// log2 of the X tile dimension in GOBs
    pub x_log2: u8,
    /// log2 of the Y tile dimension in GOBs
    pub y_log2: u8,
    /// log2 of the z tile dimension in GOBs
    pub z_log2: u8,
}

impl Tiling {
    /// Clamps the tiling to less than 2x the given extent in each dimension.
    ///
    /// This operation is done by the hardware at each LOD.
    pub fn clamp(&self, extent_B: Extent4D<units::Bytes>) -> Self {
        let mut tiling = *self;

        if !self.is_tiled() {
            return tiling;
        }

        let tiling_extent_B = self.extent_B();

        if extent_B.width < tiling_extent_B.width
            || extent_B.height < tiling_extent_B.height
            || extent_B.depth < tiling_extent_B.depth
        {
            tiling.x_log2 = 0;
        }

        let extent_GOB = extent_B.to_GOB(tiling.gob_type);

        let ceil_h = extent_GOB.height.ilog2_ceil() as u8;
        let ceil_d = extent_GOB.depth.ilog2_ceil() as u8;

        tiling.y_log2 = std::cmp::min(tiling.y_log2, ceil_h);
        tiling.z_log2 = std::cmp::min(tiling.z_log2, ceil_d);
        tiling
    }

    pub fn size_B(&self) -> u32 {
        let extent_B = self.extent_B();
        extent_B.width * extent_B.height * extent_B.depth * extent_B.array_len
    }

    #[no_mangle]
    pub extern "C" fn nil_tiling_size_B(&self) -> u32 {
        self.size_B()
    }

    pub fn extent_B(&self) -> Extent4D<units::Bytes> {
        let gob_extent_B = self.gob_type.extent_B();
        debug_assert!(gob_extent_B.array_len == 1);
        Extent4D::new(
            gob_extent_B.width << self.x_log2,
            gob_extent_B.height << self.y_log2,
            gob_extent_B.depth << self.z_log2,
            1,
        )
    }
}

pub fn sparse_block_extent_el(
    format: Format,
    dim: ImageDim,
) -> Extent4D<units::Elements> {
    let bits = format.el_size_B() * 8;

    // Taken from Vulkan 1.3.279 spec section entitled "Standard Sparse
    // Image Block Shapes".
    match dim {
        ImageDim::_2D => match bits {
            8 => Extent4D::new(256, 256, 1, 1),
            16 => Extent4D::new(256, 128, 1, 1),
            32 => Extent4D::new(128, 128, 1, 1),
            64 => Extent4D::new(128, 64, 1, 1),
            128 => Extent4D::new(64, 64, 1, 1),
            other => panic!("Invalid texel size {other}"),
        },
        ImageDim::_3D => match bits {
            8 => Extent4D::new(64, 32, 32, 1),
            16 => Extent4D::new(32, 32, 32, 1),
            32 => Extent4D::new(32, 32, 16, 1),
            64 => Extent4D::new(32, 16, 16, 1),
            128 => Extent4D::new(16, 16, 16, 1),
            _ => panic!("Invalid texel size"),
        },
        _ => panic!("Invalid sparse image dimension"),
    }
}

pub fn sparse_block_extent_px(
    format: Format,
    dim: ImageDim,
    sample_layout: SampleLayout,
) -> Extent4D<units::Pixels> {
    sparse_block_extent_el(format, dim)
        .to_sa(format)
        .to_px(sample_layout)
}

pub fn sparse_block_extent_B(
    format: Format,
    dim: ImageDim,
) -> Extent4D<units::Bytes> {
    sparse_block_extent_el(format, dim).to_B(format)
}

#[no_mangle]
pub extern "C" fn nil_sparse_block_extent_px(
    format: Format,
    dim: ImageDim,
    sample_layout: SampleLayout,
) -> Extent4D<units::Pixels> {
    sparse_block_extent_px(format, dim, sample_layout)
}

impl Tiling {
    pub fn sparse(
        dev: &nil_rs_bindings::nv_device_info,
        format: Format,
        dim: ImageDim,
    ) -> Self {
        let sparse_block_extent_B = sparse_block_extent_B(format, dim);

        assert!(sparse_block_extent_B.width.is_power_of_two());
        assert!(sparse_block_extent_B.height.is_power_of_two());
        assert!(sparse_block_extent_B.depth.is_power_of_two());

        let gob_type = GOBType::choose(dev, format);
        let sparse_block_extent_gob = sparse_block_extent_B.to_GOB(gob_type);

        Self {
            gob_type,
            x_log2: sparse_block_extent_gob.width.ilog2().try_into().unwrap(),
            y_log2: sparse_block_extent_gob.height.ilog2().try_into().unwrap(),
            z_log2: sparse_block_extent_gob.depth.ilog2().try_into().unwrap(),
        }
    }

    pub fn choose(
        dev: &nil_rs_bindings::nv_device_info,
        extent_px: Extent4D<units::Pixels>,
        format: Format,
        sample_layout: SampleLayout,
        usage: ImageUsageFlags,
        max_tile_size_B: u32,
    ) -> Tiling {
        assert!((usage & IMAGE_USAGE_LINEAR_BIT) == 0);

        let mut tiling = Tiling {
            gob_type: GOBType::choose(dev, format),
            x_log2: 0,
            y_log2: 5,
            z_log2: 5,
        };

        if (usage & IMAGE_USAGE_2D_VIEW_BIT) != 0 {
            tiling.z_log2 = 0;
        }

        tiling = tiling.clamp(extent_px.to_B(format, sample_layout));

        if max_tile_size_B > 0 {
            while tiling.size_B() > max_tile_size_B {
                let extent_B = tiling.extent_B();
                if tiling.y_log2 > 0 && extent_B.height > extent_B.depth {
                    tiling.y_log2 -= 1;
                } else {
                    tiling.z_log2 -= 1;
                }
            }
        }

        tiling
    }

    pub fn is_tiled(&self) -> bool {
        if self.gob_type == GOBType::Linear {
            debug_assert!(self.x_log2 == 0);
            debug_assert!(self.y_log2 == 0);
            debug_assert!(self.z_log2 == 0);
            false
        } else {
            true
        }
    }
}
