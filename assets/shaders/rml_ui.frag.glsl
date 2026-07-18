#version 450

layout(location = 0) in vec4 frag_colour;
layout(location = 1) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_colour;

layout(set = 0, binding = 0) uniform sampler2D rml_texture;

layout(push_constant) uniform RmlPushConstants {
    vec2 scale;
    vec2 offset;
    vec2 translation;
    uint has_texture;
} push_constants;

void main()
{
    vec4 colour = frag_colour;
    if (push_constants.has_texture != 0) {
        colour *= texture(rml_texture, frag_tex_coord);
    }
    out_colour = colour;
}
