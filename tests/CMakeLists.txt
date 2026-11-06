# A test program that loads the built plug-in and drives it like a host. It doesn't link any of the
# plug-in's code: it only uses one of its headers, for the parameter IDs.

add_executable(RabidPhattTests
    TestMain.cpp
    Tests.h
    Harness.h
    Harness.cpp
    CompatibilityTests.cpp
    VoiceTests.cpp
    OscillatorTests.cpp
    PanelTest.cpp
    "${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp"
    "${VST3_SDK_DIR}/pluginterfaces/base/ustring.cpp"
    "${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp"
)

target_include_directories(RabidPhattTests PRIVATE "${VST3_SDK_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(RabidPhattTests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(RabidPhattTests PRIVATE hadronyche_params user32 gdi32)
add_dependencies(RabidPhattTests RabidPhatt)

# One CTest test per test in the program, so that a single one can be run with ctest -R.
set(RABIDPHATT_TESTS
    class-ids parameter-ids parameter-ranges saved-format buses
    base-pitch motion-minimum banks-rows motion-spread motion-smooth lfo-modulates lfo-rate lfo-delay lfo-smooth lfo-sync-tempo lfo-sync-divisions lfo-sync-grid lfo-sync-free level-flat stereo-width dc-offset aliasing top-of-keyboard
    last-note-priority legato glide amp-envelope silence-flags sample-accurate
    filter-slope filter-envelope filter-tail volume levels determinism extreme-settings parameter-state parameter-text
    panel
)

foreach(test_name ${RABIDPHATT_TESTS})
    add_test(NAME ${test_name} COMMAND RabidPhattTests "$<TARGET_FILE:RabidPhatt>" ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 300)
endforeach()
