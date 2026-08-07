cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# BLOCKWAVE — square-wave-only synthesizer (GPLv3)
# Working title lives here — single point of change (see CLAUDE.md).
# ---------------------------------------------------------------------------
set(BLOCKWAVE_PRODUCT_NAME "BLOCKWAVE")

# --- Versioning -------------------------------------------------------------
# CMake's project(VERSION) and JUCE's plugin VERSION must both be strictly
# numeric (JUCE turns them into VST3/AU/bundle version codes), so a pre-release
# suffix cannot live there. The numeric release version is therefore 1.0.0 and
# the pre-release tag is carried separately:
#
#   BLOCKWAVE_VERSION         1.0.0        -> VST3 / AU / .app bundle version
#   BLOCKWAVE_VERSION_TAG     rc1          -> "" for a final release
#   BLOCKWAVE_VERSION_STRING  1.0.0-rc1    -> human-facing string (UI/About/zip)
#
# To ship 1.0.0 final: clear BLOCKWAVE_VERSION_TAG (set it to ""). Nothing else
# changes. See docs/RELEASE.md.
project(BLOCKWAVE VERSION 1.0.0 LANGUAGES C CXX)
set(BLOCKWAVE_VERSION_TAG "")
if(BLOCKWAVE_VERSION_TAG STREQUAL "")
    set(BLOCKWAVE_VERSION_STRING "${PROJECT_VERSION}")
else()
    set(BLOCKWAVE_VERSION_STRING "${PROJECT_VERSION}-${BLOCKWAVE_VERSION_TAG}")
endif()
message(STATUS "BLOCKWAVE version: ${BLOCKWAVE_VERSION_STRING} (numeric ${PROJECT_VERSION})")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- JUCE 8 via FetchContent ------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    # Pinned to the COMMIT the 8.0.8 tag pointed at on 2026-08-07, not to the
    # tag itself. A git tag is mutable: whoever controls the upstream repo can
    # move it, and every clean build here and in CI would then silently fetch
    # different dependency code. A commit hash cannot be repointed.
    # To upgrade: resolve the new tag with
    #   git ls-remote https://github.com/juce-framework/JUCE.git refs/tags/<tag>
    # and replace the hash below, noting the tag it came from.
    GIT_TAG        d6181bde38d858c283c3b7bf699ce6340c050b5d   # tag 8.0.8
    GIT_SHALLOW    FALSE
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
    VERSION                     "${PROJECT_VERSION}"
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

set(BLOCKWAVE_UI_SOURCES
    plugin/ui/PixelFont.cpp
    plugin/ui/PixelLookAndFeel.cpp
    plugin/ui/ParamCells.cpp
    plugin/ui/BlockPanel.cpp
    plugin/ui/TweakTab.cpp
    plugin/ui/TopBar.cpp
    plugin/ui/PresetBrowser.cpp
    plugin/ui/MaterialArt.cpp
    plugin/ui/CraftBlocks.cpp
    plugin/ui/CraftTab.cpp
    plugin/ui/KeyStrip.cpp
)

set(BLOCKWAVE_PLUGIN_SOURCES
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    ${BLOCKWAVE_UI_SOURCES}
)

target_sources(${PROJECT_NAME} PRIVATE ${BLOCKWAVE_PLUGIN_SOURCES})
blockwave_strict_warnings(${BLOCKWAVE_PLUGIN_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

# --- Factory preset bank (8 temporary dev presets, replaced in Phase 6) ------
file(GLOB BLOCKWAVE_FACTORY_PRESETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.json")
juce_add_binary_data(BlockwaveFactoryPresets SOURCES ${BLOCKWAVE_FACTORY_PRESETS})

# --- Recipe book (Phase 4; schema in docs/RECIPES_FORMAT.md) -----------------
juce_add_binary_data(BlockwaveRecipeData
    HEADER_NAME RecipeData.h
    NAMESPACE RecipeData
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/data/recipes.json")

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        BLOCKWAVE_VERSION_STRING="${BLOCKWAVE_VERSION_STRING}"
)

# Version stamp for the packaging scripts (scripts/package-macos.sh and the
# Windows packaging step in .github/workflows/ci.yml read this file, so the zip
# name can never drift from the built binaries).
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/blockwave-version.txt"
     CONTENT "${BLOCKWAVE_VERSION_STRING}\n")

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        BlockwaveFactoryPresets
        BlockwaveRecipeData
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
    BlockwaveRecipeData
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
    ${BLOCKWAVE_PLUGIN_SOURCES})
blockwave_strict_warnings(${BLOCKWAVE_STATE_TEST_SOURCES})
target_include_directories(blockwave_state_tests PRIVATE src plugin tools/render)
target_compile_definitions(blockwave_state_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # The UI component tests have to let REAL time pass with the message loop
    # running (tests/UiComponentTests.h): the scale settle guard is keyed on
    # pointer movement rather than on a clock, and the only honest way to prove
    # that is to run the clock — with juce::Timer actually dispatching — and
    # show nothing expires. Test binary only; the plugin never runs a modal loop.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="BLOCKWAVE"
    BLOCKWAVE_VERSION_STRING="${BLOCKWAVE_VERSION_STRING}"
    BLOCKWAVE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(blockwave_state_tests PRIVATE
    BlockwaveFactoryPresets
    BlockwaveRecipeData
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
add_test(NAME blockwave_state_tests COMMAND blockwave_state_tests)
if(MSVC)
    # StateTests.cpp includes TestUtil.h (portable fopen) — same C4996 waiver.
    target_compile_definitions(blockwave_state_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- Phase-3 headless UI screenshot renderer ---------------------------------
# blockwave_screenshots [outDir] — renders every screen offscreen via
# Component::createComponentSnapshot so qa-runner / the human can review
# look-and-feel without launching a host. Default outDir: CHECKPOINTS/screenshots.
juce_add_console_app(blockwave_screenshots PRODUCT_NAME "blockwave_screenshots")
set(BLOCKWAVE_SCREENSHOT_SOURCES tools/screenshots/ScreenshotsMain.cpp)
target_sources(blockwave_screenshots PRIVATE
    ${BLOCKWAVE_SCREENSHOT_SOURCES}
    ${BLOCKWAVE_PLUGIN_SOURCES})
blockwave_strict_warnings(${BLOCKWAVE_SCREENSHOT_SOURCES})
target_include_directories(blockwave_screenshots PRIVATE src plugin)
target_compile_definitions(blockwave_screenshots PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="BLOCKWAVE"
    BLOCKWAVE_VERSION_STRING="${BLOCKWAVE_VERSION_STRING}")
target_link_libraries(blockwave_screenshots PRIVATE
    BlockwaveFactoryPresets
    BlockwaveRecipeData
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)

# --- Press-kit image renderer -------------------------------------------------
# blockwave_presskit [frames|logo] [outDir] — sibling of blockwave_screenshots.
# "frames" renders a numbered PNG sequence of one real craft ending in a recipe
# discovery (assembled into the launch GIF by tools/presskit/make_gif.py);
# "logo" renders the wordmark / avatar pack from the product's own PixelFont.
juce_add_console_app(blockwave_presskit PRODUCT_NAME "blockwave_presskit")
set(BLOCKWAVE_PRESSKIT_SOURCES tools/presskit/PresskitMain.cpp)
target_sources(blockwave_presskit PRIVATE
    ${BLOCKWAVE_PRESSKIT_SOURCES}
    ${BLOCKWAVE_PLUGIN_SOURCES})
blockwave_strict_warnings(${BLOCKWAVE_PRESSKIT_SOURCES})
target_include_directories(blockwave_presskit PRIVATE src plugin)
target_compile_definitions(blockwave_presskit PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="BLOCKWAVE"
    BLOCKWAVE_VERSION_STRING="${BLOCKWAVE_VERSION_STRING}")
target_link_libraries(blockwave_presskit PRIVATE
    BlockwaveFactoryPresets
    BlockwaveRecipeData
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
