// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::OpNop;
use compiler::cfg::*;
use rustc_hash::{FxBuildHasher, FxHashMap, FxHashSet};

fn validate_jump_instr(instr: &Instr) {
    match &instr.op {
        Op::Branch(_) => (),
        Op::Nop(_) if instr.flow.get_end_shader() => (),
        _ => panic!("All blocks must end in BRANCH or NOP.end"),
    }
}

fn jump_is_unconditional(instr: &Instr) -> bool {
    match &instr.op {
        Op::Branch(op) => op.is_unconditional(),
        Op::Nop(_) => {
            assert!(instr.flow.get_end_shader());
            true
        }
        _ => panic!("All blocks must end in BRANCH or NOP.end"),
    }
}

enum Jump {
    End,
    Branch(Label),
}

impl Shader<'_> {
    /// This pass replaces jumps to empty blocks with a single successor with
    /// jumps to the successor.  This is especially common with if ladders
    /// where we go 2 or 3 ifs deep and then everyone reconverges at the end.
    /// We need those extra blocks when doing RA in case we need to place a phi.
    /// But once RA is complete, if any empty blocks with a single successor
    /// remain, they can be eliminated.
    ///
    /// The one exception here is loop headers.  We can't thread a jump to a
    /// loop header because that would let control flow jump into the middle of
    /// a loop, breaking dominance.  Every path into a loop must go through the
    /// loop header.
    pub fn opt_jump_thread(&mut self) {
        let mut progress = false;

        // A branch to label can be replaced with Jump
        let mut jumps: FxHashMap<Label, Jump> = Default::default();

        // Labels of all blocks which are potential jump targets.  This prevents
        // us from threading through loop headers or back edges, which might
        // break our CFG assumptions.
        let mut labels: FxHashSet<Label> = Default::default();

        // Invariant 1:
        //
        //      At the end of each loop iteration, every trivial block with
        //      an index in [i, blocks.len()) is represented in jumps.keys()
        //
        // Invariant 2:
        //
        //      jumps.values() never contains a branch to a trivial block
        //
        for bi in (0..self.blocks.len()).into_iter().rev() {
            let is_loop_header = self.blocks.is_loop_header(bi);
            let block = &mut self.blocks[bi];
            let label = block.label;

            // Prior to jump threading, every block ends in BRANCH or NOP.end
            let is_empty = block.instrs.len() == 1;
            let last = block.instrs.last_mut().unwrap();
            validate_jump_instr(last);

            if let Op::Branch(op) = &mut last.op {
                match jumps.get(&op.label) {
                    Some(Jump::Branch(label)) => {
                        // A branch label can always be replaced
                        op.label = *label;
                        progress = true;
                    }
                    Some(Jump::End) => {
                        // We can only replace a branch with NOP.end if it's
                        // unconditional.  We can't predicate flow modifiers
                        if op.is_unconditional() {
                            last.op = Op::Nop(OpNop {});
                            last.flow.set_end_shader();
                            progress = true;
                        }
                    }
                    None => (),
                }
                // If the branch target was previously a trivial block then the
                // branch was previously a forward edge (see above) and by
                // invariants 1 and 2 we just updated the branch to target a
                // nontrivial block
            }

            // Disallow threading through loop headers.  While we want to thread
            // forward edges, loop headers need to act as a barrier so we don't
            // end up breaking dominance by having a block jump directly to the
            // inside of a loop.
            if is_empty && !is_loop_header {
                match &last.op {
                    Op::Branch(op) => {
                        // We can only add a branch if it is an unconditional
                        // forward branch.
                        if op.is_unconditional() && labels.contains(&op.label) {
                            jumps.insert(label, Jump::Branch(op.label));
                        }
                    }
                    Op::Nop(_) => {
                        assert!(last.flow.get_end_shader());
                        jumps.insert(label, Jump::End);
                    }
                    _ => panic!("All blocks must end in BRANCH or NOP.end"),
                }
            }

            labels.insert(label);
        }

        if !progress {
            return;
        }

        // We don't update the CFG above, so rewrite it now
        //
        // CFGBuilder takes care of removing dead blocks for us
        // We use the basic block's label to identify it
        let mut cfg = CFGBuilder::<_, _, FxBuildHasher>::new();

        for bi in 0..self.blocks.len() {
            let block = &self.blocks[bi];
            let last = block.instrs.last().unwrap();

            match &last.op {
                Op::Branch(op) => {
                    if !op.is_unconditional() {
                        let succ = self.blocks.succ_indices(bi);
                        assert!(succ.len() == 2);
                        assert!(succ[0] == bi + 1 || succ[1] == bi + 1);
                        cfg.add_edge(block.label, self.blocks[bi + 1].label);
                    }
                    cfg.add_edge(block.label, op.label);
                }
                Op::Nop(_) => {
                    assert!(last.flow.get_end_shader());
                }
                _ => panic!("All blocks must end in BRANCH or NOP.end"),
            }
        }

        for block in self.blocks.drain() {
            cfg.add_node(block.label, block);
        }

        self.blocks = cfg.as_cfg(false);
    }

    /// This pass replaces jumps to the following block with fall-through
    pub fn opt_fall_through(&mut self) {
        for bi in (0..self.blocks.len()).into_iter().rev() {
            // Every block ends in something
            let last = self.blocks[bi].instrs.last().unwrap();
            match &last.op {
                Op::Branch(op) => {
                    let next = self.blocks.get(bi + 1);
                    if next.is_some_and(|next| op.label == next.label) {
                        let branch = self.blocks[bi].instrs.pop().unwrap();
                        assert!(matches!(branch.op, Op::Branch(_)));
                        assert!(branch.flow == FlowCtrl::NONE);
                    }
                }
                Op::Nop(_) => {
                    assert!(last.flow.get_end_shader());
                }
                _ => panic!("All blocks must end in BRANCH or NOP.end"),
            }
        }
    }
}
