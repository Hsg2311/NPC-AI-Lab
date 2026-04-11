#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace sim {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    constexpr Vec3() = default;
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s)       const { return { x * s,   y * s,   z * s   }; }
    Vec3 operator/(float s)       const { return { x / s,   y / s,   z / s   }; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    float dot(const Vec3& o)  const { return x * o.x + y * o.y + z * o.z; }
    float lengthSq()          const { return x * x + y * y + z * z; }
    float length()            const { return std::sqrt(lengthSq()); }

    Vec3 normalized() const {
        float len = length();
        if (len < 1e-6f) return { 0.f, 0.f, 0.f };
        return *this / len;
    }

    static float distance(const Vec3& a, const Vec3& b) {
        return (a - b).length();
    }

    static Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
        return a + (b - a) * t;
    }

    std::string toString() const {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "(%.1f,%.1f,%.1f)", x, y, z);
        return buf;
    }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

} // namespace sim
