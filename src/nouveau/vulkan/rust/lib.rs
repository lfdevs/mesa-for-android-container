// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use nv_push_rs::Push;

mod ffi {
    pub use nvk_bindings_rs::*;
}

mod util;
mod video;

unsafe fn nv_push_append_push(p: *mut ffi::nv_push, push: Push) {
    let p = unsafe { p.as_mut() }.unwrap();
    if push.is_empty() {
        return;
    }

    unsafe {
        assert!(!p.end.is_null());
        let new_end = p.end.offset(push.len().try_into().unwrap());
        assert!(new_end <= p.limit);

        std::ptr::copy_nonoverlapping(push.as_ptr(), p.end, push.len());
        p.end = new_end;
    }
}

fn nvk_cmd_buffer_push(cmd: &mut ffi::nvk_cmd_buffer, push: Push) {
    unsafe {
        let p = ffi::nvk_cmd_buffer_push(cmd, push.len().try_into().unwrap());
        nv_push_append_push(p, push);
    }
}
