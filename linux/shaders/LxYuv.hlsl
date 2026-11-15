// VeyAmux for Linux: GPU colour conversion of raw capture frames (no CPU conversion, like the Windows capture path).
//   mode 0: YUYV (Y0 U Y1 V)   mode 1: UYVY (U Y0 V Y1)   mode 2: NV12 (Y plane + interleaved UV plane)
// Output: sRGB-coded RGBA8 in the same form the file path produces, so RgbToLinear etc. follow unchanged.
cbuffer YuvParams : register(b0) { uint width; uint height; uint mode; uint fullRange; uint bt709; uint3 pad; };
Texture2D<float4> rawPacked : register(t0);   // YUYV/UYVY: one texel = two pixels
Texture2D<float>  rawY : register(t1);        // NV12 luma
Texture2D<float2> rawUV : register(t2);       // NV12 chroma (half resolution)
RWTexture2D<float4> dst : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    float y, u, v;
    if (mode == 0) {
        const float4 t = rawPacked[int2(p.x >> 1, p.y)];
        y = (p.x & 1) ? t.b : t.r; u = t.g; v = t.a;
    } else if (mode == 1) {
        const float4 t = rawPacked[int2(p.x >> 1, p.y)];
        y = (p.x & 1) ? t.a : t.g; u = t.r; v = t.b;
    } else {
        y = rawY[p];
        const float2 c = rawUV[p >> 1];
        u = c.x; v = c.y;
    }
    if (fullRange != 0) { u -= 0.5; v -= 0.5; }
    else { y = (y * 255.0 - 16.0) / 219.0; u = (u * 255.0 - 128.0) / 224.0; v = (v * 255.0 - 128.0) / 224.0; }
    float3 rgb;
    if (bt709 != 0) rgb = float3(y + 1.5748 * v, y - 0.18732 * u - 0.46812 * v, y + 1.8556 * u);
    else            rgb = float3(y + 1.4020 * v, y - 0.34414 * u - 0.71414 * v, y + 1.7720 * u);
    dst[id.xy] = float4(saturate(rgb), 1.0);
}
