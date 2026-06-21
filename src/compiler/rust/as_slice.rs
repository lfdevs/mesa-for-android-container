// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::ops::Deref;
use std::ops::DerefMut;
use std::ops::Range;

/// A trait which indicates that the given type may be viewed as a slice of
/// (some of) its fields.
pub trait AsSlice<T> {
    /// The type of field attributes.  If no field attributes are needed, this
    /// can be `()`.
    type Attr;

    /// Returns a slice representing the fields of type `T`.
    fn as_slice(&self) -> &[T];

    /// Returns a mut slice representing the fields of type `T`.
    fn as_mut_slice(&mut self) -> &mut [T];

    /// Returns a slice containing the field attributes for fields of type T.
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

/// A trait which indicates that the given type may be viewed as an array of
/// (some of) its children.  This is a stronger version of `AsSlice` which is
/// usable from const contexts.
///
/// It also provides methods for determining when a pointer pointers to one
/// of the array's elements.  Thes methods are able to operate without
/// dereferencing anything and so can even be used on dangling pointers.
///
/// If `AsSlice<T>` is implemented on a struct using the derive macro in the
/// proc crate, `AsArray<T>` is also implemented as a side-effect.  This only
/// applies to struct types.  Enum types may not have a consistant offset and
/// element count across variants.
///
/// SAFETY:
///
/// This trait assumes the following:
///
///  1. `ARRAY_OFFSET <= size_of::<T>()`
///  2. `ARRAY_OFFSET % align_of::<T>() == 0`
///  3. That, given any valid `p: *Self`, `p.byte_offset(ARRAY_OFFSET)` is a
///     valid pointer to a member of `Self` of type `T` and that the next
///     `N - 1` members of `Self`, in memory order, re also of type `T`.
///
/// Because there is no way for the compiler to verify this, AsArray is marked
/// as an unsafe trait.
pub unsafe trait AsArray<T, const N: usize> {
    /// The type of field attributes.  If no field attributes are needed, this
    /// can be `()`.
    type Attr;

    /// An array containing the field attributes
    const ATTRS: [Self::Attr; N];

    /// The offset, in bytes, from `Self` to the start of the array
    const ARRAY_OFFSET: usize;

    /// Returns the fields of type `T` as an array
    fn as_array(&self) -> &[T; N] {
        unsafe {
            let ptr = self as *const Self;
            let ptr = ptr.byte_offset(Self::ARRAY_OFFSET.try_into().unwrap());
            // TODO: Use as_ref_unchecked() when we update to Rust 1.95.0
            (ptr as *const [T; N]).as_ref().unwrap()
        }
    }

    /// Returns the fields of type `T` as a `mut` array
    fn as_mut_array(&mut self) -> &mut [T; N] {
        unsafe {
            let ptr = self as *mut Self;
            let ptr = ptr.byte_offset(Self::ARRAY_OFFSET.try_into().unwrap());
            // TODO: Use as_mut_unchecked() when we update to Rust 1.95.0
            (ptr as *mut [T; N]).as_mut().unwrap()
        }
    }

    /// Returns a pointer range representing the the fields of type `T`.  This
    /// method is guaranteed to never dereference `base`.
    ///
    /// SAFETY:
    ///
    /// While we guarantee that `base` is never dereferenced, we do require
    /// that `base` be an otherwise valid pointer to `Self`.  In particular, it
    /// must be non-null, satisfy the alignment requirements of `Self`, and
    /// `base.addr() + (size_of::<Self>() - 1)` must not overflow`.
    unsafe fn as_ptr_range(base: *const Self) -> Range<*const T> {
        unsafe {
            let ptr = base.byte_offset(Self::ARRAY_OFFSET.try_into().unwrap());
            let ptr = ptr as *const T;
            ptr..ptr.offset(N.try_into().unwrap())
        }
    }

    /// Determines if `field` is one of the elements of the array returned by
    /// `as_array()` and, if it is, returns its index in the array.  If `field`
    /// is not a member of the array, `None` is returned.  This computation is
    /// done without dereferencing either `base` or `field`.
    ///
    /// SAFETY:
    ///
    /// While we guarantee that `base` is never dereferenced, we do require
    /// that `base` be an otherwise valid pointer to `Self`.  In particular, it
    /// must be non-null, satisfy the alignment requirements of `Self`, and
    /// `base.addr() + (size_of::<Self>() - 1)` must not overflow`.
    ///
    /// No additional requirements are placed on `field`. It can be an
    /// arbitrary pointer.
    unsafe fn index_of_ptr(
        base: *const Self,
        field: *const T,
    ) -> Option<usize> {
        unsafe {
            let ptr_range = Self::as_ptr_range(base);
            if !ptr_range.contains(&field) {
                return None;
            }
            let diff = (field as usize) - (ptr_range.start as usize);
            if diff % size_of::<T>() != 0 {
                return None;
            }
            Some(diff / size_of::<T>())
        }
    }
}

/// Computes the index of an element in a struct decorated AsArray.
///
/// While the macro is not capable of consuming every possible struct member
/// specifier, it is capable of handling structs of structs of arrays of arrays,
/// which is everything #[derive(AsSlice)] is capable of handling, so it should
/// be good enough.
#[macro_export]
macro_rules! index_of {
    ($AsArray:ty, $($fields:tt).*$([$idx:expr])*) => {
        unsafe {
            let p = std::ptr::NonNull::<$AsArray>::dangling().as_ptr();
            let f = &raw const (*p).$($fields).*$([$idx])*;
            <$AsArray>::index_of_ptr(p, f).unwrap()
        }
    };
}
