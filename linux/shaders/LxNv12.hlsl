// VeyAmux for Linux: the finished RGBA8 picture (sRGB-coded) -> NV12 (BT.709, limited range) for the hardware video
// encoder (instant replay / recording), downscaled to the recording size on the way. One thread per 2x2 luma block,
// which also writes that block's chroma sample (the average of the four pixels).
#include "PqCode.hlsli"
cbuffer Nv12Params : register(b0) { uint srcW; uint srcH; uint dstW; uint dstH; uint pqIn; uint p0; uint p1; uint p2; };
Texture2D<float4> src : register(t0);
RWTexture2D<float> lumaOut : register(u0);     // R8_UNORM,   dstW x dstH
RWTexture2D<float2> chromaOut : register(u1);  // R8G8_UNORM, dstW/2 x dstH/2
SamplerState linearClamp : register(s1);

float3 Fetch(uint2 p) {
    const float2 scale = float2(srcW, srcH) / float2(dstW, dstH);
    if (scale.x <= 1.0001 && scale.y <= 1.0001) return src[min(p, uint2(srcW, srcH) - 1)].rgb;
    const uint2 n = clamp(uint2(ceil(scale * 0.5)), uint2(1, 1), uint2(4, 4));
    float3 acc = 0;
    for (uint j = 0; j < n.y; ++j)
        for (uint i = 0; i < n.x; ++i)
            acc += src.SampleLevel(linearClamp, (float2(p) + (float2(i, j) + 0.5) / float2(n)) * scale / float2(srcW, srcH), 0).rgb;
    return acc / float(n.x * n.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint2 b = id.xy * 2;
    if (b.x >= dstW || b.y >= dstH) return;
    float3 sum = 0;
    [unroll] for (uint j = 0; j < 2; ++j)
        [unroll] for (uint i = 0; i < 2; ++i) {
            const uint2 p = min(b + uint2(i, j), uint2(dstW, dstH) - 1);
            const float3 f = Fetch(p);
            const float3 c = saturate(pqIn != 0 ? PqCodedToSdr(f) : f);
            sum += c;
            const float y = dot(c, float3(0.2126, 0.7152, 0.0722));
            lumaOut[p] = (16.0 + 219.0 * y) / 255.0;
        }
    const float3 c = sum * 0.25;
    const float y = dot(c, float3(0.2126, 0.7152, 0.0722));
    chromaOut[id.xy] = float2((128.0 + 224.0 * (c.b - y) / 1.8556) / 255.0, (128.0 + 224.0 * (c.r - y) / 1.5748) / 255.0);
}
