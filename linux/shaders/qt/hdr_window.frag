#version 440
// VeyAmux HDR output: the whole window (interface in normal sRGB coding, video in signed extended sRGB coding, both
// BT.709 relative to SDR white) -> the HDR swapchain:
//   mode 0: HDR10 (PQ, BT.2020)     mode 1: scRGB (FP16, linear BT.709, 1.0 = 80 nits)   (as PlayerBridge::hdrMode)
// SDR white is placed at 'white' nits.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf { mat4 qt_Matrix; float qt_Opacity; float white; float mode; };
layout(binding = 1) uniform sampler2D source;

vec3 srgbDecodeExt(vec3 c) {
    vec3 a = abs(c);
    vec3 lo = a / 12.92, hi = pow((a + 0.055) / 1.055, vec3(2.4));
    return sign(c) * mix(hi, lo, vec3(lessThanEqual(a, vec3(0.04045))));
}
vec3 pqEncodeNits(vec3 nits) {
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0, c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}
void main() {
    vec4 c = texture(source, qt_TexCoord0);
    vec3 rel = c.a > 0.0 ? srgbDecodeExt(c.rgb / c.a) : vec3(0.0);
    if (mode > 0.5) {
        fragColor = vec4(rel * (white / 80.0) * c.a, c.a) * qt_Opacity;
    } else {
        const mat3 to2020 = mat3(0.627404, 0.069097, 0.016391,     // column-major: BT.709 -> BT.2020
                                 0.329283, 0.919540, 0.088013,
                                 0.043313, 0.011362, 0.895595);
        vec3 pq = pqEncodeNits(max(to2020 * rel * white, 0.0));
        fragColor = vec4(pq * c.a, c.a) * qt_Opacity;
    }
}
