cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
project(VxStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt")
include(VxSuitePlugin)

set(VXSUITE_VST3_STAGE_DIR "${CMAKE_SOURCE_DIR}/Source/vxsuite/vst")

function(vxsuite_stage_vst3_bundle target_name bundle_name)
  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/VST3/${bundle_name}.vst3")
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSUITE_VST3_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VXSUITE_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${bundle_source_dir}"
            "${VXSUITE_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMENT "Staging ${bundle_name}.vst3 into Source/vxsuite/vst")

  add_custom_target(${bundle_name}Stage
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSUITE_VST3_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VXSUITE_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${bundle_source_dir}"
            "${VXSUITE_VST3_STAGE_DIR}/${bundle_name}.vst3"
    DEPENDS ${target_name}
    COMMENT "Refreshing staged ${bundle_name}.vst3 in Source/vxsuite/vst")
endfunction()

find_package(Eigen3 CONFIG QUIET)
if(NOT TARGET Eigen3::Eigen)
  set(Eigen3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(Eigen3 REQUIRED)
endif()

vxsuite_add_framework(VxSuiteFramework)

juce_add_plugin(VXDeverb
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxdeverb"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXDV"
)
target_compile_definitions(VXDeverb PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXDeverb)
target_sources(VXDeverb PRIVATE
  Source/vxsuite/products/deverb/VxDeverbProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverb PRIVATE Source)
target_link_libraries(VXDeverb PRIVATE VxSuiteFramework Eigen3::Eigen juce::juce_dsp)
add_custom_target(VXDeverbPlugin ALL DEPENDS VXDeverb_VST3)
vxsuite_stage_vst3_bundle(VXDeverb_VST3 "VXDeverb")

juce_add_plugin(VXProximity
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxproximity"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXPR"
)
target_compile_definitions(VXProximity PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXProximity)
target_sources(VXProximity PRIVATE
  Source/vxsuite/products/proximity/VxProximityProcessor.cpp
  Source/vxsuite/products/proximity/dsp/VxProximityDsp.cpp
)
target_include_directories(VXProximity PRIVATE Source)
target_link_libraries(VXProximity PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXProximityPlugin ALL DEPENDS VXProximity_VST3)
vxsuite_stage_vst3_bundle(VXProximity_VST3 "VXProximity")

juce_add_plugin(VXPolish
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxpolish"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXPL"
)
target_compile_definitions(VXPolish PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXPolish)
target_sources(VXPolish PRIVATE
  Source/vxsuite/products/polish/VxPolishProcessor.cpp
  Source/vxsuite/products/polish/dsp/VxPolishDsp.cpp
)
target_include_directories(VXPolish PRIVATE Source)
target_link_libraries(VXPolish PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXPolishPlugin ALL DEPENDS VXPolish_VST3)
vxsuite_stage_vst3_bundle(VXPolish_VST3 "VXPolish")

juce_add_plugin(VXSubtract
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxsubtract"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXSB"
)
target_compile_definitions(VXSubtract PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXSubtract)
target_sources(VXSubtract PRIVATE
  Source/vxsuite/products/subtract/VxSubtractProcessor.cpp
  Source/dsp/HandmadePrimary.cpp
)
target_include_directories(VXSubtract PRIVATE Source)
target_link_libraries(VXSubtract PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXSubtractPlugin ALL DEPENDS VXSubtract_VST3)
vxsuite_stage_vst3_bundle(VXSubtract_VST3 "VXSubtract")

juce_add_plugin(VXDenoiser
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxdenoiser"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXDN"
)
target_compile_definitions(VXDenoiser PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXDenoiser)
target_sources(VXDenoiser PRIVATE
  Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp
)
target_include_directories(VXDenoiser PRIVATE Source)
target_link_libraries(VXDenoiser PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXDenoiserPlugin ALL DEPENDS VXDenoiser_VST3)
vxsuite_stage_vst3_bundle(VXDenoiser_VST3 "VXDenoiser")

add_custom_target(VXSuite_VST3
  DEPENDS
    VXDeverb_VST3
    VXProximity_VST3
    VXPolish_VST3
    VXSubtract_VST3
    VXDenoiser_VST3
    VXDeverbStage
    VXProximityStage
    VXPolishStage
    VXSubtractStage
    VXDenoiserStage
  COMMENT "Building all VX Suite VST3 products")

add_executable(VXDeverbTests
  tests/VXDeverbTests.cpp
  Source/vxsuite/products/deverb/VxDeverbProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverbTests PRIVATE Source tests)
target_link_libraries(VXDeverbTests PRIVATE
  VxSuiteFramework
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
)

add_executable(VXDeverbMeasure
  tests/VXDeverbMeasure.cpp
  Source/vxsuite/products/deverb/VxDeverbProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverbMeasure PRIVATE Source tests)
target_link_libraries(VXDeverbMeasure PRIVATE
  VxSuiteFramework
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
)

add_executable(VxSuiteVoiceAnalysisTests
  tests/VxSuiteVoiceAnalysisTests.cpp
)
target_include_directories(VxSuiteVoiceAnalysisTests PRIVATE Source)
target_link_libraries(VxSuiteVoiceAnalysisTests PRIVATE
  VxSuiteFramework
  juce::juce_audio_basics
)
