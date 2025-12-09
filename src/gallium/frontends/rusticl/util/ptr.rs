// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use std::{
    alloc::Layout,
    collections::{
        btree_map::{Entry, Values, ValuesMut},
        BTreeMap,
    },
    hash::{Hash, Hasher},
    mem,
    ops::{Add, Deref},
    ptr::NonNull,
};

/// A wrapper around pointers to C data type which are considered thread safe.
#[derive(Copy, Clone, Eq)]
#[repr(transparent)]
pub struct ThreadSafeCPtr<T>(NonNull<T>);

impl<T> ThreadSafeCPtr<T> {
    /// # Safety
    ///
    /// Only safe on `T` which are thread-safe C data types. That usually means the following:
    /// * Fields are accessed in a thread-safe manner, either through atomic operations or
    ///   functions
    /// * Bugs and Data races caused by accessing the type in multiple threads is considered a bug.
    ///
    /// As nothing of this can actually be verified this solely relies on contracts made on those
    /// types, either by a specification or by convention. In practical terms this means that a
    /// pointer to `T` meets all requirements expected by [Send] and [Sync]
    pub unsafe fn new(ptr: *mut T) -> Option<Self> {
        Some(Self(NonNull::new(ptr)?))
    }
}

impl<T> Deref for ThreadSafeCPtr<T> {
    type Target = NonNull<T>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T> Hash for ThreadSafeCPtr<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.as_ptr().hash(state)
    }
}

impl<T> PartialEq for ThreadSafeCPtr<T> {
    fn eq(&self, other: &Self) -> bool {
        self.0.as_ptr() == other.0.as_ptr()
    }
}

// SAFETY: safety requierements of Send fullfilled at [ThreadSafeCPtr::new] time
unsafe impl<T> Send for ThreadSafeCPtr<T> {}

// SAFETY: safety requierements of Sync fullfilled at [ThreadSafeCPtr::new] time
unsafe impl<T> Sync for ThreadSafeCPtr<T> {}

pub trait BetterPointer<T> {
    /// Reads the pointer and advances it by 1 element.
    ///
    /// # Safety
    ///
    /// `self` needs to be valid for reading.
    unsafe fn read_and_advance(&mut self) -> T;
}

impl<T> BetterPointer<T> for *const T {
    unsafe fn read_and_advance(&mut self) -> T {
        let val = unsafe { self.read() };
        *self = unsafe { self.add(1) };
        val
    }
}

pub trait CheckedPtr<T> {
    /// Copies `count * size_of::<T>()` bytes from `src` to `self`. The source
    /// and destination may overlap.
    ///
    /// # Safety
    ///
    /// The nullity of `self` is checked. `self` and `src` must fulfill all
    /// other invariants of [`std::ptr::copy`].
    unsafe fn copy_from_checked(self, src: *const T, count: usize);

    /// Overwrites a memory location with the given value without reading or
    /// dropping the old value.
    ///
    /// # Safety
    ///
    /// The nullity of `self` is checked. `self` must fulfill all other
    /// invariants of [`std::ptr::write`].
    unsafe fn write_checked(self, val: T);
}

impl<T> CheckedPtr<T> for *mut T {
    unsafe fn copy_from_checked(self, src: *const T, count: usize) {
        if !self.is_null() {
            // SAFETY: Caller is responsible for satisfying all invariants save
            // pointer nullity.
            unsafe {
                self.copy_from(src, count);
            }
        }
    }

    unsafe fn write_checked(self, val: T) {
        if !self.is_null() {
            // SAFETY: Caller is responsible for satisfying all invariants save
            // pointer nullity.
            unsafe {
                self.write(val);
            }
        }
    }
}

// Adapted from libstd since std::ptr::is_aligned_to is still unstable
// See https://github.com/rust-lang/rust/issues/96284
#[must_use]
#[inline]
pub fn is_aligned_to<T>(ptr: *const T, align: usize) -> bool {
    addr(ptr) & (align - 1) == 0
}

// Adapted from libstd since std::ptr::addr isn't stable until 1.84.0
// See https://github.com/rust-lang/rust/issues/95228
#[must_use]
#[inline(always)]
pub fn addr<T>(ptr: *const T) -> usize {
    // The libcore implementations of `addr` and `expose_addr` suggest that, while both transmuting
    // and casting to usize will give you the address of a ptr in the end, they are not identical
    // in their side-effects.
    // A cast "exposes" a ptr, which can potentially cause the compiler to optimize less
    // aggressively around it.
    // Let's trust the libcore devs over clippy on whether a transmute also exposes a ptr.
    #[allow(clippy::transmutes_expressible_as_ptr_casts)]
    // SAFETY: Pointer-to-integer transmutes are valid outside of const contexts (if you are okay
    // with losing the provenance).
    unsafe {
        mem::transmute(ptr.cast::<()>())
    }
}

pub trait AllocSize<P> {
    fn size(&self) -> P;
}

impl AllocSize<usize> for Layout {
    fn size(&self) -> usize {
        Self::size(self)
    }
}

pub struct TrackedPointers<P, T: AllocSize<P>> {
    ptrs: BTreeMap<P, T>,
}

impl<P, T: AllocSize<P>> TrackedPointers<P, T> {
    pub fn new() -> Self {
        Self {
            ptrs: BTreeMap::new(),
        }
    }

    pub fn values(&self) -> Values<'_, P, T> {
        self.ptrs.values()
    }

    pub fn values_mut(&mut self) -> ValuesMut<'_, P, T> {
        self.ptrs.values_mut()
    }
}

impl<P, T: AllocSize<P>> TrackedPointers<P, T>
where
    P: Ord + Add<Output = P> + Copy,
{
    pub fn contains_key(&self, ptr: P) -> bool {
        self.ptrs.contains_key(&ptr)
    }

    pub fn entry(&mut self, ptr: P) -> Entry<'_, P, T> {
        self.ptrs.entry(ptr)
    }

    pub fn find_alloc(&self, ptr: P) -> Option<(P, &T)> {
        if let Some((&base, val)) = self.ptrs.range(..=ptr).next_back() {
            let size = val.size();
            // we check if ptr is within [base..base+size)
            // means we can check if ptr - (base + size) < 0
            if ptr < (base + size) {
                return Some((base, val));
            }
        }
        None
    }

    pub fn find_alloc_mut(&mut self, ptr: P) -> Option<(P, &mut T)> {
        if let Some((&base, val)) = self.ptrs.range_mut(..=ptr).next_back() {
            let size = val.size();
            // we check if ptr is within [base..base+size)
            // means we can check if ptr - (base + size) < 0
            if ptr < (base + size) {
                return Some((base, val));
            }
        }
        None
    }

    pub fn find_alloc_precise(&self, ptr: P) -> Option<&T> {
        self.ptrs.get(&ptr)
    }

    pub fn insert(&mut self, ptr: P, val: T) -> Option<T> {
        self.ptrs.insert(ptr, val)
    }

    pub fn remove(&mut self, ptr: P) -> Option<T> {
        self.ptrs.remove(&ptr)
    }
}

impl<P, T: AllocSize<P>> Default for TrackedPointers<P, T> {
    fn default() -> Self {
        Self::new()
    }
}
