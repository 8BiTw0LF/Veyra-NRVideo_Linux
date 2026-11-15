// VeyAmux for Linux: temporal denoiser - motion-compensated recursive average.
// The previous result (t1) is fetched where this pixel was in the previous frame (motion t2 from MotionEstimator:
// current pixel p was at p + mv), with Catmull-Rom sampling so repeated resampling does not blur, clamped to what the
// current 3x3 neighbourhood allows (plus the noise level) so wrong motion cannot drag old content along, and blended
// with the current frame. Where the result differs from the current frame by more than the noise, the history is
// trusted less. Still and correctly tracked areas average over many frames and keep all their detail; new or
// unpredictable content falls back to the current frame. Perceptual domain (sqrt of linear light).
// u0 = output (also the next frame's history).
cbuffer TnrParams : register(b0) { uint width; uint height; float sigma; float maxBlend; uint reset; uint p0; uint p1; uint p2; };
Texture2D<float4> cur : register(t0);
Texture2D<float4> hist : register(t1);
Texture2D<float4> motion : register(t2);
RWTexture2D<float4> dst : register(u0);
SamplerState linearClamp : register(s1);

float3 P(float3 c) { return sqrt(max(c, 0.0)); }

float3 SampleSharp(float2 pos, float2 inv) {
    const float2 p = pos - 0.5;
    const float2 tc = floor(p) + 0.5;
    const float2 f = p - floor(p);
    const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);
    const float2 w12 = w1 + w2;
    const float2 o12 = w2 / w12;
    const float2 t0 = (tc - 1.0) * inv, t3 = (tc + 2.0) * inv, t12 = (tc + o12) * inv;
    float3 r = 0;
    r += P(hist.SampleLevel(linearClamp, float2(t0.x, t0.y), 0).rgb) * w0.x * w0.y;
    r += P(hist.SampleLevel(linearClamp, float2(t12.x, t0.y), 0).rgb) * w12.x * w0.y;
    r += P(hist.SampleLevel(linearClamp, float2(t3.x, t0.y), 0).rgb) * w3.x * w0.y;
    r += P(hist.SampleLevel(linearClamp, float2(t0.x, t12.y), 0).rgb) * w0.x * w12.y;
    r += P(hist.SampleLevel(linearClamp, float2(t12.x, t12.y), 0).rgb) * w12.x * w12.y;
    r += P(hist.SampleLevel(linearClamp, float2(t3.x, t12.y), 0).rgb) * w3.x * w12.y;
    r += P(hist.SampleLevel(linearClamp, float2(t0.x, t3.y), 0).rgb) * w0.x * w3.y;
    r += P(hist.SampleLevel(linearClamp, float2(t12.x, t3.y), 0).rgb) * w12.x * w3.y;
    r += P(hist.SampleLevel(linearClamp, float2(t3.x, t3.y), 0).rgb) * w3.x * w3.y;
    return r;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    const int2 hi = int2(width, height) - 1;
    const float4 c4 = cur[p];
    const float3 c = P(c4.rgb);
    if (reset != 0) { dst[p] = c4; return; }
    // neighbourhood statistics of the (noisy) current frame
    float3 m = 0, m2 = 0, mn = 1e9, mx = -1e9;
    [unroll] for (int j = -1; j <= 1; ++j)
        [unroll] for (int i = -1; i <= 1; ++i) {
            const float3 v = P(cur[clamp(p + int2(i, j), int2(0, 0), hi)].rgb);
            m += v; m2 += v * v; mn = min(mn, v); mx = max(mx, v);
        }
    m /= 9.0;
    const float3 sd = sqrt(max(m2 / 9.0 - m * m, 0.0));
    const float2 mv = motion[p].xy;
    const float2 q = float2(p) + 0.5 + mv;
    if (any(q < 0.0) || any(q > float2(width, height))) { dst[p] = c4; return; }
    const float3 h = SampleSharp(q, 1.0 / float2(width, height));
    // variance clipping, widened by the noise level, within the neighbourhood's range
    const float3 lo = max(m - (sd + sigma), mn - sigma), hiB = min(m + (sd + sigma), mx + sigma);
    const float3 hc = clamp(h, lo, hiB);
    // trust the history less where it disagrees with this frame by more than the noise would explain
    const float d = length(hc - c) / (1.732 * sigma * 3.5);
    const float a = maxBlend * exp(-d * d);
    const float3 r = lerp(c, hc, a);
    dst[p] = float4(r * r, c4.a);
}
