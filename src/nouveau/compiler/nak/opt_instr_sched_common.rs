// Copyright © 2024 Valve Corporation
// SPDX-License-Identifier: MIT

use crate::ir::*;
use compiler::cfg::CFG;
use std::cmp::max;
use std::cmp::Reverse;

pub mod graph {
    #[derive(Clone)]
    pub struct Edge<EdgeLabel> {
        pub label: EdgeLabel,
        pub head_idx: usize,
    }

    #[derive(Clone)]
    pub struct Node<NodeLabel, EdgeLabel> {
        pub label: NodeLabel,
        pub outgoing_edges: Vec<Edge<EdgeLabel>>,
    }

    #[derive(Clone)]
    pub struct Graph<NodeLabel, EdgeLabel> {
        pub nodes: Vec<Node<NodeLabel, EdgeLabel>>,
    }

    impl<NodeLabel, EdgeLabel> Graph<NodeLabel, EdgeLabel> {
        pub fn new(node_labels: impl Iterator<Item = NodeLabel>) -> Self {
            let nodes = node_labels
                .map(|label| Node {
                    label,
                    outgoing_edges: Vec::new(),
                })
                .collect();

            Graph { nodes }
        }

        pub fn add_edge(
            &mut self,
            tail_idx: usize,
            head_idx: usize,
            label: EdgeLabel,
        ) {
            assert!(head_idx < self.nodes.len());
            self.nodes[tail_idx]
                .outgoing_edges
                .push(Edge { label, head_idx });
        }

        pub fn reverse(&mut self) {
            let old_edges: Vec<_> = self
                .nodes
                .iter_mut()
                .map(|node| std::mem::take(&mut node.outgoing_edges))
                .collect();

            for (tail_idx, edges) in old_edges.into_iter().enumerate() {
                for e in edges.into_iter() {
                    self.add_edge(e.head_idx, tail_idx, e.label);
                }
            }
        }
    }
}

#[derive(Eq, PartialEq)]
pub enum SideEffect {
    /// No side effect (ALU-like)
    None,

    /// Instruction reads or writes memory
    ///
    /// This will be serialized with respect to other
    /// SideEffect::Memory instructions
    Memory,

    /// This instcuction is a full code motion barrier
    ///
    /// No other instruction will be re-ordered with respect to this one
    Barrier,
}

pub fn side_effect_type(op: &Op) -> SideEffect {
    match op {
        // Float ALU
        Op::F2FP(_)
        | Op::FAdd(_)
        | Op::FFma(_)
        | Op::FMnMx(_)
        | Op::FMul(_)
        | Op::FSet(_)
        | Op::FSetP(_)
        | Op::HAdd2(_)
        | Op::HFma2(_)
        | Op::HMul2(_)
        | Op::HSet2(_)
        | Op::HSetP2(_)
        | Op::HMnMx2(_)
        | Op::FSwz(_)
        | Op::FSwzAdd(_) => SideEffect::None,

        // Multi-function unit
        Op::Rro(_) | Op::MuFu(_) => SideEffect::None,

        // Double-precision float ALU
        Op::DAdd(_)
        | Op::DFma(_)
        | Op::DMnMx(_)
        | Op::DMul(_)
        | Op::DSetP(_) => SideEffect::None,

        // Integer ALU
        Op::BRev(_)
        | Op::Flo(_)
        | Op::PopC(_)
        | Op::IMad(_)
        | Op::IMul(_)
        | Op::BMsk(_)
        | Op::IAbs(_)
        | Op::IAdd2(_)
        | Op::IAdd2X(_)
        | Op::IAdd3(_)
        | Op::IAdd3X(_)
        | Op::IDp4(_)
        | Op::IMad64(_)
        | Op::IMnMx(_)
        | Op::ISetP(_)
        | Op::Lea(_)
        | Op::LeaX(_)
        | Op::Lop2(_)
        | Op::Lop3(_)
        | Op::SuClamp(_)
        | Op::SuBfm(_)
        | Op::SuEau(_)
        | Op::IMadSp(_)
        | Op::Shf(_)
        | Op::Shl(_)
        | Op::Shr(_)
        | Op::Bfe(_) => SideEffect::None,

        // Conversions
        Op::F2F(_) | Op::F2I(_) | Op::I2F(_) | Op::I2I(_) | Op::FRnd(_) => {
            SideEffect::None
        }

        // Move ops
        Op::Mov(_) | Op::Prmt(_) | Op::Sel(_) | Op::Sgxt(_) => SideEffect::None,
        Op::Shfl(_) => SideEffect::None,

        // Predicate ops
        Op::PLop3(_) | Op::PSetP(_) => SideEffect::None,

        // Uniform ops
        Op::R2UR(_) | Op::Redux(_) => SideEffect::None,

        // Texture ops
        Op::Tex(_)
        | Op::Tld(_)
        | Op::Tld4(_)
        | Op::Tmml(_)
        | Op::Txd(_)
        | Op::Txq(_) => SideEffect::Memory,

        // Surface ops
        Op::SuLd(_)
        | Op::SuSt(_)
        | Op::SuAtom(_)
        | Op::SuLdGa(_)
        | Op::SuStGa(_) => SideEffect::Memory,

        // Memory ops
        Op::Ipa(_) | Op::Ldc(_) => SideEffect::None,
        Op::Ld(_)
        | Op::Ldsm(_)
        | Op::LdSharedLock(_)
        | Op::St(_)
        | Op::StSCheckUnlock(_)
        | Op::Atom(_)
        | Op::AL2P(_)
        | Op::ALd(_)
        | Op::ASt(_)
        | Op::CCtl(_)
        | Op::LdTram(_)
        | Op::MemBar(_) => SideEffect::Memory,

        // Matrix ops
        Op::Imma(_) | Op::Hmma(_) | Op::Movm(_) => SideEffect::None,

        // Control-flow ops
        Op::BClear(_)
        | Op::Break(_)
        | Op::BSSy(_)
        | Op::BSync(_)
        | Op::SSy(_)
        | Op::Sync(_)
        | Op::Brk(_)
        | Op::PBk(_)
        | Op::Cont(_)
        | Op::PCnt(_)
        | Op::Bra(_)
        | Op::Exit(_)
        | Op::WarpSync(_) => SideEffect::Barrier,

        // We don't model the barrier register yet, so serialize these
        Op::BMov(_) => SideEffect::Memory,

        // Geometry ops
        Op::Out(_) | Op::OutFinal(_) => SideEffect::Barrier,

        // Miscellaneous ops
        Op::Bar(_)
        | Op::TexDepBar(_)
        | Op::CS2R(_)
        | Op::Isberd(_)
        | Op::ViLd(_)
        | Op::Kill(_)
        | Op::S2R(_) => SideEffect::Barrier,
        Op::PixLd(_) | Op::Vote(_) | Op::Match(_) => SideEffect::None,
        Op::Nop(OpNop { label, .. }) => {
            if label.is_none() {
                SideEffect::None
            } else {
                SideEffect::Barrier
            }
        }

        // Virtual ops
        Op::Annotate(_)
        | Op::ParCopy(_)
        | Op::Swap(_)
        | Op::Copy(_)
        | Op::Undef(_) => SideEffect::None,

        Op::SrcBar(_)
        | Op::Pin(_)
        | Op::Unpin(_)
        | Op::PhiSrcs(_)
        | Op::PhiDsts(_)
        | Op::RegOut(_) => SideEffect::Barrier,
    }
}

pub fn estimate_block_weight(cfg: &CFG<BasicBlock>, block_idx: usize) -> u64 {
    let loop_depth = cfg.loop_depth(block_idx) as f32;
    10_f32.powf((loop_depth + 1.0).log2()) as u64
}

/// Try to guess how many cycles a variable latency instruction will take
///
/// These values are based on the cycle estimates from ["Dissecting the NVidia
/// Turing T4 GPU via Microbenchmarking"](https://arxiv.org/pdf/1903.07486).
/// Memory instructions were copied from L1 data cache latencies.
/// For instructions not mentioned in the paper, I made up numbers.
/// This could probably be improved.
pub fn estimate_variable_latency(sm: u8, op: &Op) -> u32 {
    match op {
        // Multi-function unit
        Op::Rro(_) | Op::MuFu(_) => 15,

        // Double-precision float ALU
        Op::DFma(_) | Op::DSetP(_) => 54,
        Op::DAdd(_) | Op::DMnMx(_) | Op::DMul(_) => 48,

        // Integer ALU
        Op::BRev(_) | Op::Flo(_) | Op::PopC(_) => 15,
        Op::IMad(_) | Op::IMul(_) => {
            assert!(sm < 70);
            86
        }

        // Conversions
        Op::F2F(_) | Op::F2I(_) | Op::I2F(_) | Op::I2I(_) | Op::FRnd(_) => 15,

        // Move ops
        Op::Shfl(_) => 15,

        // Uniform ops
        Op::Redux(_) | Op::R2UR(_) => 15,

        // Texture ops
        Op::Tex(_)
        | Op::Tld(_)
        | Op::Tld4(_)
        | Op::Tmml(_)
        | Op::Txd(_)
        | Op::Txq(_) => 32,

        // Surface ops
        Op::SuLd(_)
        | Op::SuSt(_)
        | Op::SuAtom(_)
        | Op::SuLdGa(_)
        | Op::SuStGa(_) => 32,

        // Memory ops
        Op::Ldc(_) => 4,

        Op::Ld(_)
        | Op::Ldsm(_)
        | Op::Movm(_)
        | Op::LdSharedLock(_)
        | Op::St(_)
        | Op::StSCheckUnlock(_)
        | Op::Atom(_)
        | Op::AL2P(_)
        | Op::ALd(_)
        | Op::ASt(_)
        | Op::Ipa(_)
        | Op::CCtl(_)
        | Op::LdTram(_)
        | Op::MemBar(_) => 32,

        // Control-flow ops
        Op::WarpSync(_) => 16,

        // Barrier
        Op::BMov(_) => 16,

        // Geometry ops
        Op::Out(_) | Op::OutFinal(_) => 2,

        // Miscellaneous ops
        Op::Bar(_)
        | Op::TexDepBar(_)
        | Op::CS2R(_)
        | Op::Isberd(_)
        | Op::ViLd(_)
        | Op::Kill(_)
        | Op::PixLd(_)
        | Op::S2R(_)
        | Op::Match(_) => 16,

        Op::Hmma(_) | Op::Imma(_) => 22,

        _ => panic!("Unknown variable latency op {op}"),
    }
}

#[derive(Default, Clone)]
pub struct NodeLabel {
    pub cycles_to_end: u32,
    pub num_uses: u32,

    /// The first cycle that the instruction can begin executing
    pub ready_cycle: u32,

    pub exec_latency: u32,
}

pub struct EdgeLabel {
    pub latency: u32,
}

pub type DepGraph = graph::Graph<NodeLabel, EdgeLabel>;

pub fn calc_statistics(g: &mut DepGraph) -> Vec<usize> {
    let mut initial_ready_list = Vec::new();
    for i in (0..g.nodes.len()).rev() {
        let node = &g.nodes[i];
        let mut max_delay = 0;
        for edge in &node.outgoing_edges {
            assert!(edge.head_idx > i);
            max_delay = max(
                max_delay,
                g.nodes[edge.head_idx].label.cycles_to_end + edge.label.latency,
            );
        }
        let node = &mut g.nodes[i];
        node.label.cycles_to_end = max_delay;
        node.label.num_uses = node.outgoing_edges.len().try_into().unwrap();
        if node.label.num_uses == 0 {
            initial_ready_list.push(i);
        }
    }

    initial_ready_list
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct ReadyInstr {
    cycles_to_end: u32,

    // We use the original instruction order as a final tie-breaker, the idea
    // being that the original schedule is often not too bad. Since we're
    // iterating in reverse order, that means scheduling the largest instruciton
    // index first.
    pub index: usize,
}

impl ReadyInstr {
    pub fn new<E>(g: &graph::Graph<NodeLabel, E>, i: usize) -> Self {
        let label = &g.nodes[i].label;
        // NodeLabel::cycles_to_end is cycles from the beginning of the
        // instruction to the top of the block, but we want from the end of the
        // instruction to the top of the block
        let cycles_to_end = label.cycles_to_end + label.exec_latency;
        ReadyInstr {
            cycles_to_end,
            index: i,
        }
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct FutureReadyInstr {
    /// The first cycle that the instruction can end executing
    pub ready_cycle: Reverse<u32>,
    pub index: usize,
}

impl FutureReadyInstr {
    pub fn new<E>(g: &graph::Graph<NodeLabel, E>, i: usize) -> Self {
        let label = &g.nodes[i].label;
        // NodeLabel::ready_cycle is the earliest beginning cycle for the
        // instruction, but we need the earliest end cycle for the instruction.
        let ready_cycle = label.ready_cycle.saturating_sub(label.exec_latency);
        FutureReadyInstr {
            ready_cycle: Reverse(ready_cycle),
            index: i,
        }
    }
}

#[allow(dead_code)]
pub fn save_graphviz(
    instrs: &[Box<Instr>],
    g: &DepGraph,
) -> std::io::Result<()> {
    // dot /tmp/instr_dep_graph.dot -Tsvg > /tmp/instr_dep_graph.svg

    use std::fs::File;
    use std::io::{BufWriter, Write};

    let file = File::create("/tmp/instr_dep_graph.dot")?;
    let mut w = BufWriter::new(file);

    writeln!(w, "digraph {{")?;
    for (i, instr) in instrs.iter().enumerate() {
        let l = &g.nodes[i].label;
        writeln!(
            w,
            "    {i} [label=\"{}\\n{}, {}\"];",
            instr, l.cycles_to_end, l.num_uses
        )?;
    }
    for (i, node) in g.nodes.iter().enumerate() {
        for j in &node.outgoing_edges {
            writeln!(
                w,
                "    {i} -> {} [label=\"{}\"];",
                j.head_idx, j.label.latency
            )?;
        }
    }
    writeln!(w, "}}")?;
    w.flush()?;
    Ok(())
}
