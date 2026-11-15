// VeyAmux for Linux: Lanczos-3 upscaler (linear RGBA16F in/out) with anti-ringing clamp.
// Same binding convention as the Windows shaders (t0 / u0 / b0) so DXC's -fvk-*-shift flags apply.
cbuffer LanczosParams : register(b0) { uint srcW; uint srcH; uint dstW; uint dstH; };
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

float Lanczos3(float x) {
    x = abs(x);
    if (x < 1e-4) return 1.0;
    if (x >= 3.0) return 0.0;
    const float px = 3.14159265 * x;
    return 3.0 * sin(px) * sin(px / 3.0) / (px * px);
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    const float2 scale = float2(srcW, srcH) / float2(dstW, dstH);
    const float2 pos = (float2(id.xy) + 0.5) * scale - 0.5;
    const int2 base = int2(floor(pos));
    const float2 f = pos - float2(base);
    const int2 last = int2(srcW, srcH) - 1;
    float wx[6]; float wy[6];
    [unroll] for (int i = 0; i < 6; ++i) { wx[i] = Lanczos3(f.x - float(i - 2)); wy[i] = Lanczos3(f.y - float(i - 2)); }
    float4 sum = 0; float wsum = 0;
    float4 mn = float4(1e9, 1e9, 1e9, 1e9), mx = float4(-1e9, -1e9, -1e9, -1e9);
    [unroll] for (int y = 0; y < 6; ++y) {
        [unroll] for (int x = 0; x < 6; ++x) {
            const float4 s = src[clamp(base + int2(x - 2, y - 2), int2(0, 0), last)];
            const float w = wx[x] * wy[y];
            sum += s * w; wsum += w;
            if (x >= 2 && x <= 3 && y >= 2 && y <= 3) { mn = min(mn, s); mx = max(mx, s); }
        }
    }
    float4 r = sum / max(wsum, 1e-6);
    r = clamp(r, mn, mx);
    dst[id.xy] = float4(max(r.rgb, 0.0), r.a);
}
