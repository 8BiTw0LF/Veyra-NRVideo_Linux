// VeyAmux for Linux: keeps fine detail in generated frames (last pass of FSR frame generation).
// Frame interpolation averages the previous and the current frame, each moved half-way along the motion. Where the two
// do not line up exactly - fine, aliased detail far away in games, foliage, noise - the average is a smear. Real and
// generated frames then alternate sharp / blurry many times a second, which reads as strong flicker while panning.
// Per pixel: fetch both half-way samples unfiltered; where they disagree, use the one closer to FSR's result (FSR still
// decides which side is visible at disocclusions) instead of the smeared average. Where they agree, FSR's result stays.
cbuffer DetailParams : register(b0) { uint width; uint height; float threshold; uint p0; };
Texture2D<float4> cur : register(t0);
Texture2D<float4> prev : register(t1);
RWTexture2D<float4> motion : register(u0);
RWTexture2D<float4> generated : register(u1);
SamplerState linearClamp : register(s1);

float4 Fetch(Texture2D<float4> t, float2 q) {
    return t[clamp(int2(floor(q + 0.5)), int2(0, 0), int2(width, height) - 1)];
}
float Diff(float3 a, float3 b) { const float3 d = abs(a - b); return max(max(d.r, d.g), d.b); }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    const float2 mv = motion[p].xy;
    const float2 qa = float2(p) - 0.5 * mv, qb = float2(p) + 0.5 * mv;
    // content entering/leaving the frame: one side does not exist, FSR's one-sided result is right
    const float2 lim = float2(width, height) - 1.0;
    if (any(qa < 0.0) || any(qa > lim) || any(qb < 0.0) || any(qb > lim)) return;
    // decide on exact (filtered) samples: where the motion is right they agree even on sharp edges, and FSR's result
    // is kept; only a real mismatch (motion that does not fit the detail) switches to an unfiltered single sample
    const float2 inv = 1.0 / float2(width, height);
    const float3 fa = cur.SampleLevel(linearClamp, (qa + 0.5) * inv, 0).rgb;
    const float3 fb = prev.SampleLevel(linearClamp, (qb + 0.5) * inv, 0).rgb;
    if (Diff(fa, fb) <= threshold) return;
    const float3 a = Fetch(cur, qa).rgb;    // current frame, moved back half-way
    const float3 b = Fetch(prev, qb).rgb;   // previous frame, moved forward half-way
    const float4 g = generated[p];
    generated[p] = float4(Diff(a, g.rgb) <= Diff(b, g.rgb) ? a : b, g.a);
}
