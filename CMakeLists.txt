cmake_minimum_required(VERSION 3.22)
project(AxellSpace VERSION 3.0.0)
# the version for the GUI (every target: the plugin, tests and tools draw the same header)
add_compile_definitions(AXELL_VERSION_STRING="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.15
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Axell core (shared DSP + look): next to this project, or vendored into external/axell-core for releases
set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/external/axell-core" CACHE PATH "Axell core")
if(NOT EXISTS "${AXELL_CORE_DIR}/CMakeLists.txt")
  set(AXELL_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../axell-core" CACHE PATH "Axell core" FORCE)
endif()
add_subdirectory(${AXELL_CORE_DIR} axell-core)

juce_add_plugin(AxellSpace
  COMPANY_NAME              "Axell Yobag"
  COMPANY_EMAIL             "axellpetre@protonmail.com"
  BUNDLE_ID                 com.axellyobag.axellspace
  PLUGIN_MANUFACTURER_CODE  Axyb
  PLUGIN_CODE               Spc1
  FORMATS                   VST3
  PRODUCT_NAME              "Axell Space"
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx Reverb Delay)

set(SPACE_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(SPACE_DEFS JUCE_WEB_BROWSER=0 AXELL_THEME_HUE=285 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_sources(AxellSpace PRIVATE ${SPACE_SOURCES})
target_compile_definitions(AxellSpace PUBLIC ${SPACE_DEFS})
axell_core_add_gui(AxellSpace)
target_link_libraries(AxellSpace PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(SPACE_SANITIZE "AddressSanitizer + UBSan for the test programs" OFF)
function(space_sanitize target)
  if(SPACE_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# DSP suite (JUCE-free: the engine headers exactly as they ship)
enable_testing()
add_executable(SpaceDspTests tests/DspTests.cpp)
target_include_directories(SpaceDspTests PRIVATE Source)
target_link_libraries(SpaceDspTests PRIVATE axell_core_dsp)
target_compile_options(SpaceDspTests PRIVATE -Wall -Wextra -Wshadow -Wno-mismatched-new-delete $<$<CONFIG:Release>:-O3>)
space_sanitize(SpaceDspTests)
add_test(NAME dsp_suite COMMAND SpaceDspTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# plugin suite + GUI snapshots (JUCE)
foreach(tool SpacePluginTests SpaceGuiSnapshot)
  juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
  target_compile_definitions(${tool} PRIVATE ${SPACE_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Axell Space")
  target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  axell_core_add_gui(${tool})
  space_sanitize(${tool})
endforeach()
target_sources(SpacePluginTests PRIVATE tests/PluginTests.cpp ${SPACE_SOURCES})
target_sources(SpaceGuiSnapshot PRIVATE tests/GuiSnapshot.cpp ${SPACE_SOURCES})
add_test(NAME plugin_suite COMMAND SpacePluginTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# independent black-box audit host (from the core)
juce_add_console_app(SpaceAuditHost PRODUCT_NAME "SpaceAuditHost")
target_sources(SpaceAuditHost PRIVATE ${AXELL_CORE_DIR}/tools/AuditHost.cpp)
target_compile_definitions(SpaceAuditHost PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpaceAuditHost PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)

# research / benchmark tools (JUCE-free; the shipping engine headers)
foreach(tool SpaceRender)
  add_executable(${tool} tools/${tool}.cpp)
  target_include_directories(${tool} PRIVATE Source)
  target_link_libraries(${tool} PRIVATE axell_core_dsp)
  target_compile_options(${tool} PRIVATE -O3)
endforeach()
