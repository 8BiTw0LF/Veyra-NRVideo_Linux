// VeyAmux for Linux: protects static on-screen elements (HUD, text, mini-maps, logos) from frame interpolation.
//   mode 0 (before interpolation): per pixel, count how many frames in a row it has not changed (u0). Pixels that have
//          been unchanged for a while are "static": their motion vector is forced to zero (u1), so FSR does not drag
//          them along with the moving background behind them.
//   mode 1 (after interpolation): static pixels are identical in both real frames, so the generated frame gets the
//          real pixel (u2 <- t0), blended by how much of the 3x3 neighbourhood is static to avoid hard seams.
//          Moving background next to static elements: interpolation fetches it from p - mv/2 in the current frame and
//          p + mv/2 in the previous one; when one of those spots is covered by a static element, it would pull
//          pieces of the text/HUD along ("ghosts"). Such pixels are taken from the other frame only.
cbuffer StaticParams : register(b0) { uint width; uint height; uint mode; float needFrames; };
Texture2D<float4> cur : register(t0);
Texture2D<float4> prev : register(t1);
RWTexture2D<float> stable : register(u0);
RWTexture2D<float4> motion : register(u1);
RWTexture2D<float4> generated : register(u2);
SamplerState linearClamp : register(s1);

bool StaticNear(float2 q, float need) {
    // static within one pixel of q (covers the anti-aliased rim of text)
    const int2 c = int2(floor(q + 0.5));
    [unroll] for (int j = -1; j <= 1; ++j)
        [unroll] for (int i = -1; i <= 1; ++i)
            if (stable[clamp(c + int2(i, j), int2(0, 0), int2(width, height) - 1)] >= need) return true;
    return false;
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    if (mode == 0) {
        const float3 d = abs(cur[p].rgb - prev[p].rgb);
        float n = max(max(d.r, d.g), d.b) <= 2.5 / 255.0 ? min(stable[p] + 1.0, 250.0) : 0.0;
        // Only textured pixels can be "static UI": a flat area of a panning picture also stays unchanged pixel by
        // pixel, but it needs no protection (it interpolates correctly) and treating it as UI would pin it in place.
        float lo = 1.0, hi = 0.0;
        [unroll] for (int j = -1; j <= 1; ++j)
            [unroll] for (int i = -1; i <= 1; ++i) {
                const float l = dot(cur[clamp(p + int2(i, j), int2(0, 0), int2(width, height) - 1)].rgb, float3(0.299, 0.587, 0.114));
                lo = min(lo, l); hi = max(hi, l);
            }
        if (hi - lo < 0.12) n = min(n, needFrames - 1.0);
        stable[p] = n;
        if (n >= needFrames) motion[p] = float4(0, 0, 1, 0);
        return;
    }
    float s = 0;
    [unroll] for (int j = -1; j <= 1; ++j)
        [unroll] for (int i = -1; i <= 1; ++i)
            s += stable[clamp(p + int2(i, j), int2(0, 0), int2(width, height) - 1)] >= needFrames ? 1.0 : 0.0;
    const float w = stable[p] >= needFrames ? saturate(s / 6.0) : 0.0;
    if (w > 0) {
        const float4 g = generated[p];
        generated[p] = float4(lerp(g.rgb, cur[p].rgb, w), g.a);
        return;
    }
    const float2 mv = motion[p].xy;
    if (dot(mv, mv) < 0.25) return;
    const float2 qc = float2(p) - 0.5 * mv, qp = float2(p) + 0.5 * mv;
    const bool sc = StaticNear(qc, needFrames), sp = StaticNear(qp, needFrames);
    if (!sc && !sp) return;   // the normal case
    if (sc && sp) {           // a gap inside the text in both frames: the real pixel beats interpolated text pieces
        generated[p] = float4(cur[p].rgb, generated[p].a);
        return;
    }
    const float2 inv = 1.0 / float2(width, height);
    const float3 one = sc ? prev.SampleLevel(linearClamp, (qp + 0.5) * inv, 0).rgb : cur.SampleLevel(linearClamp, (qc + 0.5) * inv, 0).rgb;
    generated[p] = float4(one, generated[p].a);
}
