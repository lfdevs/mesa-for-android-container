// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;

fn try_fold_src(src: &mut Src, sc: &SmallConstant, sc_swz: Swizzle) -> bool {
    let Some(swz) = sc_swz.swizzle(src.swizzle) else {
        return false;
    };

    src.swizzle = swz;
    src.src_ref = SrcRef::FAU(FAURef::from(sc).into());
    true
}

fn try_lower_src(
    src: &mut Src,
    src_type: DataType,
    sc_table: &[SmallConstant],
) -> bool {
    let SrcRef::Imm32(imm32) = src.src_ref else {
        return false;
    };
    let imm32 = u32::from(imm32);

    let imm_bytes_read = src.swizzle.bytes_read(src_type.total_bytes());

    if imm_bytes_read.count_ones() == 1 {
        let imm_byte = imm_bytes_read.trailing_zeros();
        let imm8 = (imm32 >> (imm_byte * 8)) as u8;
        for sc in sc_table {
            for b in 0..4 {
                if imm8 == ((sc.imm32 >> (b * 8)) as u8)
                    && try_fold_src(src, sc, Swizzle::replicate_byte(b))
                {
                    return true;
                }
            }
        }
    } else if imm_bytes_read == 0b1100 || imm_bytes_read == 0b0011 {
        let imm_half = imm_bytes_read.trailing_zeros() / 2;
        let imm16 = (imm32 >> (imm_half * 16)) as u16;
        for sc in sc_table {
            for h in 0..2 {
                if imm16 == ((sc.imm32 >> (h * 16)) as u16)
                    && try_fold_src(src, sc, Swizzle::replicate_half(h))
                {
                    return true;
                }
            }
        }
    } else {
        for sc in sc_table {
            if imm32 == sc.imm32 && try_fold_src(src, sc, Swizzle::NONE) {
                return true;
            }
        }
    }
    false
}

impl Shader<'_> {
    pub fn lower_small_constants(&mut self) {
        let sc_table = self.model.small_constants();
        for b in self.blocks.iter_mut() {
            for i in b.instrs.iter_mut() {
                for (src, src_type) in i.srcs_types_mut() {
                    try_lower_src(src, src_type, sc_table);
                }
            }
        }
    }
}
