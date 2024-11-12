// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::extent::{units, Extent4D};
use crate::format::Format;
use crate::image::{
    ImageDim, ImageUsageFlags, SampleLayout, IMAGE_USAGE_2D_VIEW_BIT,
    IMAGE_USAGE_LINEAR_BIT,
};
use crate::ILog2Ceil;

#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum GOBType {
    #[default]
    Linear,
    Fermi8,
}

impl GOBType {
    pub fn extent_B(&self) -> Extent4D<units::Bytes> {
        match self {
            GOBType::Linear => Extent4D::new(1, 1, 1, 1),
            GOBType::Fermi8 => Extent4D::new(64, 8, 1, 1),
        }
    }

    #[no_mangle]
    pub extern "C" fn nil_gob_type_height(self) -> u32 {
        self.extent_B().height
    }
}

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
    pub fn sparse(format: Format, dim: ImageDim) -> Self {
        let sparse_block_extent_B = sparse_block_extent_B(format, dim);

        assert!(sparse_block_extent_B.width.is_power_of_two());
        assert!(sparse_block_extent_B.height.is_power_of_two());
        assert!(sparse_block_extent_B.depth.is_power_of_two());

        let gob_type = GOBType::Fermi8;
        let sparse_block_extent_gob = sparse_block_extent_B.to_GOB(gob_type);

        Self {
            gob_type,
            x_log2: sparse_block_extent_gob.width.ilog2().try_into().unwrap(),
            y_log2: sparse_block_extent_gob.height.ilog2().try_into().unwrap(),
            z_log2: sparse_block_extent_gob.depth.ilog2().try_into().unwrap(),
        }
    }

    pub fn choose(
        extent_px: Extent4D<units::Pixels>,
        format: Format,
        sample_layout: SampleLayout,
        usage: ImageUsageFlags,
    ) -> Tiling {
        if (usage & IMAGE_USAGE_LINEAR_BIT) != 0 {
            return Default::default();
        }

        let mut tiling = Tiling {
            gob_type: GOBType::Fermi8,
            x_log2: 0,
            y_log2: 5,
            z_log2: 5,
        };

        if (usage & IMAGE_USAGE_2D_VIEW_BIT) != 0 {
            tiling.z_log2 = 0;
        }

        tiling.clamp(extent_px.to_B(format, sample_layout))
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
