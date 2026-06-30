// geometry.hpp — ISO 8855 coordinate geometry for the AFI920 C++ 3D viewer.
//
// World frame: ISO 8855 automotive convention
//     X = forward, Y = left, Z = up   (right-handed)
//
// A detection is reported in its sensor frame as spherical
// (radial_distance, azimuth, elevation):
//     azimuth   : + from X (forward) toward Y (left)
//     elevation : + upward toward Z
//
//     x = r * cos(el) * cos(az)
//     y = r * cos(el) * sin(az)
//     z = r * sin(el)
//
// The world point is  p_world = R(yaw,pitch,roll) * p_sensor + offset,
// with intrinsic Z-Y-X rotation (R = Rz(yaw) * Ry(pitch) * Rx(roll)).
//
// Header-only, depends only on the C++ standard library (no Qt / no SDK),
// so it is unit-testable headless.
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <array>
#include <cmath>

namespace viewer {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// 3x3 row-major rotation matrix.
struct Mat3 {
    std::array<float, 9> m{{1, 0, 0, 0, 1, 0, 0, 0, 1}};

    Vec3 operator*(const Vec3& v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
};

inline Mat3 rotationMatrix(float yawDeg, float pitchDeg, float rollDeg) {
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    const float y = yawDeg * kDeg2Rad;
    const float p = pitchDeg * kDeg2Rad;
    const float r = rollDeg * kDeg2Rad;
    const float cy = std::cos(y), sy = std::sin(y);
    const float cp = std::cos(p), sp = std::sin(p);
    const float cr = std::cos(r), sr = std::sin(r);

    // R = Rz(yaw) * Ry(pitch) * Rx(roll)
    Mat3 R;
    R.m[0] = cy * cp;  R.m[1] = cy * sp * sr - sy * cr;  R.m[2] = cy * sp * cr + sy * sr;
    R.m[3] = sy * cp;  R.m[4] = sy * sp * sr + cy * cr;  R.m[5] = sy * sp * cr - cy * sr;
    R.m[6] = -sp;      R.m[7] = cp * sr;                 R.m[8] = cp * cr;
    return R;
}

inline Vec3 sphericalToCartesian(float r, float az, float el) {
    const float ce = std::cos(el);
    return {r * ce * std::cos(az), r * ce * std::sin(az), r * std::sin(el)};
}

// Rigid mounting transform (world_from_sensor).
class SensorPose {
public:
    SensorPose() = default;
    SensorPose(Vec3 offset, float yawDeg, float pitchDeg, float rollDeg)
        : offset_(offset), yawDeg_(yawDeg), pitchDeg_(pitchDeg),
          rollDeg_(rollDeg), R_(rotationMatrix(yawDeg, pitchDeg, rollDeg)) {}

    // Map a sensor-frame point into the world frame.
    Vec3 transform(const Vec3& p) const {
        Vec3 w = R_ * p;
        w.x += offset_.x;
        w.y += offset_.y;
        w.z += offset_.z;
        return w;
    }

    const Vec3& position() const { return offset_; }
    Vec3 forward() const { return R_ * Vec3{1.0f, 0.0f, 0.0f}; }

    float yawDeg() const { return yawDeg_; }
    float pitchDeg() const { return pitchDeg_; }
    float rollDeg() const { return rollDeg_; }

private:
    Vec3 offset_{};
    float yawDeg_ = 0.0f, pitchDeg_ = 0.0f, rollDeg_ = 0.0f;
    Mat3 R_{};
};

}  // namespace viewer
