#pragma once

#include <cmath>
#include <numbers>

namespace bitematica::render {

/// A 3-component vector.
struct Vec3 {
  float x{};
  float y{};
  float z{};

  Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

inline float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3 &v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3 &v) {
  const float l = length(v);
  return l <= 1e-6f ? Vec3{} : Vec3{v.x / l, v.y / l, v.z / l};
}

/// A 4x4 matrix in column-major order, matching OpenGL ES (so it uploads without transposing).
///
/// BiteMatica keeps its own copy rather than depending on the SDK's: the mesher, the frustum cull
/// and the world transform all need to be exercised by the host test suite, which must not pull in
/// the preloader. The two are converted at the single boundary where the mod calls the render API.
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  float at(int row, int col) const { return m[col * 4 + row]; }

  static Mat4 identity() { return Mat4{}; }

  static Mat4 translation(const Vec3 &t) {
    Mat4 r;
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
  }

  static Mat4 rotationY(float degrees) {
    const float r = degrees * static_cast<float>(std::numbers::pi) / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    Mat4 out;
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
  }

  static Mat4 multiply(const Mat4 &a, const Mat4 &b) {
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, col);
        out.m[col * 4 + row] = sum;
      }
    }
    return out;
  }

  Vec3 transformPoint(const Vec3 &p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
  }

  bool isFinite() const {
    for (float v : m) {
      if (!std::isfinite(v)) return false;
    }
    return true;
  }
};

/// Builds a right-handed perspective projection (column-major), OpenGL convention.
inline Mat4 perspective(float fovDegrees, float aspect, float nearPlane, float farPlane) {
  const float radians = fovDegrees * static_cast<float>(std::numbers::pi) / 180.0f;
  const float f = 1.0f / std::tan(radians * 0.5f);
  Mat4 out;
  out.m[0] = f / aspect;
  out.m[5] = f;
  out.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
  out.m[11] = -1.0f;
  out.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
  out.m[15] = 0.0f;
  return out;
}

/// Builds a right-handed look-at view matrix (column-major).
inline Mat4 lookAt(const Vec3 &eye, const Vec3 &forward, const Vec3 &up) {
  const Vec3 f = normalize(forward);
  const Vec3 s = normalize(cross(f, up));
  const Vec3 u = cross(s, f);
  Mat4 out;
  out.m[0] = s.x;  out.m[4] = s.y;  out.m[8] = s.z;
  out.m[1] = u.x;  out.m[5] = u.y;  out.m[9] = u.z;
  out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z;
  out.m[12] = -dot(s, eye);
  out.m[13] = -dot(u, eye);
  out.m[14] = dot(f, eye);
  out.m[15] = 1.0f;
  return out;
}

/// The six clip planes extracted from a world->clip matrix, for frustum culling.
///
/// Each plane is stored as (a, b, c, d) with the inside being `a*x + b*y + c*z + d >= 0`.
struct Frustum {
  float planes[6][4]{};

  static Frustum fromViewProjection(const Mat4 &vp) {
    // Gribb-Hartmann plane extraction. Each clip plane is a sum/difference of two rows of the
    // world->clip matrix: left/right are w±x, bottom/top are w±y, near/far are w±z.
    auto row = [&](int r, int c) { return vp.at(r, c); };
    Frustum frustum;
    auto setPlane = [&](int index, float sign, int r) {
      for (int c = 0; c < 4; ++c) frustum.planes[index][c] = row(3, c) + sign * row(r, c);
    };
    setPlane(0, 1.0f, 0);   // left:   w + x
    setPlane(1, -1.0f, 0);  // right:  w - x
    setPlane(2, 1.0f, 1);   // bottom: w + y
    setPlane(3, -1.0f, 1);  // top:    w - y
    setPlane(4, 1.0f, 2);   // near:   w + z
    setPlane(5, -1.0f, 2);  // far:    w - z
    return frustum;
  }

  /// True when an axis-aligned box (min/max corners) intersects the frustum.
  bool intersectsBox(const Vec3 &min, const Vec3 &max) const {
    for (const auto &plane : planes) {
      // The positive vertex of the box for this plane.
      const float px = plane[0] >= 0 ? max.x : min.x;
      const float py = plane[1] >= 0 ? max.y : min.y;
      const float pz = plane[2] >= 0 ? max.z : min.z;
      if (plane[0] * px + plane[1] * py + plane[2] * pz + plane[3] < 0.0f) return false;
    }
    return true;
  }
};

} // namespace bitematica::render
