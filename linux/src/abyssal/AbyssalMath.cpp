// VeyAmux for Linux: Abyssal Garden demo scene, math.js (see AbyssalMath.h).
#include "AbyssalMath.h"

#include <algorithm>

namespace abyssal {

Rng rnd;

NetMesh surfaceNets(const float* D, int nx, int ny, int nz) {
    NetMesh r;
    r.pos.reserve(1 << 20);
    r.idx.reserve(1 << 21);
    const size_t sl = size_t(nx) * ny;
    std::vector<int32_t> cur(sl), prev(sl);
    for (int z = 0; z < nz - 1; z++) {
        for (int y = 0; y < ny - 1; y++) {
            size_t i0 = size_t(nx) * (y + size_t(ny) * z);
            for (int x = 0; x < nx - 1; x++, i0++) {
                const float d0 = D[i0], d1 = D[i0 + 1], d2 = D[i0 + nx], d3 = D[i0 + nx + 1];
                const float d4 = D[i0 + sl], d5 = D[i0 + sl + 1], d6 = D[i0 + sl + nx], d7 = D[i0 + sl + nx + 1];
                const int m = (d0 > 0) | ((d1 > 0) << 1) | ((d2 > 0) << 2) | ((d3 > 0) << 3) | ((d4 > 0) << 4) | ((d5 > 0) << 5) | ((d6 > 0) << 6) | ((d7 > 0) << 7);
                if (m == 0 || m == 255) continue;
                double sx = 0, sy = 0, sz = 0, t;
                int n = 0;
                auto e = [&](double a, double b) { return double(a) / (double(a) - double(b)); };
                if ((d0 > 0) != (d1 > 0)) { t = e(d0, d1); sx += t; n++; }
                if ((d2 > 0) != (d3 > 0)) { t = e(d2, d3); sx += t; sy += 1; n++; }
                if ((d4 > 0) != (d5 > 0)) { t = e(d4, d5); sx += t; sz += 1; n++; }
                if ((d6 > 0) != (d7 > 0)) { t = e(d6, d7); sx += t; sy += 1; sz += 1; n++; }
                if ((d0 > 0) != (d2 > 0)) { t = e(d0, d2); sy += t; n++; }
                if ((d1 > 0) != (d3 > 0)) { t = e(d1, d3); sy += t; sx += 1; n++; }
                if ((d4 > 0) != (d6 > 0)) { t = e(d4, d6); sy += t; sz += 1; n++; }
                if ((d5 > 0) != (d7 > 0)) { t = e(d5, d7); sy += t; sx += 1; sz += 1; n++; }
                if ((d0 > 0) != (d4 > 0)) { t = e(d0, d4); sz += t; n++; }
                if ((d1 > 0) != (d5 > 0)) { t = e(d1, d5); sz += t; sx += 1; n++; }
                if ((d2 > 0) != (d6 > 0)) { t = e(d2, d6); sz += t; sy += 1; n++; }
                if ((d3 > 0) != (d7 > 0)) { t = e(d3, d7); sz += t; sx += 1; sy += 1; n++; }
                const int32_t vi = int32_t(r.nv++);
                r.pos.push_back(float(x + sx / n)); r.pos.push_back(float(y + sy / n)); r.pos.push_back(float(z + sz / n));
                const size_t ci = size_t(x) + size_t(nx) * y;
                cur[ci] = vi;
                const bool s0 = d0 > 0;
                auto emit = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
                    if (s0) r.idx.insert(r.idx.end(), {a, b, c, a, c, d});
                    else r.idx.insert(r.idx.end(), {a, c, b, a, d, c});
                };
                if ((d1 > 0) != s0 && y > 0 && z > 0) emit(vi, cur[ci - nx], prev[ci - nx], prev[ci]);
                if ((d2 > 0) != s0 && x > 0 && z > 0) emit(vi, prev[ci], prev[ci - 1], cur[ci - 1]);
                if ((d4 > 0) != s0 && x > 0 && y > 0) emit(vi, cur[ci - 1], cur[ci - 1 - nx], cur[ci - nx]);
            }
        }
        std::swap(cur, prev);
    }
    r.ni = r.idx.size();
    return r;
}

void Mesh::sweep(const std::vector<V3>& pts, const std::function<double(int)>& w, const std::function<double(int)>& h,
                 const std::function<V3(const V3&, int)>& upFn, bool caps) {
    std::vector<std::array<uint32_t, 8>> rings;
    const int n = int(pts.size());
    for (int k = 0; k < n; k++) {
        const V3& p = pts[k]; const V3& a = pts[std::max(k - 1, 0)]; const V3& b = pts[std::min(k + 1, n - 1)];
        double tx = b[0] - a[0], ty = b[1] - a[1], tz = b[2] - a[2];
        double tl = hyp(tx, ty, tz); if (tl == 0) tl = 1;
        tx /= tl; ty /= tl; tz /= tl;
        const V3 u0 = upFn ? upFn(p, k) : V3{0, 1, 0};
        double sx = u0[1] * tz - u0[2] * ty, sy = u0[2] * tx - u0[0] * tz, sz = u0[0] * ty - u0[1] * tx;
        double sl = hyp(sx, sy, sz); if (sl == 0) sl = 1;
        sx /= sl; sy /= sl; sz /= sl;
        const double ux = ty * sz - tz * sy, uy = tz * sx - tx * sz, uz = tx * sy - ty * sx;
        const double ww = w(k), hh = h(k);
        static const int E[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        V3 c[4];
        for (int s = 0; s < 4; s++) {
            const double e = E[s][0], f = E[s][1];
            c[s] = {p[0] + sx * e * ww / 2 + ux * f * hh / 2, p[1] + sy * e * ww / 2 + uy * f * hh / 2, p[2] + sz * e * ww / 2 + uz * f * hh / 2};
        }
        std::array<uint32_t, 8> r{};
        for (int s = 0; s < 4; s++) { r[s * 2] = v(c[s]); r[s * 2 + 1] = v(c[(s + 1) % 4]); }
        rings.push_back(r);
        if (caps && (k == 0 || k == n - 1)) {
            uint32_t q[4];
            for (int s = 0; s < 4; s++) q[s] = v(c[s]);
            if (k == 0) quad(q[0], q[1], q[2], q[3]); else quad(q[3], q[2], q[1], q[0]);
        }
    }
    for (size_t k = 0; k + 1 < rings.size(); k++)
        for (int s = 0; s < 4; s++) {
            const uint32_t a = rings[k][s * 2], b = rings[k][s * 2 + 1], c = rings[k + 1][s * 2 + 1], d = rings[k + 1][s * 2];
            quad(a, d, c, b);
        }
}

void Mesh::tube(const std::vector<V3>& pts, const std::function<double(int)>& rad, int sides, bool capEnd,
                const std::function<double(int)>& auxFn) {
    std::vector<std::vector<uint32_t>> rings;
    double ux = 0.31, uy = 0.9, uz = 0.3;
    const int n = int(pts.size());
    for (int k = 0; k < n; k++) {
        const V3& p = pts[k]; const V3& a = pts[std::max(k - 1, 0)]; const V3& b = pts[std::min(k + 1, n - 1)];
        double tx = b[0] - a[0], ty = b[1] - a[1], tz = b[2] - a[2];
        double tl = hyp(tx, ty, tz); if (tl == 0) tl = 1;
        tx /= tl; ty /= tl; tz /= tl;
        double sx = uy * tz - uz * ty, sy = uz * tx - ux * tz, sz = ux * ty - uy * tx;
        double sl = hyp(sx, sy, sz); if (sl == 0) sl = 1;
        sx /= sl; sy /= sl; sz /= sl;
        ux = ty * sz - tz * sy; uy = tz * sx - tx * sz; uz = tx * sy - ty * sx;
        const double r = rad(k);
        std::vector<uint32_t> ring;
        for (int s = 0; s < sides; s++) {
            const double an = double(s) / sides * TAU;
            ring.push_back(v(p[0] + (sx * std::cos(an) + ux * std::sin(an)) * r, p[1] + (sy * std::cos(an) + uy * std::sin(an)) * r,
                             p[2] + (sz * std::cos(an) + uz * std::sin(an)) * r,
                             auxFn ? auxFn(k) : std::min(1.0, 0.35 + 0.65 * k / (n - 1))));
        }
        rings.push_back(ring);
    }
    for (size_t k = 0; k + 1 < rings.size(); k++)
        for (int s = 0; s < sides; s++) {
            const int s2 = (s + 1) % sides;
            quad(rings[k][s], rings[k][s2], rings[k + 1][s2], rings[k + 1][s]);
        }
    if (capEnd) {
        const V3& e = pts[n - 1];
        const uint32_t c = v(e[0], e[1], e[2], auxFn ? auxFn(n - 1) : 1);
        const auto& r = rings.back();
        for (int s = 0; s < sides; s++) tri(r[s], r[(s + 1) % sides], c);
    }
}

BuiltMesh Mesh::build() const {
    const size_t n = p.size() / 3;
    std::vector<float> N(n * 3, 0.0f);
    for (size_t t = 0; t < i.size(); t += 3) {
        const size_t a = size_t(i[t]) * 3, b = size_t(i[t + 1]) * 3, c = size_t(i[t + 2]) * 3;
        const double ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2], vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        for (size_t k : {a, b, c}) { N[k] = float(N[k] + nx); N[k + 1] = float(N[k + 1] + ny); N[k + 2] = float(N[k + 2] + nz); }
    }
    BuiltMesh m;
    m.vb.resize(n * 16);
    for (size_t k = 0; k < n; k++) {
        const double nx = N[k * 3], ny = N[k * 3 + 1], nz = N[k * 3 + 2];
        double l = hyp(nx, ny, nz); if (l == 0) l = 1;
        float* f = reinterpret_cast<float*>(m.vb.data() + k * 16);
        f[0] = float(p[k * 3]); f[1] = float(p[k * 3 + 1]); f[2] = float(p[k * 3 + 2]);
        int8_t* s = reinterpret_cast<int8_t*>(m.vb.data() + k * 16 + 12);
        s[0] = int8_t(jsround(nx / l * 127)); s[1] = int8_t(jsround(ny / l * 127)); s[2] = int8_t(jsround(nz / l * 127));
        s[3] = int8_t(jsround(clampd(aux[k], -1, 1) * 127));
    }
    m.ib = i;
    m.nv = uint32_t(n);
    return m;
}

Mesh icosphere(int sub, const std::function<V3(double, double, double)>& fn) {
    Mesh m;
    const double t = (1 + std::sqrt(5.0)) / 2;
    std::vector<V3> V = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& v : V) { const double l = hyp(v[0], v[1], v[2]); for (auto& c : v) c /= l; }
    std::vector<std::array<int, 3>> F = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (int s = 0; s < sub; s++) {
        std::unordered_map<int64_t, int> cache;
        std::vector<std::array<int, 3>> F2;
        auto mid = [&](int a, int b) {
            const int64_t key = a < b ? int64_t(a) * 65536 + b : int64_t(b) * 65536 + a;
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            V3 p{V[a][0] + V[b][0], V[a][1] + V[b][1], V[a][2] + V[b][2]};
            const double l = hyp(p[0], p[1], p[2]);
            V.push_back({p[0] / l, p[1] / l, p[2] / l});
            const int r = int(V.size()) - 1;
            cache[key] = r;
            return r;
        };
        for (const auto& f : F) {
            const int a = f[0], b = f[1], c = f[2];
            const int ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            F2.push_back({a, ab, ca}); F2.push_back({b, bc, ab}); F2.push_back({c, ca, bc}); F2.push_back({ab, bc, ca});
        }
        F = F2;
    }
    for (const auto& v : V) { const V3 p = fn ? fn(v[0], v[1], v[2]) : v; m.v(p[0], p[1], p[2]); }
    for (const auto& f : F) m.tri(uint32_t(f[0]), uint32_t(f[1]), uint32_t(f[2]));
    return m;
}

} // namespace abyssal
