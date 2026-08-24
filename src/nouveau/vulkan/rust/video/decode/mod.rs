// Copyright © 2024 Collabora, Ltd
// SPDX-License-Identifier: MIT

use crate::ffi;
use nv_push_rs;
use nvidia_headers::classes::cl906f::mthd as cl906f;
use nvidia_headers::classes::clc5b0::NVC5B0_VIDEO_DECODER;
use nvidia_headers::Mthd;

mod h264;

struct SessionData {
    decoder: Box<dyn VideoDecoder>,
}

pub(crate) trait VideoDecoder {
    fn begin(
        &mut self,
        _cmd: &mut ffi::nvk_cmd_buffer,
        _begin_info: &ffi::VkVideoBeginCodingInfoKHR,
    ) {
    }

    fn decode(
        &mut self,
        cmd: &mut ffi::nvk_cmd_buffer,
        frame_info: &ffi::VkVideoDecodeInfoKHR,
    ) -> Result<(), ffi::VkResult>;
}

#[unsafe(no_mangle)]
pub extern "C" fn nvk_video_create_video_session(
    vid: &mut ffi::nvk_video_session,
) {
    vid.rust = Box::into_raw(Box::new(SessionData {
        decoder: Box::new(h264::Decoder::default()),
    }))
    .cast();
}

#[unsafe(no_mangle)]
pub extern "C" fn nvk_video_destroy_video_session(
    vid: &mut ffi::nvk_video_session,
) {
    drop(unsafe { Box::from_raw(vid.rust as *mut SessionData) });
}

/// Cast the opaque `nvk_video_session::rust` pointer to a mutable reference to
/// a `SessionData`.
///
/// # Safety:
///
/// The caller must ensure that the pointer is valid when accessing the
/// reference and that Rust's aliasing rules are followed.
///
fn to_session<'a>(cmd_buf: *mut ffi::nvk_cmd_buffer) -> &'a mut SessionData {
    unsafe {
        let video = (*cmd_buf).state.video;
        let rust_ptr = (*video.vid).rust as *mut SessionData;

        // SAFETY: Vulkan requires external synchronization for the command
        // buffer being recorded and for the video session bound to it
        // (vkCmdBeginVideoCodingKHR's videoSession parameter), so no other
        // thread can access this session concurrently. Each entry point drops
        // the returned reference before returning, so no two mutable
        // references ever coexist.
        &mut *rust_ptr
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn nvk_push_video_decode_state_init(
    _queue: &mut ffi::nvk_queue,
    p: *mut ffi::nv_push,
) -> ffi::VkResult {
    let mut push = nv_push_rs::Push::new();

    let set_object = cl906f::SetObject {
        nvclass: NVC5B0_VIDEO_DECODER.into(),
        engine: cl906f::SetObjectEngine::Sw,
    };
    push.push_mthd_bits(
        nv_push_rs::class_to_subc(NVC5B0_VIDEO_DECODER),
        cl906f::SetObject::ADDR,
        set_object.to_bits(),
    );
    unsafe { crate::nv_push_append_push(p, push) };

    ffi::VK_SUCCESS
}

#[unsafe(no_mangle)]
pub extern "C" fn nvk_cmd_buffer_begin_video_decode(
    _cmd: &mut ffi::nvk_cmd_buffer,
    _begin_info: &ffi::VkCommandBufferBeginInfo,
) {
}

#[unsafe(no_mangle)]
pub extern "C" fn nvk_video_cmd_begin_video_coding_khr(
    cmd: &mut ffi::nvk_cmd_buffer,
    begin_info: &ffi::VkVideoBeginCodingInfoKHR,
) {
    let session = to_session(cmd);

    session.decoder.begin(cmd, &begin_info);
}

#[unsafe(no_mangle)]
pub extern "C" fn nvk_video_cmd_decode_video_khr(
    cmd: &mut ffi::nvk_cmd_buffer,
    frame_info: &ffi::VkVideoDecodeInfoKHR,
) -> ffi::VkResult {
    let session = to_session(cmd);

    session
        .decoder
        .decode(cmd, &frame_info)
        .err()
        .unwrap_or(ffi::VK_SUCCESS)
}
