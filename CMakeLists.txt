cmake_minimum_required(VERSION 3.15)

project(MonarchOfTone VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment target")

add_subdirectory(libs/JUCE)
add_subdirectory(libs/xsimd)
add_subdirectory(libs/chowdsp_wdf)

# Treat vendored deps as SYSTEM headers so their (harmless) warnings don't leak into our
# targets — keeps -Wall -Wextra (and a future -Werror CI gate, per build.md) enforced on our
# code only. JUCE already marks its own includes system.
foreach(dep chowdsp_wdf xsimd)
    if(TARGET ${dep})
        get_target_property(_inc ${dep} INTERFACE_INCLUDE_DIRECTORIES)
        if(_inc)
            set_target_properties(${dep} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
        endif()
    endif()
endforeach()

juce_add_plugin(Monarch
    COMPANY_NAME "Leigh Pierce"
    BUNDLE_ID "com.leighpierce.monarchoftone"
    PLUGIN_MANUFACTURER_CODE LeP1
    PLUGIN_CODE Mon1
    VERSION ${PROJECT_VERSION}
    FORMATS AU Standalone
    PRODUCT_NAME "Monarch of Tone"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Monarch
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/ui/MonarchLookAndFeel.cpp
        src/ui/PedalFace.cpp
)

target_include_directories(Monarch
    PRIVATE
        src
)

target_compile_definitions(Monarch
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_options(Monarch
    PRIVATE
        -Wall -Wextra
)

target_link_libraries(Monarch
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
    PUBLIC
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_executable(SmokeTest_RC tests/SmokeTest_RC.cpp)

target_link_libraries(SmokeTest_RC
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(SmokeTest_RC
    PRIVATE
        -Wall -Wextra
)

add_executable(Stage1_FreqResponse tests/Stage1_FreqResponse.cpp)

target_link_libraries(Stage1_FreqResponse
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(Stage1_FreqResponse
    PRIVATE
        -Wall -Wextra
)

add_executable(Stage1_HiGain tests/Stage1_HiGain.cpp)

target_link_libraries(Stage1_HiGain
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(Stage1_HiGain
    PRIVATE
        -Wall -Wextra
)

add_executable(VolumePot_Taper tests/VolumePot_Taper.cpp)

target_link_libraries(VolumePot_Taper
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(VolumePot_Taper
    PRIVATE
        -Wall -Wextra
)

add_executable(FullChain_DualChannel tests/FullChain_DualChannel.cpp)

target_link_libraries(FullChain_DualChannel
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(FullChain_DualChannel
    PRIVATE
        -Wall -Wextra
)

add_executable(ToneStage_FreqResponse tests/ToneStage_FreqResponse.cpp)

target_link_libraries(ToneStage_FreqResponse
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(ToneStage_FreqResponse
    PRIVATE
        -Wall -Wextra
)

add_executable(Stage2_Gain tests/Stage2_Gain.cpp)

target_link_libraries(Stage2_Gain
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(Stage2_Gain
    PRIVATE
        -Wall -Wextra
)

add_executable(SW1SoftClip_Sine tests/SW1SoftClip_Sine.cpp)

target_link_libraries(SW1SoftClip_Sine
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(SW1SoftClip_Sine
    PRIVATE
        -Wall -Wextra
)

add_executable(SW2HardClip_Sine tests/SW2HardClip_Sine.cpp)

target_link_libraries(SW2HardClip_Sine
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
)

target_compile_options(SW2HardClip_Sine
    PRIVATE
        -Wall -Wextra
)

# ---- Headless UI snapshot tool (renders the editor to /tmp/monarch_ui.png; no display needed) ----
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")

target_sources(UISnapshot
    PRIVATE
        tools/UISnapshot.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/ui/MonarchLookAndFeel.cpp
        src/ui/PedalFace.cpp
)

target_include_directories(UISnapshot PRIVATE src)

target_compile_definitions(UISnapshot
    PRIVATE
        "JucePlugin_Name=\"Monarch of Tone\""
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(UISnapshot
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

# ---- Offline render tool (runs a WAV through the processor; for real-pedal A/B validation) ----
juce_add_console_app(PedalRender PRODUCT_NAME "PedalRender")

target_sources(PedalRender
    PRIVATE
        tools/PedalRender.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/ui/MonarchLookAndFeel.cpp
        src/ui/PedalFace.cpp
)

target_include_directories(PedalRender PRIVATE src)

target_compile_definitions(PedalRender
    PRIVATE
        "JucePlugin_Name=\"Monarch of Tone\""
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(PedalRender
    PRIVATE
        xsimd::xsimd
        chowdsp::chowdsp_wdf
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
)
