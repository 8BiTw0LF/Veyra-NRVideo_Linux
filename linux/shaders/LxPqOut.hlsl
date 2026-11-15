// VeyAmux for Linux: HDR display output. Linear working image (1.0 = nitsScale nits) -> PQ-coded BT.2020 RGBA8 with a little
// dither (8-bit PQ steps would otherwise band in smooth gradients). Qt's scRGB swapchain gets the light back through
// a small Qt Quick shader (qml/shaders/pq_video.frag).
#include "PqCode.hlsli"
cbuffer PqOutParams : register(b0) { uint width; uint height; float nitsScale; uint frame; uint wide; uint p0; uint p1; uint p2; };   // wide: input is already BT.2020
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

float Hash(uint2 p, uint f) {
    uint h = p.x * 1973u + p.y * 9277u + f * 26699u;
    h = (h ^ 61u) ^ (h >> 16); h *= 9u; h ^= h >> 4; h *= 0x27d4eb2du; h ^= h >> 15;
    return float(h & 0xffffu) / 65535.0;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    float3 c = max(src[id.xy].rgb, 0.0) * nitsScale;
    if (wide == 0) c = mul(k709To2020, c);   // SDR (BT.709) picture into the BT.2020 container
    const float3 pq = PqEncodeNits(c);
    const float d = (Hash(id.xy, frame) + Hash(id.xy + 7, frame + 3) - 1.0) / 255.0;   // triangular dither, +-1 LSB
    dst[id.xy] = float4(saturate(pq + d), 1.0);
}
