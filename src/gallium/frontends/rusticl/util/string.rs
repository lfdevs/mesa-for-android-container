use std::ffi::CStr;
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

/// # Safety
///
/// Same as [`CStr::from_ptr`]
pub unsafe fn char_arr_to_cstr(c_str: &[c_char]) -> &CStr {
    unsafe { CStr::from_ptr(c_str.as_ptr()) }
}
