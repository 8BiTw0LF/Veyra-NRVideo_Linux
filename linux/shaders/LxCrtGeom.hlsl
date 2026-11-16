// VeyAmux for Linux: "Retro TV" - CRT-Geom (curved CRT with scanlines and an aperture-grille mask).
//
// Port of crt-geom as used by ShaderGlass (https://github.com/mausimus/ShaderGlass, preset crt/crt-geom), which imports
// libretro slang-shaders commit a4f3aeec04fcb2624ec6df5dd17e38f9b575eab9 crt/shaders/crt-geom.slang (preset
// crt/crt-geom.slangp: filter_linear = false, scale_type = viewport). ShaderGlass ships it only as compiled DirectX
// bytecode, so this is a port of that same source, with ShaderGlass' default parameters:
//
//     CRT-interlaced
//
//     Copyright (C) 2010-2012 cgwg, Themaister and DOLLS
//
//     This program is free software; you can redistribute it and/or modify it
//     under the terms of the GNU General Public License as published by the Free
//     Software Foundation; either version 2 of the License, or (at your option)
//     any later version.
//
//     (cgwg gave their consent to have the original version of this shader
//     distributed under the GPL in this message:
//
//     http://board.byuu.org/viewtopic.php?p=26075#p26075
//
//     "Feel free to distribute my shaders under the GPL. After all, the
//     barrel distortion code was taken from the Curvature shader, which is
//     under the GPL."
//     )
//     This shader variant is pre-configured with screen curvature
//
// VeyAmux changes (the picture is the same):
// - Compute shader, one thread per output pixel; the vertex-stage values are computed per pixel.
// - VeyAmux works in linear light: the source is gamma-encoded (2.2) before crt-geom's own CRTgamma, and the result is
//   written back as linear light (crt-geom ends with pow(1 / monitorgamma) for a 2.2 display, i.e. the same light).
// - fwidth() (OVERSAMPLE) comes from the neighbouring output pixel's position, as the rasteriser would compute it.
// - "Simulated height" (crt-geom's ysize) is set by VeyAmux (240 / 480 lines, or the source). A capture card delivers
//   such a picture scaled up to 1080p, so each simulated line averages the source rows it covers instead of picking one.
// - Only the default mask pattern (mask_type 1, aperture grille) is included.
cbuffer CrtParams : register(b0) {
    uint srcW; uint srcH;          // source picture (capture size)
    uint dstW; uint dstH;          // output
    uint frame;                    // FrameCount (interlace simulation)
    float simW; float simH;        // simulated size (crt-geom xsize / ysize); 0 = the source size
    uint pad0;
};
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);
SamplerState pointClamp : register(s0);

// ShaderGlass / libretro defaults
static const float CRTgamma = 2.4;
static const float monitorgamma = 2.2;
static const float d = 1.5;
static const float CURVATURE = 1.0;
static const float invert_aspect = 0.0;
static const float R = 2.0;
static const float cornersize = 0.03;
static const float cornersmooth = 1000.0;
static const float x_tilt = 0.0;
static const float y_tilt = 0.0;
static const float overscan_x = 100.0;
static const float overscan_y = 100.0;
static const float DOTMASK = 0.3;
static const float DOTMASK_brightboost = 0.0;
static const float SHARPER = 1.0;
static const float scanline_weight = 0.3;
static const float lum = 0.0;
static const float interlace_detect = 1.0;

#define PI 3.141592653589
static const float2 aspect = invert_aspect > 0.5 ? float2(0.75, 1.0) : float2(1.0, 0.75);

float2 FIX2(float2 c) { return max(abs(c), 1e-5); }
float4 FIX4(float4 c) { return max(abs(c), 1e-5); }
float FIX1(float c) { return max(abs(c), 1e-5); }

static float2 sinangle, cosangle;
static float3 stretch;

float intersect_(float2 xy) {
    float A = dot(xy, xy) + d * d;
    float B = 2.0 * (R * (dot(xy, sinangle) - d * cosangle.x * cosangle.y) - d * d);
    float C = d * d + 2.0 * R * d * cosangle.x * cosangle.y;
    return (-B - sqrt(B * B - 4.0 * A * C)) / (2.0 * A);
}

float2 bkwtrans(float2 xy) {
    float c = intersect_(xy);
    float2 pnt = (float2(c, c) * xy - float2(-R, -R) * sinangle) / float2(R, R);
    float2 poc = pnt / cosangle;
    float2 tang = sinangle / cosangle;
    float A = dot(tang, tang) + 1.0;
    float B = -2.0 * dot(poc, tang);
    float C = dot(poc, poc) - 1.0;
    float a = (-B + sqrt(B * B - 4.0 * A * C)) / (2.0 * A);
    float2 uv = (pnt - a * sinangle) / cosangle;
    float r = FIX1(R * acos(a));
    return uv * r / sin(r / R);
}

float2 fwtrans(float2 uv) {
    float r = FIX1(sqrt(dot(uv, uv)));
    uv *= sin(r / R) / r;
    float x = 1.0 - cos(r / R);
    float D = d / R + x * cosangle.x * cosangle.y + dot(uv, sinangle);
    return d * (uv * cosangle - x * sinangle) / D;
}

float3 maxscale() {
    float2 c = bkwtrans(-R * sinangle / (1.0 + R / d * cosangle.x * cosangle.y));
    float2 a = float2(0.5, 0.5) * aspect;
    float2 lo = float2(fwtrans(float2(-a.x, c.y)).x, fwtrans(float2(c.x, -a.y)).y) / aspect;
    float2 hi = float2(fwtrans(float2(+a.x, c.y)).x, fwtrans(float2(c.x, +a.y)).y) / aspect;
    return float3((hi + lo) * aspect * 0.5, max(hi.x - lo.x, hi.y - lo.y));
}

float4 scanlineWeights(float distance, float4 color) {
    float4 wid = 2.0 + 2.0 * pow(color, 4.0);
    float4 weights = distance / scanline_weight;
    return (lum + 1.4) * exp(-pow(weights * rsqrt(0.5 * wid), wid)) / (0.6 + 0.2 * wid);
}

float2 transform(float2 coord) {
    coord = (coord - float2(0.5, 0.5)) * aspect * stretch.z + stretch.xy;
    return (bkwtrans(coord) / float2(overscan_x / 100.0, overscan_y / 100.0) / aspect + float2(0.5, 0.5));
}

float corner(float2 coord) {
    coord = (coord - 0.5) * float2(overscan_x / 100.0, overscan_y / 100.0) + float2(0.5, 0.5);
    coord = min(coord, 1.0 - coord) * aspect;
    float2 cdist = cornersize;
    coord = (cdist - min(coord, cdist));
    float dist = sqrt(dot(coord, coord));
    return clamp((cdist.x - dist) * cornersmooth, 0.0, 1.0);
}

// mask_type 1: classic aperture for RGB panels (aperture_1_2_bgr)
float3 mask_weights_alpha(float2 coord, float mask_intensity, out float alpha) {
    float on = 1.0, off = 1.0 - mask_intensity;
    float3 green = float3(off, on, off);
    float3 magenta = float3(on, off, on);
    alpha = 3.0 / 6.0;
    return lerp(magenta, green, floor(fmod(coord.x, 2.0)));
}

static float2 simSize, simInv;   // crt-geom's SourceSize (simulated)
static uint taps;                // source rows averaged per simulated line

// TEX2D: source at c (crt-geom samples with a point filter), gamma-encoded for a 2.2 display, then CRTgamma.
float4 TEX2D(float2 c) {
    float3 s = 0;
    const float step = simInv.y / float(taps);
    for (uint k = 0; k < taps; ++k)
        s += src.SampleLevel(pointClamp, float2(c.x, c.y + (float(k) + 0.5) * step - 0.5 * simInv.y), 0).rgb;
    s = saturate(s / float(taps));
    return float4(pow(pow(s, 1.0 / 2.2), CRTgamma), 1.0);
}

float2 screenXY(float2 vTexCoord) { return CURVATURE > 0.5 ? transform(vTexCoord) : vTexCoord; }

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) return;
    const float2 OutputSize = float2(dstW, dstH);
    const float2 realSource = float2(srcW, srcH);
    simSize = float2(simW > 0.5 ? simW : float(srcW), simH > 0.5 ? simH : float(srcH));
    simInv = 1.0 / simSize;
    taps = clamp(uint(round(float(srcH) / simSize.y)), 1u, 8u);

    // vertex stage
    const float2 vTexCoord = (float2(id.xy) + 0.5) / OutputSize * 1.00001;
    sinangle = sin(float2(x_tilt, y_tilt));
    cosangle = cos(float2(x_tilt, y_tilt));
    stretch = maxscale();
    const float2 TextureSize = float2(SHARPER * simSize.x, simSize.y);
    const float2 ilfac = float2(1.0, clamp(floor(simSize.y / (interlace_detect > 0.5 ? 200.0 : 1000.0)), 1.0, 2.0));
    const float2 one = ilfac / TextureSize;

    // fragment stage
    float2 xy = screenXY(vTexCoord);
    float cval = corner(xy);
    float2 ilvec = float2(0.0, ilfac.y * interlace_detect > 1.5 ? fmod(float(frame), 2.0) : 0.0);
    float2 ratio_scale = (xy * TextureSize - float2(0.5, 0.5) + ilvec) / ilfac;
    // fwidth(ratio_scale.y): change to the next pixel to the right and below
    const float2 xyR = screenXY(vTexCoord + float2(1.0 / OutputSize.x, 0.0));
    const float2 xyD = screenXY(vTexCoord + float2(0.0, 1.0 / OutputSize.y));
    const float filter_ = abs(((xyR.y - xy.y) * TextureSize.y) / ilfac.y) + abs(((xyD.y - xy.y) * TextureSize.y) / ilfac.y);
    float2 uv_ratio = frac(ratio_scale);

    xy = (floor(ratio_scale) * ilfac + float2(0.5, 0.5) - ilvec) / TextureSize;

    float4 coeffs = PI * float4(1.0 + uv_ratio.x, uv_ratio.x, 1.0 - uv_ratio.x, 2.0 - uv_ratio.x);
    coeffs = FIX4(coeffs);
    coeffs = 2.0 * sin(coeffs) * sin(coeffs / 2.0) / (coeffs * coeffs);
    coeffs /= dot(coeffs, 1.0);

    // GLSL mat4(c0, c1, c2, c3) * v = c0 * v.x + c1 * v.y + c2 * v.z + c3 * v.w
    float4 col = clamp(TEX2D(xy + float2(-one.x, 0.0)) * coeffs.x + TEX2D(xy) * coeffs.y +
                       TEX2D(xy + float2(one.x, 0.0)) * coeffs.z + TEX2D(xy + float2(2.0 * one.x, 0.0)) * coeffs.w, 0.0, 1.0);
    float4 col2 = clamp(TEX2D(xy + float2(-one.x, one.y)) * coeffs.x + TEX2D(xy + float2(0.0, one.y)) * coeffs.y +
                        TEX2D(xy + one) * coeffs.z + TEX2D(xy + float2(2.0 * one.x, one.y)) * coeffs.w, 0.0, 1.0);

    float4 weights = scanlineWeights(uv_ratio.y, col);
    float4 weights2 = scanlineWeights(1.0 - uv_ratio.y, col2);
    // OVERSAMPLE
    uv_ratio.y = uv_ratio.y + 1.0 / 3.0 * filter_;
    weights = (weights + scanlineWeights(uv_ratio.y, col)) / 3.0;
    weights2 = (weights2 + scanlineWeights(abs(1.0 - uv_ratio.y), col2)) / 3.0;
    uv_ratio.y = uv_ratio.y - 2.0 / 3.0 * filter_;
    weights = weights + scanlineWeights(abs(uv_ratio.y), col) / 3.0;
    weights2 = weights2 + scanlineWeights(abs(1.0 - uv_ratio.y), col2) / 3.0;

    float3 mul_res = (col * weights + col2 * weights2).rgb * cval;

    // shadow mask (subpixel mask code)
    float alpha;
    float4 mask = float4(mask_weights_alpha(vTexCoord.xy * OutputSize, 1.0, alpha), 1.0);
    mask.a = alpha;
    const float2 u_tex_size1 = OutputSize / realSource;
    float nbright = 255.0 - 255.0 * mask.a;
    float fbright = nbright / (u_tex_size1.x * u_tex_size1.y);
    float aperture_average = lerp(1.0 - DOTMASK * (1.0 - DOTMASK_brightboost), 1.0, fbright);
    float3 clow = (1.0 - DOTMASK) * mul_res + DOTMASK * DOTMASK_brightboost * mul_res * mul_res;
    float ifbright = 1.0 / fbright;
    float3 chi = ifbright * aperture_average * mul_res - (ifbright - 1.0) * clow;
    float3 cout_ = lerp(clow, chi, mask.rgb);

    // crt-geom: pow(cout, 1 / monitorgamma) for a 2.2 display = cout as linear light
    dst[id.xy] = float4(max(cout_, 0.0), 1.0);
}
