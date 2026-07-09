# Copyright © 2026 Valve Corporation
#
# SPDX-License-Identifier: MIT

import argparse
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--import-path', nargs='+', required=True)
parser.add_argument('--src', required=True)
parser.add_argument('--entrypoints-src', required=True)
parser.add_argument('--hdr', required=True)
parser.add_argument('--perfetto-hdr', required=True)
parser.add_argument("--xml", help="Vulkan API XML file.", required=True, action="append", dest="xml_files")
args = parser.parse_args()

for path in args.import_path:
    sys.path.insert(0, path)

from u_trace import Tracepoint
from u_trace import TracepointArg as Arg
from u_trace import utrace_generate
from u_trace import utrace_generate_perfetto_utils

from vk_entrypoints import EntrypointParam, get_entrypoints_from_xml

def remove_prefix(text, prefix):
    if text.startswith(prefix):
        return text[len(prefix):]
    return text

class CodeBuilder:
    def __init__(self, level):
        self.code = ""
        self.level = level

    def add(self, line=''):
        self.code += "%s%s\n" % ("   " * self.level, line)

radv_default_tps = []

wrapper_builder = CodeBuilder(0)
wrapper_builder.add("#include \"radv_cmd_buffer.h\"")
wrapper_builder.add()

def begin_end_tp(name, toggle_name=None, tp_args=[], tp_struct = None, tp_print=None, tp_default_enabled=True):
    if not toggle_name:
        toggle_name = name

    global radv_default_tps
    if tp_default_enabled:
        radv_default_tps.append(toggle_name)

    Tracepoint('radv_begin_{0}'.format(name),
               toggle_name=toggle_name,
               args=tp_args,
               tp_struct=tp_struct,
               tp_print=tp_print,
               need_cs_param=True,
               tp_type="u_tracepoint_type_begin_range")
    Tracepoint('radv_end_{0}'.format(name),
               toggle_name=toggle_name,
               need_cs_param=True,
               tp_type="u_tracepoint_type_end_range")
    
    global wrapper_builder
    for tp in ["begin", "end"]:
        args = [] if tp == "end" else tp_args
        wrapper_builder.add("static inline void")
        wrapper_builder.add("radv_utrace_%s_%s(struct radv_cmd_buffer *cmd_buffer%s)" % (tp, name, ''.join([', %s %s' % (arg.type, arg.var) for arg in args])))
        wrapper_builder.add("{")
        wrapper_builder.level += 1
        wrapper_builder.add("if (unlikely(cmd_buffer->utrace.trace))")
        wrapper_builder.level += 1
        wrapper_builder.add("trace_radv_%s_%s(cmd_buffer->utrace.trace, cmd_buffer%s);" % (tp, name, ''.join([', ' + arg.var for arg in args])))
        wrapper_builder.level -= 2
        wrapper_builder.add("}")
        wrapper_builder.add()
        wrapper_builder.add('#ifndef RADV_U_TRACE')
        wrapper_builder.add('#define radv_utrace_%s_%s(...)' % (tp, name))
        wrapper_builder.add('#endif')
        wrapper_builder.add()

begin_end_tp('cmd_buffer')

begin_end_tp('rendering')

begin_end_tp('secondary')

begin_end_tp('image_transition', toggle_name='internal')

begin_end_tp('copy_vrs_htile', toggle_name='internal')

begin_end_tp('clear_rendering', toggle_name='internal')
begin_end_tp('resolve_rendering', toggle_name='internal')

begin_end_tp('compute_copy_memory', toggle_name='internal', tp_args=[Arg(type='uint32_t', var='size', c_format=r'%u')])
begin_end_tp('cp_dma_copy_memory', toggle_name='internal', tp_args=[Arg(type='uint32_t', var='size', c_format=r'%u')])

begin_end_tp('leaves', toggle_name='internal')
begin_end_tp('morton_generate', toggle_name='internal')
begin_end_tp('morton_sort', toggle_name='internal')
begin_end_tp('lbvh_main', toggle_name='internal')
begin_end_tp('lbvh_generate_ir', toggle_name='internal')
begin_end_tp('ploc', toggle_name='internal')
begin_end_tp('hploc', toggle_name='internal')
begin_end_tp('update_as', toggle_name='internal', tp_args=[
    Arg(type='uint32_t', var='build_flags', c_format=r'0x%x', fuzzy_hash=''),
])
begin_end_tp('encode_as', toggle_name='internal', tp_args=[
    Arg(type='uint32_t', var='build_flags', c_format=r'0x%x', fuzzy_hash=''),
])
begin_end_tp('encode_triangles', toggle_name='internal', tp_args=[
    Arg(type='uint32_t', var='build_flags', c_format=r'0x%x', fuzzy_hash=''),
])
begin_end_tp('encode_triangles_retry', toggle_name='internal', tp_args=[
    Arg(type='uint32_t', var='build_flags', c_format=r'0x%x', fuzzy_hash=''),
])
begin_end_tp('init_header', toggle_name='internal', tp_args=[
    Arg(type='uint32_t', var='build_flags', c_format=r'0x%x', fuzzy_hash=''),
])

class ArgInfo:
    def __init__(self, type, c_format, name, expr=None):
        if not expr:
            expr = name
        self.type = type
        self.c_format = c_format
        self.name = name
        self.expr = expr

def uint32_arg(name, expr=None):
    return ArgInfo('uint32_t', r'%u', name, expr)

class EntrypointTracepoint:
    def __init__(self, category, args=[]):
        self.category = category
        self.args = args

ENTRYPOINT_TRACEPOINTS = {
    # sync
    'CmdPipelineBarrier2' : EntrypointTracepoint('sync'),
    'CmdSetEvent2' : EntrypointTracepoint('sync'),
    'CmdResetEvent2' : EntrypointTracepoint('sync'),
    'CmdWaitEvents2' : EntrypointTracepoint('sync'),

    # draw
    'CmdDraw' : EntrypointTracepoint('draw', args=[
        uint32_arg('vertex_count', 'vertexCount'), uint32_arg('instance_count', 'instanceCount'),
    ]),
    'CmdDrawMultiEXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'drawCount'), uint32_arg('instance_count', 'instanceCount'),
    ]),
    'CmdDrawIndexed' : EntrypointTracepoint('draw', args=[
        uint32_arg('index_count', 'indexCount'), uint32_arg('instance_count', 'instanceCount'),
    ]),
    'CmdDrawMultiIndexedEXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'drawCount'), uint32_arg('instance_count', 'instanceCount'),
    ]),
    'CmdDrawIndirect' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'drawCount'),
    ]),
    'CmdDrawIndirect2KHR' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'pInfo->drawCount'),
    ]),
    'CmdDrawIndexedIndirect' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'drawCount'),
    ]),
    'CmdDrawIndexedIndirect2KHR' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'pInfo->drawCount'),
    ]),
    'CmdDrawIndirectCount' : EntrypointTracepoint('draw', args=[
        uint32_arg('max_draw_count', 'maxDrawCount'),
    ]),
    'CmdDrawIndirectCount2KHR' : EntrypointTracepoint('draw', args=[
        uint32_arg('max_draw_count', 'pInfo->maxDrawCount'),
    ]),
    'CmdDrawIndexedIndirectCount' : EntrypointTracepoint('draw', args=[
        uint32_arg('max_draw_count', 'maxDrawCount'),
    ]),
    'CmdDrawIndexedIndirectCount2KHR' : EntrypointTracepoint('draw', args=[
        uint32_arg('max_draw_count', 'pInfo->maxDrawCount'),
    ]),
    'CmdDrawMeshTasksEXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('x', 'groupCountX'), uint32_arg('y', 'groupCountY'), uint32_arg('z', 'groupCountZ'),
    ]),
    'CmdDrawMeshTasksIndirectEXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'drawCount'),
    ]),
    'CmdDrawMeshTasksIndirect2EXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('draw_count', 'pInfo->drawCount'),
    ]),
    'CmdDrawMeshTasksIndirectCountEXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('max_draw_count', 'maxDrawCount'),
    ]),
    'CmdDrawMeshTasksIndirectCount2EXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('max_draw_count', 'pInfo->maxDrawCount'),
    ]),
    'CmdDrawIndirectByteCountEXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('instance_count', 'instanceCount'),
    ]),
    'CmdDrawIndirectByteCount2EXT' : EntrypointTracepoint('draw', args=[
        uint32_arg('instance_count', 'instanceCount'),
    ]),

    # dispatch
    'CmdDispatchBase' : EntrypointTracepoint('dispatch', args=[
        uint32_arg('base_x', 'baseGroupX'), uint32_arg('base_y', 'baseGroupY'), uint32_arg('base_z', 'baseGroupZ'),
        uint32_arg('x', 'groupCountX'), uint32_arg('y', 'groupCountY'), uint32_arg('z', 'groupCountZ'),
    ]),
    'CmdDispatchIndirect' : EntrypointTracepoint('dispatch'),
    'CmdDispatchIndirect2KHR' : EntrypointTracepoint('dispatch'),

    # trace_rays
    'CmdTraceRaysKHR' : EntrypointTracepoint('trace_rays', args= [
        uint32_arg('width'), uint32_arg('height'), uint32_arg('depth'),
    ]),
    'CmdTraceRaysIndirectKHR' : EntrypointTracepoint('trace_rays'),
    'CmdTraceRaysIndirect2KHR' : EntrypointTracepoint('trace_rays'),

    # accel_struct
    'CmdBuildAccelerationStructuresKHR' : EntrypointTracepoint('accel_struct'),
    'CmdCopyAccelerationStructureKHR' : EntrypointTracepoint('accel_struct'),
    'CmdCopyMemoryToAccelerationStructureKHR' : EntrypointTracepoint('accel_struct'),
    'CmdCopyAccelerationStructureToMemoryKHR' : EntrypointTracepoint('accel_struct'),

    # dgc
    'CmdExecuteGeneratedCommandsEXT' : EntrypointTracepoint('dgc'),
    'CmdPreprocessGeneratedCommandsEXT' : EntrypointTracepoint('dgc'),

    # query
    'CmdCopyQueryPoolResults' : EntrypointTracepoint('query'),
    'CmdResetQueryPool' : EntrypointTracepoint('query'),
    'CmdCopyQueryPoolResultsToMemoryKHR' : EntrypointTracepoint('query'),

    # image
    'CmdBlitImage2' : EntrypointTracepoint('image'),
    'CmdClearColorImage' : EntrypointTracepoint('image'),
    'CmdClearDepthStencilImage' : EntrypointTracepoint('image'),
    'CmdClearAttachments' : EntrypointTracepoint('image'),
    'CmdCopyBufferToImage2' : EntrypointTracepoint('image'),
    'CmdCopyMemoryToImageKHR' : EntrypointTracepoint('image'),
    'CmdCopyMemoryToImageIndirectKHR' : EntrypointTracepoint('image'),
    'CmdCopyImageToBuffer2' : EntrypointTracepoint('image'),
    'CmdCopyImageToMemoryKHR' : EntrypointTracepoint('image'),
    'CmdCopyImage2' : EntrypointTracepoint('image'),
    'CmdResolveImage2' : EntrypointTracepoint('image'),

    # buffer
    'CmdFillBuffer' : EntrypointTracepoint('buffer', args=[
        uint32_arg('size'),
    ]),
    'CmdFillMemoryKHR' : EntrypointTracepoint('buffer'),
    'CmdCopyBuffer2' : EntrypointTracepoint('buffer'),
    'CmdCopyMemoryIndirectKHR' : EntrypointTracepoint('buffer'),
    'CmdCopyMemoryKHR' : EntrypointTracepoint('buffer'),
    'CmdUpdateBuffer' : EntrypointTracepoint('buffer', args=[
        uint32_arg('size', 'dataSize'),
    ]),
    'CmdUpdateMemoryKHR' : EntrypointTracepoint('buffer'),
}

entrypoints_builder = CodeBuilder(0)

entrypoints_builder.add('/* Copyright © 2026 Valve Corporation')
entrypoints_builder.add(' * SPDX-License-Identifier: MIT')
entrypoints_builder.add(' */')
entrypoints_builder.add()
entrypoints_builder.add('#include "radv_cmd_buffer.h"')
entrypoints_builder.add('#include "radv_entrypoints.h"')
entrypoints_builder.add('#include "radv_tracepoints.h"')
entrypoints_builder.add()

for e in get_entrypoints_from_xml(args.xml_files, False):
    if e.name in ENTRYPOINT_TRACEPOINTS:
        info = ENTRYPOINT_TRACEPOINTS[e.name]

        entrypoints_builder.add('VKAPI_ATTR void VKAPI_CALL')
        entrypoints_builder.add('utrace_%s(' % (e.name))
        entrypoints_builder.level += 1
        entrypoints_builder.add(', '.join([p.decl for p in e.params]))
        entrypoints_builder.level -= 1
        entrypoints_builder.add(') {')
        entrypoints_builder.level += 1
        entrypoints_builder.add('VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);')
        entrypoints_builder.add('struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);')
        entrypoints_builder.add()
        entrypoints_builder.add('if (!cmd_buffer->state.meta.inside_meta_op)')
        entrypoints_builder.level += 1
        entrypoints_builder.add('trace_radv_begin_%s(cmd_buffer->utrace.trace, cmd_buffer%s);' %
                                (remove_prefix(e.name, 'Cmd'), ''.join([', ' + arg.expr for arg in info.args])))
        entrypoints_builder.level -= 1
        entrypoints_builder.add()
        entrypoints_builder.add('device->layer_dispatch.utrace.%s(%s);' % (e.name, ', '.join([p.name for p in e.params])))
        entrypoints_builder.add()
        entrypoints_builder.add('if (!cmd_buffer->state.meta.inside_meta_op)')
        entrypoints_builder.level += 1
        entrypoints_builder.add('trace_radv_end_%s(cmd_buffer->utrace.trace, cmd_buffer);' % (remove_prefix(e.name, 'Cmd')))
        entrypoints_builder.level -= 2
        entrypoints_builder.add('}')
        entrypoints_builder.add()

        tp_args = [Arg(type=arg.type, var=arg.name, c_format=arg.c_format) for arg in info.args]
        begin_end_tp(remove_prefix(e.name, 'Cmd'), toggle_name=info.category, tp_args=tp_args)

with open(args.entrypoints_src, 'w', encoding='utf-8') as f:
    f.write(entrypoints_builder.code)

utrace_generate(cpath=args.src, hpath=args.hdr,
                ctx_param='void *ctx',
                trace_toggle_name='radv_gpu_tracepoint',
                trace_toggle_defaults=radv_default_tps,
                additional_code=wrapper_builder.code)
utrace_generate_perfetto_utils(hpath=args.perfetto_hdr)
