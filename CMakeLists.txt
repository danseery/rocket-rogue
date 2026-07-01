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
    src/core/MiningSystem.cpp
    src/core/Random.cpp
    src/core/ResearchSystem.cpp
    src/core/SaveData.cpp)

target_include_directories(rocket_core PUBLIC src)

if(MSVC)
    target_compile_options(rocket_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rocket_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rocket_core_tests
    src/game/GamePanel.cpp
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
    include(FetchContent)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)

    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype INTERFACE IMPORTED)
        target_compile_options(Freetype::Freetype INTERFACE -sUSE_FREETYPE=1)
        target_link_options(Freetype::Freetype INTERFACE -sUSE_FREETYPE=1)
    endif()

    set(ROCKET_RMLUI_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/RmlUi" CACHE PATH "Path to the RmlUi source tree")
    if(EXISTS "${ROCKET_RMLUI_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${ROCKET_RMLUI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rmlui")
    else()
        FetchContent_Declare(rmlui_source
            GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
            GIT_TAG 2cd28864ae25ed345b70598751703a5433b12356)
        FetchContent_MakeAvailable(rmlui_source)
        set(ROCKET_RMLUI_SOURCE_DIR "${rmlui_source_SOURCE_DIR}")
    endif()

    add_executable(rocket_rogue
        src/main.cpp
        src/game/GamePanel.cpp
        src/game/GameRmlUi.cpp
        src/game/RocketGameApp.cpp
        src/platform/WebSaveStore.cpp
        src/render/WebGLRenderer.cpp)

    add_custom_target(copy_rocket_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_CURRENT_BINARY_DIR}/assets
        COMMENT "Copying Rocket Rogue web assets")
    add_dependencies(rocket_rogue copy_rocket_assets)

    target_link_libraries(rocket_rogue PRIVATE rocket_core rmlui_core)
    target_include_directories(rocket_rogue PRIVATE
        src
        ${ROCKET_RMLUI_SOURCE_DIR}/Source/Debugger)
    target_compile_definitions(rocket_rogue PRIVATE RMLUI_STATIC_LIB)
    target_compile_options(rocket_rogue PRIVATE -sUSE_FREETYPE=1)

    set(ROCKET_EXPORTED_FUNCTIONS
        "['_main','_rr_prepare_launch','_rr_start_launch','_rr_eject_now','_rr_return_home','_rr_arrival_ops','_rr_skip_arrival_fanfare','_rr_cut_engines','_rr_pressure_relief','_rr_close_relief_valve','_rr_jettison_cargo','_rr_next','_rr_attempt_frontier','_rr_open_navigation','_rr_ark_jump','_rr_select_navigation','_rr_buy_offer','_rr_reroll_offers','_rr_arrival_flyby','_rr_flyby_move','_rr_flyby_abort','_rr_flyby_continue','_rr_arrival_orbit','_rr_orbit_move','_rr_orbit_abort','_rr_orbit_continue','_rr_arrival_landing','_rr_research_project','_rr_skip_research','_rr_survey_surface','_rr_mine_surface','_rr_push_surface','_rr_extract_surface','_rr_surface_upgrade','_rr_drone_ops','_rr_surface_ops','_rr_equip_drone','_rr_upgrade_drone_slot','_rr_mining_move','_rr_mining_aim','_rr_mining_drill','_rr_mining_scanner','_rr_mining_stow','_rr_mining_abort','_rr_mining_failure_ack','_rr_repair_ship','_rr_recruit_crew','_rr_recruit_candidate','_rr_train_crew','_rr_rest_crew','_rr_reset_save','_rr_rml_mouse_move','_rr_rml_mouse_down','_rr_rml_mouse_up','_rr_rml_wheel','_rr_rml_hit_test']")

    set_target_properties(rocket_rogue PROPERTIES
        OUTPUT_NAME "rocket_rogue"
        SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html"
        LINK_FLAGS "--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sUSE_FREETYPE=1 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -sEXPORTED_FUNCTIONS=${ROCKET_EXPORTED_FUNCTIONS}")
endif()
