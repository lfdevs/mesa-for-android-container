#!/usr/bin/env python3 

from __future__ import annotations
from typing import Optional
from pathlib import Path

import argparse
import re
import sys

class Line:
    def __init__(self, line_str: str):
        self.valid = False
        if line_str.count(',') < 4:
            return
        (frame, cmd_buf, timestamp, self.name, self.args) = line_str.strip().split(',', 4)
        if self.name.count('_') < 2:
            return
        (self.intel, begin_or_end, self.func) = self.name.split('_', 2)

        self.frame: int = int(frame)
        self.cmd_buf: int = int(cmd_buf)
        self.timestamp: float = float(timestamp)
        self.is_begin: bool = begin_or_end == 'begin'
        self.valid: bool = True


class Annotations:
    class List:
        def __init__(self):
            self.list: list = []
            self.idx: int = 0
            self.call_depth: int = 0

    def __init__(self, file: Path):
        self.cmdbuf: Annotations.List = Annotations.List()
        self.queue: Annotations.List = Annotations.List()

        with open(file, 'r') as f:
            for line_str in f.readlines():
                if '_annotation' not in line_str or '_end_' not in line_str:
                    continue
                line = Line(line_str)
                if line.valid:
                    self._add_annotation(line)

    def __str__(self) -> str:
        annotation_list = []
        if self.queue.call_depth > 0 and self.queue.idx < len(self.queue.list):
            annotation_list.append(self.queue.list[self.queue.idx])
        if self.cmdbuf.call_depth > 0 and self.cmdbuf.idx < len(self.cmdbuf.list):
            annotation_list.append(self.cmdbuf.list[self.cmdbuf.idx])
        if annotation_list:
            return f'; annotations={" / ".join(annotation_list)}'
        return ''

    def _add_annotation(self, line: Line) -> None:
        # TODO: handle nested annotations properly.
        # Currently, nested annotation will be out of order
        arg_dict = dict(re.findall(r'(\w+)=([^,]+)', line.args))
        if line.func == 'cmd_buffer_annotation':
            # expect: command_buffer_handle=<num>, str=<str>
            self.cmdbuf.list.append(arg_dict['str'])
        if line.func == 'queue_annotation':
            # expect: str=<str>
            self.queue.list.append(arg_dict['str'])

    def begin_cmdbuf(self) -> None:
        self.cmdbuf.call_depth += 1

    def end_cmdbuf(self) -> None:
        self.cmdbuf.idx += 1
        self.cmdbuf.call_depth -= 1

    def begin_queue(self) -> None:
        self.queue.call_depth += 1

    def end_queue(self) -> None:
        self.queue.idx += 1
        self.queue.call_depth -= 1

    def is_trace_internal(self) -> bool:
        trace_internal_msg = (
            'ApplyInitialContents',
            'FetchShaderFeedback for',
            'Initial state for ResourceId::',
            'Clear depth state for ResourceId::',
            'FillWithDiscardPattern ResourceId::',
        )
        active_annotations = str(self)
        return any(m in active_annotations for m in trace_internal_msg)

class Utrace_Parser:
    def __init__(self, cl_args: argparse.Namespace):
        self.cl_args = cl_args
        self.events: dict[str, float] = {}
        self.renderpass: int = 0
        self.next_renderpass: int = 1
        self.idx: int = 0
        self.prev_end_ts_ns: float = 0
        self.last_eop: float = 0
        self.annotations: Optional[Annotations] = None

        self.func: str
        self.args: str
        self.vs: str
        self.fs: str
        self.cs: str

    def get_header(self) -> str:
        if self.cl_args.mode == 'top':
            if self.cl_args.verbose:
                return 'start_ts,end_ts,frame,cmdbuf,rp,idx,event,count,vs,fs,cs,gap_us,time_us,details'
            return 'frame,cmdbuf,rp,idx,event,count,vs,fs,cs,gap_us,time_us'
        elif self.cl_args.mode == 'eop':
            if self.cl_args.verbose:
                return 'end_ts,frame,cmdbuf,rp,idx,event,count,vs,fs,cs,time_us,details'
            return 'frame,cmdbuf,rp,idx,event,count,vs,fs,cs,time_us'
        raise RuntimeError('invalid mode')

    def extract_count(self) -> None:
        if 'count' in self.args:
            filter = r'(.*)count=(\w+);?(.*)'
            match = re.search(filter, self.args)
            if match:
                self.count = match.group(2)
                self.args = match.group(1) + match.group(3)
        elif 'compute' in self.func:
            filter = r'(.*)group_x=(\w+); group_y=(\w+); group_z=(\w+);?(.*)'
            match = re.search(filter, self.args)
            if match:
                self.count = int(match.group(2)) * int(match.group(3)) * int(match.group(4))
                self.func += f'({match.group(2)}x{match.group(3)}x{match.group(4)})'
                self.args = match.group(1) + match.group(5)

    def extract_shaders(self) -> None:
        filter = r'(.*)(vs_hash=[0-9a-fx]+); (fs_hash=[0-9a-fx]+);?(.*)'
        match = re.search(filter, self.args)
        if match:
            self.vs = match.group(2).split('=')[1]
            self.fs = match.group(3).split('=')[1]
            self.args = match.group(1) + match.group(4)
            return

        filter = r'(.*)(cs_hash=0x[0-9a-f]+);?(.*)'
        match = re.search(filter, self.args)
        if match:
            self.cs = match.group(2).split('=')[1]
            self.args = match.group(1) + match.group(3)
            return

    def extract_blorp_op(self) -> None:
        filter = r'(.*)op=([A-Z_]+);?(.*)'
        match = re.search(filter, self.args)
        if match:
            self.func = match.group(2).lower()
            self.args = match.group(1) + match.group(3)

    def parse_line(self, line_str: str) -> Optional[str]:
        line = Line(line_str)
        if not line.valid:
            return None
        self.func = line.func

        if self.func == 'render_pass':
            if line.is_begin:
                self.renderpass = self.next_renderpass
                self.last_eop = line.timestamp
                return None
            self.next_renderpass = self.renderpass + 1
            self.renderpass = 0
            return None

        if self.func == 'cmd_buffer':
            if self.prev_end_ts_ns == 0:
                self.prev_end_ts_ns = line.timestamp
            self.last_eop = line.timestamp
            return None

        if self.annotations:
            if self.func == 'cmd_buffer_annotation':
                if line.is_begin:
                    self.annotations.begin_cmdbuf()
                else:
                    self.annotations.end_cmdbuf()
                return None
            if self.func == 'queue_annotation':
                if line.is_begin:
                    self.annotations.begin_queue()
                else:
                    self.annotations.end_queue()
                return None
            if self.cl_args.trace and self.annotations.is_trace_internal():
                return None

        ignore_funcs = {'frame': True}
        if ignore_funcs.get(self.func, None):
            return None

        self.count = 0
        self.vs = ''
        self.fs = ''
        self.cs = ''
        self.args = line.args.replace(',', ';').strip(';')

        if self.cl_args.mode == 'top':
            result = self.parse_top(line)
        elif self.cl_args.mode == 'eop':
            result = self.parse_eop(line)
        else:
            raise RuntimeError('invalid mode')
        return result

    def parse_top(self, line: Line) -> Optional[str]:
        if line.is_begin:
            self.events[self.func] = line.timestamp
            return None

        # end event
        begin_ts_ns = self.events.pop(self.func, 0)
        end_ts_ns = line.timestamp
        gap_ts_us = (begin_ts_ns - self.prev_end_ts_ns) / 1000
        delta_ts_us = (end_ts_ns - begin_ts_ns) / 1000

        self.extract_count()
        if 'draw' in line.name or 'compute' in line.name:
            self.extract_shaders()
            self.idx += 1
        elif 'blorp' in line.name:
            self.extract_blorp_op()
            self.idx += 1
        self.args = self.args.strip().lstrip('+')

        if self.cl_args.verbose:
            out = [f'{begin_ts_ns:.0f}', f'{end_ts_ns:.0f}', line.frame,
                   line.cmd_buf, self.renderpass, self.idx, self.func,
                   self.count, self.vs, self.fs, self.cs,
                   f'{gap_ts_us:.3f}', f'{delta_ts_us:.3f}',
                   str(self.args + str(self.annotations)).strip(' ;')]
        else:
            out = [line.frame, line.cmd_buf, self.renderpass, self.idx,
                   self.func, self.count, self.vs, self.fs, self.cs,
                   f'{gap_ts_us:.3f}', f'{delta_ts_us:.3f}']

        self.prev_end_ts_ns = end_ts_ns
        result = ','.join(v if isinstance(v, str) else str(v) for v in out)
        return result

    def parse_eop(self, line: Line) -> Optional[str]:
        if line.is_begin:
            return None

        # end event
        start_ts_ns = self.last_eop
        end_ts_ns = line.timestamp
        delta_ts_us = max(0, (end_ts_ns - start_ts_ns) / 1000)

        self.extract_count()
        if 'draw' in line.name or 'compute' in line.name:
            self.extract_shaders()
            self.idx += 1
        elif 'blorp' in line.name:
            self.extract_blorp_op()
            self.idx += 1
        self.args = self.args.strip().lstrip('+')

        if self.cl_args.verbose:
            out = [f'{end_ts_ns:.0f}', line.frame,
                   line.cmd_buf, self.renderpass, self.idx, self.func,
                   self.count, self.vs, self.fs, self.cs, f'{delta_ts_us:.3f}',
                   str(self.args + str(self.annotations)).strip(' ;')]
        else:
            out = [line.frame, line.cmd_buf, self.renderpass, self.idx,
                   self.func, self.count, self.vs, self.fs, self.cs,
                   f'{delta_ts_us:.3f}']

        self.last_eop = line.timestamp
        result = ','.join(v if isinstance(v, str) else str(v) for v in out)
        return result

class CustomArgumentParser(argparse.ArgumentParser):
    examples = """
Examples
========
> Generate csv of EndToEndOfPipe gpu events while running <cmd>. Events running
> in parallel.

    MESA_GPU_TRACES=print_csv MESA_GPU_TRACEFILE=/tmp/ut.csv <cmd>
    intel_measure.py --eop /tmp/ut.csv > im.csv

> Generate csv of TopToEndOfPipe gpu events while running <cmd>. Use stalls to
> avoid overlapping events:

    MESA_GPU_TRACES=print_csv MESA_GPU_TRACEFILE=/tmp/ut.csv INTEL_DEBUG=stall <cmd>
    intel_measure.py --top /tmp/ut.csv > im.csv

> Generate csv from gpu events of trace replay, filtering out events that are marked
> as trace internal with annotations. These events did not appear in original game.

    MESA_GPU_TRACES=print_csv MESA_GPU_TRACEFILE=/tmp/ut.csv <cmd>
    intel_measure.py -t /tmp/ut.csv > im.csv
"""
    def format_help(self):
        return super().format_help() + self.examples

def main():
    cl_parser = CustomArgumentParser()
    cl_parser.add_argument('utrace_log', type=Path, help='path to utrace log to parse')
    cl_parser.add_argument('-v', '--verbose', default=False, action='store_true', help='dump all fields to output')
    cl_parser.add_argument('-f', '--file', type=Path, default=None, help='save results to file')
    cl_parser.add_argument(      '--eop', action='store_const', const='eop', dest='mode', help='use end-to-end-of-pipe gpu measurements (default)')
    cl_parser.add_argument(      '--top', action='store_const', const='top', dest='mode', help='use top-to-end-of-pipe gpu measurements')
    cl_parser.add_argument('-t', '--trace', default=False, action='store_true', help='remove trace internal events')
    cl_parser.set_defaults(mode='eop')

    cl_args = cl_parser.parse_args()
    if not cl_args.utrace_log.exists():
        print(f'utrace log not found: "{cl_args.utrace_log}"', sys.stderr)
        sys.exit(1)

    parser = Utrace_Parser(cl_args)
    if cl_args.verbose or cl_args.trace:
        parser.annotations = Annotations(cl_args.utrace_log)

    if cl_args.file:
        file = open(cl_args.file, 'w')
    else:
        file = sys.stdout

    with open(cl_args.utrace_log, 'r') as f:
        print(parser.get_header(), file=file)
        for line in f.readlines():
            result = parser.parse_line(line)
            if result:
                print(result, file=file)

    if cl_args.file:
        file.close()
    return 0

if __name__ == '__main__':
    sys.exit(main())
