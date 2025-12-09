// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitset::BitSet;

/// A trait implementing a depth-first search over a graph
pub trait DepthFirstSearch {
    type ChildIter;

    /// Pre-visit a node.  This is called before any children or edges are
    /// visited.  Returns an iterator to this node's children in the graph.
    fn pre(&mut self, id: usize) -> Self::ChildIter;

    /// Visit an edge.  An edge is visited before the child at the end of that
    /// edge is pre-visited.  Every edge is visited, even if if the node this
    /// edge points to has already been visited.
    fn edge(&mut self, _parent: usize, _child: usize) {
        // Does nothing by default
    }

    /// Post-visit a node.  This is called after all the children have been
    /// visited.
    fn post(&mut self, _id: usize) {
        // Does nothing by default
    }
}

struct DFSEntry<I: Iterator<Item = usize>> {
    id: usize,
    children: I,
}

pub fn dfs<I, D>(dfs: &mut D, start: usize)
where
    I: Iterator<Item = usize>,
    D: DepthFirstSearch<ChildIter = I>,
{
    let mut seen = BitSet::new();
    let mut stack = Vec::new();

    seen.insert(start);
    let children = dfs.pre(start);

    stack.push(DFSEntry {
        id: start,
        children,
    });
    loop {
        let Some(entry) = stack.last_mut() else {
            break;
        };

        loop {
            if let Some(id) = entry.children.next() {
                dfs.edge(entry.id, id);

                if seen.contains(id) {
                    continue;
                }

                seen.insert(id);
                let children = dfs.pre(id);

                stack.push(DFSEntry { id, children });
            } else {
                dfs.post(entry.id);
                stack.pop();
            }

            // We're only looping here because we want to make the
            // seen.contains() case a fast path.  Both the case where we
            // push the stack and recurse into a child or when we pop the
            // stack want to continue in the next iteration of the outer
            // loop.
            break;
        }
    }
}
