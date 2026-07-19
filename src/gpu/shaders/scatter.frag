#version 450

// Per-series push constant — must match SeriesPushConstants in backend.hpp
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

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in float v_color_value;

layout(location = 0) out vec4 out_color;

vec3 apply_colormap(int cm_type, float t) {
    t = clamp(t, 0.0, 1.0);
    if (cm_type == 1) { // Viridis
        vec3 c0 = vec3(0.267, 0.005, 0.329);
        vec3 c1 = vec3(0.128, 0.567, 0.551);
        vec3 c2 = vec3(0.993, 0.906, 0.144);
        return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);
    }
    if (cm_type == 2) { // Plasma
        vec3 c0 = vec3(0.050, 0.030, 0.528);
        vec3 c1 = vec3(0.798, 0.280, 0.470);
        vec3 c2 = vec3(0.940, 0.975, 0.131);
        return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);
    }
    if (cm_type == 3) { // Inferno
        vec3 c0 = vec3(0.001, 0.000, 0.014);
        vec3 c1 = vec3(0.735, 0.216, 0.330);
        vec3 c2 = vec3(0.988, 0.998, 0.645);
        return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);
    }
    if (cm_type == 4) { // Magma
        vec3 c0 = vec3(0.001, 0.000, 0.014);
        vec3 c1 = vec3(0.716, 0.215, 0.475);
        vec3 c2 = vec3(0.987, 0.991, 0.750);
        return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);
    }
    if (cm_type == 5) { // Jet
        return clamp(vec3(1.5) - abs(4.0 * t - vec3(3.0, 2.0, 1.0)), 0.0, 1.0);
    }
    if (cm_type == 6) { // Coolwarm
        vec3 cool = vec3(0.230, 0.299, 0.754);
        vec3 mid = vec3(0.865, 0.865, 0.865);
        vec3 warm = vec3(0.706, 0.016, 0.150);
        return t < 0.5 ? mix(cool, mid, t * 2.0) : mix(mid, warm, (t - 0.5) * 2.0);
    }
    if (cm_type == 7) { // Grayscale
        return vec3(t);
    }
    return color.rgb;
}

// ─── SDF helpers ─────────────────────────────────────────────────────────────
// All shapes are centered at origin, fitting within the -1..1 UV quad.
// Shapes are sized to ~0.8 radius for consistent visual weight.

float sdf_circle(vec2 p) {
    return length(p) - 0.85;
}

float sdf_square(vec2 p) {
    vec2 d = abs(p) - vec2(0.7);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdf_diamond(vec2 p) {
    vec2 d = abs(p);
    return (d.x + d.y) * 0.7071 - 0.6;
}

float sdf_triangle_up(vec2 p) {
    // Equilateral triangle pointing up, centered
    p.y += 0.15; // shift down to visually center
    float k = sqrt(3.0);
    p.x = abs(p.x) - 0.75;
    p.y = p.y + 0.75 / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -1.5, 0.0);
    return -length(p) * sign(p.y);
}

float sdf_triangle_down(vec2 p) {
    return sdf_triangle_up(vec2(p.x, -p.y));
}

float sdf_triangle_left(vec2 p) {
    return sdf_triangle_up(vec2(p.y, -p.x));
}

float sdf_triangle_right(vec2 p) {
    return sdf_triangle_up(vec2(-p.y, p.x));
}

float sdf_plus(vec2 p, float arm_w) {
    vec2 d = abs(p);
    return min(max(d.x - arm_w, d.y - 0.75),
               max(d.x - 0.75, d.y - arm_w));
}

float sdf_cross(vec2 p, float arm_w) {
    vec2 r = vec2(p.x - p.y, p.x + p.y) * 0.7071;
    return sdf_plus(r, arm_w);
}

float sdf_star(vec2 p) {
    // 5-pointed star — Inigo Quilez formula
    const float PI = 3.14159265;
    float an = PI * 2.0 / 5.0;
    float en = PI / 5.0;
    vec2 acs = vec2(cos(an), sin(an));
    vec2 ecs = vec2(cos(en), sin(en));
    float bn = mod(atan(p.x, p.y), an) - 0.5 * an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= 0.65 * acs;
    p += ecs * clamp(-dot(p, ecs), 0.0, 0.65 * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

float sdf_pentagon(vec2 p) {
    const float PI = 3.14159265;
    const vec3 k = vec3(0.809016994, 0.587785252, 0.726542528); // cos/sin/tan of pi/5
    p.x = abs(p.x);
    p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
    p -= 2.0 * min(dot(vec2( k.x, k.y), p), 0.0) * vec2( k.x, k.y);
    p -= vec2(clamp(p.x, -0.7 * k.z, 0.7 * k.z), 0.7);
    return length(p) * sign(p.y);
}

float sdf_hexagon(vec2 p) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * 0.75, k.z * 0.75), 0.75);
    return length(p) * sign(p.y);
}

// ─── Main ────────────────────────────────────────────────────────────────────

void main() {
    // AA width in UV space — scale by point size for resolution-independent smoothing
    // Larger point_size → smaller AA band in UV → sharper edges
    float aa = clamp(2.5 / point_size, 0.01, 0.15);

    float d;
    bool is_filled = true;
    // Stroke width in UV space — visible and crisp at all sizes
    float stroke_w = clamp(4.0 / point_size, 0.08, 0.25);

    // marker_type enum values:
    // 0=None, 1=Point, 2=Circle, 3=Plus, 4=Cross, 5=Star,
    // 6=Square, 7=Diamond, 8=TriUp, 9=TriDown, 10=TriLeft, 11=TriRight,
    // 12=Pentagon, 13=Hexagon,
    // 14=FilledCircle, 15=FilledSquare, 16=FilledDiamond, 17=FilledTriUp

    switch (marker_type) {
        case 1u: // Point — small filled circle
            d = length(v_uv) - 0.55;
            is_filled = true;
            break;
        case 2u: // Circle (outline)
            d = sdf_circle(v_uv);
            is_filled = false;
            break;
        case 3u: // Plus
            d = sdf_plus(v_uv, 0.15);
            is_filled = true;
            break;
        case 4u: // Cross (X)
            d = sdf_cross(v_uv, 0.15);
            is_filled = true;
            break;
        case 5u: // Star
            d = sdf_star(v_uv);
            is_filled = true;
            break;
        case 6u: // Square (outline)
            d = sdf_square(v_uv);
            is_filled = false;
            break;
        case 7u: // Diamond (outline)
            d = sdf_diamond(v_uv);
            is_filled = false;
            break;
        case 8u: // Triangle Up (outline)
            d = sdf_triangle_up(v_uv);
            is_filled = false;
            break;
        case 9u: // Triangle Down (outline)
            d = sdf_triangle_down(v_uv);
            is_filled = false;
            break;
        case 10u: // Triangle Left (outline)
            d = sdf_triangle_left(v_uv);
            is_filled = false;
            break;
        case 11u: // Triangle Right (outline)
            d = sdf_triangle_right(v_uv);
            is_filled = false;
            break;
        case 12u: // Pentagon (outline)
            d = sdf_pentagon(v_uv);
            is_filled = false;
            break;
        case 13u: // Hexagon (outline)
            d = sdf_hexagon(v_uv);
            is_filled = false;
            break;
        case 14u: // Filled Circle
            d = sdf_circle(v_uv);
            is_filled = true;
            break;
        case 15u: // Filled Square
            d = sdf_square(v_uv);
            is_filled = true;
            break;
        case 16u: // Filled Diamond
            d = sdf_diamond(v_uv);
            is_filled = true;
            break;
        case 17u: // Filled Triangle Up
            d = sdf_triangle_up(v_uv);
            is_filled = true;
            break;
        default: // Fallback: filled circle
            d = sdf_circle(v_uv);
            is_filled = true;
            break;
    }

    float alpha;
    if (is_filled) {
        // Filled shape: smooth edge
        alpha = smoothstep(aa, -aa, d);
    } else {
        // Outline shape: annular ring with smooth inner and outer edges
        float outer = smoothstep(aa, -aa, d);
        float inner = smoothstep(-aa, aa, d + stroke_w);
        alpha = outer * inner;
    }

    if (alpha < 0.002) discard;

    vec3 base_color = color.rgb;
    if (_pad2[0] > 0.5 && dash_count > 0 && !isnan(v_color_value) && !isinf(v_color_value)) {
        float value_min = dash_pattern[0];
        float value_max = dash_pattern[1];
        float range = value_max - value_min;
        float t = abs(range) > 1e-20 ? (v_color_value - value_min) / range : 0.5;
        base_color = apply_colormap(dash_count, t);
    }

    out_color = vec4(base_color, color.a * opacity * alpha);
}
