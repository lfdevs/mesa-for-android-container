#
# Copyright © 2026 Igalia S.L.
#
# SPDX-License-Identifier: MIT
#

import argparse
import sys

parser = argparse.ArgumentParser()
parser.add_argument('-p', '--import-path', required=True)
parser.add_argument('-C', '--src', required=True)
parser.add_argument('-H', '--hdr', required=True)
parser.add_argument('-P', '--perfetto-hdr', required=False)
args = parser.parse_args()
sys.path.insert(0, args.import_path)


from u_trace import Header
from u_trace import Tracepoint
from u_trace import TracepointArg
from u_trace import utrace_generate
from u_trace import utrace_generate_perfetto_utils

# List of the default tracepoints enabled. By default tracepoints are enabled,
# set tp_default_enabled=False to disable them by default.
panfrost_default_tps = []

#
# Tracepoint definitions:
#

Header('util/u_dump.h')
Header('pan_job.h')


def begin_end_tp(name, args=[], tp_struct=None, tp_print=None,
                 tp_default_enabled=True):
    global panfrost_default_tps
    if tp_default_enabled:
        panfrost_default_tps.append(name)
    Tracepoint('panfrost_start_{0}'.format(name),
               toggle_name=name,
               tp_perfetto='panfrost_start_{0}'.format(name))
    Tracepoint('panfrost_end_{0}'.format(name),
               toggle_name=name,
               args=args,
               tp_struct=tp_struct,
               tp_perfetto='panfrost_end_{0}'.format(name),
               tp_print=tp_print)


begin_end_tp('vertex_tiler',
    args=[TracepointArg(type='uint32_t', var='draw_count', c_format='%u')]
)

begin_end_tp('fragment',
    args=[TracepointArg(type='uint32_t',         var='submit_id',    name='submission_id', c_format='%u', perfetto_field=True),
          TracepointArg(type='enum pipe_format', var='cbuf0_format',  c_format='%s', to_prim_type='util_format_description({})->short_name'),
          TracepointArg(type='enum pipe_format', var='zs_format',     c_format='%s', to_prim_type='util_format_description({})->short_name'),
          TracepointArg(type='uint16_t',         var='width',         c_format='%u'),
          TracepointArg(type='uint16_t',         var='height',        c_format='%u'),
          TracepointArg(type='uint8_t',          var='mrts',          c_format='%u'),
          TracepointArg(type='uint8_t',          var='samples',       c_format='%u')],
)

begin_end_tp('batch')

begin_end_tp('barrier')

begin_end_tp('cache_flush')

begin_end_tp('compute',
    args=[TracepointArg(type='uint16_t', var='local_size_x', c_format='%u'),
          TracepointArg(type='uint16_t', var='local_size_y', c_format='%u'),
          TracepointArg(type='uint16_t', var='local_size_z', c_format='%u'),
          TracepointArg(type='uint32_t', var='num_groups_x', c_format='%u'),
          TracepointArg(type='uint32_t', var='num_groups_y', c_format='%u'),
          TracepointArg(type='uint32_t', var='num_groups_z', c_format='%u')],
)

# For indirect dispatches: local_size is known on the CPU size since it is
# declared in the shader, but num_groups_* must be captured from the indirect
# buffer at execution time.
begin_end_tp('compute_indirect',
    args=[TracepointArg(type='uint16_t', var='local_size_x', c_format='%u'),
          TracepointArg(type='uint16_t', var='local_size_y', c_format='%u'),
          TracepointArg(type='uint16_t', var='local_size_z', c_format='%u'),
          TracepointArg(type='uint32_t', var='num_groups_x', c_format='%u', is_indirect=True),
          TracepointArg(type='uint32_t', var='num_groups_y', c_format='%u', is_indirect=True),
          TracepointArg(type='uint32_t', var='num_groups_z', c_format='%u', is_indirect=True)],
)

utrace_generate(cpath=args.src,
                hpath=args.hdr,
                ctx_param='struct pipe_context *pctx',
                trace_toggle_name='panfrost_gpu_tracepoint',
                trace_toggle_defaults=panfrost_default_tps)

utrace_generate_perfetto_utils(hpath=args.perfetto_hdr)
