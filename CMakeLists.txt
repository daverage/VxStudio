cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
project(VxStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VXSUITE_FRAMEWORK_VERSION "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXDEVERB "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXPROXIMITY "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXCLEANUP "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXFINISH "0.3.0")
set(VXSUITE_PLUGIN_VERSION_VXOPTOCOMP "0.3.0")
set(VXSUITE_PLUGIN_VERSION_VXSUBTRACT "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXDENOISER "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXDEEPFILTERNET "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXTONE "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXLEVELER "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXREBALANCE "0.2.0")
set(VXSUITE_PLUGIN_VERSION_VXSTUDIOANALYSER "0.2.0")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/VxSuiteVersions.h.in"
  "${CMAKE_BINARY_DIR}/generated/VxSuiteVersions.h"
  @ONLY
)

enable_testing()

add_subdirectory(JUCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt")
endif()
include(VxSuitePlugin)
include(VxSuiteValidation)

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

function(vxsuite_copy_vst3_resources target_name bundle_name)
  if(NOT ARGN)
    return()
  endif()

  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/VST3/${bundle_name}.vst3")
  set(stage_bundle_dir "${VXSUITE_VST3_STAGE_DIR}/${bundle_name}.vst3")

  foreach(resource IN LISTS ARGN)
    get_filename_component(resource_name "${resource}" NAME)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle_source_dir}/Contents/Resources"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${resource}"
              "${bundle_source_dir}/Contents/Resources/${resource_name}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${stage_bundle_dir}/Contents/Resources"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${resource}"
              "${stage_bundle_dir}/Contents/Resources/${resource_name}"
      VERBATIM)
  endforeach()
endfunction()

find_package(Eigen3 CONFIG QUIET)
if(NOT TARGET Eigen3::Eigen)
  set(Eigen3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(Eigen3 REQUIRED)
endif()

find_program(CARGO_CMD cargo REQUIRED)
set(VXSUITE_LIBDF_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/deep_filter/libDF")
set(VXSUITE_LIBDF_TARGET_ROOT "${CMAKE_BINARY_DIR}/cargo/libdf")
set(VXSUITE_LIBDF_TARGET_DIR "${VXSUITE_LIBDF_TARGET_ROOT}/release")
set(VXSUITE_LIBDF_DFN2_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/deep_filter_dfn2/libDF")
set(VXSUITE_LIBDF_DFN2_TARGET_ROOT "${CMAKE_BINARY_DIR}/cargo/libdf_dfn2")
set(VXSUITE_LIBDF_DFN2_TARGET_DIR "${VXSUITE_LIBDF_DFN2_TARGET_ROOT}/release")
set(VXSUITE_MACOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(APPLE AND VXSUITE_MACOS_DEPLOYMENT_TARGET STREQUAL "")
  set(VXSUITE_MACOS_DEPLOYMENT_TARGET "15.0")
endif()
set(VXSUITE_DF_EMBED_MODEL_FILES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/DeepFilterNet2_onnx.tar.gz")
  list(APPEND VXSUITE_DF_EMBED_MODEL_FILES "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/DeepFilterNet2_onnx.tar.gz")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/DeepFilterNet2_onnx_ll.tar.gz")
  list(APPEND VXSUITE_DF_EMBED_MODEL_FILES "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/DeepFilterNet2_onnx_ll.tar.gz")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/DeepFilterNet3_onnx.tar.gz")
  list(APPEND VXSUITE_DF_EMBED_MODEL_FILES "${CMAKE_SOURCE_DIR}/assets/deepfilternet/models/DeepFilterNet3_onnx.tar.gz")
endif()
if(VXSUITE_DF_EMBED_MODEL_FILES)
  juce_add_binary_data(VxDeepFilterNetModels SOURCES ${VXSUITE_DF_EMBED_MODEL_FILES})
endif()

if(WIN32)
  set(VXSUITE_DF_RUNTIME_LIB "${VXSUITE_LIBDF_TARGET_DIR}/df.lib")
  set(VXSUITE_DF_RUNTIME_DFN2_LIB "${VXSUITE_LIBDF_DFN2_TARGET_DIR}/df.lib")
else()
  set(VXSUITE_DF_RUNTIME_LIB "${VXSUITE_LIBDF_TARGET_DIR}/libdf.a")
  set(VXSUITE_DF_RUNTIME_DFN2_LIB "${VXSUITE_LIBDF_DFN2_TARGET_DIR}/libdf.a")
endif()

if(APPLE)
  add_custom_target(build_vxsuite_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSUITE_LIBDF_TARGET_ROOT}
            MACOSX_DEPLOYMENT_TARGET=${VXSUITE_MACOS_DEPLOYMENT_TARGET}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSUITE_LIBDF_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSUITE_LIBDF_DIR}
    BYPRODUCTS ${VXSUITE_DF_RUNTIME_LIB}
    COMMENT "Building bundled DeepFilter runtime"
    VERBATIM)
  add_custom_target(build_vxsuite_libdf_dfn2
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSUITE_LIBDF_DFN2_TARGET_ROOT}
            MACOSX_DEPLOYMENT_TARGET=${VXSUITE_MACOS_DEPLOYMENT_TARGET}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSUITE_LIBDF_DFN2_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSUITE_LIBDF_DFN2_DIR}
    BYPRODUCTS ${VXSUITE_DF_RUNTIME_DFN2_LIB}
    COMMENT "Building bundled DeepFilter DFN2 runtime"
    VERBATIM)
else()
  add_custom_target(build_vxsuite_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSUITE_LIBDF_TARGET_ROOT}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSUITE_LIBDF_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSUITE_LIBDF_DIR}
    BYPRODUCTS ${VXSUITE_DF_RUNTIME_LIB}
    COMMENT "Building bundled DeepFilter runtime"
    VERBATIM)
  add_custom_target(build_vxsuite_libdf_dfn2
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSUITE_LIBDF_DFN2_TARGET_ROOT}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSUITE_LIBDF_DFN2_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSUITE_LIBDF_DFN2_DIR}
    BYPRODUCTS ${VXSUITE_DF_RUNTIME_DFN2_LIB}
    COMMENT "Building bundled DeepFilter DFN2 runtime"
    VERBATIM)
endif()

add_library(VxSuiteDeepFilterRuntime STATIC IMPORTED GLOBAL)
set_target_properties(VxSuiteDeepFilterRuntime PROPERTIES IMPORTED_LOCATION "${VXSUITE_DF_RUNTIME_LIB}")
add_dependencies(VxSuiteDeepFilterRuntime build_vxsuite_libdf)
add_library(VxSuiteDeepFilterRuntimeDfn2 STATIC IMPORTED GLOBAL)
set_target_properties(VxSuiteDeepFilterRuntimeDfn2 PROPERTIES IMPORTED_LOCATION "${VXSUITE_DF_RUNTIME_DFN2_LIB}")
add_dependencies(VxSuiteDeepFilterRuntimeDfn2 build_vxsuite_libdf_dfn2)

if(WIN32)
  set(VXSUITE_DF_SYSTEM_LIBS
    Bcrypt.lib
    Userenv.lib
    Ntdll.lib
    Advapi32.lib)
elseif(APPLE)
  find_library(SECURITY_FW Security)
  find_library(COREFOUNDATION_FW CoreFoundation)
  find_library(SYSTEMCONFIGURATION_FW SystemConfiguration)
  set(VXSUITE_DF_SYSTEM_LIBS
    ${SECURITY_FW}
    ${COREFOUNDATION_FW}
    ${SYSTEMCONFIGURATION_FW}
    resolv)
else()
  set(VXSUITE_DF_SYSTEM_LIBS
    pthread
    dl
    m)
endif()

function(vxsuite_link_df_models target_name)
  if(TARGET VxDeepFilterNetModels)
    target_link_libraries(${target_name} PRIVATE VxDeepFilterNetModels)
    target_compile_definitions(${target_name} PRIVATE VXC_HAS_EMBEDDED_DF_MODELS=1)
  endif()
endfunction()

vxsuite_add_framework(VxSuiteFramework)

juce_add_plugin(VXDeverb
  VERSION ${VXSUITE_PLUGIN_VERSION_VXDEVERB}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXDEVERB}
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
  Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverb PRIVATE Source)
target_link_libraries(VXDeverb PRIVATE VxSuiteFramework Eigen3::Eigen juce::juce_dsp)
add_custom_target(VXDeverbPlugin ALL DEPENDS VXDeverb_VST3)
vxsuite_stage_vst3_bundle(VXDeverb_VST3 "VXDeverb")

juce_add_plugin(VXProximity
  VERSION ${VXSUITE_PLUGIN_VERSION_VXPROXIMITY}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXPROXIMITY}
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

juce_add_plugin(VXCleanup
  VERSION ${VXSUITE_PLUGIN_VERSION_VXCLEANUP}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXCLEANUP}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxcleanup"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXCL"
)
target_compile_definitions(VXCleanup PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXCleanup)
target_sources(VXCleanup PRIVATE
  Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp
  Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXCleanup PRIVATE Source)
target_link_libraries(VXCleanup PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXCleanupPlugin ALL DEPENDS VXCleanup_VST3)
vxsuite_stage_vst3_bundle(VXCleanup_VST3 "VXCleanup")

juce_add_plugin(VXFinish
  VERSION ${VXSUITE_PLUGIN_VERSION_VXFINISH}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXFINISH}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxfinish"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXFN"
)
target_compile_definitions(VXFinish PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXFinish)
target_sources(VXFinish PRIVATE
  Source/vxsuite/products/finish/VxFinishProcessor.cpp
  Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp
  Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXFinish PRIVATE Source)
target_link_libraries(VXFinish PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXFinishPlugin ALL DEPENDS VXFinish_VST3)
vxsuite_stage_vst3_bundle(VXFinish_VST3 "VXFinish")

juce_add_plugin(VXOptoComp
  VERSION ${VXSUITE_PLUGIN_VERSION_VXOPTOCOMP}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXOPTOCOMP}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxoptocomp"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXOC"
)
target_compile_definitions(VXOptoComp PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXOptoComp)
target_sources(VXOptoComp PRIVATE
  Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp
  Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp
)
target_include_directories(VXOptoComp PRIVATE Source)
target_link_libraries(VXOptoComp PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXOptoCompPlugin ALL DEPENDS VXOptoComp_VST3)
vxsuite_stage_vst3_bundle(VXOptoComp_VST3 "VXOptoComp")

juce_add_plugin(VXSubtract
  VERSION ${VXSUITE_PLUGIN_VERSION_VXSUBTRACT}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXSUBTRACT}
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
  Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp
)
target_include_directories(VXSubtract PRIVATE Source)
target_link_libraries(VXSubtract PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXSubtractPlugin ALL DEPENDS VXSubtract_VST3)
vxsuite_stage_vst3_bundle(VXSubtract_VST3 "VXSubtract")

juce_add_plugin(VXDenoiser
  VERSION ${VXSUITE_PLUGIN_VERSION_VXDENOISER}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXDENOISER}
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
  VERSION ${VXSUITE_PLUGIN_VERSION_VXDEEPFILTERNET}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXDEEPFILTERNET}
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
)
juce_generate_juce_header(VXDeepFilterNet)
target_sources(VXDeepFilterNet PRIVATE
  Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp
  Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
)
target_include_directories(VXDeepFilterNet PRIVATE Source)
target_link_libraries(VXDeepFilterNet PRIVATE
  VxSuiteFramework
  VxSuiteDeepFilterRuntime
  VxSuiteDeepFilterRuntimeDfn2
  VxDeepFilterNetModels
  ${VXSUITE_DF_SYSTEM_LIBS}
  juce::juce_dsp)
vxsuite_link_df_models(VXDeepFilterNet)
add_custom_target(VXDeepFilterNetPlugin ALL DEPENDS VXDeepFilterNet_VST3)
vxsuite_stage_vst3_bundle(VXDeepFilterNet_VST3 "VXDeepFilterNet")
vxsuite_copy_vst3_resources(VXDeepFilterNet_VST3 "VXDeepFilterNet" ${VXSUITE_DF_EMBED_MODEL_FILES})

juce_add_plugin(VXTone
  VERSION ${VXSUITE_PLUGIN_VERSION_VXTONE}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXTONE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxtone"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXTN"
)
target_compile_definitions(VXTone PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXTone)
target_sources(VXTone PRIVATE
  Source/vxsuite/products/tone/VxToneProcessor.cpp
)
target_include_directories(VXTone PRIVATE Source)
target_link_libraries(VXTone PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXTonePlugin ALL DEPENDS VXTone_VST3)
vxsuite_stage_vst3_bundle(VXTone_VST3 "VXTone")

juce_add_plugin(VXLeveler
  VERSION ${VXSUITE_PLUGIN_VERSION_VXLEVELER}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXLEVELER}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxleveler"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXLV"
)
target_compile_definitions(VXLeveler PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXLeveler)
target_sources(VXLeveler PRIVATE
  Source/vxsuite/products/leveler/VxLevelerProcessor.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VXLeveler PRIVATE Source)
target_link_libraries(VXLeveler PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXLevelerPlugin ALL DEPENDS VXLeveler_VST3)
vxsuite_stage_vst3_bundle(VXLeveler_VST3 "VXLeveler")

juce_add_plugin(VXRebalance
  VERSION ${VXSUITE_PLUGIN_VERSION_VXREBALANCE}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXREBALANCE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxrebalance"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXRB"
)
target_compile_definitions(VXRebalance PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXRebalance)
target_sources(VXRebalance PRIVATE
  Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp
  Source/vxsuite/products/rebalance/ml/VxRebalanceConfidence.cpp
  Source/vxsuite/products/rebalance/ml/VxRebalanceFeatureBuffer.cpp
  Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp
)
target_include_directories(VXRebalance PRIVATE Source)
target_link_libraries(VXRebalance PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXRebalancePlugin ALL DEPENDS VXRebalance_VST3)
vxsuite_stage_vst3_bundle(VXRebalance_VST3 "VXRebalance")

juce_add_plugin(VXStudioAnalyser
  VERSION ${VXSUITE_PLUGIN_VERSION_VXSTUDIOANALYSER}
  BUILD_VERSION ${VXSUITE_PLUGIN_VERSION_VXSTUDIOANALYSER}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxsuite.vxstudioanalyser"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXSA"
)
target_compile_definitions(VXStudioAnalyser PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXStudioAnalyser)
target_sources(VXStudioAnalyser PRIVATE
  Source/vxsuite/products/analyser/VXStudioAnalyserProcessor.cpp
  Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp
)
target_include_directories(VXStudioAnalyser PRIVATE Source)
target_link_libraries(VXStudioAnalyser PRIVATE VxSuiteFramework juce::juce_dsp)
add_custom_target(VXStudioAnalyserPlugin ALL DEPENDS VXStudioAnalyser_VST3)
vxsuite_stage_vst3_bundle(VXStudioAnalyser_VST3 "VXStudioAnalyser")

add_custom_target(VXSuite_VST3
  DEPENDS
    VXDeverb_VST3
    VXProximity_VST3
    VXCleanup_VST3
    VXFinish_VST3
    VXOptoComp_VST3
    VXSubtract_VST3
    VXDenoiser_VST3
    VXDeepFilterNet_VST3
    VXTone_VST3
    VXLeveler_VST3
    VXRebalance_VST3
    VXStudioAnalyser_VST3
    VXDeverbStage
    VXProximityStage
    VXCleanupStage
    VXFinishStage
    VXOptoCompStage
    VXSubtractStage
    VXDenoiserStage
    VXDeepFilterNetStage
    VXToneStage
    VXLevelerStage
    VXRebalanceStage
    VXStudioAnalyserStage
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
vxsuite_add_executable_test(VXDeverbTests)

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
vxsuite_add_executable_test(VXDeverbMeasure)

add_executable(VXLevelerMeasure
  tests/VXLevelerMeasure.cpp
  Source/vxsuite/products/leveler/VxLevelerProcessor.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VXLevelerMeasure PRIVATE Source tests)
target_link_libraries(VXLevelerMeasure PRIVATE
  VxSuiteFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXLevelerMeasure PRIVATE VXSUITE_DISABLE_PLUGIN_ENTRYPOINT=1)
vxsuite_add_executable_test(VXLevelerMeasure)

add_executable(VXLevelerTuneSearch
  tests/VXLevelerTuneSearch.cpp
  Source/vxsuite/products/leveler/VxLevelerProcessor.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VXLevelerTuneSearch PRIVATE Source tests)
target_link_libraries(VXLevelerTuneSearch PRIVATE
  VxSuiteFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXLevelerTuneSearch PRIVATE VXSUITE_DISABLE_PLUGIN_ENTRYPOINT=1)
vxsuite_add_executable_test(VXLevelerTuneSearch)

add_executable(VXRebalanceMeasure
  tests/VXRebalanceMeasure.cpp
  Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp
  Source/vxsuite/products/rebalance/ml/VxRebalanceConfidence.cpp
  Source/vxsuite/products/rebalance/ml/VxRebalanceFeatureBuffer.cpp
  Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp
)
target_include_directories(VXRebalanceMeasure PRIVATE Source tests)
target_link_libraries(VXRebalanceMeasure PRIVATE
  VxSuiteFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXRebalanceMeasure PRIVATE VXSUITE_DISABLE_PLUGIN_ENTRYPOINT=1)
vxsuite_add_executable_test(VXRebalanceMeasure)

add_executable(VxSuiteVoiceAnalysisTests
  tests/VxSuiteVoiceAnalysisTests.cpp
)
target_include_directories(VxSuiteVoiceAnalysisTests PRIVATE Source)
target_link_libraries(VxSuiteVoiceAnalysisTests PRIVATE
  VxSuiteFramework
  juce::juce_audio_basics
)
vxsuite_add_executable_test(VxSuiteVoiceAnalysisTests)

add_executable(VXSuitePluginRegressionTests
  tests/VXSuitePluginRegressionTests.cpp
  Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp
  Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxsuite/products/deverb/VxDeverbProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxsuite/products/finish/VxFinishProcessor.cpp
  Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp
  Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxsuite/products/proximity/VxProximityProcessor.cpp
  Source/vxsuite/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxsuite/products/subtract/VxSubtractProcessor.cpp
  Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxsuite/products/tone/VxToneProcessor.cpp
  Source/vxsuite/products/leveler/VxLevelerProcessor.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
  Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXSuitePluginRegressionTests PRIVATE Source tests)
target_link_libraries(VXSuitePluginRegressionTests PRIVATE
  VxSuiteFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXSuitePluginRegressionTests PRIVATE VXSUITE_DISABLE_PLUGIN_ENTRYPOINT=1)
vxsuite_add_executable_test(VXSuitePluginRegressionTests)

add_executable(VXSuiteBatchAudioCheck
  tests/VXSuiteBatchAudioCheck.cpp
  Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp
  Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxsuite/products/deverb/VxDeverbProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxsuite/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxsuite/products/finish/VxFinishProcessor.cpp
  Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp
  Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxsuite/products/proximity/VxProximityProcessor.cpp
  Source/vxsuite/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxsuite/products/subtract/VxSubtractProcessor.cpp
  Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxsuite/products/tone/VxToneProcessor.cpp
  Source/vxsuite/products/leveler/VxLevelerProcessor.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
  Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXSuiteBatchAudioCheck PRIVATE Source tests)
target_link_libraries(VXSuiteBatchAudioCheck PRIVATE
  VxSuiteFramework
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXSuiteBatchAudioCheck PRIVATE VXSUITE_DISABLE_PLUGIN_ENTRYPOINT=1)
vxsuite_add_executable_test(VXSuiteBatchAudioCheck)

add_executable(VXSuiteProfile
  tests/VXSuiteProfile.cpp
  Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp
  Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxsuite/products/finish/VxFinishProcessor.cpp
  Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp
  Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxsuite/products/proximity/VxProximityProcessor.cpp
  Source/vxsuite/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxsuite/products/subtract/VxSubtractProcessor.cpp
  Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXSuiteProfile PRIVATE Source tests)
target_link_libraries(VXSuiteProfile PRIVATE
  VxSuiteFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXSuiteProfile PRIVATE VXSUITE_DISABLE_PLUGIN_ENTRYPOINT=1)
vxsuite_add_executable_test(VXSuiteProfile)

vxsuite_add_pluginval_test(VXCleanup)
vxsuite_add_pluginval_test(VXDeepFilterNet)
vxsuite_add_pluginval_test(VXDenoiser)
vxsuite_add_pluginval_test(VXDeverb)
vxsuite_add_pluginval_test(VXFinish)
vxsuite_add_pluginval_test(VXOptoComp)
vxsuite_add_pluginval_test(VXProximity)
vxsuite_add_pluginval_test(VXStudioAnalyser)
vxsuite_add_pluginval_test(VXSubtract)
vxsuite_add_pluginval_test(VXTone)
vxsuite_add_pluginval_test(VXLeveler)
