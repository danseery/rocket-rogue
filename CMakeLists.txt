cmake_minimum_required(VERSION 3.22)

project(RocketRoguePoc
    VERSION 0.1.0
    DESCRIPTION "A C++20/WebGL2 proof of concept for a rocket-launch roguelite"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rocket_core
    src/core/Content.cpp
    src/core/GameState.cpp
    src/core/LaunchSimulation.cpp
    src/core/Random.cpp
    src/core/SaveData.cpp)

target_include_directories(rocket_core PUBLIC src)

if(MSVC)
    target_compile_options(rocket_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rocket_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rocket_core_tests
    tests/core_tests.cpp)
target_link_libraries(rocket_core_tests PRIVATE rocket_core)
set_target_properties(rocket_core_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

enable_testing()
if(EMSCRIPTEN)
    if(DEFINED ENV{EMSDK_NODE})
        set(ROCKET_TEST_NODE "$ENV{EMSDK_NODE}")
    else()
        find_program(ROCKET_TEST_NODE NAMES node node.exe REQUIRED)
    endif()
    add_test(NAME rocket_core_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_core_tests>")
else()
    add_test(NAME rocket_core_tests COMMAND rocket_core_tests)
endif()

if(EMSCRIPTEN)
    add_executable(rocket_rogue
        src/main.cpp
        src/game/GamePanel.cpp
        src/game/RocketGameApp.cpp
        src/platform/WebSaveStore.cpp
        src/render/WebGLRenderer.cpp)

    add_custom_target(copy_rocket_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_CURRENT_BINARY_DIR}/assets
        COMMENT "Copying Rocket Rogue web assets")
    add_dependencies(rocket_rogue copy_rocket_assets)

    target_link_libraries(rocket_rogue PRIVATE rocket_core)
    target_include_directories(rocket_rogue PRIVATE src)

    set(ROCKET_EXPORTED_FUNCTIONS
        "['_main','_rr_start_launch','_rr_eject_now','_rr_return_home','_rr_cut_engines','_rr_pressure_relief','_rr_close_relief_valve','_rr_jettison_cargo','_rr_next','_rr_attempt_frontier','_rr_buy_offer','_rr_repair_ship','_rr_recruit_crew','_rr_train_crew','_rr_rest_crew','_rr_reset_save']")

    set_target_properties(rocket_rogue PROPERTIES
        OUTPUT_NAME "rocket_rogue"
        SUFFIX ".html"
        LINK_FLAGS "--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -sEXPORTED_FUNCTIONS=${ROCKET_EXPORTED_FUNCTIONS}")
endif()
