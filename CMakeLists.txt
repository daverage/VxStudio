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

option(VXSUITE_ENABLE_ONNX_RUNTIME "Enable ONNX Runtime for VX DeepFilterNet realtime inference" ON)
option(VXSUITE_ENABLE_COREML "Enable CoreML execution provider for ONNX Runtime" OFF)
option(VXSUITE_ENABLE_DIRECTML "Enable DirectML execution provider for ONNX Runtime" OFF)
set(ORT_DIR "" CACHE PATH "Root directory of an ONNX Runtime installation")

set(VXSUITE_HAS_ONNXRUNTIME FALSE)

if(VXSUITE_ENABLE_ONNX_RUNTIME)
  if(NOT ONNXRUNTIME_INCLUDE_DIR)
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
      PATHS
        ${ORT_DIR}/include/onnxruntime
        $ENV{ORT_ROOT}/include/onnxruntime
        /opt/homebrew/include/onnxruntime
        /usr/local/include/onnxruntime
        C:/onnxruntime/include/onnxruntime
    )
  endif()
  if(NOT ONNXRUNTIME_LIBRARY)
    find_library(ONNXRUNTIME_LIBRARY onnxruntime
      PATHS
        ${ORT_DIR}/lib
        $ENV{ORT_ROOT}/lib
        /opt/homebrew/lib
        /usr/local/lib
        C:/onnxruntime/lib
    )
  endif()
  if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    set(VXSUITE_HAS_ONNXRUNTIME TRUE)
    message(STATUS "ONNX Runtime enabled for VX DeepFilterNet: ${ONNXRUNTIME_LIBRARY}")
  else()
    message(WARNING "ONNX Runtime not found; VX DeepFilterNet will build with realtime inference disabled.")
  endif()
endif()

set(VXSUITE_ORT_COMPILE_DEFS
  VXC_HAS_ONNXRUNTIME=$<BOOL:${VXSUITE_HAS_ONNXRUNTIME}>
  VXC_HAS_ORT_COREML_FACTORY=$<BOOL:${VXSUITE_ENABLE_COREML}>
  VXC_HAS_ORT_DML_FACTORY=$<BOOL:${VXSUITE_ENABLE_DIRECTML}>
)

set(VXSUITE_DF_EMBED_MODEL_FILES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/aggressive.onnx")
  list(APPEND VXSUITE_DF_EMBED_MODEL_FILES "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/aggressive.onnx")
endif()
if(VXSUITE_DF_EMBED_MODEL_FILES)
  juce_add_binary_data(VxDeepFilterNetModels SOURCES ${VXSUITE_DF_EMBED_MODEL_FILES})
endif()

function(vxsuite_link_df_models target_name)
  if(TARGET VxDeepFilterNetModels)
    target_link_libraries(${target_name} PRIVATE VxDeepFilterNetModels)
    target_compile_definitions(${target_name} PRIVATE VXC_HAS_EMBEDDED_DF_MODELS=1)
  endif()
endfunction()

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

juce_add_plugin(VXDeepFilterNet
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxdeepfilternet"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXDF"
)
target_compile_definitions(VXDeepFilterNet PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
  ${VXSUITE_ORT_COMPILE_DEFS}
)
juce_generate_juce_header(VXDeepFilterNet)
target_sources(VXDeepFilterNet PRIVATE
  Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp
  Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
)
target_include_directories(VXDeepFilterNet PRIVATE Source)
if(VXSUITE_HAS_ONNXRUNTIME)
  target_include_directories(VXDeepFilterNet PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
endif()
target_link_libraries(VXDeepFilterNet PRIVATE VxSuiteFramework juce::juce_dsp)
if(VXSUITE_HAS_ONNXRUNTIME)
  target_link_libraries(VXDeepFilterNet PRIVATE ${ONNXRUNTIME_LIBRARY})
endif()
vxsuite_link_df_models(VXDeepFilterNet)
add_custom_target(VXDeepFilterNetPlugin ALL DEPENDS VXDeepFilterNet_VST3)
add_custom_command(TARGET VXDeepFilterNet_VST3 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn2"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn3"
  COMMENT "Ensuring VXDeepFilterNet model resource directories exist")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/aggressive.onnx")
  add_custom_command(TARGET VXDeepFilterNet_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/aggressive.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/aggressive.onnx"
    COMMENT "Bundling VXDeepFilterNet monolithic realtime model")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn2/config.ini")
  add_custom_command(TARGET VXDeepFilterNet_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn2/config.ini"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn2/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn2/enc.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn2/enc.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn2/erb_dec.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn2/erb_dec.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn2/df_dec.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn2/df_dec.onnx"
    COMMENT "Bundling VXDeepFilterNet 2 model bundle")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn3/config.ini")
  add_custom_command(TARGET VXDeepFilterNet_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn3/config.ini"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn3/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn3/enc.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn3/enc.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn3/erb_dec.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn3/erb_dec.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/dfn3/df_dec.onnx"
            "$<TARGET_FILE_DIR:VXDeepFilterNet_VST3>/../Resources/models/dfn3/df_dec.onnx"
    COMMENT "Bundling VXDeepFilterNet 3 model bundle")
endif()
vxsuite_stage_vst3_bundle(VXDeepFilterNet_VST3 "VXDeepFilterNet")

add_custom_target(VXSuite_VST3
  DEPENDS
    VXDeverb_VST3
    VXProximity_VST3
    VXPolish_VST3
    VXSubtract_VST3
    VXDenoiser_VST3
    VXDeepFilterNet_VST3
    VXDeverbStage
    VXProximityStage
    VXPolishStage
    VXSubtractStage
    VXDenoiserStage
    VXDeepFilterNetStage
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
