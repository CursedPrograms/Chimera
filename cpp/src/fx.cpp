// LayerTack - Sharpen, Distort, Stylize and Select > Modify (see fx.h)
#include "fx.h"
#include "refine.h"
#include <algorithm>
#include <cmath>

static const float PI = 3.14159265358979f, TAU = 6.28318530717959f;

FxBox fxBounds(const Doc& d, const Work& w) {
    FxBox b{0, 0, w.w, w.h};
    if (d.hasSel()) {
        const Rect s = d.selBounds();
        if (!s.empty()) b = {std::max(0, s.x0 - w.ox), std::max(0, s.y0 - w.oy), std::min(w.w, s.x1 - w.ox), std::min(w.h, s.y1 - w.oy)};
        if (b.x1 <= b.x0 || b.y1 <= b.y0) b = {0, 0, w.w, w.h};
    }
    return b;
}

// ---------- Sharpen ----------
void fxUnsharp(Work& w, float amount, float radius, float threshold) {
    Work bl = w;
    filterGaussianBlur(bl, radius);
    const float k = amount / 100.0f, t = threshold / 255.0f;
    for (size_t i = 0; i < w.px.size(); i += 4) {
        if (w.px[i + 3] <= 0) continue;
        for (int c = 0; c < 3; ++c) {
            const float o = w.px[i + c], d = o - bl.px[i + c];
            if (std::fabs(d) + 1e-6f >= t) w.px[i + c] = std::clamp(o + k * d, 0.0f, 1.0f);
        }
    }
}

void fxHighPass(Work& w, float radius) {
    Work bl = w;
    filterGaussianBlur(bl, radius);
    for (size_t i = 0; i < w.px.size(); i += 4) {
        if (w.px[i + 3] <= 0) continue;
        for (int c = 0; c < 3; ++c) w.px[i + c] = std::clamp(0.5f + w.px[i + c] - bl.px[i + c], 0.0f, 1.0f);
    }
}

// ---------- Distort: every output pixel looks up where it comes from (bilinear, edges repeated) ----------
template <class F> static void remap(Work& w, F f) {
    const Work src = w;
    auto px = [&](int x, int y) { return &src.px[((size_t)std::clamp(y, 0, src.h - 1) * src.w + std::clamp(x, 0, src.w - 1)) * 4]; };
    for (int y = 0; y < w.h; ++y)
        for (int x = 0; x < w.w; ++x) {
            float sx, sy;
            f(x + 0.5f, y + 0.5f, sx, sy);
            sx -= 0.5f; sy -= 0.5f;
            const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
            const float fx = sx - x0, fy = sy - y0;
            const float* q[4] = {px(x0, y0), px(x0 + 1, y0), px(x0, y0 + 1), px(x0 + 1, y0 + 1)};
            const float wt[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
            float acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {   // premultiplied, so transparent pixels don't bleed their colour
                const float a = q[k][3] * wt[k];
                for (int c = 0; c < 3; ++c) acc[c] += q[k][c] * a;
                acc[3] += a;
            }
            float* o = w.at(x, y);
            for (int c = 0; c < 3; ++c) o[c] = acc[3] > 1e-6f ? acc[c] / acc[3] : 0.0f;
            o[3] = acc[3];
        }
}

// Twirl, Pinch and Spherize act inside the ellipse fitted to the box, as Photoshop's do
struct Ell { float cx, cy, hx, hy; };
static Ell ellipse(FxBox b) { return {(b.x0 + b.x1) / 2.0f, (b.y0 + b.y1) / 2.0f, (b.x1 - b.x0) / 2.0f, (b.y1 - b.y0) / 2.0f}; }

void fxTwirl(Work& w, FxBox b, float angle) {
    const Ell e = ellipse(b);
    const float a = angle * PI / 180;
    remap(w, [&](float x, float y, float& sx, float& sy) {
        sx = x; sy = y;
        if (e.hx <= 0 || e.hy <= 0) return;
        const float u = (x - e.cx) / e.hx, v = (y - e.cy) / e.hy, r = std::sqrt(u * u + v * v);
        if (r >= 1) return;
        const float t = a * (1 - r), s = std::sin(t), c = std::cos(t);
        sx = e.cx + (u * c - v * s) * e.hx; sy = e.cy + (u * s + v * c) * e.hy;
    });
}

void fxPinch(Work& w, FxBox b, float amount) {
    const Ell e = ellipse(b);
    const float a = std::clamp(amount / 100, -1.0f, 1.0f);
    remap(w, [&](float x, float y, float& sx, float& sy) {
        sx = x; sy = y;
        if (e.hx <= 0 || e.hy <= 0) return;
        const float u = (x - e.cx) / e.hx, v = (y - e.cy) / e.hy, r = std::sqrt(u * u + v * v);
        if (r >= 1 || r <= 0) return;
        const float k = std::pow(r, 1 - a * 0.5f) / r;
        sx = e.cx + u * k * e.hx; sy = e.cy + v * k * e.hy;
    });
}

static float sphere1d(float rn, float a) {
    const float target = a >= 0 ? std::asin(rn) * 2 / PI : std::sin(rn * PI / 2);
    return rn + std::fabs(a) * (target - rn);
}

void fxSpherize(Work& w, FxBox b, float amount, int mode) {
    const Ell e = ellipse(b);
    const float a = std::clamp(amount / 100, -1.0f, 1.0f);
    remap(w, [&](float x, float y, float& sx, float& sy) {
        sx = x; sy = y;
        const float dx = x - e.cx, dy = y - e.cy;
        if (mode == 1) { if (std::fabs(dx) < e.hx) sx = e.cx + (dx < 0 ? -1 : 1) * e.hx * sphere1d(std::fabs(dx) / e.hx, a); return; }
        if (mode == 2) { if (std::fabs(dy) < e.hy) sy = e.cy + (dy < 0 ? -1 : 1) * e.hy * sphere1d(std::fabs(dy) / e.hy, a); return; }
        if (e.hx <= 0 || e.hy <= 0) return;
        const float u = dx / e.hx, v = dy / e.hy, r = std::sqrt(u * u + v * v);
        if (r >= 1 || r <= 0) return;
        const float k = sphere1d(r, a) / r;
        sx = e.cx + u * k * e.hx; sy = e.cy + v * k * e.hy;
    });
}

static float hash01(int x, int y, unsigned c, unsigned seed) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ c * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return (float)(h >> 8) / (float)(1u << 24);
}

static float waveShape(int type, float phase) {
    float p = std::fmod(phase, TAU); if (p < 0) p += TAU; p /= TAU;
    if (type == 1) return 1 - 4 * std::fabs(p - 0.5f);
    if (type == 2) return p < 0.5f ? 1.0f : -1.0f;
    return std::sin(phase);
}

void fxWave(Work& w, FxBox b, int generators, float wlMin, float wlMax, float ampMin, float ampMax, int type, unsigned seed) {
    const int n = std::clamp(generators, 1, 64);
    struct G { float len, amp, p1, p2; };
    std::vector<G> gens;
    for (int i = 0; i < n; ++i) {
        auto r = [&](unsigned k) { return hash01(i, (int)k, 7, seed); };
        gens.push_back({std::max(1.0f, wlMin + std::max(0.0f, wlMax - wlMin) * r(1)), ampMin + std::max(0.0f, ampMax - ampMin) * r(2), r(3) * TAU, r(4) * TAU});
    }
    remap(w, [&](float x, float y, float& sx, float& sy) {
        const float lx = x - b.x0, ly = y - b.y0;
        float dx = 0, dy = 0;
        for (const G& g : gens) { dx += g.amp * waveShape(type, TAU * ly / g.len + g.p1); dy += g.amp * waveShape(type, TAU * lx / g.len + g.p2); }
        sx = x + dx / n; sy = y + dy / n;
    });
}

void fxRipple(Work& w, FxBox b, float amount, int size) {
    const float len = size == 0 ? 8.0f : size == 1 ? 16.0f : 32.0f;
    const float amp = amount / 100 * len / 8;
    remap(w, [&](float x, float y, float& sx, float& sy) {
        const float lx = x - b.x0, ly = y - b.y0;
        sx = x + amp * std::sin(TAU * ly / len); sy = y + amp * std::sin(TAU * lx / len);
    });
}

void fxPolar(Work& w, FxBox b, bool toPolar) {
    const float W = (float)std::max(1, b.x1 - b.x0), H = (float)std::max(1, b.y1 - b.y0);
    const float cx = b.x0 + W / 2, cy = b.y0 + H / 2, rmax = std::min(W, H) / 2;
    // Photoshop's: the angle runs counter-clockwise from 12 o'clock, evaluated at pixel corners
    remap(w, [&](float x, float y, float& sx, float& sy) {
        x -= 0.5f; y -= 0.5f;
        if (toPolar) {
            const float dx = x - cx, dy = y - cy;
            float th = std::atan2(-dx, -dy); if (th <= 0) th += TAU;
            sx = b.x0 + th / TAU * W; sy = b.y0 + std::sqrt(dx * dx + dy * dy) / rmax * H;
        } else {
            const float th = (x + 1 - b.x0) / W * TAU, r = (y - b.y0) / H * rmax;
            sx = cx - r * std::sin(th); sy = cy - r * std::cos(th);
        }
        sx += 0.5f; sy += 0.5f;
    });
}

// ---------- Stylize ----------
void fxMosaic(Work& w, FxBox b, float cell) {
    const int cs = std::max(1, (int)std::lround(cell));
    for (int y0 = b.y0; y0 < b.y1; y0 += cs)
        for (int x0 = b.x0; x0 < b.x1; x0 += cs) {
            const int x1 = std::min(x0 + cs, b.x1), y1 = std::min(y0 + cs, b.y1);
            double acc[4] = {0, 0, 0, 0}; int count = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) { const float* p = w.at(x, y); for (int c = 0; c < 3; ++c) acc[c] += p[c] * p[3]; acc[3] += p[3]; ++count; }
            float o[4];
            for (int c = 0; c < 3; ++c) o[c] = acc[3] > 1e-7 ? (float)(acc[c] / acc[3]) : 0.0f;
            o[3] = (float)(acc[3] / count);
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) std::copy(o, o + 4, w.at(x, y));
        }
}

static float luma(const float* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

void fxEmboss(Work& w, float angle, float height, float amount) {
    const Work src = w;
    const float a = angle * PI / 180, h = std::max(height, 1.0f), k = amount / 100;
    const int dx = (int)std::lround(std::cos(a) * h), dy = (int)std::lround(-std::sin(a) * h);
    auto L = [&](int x, int y) { return luma(&src.px[((size_t)std::clamp(y, 0, w.h - 1) * w.w + std::clamp(x, 0, w.w - 1)) * 4]); };
    for (int y = 0; y < w.h; ++y)
        for (int x = 0; x < w.w; ++x) {
            float* o = w.at(x, y);
            o[0] = o[1] = o[2] = std::clamp(0.5f + k * (L(x + dx, y + dy) - L(x - dx, y - dy)), 0.0f, 1.0f);
        }
}

void fxFindEdges(Work& w) {
    const Work src = w;
    auto g = [&](int x, int y, int c) { return src.px[((size_t)std::clamp(y, 0, w.h - 1) * w.w + std::clamp(x, 0, w.w - 1)) * 4 + c]; };
    for (int y = 0; y < w.h; ++y)
        for (int x = 0; x < w.w; ++x)
            for (int c = 0; c < 3; ++c) {
                const float gx = (g(x + 1, y - 1, c) + 2 * g(x + 1, y, c) + g(x + 1, y + 1, c)) - (g(x - 1, y - 1, c) + 2 * g(x - 1, y, c) + g(x - 1, y + 1, c));
                const float gy = (g(x - 1, y + 1, c) + 2 * g(x, y + 1, c) + g(x + 1, y + 1, c)) - (g(x - 1, y - 1, c) + 2 * g(x, y - 1, c) + g(x + 1, y - 1, c));
                w.at(x, y)[c] = 1 - std::clamp(std::sqrt(gx * gx + gy * gy), 0.0f, 1.0f);
            }
}

void fxSolarize(Work& w) {
    for (size_t i = 0; i < w.px.size(); i += 4) for (int c = 0; c < 3; ++c) if (w.px[i + c] > 0.5f) w.px[i + c] = 1 - w.px[i + c];
}

// ---------- Select > Modify: by true distance from the edge, so even a 100 px expand is quick ----------
static void modifyByDistance(Doc& d, float px, bool grow) {
    if (!d.hasSel() || px <= 0) return;
    std::vector<uint8_t> seed(d.sel.size());
    for (size_t i = 0; i < seed.size(); ++i) seed[i] = grow ? d.sel[i] >= 128 : d.sel[i] < 128;
    const std::vector<float> dist = distanceTo(seed, d.w, d.h);
    for (size_t i = 0; i < seed.size(); ++i) {
        const float in = std::clamp(px + 0.5f - dist[i], 0.0f, 1.0f);   // a half-pixel ramp: anti-aliased
        const float v = grow ? std::max(d.sel[i] / 255.0f, in) : std::min(d.sel[i] / 255.0f, 1 - in);
        d.sel[i] = to8(v);
    }
}

void selExpand(Doc& d, float px) { modifyByDistance(d, px, true); }
void selContract(Doc& d, float px) { modifyByDistance(d, px, false); }

void selSmooth(Doc& d, int radius) {
    // Photoshop's Smooth: each pixel goes with the majority round it, which rounds corners and drops specks
    if (!d.hasSel() || radius <= 0) return;
    Work m; m.w = d.w; m.h = d.h; m.px.resize((size_t)d.w * d.h * 4);
    for (size_t i = 0; i < d.sel.size(); ++i) { m.px[i * 4] = m.px[i * 4 + 1] = m.px[i * 4 + 2] = d.sel[i] >= 128 ? 1.0f : 0.0f; m.px[i * 4 + 3] = 1; }
    filterBoxBlur(m, radius);
    for (size_t i = 0; i < d.sel.size(); ++i) d.sel[i] = to8(std::clamp((m.px[i * 4] - 0.5f) * 4 + 0.5f, 0.0f, 1.0f));
}

void selBorder(Doc& d, float width) {
    // a band width wide centred on the edge, soft at both sides
    if (!d.hasSel() || width <= 0) return;
    std::vector<uint8_t> in(d.sel.size()), out(d.sel.size());
    for (size_t i = 0; i < in.size(); ++i) { in[i] = d.sel[i] >= 128; out[i] = !in[i]; }
    const std::vector<float> dIn = distanceTo(in, d.w, d.h), dOut = distanceTo(out, d.w, d.h);
    const float half = width / 2;
    for (size_t i = 0; i < d.sel.size(); ++i) {
        const float dist = in[i] ? dOut[i] : dIn[i];   // how far from the edge, either side
        d.sel[i] = to8(std::clamp((half + 0.5f - dist) / std::max(1.0f, half * 0.5f), 0.0f, 1.0f));
    }
}
