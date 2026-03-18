function(vxsuite_add_framework target_name)
  add_library(${target_name} STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxsuite/framework/VxSuiteLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxsuite/framework/VxSuiteProcessorBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxsuite/framework/VxSuiteEditorBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxsuite/framework/VxSuiteVoiceAnalysis.cpp
  )

  target_include_directories(${target_name} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
  )

  target_link_libraries(${target_name} PUBLIC
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_graphics
  )

  target_compile_features(${target_name} PUBLIC cxx_std_20)
  target_compile_definitions(${target_name} PUBLIC
    VXSUITE_VERSION_STRING="${PROJECT_VERSION}"
  )
endfunction()
