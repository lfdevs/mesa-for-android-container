// Copyright 2026 Google
// SPDX-License-Identifier: MIT

mod atomic_memory_sentinel;
mod bytestream;
mod descriptor;
mod error;
mod memory_mapping;
mod shm;
mod sys;

pub mod defines;

pub use atomic_memory_sentinel::AtomicMemorySentinel;
pub use bytestream::Reader;
pub use bytestream::Writer;
pub use defines::*;
pub use descriptor::AsBorrowedDescriptor;
pub use descriptor::AsRawDescriptor;
pub use descriptor::FromRawDescriptor;
pub use descriptor::IntoRawDescriptor;
pub use error::Error;
pub use error::Result;
pub use memory_mapping::MemoryMapping;
pub use shm::round_up_to_page_size;
pub use shm::SharedMemory;
pub use sys::platform::descriptor::OwnedDescriptor;
pub use sys::platform::descriptor::RawDescriptor;
pub use sys::platform::descriptor::DEFAULT_RAW_DESCRIPTOR;
pub use sys::platform::event::Event;
pub use sys::platform::pipe::create_pipe;
pub use sys::platform::pipe::ReadPipe;
pub use sys::platform::pipe::WritePipe;
pub use sys::platform::tube::Listener;
pub use sys::platform::tube::Tube;
pub use sys::platform::wait_context::WaitContext;
