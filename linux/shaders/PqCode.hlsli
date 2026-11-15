// VeyAmux for Linux: SMPTE ST 2084 (PQ) helpers for the HDR display output.
// In HDR output mode the finished picture (out8_) holds PQ-coded BT.709 light (8 bits, dithered); Qt draws it into
// an scRGB swapchain. The OBS / recording / screenshot copies convert it back to SDR with PqCodedToSdr().
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
// PQ-coded picture -> sRGB-coded SDR (203 nits = white, a soft shoulder keeps highlights instead of clipping them)
float3 PqCodedToSdr(float3 pq) {
    const float3 x = PqDecodeNits(pq) / 203.0;
    const float knee = 0.75;
    const float3 hi = knee + (1.0 - knee) * (1.0 - exp(-(x - knee) / (1.0 - knee)));
    return SrgbEncode(select(x <= knee, x, hi));
}
