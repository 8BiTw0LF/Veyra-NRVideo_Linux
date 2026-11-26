// VeyAmux for Linux: OpenGL for emulator cores. See RetroGl.h.
#include "RetroGl.h"

#include "../third_party/libretro/libretro.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace veyamux::retro::gl {

namespace {

// ---- the little of EGL and OpenGL that is used (declared here: no headers or libraries needed to build)
using EGLDisplay = void*; using EGLConfig = void*; using EGLContext = void*; using EGLSurface = void*; using EGLDeviceEXT = void*;
using EGLint = int32_t; using EGLBoolean = unsigned; using EGLenum = unsigned;
constexpr EGLint EGL_NONE = 0x3038, EGL_RED_SIZE = 0x3024, EGL_GREEN_SIZE = 0x3023, EGL_BLUE_SIZE = 0x3022, EGL_ALPHA_SIZE = 0x3021,
                 EGL_DEPTH_SIZE = 0x3025, EGL_STENCIL_SIZE = 0x3026, EGL_RENDERABLE_TYPE = 0x3040, EGL_SURFACE_TYPE = 0x3033,
                 EGL_PBUFFER_BIT = 0x0001, EGL_OPENGL_BIT = 0x0008, EGL_OPENGL_ES2_BIT = 0x0004, EGL_OPENGL_ES3_BIT = 0x0040,
                 EGL_WIDTH = 0x3057, EGL_HEIGHT = 0x3056, EGL_CONTEXT_MAJOR_VERSION = 0x3098, EGL_CONTEXT_MINOR_VERSION = 0x30FB,
                 EGL_CONTEXT_OPENGL_PROFILE_MASK = 0x30FD, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT = 0x1,
                 EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT = 0x2, EGL_EXTENSIONS = 0x3055;
constexpr EGLenum EGL_OPENGL_API = 0x30A2, EGL_OPENGL_ES_API = 0x30A0, EGL_PLATFORM_SURFACELESS_MESA = 0x31DD,
                  EGL_PLATFORM_DEVICE_EXT = 0x313F;

using GLenum = unsigned; using GLuint = unsigned; using GLint = int; using GLsizei = int;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_RENDERBUFFER = 0x8D41, GL_RGBA8 = 0x8058, GL_DEPTH24_STENCIL8 = 0x88F0,
                 GL_DEPTH_COMPONENT16 = 0x81A5, GL_DEPTH_COMPONENT24 = 0x81A6, GL_COLOR_ATTACHMENT0 = 0x8CE0,
                 GL_DEPTH_ATTACHMENT = 0x8D00, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A, GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
                 GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401, GL_PACK_ALIGNMENT = 0x0D05, GL_VERSION = 0x1F02, GL_RENDERER = 0x1F01;

struct Egl {
    void* so = nullptr;
    void* (*getProcAddress)(const char*) = nullptr;
    EGLDisplay (*getDisplay)(void*) = nullptr;
    EGLDisplay (*getPlatformDisplay)(EGLenum, void*, const intptr_t*) = nullptr;           // EGL 1.5
    EGLDisplay (*getPlatformDisplayExt)(EGLenum, void*, const EGLint*) = nullptr;         // EGL_EXT_platform_base
    EGLBoolean (*queryDevices)(EGLint, EGLDeviceEXT*, EGLint*) = nullptr;                 // EGL_EXT_device_enumeration
    EGLBoolean (*initialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
    EGLBoolean (*terminate)(EGLDisplay) = nullptr;
    EGLBoolean (*bindApi)(EGLenum) = nullptr;
    EGLBoolean (*chooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;
    EGLBoolean (*destroyContext)(EGLDisplay, EGLContext) = nullptr;
    EGLSurface (*createPbuffer)(EGLDisplay, EGLConfig, const EGLint*) = nullptr;
    EGLBoolean (*destroySurface)(EGLDisplay, EGLSurface) = nullptr;
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
    EGLint (*getError)() = nullptr;
} egl;

struct Gl {
    void (*genFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*bindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*deleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*genRenderbuffers)(GLsizei, GLuint*) = nullptr;
    void (*bindRenderbuffer)(GLenum, GLuint) = nullptr;
    void (*deleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
    void (*renderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
    void (*framebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
    GLenum (*checkFramebufferStatus)(GLenum) = nullptr;
    void (*readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*pixelStorei)(GLenum, GLint) = nullptr;
    const unsigned char* (*getString)(GLenum) = nullptr;
} glf;

// ---- GLX (X11 / XWayland): what RetroArch uses on Linux, and what some cores need - PPSSPP's GLEW loads OpenGL only
// with a GLX display. It also runs on the GPU the desktop is on (EGL's surfaceless platform takes the first one).
using XDisplay = void*; using GLXFBConfig = void*; using GLXContext = void*; using GLXPbuffer = unsigned long;
constexpr int GLX_DRAWABLE_TYPE = 0x8010, GLX_PBUFFER_BIT = 0x4, GLX_RENDER_TYPE = 0x8011, GLX_RGBA_BIT = 0x1,
              GLX_RED_SIZE = 8, GLX_GREEN_SIZE = 9, GLX_BLUE_SIZE = 10, GLX_ALPHA_SIZE = 11, GLX_PBUFFER_WIDTH = 0x8041,
              GLX_PBUFFER_HEIGHT = 0x8040, GLX_CONTEXT_MAJOR_VERSION_ARB = 0x2091, GLX_CONTEXT_MINOR_VERSION_ARB = 0x2092,
              GLX_CONTEXT_PROFILE_MASK_ARB = 0x9126, GLX_CONTEXT_CORE_PROFILE_BIT_ARB = 0x1,
              GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB = 0x2;
struct Glx {
    void* x11 = nullptr;
    void* gl = nullptr;
    XDisplay (*openDisplay)(const char*) = nullptr;
    int (*closeDisplay)(XDisplay) = nullptr;
    int (*xfree)(void*) = nullptr;
    int (*defaultScreen)(XDisplay) = nullptr;
    GLXFBConfig* (*chooseFbConfig)(XDisplay, int, const int*, int*) = nullptr;
    GLXPbuffer (*createPbuffer)(XDisplay, GLXFBConfig, const int*) = nullptr;
    void (*destroyPbuffer)(XDisplay, GLXPbuffer) = nullptr;
    int (*makeContextCurrent)(XDisplay, GLXPbuffer, GLXPbuffer, GLXContext) = nullptr;
    void (*destroyContext)(XDisplay, GLXContext) = nullptr;
    void* (*getProcAddress)(const unsigned char*) = nullptr;
    GLXContext (*createContextAttribs)(XDisplay, GLXFBConfig, GLXContext, int, const int*) = nullptr;
} glx;
XDisplay xdpy = nullptr;
GLXContext xctx = nullptr;
GLXPbuffer xpb = 0;

bool loadGlx() {
    if (glx.gl) return true;
    glx.x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    glx.gl = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);   // (global: the core's own OpenGL calls find the same)
    if (!glx.x11 || !glx.gl) return false;
    auto x = [](auto& f, const char* n) { f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(dlsym(glx.x11, n)); return f != nullptr; };
    auto g = [](auto& f, const char* n) { f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(dlsym(glx.gl, n)); return f != nullptr; };
    const bool ok = x(glx.openDisplay, "XOpenDisplay") && x(glx.closeDisplay, "XCloseDisplay") && x(glx.xfree, "XFree") &&
                    x(glx.defaultScreen, "XDefaultScreen") && g(glx.chooseFbConfig, "glXChooseFBConfig") &&
                    g(glx.createPbuffer, "glXCreatePbuffer") && g(glx.destroyPbuffer, "glXDestroyPbuffer") &&
                    g(glx.makeContextCurrent, "glXMakeContextCurrent") && g(glx.destroyContext, "glXDestroyContext") &&
                    g(glx.getProcAddress, "glXGetProcAddressARB");
    if (ok) glx.createContextAttribs = reinterpret_cast<decltype(glx.createContextAttribs)>(
                glx.getProcAddress(reinterpret_cast<const unsigned char*>("glXCreateContextAttribsARB")));
    return ok && glx.createContextAttribs;
}

// a GLX context on a 16x16 pbuffer of the X display (DISPLAY: X11, XWayland, gamescope)
bool createGlx(const retro_hw_render_callback& cb) {
    const char* d = std::getenv("DISPLAY");
    if (!d || !*d || std::getenv("VEYAMUX_RETRO_EGL") || !loadGlx()) return false;
    xdpy = glx.openDisplay(nullptr);
    if (!xdpy) return false;
    const int fa[] = {GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                      GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, 0};
    int n = 0;
    GLXFBConfig* cfgs = glx.chooseFbConfig(xdpy, glx.defaultScreen(xdpy), fa, &n);
    if (!cfgs || n < 1) { if (cfgs) glx.xfree(cfgs); glx.closeDisplay(xdpy); xdpy = nullptr; return false; }
    GLXFBConfig cfg = cfgs[0];
    glx.xfree(cfgs);
    std::vector<int> ca;
    if (cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE)
        ca = {GLX_CONTEXT_MAJOR_VERSION_ARB, int(cb.version_major ? cb.version_major : 3), GLX_CONTEXT_MINOR_VERSION_ARB, int(cb.version_minor),
              GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB};
    else
        ca = {GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB};
    ca.push_back(0);
    xctx = glx.createContextAttribs(xdpy, cfg, nullptr, 1, ca.data());
    const int pa[] = {GLX_PBUFFER_WIDTH, 16, GLX_PBUFFER_HEIGHT, 16, 0};
    if (xctx) xpb = glx.createPbuffer(xdpy, cfg, pa);
    if (!xctx || !xpb || !glx.makeContextCurrent(xdpy, xpb, xpb, xctx)) {
        if (xpb) glx.destroyPbuffer(xdpy, xpb);
        if (xctx) glx.destroyContext(xdpy, xctx);
        glx.closeDisplay(xdpy);
        xdpy = xctx = nullptr;
        xpb = 0;
        return false;
    }
    return true;
}

EGLDisplay dpy = nullptr;
EGLContext ctx = nullptr;
EGLSurface surf = nullptr;
GLuint fbo = 0, colorRb = 0, depthRb = 0;
bool gles = false;
void* libGl = nullptr;   // for core functions an EGL may not hand out

template <class T> bool sym(T& f, const char* name) { f = reinterpret_cast<T>(dlsym(egl.so, name)); return f != nullptr; }

bool loadEgl() {
    if (egl.so) return true;
    egl.so = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!egl.so) return false;
    bool ok = sym(egl.getProcAddress, "eglGetProcAddress") && sym(egl.getDisplay, "eglGetDisplay") && sym(egl.initialize, "eglInitialize") &&
              sym(egl.terminate, "eglTerminate") && sym(egl.bindApi, "eglBindAPI") && sym(egl.chooseConfig, "eglChooseConfig") &&
              sym(egl.createContext, "eglCreateContext") && sym(egl.destroyContext, "eglDestroyContext") &&
              sym(egl.createPbuffer, "eglCreatePbufferSurface") && sym(egl.destroySurface, "eglDestroySurface") &&
              sym(egl.makeCurrent, "eglMakeCurrent") && sym(egl.getError, "eglGetError");
    sym(egl.getPlatformDisplay, "eglGetPlatformDisplay");
    egl.getPlatformDisplayExt = reinterpret_cast<decltype(egl.getPlatformDisplayExt)>(egl.getProcAddress("eglGetPlatformDisplayEXT"));
    egl.queryDevices = reinterpret_cast<decltype(egl.queryDevices)>(egl.getProcAddress("eglQueryDevicesEXT"));
    if (!ok) { dlclose(egl.so); egl = {}; }
    return ok;
}

EGLDisplay platformDisplay(EGLenum platform, void* native) {
    if (egl.getPlatformDisplay) return egl.getPlatformDisplay(platform, native, nullptr);
    if (egl.getPlatformDisplayExt) return egl.getPlatformDisplayExt(platform, native, nullptr);
    return nullptr;
}

// a display that can render without a window: Mesa's surfaceless platform, else the first GPU device (NVIDIA), else
// the default display (needs X / Wayland)
EGLDisplay openDisplay(std::string& how) {
    auto tryInit = [](EGLDisplay d) { EGLint ma = 0, mi = 0; return d && egl.initialize(d, &ma, &mi); };
    if (EGLDisplay d = platformDisplay(EGL_PLATFORM_SURFACELESS_MESA, nullptr); tryInit(d)) { how = "surfaceless"; return d; }
    if (egl.queryDevices) {
        EGLDeviceEXT devs[8];
        EGLint n = 0;
        if (egl.queryDevices(8, devs, &n))
            for (EGLint i = 0; i < n; ++i)
                if (EGLDisplay d = platformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i]); tryInit(d)) { how = "device"; return d; }
    }
    if (EGLDisplay d = egl.getDisplay(nullptr); tryInit(d)) { how = "default"; return d; }
    return nullptr;
}

void* glSym(const char* name) {
    if (xctx) return glx.getProcAddress(reinterpret_cast<const unsigned char*>(name));
    if (void* p = egl.getProcAddress ? egl.getProcAddress(name) : nullptr) return p;
    if (!libGl) libGl = dlopen(gles ? "libGLESv2.so.2" : "libOpenGL.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!libGl && !gles) libGl = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
    return libGl ? dlsym(libGl, name) : nullptr;
}

} // namespace

bool finish(const retro_hw_render_callback& cb, int maxW, int maxH, const std::string& how, std::string& error);

bool supported(const retro_hw_render_callback& cb) {
    switch (cb.context_type) {
    case RETRO_HW_CONTEXT_OPENGL: case RETRO_HW_CONTEXT_OPENGL_CORE: case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGLES3: case RETRO_HW_CONTEXT_OPENGLES_VERSION:
        return loadEgl() || loadGlx();
    default:
        return false;   // Vulkan, Direct3D: not here (the cores that need them have an OpenGL choice)
    }
}

bool create(const retro_hw_render_callback& cb, int maxW, int maxH, std::string& error) {
    destroy();
    gles = cb.context_type == RETRO_HW_CONTEXT_OPENGLES2 || cb.context_type == RETRO_HW_CONTEXT_OPENGLES3 ||
           cb.context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION;
    std::string how;
    if (!gles && createGlx(cb)) { how = "GLX"; return finish(cb, maxW, maxH, how, error); }
    if (!loadEgl()) { error = "OpenGL is not available (libEGL.so.1 not found)"; return false; }
    dpy = openDisplay(how);
    if (!dpy) { error = "OpenGL is not available (no EGL display)"; return false; }
    egl.bindApi(gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    const int esMajor = cb.context_type == RETRO_HW_CONTEXT_OPENGLES2 ? 2 : cb.context_type == RETRO_HW_CONTEXT_OPENGLES3 ? 3
                        : int(cb.version_major ? cb.version_major : 2);
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                              EGL_RENDERABLE_TYPE, gles ? (esMajor >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT) : EGL_OPENGL_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    if (!egl.chooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) { error = "OpenGL is not available (no EGL config)"; destroy(); return false; }
    std::vector<EGLint> ca;
    if (gles) {
        ca = {EGL_CONTEXT_MAJOR_VERSION, esMajor, EGL_CONTEXT_MINOR_VERSION, int(cb.context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION ? cb.version_minor : 0)};
    } else if (cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE) {
        ca = {EGL_CONTEXT_MAJOR_VERSION, int(cb.version_major ? cb.version_major : 3), EGL_CONTEXT_MINOR_VERSION, int(cb.version_minor),
              EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT};
    } else {
        ca = {EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT};
    }
    ca.push_back(EGL_NONE);
    ctx = egl.createContext(dpy, cfg, nullptr, ca.data());
    if (!ctx && !gles && cb.context_type == RETRO_HW_CONTEXT_OPENGL) {   // (an EGL without profile selection)
        const EGLint none[] = {EGL_NONE};
        ctx = egl.createContext(dpy, cfg, nullptr, none);
    }
    if (!ctx) { error = "OpenGL " + std::to_string(cb.version_major) + "." + std::to_string(cb.version_minor) + " is not available"; destroy(); return false; }
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    surf = egl.createPbuffer(dpy, cfg, pb);
    if (!egl.makeCurrent(dpy, surf, surf, ctx)) { error = "OpenGL could not be made current"; destroy(); return false; }
    return finish(cb, maxW, maxH, how, error);
}

// the framebuffer the core draws into, on the context made current above
bool finish(const retro_hw_render_callback& cb, int maxW, int maxH, const std::string& how, std::string& error) {

    auto g = [](auto& f, const char* name) { f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(glSym(name)); return f != nullptr; };
    if (!(g(glf.genFramebuffers, "glGenFramebuffers") && g(glf.bindFramebuffer, "glBindFramebuffer") &&
          g(glf.deleteFramebuffers, "glDeleteFramebuffers") && g(glf.genRenderbuffers, "glGenRenderbuffers") &&
          g(glf.bindRenderbuffer, "glBindRenderbuffer") && g(glf.deleteRenderbuffers, "glDeleteRenderbuffers") &&
          g(glf.renderbufferStorage, "glRenderbufferStorage") && g(glf.framebufferRenderbuffer, "glFramebufferRenderbuffer") &&
          g(glf.checkFramebufferStatus, "glCheckFramebufferStatus") && g(glf.readPixels, "glReadPixels") &&
          g(glf.pixelStorei, "glPixelStorei") && g(glf.getString, "glGetString"))) {
        error = "OpenGL functions are missing";
        destroy();
        return false;
    }
    // the core's framebuffer: colour, and depth / stencil when it asks for them
    glf.genFramebuffers(1, &fbo);
    glf.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glf.genRenderbuffers(1, &colorRb);
    glf.bindRenderbuffer(GL_RENDERBUFFER, colorRb);
    glf.renderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, maxW, maxH);
    glf.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRb);
    if (cb.depth || cb.stencil) {
        glf.genRenderbuffers(1, &depthRb);
        glf.bindRenderbuffer(GL_RENDERBUFFER, depthRb);
        if (cb.stencil) {
            glf.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, maxW, maxH);
            glf.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRb);
        } else {
            glf.renderbufferStorage(GL_RENDERBUFFER, gles ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24, maxW, maxH);
            glf.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
        }
    }
    if (glf.checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { error = "OpenGL framebuffer incomplete"; destroy(); return false; }
    std::fprintf(stderr, "Games: OpenGL for the core (%s, %s, %s), %dx%d\n", how.c_str(),
                 reinterpret_cast<const char*>(glf.getString(GL_VERSION)), reinterpret_cast<const char*>(glf.getString(GL_RENDERER)), maxW, maxH);
    return true;
}

uintptr_t framebuffer() { return fbo; }
void* procAddress(const char* name) { return glSym(name); }
bool active() { return ctx != nullptr || xctx != nullptr; }

bool read(int w, int h, bool bottomLeft, std::vector<uint8_t>& out) {
    if (!active() || w <= 0 || h <= 0) return false;
    static std::vector<uint8_t> rgba;
    rgba.resize(size_t(w) * h * 4);
    glf.bindFramebuffer(GL_FRAMEBUFFER, fbo);
    glf.pixelStorei(GL_PACK_ALIGNMENT, 4);
    glf.readPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // RGBA bytes -> XRGB8888 (B, G, R, X in memory), top row first
    out.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = rgba.data() + size_t(bottomLeft ? h - 1 - y : y) * w * 4;
        uint8_t* d = out.data() + size_t(y) * w * 4;
        for (int x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0xff; }
    }
    return true;
}

void destroy() {
    if (active() && glf.deleteFramebuffers) {
        if (fbo) glf.deleteFramebuffers(1, &fbo);
        if (colorRb) glf.deleteRenderbuffers(1, &colorRb);
        if (depthRb) glf.deleteRenderbuffers(1, &depthRb);
    }
    fbo = colorRb = depthRb = 0;
    if (dpy) {
        egl.makeCurrent(dpy, nullptr, nullptr, nullptr);
        if (surf) egl.destroySurface(dpy, surf);
        if (ctx) egl.destroyContext(dpy, ctx);
        egl.terminate(dpy);
    }
    dpy = ctx = surf = nullptr;
    if (xdpy) {
        glx.makeContextCurrent(xdpy, 0, 0, nullptr);
        if (xpb) glx.destroyPbuffer(xdpy, xpb);
        if (xctx) glx.destroyContext(xdpy, xctx);
        glx.closeDisplay(xdpy);
    }
    xdpy = xctx = nullptr;
    xpb = 0;
}

} // namespace veyamux::retro::gl
