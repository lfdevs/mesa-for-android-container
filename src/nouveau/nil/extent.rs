// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::format::Format;
use crate::image::SampleLayout;
use crate::tiling::{GOBType, Tiling};
use crate::Minify;

use std::ops::Add;

pub mod units {
    #[derive(Clone, Debug, Copy, PartialEq)]
    pub struct Elements {}

    #[derive(Clone, Debug, Copy, PartialEq)]
    pub struct Pixels {}

    #[derive(Clone, Debug, Copy, PartialEq)]
    pub struct Samples {}

    #[derive(Clone, Debug, Copy, PartialEq)]
    pub struct Bytes {}

    #[derive(Clone, Debug, Copy, PartialEq)]
    pub struct Tiles {}

    #[derive(Clone, Debug, Copy, PartialEq)]
    pub struct GOBs {}
}

#[derive(Clone, Debug, Copy, PartialEq, Default)]
#[repr(C)]
pub struct Extent4D<U> {
    pub width: u32,
    pub height: u32,
    pub depth: u32,
    pub array_len: u32,
    phantom: std::marker::PhantomData<U>,
}

impl<U> Extent4D<U> {
    pub const fn new(
        width: u32,
        height: u32,
        depth: u32,
        array_len: u32,
    ) -> Extent4D<U> {
        Extent4D {
            width,
            height,
            depth,
            array_len,
            phantom: std::marker::PhantomData,
        }
    }

    pub fn align(self, alignment: &Self) -> Self {
        Self {
            width: self.width.next_multiple_of(alignment.width),
            height: self.height.next_multiple_of(alignment.height),
            depth: self.depth.next_multiple_of(alignment.depth),
            array_len: self.array_len.next_multiple_of(alignment.array_len),
            phantom: std::marker::PhantomData,
        }
    }

    #[allow(dead_code)]
    pub fn is_aligned_to(&self, alignment: Extent4D<U>) -> bool {
        (self.width % alignment.width) == 0
            && (self.height % alignment.height) == 0
            && (self.depth % alignment.depth) == 0
            && (self.array_len % alignment.array_len) == 0
    }

    fn mul<V>(self, other: Extent4D<V>) -> Extent4D<V> {
        Extent4D {
            width: self.width * other.width,
            height: self.height * other.height,
            depth: self.depth * other.depth,
            array_len: self.array_len * other.array_len,
            phantom: std::marker::PhantomData,
        }
    }

    fn div_ceil<V>(self, other: Self) -> Extent4D<V> {
        Extent4D {
            width: self.width.div_ceil(other.width),
            height: self.height.div_ceil(other.height),
            depth: self.depth.div_ceil(other.depth),
            array_len: self.array_len.div_ceil(other.array_len),
            phantom: std::marker::PhantomData,
        }
    }

    pub fn cast_units<V>(self) -> Extent4D<V> {
        Extent4D {
            width: self.width,
            height: self.height,
            depth: self.depth,
            array_len: self.array_len,
            phantom: std::marker::PhantomData,
        }
    }
}

impl Extent4D<units::Pixels> {
    pub fn to_sa(
        self,
        sample_layout: SampleLayout,
    ) -> Extent4D<units::Samples> {
        self.mul(sample_layout.px_extent_sa())
    }

    pub fn to_el(
        self,
        format: Format,
        sample_layout: SampleLayout,
    ) -> Extent4D<units::Elements> {
        self.to_sa(sample_layout).div_ceil(format.el_extent_sa())
    }

    pub fn to_B(
        self,
        format: Format,
        sample_layout: SampleLayout,
    ) -> Extent4D<units::Bytes> {
        self.to_el(format, sample_layout).to_B(format)
    }

    pub fn to_tl(
        self,
        tiling: &Tiling,
        format: Format,
        sample_layout: SampleLayout,
    ) -> Extent4D<units::Tiles> {
        let tl_extent_B = tiling.extent_B();
        self.to_B(format, sample_layout).div_ceil(tl_extent_B)
    }
}

#[no_mangle]
pub extern "C" fn nil_extent4d_px_to_el(
    extent_px: Extent4D<units::Pixels>,
    format: Format,
    sample_layout: SampleLayout,
) -> Extent4D<units::Elements> {
    extent_px.to_el(format, sample_layout)
}

#[no_mangle]
pub extern "C" fn nil_extent4d_px_to_tl(
    extent_px: Extent4D<units::Pixels>,
    tiling: &Tiling,
    format: Format,
    sample_layout: SampleLayout,
) -> Extent4D<units::Tiles> {
    extent_px.to_tl(tiling, format, sample_layout)
}

#[no_mangle]
pub extern "C" fn nil_extent4d_px_to_B(
    extent_px: Extent4D<units::Pixels>,
    format: Format,
    sample_layout: SampleLayout,
) -> Extent4D<units::Bytes> {
    extent_px.to_B(format, sample_layout)
}

impl Extent4D<units::Samples> {
    pub fn to_px(self, sample_layout: SampleLayout) -> Extent4D<units::Pixels> {
        self.div_ceil(sample_layout.px_extent_sa())
    }
}

impl Extent4D<units::Elements> {
    pub fn to_B(self, format: Format) -> Extent4D<units::Bytes> {
        Extent4D {
            width: self.width * format.el_size_B(),
            ..self.cast_units()
        }
    }

    pub fn to_sa(self, format: Format) -> Extent4D<units::Samples> {
        self.mul(format.el_extent_sa())
    }
}

impl Extent4D<units::Bytes> {
    pub fn size_B(&self) -> u64 {
        // size_B of something with layers doesn't make sense because we can't
        // know the array stride based only on the other dimensions.
        assert!(self.array_len == 1);
        u64::from(self.width) * u64::from(self.height) * u64::from(self.depth)
    }

    pub fn to_GOB(self, gob_type: GOBType) -> Extent4D<units::GOBs> {
        self.div_ceil(gob_type.extent_B())
    }
}

#[derive(Clone, Debug, Copy, PartialEq)]
#[repr(C)]
pub struct Offset4D<U> {
    pub x: u32,
    pub y: u32,
    pub z: u32,
    pub a: u32,
    phantom: std::marker::PhantomData<U>,
}

impl<U> Offset4D<U> {
    pub const fn new(x: u32, y: u32, z: u32, a: u32) -> Offset4D<U> {
        Offset4D {
            x,
            y,
            z,
            a,
            phantom: std::marker::PhantomData,
        }
    }

    #[allow(dead_code)]
    pub fn is_aligned_to(&self, alignment: Extent4D<U>) -> bool {
        (self.x % alignment.width) == 0
            && (self.y % alignment.height) == 0
            && (self.z % alignment.depth) == 0
            && (self.a % alignment.array_len) == 0
    }

    fn div_floor<V>(self, other: Extent4D<U>) -> Offset4D<V> {
        Offset4D {
            x: self.x / other.width,
            y: self.y / other.height,
            z: self.z / other.depth,
            a: self.a / other.array_len,
            phantom: std::marker::PhantomData,
        }
    }

    fn mul<V>(self, other: Extent4D<V>) -> Offset4D<V> {
        Offset4D {
            x: self.x * other.width,
            y: self.y * other.height,
            z: self.z * other.depth,
            a: self.a * other.array_len,
            phantom: std::marker::PhantomData,
        }
    }

    fn cast_units<V>(self) -> Offset4D<V> {
        Offset4D {
            x: self.x,
            y: self.y,
            z: self.z,
            a: self.a,
            phantom: std::marker::PhantomData,
        }
    }
}

impl<U> Add<Extent4D<U>> for Offset4D<U> {
    type Output = Offset4D<U>;

    fn add(self, rhs: Extent4D<U>) -> Offset4D<U> {
        Self {
            x: self.x + rhs.width,
            y: self.y + rhs.height,
            z: self.z + rhs.depth,
            a: self.a + rhs.array_len,
            phantom: std::marker::PhantomData,
        }
    }
}

impl Offset4D<units::Pixels> {
    pub fn to_el(
        self,
        format: Format,
        sample_layout: SampleLayout,
    ) -> Offset4D<units::Elements> {
        self.mul(sample_layout.px_extent_sa())
            .div_floor(format.el_extent_sa())
    }

    pub fn to_B(
        self,
        format: Format,
        sample_layout: SampleLayout,
    ) -> Offset4D<units::Bytes> {
        self.to_el(format, sample_layout).to_B(format)
    }

    pub fn to_tl(
        self,
        tiling: &Tiling,
        format: Format,
        sample_layout: SampleLayout,
    ) -> Offset4D<units::Tiles> {
        self.to_B(format, sample_layout)
            .div_floor(tiling.extent_B())
    }
}

#[no_mangle]
pub extern "C" fn nil_offset4d_px_to_el(
    offset: Offset4D<units::Pixels>,
    format: Format,
    sample_layout: SampleLayout,
) -> Offset4D<units::Elements> {
    offset.to_el(format, sample_layout)
}

#[no_mangle]
pub extern "C" fn nil_offset4d_px_to_tl(
    offset: Offset4D<units::Pixels>,
    tiling: &Tiling,
    format: Format,
    sample_layout: SampleLayout,
) -> Offset4D<units::Tiles> {
    offset.to_tl(tiling, format, sample_layout)
}

#[no_mangle]
pub extern "C" fn nil_offset4d_px_to_B(
    offset: Offset4D<units::Pixels>,
    format: Format,
    sample_layout: SampleLayout,
) -> Offset4D<units::Bytes> {
    offset.to_B(format, sample_layout)
}

impl Offset4D<units::Elements> {
    pub fn to_B(self, format: Format) -> Offset4D<units::Bytes> {
        Offset4D {
            x: self.x * format.el_size_B(),
            ..self.cast_units()
        }
    }
}

impl Minify<u32> for Extent4D<units::Pixels> {
    fn minify(self, level: u32) -> Self {
        Self {
            width: self.width.minify(level),
            height: self.height.minify(level),
            depth: self.depth.minify(level),
            ..self
        }
    }
}
