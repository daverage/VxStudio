cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
project(VxStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VXSTUDIO_FRAMEWORK_VERSION "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXDEVERB "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXPROXIMITY "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXCLEANUP "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXFINISH "0.3.0")
set(VXSTUDIO_PLUGIN_VERSION_VXOPTOCOMP "0.3.0")
set(VXSTUDIO_PLUGIN_VERSION_VXSUBTRACT "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXDENOISER "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXDEEPFILTERNET "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXTONE "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXLEVELER "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXREBALANCE "0.2.1")
set(VXSTUDIO_PLUGIN_VERSION_VXSTUDIOANALYSER "0.2.0")

execute_process(
  COMMAND python3 -c "import pathlib, onnxruntime; p = pathlib.Path(onnxruntime.__file__).resolve().parent / 'capi'; libs = sorted(p.glob('libonnxruntime*.dylib')); print(libs[0] if libs else '', end='')"
  OUTPUT_VARIABLE VXSTUDIO_ONNXRUNTIME_DYLIB
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
set(VXSTUDIO_HAS_ONNXRUNTIME FALSE)
if(APPLE AND EXISTS "${VXSTUDIO_ONNXRUNTIME_DYLIB}")
  add_library(VxStudioOnnxRuntime SHARED IMPORTED GLOBAL)
  set_target_properties(VxStudioOnnxRuntime PROPERTIES IMPORTED_LOCATION "${VXSTUDIO_ONNXRUNTIME_DYLIB}")
  set(VXSTUDIO_HAS_ONNXRUNTIME TRUE)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/VxStudioVersions.h.in"
  "${CMAKE_BINARY_DIR}/generated/VxStudioVersions.h"
  @ONLY
)

enable_testing()

add_subdirectory(JUCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt")
endif()
include(VxStudioPlugin)
include(VxStudioValidation)

set(VXSTUDIO_VST3_STAGE_DIR "${CMAKE_SOURCE_DIR}/Source/vxstudio/vst")

function(vxstudio_stage_vst3_bundle target_name bundle_name)
  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/Debug/VST3/${bundle_name}.vst3")
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSTUDIO_VST3_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${bundle_source_dir}"
            "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMENT "Staging ${bundle_name}.vst3 into Source/vxstudio/vst")

  add_custom_target(${bundle_name}Stage
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSTUDIO_VST3_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${bundle_source_dir}"
            "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    DEPENDS ${target_name}
    COMMENT "Refreshing staged ${bundle_name}.vst3 in Source/vxstudio/vst")
endfunction()

function(vxstudio_copy_vst3_resources target_name bundle_name)
  if(NOT ARGN)
    return()
  endif()

  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/Debug/VST3/${bundle_name}.vst3")
  set(stage_bundle_dir "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3")

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

function(vxstudio_prune_vst3_resources target_name bundle_name)
  if(NOT ARGN)
    return()
  endif()

  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/Debug/VST3/${bundle_name}.vst3")
  set(stage_bundle_dir "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3")

  foreach(resource IN LISTS ARGN)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -f "${bundle_source_dir}/Contents/Resources/${resource}"
      COMMAND ${CMAKE_COMMAND} -E rm -f "${stage_bundle_dir}/Contents/Resources/${resource}"
      VERBATIM)
  endforeach()

  if(APPLE)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND /usr/bin/codesign --force --sign - "${bundle_source_dir}"
      COMMAND /usr/bin/codesign --force --sign - "${stage_bundle_dir}"
      VERBATIM)
  endif()
endfunction()

function(vxstudio_strip_vst3_binary target_name bundle_name)
  if(NOT APPLE)
    return()
  endif()

  set(bundle_source_binary "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/Debug/VST3/${bundle_name}.vst3/Contents/MacOS/${bundle_name}")
  set(stage_bundle_binary "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3/Contents/MacOS/${bundle_name}")
  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/Debug/VST3/${bundle_name}.vst3")
  set(stage_bundle_dir "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3")

  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND /usr/bin/strip -x "${bundle_source_binary}"
    COMMAND /usr/bin/strip -x "${stage_bundle_binary}"
    COMMAND /usr/bin/codesign --force --sign - "${bundle_source_dir}"
    COMMAND /usr/bin/codesign --force --sign - "${stage_bundle_dir}"
    VERBATIM)
endfunction()

find_package(Eigen3 CONFIG QUIET)
if(NOT TARGET Eigen3::Eigen)
  set(Eigen3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(Eigen3 REQUIRED)
endif()

find_program(CARGO_CMD cargo REQUIRED)
set(VXSTUDIO_LIBDF_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/deep_filter/libDF")
set(VXSTUDIO_LIBDF_TARGET_ROOT "${CMAKE_BINARY_DIR}/cargo/libdf")
set(VXSTUDIO_LIBDF_TARGET_DIR "${VXSTUDIO_LIBDF_TARGET_ROOT}/release")
set(VXSTUDIO_LIBDF_DFN2_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/deep_filter_dfn2/libDF")
set(VXSTUDIO_LIBDF_DFN2_TARGET_ROOT "${CMAKE_BINARY_DIR}/cargo/libdf_dfn2")
set(VXSTUDIO_LIBDF_DFN2_TARGET_DIR "${VXSTUDIO_LIBDF_DFN2_TARGET_ROOT}/release")
set(VXSTUDIO_MACOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
if(APPLE AND VXSTUDIO_MACOS_DEPLOYMENT_TARGET STREQUAL "")
  set(VXSTUDIO_MACOS_DEPLOYMENT_TARGET "15.0")
endif()
set(VXSTUDIO_DF_EMBED_MODEL_FILES "")

if(WIN32)
  set(VXSTUDIO_DF_RUNTIME_LIB "${VXSTUDIO_LIBDF_TARGET_DIR}/df.lib")
  set(VXSTUDIO_DF_RUNTIME_DFN2_LIB "${VXSTUDIO_LIBDF_DFN2_TARGET_DIR}/df.lib")
else()
  set(VXSTUDIO_DF_RUNTIME_LIB "${VXSTUDIO_LIBDF_TARGET_DIR}/libdf.a")
  set(VXSTUDIO_DF_RUNTIME_DFN2_LIB "${VXSTUDIO_LIBDF_DFN2_TARGET_DIR}/libdf.a")
endif()

if(APPLE)
  add_custom_target(build_vxstudio_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_TARGET_ROOT}
            MACOSX_DEPLOYMENT_TARGET=${VXSTUDIO_MACOS_DEPLOYMENT_TARGET}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSTUDIO_LIBDF_DIR}
    BYPRODUCTS ${VXSTUDIO_DF_RUNTIME_LIB}
    COMMENT "Building bundled DeepFilter runtime"
    VERBATIM)
  add_custom_target(build_vxstudio_libdf_dfn2
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_DFN2_TARGET_ROOT}
            MACOSX_DEPLOYMENT_TARGET=${VXSTUDIO_MACOS_DEPLOYMENT_TARGET}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DFN2_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSTUDIO_LIBDF_DFN2_DIR}
    BYPRODUCTS ${VXSTUDIO_DF_RUNTIME_DFN2_LIB}
    COMMENT "Building bundled DeepFilter DFN2 runtime"
    VERBATIM)
else()
  add_custom_target(build_vxstudio_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_TARGET_ROOT}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSTUDIO_LIBDF_DIR}
    BYPRODUCTS ${VXSTUDIO_DF_RUNTIME_LIB}
    COMMENT "Building bundled DeepFilter runtime"
    VERBATIM)
  add_custom_target(build_vxstudio_libdf_dfn2
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_DFN2_TARGET_ROOT}
            ${CARGO_CMD} build --release --locked --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DFN2_DIR}/Cargo.toml --features capi
    WORKING_DIRECTORY ${VXSTUDIO_LIBDF_DFN2_DIR}
    BYPRODUCTS ${VXSTUDIO_DF_RUNTIME_DFN2_LIB}
    COMMENT "Building bundled DeepFilter DFN2 runtime"
    VERBATIM)
endif()

add_library(VxStudioDeepFilterRuntime STATIC IMPORTED GLOBAL)
set_target_properties(VxStudioDeepFilterRuntime PROPERTIES IMPORTED_LOCATION "${VXSTUDIO_DF_RUNTIME_LIB}")
add_dependencies(VxStudioDeepFilterRuntime build_vxstudio_libdf)
add_library(VxStudioDeepFilterRuntimeDfn2 STATIC IMPORTED GLOBAL)
set_target_properties(VxStudioDeepFilterRuntimeDfn2 PROPERTIES IMPORTED_LOCATION "${VXSTUDIO_DF_RUNTIME_DFN2_LIB}")
add_dependencies(VxStudioDeepFilterRuntimeDfn2 build_vxstudio_libdf_dfn2)

if(WIN32)
  set(VXSTUDIO_DF_SYSTEM_LIBS
    Bcrypt.lib
    Userenv.lib
    Ntdll.lib
    Advapi32.lib)
elseif(APPLE)
  find_library(SECURITY_FW Security)
  find_library(COREFOUNDATION_FW CoreFoundation)
  find_library(SYSTEMCONFIGURATION_FW SystemConfiguration)
  set(VXSTUDIO_DF_SYSTEM_LIBS
    ${SECURITY_FW}
    ${COREFOUNDATION_FW}
    ${SYSTEMCONFIGURATION_FW}
    resolv)
else()
  set(VXSTUDIO_DF_SYSTEM_LIBS
    pthread
    dl
    m)
endif()

function(vxstudio_link_df_models target_name)
  if(TARGET VxDeepFilterNetModels)
    target_link_libraries(${target_name} PRIVATE VxDeepFilterNetModels)
    target_compile_definitions(${target_name} PRIVATE VXC_HAS_EMBEDDED_DF_MODELS=1)
  endif()
endfunction()

vxstudio_add_framework(VxStudioFramework)

juce_add_plugin(VXDeverb
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXDEVERB}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXDEVERB}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxdeverb"
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
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverb PRIVATE Source)
target_link_libraries(VXDeverb PRIVATE VxStudioFramework Eigen3::Eigen juce::juce_dsp)
add_custom_target(VXDeverbPlugin ALL DEPENDS VXDeverb_VST3)
vxstudio_stage_vst3_bundle(VXDeverb_VST3 "VXDeverb")

juce_add_plugin(VXProximity
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXPROXIMITY}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXPROXIMITY}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxproximity"
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
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
)
target_include_directories(VXProximity PRIVATE Source)
target_link_libraries(VXProximity PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXProximityPlugin ALL DEPENDS VXProximity_VST3)
vxstudio_stage_vst3_bundle(VXProximity_VST3 "VXProximity")

juce_add_plugin(VXCleanup
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXCLEANUP}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXCLEANUP}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxcleanup"
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
  Source/vxstudio/products/cleanup/VxCleanupProcessor.cpp
  Source/vxstudio/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxstudio/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXCleanup PRIVATE Source)
target_link_libraries(VXCleanup PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXCleanupPlugin ALL DEPENDS VXCleanup_VST3)
vxstudio_stage_vst3_bundle(VXCleanup_VST3 "VXCleanup")

juce_add_plugin(VXFinish
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXFINISH}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXFINISH}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxfinish"
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
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/finish/dsp/VxFinishDsp.cpp
  Source/vxstudio/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxstudio/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXFinish PRIVATE Source)
target_link_libraries(VXFinish PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXFinishPlugin ALL DEPENDS VXFinish_VST3)
vxstudio_stage_vst3_bundle(VXFinish_VST3 "VXFinish")

juce_add_plugin(VXOptoComp
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXOPTOCOMP}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXOPTOCOMP}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxoptocomp"
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
  Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxstudio/products/finish/dsp/VxFinishDsp.cpp
  Source/vxstudio/products/OptoComp/OptoCompressorLA2A.cpp
)
target_include_directories(VXOptoComp PRIVATE Source)
target_link_libraries(VXOptoComp PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXOptoCompPlugin ALL DEPENDS VXOptoComp_VST3)
vxstudio_stage_vst3_bundle(VXOptoComp_VST3 "VXOptoComp")

juce_add_plugin(VXSubtract
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXSUBTRACT}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXSUBTRACT}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxsubtract"
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
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
)
target_include_directories(VXSubtract PRIVATE Source)
target_link_libraries(VXSubtract PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXSubtractPlugin ALL DEPENDS VXSubtract_VST3)
vxstudio_stage_vst3_bundle(VXSubtract_VST3 "VXSubtract")

juce_add_plugin(VXDenoiser
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXDENOISER}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXDENOISER}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxdenoiser"
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
  Source/vxstudio/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
)
target_include_directories(VXDenoiser PRIVATE Source)
target_link_libraries(VXDenoiser PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXDenoiserPlugin ALL DEPENDS VXDenoiser_VST3)
vxstudio_stage_vst3_bundle(VXDenoiser_VST3 "VXDenoiser")

juce_add_plugin(VXDeepFilterNet
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXDEEPFILTERNET}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXDEEPFILTERNET}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxdeepfilternet"
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
  Source/vxstudio/products/deepfilternet/VxDeepFilterNetProcessor.cpp
  Source/vxstudio/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
)
target_include_directories(VXDeepFilterNet PRIVATE Source)
target_link_libraries(VXDeepFilterNet PRIVATE
  VxStudioFramework
  VxStudioDeepFilterRuntime
  VxStudioDeepFilterRuntimeDfn2
  ${VXSTUDIO_DF_SYSTEM_LIBS}
  juce::juce_dsp)
add_custom_target(VXDeepFilterNetPlugin ALL DEPENDS VXDeepFilterNet_VST3)
vxstudio_stage_vst3_bundle(VXDeepFilterNet_VST3 "VXDeepFilterNet")
vxstudio_strip_vst3_binary(VXDeepFilterNet_VST3 "VXDeepFilterNet")
vxstudio_prune_vst3_resources(VXDeepFilterNet_VST3 "VXDeepFilterNet"
  "DeepFilterNet2_onnx.tar.gz"
  "DeepFilterNet2_onnx_ll.tar.gz"
  "DeepFilterNet3_onnx.tar.gz")

juce_add_plugin(VXTone
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXTONE}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXTONE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxtone"
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
  Source/vxstudio/products/tone/VxToneProcessor.cpp
)
target_include_directories(VXTone PRIVATE Source)
target_link_libraries(VXTone PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXTonePlugin ALL DEPENDS VXTone_VST3)
vxstudio_stage_vst3_bundle(VXTone_VST3 "VXTone")

juce_add_plugin(VXLeveler
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXLEVELER}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXLEVELER}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxleveler"
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
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VXLeveler PRIVATE Source)
target_link_libraries(VXLeveler PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXLevelerPlugin ALL DEPENDS VXLeveler_VST3)
vxstudio_stage_vst3_bundle(VXLeveler_VST3 "VXLeveler")

juce_add_plugin(VXRebalance
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREBALANCE}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREBALANCE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxrebalance"
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
  Source/vxstudio/products/rebalance/VxRebalanceEditor.cpp
  Source/vxstudio/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.cpp
)
target_include_directories(VXRebalance PRIVATE Source)
target_link_libraries(VXRebalance PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXRebalancePlugin ALL DEPENDS VXRebalance_VST3)
vxstudio_stage_vst3_bundle(VXRebalance_VST3 "VXRebalance")

juce_add_plugin(VXStudioAnalyser
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXSTUDIOANALYSER}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXSTUDIOANALYSER}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxstudioanalyser"
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
  Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserEditor.cpp
)
target_include_directories(VXStudioAnalyser PRIVATE Source)
target_link_libraries(VXStudioAnalyser PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXStudioAnalyserPlugin ALL DEPENDS VXStudioAnalyser_VST3)
vxstudio_stage_vst3_bundle(VXStudioAnalyser_VST3 "VXStudioAnalyser")

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
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverbTests PRIVATE Source tests)
target_link_libraries(VXDeverbTests PRIVATE
  VxStudioFramework
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
)
vxstudio_add_executable_test(VXDeverbTests)

add_executable(VXDeverbMeasure
  tests/VXDeverbMeasure.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXDeverbMeasure PRIVATE Source tests)
target_link_libraries(VXDeverbMeasure PRIVATE
  VxStudioFramework
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
)
vxstudio_add_executable_test(VXDeverbMeasure)

add_executable(VXLevelerMeasure
  tests/VXLevelerMeasure.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VXLevelerMeasure PRIVATE Source tests)
target_link_libraries(VXLevelerMeasure PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXLevelerMeasure PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXLevelerMeasure)

add_executable(VXLevelerTuneSearch
  tests/VXLevelerTuneSearch.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VXLevelerTuneSearch PRIVATE Source tests)
target_link_libraries(VXLevelerTuneSearch PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXLevelerTuneSearch PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXLevelerTuneSearch)

add_executable(VXRebalanceMeasure
  tests/VXRebalanceMeasure.cpp
  Source/vxstudio/products/rebalance/VxRebalanceEditor.cpp
  Source/vxstudio/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.cpp
)
target_include_directories(VXRebalanceMeasure PRIVATE Source tests)
target_link_libraries(VXRebalanceMeasure PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXRebalanceMeasure PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXRebalanceMeasure)

add_executable(VxStudioVoiceAnalysisTests
  tests/VxStudioVoiceAnalysisTests.cpp
)
target_include_directories(VxStudioVoiceAnalysisTests PRIVATE Source)
target_link_libraries(VxStudioVoiceAnalysisTests PRIVATE
  VxStudioFramework
  juce::juce_audio_basics
)
vxstudio_add_executable_test(VxStudioVoiceAnalysisTests)

add_executable(VXStudioPluginRegressionTests
  tests/VXStudioPluginRegressionTests.cpp
  Source/vxstudio/products/cleanup/VxCleanupProcessor.cpp
  Source/vxstudio/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/finish/dsp/VxFinishDsp.cpp
  Source/vxstudio/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxstudio/products/tone/VxToneProcessor.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
  Source/vxstudio/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXStudioPluginRegressionTests PRIVATE Source tests)
target_link_libraries(VXStudioPluginRegressionTests PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXStudioPluginRegressionTests PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXStudioPluginRegressionTests)

add_executable(VXStudioBatchAudioCheck
  tests/VXStudioBatchAudioCheck.cpp
  Source/vxstudio/products/cleanup/VxCleanupProcessor.cpp
  Source/vxstudio/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/finish/dsp/VxFinishDsp.cpp
  Source/vxstudio/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxstudio/products/tone/VxToneProcessor.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
  Source/vxstudio/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXStudioBatchAudioCheck PRIVATE Source tests)
target_link_libraries(VXStudioBatchAudioCheck PRIVATE
  VxStudioFramework
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXStudioBatchAudioCheck PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXStudioBatchAudioCheck)

add_executable(VXSuiteProfile
  tests/VXSuiteProfile.cpp
  Source/vxstudio/products/cleanup/VxCleanupProcessor.cpp
  Source/vxstudio/products/cleanup/dsp/VxCleanupDsp.cpp
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/finish/dsp/VxFinishDsp.cpp
  Source/vxstudio/products/OptoComp/OptoCompressorLA2A.cpp
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxstudio/products/polish/dsp/VxPolishCorrectiveStage.cpp
)
target_include_directories(VXSuiteProfile PRIVATE Source tests)
target_link_libraries(VXSuiteProfile PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXSuiteProfile PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXSuiteProfile)

vxstudio_add_pluginval_test(VXCleanup)
vxstudio_add_pluginval_test(VXDeepFilterNet)
vxstudio_add_pluginval_test(VXDenoiser)
vxstudio_add_pluginval_test(VXDeverb)
vxstudio_add_pluginval_test(VXFinish)
vxstudio_add_pluginval_test(VXOptoComp)
vxstudio_add_pluginval_test(VXProximity)
vxstudio_add_pluginval_test(VXStudioAnalyser)
vxstudio_add_pluginval_test(VXSubtract)
vxstudio_add_pluginval_test(VXTone)
vxstudio_add_pluginval_test(VXLeveler)
