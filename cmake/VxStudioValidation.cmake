option(VXSTUDIO_ENABLE_PLUGINVAL_TESTS "Register pluginval-based CTest entries when pluginval is available" ON)
set(VXSTUDIO_PLUGINVAL_STRICTNESS_LEVEL "5" CACHE STRING "pluginval strictness level used for CTest registration")
set(VXSTUDIO_PLUGINVAL_EXE "" CACHE FILEPATH "Path to the pluginval executable")

if(VXSTUDIO_ENABLE_PLUGINVAL_TESTS AND NOT VXSTUDIO_PLUGINVAL_EXE)
  find_program(VXSTUDIO_PLUGINVAL_EXE NAMES pluginval)
endif()

function(vxstudio_add_executable_test target_name)
  add_test(NAME ${target_name}
    COMMAND $<TARGET_FILE:${target_name}>)
  set_tests_properties(${target_name} PROPERTIES LABELS "unit")
endfunction()

function(vxstudio_add_pluginval_test bundle_name)
  if(NOT VXSTUDIO_ENABLE_PLUGINVAL_TESTS OR NOT VXSTUDIO_PLUGINVAL_EXE)
    return()
  endif()

  set(plugin_bundle "${CMAKE_SOURCE_DIR}/Source/vxstudio/vst/${bundle_name}.vst3")
  add_test(NAME pluginval_${bundle_name}
    COMMAND "${VXSTUDIO_PLUGINVAL_EXE}"
            --strictness-level "${VXSTUDIO_PLUGINVAL_STRICTNESS_LEVEL}"
            --validate "${plugin_bundle}")
  set_tests_properties(pluginval_${bundle_name} PROPERTIES LABELS "pluginval;host")
endfunction()

