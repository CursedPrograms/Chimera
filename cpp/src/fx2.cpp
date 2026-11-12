// LayerTack - Motion and Radial Blur, Median, Clouds, Crystallize and Pointillize, ported from PhotoCraft
// (crates/algo/src/blur.rs, noise.rs, render.rs, pixelate.rs; MIT OR Apache-2.0). See fx.h.
#include "fx.h"
#include <algorithm>
#include <cmath>
#include <thread>

static const float PI = 3.14159265358979f;

// rows shared out over the CPU's cores
template <class F> static void rows(int h, const F& f) {
    const int n = (int)std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> t;
    for (int k = 0; k < n; ++k) t.emplace_back([k, n, h, &f] { for (int y = k; y < h; y += n) f(y); });
    for (auto& th : t) th.join();
}

static float hash01(int x, int y, unsigned c, unsigned seed) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ c * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return (float)(h >> 8) / (float)(1u << 24);
}

// premultiplied bilinear sample, edges repeated
static void sample(const Work& s, float x, float y, float acc[4]) {
    x -= 0.5f; y -= 0.5f;
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - x0, fy = y - y0;
    const int xs[2] = {std::clamp(x0, 0, s.w - 1), std::clamp(x0 + 1, 0, s.w - 1)}, ys[2] = {std::clamp(y0, 0, s.h - 1), std::clamp(y0 + 1, 0, s.h - 1)};
    const float wx[2] = {1 - fx, fx}, wy[2] = {1 - fy, fy};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const float* p = &s.px[((size_t)ys[j] * s.w + xs[i]) * 4];
            const float a = p[3] * wx[i] * wy[j];
            for (int c = 0; c < 3; ++c) acc[c] += p[c] * a;
            acc[3] += a;
        }
}

static void unpremul(const float acc[4], float total, float* o) {
    for (int c = 0; c < 3; ++c) o[c] = acc[3] > 1e-7f ? acc[c] / acc[3] : 0.0f;
    o[3] = acc[3] / total;
}

void fxMotionBlur(Work& w, float angle, float distance) {
    // the mean of samples along a line centred on the pixel
    const float d = std::fabs(distance);
    if (d < 0.5f) return;
    const float s = std::sin(angle * PI / 180), c = std::cos(angle * PI / 180);
    const int steps = (int)std::ceil(d);
    const Work src = w;
    rows(w.h, [&](int y) {
        for (int x = 0; x < w.w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int i = 0; i <= steps; ++i) { const float t = (float)i / steps - 0.5f; sample(src, x + 0.5f + c * d * t, y + 0.5f - s * d * t, acc); }
            unpremul(acc, (float)(steps + 1), w.at(x, y));
        }
    });
}

void fxRadialBlur(Work& w, FxBox b, float amount, bool zoom, int quality, float cxf, float cyf) {
    const float cx = b.x0 + (b.x1 - b.x0) * cxf, cy = b.y0 + (b.y1 - b.y0) * cyf;
    amount = std::clamp(amount, 0.0f, 100.0f);
    const int maxN = quality == 0 ? 64 : quality == 1 ? 256 : 4096;
    const Work src = w;
    rows(w.h, [&](int y) {
        for (int x = 0; x < w.w; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, r = std::sqrt(dx * dx + dy * dy);
            float acc[4] = {0, 0, 0, 0};
            int n;
            if (!zoom) {   // Spin: an arc of `amount` degrees centred on the pixel
                const float arc = amount * PI / 180;
                n = std::clamp((int)std::ceil(arc * r), 1, maxN);
                for (int i = 0; i <= n; ++i) {
                    const float t = ((float)i / n - 0.5f) * arc, s = std::sin(t), c = std::cos(t);
                    sample(src, cx + dx * c - dy * s, cy + dx * s + dy * c, acc);
                }
            } else {       // Zoom: along the ray, up to amount/2 % closer to the centre
                const float span = amount / 200;
                n = std::clamp((int)std::ceil(span * r), 1, maxN);
                for (int i = 0; i <= n; ++i) { const float k = 1 - span * i / n; sample(src, cx + dx * k, cy + dy * k, acc); }
            }
            unpremul(acc, (float)(n + 1), w.at(x, y));
        }
    });
}

void fxMedian(Work& w, int r) {
    // a disc of radius r (rows reaching dx^2 + dy^2 <= r^2 + r, as Photoshop's); 8-bit histograms slide along each row
    if (r <= 0) return;
    std::vector<int> hw(2 * r + 1);
    for (int dy = -r; dy <= r; ++dy) {
        const int lim = r * r + r - dy * dy;
        int v = (int)std::sqrt((double)lim);
        while (v * v > lim) --v;
        while ((v + 1) * (v + 1) <= lim) ++v;
        hw[dy + r] = v;
    }
    int count = 0; for (int v : hw) count += 2 * v + 1;
    const int mid = count / 2;
    std::vector<uint8_t> lv((size_t)w.w * w.h * 3);
    for (size_t i = 0; i < (size_t)w.w * w.h; ++i) for (int c = 0; c < 3; ++c) lv[i * 3 + c] = to8(w.px[i * 4 + c]);
    auto at = [&](int x, int y, int c) { return lv[((size_t)std::clamp(y, 0, w.h - 1) * w.w + std::clamp(x, 0, w.w - 1)) * 3 + c]; };
    rows(w.h, [&](int y) {
        for (int c = 0; c < 3; ++c) {
            int hist[256] = {};
            for (int dy = -r; dy <= r; ++dy) for (int dx = -hw[dy + r]; dx <= hw[dy + r]; ++dx) ++hist[at(dx, y + dy, c)];
            for (int x = 0; x < w.w; ++x) {
                int k = 0, sum = hist[0];
                while (sum <= mid) sum += hist[++k];
                w.at(x, y)[c] = k / 255.0f;
                if (x + 1 < w.w)
                    for (int dy = -r; dy <= r; ++dy) { --hist[at(x - hw[dy + r], y + dy, c)]; ++hist[at(x + 1 + hw[dy + r], y + dy, c)]; }
            }
        }
    });
}

static float cloudHash(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0x00ffffffu) / (float)0x00ffffff;
}

static float cloudsValue(float x, float y, float base, uint32_t seed) {
    float sum = 0, amp = 1, freq = 1 / base, norm = 0;
    for (uint32_t o = 0; o < 8; ++o) {
        const float fx = x * freq, fy = y * freq;
        const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
        const float tx = fx - ix, ty = fy - iy, sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
        const uint32_t s = seed + o * 7919;
        const float top = cloudHash(ix, iy, s) + (cloudHash(ix + 1, iy, s) - cloudHash(ix, iy, s)) * sx;
        const float bot = cloudHash(ix, iy + 1, s) + (cloudHash(ix + 1, iy + 1, s) - cloudHash(ix, iy + 1, s)) * sx;
        sum += (top + (bot - top) * sy) * amp; norm += amp; amp *= 0.5f; freq *= 2;
    }
    // an S-curve round mid-grey, so the clouds use the whole range between the two colours
    const float C = 2.2f, h = std::sqrt(C * C - 1), d = (sum / norm - 0.5f) * 2 * h;
    return std::clamp(0.5f + 0.5f * C / h * d / std::sqrt(1 + d * d), 0.0f, 1.0f);
}

void fxClouds(Work& w, FxBox b, const uint8_t fg[3], const uint8_t bg[3], unsigned seed, bool difference) {
    const float base = std::clamp(std::max(b.x1 - b.x0, b.y1 - b.y0) / 4.0f, 16.0f, 512.0f);
    rows(w.h, [&](int y) {
        for (int x = 0; x < w.w; ++x) {
            const float t = cloudsValue((float)(x - b.x0), (float)(y - b.y0), base, seed);
            float* o = w.at(x, y);
            for (int c = 0; c < 3; ++c) {
                const float v = (fg[c] + (bg[c] - fg[c]) * t) / 255.0f;
                o[c] = difference ? std::fabs(o[c] - v) : v;
            }
            if (!difference) o[3] = 1;
        }
    });
}

static void cellPoint(int cx, int cy, float cell, unsigned seed, float ox, float oy, float& px, float& py) {
    px = ox + (cx + 0.1f + 0.8f * hash01(cx, cy, 11, seed)) * cell;
    py = oy + (cy + 0.1f + 0.8f * hash01(cx, cy, 12, seed)) * cell;
}

void fxCrystallize(Work& w, FxBox b, float cell, unsigned seed) {
    // Voronoi cells round jittered points, each the colour at its point (cells stay solid, as Photoshop's)
    cell = std::max(cell, 2.0f);
    const Work src = w;
    rows(b.y1 - b.y0, [&](int yy) {
        const int y = yy + b.y0;
        for (int x = b.x0; x < b.x1; ++x) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const int cx = (int)std::floor((fx - b.x0) / cell), cy = (int)std::floor((fy - b.y0) / cell);
            float best = 1e30f, bx = fx, by = fy;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    float px, py; cellPoint(cx + dx, cy + dy, cell, seed, (float)b.x0, (float)b.y0, px, py);
                    const float d = (px - fx) * (px - fx) + (py - fy) * (py - fy);
                    if (d < best) { best = d; bx = px; by = py; }
                }
            const int sx = std::clamp((int)std::floor(bx), b.x0, b.x1 - 1), sy = std::clamp((int)std::floor(by), b.y0, b.y1 - 1);
            const float* s = &src.px[((size_t)sy * w.w + sx) * 4];
            std::copy(s, s + 4, w.at(x, y));
        }
    });
}

void fxPointillize(Work& w, FxBox b, float cell, unsigned seed, const uint8_t bgc[3]) {
    // round dots of the picture's colours on the background colour; the dot a pixel is deepest inside wins
    cell = std::max(cell, 2.0f);
    const Work src = w;
    rows(b.y1 - b.y0, [&](int yy) {
        const int y = yy + b.y0;
        for (int x = b.x0; x < b.x1; ++x) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const int cx = (int)std::floor((fx - b.x0) / cell), cy = (int)std::floor((fy - b.y0) / cell);
            float bestDepth = -1e30f, bx = 0, by = 0; int gx = 0, gy = 0; bool any = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    float px, py; cellPoint(cx + dx, cy + dy, cell, seed, (float)b.x0, (float)b.y0, px, py);
                    const float rad = cell * (0.45f + 0.35f * hash01(cx + dx, cy + dy, 13, seed));
                    const float depth = rad - std::sqrt((px - fx) * (px - fx) + (py - fy) * (py - fy));
                    if (depth > -0.5f && depth > bestDepth) { bestDepth = depth; bx = px; by = py; gx = cx + dx; gy = cy + dy; any = true; }
                }
            float canvas[3] = {bgc[0] / 255.0f, bgc[1] / 255.0f, bgc[2] / 255.0f};
            if (any) {
                const int sx = std::clamp((int)std::floor(bx), 0, w.w - 1), sy = std::clamp((int)std::floor(by), 0, w.h - 1);
                const float* d = &src.px[((size_t)sy * w.w + sx) * 4];
                const float j = (hash01(gx, gy, 17, seed) - 0.5f) * 0.08f, k = std::clamp(bestDepth + 0.5f, 0.0f, 1.0f);
                for (int c = 0; c < 3; ++c) canvas[c] += (std::clamp(d[c] + j, 0.0f, 1.0f) - canvas[c]) * k;
            }
            float* o = w.at(x, y);
            for (int c = 0; c < 3; ++c) o[c] = canvas[c];
            o[3] = std::max(o[3], 1.0f);
        }
    });
}
