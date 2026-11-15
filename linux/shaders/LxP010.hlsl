// VeyAmux for Linux: HDR capture (P010: 10-bit Y'CbCr 4:2:0, BT.2020 non-constant luminance, SMPTE ST 2084 / PQ) ->
// the linear SDR working image (1.0 = SDR white), written straight to the linear image (no 8-bit step in between).
// Uses Veyra's own HDR helpers: HdrDecodePq (PQ -> linear BT.709 in scRGB units, 1 = 80 nits) and HdrToSdr
// (BT.2390 EETF in the PQ domain with a Hermite shoulder, hue-preserving gamut compression), SDR white = 203 nits
// (ITU-R BT.2408).
#include "HdrColor.hlsli"
#include "HdrToSdr.hlsli"
cbuffer P010Params : register(b0) { uint width; uint height; uint fullRange; uint p0; float sourcePeak; float sdrWhite; float2 p1; };
Texture2D<float>  rawY : register(t0);        // R16_UNORM: 10 significant bits in the top of each 16-bit sample
Texture2D<float2> rawUV : register(t1);       // R16G16_UNORM, half resolution
RWTexture2D<float4> dst : register(u0);       // linear BT.709, 1.0 = SDR white

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    // 16-bit unorm -> 10-bit code value (P010 keeps the 10 bits in the most significant part)
    const float y10 = rawY[p] * 65535.0 / 64.0;
    const float2 c10 = rawUV[p >> 1] * 65535.0 / 64.0;
    float y, u, v;
    if (fullRange != 0) { y = y10 / 1023.0; u = (c10.x - 512.0) / 1023.0; v = (c10.y - 512.0) / 1023.0; }
    else { y = (y10 - 64.0) / 876.0; u = (c10.x - 512.0) / 896.0; v = (c10.y - 512.0) / 896.0; }
    // BT.2020 NCL Y'CbCr -> R'G'B' (PQ-coded)
    const float3 pq = saturate(float3(y + 1.4746 * v, y - 0.164553 * u - 0.571353 * v, y + 1.8814 * u));
    // linear BT.709 in nits; components outside the 709 gamut stay negative until HdrToSdr compresses the gamut
    const float3 sdr = HdrToSdr(HdrDecodePq(pq) * 80.0, sourcePeak, sdrWhite);
    dst[id.xy] = float4(max(sdr, 0.0), 1.0);
}
