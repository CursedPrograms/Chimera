// Chimera - the Quick Selection brush (see quicksel.h), PhotoCraft's model and constants in C++.
#include "quicksel.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace QuickSel {

// ---- the model's constants: PhotoCraft's fit to Photoshop 25.4 (quick.rs, Tuning::default)
static const float AREA = 0.004079f, DIST = 0.002651f, POW = 0.98885f;
static const float FLOOR_W = 0.018847f, SIGMA = 0.048450f, KEXP = 1.98549f;
static const float GROW = 1.69371f, GROW_FRAC = 0.103003f;
static const size_t DIRECT_PX = 300000;
static const float BAND = 3.0f;
enum { FREE = 0, HARD_FG = 1, HARD_BG = 2 };

// ---- the exact Euclidean distance transform (Felzenszwalb & Huttenlocher), squared distances along rows then columns
static void edt1(const float* f, int n, float* d, int* v, float* z) {
    int k = 0; v[0] = 0; z[0] = -1e20f; z[1] = 1e20f;
    for (int q = 1; q < n; ++q) {
        float s;
        while (true) {
            s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
            if (s <= z[k] && k > 0) --k; else break;
        }
        if (s <= z[k]) { v[0] = q; z[0] = -1e20f; z[1] = 1e20f; k = 0; continue; }
        ++k; v[k] = q; z[k] = s; z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; ++q) { while (z[k + 1] < q) ++k; d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]]; }
}
std::vector<float> edt(const std::vector<uint8_t>& seeds, int w, int h) {
    const float INF = 1e20f;
    std::vector<float> g((size_t)w * h);
    for (size_t i = 0; i < g.size(); ++i) g[i] = seeds[i] ? 0.0f : INF;
    const int n = std::max(w, h);
    std::vector<float> f(n), d(n), z(n + 1); std::vector<int> v(n);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[y] = g[(size_t)y * w + x];
        edt1(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; ++y) g[(size_t)y * w + x] = d[y];
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) f[x] = g[(size_t)y * w + x];
        edt1(f.data(), w, d.data(), v.data(), z.data());
        for (int x = 0; x < w; ++x) g[(size_t)y * w + x] = std::sqrt(std::min(d[x], INF));
    }
    return g;
}

// ---- max-flow: Boykov & Kolmogorov's algorithm (search trees grown from both terminals, reused between augmentations)
class Graph {
    struct Arc { int head, next, sister; float cap; };
    struct Node { int first = -1, parent = NONE, ts = 0, dist = 0; bool sink = false, active = false; float tr = 0; };
    static const int NONE = -1, TERMINAL = -2, ORPHAN = -3;
    std::vector<Node> n;
    std::vector<Arc> a;
    std::deque<int> active, orphans;
    int time = 0;
    void setActive(int i) { if (!n[i].active) { n[i].active = true; active.push_back(i); } }
public:
    explicit Graph(size_t nodes, size_t edges) { n.resize(nodes); a.reserve(edges * 2); }
    void addT(int i, float src, float snk) {
        const float delta = n[i].tr;
        if (delta > 0) src += delta; else snk -= delta;
        n[i].tr = src - snk;
    }
    void addEdge(int i, int j, float cap, float rev) {
        const int ai = (int)a.size(), aj = ai + 1;
        a.push_back({j, n[i].first, aj, cap}); n[i].first = ai;
        a.push_back({i, n[j].first, ai, rev}); n[j].first = aj;
    }
    bool inSource(int i) const { return n[i].parent != NONE && !n[i].sink; }
    void maxflow() {
        for (size_t i = 0; i < n.size(); ++i) {
            Node& x = n[i];
            if (x.tr != 0) { x.sink = x.tr < 0; x.parent = TERMINAL; x.ts = 0; x.dist = 1; setActive((int)i); }
            else x.parent = NONE;
        }
        int cur = -1;
        while (true) {
            // the next active node (the current one stays while it still finds paths)
            if (cur >= 0 && n[cur].parent == NONE) cur = -1;
            if (cur < 0) {
                while (!active.empty()) { const int i = active.front(); active.pop_front(); n[i].active = false; if (n[i].parent != NONE) { cur = i; break; } }
                if (cur < 0) break;
            }
            // grow its tree until it meets the other one
            int middle = -1;
            const Node& c = n[cur];
            for (int e = c.first; e >= 0; e = a[e].next) {
                const bool open = c.sink ? a[a[e].sister].cap > 0 : a[e].cap > 0;
                if (!open) continue;
                const int j = a[e].head;
                Node& nj = n[j];
                if (nj.parent == NONE) {
                    nj.sink = c.sink; nj.parent = a[e].sister; nj.ts = c.ts; nj.dist = c.dist + 1; setActive(j);
                } else if (nj.sink != c.sink) { middle = c.sink ? a[e].sister : e; break; }
                else if (nj.ts <= c.ts && nj.dist > c.dist) { nj.parent = a[e].sister; nj.ts = c.ts; nj.dist = c.dist + 1; }
            }
            ++time;
            if (middle < 0) { cur = -1; continue; }
            n[cur].active = true; active.push_front(cur);   // it may have more to give
            augment(middle);
            adopt();
            cur = -1;
        }
    }
private:
    void augment(int middle) {
        // the bottleneck along source ... middle ... sink
        float b = a[middle].cap;
        for (int i = a[a[middle].sister].head;;) {
            const int e = n[i].parent;
            if (e == TERMINAL) break;
            b = std::min(b, a[a[e].sister].cap); i = a[e].head;
        }
        for (int i = a[a[middle].sister].head;; ) { const int e = n[i].parent; if (e == TERMINAL) { b = std::min(b, n[i].tr); break; } i = a[e].head; }
        for (int i = a[middle].head;;) {
            const int e = n[i].parent;
            if (e == TERMINAL) { b = std::min(b, -n[i].tr); break; }
            b = std::min(b, a[e].cap); i = a[e].head;
        }
        // push it
        a[a[middle].sister].cap += b; a[middle].cap -= b;
        for (int i = a[a[middle].sister].head;;) {
            const int e = n[i].parent;
            if (e == TERMINAL) { n[i].tr -= b; if (n[i].tr == 0) { n[i].parent = ORPHAN; orphans.push_front(i); } break; }
            a[e].cap += b; a[a[e].sister].cap -= b;
            if (a[a[e].sister].cap == 0) { n[i].parent = ORPHAN; orphans.push_front(i); }
            i = a[e].head;
        }
        for (int i = a[middle].head;;) {
            const int e = n[i].parent;
            if (e == TERMINAL) { n[i].tr += b; if (n[i].tr == 0) { n[i].parent = ORPHAN; orphans.push_front(i); } break; }
            a[a[e].sister].cap += b; a[e].cap -= b;
            if (a[e].cap == 0) { n[i].parent = ORPHAN; orphans.push_front(i); }
            i = a[e].head;
        }
    }
    void adopt() {
        const int INF_D = 1 << 30;
        while (!orphans.empty()) {
            const int i = orphans.front(); orphans.pop_front();
            const bool sink = n[i].sink;
            int best = NONE, dMin = INF_D;
            for (int e = n[i].first; e >= 0; e = a[e].next) {
                const bool open = sink ? a[e].cap > 0 : a[a[e].sister].cap > 0;
                if (!open) continue;
                int j = a[e].head;
                if (n[j].sink != sink || n[j].parent == NONE) continue;
                int d = 0;
                while (true) {   // does j still reach a terminal?
                    if (n[j].ts == time) { d += n[j].dist; break; }
                    const int p = n[j].parent; ++d;
                    if (p == TERMINAL) { n[j].ts = time; n[j].dist = 1; break; }
                    if (p == ORPHAN) { d = INF_D; break; }
                    j = a[p].head;
                }
                if (d < INF_D) {
                    if (d < dMin) { best = e; dMin = d; }
                    for (j = a[e].head; n[j].ts != time; j = a[n[j].parent].head) { n[j].ts = time; n[j].dist = d--; }
                }
            }
            if (best != NONE) { n[i].parent = best; n[i].ts = time; n[i].dist = dMin + 1; continue; }
            n[i].parent = NONE;   // free: its children are orphans, its neighbours may grow into it
            for (int e = n[i].first; e >= 0; e = a[e].next) {
                const int j = a[e].head;
                if (n[j].sink != sink || n[j].parent == NONE) continue;
                const bool open = sink ? a[e].cap > 0 : a[a[e].sister].cap > 0;
                if (open) setActive(j);
                const int p = n[j].parent;
                if (p != TERMINAL && p != ORPHAN && a[p].head == i) { n[j].parent = ORPHAN; orphans.push_back(j); }
            }
        }
    }
};

struct Rgb { int w = 0, h = 0; std::vector<float> px; const float* at(int x, int y) const { return &px[((size_t)y * w + x) * 3]; } };
static float d2(const float* p, const float* q) { const float a = p[0] - q[0], b = p[1] - q[1], c = p[2] - q[2]; return a * a + b * b + c * c; }
static float weight(float dd) { return FLOOR_W + 1.0f / (1.0f + std::pow(std::sqrt(dd) / SIGMA, KEXP)); }

// the min cut on a grid: costFg[i] is what selecting i costs; fixed: FREE, HARD_FG or HARD_BG; 8-connected
static std::vector<uint8_t> gridCut(const Rgb& img, const std::vector<float>& costFg, const std::vector<uint8_t>& fixed, float scale) {
    const int w = img.w, h = img.h;
    const size_t N = (size_t)w * h;
    std::vector<int> idx(N, -1);
    int count = 0;
    for (size_t i = 0; i < N; ++i) if (fixed[i] == FREE) idx[i] = count++;
    std::vector<uint8_t> out(N);
    for (size_t i = 0; i < N; ++i) out[i] = fixed[i] == HARD_FG;
    if (!count) return out;
    Graph g(count, (size_t)count * 4);
    for (size_t i = 0; i < N; ++i) if (idx[i] >= 0) g.addT(idx[i], 0.0f, costFg[i]);   // source: selected
    static const int NB[4][2] = {{1, 0}, {0, 1}, {1, 1}, {-1, 1}};
    static const float LEN[4] = {1.0f, 1.0f, 1.41421356f, 1.41421356f};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + NB[k][0], ny = y + NB[k][1];
                if (nx < 0 || nx >= w || ny >= h) continue;
                const size_t j = (size_t)ny * w + nx;
                if (idx[i] < 0 && idx[j] < 0) continue;
                const float wt = scale * weight(d2(img.at(x, y), img.at(nx, ny))) / LEN[k];
                if (idx[i] >= 0 && idx[j] >= 0) g.addEdge(idx[i], idx[j], wt, wt);
                else {   // a link to a fixed pixel: cutting it costs wt
                    const int node = idx[i] >= 0 ? idx[i] : idx[j];
                    const uint8_t f = idx[i] >= 0 ? fixed[j] : fixed[i];
                    if (f == HARD_FG) g.addT(node, wt, 0.0f); else g.addT(node, 0.0f, wt);
                }
            }
        }
    g.maxflow();
    for (size_t i = 0; i < N; ++i) if (idx[i] >= 0) out[i] = g.inSource(idx[i]);
    return out;
}

static Rgb downsample(const Rgb& s, int step) {
    Rgb o; o.w = (s.w + step - 1) / step; o.h = (s.h + step - 1) / step; o.px.assign((size_t)o.w * o.h * 3, 0.0f);
    for (int oy = 0; oy < o.h; ++oy)
        for (int ox = 0; ox < o.w; ++ox) {
            float acc[3] = {0, 0, 0}; int n = 0;
            for (int y = oy * step; y < std::min(s.h, (oy + 1) * step); ++y)
                for (int x = ox * step; x < std::min(s.w, (ox + 1) * step); ++x, ++n) { const float* p = s.at(x, y); acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; }
            float* q = &o.px[((size_t)oy * o.w + ox) * 3];
            for (int c = 0; c < 3; ++c) q[c] = acc[c] / std::max(1, n);
        }
    return o;
}

// the cut, coarse to fine above DIRECT_PX pixels: the half-size problem gives a boundary, the full-size cut is redone
// only within BAND of it
static std::vector<uint8_t> cutLevels(const Rgb& img, const std::vector<uint8_t>& fixed, const std::vector<float>& costFg, float scale) {
    const int w = img.w, h = img.h;
    if ((size_t)w * h <= DIRECT_PX || w < 8 || h < 8) return gridCut(img, costFg, fixed, scale);
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    const Rgb cimg = downsample(img, 2);
    std::vector<uint8_t> cfix((size_t)cw * ch, FREE);
    std::vector<float> ccost((size_t)cw * ch, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t c = (size_t)(y / 2) * cw + x / 2, i = (size_t)y * w + x;
            ccost[c] += costFg[i];
            if (fixed[i] == HARD_FG) cfix[c] = HARD_FG;
        }
    const std::vector<uint8_t> coarse = cutLevels(cimg, cfix, ccost, scale * 2);
    std::vector<uint8_t> proj((size_t)w * h), edge((size_t)w * h, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) proj[(size_t)y * w + x] = coarse[(size_t)(y / 2) * cw + x / 2];
    bool anyEdge = false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x; const uint8_t v = proj[i];
            if ((x > 0 && proj[i - 1] != v) || (x + 1 < w && proj[i + 1] != v) || (y > 0 && proj[i - w] != v) || (y + 1 < h && proj[i + w] != v)) { edge[i] = 1; anyEdge = true; }
        }
    if (!anyEdge) { for (size_t i = 0; i < proj.size(); ++i) proj[i] = proj[i] || fixed[i] == HARD_FG; return proj; }
    const std::vector<float> near = edt(edge, w, h);
    std::vector<uint8_t> banded((size_t)w * h);
    for (size_t i = 0; i < banded.size(); ++i)
        banded[i] = fixed[i] == HARD_FG ? HARD_FG : near[i] <= BAND ? FREE : proj[i] ? HARD_FG : HARD_BG;
    return gridCut(img, costFg, banded, scale);
}

// pixels within `radius` of the polyline, plus each point's own pixel
static std::vector<uint8_t> strokeMask(const std::vector<float>& pts, float radius, int w, int h) {
    std::vector<uint8_t> m((size_t)w * h, 0);
    const size_t np = pts.size() / 2;
    for (size_t s = 0; s < std::max<size_t>(1, np - 1) && np; ++s) {
        const float ax = pts[s * 2], ay = pts[s * 2 + 1];
        const float bx = np > 1 ? pts[s * 2 + 2] : ax, by = np > 1 ? pts[s * 2 + 3] : ay;
        const int x0 = std::max(0, (int)std::floor(std::min(ax, bx) - radius)), y0 = std::max(0, (int)std::floor(std::min(ay, by) - radius));
        const int x1 = std::min(w, (int)std::ceil(std::max(ax, bx) + radius)), y1 = std::min(h, (int)std::ceil(std::max(ay, by) + radius));
        const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float t = len2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
                const float qx = ax + t * dx - px, qy = ay + t * dy - py;
                if (qx * qx + qy * qy <= radius * radius) m[(size_t)y * w + x] = 1;
            }
    }
    for (size_t k = 0; k < np; ++k) {
        const float x = pts[k * 2], y = pts[k * 2 + 1];
        if (x >= 0 && y >= 0 && x < w && y < h) m[(size_t)y * w + (int)x] = 1;
    }
    return m;
}

std::vector<uint8_t> select(const Image& img, const std::vector<float>& points, float size) {
    const int W = img.w, H = img.h;
    if (points.size() < 2 || W <= 0 || H <= 0) return {};
    const float r = std::max(1.0f, size) / 2;
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    for (size_t k = 0; k + 1 < points.size(); k += 2) {
        bx0 = std::min(bx0, points[k] - r); by0 = std::min(by0, points[k + 1] - r);
        bx1 = std::max(bx1, points[k] + r); by1 = std::max(by1, points[k + 1] + r);
    }
    Rect bbox{std::max(0, (int)std::floor(bx0)), std::max(0, (int)std::floor(by0)), std::min(W, (int)std::ceil(bx1) + 1), std::min(H, (int)std::ceil(by1) + 1)};
    if (bbox.empty()) return {};
    const int step = (size_t)W * H <= 512u * 512u ? 1 : 2;   // Photoshop: the whole document up to 512 x 512, half size above
    const float rw = r / step;
    // a window round the stroke, grown while the selection runs into a side of it that isn't the canvas's edge
    int margin = (int)std::max(48.0f, 2 * rw) * step;
    while (true) {
        Rect g{std::max(0, bbox.x0 - margin), std::max(0, bbox.y0 - margin), std::min(W, bbox.x1 + margin), std::min(H, bbox.y1 + margin)};
        g.x0 -= g.x0 % step; g.y0 -= g.y0 % step;
        Rgb full; full.w = g.x1 - g.x0; full.h = g.y1 - g.y0; full.px.resize((size_t)full.w * full.h * 3);
        for (int y = 0; y < full.h; ++y)
            for (int x = 0; x < full.w; ++x) {
                const uint8_t* p = img.at(x + g.x0, y + g.y0);
                float* q = &full.px[((size_t)y * full.w + x) * 3];
                q[0] = p[0] / 255.0f; q[1] = p[1] / 255.0f; q[2] = p[2] / 255.0f;
            }
        const Rgb work = step > 1 ? downsample(full, step) : full;
        const int lw = work.w, lh = work.h;
        std::vector<float> pts;
        for (size_t k = 0; k + 1 < points.size(); k += 2) { pts.push_back((points[k] - g.x0) / step); pts.push_back((points[k + 1] - g.y0) / step); }
        const std::vector<uint8_t> seeds = strokeMask(pts, std::max(0.5f, rw), lw, lh);
        if (std::none_of(seeds.begin(), seeds.end(), [](uint8_t v) { return v != 0; })) return {};
        const std::vector<float> d = edt(seeds, lw, lh);
        const float grow = std::max(0.0f, GROW + GROW_FRAC * rw);
        std::vector<float> cost(d.size()); std::vector<uint8_t> fixed(d.size());
        for (size_t i = 0; i < d.size(); ++i) {
            cost[i] = AREA * (1 + DIST * std::pow(std::max(0.0f, d[i] - grow), POW));
            fixed[i] = d[i] <= grow ? HARD_FG : FREE;
        }
        const std::vector<uint8_t> low = cutLevels(work, fixed, cost, 1.0f);
        const bool inner[4] = {g.x0 > 0, g.y0 > 0, g.x1 < W, g.y1 < H};
        bool touches = false;
        for (int y = 0; y < lh && !touches; ++y) if ((inner[0] && low[(size_t)y * lw]) || (inner[2] && low[(size_t)y * lw + lw - 1])) touches = true;
        for (int x = 0; x < lw && !touches; ++x) if ((inner[1] && low[x]) || (inner[3] && low[(size_t)(lh - 1) * lw + x])) touches = true;
        const bool whole = !inner[0] && !inner[1] && !inner[2] && !inner[3];
        if (touches && !whole) { margin *= 2; continue; }
        // up to the canvas: bilinear from half size, as Photoshop scales its half-resolution result
        std::vector<uint8_t> out((size_t)W * H, 0);
        for (int y = g.y0; y < g.y1; ++y)
            for (int x = g.x0; x < g.x1; ++x) {
                float v;
                if (step == 1) v = low[(size_t)(y - g.y0) * lw + (x - g.x0)] ? 1.0f : 0.0f;
                else {
                    const float fx = (x - g.x0 + 0.5f) / step - 0.5f, fy = (y - g.y0 + 0.5f) / step - 0.5f;
                    const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
                    const float tx = fx - ix, ty = fy - iy;
                    auto at = [&](int ax, int ay) { return low[(size_t)std::clamp(ay, 0, lh - 1) * lw + std::clamp(ax, 0, lw - 1)] ? 1.0f : 0.0f; };
                    v = (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty) + (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
                }
                out[(size_t)y * W + x] = (uint8_t)std::lround(v * 255);
            }
        return out;
    }
}

}
