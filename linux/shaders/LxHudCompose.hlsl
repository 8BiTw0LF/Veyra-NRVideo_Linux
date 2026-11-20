// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS shaders/PushHudLockCompose.hlsl (commit 3be641ca); taken into VeyAmux for
// Linux on 2026-11-20 from Veyra-NRVideo 2.2.0 shaders/HudLockCompose.hlsl and adapted to VeyAmux's bindings.
// HUD lock, step 2: a generated frame takes its locked pixels from the real frame. A pixel is locked when it has been
// still for about a second itself and it, or a neighbour that is just as still, has an edge: the inside of a thin
// stroke and the pixel beside an edge belong to the overlay too. A pixel that moves is never locked.
Texture2D<float4> generatedTex : register(t0);
Texture2D<float4> heldTex : register(t1);
Texture2D<float4> stateTex : register(t2);
RWTexture2D<float4> outputTex : register(u0);
cbuffer Constants : register(b0) { uint width; uint height; uint p0; uint p1; };
float stillOf(float2 s) { return smoothstep(0.80, 0.95, s.x); }
float edgeOf(float2 s) { return smoothstep(0.04, 0.10, s.y); }
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float still = stillOf(stateTex[id.xy].xy);
    float edge = 0;
    if (still > 0) {
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x) {
                const float2 q = stateTex[clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(width - 1, height - 1))].xy;
                edge = max(edge, edgeOf(q) * stillOf(q));
            }
    }
    const float w = still * edge;
    const float4 g = generatedTex[id.xy];
    outputTex[id.xy] = w > 0 ? float4(lerp(g.rgb, heldTex[id.xy].rgb, w), g.a) : g;
}
