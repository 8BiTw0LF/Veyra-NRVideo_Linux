// VeyAmux for Linux: HDR video (decoded to 16-bit R'G'B' in BT.2020 by FFmpeg) -> the linear SDR working image.
//   mode 1: PQ (SMPTE ST 2084, HDR10)      mode 2: HLG (ARIB STD-B67, BT.2100; 1000-nit display, system gamma 1.2)
// Tone mapping as for HDR capture (LxP010): Veyra's HdrToSdr (BT.2390 EETF, hue-preserving gamut compression),
// SDR white = 203 nits.
#include "HdrColor.hlsli"
#include "PqCode.hlsli"
cbuffer HdrRgbParams : register(b0) { uint width; uint height; uint mode; uint wide; float sourcePeak; float sdrWhite; float2 p1; };
Texture2D<float4> src : register(t0);     // R16G16B16A16_UNORM, full-range R'G'B'
RWTexture2D<float4> dst : register(u0);   // linear BT.709, 1.0 = SDR white

float HlgInverseOetf(float e) {
    const float a = 0.17883277, b = 0.28466892, c = 0.55991073;
    return e <= 0.5 ? e * e / 3.0 : (exp((e - c) / a) + b) / 12.0;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float4 e = src[id.xy];
    float3 nits709;
    float peak = sourcePeak;
    if (mode == 2) {
        // HLG: scene light -> display light (OOTF) for a 1000-nit reference display, then BT.2020 -> BT.709
        const float3 sc = float3(HlgInverseOetf(saturate(e.r)), HlgInverseOetf(saturate(e.g)), HlgInverseOetf(saturate(e.b)));
        const float ys = dot(sc, float3(0.2627, 0.6780, 0.0593));
        const float3 disp2020 = 1000.0 * pow(max(ys, 1e-6), 0.2) * sc;
        nits709 = HdrTo709(disp2020);
        peak = 1000.0;
    } else {
        nits709 = HdrDecodePq(e.rgb) * 80.0;   // PQ -> linear BT.709 in nits
    }
    dst[id.xy] = float4(wide != 0 ? HdrToDisplay2020(nits709, peak, sdrWhite) : max(HdrToSdr(nits709, peak, sdrWhite), 0.0), 1.0);
}
