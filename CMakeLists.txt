cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0167 NEW)
project(VxStudio VERSION 1.0.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# An unset build type silently compiles at -O0, which makes DSP profiling and
# realtime headroom checks meaningless (measured ~10× slower than Release).
# CI always passes -DCMAKE_BUILD_TYPE=Release explicitly; default local builds
# to the same.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "VxStudio: CMAKE_BUILD_TYPE was unset — defaulting to Release")
endif()

set(VXSTUDIO_FRAMEWORK_VERSION "0.2.2")
set(VXSTUDIO_PLUGIN_VERSION_VXDEVERB "0.2.1")
set(VXSTUDIO_PLUGIN_VERSION_VXPROXIMITY "0.3.0")
set(VXSTUDIO_PLUGIN_VERSION_VXPROXIMITYCLASSIC "0.1.0")
set(VXSTUDIO_PLUGIN_VERSION_VXSPEECHCLARITY "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXFINISH "0.3.0")
set(VXSTUDIO_PLUGIN_VERSION_VXOPTOCOMP "0.3.0")
set(VXSTUDIO_PLUGIN_VERSION_VXSUBTRACT "0.2.1")
set(VXSTUDIO_PLUGIN_VERSION_VXDENOISER "0.2.1")
set(VXSTUDIO_PLUGIN_VERSION_VXDEEPFILTERNET "0.2.2")
set(VXSTUDIO_PLUGIN_VERSION_VXTONE "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXREFINE "0.1.0")
set(VXSTUDIO_PLUGIN_VERSION_VXLEVELER "0.2.0")
set(VXSTUDIO_PLUGIN_VERSION_VXREBALANCE "0.2.2")
set(VXSTUDIO_PLUGIN_VERSION_VXSTUDIOANALYSER "0.2.1")
set(VXSTUDIO_PLUGIN_VERSION_VXREPAIR "0.1.1")
set(VXSTUDIO_PLUGIN_VERSION_VXTUNE "0.1.1")
set(VXSTUDIO_PLUGIN_VERSION_VXWIDTH "0.4.0")
option(VXSTUDIO_ENABLE_REBALANCE_AI "Build the experimental, non-product VxRebalanceAI tool" OFF)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/VxStudioVersions.h.in"
  "${CMAKE_BINARY_DIR}/generated/VxStudioVersions.h"
  @ONLY
)

enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/extras/Build/CMake")
add_subdirectory(JUCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt")
endif()
include(VxStudioPlugin)
include(VxStudioValidation)

set(VXSTUDIO_VST3_STAGE_DIR "${CMAKE_SOURCE_DIR}/Source/vxstudio/vst")
set(VXSTUDIO_VST3_INSTALL_DIR "/Library/Audio/Plug-Ins/VST3/VxStudio")
option(VXSTUDIO_INSTALL_VST3_TO_SYSTEM "Copy staged VST3 bundles into the system VST3 folder" OFF)

function(vxstudio_stage_vst3_bundle target_name bundle_name)
  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/$<CONFIG>/VST3/${bundle_name}.vst3")
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSTUDIO_VST3_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${bundle_source_dir}"
            "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMENT "Staging ${bundle_name}.vst3 into Source/vxstudio/vst")

  if(VXSTUDIO_INSTALL_VST3_TO_SYSTEM)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSTUDIO_VST3_INSTALL_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
              "${VXSTUDIO_VST3_INSTALL_DIR}/${bundle_name}.vst3"
      COMMENT "Installing ${bundle_name}.vst3 to system VST3")
  endif()

  add_custom_target(${bundle_name}Stage
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSTUDIO_VST3_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${bundle_source_dir}"
            "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
    DEPENDS ${target_name}
    COMMENT "Refreshing staged ${bundle_name}.vst3 in Source/vxstudio/vst")

  if(VXSTUDIO_INSTALL_VST3_TO_SYSTEM)
    add_custom_command(TARGET ${bundle_name}Stage POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${VXSTUDIO_VST3_INSTALL_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3"
              "${VXSTUDIO_VST3_INSTALL_DIR}/${bundle_name}.vst3"
      COMMENT "Installing staged ${bundle_name}.vst3 to system VST3")
  endif()
endfunction()

function(vxstudio_copy_vst3_resources target_name bundle_name)
  if(NOT ARGN)
    return()
  endif()

  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/$<CONFIG>/VST3/${bundle_name}.vst3")
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

  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/$<CONFIG>/VST3/${bundle_name}.vst3")
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

  set(bundle_source_binary "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/$<CONFIG>/VST3/${bundle_name}.vst3/Contents/MacOS/${bundle_name}")
  set(stage_bundle_binary "${VXSTUDIO_VST3_STAGE_DIR}/${bundle_name}.vst3/Contents/MacOS/${bundle_name}")
  set(bundle_source_dir "${CMAKE_BINARY_DIR}/${bundle_name}_artefacts/$<CONFIG>/VST3/${bundle_name}.vst3")
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
  include(FetchContent)
  FetchContent_Declare(
    Eigen3
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE
  )
  FetchContent_GetProperties(Eigen3)
  if(NOT eigen3_POPULATED)
    FetchContent_Populate(Eigen3)
  endif()
  add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
  target_include_directories(Eigen3::Eigen INTERFACE "${eigen3_SOURCE_DIR}")
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

set(VXSTUDIO_DF_FEATURES "capi,default-model")

if(WIN32)
  set(VXSTUDIO_DF_RUNTIME_LIB "${VXSTUDIO_LIBDF_TARGET_DIR}/df.lib")
  set(VXSTUDIO_DF_RUNTIME_DFN2_LIB "${VXSTUDIO_LIBDF_DFN2_TARGET_DIR}/df.lib")
elseif(APPLE)
  set(VXSTUDIO_DF_RUNTIME_LIB "${VXSTUDIO_LIBDF_TARGET_DIR}/libdf.a")
  set(VXSTUDIO_DF_RUNTIME_DFN2_LIB "${VXSTUDIO_LIBDF_DFN2_TARGET_DIR}/libdf.a")
else()
  set(VXSTUDIO_DF_RUNTIME_LIB "${VXSTUDIO_LIBDF_TARGET_DIR}/libdf.a")
  set(VXSTUDIO_DF_RUNTIME_DFN2_LIB "${VXSTUDIO_LIBDF_DFN2_TARGET_DIR}/libdf.a")
endif()

if(APPLE)
  add_custom_target(build_vxstudio_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_TARGET_ROOT}
            MACOSX_DEPLOYMENT_TARGET=${VXSTUDIO_MACOS_DEPLOYMENT_TARGET}
            ${CARGO_CMD} build --release --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DIR}/Cargo.toml --features ${VXSTUDIO_DF_FEATURES}
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
elseif(WIN32)
  add_custom_target(build_vxstudio_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_TARGET_ROOT}
            ${CARGO_CMD} build --release --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DIR}/Cargo.toml --features ${VXSTUDIO_DF_FEATURES}
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
else()
  add_custom_target(build_vxstudio_libdf
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${VXSTUDIO_LIBDF_TARGET_ROOT}
            ${CARGO_CMD} build --release --no-default-features --manifest-path ${VXSTUDIO_LIBDF_DIR}/Cargo.toml --features ${VXSTUDIO_DF_FEATURES}
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
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXProximity PRIVATE Source)
target_link_libraries(VXProximity PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXProximityPlugin ALL DEPENDS VXProximity_VST3)
vxstudio_stage_vst3_bundle(VXProximity_VST3 "VXProximity")

juce_add_plugin(VXProximityClassic
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXPROXIMITYCLASSIC}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXPROXIMITYCLASSIC}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxProximityClassic"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXP2"
)
target_compile_definitions(VXProximityClassic PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXProximityClassic)
target_sources(VXProximityClassic PRIVATE
  Source/vxstudio/products/ProximityClassic/VxProximityClassicProcessor.cpp
  Source/vxstudio/products/ProximityClassic/dsp/VxProximityClassicDsp.cpp
)
target_include_directories(VXProximityClassic PRIVATE Source)
target_link_libraries(VXProximityClassic PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXProximityClassicPlugin ALL DEPENDS VXProximityClassic_VST3)
vxstudio_stage_vst3_bundle(VXProximityClassic_VST3 "VXProximityClassic")

juce_add_plugin(VXClarity
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXSPEECHCLARITY}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXSPEECHCLARITY}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxclarity"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXCR"
)
target_compile_definitions(VXClarity PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXClarity)
target_sources(VXClarity PRIVATE
  Source/vxstudio/products/speech_clarity/VxSpeechClarityProcessor.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeClickDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeEsserDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDePolosiveDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeBreathDsp.cpp
)
target_include_directories(VXClarity PRIVATE Source)
target_link_libraries(VXClarity PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXClarityPlugin ALL DEPENDS VXClarity_VST3)
vxstudio_stage_vst3_bundle(VXClarity_VST3 "VXClarity")

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

add_library(VxStudioRNNoiseRuntime STATIC
  ThirdParty/rnnoise/src/celt_lpc.c
  ThirdParty/rnnoise/src/denoise.c
  ThirdParty/rnnoise/src/kiss_fft.c
  ThirdParty/rnnoise/src/nnet.c
  ThirdParty/rnnoise/src/nnet_default.c
  ThirdParty/rnnoise/src/parse_lpcnet_weights.c
  ThirdParty/rnnoise/src/pitch.c
  ThirdParty/rnnoise/src/rnn.c
  ThirdParty/rnnoise/src/rnnoise_data.c
  ThirdParty/rnnoise/src/rnnoise_tables.c
)
target_include_directories(VxStudioRNNoiseRuntime PUBLIC
  ThirdParty/rnnoise/include
  ThirdParty/rnnoise/src
)
target_compile_definitions(VxStudioRNNoiseRuntime PRIVATE
  RNNOISE_BUILD
)

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
  VxStudioRNNoiseRuntime
  ${VXSTUDIO_DF_SYSTEM_LIBS}
  juce::juce_dsp)
add_custom_target(VXDeepFilterNetPlugin ALL DEPENDS VXDeepFilterNet_VST3)
vxstudio_stage_vst3_bundle(VXDeepFilterNet_VST3 "VXDeepFilterNet")
vxstudio_strip_vst3_binary(VXDeepFilterNet_VST3 "VXDeepFilterNet")
vxstudio_prune_vst3_resources(VXDeepFilterNet_VST3 "VXDeepFilterNet"
  "DeepFilterNet2_onnx.tar.gz"
  "DeepFilterNet2_onnx_ll.tar.gz")

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

juce_add_plugin(VXTune
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXTUNE}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXTUNE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxtune"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXTU"
)
target_compile_definitions(VXTune PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXTune)
target_sources(VXTune PRIVATE
  Source/vxstudio/products/tune/VxTuneProcessor.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
  Source/vxstudio/products/tune/dsp/VxTuneHarmonicContext.cpp
)
target_include_directories(VXTune PRIVATE
  Source
  ThirdParty/signalsmith-stretch/include
)
target_link_libraries(VXTune PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXTunePlugin ALL DEPENDS VXTune_VST3)
vxstudio_stage_vst3_bundle(VXTune_VST3 "VXTune")

juce_add_plugin(VXWidth
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXWIDTH}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXWIDTH}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxwidth"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXWD"
)
target_compile_definitions(VXWidth PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXWidth)
target_sources(VXWidth PRIVATE
  Source/vxstudio/products/width/VxWidthProcessor.cpp
)
target_include_directories(VXWidth PRIVATE Source)
target_link_libraries(VXWidth PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXWidthPlugin ALL DEPENDS VXWidth_VST3)
vxstudio_stage_vst3_bundle(VXWidth_VST3 "VXWidth")

juce_add_plugin(VXRefine
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREFINE}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREFINE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxrefine"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXRF"
)
target_compile_definitions(VXRefine PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXRefine)
target_sources(VXRefine PRIVATE
  Source/vxstudio/products/tone_refine/VxToneRefineProcessor.cpp
  Source/vxstudio/products/tone_refine/dsp/VxDeMudDsp.cpp
  Source/vxstudio/products/tone_refine/dsp/VxDeHarshnessDsp.cpp
  Source/vxstudio/products/tone_refine/dsp/VxIntelligentSmoothDsp.cpp
)
target_include_directories(VXRefine PRIVATE Source)
target_link_libraries(VXRefine PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXRefinePlugin ALL DEPENDS VXRefine_VST3)
vxstudio_stage_vst3_bundle(VXRefine_VST3 "VXRefine")

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

set(VXSTUDIO_REBALANCE_AI_SPLITTER_ENABLED FALSE)
if(VXSTUDIO_ENABLE_REBALANCE_AI)
juce_add_plugin(VxRebalanceAI
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREBALANCE}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREBALANCE}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxrebalanceai"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VRAI"
)
target_compile_definitions(VxRebalanceAI PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
  VXSTUDIO_REBALANCE_AI_VARIANT=1
)
juce_generate_juce_header(VxRebalanceAI)
target_sources(VxRebalanceAI PRIVATE
  Source/vxstudio/products/rebalance/VxRebalanceEditor.cpp
  Source/vxstudio/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxstudio/products/rebalance/ai/VxAiStemRebalanceDsp.cpp
  Source/vxstudio/products/rebalance/ai/OnnxStemgenBackend.cpp
  Source/vxstudio/products/rebalance/ai/VxRealtimeStemSplitter.cpp
)
target_include_directories(VxRebalanceAI PRIVATE Source)
target_link_libraries(VxRebalanceAI PRIVATE VxStudioFramework juce::juce_dsp)

set(VXSTUDIO_REBALANCE_AI_ORT_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/onnxruntime")
set(VXSTUDIO_REBALANCE_AI_MODEL_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/stemgenrt/model")
set(VXSTUDIO_REBALANCE_AI_MODEL_ONNX "${VXSTUDIO_REBALANCE_AI_MODEL_DIR}/model.onnx")
set(VXSTUDIO_REBALANCE_AI_MODEL_DATA "${VXSTUDIO_REBALANCE_AI_MODEL_DIR}/model.onnx.data")
find_library(VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_LIBRARY
  NAMES onnxruntime
  PATHS "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/lib"
  NO_DEFAULT_PATH
)
if(VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_LIBRARY
   AND EXISTS "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/include"
   AND EXISTS "${VXSTUDIO_REBALANCE_AI_MODEL_ONNX}"
   AND EXISTS "${VXSTUDIO_REBALANCE_AI_MODEL_DATA}")
  set(VXSTUDIO_REBALANCE_AI_SPLITTER_ENABLED TRUE)
  message(STATUS "VxRebalanceAI realtime splitter enabled")
  target_sources(VxRebalanceAI PRIVATE
    ThirdParty/stemgenrt/plugin/source/OnnxRuntime.cpp
  )
  target_include_directories(VxRebalanceAI PRIVATE
    ThirdParty/stemgenrt/plugin/include
    "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/include"
  )
  target_link_libraries(VxRebalanceAI PRIVATE "${VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_LIBRARY}")
  target_compile_definitions(VxRebalanceAI PRIVATE
    VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT=1
    STEMGENRT_USE_ONNXRUNTIME=1
    VXSTUDIO_REBALANCE_AI_MODEL_PATH="${VXSTUDIO_REBALANCE_AI_MODEL_ONNX}"
  )

  if(APPLE)
    get_filename_component(VXSTUDIO_REBALANCE_AI_ORT_REALPATH
      "${VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_LIBRARY}" REALPATH)
    get_filename_component(VXSTUDIO_REBALANCE_AI_ORT_BASENAME
      "${VXSTUDIO_REBALANCE_AI_ORT_REALPATH}" NAME)
    set_property(TARGET VxRebalanceAI_VST3 APPEND PROPERTY BUILD_RPATH "@loader_path/../Frameworks")
    add_custom_command(TARGET VxRebalanceAI_VST3 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${VXSTUDIO_REBALANCE_AI_ORT_REALPATH}"
              "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../Frameworks/${VXSTUDIO_REBALANCE_AI_ORT_BASENAME}"
      COMMAND install_name_tool -id
              "@rpath/${VXSTUDIO_REBALANCE_AI_ORT_BASENAME}"
              "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../Frameworks/${VXSTUDIO_REBALANCE_AI_ORT_BASENAME}"
      COMMAND install_name_tool -change
              "${VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_LIBRARY}"
              "@rpath/${VXSTUDIO_REBALANCE_AI_ORT_BASENAME}"
              "$<TARGET_FILE:VxRebalanceAI_VST3>" || true
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3/Contents/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${VXSTUDIO_REBALANCE_AI_ORT_REALPATH}"
              "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3/Contents/Frameworks/${VXSTUDIO_REBALANCE_AI_ORT_BASENAME}"
      VERBATIM)
  elseif(WIN32)
    find_file(VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_DLL
      NAMES onnxruntime.dll
      PATHS "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/lib" "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/bin"
      NO_DEFAULT_PATH
    )
    find_file(VXSTUDIO_REBALANCE_AI_DIRECTML_DLL
      NAMES DirectML.dll
      PATHS "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/lib" "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/bin"
      NO_DEFAULT_PATH
    )
    file(GLOB VXSTUDIO_REBALANCE_AI_ORT_PROVIDER_DLLS
      "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/lib/onnxruntime_providers_*.dll"
      "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/bin/onnxruntime_providers_*.dll"
    )
    set(VXSTUDIO_REBALANCE_AI_RUNTIME_DLLS "")
    if(VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_DLL)
      list(APPEND VXSTUDIO_REBALANCE_AI_RUNTIME_DLLS "${VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_DLL}")
    endif()
    if(VXSTUDIO_REBALANCE_AI_DIRECTML_DLL)
      list(APPEND VXSTUDIO_REBALANCE_AI_RUNTIME_DLLS "${VXSTUDIO_REBALANCE_AI_DIRECTML_DLL}")
    endif()
    list(APPEND VXSTUDIO_REBALANCE_AI_RUNTIME_DLLS ${VXSTUDIO_REBALANCE_AI_ORT_PROVIDER_DLLS})

    foreach(runtime_dll IN LISTS VXSTUDIO_REBALANCE_AI_RUNTIME_DLLS)
      get_filename_component(runtime_dll_name "${runtime_dll}" NAME)
      add_custom_command(TARGET VxRebalanceAI_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtime_dll}"
                "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/${runtime_dll_name}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtime_dll}"
                "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3/Contents/x86_64-win/${runtime_dll_name}"
        VERBATIM)
    endforeach()
  endif()

  add_custom_command(TARGET VxRebalanceAI_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VXSTUDIO_REBALANCE_AI_MODEL_ONNX}"
            "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../Resources/model.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VXSTUDIO_REBALANCE_AI_MODEL_DATA}"
            "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../Resources/model.onnx.data"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VXSTUDIO_REBALANCE_AI_MODEL_ONNX}"
            "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3/Contents/Resources/model.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VXSTUDIO_REBALANCE_AI_MODEL_DATA}"
            "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3/Contents/Resources/model.onnx.data"
    VERBATIM)
else()
  message(FATAL_ERROR "VxRebalanceAI is experimental and requires ONNX Runtime plus ThirdParty/stemgenrt/model/model.onnx and model.onnx.data")
endif()
add_custom_target(VxRebalanceAIPlugin DEPENDS VxRebalanceAI_VST3)
vxstudio_stage_vst3_bundle(VxRebalanceAI_VST3 "VxRebalanceAI")
if(VXSTUDIO_REBALANCE_AI_SPLITTER_ENABLED AND APPLE)
  add_custom_command(TARGET VxRebalanceAI_VST3 POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE_DIR:VxRebalanceAI_VST3>/../.."
    COMMAND /usr/bin/codesign --force --sign - "${VXSTUDIO_VST3_STAGE_DIR}/VxRebalanceAI.vst3"
    COMMENT "Signing VxRebalanceAI.vst3 after bundling realtime splitter resources"
    VERBATIM)
endif()
else()
  message(STATUS "VxRebalanceAI experimental tool disabled; set VXSTUDIO_ENABLE_REBALANCE_AI=ON to build it")
endif()

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
  Source/vxstudio/products/analyser/VXStudioAnalyserController.cpp
)
target_include_directories(VXStudioAnalyser PRIVATE Source)
target_link_libraries(VXStudioAnalyser PRIVATE VxStudioFramework juce::juce_dsp)
add_custom_target(VXStudioAnalyserPlugin ALL DEPENDS VXStudioAnalyser_VST3)
vxstudio_stage_vst3_bundle(VXStudioAnalyser_VST3 "VXStudioAnalyser")
juce_add_plugin(VXRepair
  VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREPAIR}
  BUILD_VERSION ${VXSTUDIO_PLUGIN_VERSION_VXREPAIR}
  FORMATS VST3
  COMPANY_NAME "VX Suite"
  BUNDLE_ID "com.vxstudio.vxrepair"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PLUGIN_MANUFACTURER_CODE "VXS1"
  PLUGIN_CODE "VXRP"
)
target_compile_definitions(VXRepair PRIVATE
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_FORCE_USE_LEGACY_PARAM_IDS=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
  JUCE_WEB_BROWSER=0
)
juce_generate_juce_header(VXRepair)
target_sources(VXRepair PRIVATE
  Source/vxstudio/products/repair/VxRepairAnalysis.cpp
  Source/vxstudio/products/repair/VxRepairProcessor.cpp
  Source/vxstudio/products/repair/VxRepairEditor.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeClickDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeEsserDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDePolosiveDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeBreathDsp.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
)
target_include_directories(VXRepair PRIVATE Source)
target_link_libraries(VXRepair PRIVATE
  VxStudioFramework
  Eigen3::Eigen
  juce::juce_dsp
  VxStudioDeepFilterRuntime
  VxStudioDeepFilterRuntimeDfn2
  VxStudioRNNoiseRuntime
  ${VXSTUDIO_DF_SYSTEM_LIBS}
)
add_custom_target(VXRepairPlugin ALL DEPENDS VXRepair_VST3)
vxstudio_stage_vst3_bundle(VXRepair_VST3 "VXRepair")


add_custom_target(VXSuite_VST3
  DEPENDS
    VXDeverb_VST3
    VXProximity_VST3
    VXProximityClassic_VST3
    VXClarity_VST3
    VXFinish_VST3
    VXOptoComp_VST3
    VXSubtract_VST3
    VXDenoiser_VST3
    VXDeepFilterNet_VST3
    VXTone_VST3
    VXRefine_VST3
    VXLeveler_VST3
    VXRebalance_VST3
    VXStudioAnalyser_VST3
    VXRepair_VST3
    VXDeverbStage
    VXProximityStage
    VXClarityStage
    VXFinishStage
    VXOptoCompStage
    VXSubtractStage
    VXDenoiserStage
    VXDeepFilterNetStage
    VXToneStage
    VXRefineStage
    VXLevelerStage
    VXRebalanceStage
    VXStudioAnalyserStage
    VXRepairStage
  COMMENT "Building all VX Suite VST3 products")

add_executable(VxTuneAnalysisTests
  tests/VxTuneAnalysisTests.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
  Source/vxstudio/products/tune/dsp/VxTuneHarmonicContext.cpp
)
target_include_directories(VxTuneAnalysisTests PRIVATE
  Source
  tests
  ThirdParty/signalsmith-stretch/include
)
target_link_libraries(VxTuneAnalysisTests PRIVATE juce::juce_dsp)
vxstudio_add_executable_test(VxTuneAnalysisTests)

add_executable(VxTuneRendererTests
  tests/VxTuneRendererTests.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
)
target_include_directories(VxTuneRendererTests PRIVATE
  Source
  tests
  ThirdParty/signalsmith-stretch/include
)
vxstudio_add_executable_test(VxTuneRendererTests)

add_executable(VxTunePluginTests
  tests/VxTunePluginTests.cpp
  Source/vxstudio/products/tune/VxTuneProcessor.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
  Source/vxstudio/products/tune/dsp/VxTuneHarmonicContext.cpp
)
target_include_directories(VxTunePluginTests PRIVATE
  Source
  tests
  ThirdParty/signalsmith-stretch/include
)
target_link_libraries(VxTunePluginTests PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VxTunePluginTests PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VxTunePluginTests)

add_executable(VxTuneBatchHarness
  tools/VxTuneBatchHarness.cpp
  Source/vxstudio/products/tune/VxTuneProcessor.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
  Source/vxstudio/products/tune/dsp/VxTuneHarmonicContext.cpp
)
target_include_directories(VxTuneBatchHarness PRIVATE
  Source
  tests
  ThirdParty/signalsmith-stretch/include
)
target_link_libraries(VxTuneBatchHarness PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VxTuneBatchHarness PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)

add_executable(VxTuneCompareReference
  tools/VxTuneCompareReference.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
)
target_include_directories(VxTuneCompareReference PRIVATE
  Source
  tests
)
target_link_libraries(VxTuneCompareReference PRIVATE
  VxStudioFramework
  juce::juce_audio_formats
)

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

add_executable(VxLevelerRiderTests
  tests/VxLevelerRiderTests.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
)
target_include_directories(VxLevelerRiderTests PRIVATE Source tests)
target_link_libraries(VxLevelerRiderTests PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VxLevelerRiderTests PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VxLevelerRiderTests)

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

if(VXSTUDIO_REBALANCE_AI_SPLITTER_ENABLED)
  add_executable(VxAiStemMixerTests
    tests/VxAiStemMixerTests.cpp
    Source/vxstudio/products/rebalance/ai/VxAiStemRebalanceDsp.cpp
  )
  target_include_directories(VxAiStemMixerTests PRIVATE Source tests)
  target_link_libraries(VxAiStemMixerTests PRIVATE
    VxStudioFramework
    juce::juce_audio_basics
  )
  vxstudio_add_executable_test(VxAiStemMixerTests)

  add_executable(VxRebalanceAISmokeTest
    tests/VxRebalanceAISmokeTest.cpp
    Source/vxstudio/products/rebalance/ai/VxAiStemRebalanceDsp.cpp
    Source/vxstudio/products/rebalance/ai/OnnxStemgenBackend.cpp
    Source/vxstudio/products/rebalance/ai/VxRealtimeStemSplitter.cpp
    ThirdParty/stemgenrt/plugin/source/OnnxRuntime.cpp
  )
  target_include_directories(VxRebalanceAISmokeTest PRIVATE
    Source
    tests
    ThirdParty/stemgenrt/plugin/include
    "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/include"
  )
  target_link_libraries(VxRebalanceAISmokeTest PRIVATE
    VxStudioFramework
    juce::juce_dsp
    juce::juce_audio_basics
    "${VXSTUDIO_REBALANCE_AI_ONNXRUNTIME_LIBRARY}"
  )
  target_compile_definitions(VxRebalanceAISmokeTest PRIVATE
    VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT=1
    STEMGENRT_USE_ONNXRUNTIME=1
    VXSTUDIO_REBALANCE_AI_MODEL_PATH="${VXSTUDIO_REBALANCE_AI_MODEL_ONNX}"
  )
  if(APPLE)
    set_property(TARGET VxRebalanceAISmokeTest APPEND PROPERTY
      BUILD_RPATH "${VXSTUDIO_REBALANCE_AI_ORT_DIR}/lib")
  endif()
  vxstudio_add_executable_test(VxRebalanceAISmokeTest)
endif()

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
  Source/vxstudio/products/analyser/VXStudioAnalyserEditor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserController.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/deepfilternet/VxDeepFilterNetProcessor.cpp
  Source/vxstudio/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
  Source/vxstudio/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxstudio/products/ProximityClassic/VxProximityClassicProcessor.cpp
  Source/vxstudio/products/ProximityClassic/dsp/VxProximityClassicDsp.cpp
  Source/vxstudio/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxstudio/products/rebalance/VxRebalanceEditor.cpp
  Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.cpp
  Source/vxstudio/products/speech_clarity/VxSpeechClarityProcessor.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeClickDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeEsserDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDePolosiveDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeBreathDsp.cpp
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxstudio/products/tone/VxToneProcessor.cpp
  Source/vxstudio/products/tone_refine/VxToneRefineProcessor.cpp
  Source/vxstudio/products/tone_refine/dsp/VxDeHarshnessDsp.cpp
  Source/vxstudio/products/tone_refine/dsp/VxDeMudDsp.cpp
  Source/vxstudio/products/tone_refine/dsp/VxIntelligentSmoothDsp.cpp
  Source/vxstudio/products/repair/VxRepairProcessor.cpp
  Source/vxstudio/products/repair/VxRepairAnalysis.cpp
  Source/vxstudio/products/repair/VxRepairEditor.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
  Source/vxstudio/products/tune/VxTuneProcessor.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
  Source/vxstudio/products/tune/dsp/VxTuneHarmonicContext.cpp
  Source/vxstudio/products/width/VxWidthProcessor.cpp
)
target_include_directories(VXStudioPluginRegressionTests PRIVATE
  Source
  tests
  ThirdParty/signalsmith-stretch/include
)
target_link_libraries(VXStudioPluginRegressionTests PRIVATE
  VxStudioFramework
  VxStudioDeepFilterRuntime
  VxStudioDeepFilterRuntimeDfn2
  VxStudioRNNoiseRuntime
  ${VXSTUDIO_DF_SYSTEM_LIBS}
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXStudioPluginRegressionTests PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXStudioPluginRegressionTests)

add_executable(VXStudioAnalyserTests
  tests/VXStudioAnalyserTests.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserEditor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserController.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
)
target_include_directories(VXStudioAnalyserTests PRIVATE Source tests)
target_link_libraries(VXStudioAnalyserTests PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXStudioAnalyserTests PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXStudioAnalyserTests)

add_executable(VXRepairPassthroughCheck
  tests/VXRepairPassthroughCheck.cpp
  Source/vxstudio/products/repair/VxRepairAnalysis.cpp
  Source/vxstudio/products/repair/VxRepairProcessor.cpp
  Source/vxstudio/products/repair/VxRepairEditor.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeClickDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeEsserDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDePolosiveDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeBreathDsp.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
)
target_include_directories(VXRepairPassthroughCheck PRIVATE Source tests)
target_link_libraries(VXRepairPassthroughCheck PRIVATE
  VxStudioFramework
  VxStudioDeepFilterRuntime
  VxStudioDeepFilterRuntimeDfn2
  VxStudioRNNoiseRuntime
  ${VXSTUDIO_DF_SYSTEM_LIBS}
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXRepairPassthroughCheck PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXRepairPassthroughCheck)

add_executable(VXWidthShellCheck
  tests/VXWidthShellCheck.cpp
  Source/vxstudio/products/width/VxWidthProcessor.cpp
)
target_include_directories(VXWidthShellCheck PRIVATE Source tests)
target_link_libraries(VXWidthShellCheck PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXWidthShellCheck PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXWidthShellCheck)

# Manual offline research tool (2026-08-07 imbalance investigation) - NOT
# registered via vxstudio_add_executable_test, deliberately not part of the
# regression suite/CI. Run on demand: ./VXWidthAdtSeedOptimizer
add_executable(VXWidthAdtSeedOptimizer
  tests/VXWidthAdtSeedOptimizer.cpp
)
target_include_directories(VXWidthAdtSeedOptimizer PRIVATE Source tests)
target_link_libraries(VXWidthAdtSeedOptimizer PRIVATE
  VxStudioFramework
  juce::juce_dsp
  juce::juce_audio_processors
  juce::juce_audio_formats
)
target_compile_definitions(VXWidthAdtSeedOptimizer PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)

add_executable(VXStudioBatchAudioCheck
  tests/VXStudioBatchAudioCheck.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxstudio/products/ProximityClassic/VxProximityClassicProcessor.cpp
  Source/vxstudio/products/ProximityClassic/dsp/VxProximityClassicDsp.cpp
  Source/vxstudio/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxstudio/products/rebalance/VxRebalanceEditor.cpp
  Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.cpp
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxstudio/products/tone/VxToneProcessor.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
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
  tests/VXStudioProfile.cpp
  Source/vxstudio/products/finish/VxFinishProcessor.cpp
  Source/vxstudio/products/proximity/VxProximityProcessor.cpp
  Source/vxstudio/products/proximity/dsp/VxProximityDsp.cpp
  Source/vxstudio/products/ProximityClassic/VxProximityClassicProcessor.cpp
  Source/vxstudio/products/ProximityClassic/dsp/VxProximityClassicDsp.cpp
  Source/vxstudio/products/subtract/VxSubtractProcessor.cpp
  Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp
  Source/vxstudio/products/tone/VxToneProcessor.cpp
  Source/vxstudio/products/tone_refine/VxToneRefineProcessor.cpp
  Source/vxstudio/products/tone_refine/dsp/VxDeHarshnessDsp.cpp
  Source/vxstudio/products/tone_refine/dsp/VxDeMudDsp.cpp
  Source/vxstudio/products/tone_refine/dsp/VxIntelligentSmoothDsp.cpp
  Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp
  Source/vxstudio/products/speech_clarity/VxSpeechClarityProcessor.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeClickDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeEsserDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDePolosiveDsp.cpp
  Source/vxstudio/products/speech_clarity/dsp/VxDeBreathDsp.cpp
  Source/vxstudio/products/leveler/VxLevelerProcessor.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDetector.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp
  Source/vxstudio/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp
  Source/vxstudio/products/denoiser/VxDenoiserProcessor.cpp
  Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp
  Source/vxstudio/products/deverb/VxDeverbProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbRt60Estimator.cpp
  Source/vxstudio/products/deverb/dsp/VxDeverbWpeStage.cpp
  Source/vxstudio/products/deepfilternet/VxDeepFilterNetProcessor.cpp
  Source/vxstudio/products/deepfilternet/dsp/VxDeepFilterNetService.cpp
  Source/vxstudio/products/rebalance/VxRebalanceProcessor.cpp
  Source/vxstudio/products/rebalance/VxRebalanceEditor.cpp
  Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.cpp
  Source/vxstudio/products/repair/VxRepairProcessor.cpp
  Source/vxstudio/products/repair/VxRepairAnalysis.cpp
  Source/vxstudio/products/repair/VxRepairEditor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserController.cpp
  Source/vxstudio/products/analyser/VXStudioAnalyserEditor.cpp
  Source/vxstudio/products/tune/VxTuneProcessor.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchDetector.cpp
  Source/vxstudio/products/tune/dsp/VxTuneDecomposition.cpp
  Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.cpp
  Source/vxstudio/products/tune/dsp/VxTuneHarmonicContext.cpp
  Source/vxstudio/products/width/VxWidthProcessor.cpp
)
target_include_directories(VXSuiteProfile PRIVATE
  Source
  tests
  ThirdParty/signalsmith-stretch/include
)
target_link_libraries(VXSuiteProfile PRIVATE
  VxStudioFramework
  VxStudioDeepFilterRuntime
  VxStudioDeepFilterRuntimeDfn2
  VxStudioRNNoiseRuntime
  ${VXSTUDIO_DF_SYSTEM_LIBS}
  Eigen3::Eigen
  juce::juce_dsp
  juce::juce_audio_processors
)
target_compile_definitions(VXSuiteProfile PRIVATE VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT=1)
vxstudio_add_executable_test(VXSuiteProfile)

vxstudio_add_pluginval_test(VXDeepFilterNet)
vxstudio_add_pluginval_test(VXDenoiser)
vxstudio_add_pluginval_test(VXDeverb)
vxstudio_add_pluginval_test(VXFinish)
vxstudio_add_pluginval_test(VXOptoComp)
vxstudio_add_pluginval_test(VXProximity)
vxstudio_add_pluginval_test(VXProximityClassic)
vxstudio_add_pluginval_test(VXStudioAnalyser)
vxstudio_add_pluginval_test(VXSubtract)
vxstudio_add_pluginval_test(VXTone)
vxstudio_add_pluginval_test(VXLeveler)
vxstudio_add_pluginval_test(VXWidth)

# Test Host for CPU/Memory Profiling
add_subdirectory(Source/vxstudio/tools/test_host)
