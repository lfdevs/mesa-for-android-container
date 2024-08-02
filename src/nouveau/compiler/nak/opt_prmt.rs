/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

use std::collections::HashMap;

use crate::ir::*;

struct PrmtSrcs {
    srcs: [SrcRef; 2],
    num_srcs: usize,
    imm_src: usize,
    num_imm_bytes: usize,
}

impl PrmtSrcs {
    fn new() -> PrmtSrcs {
        PrmtSrcs {
            srcs: [SrcRef::Zero; 2],
            num_srcs: 0,
            imm_src: usize::MAX,
            num_imm_bytes: 0,
        }
    }

    fn try_add_src(&mut self, src: SrcRef) -> Option<usize> {
        for i in 0..self.num_srcs {
            if self.srcs[i] == src {
                return Some(i);
            }
        }

        if self.num_srcs < 2 {
            let i = self.num_srcs;
            self.num_srcs += 1;
            self.srcs[i] = src;
            Some(i)
        } else {
            None
        }
    }

    fn try_add_imm_u8(&mut self, u: u8) -> Option<usize> {
        if self.imm_src == usize::MAX {
            if self.num_srcs >= 2 {
                return None;
            }
            self.imm_src = self.num_srcs;
            self.num_srcs += 1;
        }

        match &mut self.srcs[self.imm_src] {
            SrcRef::Zero => {
                if u == 0 {
                    // Common case, just leave it as a SrcRef::Zero
                    debug_assert!(self.num_imm_bytes <= 1);
                    self.num_imm_bytes = 1;
                    Some(0)
                } else {
                    let b = self.num_imm_bytes;
                    self.num_imm_bytes += 1;
                    let imm = u32::from(u) << (b * 8);
                    self.srcs[self.imm_src] = SrcRef::Imm32(imm);
                    Some(b)
                }
            }
            SrcRef::Imm32(imm) => {
                let b = self.num_imm_bytes;
                self.num_imm_bytes += 1;
                *imm |= u32::from(u) << (b * 8);
                Some(b)
            }
            _ => panic!("We said this was the imm src"),
        }
    }
}

struct PrmtEntry {
    sel: PrmtSel,
    srcs: [SrcRef; 2],
}

struct PrmtPass {
    ssa_prmt: HashMap<SSAValue, PrmtEntry>,
}

impl PrmtPass {
    fn new() -> PrmtPass {
        PrmtPass {
            ssa_prmt: HashMap::new(),
        }
    }

    fn add_prmt(&mut self, op: &OpPrmt) {
        let Dst::SSA(dst_ssa) = op.dst else {
            return;
        };
        debug_assert!(dst_ssa.comps() == 1);
        let dst_ssa = dst_ssa[0];

        let Some(sel) = op.get_sel() else {
            return;
        };

        debug_assert!(op.srcs[0].src_mod.is_none());
        debug_assert!(op.srcs[1].src_mod.is_none());
        let srcs = [op.srcs[0].src_ref, op.srcs[1].src_ref];

        self.ssa_prmt.insert(dst_ssa, PrmtEntry { sel, srcs });
    }

    fn get_prmt(&self, ssa: &SSAValue) -> Option<&PrmtEntry> {
        self.ssa_prmt.get(ssa)
    }

    fn get_prmt_for_src(&self, src: &Src) -> Option<&PrmtEntry> {
        debug_assert!(src.src_mod.is_none());
        if let SrcRef::SSA(vec) = &src.src_ref {
            debug_assert!(vec.comps() == 1);
            self.get_prmt(&vec[0])
        } else {
            None
        }
    }

    /// Try to optimize for the OpPrmt of OpPrmt case where only one source of
    /// the inner OpPrmt is used
    fn try_opt_prmt_src(&mut self, op: &mut OpPrmt, src_idx: usize) -> bool {
        let Some(op_sel) = op.get_sel() else {
            return false;
        };

        let Some(src_prmt) = self.get_prmt_for_src(&op.srcs[src_idx]) else {
            return false;
        };

        let mut new_sel = [PrmtSelByte::INVALID; 4];
        let mut src_prmt_src = usize::MAX;
        for i in 0..4 {
            let op_sel_byte = op_sel.get(i);
            if op_sel_byte.src() != src_idx {
                new_sel[i] = op_sel_byte;
                continue;
            }

            let src_sel_byte = src_prmt.sel.get(op_sel_byte.byte());

            if src_prmt_src != usize::MAX && src_prmt_src != src_sel_byte.src()
            {
                return false;
            }
            src_prmt_src = src_sel_byte.src();

            new_sel[i] = PrmtSelByte::new(
                src_idx,
                src_sel_byte.byte(),
                op_sel_byte.msb() | src_sel_byte.msb(),
            );
        }

        let new_sel = PrmtSel::new(new_sel);

        op.sel = new_sel.into();
        if src_prmt_src == usize::MAX {
            // This source is unused
            op.srcs[src_idx] = 0.into();
        } else {
            op.srcs[src_idx] = src_prmt.srcs[src_prmt_src].into();
        }
        true
    }

    /// Try to optimize for the OpPrmt of OpPrmt case as if we're considering a
    /// full 4-way OpPrmt in which some sources may be duplicates
    fn try_opt_prmt4(&mut self, op: &mut OpPrmt) -> bool {
        let Some(op_sel) = op.get_sel() else {
            return false;
        };

        let mut srcs = PrmtSrcs::new();
        let mut new_sel = [PrmtSelByte::INVALID; 4];
        for i in 0..4 {
            let op_sel_byte = op_sel.get(i);
            let src = &op.srcs[op_sel_byte.src()];

            if let Some(src_prmt) = self.get_prmt_for_src(src) {
                let src_sel_byte = src_prmt.sel.get(op_sel_byte.byte());
                let src_prmt_src = &src_prmt.srcs[src_sel_byte.src()];
                if let Some(u) = src_prmt_src.as_u32() {
                    let mut imm_u8 = src_sel_byte.fold_u32(u);
                    if op_sel_byte.msb() {
                        imm_u8 = ((imm_u8 as i8) >> 7) as u8;
                    }

                    let Some(byte_idx) = srcs.try_add_imm_u8(imm_u8) else {
                        return false;
                    };

                    new_sel[i] =
                        PrmtSelByte::new(srcs.imm_src, byte_idx, false);
                } else {
                    let Some(src_idx) = srcs.try_add_src(*src_prmt_src) else {
                        return false;
                    };

                    new_sel[i] = PrmtSelByte::new(
                        src_idx,
                        src_sel_byte.byte(),
                        op_sel_byte.msb() | src_sel_byte.msb(),
                    );
                }
            } else if let Some(u) = src.as_u32() {
                let imm_u8 = op_sel_byte.fold_u32(u);
                let Some(byte_idx) = srcs.try_add_imm_u8(imm_u8) else {
                    return false;
                };

                new_sel[i] = PrmtSelByte::new(srcs.imm_src, byte_idx, false);
            } else {
                debug_assert!(src.src_mod.is_none());
                let Some(src_idx) = srcs.try_add_src(src.src_ref) else {
                    return false;
                };

                new_sel[i] = PrmtSelByte::new(
                    src_idx,
                    op_sel_byte.byte(),
                    op_sel_byte.msb(),
                );
            }
        }

        let new_sel = PrmtSel::new(new_sel);
        if new_sel == op_sel
            && srcs.srcs[0] == op.srcs[0].src_ref
            && srcs.srcs[1] == op.srcs[1].src_ref
        {
            return false;
        }

        op.sel = new_sel.into();
        op.srcs[0] = srcs.srcs[0].into();
        op.srcs[1] = srcs.srcs[1].into();
        true
    }

    fn opt_prmt(&mut self, op: &mut OpPrmt) {
        for i in 0..2 {
            loop {
                if !self.try_opt_prmt_src(op, i) {
                    break;
                }
            }
        }

        loop {
            if !self.try_opt_prmt4(op) {
                break;
            }
        }

        self.add_prmt(op);
    }

    fn run(&mut self, f: &mut Function) {
        for b in &mut f.blocks {
            for instr in &mut b.instrs {
                if let Op::Prmt(op) = &mut instr.op {
                    self.opt_prmt(op);
                }
            }
        }
    }
}

impl Shader<'_> {
    pub fn opt_prmt(&mut self) {
        for f in &mut self.functions {
            PrmtPass::new().run(f);
        }
    }
}
