// VeyAmux for Linux: force-included when building the FidelityFX SDK 1.1.4 sources with GCC/Clang.
// The SDK is written against MSVC; these are the few things MSVC provides implicitly.
#pragma once
#ifdef __cplusplus
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <locale>
#include <codecvt>
#include <algorithm>
#include <mutex>
#endif
#include <string.h>
#include <wchar.h>
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef FFX_UNUSED
#define FFX_UNUSED(x) ((void)(x))
#endif

// MSVC "secure" CRT functions used by the SDK.
#ifdef __cplusplus
#include <cmath>
#include <cstdarg>
template <size_t N> inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) {
    size_t i = 0; for (; src && src[i] && i + 1 < N; ++i) dst[i] = src[i]; dst[i] = 0; return 0;
}
inline int wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src) {
    if (!dst || n == 0) return 22; size_t i = 0; for (; src && src[i] && i + 1 < n; ++i) dst[i] = src[i]; dst[i] = 0; return 0;
}
template <size_t N> inline int strcpy_s(char (&dst)[N], const char* src) { std::snprintf(dst, N, "%s", src ? src : ""); return 0; }
inline int strcpy_s(char* dst, size_t n, const char* src) { if (!dst || !n) return 22; std::snprintf(dst, n, "%s", src ? src : ""); return 0; }
inline int sprintf_s(char* buf, size_t n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = std::vsnprintf(buf, n, fmt, ap); va_end(ap); return r;
}
template <size_t N> inline int sprintf_s(char (&buf)[N], const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = std::vsnprintf(buf, N, fmt, ap); va_end(ap); return r;
}
#endif
