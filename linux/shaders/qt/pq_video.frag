#version 440
// VeyAmux HDR output: the video layer (PQ-coded BT.709 light, 8 bits) -> extended sRGB coding relative to SDR white
// (203 nits = 1.0), so the window layer (hdr_window.frag) can treat video and interface the same way.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf { mat4 qt_Matrix; float qt_Opacity; };
layout(binding = 1) uniform sampler2D source;

vec3 pqDecodeNits(vec3 pq) {
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0, c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    vec3 e = pow(clamp(pq, 0.0, 1.0), vec3(1.0 / m2));
    return 10000.0 * pow(max(e - c1, 0.0) / (c2 - c3 * e), vec3(1.0 / m1));
}
vec3 srgbEncodeExt(vec3 x) {   // the sRGB curve, continued above 1.0
    vec3 lo = x * 12.92, hi = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, vec3(lessThanEqual(x, vec3(0.0031308))));
}
void main() {
    vec4 c = texture(source, qt_TexCoord0);
    if (c.a <= 0.0) { fragColor = vec4(0.0); return; }
    vec3 rel = pqDecodeNits(c.rgb / c.a) / 203.0;
    fragColor = vec4(srgbEncodeExt(rel) * c.a, c.a) * qt_Opacity;
}
