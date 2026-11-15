// VeyAmux for Linux: luma pyramid for the motion estimator of AMD FSR 3.1 upscaling.
//   mode 0: linear RGB (t0, RGBA16F) -> perceptual luma (u0, R16F), same size
//   mode 1: luma level k (t1) -> level k+1 (u0), 2x2 box filter (odd edges clamp)
cbuffer LumaParams : register(b0) { uint dstW; uint dstH; uint srcW; uint srcH; uint mode; uint p0; uint p1; uint p2; };
Texture2D<float4> rgb : register(t0);
Texture2D<float> lumaIn : register(t1);
RWTexture2D<float> lumaOut : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    if (mode == 0) {
        lumaOut[id.xy] = sqrt(max(dot(rgb[id.xy].rgb, float3(0.2126, 0.7152, 0.0722)), 0.0));
        return;
    }
    const int2 m = int2(srcW, srcH) - 1;
    const int2 s = int2(id.xy) * 2;
    lumaOut[id.xy] = 0.25 * (lumaIn[min(s, m)] + lumaIn[min(s + int2(1, 0), m)] + lumaIn[min(s + int2(0, 1), m)] + lumaIn[min(s + int2(1, 1), m)]);
}
