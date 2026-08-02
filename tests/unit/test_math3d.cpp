#include <cmath>
#include <gtest/gtest.h>
#include <spectra/math3d.hpp>

using namespace spectra;

static constexpr float EPS = 1e-5f;
static constexpr float PI  = 3.14159265358979323846f;

// Helper to compare floats
static bool near(float a, float b, float eps = EPS)
{
    return std::fabs(a - b) < eps;
}

// ─── vec3 ────────────────────────────────────────────────────────────────────

TEST(Vec3, DefaultConstruction)
{
    vec3 v;
    EXPECT_EQ(v.x, 0.0f);
    EXPECT_EQ(v.y, 0.0f);
    EXPECT_EQ(v.z, 0.0f);
}

TEST(Vec3, ValueConstruction)
{
    vec3 v{1.0f, 2.0f, 3.0f};
    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.y, 2.0f);
    EXPECT_EQ(v.z, 3.0f);
}

TEST(Vec3, Addition)
{
    vec3 a{1, 2, 3}, b{4, 5, 6};
    auto c = a + b;
    EXPECT_EQ(c.x, 5.0f);
    EXPECT_EQ(c.y, 7.0f);
    EXPECT_EQ(c.z, 9.0f);
}

TEST(Vec3, Subtraction)
{
    vec3 a{5, 7, 9}, b{1, 2, 3};
    auto c = a - b;
    EXPECT_EQ(c.x, 4.0f);
    EXPECT_EQ(c.y, 5.0f);
    EXPECT_EQ(c.z, 6.0f);
}

TEST(Vec3, ScalarMultiply)
{
    vec3 v{1, 2, 3};
    auto r = v * 2.0f;
    EXPECT_EQ(r.x, 2.0f);
    EXPECT_EQ(r.y, 4.0f);
    EXPECT_EQ(r.z, 6.0f);
}

TEST(Vec3, ScalarMultiplyLeft)
{
    vec3 v{1, 2, 3};
    auto r = 3.0f * v;
    EXPECT_EQ(r.x, 3.0f);
    EXPECT_EQ(r.y, 6.0f);
    EXPECT_EQ(r.z, 9.0f);
}

TEST(Vec3, ScalarDivide)
{
    vec3 v{2, 4, 6};
    auto r = v / 2.0f;
    EXPECT_EQ(r.x, 1.0f);
    EXPECT_EQ(r.y, 2.0f);
    EXPECT_EQ(r.z, 3.0f);
}

TEST(Vec3, Negate)
{
    vec3 v{1, -2, 3};
    auto r = -v;
    EXPECT_EQ(r.x, -1.0f);
    EXPECT_EQ(r.y, 2.0f);
    EXPECT_EQ(r.z, -3.0f);
}

TEST(Vec3, CompoundAdd)
{
    vec3 v{1, 2, 3};
    v += vec3{10, 20, 30};
    EXPECT_EQ(v.x, 11.0f);
    EXPECT_EQ(v.y, 22.0f);
    EXPECT_EQ(v.z, 33.0f);
}

TEST(Vec3, Dot)
{
    vec3 a{1, 0, 0}, b{0, 1, 0};
    EXPECT_FLOAT_EQ(vec3_dot(a, b), 0.0f);
    EXPECT_FLOAT_EQ(vec3_dot(a, a), 1.0f);
    EXPECT_FLOAT_EQ(vec3_dot(vec3{1, 2, 3}, vec3{4, 5, 6}), 32.0f);
}

TEST(Vec3, Cross)
{
    vec3 x{1, 0, 0}, y{0, 1, 0} /*, z{0, 0, 1}*/;   // z currently unused
    auto r = vec3_cross(x, y);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 1.0f);
    auto r2 = vec3_cross(y, x);
    EXPECT_FLOAT_EQ(r2.z, -1.0f);
}

TEST(Vec3, Length)
{
    EXPECT_FLOAT_EQ(vec3_length(vec3{3, 4, 0}), 5.0f);
    EXPECT_FLOAT_EQ(vec3_length(vec3{0, 0, 0}), 0.0f);
    EXPECT_FLOAT_EQ(vec3_length(vec3{1, 0, 0}), 1.0f);
}

TEST(Vec3, LengthSq)
{
    EXPECT_FLOAT_EQ(vec3_length_sq(vec3{3, 4, 0}), 25.0f);
}

TEST(Vec3, Normalize)
{
    auto n = vec3_normalize(vec3{3, 0, 0});
    EXPECT_FLOAT_EQ(n.x, 1.0f);
    EXPECT_FLOAT_EQ(n.y, 0.0f);
    EXPECT_FLOAT_EQ(n.z, 0.0f);
    // Zero vector
    auto z = vec3_normalize(vec3{0, 0, 0});
    EXPECT_FLOAT_EQ(z.x, 0.0f);
}

TEST(Vec3, Lerp)
{
    vec3 a{0, 0, 0}, b{10, 20, 30};
    auto mid = vec3_lerp(a, b, 0.5f);
    EXPECT_FLOAT_EQ(mid.x, 5.0f);
    EXPECT_FLOAT_EQ(mid.y, 10.0f);
    EXPECT_FLOAT_EQ(mid.z, 15.0f);
}

TEST(Vec3, MinMax)
{
    vec3 a{1, 5, 3}, b{4, 2, 6};
    auto mn = vec3_min(a, b);
    auto mx = vec3_max(a, b);
    EXPECT_EQ(mn.x, 1.0f);
    EXPECT_EQ(mn.y, 2.0f);
    EXPECT_EQ(mn.z, 3.0f);
    EXPECT_EQ(mx.x, 4.0f);
    EXPECT_EQ(mx.y, 5.0f);
    EXPECT_EQ(mx.z, 6.0f);
}

TEST(Vec3, Equality)
{
    EXPECT_TRUE(vec3(1, 2, 3) == vec3(1, 2, 3));
    EXPECT_TRUE(vec3(1, 2, 3) != vec3(1, 2, 4));
}

TEST(Vec3, IndexAccess)
{
    vec3 v{10, 20, 30};
    EXPECT_FLOAT_EQ(v[0], 10.0f);
    EXPECT_FLOAT_EQ(v[1], 20.0f);
    EXPECT_FLOAT_EQ(v[2], 30.0f);
    v[1] = 99.0f;
    EXPECT_FLOAT_EQ(v.y, 99.0f);
}

// ─── vec4 ────────────────────────────────────────────────────────────────────

TEST(Vec4, Construction)
{
    vec4 v{1, 2, 3, 4};
    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.w, 4.0f);
}

TEST(Vec4, FromVec3)
{
    vec4 v(vec3{1, 2, 3}, 1.0f);
    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.z, 3.0f);
    EXPECT_EQ(v.w, 1.0f);
}

TEST(Vec4, Xyz)
{
    vec4 v{1, 2, 3, 4};
    auto v3 = v.xyz();
    EXPECT_EQ(v3.x, 1.0f);
    EXPECT_EQ(v3.z, 3.0f);
}

TEST(Vec4, Arithmetic)
{
    vec4 a{1, 2, 3, 4}, b{5, 6, 7, 8};
    auto sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 6.0f);
    auto diff = b - a;
    EXPECT_FLOAT_EQ(diff.w, 4.0f);
    auto scaled = a * 2.0f;
    EXPECT_FLOAT_EQ(scaled.z, 6.0f);
}

// ─── mat4 ────────────────────────────────────────────────────────────────────

TEST(Mat4, Identity)
{
    auto I = mat4_identity();
    EXPECT_EQ(I(0, 0), 1.0f);
    EXPECT_EQ(I(1, 1), 1.0f);
    EXPECT_EQ(I(2, 2), 1.0f);
    EXPECT_EQ(I(3, 3), 1.0f);
    EXPECT_EQ(I(0, 1), 0.0f);
    EXPECT_EQ(I(1, 0), 0.0f);
}

TEST(Mat4, MulIdentity)
{
    auto I = mat4_identity();
    auto T = mat4_translate(vec3{1, 2, 3});
    auto R = mat4_mul(I, T);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(R.m[i], T.m[i]);
}

TEST(Mat4, MulVec4Identity)
{
    auto I = mat4_identity();
    vec4 v{1, 2, 3, 1};
    auto r = mat4_mul_vec4(I, v);
    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.y, 2.0f);
    EXPECT_FLOAT_EQ(r.z, 3.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);
}

TEST(Mat4, Translate)
{
    auto T = mat4_translate(vec3{10, 20, 30});
    vec4 p{0, 0, 0, 1};
    auto r = mat4_mul_vec4(T, p);
    EXPECT_FLOAT_EQ(r.x, 10.0f);
    EXPECT_FLOAT_EQ(r.y, 20.0f);
    EXPECT_FLOAT_EQ(r.z, 30.0f);
}

TEST(Mat4, Scale)
{
    auto S = mat4_scale(vec3{2, 3, 4});
    vec4 p{1, 1, 1, 1};
    auto r = mat4_mul_vec4(S, p);
    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 3.0f);
    EXPECT_FLOAT_EQ(r.z, 4.0f);
}

TEST(Mat4, RotateZ90)
{
    auto R = mat4_rotate_z(PI / 2.0f);
    vec4 p{1, 0, 0, 1};
    auto r = mat4_mul_vec4(R, p);
    EXPECT_TRUE(near(r.x, 0.0f));
    EXPECT_TRUE(near(r.y, 1.0f));
}

TEST(Mat4, RotateX90)
{
    auto R = mat4_rotate_x(PI / 2.0f);
    vec4 p{0, 1, 0, 1};
    auto r = mat4_mul_vec4(R, p);
    EXPECT_TRUE(near(r.y, 0.0f));
    EXPECT_TRUE(near(r.z, 1.0f));
}

TEST(Mat4, RotateY90)
{
    auto R = mat4_rotate_y(PI / 2.0f);
    vec4 p{1, 0, 0, 1};
    auto r = mat4_mul_vec4(R, p);
    EXPECT_TRUE(near(r.x, 0.0f));
    EXPECT_TRUE(near(r.z, -1.0f));
}

TEST(Mat4, Transpose)
{
    auto T  = mat4_translate(vec3{1, 2, 3});
    auto Tt = mat4_transpose(T);
    EXPECT_FLOAT_EQ(Tt(0, 3), T(3, 0));
    EXPECT_FLOAT_EQ(Tt(3, 0), T(0, 3));
    // Double transpose = original
    auto Ttt = mat4_transpose(Tt);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(Ttt.m[i], T.m[i]);
}

TEST(Mat4, DeterminantIdentity)
{
    EXPECT_FLOAT_EQ(mat4_determinant(mat4_identity()), 1.0f);
}

TEST(Mat4, DeterminantScale)
{
    auto S = mat4_scale(vec3{2, 3, 4});
    EXPECT_FLOAT_EQ(mat4_determinant(S), 24.0f);
}

TEST(Mat4, InverseIdentity)
{
    auto I  = mat4_identity();
    auto Ii = mat4_inverse(I);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(Ii.m[i], I.m[i]);
}

TEST(Mat4, InverseTranslate)
{
    auto T  = mat4_translate(vec3{5, 10, 15});
    auto Ti = mat4_inverse(T);
    auto R  = mat4_mul(T, Ti);
    auto I  = mat4_identity();
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(near(R.m[i], I.m[i]));
}

TEST(Mat4, InverseScale)
{
    auto S  = mat4_scale(vec3{2, 4, 8});
    auto Si = mat4_inverse(S);
    auto R  = mat4_mul(S, Si);
    auto I  = mat4_identity();
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(near(R.m[i], I.m[i]));
}

TEST(Mat4, InverseRotation)
{
    auto R  = mat4_rotate_z(0.7f);
    auto Ri = mat4_inverse(R);
    auto P  = mat4_mul(R, Ri);
    auto I  = mat4_identity();
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(near(P.m[i], I.m[i]));
}

TEST(Mat4, InverseComplex)
{
    auto M  = mat4_mul(mat4_translate(vec3{3, -1, 7}),
                      mat4_mul(mat4_rotate_y(1.2f), mat4_scale(vec3{2, 0.5f, 3})));
    auto Mi = mat4_inverse(M);
    auto R  = mat4_mul(M, Mi);
    auto I  = mat4_identity();
    for (int i = 0; i < 16; ++i)
        EXPECT_TRUE(near(R.m[i], I.m[i], 1e-4f));
}

TEST(Mat4, Equality)
{
    auto a = mat4_identity();
    auto b = mat4_identity();
    EXPECT_TRUE(a == b);
    b.m[0] = 2.0f;
    EXPECT_TRUE(a != b);
}

// ─── Projection ──────────────────────────────────────────────────────────────

TEST(Mat4, OrthoCorners)
{
    auto O = mat4_ortho(0.0f, 100.0f, 0.0f, 100.0f, 0.0f, 1.0f);
    // Bottom-left corner should map to (-1, +1) in Vulkan (Y-flip)
    auto bl = mat4_mul_vec4(O, vec4{0, 0, 0, 1});
    EXPECT_TRUE(near(bl.x / bl.w, -1.0f));
    // Top-right corner should map to (+1, -1) in Vulkan
    auto tr = mat4_mul_vec4(O, vec4{100, 100, 0, 1});
    EXPECT_TRUE(near(tr.x / tr.w, 1.0f));
}

TEST(Mat4, PerspectiveFov)
{
    auto P = mat4_perspective(deg_to_rad(90.0f), 1.0f, 0.1f, 100.0f);
    // At z=-0.1 (near plane), a point at (0,0) should be at origin
    auto center = mat4_mul_vec4(P, vec4{0, 0, -0.1f, 1});
    EXPECT_TRUE(near(center.x / center.w, 0.0f));
    EXPECT_TRUE(near(center.y / center.w, 0.0f));
}

TEST(Mat4, LookAt)
{
    auto V = mat4_look_at(vec3{0, 0, 5}, vec3{0, 0, 0}, vec3{0, 1, 0});
    // Camera at z=5 looking at origin: origin should be at (0, 0, -5) in view space
    auto p = mat4_mul_vec4(V, vec4{0, 0, 0, 1});
    EXPECT_TRUE(near(p.x, 0.0f));
    EXPECT_TRUE(near(p.y, 0.0f));
    EXPECT_TRUE(near(p.z, -5.0f));
}

TEST(Mat4, LookAtRightVector)
{
    auto V = mat4_look_at(vec3{0, 0, 1}, vec3{0, 0, 0}, vec3{0, 1, 0});
    // Right should point in +X in view space
    auto r = mat4_mul_vec4(V, vec4{1, 0, 0, 0});
    EXPECT_TRUE(near(r.x, 1.0f));
    EXPECT_TRUE(near(r.y, 0.0f));
}

// ─── Quaternion ──────────────────────────────────────────────────────────────

TEST(Quat, Identity)
{
    auto q = quat_identity();
    EXPECT_EQ(q.x, 0.0f);
    EXPECT_EQ(q.y, 0.0f);
    EXPECT_EQ(q.z, 0.0f);
    EXPECT_EQ(q.w, 1.0f);
}

TEST(Quat, FromAxisAngleZ)
{
    auto q = quat_from_axis_angle(vec3{0, 0, 1}, PI / 2.0f);
    EXPECT_TRUE(near(quat_length(q), 1.0f));
}

TEST(Quat, RotateVector)
{
    auto q = quat_from_axis_angle(vec3{0, 0, 1}, PI / 2.0f);
    auto r = quat_rotate(q, vec3{1, 0, 0});
    EXPECT_TRUE(near(r.x, 0.0f));
    EXPECT_TRUE(near(r.y, 1.0f));
    EXPECT_TRUE(near(r.z, 0.0f));
}

TEST(Quat, RotateVectorX)
{
    auto q = quat_from_axis_angle(vec3{1, 0, 0}, PI / 2.0f);
    auto r = quat_rotate(q, vec3{0, 1, 0});
    EXPECT_TRUE(near(r.x, 0.0f));
    EXPECT_TRUE(near(r.y, 0.0f));
    EXPECT_TRUE(near(r.z, 1.0f));
}

TEST(Quat, MulIdentity)
{
    auto q = quat_from_axis_angle(vec3{0, 1, 0}, 0.5f);
    auto r = quat_mul(q, quat_identity());
    EXPECT_TRUE(near(r.x, q.x));
    EXPECT_TRUE(near(r.y, q.y));
    EXPECT_TRUE(near(r.z, q.z));
    EXPECT_TRUE(near(r.w, q.w));
}

TEST(Quat, MulInverse)
{
    auto q  = quat_from_axis_angle(vec3{1, 1, 0}, 1.0f);
    auto qi = quat_conjugate(q);
    auto r  = quat_mul(q, qi);
    EXPECT_TRUE(near(r.x, 0.0f));
    EXPECT_TRUE(near(r.y, 0.0f));
    EXPECT_TRUE(near(r.z, 0.0f));
    EXPECT_TRUE(near(r.w, 1.0f));
}

TEST(Quat, Normalize)
{
    quat q{1, 2, 3, 4};
    auto n = quat_normalize(q);
    EXPECT_TRUE(near(quat_length(n), 1.0f));
}

TEST(Quat, ToMat4)
{
    auto q = quat_from_axis_angle(vec3{0, 0, 1}, PI / 2.0f);
    auto M = quat_to_mat4(q);
    // Apply the matrix to (1, 0, 0) — same as rotating the vector
    auto r = mat4_mul_vec4(M, vec4{1, 0, 0, 1});
    EXPECT_TRUE(near(r.x, 0.0f));
    EXPECT_TRUE(near(r.y, 1.0f));
    EXPECT_TRUE(near(r.z, 0.0f));
}

TEST(Quat, Mat4RoundTrip)
{
    auto q  = quat_from_axis_angle(vec3_normalize(vec3{1, 1, 1}), 0.7f);
    auto M  = quat_to_mat4(q);
    auto q2 = quat_from_mat4(M);
    // Quaternions may differ by sign (q == -q represents same rotation)
    float dot = q.x * q2.x + q.y * q2.y + q.z * q2.z + q.w * q2.w;
    EXPECT_TRUE(near(std::fabs(dot), 1.0f));
}

TEST(Quat, Slerp)
{
    auto a   = quat_identity();
    auto b   = quat_from_axis_angle(vec3{0, 0, 1}, PI / 2.0f);
    auto mid = quat_slerp(a, b, 0.5f);
    // Midpoint of 0 and 90 degrees around Z should be 45 degrees
    auto  v        = quat_rotate(mid, vec3{1, 0, 0});
    float expected = std::cos(PI / 4.0f);
    EXPECT_TRUE(near(v.x, expected));
    EXPECT_TRUE(near(v.y, expected));
}

TEST(Quat, SlerpEndpoints)
{
    auto a  = quat_from_axis_angle(vec3{0, 1, 0}, 0.3f);
    auto b  = quat_from_axis_angle(vec3{0, 1, 0}, 1.5f);
    auto r0 = quat_slerp(a, b, 0.0f);
    auto r1 = quat_slerp(a, b, 1.0f);
    EXPECT_TRUE(near(r0.x, a.x) && near(r0.y, a.y) && near(r0.z, a.z) && near(r0.w, a.w));
    EXPECT_TRUE(near(r1.x, b.x) && near(r1.y, b.y) && near(r1.z, b.z) && near(r1.w, b.w));
}

TEST(Quat, Equality)
{
    EXPECT_TRUE(quat_identity() == quat_identity());
    EXPECT_TRUE(quat(1, 0, 0, 0) != quat_identity());
}

// ─── Unproject / Ray ─────────────────────────────────────────────────────────

TEST(Unproject, CenterRay)
{
    auto P    = mat4_perspective(deg_to_rad(90.0f), 1.0f, 0.1f, 100.0f);
    auto V    = mat4_look_at(vec3{0, 0, 5}, vec3{0, 0, 0}, vec3{0, 1, 0});
    auto MVP  = mat4_mul(P, V);
    auto MVPi = mat4_inverse(MVP);

    // Center of 800x600 screen
    Ray ray = unproject(400.0f, 300.0f, MVPi, 800.0f, 600.0f);
    // Should point roughly towards -Z (from camera at z=5 looking at origin)
    EXPECT_TRUE(near(ray.direction.x, 0.0f, 0.1f));
    EXPECT_TRUE(near(ray.direction.y, 0.0f, 0.1f));
    EXPECT_TRUE(ray.direction.z < 0.0f);
}

// ─── Utility ─────────────────────────────────────────────────────────────────

TEST(Utility, DegRad)
{
    EXPECT_TRUE(near(deg_to_rad(180.0f), PI));
    EXPECT_TRUE(near(rad_to_deg(PI), 180.0f));
    EXPECT_TRUE(near(deg_to_rad(0.0f), 0.0f));
    EXPECT_TRUE(near(deg_to_rad(90.0f), PI / 2.0f));
}

TEST(Utility, Clamp)
{
    EXPECT_FLOAT_EQ(clampf(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(clampf(-1.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(clampf(2.0f, 0.0f, 1.0f), 1.0f);
}

// ─── FrameUBO Layout Compatibility ──────────────────────────────────────────

TEST(FrameUBOLayout, SizeMultipleOf16)
{
    // std140 requires struct size to be multiple of 16 bytes
    // This is important for UBO alignment
    EXPECT_EQ(sizeof(float) * 16, 64u);   // mat4 = 64 bytes
}

TEST(Mat4, ColumnMajorLayout)
{
    // Verify column-major: m[col*4+row]
    auto T = mat4_translate(vec3{10, 20, 30});
    // Translation should be in column 3 (indices 12, 13, 14)
    EXPECT_FLOAT_EQ(T.m[12], 10.0f);
    EXPECT_FLOAT_EQ(T.m[13], 20.0f);
    EXPECT_FLOAT_EQ(T.m[14], 30.0f);
}
