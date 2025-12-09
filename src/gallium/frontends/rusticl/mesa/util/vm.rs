// Copyright 2024 Red Hat.
// SPDX-License-Identifier: MIT

use std::{num::NonZeroU64, ops::DerefMut, pin::Pin};

use mesa_rust_gen::*;

pub struct VM {
    // util_vma_heap is a linked list, so we need to pin it as it references itself.
    vm: Pin<Box<util_vma_heap>>,
}

// SAFETY: util_vma_heap is safe to send between threads.
unsafe impl Send for VM {}

impl VM {
    pub fn alloc(&mut self, size: NonZeroU64, alignment: NonZeroU64) -> Option<NonZeroU64> {
        // SAFETY: Size and alignment must not be 0, but we use NonZeroU64 anyway.
        //         Returns 0 on allocation failure.
        NonZeroU64::new(unsafe {
            util_vma_heap_alloc(self.vm.deref_mut(), size.get(), alignment.get())
        })
    }

    // TODO: to guarantee a safe interface we should rather return a new object from alloc owning
    // a reference to the vm and take care of the free via drop.
    pub fn free(&mut self, address: NonZeroU64, size: NonZeroU64) {
        unsafe {
            util_vma_heap_free(self.vm.deref_mut(), address.get(), size.get());
        }
    }

    pub fn new(start: NonZeroU64, size: NonZeroU64) -> Self {
        let mut vm = Box::pin(util_vma_heap::default());

        // Safety: util_vma_heap is a linked list with itself (or rather one of its members) as the
        //         start/end, therefore we have to pin the allocation so that its address never
        //         changes.
        //         It is also invalid to have the start be 0, but we guarantee it with NonZeroU64.
        unsafe {
            util_vma_heap_init(vm.deref_mut(), start.get(), size.get());
        }

        Self { vm: vm }
    }
}

impl Drop for VM {
    fn drop(&mut self) {
        unsafe {
            util_vma_heap_finish(self.vm.deref_mut());
        }
    }
}
