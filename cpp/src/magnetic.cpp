// Chimera - Magnetic Lasso live wire, after PhotoCraft
// (crates/algo/src/magnetic.rs).
#include "magnetic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <tuple>

namespace Magnetic {
namespace {
// Cache source pixels in small blocks so pointer motion never scans the full image.
constexpr int BLOCK = 128;
constexpr float WC = 0.43f, WS = 0.43f, WD = 0.14f, WL = 0.02f, WG = 0.4f;
constexpr float PI = 3.14159265358979323846f, SQRT2 = 1.4142135623730951f;
constexpr float NEUTRAL = 2.0f / 3.0f;
constexpr int DIRS[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
struct Box {
    int x0, y0, x1, y1;
    // Return the nonnegative width of this half-open pixel box.
    int w() const { return std::max(0, x1 - x0); }
    // Return the nonnegative height of this half-open pixel box.
    int h() const { return std::max(0, y1 - y0); }
    // Test whether a pixel lies inside this half-open box.
    bool has(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};
// Intersect two half-open pixel boxes.
static Box intersect(Box a, Box b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}
// Measure the straight-line distance between two image points.
static double distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
// Return squared distance from a point to a clamped line segment.
static double segmentD2(Point p, Point a, Point b) {
    const double vx = b.x - a.x, vy = b.y - a.y, l2 = vx * vx + vy * vy;
    const double t = l2 > 0 ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / l2, 0.0, 1.0) : 0.0;
    const double dx = p.x - a.x - t * vx, dy = p.y - a.y - t * vy;
    return dx * dx + dy * dy;
}
// Sum the lengths of all segments in a path.
static double pathLength(const std::vector<Point> &p) {
    double n = 0;
    for (size_t i = 1; i < p.size(); ++i)
        n += distance(p[i - 1], p[i]);
    return n;
}
// Reduce a path with Douglas-Peucker while preserving its endpoints.
static std::vector<Point> simplify(const std::vector<Point> &p, double tol) {
    if (p.size() < 3)
        return p;
    std::vector<uint8_t> keep(p.size());
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> stack{{0, p.size() - 1}};
    const double tol2 = tol * tol;
    while (!stack.empty()) {
        auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1)
            continue;
        double far = 0;
        size_t at = a;
        for (size_t k = a + 1; k < b; ++k) {
            double d = segmentD2(p[k], p[a], p[b]);
            if (d > far) {
                far = d;
                at = k;
            }
        }
        if (far > tol2) {
            keep[at] = 1;
            stack.push_back({a, at});
            stack.push_back({at, b});
        }
    }
    std::vector<Point> out;
    for (size_t i = 0; i < p.size(); ++i)
        if (keep[i])
            out.push_back(p[i]);
    return out;
}
struct Field {
    Box area;
    int width;
    std::vector<float> strength;
    std::vector<std::array<float, 2>> grad, offset;
    std::vector<uint8_t> ridge;
    // Map an image pixel into this field's tightly packed arrays.
    size_t index(int x, int y) const { return (size_t)(y - area.y0) * width + (x - area.x0); }
    // Return the pixel centre, refined to the edge ridge when one is present.
    Point point(size_t i, int x, int y, float threshold) const {
        bool on = ridge[i] && strength[i] >= threshold;
        return {x + 0.5 + (on ? offset[i][0] : 0), y + 0.5 + (on ? offset[i][1] : 0)};
    }
    // Convert a packed field index back into an image point.
    Point pointAt(size_t i, float threshold) const {
        return point(i, (int)(i % width) + area.x0, (int)(i / width) + area.y0, threshold);
    }
};
struct SettingsC {
    float width, contrast;
    // Leave a small margin around the requested pointer corridor.
    float radius() const { return width + 1.5f; }
};
// Clamp settings to safe values for the field and graph search.
static SettingsC settings(Settings s) {
    return {std::clamp(std::isfinite(s.width) ? s.width : 10.0f, 1.0f, 256.0f),
            std::clamp(std::isfinite(s.contrast) ? s.contrast : 0.1f, 0.01f, 1.0f)};
}
} // namespace

std::array<uint8_t, 4> Tracer::pixel(int x, int y) {
    // Read through the 128-pixel cache so local tracing avoids whole-image copies.
    x = std::clamp(x, 0, image.w - 1);
    y = std::clamp(y, 0, image.h - 1);
    const int bx = x / BLOCK, by = y / BLOCK;
    const long long key = ((long long)bx << 32) | (uint32_t)by;
    if (blocks.size() > 1024)
        blocks.clear();
    auto it = blocks.find(key);
    if (it == blocks.end()) {
        const int x0 = bx * BLOCK, y0 = by * BLOCK, w = std::min(BLOCK, image.w - x0),
                  h = std::min(BLOCK, image.h - y0);
        std::vector<std::array<uint8_t, 4>> data((size_t)w * h);
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
                std::copy_n(image.at(x0 + xx, y0 + yy), 4, data[(size_t)yy * w + xx].begin());
        it = blocks.emplace(key, std::move(data)).first;
    }
    const int x0 = bx * BLOCK, y0 = by * BLOCK, w = std::min(BLOCK, image.w - x0);
    return it->second[(size_t)(y - y0) * w + (x - x0)];
}

namespace {
static Field makeField(Tracer &tr, Box area, Box bounds, float threshold) {
    // Sobel gradients find edges; non-maximum suppression keeps their centres
    // and estimates subpixel offsets for a smoother traced boundary.
    const Box ext = intersect({area.x0 - 2, area.y0 - 2, area.x1 + 2, area.y1 + 2}, bounds);
    const int ew = ext.w(), eh = ext.h();
    std::vector<std::array<float, 4>> ch((size_t)ew * eh);
    for (int y = ext.y0; y < ext.y1; ++y)
        for (int x = ext.x0; x < ext.x1; ++x) {
            auto p = tr.pixel(x, y);
            const float a = p[3] / 255.0f;
            ch[(size_t)(y - ext.y0) * ew + x - ext.x0] = {p[0] / 255.0f * a, p[1] / 255.0f * a, p[2] / 255.0f * a, a};
        }
    auto at = [&](int x, int y) {
        x = std::clamp(x - ext.x0, 0, std::max(0, ew - 1));
        y = std::clamp(y - ext.y0, 0, std::max(0, eh - 1));
        return ch[(size_t)y * ew + x];
    };
    const Box g1 = intersect({area.x0 - 1, area.y0 - 1, area.x1 + 1, area.y1 + 1}, ext);
    const int gw = g1.w();
    std::vector<float> st((size_t)gw * g1.h());
    std::vector<std::array<float, 2>> gd(st.size());
    auto si = [&](int x, int y) { return (size_t)(y - g1.y0) * gw + (x - g1.x0); };
    for (int y = g1.y0; y < g1.y1; ++y)
        for (int x = g1.x0; x < g1.x1; ++x) {
            auto nw = at(x - 1, y - 1), n = at(x, y - 1), ne = at(x + 1, y - 1), west = at(x - 1, y),
                 east = at(x + 1, y), sw = at(x - 1, y + 1), south = at(x, y + 1), se = at(x + 1, y + 1);
            float best = 0, gxBest = 0, gyBest = 0;
            for (int c = 0; c < 4; ++c) {
                float gx = ne[c] + 2 * east[c] + se[c] - nw[c] - 2 * west[c] - sw[c],
                      gy = sw[c] + 2 * south[c] + se[c] - nw[c] - 2 * n[c] - ne[c], m = gx * gx + gy * gy;
                if (m > best) {
                    best = m;
                    gxBest = gx;
                    gyBest = gy;
                }
            }
            float mag = std::sqrt(best);
            size_t i = si(x, y);
            st[i] = std::min(mag / 4.0f, 1.0f);
            gd[i] = mag > 1e-6f ? std::array<float, 2>{gxBest / mag, gyBest / mag} : std::array<float, 2>{0, 0};
        }
    auto strengthAt = [&](int x, int y) { return g1.has(x, y) ? st[si(x, y)] : 0.0f; };
    Field f{area,
            area.w(),
            std::vector<float>((size_t)area.w() * area.h()),
            std::vector<std::array<float, 2>>((size_t)area.w() * area.h()),
            std::vector<std::array<float, 2>>((size_t)area.w() * area.h()),
            std::vector<uint8_t>((size_t)area.w() * area.h())};
    for (int y = area.y0; y < area.y1; ++y)
        for (int x = area.x0; x < area.x1; ++x) {
            size_t i = f.index(x, y);
            float s0 = strengthAt(x, y);
            auto g = g1.has(x, y) ? gd[si(x, y)] : std::array<float, 2>{0, 0};
            f.strength[i] = s0;
            f.grad[i] = g;
            if (g[0] == 0 && g[1] == 0)
                continue;
            auto q = [](float v, float o) {
                return std::fabs(v) >= 0.4142f * std::fabs(o) ? (v > 0 ? 1 : (v < 0 ? -1 : 0)) : 0;
            };
            int dx = q(g[0], g[1]), dy = q(g[1], g[0]);
            float sm = strengthAt(x - dx, y - dy), sp = strengthAt(x + dx, y + dy);
            if (s0 > 0 && s0 >= sm && s0 >= sp) {
                f.ridge[i] = 1;
                float den = sm - 2 * s0 + sp, t = den < -1e-6f ? std::clamp(0.5f * (sm - sp) / den, -0.5f, 0.5f) : 0;
                f.offset[i] = {t * dx, t * dy};
            }
        }
    (void)threshold;
    return f;
}
static std::vector<float> makeCorridor(Box area, const std::vector<Point> &guide, double rad) {
    // Restrict graph search to pixels near the pointer guide and retain each
    // pixel's distance for the small cost that pulls the wire towards it.
    const int w = area.w();
    std::vector<float> d((size_t)w * area.h(), std::numeric_limits<float>::infinity());
    double r2 = rad * rad;
    if (guide.empty())
        return d;
    for (size_t i = 1; i < guide.size(); ++i) {
        Point a = guide[i - 1], b = guide[i];
        Box r = intersect({(int)std::floor(std::min(a.x, b.x) - rad), (int)std::floor(std::min(a.y, b.y) - rad),
                           (int)std::ceil(std::max(a.x, b.x) + rad) + 1, (int)std::ceil(std::max(a.y, b.y) + rad) + 1},
                          area);
        for (int y = r.y0; y < r.y1; ++y)
            for (int x = r.x0; x < r.x1; ++x) {
                double q = segmentD2({x + 0.5, y + 0.5}, a, b);
                if (q <= r2) {
                    size_t k = (size_t)(y - area.y0) * w + x - area.x0;
                    d[k] = std::min(d[k], (float)q);
                }
            }
    }
    if (guide.size() == 1) {
        Point p = guide[0];
        Box r = intersect({(int)std::floor(p.x - rad), (int)std::floor(p.y - rad), (int)std::ceil(p.x + rad) + 1,
                           (int)std::ceil(p.y + rad) + 1},
                          area);
        for (int y = r.y0; y < r.y1; ++y)
            for (int x = r.x0; x < r.x1; ++x) {
                double q = segmentD2({x + 0.5, y + 0.5}, p, p);
                if (q <= r2)
                    d[(size_t)(y - area.y0) * w + x - area.x0] =
                        std::min(d[(size_t)(y - area.y0) * w + x - area.x0], (float)q);
            }
    }
    return d;
}
// Penalize paths that run along an edge instead of crossing its gradient.
static float directionCost(std::array<float, 2> a, std::array<float, 2> b, int dx, int dy, float len) {
    if ((a[0] == 0 && a[1] == 0) || (b[0] == 0 && b[1] == 0))
        return NEUTRAL;
    float x = dx / len, y = dy / len;
    float da = std::clamp(std::fabs(a[0] * x + a[1] * y), 0.0f, 1.0f),
          db = std::clamp(std::fabs(b[0] * x + b[1] * y), 0.0f, 1.0f);
    return 2.0f / (3.0f * PI) * (std::acos(da) + std::acos(db));
}
static std::vector<size_t> searchPath(const Field &f, const std::vector<float> &corridor, float radius2, size_t start,
                                      size_t goal, float contrast) {
    // Dijkstra explores eight neighbours using edge, centring, direction, and
    // pointer-guide costs from PhotoCraft's live-wire model.
    const size_t n = f.strength.size();
    const int w = f.width, ht = (int)(n / std::max(1, w));
    std::vector<float> node(n, std::numeric_limits<float>::infinity()), dist(n, std::numeric_limits<float>::infinity());
    std::vector<std::array<float, 2>> tangent(n);
    std::vector<uint8_t> from(n, 255);
    for (size_t i = 0; i < n; ++i) {
        float d2 = corridor[i], s = f.strength[i];
        if (!std::isfinite(d2))
            continue;
        bool edge = s >= contrast && f.ridge[i];
        float good =
            std::clamp((s - contrast) / std::max(contrast, 0.02f), 0.0f, 1.0f) * (0.6f + 0.4f * std::min(s, 1.0f));
        float pull = std::min(std::sqrt(d2 / std::max(radius2, 1.0f)), 1.0f);
        node[i] = WC * (edge ? 0 : 1) + WS * (1 - good) + WL + WG * pull;
        if (s >= 0.5f * contrast)
            tangent[i] = {f.grad[i][1], -f.grad[i][0]};
    }
    if (start >= n || goal >= n)
        return {};
    dist[start] = 0;
    using Q = std::pair<float, size_t>;
    std::priority_queue<Q, std::vector<Q>, std::greater<Q>> heap;
    heap.push({0.0f, start});
    while (!heap.empty()) {
        auto [d, i] = heap.top();
        heap.pop();
        if (d > dist[i])
            continue;
        if (i == goal)
            break;
        int x = (int)(i % w), y = (int)(i / w);
        for (int k = 0; k < 8; ++k) {
            int dx = DIRS[k][0], dy = DIRS[k][1], nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= ht)
                continue;
            size_t j = (size_t)ny * w + nx;
            float c = node[j];
            if (!std::isfinite(c))
                continue;
            float len = dx && dy ? SQRT2 : 1.0f,
                  nd = d + c * len + WD * directionCost(tangent[i], tangent[j], dx, dy, len);
            if (nd < dist[j]) {
                dist[j] = nd;
                from[j] = (uint8_t)k;
                heap.push({nd, j});
            }
        }
    }
    if (!std::isfinite(dist[goal]))
        return {};
    std::vector<size_t> cells{goal};
    size_t cur = goal;
    while (cur != start && cells.size() <= n) {
        uint8_t k = from[cur];
        if (k >= 8)
            return {};
        int x = (int)(cur % w) - DIRS[k][0], y = (int)(cur / w) - DIRS[k][1];
        if (x < 0 || y < 0)
            return {};
        cur = (size_t)y * w + x;
        cells.push_back(cur);
    }
    std::reverse(cells.begin(), cells.end());
    return cells;
}
static std::vector<Point> tracePiece(Tracer &tr, Point a, Point b, const std::vector<Point> &guide, SettingsC s,
                                     Box bounds) {
    // Build and search one bounded section of the live wire.
    const int ax = std::clamp((int)std::floor(a.x), bounds.x0, bounds.x1 - 1),
              ay = std::clamp((int)std::floor(a.y), bounds.y0, bounds.y1 - 1);
    const int bx = std::clamp((int)std::floor(b.x), bounds.x0, bounds.x1 - 1),
              by = std::clamp((int)std::floor(b.y), bounds.y0, bounds.y1 - 1);
    Box box{std::min(ax, bx), std::min(ay, by), std::max(ax, bx) + 1, std::max(ay, by) + 1};
    for (Point p : guide) {
        int x = (int)std::floor(p.x), y = (int)std::floor(p.y);
        box = {std::min(box.x0, x), std::min(box.y0, y), std::max(box.x1, x + 1), std::max(box.y1, y + 1)};
    }
    const int pad = (int)std::ceil(s.radius()) + 1;
    Box area = intersect({box.x0 - pad, box.y0 - pad, box.x1 + pad, box.y1 + pad}, bounds);
    Field f = makeField(tr, area, bounds, s.contrast);
    std::vector<float> distances = makeCorridor(area, guide, s.radius());
    size_t start = f.index(ax, ay), goal = f.index(bx, by);
    distances[start] = 0;
    distances[goal] = 0;
    auto cells = searchPath(f, distances, s.radius() * s.radius(), start, goal, s.contrast);
    if (cells.empty())
        return {a, b};
    std::vector<Point> pts;
    pts.reserve(cells.size());
    for (size_t i : cells)
        pts.push_back(f.pointAt(i, s.contrast));
    if (pts.size() < 2)
        return {a, b};
    pts.front() = a;
    pts.back() = b;
    return pts;
}
} // namespace

Point Tracer::snap(Point p, Settings raw) {
    // Snap anchors to nearby strong edge centres; the path between anchors is
    // selected by searchPath rather than by independent point snapping.
    SettingsC s = settings(raw);
    if (image.w <= 0 || image.h <= 0)
        return {};
    p.x = std::clamp(p.x, 0.0, (double)image.w);
    p.y = std::clamp(p.y, 0.0, (double)image.h);
    int px = std::clamp((int)std::floor(p.x), 0, image.w - 1), py = std::clamp((int)std::floor(p.y), 0, image.h - 1),
        r = (int)std::ceil(s.width) + 1;
    Box bounds{0, 0, image.w, image.h}, area = intersect({px - r, py - r, px + r + 1, py + r + 1}, bounds);
    Field f = makeField(*this, area, bounds, s.contrast);
    float best = s.contrast;
    Point out = p;
    for (int y = area.y0; y < area.y1; ++y)
        for (int x = area.x0; x < area.x1; ++x) {
            size_t i = f.index(x, y);
            float strength = f.strength[i];
            if (strength < s.contrast || !f.ridge[i])
                continue;
            double d = distance({x + 0.5, y + 0.5}, p);
            if (d > s.width)
                continue;
            float score = strength * (1.0f - 0.25f * (float)(d / s.width));
            if (score > best) {
                best = score;
                out = f.point(i, x, y, s.contrast);
            }
        }
    return out;
}

std::vector<Point> Tracer::trace(Point from, Point to, const std::vector<Point> &input, Settings raw) {
    // Split long pointer movements into bounded searches, then simplify the
    // combined pixel path to keep interaction responsive.
    if (image.w <= 0 || image.h <= 0)
        return {};
    SettingsC s = settings(raw);
    Box bounds{0, 0, image.w, image.h};
    auto clamp = [&](Point p) {
        p.x = std::clamp(p.x, 0.0, (double)image.w);
        p.y = std::clamp(p.y, 0.0, (double)image.h);
        return p;
    };
    from = clamp(from);
    to = clamp(to);
    std::vector<Point> guide{from};
    for (Point p : input)
        guide.push_back(clamp(p));
    guide.push_back(to);
    guide = simplify(guide, 0.5);
    if (pathLength(guide) > 262144)
        return guide;
    std::vector<Point> out{from};
    Point cur = from;
    std::vector<Point> piece{from};
    double length = 0;
    for (size_t i = 1; i < guide.size(); ++i) {
        Point a = guide[i - 1], b = guide[i];
        double seg = distance(a, b);
        while (length + seg > 256 && seg > 0) {
            double t = (256 - length) / seg;
            Point mid{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            piece.push_back(mid);
            Point target = snap(mid, raw);
            std::vector<Point> local = piece;
            auto part = tracePiece(*this, cur, target, local, s, bounds);
            out.insert(out.end(), part.begin() + std::min<size_t>(1, part.size()), part.end());
            cur = target;
            piece = {target, mid};
            a = mid;
            seg = distance(a, b);
            length = 0;
        }
        piece.push_back(b);
        length += seg;
    }
    if (distance(cur, to) > 1e-6) {
        auto part = tracePiece(*this, cur, to, piece, s, bounds);
        out.insert(out.end(), part.begin() + std::min<size_t>(1, part.size()), part.end());
    }
    return simplify(out, 0.5);
}
} // namespace Magnetic
