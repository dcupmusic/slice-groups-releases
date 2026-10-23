cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
project(SliceGroups VERSION 0.40.11)
set(CMAKE_CXX_STANDARD 17)
set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_ROOT} juce)
juce_add_plugin(SliceGroups COMPANY_NAME "Slice Groups" BUNDLE_ID "audio.slicegroups.plugin" PLUGIN_MANUFACTURER_CODE Slgr PLUGIN_CODE Slgp FORMATS VST3 Standalone PRODUCT_NAME "Slice Groups" IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
find_package(SQLite3 REQUIRED)
enable_language(OBJCXX)
target_sources(SliceGroups PRIVATE Source/Main.cpp Source/NativeTrash.mm)
target_compile_definitions(SliceGroups PUBLIC SLICE_GROUPS_LEGACY_TRIAL=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SliceGroups PRIVATE "-framework CoreServices" SQLite::SQLite3 juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography PUBLIC juce::juce_recommended_config_flags)
add_executable(SliceGroupsChecks Source/Checks.cpp)
target_link_libraries(SliceGroupsChecks PRIVATE SliceGroups)
if(APPLE)
    add_custom_command(TARGET SliceGroups_VST3 POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:SliceGroups_VST3>"
        VERBATIM)
    add_custom_command(TARGET SliceGroups_Standalone POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:SliceGroups_Standalone>"
        VERBATIM)
endif()

target_include_directories(SliceGroups PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/include)
target_link_libraries(SliceGroups PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/lib/librubberband.a ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/lib/libsamplerate.a "-framework Accelerate")

set(SCNET_RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/scnet-runtime")
set(SCNET_TORCH_SDK "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/torch-sdk" CACHE PATH "PyTorch native development SDK")
add_library(SliceGroupsSCNet SHARED Source/SCNetRuntime.cpp)
set_property(TARGET SliceGroupsSCNet PROPERTY CXX_STANDARD 20)
target_include_directories(SliceGroupsSCNet PRIVATE "${SCNET_TORCH_SDK}/include" "${SCNET_TORCH_SDK}/include/torch/csrc/api/include")
target_link_libraries(SliceGroupsSCNet PRIVATE "${SCNET_RUNTIME}/libtorch.dylib" "${SCNET_RUNTIME}/libtorch_cpu.dylib" "${SCNET_RUNTIME}/libc10.dylib")
set_target_properties(SliceGroupsSCNet PROPERTIES BUILD_RPATH "@loader_path;${SCNET_RUNTIME}" INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR TRUE)
add_executable(SliceGroupsHQ Source/HQProcessMain.cpp)
target_link_libraries(SliceGroupsHQ PRIVATE SliceGroupsSCNet)
set_target_properties(SliceGroupsHQ PROPERTIES BUILD_RPATH "@executable_path/../Frameworks;${SCNET_RUNTIME}")
target_link_libraries(SliceGroups PRIVATE SliceGroupsSCNet)
set_property(TARGET SliceGroupsChecks APPEND PROPERTY BUILD_RPATH "@loader_path/../Frameworks")
foreach(stem_target SliceGroups_VST3 SliceGroups_Standalone)
 add_dependencies(${stem_target} SliceGroupsHQ)
 set_property(TARGET ${stem_target} APPEND PROPERTY BUILD_RPATH "@loader_path/../Frameworks")
 add_custom_command(TARGET ${stem_target} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "${SCNET_RUNTIME}/libtorch.dylib" "${SCNET_RUNTIME}/libtorch_cpu.dylib" "${SCNET_RUNTIME}/libc10.dylib" "${SCNET_RUNTIME}/libomp.dylib" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/"
  COMMAND ${CMAKE_COMMAND} -E copy "${SCNET_RUNTIME}/PyTorch-LICENSE.txt" "${SCNET_RUNTIME}/PyTorch-THIRD-PARTY-NOTICES.txt" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Resources/"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:SliceGroupsSCNet>" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/"
  COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Resources/TrackStems.onnx" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Resources/ONNX-Runtime-LICENSE.txt" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Resources/ONNX-Runtime-ThirdPartyNotices.txt"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Helpers"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:SliceGroupsHQ>" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Helpers/SliceGroupsHQ"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Helpers/SliceGroupsHQ"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Resources"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/libc10.dylib"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/libomp.dylib"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/libtorch_cpu.dylib"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/libtorch.dylib"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>/Contents/Frameworks/libSliceGroupsSCNet.dylib"
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${stem_target}>"
  VERBATIM)
endforeach()
