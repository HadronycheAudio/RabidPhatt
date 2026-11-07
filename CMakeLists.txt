cmake_minimum_required(VERSION 3.25)

# The oldest macOS it runs on. This has to come before project (), or it defaults to the build machine's own.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "The oldest macOS supported")

project(RabidPhatt VERSION 1.3.0 LANGUAGES CXX)

# Can point at another checkout, to work on the library and a plug-in together.
set(HADRONYCHE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/hadronychecore" CACHE PATH "The HadronycheCore library")

if(NOT EXISTS "${HADRONYCHE_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "HadronycheCore submodule is missing. Run: git submodule update --init --recursive")
endif()

include("${HADRONYCHE_CORE_DIR}/cmake/HadronychePlugin.cmake")

hadronyche_use_vst3_sdk()

# --- Plug-in ------------------------------------------------------------------
hadronyche_add_plugin(RabidPhatt AU_TYPE aumu AU_SUBTYPE RbPh SOURCES
    src/PluginConfig.h
    src/PluginEntry.cpp
    src/Parameters.h
    src/Parameters.cpp
    src/Processor.h
    src/Processor.cpp
    src/Controller.h
    src/Controller.cpp
    src/Editor.h
    src/Editor.cpp
    src/dsp/Adsr.h
    src/dsp/Lfo.h
    src/dsp/LowPassFilter.h
    src/dsp/NoteStack.h
    src/dsp/OscillatorBank.h
)

# --- LV2 ----------------------------------------------------------------------
# The same sources as the VST3, plus an LV2 entry point. Only on Linux, so nothing changes elsewhere.
if(LINUX)
    set(RABIDPHATT_LV2_BUNDLE "${CMAKE_BINARY_DIR}/LV2/RabidPhatt.lv2")

    add_library(RabidPhatt-lv2 MODULE
        src/lv2/Lv2Entry.cpp
        src/PluginEntry.cpp
        src/Parameters.cpp
        src/Processor.cpp
        src/Controller.cpp
        src/Editor.cpp
    )
    smtg_target_add_library_main(RabidPhatt-lv2)
    target_include_directories(RabidPhatt-lv2 PRIVATE src)
    target_compile_definitions(RabidPhatt-lv2 PRIVATE RABIDPHATT_VERSION="${PROJECT_VERSION}")
    target_link_libraries(RabidPhatt-lv2 PRIVATE sdk vstgui_support hadronyche_dsp hadronyche_vst hadronyche_gui ${CMAKE_DL_LIBS})
    target_link_options(RabidPhatt-lv2 PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/lv2/lv2-exports.map" "$<$<CONFIG:Release>:-s>")
    set_target_properties(RabidPhatt-lv2 PROPERTIES
        OUTPUT_NAME RabidPhatt
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${RABIDPHATT_LV2_BUNDLE}"
    )
    configure_file(src/lv2/manifest.ttl "${RABIDPHATT_LV2_BUNDLE}/manifest.ttl" COPYONLY)
    configure_file(src/lv2/RabidPhatt.ttl "${RABIDPHATT_LV2_BUNDLE}/RabidPhatt.ttl" COPYONLY)
endif()

# --- Tests --------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
