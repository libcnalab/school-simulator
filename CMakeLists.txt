# =============================================================================
# school-simulator -- a large walkable 3D school, built on the CNA framework.
# =============================================================================
# Two halves:
#   school_sim   pure C++ (no CNA): the school plan, navigation, schedule, NPC simulation.
#                Built and unit-tested without a window, a graphics device or CNA.
#   school_app   the CNA application: rendering, input, audio, HUD, interaction.
# CNA is consumed like museum-simulator does: a sibling checkout added with add_subdirectory().
cmake_minimum_required(VERSION 3.23)

project(SchoolSimulator
    VERSION 0.1.0
    DESCRIPTION "A living 3D school simulation built on CNA (C++ XNA 4.0)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SCHOOL_BUILD_APP "Build the CNA application (needs the CNA checkout)" ON)

# --- the simulation core (no CNA) ---------------------------------------------------------------
file(GLOB_RECURSE SCHOOL_SIM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/World/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Rooms/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NPC/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Navigation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Simulation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Testing/*.cpp")

add_library(school_sim STATIC ${SCHOOL_SIM_SOURCES})
target_include_directories(school_sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(school_sim PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
target_compile_definitions(school_sim PUBLIC
    SCHOOL_VERSION="${PROJECT_VERSION}"
    SCHOOL_SOURCE_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Content")

# Headless command line: validation, one simulated day, soak runs, benchmarks.
add_executable(school-sim-cli src/Testing/cli/SimCli.cpp)
target_link_libraries(school-sim-cli PRIVATE school_sim)

include(CTest)
add_executable(school_tests tests/school_tests.cpp)
target_link_libraries(school_tests PRIVATE school_sim)
target_compile_options(school_tests PRIVATE -Wall -Wextra)
add_test(NAME school_logic COMMAND school_tests)
add_test(NAME school_day COMMAND school-sim-cli --day --students 500 --check)
add_test(NAME school_validate COMMAND school-sim-cli --validate)
add_test(NAME school_day_extreme COMMAND school-sim-cli --day --preset extreme --check)

# --- the CNA application -------------------------------------------------------------------------
if(SCHOOL_BUILD_APP)
    set(CNA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cna" CACHE PATH "Path to the CNA framework checkout")
    if(NOT EXISTS "${CNA_ROOT_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "school-simulator: CNA not found at '${CNA_ROOT_DIR}'.\n"
            "Clone it next to this project or pass -DCNA_ROOT_DIR=/path/to/cna (or -DSCHOOL_BUILD_APP=OFF).")
    endif()

    # The renderer is CNA's own cache variable; nothing here is renderer-specific.
    set(CNA_BUILD_TESTS    OFF CACHE BOOL "Build CNA's own tests")
    set(CNA_BUILD_EXAMPLES OFF CACHE BOOL "Build CNA's example applications")
    set(CNA_BUILD_C_API    OFF CACHE BOOL "Build the CNA C API")
    set(CNA_ENABLE_NET     OFF CACHE BOOL "Build CNA networking")
    set(CNA_ENABLE_GAMER_SERVICES OFF CACHE BOOL "Build CNA GamerServices")
    set(CNA_ENABLE_VIDEO   "OFF" CACHE STRING "Enable FFmpeg video playback")
    set(CNA_CNAEXT      ON    CACHE BOOL   "Build CNA's graphics extensions")
    set(CNA_DEVICES     OFF   CACHE BOOL   "Build CNA's device extensions")
    set(CNA_DIAGNOSTICS STATS CACHE STRING "CNA diagnostics level (OFF|STATS|FULL)")
    set(EASYGL_BUILD_TESTS    OFF CACHE BOOL "Build easy-gl tests")
    set(EASYGL_BUILD_EXAMPLES OFF CACHE BOOL "Build easy-gl examples")
    set(METAGL_BUILD_TESTS    OFF CACHE BOOL "Build meta-gl tests")

    # CNA caches its vendored SDL build in its checkout by default; keep it in this build tree instead.
    if(NOT DEFINED CNA_SDL_PREBUILT_ROOT)
        set(CNA_SDL_PREBUILT_ROOT
            "${CMAKE_BINARY_DIR}/cna-sdl-prebuilt-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_COMPILER_ID}"
            CACHE PATH "Persistent CNA SDL build cache, kept outside the CNA checkout")
    endif()

    include("${CNA_ROOT_DIR}/cmake/SharpRuntimeConsumption.cmake" OPTIONAL)
    if(DEFINED CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS AND NOT DEFINED SHARP_RUNTIME_COMPONENTS)
        set(SHARP_RUNTIME_COMPONENTS "${CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS}"
            CACHE STRING "Sharp Runtime components required by CNA")
    endif()

    add_subdirectory("${CNA_ROOT_DIR}" cna EXCLUDE_FROM_ALL)

    file(GLOB_RECURSE SCHOOL_APP_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Scene/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Interaction/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Audio/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/UI/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.cpp")

    add_library(school_app STATIC ${SCHOOL_APP_SOURCES})
    target_include_directories(school_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(school_app PUBLIC school_sim CNA)

    # CNA's and sharp-runtime's public headers are not warning-clean under -Wextra/-Wpedantic,
    # so their include directories are passed as -isystem.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(school_app PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
            -Wno-sign-conversion -Wno-overloaded-virtual -Wno-missing-field-initializers)
        file(GLOB _school_system_includes
            "${CNA_ROOT_DIR}/modules/*/include"
            "${CNA_ROOT_DIR}/modules/renderers/*/include"
            "${CNA_ROOT_DIR}/../sharp-runtime/modules/*/include")
        foreach(_dir IN LISTS _school_system_includes)
            target_compile_options(school_app PRIVATE "SHELL:-isystem ${_dir}")
        endforeach()
    endif()

    add_executable(cna-school-simulator src/Main.cpp)
    target_link_libraries(cna-school-simulator PRIVATE school_app)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna-school-simulator PRIVATE SDL3::SDL3main)
    endif()
    if(COMMAND cna_copy_renderer_runtime)
        cna_copy_renderer_runtime(cna-school-simulator)
    endif()

    # Content/ next to the executable keeps the build tree self-contained.
    add_custom_command(TARGET cna-school-simulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Content" "$<TARGET_FILE_DIR:cna-school-simulator>/Content"
        COMMENT "Copying Content next to cna-school-simulator")
    if(WIN32 AND COMMAND cna_copy_sdl_runtime)
        cna_copy_sdl_runtime(cna-school-simulator)
    endif()
endif()
