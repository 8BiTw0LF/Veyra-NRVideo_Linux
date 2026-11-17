// VeyAmux for Linux: the demo scene "Abyssal Garden" (https://github.com/Noniv/abyssal-garden, MIT, Copyright (c) 2026
// Maksymilian Dendura), ported from JavaScript/WebGPU to C++.
//
// This file: math.js - random numbers, noise, quaternions, the mesh builder and surface nets. The JavaScript semantics
// are kept exactly (doubles for arithmetic, float storage where JS used a Float32Array, the same random stream), so the
// world comes out the same as in the browser.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace abyssal {

constexpr double PI = 3.14159265358979323846, TAU = PI * 2;

// world grid
constexpr int NX = 512, NY = 80, NZ = 512;
constexpr double VS = 0.75, OX = -192, OY = -56, OZ = -192;
constexpr int CELLS = 16;
constexpr double CELLW = 24;   // culling grid over XZ
constexpr int NCHUNK = 7;      // 3 small + 3 large rock chunk meshes + pebbles

// quality: the High level of the original (desktop)
constexpr int LV = 0, THIN = 1, SHADOW = 4096, AA = 4, WATER = 512, RAYN = 34;

// ---- JavaScript number helpers
inline double jsround(double x) { return std::floor(x + 0.5); }   // Math.round (halves go up)
inline int32_t toI32(double x) {                                  // x | 0
    if (!std::isfinite(x)) return 0;
    const double t = std::trunc(x);
    const double m = std::fmod(t, 4294967296.0);
    const int64_t v = int64_t(m < 0 ? m + 4294967296.0 : m);
    return int32_t(uint32_t(v));
}
inline uint8_t toU8(double x) { return uint8_t(uint32_t(toI32(x))); }   // store into a Uint8Array
inline double hyp(double a, double b) { return std::hypot(a, b); }
inline double hyp(double a, double b, double c) { return std::hypot(a, b, c); }
inline double hyp(double a, double b, double c, double d) { return std::sqrt(a * a + b * b + c * c + d * d); }
inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double mix(double a, double b, double t) { return a + (b - a) * t; }
inline double sstep(double a, double b, double x) { x = clampd((x - a) / (b - a), 0, 1); return x * x * (3 - 2 * x); }
inline double fract(double x) { return x - std::floor(x); }

// ---- random stream (mulberry32)
struct Rng {
    int32_t seed = 20261110;
    double operator()() {
        seed = int32_t(uint32_t(seed) + 0x6D2B79F5u);
        const uint32_t s = uint32_t(seed);
        uint32_t t = (s ^ (s >> 15)) * (1u | s);
        t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
        return double(t ^ (t >> 14)) / 4294967296.0;
    }
    double rr(double a, double b) { return a + (b - a) * (*this)(); }
    // each generator system starts its own random stream, so the systems stay independent
    void reseed(int s) { seed = int32_t(uint32_t(20261110) + uint32_t(s) * 7919u); }
};
extern Rng rnd;   // the one global stream, as in the original
inline double rr(double a, double b) { return rnd.rr(a, b); }
inline void rseed(int s) { rnd.reseed(s); }

inline double ihash(double x, double y, double z) {
    uint32_t h = uint32_t(toI32(x)) * 374761393u + uint32_t(toI32(y)) * 668265263u + uint32_t(toI32(z)) * 1274126177u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return double(h ^ (h >> 16)) / 4294967296.0;
}
inline double vnoise(double x, double y, double z) {
    const double ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
    double fx = x - ix, fy = y - iy, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
    const double a = mix(mix(ihash(ix, iy, iz), ihash(ix + 1, iy, iz), fx), mix(ihash(ix, iy + 1, iz), ihash(ix + 1, iy + 1, iz), fx), fy);
    const double b = mix(mix(ihash(ix, iy, iz + 1), ihash(ix + 1, iy, iz + 1), fx), mix(ihash(ix, iy + 1, iz + 1), ihash(ix + 1, iy + 1, iz + 1), fx), fy);
    return mix(a, b, fz);
}
inline double fbm(double x, double y, double z, int o = 4) {
    double a = 0.5, s = 0;
    for (int i = 0; i < o; i++) { s += a * vnoise(x, y, z); x = x * 2.03 + 17.1; y = y * 2.03 + 31.7; z = z * 2.03 + 5.3; a *= 0.5; }
    return s / (1 - 2 * a);
}

// ---- quaternions
struct Quat { double x = 0, y = 0, z = 0, w = 1; };
inline Quat qAxis(double x, double y, double z, double a) {
    double l = hyp(x, y, z); if (l == 0) l = 1;
    const double s = std::sin(a / 2) / l;
    return {x * s, y * s, z * s, std::cos(a / 2)};
}
inline Quat qMul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat qRand() {
    const double a = rr(-1, 1), b = rr(-1, 1), c = rr(-1, 1), d = rr(-1, 1);
    double l = hyp(a, b, c, d); if (l == 0) l = 1;
    return {a / l, b / l, c / l, d / l};
}
// shortest rotation taking +Y to n
inline Quat qFromY(double nx, double ny, double nz) {
    if (ny < -0.9999) return {1, 0, 0, 0};
    const double l = hyp(nz, 0, -nx, 1 + ny);
    return {nz / l, 0 / l, -nx / l, (1 + ny) / l};
}

// ---- surface nets: D > 0 inside; grid-space positions + triangles
struct NetMesh { std::vector<float> pos; std::vector<uint32_t> idx; uint32_t nv = 0; size_t ni = 0; };
NetMesh surfaceNets(const float* D, int nx, int ny, int nz);

// trilinear sampler over a scalar grid (grid coords)
struct Sampler3 {
    const float* D = nullptr; int nx = 0, ny = 0, nz = 0;
    double operator()(double x, double y, double z) const {
        x = clampd(x, 0, nx - 1.001); y = clampd(y, 0, ny - 1.001); z = clampd(z, 0, nz - 1.001);
        const int ix = int(x), iy = int(y), iz = int(z);
        const double fx = x - ix, fy = y - iy, fz = z - iz;
        const size_t i = size_t(ix) + size_t(nx) * (size_t(iy) + size_t(ny) * iz), sl = size_t(nx) * ny;
        const double a = mix(mix(D[i], D[i + 1], fx), mix(D[i + nx], D[i + nx + 1], fx), fy);
        const double b = mix(mix(D[i + sl], D[i + sl + 1], fx), mix(D[i + sl + nx], D[i + sl + nx + 1], fx), fy);
        return mix(a, b, fz);
    }
};

// ---- mesh builder (solid vertex format: f32x3 position, snorm8x4 normal + aux)
using V3 = std::array<double, 3>;
struct BuiltMesh {
    std::vector<uint8_t> vb;   // 16 bytes per vertex
    std::vector<uint32_t> ib;
    uint32_t nv = 0;
    uint32_t v0 = 0, i0 = 0;   // place in the merged buffers
};
class Mesh {
public:
    std::vector<double> p, aux;
    std::vector<uint32_t> i;
    uint32_t v(double x, double y, double z, double a = 1) { p.push_back(x); p.push_back(y); p.push_back(z); aux.push_back(a); return uint32_t(p.size() / 3 - 1); }
    uint32_t v(const V3& q, double a = 1) { return v(q[0], q[1], q[2], a); }
    void tri(uint32_t a, uint32_t b, uint32_t c) { i.insert(i.end(), {a, b, c}); }
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { i.insert(i.end(), {a, b, c, a, c, d}); }
    // box-section sweep along a polyline; faces get their own vertices (hard edges)
    void sweep(const std::vector<V3>& pts, const std::function<double(int)>& w, const std::function<double(int)>& h,
               const std::function<V3(const V3&, int)>& upFn, bool caps = true);
    void sweep(const std::vector<V3>& pts, double w, double h, const std::function<V3(const V3&, int)>& upFn, bool caps = true) {
        sweep(pts, [w](int) { return w; }, [h](int) { return h; }, upFn, caps);
    }
    // round tube along a polyline with a radius function
    void tube(const std::vector<V3>& pts, const std::function<double(int)>& rad, int sides = 6, bool capEnd = true,
              const std::function<double(int)>& auxFn = nullptr);
    BuiltMesh build() const;
};
Mesh icosphere(int sub, const std::function<V3(double, double, double)>& fn);

} // namespace abyssal
