# Copyright (c) 2026 Arm Ltd.
# Copyright (c) 2021 Collabora, Ltd.
# SPDX-License-Identifier: MIT

import ast
import argparse
import textwrap
import os
import datetime
from dataclasses import dataclass,field
from pathlib import Path
import xml.etree.ElementTree as et


TAB_SIZE = 3
COUNTERINFO_RELPATH = "database/counterinfo"
HARDWARE_LAYOUT_RELPATH = "database/hardwarelayout"

DERIVED_COUNTERS: list[str, "CounterInfo"] = []

class SourceFile:
   def __init__(self, filename):
      self.file = open(filename, 'w')
      self._indent = 0

   def write(self, *args):
      code = ' '.join(map(str, args))
      for line in code.splitlines():
         text = ''.rjust(self._indent) + line
         self.file.write(text.rstrip() + "\n")

   def indent(self, n):
      self._indent += n

   def outdent(self, n):
      self._indent -= n


@dataclass(frozen=True)
class HardwareCounter:
   block: str
   index: int


@dataclass
class HardwareLayout:
   gpu_name: str
   counters: dict[str, HardwareCounter]

   @staticmethod
   def from_xml(xml: et.Element) -> "HardwareLayout":
      gpu_name = xml.get("gpu")
      counters = {}
      assert gpu_name is not None
      for cbe in xml.findall("CounterBlock"):
         blk_type = cbe.get("type")
         assert blk_type is not None
         for counter in cbe.findall("Counter"):
            counter_name = counter.get("name")
            counter_index = counter.get("index")
            assert counter_index is not None
            counters[counter_name] = HardwareCounter(blk_type, int(counter_index))
         sorted(counters, key=lambda counter: counter[1])

      return HardwareLayout(gpu_name=gpu_name, counters=counters)


def parse_hw_layout(path: Path):
   xml = et.parse(path)
   return HardwareLayout.from_xml(xml.getroot())


def map_nn(v, f):
   return None if v is None else f(v)


class SanitizeEquation(ast.NodeTransformer):
   def visit_BinOp(self, node):
      self.generic_visit(node)
      if isinstance(node.op, ast.Div):
         return ast.Call(func=ast.Name(id='safe_div', ctx=ast.Load()), args=[node.left, node.right], keywords=[])
      else:
         return node


def sanitize_equation(equation, units):
   if equation == '':
      return equation
   equation = ast.unparse(ast.fix_missing_locations(SanitizeEquation().visit(ast.parse(equation))))

   # We don't want negative values, regarless of the units, and we clamp to
   # percentage to 100
   equation = 'max({}, 0.)'.format(equation)
   if units == 'percent':
      equation = 'min({}, 100.)'.format(equation)
   return equation

def extract_sources(counter, counters, hwlayout):
   if counter.source_name:
      return {counter.machine_name: hwlayout.counters[counter.source_name]}
   elif counter.equation == '':
      return {}

   sources = {}
   tree = ast.parse(counter.equation)
   for node in ast.walk(tree):
      if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load):
         if node.id in counters:
            subcounter = counters[node.id]
            subsources = extract_sources(subcounter, counters, hwlayout)
            sources.update(subsources)
   return sources


def get_elem_text(xml, name):
   e = xml.find(name)
   if e is not None:
      return e.text
   else:
      return None


def parse_supported_gpus(xml):
   supported_list = xml.find("SupportedGPUs")
   return [e.text for e in supported_list.findall("GPU")]


@dataclass
class CounterInfo:
   machine_name: str
   block_type: str
   equation: str = ""
   source_name: str = ""
   human_name: str = ""
   short_desc: str = ""
   units: str = ""
   perfetto_groups: list[str] = field(default_factory=[])

   @staticmethod
   def from_xml(xml, blk_type, hwlayout):
      machine_name = get_elem_text(xml, "MachineName")
      assert machine_name is not None
      supported = parse_supported_gpus(xml)

      if hwlayout.gpu_name not in supported:
         return None

      desc_raw = get_elem_text(xml, "ShortDescription") or ""
      desc_san = " ".join(map(str.strip, desc_raw.splitlines())).strip()
      source_name = get_elem_text(xml, "SourceName") or ""
      if source_name != "" and source_name not in hwlayout.counters:
         for source_alias in xml.findall("SourceAlias"):
            if source_alias.text in hwlayout.counters:
               source_name = source_alias.text

      perfetto_groups = []
      perfetto_node = xml.find("Perfetto")
      if perfetto_node != None:
         groups_node = perfetto_node.find("Groups")
         for g in groups_node.findall("GroupName"):
            perfetto_groups.append(g.text)

      assert(source_name == "" or source_name in hwlayout.counters)
      units = (get_elem_text(xml, "Units") or "").strip()
      equation = map_nn(get_elem_text(xml, "Equation"), str.strip) or ""
      equation = sanitize_equation(equation, units)

      return CounterInfo(
          machine_name,
          blk_type,
          equation=equation,
          source_name=source_name,
          human_name=get_elem_text(xml, "HumanName") or "",
          short_desc=desc_san,
          units=units,
          perfetto_groups=perfetto_groups,
      )

   def is_derived(self):
      return not self.source_name

   def get_hw_counter(self, hwlayout: HardwareLayout) -> HardwareCounter:
      assert self.source_name != ""
      if self.source_name in hwlayout.counters:
         return hwlayout.counters[self.source_name]

   def is_supported(self):
      return "MALI_CONFIG_TIME_SPAN" not in self.equation


@dataclass
class PerfettoGroup:
   name: str
   counters: list[CounterInfo]


def blk_type_from_filename(fname):
   # This maps to the values of the "type" field in the CounterBlock xml blocks.
   fname_to_dbkey = {
       "GPUFrontEnd": "GPU Front-end",
       "L2Cache": "Memory System",
       "Memory-External": "Memory System",
       "Memory-L1-to-L2": "Memory System",
       "Memory-MMU": "Memory System",
       "Tiler": "Tiler",
       "ShaderCore": "Shader Core",
       "Constants": None,
       "Content": None,
   }
   for name, key in fname_to_dbkey.items():
      if name in fname:
         return key
   print(fname, "not found")
   assert False and "could not find group from filename"


def parse_counters(path: Path, hwlayout: HardwareLayout,
                   counters: dict[str, CounterInfo]):
   blk_type = blk_type_from_filename(path.name)
   xml = et.parse(path)
   for e in xml.findall("CounterInfo"):
      counter = CounterInfo.from_xml(e, blk_type, hwlayout)
      if counter != None:
         counters[counter.machine_name] = counter


def get_counters(hwlayout: HardwareLayout,
                 counters: dict[str, CounterInfo]):
   counterinfo_path = Path(__file__).parent / COUNTERINFO_RELPATH
   for f in counterinfo_path.glob("*.xml"):
      parse_counters(f, hwlayout, counters)


def sanitize_name_for_c(name):
    return name.replace(' ', '_').replace('-', '_').lower()


TO_MALI_BLOCK_TYPE = {
   'GPU Front-end': 'MALI_PERF_BLOCK_GPU_FRONT_END',
   'Job Manager': 'MALI_PERF_BLOCK_GPU_FRONT_END',
   'CSF': 'MALI_PERF_BLOCK_GPU_FRONT_END',
   'Memory System': 'MALI_PERF_BLOCK_MEMSYS',
   'L2 Cache': 'MALI_PERF_BLOCK_MEMSYS',
   'Tiler': 'MALI_PERF_BLOCK_TILER',
   'Shader Core': 'MALI_PERF_BLOCK_SHADER_CORE',
}


TO_MALI_CLASS = {
   'Unclassified': 'MALI_PERF_UNCLASSIFIED',
   'System': 'MALI_PERF_SYSTEM',
   'Vertices': 'MALI_PERF_VERTICES',
   'Fragments': 'MALI_PERF_FRAGMENTS',
   'Primitives': 'MALI_PERF_PRIMITIVES',
   'Memory': 'MALI_PERF_MEMORY',
   'Compute': 'MALI_PERF_COMPUTE',
   'Ray tracing': 'MALI_PERF_RAY_TRACING',
}


TO_MALI_UNITS = {
   'beats': 'MALI_PERF_COUNTER_UNITS_BEATS',
   'bits': 'MALI_PERF_COUNTER_UNITS_BITS',
   'boxes': 'MALI_PERF_COUNTER_UNITS_BOXES',
   'bytes': 'MALI_PERF_COUNTER_UNITS_BYTES',
   'bytes/second': 'MALI_PERF_COUNTER_UNITS_BYTES_PER_SECOND',
   'cycles': 'MALI_PERF_COUNTER_UNITS_CYCLES',
   'instances': 'MALI_PERF_COUNTER_UNITS_INSTANCES',
   'instructions': 'MALI_PERF_COUNTER_UNITS_INSTRUCTIONS',
   'interrupts': 'MALI_PERF_COUNTER_UNITS_INTERRUPTS',
   'issues': 'MALI_PERF_COUNTER_UNITS_ISSUES',
   'jobs': 'MALI_PERF_COUNTER_UNITS_JOBS',
   'nodes': 'MALI_PERF_COUNTER_UNITS_NODES',
   'percent': 'MALI_PERF_COUNTER_UNITS_PERCENT',
   'pixels': 'MALI_PERF_COUNTER_UNITS_PIXELS',
   'primitives': 'MALI_PERF_COUNTER_UNITS_PRIMITIVES',
   'quads': 'MALI_PERF_COUNTER_UNITS_QUADS',
   'rays': 'MALI_PERF_COUNTER_UNITS_RAYS',
   'requests': 'MALI_PERF_COUNTER_UNITS_REQUESTS',
   'tasks': 'MALI_PERF_COUNTER_UNITS_TASKS',
   'tests': 'MALI_PERF_COUNTER_UNITS_TESTS',
   'threads': 'MALI_PERF_COUNTER_UNITS_THREADS',
   'tiles': 'MALI_PERF_COUNTER_UNITS_TILES',
   'transactions': 'MALI_PERF_COUNTER_UNITS_TRANSACTIONS',
   'warps': 'MALI_PERF_COUNTER_UNITS_WARPS',
}



def main():
   parser = argparse.ArgumentParser()
   parser.add_argument("--hwlayout", help="Hardware layout xml", required=True)
   parser.add_argument("--code", help="C file to write", required=True)

   args = parser.parse_args()

   hwlayout = parse_hw_layout(Path(args.hwlayout))
   counters = {}
   get_counters(hwlayout, counters)

   c = SourceFile(args.code)

   tab_size = TAB_SIZE

   copyright = textwrap.dedent("""\
      /* Autogenerated file, DO NOT EDIT manually!
       * Generated by {} using XMLs copied from
       * https://github.com/ARM-software/libGPUCounters which is
       *  Copyright (c) 2023-2025 Arm Limited
       *  SPDX-License-Identifier: MIT
       *
       * Copyright © {year} Arm Limited
       * Copyright © {year} Collabora Ltd.
       * SPDX-License-Identifier: MIT
       */

      """).format(os.path.basename(__file__), year=datetime.datetime.now().year)

   c.write(copyright)
   c.write("#include \"mali_perf.h\"")
   c.write(textwrap.dedent("""\

      #include <math.h>

      #include <util/macros.h>

      #include <lib/pan_props.h>

      #include "mali_perf.h"

      #define GET_RAW_COUNTER_VALUE(...)                                            \\
              ({                                                                    \\
                 struct mali_perf_hw_counter_id id = {                              \\
                    __VA_ARGS__,                                                    \\
                 };                                                                 \\
                                                                                    \\
                 (double)backend->get_hw_counter_value(backend, id);                \\
              })

      #define MALI_CONFIG_EXT_BUS_BYTE_SIZE (double)constants->ext_bus_byte_size
      #define MALI_CONFIG_L2_CACHE_COUNT (double)constants->l2_cache_count
      #define MALI_CONFIG_SHADER_CORE_COUNT (double)constants->shader_core_count
      #define MALI_CONFIG_TIME_SPAN                                                 \\
              (double)(MIN2(dump_info->time_span.end_ns - dump_info->time_span.start_ns, 1))

      /* We seem to have a few cases where the dividend is low and the divisor
       * is zero. Let's assume a divisor of zero means we just had an
       * inconsistent sample, where the dividend should have been zero as well.
       */
      #define safe_div(dividend, divisor)                                           \\
              ({                                                                    \\
                 double _divisor = divisor;                                         \\
                 _divisor != 0.0 ? (dividend) / _divisor : 0;                       \\
              })

      #define max(...)                                                              \\
              ({                                                                    \\
                 const double _vals[] = {__VA_ARGS__};                              \\
                 double _ret = _vals[0];                                            \\
                 for (unsigned i = 1; i < ARRAY_SIZE(_vals); i++)                   \\
                    _ret = MAX2(_vals[i], _ret);                                    \\
                 _ret;                                                              \\
              })

      #define min(...)                                                              \\
              ({                                                                    \\
                 const double _vals[] = {__VA_ARGS__};                              \\
                 double _ret = _vals[0];                                            \\
                 for (unsigned i = 1; i < ARRAY_SIZE(_vals); i++)                   \\
                    _ret = MIN2(_vals[i], _ret);                                    \\
                 _ret;                                                              \\
              })

      #define check_res(expr)                                                       \\
              ({                                                                    \\
                 double _ret = expr;                                                \\
                 assert(_ret != INFINITY && _ret != -INFINITY);                     \\
                 assert(_ret != NAN);                                               \\
                 assert(_ret >= (double)INT64_MIN && _ret <= (double)INT64_MAX);    \\
                 _ret;                                                              \\
              })

      """))

   for hwcounter_name, hwcounter in hwlayout.counters.items():
      mali_blk_type = TO_MALI_BLOCK_TYPE[hwcounter.block]
      args = ""
      if hwcounter.block in ['L2 Cache', 'Memory System', 'Shader Core']:
         args += ".block.index = blk_idx, ".format()
      args += ".block.type = {}, .index = {}".format(mali_blk_type, hwcounter.index)
      c.write("#define {} GET_RAW_COUNTER_VALUE({})".format(hwcounter_name, args))


   c.write("\n")

   for counter_name, counter in counters.items():
      if counter.source_name != "":
         assert(counter.equation == '')
         c.write("#define {} (double){}".format(counter.machine_name, counter.source_name))
      else:
         assert(counter.equation != '')
         c.write("#define {} (double)({})".format(counter.machine_name, counter.equation))

   c.write("\n")

   for counter_name, counter in counters.items():
      c.write(textwrap.dedent("""\
      static inline double
      get_{0}(struct mali_perf_backend *backend, uint8_t blk_idx,
         const struct mali_perf_constants *constants,
         const struct mali_perf_dump_info *dump_info)
      {{
         return check_res({0});
      }}\n
      """).format(counter.machine_name))

   c.write("\n")

   c.write("static const struct mali_perf_counter counters[] = {")
   for counter_name, counter in counters.items():
      sources = extract_sources(counter, counters, hwlayout)
      if counter.block_type != None:
         blk_type = TO_MALI_BLOCK_TYPE[counter.block_type]
      else:
         blk_type = "MALI_PERF_BLOCK_NONE"

      c.write("   {")
      c.write("      .name = \"{}\",".format(counter.machine_name))
      c.write("      .desc = \"{}\",".format(counter.short_desc))
      c.write("      .units = {},".format(TO_MALI_UNITS[counter.units]))
      c.write("      .block = {},".format(blk_type))
      c.write("      .get_value = get_{},".format(counter.machine_name))
      c.write("      .sources = (struct mali_perf_counter_source[]){")
      for source_name, source in sources.items():
         c.write("         {{ .block = {}, .counter = {} }},".format(TO_MALI_BLOCK_TYPE[source.block], source.index))
      c.write("         { .block = MALI_PERF_BLOCK_NONE, /* sentinel */ },")
      c.write("      },")

      if len(counter.perfetto_groups) == 0:
         classes = "BITFIELD64_BIT(MALI_PERF_UNCLASSIFIED)"
      elif len(counter.perfetto_groups) > 1:
         classes = "("
      else:
         classes = ""

      for idx, group in enumerate(counter.perfetto_groups):
         if idx > 0:
            classes += " | "
         classes += "BITFIELD64_BIT({})".format(TO_MALI_CLASS[group])

      if len(counter.perfetto_groups) > 1:
         classes += ")"

      c.write("      .classes = {}".format(classes))
      c.write("   },")
   c.write("   { /* sentinel */ },")
   c.write("};")

   c.write("\n")

   gpu_name = hwlayout.gpu_name.removeprefix("Mali-").removeprefix("Mali ").lower()
   c.write("const struct mali_perf_gpu_info mali_perf_{} = {{".format(gpu_name))
   c.write("   .counters = counters,")
   c.write("};")


if __name__ == '__main__':
   main()
