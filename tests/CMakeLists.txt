# Regression tests. Configure with -DTHIFO_BUILD_TESTS=ON (build folder: build-tests), run ctest.

function(thifo_add_test name)
    add_executable(${name} ${ARGN} TestMain.cpp)
    # The plug-in's shared code is a static library; the test compiles the same JUCE modules
    # itself (so their headers are visible) and the linker takes plug-in objects from the library.
    target_link_libraries(${name} PRIVATE thfGrain thfGrain_Data
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
    target_include_directories(${name} PRIVATE
        "${CMAKE_SOURCE_DIR}/shared"
        "${CMAKE_SOURCE_DIR}/plugins/granular/Source")
    # Tests pump the message loop themselves (editors post async updates).
    # Assertions go through juce::Logger so TestMain can count them.
    target_compile_definitions(${name} PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_LOG_ASSERTIONS=1)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "JUCE Assertion failure")
endfunction()

thifo_add_test(GrainTest GrainTest.cpp)
thifo_add_test(GrainLayoutTest GrainLayoutTest.cpp)
