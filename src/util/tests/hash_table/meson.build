# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

foreach t : ['clear', 'collision', 'delete_and_lookup', 'delete_management',
             'destroy_callback', 'insert_and_lookup', 'insert_many',
             'null_destroy', 'random_entry', 'remove_key', 'remove_null',
             'replacement']
  test(
    t,
    executable(
      '@0@_test'.format(t),
      files('@0@.c'.format(t)),
      c_args : [c_msvc_compat_args],
      dependencies : idep_mesautil,
    ),
    suite : ['util'],
  )
endforeach
