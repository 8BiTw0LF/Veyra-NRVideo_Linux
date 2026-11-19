// VeyAmux for Linux: Abyssal Garden demo scene, main.js (see AbyssalScene.h). Translated from the WebGPU JavaScript
// API to webgpu.h one call at a time; the frame loop, culling, collision and camera follow the original.
#include "AbyssalScene.h"
#include "AbyssalWorld.h"
#include "abyssal_wgsl.h"   // generated: the WGSL modules (wgsl/*.wgsl)

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace abyssal {

namespace {

WGPUStringView sv(const char* s) { return {s, WGPU_STRLEN}; }

// preset viewpoints (position, look-at), keys 1-6; the world generator dresses the scene around them
const Pose POSES0[6] = {
    {10, 1.3, 7, -9, -31, -10}, {50, 1.3, 34, 58, -23, 8}, {-26, 2.2, -100, 0, -9, -108},
    {-55, 1.5, -18, -60, -30, 8}, {10, 1.2, -62, 6, -22, -90}, {3, 1.6, 62, 1, -28, 100},
};
// 60 s camera path (position + look-at, Catmull-Rom): the world generator keeps it clear
const double FLY[] = {
    10, -42.2, 7, -9, -31, -10, 7.5, -41.8, 5.5, -10, -31, -12, 9, -40.5, 18, 17, -37, 42, 22, -39.2, 40, 48, -36, 45,
    38, -39.2, 46, 57, -27, 18, 50, -40.4, 35, 58, -24, 8, 55, -39, 20, 60, -18, -10, 58, -34, -12, 40, -26, -60,
    50, -30, -44, 10, -26, -80, 30, -30, -70, 4, -28, -90, 12, -38.5, -62, 6, -27, -90, -4, -36.5, -70, 0, -14, -100,
    -16, -37, -84, 0, -22, -108, -29, -38, -90, -44, -34, -60, -41, -38, -70, -56, -34, -40, -53, -38, -50, -60, -34, -20,
    -58, -38, -28, -60, -32, 0, -57, -38, -10, -60, -30, 16, -60, -38, 10, -50, -36, 40, -52, -37, 36, -20, -35, 44,
    -22, -38, 43, 4, -38, 52, -6, -39.2, 44.5, 4, -37, 70, 3, -40.6, 53, 3, -35, 84, 3, -41.5, 62, 1, -30, 100,
    4, -39, 76, 3, -22, 100,
};
constexpr int FLYN = int(sizeof(FLY) / sizeof(double)) / 6, LUTN = 1024;
constexpr double FLYT = 60;
constexpr double TANH = 0.6, NEAR = 0.1, EXPO = 0.62, RAYS = 0.54;
const WGPUTextureFormat HDR = WGPUTextureFormat_RGBA16Float, DEPTH = WGPUTextureFormat_Depth32Float, OUTF = WGPUTextureFormat_RGBA8Unorm;

struct PipeOpt {
    bool strip = false;
    WGPUCullMode cull = WGPUCullMode_None;
    const WGPUBlendState* blend = nullptr;
    std::vector<std::pair<const char*, double>> constants;
};

} // namespace

struct Scene::Impl {
    // ---- device
    WGPUInstance inst = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice dev = nullptr;
    WGPUQueue Q = nullptr;
    std::string adapterName, lastError;
    int W = 0, H = 0;

    // ---- camera state (main.js keeps it in the F array)
    double CX = 0, CY = 0, CZ = 0, YAW = 0, PIT = 0, FX = 0, FY = 0, FZ = 0, RX = 0, RZ = 0, UX = 0, UY = 0, UZ = 0;
    double VX = 0, VY = 0, VZ = 0, TIME = 0, MX = 0, MY = 0, MZ = 0;
    double lkx = 0, lky = 0, l2x = 0, l2y = 0;
    double det = 1;
    std::vector<Pose> POSES;

    // ---- collision field (distance to rock near the surface, 4x4x4 bricks)
    static constexpr int CO = 52, CN = 102;
    std::vector<uint16_t> CI;
    std::vector<int8_t> CG;

    // ---- world data kept for drawing
    Terrain T;
    World Wd;
    std::vector<BuiltMesh> meshes;
    uint32_t nSolid = 0, nFish = 0, nManta = 0, nBub = 0, nSnow = 0, nGl = 0;

    // ---- GPU objects
    std::vector<WGPUBuffer> buffers;
    std::vector<WGPUTexture> textures;
    std::vector<WGPUTextureView> views;
    WGPUSampler sRep = nullptr, sClamp = nullptr, sCmp = nullptr;
    WGPUTexture tShadow = nullptr, tDummy = nullptr, tWater = nullptr, tDetail = nullptr, tVol = nullptr, tMap = nullptr;
    WGPUBuffer ubo = nullptr, uboS = nullptr, uboW = nullptr;
    WGPUBindGroupLayout bgl0 = nullptr, bglBlit = nullptr;
    WGPUPipelineLayout pl0 = nullptr, plBlit = nullptr;
    WGPUBindGroup bg0 = nullptr, bg0s = nullptr, bgWater = nullptr;
    WGPURenderPipeline pMip8 = nullptr, pMip16 = nullptr, pDown0 = nullptr, pDown = nullptr, pUp = nullptr;
    WGPURenderPipeline pTerrain = nullptr, pSolid = nullptr, pLeaf = nullptr, pLeaf5 = nullptr, pLeafS = nullptr, pLeafF = nullptr;
    WGPURenderPipeline pFish = nullptr, pManta = nullptr, pSurf = nullptr, pBack = nullptr, pBub = nullptr, pSnow = nullptr, pGl = nullptr;
    WGPURenderPipeline pSolidSh = nullptr, pTerrSh = nullptr, pLeafSh = nullptr;
    WGPURenderPipeline pWater = nullptr, pRB = nullptr, pRays = nullptr, pPost = nullptr;
    std::array<WGPUTextureView, 5> waterViews{};
    std::array<WGPUBindGroup, 5> waterBG{};
    WGPUBuffer vbMesh = nullptr, ibMesh = nullptr, vbTerrain = nullptr, ibTerrain = nullptr, ibSolids = nullptr;
    std::array<WGPUBuffer, NCHUNK> ibChunk{};
    std::array<WGPUBuffer, 6> ibProp{};
    WGPUBuffer ibBig = nullptr, ibBigF = nullptr, ibSmall = nullptr, ibFish = nullptr, ibManta = nullptr, ibBubbles = nullptr, ibSnowB = nullptr, ibGl = nullptr;
    // render targets
    WGPUTextureView vMS = nullptr, vDp = nullptr, vRes = nullptr, vRay = nullptr, vRay2 = nullptr, vOut = nullptr;
    std::array<WGPUTextureView, 6> vBl{};
    WGPUTexture tOut = nullptr;
    WGPUBindGroup bgBlur = nullptr, bgRays = nullptr, bgPost = nullptr;
    std::vector<WGPUBindGroup> bloomBG;
    std::vector<WGPUTextureView> bloomTarget;
    std::vector<bool> bloomClear;
    WGPUBuffer readback = nullptr;
    uint32_t rowBytes = 0;
    // how many tracked resources existed before the render targets: everything after these marks is size-dependent
    size_t markViews = 0, markTextures = 0, markBuffers = 0, markBloomBG = 0;

    float UB[60]{}, UBS[60]{}, WT[4]{};
    std::array<uint8_t, CELLS> rowOrd{};

    ~Impl() {
        for (auto* b : bloomBG) if (b) wgpuBindGroupRelease(b);
        for (auto* v : views) wgpuTextureViewRelease(v);
        for (auto* t : textures) { wgpuTextureDestroy(t); wgpuTextureRelease(t); }
        for (auto* b : buffers) { wgpuBufferDestroy(b); wgpuBufferRelease(b); }
        if (dev) wgpuDeviceRelease(dev);
        if (adapter) wgpuAdapterRelease(adapter);
        if (inst) wgpuInstanceRelease(inst);
    }

    // ------------------------------------------------------------ helpers (main.js: mod, buf, tex)
    WGPUShaderModule mod(const char* code, const char* label) {
        WGPUShaderSourceWGSL w = WGPU_SHADER_SOURCE_WGSL_INIT;
        w.code = sv(code);
        WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        d.nextInChain = &w.chain;
        d.label = sv(label);
        return wgpuDeviceCreateShaderModule(dev, &d);
    }
    WGPUBuffer buf(const void* data, size_t bytes, WGPUBufferUsage usage) {
        WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT;
        d.size = std::max<size_t>((bytes + 3) & ~size_t(3), 4);
        d.usage = usage | WGPUBufferUsage_CopyDst;
        WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &d);
        if (bytes) {
            if (bytes & 3) {   // writeBuffer wants a multiple of 4
                std::vector<uint8_t> pad(d.size, 0);
                std::memcpy(pad.data(), data, bytes);
                wgpuQueueWriteBuffer(Q, b, 0, pad.data(), pad.size());
            } else {
                wgpuQueueWriteBuffer(Q, b, 0, data, bytes);
            }
        }
        buffers.push_back(b);
        return b;
    }
    WGPUBuffer emptyBuf(size_t bytes, WGPUBufferUsage usage) {
        WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT;
        d.size = bytes;
        d.usage = usage;
        WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &d);
        buffers.push_back(b);
        return b;
    }
    WGPUTexture tex(uint32_t w, uint32_t h, WGPUTextureFormat format, WGPUTextureUsage usage, uint32_t mips = 1, uint32_t samples = 1, uint32_t depth3d = 0) {
        WGPUTextureDescriptor d = WGPU_TEXTURE_DESCRIPTOR_INIT;
        d.size = {w, h, depth3d ? depth3d : 1};
        d.format = format;
        d.usage = usage;
        d.mipLevelCount = mips;
        d.sampleCount = samples;
        d.dimension = depth3d ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
        WGPUTexture t = wgpuDeviceCreateTexture(dev, &d);
        textures.push_back(t);
        return t;
    }
    WGPUTextureView view(WGPUTexture t, int mip = -1) {
        WGPUTextureView v;
        if (mip < 0) v = wgpuTextureCreateView(t, nullptr);
        else {
            WGPUTextureViewDescriptor d = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            d.baseMipLevel = uint32_t(mip);
            d.mipLevelCount = 1;
            v = wgpuTextureCreateView(t, &d);
        }
        views.push_back(v);
        return v;
    }
    void wait() { wgpuDevicePoll(dev, true, nullptr); }

    // ------------------------------------------------------------ pipelines
    struct VB { std::vector<WGPUVertexAttribute> attr; uint64_t stride; WGPUVertexStepMode step; };
    static WGPUVertexAttribute A(uint32_t loc, uint64_t off, WGPUVertexFormat f) { WGPUVertexAttribute a = WGPU_VERTEX_ATTRIBUTE_INIT; a.shaderLocation = loc; a.offset = off; a.format = f; return a; }
    VB vbSolid() { return {{A(0, 0, WGPUVertexFormat_Float32x3), A(1, 12, WGPUVertexFormat_Snorm8x4)}, 16, WGPUVertexStepMode_Vertex}; }
    VB ibSolid() { return {{A(2, 0, WGPUVertexFormat_Float32x4), A(3, 16, WGPUVertexFormat_Float32x4), A(4, 32, WGPUVertexFormat_Float32x4)}, 48, WGPUVertexStepMode_Instance}; }
    VB ibLeaf() { return {{A(0, 0, WGPUVertexFormat_Float32x4), A(1, 16, WGPUVertexFormat_Float32x4), A(2, 32, WGPUVertexFormat_Unorm8x4)}, 36, WGPUVertexStepMode_Instance}; }
    VB ibAnim() { return {{A(2, 0, WGPUVertexFormat_Float32x4), A(3, 16, WGPUVertexFormat_Float32x4)}, 32, WGPUVertexStepMode_Instance}; }
    VB ibBub() { return {{A(0, 0, WGPUVertexFormat_Float32x4), A(1, 16, WGPUVertexFormat_Float32x4)}, 32, WGPUVertexStepMode_Instance}; }
    VB ibSnow() { return {{A(0, 0, WGPUVertexFormat_Float32x4)}, 16, WGPUVertexStepMode_Instance}; }

    WGPURenderPipeline makePipe(WGPUPipelineLayout layout, WGPUShaderModule m, const char* vsE, const char* fsE, const std::vector<VB>& vbs,
                                const PipeOpt& o, WGPUTextureFormat colorFormat, bool scene, bool shadow, uint32_t samples) {
        std::vector<WGPUVertexBufferLayout> L;
        for (const VB& v : vbs) {
            WGPUVertexBufferLayout l = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
            l.stepMode = v.step; l.arrayStride = v.stride; l.attributeCount = v.attr.size(); l.attributes = v.attr.data();
            L.push_back(l);
        }
        std::vector<WGPUConstantEntry> C;
        for (const auto& c : o.constants) { WGPUConstantEntry e = WGPU_CONSTANT_ENTRY_INIT; e.key = sv(c.first); e.value = c.second; C.push_back(e); }
        WGPURenderPipelineDescriptor d = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        d.layout = layout;
        d.vertex.module = m; d.vertex.entryPoint = sv(vsE);
        d.vertex.bufferCount = L.size(); d.vertex.buffers = L.data();
        d.vertex.constantCount = C.size(); d.vertex.constants = C.data();
        d.primitive.topology = o.strip ? WGPUPrimitiveTopology_TriangleStrip : WGPUPrimitiveTopology_TriangleList;
        d.primitive.cullMode = o.cull;
        WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
        ct.format = colorFormat;
        ct.blend = o.blend;
        WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
        if (!shadow) {
            fs.module = m; fs.entryPoint = sv(fsE);
            fs.targetCount = 1; fs.targets = &ct;
            fs.constantCount = C.size(); fs.constants = C.data();
            d.fragment = &fs;
        }
        WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
        if (scene || shadow) {
            ds.format = DEPTH;
            if (shadow) { ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less; ds.depthBias = 2; ds.depthBiasSlopeScale = 1.5f; }
            else { ds.depthWriteEnabled = o.blend ? WGPUOptionalBool_False : WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Greater; }
            d.depthStencil = &ds;
        }
        d.multisample.count = samples;
        return wgpuDeviceCreateRenderPipeline(dev, &d);
    }
    WGPURenderPipeline scenePipe(WGPUShaderModule m, const char* vsE, const char* fsE, const std::vector<VB>& vbs, const PipeOpt& o = {}) {
        return makePipe(pl0, m, vsE, fsE, vbs, o, HDR, true, false, AA);
    }
    WGPURenderPipeline shadowPipe(WGPUShaderModule m, const char* vsE, const std::vector<VB>& vbs, const PipeOpt& o = {}) {
        return makePipe(pl0, m, vsE, nullptr, vbs, o, HDR, false, true, 1);
    }
    WGPURenderPipeline blitPipe(WGPUShaderModule m, const char* entry, WGPUTextureFormat format, bool add) {
        static const WGPUBlendState ADD = {{WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One}, {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One}};
        PipeOpt o; o.blend = add ? &ADD : nullptr;
        return makePipe(plBlit, m, "vs", entry, {}, o, format, false, false, 1);
    }
    WGPURenderPipeline autoPipe(WGPUShaderModule m, WGPUTextureFormat format) { return makePipe(nullptr, m, "vs", "fs", {}, {}, format, false, false, 1); }

    WGPUBindGroup blitBG(WGPUTextureView v) {
        WGPUBindGroupEntry e[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        e[0].binding = 0; e[0].textureView = v;
        e[1].binding = 1; e[1].sampler = sClamp;
        WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        d.layout = bglBlit; d.entryCount = 2; d.entries = e;
        return wgpuDeviceCreateBindGroup(dev, &d);
    }
    struct Ent { uint32_t binding; WGPUBuffer b = nullptr; WGPUSampler s = nullptr; WGPUTextureView v = nullptr; };
    WGPUBindGroup group(WGPUBindGroupLayout layout, std::initializer_list<Ent> es) {
        std::vector<WGPUBindGroupEntry> e;
        for (const Ent& x : es) {
            WGPUBindGroupEntry g = WGPU_BIND_GROUP_ENTRY_INIT;
            g.binding = x.binding; g.buffer = x.b; g.size = x.b ? WGPU_WHOLE_SIZE : 0; g.sampler = x.s; g.textureView = x.v;
            e.push_back(g);
        }
        WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        d.layout = layout; d.entryCount = e.size(); d.entries = e.data();
        return wgpuDeviceCreateBindGroup(dev, &d);
    }
    // a render pass with one colour target (and optionally depth)
    WGPURenderPassEncoder pass(WGPUCommandEncoder e, WGPUTextureView colour, bool clear, WGPUTextureView resolve = nullptr, bool discard = false,
                               WGPUTextureView depth = nullptr, float depthClear = 0, double alpha = 0) {
        WGPURenderPassColorAttachment c = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        c.view = colour; c.resolveTarget = resolve;
        c.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
        c.storeOp = discard ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
        c.clearValue = {0, 0, 0, alpha};
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        WGPURenderPassDescriptor d = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        if (colour) { d.colorAttachmentCount = 1; d.colorAttachments = &c; }
        if (depth) {
            ds.view = depth; ds.depthClearValue = depthClear; ds.depthLoadOp = WGPULoadOp_Clear; ds.depthStoreOp = WGPUStoreOp_Store;
            d.depthStencilAttachment = &ds;
        }
        return wgpuCommandEncoderBeginRenderPass(e, &d);
    }

    // ------------------------------------------------------------ device
    bool initDevice(std::string& err) {
        inst = wgpuCreateInstance(nullptr);
        if (!inst) { err = "wgpu: no instance"; return false; }
        WGPURequestAdapterOptions o = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        o.powerPreference = WGPUPowerPreference_HighPerformance;
        o.backendType = WGPUBackendType_Vulkan;
        WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        ac.mode = WGPUCallbackMode_AllowProcessEvents;
        ac.userdata1 = this;
        ac.callback = [](WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void* u, void*) {
            auto* self = static_cast<Impl*>(u);
            if (s == WGPURequestAdapterStatus_Success) self->adapter = a;
            else self->lastError.assign(m.data ? m.data : "", m.data ? (m.length == WGPU_STRLEN ? std::strlen(m.data) : m.length) : 0);
        };
        wgpuInstanceRequestAdapter(inst, &o, ac);
        wgpuInstanceProcessEvents(inst);
        if (!adapter) { err = "No Vulkan graphics card for the demo" + (lastError.empty() ? std::string() : ": " + lastError); return false; }
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success && info.device.data)
            adapterName.assign(info.device.data, info.device.length == WGPU_STRLEN ? std::strlen(info.device.data) : info.device.length);
        wgpuAdapterInfoFreeMembers(info);
        WGPULimits sup = WGPU_LIMITS_INIT;
        wgpuAdapterGetLimits(adapter, &sup);
        WGPULimits lim = WGPU_LIMITS_INIT;
        // the density grid (84 MB) is one storage buffer; the leaf buffer is ~80 MB
        lim.maxStorageBufferBindingSize = std::min<uint64_t>(sup.maxStorageBufferBindingSize, 512ull << 20);
        lim.maxBufferSize = std::min<uint64_t>(sup.maxBufferSize, 1024ull << 20);
        WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
        dd.requiredLimits = &lim;
        dd.uncapturedErrorCallbackInfo.userdata1 = this;
        dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView m, void* u, void*) {
            auto* self = static_cast<Impl*>(u);
            self->lastError.assign(m.data ? m.data : "", m.data ? (m.length == WGPU_STRLEN ? std::strlen(m.data) : m.length) : 0);
            std::fprintf(stderr, "abyssal: GPU error: %s\n", self->lastError.c_str());
        };
        WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        dc.mode = WGPUCallbackMode_AllowProcessEvents;
        dc.userdata1 = this;
        dc.callback = [](WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView, void* u, void*) { if (s == WGPURequestDeviceStatus_Success) static_cast<Impl*>(u)->dev = d; };
        wgpuAdapterRequestDevice(adapter, &dd, dc);
        wgpuInstanceProcessEvents(inst);
        if (!dev) { err = "The graphics card refused the demo's device"; return false; }
        Q = wgpuDeviceGetQueue(dev);
        return true;
    }

    // ------------------------------------------------------------ camera path (Catmull-Rom over position + look-at)
    double P6[6]{};
    void aim() {
        const double dx = P6[3] - P6[0], dy = P6[4] - P6[1], dz = P6[5] - P6[2];
        CX = P6[0]; CY = P6[1]; CZ = P6[2];
        YAW = std::atan2(dx, -dz); PIT = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    }
    void lookAt(const Pose& p) { for (int i = 0; i < 6; i++) P6[i] = p[i]; aim(); }
    void flyRaw(double s) {
        int i = int(std::floor(s)); if (i > FLYN - 2) i = FLYN - 2;
        const double u = s - i;
        const int i0 = (i > 0 ? i - 1 : 0) * 6, i1 = i * 6, i2 = (i + 1) * 6, i3 = (i + 2 < FLYN ? i + 2 : FLYN - 1) * 6;
        for (int o = 0; o < 6; o++) {
            const double a = FLY[i0 + o], b = FLY[i1 + o], c = FLY[i2 + o], d = FLY[i3 + o];
            P6[o] = 0.5 * (2 * b + (c - a) * u + (2 * a - 5 * b + 4 * c - d) * u * u + (3 * b - a - 3 * c + d) * u * u * u);
        }
        aim();
    }
    std::vector<V3> flyPoints() {
        // arc-length table: flyLUT[k] = spline parameter at fraction k/LUTN of the total path length
        const int M = 4096;
        std::vector<float> len(M + 1, 0.0f), flyLUT(LUTN + 1, 0.0f);
        double px = 0, py = 0, pz = 0;
        for (int k = 0; k <= M; k++) {
            flyRaw(double(k) / M * (FLYN - 1));
            if (k) len[k] = float(len[k - 1] + hyp(CX - px, CY - py, CZ - pz));
            px = CX; py = CY; pz = CZ;
        }
        for (int k = 0, j = 0; k <= LUTN; k++) {
            const double dd = double(k) / LUTN * len[M];
            while (j < M - 1 && len[j + 1] < dd) j++;
            flyLUT[k] = float((j + (dd - len[j]) / std::max(double(len[j + 1]) - len[j], 1e-6)) / M * (FLYN - 1));
        }
        std::vector<V3> pts;
        for (double t = 0; t <= FLYT; t += 0.1) {
            double s = t / FLYT; s = s < 0 ? 0 : s > 1 ? 1 : s; s = s * s * (3 - 2 * s) * 0.25 + s * 0.75;
            s *= LUTN;
            int k = int(std::floor(s)); if (k > LUTN - 1) k = LUTN - 1;
            flyRaw(flyLUT[k] + (double(flyLUT[k + 1]) - flyLUT[k]) * (s - k));
            pts.push_back({CX, CY, CZ});
        }
        for (const Pose& p : POSES) pts.push_back({p[0], -40, p[2]});
        return pts;
    }

    // ------------------------------------------------------------ the density grid on the GPU
    bool density(std::vector<float>& D, std::string& err) {
        const std::vector<float> prims = primBuffer();
        const PrimCounts pc = primCounts();
        WGPUShaderModule m = mod(W_DENS, "dens");
        WGPUComputePipelineDescriptor cd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        cd.compute.module = m; cd.compute.entryPoint = sv("main");
        WGPUComputePipeline p = wgpuDeviceCreateComputePipeline(dev, &cd);
        const size_t size = size_t(NX) * NY * NZ * 4;
        uint32_t u[12]{};
        u[0] = NX; u[1] = NY; u[2] = NZ;
        const float of[4] = {float(OX), float(OY), float(OZ), float(VS)};
        std::memcpy(&u[4], of, 16);
        u[8] = pc.mesas; u[9] = pc.arches; u[10] = pc.holes; u[11] = pc.clears;
        WGPUBuffer bu = buf(u, sizeof u, WGPUBufferUsage_Uniform), bp = buf(prims.data(), prims.size() * 4, WGPUBufferUsage_Storage);
        WGPUBuffer bd = emptyBuf(size, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc), br = emptyBuf(size, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        WGPUBindGroupLayout l = wgpuComputePipelineGetBindGroupLayout(p, 0);
        WGPUBindGroup bg = group(l, {{0, bu}, {1, bd}, {2, bp}});
        WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(dev, nullptr);
        WGPUComputePassEncoder c = wgpuCommandEncoderBeginComputePass(e, nullptr);
        wgpuComputePassEncoderSetPipeline(c, p);
        wgpuComputePassEncoderSetBindGroup(c, 0, bg, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(c, NX / 4, NY / 4, NZ / 4);
        wgpuComputePassEncoderEnd(c);
        wgpuComputePassEncoderRelease(c);
        wgpuCommandEncoderCopyBufferToBuffer(e, bd, 0, br, 0, size);
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, nullptr);
        wgpuQueueSubmit(Q, 1, &cb);
        wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(e);
        if (!mapRead(br, size)) { err = "The density grid could not be read back"; return false; }
        D.resize(size / 4);
        std::memcpy(D.data(), wgpuBufferGetConstMappedRange(br, 0, size), size);
        wgpuBufferUnmap(br);
        wgpuBindGroupRelease(bg); wgpuBindGroupLayoutRelease(l); wgpuComputePipelineRelease(p); wgpuShaderModuleRelease(m);
        return true;
    }
    bool mapRead(WGPUBuffer b, size_t size) {
        bool done = false, ok = false;
        struct S { bool* done; bool* ok; } s{&done, &ok};
        WGPUBufferMapCallbackInfo ci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        ci.mode = WGPUCallbackMode_AllowProcessEvents;
        ci.userdata1 = &s;
        ci.callback = [](WGPUMapAsyncStatus st, WGPUStringView, void* u, void*) { auto* s = static_cast<S*>(u); *s->done = true; *s->ok = st == WGPUMapAsyncStatus_Success; };
        wgpuBufferMapAsync(b, WGPUMapMode_Read, 0, size, ci);
        for (int i = 0; i < 100000 && !done; i++) { wait(); wgpuInstanceProcessEvents(inst); }
        return ok;
    }

    // ------------------------------------------------------------ rock connectivity + collision field (main.js)
    static void connect(std::vector<float>& D) {
        // rock connected to the seabed: each column's unbroken run up from the bottom is, and the rock above a gap is
        // when it touches rock that is; what never connects is a speck of noise floating in the water: it becomes water
        const size_t S = size_t(NX) * NY, N = D.size();
        std::vector<uint8_t> R(N, 0), b(NX);
        std::vector<size_t> L;
        for (size_t i = 0; i < N;) {
            std::fill(b.begin(), b.end(), 1);
            for (int y = 0; y < NY; y++) for (int x = 0; x < NX; x++, i++) {
                if (D[i] <= 0) b[x] = 0;
                else if (b[x]) R[i] = 1;
                else L.push_back(i);
            }
        }
        const int64_t O[6] = {1, -1, NX, -NX, int64_t(S), -int64_t(S)};
        for (bool k = true; k; std::reverse(L.begin(), L.end())) {
            k = false;
            for (size_t i : L) {
                if (R[i]) continue;
                for (int64_t o : O) {
                    const int64_t j = int64_t(i) + o;
                    if (j >= 0 && size_t(j) < N && R[size_t(j)]) { R[i] = 1; k = true; break; }
                }
            }
        }
        for (size_t i : L) if (!R[i]) D[i] = -D[i];
    }
    void collisionField(const std::vector<float>& D) {
        CI.assign(size_t(CN) * CN * (NY / 4), 0);
        CG.assign(size_t(64) * 65534, 0);
        int n = 0;
        const size_t S = size_t(NX) * NY;
        for (int bz = 0; bz < CN; bz++) for (int by = 1; by < NY / 4 - 1; by++) for (int bx = 0; bx < CN; bx++) {
            if (hyp(bx - CN / 2.0 + 0.5, bz - CN / 2.0 + 0.5) > 52) continue;   // more than 156 m out: never reached
            int lo = 127, hi = -127;
            for (int z = 0; z < 4; z++) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                const size_t i = size_t(CO + bx * 4 + x) + size_t(NX) * (size_t(by * 4 + y) + size_t(NY) * size_t(CO + bz * 4 + z));
                const double gz = double(D[i + S]) - D[i - S], gx = double(D[i + 1]) - D[i - 1], g = double(D[i + NX]) - D[i - NX];
                const double l = std::sqrt(gx * gx + g * g + gz * gz) / 1.5;
                const int q = int(jsround(clampd(D[i] / std::max(l, 0.05), -1, 1) * 127));
                CG[size_t(n) * 64 + x + 4 * y + 16 * z] = int8_t(q);
                lo = std::min(lo, q); hi = std::max(hi, q);
            }
            uint16_t& c = CI[size_t(bx) + CN * (size_t(by) + (NY / 4) * size_t(bz))];
            if (lo == hi && lo * lo == 16129) c = lo > 0 ? 1 : 0;
            else { n++; c = uint16_t(n + 1); }
        }
        CG.resize(size_t(n) * 64);
    }
    int cv(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 || (x >> 2) >= CN || (y >> 2) >= NY / 4 || (z >> 2) >= CN) return -127;
        const int b = CI[size_t(x >> 2) + CN * (size_t(y >> 2) + (NY / 4) * size_t(z >> 2))];
        return b > 1 ? CG[size_t(b) * 64 - 128 + (x & 3) + 4 * (y & 3) + 16 * (z & 3)] : b * 254 - 127;
    }
    // distance to the rock (metres, < 0 in water) at the camera + offset
    double sd(double ox, double oy, double oz) const {
        const double x = (CX + ox - OX) / VS - CO, y = (CY + oy - OY) / VS, z = (CZ + oz - OZ) / VS - CO;
        const int ix = int(x), iy = int(y), iz = int(z);
        const double u = x - ix, v = y - iy;
        const double a = cv(ix, iy, iz), b = cv(ix, iy + 1, iz), c = cv(ix, iy, iz + 1), d = cv(ix, iy + 1, iz + 1);
        const double p = a + (cv(ix + 1, iy, iz) - a) * u, q = b + (cv(ix + 1, iy + 1, iz) - b) * u, r = c + (cv(ix + 1, iy, iz + 1) - c) * u, s = d + (cv(ix + 1, iy + 1, iz + 1) - d) * u;
        const double e = p + (q - p) * v;
        return (e + (r + (s - r) * v - e) * (z - iz)) / 127;
    }
    // moves the camera by M in steps shorter than its radius, pushing it back out of the rock (it slides along)
    void collide() {
        const double CR = 0.5;
        const int n = int(std::floor(std::sqrt(MX * MX + MY * MY + MZ * MZ) / 0.4)) + 1;
        for (int i = 0; i < n; i++) {
            CX += MX / n; CY += MY / n; CZ += MZ / n;
            for (int k = 0; k < 2; k++) {
                const double s = sd(0, 0, 0) + CR;
                if (s <= 0) break;
                double gx = sd(.375, 0, 0) - sd(-.375, 0, 0), gy = sd(0, .375, 0) - sd(0, -.375, 0), gz = sd(0, 0, .375) - sd(0, 0, -.375);
                const double l = std::sqrt(gx * gx + gy * gy + gz * gz);
                if (l < 0.075) break;   // flat: deep in rock
                gx /= l; gy /= l; gz /= l;
                const double vn = VX * gx + VY * gy + VZ * gz;
                CX -= gx * s; CY -= gy * s; CZ -= gz * s;
                if (vn > 0) { VX -= gx * vn; VY -= gy * vn; VZ -= gz * vn; }
            }
        }
    }

    // ------------------------------------------------------------ build everything (main.js top level)
    bool build(const std::function<void(const char*, double)>& progress, std::string& err) {
        progress("Compiling shaders", 0.05);
        if (!initDevice(err)) return false;
        WGPUSamplerDescriptor sd0 = WGPU_SAMPLER_DESCRIPTOR_INIT;
        sd0.addressModeU = sd0.addressModeV = WGPUAddressMode_Repeat;
        sd0.magFilter = sd0.minFilter = WGPUFilterMode_Linear; sd0.mipmapFilter = WGPUMipmapFilterMode_Linear; sd0.maxAnisotropy = 8;
        sRep = wgpuDeviceCreateSampler(dev, &sd0);
        WGPUSamplerDescriptor sd1 = WGPU_SAMPLER_DESCRIPTOR_INIT;
        sd1.magFilter = sd1.minFilter = WGPUFilterMode_Linear;
        sClamp = wgpuDeviceCreateSampler(dev, &sd1);
        WGPUSamplerDescriptor sd2 = sd1;
        sd2.compare = WGPUCompareFunction_LessEqual;
        sCmp = wgpuDeviceCreateSampler(dev, &sd2);
        const auto RA = WGPUTextureUsage_RenderAttachment, TB = WGPUTextureUsage_TextureBinding, CD = WGPUTextureUsage_CopyDst;
        tShadow = tex(SHADOW, SHADOW, DEPTH, RA | TB);
        tDummy = tex(1, 1, DEPTH, RA | TB);
        tWater = tex(WATER, WATER, HDR, RA | TB, 5);
        tDetail = tex(1024, 1024, WGPUTextureFormat_RGBA8Unorm, RA | TB, 11);
        tVol = tex(NX / 2, NY / 2, WGPUTextureFormat_RG8Unorm, TB | CD, 1, 1, NZ / 2);
        tMap = tex(512, 512, WGPUTextureFormat_RGBA8Unorm, TB | CD);
        ubo = emptyBuf(240, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        uboS = emptyBuf(240, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        uboW = emptyBuf(16, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);

        const WGPUShaderStage VF = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        {
            std::array<WGPUBindGroupLayoutEntry, 9> e;
            for (uint32_t i = 0; i < 9; i++) { e[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT; e[i].binding = i; e[i].visibility = VF; }
            e[0].buffer.type = WGPUBufferBindingType_Uniform;
            e[1].sampler.type = WGPUSamplerBindingType_Filtering;
            e[2].sampler.type = WGPUSamplerBindingType_Filtering;
            e[3].sampler.type = WGPUSamplerBindingType_Comparison;
            e[4].texture.sampleType = WGPUTextureSampleType_Depth; e[4].texture.viewDimension = WGPUTextureViewDimension_2D;
            for (int i : {5, 6, 8}) { e[i].texture.sampleType = WGPUTextureSampleType_Float; e[i].texture.viewDimension = WGPUTextureViewDimension_2D; }
            e[7].texture.sampleType = WGPUTextureSampleType_Float; e[7].texture.viewDimension = WGPUTextureViewDimension_3D;
            WGPUBindGroupLayoutDescriptor d = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            d.entryCount = e.size(); d.entries = e.data();
            bgl0 = wgpuDeviceCreateBindGroupLayout(dev, &d);
        }
        const auto mkBG0 = [&](WGPUBuffer u, WGPUTexture sh) {
            return group(bgl0, {{0, u}, {1, nullptr, sRep}, {2, nullptr, sClamp}, {3, nullptr, sCmp}, {4, nullptr, nullptr, view(sh)}, {5, nullptr, nullptr, view(tWater)},
                                {6, nullptr, nullptr, view(tDetail)}, {7, nullptr, nullptr, view(tVol)}, {8, nullptr, nullptr, view(tMap)}});
        };
        bg0 = mkBG0(ubo, tShadow); bg0s = mkBG0(uboS, tDummy);
        {
            WGPUPipelineLayoutDescriptor d = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            d.bindGroupLayoutCount = 1; d.bindGroupLayouts = &bgl0;
            pl0 = wgpuDeviceCreatePipelineLayout(dev, &d);
            WGPUBindGroupLayoutEntry e[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
            e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment; e[0].texture.sampleType = WGPUTextureSampleType_Float; e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
            e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
            WGPUBindGroupLayoutDescriptor bd = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bd.entryCount = 2; bd.entries = e;
            bglBlit = wgpuDeviceCreateBindGroupLayout(dev, &bd);
            d.bindGroupLayouts = &bglBlit;
            plBlit = wgpuDeviceCreatePipelineLayout(dev, &d);
        }
        WGPUShaderModule mBlit = mod(W_BLIT, "blit");
        pMip8 = blitPipe(mBlit, "fsMip", WGPUTextureFormat_RGBA8Unorm, false);
        pMip16 = blitPipe(mBlit, "fsMip", HDR, false);
        pDown0 = blitPipe(mBlit, "fsDown0", HDR, false);
        pDown = blitPipe(mBlit, "fsDown", HDR, false);
        pUp = blitPipe(mBlit, "fsUp", HDR, true);

        WGPUShaderModule mSolid = mod(W_SOLID, "solid"), mLeaf = mod(W_LEAF, "leaf"), mAnim = mod(W_ANIM, "anim"), mSurf = mod(W_SURF, "surf"), mSpr = mod(W_SPR, "spr");
        static const WGPUBlendState PRE = {{WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha}, {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha}};
        static const WGPUBlendState ADD = {{WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One}, {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One}};
        const auto opt = [](bool strip, WGPUCullMode cull, const WGPUBlendState* blend, std::vector<std::pair<const char*, double>> c) { PipeOpt o; o.strip = strip; o.cull = cull; o.blend = blend; o.constants = std::move(c); return o; };
        pTerrain = scenePipe(mSolid, "vs", "fs", {vbSolid(), ibSolid()}, opt(false, WGPUCullMode_Back, nullptr, {}));
        pSolid = scenePipe(mSolid, "vs", "fs", {vbSolid(), ibSolid()});
        pLeaf = scenePipe(mLeaf, "vs", "fs", {ibLeaf()}, opt(true, WGPUCullMode_None, nullptr, {{"ROWS", 9}}));
        pLeaf5 = scenePipe(mLeaf, "vs", "fs", {ibLeaf()}, opt(true, WGPUCullMode_None, nullptr, {{"ROWS", 5}}));
        pLeafS = scenePipe(mLeaf, "vs", "fs", {ibLeaf()}, opt(true, WGPUCullMode_None, nullptr, {{"ROWS", 3}, {"FAR", 70}}));
        pLeafF = scenePipe(mLeaf, "vs", "fs", {ibLeaf()}, opt(true, WGPUCullMode_None, nullptr, {{"ROWS", 3}}));
        pFish = scenePipe(mAnim, "vsFish", "fsFish", {vbSolid(), ibAnim()});
        pManta = scenePipe(mAnim, "vsManta", "fsManta", {vbSolid(), ibAnim()});
        pSurf = scenePipe(mSurf, "vs", "fs", {}, opt(true, WGPUCullMode_None, nullptr, {}));
        pBack = scenePipe(mSurf, "vsB", "fsB", {}, opt(true, WGPUCullMode_None, nullptr, {}));
        pBub = scenePipe(mSpr, "vsBub", "fsBub", {ibBub()}, opt(true, WGPUCullMode_None, &PRE, {}));
        pSnow = scenePipe(mSpr, "vsSnow", "fsSnow", {ibSnow()}, opt(true, WGPUCullMode_None, &ADD, {}));
        pGl = scenePipe(mLeaf, "vsGl", "fsGl", {ibBub()}, opt(true, WGPUCullMode_None, &ADD, {{"ROWS", 7}}));
        pSolidSh = shadowPipe(mSolid, "vs", {vbSolid(), ibSolid()});
        pTerrSh = shadowPipe(mSolid, "vs", {vbSolid(), ibSolid()}, opt(false, WGPUCullMode_Front, nullptr, {}));
        pLeafSh = shadowPipe(mLeaf, "vs", {ibLeaf()}, opt(true, WGPUCullMode_None, nullptr, {{"ROWS", 3}}));

        WGPUShaderModule mWater = mod(W_WATER, "water");
        pWater = autoPipe(mWater, HDR);
        {
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(pWater, 0);
            bgWater = group(l, {{0, uboW}});
            wgpuBindGroupLayoutRelease(l);
        }
        for (int m = 0; m < 5; m++) { waterViews[m] = view(tWater, m); waterBG[m] = blitBG(waterViews[m]); }
        WGPUShaderModule mRays = mod(W_RAYS, "rays"), mPost = mod(W_POST, "post"), mRB = mod(W_RBLUR, "rblur");
        pRB = autoPipe(mRB, HDR);
        pRays = autoPipe(mRays, HDR);
        pPost = autoPipe(mPost, OUTF);
        if (!lastError.empty()) { err = "Shader error: " + lastError; return false; }

        // sun + shadow matrix
        {
            double v[3] = {0.27, 1, -0.24};
            const double l0 = hyp(v[0], v[1], v[2]);
            const double L[3] = {v[0] / l0, v[1] / l0, v[2] / l0};
            double lr[3] = {L[1], -L[0], 0};
            const double ll = hyp(lr[0], lr[1], lr[2]); lr[0] /= ll; lr[1] /= ll;
            const double lu[3] = {L[1] * lr[2] - L[2] * lr[1], L[2] * lr[0] - L[0] * lr[2], L[0] * lr[1] - L[1] * lr[0]};
            const double R = 225, d0 = -175, d1 = 125;
            float* m = UBS;
            m[0] = float(lr[0] / R); m[4] = float(lr[1] / R); m[8] = float(lr[2] / R); m[12] = 0;
            m[1] = float(lu[0] / R); m[5] = float(lu[1] / R); m[9] = float(lu[2] / R); m[13] = 0;
            m[2] = float(-L[0] / (d1 - d0)); m[6] = float(-L[1] / (d1 - d0)); m[10] = float(-L[2] / (d1 - d0)); m[14] = float(d1 / (d1 - d0));
            m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
            for (int i = 0; i < 16; i++) { UBS[16 + i] = m[i]; UB[16 + i] = m[i]; }
            for (int i = 0; i < 3; i++) UB[48 + i] = UBS[48 + i] = float(L[i]);
            UBS[32] = 0; UBS[33] = -20; UBS[34] = 0; UBS[35] = 0; UBS[58] = 1;
            wgpuQueueWriteBuffer(Q, uboS, 0, UBS, sizeof UBS);
        }

        // world generation
        progress("Sculpting the seabed", 0.25);
        std::vector<float> D;
        if (!density(D, err)) return false;
        // ABYSSAL_DUMP=<dir>: write the generator's input and output, to compare with the JavaScript original
        const char* dump = std::getenv("ABYSSAL_DUMP");
        const auto put = [&](const char* name, const void* p, size_t n) {
            if (!dump) return;
            FILE* f = std::fopen((std::string(dump) + "/" + name).c_str(), "wb");
            if (f) { std::fwrite(p, 1, n, f); std::fclose(f); }
        };
        put("D.bin", D.data(), D.size() * 4);
        connect(D);
        collisionField(D);
        progress("Carving canyons", 0.32);
        T = buildTerrain(D);
        POSES.assign(std::begin(POSES0), std::end(POSES0));
        const std::vector<V3> flyPts = flyPoints();
        progress("Growing kelp", 0.73);
        Wd = makeWorld(D, T, flyPts, POSES);
        D.clear(); D.shrink_to_fit();
        if (dump) {
            put("terrain_vb.bin", T.vb.data(), T.vb.size()); put("terrain_ib.bin", T.ib.data(), T.ib.size() * 4);
            put("big.bin", Wd.big.buf.data(), Wd.big.buf.size()); put("bigF.bin", Wd.bigF.buf.data(), Wd.bigF.buf.size());
            put("small.bin", Wd.small.buf.data(), Wd.small.buf.size());
            for (int i = 0; i < NCHUNK; i++) put(("chunk" + std::to_string(i) + ".bin").c_str(), Wd.chunks[i].data.data(), Wd.chunks[i].data.size() * 4);
            for (int i = 0; i < 6; i++) put(("prop" + std::to_string(i) + ".bin").c_str(), Wd.props[i].data.data(), Wd.props[i].data.size() * 4);
            put("solids.bin", Wd.solidData.data(), Wd.solidData.size() * 4); put("glints.bin", Wd.glints.data(), Wd.glints.size() * 4);
            put("bubbles.bin", Wd.bubbles.data(), Wd.bubbles.size() * 4); put("fish.bin", Wd.fish.data(), Wd.fish.size() * 4);
            put("volD.bin", Wd.volD.data(), Wd.volD.size()); put("gm.bin", Wd.gm.data(), Wd.gm.size());
            put("CG.bin", CG.data(), CG.size()); put("CI.bin", CI.data(), CI.size() * 2);
            std::vector<double> poses; for (const Pose& p : POSES) poses.insert(poses.end(), p.begin(), p.end());
            put("poses.bin", poses.data(), poses.size() * 8);
        }

        progress("Building set pieces", 0.94);
        meshes = makeMeshes();
        for (size_t i = 0; dump && i < meshes.size(); i++) {
            put(("mesh" + std::to_string(i) + "_vb.bin").c_str(), meshes[i].vb.data(), meshes[i].vb.size());
            put(("mesh" + std::to_string(i) + "_ib.bin").c_str(), meshes[i].ib.data(), meshes[i].ib.size() * 4);
        }
        uint32_t nV = 0, nI = 0;
        for (auto& m : meshes) { m.v0 = nV; m.i0 = nI; nV += m.nv; nI += uint32_t(m.ib.size()); }
        std::vector<uint8_t> allV(size_t(nV) * 16);
        std::vector<uint32_t> allI(nI);
        for (const auto& m : meshes) { std::memcpy(allV.data() + size_t(m.v0) * 16, m.vb.data(), m.vb.size()); std::copy(m.ib.begin(), m.ib.end(), allI.begin() + m.i0); }
        const auto VBU = WGPUBufferUsage_Vertex, IBU = WGPUBufferUsage_Index;
        vbMesh = buf(allV.data(), allV.size(), VBU); ibMesh = buf(allI.data(), allI.size() * 4, IBU);
        vbTerrain = buf(T.vb.data(), T.vb.size(), VBU); ibTerrain = buf(T.ib.data(), T.ib.size() * 4, IBU);
        nSolid = uint32_t(Wd.solidData.size() / 12);
        std::vector<float> solidData(Wd.solidData);
        for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f}) solidData.push_back(v);
        ibSolids = buf(solidData.data(), solidData.size() * 4, VBU);
        for (int i = 0; i < NCHUNK; i++) ibChunk[i] = buf(Wd.chunks[i].data.data(), Wd.chunks[i].data.size() * 4, VBU);
        for (int i = 0; i < 6; i++) ibProp[i] = buf(Wd.props[i].data.data(), Wd.props[i].data.size() * 4, VBU);
        ibBig = buf(Wd.big.buf.data(), Wd.big.buf.size(), VBU); ibBigF = buf(Wd.bigF.buf.data(), Wd.bigF.buf.size(), VBU); ibSmall = buf(Wd.small.buf.data(), Wd.small.buf.size(), VBU);
        ibFish = buf(Wd.fish.data(), Wd.fish.size() * 4, VBU); ibManta = buf(Wd.mantas.data(), Wd.mantas.size() * 4, VBU);
        ibBubbles = buf(Wd.bubbles.data(), Wd.bubbles.size() * 4, VBU); ibSnowB = buf(Wd.snow.data(), Wd.snow.size() * 4, VBU);
        ibGl = buf(Wd.glints.data(), Wd.glints.size() * 4, VBU);
        nGl = uint32_t(Wd.glints.size() / 8); nFish = uint32_t(Wd.fish.size() / 8); nManta = uint32_t(Wd.mantas.size() / 8);
        nBub = uint32_t(Wd.bubbles.size() / 8); nSnow = uint32_t(Wd.snow.size() / 4);
        {
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = tVol;
            WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = NX; lay.rowsPerImage = NY / 2;
            const WGPUExtent3D ext = {NX / 2, NY / 2, NZ / 2};
            wgpuQueueWriteTexture(Q, &dst, Wd.volD.data(), Wd.volD.size(), &lay, &ext);
            dst.texture = tMap;
            WGPUTexelCopyBufferLayout lay2 = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay2.bytesPerRow = 2048; lay2.rowsPerImage = 512;
            const WGPUExtent3D ext2 = {512, 512, 1};
            wgpuQueueWriteTexture(Q, &dst, Wd.gm.data(), Wd.gm.size(), &lay2, &ext2);
        }

        // one-time bakes: detail texture, shadow map
        {
            WGPUShaderModule m = mod(W_DETAIL, "detail");
            WGPURenderPipeline p = autoPipe(m, WGPUTextureFormat_RGBA8Unorm);
            WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(dev, nullptr);
            WGPURenderPassEncoder r = pass(e, view(tDetail, 0), true);
            wgpuRenderPassEncoderSetPipeline(r, p); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); wgpuRenderPassEncoderEnd(r); wgpuRenderPassEncoderRelease(r);
            for (int i = 1; i < 11; i++) {
                r = pass(e, view(tDetail, i), true);
                WGPUBindGroup bg = blitBG(view(tDetail, i - 1));
                wgpuRenderPassEncoderSetPipeline(r, pMip8); wgpuRenderPassEncoderSetBindGroup(r, 0, bg, 0, nullptr); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(r); wgpuRenderPassEncoderRelease(r);
                bloomBG.push_back(bg);   // released with the scene
            }
            r = pass(e, nullptr, false, nullptr, false, view(tShadow), 1);
            wgpuRenderPassEncoderSetBindGroup(r, 0, bg0s, 0, nullptr);
            wgpuRenderPassEncoderSetPipeline(r, pTerrSh);
            wgpuRenderPassEncoderSetVertexBuffer(r, 0, vbTerrain, 0, WGPU_WHOLE_SIZE); wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibSolids, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(r, ibTerrain, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(r, uint32_t(T.ib.size()), 1, 0, 0, nSolid);
            wgpuRenderPassEncoderSetPipeline(r, pSolidSh);
            wgpuRenderPassEncoderSetVertexBuffer(r, 0, vbMesh, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(r, ibMesh, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            for (int i = 0; i < 12; i++) { const auto& g = Wd.solidRanges[i]; if (g[1]) wgpuRenderPassEncoderDrawIndexed(r, uint32_t(meshes[i].ib.size()), g[1], meshes[i].i0, int32_t(meshes[i].v0), g[0]); }
            // small rocks, sponges and corals only on High (this port always is)
            for (int i = 0; i < NCHUNK; i++) if (Wd.chunks[i].n) {
                wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibChunk[i], 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDrawIndexed(r, uint32_t(meshes[14 + i].ib.size()), Wd.chunks[i].n, meshes[14 + i].i0, int32_t(meshes[14 + i].v0), 0);
            }
            for (int i = 0; i < 6; i++) if (Wd.props[i].n) {
                wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibProp[i], 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDrawIndexed(r, uint32_t(meshes[4 + i].ib.size()), Wd.props[i].n, meshes[4 + i].i0, int32_t(meshes[4 + i].v0), 0);
            }
            wgpuRenderPassEncoderSetPipeline(r, pLeafSh); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibBig, 0, WGPU_WHOLE_SIZE);
            if (Wd.big.n) wgpuRenderPassEncoderDraw(r, 6, Wd.big.n, 0, 0);
            wgpuRenderPassEncoderEnd(r); wgpuRenderPassEncoderRelease(r);
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, nullptr);
            wgpuQueueSubmit(Q, 1, &cb);
            wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(e);
            wait();
            wgpuRenderPipelineRelease(p); wgpuShaderModuleRelease(m);
        }
        // the GPU has its own copies now
        for (auto* s : {&Wd.big, &Wd.bigF, &Wd.small}) { s->buf.clear(); s->buf.shrink_to_fit(); }
        for (auto& c : Wd.chunks) { c.data.clear(); c.data.shrink_to_fit(); }
        for (auto& c : Wd.props) { c.data.clear(); c.data.shrink_to_fit(); }
        Wd.glints = {}; Wd.volD = {}; Wd.gm = {}; Wd.fish = {}; Wd.mantas = {}; Wd.bubbles = {}; Wd.snow = {}; Wd.solidData = {};
        T.vb = {}; T.wp = {}; T.wn = {};
        T.ib.shrink_to_fit();
        for (auto& m : meshes) { m.vb = {}; }

        targets();
        lookAt(POSES[0]);
        progress("Letting the light in", 1.0);
        return lastError.empty() || (err = "GPU error: " + lastError, false);
    }

    // ------------------------------------------------------------ render targets (size-dependent)
    void targets() {
        markViews = views.size(); markTextures = textures.size(); markBuffers = buffers.size(); markBloomBG = bloomBG.size();
        const auto RA = WGPUTextureUsage_RenderAttachment, TB = WGPUTextureUsage_TextureBinding;
        WGPUTexture tMS = tex(W, H, HDR, RA, 1, AA), tDp = tex(W, H, DEPTH, RA | TB, 1, AA);
        WGPUTexture tRes = tex(W, H, HDR, RA | TB), tRay = tex(W / 2, H / 2, HDR, RA | TB), tRay2 = tex(W / 2, H / 2, HDR, RA | TB);
        tOut = tex(W, H, OUTF, RA | WGPUTextureUsage_CopySrc);
        vMS = view(tMS); vDp = view(tDp); vRes = view(tRes); vRay = view(tRay); vRay2 = view(tRay2); vOut = view(tOut);
        for (int i = 0; i < 6; i++) vBl[i] = view(tex(std::max(1, W >> (i + 1)), std::max(1, H >> (i + 1)), HDR, RA | TB));
        {
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(pRB, 0);
            bgBlur = group(l, {{0, nullptr, nullptr, vRay}});
            wgpuBindGroupLayoutRelease(l);
        }
        {
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(pRays, 0);
            bgRays = group(l, {{0, ubo}, {1, nullptr, sRep}, {2, nullptr, sCmp}, {3, nullptr, nullptr, view(tShadow)}, {4, nullptr, nullptr, view(tWater)},
                               {5, nullptr, nullptr, vDp}, {6, nullptr, nullptr, view(tVol)}, {7, nullptr, sClamp}});
            wgpuBindGroupLayoutRelease(l);
        }
        {
            WGPUBindGroupLayout l = wgpuRenderPipelineGetBindGroupLayout(pPost, 0);
            bgPost = group(l, {{0, ubo}, {1, nullptr, sClamp}, {2, nullptr, nullptr, vRes}, {3, nullptr, nullptr, vRay2}, {4, nullptr, nullptr, vBl[0]}, {5, nullptr, nullptr, vDp}});
            wgpuBindGroupLayoutRelease(l);
        }
        // down: res -> b0 -> ... -> b5 ; up: b5 -> b4 (additive) ... -> b0
        for (int i = 0; i < 6; i++) { bloomTarget.push_back(vBl[i]); bloomClear.push_back(true); bloomBG.push_back(blitBG(i ? vBl[i - 1] : vRes)); }
        for (int i = 5; i > 0; i--) { bloomTarget.push_back(vBl[i - 1]); bloomClear.push_back(false); bloomBG.push_back(blitBG(vBl[i])); }
        rowBytes = uint32_t(W) * 4;   // 1920 * 4 is a multiple of 256
        rowBytes = (rowBytes + 255) & ~255u;
        readback = emptyBuf(size_t(rowBytes) * H, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        UB[52] = float(W); UB[53] = float(H); UB[54] = float(1.0 / W); UB[55] = float(1.0 / H);
    }

    // A new picture size without building the world again: the render targets are made anew.
    void resizeTargets(int w, int h) {
        wait();
        for (auto* g : {bgBlur, bgRays, bgPost}) if (g) wgpuBindGroupRelease(g);
        bgBlur = bgRays = bgPost = nullptr;
        for (size_t i = markBloomBG; i < bloomBG.size(); i++) if (bloomBG[i]) wgpuBindGroupRelease(bloomBG[i]);
        bloomBG.resize(markBloomBG);
        bloomTarget.clear(); bloomClear.clear();
        for (size_t i = markViews; i < views.size(); i++) wgpuTextureViewRelease(views[i]);
        views.resize(markViews);
        for (size_t i = markTextures; i < textures.size(); i++) { wgpuTextureDestroy(textures[i]); wgpuTextureRelease(textures[i]); }
        textures.resize(markTextures);
        for (size_t i = markBuffers; i < buffers.size(); i++) { wgpuBufferDestroy(buffers[i]); wgpuBufferRelease(buffers[i]); }
        buffers.resize(markBuffers);
        W = w; H = h;
        targets();
    }

    // Draws the cells of a cell-sorted buffer that are in view and within [minD, maxD) as merged runs, nearest rows first.
    // kind: 1 terrain (indexed quads), 2 instanced mesh, 0 vertex-pulled instances (leaves)
    void drawCells(WGPURenderPassEncoder r, const std::vector<uint32_t>& start, const std::vector<float>& lo, const std::vector<float>& hi, double mar,
                   int kind, const BuiltMesh* mesh, uint32_t verts, double minD, double maxD) {
        const double cx = CX, cy = CY, cz = CZ, fx = FX, fy = FY, fz = FZ, rx = RX, rz = RZ, ux = UX, uy = UY, uz = UZ;
        const double ds = kind == 1 ? 1 : det * det;
        const double tx = TANH * W / H, hw = CELLW * 0.5, ex = hw + mar, min2 = minD * minD * ds, max2 = maxD * maxD * ds;
        for (int k = 0; k < CELLS; k++) {
            const int rw = rowOrd[k], row = rw * CELLS;
            const double pz = OZ + (rw + 0.5) * CELLW - cz, hz = std::max(std::abs(pz) - hw, 0.0);
            int run = -1;
            for (int x = 0; x <= CELLS; x++) {
                const int c = row + x;
                bool v = false;
                if (x < CELLS) {
                    const double l = lo[c], h = hi[c];
                    if (l <= h) {
                        const double px = OX + (x + 0.5) * CELLW - cx, py = (l + h) * 0.5 - cy, ey = (h - l) * 0.5 + mar, hx = std::max(std::abs(px) - hw, 0.0), h2 = hx * hx + hz * hz;
                        if (h2 < max2 && h2 >= min2) {
                            const double lim = px * fx + py * fy + pz * fz + ex * std::abs(fx) + ey * std::abs(fy) + ex * std::abs(fz);
                            if (lim >= 0 && std::abs(px * rx + pz * rz) - ex * (std::abs(rx) + std::abs(rz)) <= lim * tx)
                                v = std::abs(px * ux + py * uy + pz * uz) - ex * (std::abs(ux) + std::abs(uz)) - ey * std::abs(uy) <= lim * TANH;
                        }
                    }
                }
                if (v) { if (run < 0) run = c; }
                else if (run >= 0) {
                    const uint32_t a = start[run], n = start[c] - a;
                    if (n > 0) {
                        if (kind == 1) wgpuRenderPassEncoderDrawIndexed(r, n * 6, 1, a * 6, 0, nSolid);
                        else if (kind == 2) wgpuRenderPassEncoderDrawIndexed(r, uint32_t(mesh->ib.size()), n, mesh->i0, int32_t(mesh->v0), a);
                        else wgpuRenderPassEncoderDraw(r, verts, n, 0, a);
                    }
                    run = -1;
                }
            }
        }
    }

    void render() {
        // camera basis (right vector is horizontal)
        const double cx = CX, cy = CY, cz = CZ, pitch = PIT, yaw = YAW;
        const double cp = std::cos(pitch), fy = std::sin(pitch), rx = std::cos(yaw), rz = std::sin(yaw), fx = cp * rz, fz = -cp * rx;
        const double ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;
        FX = fx; FY = fy; FZ = fz; RX = rx; RZ = rz; UX = ux; UY = uy; UZ = uz;
        const double asp = double(W) / H, sx = 1 / (TANH * asp), sy = 1 / TANH;
        float* m = UB;
        m[0] = float(rx * sx); m[4] = 0; m[8] = float(rz * sx); m[12] = float(-(rx * cx + rz * cz) * sx);
        m[1] = float(ux * sy); m[5] = float(uy * sy); m[9] = float(uz * sy); m[13] = float(-(ux * cx + uy * cy + uz * cz) * sy);
        m[2] = 0; m[6] = 0; m[10] = 0; m[14] = float(NEAR);
        m[3] = float(fx); m[7] = float(fy); m[11] = float(fz); m[15] = float(-(fx * cx + fy * cy + fz * cz));
        m[32] = float(cx); m[33] = float(cy); m[34] = float(cz); m[35] = float(TIME);
        m[36] = float(rx); m[37] = 0; m[38] = float(rz); m[39] = float(TANH * asp);
        m[40] = float(ux); m[41] = float(uy); m[42] = float(uz); m[43] = float(TANH);
        m[44] = float(fx); m[45] = float(fy); m[46] = float(fz); m[47] = float(NEAR);
        const double zx = cx - PURPLE[0], zz = cz - PURPLE[1];
        double zt = (44 - std::sqrt(zx * zx + zz * zz)) / 18; zt = zt < 0 ? 0 : zt > 1 ? 1 : zt;
        m[51] = float(zt * zt * (3 - 2 * zt));
        double ek = (-cy - 8) / 26; ek = ek < 0 ? 0 : ek > 1 ? 1 : ek;
        m[56] = float(EXPO * (0.6 + 0.4 * ek)); m[57] = float(RAYS); m[58] = float(det);
        wgpuQueueWriteBuffer(Q, ubo, 0, UB, sizeof UB);
        WT[0] = float(TIME);
        wgpuQueueWriteBuffer(Q, uboW, 0, WT, sizeof WT);

        {
            int cr = int(std::floor((cz - OZ) / CELLW)); cr = cr < 0 ? 0 : cr > CELLS - 1 ? CELLS - 1 : cr;
            int n = 0; rowOrd[n++] = uint8_t(cr);
            for (int dd = 1; dd < CELLS; dd++) { if (cr - dd >= 0) rowOrd[n++] = uint8_t(cr - dd); if (cr + dd < CELLS) rowOrd[n++] = uint8_t(cr + dd); }
        }
        WGPUCommandEncoder e = wgpuDeviceCreateCommandEncoder(dev, nullptr);
        const auto end = [](WGPURenderPassEncoder r) { wgpuRenderPassEncoderEnd(r); wgpuRenderPassEncoderRelease(r); };
        // animated water texture + mips
        WGPURenderPassEncoder r = pass(e, waterViews[0], true);
        wgpuRenderPassEncoderSetPipeline(r, pWater); wgpuRenderPassEncoderSetBindGroup(r, 0, bgWater, 0, nullptr); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); end(r);
        for (int i = 1; i < 5; i++) {
            r = pass(e, waterViews[i], true);
            wgpuRenderPassEncoderSetPipeline(r, pMip16); wgpuRenderPassEncoderSetBindGroup(r, 0, waterBG[i - 1], 0, nullptr); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); end(r);
        }
        // scene
        r = pass(e, vMS, true, vRes, true, vDp, 0, 1);
        wgpuRenderPassEncoderSetBindGroup(r, 0, bg0, 0, nullptr);
        wgpuRenderPassEncoderSetPipeline(r, pTerrain);
        wgpuRenderPassEncoderSetVertexBuffer(r, 0, vbTerrain, 0, WGPU_WHOLE_SIZE); wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibSolids, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(r, ibTerrain, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        drawCells(r, T.start, T.lo, T.hi, 1.5, 1, nullptr, 0, 0, 240);
        wgpuRenderPassEncoderSetPipeline(r, pSolid);
        wgpuRenderPassEncoderSetVertexBuffer(r, 0, vbMesh, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(r, ibMesh, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        for (int i = 0; i < 12; i++) { const auto& g = Wd.solidRanges[i]; if (g[1]) wgpuRenderPassEncoderDrawIndexed(r, uint32_t(meshes[i].ib.size()), g[1], meshes[i].i0, int32_t(meshes[i].v0), g[0]); }
        for (int i = 0; i < NCHUNK; i++) {
            const CellSet& c = Wd.chunks[i];
            const bool bigc = i > 2 && i < 6;
            wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibChunk[i], 0, WGPU_WHOLE_SIZE);
            drawCells(r, c.start, c.lo, c.hi, 3, 2, &meshes[14 + i], 0, 0, i > 5 ? 30 : bigc ? 40 : 96);
            if (bigc) drawCells(r, c.start, c.lo, c.hi, 3, 2, &meshes[20 + i], 0, 40, 150);
        }
        for (int i = 0; i < 6; i++) {
            const bool lod = i == 3 || i == 4;
            const CellSet& c = Wd.props[i];
            wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibProp[i], 0, WGPU_WHOLE_SIZE);
            drawCells(r, c.start, c.lo, c.hi, 3, 2, &meshes[4 + i], 0, 0, lod ? 6 : 110);
            if (lod) drawCells(r, c.start, c.lo, c.hi, 3, 2, &meshes[18 + i], 0, 6, 110);
        }
        const BuiltMesh& MANTA = meshes[12];
        const BuiltMesh& FISH = meshes[13];
        wgpuRenderPassEncoderSetPipeline(r, pManta); wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibManta, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(r, uint32_t(MANTA.ib.size()), nManta, MANTA.i0, int32_t(MANTA.v0), 0);
        wgpuRenderPassEncoderSetPipeline(r, pFish); wgpuRenderPassEncoderSetVertexBuffer(r, 1, ibFish, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(r, uint32_t(FISH.ib.size()), nFish, FISH.i0, int32_t(FISH.v0), 0);
        // kelp and plants: 9-row blades up close, 5 rows mid-range, a decimated set far away
        wgpuRenderPassEncoderSetPipeline(r, pLeaf); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibBig, 0, WGPU_WHOLE_SIZE);
        drawCells(r, Wd.big.start, Wd.big.lo, Wd.big.hi, 5, 0, nullptr, 18, 0, 24);
        wgpuRenderPassEncoderSetPipeline(r, pLeaf5);
        drawCells(r, Wd.big.start, Wd.big.lo, Wd.big.hi, 5, 0, nullptr, 10, 24, 58);
        wgpuRenderPassEncoderSetPipeline(r, pLeafF); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibBigF, 0, WGPU_WHOLE_SIZE);
        drawCells(r, Wd.bigF.start, Wd.bigF.lo, Wd.bigF.hi, 5, 0, nullptr, 6, 58, 150);
        wgpuRenderPassEncoderSetPipeline(r, pLeafS); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibSmall, 0, WGPU_WHOLE_SIZE);
        drawCells(r, Wd.small.start, Wd.small.lo, Wd.small.hi, 1.5, 0, nullptr, 6, 0, 70);
        wgpuRenderPassEncoderSetPipeline(r, pSurf); wgpuRenderPassEncoderDraw(r, 4, 1, 0, 0);
        wgpuRenderPassEncoderSetPipeline(r, pBack); wgpuRenderPassEncoderDraw(r, 4, 1, 0, 0);
        wgpuRenderPassEncoderSetPipeline(r, pBub); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibBubbles, 0, WGPU_WHOLE_SIZE); wgpuRenderPassEncoderDraw(r, 4, nBub, 0, 0);
        wgpuRenderPassEncoderSetPipeline(r, pGl); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibGl, 0, WGPU_WHOLE_SIZE); wgpuRenderPassEncoderDraw(r, 4, nGl, 0, 0);
        wgpuRenderPassEncoderSetPipeline(r, pSnow); wgpuRenderPassEncoderSetVertexBuffer(r, 0, ibSnowB, 0, WGPU_WHOLE_SIZE); wgpuRenderPassEncoderDraw(r, 4, nSnow, 0, 0);
        end(r);
        // light shafts
        r = pass(e, vRay, true); wgpuRenderPassEncoderSetPipeline(r, pRays); wgpuRenderPassEncoderSetBindGroup(r, 0, bgRays, 0, nullptr); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); end(r);
        r = pass(e, vRay2, true); wgpuRenderPassEncoderSetPipeline(r, pRB); wgpuRenderPassEncoderSetBindGroup(r, 0, bgBlur, 0, nullptr); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); end(r);
        // bloom
        for (size_t i = 0; i < bloomTarget.size(); i++) {
            r = pass(e, bloomTarget[i], bloomClear[i]);
            wgpuRenderPassEncoderSetPipeline(r, i == 0 ? pDown0 : i < 6 ? pDown : pUp);
            wgpuRenderPassEncoderSetBindGroup(r, 0, bloomBG[bloomBG.size() - bloomTarget.size() + i], 0, nullptr);
            wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); end(r);
        }
        // composite
        r = pass(e, vOut, true, nullptr, false, nullptr, 0, 1);
        wgpuRenderPassEncoderSetPipeline(r, pPost); wgpuRenderPassEncoderSetBindGroup(r, 0, bgPost, 0, nullptr); wgpuRenderPassEncoderDraw(r, 3, 1, 0, 0); end(r);
        // read the picture back
        WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; src.texture = tOut;
        WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT; dst.buffer = readback; dst.layout.bytesPerRow = rowBytes; dst.layout.rowsPerImage = uint32_t(H);
        const WGPUExtent3D ext = {uint32_t(W), uint32_t(H), 1};
        wgpuCommandEncoderCopyTextureToBuffer(e, &src, &dst, &ext);
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(e, nullptr);
        wgpuQueueSubmit(Q, 1, &cb);
        wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(e);
    }

    // one step of main.js frame(): look, swim, collide
    void move(double dt, const Controls& in) {
        if (in.pose >= 0 && in.pose < int(POSES.size())) { lookAt(POSES[in.pose]); VX = VY = VZ = 0; }
        TIME += dt;
        // right stick: holding it turns, like the touch joystick of the original
        lkx += in.stickX * 2.2 * dt; lky += in.stickY * 1.4 * dt;
        // mouse look through two cascaded low-pass stages
        const double kq = 1 - std::exp(-dt * 30);
        lkx += in.lookX * 0.0022; lky += in.lookY * 0.0022;
        const double ax = lkx * kq, ay = lky * kq; lkx -= ax; lky -= ay; l2x += ax; l2y += ay;
        const double dyw = l2x * kq, dpt = l2y * kq; l2x -= dyw; l2y -= dpt;
        const double yaw = YAW + dyw;
        double pitch = PIT - dpt; pitch = pitch < -1.55 ? -1.55 : pitch > 1.55 ? 1.55 : pitch;
        const double sp = in.fast ? 26 : 7, k = 1 - std::exp(-dt * 4.5), mf = in.forward, mr = in.right, mu = in.up;
        const double cp = std::cos(pitch), sy = std::sin(yaw), cw = std::cos(yaw);
        const double vx = VX + ((sy * cp * mf + cw * mr) * sp - VX) * k, vy = VY + ((std::sin(pitch) * mf + mu) * sp - VY) * k, vz = VZ + ((-cw * cp * mf + sy * mr) * sp - VZ) * k;
        double x = CX + vx * dt, y = CY + vy * dt, z = CZ + vz * dt;
        const double rad = std::sqrt(x * x + z * z);
        if (rad > 150) { x *= 150 / rad; z *= 150 / rad; }
        y = y < -52 ? -52 : y > -1.2 ? -1.2 : y;
        YAW = yaw; PIT = pitch; VX = vx; VY = vy; VZ = vz; MX = x - CX; MY = y - CY; MZ = z - CZ;
        collide();
    }
};

Scene::Scene() : d(std::make_unique<Impl>()) {}
Scene::~Scene() = default;

bool Scene::init(int width, int height, const std::function<void(const char*, double)>& progress, std::string& error) {
    d->W = width & ~1; d->H = height & ~1;
    return d->build(progress, error);
}

bool Scene::frame(double dt, const Controls& in, std::vector<uint8_t>& rgba) {
    d->move(std::min(dt, 0.1), in);
    d->render();
    const size_t bytes = size_t(d->rowBytes) * d->H;
    if (!d->mapRead(d->readback, bytes)) return false;
    const auto* src = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(d->readback, 0, bytes));
    rgba.resize(size_t(d->W) * d->H * 4);
    if (src) for (int y = 0; y < d->H; y++) std::memcpy(rgba.data() + size_t(y) * d->W * 4, src + size_t(y) * d->rowBytes, size_t(d->W) * 4);
    wgpuBufferUnmap(d->readback);
    return src != nullptr;
}

bool Scene::resize(int width, int height) {
    width &= ~1; height &= ~1;
    if (width == d->W && height == d->H) return true;
    if (width < 64 || height < 64) return false;
    d->resizeTargets(width, height);
    return d->lastError.empty();
}

int Scene::width() const { return d->W; }
int Scene::height() const { return d->H; }
void Scene::camera(double& x, double& y, double& z) const { x = d->CX; y = d->CY; z = d->CZ; }
std::string Scene::adapterName() const { return d->adapterName; }

} // namespace abyssal
