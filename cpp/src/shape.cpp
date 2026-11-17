// Chimera - shape layers (see shape.h)
#include "shape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

static const float PI = 3.14159265f;

const char* shapeName(int k) {
    static const char* n[] = {"Shape", "Rectangle", "Ellipse", "Triangle", "Polygon", "Star", "Line", "Path"};
    return k >= 0 && k < SHAPE_COUNT ? n[k] : "Shape";
}

std::string Shape::save() const {
    char b[320];
    snprintf(b, sizeof b, "shape1 %d %g %g %g %g %g %d %g %g %d %d %g %g %g %g", kind, x0, y0, x1, y1, radius, sides, inner, weight,
             fillOn ? 1 : 0, strokeOn ? 1 : 0, stroke[0], stroke[1], stroke[2], strokeWidth);
    std::string o = b;
    if (kind == SHAPE_PATH) {   // then its path: subpaths, each "closed count" and its knots
        o += " " + std::to_string(path.size());
        for (const auto& sp : path) {
            o += " " + std::to_string(sp.closed ? 1 : 0) + " " + std::to_string(sp.knots.size());
            for (const auto& k : sp.knots) { snprintf(b, sizeof b, " %g %g %g %g %g %g %d", k.ax, k.ay, k.ix, k.iy, k.ox, k.oy, k.smooth ? 1 : 0); o += b; }
        }
    }
    return o;
}
bool Shape::load(const std::string& blob) {
    int k = 0, f = 1, st = 0;
    if (sscanf(blob.c_str(), "shape1 %d %f %f %f %f %f %d %f %f %d %d %f %f %f %f", &k, &x0, &y0, &x1, &y1, &radius, &sides, &inner, &weight,
               &f, &st, &stroke[0], &stroke[1], &stroke[2], &strokeWidth) != 15) return false;
    kind = std::clamp(k, 0, SHAPE_COUNT - 1); fillOn = f != 0; strokeOn = st != 0;
    sides = std::clamp(sides, 3, 100);
    path.clear();
    if (kind == SHAPE_PATH) {
        std::istringstream in(blob);
        std::string w; for (int i = 0; i < 16 && (in >> w); ++i) {}   // past the fixed fields
        size_t n = 0; in >> n;
        for (size_t i = 0; i < n && in; ++i) {
            Subpath sp; int c = 0; size_t m = 0; in >> c >> m; sp.closed = c != 0;
            for (size_t j = 0; j < m && in; ++j) { Knot kn; int sm = 0; in >> kn.ax >> kn.ay >> kn.ix >> kn.iy >> kn.ox >> kn.oy >> sm; kn.smooth = sm != 0; sp.knots.push_back(kn); }
            path.push_back(sp);
        }
    }
    return true;
}

// a polygon's corners rounded: each corner replaced by an arc of radius r touching both its edges (PhotoCraft's fillet)
static std::vector<float> roundCorners(const std::vector<float>& p, float r) {
    const int n = (int)p.size() / 2;
    if (r <= 0.25f || n < 3) return p;
    std::vector<float> out;
    for (int i = 0; i < n; ++i) {
        const float vx = p[i * 2], vy = p[i * 2 + 1];
        const float ax = p[((i + n - 1) % n) * 2] - vx, ay = p[((i + n - 1) % n) * 2 + 1] - vy;
        const float bx = p[((i + 1) % n) * 2] - vx, by = p[((i + 1) % n) * 2 + 1] - vy;
        const float la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la < 1e-3f || lb < 1e-3f) { out.push_back(vx); out.push_back(vy); continue; }
        const float ux = ax / la, uy = ay / la, wx = bx / lb, wy = by / lb;
        const float theta = std::acos(std::clamp(ux * wx + uy * wy, -1.0f, 1.0f));
        if (theta < 1e-3f || theta > PI - 1e-3f) { out.push_back(vx); out.push_back(vy); continue; }
        // how far along each edge the arc starts (at most half the shorter edge), and the radius that fits
        float d = r / std::tan(theta / 2);
        d = std::min(d, 0.5f * std::min(la, lb));
        const float rr = d * std::tan(theta / 2);
        const float hx = ux + wx, hy = uy + wy, hl = std::hypot(hx, hy);
        const float cx = vx + hx / hl * (rr / std::sin(theta / 2)), cy = vy + hy / hl * (rr / std::sin(theta / 2));
        const float t1x = vx + ux * d, t1y = vy + uy * d, t2x = vx + wx * d, t2y = vy + wy * d;
        float a1 = std::atan2(t1y - cy, t1x - cx), a2 = std::atan2(t2y - cy, t2x - cx);
        // the short way round
        float da = a2 - a1;
        while (da > PI) da -= 2 * PI;
        while (da < -PI) da += 2 * PI;
        const int steps = std::max(2, (int)std::ceil(std::fabs(da) * std::sqrt(rr) * 1.2f));
        for (int k = 0; k <= steps; ++k) { const float a = a1 + da * k / steps; out.push_back(cx + std::cos(a) * rr); out.push_back(cy + std::sin(a) * rr); }
    }
    return out;
}

std::vector<float> shapeOutline(const Shape& s) {
    std::vector<float> p;
    const float x0 = std::min(s.x0, s.x1), x1 = std::max(s.x0, s.x1), y0 = std::min(s.y0, s.y1), y1 = std::max(s.y0, s.y1);
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    switch (s.kind) {
    case SHAPE_RECT: p = {x0, y0, x1, y0, x1, y1, x0, y1}; return roundCorners(p, std::min(s.radius, std::min(rx, ry)));
    case SHAPE_ELLIPSE: {
        const int n = std::clamp((int)(std::sqrt(std::max(rx, ry)) * 10), 24, 720);
        for (int i = 0; i < n; ++i) { const float a = 2 * PI * i / n; p.push_back(cx + std::cos(a) * rx); p.push_back(cy + std::sin(a) * ry); }
        return p;
    }
    case SHAPE_TRIANGLE: p = {cx, y0, x1, y1, x0, y1}; return roundCorners(p, s.radius);
    case SHAPE_POLYGON: case SHAPE_STAR: {   // the first point at the top, as PhotoCraft's polygon()
        const int n = std::clamp(s.sides, 3, 100);
        const bool star = s.kind == SHAPE_STAR;
        const int m = star ? n * 2 : n;
        for (int i = 0; i < m; ++i) {
            const float a = -PI / 2 + 2 * PI * i / m, k = star && (i & 1) ? std::clamp(s.inner, 0.05f, 0.95f) : 1.0f;
            p.push_back(cx + std::cos(a) * rx * k); p.push_back(cy + std::sin(a) * ry * k);
        }
        return roundCorners(p, s.radius);
    }
    case SHAPE_PATH: { auto ps = flattenPath(s.path); return ps.empty() ? p : ps[0]; }
    case SHAPE_LINE: {   // a thick line: the rectangle round the segment
        const float dx = s.x1 - s.x0, dy = s.y1 - s.y0, l = std::hypot(dx, dy);
        if (l < 1e-3f) return p;
        const float nx = -dy / l * s.weight / 2, ny = dx / l * s.weight / 2;
        return {s.x0 + nx, s.y0 + ny, s.x1 + nx, s.y1 + ny, s.x1 - nx, s.y1 - ny, s.x0 - nx, s.y0 - ny};
    }
    }
    return p;
}

std::vector<std::vector<float>> flattenPath(const BezierPath& path) {
    std::vector<std::vector<float>> out;
    for (const auto& sp : path) {
        const size_t n = sp.knots.size();
        if (n == 0) continue;
        std::vector<float> poly;
        const size_t segs = sp.closed ? n : n - 1;
        poly.push_back(sp.knots[0].ax); poly.push_back(sp.knots[0].ay);
        for (size_t i = 0; i < segs; ++i) {
            const Knot& a = sp.knots[i]; const Knot& b = sp.knots[(i + 1) % n];
            const float len = std::hypot(a.ox - a.ax, a.oy - a.ay) + std::hypot(b.ix - a.ox, b.iy - a.oy) + std::hypot(b.ax - b.ix, b.ay - b.iy);
            const int steps = std::clamp((int)(len / 3), 1, 200);
            for (int k = 1; k <= steps; ++k) {
                const float t = (float)k / steps, u = 1 - t;
                const float x = u * u * u * a.ax + 3 * u * u * t * a.ox + 3 * u * t * t * b.ix + t * t * t * b.ax;
                const float y = u * u * u * a.ay + 3 * u * u * t * a.oy + 3 * u * t * t * b.iy + t * t * t * b.ay;
                poly.push_back(x); poly.push_back(y);
            }
        }
        if (sp.closed && poly.size() >= 4) { poly.pop_back(); poly.pop_back(); }   // (the last point is the first again)
        out.push_back(poly);
    }
    return out;
}

// the polygons' area coverage per pixel (even-odd): 5 rows per pixel, each span's ends exact along x
void polygonsCoverage(const std::vector<std::vector<float>>& polys, int w, int h, std::vector<float>& cov) {
    cov.assign((size_t)w * h, 0.0f);
    std::vector<float> e;   // edges: x0 y0 x1 y1
    for (const auto& p : polys) {
        const int n = (int)p.size() / 2;
        if (n < 2) continue;
        for (int i = 0; i < n; ++i) { e.push_back(p[i * 2]); e.push_back(p[i * 2 + 1]); e.push_back(p[((i + 1) % n) * 2]); e.push_back(p[((i + 1) % n) * 2 + 1]); }
    }
    if (e.empty()) return;
    float ymin = 1e9f, ymax = -1e9f;
    for (size_t i = 0; i < e.size(); i += 2) { ymin = std::min(ymin, e[i + 1]); ymax = std::max(ymax, e[i + 1]); }
    const int SUB = 5;
    const int r0 = std::max(0, (int)std::floor(ymin)), r1 = std::min(h - 1, (int)std::ceil(ymax));
    std::vector<float> xs;
    for (int y = r0; y <= r1; ++y)
        for (int s = 0; s < SUB; ++s) {
            const float sy = y + (s + 0.5f) / SUB;
            xs.clear();
            for (size_t i = 0; i < e.size(); i += 4) {
                const float ax = e[i], ay = e[i + 1], bx = e[i + 2], by = e[i + 3];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs.push_back(ax + (sy - ay) / (by - ay) * (bx - ax));
            }
            std::sort(xs.begin(), xs.end());
            float* row = &cov[(size_t)y * w];
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                const float a = std::clamp(xs[k], 0.0f, (float)w), b = std::clamp(xs[k + 1], 0.0f, (float)w);
                if (b <= a) continue;
                const int ia = (int)a, ib = (int)b;
                if (ia == ib) { if (ia < w) row[ia] += (b - a) / SUB; continue; }
                row[ia] += (ia + 1 - a) / SUB;
                for (int x = ia + 1; x < ib && x < w; ++x) row[x] += 1.0f / SUB;
                if (ib < w) row[ib] += (b - ib) / SUB;
            }
        }
    for (auto& v : cov) v = std::min(v, 1.0f);
}
static void fillCoverage(const std::vector<float>& p, int w, int h, std::vector<float>& cov) { polygonsCoverage({p}, w, h, cov); }

// the outline's stroke: how close each pixel is to the outline, against half the width (a soft pixel edge)
static void strokeCoverage(const std::vector<float>& p, float width, int w, int h, std::vector<float>& cov, bool closed = true, bool clear = true) {
    if (clear) cov.assign((size_t)w * h, 0.0f);
    const int n = (int)p.size() / 2;
    if (n < 2 || width <= 0) return;
    float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; ++i) { xmin = std::min(xmin, p[i * 2]); xmax = std::max(xmax, p[i * 2]); ymin = std::min(ymin, p[i * 2 + 1]); ymax = std::max(ymax, p[i * 2 + 1]); }
    const float half = width / 2;
    const int x0 = std::max(0, (int)(xmin - half - 2)), x1 = std::min(w - 1, (int)(xmax + half + 2));
    const int y0 = std::max(0, (int)(ymin - half - 2)), y1 = std::min(h - 1, (int)(ymax + half + 2));
    for (int i = 0; i < (closed ? n : n - 1); ++i) {   // each segment marks the pixels near it (its own box only)
        const float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[((i + 1) % n) * 2], by = p[((i + 1) % n) * 2 + 1];
        const float dx = bx - ax, dy = by - ay, l2 = std::max(1e-6f, dx * dx + dy * dy);
        const int sx0 = std::max(x0, (int)(std::min(ax, bx) - half - 2)), sx1 = std::min(x1, (int)(std::max(ax, bx) + half + 2));
        const int sy0 = std::max(y0, (int)(std::min(ay, by) - half - 2)), sy1 = std::min(y1, (int)(std::max(ay, by) + half + 2));
        for (int y = sy0; y <= sy1; ++y)
            for (int x = sx0; x <= sx1; ++x) {
                const float px = x + 0.5f - ax, py = y + 0.5f - ay;
                const float t = std::clamp((px * dx + py * dy) / l2, 0.0f, 1.0f);
                const float d = std::hypot(px - t * dx, py - t * dy);
                const float c = std::clamp(half + 0.5f - d, 0.0f, 1.0f);
                float& o = cov[(size_t)y * w + x];
                o = std::max(o, c);
            }
    }
}

void polylinesStroke(const std::vector<std::vector<float>>& lines, const std::vector<bool>& closed, float width, int w, int h, std::vector<float>& cov) {
    cov.assign((size_t)w * h, 0.0f);
    for (size_t i = 0; i < lines.size(); ++i) strokeCoverage(lines[i], width, w, h, cov, i < closed.size() ? closed[i] : true, false);
}

void shapeCoverage(const Shape& s, int w, int h, std::vector<float>& fill, std::vector<float>& stroke) {
    if (s.kind == SHAPE_PATH) {   // every subpath: filled even-odd (holes), stroked as drawn
        const auto polys = flattenPath(s.path);
        std::vector<bool> closed; for (const auto& sp : s.path) closed.push_back(sp.closed);
        if (s.fillOn) polygonsCoverage(polys, w, h, fill); else fill.assign((size_t)w * h, 0.0f);
        if (s.strokeOn) polylinesStroke(polys, closed, s.strokeWidth, w, h, stroke); else stroke.clear();
        return;
    }
    const std::vector<float> outline = shapeOutline(s);
    if (s.fillOn) fillCoverage(outline, w, h, fill); else fill.assign((size_t)w * h, 0.0f);
    if (s.strokeOn) strokeCoverage(outline, s.strokeWidth, w, h, stroke); else stroke.clear();
}
