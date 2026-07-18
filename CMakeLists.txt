cmake_minimum_required(VERSION 3.22)

project(RocketRoguePoc
    VERSION 0.1.0
    DESCRIPTION "A C++20/Vulkan and WebGL2 rocket-launch roguelite"
    LANGUAGES C CXX)

option(ROCKET_PROFILE_BUILD "Keep optimized native builds profiler-friendly" OFF)
set(ROCKET_STEAMWORKS_SDK_ROOT "" CACHE PATH "Optional Steamworks SDK root for Deck runtime detection")

if(MSVC)
    add_compile_options(/EHsc)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG>)
else()
    add_compile_options($<$<CONFIG:Release>:-g>)
endif()

if(ROCKET_PROFILE_BUILD)
    if(EMSCRIPTEN)
        message(FATAL_ERROR "ROCKET_PROFILE_BUILD is intended for native Windows/Linux builds.")
    elseif(MSVC)
        add_compile_options(/Oy-)
        add_link_options(/DEBUG)
    else()
        add_compile_options(-fno-omit-frame-pointer)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

add_library(rocket_core
    src/core/Content.cpp
    src/core/GameState.cpp
    src/core/LaunchSimulation.cpp
    src/core/MiniDroneCoordination.cpp
    src/core/MiningProgression.cpp
    src/core/MiningSystem.cpp
    src/core/Random.cpp
    src/core/ResearchSystem.cpp
    src/core/SaveData.cpp
    src/input/ControllerInput.cpp)
target_include_directories(rocket_core PUBLIC src)

if(MSVC)
    target_compile_options(rocket_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rocket_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)

if(EMSCRIPTEN)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype INTERFACE IMPORTED)
        target_compile_options(Freetype::Freetype INTERFACE -sUSE_FREETYPE=1)
        target_link_options(Freetype::Freetype INTERFACE -sUSE_FREETYPE=1)
    endif()
else()
    set(ROCKET_FREETYPE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/freetype" CACHE PATH "Optional local FreeType 2.13.3 source tree")
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    if(EXISTS "${ROCKET_FREETYPE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${ROCKET_FREETYPE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/freetype" EXCLUDE_FROM_ALL)
        set(ROCKET_FREETYPE_LICENSE "${ROCKET_FREETYPE_SOURCE_DIR}/docs/FTL.TXT")
    else()
        FetchContent_Declare(freetype_source
            GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
            GIT_TAG VER-2-13-3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(freetype_source)
        set(ROCKET_FREETYPE_LICENSE "${freetype_source_SOURCE_DIR}/docs/FTL.TXT")
    endif()
    if(TARGET freetype AND NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
endif()

set(ROCKET_RMLUI_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/RmlUi" CACHE PATH "Path to the pinned RmlUi source tree")
if(EXISTS "${ROCKET_RMLUI_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${ROCKET_RMLUI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rmlui" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(rmlui_source
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG 2cd28864ae25ed345b70598751703a5433b12356)
    FetchContent_MakeAvailable(rmlui_source)
    set(ROCKET_RMLUI_SOURCE_DIR "${rmlui_source_SOURCE_DIR}")
endif()

add_library(rocket_app
    src/game/GamePanel.cpp
    src/game/GameRmlUi.cpp
    src/game/GameRunner.cpp
    src/game/RocketGameApp.cpp
    src/performance/PerformanceMetrics.cpp
    src/render/FrameCapture.cpp
    src/render/SceneComposer.cpp)
target_link_libraries(rocket_app PUBLIC rocket_core PRIVATE rmlui_core)
target_include_directories(rocket_app PUBLIC
    src
    "${ROCKET_RMLUI_SOURCE_DIR}/Backends"
    PRIVATE "${ROCKET_RMLUI_SOURCE_DIR}/Source/Debugger")
target_compile_definitions(rocket_app PRIVATE RMLUI_STATIC_LIB)
if(EMSCRIPTEN)
    target_sources(rocket_app PRIVATE
        src/render/GlRmlRenderHost.cpp
        src/render/OpenGlApi.cpp
        src/render/OpenGlRenderer.cpp)
    target_compile_definitions(rocket_app PUBLIC ROCKET_OPENGL_ES)
    target_compile_options(rocket_app PRIVATE -sUSE_FREETYPE=1)
endif()

add_executable(rocket_core_tests tests/core_tests.cpp)
target_link_libraries(rocket_core_tests PRIVATE rocket_core)
target_sources(rocket_core_tests PRIVATE src/game/GamePanel.cpp)
set_target_properties(rocket_core_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_mining_progression_tests tests/mining_progression_tests.cpp)
target_link_libraries(rocket_mining_progression_tests PRIVATE rocket_core)
set_target_properties(rocket_mining_progression_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_mining_economy_tests tests/mining_economy_tests.cpp)
target_link_libraries(rocket_mining_economy_tests PRIVATE rocket_core)
set_target_properties(rocket_mining_economy_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_controller_input_tests tests/controller_input_tests.cpp)
target_link_libraries(rocket_controller_input_tests PRIVATE rocket_core)
set_target_properties(rocket_controller_input_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_app_services_tests tests/app_services_tests.cpp)
target_link_libraries(rocket_app_services_tests PRIVATE rocket_app)
set_target_properties(rocket_app_services_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_performance_metrics_tests tests/performance_metrics_tests.cpp)
target_link_libraries(rocket_performance_metrics_tests PRIVATE rocket_app)
set_target_properties(rocket_performance_metrics_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_frame_capture_tests tests/frame_capture_tests.cpp)
target_link_libraries(rocket_frame_capture_tests PRIVATE rocket_app)
set_target_properties(rocket_frame_capture_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(rocket_scene_atlas_tests tests/scene_atlas_tests.cpp)
target_include_directories(rocket_scene_atlas_tests PRIVATE src)
set_target_properties(rocket_scene_atlas_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")

set(ROCKET_TEST_TARGETS
    rocket_core_tests
    rocket_mining_progression_tests
    rocket_mining_economy_tests
    rocket_controller_input_tests
    rocket_app_services_tests
    rocket_performance_metrics_tests
    rocket_frame_capture_tests
    rocket_scene_atlas_tests)
foreach(test_target IN LISTS ROCKET_TEST_TARGETS)
    # The lightweight tests use assert() for both execution and verification. Keep
    # those checks live in Release/RelWithDebInfo so optimized test runs exercise
    # the same code paths instead of compiling the test bodies away.
    if(MSVC)
        target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
endforeach()

enable_testing()
if(EMSCRIPTEN)
    foreach(test_target IN LISTS ROCKET_TEST_TARGETS)
        target_link_options(${test_target} PRIVATE -sSTACK_SIZE=2097152 -sALLOW_MEMORY_GROWTH=1)
    endforeach()
    if(DEFINED ENV{EMSDK_NODE})
        set(ROCKET_TEST_NODE "$ENV{EMSDK_NODE}")
    else()
        find_program(ROCKET_TEST_NODE NAMES node node.exe REQUIRED)
    endif()
    add_test(NAME rocket_core_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_core_tests>")
    add_test(NAME rocket_mining_progression_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_mining_progression_tests>")
    add_test(NAME rocket_mining_economy_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_mining_economy_tests>")
    add_test(NAME rocket_controller_input_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_controller_input_tests>")
    add_test(NAME rocket_app_services_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_app_services_tests>")
    add_test(NAME rocket_performance_metrics_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_performance_metrics_tests>")
    add_test(NAME rocket_frame_capture_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_frame_capture_tests>")
    add_test(NAME rocket_scene_atlas_tests COMMAND "${ROCKET_TEST_NODE}" "$<TARGET_FILE:rocket_scene_atlas_tests>")
else()
    add_test(NAME rocket_core_tests COMMAND rocket_core_tests)
    add_test(NAME rocket_mining_progression_tests COMMAND rocket_mining_progression_tests)
    add_test(NAME rocket_mining_economy_tests COMMAND rocket_mining_economy_tests)
    add_test(NAME rocket_controller_input_tests COMMAND rocket_controller_input_tests)
    add_test(NAME rocket_app_services_tests COMMAND rocket_app_services_tests)
    add_test(NAME rocket_performance_metrics_tests COMMAND rocket_performance_metrics_tests)
    add_test(NAME rocket_frame_capture_tests COMMAND rocket_frame_capture_tests)
    add_test(NAME rocket_scene_atlas_tests COMMAND rocket_scene_atlas_tests)
endif()

find_program(ROCKET_NODE_EXECUTABLE NAMES node node.exe)
if(ROCKET_NODE_EXECUTABLE)
    add_test(NAME rocket_platform_boundary_tests
        COMMAND "${ROCKET_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check-platform-boundaries.mjs")
endif()

if(EMSCRIPTEN)
    add_library(rocket_platform_web
        src/platform/web/WebGamepadSource.cpp
        src/platform/web/WebPlatform.cpp
        src/platform/web/WebSaveStore.cpp)
    target_link_libraries(rocket_platform_web PUBLIC rocket_core)
    target_include_directories(rocket_platform_web PUBLIC src)
    target_compile_definitions(rocket_platform_web PUBLIC ROCKET_OPENGL_ES)

    add_executable(rocket_rogue src/platform/web/WebMain.cpp)
    target_link_libraries(rocket_rogue PRIVATE rocket_app rocket_platform_web rmlui_core)
    target_compile_options(rocket_rogue PRIVATE -sUSE_FREETYPE=1)

    add_custom_target(copy_rocket_assets
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/scene-atlas"
            "${CMAKE_CURRENT_BINARY_DIR}/assets/scene-atlas"
        COMMENT "Copying Rocket Rogue web assets")
    add_dependencies(rocket_rogue copy_rocket_assets)

    set(ROCKET_EXPORTED_FUNCTIONS
        "['_main','_rr_new_game','_rr_continue_game','_rr_prepare_launch','_rr_current_screen','_rr_start_launch','_rr_eject_now','_rr_return_home','_rr_arrival_ops','_rr_skip_arrival_fanfare','_rr_cut_engines','_rr_pressure_relief','_rr_close_relief_valve','_rr_jettison_cargo','_rr_next','_rr_attempt_frontier','_rr_open_navigation','_rr_ark_jump','_rr_select_navigation','_rr_buy_offer','_rr_reroll_offers','_rr_arrival_flyby','_rr_flyby_move','_rr_flyby_abort','_rr_flyby_continue','_rr_arrival_orbit','_rr_orbit_move','_rr_orbit_abort','_rr_orbit_continue','_rr_arrival_landing','_rr_research_project','_rr_skip_research','_rr_survey_surface','_rr_mine_surface','_rr_push_surface','_rr_extract_surface','_rr_surface_scan_pulse','_rr_surface_scan_bank','_rr_surface_scan_abort','_rr_surface_push_step','_rr_surface_push_bank','_rr_surface_push_abort','_rr_surface_upgrade','_rr_drone_ops','_rr_surface_ops','_rr_equip_drone','_rr_unequip_drone_slot','_rr_upgrade_drone','_rr_upgrade_drone_slot','_rr_mining_move','_rr_mining_aim','_rr_mining_drill','_rr_mining_scanner','_rr_mining_stow','_rr_mining_abort','_rr_mining_failure_ack','_rr_debug_mining','_rr_debug_combat_mining','_rr_debug_mining_arena','_rr_debug_mining_preview','_rr_debug_surface_scan','_rr_debug_surface_push','_rr_debug_flyby','_rr_debug_orbit','_rr_debug_act_one_start','_rr_debug_act_one_previous','_rr_debug_act_one_next','_rr_debug_act_one_checkpoint','_rr_debug_exit','_rr_repair_ship','_rr_recruit_crew','_rr_recruit_candidate','_rr_train_crew','_rr_rest_crew','_rr_reset_save','_rr_rml_mouse_move','_rr_rml_mouse_down','_rr_rml_mouse_up','_rr_rml_wheel','_rr_rml_hit_test','_rr_controller_debug_status','_rr_controller_app_debug_status','_rr_debug_controller_set','_rr_debug_controller_clear','_rr_controller_haptic']")
    set_target_properties(rocket_rogue PROPERTIES
        OUTPUT_NAME "rocket_rogue"
        SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html"
        LINK_FLAGS "--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sUSE_FREETYPE=1 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -sEXPORTED_FUNCTIONS=${ROCKET_EXPORTED_FUNCTIONS}")
else()
    if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Native Rocket Rogue builds currently support only Windows and Linux.")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ROCKET_NATIVE_PROCESSOR)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT ROCKET_NATIVE_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
        message(FATAL_ERROR
            "Native Rocket Rogue builds currently support only x86-64; "
            "CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}'.")
    endif()

    # Native rendering uses a pinned loader shim and headers. The Vulkan loader
    # itself is deliberately not linked or packaged; SDL loads the driver's
    # system/Steam Runtime loader and volk resolves functions through it.
    FetchContent_Declare(vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG 8864cdc896bbc2a9b6eb36b3218fc9ef57908d77
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(vulkan_headers)
    set(ROCKET_VULKAN_HEADERS_LICENSE "${vulkan_headers_SOURCE_DIR}/LICENSE.md")

    set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(volk_source
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG 3ca312a4f38baa63d8006b6905abbeeb89c8087d
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(volk_source)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    set(ROCKET_VOLK_LICENSE "${volk_source_SOURCE_DIR}/LICENSE.md")

    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(vma_source
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG 1d8f600fd424278486eade7ed3e877c99f0846b1
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(vma_source)
    set(ROCKET_VMA_LICENSE "${vma_source_SOURCE_DIR}/LICENSE.txt")

    target_sources(rocket_app PRIVATE
        src/performance/BenchmarkScenarioDriver.cpp
        src/render/VulkanRmlRenderHost.cpp
        src/render/vulkan/VmaImplementation.cpp
        src/render/vulkan/VulkanGraphicsBackend.cpp
        src/render/vulkan/VulkanPolicy.cpp)
    target_compile_definitions(rocket_app PRIVATE VK_NO_PROTOTYPES)
    target_link_libraries(rocket_app PUBLIC
        volk::volk
        Vulkan::Headers
        GPUOpen::VulkanMemoryAllocator)

    set(ROCKET_SDL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/SDL" CACHE PATH "Optional local SDL 3.4.10 source tree")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)
    if(EXISTS "${ROCKET_SDL_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${ROCKET_SDL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdl" EXCLUDE_FROM_ALL)
        set(ROCKET_SDL_LICENSE "${ROCKET_SDL_SOURCE_DIR}/LICENSE.txt")
        set(ROCKET_SDL_INCLUDE_DIR "${ROCKET_SDL_SOURCE_DIR}/include")
    else()
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.4.10
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL3)
        set(ROCKET_SDL_LICENSE "${SDL3_SOURCE_DIR}/LICENSE.txt")
        set(ROCKET_SDL_INCLUDE_DIR "${SDL3_SOURCE_DIR}/include")
    endif()
    target_include_directories(rocket_app PRIVATE "${ROCKET_SDL_INCLUDE_DIR}")

    add_library(rocket_platform_sdl
        src/performance/NativeBenchmarkOptions.cpp
        src/platform/FrameLimitPolicy.cpp
        src/platform/sdl/NativeStorage.cpp
        src/platform/sdl/NativeTextureSource.cpp
        src/platform/sdl/SdlPlatform.cpp
        src/platform/steam/SteamPlatformService.cpp
        third_party/lodepng/lodepng.cpp)
    target_include_directories(rocket_platform_sdl PUBLIC src third_party/lodepng "${ROCKET_RMLUI_SOURCE_DIR}/Backends")
    target_link_libraries(rocket_platform_sdl PUBLIC rocket_core SDL3::SDL3-static)
    target_compile_definitions(rocket_platform_sdl PRIVATE VK_NO_PROTOTYPES)
    target_link_libraries(rocket_platform_sdl PRIVATE Vulkan::Headers)

    set(ROCKET_STEAM_RUNTIME "")
    if(ROCKET_STEAMWORKS_SDK_ROOT)
        set(ROCKET_STEAMWORKS_INCLUDE "${ROCKET_STEAMWORKS_SDK_ROOT}/public")
        if(WIN32)
            set(ROCKET_STEAMWORKS_LIBRARY "${ROCKET_STEAMWORKS_SDK_ROOT}/redistributable_bin/win64/steam_api64.lib")
            set(ROCKET_STEAM_RUNTIME "${ROCKET_STEAMWORKS_SDK_ROOT}/redistributable_bin/win64/steam_api64.dll")
        else()
            set(ROCKET_STEAMWORKS_LIBRARY "${ROCKET_STEAMWORKS_SDK_ROOT}/redistributable_bin/linux64/libsteam_api.so")
            set(ROCKET_STEAM_RUNTIME "${ROCKET_STEAMWORKS_LIBRARY}")
        endif()
        if(NOT EXISTS "${ROCKET_STEAMWORKS_INCLUDE}/steam/steam_api.h"
            OR NOT EXISTS "${ROCKET_STEAMWORKS_LIBRARY}"
            OR NOT EXISTS "${ROCKET_STEAM_RUNTIME}")
            message(FATAL_ERROR "ROCKET_STEAMWORKS_SDK_ROOT does not contain the expected public headers and 64-bit redistributables")
        endif()
        target_compile_definitions(rocket_platform_sdl PUBLIC ROCKET_STEAMWORKS_ENABLED=1)
        target_include_directories(rocket_platform_sdl PRIVATE "${ROCKET_STEAMWORKS_INCLUDE}")
        target_link_libraries(rocket_platform_sdl PRIVATE "${ROCKET_STEAMWORKS_LIBRARY}")
    endif()

    add_executable(RocketRogue src/platform/sdl/NativeMain.cpp)
    target_link_libraries(RocketRogue PRIVATE rocket_app rocket_platform_sdl)
    if(ROCKET_STEAMWORKS_SDK_ROOT AND UNIX AND NOT APPLE)
        # The packaged optional Steamworks runtime lives beside the executable.
        # Keep that package relocatable instead of depending on the SDK path
        # from the build machine.
        set_target_properties(RocketRogue PROPERTIES
            BUILD_RPATH "$ORIGIN"
            BUILD_RPATH_USE_ORIGIN TRUE
            INSTALL_RPATH "$ORIGIN")
    endif()
    set_target_properties(RocketRogue PROPERTIES
        OUTPUT_NAME "RocketRogue"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    add_custom_target(copy_native_assets
        COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:RocketRogue>/assets"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RocketRogue>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/scene-atlas"
            "$<TARGET_FILE_DIR:RocketRogue>/assets/scene-atlas"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/shaders"
            "$<TARGET_FILE_DIR:RocketRogue>/assets/shaders"
        COMMENT "Copying native Rocket Rogue assets")
    add_dependencies(RocketRogue copy_native_assets)

    add_executable(rocket_native_platform_tests tests/native_platform_tests.cpp)
    target_link_libraries(rocket_native_platform_tests PRIVATE rocket_platform_sdl rocket_app)
    if(MSVC)
        target_compile_options(rocket_native_platform_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(rocket_native_platform_tests PRIVATE -UNDEBUG)
    endif()
    set_target_properties(rocket_native_platform_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME rocket_native_platform_tests COMMAND rocket_native_platform_tests)

    add_executable(rocket_native_benchmark_options_tests tests/native_benchmark_options_tests.cpp)
    target_link_libraries(rocket_native_benchmark_options_tests PRIVATE rocket_platform_sdl rocket_app)
    add_executable(rocket_frame_limit_policy_tests tests/frame_limit_policy_tests.cpp)
    target_link_libraries(rocket_frame_limit_policy_tests PRIVATE rocket_platform_sdl)
    foreach(test_target rocket_native_benchmark_options_tests rocket_frame_limit_policy_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
        set_target_properties(${test_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    endforeach()
    add_test(NAME rocket_native_benchmark_options_tests COMMAND rocket_native_benchmark_options_tests)
    add_test(NAME rocket_frame_limit_policy_tests COMMAND rocket_frame_limit_policy_tests)

    add_executable(rocket_scene_composer_tests tests/scene_composer_tests.cpp)
    target_link_libraries(rocket_scene_composer_tests PRIVATE rocket_app)
    if(MSVC)
        target_compile_options(rocket_scene_composer_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(rocket_scene_composer_tests PRIVATE -UNDEBUG)
    endif()
    set_target_properties(rocket_scene_composer_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    add_test(NAME rocket_scene_composer_tests COMMAND rocket_scene_composer_tests)

    add_executable(rocket_vulkan_policy_tests tests/vulkan_policy_tests.cpp)
    target_link_libraries(rocket_vulkan_policy_tests PRIVATE rocket_app)
    add_executable(rocket_benchmark_scenario_driver_tests tests/benchmark_scenario_driver_tests.cpp)
    target_link_libraries(rocket_benchmark_scenario_driver_tests PRIVATE rocket_app)
    foreach(test_target rocket_vulkan_policy_tests rocket_benchmark_scenario_driver_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
        set_target_properties(${test_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    endforeach()
    add_test(NAME rocket_vulkan_policy_tests COMMAND rocket_vulkan_policy_tests)
    add_test(NAME rocket_benchmark_scenario_driver_tests COMMAND rocket_benchmark_scenario_driver_tests)

    if(ROCKET_NODE_EXECUTABLE)
        add_test(NAME rocket_vulkan_shader_tool_tests
            COMMAND "${ROCKET_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build-vulkan-shaders.test.mjs")
        add_test(NAME rocket_native_benchmark_summary_tool_tests
            COMMAND "${ROCKET_NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/summarize-native-benchmarks.test.mjs")
    endif()

    if(WIN32)
        set(ROCKET_PACKAGE_NAME "RocketRogue-windows-x64.zip")
        set(ROCKET_PACKAGE_MODE "zip")
    else()
        set(ROCKET_PACKAGE_NAME "RocketRogue-linux-x64.tar.gz")
        set(ROCKET_PACKAGE_MODE "tgz")
    endif()
    add_custom_target(package-native
        COMMAND ${CMAKE_COMMAND}
            -DROCKET_EXECUTABLE=$<TARGET_FILE:RocketRogue>
            -DROCKET_ASSETS=${CMAKE_SOURCE_DIR}/assets
            -DROCKET_OUTPUT=${CMAKE_SOURCE_DIR}/build/packages/${ROCKET_PACKAGE_NAME}
            -DROCKET_STAGING=${CMAKE_CURRENT_BINARY_DIR}/package-staging
            -DROCKET_MODE=${ROCKET_PACKAGE_MODE}
            -DROCKET_PROJECT_LICENSE=${CMAKE_SOURCE_DIR}/licenses/RocketRogue.txt
            -DROCKET_RMLUI_LICENSE=${ROCKET_RMLUI_SOURCE_DIR}/LICENSE.txt
            -DROCKET_LODEPNG_LICENSE=${CMAKE_SOURCE_DIR}/third_party/lodepng/LICENSE.txt
            -DROCKET_SDL_LICENSE=${ROCKET_SDL_LICENSE}
            -DROCKET_FREETYPE_LICENSE=${ROCKET_FREETYPE_LICENSE}
            -DROCKET_VULKAN_HEADERS_LICENSE=${ROCKET_VULKAN_HEADERS_LICENSE}
            -DROCKET_VOLK_LICENSE=${ROCKET_VOLK_LICENSE}
            -DROCKET_VMA_LICENSE=${ROCKET_VMA_LICENSE}
            -DROCKET_STEAM_RUNTIME=${ROCKET_STEAM_RUNTIME}
            -P ${CMAKE_SOURCE_DIR}/cmake/PackageNative.cmake
        DEPENDS RocketRogue
        COMMENT "Creating ${ROCKET_PACKAGE_NAME}")
endif()
