// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

extern crate nvidia_headers;

use std::cmp::min;

use compiler::bindings::*;
use nak_bindings::*;
use nvidia_headers::classes::{cla0c0, clc0c0, clc3c0, clc6c0, clcbc0, clcdc0};

use bitview::*;
use paste::paste;

type QMDBitView<'a> = BitMutView<'a, [u32]>;

trait QMD {
    const GLOBAL_SIZE_LAYOUT: nak_qmd_dispatch_size_layout;

    fn new() -> Self;
    fn set_barrier_count(&mut self, barrier_count: u8);
    fn set_cbuf(&mut self, idx: u8, addr: u64, size: u32);
    fn cbuf_desc_layout(idx: u8) -> nak_qmd_cbuf_desc_layout;
    fn set_global_size(&mut self, width: u32, height: u32, depth: u32);
    fn set_local_size(&mut self, width: u16, height: u16, depth: u16);
    fn set_prog_addr(&mut self, addr: u64);
    fn set_register_count(&mut self, register_count: u8);
    fn set_crs_size(&mut self, crs_size: u32);
    fn set_slm_size(&mut self, slm_size: u32);
    fn set_smem_size(
        &mut self,
        smem_size: u32,
        dev: &nv_device_info,
        info: &nak_shader_info,
    );
}

macro_rules! set_enum {
    ($bv:expr, $cls:ident, $strct:ident, $field:ident, $enum:ident) => {
        $bv.set_field(
            paste! {$cls::[<$strct _ $field>]},
            paste! {$cls::[<$strct _ $field _ $enum>]},
        )
    };
}

macro_rules! set_field {
    ($bv:expr, $cls:ident, $strct:ident, $field:ident, $val:expr) => {
        $bv.set_field(paste! {$cls::[<$strct _ $field>]}, $val)
    };
}

macro_rules! set_array {
    ($bv:expr, $cls:ident, $strct:ident, $f:ident, $i:expr, $x:expr) => {
        $bv.set_field(paste! {$cls::[<$strct _ $f>]}($i), $x)
    };
}

macro_rules! qmd_init {
    ($bv: expr, $c:ident, $s:ident, $mjv:expr, $mnv:expr) => {
        set_field!($bv, $c, $s, QMD_MAJOR_VERSION, $mjv);
        set_field!($bv, $c, $s, QMD_VERSION, $mnv);

        set_enum!($bv, $c, $s, API_VISIBLE_CALL_LIMIT, NO_CHECK);
        set_enum!($bv, $c, $s, SAMPLER_INDEX, INDEPENDENTLY);
    };
}

macro_rules! qmd_impl_common {
    ($c:ident, $s:ident) => {
        fn set_barrier_count(&mut self, barrier_count: u8) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            set_field!(bv, $c, $s, BARRIER_COUNT, barrier_count);
        }

        const GLOBAL_SIZE_LAYOUT: nak_qmd_dispatch_size_layout = {
            assert!(paste! {$c::[<$s _MAX_BIT>]} < size_of::<Self>() * 8);
            assert!(NAK_MAX_QMD_SIZE_B as usize >= size_of::<Self>());

            let w = paste! {$c::[<$s _CTA_RASTER_WIDTH>]};
            let h = paste! {$c::[<$s _CTA_RASTER_HEIGHT>]};
            let d = paste! {$c::[<$s _CTA_RASTER_DEPTH>]};
            nak_qmd_dispatch_size_layout {
                x_start: w.start as u16,
                x_end: w.end as u16,
                y_start: h.start as u16,
                y_end: h.end as u16,
                z_start: d.start as u16,
                z_end: d.end as u16,
            }
        };

        fn set_global_size(&mut self, width: u32, height: u32, depth: u32) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            set_field!(bv, $c, $s, CTA_RASTER_WIDTH, width);
            set_field!(bv, $c, $s, CTA_RASTER_HEIGHT, height);
            set_field!(bv, $c, $s, CTA_RASTER_DEPTH, depth);
        }

        fn set_local_size(&mut self, width: u16, height: u16, depth: u16) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            set_field!(bv, $c, $s, CTA_THREAD_DIMENSION0, width);
            set_field!(bv, $c, $s, CTA_THREAD_DIMENSION1, height);
            set_field!(bv, $c, $s, CTA_THREAD_DIMENSION2, depth);
        }
    };
}

macro_rules! qmd_impl_set_crs_size {
    ($c:ident, $s:ident) => {
        fn set_crs_size(&mut self, crs_size: u32) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            let crs_size = crs_size.next_multiple_of(0x200);
            set_field!(bv, $c, $s, SHADER_LOCAL_MEMORY_CRS_SIZE, crs_size);
        }
    };
}

const NONE_SHIFT: u8 = 0;
const SHIFTED4_SHIFT: u8 = 4;
const SHIFTED6_SHIFT: u8 = 6;

macro_rules! suffix_shift {
    ($suffix:ident) => {
        paste! { [<$suffix _SHIFT>] }
    };
}

macro_rules! suffix_field2 {
    ($c:ident, $s:ident, $field:ident, $subfield:ident, NONE) => {
        paste! { $c::[<$s _ $field _ $subfield>] }
    };
    ($c:ident, $s:ident, $field:ident, $subfield:ident, $suffix:ident) => {
        paste! { $c::[<$s _ $field _ $subfield _ $suffix>] }
    };
}

macro_rules! cbuf_suffix_field {
    ($c:ident, $s:ident, $subfield:ident, $suffix:ident) => {
        suffix_field2!($c, $s, CONSTANT_BUFFER, $subfield, $suffix)
    };
}

macro_rules! qmd_impl_set_cbuf {
    ($c:ident, $s:ident, $addr_suffix:ident, $size_suffix:ident) => {
        fn set_cbuf(&mut self, idx: u8, addr: u64, size: u32) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            let idx = idx.into();

            let addr_shift = suffix_shift!($addr_suffix);
            let addr_shifted = addr >> addr_shift;
            assert!((addr_shifted << addr_shift) == addr);
            bv.set_field(
                cbuf_suffix_field!($c, $s, ADDR_LOWER, $addr_suffix)(idx),
                addr_shifted as u32,
            );
            bv.set_field(
                cbuf_suffix_field!($c, $s, ADDR_UPPER, $addr_suffix)(idx),
                (addr_shifted >> 32) as u32,
            );

            let size_shift = suffix_shift!($size_suffix);
            assert!(((size >> size_shift) << size_shift) == size);
            bv.set_field(
                cbuf_suffix_field!($c, $s, SIZE, $size_suffix)(idx),
                size >> size_shift,
            );

            set_array!(bv, $c, $s, CONSTANT_BUFFER_VALID, idx, true);
        }

        fn cbuf_desc_layout(idx: u8) -> nak_qmd_cbuf_desc_layout {
            let lo = cbuf_suffix_field!($c, $s, ADDR_LOWER, $addr_suffix)(
                idx.into(),
            );
            let hi = cbuf_suffix_field!($c, $s, ADDR_UPPER, $addr_suffix)(
                idx.into(),
            );
            nak_qmd_cbuf_desc_layout {
                addr_shift: suffix_shift!($addr_suffix).into(),
                addr_lo_start: lo.start as u16,
                addr_lo_end: lo.end as u16,
                addr_hi_start: hi.start as u16,
                addr_hi_end: hi.end as u16,
            }
        }
    };
}

macro_rules! qmd_impl_set_prog_addr_32 {
    ($c:ident, $s:ident) => {
        fn set_prog_addr(&mut self, addr: u64) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            set_field!(bv, $c, $s, PROGRAM_OFFSET, addr);
        }
    };
}

macro_rules! prog_addr_suffix_field {
    ($c:ident, $s:ident, $subfield:ident, $suffix:ident) => {
        suffix_field2!($c, $s, PROGRAM_ADDRESS, $subfield, $suffix)
    };
}

macro_rules! qmd_impl_set_prog_addr_64 {
    ($c:ident, $s:ident, $addr_suffix:ident) => {
        fn set_prog_addr(&mut self, addr: u64) {
            let mut bv = QMDBitView::new(&mut self.qmd);

            let addr_shift = suffix_shift!($addr_suffix);
            let addr_shifted = addr >> addr_shift;
            assert!((addr_shifted << addr_shift) == addr);

            bv.set_field(
                prog_addr_suffix_field!($c, $s, LOWER, $addr_suffix),
                addr_shifted as u32,
            );
            bv.set_field(
                prog_addr_suffix_field!($c, $s, UPPER, $addr_suffix),
                (addr_shifted >> 32) as u32,
            );
        }
    };
}

macro_rules! qmd_impl_set_register_count {
    ($c:ident, $s:ident, $field:ident) => {
        fn set_register_count(&mut self, register_count: u8) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            set_field!(bv, $c, $s, $field, register_count);
        }
    };
}

macro_rules! qmd_impl_set_slm_size {
    ($c:ident, $s:ident, $size_suffix:ident) => {
        fn set_slm_size(&mut self, slm_size: u32) {
            let mut bv = QMDBitView::new(&mut self.qmd);
            let slm_size = slm_size.next_multiple_of(0x10);
            let size_shift = suffix_shift!($size_suffix);
            let slm_size_shifted = slm_size >> size_shift;
            assert!(slm_size_shifted << size_shift == slm_size);
            bv.set_field(
                suffix_field2!(
                    $c,
                    $s,
                    SHADER_LOCAL_MEMORY_HIGH,
                    SIZE,
                    $size_suffix
                ),
                0,
            );
            bv.set_field(
                suffix_field2!(
                    $c,
                    $s,
                    SHADER_LOCAL_MEMORY_LOW,
                    SIZE,
                    $size_suffix
                ),
                slm_size_shifted,
            );
        }
    };
}

mod qmd_0_6 {
    use crate::qmd::*;
    use nvidia_headers::classes::cla0c0::qmd as cla0c0;

    #[repr(transparent)]
    pub struct Qmd0_6 {
        qmd: [u32; 64],
    }

    impl QMD for Qmd0_6 {
        fn new() -> Self {
            let mut qmd = [0; 64];
            let mut bv = QMDBitView::new(&mut qmd);
            qmd_init!(bv, cla0c0, QMDV00_06, 0, 6);
            set_field!(bv, cla0c0, QMDV00_06, SASS_VERSION, 0x30);
            Self { qmd }
        }

        qmd_impl_common!(cla0c0, QMDV00_06);
        qmd_impl_set_crs_size!(cla0c0, QMDV00_06);
        qmd_impl_set_cbuf!(cla0c0, QMDV00_06, NONE, NONE);
        qmd_impl_set_prog_addr_32!(cla0c0, QMDV00_06);
        qmd_impl_set_register_count!(cla0c0, QMDV00_06, REGISTER_COUNT);
        qmd_impl_set_slm_size!(cla0c0, QMDV00_06, NONE);

        fn set_smem_size(
            &mut self,
            smem_size: u32,
            _dev: &nv_device_info,
            _info: &nak_shader_info,
        ) {
            let mut bv = QMDBitView::new(&mut self.qmd);

            let smem_size = smem_size.next_multiple_of(0x100);
            set_field!(bv, cla0c0, QMDV00_06, SHARED_MEMORY_SIZE, smem_size);

            let l1_config = if smem_size <= (16 << 10) {
                cla0c0::QMDV00_06_L1_CONFIGURATION_DIRECTLY_ADDRESSABLE_MEMORY_SIZE_16KB
            } else if smem_size <= (32 << 10) {
                cla0c0::QMDV00_06_L1_CONFIGURATION_DIRECTLY_ADDRESSABLE_MEMORY_SIZE_32KB
            } else if smem_size <= (48 << 10) {
                cla0c0::QMDV00_06_L1_CONFIGURATION_DIRECTLY_ADDRESSABLE_MEMORY_SIZE_48KB
            } else {
                panic!("Invalid shared memory size");
            };
            set_field!(bv, cla0c0, QMDV00_06, L1_CONFIGURATION, l1_config);
        }
    }
}
use qmd_0_6::Qmd0_6;

mod qmd_2_1 {
    use crate::qmd::*;
    use nvidia_headers::classes::clc0c0::qmd as clc0c0;

    #[repr(transparent)]
    pub struct Qmd2_1 {
        qmd: [u32; 64],
    }

    impl QMD for Qmd2_1 {
        fn new() -> Self {
            let mut qmd = [0; 64];
            let mut bv = QMDBitView::new(&mut qmd);
            qmd_init!(bv, clc0c0, QMDV02_01, 2, 1);
            set_field!(bv, clc0c0, QMDV02_01, SM_GLOBAL_CACHING_ENABLE, true);
            Self { qmd }
        }

        qmd_impl_common!(clc0c0, QMDV02_01);
        qmd_impl_set_crs_size!(clc0c0, QMDV02_01);
        qmd_impl_set_cbuf!(clc0c0, QMDV02_01, NONE, SHIFTED4);
        qmd_impl_set_prog_addr_32!(clc0c0, QMDV02_01);
        qmd_impl_set_register_count!(clc0c0, QMDV02_01, REGISTER_COUNT);
        qmd_impl_set_slm_size!(clc0c0, QMDV02_01, NONE);

        fn set_smem_size(
            &mut self,
            smem_size: u32,
            _dev: &nv_device_info,
            _info: &nak_shader_info,
        ) {
            let mut bv = QMDBitView::new(&mut self.qmd);

            let smem_size = smem_size.next_multiple_of(0x100);
            set_field!(bv, clc0c0, QMDV02_01, SHARED_MEMORY_SIZE, smem_size);
        }
    }
}
use qmd_2_1::Qmd2_1;

fn gv100_smem_size_to_hw(size_kb: u16) -> u16 {
    assert!(size_kb % 4 == 0);
    (size_kb / 4) + 1
}

/// Returns the SM_CONFIG_SHARED_MEM_SIZE for the given workgroups and shared
/// memory size.
fn gv100_pick_smem_size_kb(
    size: u32,
    smem_sizes_kb: &[u16],
    workgroups: u32,
) -> u16 {
    let highest = *smem_sizes_kb.last().unwrap();
    // How many workgroups can we launch with the requested shared mem size?
    let workgroups = min(workgroups, u32::from(highest) * 1024 / size.max(1));
    assert!(
        workgroups > 0,
        "Requested shared memory not supported by the hw."
    );

    // Now that we know for how many workgroups we have shared memory, try to
    // find the smallest allocation that fits.
    *smem_sizes_kb
        .iter()
        .find(|&&val| u32::from(val) * 1024 >= size * workgroups)
        .unwrap_or(&highest)
}

/// Returns the (MIN_, TARGET_, MAX_) SM_CONFIG_SHARED_MEM_SIZE values.
fn gv100_get_hw_smem_sizes(
    smem_size: u32,
    dev: &nv_device_info,
    info: &nak_shader_info,
) -> (u16, u16, u16) {
    let cs_info = unsafe {
        assert!(info.stage == MESA_SHADER_COMPUTE);
        &info.__bindgen_anon_1.cs
    };

    let smem_sizes = &dev.sm_smem_sizes_kB[0..dev.sm_smem_size_count.into()];
    let threads =
        cs_info.local_size[0] * cs_info.local_size[1] * cs_info.local_size[2];
    let warps = threads.div_ceil(32);
    let workgroups_per_sm = info.max_warps_per_sm / u32::from(warps);

    let min = gv100_pick_smem_size_kb(smem_size, smem_sizes, 1);
    let target =
        gv100_pick_smem_size_kb(smem_size, smem_sizes, workgroups_per_sm);
    let max = *smem_sizes.last().unwrap();

    (
        gv100_smem_size_to_hw(min),
        gv100_smem_size_to_hw(target),
        gv100_smem_size_to_hw(max),
    )
}

macro_rules! qmd_impl_set_smem_size_bounded {
    ($c:ident, $s:ident) => {
        fn set_smem_size(
            &mut self,
            smem_size: u32,
            dev: &nv_device_info,
            info: &nak_shader_info,
        ) {
            let mut bv = QMDBitView::new(&mut self.qmd);

            let smem_size = smem_size.next_multiple_of(0x100);
            set_field!(bv, $c, $s, SHARED_MEMORY_SIZE, smem_size);

            let (min, target, max) =
                gv100_get_hw_smem_sizes(smem_size, dev, info);

            set_field!(bv, $c, $s, MIN_SM_CONFIG_SHARED_MEM_SIZE, min);
            set_field!(bv, $c, $s, MAX_SM_CONFIG_SHARED_MEM_SIZE, max);
            set_field!(bv, $c, $s, TARGET_SM_CONFIG_SHARED_MEM_SIZE, target);
        }
    };
}

macro_rules! qmd_impl_set_smem_size_bounded_gb {
    ($c:ident, $s:ident) => {
        fn set_smem_size(
            &mut self,
            smem_size: u32,
            dev: &nv_device_info,
            info: &nak_shader_info,
        ) {
            let mut bv = QMDBitView::new(&mut self.qmd);

            let smem_size = smem_size.next_multiple_of(0x100);
            let size_shift = 7;
            let smem_size_shifted = smem_size >> size_shift;
            assert!((smem_size_shifted << size_shift) == smem_size);
            set_field!(
                bv,
                $c,
                $s,
                SHARED_MEMORY_SIZE_SHIFTED7,
                smem_size_shifted
            );

            let (min, target, max) =
                gv100_get_hw_smem_sizes(smem_size, dev, info);

            set_field!(bv, $c, $s, MIN_SM_CONFIG_SHARED_MEM_SIZE, min);
            set_field!(bv, $c, $s, MAX_SM_CONFIG_SHARED_MEM_SIZE, max);
            set_field!(bv, $c, $s, TARGET_SM_CONFIG_SHARED_MEM_SIZE, target);
        }
    };
}

mod qmd_2_2 {
    use crate::qmd::*;
    use nvidia_headers::classes::clc3c0::qmd as clc3c0;

    #[repr(transparent)]
    pub struct Qmd2_2 {
        qmd: [u32; 64],
    }

    impl QMD for Qmd2_2 {
        fn new() -> Self {
            let mut qmd = [0; 64];
            let mut bv = QMDBitView::new(&mut qmd);
            qmd_init!(bv, clc3c0, QMDV02_02, 2, 2);
            set_field!(bv, clc3c0, QMDV02_02, SM_GLOBAL_CACHING_ENABLE, true);
            Self { qmd }
        }

        qmd_impl_common!(clc3c0, QMDV02_02);
        qmd_impl_set_crs_size!(clc3c0, QMDV02_02);
        qmd_impl_set_cbuf!(clc3c0, QMDV02_02, NONE, SHIFTED4);
        qmd_impl_set_prog_addr_64!(clc3c0, QMDV02_02, NONE);
        qmd_impl_set_register_count!(clc3c0, QMDV02_02, REGISTER_COUNT_V);
        qmd_impl_set_smem_size_bounded!(clc3c0, QMDV02_02);
        qmd_impl_set_slm_size!(clc3c0, QMDV02_02, NONE);
    }
}
use qmd_2_2::Qmd2_2;

mod qmd_3_0 {
    use crate::qmd::*;
    use nvidia_headers::classes::clc6c0::qmd as clc6c0;

    #[repr(transparent)]
    pub struct Qmd3_0 {
        qmd: [u32; 64],
    }

    impl QMD for Qmd3_0 {
        fn new() -> Self {
            let mut qmd = [0; 64];
            let mut bv = QMDBitView::new(&mut qmd);
            qmd_init!(bv, clc6c0, QMDV03_00, 3, 0);
            set_field!(bv, clc6c0, QMDV03_00, SM_GLOBAL_CACHING_ENABLE, true);
            Self { qmd }
        }

        qmd_impl_common!(clc6c0, QMDV03_00);

        fn set_crs_size(&mut self, crs_size: u32) {
            assert!(crs_size == 0);
        }

        qmd_impl_set_cbuf!(clc6c0, QMDV03_00, NONE, SHIFTED4);
        qmd_impl_set_prog_addr_64!(clc6c0, QMDV03_00, NONE);
        qmd_impl_set_register_count!(clc6c0, QMDV03_00, REGISTER_COUNT_V);
        qmd_impl_set_smem_size_bounded!(clc6c0, QMDV03_00);
        qmd_impl_set_slm_size!(clc6c0, QMDV03_00, NONE);
    }
}
use qmd_3_0::Qmd3_0;

mod qmd_4_0 {
    use crate::qmd::*;
    mod clcbc0 {
        pub use nvidia_headers::classes::clcbc0::qmd::*;

        // Some renames we have to carry for Hopper
        pub use QMDV04_00_GRID_DEPTH as QMDV04_00_CTA_RASTER_DEPTH;
        pub use QMDV04_00_GRID_HEIGHT as QMDV04_00_CTA_RASTER_HEIGHT;
        pub use QMDV04_00_GRID_WIDTH as QMDV04_00_CTA_RASTER_WIDTH;
        pub use QMDV04_00_QMD_MINOR_VERSION as QMDV04_00_QMD_VERSION;
    }

    #[repr(transparent)]
    pub struct Qmd4_0 {
        qmd: [u32; 96],
    }

    impl QMD for Qmd4_0 {
        fn new() -> Self {
            let mut qmd = [0; 96];
            let mut bv = QMDBitView::new(&mut qmd);
            qmd_init!(bv, clcbc0, QMDV04_00, 4, 0);
            Self { qmd }
        }

        qmd_impl_common!(clcbc0, QMDV04_00);

        fn set_crs_size(&mut self, crs_size: u32) {
            assert!(crs_size == 0);
        }

        qmd_impl_set_cbuf!(clcbc0, QMDV04_00, SHIFTED6, SHIFTED4);
        qmd_impl_set_prog_addr_64!(clcbc0, QMDV04_00, NONE);
        qmd_impl_set_register_count!(clcbc0, QMDV04_00, REGISTER_COUNT);
        qmd_impl_set_smem_size_bounded!(clcbc0, QMDV04_00);
        qmd_impl_set_slm_size!(clcbc0, QMDV04_00, NONE);
    }
}
use qmd_4_0::Qmd4_0;

mod qmd_5_0 {
    use crate::qmd::*;
    // Blackwell_A is CD97, Blackwell_B is CE97, however if Hopper is v4 and
    // Blackwell_B is v5, then Blackwell_A also has to be v5 hence we'll use
    // clcdc0qmd here
    mod clcdc0 {
        pub use nvidia_headers::classes::clcdc0::qmd::*;

        // Some renames we may have to carry for Blackwell
        pub use QMDV05_00_GRID_DEPTH as QMDV05_00_CTA_RASTER_DEPTH;
        pub use QMDV05_00_GRID_HEIGHT as QMDV05_00_CTA_RASTER_HEIGHT;
        pub use QMDV05_00_GRID_WIDTH as QMDV05_00_CTA_RASTER_WIDTH;
        pub use QMDV05_00_QMD_MINOR_VERSION as QMDV05_00_QMD_VERSION;
    }

    pub struct Qmd5_0 {
        qmd: [u32; 96],
    }

    impl QMD for Qmd5_0 {
        fn new() -> Self {
            let mut qmd = [0; 96];
            let mut bv = QMDBitView::new(&mut qmd);
            qmd_init!(bv, clcdc0, QMDV05_00, 5, 0);
            set_field!(bv, clcdc0, QMDV05_00, QMD_TYPE, 0x2);
            set_field!(bv, clcdc0, QMDV05_00, QMD_GROUP_ID, 0x1f);
            Self { qmd }
        }

        qmd_impl_common!(clcdc0, QMDV05_00);

        fn set_crs_size(&mut self, crs_size: u32) {
            assert!(crs_size == 0);
        }

        qmd_impl_set_cbuf!(clcdc0, QMDV05_00, SHIFTED6, SHIFTED4);
        qmd_impl_set_prog_addr_64!(clcdc0, QMDV05_00, SHIFTED4);
        qmd_impl_set_register_count!(clcdc0, QMDV05_00, REGISTER_COUNT);
        qmd_impl_set_smem_size_bounded_gb!(clcdc0, QMDV05_00);
        qmd_impl_set_slm_size!(clcdc0, QMDV05_00, SHIFTED4);
    }
}
use qmd_5_0::Qmd5_0;

fn fill_qmd<Q: QMD>(
    dev: &nv_device_info,
    info: &nak_shader_info,
    qmd_info: &nak_qmd_info,
) -> Q {
    let cs_info = unsafe {
        assert!(info.stage == MESA_SHADER_COMPUTE);
        &info.__bindgen_anon_1.cs
    };

    let mut qmd = Q::new();

    qmd.set_barrier_count(info.num_control_barriers);
    qmd.set_global_size(
        qmd_info.global_size[0],
        qmd_info.global_size[1],
        qmd_info.global_size[2],
    );
    qmd.set_local_size(
        cs_info.local_size[0],
        cs_info.local_size[1],
        cs_info.local_size[2],
    );
    qmd.set_prog_addr(qmd_info.addr);
    qmd.set_register_count(info.num_gprs);
    qmd.set_crs_size(info.crs_size);
    qmd.set_slm_size(info.slm_size);

    assert!(qmd_info.smem_size <= u32::from(dev.max_smem_per_wg_kB) * 1024);
    qmd.set_smem_size(qmd_info.smem_size.into(), dev, info);

    for i in 0..qmd_info.num_cbufs {
        let cb = &qmd_info.cbufs[usize::try_from(i).unwrap()];
        if cb.size > 0 {
            qmd.set_cbuf(cb.index.try_into().unwrap(), cb.addr, cb.size);
        }
    }

    qmd
}

#[no_mangle]
pub extern "C" fn nak_qmd_size_B(dev: &nv_device_info) -> u32 {
    let size_B = if dev.cls_compute >= clcdc0::BLACKWELL_COMPUTE_A {
        size_of::<Qmd5_0>().try_into().unwrap()
    } else if dev.cls_compute >= clcbc0::HOPPER_COMPUTE_A {
        size_of::<Qmd4_0>().try_into().unwrap()
    } else if dev.cls_compute >= clc6c0::AMPERE_COMPUTE_A {
        size_of::<Qmd3_0>().try_into().unwrap()
    } else if dev.cls_compute >= clc3c0::VOLTA_COMPUTE_A {
        size_of::<Qmd2_2>().try_into().unwrap()
    } else if dev.cls_compute >= clc0c0::PASCAL_COMPUTE_A {
        size_of::<Qmd2_1>().try_into().unwrap()
    } else if dev.cls_compute >= cla0c0::KEPLER_COMPUTE_A {
        size_of::<Qmd0_6>().try_into().unwrap()
    } else {
        panic!("Unknown shader model");
    };
    assert!(size_B <= NAK_MAX_QMD_SIZE_B);
    size_B
}

#[no_mangle]
pub extern "C" fn nak_fill_qmd(
    dev: *const nv_device_info,
    info: *const nak_shader_info,
    qmd_info: *const nak_qmd_info,
    qmd_out: *mut ::std::os::raw::c_void,
    qmd_size: usize,
) {
    assert!(!dev.is_null());
    let dev = unsafe { &*dev };

    assert!(!info.is_null());
    let info = unsafe { &*info };

    assert!(!qmd_info.is_null());
    let qmd_info = unsafe { &*qmd_info };

    unsafe {
        if dev.cls_compute >= clcdc0::BLACKWELL_COMPUTE_A {
            let qmd_out = qmd_out as *mut Qmd5_0;
            assert!(qmd_size == size_of_val(&*qmd_out));
            qmd_out.write(fill_qmd(dev, info, qmd_info));
        } else if dev.cls_compute >= clcbc0::HOPPER_COMPUTE_A {
            let qmd_out = qmd_out as *mut Qmd4_0;
            assert!(qmd_size == size_of_val(&*qmd_out));
            qmd_out.write(fill_qmd(dev, info, qmd_info));
        } else if dev.cls_compute >= clc6c0::AMPERE_COMPUTE_A {
            let qmd_out = qmd_out as *mut Qmd3_0;
            assert!(qmd_size == size_of_val(&*qmd_out));
            qmd_out.write(fill_qmd(dev, info, qmd_info));
        } else if dev.cls_compute >= clc3c0::VOLTA_COMPUTE_A {
            let qmd_out = qmd_out as *mut Qmd2_2;
            assert!(qmd_size == size_of_val(&*qmd_out));
            qmd_out.write(fill_qmd(dev, info, qmd_info));
        } else if dev.cls_compute >= clc0c0::PASCAL_COMPUTE_A {
            let qmd_out = qmd_out as *mut Qmd2_1;
            assert!(qmd_size == size_of_val(&*qmd_out));
            qmd_out.write(fill_qmd(dev, info, qmd_info));
        } else if dev.cls_compute >= cla0c0::KEPLER_COMPUTE_A {
            let qmd_out = qmd_out as *mut Qmd0_6;
            assert!(qmd_size == size_of_val(&*qmd_out));
            qmd_out.write(fill_qmd(dev, info, qmd_info));
        } else {
            panic!("Unknown shader model");
        }
    }
}

#[no_mangle]
pub extern "C" fn nak_get_qmd_dispatch_size_layout(
    dev: &nv_device_info,
) -> nak_qmd_dispatch_size_layout {
    if dev.cls_compute >= clcdc0::BLACKWELL_COMPUTE_A {
        Qmd5_0::GLOBAL_SIZE_LAYOUT
    } else if dev.cls_compute >= clcbc0::HOPPER_COMPUTE_A {
        Qmd4_0::GLOBAL_SIZE_LAYOUT
    } else if dev.cls_compute >= clc6c0::AMPERE_COMPUTE_A {
        Qmd3_0::GLOBAL_SIZE_LAYOUT
    } else if dev.cls_compute >= clc3c0::VOLTA_COMPUTE_A {
        Qmd2_2::GLOBAL_SIZE_LAYOUT
    } else if dev.cls_compute >= clc0c0::PASCAL_COMPUTE_A {
        Qmd2_1::GLOBAL_SIZE_LAYOUT
    } else if dev.cls_compute >= cla0c0::KEPLER_COMPUTE_A {
        Qmd0_6::GLOBAL_SIZE_LAYOUT
    } else {
        panic!("Unsupported shader model");
    }
}

#[no_mangle]
pub extern "C" fn nak_get_qmd_cbuf_desc_layout(
    dev: &nv_device_info,
    idx: u8,
) -> nak_qmd_cbuf_desc_layout {
    if dev.cls_compute >= clcdc0::BLACKWELL_COMPUTE_A {
        Qmd5_0::cbuf_desc_layout(idx)
    } else if dev.cls_compute >= clcbc0::HOPPER_COMPUTE_A {
        Qmd4_0::cbuf_desc_layout(idx)
    } else if dev.cls_compute >= clc6c0::AMPERE_COMPUTE_A {
        Qmd3_0::cbuf_desc_layout(idx)
    } else if dev.cls_compute >= clc3c0::VOLTA_COMPUTE_A {
        Qmd2_2::cbuf_desc_layout(idx)
    } else if dev.cls_compute >= clc0c0::PASCAL_COMPUTE_A {
        Qmd2_1::cbuf_desc_layout(idx)
    } else if dev.cls_compute >= cla0c0::KEPLER_COMPUTE_A {
        Qmd0_6::cbuf_desc_layout(idx)
    } else {
        panic!("Unsupported shader model");
    }
}
