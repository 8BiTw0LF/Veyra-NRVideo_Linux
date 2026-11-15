// VeyAmux for Linux: one level of the coarse-to-fine Lucas-Kanade motion estimator behind AMD FSR 3.1 upscaling.
// A temporal upscaler needs sub-pixel motion for every pixel; video has no engine motion vectors and FidelityFX optical
// flow is whole-pixel (and blind for the first frames after a reset). Per level: start from the coarser level's vector
// (doubled) and run a few regularised Lucas-Kanade iterations over a 7x7 Gaussian window. Where the window has no
// texture the coarser estimate is kept, along a lone edge only the component across it changes, and a refined vector
// that matches worse than its start is discarded. Each vector carries a confidence (z) so LxMvFill
// can replace unreliable vectors with those of confident neighbours.
// Motion convention (FSR): current pixel p was at p + mv in the previous frame, in pixels of this level.
cbuffer LkParams : register(b0) { uint width; uint height; uint coarseW; uint coarseH; uint hasCoarse; uint iterations; uint p0; uint p1; };
Texture2D<float> cur : register(t0);
Texture2D<float> prev : register(t1);
Texture2D<float4> coarse : register(t2);
RWTexture2D<float4> field : register(u0);
SamplerState linearClamp : register(s1);

static const int R = 3;

float CurL(int2 p) { return cur[clamp(p, int2(0, 0), int2(width, height) - 1)]; }
float PrevL(float2 p) { return prev.SampleLevel(linearClamp, (p + 0.5) / float2(width, height), 0); }
float W(int i, int j) { return exp(-0.125 * float(i * i + j * j)); }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    float2 v0 = 0;
    float c0 = 0;
    if (hasCoarse != 0) {
        const float2 uv = (float2(p) + 0.5) / float2(width, height);
        const float4 cv = coarse.SampleLevel(linearClamp, uv, 0);
        v0 = 2.0 * cv.xy;
        c0 = cv.z;
    }
    // iterations == 0: upsample only (very large frames are estimated at half resolution)
    if (iterations == 0) { field[p] = float4(v0, c0, 0); return; }

    float I[49]; float2 G[49];
    float gxx = 0, gxy = 0, gyy = 0;
    [unroll] for (int j = -R; j <= R; ++j) {
        [unroll] for (int i = -R; i <= R; ++i) {
            const int k = (j + R) * 7 + (i + R);
            const int2 q = p + int2(i, j);
            I[k] = CurL(q);
            G[k] = 0.5 * float2(CurL(q + int2(1, 0)) - CurL(q - int2(1, 0)), CurL(q + int2(0, 1)) - CurL(q - int2(0, 1)));
            const float w = W(i, j);
            gxx += w * G[k].x * G[k].x; gxy += w * G[k].x * G[k].y; gyy += w * G[k].y * G[k].y;
        }
    }
    const float det = gxx * gyy - gxy * gxy;
    const float tr = gxx + gyy;
    if (tr < 1e-6) { field[p] = float4(v0, 0, 0); return; }
    // Tikhonov-regularised normal equations: along an edge (aperture problem) only the component across the edge is
    // measurable; the damping keeps the other component at the coarser estimate instead of letting it drift.
    const float lam = 0.05 * tr + 1e-6;
    const float a = gxx + lam, c = gyy + lam;
    const float2x2 inv = float2x2(c, -gxy, -gxy, a) / (a * c - gxy * gxy);

    float2 v = v0;
    float err0 = 0;
    for (uint it = 0; it < iterations; ++it) {
        float2 b = 0; float err = 0;
        [unroll] for (int j = -R; j <= R; ++j) {
            [unroll] for (int i = -R; i <= R; ++i) {
                const int k = (j + R) * 7 + (i + R);
                const float w = W(i, j);
                const float e = PrevL(float2(p + int2(i, j)) + v) - I[k];
                b += w * G[k] * e; err += w * e * e;
            }
        }
        if (it == 0) err0 = err;
        float2 d = -mul(inv, b);
        const float len = length(d);
        if (len > 1.0) d /= len;
        v += d;
    }
    v = v0 + clamp(v - v0, -2.0, 2.0);
    float err1 = 0;
    [unroll] for (int j = -R; j <= R; ++j) {
        [unroll] for (int i = -R; i <= R; ++i) {
            const int k = (j + R) * 7 + (i + R);
            const float e = PrevL(float2(p + int2(i, j)) + v) - I[k];
            err1 += W(i, j) * e * e;
        }
    }
    // confidence: texture strength (smallest eigenvalue ~ det/tr) and how well the window matches afterwards
    const float lmin = det / tr;
    const float matchErr = min(err1, err0) / 12.0;   // window weights sum to ~12
    const float conf = (lmin / (lmin + 2e-4)) * exp(-matchErr / 4e-4);
    field[p] = err1 <= err0 ? float4(v, conf, 0) : float4(v0, 0.5 * conf, 0);
}
