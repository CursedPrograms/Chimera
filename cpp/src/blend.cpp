// Chimera - blend modes, ported from PhotoCraft (MIT OR Apache-2.0; see NOTICE).
#include "blend.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

static const char* NAMES[BM_Count] = {
    "Normal", "Dissolve",
    "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color",
    "Lighten", "Screen", "Color Dodge", "Linear Dodge (Add)", "Lighter Color",
    "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light", "Pin Light", "Hard Mix",
    "Difference", "Exclusion", "Subtract", "Divide",
    "Hue", "Saturation", "Color", "Luminosity"};

const char* blendName(int mode) { return mode >= 0 && mode < BM_Count ? NAMES[mode] : "?"; }
bool blendGroupStart(int m) {
    return m == BM_Darken || m == BM_Lighten || m == BM_Overlay || m == BM_Difference || m == BM_Hue;
}

static const float EDGE = 1e-4f;

// Photoshop's Color Burn / Dodge: backdrop values within EDGE of 0 / 1 count as exact (psblend.rs)
static float colorBurn(float cb, float cs) {
    if (cb >= 1.0f - EDGE) return 1.0f;
    if (cs <= 0.0f) return 0.0f;
    return 1.0f - std::min(1.0f, (1.0f - cb) / cs);
}
static float colorDodge(float cb, float cs) {
    if (cb <= EDGE) return 0.0f;
    if (cs >= 1.0f) return 1.0f;
    return std::min(1.0f, cb / (1.0f - cs));
}
// Vivid Light: the source extremes win (cs = 0 gives 0 even over white)
static float vividLightPS(float cb, float cs) {
    if (cs <= 0.5f) return cs <= 0.0f ? 0.0f : 1.0f - std::min(1.0f, (1.0f - cb) / (2.0f * cs));
    if (cs >= 1.0f) return 1.0f;
    return std::min(1.0f, cb / (2.0f * (1.0f - cs)));
}
// the generic vivid light (backdrop extremes win), thresholded: Hard Mix
static float vividLightGeneric(float cb, float cs) {
    if (cs <= 0.5f) {
        if (cb >= 1.0f - EDGE) return 1.0f;
        if (cs <= 0.0f) return 0.0f;
        return 1.0f - std::min(1.0f, (1.0f - cb) / (2.0f * cs));
    }
    if (cb <= EDGE) return 0.0f;
    if (cs >= 1.0f) return 1.0f;
    return std::min(1.0f, cb / (2.0f * (1.0f - cs)));
}
static float hardMix(float cb, float cs) { return vividLightGeneric(cb, cs) >= 0.5f - 1e-6f ? 1.0f : 0.0f; }
static float hardLight(float cb, float cs) {
    if (cs <= 0.5f) return cb * 2.0f * cs;
    const float s = 2.0f * cs - 1.0f;
    return cb + s - cb * s;
}
static float softLightPS(float cb, float cs) {
    if (cs <= 0.5f) return 2.0f * cb * cs + cb * cb * (1.0f - 2.0f * cs);
    const float b = std::max(0.0f, cb);
    const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : std::sqrt(b);
    return 2.0f * cb * (1.0f - cs) + d * (2.0f * cs - 1.0f);
}

static float channel(int mode, float cb, float cs) {
    switch (mode) {
    case BM_Darken: return std::min(cb, cs);
    case BM_Multiply: return cb * cs;
    case BM_ColorBurn: return colorBurn(cb, cs);
    case BM_LinearBurn: return std::max(0.0f, cb + cs - 1.0f);
    case BM_Lighten: return std::max(cb, cs);
    case BM_Screen: return cb + cs - cb * cs;
    case BM_ColorDodge: return colorDodge(cb, cs);
    case BM_LinearDodge: return std::min(1.0f, cb + cs);
    case BM_Overlay: return hardLight(cs, cb);
    case BM_SoftLight: return softLightPS(cb, cs);
    case BM_HardLight: return hardLight(cb, cs);
    case BM_VividLight: return vividLightPS(cb, cs);
    case BM_LinearLight: return std::clamp(cb + 2.0f * cs - 1.0f, 0.0f, 1.0f);
    case BM_PinLight: return cs <= 0.5f ? std::min(cb, 2.0f * cs) : std::max(cb, 2.0f * cs - 1.0f);
    case BM_HardMix: return hardMix(cb, cs);
    case BM_Difference: return std::fabs(cb - cs);
    case BM_Exclusion: return cb + cs - 2.0f * cb * cs;
    case BM_Subtract: return std::max(0.0f, cb - cs);
    case BM_Divide:
        if (cs <= 0.0f) return cb <= 0.0f ? 0.0f : 1.0f;
        return std::min(1.0f, cb / cs);
    default: return cs;
    }
}

// ---- the non-separable modes (Hue, Saturation, Color, Luminosity), W3C / PDF
static float lum(const float c[3]) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }
static void clipColor(float c[3]) {
    const float l = lum(c);
    const float n = std::min({c[0], c[1], c[2]}), x = std::max({c[0], c[1], c[2]});
    if (n < 0.0f) {
        const float d = l - n;
        for (int i = 0; i < 3; ++i) c[i] = (l > 0.0f && d > 0.0f) ? l + (c[i] - l) * l / d : l;
    }
    if (x > 1.0f) {
        const float d = x - l;
        for (int i = 0; i < 3; ++i) c[i] = (l < 1.0f && d > 0.0f) ? l + (c[i] - l) * (1.0f - l) / d : l;
    }
}
static void setLum(const float c[3], float l, float out[3]) {
    const float d = l - lum(c);
    out[0] = c[0] + d; out[1] = c[1] + d; out[2] = c[2] + d;
    clipColor(out);
}
static float sat(const float c[3]) { return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}); }
static void setSat(const float c[3], float s, float out[3]) {
    int idx[3] = {0, 1, 2};
    std::sort(idx, idx + 3, [&](int a, int b) { return c[a] < c[b]; });
    const int lo = idx[0], mid = idx[1], hi = idx[2];
    out[0] = out[1] = out[2] = 0.0f;
    if (c[hi] > c[lo]) {
        out[mid] = (c[mid] - c[lo]) * s / (c[hi] - c[lo]);
        out[hi] = s;
    }
    out[lo] = 0.0f;
}

void blendRGB(int mode, const float cb[3], const float cs[3], float out[3]) {
    float t[3];
    switch (mode) {
    case BM_Hue: setSat(cs, sat(cb), t); setLum(t, lum(cb), out); return;
    case BM_Saturation: setSat(cb, sat(cs), t); setLum(t, lum(cb), out); return;
    case BM_Color: setLum(cs, lum(cb), out); return;
    case BM_Luminosity: setLum(cb, lum(cs), out); return;
    case BM_DarkerColor: { const float* p = lum(cs) < lum(cb) ? cs : cb; out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; return; }
    case BM_LighterColor: { const float* p = lum(cs) > lum(cb) ? cs : cb; out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; return; }
    default:
        for (int i = 0; i < 3; ++i) out[i] = channel(mode, cb[i], cs[i]);
    }
}

// a fixed pattern for Dissolve: the same pixels show at the same opacity every time
static float hash01(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

void compositePixel(int mode, float b[4], const float s[4], int x, int y) {
    float as = s[3];
    if (mode == BM_Dissolve) as = hash01(x, y) < as ? 1.0f : 0.0f;
    if (as <= 0.0f) return;
    const float ab = b[3];
    const float ao = as + ab * (1.0f - as);
    if (ao <= 0.0f) { b[0] = b[1] = b[2] = b[3] = 0.0f; return; }
    if (mode == BM_Normal || mode == BM_Dissolve) {
        const float kb = ab * (1.0f - as), inv = 1.0f / ao;
        for (int i = 0; i < 3; ++i) b[i] = (kb * b[i] + as * s[i]) * inv;
        b[3] = ao;
        return;
    }
    float bl[3];
    blendRGB(mode, b, s, bl);
    for (int i = 0; i < 3; ++i) b[i] = ((1.0f - as) * ab * b[i] + (1.0f - ab) * as * s[i] + as * ab * bl[i]) / ao;
    b[3] = ao;
}
