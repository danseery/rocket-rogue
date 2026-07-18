#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_colour;
layout(location = 2) in vec2 in_tex_coord;

layout(location = 0) out vec4 frag_colour;
layout(location = 1) out vec2 frag_tex_coord;

layout(push_constant) uniform RmlPushConstants {
    vec2 scale;
    vec2 offset;
    vec2 translation;
    uint has_texture;
} push_constants;

void main()
{
    vec2 position = in_position + push_constants.translation;
    gl_Position = vec4(
        position * push_constants.scale + push_constants.offset,
        0.0,
        1.0);
    frag_colour = in_colour;
    frag_tex_coord = in_tex_coord;
}
