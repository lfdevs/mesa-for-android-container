// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::cmp::min;
use std::collections::BTreeMap as Map;
use std::path::PathBuf;
use std::slice::from_raw_parts_mut;

use crate::protocols::ipc::KumquatStream;
use crate::protocols::kumquat_gpu_protocol::*;
use crate::util::Error;
use crate::util::Event;
use crate::util::Handle;
use crate::util::IntoRawDescriptor;
use crate::util::MemoryMapping;
use crate::util::MesaMapping;
use crate::util::OwnedDescriptor;
use crate::util::RawDescriptor;
use crate::util::Reader;
use crate::util::Result;
use crate::util::SharedMemory;
use crate::util::Tube;
use crate::util::TubeType;
use crate::util::Writer;
use crate::util::MAGMA_GPU_HANDLE_TYPE_MEM_OPAQUE_FD;
use crate::util::MAGMA_MAP_ACCESS_RW;
use crate::util::MAGMA_MAP_CACHE_CACHED;

use crate::virtgpu_kumquat::defines::*;

// HACK: Should be part of protocol.
const RUTABAGA_FLAG_FENCE: u32 = 1 << 0;
const RUTABAGA_FLAG_INFO_RING_IDX: u32 = 1 << 1;
const RUTABAGA_FLAG_FENCE_HOST_SHAREABLE: u32 = 1 << 2;

pub struct VirtGpuResource {
    resource_id: u32,
    size: usize,
    handle: Handle,
    attached_fences: Vec<Handle>,
    vulkan_info: VulkanInfo,
    system_mapping: Option<MemoryMapping>,
}

impl VirtGpuResource {
    pub fn new(
        resource_id: u32,
        size: usize,
        handle: Handle,
        vulkan_info: VulkanInfo,
    ) -> VirtGpuResource {
        VirtGpuResource {
            resource_id,
            size,
            handle,
            attached_fences: Vec::new(),
            vulkan_info,
            system_mapping: None,
        }
    }
}

pub struct VirtGpuKumquat {
    context_id: u32,
    id_allocator: u32,
    capset_mask: u64,
    stream: KumquatStream,
    capsets: Map<u32, Vec<u8>>,
    resources: Map<u32, VirtGpuResource>,
}

impl VirtGpuKumquat {
    pub fn new(gpu_socket: &str) -> Result<VirtGpuKumquat> {
        let path = PathBuf::from(gpu_socket);
        let connection = Tube::new(path, TubeType::Packet)?;
        let mut stream = KumquatStream::new(connection);

        let get_num_capsets = kumquat_gpu_protocol_ctrl_hdr {
            type_: KUMQUAT_GPU_PROTOCOL_GET_NUM_CAPSETS,
            ..Default::default()
        };

        stream.write(KumquatGpuProtocolWrite::Cmd(get_num_capsets))?;
        let mut protocols = stream.read()?;
        let num_capsets = match protocols.remove(0) {
            KumquatGpuProtocol::RespNumCapsets(num) => num,
            _ => return Err(Error::Unsupported),
        };

        let mut capset_mask = 0;
        let mut capsets: Map<u32, Vec<u8>> = Default::default();
        for capset_index in 0..num_capsets {
            let get_capset_info = kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_GET_CAPSET_INFO,
                payload: capset_index,
            };

            stream.write(KumquatGpuProtocolWrite::Cmd(get_capset_info))?;
            protocols = stream.read()?;
            let resp_capset_info = match protocols.remove(0) {
                KumquatGpuProtocol::RespCapsetInfo(info) => info,
                _ => return Err(Error::Unsupported),
            };

            let get_capset = kumquat_gpu_protocol_get_capset {
                hdr: kumquat_gpu_protocol_ctrl_hdr {
                    type_: KUMQUAT_GPU_PROTOCOL_GET_CAPSET,
                    ..Default::default()
                },
                capset_id: resp_capset_info.capset_id,
                capset_version: resp_capset_info.version,
            };

            stream.write(KumquatGpuProtocolWrite::Cmd(get_capset))?;
            protocols = stream.read()?;
            let capset = match protocols.remove(0) {
                KumquatGpuProtocol::RespCapset(capset) => capset,
                _ => return Err(Error::Unsupported),
            };

            capset_mask |= 1u64 << resp_capset_info.capset_id;
            capsets.insert(resp_capset_info.capset_id, capset);
        }

        Ok(VirtGpuKumquat {
            context_id: 0,
            id_allocator: 0,
            capset_mask,
            stream,
            capsets,
            resources: Default::default(),
        })
    }

    pub fn allocate_id(&mut self) -> u32 {
        self.id_allocator += 1;
        self.id_allocator
    }

    pub fn get_param(&self, getparam: &mut VirtGpuParam) -> Result<()> {
        getparam.value = match getparam.param {
            VIRTGPU_KUMQUAT_PARAM_3D_FEATURES => (self.capset_mask != 0) as u64,
            VIRTGPU_KUMQUAT_PARAM_CAPSET_QUERY_FIX..=VIRTGPU_KUMQUAT_PARAM_CONTEXT_INIT => 1,
            VIRTGPU_KUMQUAT_PARAM_SUPPORTED_CAPSET_IDS => self.capset_mask,
            VIRTGPU_KUMQUAT_PARAM_EXPLICIT_DEBUG_NAME => 0,
            VIRTGPU_KUMQUAT_PARAM_FENCE_PASSING => 1,
            _ => return Err(Error::Unsupported),
        };

        Ok(())
    }

    pub fn get_caps(&self, capset_id: u32, slice: &mut [u8]) -> Result<()> {
        let caps = self.capsets.get(&capset_id).ok_or(Error::Unsupported)?;
        let length = min(slice.len(), caps.len());
        slice[0..length].copy_from_slice(&caps[0..length]);
        Ok(())
    }

    pub fn context_create(&mut self, capset_id: u64, name: &str) -> Result<u32> {
        let mut debug_name = [0u8; 64];
        debug_name
            .iter_mut()
            .zip(name.bytes())
            .for_each(|(dst, src)| *dst = src);

        let context_create = kumquat_gpu_protocol_ctx_create {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_CTX_CREATE,
                ..Default::default()
            },
            nlen: 0,
            context_init: capset_id.try_into()?,
            debug_name,
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(context_create))?;
        let mut protocols = self.stream.read()?;
        self.context_id = match protocols.remove(0) {
            KumquatGpuProtocol::RespContextCreate(ctx_id) => ctx_id,
            _ => return Err(Error::Unsupported),
        };

        Ok(self.context_id)
    }

    pub fn resource_create_3d(&mut self, create_3d: &mut VirtGpuResourceCreate3D) -> Result<()> {
        let resource_create_3d = kumquat_gpu_protocol_resource_create_3d {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_3D,
                ..Default::default()
            },
            target: create_3d.target,
            format: create_3d.format,
            bind: create_3d.bind,
            width: create_3d.width,
            height: create_3d.height,
            depth: create_3d.depth,
            array_size: create_3d.array_size,
            last_level: create_3d.last_level,
            nr_samples: create_3d.nr_samples,
            flags: create_3d.flags,
            size: create_3d.size,
            stride: create_3d.stride,
            ctx_id: self.context_id,
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(resource_create_3d))?;
        let mut protocols = self.stream.read()?;
        let resource = match protocols.remove(0) {
            KumquatGpuProtocol::RespResourceCreate(resp, handle) => {
                let size: usize = create_3d.size.try_into()?;
                VirtGpuResource::new(resp.resource_id, size, handle, resp.vulkan_info)
            }
            _ => return Err(Error::Unsupported),
        };

        create_3d.res_handle = resource.resource_id;
        create_3d.bo_handle = self.allocate_id();
        self.resources.insert(create_3d.bo_handle, resource);

        Ok(())
    }

    pub fn resource_create_blob(
        &mut self,
        create_blob: &mut VirtGpuResourceCreateBlob,
        blob_cmd: &[u8],
    ) -> Result<()> {
        if !blob_cmd.is_empty() {
            let submit_command = kumquat_gpu_protocol_cmd_submit {
                hdr: kumquat_gpu_protocol_ctrl_hdr {
                    type_: KUMQUAT_GPU_PROTOCOL_SUBMIT_3D,
                    ..Default::default()
                },
                ctx_id: self.context_id,
                pad: 0,
                size: blob_cmd.len().try_into()?,
                num_in_fences: 0,
                flags: 0,
                ring_idx: 0,
                padding: Default::default(),
            };

            let mut data: Vec<u8> = vec![0; blob_cmd.len()];
            data.copy_from_slice(blob_cmd);

            self.stream
                .write(KumquatGpuProtocolWrite::CmdWithData(submit_command, data))?;
        }

        let resource_create_blob = kumquat_gpu_protocol_resource_create_blob {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_BLOB,
                ..Default::default()
            },
            ctx_id: self.context_id,
            blob_mem: create_blob.blob_mem,
            blob_flags: create_blob.blob_flags,
            padding: 0,
            blob_id: create_blob.blob_id,
            size: create_blob.size,
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(resource_create_blob))?;
        let mut protocols = self.stream.read()?;
        let resource = match protocols.remove(0) {
            KumquatGpuProtocol::RespResourceCreate(resp, handle) => {
                let size: usize = create_blob.size.try_into()?;
                VirtGpuResource::new(resp.resource_id, size, handle, resp.vulkan_info)
            }
            _ => {
                return Err(Error::Unsupported);
            }
        };

        create_blob.res_handle = resource.resource_id;
        create_blob.bo_handle = self.allocate_id();
        self.resources.insert(create_blob.bo_handle, resource);
        Ok(())
    }

    pub fn resource_unref(&mut self, bo_handle: u32) -> Result<()> {
        let resource = self
            .resources
            .remove(&bo_handle)
            .ok_or(Error::Unsupported)?;

        let detach_resource = kumquat_gpu_protocol_ctx_resource {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_CTX_DETACH_RESOURCE,
                ..Default::default()
            },
            ctx_id: self.context_id,
            resource_id: resource.resource_id,
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(detach_resource))?;

        Ok(())
    }

    pub fn map(&mut self, bo_handle: u32) -> Result<MesaMapping> {
        let resource = self
            .resources
            .get_mut(&bo_handle)
            .ok_or(Error::Unsupported)?;

        if let Some(ref system_mapping) = resource.system_mapping {
            let mesa_mapping = system_mapping.as_mesa_mapping();
            Ok(mesa_mapping)
        } else {
            let clone = resource.handle.try_clone()?;
            let mapping = MemoryMapping::from_safe_descriptor(
                clone.os_handle,
                resource.size,
                MAGMA_MAP_CACHE_CACHED | MAGMA_MAP_ACCESS_RW,
            )?;

            let mesa_mapping = mapping.as_mesa_mapping();
            resource.system_mapping = Some(mapping);
            Ok(mesa_mapping)
        }
    }

    pub fn unmap(&mut self, bo_handle: u32) -> Result<()> {
        let resource = self
            .resources
            .get_mut(&bo_handle)
            .ok_or(Error::Unsupported)?;

        resource.system_mapping = None;
        Ok(())
    }

    pub fn transfer_to_host(&mut self, transfer: &VirtGpuTransfer) -> Result<()> {
        let resource = self
            .resources
            .get_mut(&transfer.bo_handle)
            .ok_or(Error::Unsupported)?;

        let event = Event::new()?;
        let emulated_fence: Handle = event.into();

        resource.attached_fences.push(emulated_fence.try_clone()?);

        let transfer_to_host = kumquat_gpu_protocol_transfer_host_3d {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_TRANSFER_TO_HOST_3D,
                ..Default::default()
            },
            box_: transfer._box,
            offset: transfer.offset,
            level: transfer.level,
            stride: transfer.stride,
            layer_stride: transfer.layer_stride,
            ctx_id: self.context_id,
            resource_id: resource.resource_id,
            padding: 0,
        };

        self.stream.write(KumquatGpuProtocolWrite::CmdWithHandle(
            transfer_to_host,
            emulated_fence,
        ))?;
        Ok(())
    }

    pub fn transfer_from_host(&mut self, transfer: &VirtGpuTransfer) -> Result<()> {
        let resource = self
            .resources
            .get_mut(&transfer.bo_handle)
            .ok_or(Error::Unsupported)?;

        let event = Event::new()?;
        let emulated_fence: Handle = event.into();

        resource.attached_fences.push(emulated_fence.try_clone()?);
        let transfer_from_host = kumquat_gpu_protocol_transfer_host_3d {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_TRANSFER_FROM_HOST_3D,
                ..Default::default()
            },
            box_: transfer._box,
            offset: transfer.offset,
            level: transfer.level,
            stride: transfer.stride,
            layer_stride: transfer.layer_stride,
            ctx_id: self.context_id,
            resource_id: resource.resource_id,
            padding: 0,
        };

        self.stream.write(KumquatGpuProtocolWrite::CmdWithHandle(
            transfer_from_host,
            emulated_fence,
        ))?;

        Ok(())
    }

    pub fn submit_command(
        &mut self,
        flags: u32,
        bo_handles: &[u32],
        cmd: &[u8],
        ring_idx: u32,
        in_fences: &[u64],
        raw_descriptor: &mut RawDescriptor,
    ) -> Result<()> {
        let mut fence_opt: Option<Handle> = None;
        let mut data: Vec<u8> = vec![0; cmd.len()];
        let mut host_flags = 0;

        if flags & VIRTGPU_KUMQUAT_EXECBUF_RING_IDX != 0 {
            host_flags = RUTABAGA_FLAG_INFO_RING_IDX;
        }

        let need_fence =
            !bo_handles.is_empty() || (flags & VIRTGPU_KUMQUAT_EXECBUF_FENCE_FD_OUT) != 0;

        let actual_fence = (flags & VIRTGPU_KUMQUAT_EXECBUF_SHAREABLE_OUT) != 0
            && (flags & VIRTGPU_KUMQUAT_EXECBUF_FENCE_FD_OUT) != 0;

        // Should copy from in-fences when gfxstream supports it.
        data.copy_from_slice(cmd);

        if actual_fence {
            host_flags |= RUTABAGA_FLAG_FENCE_HOST_SHAREABLE;
            host_flags |= RUTABAGA_FLAG_FENCE;
        } else if need_fence {
            host_flags |= RUTABAGA_FLAG_FENCE;
        }

        let submit_command = kumquat_gpu_protocol_cmd_submit {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_SUBMIT_3D,
                ..Default::default()
            },
            ctx_id: self.context_id,
            pad: 0,
            size: cmd.len().try_into()?,
            num_in_fences: in_fences.len().try_into()?,
            flags: host_flags,
            ring_idx: ring_idx.try_into()?,
            padding: Default::default(),
        };

        if need_fence {
            self.stream
                .write(KumquatGpuProtocolWrite::CmdWithData(submit_command, data))?;

            let mut protocols = self.stream.read()?;
            let fence = match protocols.remove(0) {
                KumquatGpuProtocol::RespCmdSubmit3d(_fence_id, handle) => handle,
                _ => {
                    return Err(Error::Unsupported);
                }
            };

            for handle in bo_handles {
                // We could support implicit sync with real fences, but the need does not exist.
                if actual_fence {
                    return Err(Error::Unsupported);
                }

                let resource = self.resources.get_mut(handle).ok_or(Error::Unsupported)?;

                resource.attached_fences.push(fence.try_clone()?);
            }

            fence_opt = Some(fence);
        } else {
            self.stream
                .write(KumquatGpuProtocolWrite::CmdWithData(submit_command, data))?;
        }

        if flags & VIRTGPU_KUMQUAT_EXECBUF_FENCE_FD_OUT != 0 {
            *raw_descriptor = fence_opt
                .ok_or(Error::WithContext("no fence found"))?
                .os_handle
                .into_raw_descriptor();
        }

        Ok(())
    }

    pub fn wait(&mut self, bo_handle: u32) -> Result<()> {
        let resource = self
            .resources
            .get_mut(&bo_handle)
            .ok_or(Error::Unsupported)?;

        let new_fences: Vec<Handle> = std::mem::take(&mut resource.attached_fences);
        for fence in new_fences {
            let mut event: Event = fence.try_into()?;
            event.wait()?;
        }

        Ok(())
    }

    pub fn resource_export(&mut self, bo_handle: u32, flags: u32) -> Result<Handle> {
        let resource = self
            .resources
            .get_mut(&bo_handle)
            .ok_or(Error::Unsupported)?;

        if flags & VIRTGPU_KUMQUAT_EMULATED_EXPORT != 0 {
            let descriptor: OwnedDescriptor =
                SharedMemory::new("virtgpu_export", VIRTGPU_KUMQUAT_PAGE_SIZE as u64)?.into();

            let clone = descriptor.try_clone()?;

            // Creating the mapping closes the cloned descriptor.
            let mapping = MemoryMapping::from_safe_descriptor(
                clone,
                VIRTGPU_KUMQUAT_PAGE_SIZE,
                MAGMA_MAP_CACHE_CACHED | MAGMA_MAP_ACCESS_RW,
            )?;
            let mesa_mapping = mapping.as_mesa_mapping();

            let slice: &mut [u8] = unsafe {
                from_raw_parts_mut(mesa_mapping.ptr as *mut u8, VIRTGPU_KUMQUAT_PAGE_SIZE)
            };
            let mut writer = Writer::new(slice);
            writer.write_obj(resource.resource_id)?;

            // Opaque to users of this API, shared memory internally
            Ok(Handle {
                os_handle: descriptor,
                handle_type: MAGMA_GPU_HANDLE_TYPE_MEM_OPAQUE_FD,
            })
        } else {
            let clone = resource.handle.try_clone()?;
            Ok(clone)
        }
    }

    pub fn resource_import(
        &mut self,
        handle: Handle,
        bo_handle: &mut u32,
        resource_handle: &mut u32,
        size: &mut u64,
    ) -> Result<()> {
        let clone = handle.try_clone()?;
        let mapping = MemoryMapping::from_safe_descriptor(
            clone.os_handle,
            VIRTGPU_KUMQUAT_PAGE_SIZE,
            MAGMA_MAP_CACHE_CACHED | MAGMA_MAP_ACCESS_RW,
        )?;

        let mesa_mapping = mapping.as_mesa_mapping();

        let slice: &mut [u8] =
            unsafe { from_raw_parts_mut(mesa_mapping.ptr as *mut u8, VIRTGPU_KUMQUAT_PAGE_SIZE) };

        let mut reader = Reader::new(slice);
        *resource_handle = reader.read_obj()?;

        let attach_resource = kumquat_gpu_protocol_ctx_resource {
            hdr: kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_CTX_ATTACH_RESOURCE,
                ..Default::default()
            },
            ctx_id: self.context_id,
            resource_id: *resource_handle,
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(attach_resource))?;
        let resource = VirtGpuResource::new(
            *resource_handle,
            VIRTGPU_KUMQUAT_PAGE_SIZE,
            handle,
            Default::default(),
        );

        *bo_handle = self.allocate_id();
        // Should ask the server about the size long-term.
        *size = VIRTGPU_KUMQUAT_PAGE_SIZE as u64;
        self.resources.insert(*bo_handle, resource);

        Ok(())
    }

    pub fn resource_info(&self, bo_handle: u32) -> Result<VulkanInfo> {
        let resource = self.resources.get(&bo_handle).ok_or(Error::Unsupported)?;

        Ok(resource.vulkan_info)
    }

    pub fn snapshot(&mut self) -> Result<()> {
        let snapshot_save = kumquat_gpu_protocol_ctrl_hdr {
            type_: KUMQUAT_GPU_PROTOCOL_SNAPSHOT_SAVE,
            ..Default::default()
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(snapshot_save))?;

        let mut protocols = self.stream.read()?;
        match protocols.remove(0) {
            KumquatGpuProtocol::RespOkSnapshot => Ok(()),
            _ => Err(Error::Unsupported),
        }
    }

    pub fn restore(&mut self) -> Result<()> {
        let snapshot_restore = kumquat_gpu_protocol_ctrl_hdr {
            type_: KUMQUAT_GPU_PROTOCOL_SNAPSHOT_RESTORE,
            ..Default::default()
        };

        self.stream
            .write(KumquatGpuProtocolWrite::Cmd(snapshot_restore))?;

        let mut protocols = self.stream.read()?;
        match protocols.remove(0) {
            KumquatGpuProtocol::RespOkSnapshot => Ok(()),
            _ => Err(Error::Unsupported),
        }
    }
}

impl Drop for VirtGpuKumquat {
    fn drop(&mut self) {
        if self.context_id != 0 {
            for (_, resource) in self.resources.iter() {
                let detach_resource = kumquat_gpu_protocol_ctx_resource {
                    hdr: kumquat_gpu_protocol_ctrl_hdr {
                        type_: KUMQUAT_GPU_PROTOCOL_CTX_DETACH_RESOURCE,
                        ..Default::default()
                    },
                    ctx_id: self.context_id,
                    resource_id: resource.resource_id,
                };

                let _ = self
                    .stream
                    .write(KumquatGpuProtocolWrite::Cmd(detach_resource));
            }

            self.resources.clear();
            let context_destroy = kumquat_gpu_protocol_ctrl_hdr {
                type_: KUMQUAT_GPU_PROTOCOL_CTX_DESTROY,
                payload: self.context_id,
            };

            let _ = self
                .stream
                .write(KumquatGpuProtocolWrite::Cmd(context_destroy));
        }
    }
}
