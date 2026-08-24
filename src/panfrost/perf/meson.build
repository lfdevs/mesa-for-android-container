# Copyright © 2021 Collabora, Ltd.
# Author: Antonio Caggiano <antonio.caggiano@collabora.com>
# SPDX-License-Identifier: MIT

mali_perf_gpus = {
  'g1': 'database/hardwarelayout/Mali-G1.xml',
  'g31': 'database/hardwarelayout/Mali-G31.xml',
  'g51': 'database/hardwarelayout/Mali-G51.xml',
  'g52': 'database/hardwarelayout/Mali-G52.xml',
  'g710': 'database/hardwarelayout/Mali-G710.xml',
  'g715': 'database/hardwarelayout/Mali-G715.xml',
  'g71': 'database/hardwarelayout/Mali-G71.xml',
  'g720': 'database/hardwarelayout/Mali-G720.xml',
  'g725': 'database/hardwarelayout/Mali-G725.xml',
  'g72': 'database/hardwarelayout/Mali-G72.xml',
  'g76': 'database/hardwarelayout/Mali-G76.xml',
  'g77': 'database/hardwarelayout/Mali-G77.xml',
  'g78': 'database/hardwarelayout/Mali-G78.xml',
  't720': 'database/hardwarelayout/Mali-T720.xml',
  't760': 'database/hardwarelayout/Mali-T760.xml',
  't830': 'database/hardwarelayout/Mali-T830.xml',
  't880': 'database/hardwarelayout/Mali-T880.xml',
}

# Only here to make sure mali_perf.c gets regenerated if
# something changes in the XML. The python script will
# assumes the database directly is placed at the same
# level the mali_gen_perf.py lives.
mali_perf_counterinfo = [
  'database/counterinfo/Mali-CounterInfo-00-Constants.xml',
  'database/counterinfo/Mali-CounterInfo-01a-GPUFrontEnd-Scheduling.xml',
  'database/counterinfo/Mali-CounterInfo-01b-GPUFrontEnd-Workload.xml',
  'database/counterinfo/Mali-CounterInfo-01c-GPUFrontEnd-Subsystem.xml',
  'database/counterinfo/Mali-CounterInfo-02a-Memory-External.xml',
  'database/counterinfo/Mali-CounterInfo-02b-Memory-L2Cache.xml',
  'database/counterinfo/Mali-CounterInfo-02c-Memory-L1-to-L2.xml',
  'database/counterinfo/Mali-CounterInfo-02d-Memory-MMU.xml',
  'database/counterinfo/Mali-CounterInfo-03a-Tiler-Vertices.xml',
  'database/counterinfo/Mali-CounterInfo-03b-Tiler-Primitives.xml',
  'database/counterinfo/Mali-CounterInfo-03d-Tiler-Subsystem.xml',
  'database/counterinfo/Mali-CounterInfo-04a-ShaderCore-Frontend.xml',
  'database/counterinfo/Mali-CounterInfo-04b-ShaderCore-FrontendPrimitives.xml',
  'database/counterinfo/Mali-CounterInfo-04c-ShaderCore-FrontendRastAndZS.xml',
  'database/counterinfo/Mali-CounterInfo-04d-ShaderCore-Program.xml',
  'database/counterinfo/Mali-CounterInfo-04e-ShaderCore-Backpressure.xml',
  'database/counterinfo/Mali-CounterInfo-04f-ShaderCore-TextureUnit.xml',
  'database/counterinfo/Mali-CounterInfo-04g-ShaderCore-RayTracingUnit.xml',
  'database/counterinfo/Mali-CounterInfo-04h-ShaderCore-OtherUnits.xml',
  'database/counterinfo/Mali-CounterInfo-05-Content.xml',
]

mali_perf_sources = ['mali_perf.h']

foreach gpu_name, hwlayout_xml : mali_perf_gpus
  mali_perf_sources += custom_target(
    'mali-perf-' + gpu_name,
    input : [
      'mali_gen_perf.py',
      hwlayout_xml,
      mali_perf_counterinfo,
    ],
    output : 'mali_perf_' + gpu_name + '.c',
    command : [
      prog_python, '@INPUT0@',
      '--hwlayout', '@INPUT1@',
      '--code', '@OUTPUT@',
    ],
  )
endforeach

pan_perf_sources = [
  'pan_perf.c',
  mali_perf_sources,
]

libpanfrost_perf = static_library(
  'panfrost_perf',
  pan_perf_sources,
  include_directories : [inc_include, inc_src, inc_panfrost],
  dependencies: libpanfrost_dep
)

dep_panfrost_perf = declare_dependency(
  link_with: libpanfrost_perf,
  include_directories: [inc_panfrost, inc_src, inc_include]
)

panfrost_quick = executable(
  'panquick',
  'quick.c',
  include_directories : [inc_include, inc_src, inc_panfrost],
  dependencies : [
    dep_libdrm,
    libpanfrost_dep,
    dep_panfrost_perf,
  ],
  build_by_default : with_tools.contains('panfrost')
)
