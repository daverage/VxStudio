option(VXSUITE_ENABLE_PLUGINVAL_TESTS "Register pluginval-based CTest entries when pluginval is available" ON)
set(VXSUITE_PLUGINVAL_STRICTNESS_LEVEL "5" CACHE STRING "pluginval strictness level used for CTest registration")
set(VXSUITE_PLUGINVAL_EXE "" CACHE FILEPATH "Path to the pluginval executable")

if(VXSUITE_ENABLE_PLUGINVAL_TESTS AND NOT VXSUITE_PLUGINVAL_EXE)
  find_program(VXSUITE_PLUGINVAL_EXE NAMES pluginval)
endif()

function(vxsuite_add_executable_test target_name)
  add_test(NAME ${target_name}
    COMMAND $<TARGET_FILE:${target_name}>)
  set_tests_properties(${target_name} PROPERTIES LABELS "unit")
endfunction()

function(vxsuite_add_pluginval_test bundle_name)
  if(NOT VXSUITE_ENABLE_PLUGINVAL_TESTS OR NOT VXSUITE_PLUGINVAL_EXE)
    return()
  endif()

  set(plugin_bundle "${CMAKE_SOURCE_DIR}/Source/vxsuite/vst/${bundle_name}.vst3")
  add_test(NAME pluginval_${bundle_name}
    COMMAND "${VXSUITE_PLUGINVAL_EXE}"
            --strictness-level "${VXSUITE_PLUGINVAL_STRICTNESS_LEVEL}"
            --validate "${plugin_bundle}")
  set_tests_properties(pluginval_${bundle_name} PROPERTIES LABELS "pluginval;host")
endfunction()

