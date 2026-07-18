#version 450

layout(location = 0) in vec2 in_center;
layout(location = 1) in vec2 in_axis_x;
layout(location = 2) in vec2 in_axis_y;
layout(location = 3) in vec4 in_colour;
layout(location = 4) in vec2 in_uv_min;
layout(location = 5) in vec2 in_uv_max;
layout(location = 6) in uvec2 in_shape;

layout(location = 0) out vec4 vertex_colour;
layout(location = 1) out vec2 vertex_uv;
layout(location = 2) out vec2 unit_position;
layout(location = 3) flat out uint instance_shape;
layout(location = 4) flat out uint polygon_segments;

layout(push_constant) uniform ScenePushConstants {
    vec2 position_scale;
    vec2 position_offset;
    vec4 effect_colour;
    vec4 effect_params;
    vec2 effect_size;
    uint effect_mode;
    uint use_texture;
} scene;

const vec2 unit_quad[6] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    vec2 local = unit_quad[gl_VertexIndex];
    vec2 position = in_center + in_axis_x * local.x + in_axis_y * local.y;
    vec2 clip_position = position * scene.position_scale + scene.position_offset;
    gl_Position = vec4(clip_position.x, -clip_position.y, 0.0, 1.0);
    vertex_colour = in_colour;
    vertex_uv = vec2(
        mix(in_uv_min.x, in_uv_max.x, local.x * 0.5 + 0.5),
        mix(in_uv_max.y, in_uv_min.y, local.y * 0.5 + 0.5));
    unit_position = local;
    instance_shape = in_shape.x;
    polygon_segments = in_shape.y;
}
