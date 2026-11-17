// VeyAmux for Linux: Abyssal Garden demo scene, world.js (see AbyssalWorld.h). Translated statement by statement; where
// JavaScript evaluates several random numbers inside one expression or argument list, they are drawn into locals first
// (left to right), because C++ leaves that order open.
#include "AbyssalWorld.h"

#include <algorithm>
#include <unordered_set>

namespace abyssal {

namespace {

using std::abs; using std::cos; using std::sin; using std::sqrt; using std::floor; using std::pow; using std::exp; using std::max; using std::min;

// designed rock masses: ax, az, bx, bz, radius, top, overhang, waist
const double MESAS[][8] = {
    {-37, -42, -33, -22, 9, -7, 3.5, 2}, {-29, -15, -33, 5, 12, -14, 3, 2}, {-36, 11, -30, 27, 9, -5, 4, 2},
    {30, -45, 35, -27, 9, -9, 4, 2}, {37, -20, 31, -3, 11, -16, 3, 2}, {32, 5, 35, 9, 7, -8, 4, 2},
    {19, -15, 19, -15, 4.4, -6, 2, 1.5},
    {6, -88, 6, -88, 10, -34, 0, 0},
    {0, -106, 0, -106, 8, 3, 2, 2},
    {44, 6, 47, -8, 7, -17, 3, 1},
    {33, 29, 37, 12, 9, -19, -6.5, 0},
    {74, 14, 74, 14, 8, -15, 3, 1},
    {-84, -34, -80, -12, 10, -7, 6, 2}, {-78, -6, -83, 12, 12, -13, 5, 2}, {-82, 18, -79, 32, 9, -8, 6, 2},
    {-17, 62, -19, 78, 10, -6, 9, 3}, {-21, 84, -18, 100, 11, -9, 8, 3},
    {22, 67, 24, 80, 7, -11, 6, 2},
    {-16, 122, 34, 122, 9, -27, 3, 2},
};
// vertical ring arches: cx, cy, cz, yaw, major radius, minor radius, y squash
const double ARCHES[][7] = {{-23, -9.5, -16, 0.3, 4.2, 1.4, 1.2}, {0, -45, -34, 1.57, 14, 2.7, 1.2}, {-40, -45, 44, 1.57, 10, 2.2, 1.3}};
// carved tunnels: ax, ay, az, radius, bx, by, bz
const double HOLES[][7] = {{19, -21, -23, 2.5, 19, -21, -7}, {9, -37, 108, 4.2, 9, -34, 136}};
// regions kept free of random mesas: ax, az, bx, bz, radius, softness
const double CLEARS[][6] = {{0, -45, 0, 35, 22, 10}, {0, -86, 0, -86, 46, 12}, {42, 24, 74, 6, 22, 10}, {72, -26, 72, -26, 26, 10},
                            {-60, -36, -60, 36, 13, 8}, {-60, 42, -18, 46, 12, 8}, {4, 58, 8, 100, 24, 10}, {-34, -74, -56, -46, 11, 8}};
const double MAGENTA[][3] = {{14, 6, 13}, {-4, 20, 9}, {6, 84, 30}, {-58, 8, 10}, {50, 30, 12}, {8, -70, 10}, {-12, -98, 9}, {-36, 44, 9},
                             {-46, -60, 9}, {24, 42, 8}, {-56, 26, 9}, {-44, 38, 8}, {-10, 44, 8}, {12, 24, 8}, {-50, -45, 8}};

template <class T, size_t N> constexpr size_t len(const T (&)[N]) { return N; }

double magAt(double x, double z) {
    double m = sstep(0.54, 0.66, fbm(x * 0.03, 7, z * 0.03, 3)) * 0.9;
    for (const auto& p : MAGENTA) m = max(m, sstep(p[2], p[2] * 0.45, hyp(x - p[0], z - p[1])));
    return m * sstep(0.3, 0.55, fbm(x * 0.17, 1, z * 0.17, 2) + m * 0.25);
}

int cellOf(double x, double z) {
    return int(clampd(floor((x - OX) / CELLW), 0, CELLS - 1) + CELLS * clampd(floor((z - OZ) / CELLW), 0, CELLS - 1));
}

} // namespace

double zoneP(double x, double z) { return sstep(44, 26, hyp(x - PURPLE[0], z - PURPLE[1])); }

std::vector<float> primBuffer() {
    std::vector<float> a;
    for (const auto& m : MESAS) for (int j = 0; j < 8; j++) a.push_back(float(m[j]));
    for (const auto& m : ARCHES) { for (int j = 0; j < 7; j++) a.push_back(float(m[j])); a.push_back(0); }
    for (const auto& m : HOLES) { for (int j = 0; j < 7; j++) a.push_back(float(m[j])); a.push_back(0); }
    for (const auto& m : CLEARS) { for (int j = 0; j < 6; j++) a.push_back(float(m[j])); a.push_back(0); a.push_back(0); }
    return a;
}
PrimCounts primCounts() { return {uint32_t(len(MESAS)), uint32_t(len(ARCHES)), uint32_t(len(HOLES)), uint32_t(len(CLEARS))}; }

// ---------------------------------------------------------------- terrain mesh from the density grid
Terrain buildTerrain(const std::vector<float>& D) {
    const NetMesh sn = surfaceNets(D.data(), NX, NY, NZ);
    const uint32_t nv = sn.nv;
    const std::vector<float>& gp = sn.pos;
    Terrain T;
    T.smp = Sampler3{D.data(), NX, NY, NZ};
    const Sampler3& smp = T.smp;
    T.vb.assign(size_t(nv) * 16, 0);
    T.wp.resize(size_t(nv) * 3); T.wn.resize(size_t(nv) * 3);
    for (uint32_t k = 0; k < nv; k++) {
        const double x = gp[k * 3], y = gp[k * 3 + 1], z = gp[k * 3 + 2], e = 0.8;
        double nx = smp(x - e, y, z) - smp(x + e, y, z), ny = smp(x, y - e, z) - smp(x, y + e, z), nz = smp(x, y, z - e) - smp(x, y, z + e);
        double l = hyp(nx, ny, nz); if (l == 0) l = 1;
        nx /= l; ny /= l; nz /= l;
        const double X = OX + x * VS, Y = OY + y * VS, Z = OZ + z * VS;
        float* vf = reinterpret_cast<float*>(T.vb.data() + size_t(k) * 16);
        vf[0] = T.wp[k * 3] = float(X); vf[1] = T.wp[k * 3 + 1] = float(Y); vf[2] = T.wp[k * 3 + 2] = float(Z);
        T.wn[k * 3] = float(nx); T.wn[k * 3 + 1] = float(ny); T.wn[k * 3 + 2] = float(nz);
        int8_t* v8 = reinterpret_cast<int8_t*>(T.vb.data() + size_t(k) * 16 + 12);
        v8[0] = int8_t(jsround(nx * 127)); v8[1] = int8_t(jsround(ny * 127)); v8[2] = int8_t(jsround(nz * 127)); v8[3] = 127;
    }
    // bucket quads into the XZ culling grid
    const size_t nq = sn.ni / 6;
    std::vector<uint8_t> key(nq);
    std::vector<uint32_t> cnt(CELLS * CELLS + 1, 0);
    T.lo.assign(CELLS * CELLS, 1e9f); T.hi.assign(CELLS * CELLS, -1e9f);
    for (size_t q = 0; q < nq; q++) {
        const size_t a = size_t(sn.idx[q * 6]) * 3;
        const int c = cellOf(T.wp[a], T.wp[a + 2]);
        key[q] = uint8_t(c); cnt[c + 1]++;
        for (int j = 0; j < 6; j++) {
            const float y = T.wp[size_t(sn.idx[q * 6 + j]) * 3 + 1];
            if (y < T.lo[c]) T.lo[c] = y;
            if (y > T.hi[c]) T.hi[c] = y;
        }
    }
    for (int c = 0; c < CELLS * CELLS; c++) cnt[c + 1] += cnt[c];
    T.ib.resize(sn.ni);
    std::vector<uint32_t> fill = cnt;
    for (size_t q = 0; q < nq; q++) { const size_t o = size_t(fill[key[q]]++) * 6; for (int j = 0; j < 6; j++) T.ib[o + j] = sn.idx[q * 6 + j]; }
    T.nv = nv;
    T.start = cnt;
    return T;
}

// ---------------------------------------------------------------- instance stores
World makeWorld(const std::vector<float>& D, const Terrain& T, const std::vector<V3>& flyPts, std::vector<Pose>& POSES) {
    const auto gi = [](size_t x, size_t y, size_t z) { return x + size_t(NX) * (y + size_t(NY) * z); };
    const Sampler3& smp = T.smp;
    const auto Dw = [&](double x, double y, double z) { return smp((x - OX) / VS, (y - OY) / VS, (z - OZ) / VS); };
    constexpr int nCells = CELLS * CELLS;
    using Cells = std::vector<std::vector<double>>;
    Cells big(nCells), small(nCells);
    int nth = 0;
    const auto leaf = [&](Cells& L, double x, double y, double z, double len, double yaw, double tilt, double wid, double hA, int type, double s1, double s2, double bend) {
        if (type == 1 || !(nth++ % THIN)) {
            auto& c = L[cellOf(x, z)];
            c.insert(c.end(), {x, y, z, len, yaw, tilt, type == 1 ? wid : wid * (1 + LV / 2.0), hA, double(type), s1, s2, bend});
        }
    };
    std::vector<double> glints;
    const auto glint = [&](std::initializer_list<double> a) { if (!(nth++ % THIN)) glints.insert(glints.end(), a); };
    std::array<std::vector<double>, 12> solids;
    std::array<Cells, 6> props; for (auto& p : props) p.assign(nCells, {});
    const auto solid = [&](int mesh, double x, double y, double z, double sc, const Quat& q, double mat, double seed, double ts = 1, double bob = 0) {
        auto& v = mesh > 3 && mesh < 10 ? props[mesh - 4][cellOf(x, z)] : solids[mesh];
        v.insert(v.end(), {x, y, z, sc, q.x, q.y, q.z, q.w, mat, seed, ts, bob});
    };
    std::array<Cells, NCHUNK> chunks; for (auto& c : chunks) c.assign(nCells, {});
    const auto rel = [&](int i, double f, double r) -> std::array<double, 2> {
        const Pose& p = POSES[i];
        double fx = p[3] - p[0], fz = p[5] - p[2];
        const double l = hyp(fx, fz); fx /= l; fz /= l;
        return {p[0] + fx * f - fz * r, p[2] + fz * f + fx * r};
    };
    std::vector<double> bubbles;
    std::vector<float> kelpAbs(size_t(NX / 2) * (NY / 2) * (NZ / 2), 0.0f);

    // --- surface queries
    std::vector<double> fl;
    const auto floorsAt = [&](double x, double z) -> const std::vector<double>& {
        const int ix = int(clampd(jsround((x - OX) / VS), 1, NX - 2)), iz = int(clampd(jsround((z - OZ) / VS), 1, NZ - 2));
        fl.clear();
        double ceil = 3;
        for (int gy = NY - 2; gy >= 0; gy--) {
            const double d = D[gi(ix, gy, iz)], da = D[gi(ix, gy + 1, iz)];
            if (da <= 0 && d > 0) { const double y = OY + (gy + d / (d - da)) * VS; fl.push_back(y); fl.push_back(min(ceil, 0.0) - y); }
            else if (da > 0 && d <= 0) ceil = OY + (gy + 0.5) * VS;
        }
        return fl;
    };
    const auto grad = [&](double x, double y, double z) -> V3 {
        const double e = 0.6;
        const double nx = Dw(x - e, y, z) - Dw(x + e, y, z), ny = Dw(x, y - e, z) - Dw(x, y + e, z), nz = Dw(x, y, z - e) - Dw(x, y, z + e);
        double l = hyp(nx, ny, nz); if (l == 0) l = 1;
        return {nx / l, ny / l, nz / l};
    };
    // march from outside along -n to the surface: position + normal
    const auto snap = [&](double x, double y, double z, const V3& n, std::array<double, 6>& out) -> bool {
        double px = x + n[0] * 2.5, py = y + n[1] * 2.5, pz = z + n[2] * 2.5, prev = Dw(px, py, pz);
        if (prev > 0) return false;
        for (int i = 0; i < 24; i++) {
            const double qx = px - n[0] * 0.25, qy = py - n[1] * 0.25, qz = pz - n[2] * 0.25, d = Dw(qx, qy, qz);
            if (d > 0) {
                const double t = prev / (prev - d);
                const V3 p{mix(px, qx, t), mix(py, qy, t), mix(pz, qz, t)};
                const V3 g = grad(p[0], p[1], p[2]);
                out = {p[0], p[1], p[2], g[0], g[1], g[2]};
                return true;
            }
            px = qx; py = qy; pz = qz; prev = d;
        }
        return false;
    };
    const auto pickV = [&]() { return size_t(toI32(rnd() * T.nv)) * 3; };
    const auto tangent = [](const V3& n, V3& t, V3& b) {
        t = abs(n[1]) < 0.9 ? V3{n[2], 0, -n[0]} : V3{1, 0, 0};
        const double l = hyp(t[0], t[1], t[2]);
        for (auto& v : t) v /= l;
        b = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]};
    };
    const auto nearFly = [&](double x, double y, double z, double r) {
        for (const V3& p : flyPts) if (hyp(x - p[0], z - p[2]) < r && y < p[1] + 3 && y > p[1] - 13) return true;
        return false;
    };
    const auto nearPose = [&](double x, double z, double r = 3) {
        for (const Pose& p : POSES) if (hyp(x - p[0], z - p[2]) < r) return true;
        return false;
    };
    // --- kelp columns
    const auto column = [&](double x, double y, double z, double H, int kind, double dens) {
        const double r1 = rnd(), r2 = rnd();
        const int strands = 2 + (r1 < 0.6) + (r2 < 0.3);
        const double ph = rnd() * TAU, tw = rr(0.1, 0.28), sr = rr(0.2, 0.55), sc = rr(0.75, 1.25);
        const double la = rnd() * TAU, lm = rr(0, 0.11), full = rr(0.55, 1.3) * dens;
        const auto stem = [&](int s, double h) -> V3 {
            const double a = ph + s * TAU / strands + h * tw, r = sr * (1 - 0.6 * h / H), ln = lm * h * h / H;
            return {x + cos(a) * r + cos(la) * ln, z + sin(a) * r + sin(la) * ln, a};
        };
        for (int s = 0; s < strands; s++)
            for (double h = 0; h < H - 1; h += 1.7) {
                const V3 p = stem(s, h);
                const double s1 = rnd(), s2 = rnd();
                leaf(big, p[0], y + h, p[1], 2.1, p[2] + PI / 2, 0.1, 0.17, h, 1, s1, s2, 0.3);
            }
        const double n = floor(H * 30 * full);
        for (double i = 0; i < n; i++) {
            const double t = pow(rnd(), 0.9), h = t * H;
            const V3 p = stem(toI32(rnd() * strands), h);
            const double ra = rnd(), rb = rnd();
            const double L = ((0.55 + 0.75 * ra * rb) * (1 - 0.5 * t * t) * sc + 0.25) * (kind == 1 ? 1.25 : 1.15);
            const double ta = rr(0.4, 1.0), tb = rr(0.15, 0.45);
            const double tilt = mix(ta, tb, t);
            const int type = kind == 1 ? (rnd() < 0.8 ? 3 : 4) : kind == 2 ? (rnd() < 0.5 ? 4 : 0) : 0;
            const double yaw = rnd() * TAU;
            const double wid = L * (kind == 1 ? rr(0.09, 0.13) : rr(0.15, 0.21)) + 0.03;
            const double s1 = rnd(), s2 = rnd(), bend = rr(0.38, 0.95);
            leaf(big, p[0], y + h, p[1], L, yaw, tilt, wid, h, type, s1, s2, bend);
        }
        for (double i = floor(H * (kind == 1 ? 16 : 13)); i > 0; i--) {
            const double h = rnd() * H, a = rnd() * TAU, r = rr(0.15, 0.8) * (1 - 0.5 * h / H), ln = lm * h * h / H;
            const double g1 = rnd(), g2 = kind == 1 ? rnd() : rnd() * 0.6, g3 = rr(0.018, 0.05);
            glint({x + cos(a) * r + cos(la) * ln, y + h, z + sin(a) * r + sin(la) * ln, h, g1, g2, g3, 0});
        }
        // light absorption footprint for the skylight bake
        const double hx = jsround((x - OX) / VS / 2), hz = jsround((z - OZ) / VS / 2);
        for (double hy = floor((y - OY) / VS / 2); hy <= floor((y + H - OY) / VS / 2); hy++)
            if (hy >= 0 && hy < NY / 2 && hx > 0 && hx < NX / 2 - 1 && hz > 0 && hz < NZ / 2 - 1) {
                float& k = kelpAbs[size_t(hx) + size_t(NX / 2) * (size_t(hy) + size_t(NY / 2) * size_t(hz))];
                k = float(k + 0.17);
            }
    };
    const double KELPZ[][4] = {{-32, -5, 13, 0.85}, {0, -86, 46, 1}, {72, -26, 27, 0.9}, {-60, -4, 11, 0.8}, {0, -36, 12, 0.55}, {-17, -8, 6, 0.8}, {15, -8, 6, 0.8}, {-4, 120, 20, 0.4}, {-45, -63, 15, 0.9}, {62, -26, 12, 0.9}, {-100, 70, 30, 0.8}, {100, 60, 30, 0.8}, {-95, -90, 32, 0.8}, {105, -85, 30, 0.7}};
    const double NOKELP[][3] = {{-5, -106.5, 8}, {4.6, -109.4, 10}, {14.2, -112.3, 12}, {-26, -100, 6}, {-20.5, -101.7, 4.5}, {-14.8, -104.5, 6}, {-7.5, -107.8, 7}, {4, 112, 11}, {2, 4, 15}, {42, 22, 12}, {6, 78, 19}, {-58, -16, 8}, {9, -66, 9}, {8, -76, 8}};
    const auto kelpDens = [&](double x, double z) {
        for (const auto& p : NOKELP) if (hyp(x - p[0], z - p[1]) < p[2]) return 0.0;
        double d = sstep(0.52, 0.64, fbm(x * 0.022, 4, z * 0.022, 3)) * 0.5;
        for (const auto& p : KELPZ) d = max(d, p[3] * sstep(p[2], p[2] * 0.75, hyp(x - p[0], z - p[1])));
        return d;
    };
    std::unordered_set<int64_t> occ;
    rseed(1);
    const auto tryColumn = [&](double x, double z, int kind, bool force, double dens) {
        const int64_t key = int64_t(floor(x / 2.3)) * 4096 + int64_t(floor(z / 2.3));
        if (occ.count(key)) return;
        const std::vector<double> f = floorsAt(x, z);
        std::vector<size_t> c;
        for (size_t i = 0; i < f.size(); i += 2) if (f[i + 1] > 9) c.push_back(i);
        if (c.empty()) return;
        const size_t i = c[size_t(toI32(rnd() * double(c.size())))];
        const double y = f[i];
        if (grad(x, y, z)[1] < 0.55) return;
        if (!force) for (const V3& p : flyPts) if (hyp(x - p[0], z - p[2]) < 5) return;
        occ.insert(key);
        const double a = f[i + 1] - rr(0.8, 5);
        const double b = force ? rr(30, 42) : rr(15, 40);
        column(x, y - 0.3, z, min(a, b), kind, dens);
    };
    // hand-placed columns framing the viewpoints: x, z, kind, density
    {
        struct H { int pose; double f, r; int kind; double dens; };
        const H hand[] = {
            {0, 11, -8.5, 0, 1}, {0, 16, -10.5, 0, 1}, {0, 21, -9, 0, 1}, {0, 9, 7, 0, 1}, {0, 14, 9.5, 0, 1}, {0, 19, 8, 0, 1}, {0, 26, 6, 0, 1},
            {5, 6.5, 5.2, 1, 1}, {5, 12, 7, 1, 1}, {5, 15, -8, 0, 1}, {5, 22, -5, 1, 1}, {3, 12, -5.5, 0, 1}, {3, 16, 5.5, 0, 1}, {3, 20, -4.5, 0, 1}, {3, 25, 6, 0, 1}, {3, 13, 8, 0, 1}, {3, 30, -7, 0, 1},
        };
        for (const H& h : hand) { const auto p = rel(h.pose, h.f, h.r); tryColumn(p[0], p[1], h.kind, true, h.dens); }
        const double fixed[][3] = {{24, 62, 1}, {-6, 96, 1}, {20, 96, 2}, {-10, 70, 0}, {16, 52, 1}};
        for (const auto& p : fixed) tryColumn(p[0], p[1], int(p[2]), true, 1);
        const H hand2[] = {
            {4, 15, -9, 0, 1.2}, {4, 19, -13, 0, 1.2}, {4, 23, -8.5, 0, 1.2}, {4, 25, -15, 0, 1.2},
            {4, 25, 0.5, 2, 1.2}, {4, 27, -3, 2, 1.2}, {4, 28, 3.5, 2, 1.2}, {4, 31, 0, 0, 1.3}, {4, 33, -5, 0, 1.3}, {4, 33, 5, 0, 1.3}, {4, 21, 9, 0, 1.2}, {4, 27, 11, 0, 1.2},
            {2, 6.5, -5.5, 0, 1.5}, {2, 9.5, -8, 0, 1.3}, {2, 8, 6, 0, 1.4}, {2, 12, 3.6, 0, 1.3}, {2, 17, -2.5, 0, 1.2}, {2, 21, 4, 0, 1.2}, {2, 12, -4.5, 2, 1.2}, {2, 27, -9, 0, 1.2}, {2, 30, 8.5, 0, 1.2}, {2, 37, -3, 0, 1.1},
            {2, 13.5, 6.5, 0, 1.3}, {2, 14.5, -7, 0, 1.3}, {2, 6.5, 6.5, 0, 1.3}, {2, 9, -8, 2, 1.3},
        };
        for (const H& h : hand2) { const auto p = rel(h.pose, h.f, h.r); tryColumn(p[0], p[1], h.kind, true, h.dens); }
    }
    // random columns across the world
    rseed(2);
    for (int i = 0; i < 15000; i++) {
        const double a = rnd() * TAU, r = sqrt(rnd()) * 170, x = cos(a) * r, z = sin(a) * r;
        if (rnd() < kelpDens(x, z)) {
            int kind;
            if (zoneP(x, z) > 0.5 && rnd() < 0.5) kind = 1;
            else kind = rnd() < 0.03 ? 2 : 0;
            tryColumn(x, z, kind, false, 1);
        }
    }

    // --- clusters of ribbon leaves on the ground
    const auto cluster = [&](double x, double y, double z, int n, int type, double lmin, double lmax, double wmin, double wmax, double tmax, double sway) {
        for (int i = 0; i < n; i++) {
            const double a = rnd() * TAU, r = rnd() * 0.5, L = rr(lmin, lmax);
            const double tilt = rr(0.05, tmax), wid = rr(wmin, wmax), s1 = rnd(), s2 = rnd(), bend = rr(0.3, 0.8);
            leaf(big, x + cos(a) * r, y - 0.15, z + sin(a) * r, L, a, tilt, wid, sway, type, s1, s2, bend);
        }
    };
    const double HOT[][3] = {{0, 5, 30}, {42, 30, 24}, {6, 82, 34}, {-58, 10, 24}, {4, -88, 30}, {-36, 44, 18}, {-46, -60, 20}};
    const auto hot = [&](double x, double z) { double h = 0.4; for (const auto& p : HOT) h = max(h, sstep(p[2], p[2] * 0.4, hyp(x - p[0], z - p[1]))); return h; };
    rseed(3);
    for (int i = 0; i < 40000; i++) {
        const size_t k = pickV();
        const double x = T.wp[k], y = T.wp[k + 1], z = T.wp[k + 2], ny = T.wn[k + 1];
        if (hyp(x, z) > 172 || y > -6) continue;
        const double h = hot(x, z), zp = zoneP(x, z), u = rnd();
        if (nearFly(x, y, z, 4) || hyp(x + 1, z - 2) < 9 || nearPose(x, z, 8)) continue;
        if (ny > 0.75 && u < 0.10 * h) { const int n = toI32(8 + rnd() * 14); cluster(x, y, z, n, 2, 1.0, 3.0, 0.13, 0.26, 0.6, 2.5); }
        else if (ny > 0.6 && u < 0.10 * h + 0.07 * max(zp, h * 0.4)) { const int n = toI32(10 + rnd() * 18); const int type = rnd() < 0.8 ? 3 : 4; cluster(x, y, z, n, type, 0.9, 2.6, 0.14, 0.28, 0.9, 1.5); }
        else if (ny < -0.35 && u < 0.5 * max(zp, 0.2)) { // hanging fronds
            const int n = toI32(12 + rnd() * 20);
            for (int j = 0; j < n; j++) {
                const double ox = rr(-0.6, 0.6), oz = rr(-0.6, 0.6), L = rr(0.8, 3.2), yaw = rnd() * TAU, tilt = PI - rr(0, 0.4), wid = rr(0.08, 0.18);
                const int type = zp > 0.3 || rnd() < 0.5 ? 6 : 0;
                const double s1 = rnd(), s2 = rnd(), bend = rr(0.3, 0.6);
                leaf(big, x + ox, y + 0.2, z + oz, L, yaw, tilt, wid, 0, type, s1, s2, bend);
            }
        } else if (ny > -0.2 && ny < 0.8 && u < 0.07) { // wall tufts
            const double yaw0 = std::atan2(double(T.wn[k + 2]), double(T.wn[k]));
            const int n = toI32(5 + rnd() * 7);
            for (int j = 0; j < n; j++) {
                const double L = rr(0.5, 1.4), yaw = yaw0 + rr(-0.9, 0.9), tilt = rr(0.3, 1.1), wid = rr(0.06, 0.12);
                const int type = zp > 0.4 && rnd() < 0.6 ? 3 : 0;
                const double s1 = rnd(), s2 = rnd(), bend = rr(0.2, 0.6);
                leaf(big, x, y, z, L, yaw, tilt, wid, 0.5, type, s1, s2, bend);
            }
        }
    }
    // --- ground cover: dense carpets of small tufts in the magenta patches
    rseed(4);
    for (const auto& p : MAGENTA)
        for (double i = p[2] * p[2] * PI * 45; i > 0; i--) {
            const double a = rnd() * TAU, r = sqrt(rnd()) * p[2], x = p[0] + cos(a) * r, z = p[1] + sin(a) * r;
            if (rnd() > magAt(x, z) + 0.12 || nearPose(x, z)) continue;
            const auto& f = floorsAt(x, z);
            if (f.empty()) continue;
            const double y = f[f.size() - 2];
            if (grad(x, y, z)[1] < 0.7) continue;
            const int nl = 2 + toI32(rnd() * 3);
            const double yw = rnd() * TAU;
            for (int j = 0; j < nl; j++) {
                const double L = rr(0.08, 0.26), yaw = yw + j * TAU / nl + rr(-0.4, 0.4), tilt = rr(0.15, 0.85), wid = rr(0.035, 0.07);
                const double s1 = rnd(), s2 = rnd(), bend = rr(0.3, 0.7);
                leaf(small, x, y - 0.03, z, L, yaw, tilt, wid, 0, 5, s1, s2, bend);
            }
            if (rnd() < 0.3)
                for (int j = 0; j < 2; j++) {
                    const double ox = rr(-0.2, 0.2), oz = rr(-0.2, 0.2), L = rr(0.2, 0.55), yaw = rnd() * TAU, tilt = rr(0.05, 0.6), wid = rr(0.03, 0.06);
                    const double s1 = rnd(), s2 = rnd(), bend = rr(0.3, 0.8);
                    leaf(small, x + ox, y - 0.04, z + oz, L, yaw, tilt, wid, 0, 7, s1, s2, bend);
                }
            if (rnd() < 0.3) {
                const double gy = y + rr(0.08, 0.22), g1 = rnd(), g2 = 0.82 + rnd() * 0.18, g3 = rr(0.01, 0.02);
                glint({x, gy, z, 0, g1, g2, g3, 0});
            }
        }
    rseed(5);
    for (int i = 0; i < 620000; i++) {
        const size_t k = pickV();
        const double ny = T.wn[k + 1];
        if (ny < 0.72) continue;
        const double x = T.wp[k] + rr(-0.45, 0.45), z = T.wp[k + 2] + rr(-0.45, 0.45), y = T.wp[k + 1];
        if (hyp(x, z) > 170 || nearPose(x, z)) continue;
        const double m = magAt(x, z), zp = zoneP(x, z), u = rnd();
        if (m > 0.3 && u < m * 0.9) {
            for (int j = 0; j < 4; j++) {
                const double ox = rr(-0.1, 0.1), oz = rr(-0.1, 0.1), L = rr(0.12, 0.34), yaw = rnd() * TAU, tilt = rr(0.05, 0.75), wid = rr(0.04, 0.08);
                const double s1 = rnd(), s2 = rnd(), bend = rr(0.3, 0.7);
                leaf(small, x + ox, y - 0.05, z + oz, L, yaw, tilt, wid, 0, 5, s1, s2, bend);
            }
        } else if (u < m * 0.9 + 0.5 * max(zp, hot(x, z)) * sstep(0.42, 0.6, fbm(x * 0.09, 9, z * 0.09, 2))) {
            for (int j = 0; j < 3; j++) {
                const double ox = rr(-0.1, 0.1), oz = rr(-0.1, 0.1), L = rr(0.25, 0.7), yaw = rnd() * TAU, tilt = rr(0.1, 0.8), wid = rr(0.05, 0.1);
                const double s1 = rnd(), s2 = rnd(), bend = rr(0.3, 0.8);
                leaf(small, x + ox, y - 0.05, z + oz, L, yaw, tilt, wid, 0, 7, s1, s2, bend);
            }
        }
    }

    // --- sponges and coral (clustered on rock)
    std::vector<V3> spots;
    const double ZS[][4] = {{-58, 8, 30, 1}, {0, 0, 30, 0.7}, {6, 84, 36, 0.9}, {4, -88, 22, 0.6}, {44, 20, 24, 0.5}};
    const auto zw = [&](double x, double z) { double h = 0.45; for (const auto& p : ZS) h = max(h, p[3] * sstep(p[2], p[2] * 0.4, hyp(x - p[0], z - p[1]))); return h; };
    rseed(6);
    for (int i = 0; i < 13000; i++) {
        const size_t k = pickV();
        const double x = T.wp[k], y = T.wp[k + 1], z = T.wp[k + 2];
        if (hyp(x, z) > 170 || y > -8 || T.wn[k + 1] < -0.15 || rnd() > zw(x, z) || nearFly(x, y, z, 3)) continue;
        const V3 n0{T.wn[k], T.wn[k + 1], T.wn[k + 2]};
        V3 t1, t2; tangent(n0, t1, t2);
        const bool coral = T.wn[k + 1] > 0.85 || rnd() < 0.35;
        const int cnt = coral ? 2 + toI32(rnd() * 5) : 3 + toI32(rnd() * 9);
        const double sd = rnd();
        for (int j = 0; j < cnt; j++) {
            const double a = rr(-1, 1) * (0.5 + cnt * 0.13), b = rr(-1, 1) * (0.5 + cnt * 0.13);
            std::array<double, 6> s;
            if (!snap(x + t1[0] * a + t2[0] * b, y + t1[1] * a + t2[1] * b, z + t1[2] * a + t2[2] * b, n0, s)) continue;
            double ux = s[3], uy = s[4] + (coral ? 0.9 : 1.1), uz = s[5];
            const double l = hyp(ux, uy, uz); ux /= l; uy /= l; uz /= l;
            const Quat q = qMul(qFromY(ux, uy, uz), qAxis(0, 1, 0, rnd() * TAU));
            double sc;
            if (coral) sc = rr(0.5, 1.5);
            else { const double b0 = rr(0.35, 0.9); sc = b0 * (rnd() < 0.1 ? 1.6 : 1); }
            const int mesh = coral ? 7 + toI32(rnd() * 3) : 4 + toI32(rnd() * 3);
            const double seed = fract(sd + rr(-0.03, 0.03));
            solid(mesh, s[0] - ux * 0.12 * sc, s[1] - uy * 0.12 * sc, s[2] - uz * 0.12 * sc, sc, q, coral ? 5 : 4, seed, 1, 0);
        }
        if (!coral && rnd() < 0.3) spots.push_back({x + n0[0], y + n0[1] + 0.5, z + n0[2]});
    }

    // dense sponge / coral gardens: x, z, radius, count
    rseed(7);
    const double GARDENS[][4] = {{-68, -6, 9, 260}, {-66, 6, 7, 160}, {16, 72, 7, 200}, {-6, 74, 6, 120}, {-13, -2, 6, 80}, {8, -88, 8, 160}, {44, 12, 7, 120}, {-62, 22, 6, 140}, {-47, 40, 6, 140}, {-30, 47, 6, 120}, {-12, 48, 5, 100}, {14, 28, 6, 140}, {28, 45, 6, 120}, {-36, -76, 6, 130}, {-49, -58, 6, 130}, {-58, -38, 6, 120}};
    for (const auto& g : GARDENS) {
        int made = 0;
        for (int i = 0; i < 400000 && made < g[3]; i++) {
            const size_t k = pickV();
            const double x = T.wp[k], y = T.wp[k + 1], z = T.wp[k + 2];
            if (hyp(x - g[0], z - g[1]) > g[2] || y > -14 || T.wn[k + 1] < -0.1 || nearFly(x, y, z, 2.2)) continue;
            made++;
            const bool coral = T.wn[k + 1] > 0.85 || rnd() < 0.3;
            const double nx = T.wn[k], ny = T.wn[k + 1] + (coral ? 0.9 : 1.1), nz = T.wn[k + 2], l = hyp(nx, ny, nz);
            double sc;
            if (coral) sc = rr(0.5, 1.4);
            else { const double b0 = rr(0.35, 0.9); sc = b0 * (rnd() < 0.15 ? 1.7 : 1); }
            const int mesh = coral ? 7 + toI32(rnd() * 3) : 4 + toI32(rnd() * 3);
            const Quat q = qMul(qFromY(nx / l, ny / l, nz / l), qAxis(0, 1, 0, rnd() * TAU));
            const double seed = rnd();
            solid(mesh, x - nx / l * 0.1 * sc, y - ny / l * 0.1 * sc, z - nz / l * 0.1 * sc, sc, q, coral ? 5 : 4, seed, 1, 0);
        }
    }
    // --- rocks: floating boulder clusters + scattered ground rocks
    std::vector<std::array<double, 5>> BOULD = {{-11, -11, -105, 14, 5}, {-56, -19, -18, 18, 6}, {10, -13, -88, 22, 7}, {24, -15, -24, 22, 7}, {-18, -12, 4, 14, 6}, {30, -14, 30, 16, 6}, {36, -12, -70, 22, 8}, {-30, -13, -84, 16, 7}, {-58, -14, 0, 14, 6}, {19, -13, 98, 18, 6}, {62, -9, 34, 14, 6}};
    rseed(8);
    for (int i = 0; i < 9; i++) {
        const double a = rnd() * TAU, r = rr(60, 150);
        const double y = rr(-20, -9), n = toI32(10 + rnd() * 12), rad = rr(5, 9);
        BOULD.push_back({cos(a) * r, y, sin(a) * r, n, rad});
    }
    for (const auto& b : BOULD)
        for (int i = 0; i < b[3]; i++) {
            const double sc = rnd() < 0.12 ? rr(1.8, 3.2) : rr(0.35, 1.5);
            const double x = b[0] + rr(-1, 1) * b[4], y = b[1] + rr(-1, 1) * b[4] * 0.6, z = b[2] + rr(-1, 1) * b[4];
            if (Dw(x, y, z) > -sc) continue;
            const double bob = rr(0.12, 0.4), sd = rnd();
            { const int mesh = toI32(rnd() * 4); const Quat q = qRand(); solid(mesh, x, min(y, -2 - sc), z, sc, q, 1, sd, 1, bob); }
            if (sc > 1.1)
                for (int j = 0; j < 7; j++) {
                    const int mesh = toI32(rnd() * 4);
                    const double px = x + rr(-1.6, 1.6) * sc, py = min(y, -2 - sc) + rr(-1.4, 1) * sc, pz = z + rr(-1.6, 1.6) * sc, s = rr(0.1, 0.35);
                    const Quat q = qRand();
                    solid(mesh, px, py, pz, s, q, 1, sd, 1, bob);
                }
        }
    rseed(9);
    for (int i = 0; i < 2600; i++) {
        const size_t k = pickV();
        if (T.wn[k + 1] < 0.7 || hyp(T.wp[k], T.wp[k + 2]) > 170 || nearFly(T.wp[k], T.wp[k + 1], T.wp[k + 2], 6)) continue;
        const double b0 = rr(0.25, 1.3), sc = b0 * (rnd() < 0.1 ? 2 : 1);
        const int mesh = toI32(rnd() * 4);
        const Quat q = qRand();
        const double seed = rnd();
        solid(mesh, T.wp[k], T.wp[k + 1] + sc * 0.2, T.wp[k + 2], sc, q, 1, seed, 1, 0);
    }

    rseed(10);
    for (int i = 0; i < 60000; i++) {
        const size_t k = pickV();
        const double x = T.wp[k], y = T.wp[k + 1], z = T.wp[k + 2], ny = T.wn[k + 1];
        if (hyp(x, z) > 172 || y > -3 || (ny > 0.8 && rnd() < 0.6) || nearFly(x, y, z, 3.5)) continue;
        const double sc = (0.35 + 2.1 * pow(rnd(), 2.6)) * (ny > 0.8 ? 0.5 : 1);
        const Quat q = qRand();
        const int ci = (sc > 1.15 ? 3 : 0) + toI32(rnd() * 3);
        const double seed = rnd();
        auto& c = chunks[ci][cellOf(x, z)];
        c.insert(c.end(), {x - T.wn[k] * sc * 0.3, y - ny * sc * 0.3, z - T.wn[k + 2] * sc * 0.3, sc, q.x, q.y, q.z, q.w, 1, seed, sc > 1.15 ? 0.0 : 96.0, 0});
    }
    rseed(11);
    for (int i = 0; i < 170000; i++) { // pebbles and rubble on the sea floor
        const size_t k = pickV();
        const double x = T.wp[k] + rr(-0.4, 0.4), z = T.wp[k + 2] + rr(-0.4, 0.4), y = T.wp[k + 1];
        if (T.wn[k + 1] < 0.8 || hyp(x, z) > 170 || nearFly(x, y, z, 1.3)) continue;
        const double sc = 0.05 + 0.3 * pow(rnd(), 2);
        const Quat q = qRand();
        if (!(nth++ % THIN)) {
            const double seed = rnd();
            auto& c = chunks[6][cellOf(x, z)];
            c.insert(c.end(), {x, y - sc * 0.25, z, sc, q.x, q.y, q.z, q.w, 1, seed, 44, 0});
        }
    }
    // --- set pieces
    const auto gy = [&](double x, double z) { const auto& f = floorsAt(x, z); return f.empty() ? -45.0 : f[f.size() - 2]; };
    solid(10, -5, gy(-5, -4) - 0.6, -4, 1, qMul(qAxis(0, 1, 0, 1.2), qMul(qAxis(0, 0, 1, 0.16), qAxis(1, 0, 0, -0.3))), 2, 0.3, 1, 0);          // whale fluke
    solid(11, 59.5, -23, 6, 1, qMul(qAxis(0, 1, 0, -PI / 2), qMul(qAxis(0, 0, 1, -0.1), qAxis(1, 0, 0, 0.32))), 3, 0.5, 1, 0); // ship hull
    // barrel sponges in front of viewpoint 4, small sponges around the fluke base
    rseed(12);
    const double BARREL[][4] = {{7, 3.2, 1.3, 4}, {8.4, 4.6, 1.0, 6}, {9.6, 2.6, 1.5, 4}, {11, 5, 1.1, 5}, {8, -3.6, 1.2, 4}, {12.5, 2, 1.2, 6}, {9, 3.9, 0.8, 4}};
    for (const auto& b : BARREL) {
        const auto p = rel(3, b[0], b[1]);
        const Pose& c = POSES[3];
        const double ux = (c[0] - p[0]) * 0.07, uz = (c[2] - p[1]) * 0.07, l = hyp(ux, 1, uz);
        solid(int(b[3]), p[0], gy(p[0], p[1]) - 0.1, p[1], b[2], qFromY(ux / l, 1 / l, uz / l), 4, rnd(), 1, 0);
    }
    for (int i = 0; i < 9; i++) {
        const double a = rnd() * TAU;
        const double x = -5 + cos(a) * rr(1.2, 2.6);
        const double z = -4 + sin(a) * rr(1.5, 4);
        const double s = rr(0.4, 0.9);
        const Quat q = qAxis(0, 1, 0, rnd() * 6);
        const double seed = rnd();
        solid(6, x, gy(x, z) + 0.05, z, s, q, 4, seed, 1, 0);
    }

    // --- bubbles
    std::vector<V3> VENTS = {{4, -44, -3}, {4.5, -44, -3.5}, {-1, -44, 2}, {2, -44, -1}, {5.5, -44, -7}, {3, -44, -9}, {6, -44, -90}, {8, -44, -84}, {4, -44, -93}, {-9, -44, 3}, {5, -35, -86}, {9, -37, -92}, {40, -44, 20}, {-56, -44, 12}, {10, -44, 80}, {-2, -44, 30}, {0, -44, -30}, {12, -44, 2}, {6, -44, -2}, {46, -44, 30}, {-20, -44, -103}, {-52, -44, -12}, {4, -44, 70}, {9, -44, -74}, {4, -44, -78}, {14, -44, 26}, {26, -44, 40}, {-45, -44, -64}, {-55, -44, -40}, {-58, -44, 22}, {-40, -44, 40}, {-14, -44, 45}, {-1, -44, -9}, {-7, -44, -6}};
    rseed(13);
    for (const V3& s : spots) if (rnd() < 0.28) VENTS.push_back(s);
    for (const V3& v : VENTS) {
        const int n = toI32(90 + rnd() * 70);
        const double y0 = v[1] == -44 ? gy(v[0], v[2]) : v[1];
        for (int i = 0; i < n; i++) {
            const double sz = rnd() < 0.24 ? rr(0.1, 0.3) : rr(0.025, 0.08);
            const double bx = v[0] + rr(-0.3, 0.3), bz = v[2] + rr(-0.3, 0.3), ph = rnd(), sp = rr(1.1, 2.4) * (0.6 + sz * 3), w = rr(0.05, 0.16);
            bubbles.insert(bubbles.end(), {bx, y0, bz, sz, ph, sp, -y0 - 0.2, w});
        }
    }

    // --- pack leaves (36 bytes: 8 floats + 4 unorm8), sorted by culling cell
    const auto pack = [&](const Cells& L, int step = 1, double wm = 1) {
        CellSet s;
        uint32_t n = 0;
        s.start.assign(nCells + 1, 0);
        for (int c = 0; c < nCells; c++) { s.start[c] = n; n += uint32_t(std::ceil(double(L[c].size()) / 12 / step)); }
        s.start[nCells] = n;
        s.buf.assign(size_t(n) * 36, 0);
        s.lo.assign(nCells, 1e9f); s.hi.assign(nCells, -1e9f);
        size_t k = 0;
        for (int c = 0; c < nCells; c++) {
            const auto& a = L[c];
            for (size_t i = 0; i < a.size(); i += size_t(12) * step, k++) {
                float* f = reinterpret_cast<float*>(s.buf.data() + k * 36);
                for (int j = 0; j < 8; j++) f[j] = float(a[i + j]);
                f[6] = float(double(f[6]) * wm);
                uint8_t* u8 = s.buf.data() + k * 36 + 32;
                u8[0] = toU8(a[i + 8]); u8[1] = toU8(a[i + 9] * 255); u8[2] = toU8(a[i + 10] * 255); u8[3] = toU8(a[i + 11] * 255);
                const double y = a[i + 1], l = a[i + 3];
                if (y - l < s.lo[c]) s.lo[c] = float(y - l);
                if (y + l > s.hi[c]) s.hi[c] = float(y + l);
            }
        }
        s.n = n;
        return s;
    };

    World W;
    // --- half-res volume: r = openness, g = skylight
    {
        const int hx = NX / 2, hy = NY / 2, hz = NZ / 2;
        W.volD.assign(size_t(hx) * hy * hz * 2, 0);
        std::vector<float> Lp(size_t(hx) * hz, 1.0f), Ln(size_t(hx) * hz, 0.0f), tmp(size_t(hx) * hz, 0.0f);
        for (int y = hy - 1; y >= 0; y--) {
            for (int z = 0; z < hz; z++) for (int x = 0; x < hx; x++) {
                const size_t i = size_t(x) + size_t(hx) * z;
                tmp[i] = float((double(Lp[x > 0 ? i - 1 : i]) + 2.0 * Lp[i] + Lp[x < hx - 1 ? i + 1 : i]) * 0.25);
            }
            for (int z = 0; z < hz; z++) for (int x = 0; x < hx; x++) {
                const size_t i = size_t(x) + size_t(hx) * z;
                const double b = (double(tmp[z > 0 ? i - hx : i]) + 2.0 * tmp[i] + tmp[z < hz - 1 ? i + hx : i]) * 0.25;
                const size_t g0 = gi(size_t(x) * 2, size_t(y) * 2, size_t(z) * 2), S = size_t(NX) * NY;
                const double d = (double(D[g0]) + D[g0 + 1] + D[g0 + NX] + D[g0 + NX + 1] + D[g0 + S] + D[g0 + S + 1] + D[g0 + S + NX] + D[g0 + S + NX + 1]) * 0.125;
                const size_t o = size_t(x) + size_t(hx) * (size_t(y) + size_t(hy) * z);
                const double l = b * clampd(0.5 - d / 1.5, 0, 1) * (1 - min(double(kelpAbs[o]), 0.5));
                Ln[i] = float(l);
                W.volD[o * 2] = toU8(clampd(0.5 - d / 12, 0, 1) * 255);
                W.volD[o * 2 + 1] = toU8(min(1.0, b) * 255);
            }
            std::swap(Lp, Ln);
        }
    }
    // --- ground map: r magenta cover, b purple zone, a sand
    W.gm.assign(512 * 512 * 4, 0);
    for (int j = 0; j < 512; j++)
        for (int i = 0; i < 512; i++) {
            const double x = (i + 0.5) / 512 * 384 - 192, z = (j + 0.5) / 512 * 384 - 192;
            const size_t o = (size_t(i) + 512 * size_t(j)) * 4;
            W.gm[o] = toU8(magAt(x, z) * 255); W.gm[o + 2] = toU8(zoneP(x, z) * 255); W.gm[o + 3] = toU8(sstep(0.42, 0.6, fbm(x * 0.04, 3, z * 0.04, 3)) * 255);
        }

    // --- fish schools + mantas
    rseed(14);
    struct School { double x, y, z, r, n, sz, dir, ss; bool hasSs; };
    const School SCH[] = {{-2, -36, -10, 15, 520, 0.3, 1, 0.945, true}, {8, -30, -70, 22, 420, 0.32, -1, 0, false}, {48, -30, 14, 18, 360, 0.3, 1, 0, false}, {-58, -30, -8, 11, 420, 0.28, -1, 0.3, true}, {8, -36, 84, 16, 320, 0.28, 1, 0, false},
                          {-90, -28, -70, 25, 300, 0.3, 1, 0, false}, {95, -30, 70, 25, 300, 0.3, -1, 0, false}, {0, -22, 20, 30, 380, 0.34, -1, 0, false}, {-30, -30, -100, 18, 300, 0.3, 1, 0, false}, {16, -36, 30, 12, 300, 0.3, 1, 0, false}, {-48, -34, -58, 12, 300, 0.3, -1, 0, false}, {-45, -35, 38, 14, 320, 0.3, 1, 0, false}};
    for (const School& s : SCH) {
        const double ss = s.hasSs ? s.ss : rnd();
        for (int i = 0; i < s.n; i++) { const double ph = rnd(); for (double v : {s.x, s.y, s.z, s.r, ph, s.dir * 1.9 / s.r, s.sz, ss}) W.fish.push_back(float(v)); }
    }
    const double MANTAS[][8] = {{1, -28, 3.5, 14, 3.012, 0.085, 3.6, 0.1}, {49, -31, 24.5, 5, 0.029, -0.08, 3.7, 0.4}, {0, -14, -80, 34, 0.3, 0.06, 5.2, 0.7}, {-50, -18, 20, 26, 5, -0.075, 3.8, 0.55}, {10, -16, 86, 26, 2.2, 0.07, 4.4, 0.85}, {20, -11, -20, 46, 4.2, -0.045, 5.6, 0.25}};
    for (const auto& m : MANTAS) for (double v : m) W.mantas.push_back(float(v));

    // --- drifting particles
    W.snow.resize(9000 * 4);
    for (auto& s : W.snow) s = float(rnd());

    uint32_t si = 0;
    for (int m = 0; m < 12; m++) {
        const auto& a = solids[m];
        W.solidRanges[m] = {si, uint32_t(a.size() / 12)};
        si += uint32_t(a.size() / 12);
        for (double v : a) W.solidData.push_back(float(v));
    }
    const auto packC = [&](const Cells& L) {
        CellSet s;
        uint32_t n = 0;
        s.start.assign(nCells + 1, 0);
        for (int c = 0; c < nCells; c++) { s.start[c] = n; n += uint32_t(L[c].size() / 12); }
        s.start[nCells] = n;
        s.data.resize(size_t(n) * 12);
        s.lo.assign(nCells, 1e9f); s.hi.assign(nCells, -1e9f);
        size_t k = 0;
        for (int c = 0; c < nCells; c++) {
            const auto& a = L[c];
            for (size_t i = 0; i < a.size(); i += 12) {
                for (int j = 0; j < 12; j++) s.data[k++] = float(a[i + j]);
                s.lo[c] = float(min(double(s.lo[c]), a[i + 1] - 3));
                s.hi[c] = float(max(double(s.hi[c]), a[i + 1] + 3));
            }
        }
        s.n = n;
        return s;
    };
    for (int i = 0; i < NCHUNK; i++) W.chunks[i] = packC(chunks[i]);
    for (int i = 0; i < 6; i++) W.props[i] = packC(props[i]);
    W.glints.assign(glints.begin(), glints.end());
    W.big = pack(big);
    W.bigF = pack(big, 2, 1.5);
    W.small = pack(small);
    W.bubbles.assign(bubbles.begin(), bubbles.end());
    for (Pose& p : POSES) p[1] += gy(p[0], p[2]);
    return W;
}

// ---------------------------------------------------------------- procedural meshes
namespace {

Mesh meshChunk(int sd, int sub = 2, int detArg = -1) {
    const bool det = detArg < 0 ? sub > 2 : detArg != 0;
    rseed(100 + sd);
    std::array<std::array<double, 4>, 4> pl;
    for (int i = 0; i < 4; i++) {
        const Quat q = qRand();
        pl[i] = {q.x, q.y, q.z, rr(0.8, 0.98)};
        double l = hyp(q.x, q.y, q.z); if (l == 0) l = 1;
        pl[i][0] /= l; pl[i][1] /= l; pl[i][2] /= l;
    }
    return icosphere(sub, [&](double x, double y, double z) -> V3 {
        const double r = 1 + (fbm(x * 1.6 + sd, y * 1.6, z * 1.6, 2) - 0.5) * 0.9 + (det ? (fbm(x * 4 + sd, y * 4, z * 4, 2) - 0.5) * 0.25 : 0);
        double px = x * r, py = y * r * 0.8, pz = z * r;
        for (const auto& p : pl) { const double d = px * p[0] + py * p[1] + pz * p[2] - p[3]; if (d > 0) { px -= p[0] * d; py -= p[1] * d; pz -= p[2] * d; } }
        return {px, py, pz};
    });
}
Mesh meshLathe(const std::vector<V3>& prof, int seg, double lump, double ribs = 0) {
    Mesh m;
    std::vector<std::vector<uint32_t>> rings;
    for (const V3& pr : prof) {
        const double r = pr[0], h = pr[1], ao = pr[2];
        std::vector<uint32_t> ring;
        for (int s = 0; s < seg; s++) {
            const double a = double(s) / seg * TAU;
            const double rl = r * (1 + lump * (fbm(std::cos(a) * 1.5 + h * 2, h * 3, std::sin(a) * 1.5, 2) - 0.5) + 0.07 * std::cos(ribs * a + h * 1.5));
            ring.push_back(m.v(std::cos(a) * rl, h, std::sin(a) * rl, ao));
        }
        rings.push_back(ring);
    }
    for (size_t k = 0; k + 1 < rings.size(); k++)
        for (int s = 0; s < seg; s++) { const int s2 = (s + 1) % seg; m.quad(rings[k][s], rings[k + 1][s], rings[k + 1][s2], rings[k][s2]); }
    return m;
}
V3 nrm(const V3& v) { double l = hyp(v[0], v[1], v[2]); if (l == 0) l = 1; return {v[0] / l, v[1] / l, v[2] / l}; }
Mesh meshCoral(int fan, bool lo = false) {
    rseed(200 + fan);
    Mesh m;
    std::function<void(V3, V3, double, double, int)> br = [&](V3 p, V3 d, double r, double len, int depth) {
        std::vector<V3> pts{p};
        V3 q = p;
        for (int i = 0; i < 3; i++) {
            const double a = rr(-0.3, 0.3), b = rr(-0.1, 0.3), c = fan ? rr(-0.05, 0.05) : rr(-0.3, 0.3);
            d = nrm({d[0] + a, d[1] + b, d[2] + c});
            q = {q[0] + d[0] * len / 3, q[1] + d[1] * len / 3, q[2] + d[2] * len / 3};
            pts.push_back(q);
        }
        if (!depth) pts.push_back({q[0] + d[0] * r * 1.6, q[1] + d[1] * r * 1.6, q[2] + d[2] * r * 1.6});
        if (lo) m.tube(depth ? std::vector<V3>{pts[0], pts[3]} : std::vector<V3>{pts[0], pts[3], pts[4]},
                       [r](int k) { return (k > 1 ? r * 0.36 : r * (1 - 0.22 * k)) * 1.25; }, 3, true,
                       [depth](int k) { return clampd(1 - (depth + 1 - k) / 5.0, 0, 1); });
        else m.tube(pts, [r](int k) { return k > 3 ? r * 0.36 : r * (1 - 0.22 * k / 3); }, fan ? 5 : 6, true,
                    [depth](int k) { return clampd(1 - (depth + 1 - k / 3.0) / 5, 0, 1); });
        if (depth > 0) {
            const int n = 2 + (rnd() < 0.5);
            for (int i = 0; i < n; i++) {
                const double a = rr(-0.8, 0.8), b = rr(-0.1, 0.5), c = fan ? rr(-0.08, 0.08) : rr(-0.8, 0.8);
                br(q, nrm({d[0] + a, d[1] + b, d[2] + c}), r * 0.78, len * 0.8, depth - 1);
            }
        }
    };
    const int n0 = fan ? 1 : 2 + toI32(rnd() * 2);
    for (int i = 0; i < n0; i++) {
        const double px = rr(-0.08, 0.08), pz = rr(-0.08, 0.08);
        const double dx = rr(-0.5, 0.5), dz = fan ? 0 : rr(-0.5, 0.5);
        br({px, 0, pz}, nrm({dx, 1, dz}), fan ? 0.06 : 0.08, fan ? 0.5 : 0.45, 3);
    }
    return m;
}
Mesh meshTubes() {
    rseed(210);
    Mesh m;
    for (int i = 0; i < 9; i++) {
        const double a = rnd() * TAU, r0 = rr(0, 0.28), h = rr(0.45, 1.1), rad = rr(0.07, 0.12), lean = rr(0.05, 0.3);
        std::vector<V3> pts;
        for (int k = 0; k <= 4; k++) pts.push_back({std::cos(a) * (r0 + lean * k / 4 * h), h * k / 4, std::sin(a) * (r0 + lean * k / 4 * h)});
        m.tube(pts, [rad](int k) { return rad * (0.8 + 0.25 * k / 4); }, 12, false, [](int k) { return 0.35 + 0.65 * k / 4; });
        // rim + dark throat
        const V3 e = pts[4];
        const uint32_t n = uint32_t(m.p.size() / 3), c = m.v(e[0], e[1] - rad * 1.2, e[2], 0.05);
        for (uint32_t s2 = 0; s2 < 12; s2++) m.tri(n - 12 + s2, n - 12 + (s2 + 1) % 12, c);
    }
    return m;
}
// stone whale fluke: SDF of tapered capsules -> surface nets
BuiltMesh meshFluke() {
    std::vector<std::array<double, 9>> C;
    const auto chain = [&](const std::vector<std::array<double, 4>>& pts, double zs) {
        for (size_t i = 0; i + 1 < pts.size(); i++) C.push_back({pts[i][0], pts[i][1], pts[i][2], pts[i][3], pts[i + 1][0], pts[i + 1][1], pts[i + 1][2], pts[i + 1][3], zs});
    };
    chain({{0, -1.6, 3.7, 1.2}, {0, 1.8, 2.35, 0.98}, {0, 4.6, 1.0, 0.76}, {0, 6.9, 0.1, 0.6}}, 0.8);
    chain({{0, 6.6, 0.1, 0.72}, {1.9, 7.9, 0, 0.98}, {3.9, 8.8, -0.1, 0.9}, {5.7, 9.8, -0.25, 0.64}, {7.0, 11.2, -0.4, 0.38}, {7.7, 12.8, -0.55, 0.2}, {7.9, 14.1, -0.65, 0.1}}, 0.18);
    chain({{0, 6.6, 0.1, 0.72}, {-1.7, 8.3, 0.05, 0.96}, {-3.3, 9.7, -0.05, 0.88}, {-4.7, 11.2, -0.2, 0.64}, {-5.6, 12.9, -0.4, 0.38}, {-6.0, 14.6, -0.5, 0.2}, {-5.9, 15.9, -0.6, 0.1}}, 0.18);
    const auto sdf = [&](double x, double y, double z) {
        double d = 1e9;
        for (const auto& c : C) {
            const double zs = c[8], pz = z / zs, az = c[2] / zs, bz = c[6] / zs;
            const double bax = c[4] - c[0], bay = c[5] - c[1], baz = bz - az, pax = x - c[0], pay = y - c[1], paz = pz - az;
            const double h = clampd((pax * bax + pay * bay + paz * baz) / (bax * bax + bay * bay + baz * baz), 0, 1);
            const double e = (hyp(pax - bax * h, pay - bay * h, paz - baz * h) - mix(c[3], c[7], h)) * zs;
            const double k = 0.45, hh = std::max(k - std::abs(d - e), 0.0) / k;
            d = std::min(d, e) - hh * hh * k * 0.25;
        }
        return d;
    };
    const double vs = 0.09, ox = -7.6, oy = -2.6, oz = -2.2;
    const int nx = 186, ny = 216, nz = 76;
    std::vector<float> D(size_t(nx) * ny * nz);
    for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) D[size_t(x) + size_t(nx) * (y + size_t(ny) * z)] = float(-sdf(ox + x * vs, oy + y * vs, oz + z * vs));
    const NetMesh sn = surfaceNets(D.data(), nx, ny, nz);
    const Sampler3 smp{D.data(), nx, ny, nz};
    BuiltMesh m;
    m.vb.assign(size_t(sn.nv) * 16, 0);
    for (uint32_t k = 0; k < sn.nv; k++) {
        const double x = sn.pos[k * 3], y = sn.pos[k * 3 + 1], z = sn.pos[k * 3 + 2], e = 0.7;
        const double ax = smp(x - e, y, z) - smp(x + e, y, z), ay = smp(x, y - e, z) - smp(x, y + e, z), az = smp(x, y, z - e) - smp(x, y, z + e);
        double l = hyp(ax, ay, az); if (l == 0) l = 1;
        float* f = reinterpret_cast<float*>(m.vb.data() + size_t(k) * 16);
        f[0] = float(ox + x * vs); f[1] = float(oy + y * vs); f[2] = float(oz + z * vs);
        int8_t* s = reinterpret_cast<int8_t*>(m.vb.data() + size_t(k) * 16 + 12);
        s[0] = int8_t(jsround(ax / l * 127)); s[1] = int8_t(jsround(ay / l * 127)); s[2] = int8_t(jsround(az / l * 127)); s[3] = 127;
    }
    m.ib = sn.idx;
    m.nv = sn.nv;
    return m;
}
double sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x; }
// sunken ship (bow at +x)
Mesh meshShip() {
    using std::abs; using std::pow; using std::max; using std::min; using std::cos; using std::sin;
    rseed(220);
    Mesh m;
    const double Lh = 17, B = 5.6, Dp = 7.6;
    const auto hb = [&](double x) { return B * pow(max(1 - pow(abs(x / Lh), x < 0 ? 3.6 : 2.6), 0.0), 0.5); };
    const auto dp = [&](double x) { return Dp * (1 - 0.36 * pow(abs(x / Lh), 2.5)); };
    const auto sheer = [&](double x) { return 0.5 + (x > 0 ? 3.0 : 3.8) * pow(abs(x / Lh), 2.2); };
    const auto pt = [&](double x, double ph, double off = 0) -> V3 {
        const double b = hb(x) + off, d = dp(x) + off, s = sheer(x);
        return {x, s - (d + s) * pow(max(cos(ph), 0.0), 0.66), b * sign(ph) * pow(abs(sin(ph)), 0.8)};
    };
    const auto out = [](const V3& p, int) -> V3 { double l = hyp(p[1] + 0.5, p[2] * 1.3); if (l == 0) l = 1; return {0, (p[1] + 0.5) / l, p[2] * 1.3 / l}; };
    const auto up = [](const V3&, int) -> V3 { return {0, 1, 0}; };
    const auto axis = [](double x, double y, double z) { return [x, y, z](const V3&, int) -> V3 { return {x, y, z}; }; };
    std::vector<V3> keel;
    for (int i = 0; i <= 44; i++) {
        const double x = mix(-Lh - 1.0, Lh + 2.4, i / 44.0), e = max(abs(x) / Lh - 0.78, 0.0) * 4.2;
        keel.push_back({x, -dp(clampd(x, -Lh, Lh)) - 0.3 + e * e * e * 4.2, 0});
    }
    m.sweep(keel, 0.55, 0.95, up);
    for (double x = -Lh + 0.7; x < Lh - 0.3; x += 0.8) {
        const double a0 = rnd() < 0.1 ? rr(-0.9, -0.2) : -PI / 2;
        const double a1 = rnd() < 0.1 ? rr(0.2, 0.9) : PI / 2;
        std::vector<V3> pts;
        for (int i = 0; i <= 16; i++) pts.push_back(pt(x, mix(a0, a1, i / 16.0)));
        m.sweep(pts, 0.24, 0.3, out);
    }
    const double holeX = rr(-6, 2), holeS = rnd() < 0.5 ? 1 : -1;
    const double BR[][4] = {{holeX, holeS * 0.8, 3.2, 0.42}, {7, -holeS * 0.95, 2.4, 0.34}, {12, holeS * 0.5, 1.7, 0.28}, {-11, -holeS * 0.55, 2.1, 0.3}};
    const auto holed = [&](double x, double ph) {
        for (const auto& h : BR) { const double a = (x - h[0]) / h[2], b = (ph - h[1]) / h[3]; if (a * a + b * b < 1 + 0.45 * sin(x * 3.1 + ph * 9)) return true; }
        return false;
    };
    const int NS = 14;
    std::vector<double> xs;
    for (double x = -Lh + 0.35; x <= Lh - 0.3; x += 0.85) xs.push_back(x);
    for (int j = 0; j < NS; j++) {
        const double p0 = mix(-1.5, 1.5, double(j) / NS), p1 = mix(-1.5, 1.5, double(j + 1) / NS), sg = p0 + p1 < 0 ? -1 : 1, lo = sg < 0 ? p1 : p0, hi = sg < 0 ? p0 : p1;
        bool havePv = false;
        std::array<uint32_t, 4> pv{};
        for (double x : xs) {
            const std::array<uint32_t, 4> cu = {m.v(pt(x, lo, 0.27)), m.v(pt(x, hi, 0.14)), m.v(pt(x, lo, 0.1)), m.v(pt(x, hi, 0.1))};
            if (havePv && !holed(x - 0.42, (p0 + p1) / 2)) {
                if (sg > 0) { m.quad(pv[0], pv[1], cu[1], cu[0]); m.quad(pv[2], cu[2], cu[3], pv[3]); }
                else { m.quad(pv[0], cu[0], cu[1], pv[1]); m.quad(pv[2], pv[3], cu[3], cu[2]); }
            }
            pv = cu; havePv = true;
        }
    }
    for (double sgn : {-1.0, 1.0}) {
        std::vector<V3> pts;
        for (double x = -Lh + 0.6; x <= Lh - 0.6; x += 1) pts.push_back(pt(x, sgn * PI / 2, 0.2));
        m.sweep(pts, 0.3, 0.28, up);
        // stern castle bulwarks
        for (int j = 0; j < 3; j++) {
            pts.clear();
            for (double x = -Lh + 0.4; x <= -Lh + 8.5 - j; x += 0.9) pts.push_back({x, sheer(x) + 0.55 + j * 0.86, sgn * (max(hb(x), 2.6) + 0.15)});
            m.sweep(pts, 0.82, 0.09, axis(0, 0, sgn));
        }
    }
    // transom
    for (double y = -3; y < sheer(-Lh) + 3; y += 0.86) {
        const double w = max(1.2, 2.9 * sstep(-4.5, 1.5, y));
        if (rnd() < 0.9) m.sweep({{-Lh + 0.35, y, -w}, {-Lh + 0.35, y, w}}, 0.82, 0.1, axis(1, 0, 0));
    }
    for (double x = -Lh + 2; x < Lh - 2; x += rr(1.8, 3.2)) m.sweep({{x, sheer(x) - 0.25, -hb(x)}, {x, sheer(x) - 0.2, 0}, {x, sheer(x) - 0.25, hb(x)}}, 0.26, 0.26, up);
    for (double z = -3.9; z <= 3.9; z += 0.62) {
        double x = -Lh + rr(1, 5);
        while (x < Lh - 4) {
            const double x1 = min(x + rr(3, 9), Lh - 3.5);
            if (rnd() < 0.6) {
                std::vector<V3> pts;
                for (double xx = x; xx <= x1; xx += 1) if (hb(xx) > abs(z) + 0.3) pts.push_back({xx, sheer(xx) - 0.05, z});
                if (pts.size() > 1) m.sweep(pts, 0.56, 0.07, up);
            }
            x = x1 + rr(0.5, 3);
        }
    }
    // foremast with yard, broken main mast, bowsprit
    const auto taper = [](double a) { return [a](int k) { return a - k * 0.1; }; };
    m.sweep({{7, -Dp + 0.6, 0}, {7.1, 1, 0}, {7.4, 8, 0.3}, {7.8, 15, 0.8}}, taper(0.62), taper(0.62), axis(1, 0, 0));
    m.sweep({{7.5, 9.5, -5.2}, {7.5, 9.6, 0.3}, {7.5, 9.2, 5.6}}, 0.26, 0.26, up);
    m.sweep({{7.7, 13.2, -3.2}, {7.7, 13.3, 3.6}}, 0.2, 0.2, up);
    m.sweep({{-3, -Dp + 0.6, 0}, {-3.2, 1, 0.2}, {-3.9, 6, 1.0}, {-5.2, 9.5, 2.6}}, taper(0.74), taper(0.74), axis(1, 0, 0));
    m.sweep({{Lh + 1.4, 2.6, 0}, {Lh + 8, 5.2, 0.2}}, 0.34, 0.34, up);
    return m;
}
// manta ray: span along x (half span = 1), nose +z; aux = span coordinate for the flap animation
Mesh meshManta() {
    using std::abs; using std::pow; using std::exp; using std::sin; using std::cos;
    Mesh m;
    const int NU = 72, M = 22;
    std::vector<std::vector<uint32_t>> rings;
    for (int j = 0; j <= NU; j++) {
        const double u = double(j) / NU * 2 - 1, au = abs(u);
        double zl = 0.52 - 0.74 * pow(au, 1.35) + 0.07 * exp(-pow((au - 0.13) / 0.05, 2)) - 0.05 * exp(-pow(au / 0.05, 2)) - 0.08 * pow(au, 5);
        const double zt = -0.5 + 0.3 * pow(au, 0.8) + 0.02 - 0.08 * pow(au, 5);
        if (zl < zt + 0.004) zl = zt + 0.004;
        const double th = 0.08 * pow(1 - au, 1.15) + 0.004 + 0.12 * exp(-pow(u / 0.2, 2));
        std::vector<uint32_t> ring;
        for (int k = 0; k < M; k++) { const double a = double(k) / M * TAU; ring.push_back(m.v(u, th * sin(a) * (0.62 + 0.38 * cos(a)), mix(zt, zl, 0.5 + 0.5 * cos(a)), u)); }
        rings.push_back(ring);
    }
    for (int j = 0; j < NU; j++) for (int k = 0; k < M; k++) { const int k2 = (k + 1) % M; m.quad(rings[j][k], rings[j + 1][k], rings[j + 1][k2], rings[j][k2]); }
    std::vector<V3> tail;
    for (int i = 0; i <= 8; i++) tail.push_back({0, 0, -0.42 - i * 0.19});
    const size_t n0 = m.p.size() / 3;
    m.tube(tail, [](int k) { return 0.016 * (1 - k / 9.0); }, 4, true);
    for (size_t k = n0; k < m.aux.size(); k++) m.aux[k] = 0;
    for (double sx : {-1.0, 1.0}) { // cephalic fins
        const size_t n1 = m.p.size() / 3;
        m.tube({{sx * 0.12, -0.02, 0.48}, {sx * 0.135, -0.05, 0.6}, {sx * 0.115, -0.09, 0.69}, {sx * 0.085, -0.125, 0.74}}, [](int k) { return 0.045 * (1 - k / 4.4); }, 6, true);
        for (size_t k = n1; k < m.aux.size(); k++) m.aux[k] = sx * 0.1;
    }
    return m;
}
Mesh meshFish() {
    Mesh m;
    const double X[] = {0.5, 0.3, 0.05, -0.22, -0.34}, H[] = {0.01, 0.13, 0.17, 0.09, 0.035}, Wd[] = {0.01, 0.05, 0.065, 0.03, 0.012};
    std::vector<std::vector<uint32_t>> rings;
    for (int i = 0; i < 5; i++) { std::vector<uint32_t> r; for (int k = 0; k < 6; k++) { const double a = k / 6.0 * TAU; r.push_back(m.v(X[i], std::sin(a) * H[i], std::cos(a) * Wd[i])); } rings.push_back(r); }
    for (int i = 0; i < 4; i++) for (int k = 0; k < 6; k++) { const int k2 = (k + 1) % 6; m.quad(rings[i][k], rings[i + 1][k], rings[i + 1][k2], rings[i][k2]); }
    const uint32_t a = m.v(-0.33, 0, 0), b = m.v(-0.56, 0.2, 0), c = m.v(-0.47, 0, 0), d = m.v(-0.56, -0.2, 0);
    m.tri(a, b, c); m.tri(a, c, d);
    return m;
}

} // namespace

std::vector<BuiltMesh> makeMeshes() {
    // ids: 0-3 boulders, 4-6 sponges, 7-8 coral, 9 tube sponge, 10 whale fluke, 11 ship, 12 manta, 13 fish,
    // 14-20 rock chunks + pebbles, 21-22 coral LOD, 23-25 far LOD of the large rock chunks
    std::vector<Mesh> first;
    first.push_back(meshChunk(1, 3)); first.push_back(meshChunk(7, 3)); first.push_back(meshChunk(13, 3)); first.push_back(meshChunk(22, 3));
    first.push_back(meshLathe({{0, 0, 0.5}, {0.34, 0, 0.6}, {0.5, 0.2, 0.85}, {0.6, 0.6, 1}, {0.55, 0.95, 1}, {0.47, 1.08, 1}, {0.38, 1.1, 0.9}, {0.3, 1.0, 0.45}, {0.26, 0.6, 0.15}, {0, 0.35, 0.05}}, 18, 0.3, 9));
    first.push_back(meshLathe({{0, 0, 0.5}, {0.2, 0, 0.6}, {0.26, 0.5, 0.85}, {0.4, 1.25, 1}, {0.4, 1.42, 1}, {0.33, 1.48, 0.9}, {0.26, 1.38, 0.4}, {0.18, 0.8, 0.12}, {0, 0.5, 0.05}}, 16, 0.35, 8));
    first.push_back(meshLathe({{0, 0, 0.5}, {0.5, 0, 0.7}, {0.72, 0.18, 1}, {0.74, 0.38, 1}, {0.62, 0.5, 0.9}, {0.48, 0.42, 0.45}, {0.4, 0.2, 0.15}, {0, 0.14, 0.06}}, 18, 0.25, 10));
    first.push_back(meshCoral(0)); first.push_back(meshCoral(1)); first.push_back(meshTubes());
    std::vector<BuiltMesh> out;
    for (const Mesh& m : first) out.push_back(m.build());
    out.push_back(meshFluke());
    out.push_back(meshShip().build()); out.push_back(meshManta().build()); out.push_back(meshFish().build());
    out.push_back(meshChunk(3).build()); out.push_back(meshChunk(11).build()); out.push_back(meshChunk(29).build());
    out.push_back(meshChunk(5, 3).build()); out.push_back(meshChunk(17, 3).build()); out.push_back(meshChunk(41, 3).build());
    out.push_back(meshChunk(9, 1).build()); out.push_back(meshCoral(0, true).build()); out.push_back(meshCoral(1, true).build());
    out.push_back(meshChunk(5, 2, 1).build()); out.push_back(meshChunk(17, 2, 1).build()); out.push_back(meshChunk(41, 2, 1).build());
    return out;
}

} // namespace abyssal
