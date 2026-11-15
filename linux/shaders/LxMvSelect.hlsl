// VeyAmux for Linux: sharpens motion boundaries (last step of MotionEstimator). Smooth coarse-to-fine estimation lets the
// motion of a panning background bleed a few pixels into a static object next to it (and the other way round), which
// frame interpolation shows as a halo. Per pixel, the vectors of nearby pixels (up to 8 px away) are tried as
// candidates; the one whose 5x5 neighbourhood matches the previous frame best wins (ties keep the pixel's own vector).
cbuffer SelectParams : register(b0) { uint width; uint height; uint p0; uint p1; };
Texture2D<float> cur : register(t0);
Texture2D<float> prev : register(t1);
Texture2D<float4> fieldIn : register(t2);
RWTexture2D<float4> fieldOut : register(u0);
SamplerState linearClamp : register(s1);

float CurL(int2 p) { return cur[clamp(p, int2(0, 0), int2(width, height) - 1)]; }
float PrevL(float2 p) { return prev.SampleLevel(linearClamp, (p + 0.5) / float2(width, height), 0); }
float4 F(int2 p) { return fieldIn[clamp(p, int2(0, 0), int2(width, height) - 1)]; }

// 3x3 samples spread over a 5x5 neighbourhood (every second pixel): same footprint, a third of the cost
float Cost(int2 p, float2 v) {
    float c = 0;
    [unroll] for (int j = -2; j <= 2; j += 2)
        [unroll] for (int i = -2; i <= 2; i += 2)
            c += abs(PrevL(float2(p + int2(i, j)) + v) - CurL(p + int2(i, j)));
    return c;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    float4 best = F(p);
    float bestCost = Cost(p, best.xy) * 0.97;   // small bias towards the pixel's own vector
    static const int2 offs[12] = {int2(4, 0), int2(-4, 0), int2(0, 4), int2(0, -4), int2(8, 0), int2(-8, 0),
                                  int2(0, 8), int2(0, -8), int2(6, 6), int2(-6, 6), int2(6, -6), int2(-6, -6)};
    [unroll] for (int k = 0; k < 12; ++k) {
        const float4 c = F(p + offs[k]);
        const float cost = Cost(p, c.xy);
        if (cost < bestCost) { bestCost = cost; best = c; }
    }
    fieldOut[p] = best;
}
