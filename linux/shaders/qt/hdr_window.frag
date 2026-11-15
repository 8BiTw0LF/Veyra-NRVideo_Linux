#version 440
// VeyAmux HDR output: the whole window (interface in normal sRGB coding, video already in extended sRGB coding)
// -> scRGB linear light for the HDR swapchain (1.0 = 80 nits), with SDR white placed at 'white' nits.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf { mat4 qt_Matrix; float qt_Opacity; float white; };
layout(binding = 1) uniform sampler2D source;

vec3 srgbDecodeExt(vec3 c) {
    vec3 lo = c / 12.92, hi = pow(max(c + 0.055, vec3(0.0)) / 1.055, vec3(2.4));
    return mix(hi, lo, vec3(lessThanEqual(c, vec3(0.04045))));
}
void main() {
    vec4 c = texture(source, qt_TexCoord0);
    vec3 rgb = c.a > 0.0 ? srgbDecodeExt(c.rgb / c.a) * c.a : vec3(0.0);
    fragColor = vec4(rgb * (white / 80.0), c.a) * qt_Opacity;
}
