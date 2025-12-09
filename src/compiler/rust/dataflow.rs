// Copyright © 2025 Valve Corporation
// SPDX-License-Identifier: MIT

//! Dataflow analysis
//!
//! This module contains helpers for solving dataflow problems. A dataflow
//! problem is characterized by a "transfer" function, which updates information
//! based on a single block, and a "join" function, which updates information
//! along a control flow edge. See the wikipedia article for more information on
//! this terminology.
//! https://en.wikipedia.org/wiki/Data-flow_analysis#Basic_principles

use crate::bitset::BitSet;
use crate::cfg::CFG;
use std::collections::VecDeque;

/// A FIFO where each item is unique
#[derive(Default)]
struct FIFOSet {
    vec_deque: VecDeque<usize>,
    bit_set: BitSet,
}

impl FIFOSet {
    fn push_back(&mut self, x: usize) {
        if self.bit_set.insert(x) {
            self.vec_deque.push_back(x);
        }
    }

    fn pop_front(&mut self) -> Option<usize> {
        let out = self.vec_deque.pop_front();
        if let Some(x) = out {
            let exists = self.bit_set.remove(x);
            debug_assert!(exists);
        }
        out
    }
}

pub struct BackwardDataflow<'a, Block, BlockIn, BlockOut, Transfer, Join>
where
    Transfer: FnMut(usize, &Block, &mut BlockIn, &BlockOut) -> bool,
    Join: FnMut(&mut BlockOut, &BlockIn),
{
    pub cfg: &'a CFG<Block>,
    pub block_in: &'a mut [BlockIn],
    pub block_out: &'a mut [BlockOut],

    /// Generate the block input from the block's output
    ///
    /// Returns true if block_in has changed, false otherwise
    pub transfer: Transfer,

    /// Update the block output based on a successor's input
    pub join: Join,
}

impl<'a, Block, BlockIn, BlockOut, Transfer, Join>
    BackwardDataflow<'a, Block, BlockIn, BlockOut, Transfer, Join>
where
    Transfer: FnMut(usize, &Block, &mut BlockIn, &BlockOut) -> bool,
    Join: FnMut(&mut BlockOut, &BlockIn),
{
    fn transfer(&mut self, block_idx: usize) -> bool {
        (self.transfer)(
            block_idx,
            &self.cfg[block_idx],
            &mut self.block_in[block_idx],
            &self.block_out[block_idx],
        )
    }

    fn join(&mut self, pred_idx: usize, block_idx: usize) {
        (self.join)(&mut self.block_out[pred_idx], &self.block_in[block_idx]);
    }

    /// Solve the dataflow problem and generate output for each block
    pub fn solve(mut self) {
        let num_blocks = self.cfg.len();
        assert_eq!(num_blocks, self.block_in.len());
        assert_eq!(num_blocks, self.block_out.len());

        let mut worklist = FIFOSet::default();

        // Perform an initial pass over the data
        for block_idx in (0..num_blocks).rev() {
            self.transfer(block_idx);

            for &pred_idx in self.cfg.pred_indices(block_idx) {
                // On the first iteration, we unconditionally join so that the
                // join operator is called at least once for each edge
                self.join(pred_idx, block_idx);
                if pred_idx >= block_idx {
                    // Otherwise we're about to process it in the first pass
                    worklist.push_back(pred_idx);
                }
            }
        }

        // Process the worklist
        while let Some(block_idx) = worklist.pop_front() {
            let changed = self.transfer(block_idx);
            if changed {
                for &pred_idx in self.cfg.pred_indices(block_idx) {
                    self.join(pred_idx, block_idx);
                    worklist.push_back(pred_idx);
                }
            }
        }
    }
}

pub struct ForwardDataflow<'a, Block, BlockIn, BlockOut, Transfer, Join>
where
    Transfer: FnMut(usize, &Block, &mut BlockOut, &BlockIn) -> bool,
    Join: FnMut(&mut BlockIn, &BlockOut),
{
    pub cfg: &'a CFG<Block>,
    pub block_in: &'a mut [BlockIn],
    pub block_out: &'a mut [BlockOut],

    /// Generate the block output from the block's input
    ///
    /// Returns true if block_out has changed, false otherwise
    pub transfer: Transfer,

    /// Update the block input based on a predecessor's output
    pub join: Join,
}

impl<'a, Block, BlockIn, BlockOut, Transfer, Join>
    ForwardDataflow<'a, Block, BlockIn, BlockOut, Transfer, Join>
where
    Transfer: FnMut(usize, &Block, &mut BlockOut, &BlockIn) -> bool,
    Join: FnMut(&mut BlockIn, &BlockOut),
{
    fn transfer(&mut self, block_idx: usize) -> bool {
        (self.transfer)(
            block_idx,
            &self.cfg[block_idx],
            &mut self.block_out[block_idx],
            &self.block_in[block_idx],
        )
    }

    fn join(&mut self, succ_idx: usize, block_idx: usize) {
        (self.join)(&mut self.block_in[succ_idx], &self.block_out[block_idx]);
    }

    /// Solve the dataflow problem and generate output for each block
    pub fn solve(mut self) {
        let num_blocks = self.cfg.len();
        assert_eq!(num_blocks, self.block_in.len());
        assert_eq!(num_blocks, self.block_out.len());

        let mut worklist = FIFOSet::default();

        // Perform an initial pass over the data
        for block_idx in 0..num_blocks {
            self.transfer(block_idx);

            for &succ_idx in self.cfg.succ_indices(block_idx) {
                // On the first iteration, we unconditionally join so that the
                // join operator is called at least once for each edge
                self.join(succ_idx, block_idx);
                if succ_idx <= block_idx {
                    // Otherwise we're about to process it in the first pass
                    worklist.push_back(succ_idx);
                }
            }
        }

        // Process the worklist
        while let Some(block_idx) = worklist.pop_front() {
            let changed = self.transfer(block_idx);
            if changed {
                for &succ_idx in self.cfg.succ_indices(block_idx) {
                    self.join(succ_idx, block_idx);
                    worklist.push_back(succ_idx);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bitset::BitSet;
    use crate::cfg::CFGBuilder;
    use std::hash::RandomState;

    fn check_graph_reachability(
        edges: &[(usize, usize)],
        expected: &[&[usize]],
    ) {
        let mut builder = CFGBuilder::<_, _, RandomState>::new();
        for (i, expected) in expected.iter().enumerate() {
            builder.add_node(i, (i, expected));
        }
        for &(a, b) in edges {
            builder.add_edge(a, b);
        }
        let cfg = builder.as_cfg();

        let mut reachable_in: Vec<BitSet> =
            (0..cfg.len()).map(|_| Default::default()).collect();
        let mut reachable_out: Vec<BitSet> =
            (0..cfg.len()).map(|_| Default::default()).collect();
        BackwardDataflow {
            cfg: &cfg,
            block_in: &mut reachable_in[..],
            block_out: &mut reachable_out[..],
            transfer: |_block_idx, block, live_in, live_out| {
                // The dataflow framework guarantees that each block and edge is
                // processed at least once, so we don't need to record whether
                // this first insert changed anything or not
                live_in.insert(block.0);

                live_in.union_with(live_out.s(..))
            },
            join: |live_out, succ_live_in| {
                *live_out |= succ_live_in.s(..);
            },
        }
        .solve();

        for (node, reachable_out) in cfg.iter().zip(reachable_out.iter()) {
            let (_i, expected) = **node;
            let r: Vec<usize> = reachable_out.iter().collect();
            assert_eq!(*expected, &r[..]);
        }
    }

    #[test]
    fn test_if_else() {
        check_graph_reachability(
            &[(0, 1), (0, 2), (1, 3), (2, 3)],
            &[&[1, 2, 3], &[3], &[3], &[]],
        );
    }

    #[test]
    fn test_loop() {
        check_graph_reachability(
            &[(0, 1), (1, 2), (2, 3), (2, 1)],
            &[&[1, 2, 3], &[1, 2, 3], &[1, 2, 3], &[]],
        );
    }

    #[test]
    fn test_irreducible() {
        check_graph_reachability(
            &[(0, 1), (0, 2), (1, 2), (2, 1), (1, 3), (2, 3)],
            &[&[1, 2, 3], &[1, 2, 3], &[1, 2, 3], &[]],
        );
    }

    fn check_graph_origin(edges: &[(usize, usize)], expected: &[&[usize]]) {
        let mut builder = CFGBuilder::<_, _, RandomState>::new();
        for (i, expected) in expected.iter().enumerate() {
            builder.add_node(i, (i, expected));
        }
        for &(a, b) in edges {
            builder.add_edge(a, b);
        }
        let cfg = builder.as_cfg();

        let mut reachable_in: Vec<BitSet> =
            (0..cfg.len()).map(|_| Default::default()).collect();
        let mut reachable_out: Vec<BitSet> =
            (0..cfg.len()).map(|_| Default::default()).collect();
        ForwardDataflow {
            cfg: &cfg,
            block_in: &mut reachable_in[..],
            block_out: &mut reachable_out[..],
            transfer: |_block_idx, block, live_out, live_in| {
                // The dataflow framework guarantees that each block and edge is
                // processed at least once, so we don't need to record whether
                // this first insert changed anything or not
                live_out.insert(block.0);

                live_out.union_with(live_in.s(..))
            },
            join: |live_in, pred_live_out| {
                *live_in |= pred_live_out.s(..);
            },
        }
        .solve();

        for (node, reachable_in) in cfg.iter().zip(reachable_in.iter()) {
            let (_i, expected) = **node;
            let r: Vec<usize> = reachable_in.iter().collect();
            assert_eq!(*expected, &r[..]);
        }
    }

    #[test]
    fn test_fw_if_else() {
        check_graph_origin(
            &[(0, 1), (0, 2), (1, 3), (2, 3)],
            &[&[], &[0], &[0], &[0, 1, 2]],
        );
    }

    #[test]
    fn test_fw_loop() {
        check_graph_origin(
            &[(0, 1), (1, 2), (2, 3), (2, 1)],
            &[&[], &[0, 1, 2], &[0, 1, 2], &[0, 1, 2]],
        );
    }

    #[test]
    fn test_fw_irreducible() {
        check_graph_origin(
            &[(0, 1), (0, 2), (1, 2), (2, 1), (1, 3), (2, 3)],
            &[&[], &[0, 1, 2], &[0, 1, 2], &[0, 1, 2]],
        );
    }

    fn check_max_iter_count(edges: &[(usize, usize)], expected: &[u32]) {
        let mut builder = CFGBuilder::<_, _, RandomState>::new();
        for (i, expected) in expected.iter().enumerate() {
            builder.add_node(i, (i, expected));
        }
        for &(a, b) in edges {
            builder.add_edge(a, b);
        }
        let cfg = builder.as_cfg();

        let mut iter_in: Vec<u32> = vec![0; cfg.len()];
        let mut iter_out: Vec<u32> = vec![0; cfg.len()];
        // This unusual data-flow analysis counts how many cfg-blocks
        // are visited, there is a hard limit of 5 to allow for convergence
        ForwardDataflow {
            cfg: &cfg,
            block_in: &mut iter_in[..],
            block_out: &mut iter_out[..],
            transfer: |_block_idx, _block, it_out, it_in| {
                let new_it = (*it_in + 1).min(5);

                let is_diff = new_it != *it_out;
                *it_out = new_it;
                is_diff
            },
            join: |live_in, pred_live_out| {
                *live_in = (*live_in).max(*pred_live_out)
            },
        }
        .solve();

        for (node, iter_in) in cfg.iter().zip(iter_in.iter()) {
            let (_i, expected) = **node;
            assert_eq!(*expected, *iter_in);
        }
    }

    #[test]
    fn test_max_iter_while() {
        // This test fails if same-node back-edges are not accounted for
        check_max_iter_count(&[(0, 1), (1, 2), (1, 1)], &[0, 5, 5]);
    }
}
