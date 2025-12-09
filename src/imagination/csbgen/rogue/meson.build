# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

pvr_xml_files = [
  'cdm.xml',
  'cr.xml',
  'ipf.xml',
  'kmd_stream.xml',
  'lls.xml',
  'pbestate.xml',
  'pds.xml',
  'ppp.xml',
  'texstate.xml',
  'vdm.xml',
]

foreach f : pvr_xml_files
  _name = '@0@.h'.format(f.split('.')[0])
  pvr_xml_pack += custom_target(
    _name,
    input : ['../gen_pack_header.py', f],
    output : _name,
    command : [prog_python, '@INPUT@'],
    capture : true,
  )
endforeach
