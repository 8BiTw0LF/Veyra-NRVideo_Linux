// VeyAmux for Linux: AMD FidelityFX Super Resolution 1 (EASU edge-adaptive upscaling + RCAS sharpening), MIT,
// from the FidelityFX SDK 1.1.4 headers in third_party/fidelityfx (gpu/fsr1/ffx_fsr1.h, unchanged).
//   mode 0: EASU  t0 (linear RGBA16F, input size) -> u0 (output size)
//   mode 1: RCAS  t0 (output size)                -> u0 (output size), sharpness in stops (0 = strongest)
// FSR 1 expects perceptually encoded colour; the working image is linear, so values are taken to a gamma-2 encoding
// (sqrt) on the way in and squared again on the way out (AMD's recommended "gamma 2.0" input).
#define FFX_GPU 1
#define FFX_HLSL 1
#define FFX_HALF 0
#define FFX_FSR_EASU_FLOAT 1
#define FSR_RCAS_F 1
#define FSR_RCAS_PASSTHROUGH_ALPHA 1
#include "ffx_core.h"

cbuffer Fsr1Params : register(b0) { uint inW; uint inH; uint outW; uint outH; uint mode; float sharpness; uint p0; uint p1; };
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);
SamplerState linearClamp : register(s1);

FfxFloat32x4 FsrEasuRF(FfxFloat32x2 p) { return sqrt(max(src.GatherRed(linearClamp, p), 0.0)); }
FfxFloat32x4 FsrEasuGF(FfxFloat32x2 p) { return sqrt(max(src.GatherGreen(linearClamp, p), 0.0)); }
FfxFloat32x4 FsrEasuBF(FfxFloat32x2 p) { return sqrt(max(src.GatherBlue(linearClamp, p), 0.0)); }
FfxFloat32x4 FsrRcasLoadF(FfxInt32x2 p) {
    const float4 c = src.Load(int3(clamp(p, int2(0, 0), int2(outW, outH) - 1), 0));
    return float4(sqrt(max(c.rgb, 0.0)), c.a);
}
void FsrRcasInputF(inout FfxFloat32 r, inout FfxFloat32 g, inout FfxFloat32 b) {}

#include "fsr1/ffx_fsr1.h"

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= outW || id.y >= outH) return;
    if (mode == 0) {
        FfxUInt32x4 con0, con1, con2, con3;
        ffxFsrPopulateEasuConstants(con0, con1, con2, con3, float(inW), float(inH), float(inW), float(inH), float(outW), float(outH));
        FfxFloat32x3 c;
        ffxFsrEasuFloat(c, id.xy, con0, con1, con2, con3);
        c = max(c, 0.0);
        dst[id.xy] = float4(c * c, src.SampleLevel(linearClamp, (float2(id.xy) + 0.5) / float2(outW, outH), 0).a);
    } else {
        // FsrRcasCon() takes its output by value in the HLSL build of the header, so the constant is set up here
        // exactly as it does: stops -> linear sharpness in con.x.
        const FfxUInt32x4 con = FfxUInt32x4(asuint(exp2(-sharpness)), 0, 0, 0);
        float r, g, b, a;
        FsrRcasF(r, g, b, a, id.xy, con);
        const float3 c = max(float3(r, g, b), 0.0);
        dst[id.xy] = float4(c * c, a);
    }
}
