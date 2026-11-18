// VeyAmux for Linux: anti-flicker - calms pixels that flicker between frames (thin lines, fences, foliage, distant
// detail crawling) without touching anything else.
// The two previous source frames are brought to this frame's positions with the motion field (t3, from
// MotionEstimator: this pixel was at p + mv in the previous frame); prevAl (t2) already holds the frame before that,
// aligned to the previous frame. A pixel flickers when its brightness goes up and then down again (or down, then up)
// over the three frames by more than a threshold. Only there the three frames are averaged as 1-2-1, which turns an
// on/off flicker into its steady middle value; the result is clamped to the current 3x3 neighbourhood so a wrong
// motion vector cannot drag old content along. No added frames of delay. Perceptual domain (sqrt of linear light).
// u0 = output, u1 = this frame (next frame's "previous"), u2 = the previous frame aligned to this one,
// u3 = the previous frame as it was (next frame's "prevPrev").
cbuffer DeflickerParams : register(b0) { uint width; uint height; float threshold; float strength; uint history; uint p0; uint p1; uint p2; };
Texture2D<float4> cur : register(t0);
Texture2D<float4> prevIn : register(t1);
Texture2D<float4> prevAl : register(t2);
Texture2D<float4> motion : register(t3);
Texture2D<float4> prevPrev : register(t4);   // the frame before the previous one, as it was (no motion applied)
RWTexture2D<float4> dst : register(u0);
RWTexture2D<float4> nextPrev : register(u1);
RWTexture2D<float4> nextAl : register(u2);
RWTexture2D<float4> nextPrevPrev : register(u3);
SamplerState linearClamp : register(s1);

float3 P(float3 c) { return sqrt(max(c, 0.0)); }
float Luma(float3 p) { return dot(p, float3(0.2126, 0.7152, 0.0722)); }

// One hypothesis (a = previous frame, b = the one before, both at this pixel): flicker when the brightness goes up
// and then down again (or down, then up) by more than the threshold, and this frame matches the one two frames back
// (an on/off pattern). w = how sure, m = the 1-2-1 average.
void Candidate(float3 c, float3 a, float3 b, float trust, out float w, out float3 m, out float amp) {
    const float d1 = Luma(c) - Luma(a), d2 = Luma(a) - Luma(b);
    amp = min(abs(d1), abs(d2));
    const float repeat = saturate(1.0 - abs(Luma(c) - Luma(b)) / max(amp, 1e-4));
    w = (d1 * d2 < 0.0) ? smoothstep(threshold, threshold * 2.0, amp) * repeat * trust : 0.0;
    m = 0.25 * c + 0.5 * a + 0.25 * b;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    const int2 hi = int2(width, height) - 1;
    const float4 c4 = cur[p];
    nextPrev[p] = c4;
    nextPrevPrev[p] = prevIn[p];
    const float3 c = P(c4.rgb);
    if (history == 0) { nextAl[p] = c4; dst[p] = c4; return; }

    const float4 mvc = motion[p];
    const float2 q = float2(p) + 0.5 + mvc.xy;
    const float2 inv = 1.0 / float2(width, height);
    if (any(q < 0.0) || any(q > float2(width, height))) { nextAl[p] = c4; dst[p] = c4; return; }
    const float4 a4 = prevIn.SampleLevel(linearClamp, q * inv, 0);   // previous frame, here
    const float4 b4 = prevAl.SampleLevel(linearClamp, q * inv, 0);   // the frame before, here
    nextAl[p] = a4;
    // Two candidates: the frames along the measured motion, and the same spot without motion. The motion measurement
    // itself is fooled by flicker (a line that turns dark looks like it moved), and most flicker sits on still or
    // slowly moving detail, so the candidate that shows the flicker more clearly wins.
    float3 m;
    float wgt = 0.0;
    float amp = 0.0;
    {
        const float mlen = length(mvc.xy);
        // trust the motion: a (nearly) still pixel always, moving pixels by their confidence; fast motion less
        const float trust = saturate(max(mvc.z * 1.5, 1.0 - mlen / 1.5)) * saturate(1.5 - mlen / 8.0);
        Candidate(c, P(a4.rgb), P(b4.rgb), trust, wgt, m, amp);
        // the still candidate only where the surroundings stand still: the smallest motion 3 pixels around (flicker
        // fools the measurement on the flickering pixels themselves, not on the still background next to them)
        float still = mlen;
        [unroll] for (int k = 0; k < 8; ++k) {
            const int2 o = int2(k == 0 || k == 3 || k == 5 ? -3 : (k == 1 || k == 6 ? 0 : 3), k < 3 ? -3 : (k < 5 ? 0 : 3));
            still = min(still, length(motion[clamp(p + o, int2(0, 0), hi)].xy));
        }
        const float zeroTrust = saturate(1.5 - still);
        float w0; float3 m0; float amp0;
        Candidate(c, P(prevIn[p].rgb), P(prevPrev[p].rgb), zeroTrust, w0, m0, amp0);
        if (w0 > wgt) { wgt = w0; m = m0; amp = amp0; }
    }
    if (wgt <= 0.0) { dst[p] = c4; return; }

    // the steady middle value, clamped to what this frame's 3x3 neighbourhood allows
    float3 mn = 1e9, mx = -1e9;
    [unroll] for (int j = -1; j <= 1; ++j)
        [unroll] for (int i = -1; i <= 1; ++i) {
            const float3 v = P(cur[clamp(p + int2(i, j), int2(0, 0), hi)].rgb);
            mn = min(mn, v); mx = max(mx, v);
        }
    // (widened by half the flicker, so a flickering area larger than 3x3 still gets calmer)
    m = clamp(m, mn - 0.5 * amp, mx + 0.5 * amp);
    const float3 r = lerp(c, m, wgt * strength);
    dst[p] = float4(r * r, c4.a);
}
