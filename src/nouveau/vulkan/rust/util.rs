// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

/// A Rust version of the `vk_find_struct` macro. Uses the `paste` crate to call
/// the equivalent C function.
macro_rules! vk_find_struct_const(
    ($p:expr, $s:ident, $vendor:ident) => {
        {
            let s = unsafe {
                paste::paste! {
                    crate::ffi::__vk_find_struct(
                        $p as *mut _,
                        crate::ffi::[<VK_STRUCTURE_TYPE_ $s _ $vendor>]
                    ) as *const crate::ffi::[<Vk $s:lower:camel $vendor>]
                }
            };

            unsafe { *s }
        }
    }
);
pub(crate) use vk_find_struct_const;
