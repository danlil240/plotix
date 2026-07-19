#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec2 viewport_size;
    float time;
    float _pad0;
    vec3 camera_pos;
    float near_plane;
    vec3 light_dir;
    float far_plane;
};

layout(push_constant) uniform SeriesPC {
    vec4  color;
    float line_width;
    float point_size;
    float data_offset_x;
    float data_offset_y;
    uint  line_style;
    uint  marker_type;
    float marker_size;
    float opacity;
    float dash_pattern[8];
    float dash_total;
    int   dash_count;
    float _pad2[2];
};

// Position plus scalar value, padded to a 16-byte std430 stride.
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec4 points[];
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out float v_color_value;

void main() {
    int point_index = gl_InstanceIndex;
    int tri_vert = gl_VertexIndex % 6;
    int corner_map[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner = corner_map[tri_vert];

    vec4 point = points[point_index];
    vec2 center = point.xy + vec2(data_offset_x, data_offset_y);

    vec4 clip_center = projection * view * model * vec4(center, 0.0, 1.0);
    vec2 ndc_center = clip_center.xy / clip_center.w;
    vec2 screen_center = (ndc_center * 0.5 + 0.5) * viewport_size;
    float half_size = point_size * 0.5 + 1.0;

    vec2 offset = vec2(
        (corner & 1) == 0 ? -1.0 : 1.0,
        (corner & 2) == 0 ? -1.0 : 1.0
    );
    vec2 screen_pos = screen_center + offset * half_size;
    vec2 ndc = (screen_pos / viewport_size) * 2.0 - 1.0;

    gl_Position = vec4(ndc * clip_center.w, 0.0, clip_center.w);
    v_uv = offset;
    v_color_value = point.z;
}
