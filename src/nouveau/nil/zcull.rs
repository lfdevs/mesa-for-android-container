// Copyright © 2025 Valve Corporation
// SPDX-License-Identifier: MIT

use crate::nvidia_headers::Mthd;
use nil_rs_bindings::*;
use nvidia_headers::classes::cl9297::mthd::{
    AssignZcullSubregionsAlgorithm, SetZcullRegionFormatType,
    SetZcullSubregionAllocation, SetZcullSubregionAllocationFormat,
};

pub const MAX_SUBREGIONS: usize = 16;

fn prev_multiple_of(x: u32, y: u32) -> u32 {
    (x / y) * y
}

#[repr(C)]
#[derive(Clone, Debug, PartialEq)]
pub struct ZCull {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
    pub size_B: u32,
    pub align_B: u32,
    pub region_format: u16, // SetZcullRegionFormatType
    pub normalized_aliquots: u32,
    pub aliquot_count: u32,
    pub subregions: [u32; MAX_SUBREGIONS],
    pub subregion_count: u32,
    pub subregion_algorithm: u16, // AssignZcullSubregionsAlgorithm
}

impl ZCull {
    #[unsafe(no_mangle)]
    pub extern "C" fn nil_zcull_new(
        dev: &nv_zcull_device_info,
        x: u32,
        y: u32,
        width: u32,
        height: u32,
    ) -> Self {
        Self::new(dev, x, y, width, height)
    }

    pub fn new(
        dev_info: &nv_zcull_device_info,
        x: u32,
        y: u32,
        width: u32,
        height: u32,
    ) -> Self {
        // Align region
        let x_align: u32 = dev_info.subregion_width_align_pixels;
        let y_align: u32 = dev_info.subregion_height_align_pixels;

        let x0 = prev_multiple_of(x, x_align);
        let x1 = (x + width).next_multiple_of(x_align);
        let y0 = prev_multiple_of(y, y_align);
        let y1 = (y + height).next_multiple_of(y_align);

        let x = x0;
        let y = y0;
        let width: u64 = (x1 - x0).into();
        let height: u64 = (y1 - y0).into();

        // This is based on the heuristic reverse engineered here:
        // https://gitlab.freedesktop.org/mhenning/re/-/blob/main/zcull_fb_size/heuristic.py
        // Our heuristic doesn't match the proprietary diver exactly but it's
        // pretty close
        let normalized_aliquots_per_region = (width * height)
            .div_ceil(dev_info.pixel_squares_by_aliquots.into())
            .div_ceil(dev_info.subregion_count.into());
        let normalized_aliquots = normalized_aliquots_per_region
            * u64::from(dev_info.subregion_count);

        assert!(dev_info.subregion_count <= MAX_SUBREGIONS.try_into().unwrap());
        let mut subregion_aliquots = [0; MAX_SUBREGIONS];
        let mut subregion_format =
            [SetZcullSubregionAllocationFormat::None; MAX_SUBREGIONS];
        let mut subregions = [0; MAX_SUBREGIONS];

        let format_priorities = [
            (SetZcullSubregionAllocationFormat::Z4x8_2x2, 8),
            (SetZcullSubregionAllocationFormat::Z8x8_4x2, 4),
            (SetZcullSubregionAllocationFormat::Z16x16_4x2, 2),
            (SetZcullSubregionAllocationFormat::Z16x16x2_4x4, 1),
        ];

        let mut remaining_aliquots: u64 = dev_info.aliquot_total.into();
        for i in (0..usize::try_from(dev_info.subregion_count).unwrap()).rev() {
            for (fmt, factor) in format_priorities {
                let aliquot_count =
                    (factor * normalized_aliquots_per_region).div_ceil(2);
                let aliquot_count_all =
                    u64::try_from(i + 1).unwrap() * aliquot_count;
                if aliquot_count_all <= remaining_aliquots {
                    subregion_aliquots[i] = aliquot_count;
                    subregion_format[i] = fmt;
                    remaining_aliquots -= subregion_aliquots[i];
                    break;
                }
            }

            subregions[i] = SetZcullSubregionAllocation {
                subregion_id: i.try_into().unwrap(),
                aliquots: subregion_aliquots[i].try_into().unwrap(),
                format: subregion_format[i],
            }
            .to_bits();
        }

        let region_format = SetZcullRegionFormatType::Z4x4;
        let aliquot_count: u64 = subregion_aliquots.iter().sum();

        let mut subregion_algorithm = AssignZcullSubregionsAlgorithm::Static;
        for i in 1..usize::try_from(dev_info.subregion_count).unwrap() {
            if subregion_format[0] != subregion_format[i] {
                subregion_algorithm = AssignZcullSubregionsAlgorithm::Adaptive;
            }
        }

        let aliquot_count = u32::try_from(aliquot_count).unwrap();
        assert!(aliquot_count <= dev_info.aliquot_total);

        let size_B = aliquot_count * dev_info.zcull_region_byte_multiplier
            + dev_info.zcull_region_header_size
            + dev_info.zcull_subregion_header_size;

        ZCull {
            x,
            y,
            width: width.try_into().unwrap(),
            height: height.try_into().unwrap(),
            size_B,
            align_B: 0x400, // TODO: Can this be smaller?
            region_format: region_format as u16,
            normalized_aliquots: normalized_aliquots.try_into().unwrap(),
            aliquot_count,
            subregions,
            subregion_count: dev_info.subregion_count,
            subregion_algorithm: subregion_algorithm as u16,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const REFERENCE_DEV_INFO: nv_zcull_device_info = nv_zcull_device_info {
        width_align_pixels: 224,
        height_align_pixels: 32,
        pixel_squares_by_aliquots: 3584,
        aliquot_total: 5120,
        zcull_region_byte_multiplier: 128,
        zcull_region_header_size: 224,
        zcull_subregion_header_size: 1344,
        subregion_count: 16,
        subregion_width_align_pixels: 224,
        subregion_height_align_pixels: 64,

        ctxsw_size: 657408,
        ctxsw_align: 4096,
    };

    fn check_subregions(zcull: &ZCull, expected_subregion_data: &[u32]) {
        assert_eq!(
            usize::try_from(zcull.subregion_count).unwrap() * 2,
            expected_subregion_data.len(),
        );

        for (i, item) in expected_subregion_data.chunks_exact(2).enumerate() {
            let subregion_aliquots = item[0];
            let subregion_format = u16::try_from(item[1]).unwrap();
            let subregion_format: SetZcullSubregionAllocationFormat =
                unsafe { std::mem::transmute(subregion_format) };
            let expected = SetZcullSubregionAllocation {
                subregion_id: i.try_into().unwrap(),
                aliquots: subregion_aliquots,
                format: subregion_format,
            }
            .to_bits();

            // For Adaptive, our current region assignment heuristic differs
            // slightly from the proprietary driver, so only check equivalence
            // for Static
            if zcull.subregion_algorithm
                == AssignZcullSubregionsAlgorithm::Static as u16
            {
                assert_eq!(zcull.subregions[i], expected);
            }
        }
    }

    fn check_reference(data: [u32; 5 + 16 * 2]) {
        let [width, height, aliquot_count, normalized_aliquots, subregion_algorithm, expected_subregion_data @ ..] =
            data;

        let zcull =
            ZCull::new(&REFERENCE_DEV_INFO, 0, 0, width.into(), height.into());
        assert_eq!(zcull.aliquot_count, aliquot_count);
        assert_eq!(zcull.normalized_aliquots, normalized_aliquots);
        assert_eq!(u32::from(zcull.subregion_algorithm), subregion_algorithm);
        check_subregions(&zcull, &expected_subregion_data);
    }

    macro_rules! test_data {
        {
            $(
                $w:literal, $h:literal, "Z_4X4", $($i:literal),* ;
            )*
        }
        => {
            #[test]
            fn test_reference() {
                $(
                    check_reference([ $w, $h, $($i),* ]);
                )*
            }
        }
    }

    // This data is dumped from the proprietary driver by running a simple
    // vulkan app and analyzing the command buffers it submits
    // See https://gitlab.freedesktop.org/mhenning/re/-/blob/main/zcull_fb_size/collect_data.py
    test_data! {
        224,32,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        224,64,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        224,128,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        224,256,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        224,512,"Z_4X4",128,32,0,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8;
        224,1024,"Z_4X4",256,64,0,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8;
        224,2048,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        224,4096,"Z_4X4",1024,256,0,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8;
        224,8192,"Z_4X4",2048,512,0,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8;
        224,16384,"Z_4X4",4096,1024,0,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8;
        448,32,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        448,64,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        448,128,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        448,256,"Z_4X4",128,32,0,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8;
        448,512,"Z_4X4",256,64,0,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8;
        448,1024,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        448,2048,"Z_4X4",1024,256,0,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8;
        448,4096,"Z_4X4",2048,512,0,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8;
        448,8192,"Z_4X4",4096,1024,0,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8;
        448,16384,"Z_4X4",5120,2048,1,512,8,512,8,512,8,512,8,512,8,512,8,512,8,256,5,256,5,256,5,256,5,128,2,128,2,128,2,64,0,64,0;
        896,32,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        896,64,"Z_4X4",64,16,0,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8,4,8;
        896,128,"Z_4X4",128,32,0,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8;
        896,256,"Z_4X4",256,64,0,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8;
        896,512,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        896,1024,"Z_4X4",1024,256,0,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8;
        896,2048,"Z_4X4",2048,512,0,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8;
        896,4096,"Z_4X4",4096,1024,0,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8;
        896,8192,"Z_4X4",5120,2048,1,512,8,512,8,512,8,512,8,512,8,512,8,512,8,256,5,256,5,256,5,256,5,128,2,128,2,128,2,64,0,64,0;
        896,16384,"Z_4X4",5120,4096,1,1024,8,512,5,512,5,512,5,512,5,256,2,256,2,256,2,256,2,256,2,128,0,128,0,128,0,128,0,128,0,128,0;
        1792,32,"Z_4X4",128,32,0,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8;
        1792,64,"Z_4X4",128,32,0,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8;
        1792,128,"Z_4X4",256,64,0,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8;
        1792,256,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        1792,512,"Z_4X4",1024,256,0,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8;
        1792,1024,"Z_4X4",2048,512,0,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8;
        1792,2048,"Z_4X4",4096,1024,0,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8;
        1792,4096,"Z_4X4",5120,2048,1,512,8,512,8,512,8,512,8,512,8,512,8,512,8,256,5,256,5,256,5,256,5,128,2,128,2,128,2,64,0,64,0;
        1792,8192,"Z_4X4",5120,4096,1,1024,8,512,5,512,5,512,5,512,5,256,2,256,2,256,2,256,2,256,2,128,0,128,0,128,0,128,0,128,0,128,0;
        1792,16384,"Z_4X4",5120,8192,1,512,2,512,2,512,2,512,2,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0;
        3584,32,"Z_4X4",256,64,0,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8;
        3584,64,"Z_4X4",256,64,0,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8,16,8;
        3584,128,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        3584,256,"Z_4X4",1024,256,0,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8;
        3584,512,"Z_4X4",2048,512,0,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8;
        3584,1024,"Z_4X4",4096,1024,0,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8;
        3584,2048,"Z_4X4",5120,2048,1,512,8,512,8,512,8,512,8,512,8,512,8,512,8,256,5,256,5,256,5,256,5,128,2,128,2,128,2,64,0,64,0;
        3584,4096,"Z_4X4",5120,4096,1,1024,8,512,5,512,5,512,5,512,5,256,2,256,2,256,2,256,2,256,2,128,0,128,0,128,0,128,0,128,0,128,0;
        3584,8192,"Z_4X4",5120,8192,1,512,2,512,2,512,2,512,2,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0;
        3584,16384,"Z_4X4",5120,16384,1,512,0,512,0,512,0,512,0,512,0,512,0,512,0,512,0,512,0,512,0,0,15,0,15,0,15,0,15,0,15,0,15;
        7168,32,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        7168,64,"Z_4X4",512,128,0,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8,32,8;
        7168,128,"Z_4X4",1024,256,0,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8,64,8;
        7168,256,"Z_4X4",2048,512,0,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8,128,8;
        7168,512,"Z_4X4",4096,1024,0,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8,256,8;
        7168,1024,"Z_4X4",5120,2048,1,512,8,512,8,512,8,512,8,512,8,512,8,512,8,256,5,256,5,256,5,256,5,128,2,128,2,128,2,64,0,64,0;
        7168,2048,"Z_4X4",5120,4096,1,1024,8,512,5,512,5,512,5,512,5,256,2,256,2,256,2,256,2,256,2,128,0,128,0,128,0,128,0,128,0,128,0;
        7168,4096,"Z_4X4",5120,8192,1,512,2,512,2,512,2,512,2,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0,256,0;
        7168,8192,"Z_4X4",5120,16384,1,512,0,512,0,512,0,512,0,512,0,512,0,512,0,512,0,512,0,512,0,0,15,0,15,0,15,0,15,0,15,0,15;
        7168,16384,"Z_4X4",5120,32768,1,1024,0,1024,0,1024,0,1024,0,1024,0,0,15,0,15,0,15,0,15,0,15,0,15,0,15,0,15,0,15,0,15,0,15;
    }
}
