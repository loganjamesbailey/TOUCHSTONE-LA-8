juce_add_plugin(Touchstone
    PRODUCT_NAME              "Touchstone"
    COMPANY_NAME              "JamesBailey"
    BUNDLE_ID                 com.jamesbailey.touchstone
    PLUGIN_MANUFACTURER_CODE  Jbco
    PLUGIN_CODE               Tstn
    FORMATS                   AU VST3
    AU_MAIN_TYPE              kAudioUnitType_Effect
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    VST3_CATEGORIES           Fx Dynamics)

target_sources(Touchstone PRIVATE
    PluginProcessor.cpp
    Params.cpp)

target_include_directories(Touchstone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include)

target_compile_definitions(Touchstone PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Touchstone
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
