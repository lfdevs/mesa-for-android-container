// Copyright © 2025 Red Hat.
// SPDX-License-Identifier: MIT
#![allow(non_camel_case_types)]

use crate::ir::*;

use crate::sm75_instr_latencies::pred;

// This contains the register scheduling information provided by NVIDIA.  This
// file is for Ampere and Ada only.

// Coupled instructions are ones with fixed latencies, they need delays but not
// scoreboards.  Decoupled instructions are ones with variable latencies, need
// scoreboards but not delays.  There are also redirected instructions which
// depending on the SM, can be coupled or decoupled so both delays and
// scoreboards needs to be provided.

#[allow(dead_code)]
#[derive(Debug)]
enum RegLatencySM80 {
    CoupledAlu,
    CoupledDisp64,
    CoupledFMA,
    IMADWideReadAB,
    IMADWideReadCL,
    IMADWideReadCH,
    IMADWideWriteDL,
    IMADWideWriteDH,
    FP16,
    FP16_Alu,
    FP16_F32,
    HFMA2_MMA,
    RedirectedFP64,
    Clmad,
    IMMA_88,
    MMA_1x_collect,
    MMA_2x_collect,
    DMMA,
    Cbu,
    Decoupled,
    DecoupledAgu,
}

#[allow(dead_code)]
#[derive(Debug)]
enum PredLatencySM80 {
    Disp_Alu,
    Coupled,
    FMA,
    FP16,
    HFMA2_MMA,
    RedirectedFP64,
    Decoupled,
    Guard,
}

#[allow(dead_code)]
#[derive(Debug)]
enum URegLatencySM80 {
    Coupled,
    Decoupled,
    Cbu,
    CoupledBindless,
    DecoupledBindless,
    ToUr,
    Rpcmov_64,
    Udp,
    Uldc,
    Umov,
    VoteU,
}

#[allow(dead_code)]
#[derive(Debug)]
enum UPredLatencySM80 {
    Coupled,
    Udp,
    VoteU,
    UGuard,
    Bra_Jmp,
    Uldc_Mma,
}

impl RegLatencySM80 {
    fn op_category(op: &Op, reader: bool, op_reg_idx: usize) -> RegLatencySM80 {
        use RegLatencySM80::*;
        match op {
            // this will need updating if imad grows support for input predicates
            Op::IMad(_) | Op::IMul(_) => CoupledFMA,
            Op::IMad64(_) => {
                if reader {
                    match op_reg_idx {
                        0 | 1 => IMADWideReadAB,
                        2 => IMADWideReadCL, // vs upper C operand - work it out
                        _ => {
                            panic!("Illegal field in imadwide")
                        }
                    }
                } else {
                    IMADWideWriteDH // as above this needs more work
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

            Op::F2FP(_) => CoupledAlu,
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

            Op::HAdd2(hadd2) => {
                if hadd2.f32 {
                    FP16_F32
                } else {
                    FP16
                }
            }
            Op::HFma2(_) | Op::HMul2(_) => FP16,

            Op::HSet2(_) | Op::HSetP2(_) | Op::HMnMx2(_) => FP16_Alu,
            // let in for documentation purposes
            Op::Hmma(h) => match (h.mat_size, h.dst_type, h.src_type) {
                // (HmmaSize::M16N8K8, FloatType::F32, FloatType::TF32) => MMA_2x_collect,
                (HmmaSize::M16N8K8, FloatType::F32, FloatType::F16) => {
                    MMA_1x_collect
                }
                // (HmmaSize::M16N8K8, FloatType::F32, FloatType::BF16) => MMA_1x_collect,
                // (HmmaSize::M16N8K4, FloatType::F32, FloatType::TF32) => MMA_1x_collect,
                (HmmaSize::M16N8K8, FloatType::F16, _) => MMA_1x_collect,
                (HmmaSize::M16N8K16, _, _) => MMA_2x_collect,
                _ => panic!("Illegal HMMA in reg category {}", h),
            },
            Op::Ipa(_) => DecoupledAgu,
            Op::MuFu(_) => Decoupled,

            // Conversion functions all decoupled
            Op::F2F(_) => Decoupled,
            Op::F2I(_) => Decoupled,
            Op::I2F(_) => Decoupled,
            Op::FRnd(_) => Decoupled,
            Op::AL2P(_) => Decoupled,

            Op::Mov(_) => CoupledAlu,
            Op::Movm(_) => DecoupledAgu,
            Op::Sel(_) => CoupledAlu,
            Op::BRev(_) => Decoupled,
            // P2R => CoupledAlu,
            // R2P => CoupledAlu,
            Op::PLop3(_) => CoupledAlu,
            Op::Prmt(_) => CoupledAlu,
            Op::Nop(_) => CoupledDisp64,
            Op::Vote(_) => CoupledAlu,
            Op::Match(_) => Decoupled,
            Op::S2R(_) => Decoupled,
            // S2UR  => Decoupled,
            Op::R2UR(_) | Op::Redux(_) => {
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
            // B2R => DecoupledAgu,
            // LEPC => CoupledDisp64
            Op::BMov(bmov) => match bmov.dst {
                Dst::Reg(_) => Cbu,
                _ => Cbu,
            },
            // RPCMOV.32 => CoupledAlu,
            // RPCMOV.64 => CoupledDisp64
            // PMTRIG => CoupledDisp64
            // CSMTEST =>  CoupledAlu,
            Op::Bar(_) => DecoupledAgu,
            // Remove when Imma added
            Op::Imma(i) => match (i.mat_size, i.src_types[0]) {
                (ImmaSize::M16N8K64, _) => MMA_2x_collect,
                (ImmaSize::M16N8K32, IntType::I8 | IntType::U8) => {
                    MMA_2x_collect
                }
                // (ImmaSize::M16N8K32, IntType::I4 | IntType::U4) => MMA_1x_collect,
                (ImmaSize::M16N8K16, _) => MMA_1x_collect,
                (ImmaSize::M8N8K32, _) => IMMA_88,
                (ImmaSize::M8N8K16, _) => IMMA_88,
                _ => panic!("Illegal IMMA in reg category {}", i),
            },
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
            Op::ALd(_) => DecoupledAgu,
            Op::ASt(_) => DecoupledAgu,
            Op::Out(_) => DecoupledAgu,
            Op::OutFinal(_) => DecoupledAgu,
            Op::Ld(_) => DecoupledAgu,
            Op::St(_) => DecoupledAgu,
            Op::Atom(_) => DecoupledAgu,
            //CCtl.i,c are coupled
            Op::CCtl(_) => DecoupledAgu,
            Op::MemBar(_) => Decoupled,
            Op::SuLd(_) => Decoupled,
            Op::SuSt(_) => Decoupled,
            Op::SuAtom(_) => Decoupled,
            Op::PixLd(_) => DecoupledAgu,
            Op::Isberd(_) => DecoupledAgu,
            Op::LdTram(_) => DecoupledAgu,
            Op::Shfl(_) => DecoupledAgu,
            Op::Ldsm(_) => DecoupledAgu,
            x => {
                panic!("Illegal instuction in reg category {}", x);
            }
        }
    }

    fn read_after_write(writer: RegLatencySM80, reader: RegLatencySM80) -> u32 {
        use RegLatencySM80::*;
        match reader {
            CoupledAlu => match writer {
                CoupledAlu => 4,
                CoupledDisp64 => 6,
                CoupledFMA => 5,
                IMADWideWriteDL => 3,
                IMADWideWriteDH => 5,
                FP16 => 5,
                FP16_Alu => 5,
                FP16_F32 => 5,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            CoupledFMA | IMADWideReadCL => match writer {
                CoupledAlu => 5,
                CoupledDisp64 => 6,
                CoupledFMA => 4,
                IMADWideWriteDL => 2,
                IMADWideWriteDH => 4,
                FP16 => 5,
                FP16_Alu => 5,
                FP16_F32 => 5,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            IMADWideReadAB => match writer {
                CoupledAlu => 5,
                CoupledDisp64 => 6,
                CoupledFMA => 4,
                IMADWideWriteDL => 4,
                IMADWideWriteDH => 6,
                FP16 => 5,
                FP16_Alu => 5,
                FP16_F32 => 5,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            IMADWideReadCH => match writer {
                CoupledAlu => 3,
                CoupledDisp64 => 4,
                CoupledFMA => 2,
                IMADWideWriteDL => 2,
                IMADWideWriteDH => 2,
                FP16 => 3,
                FP16_Alu => 3,
                FP16_F32 => 3,
                HFMA2_MMA => 8,
                RedirectedFP64 => 8,
                Clmad => 10,
                IMMA_88 => 11,
                MMA_1x_collect => 14,
                MMA_2x_collect => 22,
                DMMA => 23,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            FP16 => match writer {
                CoupledAlu => 5,
                CoupledDisp64 => 6,
                CoupledFMA => 5,
                IMADWideWriteDL => 3,
                IMADWideWriteDH => 5,
                FP16 => 4,
                FP16_Alu => 5,
                FP16_F32 => 5,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            FP16_Alu => match writer {
                CoupledAlu => 5,
                CoupledDisp64 => 6,
                CoupledFMA => 5,
                IMADWideWriteDL => 3,
                IMADWideWriteDH => 5,
                FP16 => 5,
                FP16_Alu => 4,
                FP16_F32 => 5,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            FP16_F32 => match writer {
                CoupledAlu => 5,
                CoupledDisp64 => 6,
                CoupledFMA => 5,
                IMADWideWriteDL => 3,
                IMADWideWriteDH => 5,
                FP16 => 5,
                FP16_Alu => 5,
                FP16_F32 => 5,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            HFMA2_MMA | RedirectedFP64 => match writer {
                CoupledAlu => 6,
                CoupledDisp64 => 6,
                CoupledFMA => 6,
                IMADWideWriteDL => 6,
                IMADWideWriteDH => 6,
                FP16 => 6,
                FP16_Alu => 6,
                FP16_F32 => 6,
                HFMA2_MMA => 6,
                RedirectedFP64 => 6,
                Clmad => 12,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            Clmad => match writer {
                CoupledAlu => 6,
                CoupledDisp64 => 6,
                CoupledFMA => 6,
                IMADWideWriteDL => 6,
                IMADWideWriteDH => 6,
                FP16 => 6,
                FP16_Alu => 6,
                FP16_F32 => 6,
                HFMA2_MMA => 10,
                RedirectedFP64 => 10,
                Clmad => 8,
                IMMA_88 => 13,
                MMA_1x_collect => 16,
                MMA_2x_collect => 24,
                DMMA => 25,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            IMMA_88 | MMA_1x_collect => {
                match writer {
                    CoupledAlu => 7,
                    CoupledDisp64 => 7,
                    CoupledFMA => 7,
                    IMADWideWriteDL => 7,
                    IMADWideWriteDH => 7,
                    FP16 => 7,
                    FP16_Alu => 7,
                    FP16_F32 => 7,
                    HFMA2_MMA => 11,
                    RedirectedFP64 => 11,
                    Clmad => 13,
                    IMMA_88 => 14,        //6??
                    MMA_1x_collect => 16, //6??
                    MMA_2x_collect => 24,
                    DMMA => 26,
                    Cbu => 1,
                    Decoupled => 1,
                    DecoupledAgu => 1,
                    _ => {
                        panic!("Illegal writer in sm80 raw");
                    }
                }
            }
            MMA_2x_collect => {
                match writer {
                    CoupledAlu => 7,
                    CoupledDisp64 => 7,
                    CoupledFMA => 7,
                    IMADWideWriteDL => 7,
                    IMADWideWriteDH => 7,
                    FP16 => 7,
                    FP16_Alu => 7,
                    FP16_F32 => 7,
                    HFMA2_MMA => 11,
                    RedirectedFP64 => 11,
                    Clmad => 13,
                    IMMA_88 => 14,
                    MMA_1x_collect => 16,
                    MMA_2x_collect => 24, //10??
                    DMMA => 26,
                    Cbu => 1,
                    Decoupled => 1,
                    DecoupledAgu => 1,
                    _ => {
                        panic!("Illegal writer in sm80 raw");
                    }
                }
            }
            DMMA => {
                match writer {
                    CoupledAlu => 7,
                    CoupledDisp64 => 7,
                    CoupledFMA => 7,
                    IMADWideWriteDL => 7,
                    IMADWideWriteDH => 7,
                    FP16 => 7,
                    FP16_Alu => 7,
                    FP16_F32 => 7,
                    HFMA2_MMA => 11,
                    RedirectedFP64 => 11,
                    Clmad => 13,
                    IMMA_88 => 14,
                    MMA_1x_collect => 16,
                    MMA_2x_collect => 24,
                    DMMA => 26, //18??
                    Cbu => 1,
                    Decoupled => 1,
                    DecoupledAgu => 1,
                    _ => {
                        panic!("Illegal writer in sm80 raw");
                    }
                }
            }
            Cbu | Decoupled => match writer {
                CoupledAlu => 4,
                CoupledDisp64 => 4,
                CoupledFMA => 4,
                IMADWideWriteDL => 4,
                IMADWideWriteDH => 4,
                FP16 => 4,
                FP16_Alu => 4,
                FP16_F32 => 4,
                HFMA2_MMA => 6,
                RedirectedFP64 => 6,
                Clmad => 8,
                IMMA_88 => 11,
                MMA_1x_collect => 14,
                MMA_2x_collect => 22,
                DMMA => 23,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            DecoupledAgu => match writer {
                CoupledAlu => 5,
                CoupledDisp64 => 5,
                CoupledFMA => 5,
                IMADWideWriteDL => 5,
                IMADWideWriteDH => 5,
                FP16 => 5,
                FP16_Alu => 5,
                FP16_F32 => 5,
                HFMA2_MMA => 7,
                RedirectedFP64 => 7,
                Clmad => 9,
                IMMA_88 => 12,
                MMA_1x_collect => 15,
                MMA_2x_collect => 23,
                DMMA => 24,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 raw");
                }
            },
            CoupledDisp64 | IMADWideWriteDL | IMADWideWriteDH => {
                panic!("Illegal reader in sm80 raw");
            }
        }
    }

    fn write_after_write(
        writer1: RegLatencySM80,
        writer2: RegLatencySM80,
        has_pred: bool,
    ) -> u32 {
        use RegLatencySM80::*;
        match writer2 {
            CoupledAlu => match writer1 {
                CoupledDisp64 => pred(has_pred, 1, 1),
                CoupledAlu | CoupledFMA | IMADWideWriteDL | IMADWideWriteDH
                | FP16 | FP16_Alu | FP16_F32 => 1,
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 3, 3),
                Clmad => pred(has_pred, 5, 3),
                IMMA_88 => pred(has_pred, 8, 1),
                MMA_1x_collect => pred(has_pred, 11, 1),
                MMA_2x_collect => pred(has_pred, 19, 1),
                DMMA => pred(has_pred, 20, 1),
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            CoupledDisp64 => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 => 1,
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 3, 1),
                Clmad => pred(has_pred, 5, 1),
                IMMA_88 => 8,
                MMA_1x_collect => 11,
                MMA_2x_collect => 19,
                DMMA => 20,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            CoupledFMA => match writer1 {
                CoupledDisp64 => pred(has_pred, 1, 1),
                CoupledAlu | CoupledFMA | IMADWideWriteDL | FP16 | FP16_Alu
                | FP16_F32 => 1,
                IMADWideWriteDH => pred(has_pred, 1, 1),
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 3, 3),
                Clmad => pred(has_pred, 5, 3),
                IMMA_88 => pred(has_pred, 8, 1),
                MMA_1x_collect => pred(has_pred, 11, 1),
                MMA_2x_collect => pred(has_pred, 19, 1),
                DMMA => pred(has_pred, 20, 1),
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            IMADWideWriteDL => match writer1 {
                CoupledAlu => pred(has_pred, 1, 2),
                CoupledDisp64 => pred(has_pred, 1, 3),
                CoupledFMA => pred(has_pred, 1, 1),
                IMADWideWriteDL => 1,
                IMADWideWriteDH => pred(has_pred, 1, 1),
                FP16 | FP16_Alu | FP16_F32 => pred(has_pred, 1, 2),
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 5, 3),
                Clmad => pred(has_pred, 5, 5),
                IMMA_88 => pred(has_pred, 8, 3),
                MMA_1x_collect => pred(has_pred, 11, 3),
                MMA_2x_collect => pred(has_pred, 19, 3),
                DMMA => pred(has_pred, 20, 3),
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            IMADWideWriteDH => match writer1 {
                CoupledAlu => 1,
                CoupledDisp64 => pred(has_pred, 1, 1),
                CoupledFMA => 1,
                IMADWideWriteDL | IMADWideWriteDH | FP16 | FP16_Alu
                | FP16_F32 => 1,
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 5, 1),
                Clmad => pred(has_pred, 5, 3),
                IMMA_88 => pred(has_pred, 8, 1),
                MMA_1x_collect => pred(has_pred, 11, 1),
                MMA_2x_collect => pred(has_pred, 19, 1),
                DMMA => pred(has_pred, 20, 1),
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            FP16 | FP16_Alu => match writer1 {
                CoupledAlu => 1,
                CoupledDisp64 => pred(has_pred, 1, 1),
                CoupledFMA => 1,
                IMADWideWriteDL | IMADWideWriteDH | FP16 | FP16_Alu
                | FP16_F32 => 1,
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 3, 3),
                Clmad => pred(has_pred, 5, 3),
                IMMA_88 => pred(has_pred, 8, 1),
                MMA_1x_collect => pred(has_pred, 11, 1),
                MMA_2x_collect => pred(has_pred, 19, 1),
                DMMA => pred(has_pred, 20, 1),
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            FP16_F32 => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 => 1,
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 3, 2),
                Clmad => pred(has_pred, 5, 2),
                IMMA_88 => 8,
                MMA_1x_collect => 11,
                MMA_2x_collect => 19,
                DMMA => 20,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            HFMA2_MMA => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 => 1,
                HFMA2_MMA => 2,
                RedirectedFP64 => 3,
                Clmad => pred(has_pred, 5, 1),
                IMMA_88 => 8,
                MMA_1x_collect => 11,
                MMA_2x_collect => 19,
                DMMA => 20,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            RedirectedFP64 => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 => 1,
                HFMA2_MMA => 2,
                RedirectedFP64 => 2,
                Clmad => pred(has_pred, 4, 2),
                IMMA_88 => 7,
                MMA_1x_collect => 10,
                MMA_2x_collect => 18,
                DMMA => 19,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            Clmad => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 | HFMA2_MMA
                | RedirectedFP64 | Clmad => 2,
                IMMA_88 => 7,
                MMA_1x_collect => 10,
                MMA_2x_collect => 18,
                DMMA => 19,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            IMMA_88 | MMA_1x_collect => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 | HFMA2_MMA
                | RedirectedFP64 | Clmad => 2,
                IMMA_88 => 4,
                MMA_1x_collect => 8,
                MMA_2x_collect => 16,
                DMMA => 17,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            MMA_2x_collect | DMMA => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 | HFMA2_MMA
                | RedirectedFP64 | Clmad => 2,
                IMMA_88 => 4,
                MMA_1x_collect => 8,
                MMA_2x_collect => 16,
                DMMA => 16,
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            Cbu | Decoupled | DecoupledAgu => match writer1 {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
                | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 => {
                    pred(has_pred, 1, 5)
                }
                HFMA2_MMA | RedirectedFP64 => pred(has_pred, 1, 9),
                Clmad => pred(has_pred, 1, 11),
                IMMA_88 => pred(has_pred, 7, 6),
                MMA_1x_collect => pred(has_pred, 10, 5),
                MMA_2x_collect => pred(has_pred, 18, 5),
                DMMA => pred(has_pred, 19, 6),
                Cbu => 1,
                Decoupled => 1,
                DecoupledAgu => 1,
                _ => {
                    panic!("Illegal writer in sm80 waw");
                }
            },
            _ => {
                panic!("Illegal writer in sm80 waw");
            }
        }
    }

    fn write_after_read(reader: RegLatencySM80, writer: RegLatencySM80) -> u32 {
        use RegLatencySM80::*;
        match writer {
            CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideWriteDL
            | IMADWideWriteDH | FP16 | FP16_Alu | FP16_F32 | HFMA2_MMA => {
                match reader {
                    MMA_2x_collect => 7,
                    _ => 1,
                }
            }
            RedirectedFP64 => 1,
            Clmad | IMMA_88 | MMA_1x_collect | MMA_2x_collect | DMMA
            | Decoupled | DecoupledAgu => match reader {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideReadAB
                | IMADWideReadCL | IMADWideReadCH | FP16 | FP16_Alu
                | FP16_F32 | HFMA2_MMA => 2,
                _ => 1,
            },
            Cbu => match reader {
                CoupledAlu | CoupledDisp64 | CoupledFMA | IMADWideReadAB
                | IMADWideReadCL | IMADWideReadCH | FP16 | FP16_Alu
                | FP16_F32 | HFMA2_MMA => 2,
                MMA_2x_collect => 7,
                _ => 1,
            },
            _ => {
                panic!("Illegal writer in sm80 war");
            }
        }
    }
}

impl PredLatencySM80 {
    fn op_category(op: &Op) -> PredLatencySM80 {
        match op {
            Op::Atom(_) => PredLatencySM80::Decoupled,
            Op::Bra(_) => PredLatencySM80::Decoupled,
            Op::DSetP(_) => PredLatencySM80::RedirectedFP64,
            Op::FMnMx(_) | Op::FSetP(_) => PredLatencySM80::Coupled,
            Op::HFma2(_) => PredLatencySM80::FP16,
            Op::HMnMx2(_) => PredLatencySM80::FP16,
            Op::HSetP2(_) => PredLatencySM80::FP16,
            Op::IAdd3(_) => PredLatencySM80::Coupled,
            Op::IAdd3X(_) => PredLatencySM80::Coupled,
            Op::IMad(_) => PredLatencySM80::FMA,
            Op::IMad64(_) => PredLatencySM80::FMA,
            Op::IMnMx(_) => PredLatencySM80::Coupled,
            Op::IMul(_) => PredLatencySM80::FMA,
            Op::Ipa(_) => PredLatencySM80::Decoupled,
            Op::ISetP(_) => PredLatencySM80::Coupled,

            Op::Ld(_) => PredLatencySM80::Decoupled,

            Op::Lea(_) | Op::LeaX(_) => PredLatencySM80::Coupled,
            Op::PixLd(_) => PredLatencySM80::Decoupled,
            Op::PLop3(_) => PredLatencySM80::Coupled,
            Op::PSetP(_) => PredLatencySM80::Coupled,
            Op::R2UR(_) => PredLatencySM80::Decoupled,
            Op::Sel(_) => PredLatencySM80::Coupled,
            Op::Shfl(_) => PredLatencySM80::Decoupled,
            Op::SuLd(_) => PredLatencySM80::Decoupled,
            Op::SuSt(_) => PredLatencySM80::Decoupled,
            Op::Tex(_) => PredLatencySM80::Decoupled,
            Op::Tld(_) => PredLatencySM80::Decoupled,
            Op::Tld4(_) => PredLatencySM80::Decoupled,
            Op::Tmml(_) => PredLatencySM80::Decoupled,
            Op::Txd(_) => PredLatencySM80::Decoupled,
            Op::Txq(_) => PredLatencySM80::Decoupled,

            Op::Vote(_) => PredLatencySM80::Disp_Alu,
            Op::Match(_) => PredLatencySM80::Decoupled,
            _ => {
                panic!("Illegal op in sm80 pred latency {}", op);
            }
        }
    }
    fn pred_read_after_write(
        writer: PredLatencySM80,
        reader: PredLatencySM80,
    ) -> u32 {
        match reader {
            PredLatencySM80::Disp_Alu => match writer {
                PredLatencySM80::Disp_Alu
                | PredLatencySM80::Coupled
                | PredLatencySM80::FMA
                | PredLatencySM80::FP16 => 13,
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => 14,
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 praw");
                }
            },
            PredLatencySM80::Coupled => match writer {
                PredLatencySM80::Disp_Alu | PredLatencySM80::Coupled => 4,
                PredLatencySM80::FMA | PredLatencySM80::FP16 => 5,
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => 6,
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 praw");
                }
            },
            PredLatencySM80::FMA => match writer {
                PredLatencySM80::Disp_Alu | PredLatencySM80::Coupled => 5,
                PredLatencySM80::FMA => 4,
                PredLatencySM80::FP16 => 5,
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => 6,
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 praw");
                }
            },
            PredLatencySM80::HFMA2_MMA | PredLatencySM80::RedirectedFP64 => {
                match writer {
                    PredLatencySM80::Disp_Alu
                    | PredLatencySM80::Coupled
                    | PredLatencySM80::FMA
                    | PredLatencySM80::FP16 => 13,
                    PredLatencySM80::HFMA2_MMA
                    | PredLatencySM80::RedirectedFP64 => 6,
                    PredLatencySM80::Decoupled => 1,
                    _ => {
                        panic!("Illegal writer in sm80 praw");
                    }
                }
            }
            PredLatencySM80::Decoupled | PredLatencySM80::Guard => match writer
            {
                PredLatencySM80::Disp_Alu
                | PredLatencySM80::Coupled
                | PredLatencySM80::FMA
                | PredLatencySM80::FP16 => 13,
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => 14,
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 praw");
                }
            },
            _ => {
                panic!("Illegal reader in sm80 praw");
            }
        }
    }

    fn pred_write_after_write(
        writer1: PredLatencySM80,
        writer2: PredLatencySM80,
        has_pred: bool,
    ) -> u32 {
        match writer2 {
            PredLatencySM80::Disp_Alu
            | PredLatencySM80::Coupled
            | PredLatencySM80::FMA => match writer1 {
                PredLatencySM80::Disp_Alu
                | PredLatencySM80::Coupled
                | PredLatencySM80::FMA
                | PredLatencySM80::FP16 => 1,
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => 2,
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 pwaw");
                }
            },
            PredLatencySM80::FP16 => match writer1 {
                PredLatencySM80::Disp_Alu
                | PredLatencySM80::Coupled
                | PredLatencySM80::FMA => pred(has_pred, 2, 7),
                PredLatencySM80::FP16 => 1,
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => pred(has_pred, 2, 8),
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 pwaw");
                }
            },
            PredLatencySM80::HFMA2_MMA | PredLatencySM80::RedirectedFP64 => {
                match writer1 {
                    PredLatencySM80::Disp_Alu
                    | PredLatencySM80::Coupled
                    | PredLatencySM80::FMA
                    | PredLatencySM80::FP16 => pred(has_pred, 2, 5),
                    PredLatencySM80::HFMA2_MMA
                    | PredLatencySM80::RedirectedFP64
                    | PredLatencySM80::Decoupled => 1,
                    _ => {
                        panic!("Illegal writer in sm80 pwaw");
                    }
                }
            }
            PredLatencySM80::Decoupled => match writer1 {
                PredLatencySM80::Disp_Alu
                | PredLatencySM80::Coupled
                | PredLatencySM80::FMA => pred(has_pred, 2, 10),
                PredLatencySM80::FP16 => pred(has_pred, 1, 11),
                PredLatencySM80::HFMA2_MMA
                | PredLatencySM80::RedirectedFP64 => pred(has_pred, 1, 12),
                PredLatencySM80::Decoupled => 1,
                _ => {
                    panic!("Illegal writer in sm80 pwaw");
                }
            },
            _ => {
                panic!("Illegal writer in sm80 pwaw");
            }
        }
    }

    fn pred_write_after_read(
        _reader: PredLatencySM80,
        _writer: PredLatencySM80,
    ) -> u32 {
        1
    }
}

impl URegLatencySM80 {
    fn op_category(
        op: &Op,
        reader: bool,
        op_reg_idx: usize,
    ) -> URegLatencySM80 {
        use URegLatencySM80::*;
        // is this using a bindless cbuf as a src register.
        // this decides between the category types for readers.
        let bindless =
            reader && op.srcs_as_slice()[op_reg_idx].is_bindless_cbuf();

        let vcoupled = if bindless { CoupledBindless } else { Coupled };
        let vdecoupled = if bindless {
            DecoupledBindless
        } else {
            Decoupled
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
            Op::R2UR(_) | Op::Redux(_) => {
                if !reader {
                    ToUr
                } else {
                    panic!("Illegal R2UR in ureg");
                }
            }
            Op::S2R(_) => {
                if !reader {
                    ToUr
                } else {
                    panic!("Illegal S2UR in ureg");
                }
            }
            Op::Vote(_) => VoteU,

            Op::FRnd(_) => vdecoupled,
            Op::F2FP(_)
            | Op::FAdd(_)
            | Op::FMul(_)
            | Op::FFma(_)
            | Op::FSet(_)
            | Op::FSetP(_)
            | Op::FMnMx(_)
            | Op::HAdd2(_)
            | Op::HMul2(_)
            | Op::HSet2(_)
            | Op::HFma2(_)
            | Op::HMnMx2(_)
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
        writer: URegLatencySM80,
        reader: URegLatencySM80,
    ) -> u32 {
        use URegLatencySM80::*;
        match reader {
            Coupled => match writer {
                ToUr => 1,
                Udp => 6,
                Uldc => 2,
                Umov => 2,
                VoteU => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            Decoupled => match writer {
                ToUr => 1,
                Udp => 9,
                Uldc => 2,
                Umov => 2,
                VoteU => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            Cbu => match writer {
                ToUr => 1,
                Udp => 10,
                Uldc => 3,
                Umov => 3,
                VoteU => 3,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            CoupledBindless | DecoupledBindless | Uldc => match writer {
                ToUr => 1,
                Udp => 12,
                Uldc => 5,
                Umov => 5,
                VoteU => 5,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            Udp => match writer {
                ToUr => 1,
                Udp => 4,
                Uldc => 2,
                Umov => 2,
                VoteU => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            Umov | VoteU => match writer {
                ToUr => 1,
                Udp => 7,
                Uldc => 2,
                Umov => 2,
                VoteU => 2,
                _ => {
                    panic!("Illegal writer in raw ureg latency {:?}", writer)
                }
            },
            _ => {
                panic!("Illegal read in ureg raw latency")
            }
        }
    }

    fn write_after_write(
        writer1: URegLatencySM80,
        writer2: URegLatencySM80,
        has_pred: bool,
    ) -> u32 {
        use URegLatencySM80::*;
        match writer2 {
            ToUr => match writer1 {
                ToUr => 1,
                Udp => pred(has_pred, 4, 7),
                Uldc | Umov | VoteU => 4,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            Udp => match writer1 {
                ToUr | Udp | Uldc | Umov | VoteU => 1,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            Uldc | Umov | VoteU => match writer1 {
                ToUr => 1,
                Udp => 7,
                Uldc | Umov | VoteU => 1,
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
        reader: URegLatencySM80,
        writer: URegLatencySM80,
    ) -> u32 {
        use URegLatencySM80::*;
        match writer {
            ToUr | Udp => match reader {
                Coupled | Decoupled | Cbu | CoupledBindless
                | DecoupledBindless | Rpcmov_64 | Udp | Uldc | Umov => 1,
                _ => {
                    panic!("Illegal reader in ureg war latency")
                }
            },
            Uldc | Umov | VoteU => match reader {
                Coupled | Decoupled | Cbu | CoupledBindless
                | DecoupledBindless | Rpcmov_64 | Uldc | Umov => 1,
                Udp => 3,
                _ => {
                    panic!("Illegal reader in ureg war latency")
                }
            },
            _ => {
                panic!("Illegal writer in ureg war latency")
            }
        }
    }
}

impl UPredLatencySM80 {
    fn op_category(op: &Op) -> UPredLatencySM80 {
        use UPredLatencySM80::*;
        let uniform_op = op.is_uniform();
        match op {
            Op::BMsk(_)
            | Op::BRev(_)
            | Op::Flo(_)
            | Op::IAdd3(_)
            | Op::IAdd3X(_)
            | Op::IMad(_)
            | Op::ISetP(_)
            | Op::Lea(_)
            | Op::LeaX(_)
            | Op::Lop3(_)
            | Op::Mov(_) => Udp,
            Op::Bra(_) => Bra_Jmp,
            Op::Ldc(_) => Uldc_Mma,
            Op::PLop3(_) => {
                if uniform_op {
                    Udp
                } else {
                    Coupled
                }
            }
            Op::PSetP(_) => {
                if uniform_op {
                    Udp
                } else {
                    Coupled
                }
            }
            Op::Sel(_) => {
                if uniform_op {
                    Udp
                } else {
                    Coupled
                }
            }
            Op::Vote(_) => {
                if uniform_op {
                    VoteU
                } else {
                    panic!("Illegal Vote in upred");
                }
            }
            _ => {
                panic!("Illegal instuction in upred category {}", op);
            }
        }
    }

    fn pred_read_after_write(
        writer: UPredLatencySM80,
        reader: UPredLatencySM80,
    ) -> u32 {
        use UPredLatencySM80::*;
        match reader {
            Coupled => match writer {
                Udp => 6,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer in ureg praw latency")
                }
            },
            Udp => match writer {
                Udp => 4,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer in ureg praw latency")
                }
            },
            UGuard => match writer {
                Udp => 11,
                VoteU => 5,
                _ => {
                    panic!("Illegal writer in ureg praw latency")
                }
            },
            Bra_Jmp => match writer {
                Udp => 9,
                VoteU => 2,
                _ => {
                    panic!("Illegal writer in ureg praw latency")
                }
            },
            Uldc_Mma => match writer {
                Udp => 11,
                VoteU => 5,
                _ => {
                    panic!("Illegal writer in ureg praw latency")
                }
            },
            _ => {
                panic!("Illegal reader in ureg praw latency")
            }
        }
    }

    fn pred_write_after_write(
        writer1: UPredLatencySM80,
        writer2: UPredLatencySM80,
    ) -> u32 {
        use UPredLatencySM80::*;
        match writer2 {
            Udp => match writer1 {
                Udp => 1,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            VoteU => match writer1 {
                Udp => 7,
                VoteU => 1,
                _ => {
                    panic!("Illegal writer in ureg waw latency")
                }
            },
            _ => {
                panic!("Illegal writer in ureg waw latency")
            }
        }
    }

    fn pred_write_after_read(
        reader: UPredLatencySM80,
        writer: UPredLatencySM80,
    ) -> u32 {
        use UPredLatencySM80::*;
        match writer {
            Udp => match reader {
                Coupled | Udp | UGuard | Bra_Jmp | Uldc_Mma => 1,
                _ => {
                    panic!("Illegal reader in ureg pwar latency")
                }
            },
            VoteU => match reader {
                Coupled | Udp => 2,
                UGuard | Bra_Jmp | Uldc_Mma => 1,
                _ => {
                    panic!("Illegal reader in ureg pwar latency")
                }
            },
            _ => {
                panic!("Illegal writer in ureg pwar latency")
            }
        }
    }
}

pub struct SM80Latency {}

impl SM80Latency {
    pub fn needs_scoreboards(op: &Op) -> bool {
        if op.is_uniform() {
            match URegLatencySM80::op_category(op, false, 0) {
                URegLatencySM80::ToUr => true,
                _ => false,
            }
        } else {
            match RegLatencySM80::op_category(op, false, 0) {
                RegLatencySM80::RedirectedFP64
                | RegLatencySM80::Clmad
                | RegLatencySM80::IMMA_88
                | RegLatencySM80::MMA_1x_collect
                | RegLatencySM80::MMA_2x_collect
                | RegLatencySM80::DMMA
                | RegLatencySM80::Cbu
                | RegLatencySM80::Decoupled
                | RegLatencySM80::DecoupledAgu => true,
                _ => false,
            }
        }
    }

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
                    RegLatencySM80::op_category(write, false, dst_idx);
                let read_latency = match read {
                    Some(op) => RegLatencySM80::op_category(op, true, src_idx),
                    None => RegLatencySM80::RedirectedFP64,
                };

                RegLatencySM80::read_after_write(write_latency, read_latency)
            }
            RegFile::UGPR => {
                let write_latency =
                    URegLatencySM80::op_category(write, false, dst_idx);
                let read_latency = match read {
                    Some(op) => URegLatencySM80::op_category(op, true, src_idx),
                    None => URegLatencySM80::Uldc,
                };

                URegLatencySM80::read_after_write(write_latency, read_latency)
            }
            RegFile::Pred => {
                let write_latency = PredLatencySM80::op_category(write);
                let read_latency = match read {
                    Some(op) => PredLatencySM80::op_category(op),
                    None => PredLatencySM80::RedirectedFP64,
                };

                PredLatencySM80::pred_read_after_write(
                    write_latency,
                    read_latency,
                )
            }
            RegFile::UPred => {
                let write_latency = UPredLatencySM80::op_category(write);
                let read_latency = match read {
                    Some(op) => UPredLatencySM80::op_category(op),
                    None => UPredLatencySM80::UGuard,
                };

                UPredLatencySM80::pred_read_after_write(
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
                    RegLatencySM80::op_category(write, false, dst_idx);
                let read_latency =
                    RegLatencySM80::op_category(read, true, src_idx);

                RegLatencySM80::write_after_read(read_latency, write_latency)
            }
            RegFile::UGPR => {
                let write_latency =
                    URegLatencySM80::op_category(write, false, dst_idx);
                let read_latency =
                    URegLatencySM80::op_category(read, true, src_idx);

                URegLatencySM80::write_after_read(read_latency, write_latency)
            }
            RegFile::Pred => {
                let write_latency = PredLatencySM80::op_category(write);
                let read_latency = PredLatencySM80::op_category(read);

                PredLatencySM80::pred_write_after_read(
                    read_latency,
                    write_latency,
                )
            }
            RegFile::UPred => {
                let write_latency = UPredLatencySM80::op_category(write);
                let read_latency = UPredLatencySM80::op_category(read);

                UPredLatencySM80::pred_write_after_read(
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
                    RegLatencySM80::op_category(a, false, a_dst_idx);
                let write2_latency =
                    RegLatencySM80::op_category(b, false, b_dst_idx);

                RegLatencySM80::write_after_write(
                    write1_latency,
                    write2_latency,
                    a_op_pred,
                )
            }
            RegFile::UGPR => {
                let write1_latency =
                    URegLatencySM80::op_category(a, false, a_dst_idx);
                let write2_latency =
                    URegLatencySM80::op_category(b, false, b_dst_idx);

                URegLatencySM80::write_after_write(
                    write1_latency,
                    write2_latency,
                    a_op_pred,
                )
            }
            RegFile::Pred => {
                let write1_latency = PredLatencySM80::op_category(a);
                let write2_latency = PredLatencySM80::op_category(b);

                PredLatencySM80::pred_write_after_write(
                    write1_latency,
                    write2_latency,
                    a_op_pred,
                )
            }
            RegFile::UPred => {
                let write1_latency = UPredLatencySM80::op_category(a);
                let write2_latency = UPredLatencySM80::op_category(b);

                UPredLatencySM80::pred_write_after_write(
                    write1_latency,
                    write2_latency,
                )
            }
            _ => panic!("Not a register"),
        }
    }
}
