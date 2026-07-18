file(REMOVE_RECURSE "${ROCKET_STAGING}")
file(MAKE_DIRECTORY
    "${ROCKET_STAGING}/assets"
    "${ROCKET_STAGING}/assets/shaders"
    "${ROCKET_STAGING}/licenses")
file(COPY "${ROCKET_EXECUTABLE}" DESTINATION "${ROCKET_STAGING}")
if(NOT IS_DIRECTORY "${ROCKET_ASSETS}/scene-atlas")
    message(FATAL_ERROR "Required runtime asset directory is missing: scene-atlas")
endif()
file(COPY
    "${ROCKET_ASSETS}/scene-atlas"
    DESTINATION "${ROCKET_STAGING}/assets")
foreach(ROCKET_ATLAS_ASSET scene-atlas-0.png scene-atlas-1.png scene-atlas.json)
    if(NOT EXISTS "${ROCKET_STAGING}/assets/scene-atlas/${ROCKET_ATLAS_ASSET}")
        message(FATAL_ERROR "Required packaged scene atlas asset is missing: ${ROCKET_ATLAS_ASSET}")
    endif()
endforeach()

# Copy third-party notices directly to stable names. Several dependencies call
# their source notice LICENSE.txt, so copying and then renaming would silently
# overwrite the previous dependency's notice.
set(ROCKET_LICENSE_FILES
    "${ROCKET_PROJECT_LICENSE}|RocketRogue.txt"
    "${ROCKET_RMLUI_LICENSE}|RmlUi.txt"
    "${ROCKET_LODEPNG_LICENSE}|LodePNG.txt"
    "${ROCKET_SDL_LICENSE}|SDL3.txt"
    "${ROCKET_FREETYPE_LICENSE}|FreeType.txt"
    "${ROCKET_VULKAN_HEADERS_LICENSE}|Vulkan-Headers.txt"
    "${ROCKET_VOLK_LICENSE}|Volk.txt"
    "${ROCKET_VMA_LICENSE}|VulkanMemoryAllocator.txt")
foreach(ROCKET_LICENSE_ENTRY IN LISTS ROCKET_LICENSE_FILES)
    string(REPLACE "|" ";" ROCKET_LICENSE_PARTS "${ROCKET_LICENSE_ENTRY}")
    list(GET ROCKET_LICENSE_PARTS 0 ROCKET_LICENSE_SOURCE)
    list(GET ROCKET_LICENSE_PARTS 1 ROCKET_LICENSE_DESTINATION)
    if(NOT EXISTS "${ROCKET_LICENSE_SOURCE}")
        message(FATAL_ERROR "Required package license is missing: ${ROCKET_LICENSE_SOURCE}")
    endif()
    file(COPY_FILE
        "${ROCKET_LICENSE_SOURCE}"
        "${ROCKET_STAGING}/licenses/${ROCKET_LICENSE_DESTINATION}"
        ONLY_IF_DIFFERENT)
endforeach()

# Native shaders are compiled offline and shipped as SPIR-V assets. The Vulkan
# loader is intentionally supplied by the GPU driver or Steam Runtime and is
# never copied into the portable package.
foreach(ROCKET_SHADER
    scene.vert.spv
    scene.frag.spv
    scene_instance.vert.spv
    scene_instance.frag.spv
    rml_ui.vert.spv
    rml_ui.frag.spv)
    if(NOT EXISTS "${ROCKET_ASSETS}/shaders/${ROCKET_SHADER}")
        message(FATAL_ERROR "Required packaged SPIR-V shader is missing: ${ROCKET_SHADER}")
    endif()
    file(COPY_FILE
        "${ROCKET_ASSETS}/shaders/${ROCKET_SHADER}"
        "${ROCKET_STAGING}/assets/shaders/${ROCKET_SHADER}"
        ONLY_IF_DIFFERENT)
endforeach()
if(DEFINED ROCKET_STEAM_RUNTIME AND NOT ROCKET_STEAM_RUNTIME STREQUAL "")
    if(NOT EXISTS "${ROCKET_STEAM_RUNTIME}")
        message(FATAL_ERROR "Configured Steamworks runtime is missing: ${ROCKET_STEAM_RUNTIME}")
    endif()
    file(COPY "${ROCKET_STEAM_RUNTIME}" DESTINATION "${ROCKET_STAGING}")
endif()
get_filename_component(ROCKET_OUTPUT_DIRECTORY "${ROCKET_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${ROCKET_OUTPUT_DIRECTORY}")
file(REMOVE "${ROCKET_OUTPUT}")
if(ROCKET_MODE STREQUAL "zip")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ROCKET_OUTPUT}" --format=zip -- .
        WORKING_DIRECTORY "${ROCKET_STAGING}" COMMAND_ERROR_IS_FATAL ANY)
else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${ROCKET_OUTPUT}" -- .
        WORKING_DIRECTORY "${ROCKET_STAGING}" COMMAND_ERROR_IS_FATAL ANY)
endif()
