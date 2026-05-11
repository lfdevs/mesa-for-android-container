// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::ops::Deref;
use std::ops::DerefMut;

pub trait AsSlice<T> {
    type Attr;

    fn as_slice(&self) -> &[T];
    fn as_mut_slice(&mut self) -> &mut [T];
    fn attrs(&self) -> &'static [Self::Attr];
}

impl<T, A> AsSlice<T> for Box<A>
where
    A: AsSlice<T>,
{
    type Attr = A::Attr;

    fn as_slice(&self) -> &[T] {
        self.deref().as_slice()
    }
    fn as_mut_slice(&mut self) -> &mut [T] {
        self.deref_mut().as_mut_slice()
    }
    fn attrs(&self) -> &'static [Self::Attr] {
        self.deref().attrs()
    }
}
