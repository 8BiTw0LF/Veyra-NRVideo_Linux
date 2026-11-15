// VeyAmux for Linux: area downscale of the finished RGBA8 picture for the OBS browser stream (or a plain copy when the
// sizes match). Averages a grid of bilinear taps covering each output pixel's footprint.
#include "PqCode.hlsli"
cbuffer DownParams : register(b0) { uint srcW; uint srcH; uint dstW; uint dstH; uint pqIn; uint p0; uint p1; uint p2; };   // pqIn: HDR output mode
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);
SamplerState linearClamp : register(s1);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    const float2 scale = float2(srcW, srcH) / float2(dstW, dstH);
    if (scale.x <= 1.0001 && scale.y <= 1.0001) {
        const float3 c = src[min(id.xy, uint2(srcW, srcH) - 1)].rgb;
        dst[id.xy] = float4(pqIn != 0 ? PqCodedToSdr(c) : c, 1.0);
        return;
    }
    // taps every ~2 source pixels (each bilinear tap already averages 2x2)
    const uint2 n = clamp(uint2(ceil(scale * 0.5)), uint2(1, 1), uint2(4, 4));
    float3 acc = 0;
    for (uint j = 0; j < n.y; ++j)
        for (uint i = 0; i < n.x; ++i) {
            const float2 p = (float2(id.xy) + (float2(i, j) + 0.5) / float2(n)) * scale;   // source pixel coordinates
            acc += src.SampleLevel(linearClamp, p / float2(srcW, srcH), 0).rgb;
        }
    const float3 c = acc / float(n.x * n.y);
    dst[id.xy] = float4(pqIn != 0 ? PqCodedToSdr(c) : c, 1.0);
}
