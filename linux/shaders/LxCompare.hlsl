// VeyAmux for Linux: before/after comparison clips (after PushFPS's comparison clips, GPL-3.0-or-later).
// One recorded picture made of the original (the capture as it arrived, before any enhancement: the working image in
// linear light, the last real frame) and VeyAmux's finished picture (encoded like the window: sRGB, or PQ for HDR
// output). Layouts:
//   1 split          left half the original, right half VeyAmux, a line between them
//   2 side by side   both whole, at half size, on black; the labels in the bar above each
//   3 vertical       1080 x 1920 for TikTok / Shorts / Reels: the original on top, VeyAmux below, each the middle of
//                    the picture cropped to 9:8; the labels beside the divider
//   4 reveal         the whole picture; a glowing line sweeps across and uncovers VeyAmux (left of the line)
// The labels are small RGBA pictures (drawn by Qt), laid over with their alpha.
#include "PqCode.hlsli"
cbuffer CompareParams : register(b0) {
    uint dstW; uint dstH; uint mode; uint pq;
    float nits; float sweep; uint labAW; uint labAH;
    uint labBW; uint labBH; float labScale; float pad0;
};
Texture2D<float4> original : register(t0);   // linear light, 1.0 = `nits`
Texture2D<float4> enhanced : register(t1);   // encoded like the window
Texture2D<float4> labelA : register(t2);
Texture2D<float4> labelB : register(t3);
SamplerState linearClamp : register(s1);
RWTexture2D<float4> dst : register(u0);

float3 encodeOriginal(float3 lin) {
    if (pq != 0) return PqEncodeNits(max(lin, 0.0) * nits);
    return SrgbEncode(lin);
}
float3 sampleOriginal(float2 uv) { return encodeOriginal(original.SampleLevel(linearClamp, uv, 0).rgb); }
float3 sampleEnhanced(float2 uv) { return enhanced.SampleLevel(linearClamp, uv, 0).rgb; }
// white in the output's encoding
float3 white() { return pq != 0 ? PqEncodeNits(float3(203.0, 203.0, 203.0)) : float3(1.0, 1.0, 1.0); }

// a label at (x0, y0) in output pixels, scaled by labScale
float4 labelAt(Texture2D<float4> lab, uint lw, uint lh, float2 p, float2 origin) {
    if (lw == 0) return 0;
    const float2 q = (p - origin) / labScale;
    if (q.x < 0 || q.y < 0 || q.x >= lw || q.y >= lh) return 0;
    float4 c = lab.SampleLevel(linearClamp, q / float2(lw, lh), 0);
    if (pq != 0) c.rgb = PqEncodeNits(pow(saturate(c.rgb), 2.2) * 203.0);
    return c;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    const float2 p = float2(id.xy) + 0.5;
    const float2 size = float2(dstW, dstH);
    const float unit = max(1.0, dstH / 1080.0);
    float3 c = 0;
    float4 lab = 0;
    const float margin = 24.0 * unit;
    const float labAWs = labAW * labScale, labBWs = labBW * labScale, labHs = max(labAH, labBH) * labScale;
    if (mode == 1) {   // split
        const float mid = size.x * 0.5;
        const float2 uv = p / size;
        c = p.x < mid ? sampleOriginal(uv) : sampleEnhanced(uv);
        const float d = abs(p.x - mid);
        if (d < 1.5 * unit) c = white();
        else if (d < 2.5 * unit) c *= 0.15;
        lab = labelAt(labelA, labAW, labAH, p, float2(margin, margin));
        if (lab.a == 0) lab = labelAt(labelB, labBW, labBH, p, float2(size.x - margin - labBWs, margin));
    } else if (mode == 2) {   // side by side: two half-size pictures, centred vertically
        const float halfW = size.x * 0.5;
        const float picH = halfW * size.y / size.x;
        const float top = (size.y - picH) * 0.5;
        if (p.y >= top && p.y < top + picH) {
            const bool right = p.x >= halfW;
            const float2 uv = float2((p.x - (right ? halfW : 0)) / halfW, (p.y - top) / picH);
            c = right ? sampleEnhanced(uv) : sampleOriginal(uv);
        }
        const float ly = max(margin * 0.5, top - labHs - margin * 0.5);
        lab = labelAt(labelA, labAW, labAH, p, float2(halfW * 0.5 - labAWs * 0.5, ly));
        if (lab.a == 0) lab = labelAt(labelB, labBW, labBH, p, float2(halfW * 1.5 - labBWs * 0.5, ly));
    } else if (mode == 3) {   // vertical 9:16: the middle of the picture cropped to 9:8, original on top
        uint sw, sh;
        original.GetDimensions(sw, sh);
        const float halfH = size.y * 0.5;
        const bool bottom = p.y >= halfH;
        const float boxAspect = size.x / halfH;                  // 9:8
        const float srcAspect = float(sw) / float(sh);
        const float cropW = min(1.0, boxAspect / srcAspect);     // share of the source width that is kept
        const float2 local = float2(p.x / size.x, (p.y - (bottom ? halfH : 0)) / halfH);
        const float2 uv = float2(0.5 + (local.x - 0.5) * cropW, local.y);
        c = bottom ? sampleEnhanced(uv) : sampleOriginal(uv);
        const float d = abs(p.y - halfH);
        if (d < 2.0 * unit) c = white();
        else if (d < 3.0 * unit) c *= 0.15;
        lab = labelAt(labelA, labAW, labAH, p, float2(margin, halfH - labAH * labScale - margin * 0.75));
        if (lab.a == 0) lab = labelAt(labelB, labBW, labBH, p, float2(margin, halfH + margin * 0.75));
    } else {   // reveal: VeyAmux left of the sweeping line, the original right of it
        const float x = sweep * size.x;
        const float2 uv = p / size;
        c = p.x < x ? sampleEnhanced(uv) : sampleOriginal(uv);
        const float d = abs(p.x - x);
        const float glow = exp(-d / (6.0 * unit));
        c = lerp(c, white(), saturate(glow * 0.6 + (d < 1.5 * unit ? 1.0 : 0.0)));
        if (sweep > 0.02) lab = labelAt(labelB, labBW, labBH, p, float2(margin, margin));
        if (lab.a == 0 && sweep < 0.98) lab = labelAt(labelA, labAW, labAH, p, float2(size.x - margin - labAWs, margin));
    }
    c = lerp(c, lab.rgb, lab.a);
    dst[id.xy] = float4(c, 1.0);
}
