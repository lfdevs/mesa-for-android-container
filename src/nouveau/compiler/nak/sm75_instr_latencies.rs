// Copyright © 2025 Red Hat.
// SPDX-License-Identifier: MIT
#![allow(non_camel_case_types)]

use crate::ir::*;

// This contains the register scheduling information provided by NVIDIA.  This
// file is for Turing only.

// Coupled instructions are ones with fixed latencies, they need delays but not
// scoreboards.  Decoupled instructions are ones with variable latencies, need
// scoreboards but not delays.  There are also redirected instructions which
// depending on the SM, can be coupled or decoupled so both delays and
// scoreboards needs to be provided.

#[allow(dead_code)]
#[derive(Debug)]
enum RegLatencySM75 {
    CoupledDisp64,
    CoupledDisp,
    CoupledAlu,
    CoupledFMA,
    IMADLo,
    IMADWideAB, // readers only
    IMADWideLower,
    IMADWideUpper,
    RedirectedFP64,
    RedirectedFP16,
    RedirectedHMMA_884_F16(usize),
    RedirectedHMMA_884_F32(usize),
    RedirectedHMMA_1688,
    RedirectedHMMA_16816,
    IMMA(usize),
    Decoupled,
    DecoupledOther, //reads only
    BMov,
    GuardPredicate,
}

pub const fn pred(has_pred: bool, a: u32, b: u32) -> u32 {
    if has_pred {
        a + b
    } else {
        b
    }
}

impl RegLatencySM75 {
    fn op_category(op: &Op, reader: bool, op_reg_idx: usize) -> RegLatencySM75 {
        use RegLatencySM75::*;
        match op {
            // this will need updating if imad grows support for input predicates
            Op::IMad(_) | Op::IMul(_) => IMADLo,
            Op::IMad64(_) => {
                if reader {
                    match op_reg_idx {
                        0 | 1 => IMADWideAB,
                        2 => IMADWideLower, // vs upper C operand - work it out
                        _ => {
                            panic!("Illegal field in imadwide")
                        }
                    }
                } else {
                    IMADWideUpper // as above this needs more work
                }
            }

            Op::PopC(_) => Decoupled,
            Op::IAdd3(_) | Op::IAdd3X(_) => CoupledAlu,

            Op::BMsk(_) => CoupledAlu,
            Op::Sgxt(_) => CoupledAlu,
            Op::Lop3(_) => CoupledAlu,
            Op::Flo(_) => Decoupled,
            Op::ISetP(_) => CoupledAlu,
            Op::IAbs(_) => CoupledAlu,
            Op::Lea(_) => CoupledAlu,
            Op::LeaX(_) => CoupledAlu,
            Op::IMnMx(_) => CoupledAlu,
            Op::I2I(_) => CoupledAlu,
            // I2IP => CoupledAlu
            Op::Shf(_) => CoupledAlu,

            Op::FFma(_) => CoupledFMA,
            Op::FAdd(_) => CoupledFMA,
            Op::FMul(_) => CoupledFMA,
            Op::FMnMx(_) => CoupledAlu,
            Op::FSwzAdd(_) => CoupledFMA,
            Op::FSet(_) => CoupledAlu,
            // FSel => CoupledAlu,
            Op::FSetP(_) => CoupledAlu,
            // FChk => Decoupled,
            Op::DAdd(_) | Op::DFma(_) | Op::DMul(_) | Op::DSetP(_) => {
                RedirectedFP64
            }

            Op::DMnMx(_) => RedirectedFP64, // not in docs

            Op::HAdd2(_)
            | Op::HFma2(_)
            | Op::HMul2(_)
            | Op::HSet2(_)
            | Op::HSetP2(_) => RedirectedFP16,

            Op::HMnMx2(_) => RedirectedFP16, // not in docs
            // let in for documentation purposes
            Op::Hmma(h) => match (h.mat_size, h.dst_type) {
                (HmmaSize::M16N8K8, _) => RedirectedHMMA_1688,
                (HmmaSize::M16N8K16, _) => RedirectedHMMA_16816,
                _ => panic!("Illegal HMMA in reg category {}", h),
            },
            Op::Ipa(_) => Decoupled,
            Op::MuFu(_) => Decoupled,

            // Conversion functions all decoupled
            Op::F2F(_) => Decoupled,
            Op::F2FP(_) => CoupledAlu, // undocumented, copy Ampere
            Op::F2I(_) => Decoupled,
            Op::I2F(_) => Decoupled,
            Op::FRnd(_) => Decoupled,
            Op::AL2P(_) => Decoupled,

            Op::Mov(_) => CoupledAlu,
            Op::Movm(_) => Decoupled,
            Op::Sel(_) => CoupledAlu,
            Op::BRev(_) => Decoupled,
            // P2R => CoupledAlu,
            // R2P => CoupledAlu,
            Op::PLop3(_) => CoupledAlu,
            Op::Prmt(_) => CoupledAlu,
            Op::Nop(_) => CoupledDisp,
            Op::Vote(_) => CoupledDisp,
            Op::Match(_) => Decoupled,
            Op::S2R(_) => Decoupled,
            // S2UR  => Decoupled,
            Op::R2UR(_) => {
                if reader {
                    Decoupled
                } else {
                    panic!("Illegal R2UR");
                }
            }
            Op::CS2R(cs2r) => {
                if cs2r.dst.as_reg().unwrap().comps() == 2 {
                    CoupledDisp64
                } else {
                    CoupledAlu
                }
            }
            // B2R => Decoupled,
            // LEPC => CoupledDisp64
            Op::BMov(bmov) => match bmov.dst {
                Dst::Reg(reg) => {
                    if reg.is_gpr() {
                        BMov
                    } else {
                        Decoupled
                    }
                }
                _ => Decoupled,
            },
            // RPCMOV.32 => CoupledAlu,
            // RPCMOV.64 => CoupledDisp64
            // PMTRIG => CoupledDisp64
            // CSMTEST =>  CoupledAlu,
            Op::Bar(_) => Decoupled,
            Op::Imma(_) => IMMA(op_reg_idx),
            Op::IDp4(_) => CoupledFMA,
            Op::BClear(_) => Decoupled,
            Op::Bra(_) => Decoupled,
            Op::BSSy(_) => Decoupled,
            Op::Kill(_) => Decoupled,
            Op::Exit(_) => Decoupled,
            Op::BSync(_) => Decoupled,
            Op::Tex(_) => Decoupled,
            Op::Tld(_) => Decoupled,
            Op::Tld4(_) => Decoupled,
            Op::Tmml(_) => Decoupled,
            Op::Txd(_) => Decoupled,
            Op::Txq(_) => Decoupled,
            Op::Ldc(_) => Decoupled,
            Op::ALd(_) => Decoupled,
            Op::ASt(_) => Decoupled,
            Op::Out(_) => Decoupled,
            Op::OutFinal(_) => Decoupled,
            Op::Ld(_) => Decoupled,
            Op::St(_) => Decoupled,
            Op::Atom(_) => Decoupled,
            //CCtl.i,c are coupled
            Op::CCtl(_) => DecoupledOther,
            Op::MemBar(_) => Decoupled,
            Op::SuLd(_) => Decoupled,
            Op::SuSt(_) => Decoupled,
            Op::SuAtom(_) => Decoupled,
            Op::PixLd(_) => Decoupled,
            Op::Isberd(_) => Decoupled,
            Op::LdTram(_) => Decoupled,
            Op::Shfl(_) => Decoupled,
            Op::Ldsm(_) => Decoupled,
            x => {
                panic!("Illegal instuction in reg category {}", x);
            }
        }
    }

    pub fn read_after_write(
        writer: RegLatencySM75,
        reader: RegLatencySM75,
    ) -> u32 {
        use RegLatencySM75::*;
        match writer {
            IMADWideAB | DecoupledOther => {
                panic!("Illegal IMADWideAB for writer");
            }
            _ => {}
        }

        match reader {
            CoupledDisp64 | CoupledDisp | CoupledAlu => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 4,
                CoupledFMA | IMADLo => 5,
                IMADWideLower => 3,
                IMADWideUpper => 5,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            CoupledFMA | IMADLo => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 5,
                CoupledFMA | IMADLo => 4,
                IMADWideLower => 2,
                IMADWideUpper => 4,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            IMADWideAB => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 5,
                CoupledFMA | IMADLo => 4,
                IMADWideLower => 4,
                IMADWideUpper => 6,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            IMADWideLower | IMADWideUpper => match reader {
                IMADWideLower => match writer {
                    CoupledDisp64 => 6,
                    CoupledAlu | CoupledDisp => 5,
                    CoupledFMA | IMADLo => 4,
                    IMADWideLower => 2,
                    IMADWideUpper => 2,
                    RedirectedFP64 => 9,
                    RedirectedFP16 => 8,
                    RedirectedHMMA_884_F16(_) => 13,
                    RedirectedHMMA_884_F32(_) => 10,
                    RedirectedHMMA_1688 => 14,
                    RedirectedHMMA_16816 => 22,
                    IMMA(_) => 10,
                    _ => 1,
                },
                IMADWideUpper => match writer {
                    CoupledDisp64 => 4,
                    CoupledAlu | CoupledDisp => 3,
                    CoupledFMA | IMADLo => 2,
                    IMADWideLower => 2,
                    IMADWideUpper => 2,
                    RedirectedFP64 => 7,
                    RedirectedFP16 => 6,
                    RedirectedHMMA_884_F16(_) => 11,
                    RedirectedHMMA_884_F32(_) => 8,
                    RedirectedHMMA_1688 => 12,
                    RedirectedHMMA_16816 => 20,
                    IMMA(_) => 8,
                    _ => 1,
                },
                _ => {
                    panic!("Illegal IMAD field");
                }
            },
            RedirectedFP64 => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 6,
                CoupledFMA | IMADLo => 6,
                IMADWideLower => 6,
                IMADWideUpper => 6,
                RedirectedFP64 => 8,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            RedirectedFP16 => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 6,
                CoupledFMA | IMADLo => 6,
                IMADWideLower => 6,
                IMADWideUpper => 6,
                RedirectedFP64 => 9,
                RedirectedFP16 => 6,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            RedirectedHMMA_884_F16(read_idx) => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 6,
                CoupledFMA | IMADLo => 6,
                IMADWideLower => 6,
                IMADWideUpper => 6,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) if read_idx == 2 => 4,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            RedirectedHMMA_884_F32(read_idx) => match writer {
                CoupledDisp64 => 6,
                CoupledAlu | CoupledDisp => 6,
                CoupledFMA | IMADLo => 6,
                IMADWideLower => 6,
                IMADWideUpper => 6,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) if read_idx == 2 => 4,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            RedirectedHMMA_1688 | RedirectedHMMA_16816 | Decoupled => {
                match writer {
                    CoupledDisp64 => 6,
                    CoupledAlu | CoupledDisp => 6,
                    CoupledFMA | IMADLo => 6,
                    IMADWideLower => 6,
                    IMADWideUpper => 6,
                    RedirectedFP64 => 9,
                    RedirectedFP16 => 8,
                    RedirectedHMMA_884_F16(_) => 13,
                    RedirectedHMMA_884_F32(_) => 10,
                    RedirectedHMMA_1688 => 14,
                    RedirectedHMMA_16816 => 22,
                    IMMA(_) => 10,
                    _ => 1,
                }
            }
            IMMA(read_idx) => match writer {
                CoupledDisp64 => 8,
                CoupledAlu | CoupledDisp => 8,
                CoupledFMA | IMADLo => 8,
                IMADWideLower => 8,
                IMADWideUpper => 8,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) if read_idx == 2 => 4,
                IMMA(_) => 10,
                _ => 1,
            },
            DecoupledOther => match writer {
                CoupledDisp64 => 8,
                CoupledAlu | CoupledDisp => 8,
                CoupledFMA | IMADLo => 8,
                IMADWideLower => 8,
                IMADWideUpper => 8,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                RedirectedHMMA_884_F16(_) => 13,
                RedirectedHMMA_884_F32(_) => 10,
                RedirectedHMMA_1688 => 14,
                RedirectedHMMA_16816 => 22,
                IMMA(_) => 10,
                _ => 1,
            },
            BMov | GuardPredicate => {
                panic!("Not a RAW category")
            }
        }
    }

    fn write_after_write(
        writer1: RegLatencySM75,
        writer2: RegLatencySM75,
        has_pred: bool,
    ) -> u32 {
        use RegLatencySM75::*;
        match writer1 {
            IMADWideAB | DecoupledOther => {
                panic!("Illegal reg latency for writer");
            }
            _ => {}
        }
        match writer2 {
            CoupledDisp64 => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper => 1,
                RedirectedFP64 => 4,
                RedirectedFP16 => 3,
                RedirectedHMMA_884_F16(_) => 8,
                RedirectedHMMA_884_F32(_) => pred(has_pred, 2, 2),
                RedirectedHMMA_1688 => 9,
                RedirectedHMMA_16816 => 17,
                IMMA(_) => 5,
                _ => 1,
            },
            CoupledDisp | CoupledAlu => match writer1 {
                CoupledDisp64 => 2,
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideLower | IMADWideUpper => 1,
                RedirectedFP64 => pred(has_pred, 4, 1),
                RedirectedFP16 => pred(has_pred, 3, 1),
                RedirectedHMMA_884_F16(_) => pred(has_pred, 8, 1),
                RedirectedHMMA_884_F32(_) => pred(has_pred, 5, 1),
                RedirectedHMMA_1688 => pred(has_pred, 9, 1),
                RedirectedHMMA_16816 => pred(has_pred, 17, 1),
                IMMA(_) => pred(has_pred, 5, 1),
                _ => 1,
            },
            CoupledFMA | IMADLo => match writer1 {
                CoupledDisp64 => 2,
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideLower => 1,
                IMADWideUpper => pred(has_pred, 1, 1),
                RedirectedFP64 => pred(has_pred, 4, 1),
                RedirectedFP16 => pred(has_pred, 3, 1),
                RedirectedHMMA_884_F16(_) => pred(has_pred, 8, 1),
                RedirectedHMMA_884_F32(_) => pred(has_pred, 5, 1),
                RedirectedHMMA_1688 => pred(has_pred, 9, 1),
                RedirectedHMMA_16816 => pred(has_pred, 17, 1),
                IMMA(_) => pred(has_pred, 5, 1),
                _ => 1,
            },
            IMADWideLower => match writer1 {
                CoupledDisp64 => pred(has_pred, 2, 2),
                CoupledDisp | CoupledAlu => pred(has_pred, 2, 1),
                CoupledFMA | IMADLo => pred(has_pred, 1, 1),
                IMADWideLower => 1,
                IMADWideUpper => 1,
                RedirectedFP64 => pred(has_pred, 4, 3),
                RedirectedFP16 => pred(has_pred, 3, 3),
                RedirectedHMMA_884_F16(_) => pred(has_pred, 8, 3),
                RedirectedHMMA_884_F32(_) => pred(has_pred, 5, 3),
                RedirectedHMMA_1688 => pred(has_pred, 9, 3),
                RedirectedHMMA_16816 => pred(has_pred, 17, 3),
                IMMA(_) => pred(has_pred, 5, 3),
                _ => 1,
            },
            IMADWideUpper => match writer1 {
                CoupledDisp64 => pred(has_pred, 1, 1),
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideLower | IMADWideUpper => 1,
                RedirectedFP64 => pred(has_pred, 4, 1),
                RedirectedFP16 => pred(has_pred, 3, 1),
                RedirectedHMMA_884_F16(_) => pred(has_pred, 8, 1),
                RedirectedHMMA_884_F32(_) => pred(has_pred, 5, 1),
                RedirectedHMMA_1688 => pred(has_pred, 9, 1),
                RedirectedHMMA_16816 => pred(has_pred, 17, 1),
                IMMA(_) => pred(has_pred, 5, 1),
                _ => 1,
            },
            RedirectedFP64 => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper => 2,
                RedirectedFP64 => 1,
                RedirectedFP16 => 2,
                RedirectedHMMA_884_F16(_) => 5,
                RedirectedHMMA_884_F32(_) => 2,
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                IMMA(_) => 2,
                _ => 1,
            },
            RedirectedFP16 => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper => 2,
                RedirectedFP64 => pred(has_pred, 1, 1),
                RedirectedFP16 => 1,
                RedirectedHMMA_884_F16(_) => pred(has_pred, 6, 1),
                RedirectedHMMA_884_F32(_) => pred(has_pred, 3, 1),
                RedirectedHMMA_1688 => pred(has_pred, 7, 1),
                RedirectedHMMA_16816 => pred(has_pred, 15, 1),
                IMMA(_) => pred(has_pred, 3, 1),
                _ => 1,
            },
            RedirectedHMMA_884_F16(_) => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper => 2,
                RedirectedFP64 => pred(has_pred, 3, 2),
                RedirectedFP16 => pred(has_pred, 2, 2),
                RedirectedHMMA_884_F16(_) => 1,
                RedirectedHMMA_884_F32(_) => pred(has_pred, 2, 4),
                RedirectedHMMA_1688 => pred(has_pred, 6, 4),
                RedirectedHMMA_16816 => pred(has_pred, 16, 2),
                IMMA(_) => pred(has_pred, 2, 4),
                _ => 1,
            },
            RedirectedHMMA_884_F32(_) => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper => 2,
                RedirectedFP64 => pred(has_pred, 3, 2),
                RedirectedFP16 => pred(has_pred, 2, 2),
                RedirectedHMMA_884_F16(_) => pred(has_pred, 4, 5),
                RedirectedHMMA_884_F32(_) => 1,
                RedirectedHMMA_1688 => pred(has_pred, 6, 4),
                RedirectedHMMA_16816 => pred(has_pred, 16, 2),
                IMMA(_) => pred(has_pred, 2, 4),
                _ => 1,
            },
            RedirectedHMMA_1688 => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper | RedirectedFP64
                | RedirectedFP16 => 2,
                RedirectedHMMA_884_F16(_) => 4,
                RedirectedHMMA_884_F32(_) => 2,
                RedirectedHMMA_1688 => 1,
                RedirectedHMMA_16816 => 16,
                IMMA(_) => 2,
                _ => 1,
            },
            RedirectedHMMA_16816 => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper | RedirectedFP64
                | RedirectedFP16 => 2,
                RedirectedHMMA_884_F16(_) => 4,
                RedirectedHMMA_884_F32(_) => 2,
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 1,
                IMMA(_) => 2,
                _ => 1,
            },
            IMMA(_) => match writer1 {
                CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA
                | IMADLo | IMADWideLower | IMADWideUpper => {
                    pred(has_pred, 2, 2)
                }
                RedirectedFP64 => pred(has_pred, 2, 3),
                RedirectedFP16 => pred(has_pred, 2, 2),
                RedirectedHMMA_884_F16(_) => pred(has_pred, 2, 7),
                RedirectedHMMA_884_F32(_) => pred(has_pred, 2, 4),
                RedirectedHMMA_1688 => pred(has_pred, 6, 4),
                RedirectedHMMA_16816 => pred(has_pred, 14, 4),
                IMMA(_) => 1,
                _ => 1,
            },
            Decoupled => match writer1 {
                CoupledDisp64
                | CoupledDisp
                | CoupledAlu
                | CoupledFMA
                | IMADLo
                | IMADWideLower
                | IMADWideUpper
                | RedirectedFP64
                | RedirectedFP16
                | RedirectedHMMA_884_F16(_)
                | RedirectedHMMA_884_F32(_)
                | RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                IMMA(_) => 2,
                _ => 1,
            },
            BMov => {
                // BMOV Writing to RF?
                match writer1 {
                    CoupledDisp64
                    | CoupledDisp
                    | CoupledAlu
                    | CoupledFMA
                    | IMADLo
                    | IMADWideLower
                    | IMADWideUpper
                    | RedirectedFP64
                    | RedirectedFP16
                    | RedirectedHMMA_884_F16(_)
                    | RedirectedHMMA_884_F32(_)
                    | RedirectedHMMA_1688 => 9,
                    RedirectedHMMA_16816 => 14,
                    IMMA(_) => 9,
                    _ => 1,
                }
            }
            IMADWideAB | DecoupledOther | GuardPredicate => {
                panic!("Not a WAW category")
            }
        }
    }

    fn write_after_read(reader: RegLatencySM75, writer: RegLatencySM75) -> u32 {
        use RegLatencySM75::*;
        match writer {
            CoupledDisp64 | CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
            | IMADWideLower | IMADWideUpper => match reader {
                RedirectedHMMA_1688 => 5,
                RedirectedHMMA_16816 => 13,
                _ => 1,
            },
            RedirectedFP64 => match reader {
                RedirectedFP64 => 1,
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 2,
            },
            RedirectedFP16 => match reader {
                RedirectedFP16 => 1,
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 2,
            },
            RedirectedHMMA_884_F16(_) => match reader {
                RedirectedHMMA_884_F16(_) => 1,
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 2,
            },
            RedirectedHMMA_884_F32(_) => match reader {
                RedirectedHMMA_884_F32(_) => 1,
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 2,
            },
            RedirectedHMMA_1688 => match reader {
                RedirectedHMMA_1688 => 1,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 2,
            },
            RedirectedHMMA_16816 => match reader {
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 1,
                Decoupled => 1,
                _ => 2,
            },
            IMMA(_) => match reader {
                RedirectedHMMA_1688 => 6,
                RedirectedHMMA_16816 => 14,
                IMMA(_) => 1,
                Decoupled => 1,
                _ => 2,
            },
            Decoupled => match reader {
                RedirectedHMMA_1688 => 2,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 2,
            },
            BMov => match reader {
                RedirectedHMMA_1688 => 9,
                RedirectedHMMA_16816 => 14,
                Decoupled => 1,
                _ => 9,
            },
            IMADWideAB | DecoupledOther | GuardPredicate => {
                panic!("Illegal in WAR");
            }
        }
    }

    fn pred_read_after_write(
        writer: RegLatencySM75,
        reader: RegLatencySM75,
    ) -> u32 {
        use RegLatencySM75::*;
        match reader {
            CoupledDisp => match writer {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => 12,
                RedirectedFP64 => 15,
                RedirectedFP16 => 14,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            CoupledAlu => match writer {
                CoupledDisp | CoupledAlu => 4,
                CoupledFMA | IMADLo | IMADWideUpper | IMADWideLower => 5,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            CoupledFMA | IMADLo => match writer {
                CoupledDisp | CoupledAlu => 5,
                CoupledFMA | IMADLo | IMADWideUpper | IMADWideLower => 4,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            IMADWideUpper | IMADWideLower => match writer {
                CoupledDisp | CoupledAlu => 5,
                CoupledFMA | IMADLo => 4,
                IMADWideUpper | IMADWideLower => 2,
                RedirectedFP64 => 9,
                RedirectedFP16 => 8,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            RedirectedFP64 => match writer {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => 12,
                RedirectedFP64 => 8,
                RedirectedFP16 => 14,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            RedirectedFP16 => match writer {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => 12,
                RedirectedFP64 => 15,
                RedirectedFP16 => 6,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            Decoupled | GuardPredicate => match writer {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => 12,
                RedirectedFP64 => 15,
                RedirectedFP16 => 14,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            _ => {
                panic!("Illegal reader in reg predicate");
            }
        }
    }

    fn pred_write_after_write(
        writer1: RegLatencySM75,
        writer2: RegLatencySM75,
        has_pred: bool,
    ) -> u32 {
        use RegLatencySM75::*;
        match writer2 {
            CoupledDisp | CoupledAlu | CoupledFMA | IMADLo => match writer1 {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => 1,
                RedirectedFP64 => pred(has_pred, 4, 1),
                RedirectedFP16 => pred(has_pred, 3, 1),
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            IMADWideUpper | IMADWideLower => match writer1 {
                CoupledDisp | CoupledAlu => pred(has_pred, 1, 2),
                CoupledFMA | IMADLo => pred(has_pred, 1, 1),
                IMADWideUpper | IMADWideLower => 1,
                RedirectedFP64 => pred(has_pred, 4, 3),
                RedirectedFP16 => pred(has_pred, 3, 3),
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            RedirectedFP64 => match writer1 {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => pred(has_pred, 2, 2),
                RedirectedFP64 => 1,
                RedirectedFP16 => pred(has_pred, 2, 4),
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            RedirectedFP16 => match writer1 {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower => pred(has_pred, 2, 4),
                RedirectedFP64 => pred(has_pred, 2, 7),
                RedirectedFP16 => 1,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            Decoupled => match writer1 {
                CoupledDisp | CoupledAlu | CoupledFMA | IMADLo
                | IMADWideUpper | IMADWideLower | RedirectedFP64
                | RedirectedFP16 => 2,
                Decoupled => 1,
                _ => {
                    panic!("Illegal RAW in Predicate");
                }
            },
            _ => {
                panic!("Illegal WAR category in Predicates");
            }
        }
    }

    fn pred_write_after_read(
        reader: RegLatencySM75,
        writer: RegLatencySM75,
    ) -> u32 {
        use RegLatencySM75::*;
        match writer {
            CoupledDisp | CoupledAlu | CoupledFMA | IMADLo | IMADWideUpper
            | IMADWideLower => 1,
            RedirectedFP64 => match reader {
                CoupledAlu | CoupledFMA | IMADLo | IMADWideUpper
                | IMADWideLower | RedirectedFP16 => 2,
                _ => 1,
            },
            RedirectedFP16 => match reader {
                CoupledAlu | CoupledFMA | IMADLo | IMADWideUpper
                | IMADWideLower | RedirectedFP64 => 2,
                _ => 1,
            },
            Decoupled => match reader {
                CoupledAlu | CoupledFMA | IMADLo | IMADWideUpper
                | IMADWideLower | RedirectedFP16 | RedirectedFP64 => 2,
                _ => 1,
            },
            _ => {
                panic!("Illegal WAR category in Predicates");
            }
        }
    }
}

#[allow(non_camel_case_types)]
#[allow(dead_code)]
#[derive(Debug)]
enum URegLatencySM75 {
    Udp,
    VectorCoupled,
    VectorDecoupled,
    Uldc,
    Umov,
    VectorCoupledBindless,
    VectorDecoupledBindless,
    VoteU,
    GuardPredicate,
    R2UR,
}

impl URegLatencySM75 {
    fn op_category(
        op: &Op,
        reader: bool,
        op_reg_idx: usize,
    ) -> URegLatencySM75 {
        use URegLatencySM75::*;
        // is this using a bindless cbuf as a src register.
        // this decides between the category types for readers.
        let bindless =
            reader && op.srcs_as_slice()[op_reg_idx].is_bindless_cbuf();

        let vcoupled = if bindless {
            VectorCoupledBindless
        } else {
            VectorCoupled
        };
        let vdecoupled = if bindless {
            VectorDecoupledBindless
        } else {
            VectorDecoupled
        };

        // if this is a reader from a ureg, it could be a U* instruction or a regular instruction.
        let uniform_op = op.is_uniform();

        let vcoupled = if uniform_op { Udp } else { vcoupled };
        let vdecoupled = if uniform_op { Udp } else { vdecoupled };

        match op {
            Op::BMsk(_) => vcoupled,
            Op::BRev(_) => vcoupled,
            // uclea?
            Op::Flo(_) => vdecoupled,
            Op::IAdd3(_) | Op::IAdd3X(_) => vcoupled,
            Op::IAbs(_) => vcoupled,
            Op::IDp4(_) => vcoupled,
            Op::IMnMx(_) => vcoupled,
            Op::IMad(_) => vcoupled,

            Op::IMad64(_) => vcoupled,
            Op::ISetP(_) => vcoupled,
            Op::Ldc(_) => {
                if uniform_op {
                    Uldc
                } else {
                    vdecoupled
                }
            }
            Op::Lea(_) => vcoupled,
            Op::LeaX(_) => vcoupled,
            Op::Lop2(_) | Op::Lop3(_) => vcoupled,

            Op::MuFu(_) => vdecoupled,
            Op::Mov(_) => {
                if uniform_op {
                    Umov
                } else {
                    vcoupled
                }
            }

            // mov32i => URegLatency::Uldc,
            // p2ur => Udp,
            Op::PLop3(_) => vcoupled,
            Op::PopC(_) => vdecoupled,
            Op::Prmt(_) => vcoupled,
            Op::PSetP(_) => vcoupled,
            // UR2UP
            Op::Sel(_) => vcoupled,
            Op::Sgxt(_) => vcoupled,
            Op::Shf(_) => vcoupled,
            Op::Shfl(_) => vdecoupled,

            Op::I2F(_) => vdecoupled,
            Op::F2I(_) => vdecoupled,
            Op::F2F(_) => vdecoupled,
            Op::R2UR(_) => {
                if !reader {
                    R2UR
                } else {
                    panic!("Illegal R2UR in ureg");
                }
            }
            Op::S2R(_) => {
                if !reader {
                    R2UR
                } else {
                    panic!("Illegal S2UR in ureg");
                }
            }
            Op::Vote(_) => VoteU,

            Op::FRnd(_) => vdecoupled,
            Op::FAdd(_)
            | Op::FMul(_)
            | Op::FFma(_)
            | Op::FSet(_)
            | Op::FSetP(_)
            | Op::FMnMx(_)
            | Op::HAdd2(_)
            | Op::HMul2(_)
            | Op::HSet2(_)
            | Op::HFma2(_)
            | Op::HSetP2(_) => vcoupled,
            Op::DMul(_) | Op::DFma(_) | Op::DAdd(_) | Op::DSetP(_) => {
                vdecoupled
            }
            _ => {
                panic!("Illegal instuction in ureg category {}", op);
            }
        }
    }

    fn read_after_write(
        writer: URegLatencySM75,
        reader: URegLatencySM75,
    ) -> u32 {
        use URegLatencySM75::*;
        match reader {
            Udp => match writer {
                Udp => 4,
                R2UR => 2,
                Uldc | VoteU | Umov => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            VectorCoupled => match writer {
                Udp => 6,
                R2UR => 2,
                Uldc | VoteU | Umov => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            VectorDecoupled => match writer {
                Udp => 9,
                R2UR => 2,
                Uldc | VoteU | Umov => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            Uldc | VectorCoupledBindless | VectorDecoupledBindless => {
                match writer {
                    Udp => 12,
                    R2UR => 2,
                    Uldc | VoteU | Umov => 5,
                    _ => {
                        panic!(
                            "Illegal writer in raw ureg latency {:?}",
                            writer
                        )
                    }
                }
            }
            Umov => match writer {
                Udp => 7,
                R2UR => 2,
                Uldc | VoteU | Umov => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency")
                }
            },
            _ => {
                panic!("Illegal read in ureg raw latency")
            }
        }
    }

    fn write_after_write(
        writer1: URegLatencySM75,
        writer2: URegLatencySM75,
        has_pred: bool,
    ) -> u32 {
        use URegLatencySM75::*;
        match writer2 {
            Udp => match writer1 {
                Udp => 1,
                R2UR => 2,
                Uldc | VoteU | Umov => 1,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            R2UR => match writer1 {
                Udp => pred(has_pred, 4, 6),
                R2UR => 2,
                Uldc | VoteU | Umov => 4,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            Uldc | VoteU | Umov => match writer1 {
                Udp => 7,
                R2UR => 2,
                Uldc | VoteU | Umov => 1,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            _ => {
                panic!("Illegal writer in ureg waw latency")
            }
        }
    }

    fn write_after_read(
        reader: URegLatencySM75,
        writer: URegLatencySM75,
    ) -> u32 {
        use URegLatencySM75::*;
        match writer {
            Udp => 1,
            R2UR => 1,
            Uldc | VoteU | Umov => match reader {
                Udp => 3,
                _ => 1,
            },
            _ => {
                panic!("Illegal writer in ureg war latency")
            }
        }
    }

    fn pred_read_after_write(
        writer: URegLatencySM75,
        reader: URegLatencySM75,
    ) -> u32 {
        use URegLatencySM75::*;
        match reader {
            Udp => match writer {
                Udp => 4,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer in upred raw latency")
                }
            },
            VectorCoupled => match writer {
                Udp => 6,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer in upred raw latency")
                }
            },
            GuardPredicate => match writer {
                Udp => 11,
                VoteU => 5,
                _ => {
                    panic!("Illegal writer in upred raw latency")
                }
            },
            _ => {
                panic!("Illegal reader in upred raw latency")
            }
        }
    }

    fn pred_write_after_write(
        writer1: URegLatencySM75,
        writer2: URegLatencySM75,
    ) -> u32 {
        use URegLatencySM75::*;
        match writer2 {
            Udp => 1,
            VoteU => match writer1 {
                Udp => 7,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer1 in upred raw latency")
                }
            },
            _ => {
                panic!("Illegal writer2 in upred raw latency")
            }
        }
    }

    fn pred_write_after_read(
        reader: URegLatencySM75,
        writer: URegLatencySM75,
    ) -> u32 {
        use URegLatencySM75::*;
        match writer {
            Udp => 1,
            VoteU => match reader {
                Udp => 2,
                _ => 1,
            },
            _ => {
                panic!("Illegal writer2 in upred raw latency")
            }
        }
    }
}

pub struct SM75Latency {}

impl SM75Latency {
    pub fn needs_scoreboards(op: &Op) -> bool {
        if op.is_uniform() {
            match URegLatencySM75::op_category(op, false, 0) {
                URegLatencySM75::R2UR => true,
                _ => false,
            }
        } else {
            match RegLatencySM75::op_category(op, false, 0) {
                RegLatencySM75::RedirectedFP64 |
                // We don't think fp16 needs scoreboarding on any known hw
                // Put this back if we figure out it does.
                //RegLatencySM75::RedirectedFP16 |
                RegLatencySM75::RedirectedHMMA_884_F16(_) |
                RegLatencySM75::RedirectedHMMA_884_F32(_) |
                RegLatencySM75::RedirectedHMMA_1688 |
                RegLatencySM75::RedirectedHMMA_16816 |
                RegLatencySM75::IMMA(_) |
                RegLatencySM75::Decoupled => true,
                _ => false
            }
        }
    }

    /// if read is None pick the worst case raw latency
    pub fn raw(
        write: &Op,
        dst_idx: usize,
        read: Option<&Op>,
        src_idx: usize,
    ) -> u32 {
        let dst_file = match &write.dsts_as_slice()[dst_idx] {
            Dst::None => return 0,
            Dst::SSA(vec) => vec.file(),
            Dst::Reg(reg) => reg.file(),
        };

        match dst_file {
            RegFile::GPR => {
                let write_latency =
                    RegLatencySM75::op_category(write, false, dst_idx);
                let read_latency = match read {
                    Some(op) => RegLatencySM75::op_category(op, true, src_idx),
                    None => RegLatencySM75::RedirectedFP64,
                };

                RegLatencySM75::read_after_write(write_latency, read_latency)
            }
            RegFile::UGPR => {
                let write_latency =
                    URegLatencySM75::op_category(write, false, dst_idx);
                let read_latency = match read {
                    Some(op) => URegLatencySM75::op_category(op, true, src_idx),
                    None => URegLatencySM75::Uldc,
                };

                URegLatencySM75::read_after_write(write_latency, read_latency)
            }
            RegFile::Pred => {
                let write_latency =
                    RegLatencySM75::op_category(write, false, dst_idx);
                let read_latency = match read {
                    Some(op) => RegLatencySM75::op_category(op, true, src_idx),
                    None => RegLatencySM75::GuardPredicate,
                };

                RegLatencySM75::pred_read_after_write(
                    write_latency,
                    read_latency,
                )
            }
            RegFile::UPred => {
                let write_latency =
                    URegLatencySM75::op_category(write, false, dst_idx);
                let read_latency = match read {
                    Some(op) => URegLatencySM75::op_category(op, true, src_idx),
                    None => URegLatencySM75::GuardPredicate,
                };

                URegLatencySM75::pred_read_after_write(
                    write_latency,
                    read_latency,
                )
            }
            RegFile::Bar => 0, // Barriers have a HW scoreboard
            _ => panic!("Not a register"),
        }
    }

    pub fn war(read: &Op, src_idx: usize, write: &Op, dst_idx: usize) -> u32 {
        let dst_file = match &write.dsts_as_slice()[dst_idx] {
            Dst::None => return 0,
            Dst::SSA(vec) => vec.file(),
            Dst::Reg(reg) => reg.file(),
        };

        match dst_file {
            RegFile::GPR => {
                let write_latency =
                    RegLatencySM75::op_category(write, false, dst_idx);
                let read_latency =
                    RegLatencySM75::op_category(read, true, src_idx);

                RegLatencySM75::write_after_read(read_latency, write_latency)
            }
            RegFile::UGPR => {
                let write_latency =
                    URegLatencySM75::op_category(write, false, dst_idx);
                let read_latency =
                    URegLatencySM75::op_category(read, true, src_idx);

                URegLatencySM75::write_after_read(read_latency, write_latency)
            }
            RegFile::Pred => {
                let write_latency =
                    RegLatencySM75::op_category(write, false, dst_idx);
                let read_latency =
                    RegLatencySM75::op_category(read, true, src_idx);

                RegLatencySM75::pred_write_after_read(
                    read_latency,
                    write_latency,
                )
            }
            RegFile::UPred => {
                let write_latency =
                    URegLatencySM75::op_category(write, false, dst_idx);
                let read_latency =
                    URegLatencySM75::op_category(read, true, src_idx);

                URegLatencySM75::pred_write_after_read(
                    read_latency,
                    write_latency,
                )
            }
            _ => panic!("Not a register"),
        }
    }

    pub fn waw(
        a: &Op,
        a_dst_idx: usize,
        b: &Op,
        b_dst_idx: usize,
        a_op_pred: bool,
    ) -> u32 {
        let dst_file = match &a.dsts_as_slice()[a_dst_idx] {
            Dst::None => return 0,
            Dst::SSA(vec) => vec.file(),
            Dst::Reg(reg) => reg.file(),
        };

        match dst_file {
            RegFile::GPR => {
                let write1_latency =
                    RegLatencySM75::op_category(a, false, a_dst_idx);
                let write2_latency =
                    RegLatencySM75::op_category(b, false, b_dst_idx);

                RegLatencySM75::write_after_write(
                    write1_latency,
                    write2_latency,
                    a_op_pred,
                )
            }
            RegFile::UGPR => {
                let write1_latency =
                    URegLatencySM75::op_category(a, false, a_dst_idx);
                let write2_latency =
                    URegLatencySM75::op_category(b, false, b_dst_idx);

                URegLatencySM75::write_after_write(
                    write1_latency,
                    write2_latency,
                    a_op_pred,
                )
            }
            RegFile::Pred => {
                let write1_latency =
                    RegLatencySM75::op_category(a, false, a_dst_idx);
                let write2_latency =
                    RegLatencySM75::op_category(b, false, b_dst_idx);

                RegLatencySM75::pred_write_after_write(
                    write1_latency,
                    write2_latency,
                    a_op_pred,
                )
            }
            RegFile::UPred => {
                let write1_latency =
                    URegLatencySM75::op_category(a, false, a_dst_idx);
                let write2_latency =
                    URegLatencySM75::op_category(b, false, b_dst_idx);

                URegLatencySM75::pred_write_after_write(
                    write1_latency,
                    write2_latency,
                )
            }
            _ => panic!("Not a register"),
        }
    }
}
