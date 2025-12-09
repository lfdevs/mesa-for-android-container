use std::ffi::{c_int, c_void};

// it's never called anyway, but the linker _might_ think that. If this interface ever changes it
// might cause random issues, but... I also don't really care all that much until it happens.
#[no_mangle]
extern "C" fn pipe_loader_release(_: *mut *mut c_void, _: c_int) {}
