// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitset::BitSet;
use crate::depth_first_search::{dfs, DepthFirstSearch};
use std::collections::HashMap;
use std::hash::{BuildHasher, Hash};
use std::iter::{Cloned, Rev};
use std::ops::{Deref, DerefMut, Index, IndexMut};
use std::slice;

/// A [CFG] node
pub struct CFGNode<N> {
    node: N,
    dom: usize,
    dom_pre_idx: usize,
    dom_post_idx: usize,
    lph: usize,
    pred: Vec<usize>,
    succ: Vec<usize>,
}

impl<N> Deref for CFGNode<N> {
    type Target = N;

    fn deref(&self) -> &N {
        &self.node
    }
}

impl<N> DerefMut for CFGNode<N> {
    fn deref_mut(&mut self) -> &mut N {
        &mut self.node
    }
}

struct PostOrderSort {
    post_idx: Vec<usize>,
    count: usize,
}

struct PostOrderSortDFS<'a, N> {
    nodes: &'a [CFGNode<N>],
    sort: PostOrderSort,
}

impl<'a, N> DepthFirstSearch for PostOrderSortDFS<'a, N> {
    type ChildIter = Cloned<Rev<std::slice::Iter<'a, usize>>>;

    fn pre(&mut self, id: usize) -> Self::ChildIter {
        // Reverse the order of the successors so that any successors which are
        // forward edges get descending indices.  This ensures that, in the
        // reverse post order, successors (and their dominated children) come
        // in-order.  In particular, as long as fall-through edges are only ever
        // used for forward edges and the fall-through edge comes first, we
        // guarantee that the fallthrough block comes immediately after its
        // predecessor.
        self.nodes[id].succ.iter().rev().cloned()
    }

    fn post(&mut self, id: usize) {
        self.sort.post_idx[id] = self.sort.count;
        self.sort.count += 1;
    }
}

impl PostOrderSort {
    fn new<N>(nodes: &[CFGNode<N>]) -> Self {
        let mut post_idx: Vec<usize> = Vec::new();
        post_idx.resize(nodes.len(), usize::MAX);

        let mut sort_dfs = PostOrderSortDFS {
            nodes,
            sort: PostOrderSort { post_idx, count: 0 },
        };
        dfs(&mut sort_dfs, 0);

        sort_dfs.sort
    }
}

fn rev_post_order_sort<N>(nodes: &mut Vec<CFGNode<N>>) {
    let sort = PostOrderSort::new(nodes);

    let remap_idx = |i: usize| {
        let pid = sort.post_idx[i];
        if pid == usize::MAX {
            None
        } else {
            assert!(pid < sort.count);
            Some((sort.count - 1) - pid)
        }
    };
    assert!(remap_idx(0) == Some(0));

    // Re-map edges to use post-index numbering
    for n in nodes.iter_mut() {
        let remap_filter_idx = |i: &mut usize| {
            if let Some(r) = remap_idx(*i) {
                *i = r;
                true
            } else {
                false
            }
        };
        n.pred.retain_mut(remap_filter_idx);
        n.succ.retain_mut(remap_filter_idx);
    }

    // We know a priori that each non-MAX post_idx is unique so we can sort the
    // nodes by inserting them into a new array by index.
    let mut sorted: Vec<CFGNode<N>> = Vec::with_capacity(sort.count);
    for (i, n) in nodes.drain(..).enumerate() {
        if let Some(r) = remap_idx(i) {
            unsafe { sorted.as_mut_ptr().add(r).write(n) };
        }
    }
    unsafe { sorted.set_len(sort.count) };

    std::mem::swap(nodes, &mut sorted);
}

fn find_common_dom<N>(
    nodes: &[CFGNode<N>],
    mut a: usize,
    mut b: usize,
) -> usize {
    while a != b {
        while a > b {
            a = nodes[a].dom;
        }
        while b > a {
            b = nodes[b].dom;
        }
    }
    a
}

struct DominanceDFS<'a, N> {
    nodes: &'a mut [CFGNode<N>],
    dom_children: &'a [Vec<usize>],
    count: usize,
}

impl<'a, N> DepthFirstSearch for DominanceDFS<'a, N> {
    type ChildIter = Cloned<std::slice::Iter<'a, usize>>;

    fn pre(&mut self, id: usize) -> Self::ChildIter {
        self.nodes[id].dom_pre_idx = self.count;
        self.count += 1;

        self.dom_children[id].iter().cloned()
    }

    fn post(&mut self, id: usize) {
        self.nodes[id].dom_post_idx = self.count;
        self.count += 1;
    }
}

fn calc_dominance<N>(nodes: &mut Vec<CFGNode<N>>) {
    nodes[0].dom = 0;
    loop {
        let mut changed = false;
        for i in 1..nodes.len() {
            let mut dom = nodes[i].pred[0];
            for p in &nodes[i].pred[1..] {
                if nodes[*p].dom != usize::MAX {
                    dom = find_common_dom(nodes, dom, *p);
                }
            }
            assert!(dom != usize::MAX);
            if nodes[i].dom != dom {
                nodes[i].dom = dom;
                changed = true;
            }
        }

        if !changed {
            break;
        }
    }

    let mut dom_children = Vec::new();
    dom_children.resize(nodes.len(), Vec::new());

    for i in 1..nodes.len() {
        let p = nodes[i].dom;
        if p != i {
            dom_children[p].push(i);
        }
    }

    let mut dom_dfs = DominanceDFS {
        nodes,
        dom_children: &dom_children,
        count: 0,
    };
    dfs(&mut dom_dfs, 0);
    debug_assert!(dom_dfs.count == nodes.len() * 2);
}

struct BackEdgesDFS<'a, N> {
    nodes: &'a [CFGNode<N>],
    pre: BitSet,
    post: BitSet,
    back_edges: Vec<(usize, usize)>,
}

impl<'a, N> DepthFirstSearch for BackEdgesDFS<'a, N> {
    type ChildIter = Cloned<std::slice::Iter<'a, usize>>;

    fn pre(&mut self, id: usize) -> Self::ChildIter {
        self.pre.insert(id);

        self.nodes[id].succ.iter().cloned()
    }

    fn edge(&mut self, parent: usize, child: usize) {
        if self.pre.contains(child) && !self.post.contains(child) {
            self.back_edges.push((parent, child));
        }
    }

    fn post(&mut self, id: usize) {
        self.post.insert(id);
    }
}

fn find_back_edges<N>(nodes: &[CFGNode<N>]) -> Vec<(usize, usize)> {
    let mut be_dfs = BackEdgesDFS {
        nodes,
        pre: Default::default(),
        post: Default::default(),
        back_edges: Default::default(),
    };
    dfs(&mut be_dfs, 0);
    be_dfs.back_edges
}

struct ReachesDFS<'a, N> {
    nodes: &'a [CFGNode<N>],
    stop: usize,
    reaches: BitSet,
}

impl<'a, N> DepthFirstSearch for ReachesDFS<'a, N> {
    type ChildIter = Cloned<std::slice::Iter<'a, usize>>;

    fn pre(&mut self, id: usize) -> Self::ChildIter {
        if id == self.stop || self.reaches.contains(id) {
            return (&[]).iter().cloned();
        }

        self.reaches.insert(id);

        // Since we're trying to find the set of things that reach the start
        // node, not the set of things reachable from the start node, walk
        // predecessors.
        self.nodes[id].pred.iter().cloned()
    }
}

/// Computes the set of nodes that reach the given edge without going through
/// the edge
fn reaches<N>(nodes: &Vec<CFGNode<N>>, edge: (usize, usize)) -> BitSet {
    let mut r_dfs = ReachesDFS {
        nodes,
        stop: edge.1,
        reaches: Default::default(),
    };
    dfs(&mut r_dfs, edge.0);
    r_dfs.reaches
}

fn detect_loops<N>(nodes: &mut Vec<CFGNode<N>>) -> bool {
    let back_edges = find_back_edges(nodes);
    if back_edges.is_empty() {
        return false;
    }

    // Construct a map from nodes N to loop headers H where there is some
    // back-edge (C, H) such that C is reachable from N without going through H.
    // By running the DFS backwards, we can do this in O(B * E) time where B is
    // the number of back-edges and E is the total number of edges.
    let mut loops = BitSet::new();
    let mut node_loops: Vec<BitSet<usize>> = Default::default();
    node_loops.resize_with(nodes.len(), Default::default);
    for (c, h) in back_edges {
        // Stash the loop headers while we're here
        loops.insert(h);

        for n in reaches(nodes, (c, h)).iter() {
            node_loops[n].insert(h);
        }
    }

    for i in 0..nodes.len() {
        debug_assert!(nodes[i].lph == usize::MAX);

        if loops.contains(i) {
            // This is a loop header
            nodes[i].lph = i;
            continue;
        }

        let mut n = i;
        while n != 0 {
            let dom = nodes[n].dom;
            debug_assert!(dom < n);

            if node_loops[i].contains(dom) {
                nodes[i].lph = dom;
                break;
            };

            n = dom;
        }
    }

    true
}

/// A container structure which represents a control-flow graph.  Nodes are
/// automatically sorted and stored in reverse post-DFS order.  This means that
/// iterating over the nodes guarantees that dominators are visited before the
/// nodes they dominate.
pub struct CFG<N> {
    has_loop: bool,
    nodes: Vec<CFGNode<N>>,
}

impl<N> CFG<N> {
    /// Creates a new CFG from nodes and edges.
    pub fn from_blocks_edges(
        nodes: impl IntoIterator<Item = N>,
        edges: impl IntoIterator<Item = (usize, usize)>,
    ) -> Self {
        let mut nodes = Vec::from_iter(nodes.into_iter().map(|n| CFGNode {
            node: n,
            dom: usize::MAX,
            dom_pre_idx: usize::MAX,
            dom_post_idx: 0,
            lph: usize::MAX,
            pred: Vec::new(),
            succ: Vec::new(),
        }));

        for (p, s) in edges {
            nodes[s].pred.push(p);
            nodes[p].succ.push(s);
        }

        rev_post_order_sort(&mut nodes);
        calc_dominance(&mut nodes);
        let has_loop = detect_loops(&mut nodes);

        CFG {
            has_loop: has_loop,
            nodes: nodes,
        }
    }

    /// Returns a reference to the node at the given index.
    pub fn get(&self, idx: usize) -> Option<&N> {
        self.nodes.get(idx).map(|n| &n.node)
    }

    /// Returns a mutable reference to the node at the given index.
    pub fn get_mut(&mut self, idx: usize) -> Option<&mut N> {
        self.nodes.get_mut(idx).map(|n| &mut n.node)
    }

    /// Returns an iterator over the nodes.
    pub fn iter(&self) -> slice::Iter<'_, CFGNode<N>> {
        self.nodes.iter()
    }

    /// Returns a mutable iterator over the nodes.
    pub fn iter_mut(&mut self) -> slice::IterMut<'_, CFGNode<N>> {
        self.nodes.iter_mut()
    }

    /// Returns the number of nodes.
    pub fn len(&self) -> usize {
        self.nodes.len()
    }

    /// Returns the pre-index of the given node in a DFS of the dominance tree.
    pub fn dom_dfs_pre_index(&self, idx: usize) -> usize {
        self.nodes[idx].dom_pre_idx
    }

    /// Returns the post-index of the given node in a DFS of the dominance tree.
    pub fn dom_dfs_post_index(&self, idx: usize) -> usize {
        self.nodes[idx].dom_post_idx
    }

    /// Returns the index to the dominator parent of this node, if any.  If
    /// this is the entry node, `None` is returned.
    pub fn dom_parent_index(&self, idx: usize) -> Option<usize> {
        if idx == 0 {
            None
        } else {
            Some(self.nodes[idx].dom)
        }
    }

    /// Returns true if `parent` dominates `child`.
    pub fn dominates(&self, parent: usize, child: usize) -> bool {
        // If a block is unreachable, then dom_pre_idx == usize::MAX and
        // dom_post_idx == 0.  This allows us to trivially handle unreachable
        // blocks here with zero extra work.
        self.dom_dfs_pre_index(child) >= self.dom_dfs_pre_index(parent)
            && self.dom_dfs_post_index(child) <= self.dom_dfs_post_index(parent)
    }

    /// Returns true if this CFG contains a loop.
    pub fn has_loop(&self) -> bool {
        self.has_loop
    }

    /// Returns true if the given node is a loop header.
    ///
    /// A node H is a loop header if there is a back-edge terminating at H.
    pub fn is_loop_header(&self, idx: usize) -> bool {
        self.nodes[idx].lph == idx
    }

    /// Returns the index of the loop header of the innermost loop containing
    /// this node, if any. If this node is not contained in any loops, `None`
    /// is returned.
    ///
    /// A node N is considered to be contained to be contained in the loop with
    /// header H if both of the following are true:
    ///
    ///  1. H dominates N
    ///
    ///  2. There is a back-edge (C, H) in the CFG such that C is reachable
    ///     from N without going through H.
    ///
    /// This matches the definitions given in
    /// https://www.cs.cornell.edu/courses/cs4120/2023sp/notes.html?id=cflow
    pub fn loop_header_index(&self, idx: usize) -> Option<usize> {
        let lph = self.nodes[idx].lph;
        if lph == usize::MAX {
            None
        } else {
            debug_assert!(self.is_loop_header(lph));
            Some(lph)
        }
    }

    /// Returns the loop depth of the given node.  Nodes not in any loops have
    /// a loop depth of zero.  For nodes inside a loop, this is the count of
    /// number of loop headers above them in the dominance tree.
    pub fn loop_depth(&self, idx: usize) -> usize {
        let mut idx = idx;
        let mut depth = 0;
        loop {
            let lph = self.nodes[idx].lph;
            if lph == usize::MAX {
                return depth;
            }

            depth += 1;

            // Loop headers have themselves as the lph so we need to skip to
            // the dominator of the loop header for the next iteration.
            idx = self.nodes[lph].dom;
        }
    }

    /// Returns the indices of the successors of this node in the CFG.
    pub fn succ_indices(&self, idx: usize) -> &[usize] {
        &self.nodes[idx].succ[..]
    }

    /// Returns the indices of the predecessors of this node in the CFG.
    pub fn pred_indices(&self, idx: usize) -> &[usize] {
        &self.nodes[idx].pred[..]
    }

    /// Drains the CFG and returns an iterator over the node data.
    pub fn drain(&mut self) -> impl Iterator<Item = N> + '_ {
        self.has_loop = false;
        self.nodes.drain(..).map(|n| n.node)
    }
}

impl<N> Index<usize> for CFG<N> {
    type Output = N;

    fn index(&self, idx: usize) -> &N {
        &self.nodes[idx].node
    }
}

impl<N> IndexMut<usize> for CFG<N> {
    fn index_mut(&mut self, idx: usize) -> &mut N {
        &mut self.nodes[idx].node
    }
}

impl<'a, N> IntoIterator for &'a CFG<N> {
    type Item = &'a CFGNode<N>;
    type IntoIter = slice::Iter<'a, CFGNode<N>>;

    fn into_iter(self) -> slice::Iter<'a, CFGNode<N>> {
        self.iter()
    }
}

impl<'a, N> IntoIterator for &'a mut CFG<N> {
    type Item = &'a mut CFGNode<N>;
    type IntoIter = slice::IterMut<'a, CFGNode<N>>;

    fn into_iter(self) -> slice::IterMut<'a, CFGNode<N>> {
        self.iter_mut()
    }
}

/// A structure for building a [CFG].
///
/// Building a control-flow graph often involves mapping some preexisting data
/// structure (such as block indices another CFG) onto nodes in the new CFG.
/// `CFGBuilder` makes all that automatic by letting you add nodes and edges
/// using any key type desired.  You then call `as_cfg()` to get the final
/// control-flow graph.
pub struct CFGBuilder<K, N, H: BuildHasher + Default> {
    nodes: Vec<N>,
    edges: Vec<(K, K)>,
    key_map: HashMap<K, usize, H>,
}

impl<K, N, H: BuildHasher + Default> CFGBuilder<K, N, H> {
    /// Creates a new CFG builder.
    pub fn new() -> Self {
        CFGBuilder {
            nodes: Vec::new(),
            edges: Vec::new(),
            key_map: Default::default(),
        }
    }
}

impl<K: Eq + Hash, N, H: BuildHasher + Default> CFGBuilder<K, N, H> {
    /// Adds a node to the CFG.
    pub fn add_node(&mut self, k: K, n: N) {
        self.key_map.insert(k, self.nodes.len());
        self.nodes.push(n);
    }

    /// Adds an edge to the CFG.
    pub fn add_edge(&mut self, s: K, p: K) {
        self.edges.push((s, p));
    }

    /// Destroys this builder and returns a CFG.
    pub fn as_cfg(mut self) -> CFG<N> {
        let edges = self.edges.drain(..).map(|(s, p)| {
            let s = *self.key_map.get(&s).unwrap();
            let p = *self.key_map.get(&p).unwrap();
            (s, p)
        });
        CFG::from_blocks_edges(self.nodes, edges)
    }
}

impl<K, N, H: BuildHasher + Default> Default for CFGBuilder<K, N, H> {
    fn default() -> Self {
        CFGBuilder::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::hash::RandomState;

    fn test_loop_nesting(edges: &[(usize, usize)], expected: &[Option<usize>]) {
        let mut builder = CFGBuilder::<_, _, RandomState>::new();
        for i in 0..expected.len() {
            builder.add_node(i, i);
        }
        for &(a, b) in edges {
            builder.add_edge(a, b);
        }
        let cfg = builder.as_cfg();

        let mut lphs = Vec::new();
        lphs.resize(expected.len(), None);
        for (i, idx) in cfg.iter().enumerate() {
            let lph = cfg.loop_header_index(i);
            lphs[*idx.deref()] = lph.map(|h| cfg[h]);
        }

        assert_eq!(&lphs, expected);
    }

    #[test]
    fn test_loop_simple() {
        // block 0
        // loop {
        //     block 1
        //     if ... {
        //         block 2
        //         break;
        //     }
        //     block 3
        // }
        // block 4
        test_loop_nesting(
            &[(0, 1), (1, 2), (1, 3), (2, 4), (3, 1)],
            &[None, Some(1), None, Some(1), None, None],
        );
    }

    #[test]
    fn test_loop_simple_nested() {
        // loop {
        //     block 0
        //     loop {
        //         block 1
        //         if ... {
        //             block 2
        //             break;
        //         }
        //         block 3
        //     }
        //     block 4
        //     if ... {
        //         block 5
        //         break;
        //     }
        //     block 6
        // }
        // block 7
        test_loop_nesting(
            &[
                (0, 1),
                (1, 2),
                (1, 3),
                (2, 4),
                (3, 1),
                (4, 5),
                (4, 6),
                (5, 7),
                (6, 0),
            ],
            &[
                Some(0),
                Some(1),
                Some(0),
                Some(1),
                Some(0),
                None,
                Some(0),
                None,
            ],
        );
    }

    #[test]
    fn test_loop_two_continues() {
        // loop {
        //     block 0
        //     if ... {
        //         block 1
        //         continue;
        //     }
        //     block 2
        //     if ... {
        //         block 3
        //         break;
        //     }
        //     block 4
        // }
        // block 5
        test_loop_nesting(
            &[(0, 1), (0, 2), (1, 0), (2, 3), (2, 4), (3, 5), (4, 0)],
            &[Some(0), Some(0), Some(0), None, Some(0), None],
        );
    }

    #[test]
    fn test_loop_two_breaks() {
        // loop {
        //     block 0
        //     if ... {
        //         block 1
        //         break;
        //     }
        //     block 2
        //     if ... {
        //         block 3
        //         break;
        //     }
        //     block 4
        // }
        // block 5
        test_loop_nesting(
            &[(0, 1), (0, 2), (1, 5), (2, 3), (2, 4), (3, 5), (4, 0)],
            &[Some(0), None, Some(0), None, Some(0), None],
        );
    }

    #[test]
    fn test_loop_predicated_continue() {
        // loop {
        //     block 0
        //     continue_if(...);
        //     block 1
        //     if ... {
        //         block 2
        //         break;
        //     }
        //     block 3
        // }
        // block 4
        test_loop_nesting(
            &[(0, 0), (0, 1), (1, 2), (1, 3), (2, 4), (3, 0)],
            &[Some(0), Some(0), None, Some(0), None],
        );
    }

    #[test]
    fn test_loop_predicated_break() {
        // block 0
        // loop {
        //     block 1
        //     break_if(...);
        //     block 2
        //     if ... {
        //         block 3
        //         break;
        //     }
        //     block 4
        // }
        // block 5
        test_loop_nesting(
            &[(0, 1), (1, 2), (1, 5), (2, 3), (2, 4), (3, 5), (4, 1)],
            &[None, Some(1), Some(1), None, Some(1), None],
        );
    }

    #[test]
    fn test_loop_complex() {
        // loop {
        //     block 0
        //     loop {
        //         block 1
        //         if ... {
        //             block 2
        //             break;
        //         }
        //         block 3
        //     }
        //     loop {
        //         block 4
        //         break_if(..);
        //     }
        //     block 5
        //     if ... {
        //         block 6
        //         break;
        //     }
        //     block 7
        // }
        // block 8
        test_loop_nesting(
            &[
                (0, 1),
                (1, 2),
                (1, 3),
                (2, 4),
                (3, 1),
                (4, 5),
                (4, 4),
                (5, 6),
                (5, 7),
                (6, 8),
                (7, 0),
            ],
            &[
                Some(0),
                Some(1),
                Some(0),
                Some(1),
                Some(4),
                Some(0),
                None,
                Some(0),
                None,
            ],
        );
    }

    #[test]
    fn test_simple_irreducible() {
        // block 0
        // if ... {
        //     block 1
        // }
        // block 2
        // if ... {
        //     block 3
        // }
        // block 4
        test_loop_nesting(
            &[(0, 1), (0, 2), (1, 2), (2, 3), (2, 4), (3, 4)],
            &[None, None, None, None, None, None],
        );
    }

    #[test]
    fn test_loop_irreducible() {
        // block 0
        // goto_if(...) label;
        // loop {
        //     block 1
        //     break_if(...);
        // label:
        //     block 2
        // }
        // block 3
        test_loop_nesting(
            &[(0, 1), (0, 2), (1, 3), (1, 2), (2, 1)],
            &[None, Some(1), None, None, None, None],
        );
    }
}
