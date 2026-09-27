# thifo_add_plugin — one call that turns a folder of sources into a thf plug-in.
#
#   thifo_add_plugin(<target>
#       PRODUCT_NAME "thf Something"
#       PLUGIN_CODE  Abcd                 # 4 chars, unique within the series
#       BUNDLE_ID    com.thf.something
#       [VERSION     0.1.0]
#       [IS_SYNTH    TRUE]                # instrument: no audio input, AU MusicDevice,
#                                         # VST3 "Instrument|Synth". Default: effect.
#       SOURCES      ...
#       [RESOURCES   ...])                # extra files for BinaryData (translations etc.)
#
# Every plug-in gets the shared sources (shared/ui, shared/i18n, shared/midi), the Golos Text
# fonts in BinaryData, ad-hoc signing on macOS and (optionally) installation into
# ~/Library/Audio/Plug-Ins. Effects keep the historical defaults: MIDI input on,
# AU type kAudioUnitType_MusicEffect.

set(THIFO_SHARED_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")

set(THIFO_SHARED_SOURCES
    "${THIFO_SHARED_DIR}/ui/ThifoLookAndFeel.cpp"
    "${THIFO_SHARED_DIR}/i18n/Translator.cpp"
    CACHE INTERNAL "")

set(THIFO_SHARED_FONTS
    "${THIFO_SHARED_DIR}/fonts/GolosText-Regular.ttf"
    "${THIFO_SHARED_DIR}/fonts/GolosText-SemiBold.ttf"
    CACHE INTERNAL "")

function(thifo_add_plugin target)
    set(options)
    set(oneValue PRODUCT_NAME PLUGIN_CODE BUNDLE_ID VERSION IS_SYNTH)
    set(multiValue SOURCES RESOURCES)
    cmake_parse_arguments(ARG "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    if(NOT ARG_VERSION)
        set(ARG_VERSION "${PROJECT_VERSION}")
    endif()

    if(ARG_IS_SYNTH)
        set(kind_args
            IS_SYNTH TRUE
            NEEDS_MIDI_INPUT TRUE
            AU_MAIN_TYPE kAudioUnitType_MusicDevice
            VST3_CATEGORIES Instrument Synth)
    else()
        set(kind_args
            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT TRUE
            AU_MAIN_TYPE kAudioUnitType_MusicEffect)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME "thf"
        PLUGIN_MANUFACTURER_CODE Thfo
        PLUGIN_CODE ${ARG_PLUGIN_CODE}
        PRODUCT_NAME "${ARG_PRODUCT_NAME}"
        BUNDLE_ID ${ARG_BUNDLE_ID}
        VERSION ${ARG_VERSION}
        FORMATS VST3 AU Standalone
        ${kind_args}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE ${ARG_SOURCES} ${THIFO_SHARED_SOURCES})
    target_include_directories(${target} PRIVATE "${THIFO_SHARED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/Source")

    juce_add_binary_data(${target}_Data
        NAMESPACE BinaryData
        SOURCES ${THIFO_SHARED_FONTS} ${ARG_RESOURCES})

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_FLAC=1)

    target_link_libraries(${target}
        PRIVATE
            ${target}_Data
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # macOS: ad-hoc signature, font licence inside each bundle, then install.
    if(APPLE)
        foreach(format VST3 AU)
            set(fmt_target ${target}_${format})
            if(NOT TARGET ${fmt_target})
                continue()
            endif()
            add_custom_command(TARGET ${fmt_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${fmt_target}>/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy "${THIFO_SHARED_DIR}/fonts/OFL-GolosText.txt"
                        "$<TARGET_BUNDLE_CONTENT_DIR:${fmt_target}>/Resources/OFL-GolosText.txt"
                COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${fmt_target}>"
                VERBATIM)
            if(THIFO_INSTALL_PLUGINS)
                if(format STREQUAL "VST3")
                    set(dest "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
                else()
                    set(dest "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
                endif()
                add_custom_command(TARGET ${fmt_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E rm -rf "${dest}/$<TARGET_BUNDLE_DIR_NAME:${fmt_target}>"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${fmt_target}>"
                            "${dest}/$<TARGET_BUNDLE_DIR_NAME:${fmt_target}>"
                    VERBATIM)
            endif()
        endforeach()
    endif()
endfunction()
