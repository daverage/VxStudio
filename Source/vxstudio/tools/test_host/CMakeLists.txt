# VxStudio Test Host - Profiling and regression testing tool

add_executable(VxStudioTestHost
    Main.cpp
)

target_include_directories(VxStudioTestHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JUCE_SOURCE_DIR}
)

target_link_libraries(VxStudioTestHost PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
)

# C++17 features
target_compile_features(VxStudioTestHost PRIVATE cxx_std_17)

# Optimizations for profiling
if (MSVC)
    target_compile_options(VxStudioTestHost PRIVATE /O2 /W4)
else()
    target_compile_options(VxStudioTestHost PRIVATE -O2 -Wall -Wextra)
    if (NOT APPLE)
        # Enable profiling symbols on Linux
        target_compile_options(VxStudioTestHost PRIVATE -fno-omit-frame-pointer)
        target_link_options(VxStudioTestHost PRIVATE -fno-omit-frame-pointer)
    endif()
endif()

# Set output directory
set_target_properties(VxStudioTestHost PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
