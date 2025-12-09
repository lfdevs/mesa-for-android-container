// Copyright 2025 Google
// SPDX-License-Identifier: MIT

use std::ffi::NulError;
use std::io::Error as IoError;
use std::num::TryFromIntError;
use std::str::Utf8Error;

use remain::sorted;
use rustix::io::Errno as RustixError;
use thiserror::Error;

/// An error generated while using this crate.
#[sorted]
#[derive(Error, Debug)]
pub enum MesaError {
    /// An error with the MesaHandle
    #[error("invalid Mesa handle")]
    InvalidMesaHandle,
    /// An input/output error occurred.
    #[error("an input/output error occurred: {0}")]
    IoError(IoError),
    /// Nul crate error.
    #[error("Nul Error occurred {0}")]
    NulError(NulError),
    /// Rustix crate error.
    #[error("The errno is {0}")]
    RustixError(RustixError),
    /// An attempted integer conversion failed.
    #[error("int conversion failed: {0}")]
    TryFromIntError(TryFromIntError),
    /// The command is unsupported.
    #[error("the requested function is not implemented")]
    Unsupported,
    /// Utf8 error.
    #[error("an utf8 error occurred: {0}")]
    Utf8Error(Utf8Error),
    /// An error with a free form context, similar to anyhow
    #[error("operation failed: {0}")]
    WithContext(&'static str),
}

impl From<RustixError> for MesaError {
    fn from(e: RustixError) -> MesaError {
        MesaError::RustixError(e)
    }
}

impl From<NulError> for MesaError {
    fn from(e: NulError) -> MesaError {
        MesaError::NulError(e)
    }
}

impl From<IoError> for MesaError {
    fn from(e: IoError) -> MesaError {
        MesaError::IoError(e)
    }
}

impl From<TryFromIntError> for MesaError {
    fn from(e: TryFromIntError) -> MesaError {
        MesaError::TryFromIntError(e)
    }
}

impl From<Utf8Error> for MesaError {
    fn from(e: Utf8Error) -> MesaError {
        MesaError::Utf8Error(e)
    }
}

/// The result of an operation in this crate.
pub type MesaResult<T> = std::result::Result<T, MesaError>;
