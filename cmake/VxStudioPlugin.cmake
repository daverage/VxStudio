function(vxstudio_add_framework target_name)
  add_library(${target_name} STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioHelpView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioLevelTraceView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioModelAssets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioProcessorBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioSignalQuality.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioEditorBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioSpectrumTelemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioUiHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioVoiceAnalysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/vxstudio/framework/VxStudioVoiceContext.cpp
  )

  target_include_directories(${target_name} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_BINARY_DIR}/generated
  )

  target_link_libraries(${target_name} PUBLIC
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_graphics
  )

  target_compile_features(${target_name} PUBLIC cxx_std_20)
endfunction()
