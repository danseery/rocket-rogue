#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_texture;

layout(location = 0) in vec4 vertex_colour;
layout(location = 1) in vec2 vertex_uv;
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

void main()
{
    if (scene.effect_mode != 0u) {
        float gradient_width = scene.effect_params.x;
        float frame_width = scene.effect_params.y;
        float feather = scene.effect_params.z;
        float radius = scene.effect_params.w;
        vec2 point = (vertex_uv - vec2(0.5)) * scene.effect_size;
        vec2 half_size = scene.effect_size * 0.5;
        vec2 rounded = abs(point) - (half_size - vec2(radius));
        float signed_distance = length(max(rounded, vec2(0.0)))
            + min(max(rounded.x, rounded.y), 0.0)
            - radius;
        float inside_distance = max(-signed_distance, 0.0);
        float inside_mask = 1.0 - smoothstep(0.0, feather, signed_distance);
        float vignette = 1.0 - smoothstep(0.0, gradient_width, inside_distance);
        float frame = 1.0 - smoothstep(frame_width, frame_width + feather, inside_distance);
        float alpha = scene.effect_colour.a * max(vignette * 0.42, frame) * inside_mask;
        out_colour = vec4(scene.effect_colour.rgb, alpha);
        return;
    }

    vec4 sprite = texture(scene_texture, vertex_uv) * vertex_colour;
    out_colour = scene.use_texture != 0u ? sprite : vertex_colour;
}
