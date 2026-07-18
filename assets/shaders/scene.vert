#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_colour;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 vertex_colour;
layout(location = 1) out vec2 vertex_uv;

layout(push_constant) uniform ScenePushConstants {
    vec2 position_scale;
    vec2 position_offset;
    vec4 effect_colour;
    vec4 effect_params;
    vec2 effect_size;
    uint effect_mode;
    uint use_texture;
} scene;

void main()
{
    vec2 clip_position = in_position * scene.position_scale + scene.position_offset;
    // SceneComposer retains the WebGL/OpenGL clip-space convention so both
    // backends consume an identical packet. Vulkan's positive-height viewport
    // maps NDC Y in the opposite framebuffer direction, while RmlUi already
    // emits top-left coordinates explicitly. Flip only the scene packet here.
    gl_Position = vec4(clip_position.x, -clip_position.y, 0.0, 1.0);
    vertex_colour = in_colour;
    vertex_uv = in_uv;
}
