// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

//! hw_runner: hardware test execution layer for kraid.
//!
//! TODO docs

mod device;
mod invocation;
mod runner;

/// That's the whole public interface!
pub use device::HwError;
pub use invocation::InvocationInfo;
pub use runner::TestRunner;
