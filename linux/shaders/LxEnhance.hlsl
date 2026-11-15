// VeyAmux for Linux: adaptive contrast sharpen + exposure/contrast/saturation on the linear working image.
cbuffer EnhanceParams : register(b0) {
    uint width; uint height;
    float sharpen;    // 0..1
    float exposure;   // EV
    float contrast;   // 1 = neutral
    float saturation; // 1 = neutral
    float2 pad;
};
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

float3 ToP(float3 c) { return sqrt(max(c, 0.0)); }
float3 FromP(float3 p) { return p * p; }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    const int2 hi = int2(width, height) - 1;
    const float4 centre = src[p];
    float3 col = centre.rgb;
    if (sharpen > 0.001) {
        const float3 b = ToP(src[clamp(p + int2(0, -1), int2(0, 0), hi)].rgb);
        const float3 d = ToP(src[clamp(p + int2(-1, 0), int2(0, 0), hi)].rgb);
        const float3 f = ToP(src[clamp(p + int2(1, 0), int2(0, 0), hi)].rgb);
        const float3 h = ToP(src[clamp(p + int2(0, 1), int2(0, 0), hi)].rgb);
        const float3 e = ToP(col);
        const float3 mn = min(min(min(b, d), min(e, f)), h);
        const float3 mx = max(max(max(b, d), max(e, f)), h);
        // Contrast-adaptive weight: flat areas get more sharpening, already-crisp edges less.
        const float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
        const float peak = -1.0 / lerp(8.0, 5.0, saturate(sharpen));
        const float3 wgt = amp * peak;
        const float3 res = ((b + d + f + h) * wgt + e) / (1.0 + 4.0 * wgt);
        col = FromP(saturate(res));
    }
    col *= exp2(exposure);
    if (abs(contrast - 1.0) > 1e-3) col = FromP(max((ToP(col) - 0.5) * contrast + 0.5, 0.0));
    if (abs(saturation - 1.0) > 1e-3) {
        const float l = dot(col, float3(0.2126, 0.7152, 0.0722));
        col = lerp(float3(l, l, l), col, saturation);
    }
    dst[id.xy] = float4(max(col, 0.0), centre.a);
}
