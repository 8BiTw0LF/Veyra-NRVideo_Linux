// VeyAmux for Linux: SMPTE ST 2084 (PQ) helpers for the HDR display output.
// In HDR output mode the finished picture (out8_) holds PQ-coded BT.709 light (8 bits, dithered); Qt draws it into
// an scRGB swapchain. The OBS / recording / screenshot copies convert it back to SDR with PqCodedToSdr().
#include "HdrToSdr.hlsli"
static const float3x3 k709To2020 = float3x3(0.627404, 0.329283, 0.043313, 0.069097, 0.919540, 0.011362, 0.016391, 0.088013, 0.895595);
static const float3x3 k2020To709 = float3x3(1.660491, -0.587641, -0.072850, -0.124550, 1.132900, -0.008349, -0.018151, -0.100579, 1.118730);
// HDR output with wide colour: tone-map the luminance to the display (BT.2390, like HdrToSdr) but keep the colour
// (no gamut compression into BT.709). In: linear BT.709 nits (signed outside 709). Out: linear BT.2020, 1 = targetPeak.
float3 HdrToDisplay2020(float3 linear709Nits, float sourcePeak, float targetPeak) {
    const float y = dot(linear709Nits, float3(.212639, .715169, .072192));
    if (y <= 0) return 0;
    const float mapped = ToneLuminance(y, sourcePeak, targetPeak);
    return saturate(mul(k709To2020, linear709Nits * (mapped / y)));
}
static const float kPqM1 = 2610.0 / 16384.0, kPqM2 = 2523.0 / 32.0;
static const float kPqC1 = 3424.0 / 4096.0, kPqC2 = 2413.0 / 128.0, kPqC3 = 2392.0 / 128.0;

float3 PqEncodeNits(float3 nits) {
    const float3 y = pow(saturate(nits / 10000.0), kPqM1);
    return pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), kPqM2);
}
float3 PqDecodeNits(float3 pq) {
    const float3 e = pow(saturate(pq), 1.0 / kPqM2);
    return 10000.0 * pow(max(e - kPqC1, 0.0) / (kPqC2 - kPqC3 * e), 1.0 / kPqM1);
}
float3 SrgbEncode(float3 x) {
    x = saturate(x);
    return select(x <= 0.0031308, x * 12.92, 1.055 * pow(x, 1.0 / 2.4) - 0.055);
}
// PQ-coded BT.2020 picture -> sRGB-coded SDR BT.709 (203 nits = white; a soft shoulder on the luminance keeps
// highlights, Veyra's ToneGamut brings wide colours into BT.709 without hue shifts)
float3 PqCodedToSdr(float3 pq) {
    const float3 x = mul(k2020To709, PqDecodeNits(pq)) / 203.0;
    const float y = dot(x, float3(.212639, .715169, .072192));
    if (y <= 0) return 0;
    const float knee = 0.75;
    const float m = y <= knee ? y : knee + (1.0 - knee) * (1.0 - exp(-(y - knee) / (1.0 - knee)));
    return SrgbEncode(ToneGamut(x * (m / y), m));
}
