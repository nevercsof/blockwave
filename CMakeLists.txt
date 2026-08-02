cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# BLOCKWAVE — square-wave-only synthesizer (GPLv3)
# Working title lives here — single point of change (see CLAUDE.md).
# ---------------------------------------------------------------------------
set(BLOCKWAVE_PRODUCT_NAME "BLOCKWAVE")

project(BLOCKWAVE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- JUCE 8 via FetchContent ------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Strict warnings for OUR sources only (JUCE module sources excluded) ----
# CLAUDE.md quality gate: -Wall -Wextra as errors on clang, /W4 /WX on MSVC.
function(blockwave_strict_warnings)
    if(MSVC)
        set(_flags "/W4;/WX")
    else()
        set(_flags "-Wall;-Wextra;-Werror")
    endif()
    set_source_files_properties(${ARGV} PROPERTIES COMPILE_OPTIONS "${_flags}")
endfunction()

# --- Plugin target ----------------------------------------------------------
set(BLOCKWAVE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BLOCKWAVE_FORMATS AU)
endif()

juce_add_plugin(${PROJECT_NAME}
    PRODUCT_NAME                "${BLOCKWAVE_PRODUCT_NAME}"
    COMPANY_NAME                "Kirill Boyko"
    BUNDLE_ID                   "com.kirillboyko.blockwave"
    PLUGIN_MANUFACTURER_CODE    Krbk
    PLUGIN_CODE                 Blkw
    FORMATS                     ${BLOCKWAVE_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)

set(BLOCKWAVE_PLUGIN_SOURCES
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
)

target_sources(${PROJECT_NAME} PRIVATE ${BLOCKWAVE_PLUGIN_SOURCES})
blockwave_strict_warnings(${BLOCKWAVE_PLUGIN_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

# --- Factory preset bank (8 temporary dev presets, replaced in Phase 6) ------
file(GLOB BLOCKWAVE_FACTORY_PRESETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.json")
juce_add_binary_data(BlockwaveFactoryPresets SOURCES ${BLOCKWAVE_FACTORY_PRESETS})

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        BlockwaveFactoryPresets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

# --- Headless renderer: render <preset.json> <input.mid|note:C4:2s> <out.wav>
juce_add_console_app(render PRODUCT_NAME "render")
set(BLOCKWAVE_RENDER_SOURCES tools/render/RenderMain.cpp)
target_sources(render PRIVATE ${BLOCKWAVE_RENDER_SOURCES})
blockwave_strict_warnings(${BLOCKWAVE_RENDER_SOURCES})
target_include_directories(render PRIVATE src)
target_compile_definitions(render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(render PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_recommended_config_flags)

# --- Offline render test suite (pure C++, no JUCE) --------------------------
enable_testing()
add_executable(blockwave_tests tests/TestMain.cpp)
set(BLOCKWAVE_TEST_SOURCES tests/TestMain.cpp)
blockwave_strict_warnings(${BLOCKWAVE_TEST_SOURCES})
target_include_directories(blockwave_tests PRIVATE src)
target_compile_definitions(blockwave_tests PRIVATE
    BLOCKWAVE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_test(NAME blockwave_render_tests COMMAND blockwave_tests)
if(MSVC)
    # Tests use portable fopen deliberately; silence MSVC's C4996 under /WX.
    target_compile_definitions(blockwave_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- Phase-2 state/preset test suite (JUCE: APVTS, presets, session state) ---
juce_add_console_app(blockwave_state_tests PRODUCT_NAME "blockwave_state_tests")
set(BLOCKWAVE_STATE_TEST_SOURCES tests/StateTests.cpp)
target_sources(blockwave_state_tests PRIVATE
    ${BLOCKWAVE_STATE_TEST_SOURCES}
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp)
blockwave_strict_warnings(${BLOCKWAVE_STATE_TEST_SOURCES})
target_include_directories(blockwave_state_tests PRIVATE src plugin tools/render)
target_compile_definitions(blockwave_state_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="BLOCKWAVE"
    BLOCKWAVE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(blockwave_state_tests PRIVATE
    BlockwaveFactoryPresets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
add_test(NAME blockwave_state_tests COMMAND blockwave_state_tests)
if(MSVC)
    # StateTests.cpp includes TestUtil.h (portable fopen) — same C4996 waiver.
    target_compile_definitions(blockwave_state_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
