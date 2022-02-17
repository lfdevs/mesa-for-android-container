#
# Copyright © 2021 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

import argparse
import sys

#
# Tracepoint definitions:
#
def define_tracepoints(args):
    from u_trace import Header, HeaderScope
    from u_trace import ForwardDecl
    from u_trace import Tracepoint
    from u_trace import TracepointArg as Arg
    from u_trace import TracepointArgStruct as ArgStruct

    Header('intel_driver_ds.h', scope=HeaderScope.SOURCE)
    Header('blorp/blorp_priv.h', scope=HeaderScope.HEADER)
    Header('ds/intel_driver_ds.h', scope=HeaderScope.HEADER)

    def begin_end_tp(name, tp_args=[], tp_struct=None, tp_print=None, end_pipelined=True):
        Tracepoint('intel_begin_{0}'.format(name),
                   tp_perfetto='intel_ds_begin_{0}'.format(name))
        Tracepoint('intel_end_{0}'.format(name),
                   args=tp_args,
                   tp_struct=tp_struct,
                   tp_perfetto='intel_ds_end_{0}'.format(name),
                   tp_print=tp_print,
                   end_of_pipe=end_pipelined)


    begin_end_tp('batch',
                 tp_args=[Arg(type='uint8_t', var='name', c_format='%hhu'),],
                 end_pipelined=False)

    begin_end_tp('cmd_buffer',
                 tp_args=[Arg(type='uint8_t', var='level', c_format='%hhu'),],
                 end_pipelined=False)

    begin_end_tp('render_pass',
                 tp_args=[Arg(type='uint16_t', var='width', c_format='%hu'),
                          Arg(type='uint16_t', var='height', c_format='%hu'),
                          Arg(type='uint8_t', var='att_count', c_format='%hhu'),
                          Arg(type='uint8_t', var='msaa', c_format='%hhu'),
                          Arg(type='uint32_t', var='subpass_count', c_format='%u'),])

    begin_end_tp('blorp',
                 tp_args=[Arg(type='uint32_t', name='width', var='width', c_format='%u'),
                          Arg(type='uint32_t', name='height', var='height', c_format='%u'),
                          Arg(type='enum isl_aux_op', name='hiz_op', var='hiz_op', c_format='%s', to_prim_type='isl_aux_op_to_name({})'),
                          Arg(type='enum isl_aux_op', name='fast_clear_op', var='fast_clear_op', c_format='%s', to_prim_type='isl_aux_op_to_name({})'),
                          Arg(type='enum blorp_shader_type', name='blorp_type', var='shader_type', c_format='%s', to_prim_type='blorp_shader_type_to_name({})'),
                          Arg(type='enum blorp_shader_pipeline', name='blorp_pipe', var='shader_pipe', c_format='%s', to_prim_type='blorp_shader_pipeline_to_name({})'),])

    begin_end_tp('draw',
                 tp_args=[Arg(type='uint32_t', var='count', c_format='%u')])
    begin_end_tp('draw_multi',
                 tp_args=[Arg(type='uint32_t', var='count', c_format='%u'),])
    begin_end_tp('draw_indexed',
                 tp_args=[Arg(type='uint32_t', var='count', c_format='%u'),])
    begin_end_tp('draw_indexed_multi',
                 tp_args=[Arg(type='uint32_t', var='count', c_format='%u'),])
    begin_end_tp('draw_indirect_byte_count',
                 tp_args=[Arg(type='uint32_t', var='instance_count', c_format='%u'),])
    begin_end_tp('draw_indirect',
                 tp_args=[Arg(type='uint32_t', var='draw_count', c_format='%u'),])
    begin_end_tp('draw_indexed_indirect',
                 tp_args=[Arg(type='uint32_t', var='draw_count', c_format='%u'),])
    begin_end_tp('draw_indirect_count',
                 tp_args=[Arg(type='uint32_t', var='max_draw_count', c_format='%u'),])
    begin_end_tp('draw_indexed_indirect_count',
                 tp_args=[Arg(type='uint32_t', var='max_draw_count', c_format='%u'),])

    begin_end_tp('compute',
                 tp_args=[Arg(type='uint32_t', var='group_x', c_format='%u'),
                          Arg(type='uint32_t', var='group_y', c_format='%u'),
                          Arg(type='uint32_t', var='group_z', c_format='%u'),],
                 tp_print=['group=%ux%ux%u', '__entry->group_x', '__entry->group_y', '__entry->group_z'])

    def flag_bits(args):
        bits = [Arg(type='enum intel_ds_stall_flag', name='flags', var='decode_cb(flags)', c_format='0x%x')]
        for a in args:
            bits.append(Arg(type='bool', name=a[1], var='__entry->flags & INTEL_DS_{0}_BIT'.format(a[0]), c_format='%u'))
        return bits

    def stall_args(args):
        fmt = ''
        exprs = []
        for a in args:
            fmt += '%s'
            exprs.append('(__entry->flags & INTEL_DS_{0}_BIT) ? "+{1}" : ""'.format(a[0], a[1]))
        fmt += ' : %s'
        exprs.append('__entry->reason ? __entry->reason : "unknown"')
        fmt = [fmt]
        fmt += exprs
        return fmt

    stall_flags = [['DEPTH_CACHE_FLUSH',         'depth_flush'],
                   ['DATA_CACHE_FLUSH',          'dc_flush'],
                   ['HDC_PIPELINE_FLUSH',        'hdc_flush'],
                   ['RENDER_TARGET_CACHE_FLUSH', 'rt_flush'],
                   ['TILE_CACHE_FLUSH',          'tile_flush'],
                   ['STATE_CACHE_INVALIDATE',    'state_inval'],
                   ['CONST_CACHE_INVALIDATE',    'const_inval'],
                   ['VF_CACHE_INVALIDATE',       'vf_inval'],
                   ['TEXTURE_CACHE_INVALIDATE',  'tex_inval'],
                   ['INST_CACHE_INVALIDATE',     'ic_inval'],
                   ['STALL_AT_SCOREBOARD',       'pb_stall'],
                   ['DEPTH_STALL',               'depth_stall'],
                   ['CS_STALL',                  'cs_stall']]

    begin_end_tp('stall',
                 tp_args=[ArgStruct(type='uint32_t', var='flags'),
                          ArgStruct(type='intel_ds_stall_cb_t', var='decode_cb'),
                          ArgStruct(type='const char *', var='reason'),],
                 tp_struct=[Arg(type='uint32_t', name='flags', var='decode_cb(flags)', c_format='0x%x'),
                            Arg(type='const char *', name='reason', var='reason', c_format='%s'),],
                 tp_print=stall_args(stall_flags),
                 end_pipelined=False)


def generate_code(args):
    from u_trace import utrace_generate
    from u_trace import utrace_generate_perfetto_utils

    utrace_generate(cpath=args.utrace_src, hpath=args.utrace_hdr,
                    ctx_param='struct intel_ds_device *dev')
    utrace_generate_perfetto_utils(hpath=args.perfetto_hdr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--import-path', required=True)
    parser.add_argument('--utrace-src', required=True)
    parser.add_argument('--utrace-hdr', required=True)
    parser.add_argument('--perfetto-hdr', required=True)
    args = parser.parse_args()
    sys.path.insert(0, args.import_path)
    define_tracepoints(args)
    generate_code(args)


if __name__ == '__main__':
    main()
