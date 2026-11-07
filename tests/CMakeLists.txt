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
    panel panel-opens
)

hadronyche_add_host_tests(RabidPhattTests RabidPhatt ${RABIDPHATT_TESTS})

# Plays the same things through the VST3 and the LV2, and compares them bit for bit.
if(LINUX)
    add_executable(RabidPhattLv2Compare
        lv2/Lv2Compare.cpp
        "${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp"
        "${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp"
    )
    target_include_directories(RabidPhattLv2Compare PRIVATE "${VST3_SDK_DIR}")
    target_link_libraries(RabidPhattLv2Compare PRIVATE ${CMAKE_DL_LIBS})
    set_target_properties(RabidPhattLv2Compare PROPERTIES ENABLE_EXPORTS ON)
    add_dependencies(RabidPhattLv2Compare RabidPhatt RabidPhatt-lv2)
    add_test(NAME lv2-matches-vst3 COMMAND RabidPhattLv2Compare "$<TARGET_FILE:RabidPhatt>" "$<TARGET_FILE:RabidPhatt-lv2>")
    set_tests_properties(lv2-matches-vst3 PROPERTIES TIMEOUT 600)

    # A bundle right under /tmp is as shallow as a path can get. VSTGUI crashed on those: see enterModule in the wrapper.
    add_test(NAME lv2-shallow-path COMMAND sh -c
        "d=$(mktemp -d /tmp/rp-lv2-XXXXXX) && cp \"$2\" \"$d/\" && \"$0\" \"$1\" \"$d/RabidPhatt.so\" note-edges; s=$?; rm -rf \"$d\"; exit $s"
        "$<TARGET_FILE:RabidPhattLv2Compare>" "$<TARGET_FILE:RabidPhatt>" "$<TARGET_FILE:RabidPhatt-lv2>")
endif()
