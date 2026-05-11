# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

gen_xml_files = [
  'gen40.xml',
  'gen45.xml',
  'gen50.xml',
  'gen60.xml',
  'gen70.xml',
  'gen75.xml',
  'gen80.xml',
  'gen90.xml',
  'gen110.xml',
  'gen120.xml',
  'gen125.xml',
  'xe2.xml',
  'xe3.xml',
  'xe3p.xml',
]

gen_xml_rt_files = [
  'gen125_rt.xml',
  'xe2_rt.xml',
  'xe3_rt.xml',
  'xe3p_rt.xml',
]

gen_pack_header_deps = files('intel_genxml.py', 'util.py')
foreach f : gen_xml_files + gen_xml_rt_files
  gen_pack_header_deps += files(f)
endforeach

genX_xml_h = custom_target(
  'genX_xml.h',
  input : ['../../util/gen_zipped_xml_file.py', gen_xml_files],
  output : 'genX_xml.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

genX_bits_included_symbols = [
  # instructions
  'COMPUTE_WALKER',
  'GPGPU_WALKER',
  'MEDIA_CURBE_LOAD',
  'MEDIA_INTERFACE_DESCRIPTOR_LOAD',
  'MEDIA_STATE_FLUSH',
  'MEDIA_VFE_STATE',
  'MI_ARB_CHECK',
  '3DMESH_3D',
  '3DPRIMITIVE',
  '3DPRIMITIVE_EXTENDED',
  '3DSTATE_CLEAR_PARAMS',
  '3DSTATE_CLIP',
  '3DSTATE_CLIP_MESH',
  '3DSTATE_CONSTANT_ALL',
  '3DSTATE_CONSTANT_VS',
  '3DSTATE_DS',
  '3DSTATE_GS',
  '3DSTATE_HS',
  '3DSTATE_INDEX_BUFFER',
  '3DSTATE_MESH_CONTROL',
  '3DSTATE_MESH_DISTRIB',
  '3DSTATE_MESH_SHADER',
  '3DSTATE_MESH_SHADER_DATA',
  '3DSTATE_PRIMITIVE_REPLICATION',
  '3DSTATE_PS',
  '3DSTATE_PS_BLEND',
  '3DSTATE_PS_EXTRA',
  '3DSTATE_PUSH_CONSTANT_ALLOC_VS',
  '3DSTATE_RASTER',
  '3DSTATE_SBE',
  '3DSTATE_SBE_MESH',
  '3DSTATE_SBE_SWIZ',
  '3DSTATE_SF',
  '3DSTATE_STREAMOUT',
  '3DSTATE_TASK_CONTROL',
  '3DSTATE_TASK_REDISTRIB',
  '3DSTATE_TASK_SHADER',
  '3DSTATE_TASK_SHADER_DATA',
  '3DSTATE_TE',
  '3DSTATE_URB_ALLOC_MESH',
  '3DSTATE_URB_ALLOC_TASK',
  '3DSTATE_URB_ALLOC_VS',
  '3DSTATE_URB_VS',
  '3DSTATE_VF_INSTANCING',
  '3DSTATE_VF_SGVS',
  '3DSTATE_VF_SGVS_2',
  '3DSTATE_VF_TOPOLOGY',
  '3DSTATE_VFG',
  '3DSTATE_WM',
  '3DSTATE_VS',
  'PIPE_CONTROL',
  'RESOURCE_BARRIER',
  # instruction fields
  '3DSTATE_CPSIZE_CONTROL_BUFFER::Surface Base Address',
  '3DSTATE_CPSIZE_CONTROL_BUFFER::Surface Pitch',
  '3DSTATE_DEPTH_BUFFER::Surface Base Address',
  '3DSTATE_DEPTH_BUFFER::Surface Pitch',
  '3DSTATE_HIER_DEPTH_BUFFER::Surface Base Address',
  '3DSTATE_HIER_DEPTH_BUFFER::Surface Pitch',
  '3DSTATE_STENCIL_BUFFER::Surface Base Address',
  '3DSTATE_STENCIL_BUFFER::Surface Pitch',
  '3DSTATE_SO_BUFFER::Surface Base Address',
  '3DSTATE_SO_BUFFER::Stream Offset',
  'MI_BATCH_BUFFER_START::Batch Buffer Start Address',
  'MI_REPORT_PERF_COUNT::Memory Address',
  'MI_STORE_DATA_IMM::Address',
  'MI_STORE_DATA_IMM::Immediate Data',
  'MI_STORE_REGISTER_MEM::Memory Address',
  '3DSTATE_DEPTH_BUFFER::Surface Base Address',
  '3DSTATE_DEPTH_BUFFER::Surface Pitch',
  '3DSTATE_STENCIL_BUFFER::Surface Base Address',
  '3DSTATE_STENCIL_BUFFER::Surface Pitch',
  '3DSTATE_HIER_DEPTH_BUFFER::Surface Base Address',
  '3DSTATE_HIER_DEPTH_BUFFER::Surface Pitch',
  '3DSTATE_DS',
  '3DSTATE_HS',
  '3DSTATE_CLEAR_PARAMS',
  '3DSTATE_SO_BUFFER::Surface Base Address',
  '3DSTATE_SO_BUFFER::Stream Offset',
  '3DSTATE_CPSIZE_CONTROL_BUFFER::Surface Base Address',
  '3DSTATE_CPSIZE_CONTROL_BUFFER::Surface Pitch',
  'COMPUTE_WALKER::body',
  'EXECUTE_INDIRECT_DISPATCH::body',
  # structures
  '3DSTATE_CONSTANT_ALL_DATA',
  'RENDER_SURFACE_STATE::Surface Base Address',
  'RENDER_SURFACE_STATE::Surface Pitch',
  'RENDER_SURFACE_STATE::Surface QPitch',
  'RENDER_SURFACE_STATE::Auxiliary Surface Base Address',
  'RENDER_SURFACE_STATE::Auxiliary Surface Pitch',
  'RENDER_SURFACE_STATE::Clear Value Address',
  'RENDER_SURFACE_STATE::Red Clear Color',
  'RENDER_SURFACE_STATE::Green Clear Color',
  'RENDER_SURFACE_STATE::Blue Clear Color',
  'RENDER_SURFACE_STATE::Alpha Clear Color',
  'RENDER_SURFACE_STATE::Width',
  'RENDER_SURFACE_STATE::Height',
  'RENDER_SURFACE_STATE::Depth',
  'RENDER_SURFACE_STATE::Surface Format',
  'RENDER_SURFACE_STATE::Surface Type',
  'RENDER_SURFACE_STATE::Render Target View Extent',
  'RENDER_SURFACE_STATE::Tile Mode',
  'CLEAR_COLOR',
  'VERTEX_BUFFER_STATE::Buffer Starting Address',
  'CPS_STATE',
  'INTERFACE_DESCRIPTOR_DATA',
  'RT_DISPATCH_GLOBALS::Hit Group Table',
  'RT_DISPATCH_GLOBALS::Miss Group Table',
  'RT_DISPATCH_GLOBALS::Callable Group Table',
  'RT_DISPATCH_GLOBALS::Hit Group Stride',
  'RT_DISPATCH_GLOBALS::Miss Group Stride',
  'RT_DISPATCH_GLOBALS::Callable Group Stride',
  'RT_DISPATCH_GLOBALS::Launch Width',
  'RT_DISPATCH_GLOBALS::Launch Height',
  'RT_DISPATCH_GLOBALS::Launch Depth',
  'SAMPLER_STATE',
  'VERTEX_ELEMENT_STATE',
]

genX_bits_h = custom_target(
  'genX_bits.h',
  input : ['gen_bits_header.py', gen_xml_files, gen_xml_rt_files],
  output : 'genX_bits.h',
  command : [prog_python, '@INPUT@', '-o', '@OUTPUT@',
             '--include-symbols', ','.join(genX_bits_included_symbols)],
  depend_files: gen_pack_header_deps
)

gen_xml_pack = []
foreach f : gen_xml_files + gen_xml_rt_files
  _name = '@0@_pack.h'.format(f.split('.')[0])
  gen_xml_pack += custom_target(
    _name,
    input : ['gen_pack_header.py', f],
    output : _name,
    command : [prog_python, '@INPUT@', '--engines=render,blitter,compute'],
    capture : true,
    depend_files: gen_pack_header_deps
  )
endforeach

gen_xml_video_pack = []
foreach f : gen_xml_files
  _name = '@0@_video_pack.h'.format(f.split('.')[0])
  gen_xml_pack += custom_target(
    _name,
    input : ['gen_pack_header.py', f],
    output : _name,
    command : [prog_python, '@INPUT@', '--engines=video'],
    capture : true,
    depend_files: gen_pack_header_deps
  )
endforeach

genX_cl_included_symbols = [
  # instructions
  '3DMESH_3D',
  '3DSTATE_CLIP',
  '3DSTATE_CONSTANT_ALL',
  '3DSTATE_CONSTANT_VS',
  '3DSTATE_DS',
  '3DSTATE_GS',
  '3DSTATE_HS',
  '3DSTATE_INDEX_BUFFER',
  '3DSTATE_MESH_CONTROL',
  '3DSTATE_MESH_SHADER_DATA',
  '3DSTATE_PS',
  '3DSTATE_PS_EXTRA',
  '3DSTATE_PS_BLEND',
  '3DSTATE_RASTER',
  '3DSTATE_SF',
  '3DSTATE_STREAMOUT',
  '3DSTATE_TASK_CONTROL',
  '3DSTATE_TASK_SHADER_DATA',
  '3DSTATE_TE',
  '3DSTATE_VERTEX_BUFFERS',
  '3DSTATE_VF_TOPOLOGY',
  '3DSTATE_VFG',
  '3DSTATE_VS',
  '3DSTATE_WM',
  '3DPRIMITIVE',
  '3DPRIMITIVE_EXTENDED',
  'COMPUTE_WALKER',
  'GPGPU_WALKER',
  'MEDIA_CURBE_LOAD',
  'MEDIA_INTERFACE_DESCRIPTOR_LOAD',
  'MEDIA_STATE_FLUSH',
  'MI_ARB_CHECK',
  'MI_BATCH_BUFFER_START',
  'MI_STORE_DATA_IMM',
  'PIPE_CONTROL',
  # structures
  '3DSTATE_CONSTANT_ALL_DATA',
  '3DSTATE_CONSTANT_BODY',
  'BINDLESS_SHADER_RECORD',
  'CALL_STACK_HANDLER',
  'COMPUTE_WALKER_BODY',
  'INTERFACE_DESCRIPTOR_DATA',
  'POSTSYNC_DATA',
  'RT_DISPATCH_GLOBALS',
  'RT_SHADER_TABLE',
  'VERTEX_BUFFER_STATE',
]

gen_cl_xml_pack = []
foreach f : gen_xml_files + gen_xml_rt_files
  _name = '@0@_cl_pack.h'.format(f.split('.')[0])
  gen_cl_xml_pack += custom_target(
    _name,
    input : ['gen_pack_header.py', f],
    output : _name,
    command : [prog_python, '@INPUT@', '--engines=render,blitter,compute',
               '--opencl', '--repack',
               '--include-symbols', ','.join(genX_cl_included_symbols)],
    capture : true,
    depend_files: gen_pack_header_deps
  )
endforeach

gen_pack_header_py = files('gen_pack_header.py')

idep_genxml = declare_dependency(
  include_directories : include_directories('.'),
  dependencies : idep_mesautil,
  sources : [gen_xml_pack, gen_xml_video_pack, gen_cl_xml_pack, genX_bits_h, genX_xml_h]
)

gen_xml_test_files = ['../decoder/tests/gentest.xml']

foreach f : gen_xml_files + gen_xml_rt_files + gen_xml_test_files
  test(
    'genxml sort @0@'.format(f),
    prog_python,
    args : [files('gen_sort_tags.py'), files(f), '--validate', '--quiet'],
    suite : ['intel'],
  )
endforeach
