// VeyAmux for Linux: confidence-weighted fill of the motion field (see LxLkLevel). Pixels whose vector is unreliable
// (flat area, aperture problem, poor match) take the confidence-weighted average of their neighbours within 4 pixels
// of this pyramid level; confident vectors stay as they are. Run on every level, so on coarse levels it spreads motion
// across large flat regions.
cbuffer FillParams : register(b0) { uint width; uint height; float keepAbove; uint p0; };
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    const float4 c = src[p];
    if (c.z >= keepAbove) { dst[p] = c; return; }
    float3 acc = float3(c.xy * c.z, c.z);
    float cmax = c.z;
    const int R = 4;
    for (int j = -R; j <= R; ++j) {
        for (int i = -R; i <= R; ++i) {
            if (i == 0 && j == 0) continue;
            const float4 n = src[clamp(p + int2(i, j), int2(0, 0), int2(width, height) - 1)];
            const float w = n.z * exp(-0.08 * float(i * i + j * j));
            acc += float3(n.xy * w, w);
            cmax = max(cmax, n.z);
        }
    }
    if (acc.z < 1e-4) { dst[p] = c; return; }
    // the filled vector inherits a reduced confidence so later passes/levels can still improve it
    dst[p] = float4(acc.xy / acc.z, 0.5 * max(c.z, min(cmax, keepAbove)), 0);
}
