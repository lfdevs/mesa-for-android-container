// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::collections::VecDeque;
use std::mem::size_of;

use crate::util::AsBorrowedDescriptor;
use crate::util::Error;
use crate::util::Handle;
use crate::util::OwnedDescriptor;
use crate::util::Reader;
use crate::util::Result;
use crate::util::Tube;
use crate::util::Writer;
use crate::util::MAGMA_GPU_HANDLE_TYPE_SIGNAL_EVENT_FD;
use zerocopy::FromBytes;
use zerocopy::Immutable;
use zerocopy::IntoBytes;

use crate::protocols::kumquat_gpu_protocol::*;

const MAX_COMMAND_SIZE: usize = 4096;

pub struct KumquatStream {
    stream: Tube,
    write_buffer: [u8; MAX_COMMAND_SIZE],
    read_buffer: [u8; MAX_COMMAND_SIZE],
}

impl KumquatStream {
    pub fn new(stream: Tube) -> KumquatStream {
        KumquatStream {
            stream,
            write_buffer: [0; MAX_COMMAND_SIZE],
            read_buffer: [0; MAX_COMMAND_SIZE],
        }
    }

    pub fn write<T: FromBytes + IntoBytes + Immutable>(
        &mut self,
        encode: KumquatGpuProtocolWrite<T>,
    ) -> Result<()> {
        let mut writer = Writer::new(&mut self.write_buffer);

        let array: &[OwnedDescriptor] = match encode {
            KumquatGpuProtocolWrite::Cmd(cmd) => {
                writer.write_obj(cmd)?;
                &[]
            }
            KumquatGpuProtocolWrite::CmdWithHandle(cmd, handle) => {
                writer.write_obj(cmd)?;
                &[handle.os_handle]
            }
            KumquatGpuProtocolWrite::CmdWithData(cmd, data) => {
                writer.write_obj(cmd)?;
                writer.write_all(&data)?;
                &[]
            }
        };

        let bytes_written = writer.bytes_written();
        self.stream
            .send(&self.write_buffer[0..bytes_written], array)?;
        Ok(())
    }

    pub fn read(&mut self) -> Result<Vec<KumquatGpuProtocol>> {
        let mut vec: Vec<KumquatGpuProtocol> = Vec::new();
        let (bytes_read, descriptor_vec) = self.stream.receive(&mut self.read_buffer)?;
        let mut descriptors: VecDeque<OwnedDescriptor> = descriptor_vec.into();

        if bytes_read == 0 {
            vec.push(KumquatGpuProtocol::OkNoData);
            return Ok(vec);
        }

        let mut reader = Reader::new(&self.read_buffer[0..bytes_read]);
        while reader.available_bytes() != 0 {
            let hdr = reader.peek_obj::<kumquat_gpu_protocol_ctrl_hdr>()?;
            let protocol = match hdr.type_ {
                KUMQUAT_GPU_PROTOCOL_GET_NUM_CAPSETS => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::GetNumCapsets
                }
                KUMQUAT_GPU_PROTOCOL_GET_CAPSET_INFO => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::GetCapsetInfo(hdr.payload)
                }
                KUMQUAT_GPU_PROTOCOL_GET_CAPSET => {
                    KumquatGpuProtocol::GetCapset(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_CTX_CREATE => {
                    KumquatGpuProtocol::CtxCreate(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_CTX_DESTROY => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::CtxDestroy(hdr.payload)
                }
                KUMQUAT_GPU_PROTOCOL_CTX_ATTACH_RESOURCE => {
                    KumquatGpuProtocol::CtxAttachResource(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_CTX_DETACH_RESOURCE => {
                    KumquatGpuProtocol::CtxDetachResource(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_3D => {
                    KumquatGpuProtocol::ResourceCreate3d(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_TRANSFER_TO_HOST_3D => {
                    let os_handle = descriptors.pop_front().ok_or(Error::Unsupported)?;
                    let resp: kumquat_gpu_protocol_transfer_host_3d = reader.read_obj()?;

                    let handle = Handle {
                        os_handle,
                        handle_type: MAGMA_GPU_HANDLE_TYPE_SIGNAL_EVENT_FD,
                    };

                    KumquatGpuProtocol::TransferToHost3d(resp, handle)
                }
                KUMQUAT_GPU_PROTOCOL_TRANSFER_FROM_HOST_3D => {
                    let os_handle = descriptors.pop_front().ok_or(Error::Unsupported)?;
                    let resp: kumquat_gpu_protocol_transfer_host_3d = reader.read_obj()?;

                    let handle = Handle {
                        os_handle,
                        handle_type: MAGMA_GPU_HANDLE_TYPE_SIGNAL_EVENT_FD,
                    };

                    KumquatGpuProtocol::TransferFromHost3d(resp, handle)
                }
                KUMQUAT_GPU_PROTOCOL_SUBMIT_3D => {
                    let cmd: kumquat_gpu_protocol_cmd_submit = reader.read_obj()?;
                    if reader.available_bytes() < cmd.size.try_into()? {
                        // Large command buffers should handled via shared memory.
                        return Err(Error::Unsupported);
                    } else if reader.available_bytes() != 0 {
                        let num_in_fences = cmd.num_in_fences as usize;
                        let cmd_size = cmd.size as usize;
                        let mut cmd_buf = vec![0; cmd_size];
                        let mut fence_ids: Vec<u64> = Vec::with_capacity(num_in_fences);
                        for _ in 0..num_in_fences {
                            match reader.read_obj::<u64>() {
                                Ok(fence_id) => {
                                    fence_ids.push(fence_id);
                                }
                                Err(_) => return Err(Error::Unsupported),
                            }
                        }
                        reader.read_exact(&mut cmd_buf[..])?;
                        KumquatGpuProtocol::CmdSubmit3d(cmd, cmd_buf, fence_ids)
                    } else {
                        KumquatGpuProtocol::CmdSubmit3d(cmd, Vec::new(), Vec::new())
                    }
                }
                KUMQUAT_GPU_PROTOCOL_RESOURCE_CREATE_BLOB => {
                    KumquatGpuProtocol::ResourceCreateBlob(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_SNAPSHOT_SAVE => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::SnapshotSave
                }
                KUMQUAT_GPU_PROTOCOL_SNAPSHOT_RESTORE => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::SnapshotRestore
                }
                KUMQUAT_GPU_PROTOCOL_RESP_NUM_CAPSETS => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::RespNumCapsets(hdr.payload)
                }
                KUMQUAT_GPU_PROTOCOL_RESP_CAPSET_INFO => {
                    KumquatGpuProtocol::RespCapsetInfo(reader.read_obj()?)
                }
                KUMQUAT_GPU_PROTOCOL_RESP_CAPSET => {
                    let len: usize = hdr.payload.try_into()?;
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    let mut capset: Vec<u8> = vec![0; len];
                    reader.read_exact(&mut capset)?;
                    KumquatGpuProtocol::RespCapset(capset)
                }
                KUMQUAT_GPU_PROTOCOL_RESP_CONTEXT_CREATE => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::RespContextCreate(hdr.payload)
                }
                KUMQUAT_GPU_PROTOCOL_RESP_RESOURCE_CREATE => {
                    let os_handle = descriptors.pop_front().ok_or(Error::Unsupported)?;
                    let resp: kumquat_gpu_protocol_resp_resource_create = reader.read_obj()?;

                    let handle = Handle {
                        os_handle,
                        handle_type: resp.handle_type,
                    };

                    KumquatGpuProtocol::RespResourceCreate(resp, handle)
                }
                KUMQUAT_GPU_PROTOCOL_RESP_CMD_SUBMIT_3D => {
                    let os_handle = descriptors.pop_front().ok_or(Error::Unsupported)?;
                    let resp: kumquat_gpu_protocol_resp_cmd_submit_3d = reader.read_obj()?;

                    let handle = Handle {
                        os_handle,
                        handle_type: resp.handle_type,
                    };

                    KumquatGpuProtocol::RespCmdSubmit3d(resp.fence_id, handle)
                }
                KUMQUAT_GPU_PROTOCOL_RESP_OK_SNAPSHOT => {
                    reader.consume(size_of::<kumquat_gpu_protocol_ctrl_hdr>());
                    KumquatGpuProtocol::RespOkSnapshot
                }
                _ => {
                    return Err(Error::Unsupported);
                }
            };

            vec.push(protocol);
        }

        Ok(vec)
    }

    pub fn as_borrowed_descriptor(&self) -> &OwnedDescriptor {
        self.stream.as_borrowed_descriptor()
    }
}
