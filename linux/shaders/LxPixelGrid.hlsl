// VeyAmux for Linux: pixel-perfect scaling, step 1 - back to the game's own pixel grid.
// A capture card delivers e.g. a 320x240 game scaled up to 1920x1080 (often by a non-integer factor, with blur). Each
// output pixel here is one game pixel: the average of the source pixels it covers (box filter, linear light). The
// display then enlarges it by a whole number with nearest-neighbour sampling (VideoItem), so every game pixel becomes
// an exact, sharp block on screen.
cbuffer GridParams : register(b0) {
    uint srcW; uint srcH;   // source picture
    uint dstW; uint dstH;   // the game's pixel grid
};
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    // the source rectangle this grid pixel covers; the inner half of it is averaged, so the soft edges a scaler leaves
    // between game pixels do not bleed into the neighbours
    const float2 scale = float2(srcW, srcH) / float2(dstW, dstH);
    const float2 c = (float2(id.xy) + 0.5) * scale;
    const float2 half_ = max(scale * 0.25, 0.5);
    const int2 lo = int2(floor(c - half_)), hi = int2(ceil(c + half_)) - 1;
    float3 sum = 0;
    float n = 0;
    for (int y = max(lo.y, 0); y <= min(hi.y, int(srcH) - 1); ++y)
        for (int x = max(lo.x, 0); x <= min(hi.x, int(srcW) - 1); ++x) { sum += src[int2(x, y)].rgb; n += 1; }
    dst[id.xy] = float4(n > 0 ? sum / n : src[int2(min(uint(c.x), srcW - 1), min(uint(c.y), srcH - 1))].rgb, 1.0);
}
