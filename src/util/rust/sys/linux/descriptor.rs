// Copyright 2025 Google
// SPDX-License-Identifier: MIT

use std::fs::read_link;
use std::fs::File;
use std::io::Error;
use std::io::ErrorKind;
use std::io::Result;
use std::os::fd::AsFd;
use std::os::fd::BorrowedFd;
use std::os::fd::OwnedFd;
use std::os::unix::io::AsRawFd;
use std::os::unix::io::FromRawFd;
use std::os::unix::io::IntoRawFd;
use std::os::unix::io::RawFd;

use rustix::fs::fcntl_get_seals;
use rustix::fs::fcntl_getfl;
use rustix::fs::seek;
use rustix::fs::OFlags;
use rustix::fs::SealFlags;
use rustix::fs::SeekFrom;
use rustix::io::Errno;

use crate::descriptor::AsRawDescriptor;
use crate::descriptor::FromRawDescriptor;
use crate::descriptor::IntoRawDescriptor;
use crate::DescriptorType;
use crate::MESA_HANDLE_TYPE_MEM_DMABUF;
use crate::MESA_HANDLE_TYPE_MEM_SHM;
use crate::MESA_MAP_ACCESS_READ;
use crate::MESA_MAP_ACCESS_RW;
use crate::MESA_MAP_ACCESS_WRITE;

pub type RawDescriptor = RawFd;
pub const DEFAULT_RAW_DESCRIPTOR: RawDescriptor = -1;

#[derive(Debug)]
pub struct OwnedDescriptor {
    owned: OwnedFd,
}

impl OwnedDescriptor {
    pub fn try_clone(&self) -> Result<OwnedDescriptor> {
        let clone = self.owned.try_clone()?;
        Ok(OwnedDescriptor { owned: clone })
    }

    pub fn determine_type(&self) -> Result<DescriptorType> {
        // Check for eventfd first (not seekable, special symlink)
        if let Ok(fd_path) = read_link(format!("/proc/self/fd/{}", self.as_raw_descriptor())) {
            let path_str = fd_path.to_string_lossy();
            if path_str.starts_with("anon_inode:[eventfd]") {
                return Ok(DescriptorType::Event);
            }
        }

        match seek(&self.owned, SeekFrom::End(0)) {
            Ok(seek_size) => {
                let size: u32 = seek_size
                    .try_into()
                    .map_err(|_| Error::from(ErrorKind::Unsupported))?;

                let handle_type = self.get_memory_handle_type()?;

                Ok(DescriptorType::Memory(size, handle_type))
            }
            _ => {
                let flags = fcntl_getfl(&self.owned)?;
                match flags & OFlags::ACCMODE {
                    OFlags::WRONLY => Ok(DescriptorType::WritePipe),
                    _ => Err(Error::from(ErrorKind::Unsupported)),
                }
            }
        }
    }

    pub fn determine_map_access_mode(&self) -> Result<u32> {
        let flags = fcntl_getfl(&self.owned)?;
        let mut access = match flags & OFlags::ACCMODE {
            OFlags::RDONLY => MESA_MAP_ACCESS_READ,
            OFlags::WRONLY => MESA_MAP_ACCESS_WRITE,
            OFlags::RDWR => MESA_MAP_ACCESS_RW,
            _ => return Err(Error::from(ErrorKind::Unsupported)),
        };
        // Access mode can be RDWR, but a write seal would still prevent RW mappings!
        let seals = match fcntl_get_seals(&self.owned) {
            Ok(seals) => seals,
            Err(Errno::INVAL) => SealFlags::empty(), // It's fine if the file does not support sealing
            Err(err) => return Err(err.into()),
        };
        if seals.contains(SealFlags::WRITE) || seals.contains(SealFlags::FUTURE_WRITE) {
            access &= !MESA_MAP_ACCESS_WRITE;
        }
        Ok(access)
    }

    fn get_memory_handle_type(&self) -> Result<u32> {
        let fd_path = read_link(format!("/proc/self/fd/{}", self.as_raw_descriptor()))
            .map_err(|_| Error::from(ErrorKind::Unsupported))?;

        let path_str = fd_path.to_string_lossy();
        if path_str.starts_with("/dmabuf:") {
            Ok(MESA_HANDLE_TYPE_MEM_DMABUF)
        } else if path_str.starts_with("/memfd:") {
            Ok(MESA_HANDLE_TYPE_MEM_SHM)
        } else {
            // Default to SHM for unknown types
            Ok(MESA_HANDLE_TYPE_MEM_SHM)
        }
    }
}

impl AsRawDescriptor for OwnedDescriptor {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        self.owned.as_raw_fd()
    }
}

impl FromRawDescriptor for OwnedDescriptor {
    // SAFETY:
    // It is caller's responsibility to ensure that the descriptor is valid and
    // stays valid for the lifetime of Self
    unsafe fn from_raw_descriptor(descriptor: RawDescriptor) -> Self {
        OwnedDescriptor {
            owned: OwnedFd::from_raw_fd(descriptor),
        }
    }
}

impl IntoRawDescriptor for OwnedDescriptor {
    fn into_raw_descriptor(self) -> RawDescriptor {
        self.owned.into_raw_fd()
    }
}

impl AsFd for OwnedDescriptor {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.owned.as_fd()
    }
}

impl AsRawDescriptor for File {
    fn as_raw_descriptor(&self) -> RawDescriptor {
        self.as_raw_fd()
    }
}

impl FromRawDescriptor for File {
    // SAFETY:
    // It is caller's responsibility to ensure that the descriptor is valid and
    // stays valid for the lifetime of Self
    unsafe fn from_raw_descriptor(descriptor: RawDescriptor) -> Self {
        File::from_raw_fd(descriptor)
    }
}

impl IntoRawDescriptor for File {
    fn into_raw_descriptor(self) -> RawDescriptor {
        self.into_raw_fd()
    }
}

impl From<File> for OwnedDescriptor {
    fn from(f: File) -> OwnedDescriptor {
        OwnedDescriptor { owned: f.into() }
    }
}

impl From<OwnedFd> for OwnedDescriptor {
    fn from(o: OwnedFd) -> OwnedDescriptor {
        OwnedDescriptor { owned: o }
    }
}
