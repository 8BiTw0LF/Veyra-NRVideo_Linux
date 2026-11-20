// VeyAmux for Linux: the finished picture -> the exact pixel size it covers in the window, so Qt draws it 1:1.
// Before, Qt stretched the chain's output (source size x the chosen factor) to the window with one bilinear sample:
// soft when enlarging, and when shrinking (2x of 1080p in a 1440p window) it skipped source pixels, so thin lines and
// text crawled. Same approach as Veyra-NRVideo 2.2.0's presentation scaling (docs/PRESENT_SCALING_2026-11-18.md):
// Catmull-Rom; enlarging uses the 4x4 neighbourhood and is limited to the range of the 2x2 nearest pixels (no
// ringing); shrinking widens the kernel by the shrink factor on each axis (every source pixel counts), capped at 4:1.
// Works on the encoded values the window shows (sRGB codes, or PQ codes for HDR output), like Veyra.
cbuffer FitParams : register(b0) { uint srcW; uint srcH; uint dstW; uint dstH; };
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

float CatmullRom(float x) {
    x = abs(x);
    if (x < 1.0) return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0) return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    const float2 scale = float2(srcW, srcH) / float2(dstW, dstH);            // > 1: shrinking
    const float2 s = (float2(id.xy) + 0.5) * scale - 0.5;                    // source position of this pixel
    const float2 widen = clamp(scale, 1.0, 4.0);                             // kernel stretch when shrinking
    const float2 radius = 2.0 * widen;
    const int2 lo = int2(floor(s - radius)) + 1, hi = int2(floor(s + radius));
    const int2 last = int2(srcW, srcH) - 1;
    float4 acc = 0;
    float wsum = 0;
    for (int y = lo.y; y <= hi.y; ++y) {
        const float wy = CatmullRom((float(y) - s.y) / widen.y);
        if (wy == 0.0) continue;
        for (int x = lo.x; x <= hi.x; ++x) {
            const float w = CatmullRom((float(x) - s.x) / widen.x) * wy;
            if (w == 0.0) continue;
            acc += w * src.Load(int3(clamp(int2(x, y), int2(0, 0), last), 0));
            wsum += w;
        }
    }
    float4 c = acc / max(wsum, 1e-6);
    if (scale.x < 1.0 && scale.y < 1.0) {
        // enlarging: no ringing beyond the 2x2 pixels around the position
        const int2 b = int2(floor(s));
        const float4 p00 = src.Load(int3(clamp(b, int2(0, 0), last), 0));
        const float4 p10 = src.Load(int3(clamp(b + int2(1, 0), int2(0, 0), last), 0));
        const float4 p01 = src.Load(int3(clamp(b + int2(0, 1), int2(0, 0), last), 0));
        const float4 p11 = src.Load(int3(clamp(b + int2(1, 1), int2(0, 0), last), 0));
        c = clamp(c, min(min(p00, p10), min(p01, p11)), max(max(p00, p10), max(p01, p11)));
    }
    dst[id.xy] = float4(saturate(c.rgb), 1.0);
}
