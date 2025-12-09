mediafoundation_test_st = {}

foreach codec : mft_codecs
    test_lib = static_library(
        'mediafoundation_test_st_' + codec,
        files(
            'hmft_test_helpers.cpp',
            'hmft_codecapi_test.cpp',
            'hmft_simple_encode_test.cpp',
            'stub.cpp',
        ),
        cpp_args : mf_codecs_cpp_args[codec],
        override_options: ['cpp_std=c++20'],
        include_directories: [inc_include, inc_src, inc_gallium, inc_gallium_aux],
        dependencies: [idep_gtest, idep_mesautil, dep_dxheaders],
    )
    mediafoundation_test_st += {codec : test_lib}
endforeach