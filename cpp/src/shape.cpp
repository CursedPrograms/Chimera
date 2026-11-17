// Chimera - shape layers (see shape.h)
#include "shape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

static const float PI = 3.14159265f;

const char* shapeName(int k) {
    static const char* n[] = {"Shape", "Rectangle", "Ellipse", "Triangle", "Polygon", "Star", "Line"};
    return k >= 0 && k < SHAPE_COUNT ? n[k] : "Shape";
}

std::string Shape::save() const {
    char b[320];
    snprintf(b, sizeof b, "shape1 %d %g %g %g %g %g %d %g %g %d %d %g %g %g %g", kind, x0, y0, x1, y1, radius, sides, inner, weight,
             fillOn ? 1 : 0, strokeOn ? 1 : 0, stroke[0], stroke[1], stroke[2], strokeWidth);
    return b;
}
bool Shape::load(const std::string& blob) {
    int k = 0, f = 1, st = 0;
    if (sscanf(blob.c_str(), "shape1 %d %f %f %f %f %f %d %f %f %d %d %f %f %f %f", &k, &x0, &y0, &x1, &y1, &radius, &sides, &inner, &weight,
               &f, &st, &stroke[0], &stroke[1], &stroke[2], &strokeWidth) != 15) return false;
    kind = std::clamp(k, 0, SHAPE_COUNT - 1); fillOn = f != 0; strokeOn = st != 0;
    sides = std::clamp(sides, 3, 100);
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
    case SHAPE_LINE: {   // a thick line: the rectangle round the segment
        const float dx = s.x1 - s.x0, dy = s.y1 - s.y0, l = std::hypot(dx, dy);
        if (l < 1e-3f) return p;
        const float nx = -dy / l * s.weight / 2, ny = dx / l * s.weight / 2;
        return {s.x0 + nx, s.y0 + ny, s.x1 + nx, s.y1 + ny, s.x1 - nx, s.y1 - ny, s.x0 - nx, s.y0 - ny};
    }
    }
    return p;
}

// the polygon's area coverage per pixel (even-odd): 5 rows per pixel, each span's ends exact along x
static void fillCoverage(const std::vector<float>& p, int w, int h, std::vector<float>& cov) {
    cov.assign((size_t)w * h, 0.0f);
    const int n = (int)p.size() / 2;
    if (n < 3) return;
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; ++i) { ymin = std::min(ymin, p[i * 2 + 1]); ymax = std::max(ymax, p[i * 2 + 1]); }
    const int SUB = 5;
    const int r0 = std::max(0, (int)std::floor(ymin)), r1 = std::min(h - 1, (int)std::ceil(ymax));
    std::vector<float> xs;
    for (int y = r0; y <= r1; ++y)
        for (int s = 0; s < SUB; ++s) {
            const float sy = y + (s + 0.5f) / SUB;
            xs.clear();
            for (int i = 0; i < n; ++i) {
                const float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[((i + 1) % n) * 2], by = p[((i + 1) % n) * 2 + 1];
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

// the outline's stroke: how close each pixel is to the closed outline, against half the width (a soft pixel edge)
static void strokeCoverage(const std::vector<float>& p, float width, int w, int h, std::vector<float>& cov) {
    cov.assign((size_t)w * h, 0.0f);
    const int n = (int)p.size() / 2;
    if (n < 2 || width <= 0) return;
    float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; ++i) { xmin = std::min(xmin, p[i * 2]); xmax = std::max(xmax, p[i * 2]); ymin = std::min(ymin, p[i * 2 + 1]); ymax = std::max(ymax, p[i * 2 + 1]); }
    const float half = width / 2;
    const int x0 = std::max(0, (int)(xmin - half - 2)), x1 = std::min(w - 1, (int)(xmax + half + 2));
    const int y0 = std::max(0, (int)(ymin - half - 2)), y1 = std::min(h - 1, (int)(ymax + half + 2));
    for (int i = 0; i < n; ++i) {   // each segment marks the pixels near it (its own box only)
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

void shapeCoverage(const Shape& s, int w, int h, std::vector<float>& fill, std::vector<float>& stroke) {
    const std::vector<float> outline = shapeOutline(s);
    if (s.fillOn) fillCoverage(outline, w, h, fill); else fill.assign((size_t)w * h, 0.0f);
    if (s.strokeOn) strokeCoverage(outline, s.strokeWidth, w, h, stroke); else stroke.clear();
}
