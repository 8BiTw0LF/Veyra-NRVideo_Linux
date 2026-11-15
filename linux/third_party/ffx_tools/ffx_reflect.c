// VeyAmux for Linux: prints the descriptor reflection of a SPIR-V module in the order and classification used by
// AMD FidelityFX_SC's GLSL path (tools/ffx_shader_compiler/src/glsl_compiler.cpp, MIT). One line per binding:
//   <CBV|TextureSRV|TextureUAV|BufferSRV|BufferUAV|Sampler|RTAccelerationStructure>\t<name>\t<binding>\t<count>\t<set>
#include "spirv_reflect.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: ffx_reflect <file.spv>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void* buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);
    SpvReflectShaderModule m;
    if (spvReflectCreateShaderModule((size_t)n, buf, &m) != SPV_REFLECT_RESULT_SUCCESS) { fprintf(stderr, "reflect failed\n"); return 1; }
    uint32_t count = 0;
    spvReflectEnumerateDescriptorSets(&m, &count, NULL);
    SpvReflectDescriptorSet** sets = (SpvReflectDescriptorSet**)malloc(sizeof(void*) * (count ? count : 1));
    spvReflectEnumerateDescriptorSets(&m, &count, sets);
    for (uint32_t s = 0; s < count; ++s) {
        SpvReflectDescriptorSet* ds = sets[s];
        for (uint32_t b = 0; b < ds->binding_count; ++b) {
            SpvReflectDescriptorBinding* bd = ds->bindings[b];
            const char* type = NULL;
            switch (bd->descriptor_type) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: type = "CBV"; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: type = "TextureSRV"; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: type = "Sampler"; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: type = "TextureUAV"; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                type = (bd->resource_type == SPV_REFLECT_RESOURCE_FLAG_SRV) ? "BufferSRV" : "BufferUAV"; break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: type = "RTAccelerationStructure"; break;
            default: break;   // other types are ignored by FidelityFX_SC as well
            }
            if (type) printf("%s\t%s\t%u\t%u\t%u\n", type, bd->name ? bd->name : "", bd->binding, bd->count, ds->set);
        }
    }
    spvReflectDestroyShaderModule(&m);
    return 0;
}
