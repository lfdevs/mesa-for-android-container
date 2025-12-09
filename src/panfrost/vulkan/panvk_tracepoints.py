# Copyright 2024 Google LLC
# SPDX-License-Identifier: MIT

import argparse
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--utrace-src', required=True)
    parser.add_argument('--utrace-hdr', required=True)
    parser.add_argument('--perfetto-hdr', required=True)
    return parser.parse_args()


args = parse_args()
sys.path.insert(0, args.import_path)

from u_trace import ForwardDecl, Header, HeaderScope  # noqa: E402
from u_trace import Tracepoint  # noqa: E402
from u_trace import TracepointArg as Arg  # noqa: E402
from u_trace import TracepointArgStruct as ArgStruct  # noqa: E402
from u_trace import utrace_generate, utrace_generate_perfetto_utils  # noqa: E402

Header('vulkan/vulkan_core.h', scope=HeaderScope.HEADER)
ForwardDecl('struct pan_fb_info')
ForwardDecl('struct panvk_device')

Header('pan_desc.h', scope=HeaderScope.SOURCE)


def begin_end_tp(name, args=[], tp_struct=None):
    Tracepoint(
        f'begin_{name}',
        tp_perfetto=f'panvk_utrace_perfetto_begin_{name}',
    )

    Tracepoint(
        f'end_{name}',
        args=args,
        tp_struct=tp_struct,
        tp_perfetto=f'panvk_utrace_perfetto_end_{name}',
    )


def define_tracepoints():
    # high-level tracepoints for API calls

    begin_end_tp(
        'cmdbuf',
        args=[
            Arg(
                type='VkCommandBufferUsageFlags',
                var='flags',
                c_format='0x%x',
            ),
        ],
    )

    begin_end_tp('meta')

    begin_end_tp(
        'render',
        args=[
            Arg(
                type='VkRenderingFlags',
                var='flags',
                c_format='0x%x',
            ),
            ArgStruct(type='const struct pan_fb_info *', var='fb'),
        ],
        tp_struct=[
            Arg(
                type='uint16_t',
                name='width',
                var='fb->width',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                name='height',
                var='fb->height',
                c_format='%u',
            ),
            Arg(
                type='uint8_t',
                name='nr_samples',
                var='fb->nr_samples',
                c_format='%u',
            ),
            Arg(
                type='uint8_t',
                name='rt_count',
                var='fb->rt_count',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                name='rt0_format',
                var='fb->rts[0].view ? fb->rts[0].view->format : PIPE_FORMAT_NONE',
                c_format='%s',
                to_prim_type='util_format_description((enum pipe_format){})->name',
            ),
            Arg(
                type='uint16_t',
                name='zs_format',
                var='fb->zs.view.zs ? fb->zs.view.zs->format : PIPE_FORMAT_NONE',
                c_format='%s',
                to_prim_type='util_format_description((enum pipe_format){})->name',
            ),
            Arg(
                type='uint16_t',
                name='s_format',
                var='fb->zs.view.s ? fb->zs.view.s->format : PIPE_FORMAT_NONE',
                c_format='%s',
                to_prim_type='util_format_description((enum pipe_format){})->name',
            ),
            Arg(
                type='uint32_t',
                name='tile_size',
                var='fb->tile_size',
                c_format='%u',
            ),
        ],
    )

    begin_end_tp(
        'dispatch',
        args=[
            Arg(
                type='uint16_t',
                var='base_group_x',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='base_group_y',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='base_group_z',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='group_count_x',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='group_count_y',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='group_count_z',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='group_size_x',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='group_size_y',
                c_format='%u',
            ),
            Arg(
                type='uint16_t',
                var='group_size_z',
                c_format='%u',
            ),
        ],
    )

    begin_end_tp(
        'dispatch_indirect',
        args=[
            ArgStruct(
                type='VkDispatchIndirectCommand',
                var='group_count',
                is_indirect=True,
                c_format='%ux%ux%u',
                fields=['x', 'y', 'z'],
            ),
        ],
    )

    begin_end_tp(
        'barrier',
        args=[
            Arg(
                type='uint8_t',
                var='sb_wait',
                c_format='0x%x',
            ),
            Arg(
                type='uint8_t',
                var='sync_wait',
                c_format='0x%x',
            ),
        ],
    )

    # low-level tracepoints for CS commands

    begin_end_tp(
        'sync32_add',
        args=[
            Arg(
                type='uint64_t',
                var='sync_addr',
                is_indirect=True,
                c_format='0x%" PRIx64 "'
            ),
            Arg(
                type='uint32_t',
                var='sync_val',
                is_indirect=True,
                c_format='%u',
            ),
        ],
    )

    begin_end_tp(
        'sync64_add',
        args=[
            Arg(
                type='uint64_t',
                var='sync_addr',
                is_indirect=True,
                c_format='0x%" PRIx64 "'
            ),
            Arg(
                type='uint64_t',
                var='sync_val',
                is_indirect=True,
                c_format='%" PRIu64 "',
            ),
        ],
    )

    begin_end_tp(
        'sync32_wait',
        args=[
            Arg(
                type='uint64_t',
                var='sync_addr',
                is_indirect=True,
                c_format='0x%" PRIx64 "'
            ),
            Arg(
                type='uint32_t',
                var='sync_val',
                is_indirect=True,
                c_format='%u',
            ),
            Arg(
                type='unsigned',
                var='cond',
                c_format='%u',
            ),
        ],
    )

    begin_end_tp(
        'sync64_wait',
        args=[
            Arg(
                type='uint64_t',
                var='sync_addr',
                is_indirect=True,
                c_format='0x%" PRIx64 "'
            ),
            Arg(
                type='uint64_t',
                var='sync_val',
                is_indirect=True,
                c_format='%" PRIu64 "',
            ),
            Arg(
                type='unsigned',
                var='cond',
                c_format='%u',
            ),
        ],
    )

    begin_end_tp(
        'flush_cache',
        args=[
            Arg(
                type='uint8_t',
                var='l2',
                c_format='%u',
            ),
            Arg(
                type='uint8_t',
                var='lsc',
                c_format='%u',
            ),
            Arg(
                type='uint8_t',
                var='other',
                c_format='%u',
            ),
        ],
    )

def generate_code():
    utrace_generate(
        cpath=args.utrace_src,
        hpath=args.utrace_hdr,
        ctx_param='struct panvk_device *dev',
    )

    utrace_generate_perfetto_utils(hpath=args.perfetto_hdr)


def main():
    define_tracepoints()
    generate_code()


if __name__ == '__main__':
    main()
