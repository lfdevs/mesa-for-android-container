// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use std::ffi::{CStr, CString};
use std::mem;
use std::os::raw::c_char;

#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn c_string_to_string(cstr: *const c_char) -> String {
    if cstr.is_null() {
        return String::from("");
    }

    let res = unsafe { CStr::from_ptr(cstr).to_str() };
    assert!(res.is_ok());
    String::from(res.unwrap_or(""))
}

/// Simple wrapper around [`CStr::from_ptr`] to bind the lifetime to a slice containing the
/// nul-terminated string.
///
/// # Safety
///
/// Same as [`CStr::from_ptr`]
pub unsafe fn char_arr_to_cstr(c_str: &[c_char]) -> &CStr {
    // We make sure that at least the end of the slice is nul-terminated. We don't really care if
    // there is another nul within the slice.
    debug_assert_eq!(c_str.last().copied(), Some(b'\0' as c_char));
    unsafe { CStr::from_ptr(c_str.as_ptr()) }
}

pub trait CStrExt: ToOwned {
    /// # Safety
    ///
    /// The same as CStr::from_ptr except that ptr can be a NULL pointer.
    unsafe fn from_ptr_or_empty<'a>(ptr: &'a *const c_char) -> &'a Self;
    fn concat(&self, other: impl AsRef<Self>) -> Self::Owned;
}

impl CStrExt for CStr {
    #[inline]
    unsafe fn from_ptr_or_empty<'a>(ptr: &'a *const c_char) -> &'a Self {
        if ptr.is_null() {
            return c"";
        }
        // SAFETY: Callers responsibility
        unsafe { CStr::from_ptr(*ptr) }
    }

    fn concat(&self, other: impl AsRef<Self>) -> Self::Owned {
        let other = other.as_ref();

        let self_bytes = self.to_bytes();
        let other_bytes = other.to_bytes_with_nul();
        let size = self_bytes.len() + other_bytes.len();

        let mut buffer = Vec::with_capacity(size);
        buffer.extend_from_slice(self_bytes);
        buffer.extend_from_slice(other_bytes);

        // SAFETY: The only 0 byte in buffer is at the end
        unsafe { CString::from_vec_with_nul_unchecked(buffer) }
    }
}

#[test]
fn test_from_ptr_or_empty() {
    assert_eq!(unsafe { CStr::from_ptr_or_empty(&std::ptr::null()) }, c"");
    let some_str = c"SomeStr";
    assert_eq!(
        unsafe { CStr::from_ptr_or_empty(&some_str.as_ptr()) },
        some_str
    );
}

#[test]
fn test_concat_cstr() {
    assert_eq!(c"Test".concat(c"Other"), c"TestOther".to_owned());

    assert_eq!(
        c"Test".concat(
            #[allow(clippy::unnecessary_to_owned)]
            c"Owned".to_owned()
        ),
        c"TestOwned".to_owned()
    );
}

pub trait CStringExt {
    fn push_cstr(&mut self, other: &CStr);
}

impl CStringExt for CString {
    fn push_cstr(&mut self, other: &CStr) {
        let mut buffer = mem::take(self).into_bytes();
        buffer.extend(other.to_bytes_with_nul());

        // SAFETY: The only 0 byte in buffer is at the end
        *self = unsafe { CString::from_vec_with_nul_unchecked(buffer) };
    }
}

pub trait Join<Separator> {
    type Output;

    fn join(&self, sep: Separator) -> Self::Output;
}

impl Join<&CStr> for [CString] {
    type Output = CString;

    fn join(&self, sep: &CStr) -> Self::Output {
        let Some((first, rest)) = self.split_first() else {
            return CString::default();
        };

        let size = self.iter().map(|v| v.count_bytes()).sum::<usize>()
            + sep.count_bytes() * (self.len() - 1)
            + 1;

        let mut buffer = Vec::with_capacity(size);
        buffer.extend(first.to_bytes());

        let sep = sep.to_bytes();
        for string in rest {
            buffer.extend_from_slice(sep);
            buffer.extend_from_slice(string.to_bytes());
        }

        // SAFETY: There are no interior 0 bytes in buffer
        unsafe { CString::from_vec_unchecked(buffer) }
    }
}

#[test]
fn test_push_cstr() {
    let mut owned = c"SomeString".to_owned();
    owned.push_cstr(c"MoreString");
    assert_eq!(owned, c"SomeStringMoreString".to_owned());
}

#[test]
fn test_join_c_strings() {
    assert_eq!(Join::join([].as_slice(), c";"), c"".to_owned());
    assert_eq!([c"test".to_owned()].join(c";"), c"test".to_owned());
    assert_eq!(
        [c"String".to_owned(), c"AnotherString".to_owned()].join(c";"),
        c"String;AnotherString".to_owned()
    );
    assert_eq!(
        [
            c"String".to_owned(),
            c"AnotherString".to_owned(),
            c"Testing".to_owned()
        ]
        .join(c"###"),
        c"String###AnotherString###Testing".to_owned(),
    );
}
