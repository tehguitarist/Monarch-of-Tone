cmake_minimum_required(VERSION 3.15)

project(MonarchOfTone VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment target")

add_subdirectory(libs/JUCE)
add_subdirectory(libs/xsimd)
add_subdirectory(libs/chowdsp_wdf)

juce_add_plugin(Monarch
    COMPANY_NAME "Leigh Pierce"
    BUNDLE_ID "com.leighpierce.monarchoftone"
    PLUGIN_MANUFACTURER_CODE LeP1
    PLUGIN_CODE Mon1
    FORMATS AU
    PRODUCT_NAME "Monarch of Tone"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Monarch
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
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
