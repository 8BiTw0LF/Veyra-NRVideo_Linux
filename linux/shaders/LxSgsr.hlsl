// VeyAmux for Linux: Snapdragon Game Super Resolution 1 (SGSR 1, Qualcomm, BSD-3-Clause) as a compute shader.
// Single pass, edge-adaptive spatial upscaler: a bilinear base with a Lanczos-2-shaped correction along edges, from
// 12 neighbouring samples (third_party/sgsr/sgsr1_mobile.h, unmodified; the edge-direction variant is used).
// VeyAmux's working image is linear light and may exceed 1 (HDR); SGSR expects a perceptual 0..1 signal, so every
// sample is taken as sqrt (close to gamma 2) and the result squared back, and the final clamp to 1 becomes a clamp to
// 0 so HDR highlights survive (SGSR clamps the correction to the 4 nearest samples anyway).
cbuffer SgsrParams : register(b0) { uint srcW; uint srcH; uint dstW; uint dstH; };
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float4> dst : register(u0);
SamplerState samLinearClamp : register(s1);

half4 Perceptual(half4 v) { return sqrt(max(v, half4(0, 0, 0, 0))); }
half4 SGSRRGBH(float2 p) { return Perceptual(InputTexture.SampleLevel(samLinearClamp, p, 0)); }
half4 SGSRH(float2 p, uint channel) {
    if (channel == 0) return Perceptual(InputTexture.GatherRed(samLinearClamp, p));
    if (channel == 1) return Perceptual(InputTexture.GatherGreen(samLinearClamp, p));
    if (channel == 2) return Perceptual(InputTexture.GatherBlue(samLinearClamp, p));
    return Perceptual(InputTexture.GatherAlpha(samLinearClamp, p));
}

#define SGSR_MOBILE
#define UseEdgeDirection
#define saturate(x) max((x), 0.0)
#include "sgsr1_mobile.h"
#undef saturate

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    const float2 uv = (float2(id.xy) + 0.5) / float2(dstW, dstH);
    const float4 con1 = float4(1.0 / float(srcW), 1.0 / float(srcH), float(srcW), float(srcH));
    half4 pix = half4(0, 0, 0, 1);
    SgsrYuvH(pix, uv, con1);
    const float a = InputTexture.SampleLevel(samLinearClamp, uv, 0).a;
    dst[id.xy] = float4(float3(pix.rgb) * float3(pix.rgb), a);
}
