// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::iter::Zip;
use std::slice;

pub struct VecPair<A, B> {
    a: Vec<A>,
    b: Vec<B>,
}

impl<A, B> VecPair<A, B> {
    pub fn a_as_slice(&self) -> &[A] {
        &self.a
    }

    pub fn a_as_mut_slice(&mut self) -> &mut [A] {
        &mut self.a
    }

    pub fn b_as_slice(&self) -> &[B] {
        &self.b
    }

    pub fn b_as_mut_slice(&mut self) -> &mut [B] {
        &mut self.b
    }

    pub fn append(&mut self, other: &mut VecPair<A, B>) {
        self.a.append(&mut other.a);
        self.b.append(&mut other.b);
    }

    pub fn is_empty(&self) -> bool {
        debug_assert!(self.a.len() == self.b.len());
        self.a.is_empty()
    }

    pub fn iter(&self) -> Zip<slice::Iter<'_, A>, slice::Iter<'_, B>> {
        debug_assert!(self.a.len() == self.b.len());
        self.a.iter().zip(self.b.iter())
    }

    pub fn iter_mut(
        &mut self,
    ) -> Zip<slice::IterMut<'_, A>, slice::IterMut<'_, B>> {
        debug_assert!(self.a.len() == self.b.len());
        self.a.iter_mut().zip(self.b.iter_mut())
    }

    pub fn len(&self) -> usize {
        debug_assert!(self.a.len() == self.b.len());
        self.a.len()
    }

    pub fn new() -> Self {
        Self {
            a: Vec::new(),
            b: Vec::new(),
        }
    }

    pub fn push(&mut self, a: A, b: B) {
        debug_assert!(self.a.len() == self.b.len());
        self.a.push(a);
        self.b.push(b);
    }
}

impl<A: Clone, B: Clone> VecPair<A, B> {
    pub fn retain(&mut self, mut f: impl FnMut(&A, &B) -> bool) {
        debug_assert!(self.a.len() == self.b.len());
        let len = self.a.len();
        let mut i = 0_usize;
        while i < len {
            if !f(&self.a[i], &self.b[i]) {
                break;
            }
            i += 1;
        }

        let mut new_len = i;

        // Don't check this one twice.
        i += 1;

        while i < len {
            // This could be more efficient but it's good enough for our
            // purposes since everything we're storing is small and has a
            // trivial Drop.
            if f(&self.a[i], &self.b[i]) {
                self.a[new_len] = self.a[i].clone();
                self.b[new_len] = self.b[i].clone();
                new_len += 1;
            }
            i += 1;
        }

        if new_len < len {
            self.a.truncate(new_len);
            self.b.truncate(new_len);
        }
    }
}
