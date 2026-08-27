#pragma once
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        float len = length();
        if (len < 1e-6f) return {0, 0, 0};
        return { x / len, y / len, z / len };
    }
};

// Minimal 4x4, used only to rotate/translate Vec3 points (no full matrix stack needed).
struct Mat4 {
    float m[4][4] = {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1}
    };

    static Mat4 identity() { return Mat4{}; }

    static Mat4 rotationY(float radians) {
        Mat4 r = identity();
        float c = std::cos(radians);
        float s = std::sin(radians);
        r.m[0][0] = c;  r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r = identity();
        r.m[0][3] = x; r.m[1][3] = y; r.m[2][3] = z;
        return r;
    }

    Mat4 multiply(const Mat4& o) const {
        Mat4 r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += m[row][k] * o.m[k][col];
                r.m[row][col] = sum;
            }
        }
        return r;
    }

    Vec3 transformPoint(const Vec3& v) const {
        float x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3];
        float y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3];
        float z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3];
        return {x, y, z};
    }
};

struct ProjectedPoint {
    float screenX;
    float screenY;
    float depth; // camera-space Z; used for backface/depth-sort elsewhere
};

// Perspective projection: camera looks down +Z from the origin. Point must be in front
// (z > 0). Maps NDC [-1,1] to pixel space with (0,0) at top-left.
inline ProjectedPoint project(const Vec3& cameraSpacePoint, float focalLength,
                               int screenWidth, int screenHeight) {
    float z = cameraSpacePoint.z;
    if (z < 0.01f) z = 0.01f;
    float ndcX = (cameraSpacePoint.x * focalLength) / z;
    float ndcY = (cameraSpacePoint.y * focalLength) / z;
    ProjectedPoint p;
    p.screenX = (ndcX + 1.0f) * 0.5f * screenWidth;
    p.screenY = (1.0f - (ndcY + 1.0f) * 0.5f) * screenHeight;
    p.depth = z;
    return p;
}
