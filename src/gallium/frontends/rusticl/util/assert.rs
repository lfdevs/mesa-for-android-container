// Copyright 2022 Red Hat.
// SPDX-License-Identifier: MIT

#[macro_export]
macro_rules! static_assert {
    ($($tt:tt)*) => {
        const _: () = assert!($($tt)*);
    }
}
