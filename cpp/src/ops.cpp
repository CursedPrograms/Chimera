// Chimera - adjustments, filters, fills, selections, resizing and the brush.
#include "ops.h"
#include "blend.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ---------- the target ----------
Work getWork(const Doc& d) {
    const Layer& l = d.layers[d.active];
    Work w;
    w.w = l.img.w; w.h = l.img.h; w.ox = l.x; w.oy = l.y;
    w.mask = d.editMask && l.hasMask;
    w.px.resize((size_t)w.w * w.h * 4);
    for (size_t i = 0; i < (size_t)w.w * w.h; ++i) {
        if (w.mask) { const float v = l.mask[i] / 255.0f; w.px[i * 4] = w.px[i * 4 + 1] = w.px[i * 4 + 2] = v; w.px[i * 4 + 3] = 1.0f; }
        else for (int c = 0; c < 4; ++c) w.px[i * 4 + c] = l.img.px[i * 4 + c] / 255.0f;
    }
    return w;
}

void putWork(Doc& d, const Work& orig, const Work& r) {
    Layer& l = d.cur();
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            const int cx = x + r.ox, cy = y + r.oy;
            float t = 1.0f;
            if (d.hasSel()) t = (cx >= 0 && cy >= 0 && cx < d.w && cy < d.h) ? d.sel[(size_t)cy * d.w + cx] / 255.0f : 0.0f;
            if (t <= 0.0f) continue;
            const size_t i = (size_t)y * r.w + x;
            const float* a = &orig.px[i * 4];
            const float* b = &r.px[i * 4];
            if (r.mask) {
                l.mask[i] = to8(a[0] + (b[0] - a[0]) * t);
            } else {
                for (int c = 0; c < 4; ++c) l.img.px[i * 4 + c] = to8(a[c] + (b[c] - a[c]) * t);
            }
        }
    d.dirty = true;
}

// ---------- colour helpers ----------
static void rgb2hsl(const float* c, float& h, float& s, float& l) {
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
    l = (mx + mn) * 0.5f;
    if (mx == mn) { h = s = 0.0f; return; }
    const float d = mx - mn;
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == c[0]) h = (c[1] - c[2]) / d + (c[1] < c[2] ? 6.0f : 0.0f);
    else if (mx == c[1]) h = (c[2] - c[0]) / d + 2.0f;
    else h = (c[0] - c[1]) / d + 4.0f;
    h /= 6.0f;
}
static float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}
static void hsl2rgb(float h, float s, float l, float* c) {
    if (s <= 0) { c[0] = c[1] = c[2] = l; return; }
    const float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    c[0] = hue2rgb(p, q, h + 1.0f / 3); c[1] = hue2rgb(p, q, h); c[2] = hue2rgb(p, q, h - 1.0f / 3);
}

// ---------- adjustments ----------
void adjBrightnessContrast(Work& w, float brightness, float contrast) {
    // Photoshop's (non-legacy) behaviour, near enough: brightness bends the midtones, contrast pivots round the mean
    double sum = 0; size_t n = 0;
    for (size_t i = 0; i < w.px.size(); i += 4) if (w.px[i + 3] > 0) { sum += 0.3 * w.px[i] + 0.59 * w.px[i + 1] + 0.11 * w.px[i + 2]; ++n; }
    const float mean = n ? (float)(sum / n) : 0.5f;
    const float b = brightness / 150.0f;
    const float k = contrast >= 0 ? 1.0f + contrast / 100.0f * 1.5f : 1.0f + contrast / 100.0f;
    const float g = b >= 0 ? 1.0f / (1.0f + b * 1.2f) : 1.0f - b * 1.2f;     // a gamma: lifts without clipping
    for (size_t i = 0; i < w.px.size(); i += 4)
        for (int c = 0; c < 3; ++c) {
            float v = std::pow(clamp01(w.px[i + c]), g);
            v = (v - mean) * k + mean;
            w.px[i + c] = clamp01(v);
        }
}

void adjHueSat(Work& w, float hue, float sat, float light, bool colorize) {
    for (size_t i = 0; i < w.px.size(); i += 4) {
        float* c = &w.px[i];
        float h, s, l;
        rgb2hsl(c, h, s, l);
        if (colorize) { h = (hue < 0 ? hue + 360 : hue) / 360.0f; s = (sat + 100) / 200.0f; }
        else {
            h += hue / 360.0f; h -= std::floor(h);
            s = sat >= 0 ? s + (1 - s) * s * (sat / 100.0f) * 2.0f : s * (1 + sat / 100.0f);
            s = clamp01(s);
        }
        l = light >= 0 ? l + (1 - l) * light / 100.0f : l * (1 + light / 100.0f);
        hsl2rgb(h, s, clamp01(l), c);
    }
}

void adjVibrance(Work& w, float vib) {
    const float k = vib / 100.0f;
    for (size_t i = 0; i < w.px.size(); i += 4) {
        float* c = &w.px[i];
        float h, s, l;
        rgb2hsl(c, h, s, l);
        const float t = k >= 0 ? s + (1 - s) * (1 - s) * k * 0.9f : s * (1 + k * (1 - s * 0.5f));
        hsl2rgb(h, clamp01(t), l, c);
    }
}

// Photoshop's Black & White: grey = min + (mid - min) * secondary + (max - mid) * primary, where primary is the
// slider of the largest channel (reds, greens, blues) and secondary the colour of the two largest (yellows,
// cyans, magentas). White stays white, greys stay as they are.
void adjBlackWhite(Work& w, const float wt[6], bool tint, const float tintRGB[3]) {
    enum { RE, YE, GR, CY, BL, MA };
    for (size_t i = 0; i < w.px.size(); i += 4) {
        float* c = &w.px[i];
        int idx[3] = {0, 1, 2};
        std::sort(idx, idx + 3, [&](int a, int b) { return c[a] > c[b]; });
        const int hi = idx[0], mid = idx[1];
        const float mx = c[idx[0]], md = c[idx[1]], mn = c[idx[2]];
        const int primary = hi == 0 ? RE : (hi == 1 ? GR : BL);
        const int pair = (1 << hi) | (1 << mid);
        const int secondary = pair == 3 ? YE : (pair == 6 ? CY : MA);
        const float g = clamp01(mn + (md - mn) * wt[secondary] / 100.0f + (mx - md) * wt[primary] / 100.0f);
        if (tint) {
            // colour the grey, keeping its lightness
            float t[3] = {tintRGB[0], tintRGB[1], tintRGB[2]}, out[3];
            const float lt = 0.3f * t[0] + 0.59f * t[1] + 0.11f * t[2];
            for (int k = 0; k < 3; ++k) out[k] = clamp01(g + (t[k] - lt) * (1.0f - std::fabs(2 * g - 1)));
            c[0] = out[0]; c[1] = out[1]; c[2] = out[2];
        } else c[0] = c[1] = c[2] = g;
    }
}

void adjCurves(Work& w, const uint8_t lut[4][256]) {
    for (size_t i = 0; i < w.px.size(); i += 4)
        for (int c = 0; c < 3; ++c) {
            const int v = (int)std::lround(clamp01(w.px[i + c]) * 255.0f);
            w.px[i + c] = lut[0][lut[1 + c][v]] / 255.0f;
        }
}

void adjLevels(Work& w, float ib, float iw, float gamma, float ob, float ow) {
    const float span = std::max(1e-3f, iw - ib);
    for (size_t i = 0; i < w.px.size(); i += 4)
        for (int c = 0; c < 3; ++c) {
            float v = clamp01((w.px[i + c] - ib) / span);
            v = std::pow(v, 1.0f / std::max(0.01f, gamma));
            w.px[i + c] = ob + v * (ow - ob);
        }
}

void adjInvert(Work& w) { for (size_t i = 0; i < w.px.size(); i += 4) for (int c = 0; c < 3; ++c) w.px[i + c] = 1.0f - w.px[i + c]; }
void adjDesaturate(Work& w) {
    for (size_t i = 0; i < w.px.size(); i += 4) {
        float* c = &w.px[i];
        const float l = (std::max({c[0], c[1], c[2]}) + std::min({c[0], c[1], c[2]})) * 0.5f;
        c[0] = c[1] = c[2] = l;
    }
}
void adjThreshold(Work& w, float level) {
    for (size_t i = 0; i < w.px.size(); i += 4) {
        float* c = &w.px[i];
        const float v = (0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]) >= level ? 1.0f : 0.0f;
        c[0] = c[1] = c[2] = v;
    }
}

// monotone cubic (Fritsch-Carlson): no overshoot between points, like Photoshop's curves
void curveLUT(const std::vector<float>& xs, const std::vector<float>& ys, uint8_t out[256]) {
    const size_t n = xs.size();
    if (n < 2) { for (int i = 0; i < 256; ++i) out[i] = (uint8_t)i; return; }
    std::vector<float> d(n - 1), m(n);
    for (size_t i = 0; i + 1 < n; ++i) d[i] = (ys[i + 1] - ys[i]) / std::max(1e-6f, xs[i + 1] - xs[i]);
    m[0] = d[0]; m[n - 1] = d[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) m[i] = (d[i - 1] * d[i] <= 0) ? 0.0f : (d[i - 1] + d[i]) * 0.5f;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (d[i] == 0) { m[i] = m[i + 1] = 0; continue; }
        const float a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b;
        if (s > 9) { const float t = 3 / std::sqrt(s); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
    }
    for (int k = 0; k < 256; ++k) {
        const float x = k / 255.0f;
        float y;
        if (x <= xs[0]) y = ys[0];
        else if (x >= xs[n - 1]) y = ys[n - 1];
        else {
            size_t i = 0;
            while (i + 1 < n && xs[i + 1] < x) ++i;
            const float h = xs[i + 1] - xs[i], t = (x - xs[i]) / h, t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1];
        }
        out[k] = to8(y);
    }
}

void histogram(const Doc& d, int channel, unsigned out[256]) {
    memset(out, 0, sizeof(unsigned) * 256);
    const Layer& l = d.layers[d.active];
    const bool mask = d.editMask && l.hasMask;
    for (size_t i = 0; i < (size_t)l.img.w * l.img.h; ++i) {
        if (mask) { out[l.mask[i]]++; continue; }
        const uint8_t* p = &l.img.px[i * 4];
        if (!p[3]) continue;
        const int v = channel == 0 ? (int)std::lround(0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2]) : p[channel - 1];
        out[v]++;
    }
}

// ---------- filters ----------
// blurs run on premultiplied colour, so transparent edges don't pull in dark fringes
static void premul(Work& w, bool on) {
    for (size_t i = 0; i < w.px.size(); i += 4) {
        const float a = w.px[i + 3];
        if (on) { for (int c = 0; c < 3; ++c) w.px[i + c] *= a; }
        else if (a > 1e-6f) { for (int c = 0; c < 3; ++c) w.px[i + c] = clamp01(w.px[i + c] / a); }
    }
}
// one running-sum box pass, edges clamped. Horizontal: along each row. Vertical: down every column at once,
// a row at a time, so memory is read in order.
static void boxPass(Work& w, int r, bool horizontal) {
    if (r <= 0) return;
    const float inv = 1.0f / (2 * r + 1);
    if (horizontal) {
        const int len = w.w;
        std::vector<float> line((size_t)len * 4);
        for (int y = 0; y < w.h; ++y) {
            float* row = w.at(0, y);
            memcpy(line.data(), row, (size_t)len * 16);
            float acc[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; ++i) { const int k = std::clamp(i, 0, len - 1); for (int c = 0; c < 4; ++c) acc[c] += line[(size_t)k * 4 + c]; }
            for (int i = 0; i < len; ++i) {
                for (int c = 0; c < 4; ++c) row[(size_t)i * 4 + c] = acc[c] * inv;
                const int add = std::min(len - 1, i + r + 1), sub = std::max(0, i - r);
                for (int c = 0; c < 4; ++c) acc[c] += line[(size_t)add * 4 + c] - line[(size_t)sub * 4 + c];
            }
        }
        return;
    }
    const int W4 = w.w * 4, H = w.h;
    std::vector<float> src(w.px);              // the column values before this pass
    std::vector<float> acc((size_t)W4, 0.0f);
    auto row = [&](int y) { return &src[(size_t)std::clamp(y, 0, H - 1) * W4]; };
    for (int i = -r; i <= r; ++i) { const float* q = row(i); for (int k = 0; k < W4; ++k) acc[k] += q[k]; }
    for (int y = 0; y < H; ++y) {
        float* o = w.at(0, y);
        for (int k = 0; k < W4; ++k) o[k] = acc[k] * inv;
        const float* add = row(y + r + 1);
        const float* sub = row(y - r);
        for (int k = 0; k < W4; ++k) acc[k] += add[k] - sub[k];
    }
}

void filterBoxBlur(Work& w, int radius) {
    premul(w, true);
    boxPass(w, radius, true); boxPass(w, radius, false);
    premul(w, false);
}

// Gaussian: three box passes whose widths match the gaussian's spread (radius = its standard deviation)
void filterGaussianBlur(Work& w, float sigma) {
    if (sigma <= 0.0f) return;
    const int n = 3;
    const float wIdeal = std::sqrt(12 * sigma * sigma / n + 1);
    int wl = (int)std::floor(wIdeal); if (wl % 2 == 0) wl--;
    const int wu = wl + 2;
    const float mIdeal = (12 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4 * wl - 4);
    const int m = (int)std::lround(mIdeal);
    premul(w, true);
    for (int i = 0; i < n; ++i) {
        const int r = ((i < m ? wl : wu) - 1) / 2;
        boxPass(w, r, true); boxPass(w, r, false);
    }
    premul(w, false);
}

void filterAverage(Doc& d, Work& w) {
    double s[4] = {0, 0, 0, 0}, wt = 0;
    for (int y = 0; y < w.h; ++y)
        for (int x = 0; x < w.w; ++x) {
            const int cx = x + w.ox, cy = y + w.oy;
            if (cx < 0 || cy < 0 || cx >= d.w || cy >= d.h) continue;
            const float t = d.selAt(cx, cy) / 255.0f;
            const float* p = w.at(x, y);
            const float k = t * p[3];
            for (int c = 0; c < 3; ++c) s[c] += p[c] * k;
            s[3] += p[3] * t; wt += t;
        }
    const double a = s[3];
    if (wt <= 0) return;
    float avg[4];
    for (int c = 0; c < 3; ++c) avg[c] = a > 0 ? (float)(s[c] / a) : 0.0f;
    avg[3] = (float)(s[3] / wt);
    for (size_t i = 0; i < w.px.size(); i += 4) memcpy(&w.px[i], avg, 16);
}

// a quick generator (xorshift): noise is millions of numbers
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    float uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / 8388608.0f - 1.0f; }   // -1..1
    float gauss() { return (uni() + uni() + uni() + uni()) * 0.5f; }                                        // about N(0, 0.58)
};

void filterAddNoise(Work& w, float amount, bool gaussian, bool mono, unsigned seed) {
    Rng rng(seed);
    const float k = amount / 100.0f * 0.5f;
    for (size_t i = 0; i < w.px.size(); i += 4) {
        if (w.px[i + 3] <= 0.0f && !w.mask) continue;
        if (mono || w.mask) { const float n = (gaussian ? rng.gauss() : rng.uni()) * k; for (int c = 0; c < 3; ++c) w.px[i + c] = clamp01(w.px[i + c] + n); }
        else for (int c = 0; c < 3; ++c) w.px[i + c] = clamp01(w.px[i + c] + (gaussian ? rng.gauss() : rng.uni()) * k);
    }
}

// ---------- fills ----------
void fillSelection(Doc& d, const uint8_t rgba[4], float opacity, int blend) {
    Layer& l = d.cur();
    if (!d.editMask) expandToCanvas(d, l);
    Work o = getWork(d), r = o;
    const float s[4] = {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f * opacity};
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            float* p = r.at(x, y);
            if (r.mask) { const float g = 0.3f * s[0] + 0.59f * s[1] + 0.11f * s[2]; p[0] = p[1] = p[2] = p[0] + (g - p[0]) * s[3]; }
            else compositePixel(blend, p, s, x + r.ox, y + r.oy);
        }
    putWork(d, o, r);
}

static int colourDist(const uint8_t* a, const uint8_t* b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2]), std::abs(a[3] - b[3])});
}

// which canvas pixels match the colour at (x, y): the flood (or every match) as a canvas-sized 0/255 map
static std::vector<uint8_t> regionAt(const Doc& d, int x, int y, int tol, bool contiguous, bool sampleAll) {
    std::vector<uint8_t> out((size_t)d.w * d.h, 0);
    if (x < 0 || y < 0 || x >= d.w || y >= d.h) return out;
    Image src;
    if (sampleAll) composite(d, src, {0, 0, d.w, d.h}, false);
    else {
        // the active layer (or its mask) on canvas coordinates
        const Layer& l = d.layers[d.active];
        src = Image(d.w, d.h);
        const bool mask = d.editMask && l.hasMask;
        for (int cy = 0; cy < d.h; ++cy)
            for (int cx = 0; cx < d.w; ++cx) {
                const int lx = cx - l.x, ly = cy - l.y;
                if (lx < 0 || ly < 0 || lx >= l.img.w || ly >= l.img.h) continue;
                uint8_t* o = src.at(cx, cy);
                if (mask) { o[0] = o[1] = o[2] = l.mask[(size_t)ly * l.img.w + lx]; o[3] = 255; }
                else memcpy(o, l.img.at(lx, ly), 4);
            }
    }
    const uint8_t seed[4] = {src.at(x, y)[0], src.at(x, y)[1], src.at(x, y)[2], src.at(x, y)[3]};
    if (!contiguous) {
        for (int i = 0; i < d.w * d.h; ++i) if (colourDist(&src.px[(size_t)i * 4], seed) <= tol) out[i] = 255;
        return out;
    }
    std::vector<int> stack{y * d.w + x};
    out[(size_t)y * d.w + x] = 255;
    while (!stack.empty()) {
        const int i = stack.back(); stack.pop_back();
        const int px = i % d.w, py = i / d.w;
        const int nb[4][2] = {{px - 1, py}, {px + 1, py}, {px, py - 1}, {px, py + 1}};
        for (auto& n : nb) {
            if (n[0] < 0 || n[1] < 0 || n[0] >= d.w || n[1] >= d.h) continue;
            const int j = n[1] * d.w + n[0];
            if (out[j] || colourDist(&src.px[(size_t)j * 4], seed) > tol) continue;
            out[j] = 255;
            stack.push_back(j);
        }
    }
    return out;
}

void bucketFill(Doc& d, int x, int y, const uint8_t rgba[4], int tol, bool contiguous, bool sampleAll, float opacity) {
    std::vector<uint8_t> region = regionAt(d, x, y, tol, contiguous, sampleAll);
    // fill only where the region and the selection meet
    std::vector<uint8_t> saved = d.sel;
    if (d.hasSel()) for (size_t i = 0; i < region.size(); ++i) region[i] = (uint8_t)(region[i] * d.sel[i] / 255);
    d.sel = std::move(region);
    fillSelection(d, rgba, opacity, BM_Normal);
    d.sel = std::move(saved);
}

// ---------- selections ----------
std::vector<uint8_t> shapeRect(const Doc& d, int x0, int y0, int x1, int y1) {
    std::vector<uint8_t> m((size_t)d.w * d.h, 0);
    Rect r = Rect{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)}.clip(d.w, d.h);
    for (int y = r.y0; y < r.y1; ++y) memset(&m[(size_t)y * d.w + r.x0], 255, r.x1 - r.x0);
    return m;
}

std::vector<uint8_t> shapeEllipse(const Doc& d, int x0, int y0, int x1, int y1) {
    std::vector<uint8_t> m((size_t)d.w * d.h, 0);
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, rx = std::fabs(x1 - x0) * 0.5f, ry = std::fabs(y1 - y0) * 0.5f;
    if (rx < 0.5f || ry < 0.5f) return m;
    Rect r = Rect{(int)(cx - rx) - 1, (int)(cy - ry) - 1, (int)(cx + rx) + 2, (int)(cy + ry) + 2}.clip(d.w, d.h);
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x) {
            // 4x4 samples per pixel: smooth edges
            int in = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const float dx = (x + (sx + 0.5f) / 4 - cx) / rx, dy = (y + (sy + 0.5f) / 4 - cy) / ry;
                    if (dx * dx + dy * dy <= 1) in++;
                }
            m[(size_t)y * d.w + x] = (uint8_t)(in * 255 / 16);
        }
    return m;
}

std::vector<uint8_t> shapePolygon(const Doc& d, const std::vector<float>& pts) {
    std::vector<uint8_t> m((size_t)d.w * d.h, 0);
    const size_t n = pts.size() / 2;
    if (n < 3) return m;
    float minY = 1e9f, maxY = -1e9f;
    for (size_t i = 0; i < n; ++i) { minY = std::min(minY, pts[i * 2 + 1]); maxY = std::max(maxY, pts[i * 2 + 1]); }
    const int y0 = std::max(0, (int)std::floor(minY)), y1 = std::min(d.h, (int)std::ceil(maxY) + 1);
    const int SS = 4;                            // 4 scanlines per row, 4 samples across: anti-aliased edges
    std::vector<int> cover((size_t)d.w * SS);
    std::vector<float> xs;
    for (int y = y0; y < y1; ++y) {
        std::fill(cover.begin(), cover.end(), 0);
        for (int s = 0; s < SS; ++s) {
            const float sy = y + (s + 0.5f) / SS;
            xs.clear();
            for (size_t i = 0; i < n; ++i) {
                const float ax = pts[i * 2], ay = pts[i * 2 + 1], bx = pts[((i + 1) % n) * 2], by = pts[((i + 1) % n) * 2 + 1];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs.push_back(ax + (sy - ay) / (by - ay) * (bx - ax));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                const int a = std::max(0, (int)std::lround(xs[k] * SS)), b = std::min(d.w * SS, (int)std::lround(xs[k + 1] * SS));
                for (int sx = a; sx < b; ++sx) cover[sx]++;
            }
        }
        for (int x = 0; x < d.w; ++x) {
            int c = 0;
            for (int k = 0; k < SS; ++k) c += cover[(size_t)x * SS + k];
            if (c) m[(size_t)y * d.w + x] = (uint8_t)std::min(255, c * 255 / (SS * SS));
        }
    }
    return m;
}

std::vector<uint8_t> shapeWand(const Doc& d, int x, int y, int tol, bool contiguous, bool sampleAll) {
    return regionAt(d, x, y, tol, contiguous, sampleAll);
}

void combineSel(Doc& d, std::vector<uint8_t> shape, int mode) {
    if (mode == SEL_NEW || !d.hasSel()) {
        if (mode == SEL_SUB || mode == SEL_AND) { if (!d.hasSel()) { if (mode == SEL_AND) d.sel = std::move(shape); return; } }
        else { d.sel = std::move(shape); return; }
    }
    for (size_t i = 0; i < d.sel.size(); ++i) {
        const int a = d.sel[i], b = shape[i];
        d.sel[i] = (uint8_t)(mode == SEL_ADD ? std::max(a, b) : mode == SEL_SUB ? a * (255 - b) / 255 : a * b / 255);
    }
}

void featherSel(Doc& d, float radius) {
    if (!d.hasSel() || radius <= 0) return;
    Work w; w.w = d.w; w.h = d.h; w.px.resize((size_t)d.w * d.h * 4);
    for (size_t i = 0; i < d.sel.size(); ++i) { const float v = d.sel[i] / 255.0f; w.px[i * 4] = w.px[i * 4 + 1] = w.px[i * 4 + 2] = v; w.px[i * 4 + 3] = 1; }
    filterGaussianBlur(w, radius);
    for (size_t i = 0; i < d.sel.size(); ++i) d.sel[i] = to8(w.px[i * 4]);
}

void invertSel(Doc& d) {
    if (!d.hasSel()) { d.sel.assign((size_t)d.w * d.h, 0); return; }
    for (auto& v : d.sel) v = 255 - v;
}

void growSel(Doc& d, int px) {
    if (!d.hasSel() || px == 0) return;
    const bool grow = px > 0;
    const int r = std::abs(px);
    std::vector<uint8_t> src = d.sel;
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            int v = src[(size_t)y * d.w + x];
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dy * dy > r * r) continue;
                    const int nx = std::clamp(x + dx, 0, d.w - 1), ny = std::clamp(y + dy, 0, d.h - 1);
                    const int s = src[(size_t)ny * d.w + nx];
                    v = grow ? std::max(v, s) : std::min(v, s);
                }
            d.sel[(size_t)y * d.w + x] = (uint8_t)v;
        }
}

std::vector<int> selOutline(const Doc& d) {
    std::vector<int> segs;
    if (!d.hasSel()) return segs;
    auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < d.w && y < d.h && d.sel[(size_t)y * d.w + x] >= 128; };
    for (int y = 0; y <= d.h; ++y)
        for (int x = 0; x < d.w; ++x)
            if (in(x, y) != in(x, y - 1)) { segs.insert(segs.end(), {x, y, x + 1, y}); }
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x <= d.w; ++x)
            if (in(x, y) != in(x - 1, y)) { segs.insert(segs.end(), {x, y, x, y + 1}); }
    return segs;
}

// ---------- size ----------
static float cubic(float t) {                    // Catmull-Rom-ish (a = -0.5)
    t = std::fabs(t);
    if (t < 1) return 1.5f * t * t * t - 2.5f * t * t + 1;
    if (t < 2) return -0.5f * t * t * t + 2.5f * t * t - 4 * t + 2;
    return 0;
}
// the taps of a cubic filter for every output position along one axis
struct Taps { std::vector<int> start, count; std::vector<float> wt; int maxN = 0; };
static Taps makeTaps(int src, int dst) {
    Taps t;
    const float f = (float)src / dst, sc = std::max(1.0f, f);   // widen the filter when shrinking
    const int r = (int)std::ceil(2 * sc);
    t.maxN = 2 * r + 1;
    t.start.resize(dst); t.count.resize(dst); t.wt.assign((size_t)dst * t.maxN, 0.0f);
    for (int i = 0; i < dst; ++i) {
        const float c = (i + 0.5f) * f - 0.5f;
        const int k0 = (int)std::floor(c) - r + 1;
        float sum = 0;
        int n = 0;
        for (int k = k0; k <= (int)std::floor(c) + r; ++k, ++n) { const float wv = cubic((k - c) / sc); t.wt[(size_t)i * t.maxN + n] = wv; sum += wv; }
        for (int j = 0; j < n; ++j) t.wt[(size_t)i * t.maxN + j] /= (sum != 0 ? sum : 1);
        t.start[i] = k0; t.count[i] = n;
    }
    return t;
}

// resample n-channel 8-bit data (premultiplied when there's alpha, so edges don't darken)
static std::vector<uint8_t> resample(const uint8_t* src, int sw, int sh, int ch, int dw, int dh) {
    std::vector<float> in((size_t)sw * sh * ch);
    for (size_t i = 0; i < (size_t)sw * sh; ++i)
        for (int c = 0; c < ch; ++c) in[i * ch + c] = (ch == 4 && c < 3) ? src[i * 4 + c] * src[i * 4 + 3] / 255.0f : src[i * ch + c];
    const Taps tx = makeTaps(sw, dw), ty = makeTaps(sh, dh);
    std::vector<float> tmp((size_t)dw * sh * ch);
    for (int y = 0; y < sh; ++y) {
        const float* row = &in[(size_t)y * sw * ch];
        float* o = &tmp[(size_t)y * dw * ch];
        for (int x = 0; x < dw; ++x) {
            float v[4] = {0, 0, 0, 0};
            const float* wt = &tx.wt[(size_t)x * tx.maxN];
            for (int j = 0; j < tx.count[x]; ++j) {
                const int k = std::clamp(tx.start[x] + j, 0, sw - 1);
                for (int c = 0; c < ch; ++c) v[c] += row[(size_t)k * ch + c] * wt[j];
            }
            for (int c = 0; c < ch; ++c) o[(size_t)x * ch + c] = v[c];
        }
    }
    std::vector<uint8_t> out((size_t)dw * dh * ch);
    std::vector<float> acc((size_t)dw * ch);
    for (int y = 0; y < dh; ++y) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        const float* wt = &ty.wt[(size_t)y * ty.maxN];
        for (int j = 0; j < ty.count[y]; ++j) {
            const float* row = &tmp[(size_t)std::clamp(ty.start[y] + j, 0, sh - 1) * dw * ch];
            const float wv = wt[j];
            for (size_t k = 0; k < acc.size(); ++k) acc[k] += row[k] * wv;
        }
        uint8_t* o = &out[(size_t)y * dw * ch];
        for (int x = 0; x < dw; ++x) {
            const float* v = &acc[(size_t)x * ch];
            const float a = ch == 4 ? std::clamp(v[3], 0.0f, 255.0f) : 0.0f;
            for (int c = 0; c < ch; ++c) {
                float f = v[c];
                if (ch == 4 && c < 3) f = a > 0.5f ? f * 255.0f / a : 0.0f;
                o[(size_t)x * ch + c] = (uint8_t)fround(std::clamp(f, 0.0f, 255.0f));
            }
        }
    }
    return out;
}

void resizeImage(Doc& d, int w, int h) {
    if (w < 1 || h < 1) return;
    const float kx = (float)w / d.w, ky = (float)h / d.h;
    for (Layer& l : d.layers) {
        const int nw = std::max(1, (int)std::lround(l.img.w * kx)), nh = std::max(1, (int)std::lround(l.img.h * ky));
        Image n(nw, nh);
        n.px = resample(l.img.px.data(), l.img.w, l.img.h, 4, nw, nh);
        if (l.hasMask) l.mask = resample(l.mask.data(), l.img.w, l.img.h, 1, nw, nh);
        l.img = std::move(n);
        l.x = (int)std::lround(l.x * kx); l.y = (int)std::lround(l.y * ky);
    }
    if (d.hasSel()) d.sel = resample(d.sel.data(), d.w, d.h, 1, w, h);
    d.w = w; d.h = h;
    d.dirty = true;
}

void resizeCanvas(Doc& d, int w, int h, int ax, int ay) {
    if (w < 1 || h < 1) return;
    const int dx = ax == 0 ? 0 : (ax == 1 ? (w - d.w) / 2 : w - d.w);
    const int dy = ay == 0 ? 0 : (ay == 1 ? (h - d.h) / 2 : h - d.h);
    for (Layer& l : d.layers) { l.x += dx; l.y += dy; }
    d.sel.clear();
    d.w = w; d.h = h;
    d.dirty = true;
}

void transformLayer(Doc& d, float sx, float sy, float degrees) {
    Layer& l = d.cur();
    if (sx <= 0 || sy <= 0) return;
    const float a = degrees * 3.14159265f / 180.0f, ca = std::cos(a), sa = std::sin(a);
    const float cx = l.x + l.img.w * 0.5f, cy = l.y + l.img.h * 0.5f;          // the centre stays put
    // the new bounds: the four corners, scaled and turned
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int k = 0; k < 4; ++k) {
        const float px = ((k & 1) ? 0.5f : -0.5f) * l.img.w * sx, py = ((k & 2) ? 0.5f : -0.5f) * l.img.h * sy;
        const float rx = px * ca - py * sa, ry = px * sa + py * ca;
        minX = std::min(minX, rx); maxX = std::max(maxX, rx); minY = std::min(minY, ry); maxY = std::max(maxY, ry);
    }
    const int nx = (int)std::floor(cx + minX), ny = (int)std::floor(cy + minY);
    const int nw = std::max(1, (int)std::ceil(cx + maxX) - nx), nh = std::max(1, (int)std::ceil(cy + maxY) - ny);
    if ((size_t)nw * nh > 400000000ull) return;
    Image out(nw, nh);
    std::vector<uint8_t> mask(l.hasMask ? (size_t)nw * nh : 0, 0);
    // every new pixel looks back into the old layer (bilinear, premultiplied so edges stay clean)
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            const float dx = x + nx + 0.5f - cx, dy = y + ny + 0.5f - cy;
            const float ux = (dx * ca + dy * sa) / sx + l.img.w * 0.5f - 0.5f, uy = (-dx * sa + dy * ca) / sy + l.img.h * 0.5f - 0.5f;
            if (ux < -1 || uy < -1 || ux > l.img.w || uy > l.img.h) continue;
            const int x0 = (int)std::floor(ux), y0 = (int)std::floor(uy);
            const float fx = ux - x0, fy = uy - y0;
            float acc[4] = {0, 0, 0, 0}, m = 0;
            for (int k = 0; k < 4; ++k) {
                const int sxp = x0 + (k & 1), syp = y0 + (k >> 1);
                const float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                if (sxp < 0 || syp < 0 || sxp >= l.img.w || syp >= l.img.h) continue;
                const uint8_t* p = l.img.at(sxp, syp);
                const float al = p[3] / 255.0f;
                for (int c = 0; c < 3; ++c) acc[c] += p[c] / 255.0f * al * wgt;
                acc[3] += al * wgt;
                if (l.hasMask) m += l.mask[(size_t)syp * l.img.w + sxp] / 255.0f * wgt;
            }
            uint8_t* o = out.at(x, y);
            if (acc[3] > 1e-5f) for (int c = 0; c < 3; ++c) o[c] = to8(acc[c] / acc[3]);
            o[3] = to8(acc[3]);
            if (l.hasMask) mask[(size_t)y * nw + x] = to8(m);
        }
    l.img = std::move(out);
    l.mask = std::move(mask);
    l.x = nx; l.y = ny;
    d.dirty = true;
}

void flipLayer(Doc& d, bool horizontal) {
    Layer& l = d.cur();
    Image n(l.img.w, l.img.h);
    std::vector<uint8_t> m = l.mask;
    for (int y = 0; y < l.img.h; ++y)
        for (int x = 0; x < l.img.w; ++x) {
            const int sx = horizontal ? l.img.w - 1 - x : x, sy = horizontal ? y : l.img.h - 1 - y;
            memcpy(n.at(x, y), l.img.at(sx, sy), 4);
            if (l.hasMask) m[(size_t)y * l.img.w + x] = l.mask[(size_t)sy * l.img.w + sx];
        }
    l.img = std::move(n); l.mask = std::move(m);
    d.dirty = true;
}

// ---------- the brush ----------
void strokeBegin(Doc& d, Stroke& s) {
    if (!d.editMask) expandToCanvas(d, d.cur());     // paint can go anywhere on the canvas
    s.orig = getWork(d);
    s.cover.assign((size_t)s.orig.w * s.orig.h, 0.0f);
    s.on = true;
}

// how much of one dab reaches distance t (0 centre .. 1 rim): solid out to `hardness`, then a smooth fall
static float dabFalloff(float t, float hardness) {
    if (t >= 1.0f) return 0.0f;
    if (t <= hardness) return 1.0f;
    const float u = (t - hardness) / std::max(1e-3f, 1.0f - hardness);
    return 1.0f - u * u * (3 - 2 * u);
}

Rect strokeTo(Doc& d, Stroke& s, float x, float y, float size, float hardness, float opacity, const uint8_t rgb[3], bool erase, bool first) {
    Layer& l = d.cur();
    Work& o = s.orig;
    const float r = std::max(0.5f, size * 0.5f);
    const float spacing = std::max(0.5f, r * 0.15f);
    std::vector<float> dabs;
    if (first) { dabs = {x, y}; s.rest = 0; }
    else {
        const float dx = x - s.lastX, dy = y - s.lastY, len = std::sqrt(dx * dx + dy * dy);
        float t = spacing - s.rest;
        while (t <= len) { dabs.push_back(s.lastX + dx * t / len); dabs.push_back(s.lastY + dy * t / len); t += spacing; }
        s.rest = len - (t - spacing);
    }
    s.lastX = x; s.lastY = y;
    Rect changed{0, 0, 0, 0};
    const float col[3] = {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f};
    const float grey = 0.3f * col[0] + 0.59f * col[1] + 0.11f * col[2];
    for (size_t k = 0; k + 1 < dabs.size(); k += 2) {
        const float cx = dabs[k] - o.ox, cy = dabs[k + 1] - o.oy;    // in target coordinates
        const int x0 = std::max(0, (int)std::floor(cx - r)), y0 = std::max(0, (int)std::floor(cy - r));
        const int x1 = std::min(o.w, (int)std::ceil(cx + r) + 1), y1 = std::min(o.h, (int)std::ceil(cy + r) + 1);
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) {
                const float ddx = px + 0.5f - cx, ddy = py + 0.5f - cy;
                const float a = dabFalloff(std::sqrt(ddx * ddx + ddy * ddy) / r, hardness);
                if (a <= 0) continue;
                float& c = s.cover[(size_t)py * o.w + px];
                if (a <= c) continue;
                c = a;
                // the result from the original and this stroke's coverage, never past `opacity`
                const int canvasX = px + o.ox, canvasY = py + o.oy;
                float t = c * opacity;
                if (d.hasSel()) t *= (canvasX >= 0 && canvasY >= 0 && canvasX < d.w && canvasY < d.h) ? d.sel[(size_t)canvasY * d.w + canvasX] / 255.0f : 0.0f;
                const float* src = o.at(px, py);
                const size_t i = (size_t)py * o.w + px;
                if (o.mask) {
                    // on a mask the brush paints grey: black hides, white reveals (Eraser paints white back)
                    const float target = erase ? 1.0f : grey;
                    l.mask[i] = to8(src[0] + (target - src[0]) * t);
                } else if (erase) {
                    l.img.px[i * 4 + 3] = to8(src[3] * (1 - t));
                } else {
                    float b[4] = {src[0], src[1], src[2], src[3]};
                    const float sp[4] = {col[0], col[1], col[2], t};
                    compositePixel(BM_Normal, b, sp, canvasX, canvasY);
                    for (int ch = 0; ch < 4; ++ch) l.img.px[i * 4 + ch] = to8(b[ch]);
                }
            }
        changed = changed.unite(Rect{x0 + o.ox, y0 + o.oy, x1 + o.ox, y1 + o.oy});
    }
    d.dirty = true;
    return changed;
}

void strokeEnd(Stroke& s) { s.on = false; s.orig.px.clear(); s.cover.clear(); }

Rect pencilTo(Doc& d, Stroke& s, float x, float y, float size, float opacity, const uint8_t rgb[3], bool erase, bool first) {
    // hardness 1 and every pixel inside the circle fully: the brush's maths with an aliased dab
    return strokeTo(d, s, x, y, std::max(1.0f, std::floor(size)), 1.0f, opacity, rgb, erase, first);
}

// ---------- the retouching brushes ----------
// the dabs between the last point and this one, at the brush's spacing (as strokeTo)
static std::vector<float> dabsTo(Stroke& s, float x, float y, float r, bool first) {
    const float spacing = std::max(0.5f, r * 0.15f);
    std::vector<float> dabs;
    if (first) { dabs = {x, y}; s.rest = 0; }
    else {
        const float dx = x - s.lastX, dy = y - s.lastY, len = std::sqrt(dx * dx + dy * dy);
        float t = spacing - s.rest;
        while (t <= len) { dabs.push_back(s.lastX + dx * t / len); dabs.push_back(s.lastY + dy * t / len); t += spacing; }
        s.rest = len - (t - spacing);
    }
    return dabs;
}
static float lumOf(const float* p) { return 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]; }
Rect retouchTo(Doc& d, Stroke& s, float x, float y, float size, float hardness, const RetouchOpts& op, bool first) {
    Layer& l = d.cur();
    Work& o = s.orig;                            // the layer as the stroke began (Clone Stamp copies from it)
    if (o.mask) return {0, 0, 0, 0};             // retouching is for pixels, not masks
    const float r = std::max(0.5f, size * 0.5f);
    const float px0 = s.lastX, py0 = s.lastY;
    std::vector<float> dabs = dabsTo(s, x, y, r, first);
    s.lastX = x; s.lastY = y;
    Rect changed{0, 0, 0, 0};
    static float sampled[3] = {0, 0, 0};          // Background Eraser: the colour under the first dab
    if (first && op.mode == RT_BG_ERASE) {
        const int sx = (int)x - o.ox, sy = (int)y - o.oy;
        if (sx >= 0 && sy >= 0 && sx < o.w && sy < o.h) { const float* p = o.at(sx, sy); sampled[0] = p[0]; sampled[1] = p[1]; sampled[2] = p[2]; }
    }
    float prevX = first ? x : px0, prevY = first ? y : py0;
    auto get = [&](int px, int py, float out[4]) {   // the layer as it is now (straight RGBA 0..1)
        px = std::clamp(px, 0, o.w - 1); py = std::clamp(py, 0, o.h - 1);
        const uint8_t* q = &l.img.px[((size_t)py * o.w + px) * 4];
        for (int c = 0; c < 4; ++c) out[c] = q[c] / 255.0f;
    };
    for (size_t k = 0; k + 1 < dabs.size(); k += 2) {
        const float cx = dabs[k] - o.ox, cy = dabs[k + 1] - o.oy;
        const float mdx = dabs[k] - prevX, mdy = dabs[k + 1] - prevY;   // Smudge: how far the brush moved since the last dab
        prevX = dabs[k]; prevY = dabs[k + 1];
        const int x0 = std::max(0, (int)std::floor(cx - r)), y0 = std::max(0, (int)std::floor(cy - r));
        const int x1 = std::min(o.w, (int)std::ceil(cx + r) + 1), y1 = std::min(o.h, (int)std::ceil(cy + r) + 1);
        if (x1 <= x0 || y1 <= y0) continue;
        // this dab's result from the pixels as they are now, written after (so a dab doesn't read its own output)
        std::vector<float> out((size_t)(x1 - x0) * (y1 - y0) * 4);
        std::vector<float> wgt((size_t)(x1 - x0) * (y1 - y0), 0.0f);
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) {
                const float ddx = px + 0.5f - cx, ddy = py + 0.5f - cy;
                float a = dabFalloff(std::sqrt(ddx * ddx + ddy * ddy) / r, hardness);
                const int canvasX = px + o.ox, canvasY = py + o.oy;
                if (d.hasSel()) a *= (canvasX >= 0 && canvasY >= 0 && canvasX < d.w && canvasY < d.h) ? d.sel[(size_t)canvasY * d.w + canvasX] / 255.0f : 0.0f;
                const size_t j = (size_t)(py - y0) * (x1 - x0) + (px - x0);
                if (a <= 0) continue;
                float p[4]; get(px, py, p);
                float* q = &out[j * 4];
                for (int c = 0; c < 4; ++c) q[c] = p[c];
                const float st = op.strength;
                switch (op.mode) {
                case RT_BLUR: case RT_SHARPEN: {   // a 3x3 neighbourhood (premultiplied), towards it or away from it
                    float n[4] = {0, 0, 0, 0}, w = 0;
                    for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                        float s4[4]; get(px + ox, py + oy, s4);
                        const float kw = (ox == 0 && oy == 0) ? 4.0f : (ox == 0 || oy == 0) ? 2.0f : 1.0f;
                        for (int c = 0; c < 3; ++c) n[c] += s4[c] * s4[3] * kw;
                        n[3] += s4[3] * kw; w += kw;
                    }
                    const float na = n[3] / w;
                    const float t = a * st * (op.mode == RT_BLUR ? 1.0f : 0.5f);
                    for (int c = 0; c < 3; ++c) {
                        const float nc = n[3] > 1e-6f ? n[c] / n[3] : p[c];
                        q[c] = op.mode == RT_BLUR ? p[c] + (nc - p[c]) * t : std::clamp(p[c] + (p[c] - nc) * t, 0.0f, 1.0f);
                    }
                    if (op.mode == RT_BLUR) q[3] = p[3] + (na - p[3]) * t;
                    break;
                }
                case RT_SMUDGE: {   // the colour the brush dragged here from where it was
                    float b[4]; get((int)std::floor(px + 0.5f - mdx), (int)std::floor(py + 0.5f - mdy), b);
                    const float t = a * st;
                    for (int c = 0; c < 4; ++c) q[c] = p[c] + (b[c] - p[c]) * t;
                    break;
                }
                case RT_DODGE: case RT_BURN: case RT_SPONGE_SAT: case RT_SPONGE_DESAT: {
                    // as the brush: one stroke never goes past its exposure (the stroke's coverage, from the pixels as
                    // it began), most in the chosen tonal range (Dodge / Burn)
                    float& cv = s.cover[(size_t)py * o.w + px];
                    if (a <= cv) { a = 0; break; }
                    cv = a;
                    const float* p0 = o.at(px, py);
                    const float L = lumOf(p0);
                    if (op.mode == RT_DODGE || op.mode == RT_BURN) {
                        const float rw = op.range == 0 ? (1 - L) * (1 - L) : op.range == 2 ? L * L : 1 - std::fabs(2 * L - 1) * 0.6f;
                        const float t = cv * st * rw * 0.6f;
                        for (int c = 0; c < 3; ++c) q[c] = op.mode == RT_DODGE ? p0[c] + (1 - p0[c]) * t : p0[c] * (1 - t);
                    } else {
                        const float t = cv * st, k2 = op.mode == RT_SPONGE_SAT ? 1 + t : 1 - t;
                        for (int c = 0; c < 3; ++c) q[c] = std::clamp(L + (p0[c] - L) * k2, 0.0f, 1.0f);
                    }
                    q[3] = p0[3];
                    a = 1;
                    break;
                }
                case RT_CLONE: {   // from the layer as the stroke began, at the source offset
                    const int sx = (int)std::floor(px + op.srcDX), sy = (int)std::floor(py + op.srcDY);
                    if (sx < 0 || sy < 0 || sx >= o.w || sy >= o.h) { a = 0; break; }
                    const float* src = o.at(sx, sy);
                    float b[4] = {p[0], p[1], p[2], p[3]};
                    const float sp[4] = {src[0], src[1], src[2], src[3] * a * st};
                    compositePixel(BM_Normal, b, sp, canvasX, canvasY);
                    for (int c = 0; c < 4; ++c) q[c] = b[c];
                    a = 1;
                    break;
                }
                case RT_BG_ERASE: {   // pixels near the sampled colour go transparent
                    const float dist = std::sqrt((p[0] - sampled[0]) * (p[0] - sampled[0]) + (p[1] - sampled[1]) * (p[1] - sampled[1]) + (p[2] - sampled[2]) * (p[2] - sampled[2])) / 1.732f;
                    const float keep = std::clamp((dist - op.tolerance) / 0.08f + 0.5f, 0.0f, 1.0f);
                    q[3] = p[3] * (1 - a * (1 - keep));
                    break;
                }
                }
                wgt[j] = a;
            }
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) {
                const size_t j = (size_t)(py - y0) * (x1 - x0) + (px - x0);
                if (wgt[j] <= 0) continue;
                uint8_t* dst = &l.img.px[((size_t)py * o.w + px) * 4];
                for (int c = 0; c < 4; ++c) dst[c] = to8(out[j * 4 + c]);
            }
        changed = changed.unite(Rect{x0 + o.ox, y0 + o.oy, x1 + o.ox, y1 + o.oy});
    }
    d.dirty = true;
    return changed;
}

// ---------- the Gradient tool ----------
void gradientFill(Doc& d, float x0, float y0, float x1, float y1, const uint8_t from[3], const uint8_t to[3], int style, bool reverse, float opacity) {
    Work o = getWork(d), r = o;
    const float dx = x1 - x0, dy = y1 - y0, len2 = std::max(1e-6f, dx * dx + dy * dy), len = std::sqrt(len2);
    const float a = std::atan2(dy, dx);
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            const float px = x + r.ox + 0.5f - x0, py = y + r.oy + 0.5f - y0;
            float t;
            switch (style) {
            case GRAD_RADIAL: t = std::sqrt(px * px + py * py) / len; break;
            case GRAD_ANGLE: { float ang = std::atan2(py, px) - a; while (ang < 0) ang += 6.2831853f; while (ang >= 6.2831853f) ang -= 6.2831853f; t = ang / 6.2831853f; break; }
            case GRAD_REFLECTED: t = std::fabs((px * dx + py * dy) / len2); break;
            case GRAD_DIAMOND: { const float u = (px * dx + py * dy) / len, v = (-px * dy + py * dx) / len; t = (std::fabs(u) + std::fabs(v)) / len; break; }
            default: t = (px * dx + py * dy) / len2; break;
            }
            t = std::clamp(t, 0.0f, 1.0f);
            if (reverse) t = 1 - t;
            float* q = r.at(x, y);
            if (r.mask) { const float g = (from[0] + (to[0] - from[0]) * t) / 255.0f; q[0] = q[1] = q[2] = q[0] + (g - q[0]) * opacity; continue; }
            float b[4] = {q[0], q[1], q[2], q[3]};
            const float s[4] = {(from[0] + (to[0] - from[0]) * t) / 255.0f, (from[1] + (to[1] - from[1]) * t) / 255.0f, (from[2] + (to[2] - from[2]) * t) / 255.0f, opacity};
            compositePixel(BM_Normal, b, s, x + r.ox, y + r.oy);
            for (int c = 0; c < 4; ++c) q[c] = b[c];
        }
    putWork(d, o, r);
    d.dirty = true;
}

// ---------- the Crop tool ----------
void cropCanvas(Doc& d, Rect r, bool deleteCropped) {
    r = r.clip(d.w, d.h);
    if (r.empty()) return;
    for (Layer& l : d.layers) {
        l.x -= r.x0; l.y -= r.y0;
        if (deleteCropped && !l.isAdjust() && !l.isFill()) {   // cut what's now outside the canvas
            const int nx0 = std::max(0, -l.x), ny0 = std::max(0, -l.y);
            const int nx1 = std::min(l.img.w, r.x1 - r.x0 - l.x), ny1 = std::min(l.img.h, r.y1 - r.y0 - l.y);
            if (nx1 <= nx0 || ny1 <= ny0) { l.img = Image(1, 1); l.mask.assign(l.hasMask ? 1 : 0, 0); l.x = l.y = 0; continue; }
            Image n(nx1 - nx0, ny1 - ny0);
            std::vector<uint8_t> m(l.hasMask ? (size_t)n.w * n.h : 0);
            for (int y = 0; y < n.h; ++y) {
                memcpy(n.at(0, y), l.img.at(nx0, y + ny0), (size_t)n.w * 4);
                if (l.hasMask) memcpy(&m[(size_t)y * n.w], &l.mask[(size_t)(y + ny0) * l.img.w + nx0], n.w);
            }
            l.img = std::move(n); l.mask = std::move(m);
            l.x += nx0; l.y += ny0;
        }
    }
    d.w = r.x1 - r.x0; d.h = r.y1 - r.y0;
    d.sel.clear();
    for (Layer& l : d.layers) if (l.isFill()) renderFill(d, l);
    d.dirty = true;
}

// ---------- the Magic Eraser ----------
void magicErase(Doc& d, int x, int y, int tolerance, bool contiguous, bool sampleAll, float opacity) {
    const std::vector<uint8_t> pick = shapeWand(d, x, y, tolerance, contiguous, sampleAll);
    Layer& l = d.cur();
    if (l.isAdjust() || l.isFill()) return;
    expandToCanvas(d, l);
    for (int py = 0; py < l.img.h; ++py)
        for (int px = 0; px < l.img.w; ++px) {
            const int cx = px + l.x, cy = py + l.y;
            if (cx < 0 || cy < 0 || cx >= d.w || cy >= d.h) continue;
            const float t = pick[(size_t)cy * d.w + cx] / 255.0f * opacity * (d.hasSel() ? d.sel[(size_t)cy * d.w + cx] / 255.0f : 1.0f);
            if (t > 0) { uint8_t* p = l.img.at(px, py); p[3] = (uint8_t)(p[3] * (1 - t) + 0.5f); }
        }
    d.dirty = true;
}
