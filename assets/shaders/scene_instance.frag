#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_texture;

layout(location = 0) in vec4 vertex_colour;
layout(location = 1) in vec2 vertex_uv;
layout(location = 2) in vec2 unit_position;
layout(location = 3) flat in uint instance_shape;
layout(location = 4) flat in uint polygon_segments;
layout(location = 0) out vec4 out_colour;

layout(push_constant) uniform ScenePushConstants {
    vec2 position_scale;
    vec2 position_offset;
    vec4 effect_colour;
    vec4 effect_params;
    vec2 effect_size;
    uint effect_mode;
    uint use_texture;
} scene;

const float pi = 3.14159265358979323846;

void main()
{
    uint shape = instance_shape & 0x7fu;
    bool textured = (instance_shape & 0x80u) != 0u;
    vec4 colour = vertex_colour;
    if (textured) {
        colour = texture(scene_texture, vertex_uv) * vertex_colour;
    }

    // Match the former regular-polygon triangle fan exactly: the ray from the
    // center meets the sector chord at polygon_radius, and the triangle fan's
    // center alpha is linear in distance / polygon_radius.
    if (shape != 0u) {
        float segments = max(float(polygon_segments), 3.0);
        float sector = 2.0 * pi / segments;
        float centered_angle = mod(atan(unit_position.y, unit_position.x), sector)
            - sector * 0.5;
        float polygon_radius = cos(sector * 0.5) / cos(centered_angle);
        float normalized_radius = length(unit_position) / polygon_radius;
        // Alpha-zero coverage is equivalent to discard for this ordered,
        // alpha-blended scene pass (there is no depth or stencil attachment),
        // and avoids requiring the optional SPIR-V demote capability.
        colour.a *= step(normalized_radius, 1.0);
        if (shape == 2u) {
            colour.a *= max(0.0, 1.0 - normalized_radius);
        }
    }
    out_colour = colour;
}
