subdir('api')

test_files = [
  'test_main.cpp',

  'ir/test_ir_arithmetic.cpp',
  'ir/test_ir_builder.cpp',
  'ir/test_ir_cse.cpp',
  'ir/test_ir_dominance.cpp',
  'ir/test_ir_input_map.cpp',
  'ir/test_ir_op.cpp',
  'ir/test_ir_scalarize.cpp',
  'ir/test_ir_serialize.cpp',
  'ir/test_ir_ssa.cpp',
  'ir/test_ir_type.cpp',
  'ir/test_ir_type_propagation.cpp',

  'util/test_util_small_vector.cpp',
  'util/test_util_vle.cpp',
]

if get_option('enable_sm3')
  test_files += [ 'sm3/test_sm3_converter.cpp' ]
endif

if get_option('enable_sm5')
  test_files += [
    'dxbc/test_dxbc_common.cpp',
    'dxbc/test_dxbc_converter.cpp',
    'dxbc/test_dxbc_signature.cpp',
  ]
endif

if get_option('enable_spirv')
  test_files += [ 'spirv/test_spirv_builder.cpp' ]
endif

unit_tests = executable('test_dxbc_spv', files(test_files),
  link_with   : [ lib_dxbc_spv, lib_dxbc_spv_test_api ],
  install     : false)

test('dxbc_spv', unit_tests)

if get_option('enable_spirv')
  executable('test_spirv_lowering', files('test_spirv.cpp'),
    link_with   : [ lib_dxbc_spv, lib_dxbc_spv_test_api ],
    install     : false)
endif
