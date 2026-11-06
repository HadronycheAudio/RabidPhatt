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
)

target_include_directories(RabidPhattTests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(RabidPhattTests PRIVATE hadronyche_testhost)
add_dependencies(RabidPhattTests RabidPhatt)

# It drives a real window with Windows' mouse messages.
if(WIN32)
    target_sources(RabidPhattTests PRIVATE PanelTest.cpp)
endif()

set(RABIDPHATT_TESTS
    class-ids parameter-ids parameter-ranges saved-format buses
    base-pitch motion-minimum banks-rows motion-spread motion-smooth lfo-modulates lfo-rate lfo-delay lfo-smooth lfo-sync-tempo lfo-sync-divisions lfo-sync-grid lfo-sync-free level-flat stereo-width dc-offset aliasing top-of-keyboard
    last-note-priority legato glide amp-envelope silence-flags sample-accurate
    filter-slope filter-envelope filter-tail volume levels determinism extreme-settings parameter-state parameter-text
    panel
)

hadronyche_add_host_tests(RabidPhattTests RabidPhatt ${RABIDPHATT_TESTS})
