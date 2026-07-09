// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::ffi::NulError;
use std::io::Error as IoError;
use std::num::ParseIntError;
use std::num::TryFromIntError;
use std::str::Utf8Error;

use remain::sorted;
use rustix::io::Errno as RustixError;
use thiserror::Error;

/// An error generated while using this crate.
#[sorted]
#[derive(Error, Debug)]
pub enum Error {
    /// An error with the Handle
    #[error("invalid Mesa handle")]
    InvalidMagmaHandle,
    /// An input/output error occurred.
    #[error("an input/output error occurred: {0}")]
    IoError(IoError),
    /// Nul crate error.
    #[error("Nul Error occurred {0}")]
    NulError(NulError),
    /// An attempted integer parsing failed.
    #[error("int parsing failed: {0}")]
    ParseIntError(ParseIntError),
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

#[cfg(any(target_os = "android", target_os = "linux"))]
impl From<RustixError> for Error {
    fn from(e: RustixError) -> Error {
        Error::RustixError(e)
    }
}

impl From<NulError> for Error {
    fn from(e: NulError) -> Error {
        Error::NulError(e)
    }
}

impl From<IoError> for Error {
    fn from(e: IoError) -> Error {
        Error::IoError(e)
    }
}

impl From<ParseIntError> for Error {
    fn from(e: ParseIntError) -> Error {
        Error::ParseIntError(e)
    }
}

impl From<TryFromIntError> for Error {
    fn from(e: TryFromIntError) -> Error {
        Error::TryFromIntError(e)
    }
}

impl From<Utf8Error> for Error {
    fn from(e: Utf8Error) -> Error {
        Error::Utf8Error(e)
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;
