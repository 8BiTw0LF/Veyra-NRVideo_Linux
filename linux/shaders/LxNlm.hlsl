// VeyAmux for Linux: spatial denoiser - non-local means (Buades, Coll, Morel 2005) on the linear working image.
// Every pixel becomes a weighted average of the pixels in its 7x7 neighbourhood whose 3x3 surroundings look alike, so
// edges and repeated texture are kept while noise (film grain, dither, sensor/compression noise) averages out.
// Works in a perceptual domain (sqrt of linear light) where noise is roughly uniform across brightness; patch
// distances use luma (plus the centre pixels' colour), the average is taken over RGB. The tile + apron sits in groupshared memory.
cbuffer NlmParams : register(b0) { uint width; uint height; float sigma; float filterH; };
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

#define TILE 16
#define SR 3                      // search radius (7x7)
#define PR 1                      // patch radius (3x3)
#define APRON (SR + PR)
#define SIDE (TILE + 2 * APRON)
groupshared float3 sRgb[SIDE * SIDE];
groupshared float sY[SIDE * SIDE];

[numthreads(TILE, TILE, 1)]
void main(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID) {
    const int2 origin = int2(gid.xy) * TILE - APRON;
    const int2 hi = int2(width, height) - 1;
    const uint lin = tid.y * TILE + tid.x;
    for (uint i = lin; i < SIDE * SIDE; i += TILE * TILE) {
        const int2 q = clamp(origin + int2(i % SIDE, i / SIDE), int2(0, 0), hi);
        const float3 c = sqrt(max(src[q].rgb, 0.0));
        sRgb[i] = c;
        sY[i] = dot(c, float3(0.2126, 0.7152, 0.0722));
    }
    GroupMemoryBarrierWithGroupSync();
    const int2 p = int2(gid.xy) * TILE + int2(tid.xy);
    if (p.x >= int(width) || p.y >= int(height)) return;
    const int2 l = int2(tid.xy) + APRON;   // position in the tile

    // patch of the centre pixel
    float pc[9];
    [unroll] for (int j = -PR; j <= PR; ++j)
        [unroll] for (int i = -PR; i <= PR; ++i) pc[(j + PR) * 3 + (i + PR)] = sY[(l.y + j) * SIDE + (l.x + i)];

    const float inv2s2 = 2.0 * sigma * sigma;
    const float invH2 = 1.0 / max(filterH * filterH, 1e-8);
    float3 acc = 0; float wsum = 0; float wmax = 0;
    [unroll] for (int dy = -SR; dy <= SR; ++dy) {
        [unroll] for (int dx = -SR; dx <= SR; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int2 c = l + int2(dx, dy);
            float d2 = 0;
            [unroll] for (int j = -PR; j <= PR; ++j)
                [unroll] for (int i = -PR; i <= PR; ++i) {
                    const float d = sY[(c.y + j) * SIDE + (c.x + i)] - pc[(j + PR) * 3 + (i + PR)];
                    d2 += d * d;
                }
            // luma patch distance plus the colour difference of the two centre pixels: two colours of equal brightness
            // (a red object on a grey background) must not be averaged together
            const float3 dc = sRgb[c.y * SIDE + c.x] - sRgb[l.y * SIDE + l.x];
            d2 = d2 * (1.0 / 9.0) + dot(dc, dc) * (1.0 / 3.0);
            const float w = exp(-max(d2 - 1.35 * inv2s2, 0.0) * invH2);   // expected distance of two noisy copies of the same patch
            acc += w * sRgb[c.y * SIDE + c.x];
            wsum += w;
            wmax = max(wmax, w);
        }
    }
    // the centre pixel gets the largest weight of its neighbours (its own distance, 0, would dominate the average)
    const float wc = max(wmax, 1e-3);
    acc += wc * sRgb[l.y * SIDE + l.x];
    wsum += wc;
    const float3 r = acc / wsum;
    dst[p] = float4(r * r, src[p].a);
}
