// VeyAmux for Linux: NVIDIA Image Scaling (NIS) 1.0.x scaler (MIT, NVIDIAGameWorks/NVIDIAImageScaling) on VeyAmux's
// linear working image. NIS_HDR_MODE_LINEAR: NIS detects edges on a perceptual version internally and writes linear.
// Bindings follow VeyAmux's universal layout (t = 0.., u = 16.., b = 32, s1 = linear clamp).
#define NIS_HLSL 1
#define NIS_SCALER 1
#define NIS_HDR_MODE 1
#define NIS_BLOCK_WIDTH 32
#define NIS_BLOCK_HEIGHT 24
#define NIS_THREAD_GROUP_SIZE 256
#define NIS_CLAMP_OUTPUT 1

cbuffer cb : register(b0) {
    float kDetectRatio; float kDetectThres; float kMinContrastRatio; float kRatioNorm;
    float kContrastBoost; float kEps; float kSharpStartY; float kSharpScaleY;
    float kSharpStrengthMin; float kSharpStrengthScale; float kSharpLimitMin; float kSharpLimitScale;
    float kScaleX; float kScaleY;
    float kDstNormX; float kDstNormY; float kSrcNormX; float kSrcNormY;
    uint kInputViewportOriginX; uint kInputViewportOriginY; uint kInputViewportWidth; uint kInputViewportHeight;
    uint kOutputViewportOriginX; uint kOutputViewportOriginY; uint kOutputViewportWidth; uint kOutputViewportHeight;
    float reserved0; float reserved1;
};
SamplerState samplerLinearClamp : register(s1);
Texture2D in_texture : register(t0);
RWTexture2D<float4> out_texture : register(u0);
Texture2D coef_scaler : register(t1);
Texture2D coef_usm : register(t2);

#include "NIS_Scaler.h"

[numthreads(NIS_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 blockIdx : SV_GroupID, uint3 threadIdx : SV_GroupThreadID) {
    NVScaler(blockIdx.xy, threadIdx.x);
}
