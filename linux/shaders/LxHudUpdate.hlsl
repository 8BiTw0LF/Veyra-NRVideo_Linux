// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors (https://github.com/DosukaSOL/PushFPS).
// Ported into Veyra on 2026-11-19 from PushFPS shaders/PushHudLockUpdate.hlsl (commit 3be641ca); taken into VeyAmux for
// Linux on 2026-11-20 from Veyra-NRVideo 2.2.0 shaders/HudLockUpdate.hlsl and adapted: VeyAmux's bindings, the previous
// real frame kept in a ping-pong pair here (u1 = this frame, read as t1 next time), sRGB / PQ codes only.
// HUD lock, step 1 (EnhanceChain::recordFg, FSR frame generation): per pixel, how long it has stayed the same (x, 0..1)
// and how much contrast it has with its neighbours (y, a held maximum that fades).
Texture2D<float4> currentTex : register(t0);
Texture2D<float4> previousTex : register(t1);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> stateTex : register(u0);   // x: still time, y: contrast
RWTexture2D<float4> nextPrevious : register(u1);
cbuffer Constants : register(b0) { uint width; uint height; uint reset; uint pq; };
float luma(float3 v) { return dot(v, float3(0.2126, 0.7152, 0.0722)); }
float3 at(int2 p) { return currentTex[clamp(p, int2(0, 0), int2(width - 1, height - 1))].rgb; }   // sRGB / PQ codes: perceptual

// 1/0.06: about 38 frames (1.3 s at 30 FPS) to reach 0.9 from zero.
static const float kRise = 0.06;
static const float kStill = 0.02;   // largest channel change still counted as the same pixel
static const float kFade = 0.97;    // held contrast fades about 3 % per frame

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 p = int2(id.xy);
    const float3 c = at(p);
    nextPrevious[id.xy] = float4(c, 1);
    if (reset != 0) { stateTex[id.xy] = float4(0, 0, 0, 0); return; }
    const float3 d = abs(c - previousTex[id.xy].rgb);
    const float change = max(d.r, max(d.g, d.b));
    const float l = luma(c);
    const float contrast = max(max(abs(l - luma(at(p + int2(1, 0)))), abs(l - luma(at(p - int2(1, 0))))),
                               max(abs(l - luma(at(p + int2(0, 1)))), abs(l - luma(at(p - int2(0, 1))))));
    float2 s = stateTex[id.xy].xy;
    s.x = change < kStill ? s.x + (1 - s.x) * kRise : 0;
    s.y = change < kStill ? max(contrast, s.y * kFade) : contrast;
    stateTex[id.xy] = float4(s, 0, 0);
}
