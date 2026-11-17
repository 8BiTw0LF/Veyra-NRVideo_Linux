// VeyAmux for Linux: Abyssal Garden demo scene, world.js - the world layout and everything generated from the density
// grid: terrain mesh, kelp, plants, rocks, sponges, corals, fish, bubbles and the light volume (see AbyssalMath.h).
#pragma once
#include "AbyssalMath.h"

#include <array>
#include <vector>

namespace abyssal {

constexpr double PURPLE[2] = {6, 84};
double zoneP(double x, double z);

// prims for the density compute shader (mesas, arches, holes, clears) and their counts
std::vector<float> primBuffer();
struct PrimCounts { uint32_t mesas, arches, holes, clears; };
PrimCounts primCounts();

struct Terrain {
    std::vector<uint8_t> vb;       // 16 bytes per vertex
    std::vector<uint32_t> ib;      // quads sorted by culling cell
    uint32_t nv = 0;
    std::vector<float> wp, wn;     // world positions and normals
    Sampler3 smp;
    std::vector<uint32_t> start;   // CELLS*CELLS + 1, in quads
    std::vector<float> lo, hi;
};
Terrain buildTerrain(const std::vector<float>& D);

// instances sorted by culling cell
struct CellSet {
    std::vector<float> data;       // 12 floats per instance (solids) ...
    std::vector<uint8_t> buf;      // ... or 36 bytes per leaf
    uint32_t n = 0;
    std::vector<uint32_t> start;   // CELLS*CELLS + 1
    std::vector<float> lo, hi;
};
struct World {
    std::vector<float> glints;
    std::array<CellSet, NCHUNK> chunks;
    std::array<CellSet, 6> props;
    CellSet big, bigF, small;
    std::vector<float> solidData;
    std::array<std::array<uint32_t, 2>, 12> solidRanges{};
    std::vector<float> bubbles, fish, mantas, snow;
    std::vector<uint8_t> volD, gm;
};
using Pose = std::array<double, 6>;   // position, look-at
// POSES get their ground height added (y += ground at x, z) after the world is made, as in main.js
World makeWorld(const std::vector<float>& D, const Terrain& T, const std::vector<V3>& flyPts, std::vector<Pose>& POSES);

// procedural meshes, in the order of main.js (ids 0..25)
std::vector<BuiltMesh> makeMeshes();

} // namespace abyssal
