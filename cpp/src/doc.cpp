// Chimera - the document: layers, masks, compositing, files and undo.
#include "doc.h"
#include "blend.h"
#include "util.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Rect Rect::clip(int w, int h) const { return {std::max(0, x0), std::max(0, y0), std::min(w, x1), std::min(h, y1)}; }
Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
}

Rect Doc::selBounds() const {
    if (sel.empty()) return {0, 0, w, h};
    Rect r{w, h, 0, 0};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (sel[(size_t)y * w + x]) { r.x0 = std::min(r.x0, x); r.y0 = std::min(r.y0, y); r.x1 = std::max(r.x1, x + 1); r.y1 = std::max(r.y1, y + 1); }
    return r;
}

Doc newDoc(int w, int h, bool white) {
    Doc d;
    d.w = w; d.h = h;
    Layer l;
    l.name = "Background";
    l.img = Image(w, h);
    if (white) std::fill(l.img.px.begin(), l.img.px.end(), (uint8_t)255);
    d.layers.push_back(std::move(l));
    return d;
}

Layer emptyLayer(const Doc& d, const std::string& name) {
    Layer l;
    l.name = name;
    l.img = Image(d.w, d.h);
    return l;
}

void addLayer(Doc& d, Layer l) {
    const int at = d.layers.empty() ? 0 : d.active + 1;
    d.layers.insert(d.layers.begin() + at, std::move(l));
    d.active = at;
    d.editMask = false;
    d.dirty = true;
}

void deleteLayer(Doc& d) {
    if (d.layers.size() <= 1) return;
    d.layers.erase(d.layers.begin() + d.active);
    d.active = std::max(0, d.active - 1);
    d.editMask = false;
    d.dirty = true;
}

void duplicateLayer(Doc& d) {
    Layer c = d.cur();
    c.name += " copy";
    addLayer(d, std::move(c));
}

void moveLayer(Doc& d, int dir) {
    const int to = d.active + dir;
    if (to < 0 || to >= (int)d.layers.size()) return;
    std::swap(d.layers[d.active], d.layers[to]);
    d.active = to;
    d.dirty = true;
}

// ---- fill layers
const char* gradStyleName(int s) {
    static const char* N[GRAD_COUNT] = {"Linear", "Radial", "Angle", "Reflected", "Diamond"};
    return s >= 0 && s < GRAD_COUNT ? N[s] : "?";
}
std::string Fill::save() const {
    char b[256];
    snprintf(b, sizeof b, "fill1 %d %g %g %g %g %g %g %d %g %g %d", kind, colour[0], colour[1], colour[2], colour2[0], colour2[1], colour2[2],
             style, angle, scale, reverse ? 1 : 0);
    return b;
}
bool Fill::load(const std::string& blob) {
    int k = 0, st = 0, rv = 0;
    if (sscanf(blob.c_str(), "fill1 %d %f %f %f %f %f %f %d %f %f %d", &k, &colour[0], &colour[1], &colour[2], &colour2[0], &colour2[1], &colour2[2],
               &st, &angle, &scale, &rv) != 11) return false;
    kind = k; style = std::clamp(st, 0, GRAD_COUNT - 1); reverse = rv != 0;
    return true;
}
// where along the gradient a canvas point is (0..1), PhotoCraft's gradient geometry: linear along the angle, radial
// from the centre, angle round it, reflected (linear mirrored about the centre), diamond (the L1 distance)
static float gradT(const Fill& f, float x, float y, float w, float h) {
    const float cx = w / 2, cy = h / 2, a = f.angle * 3.14159265f / 180;
    const float dx = std::cos(a), dy = -std::sin(a);                       // y down on the canvas
    const float half = 0.5f * (std::fabs(dx) * w + std::fabs(dy) * h) * std::max(0.1f, f.scale);   // the canvas's extent along the angle
    const float px = x - cx, py = y - cy;
    float t = 0;
    switch (f.style) {
    case GRAD_RADIAL: t = std::sqrt(px * px + py * py) / half; break;
    case GRAD_ANGLE: { float ang = std::atan2(-py, px) - a; while (ang < 0) ang += 6.2831853f; while (ang >= 6.2831853f) ang -= 6.2831853f; t = ang / 6.2831853f; break; }
    case GRAD_REFLECTED: t = std::fabs(px * dx + py * dy) / half; break;
    case GRAD_DIAMOND: { const float u = px * dx + py * dy, v = -px * dy + py * dx; t = (std::fabs(u) + std::fabs(v)) / half; break; }
    default: t = ((px * dx + py * dy) / half + 1) / 2; break;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    return f.reverse ? 1 - t : t;
}
void renderFill(const Doc& d, Layer& l) {
    if (!l.isFill()) return;
    if (l.isShape() && (l.x || l.y)) l.shape.move((float)l.x, (float)l.y);   // a moved shape: its place is in its settings
    if (l.isText() && (l.x || l.y)) { l.text.x += l.x; l.text.y += l.y; }    // and a moved text
    if (l.img.w != d.w || l.img.h != d.h || l.x || l.y) {   // a fill covers the canvas (its mask moves with it)
        l.img = Image(d.w, d.h);
        if (l.hasMask && l.mask.size() != (size_t)d.w * d.h) l.mask.assign((size_t)d.w * d.h, 255);
        l.x = l.y = 0;
    }
    const Fill& f = l.fill;
    for (int y = 0; y < d.h; ++y) {
        uint8_t* p = l.img.at(0, y);
        for (int x = 0; x < d.w; ++x, p += 4) {
            float c[3];
            if (f.kind == FILL_SOLID) for (int k = 0; k < 3; ++k) c[k] = f.colour[k];
            else { const float t = gradT(f, x + 0.5f, y + 0.5f, (float)d.w, (float)d.h); for (int k = 0; k < 3; ++k) c[k] = f.colour[k] + (f.colour2[k] - f.colour[k]) * t; }
            p[0] = to8(c[0]); p[1] = to8(c[1]); p[2] = to8(c[2]); p[3] = 255;
        }
    }
    if (l.isText()) {   // text: the fill only where its letters are
        std::vector<float> tc;
        textCoverage(l.text, d.w, d.h, tc, l.text.box);
        for (size_t i = 0; i < tc.size(); ++i) l.img.px[i * 4 + 3] = to8(tc[i]);
        return;
    }
    if (!l.isShape()) return;
    // a shape: the fill only inside it, the stroke over its outline
    std::vector<float> fc, sc;
    shapeCoverage(l.shape, d.w, d.h, fc, sc);
    const float* st = l.shape.stroke;
    for (int y = 0; y < d.h; ++y) {
        uint8_t* p = l.img.at(0, y);
        for (int x = 0; x < d.w; ++x, p += 4) {
            const size_t i = (size_t)y * d.w + x;
            const float a = fc[i], b = sc.empty() ? 0.0f : sc[i];
            const float out = b + a * (1 - b);   // the stroke over the fill
            if (out <= 0) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
            for (int k = 0; k < 3; ++k) p[k] = to8((st[k] * b + p[k] / 255.0f * a * (1 - b)) / out);
            p[3] = to8(out);
        }
    }
}
void addTextLayer(Doc& d, const Text& t, const Fill& f) {
    std::string name = t.str.substr(0, t.str.find('\n'));
    if (name.size() > 30) name = name.substr(0, 30);
    Layer l = emptyLayer(d, name.empty() ? "Text" : name);
    l.fill = f; l.text = t; l.hasText = true;
    renderFill(d, l);
    addLayer(d, std::move(l));
    d.editMask = false;
}
void addShapeLayer(Doc& d, const Shape& s, const Fill& f) {
    int n = 1;   // "Rectangle 3": one more than the shapes of its kind already there
    for (const Layer& l : d.layers) if (l.isShape() && l.shape.kind == s.kind) ++n;
    Layer l = emptyLayer(d, std::string(shapeName(s.kind)) + " " + std::to_string(n));
    l.fill = f; l.shape = s;
    renderFill(d, l);
    addLayer(d, std::move(l));
    d.editMask = false;
}
void addFillLayer(Doc& d, const Fill& f) {
    Layer l = emptyLayer(d, f.kind == FILL_SOLID ? "Color Fill" : "Gradient Fill");
    l.fill = f;
    renderFill(d, l);
    addLayer(d, std::move(l));
    addMask(d, d.hasSel() ? 2 : 0);
    d.editMask = false;
}
int clipBase(const Doc& d, int i) {
    while (i > 0 && d.layers[i].clip) --i;
    return i;
}

void addAdjustmentLayer(Doc& d, int kind) {
    Layer l = emptyLayer(d, adjName(kind));
    l.adj = Adjustment::make(kind);
    addLayer(d, std::move(l));
    addMask(d, d.hasSel() ? 2 : 0);
    d.editMask = false;
}

void expandToCanvas(Doc& d, Layer& l) {
    const int x0 = std::min(0, l.x), y0 = std::min(0, l.y);
    const int x1 = std::max(d.w, l.x + l.img.w), y1 = std::max(d.h, l.y + l.img.h);
    if (x0 == l.x && y0 == l.y && x1 - x0 == l.img.w && y1 - y0 == l.img.h) return;
    Image n(x1 - x0, y1 - y0);
    std::vector<uint8_t> m;
    if (l.hasMask) m.assign((size_t)n.w * n.h, 255);
    for (int y = 0; y < l.img.h; ++y) {
        memcpy(n.at(l.x - x0, y + l.y - y0), l.img.at(0, y), (size_t)l.img.w * 4);
        if (l.hasMask) memcpy(&m[(size_t)(y + l.y - y0) * n.w + (l.x - x0)], &l.mask[(size_t)y * l.img.w], l.img.w);
    }
    l.img = std::move(n);
    l.mask = std::move(m);
    l.x = x0; l.y = y0;
}

// one layer onto a float RGBA buffer covering the rect r (row length r.x1 - r.x0)
static void drawLayer(const Layer& l, std::vector<float>& buf, Rect r, int mode, float opacity, const float* cov = nullptr) {
    const int lx0 = std::max(r.x0, l.x), ly0 = std::max(r.y0, l.y);
    const int lx1 = std::min(r.x1, l.x + l.img.w), ly1 = std::min(r.y1, l.y + l.img.h);
    const int rw = r.x1 - r.x0;
    const bool masked = l.hasMask && l.maskOn;
    for (int y = ly0; y < ly1; ++y) {
        float* b = &buf[((size_t)(y - r.y0) * rw + (lx0 - r.x0)) * 4];
        const uint8_t* p = l.img.at(lx0 - l.x, y - l.y);
        const uint8_t* m = masked ? &l.mask[(size_t)(y - l.y) * l.img.w + (lx0 - l.x)] : nullptr;
        for (int x = lx0; x < lx1; ++x, b += 4, p += 4) {
            float mk = m ? *m++ / 255.0f : 1.0f;
            if (cov) mk *= cov[(size_t)(y - r.y0) * rw + (x - r.x0)];   // clipped: only where its base shows
            if (!p[3] || mk <= 0.0f) continue;
            const float s[4] = {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f * mk * opacity};
            compositePixel(mode, b, s, x, y);
        }
    }
}

// an adjustment layer over the float buffer: what's below, adjusted, then blended back through the layer's mask and
// opacity with its blend mode; transparency is left as it was
static void drawAdjust(const Layer& l, std::vector<float>& buf, Rect r, int mode, float opacity, const float* cov = nullptr) {
    const int lx0 = std::max(r.x0, l.x), ly0 = std::max(r.y0, l.y);
    const int lx1 = std::min(r.x1, l.x + l.img.w), ly1 = std::min(r.y1, l.y + l.img.h);
    if (lx1 <= lx0 || ly1 <= ly0) return;
    AdjPrepared* prep = adjPrepare(l.adj);
    const int rw = r.x1 - r.x0;
    const bool masked = l.hasMask && l.maskOn;
    for (int y = ly0; y < ly1; ++y) {
        float* b = &buf[((size_t)(y - r.y0) * rw + (lx0 - r.x0)) * 4];
        const uint8_t* m = masked ? &l.mask[(size_t)(y - l.y) * l.img.w + (lx0 - l.x)] : nullptr;
        for (int x = lx0; x < lx1; ++x, b += 4) {
            float mk = (m ? *m++ / 255.0f : 1.0f) * opacity;
            if (cov) mk *= cov[(size_t)(y - r.y0) * rw + (x - r.x0)];
            if (b[3] <= 0.0f || mk <= 0.0f) continue;
            float c[3] = {b[0], b[1], b[2]};
            adjApply(l.adj, prep, c, x, y);
            const float s[4] = {c[0], c[1], c[2], mk};
            const float a = b[3];
            compositePixel(mode, b, s, x, y);
            b[3] = a;
        }
    }
    adjFree(prep);
}

void composite(const Doc& d, Image& out, Rect r, bool showMask) {
    if (out.w != d.w || out.h != d.h) out = Image(d.w, d.h);
    r = r.clip(d.w, d.h);
    if (r.empty()) return;
    if (showMask && d.layers[d.active].hasMask) {
        // the active layer's mask in grey: what you paint is what you see
        const Layer& l = d.layers[d.active];
        for (int y = r.y0; y < r.y1; ++y)
            for (int x = r.x0; x < r.x1; ++x) {
                const int lx = x - l.x, ly = y - l.y;
                const uint8_t v = (lx >= 0 && ly >= 0 && lx < l.img.w && ly < l.img.h) ? l.mask[(size_t)ly * l.img.w + lx] : 0;
                uint8_t* o = out.at(x, y);
                o[0] = o[1] = o[2] = v; o[3] = 255;
            }
        return;
    }
    const int rw = r.x1 - r.x0;
    std::vector<float> buf((size_t)rw * (r.y1 - r.y0) * 4, 0.0f);
    std::vector<float> cov;   // the current base's coverage (alpha x mask), for the layers clipped to it
    bool baseShown = true;
    for (size_t i = 0; i < d.layers.size(); ++i) {
        const Layer& l = d.layers[i];
        if (!l.clip) {
            baseShown = l.visible && l.opacity > 0.0f;
            const bool clippedAbove = i + 1 < d.layers.size() && d.layers[i + 1].clip;
            if (clippedAbove && baseShown) {   // where the base shows: its pixels' alpha (an adjustment's: its mask)
                cov.assign((size_t)rw * (r.y1 - r.y0), 0.0f);
                const int lx0 = std::max(r.x0, l.x), ly0 = std::max(r.y0, l.y), lx1 = std::min(r.x1, l.x + l.img.w), ly1 = std::min(r.y1, l.y + l.img.h);
                for (int y = ly0; y < ly1; ++y)
                    for (int x = lx0; x < lx1; ++x)
                        cov[(size_t)(y - r.y0) * rw + (x - r.x0)] = (l.isAdjust() ? 1.0f : l.img.at(x - l.x, y - l.y)[3] / 255.0f) * l.maskAt(x - l.x, y - l.y) / 255.0f;
            }
        }
        if (!l.visible || l.opacity <= 0.0f) continue;
        if (l.clip && (!baseShown || i == 0)) continue;   // a hidden base hides what's clipped to it
        const float* c = l.clip && i > 0 ? cov.data() : nullptr;
        if (l.isAdjust()) drawAdjust(l, buf, r, l.blend, l.opacity, c);
        else drawLayer(l, buf, r, l.blend, l.opacity, c);
    }
    for (int y = r.y0; y < r.y1; ++y) {
        const float* b = &buf[(size_t)(y - r.y0) * rw * 4];
        uint8_t* o = out.at(r.x0, y);
        for (int i = 0; i < rw * 4; ++i) o[i] = to8(b[i]);
    }
}

// merge layer `top` into `bottom` (both canvas-covering), keeping bottom's blend
static void mergeInto(Doc& d, Layer& bottom, const Layer& top) {
    expandToCanvas(d, bottom);
    if (top.isAdjust()) {
        AdjPrepared* prep = adjPrepare(top.adj);
        for (int y = 0; y < bottom.img.h; ++y)
            for (int x = 0; x < bottom.img.w; ++x) {
                const int cx = x + bottom.x, cy = y + bottom.y, tx = cx - top.x, ty = cy - top.y;
                if (tx < 0 || ty < 0 || tx >= top.img.w || ty >= top.img.h) continue;
                uint8_t* q = bottom.img.at(x, y);
                if (!q[3]) continue;
                const float mk = top.maskAt(tx, ty) / 255.0f * top.opacity * (bottom.hasMask && bottom.maskOn ? bottom.mask[(size_t)y * bottom.img.w + x] / 255.0f : 1.0f);
                if (mk <= 0) continue;
                float b[4] = {q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f, 1.0f};
                float c[3] = {b[0], b[1], b[2]};
                adjApply(top.adj, prep, c, cx, cy);
                const float s[4] = {c[0], c[1], c[2], mk};
                compositePixel(top.blend, b, s, cx, cy);
                for (int i = 0; i < 3; ++i) q[i] = to8(b[i]);
            }
        adjFree(prep);
        return;
    }
    if (bottom.hasMask) { // bake the bottom's mask first, so what you see is what you get
        for (size_t i = 0; i < bottom.mask.size(); ++i) bottom.img.px[i * 4 + 3] = (uint8_t)(bottom.img.px[i * 4 + 3] * bottom.mask[i] / 255);
        bottom.hasMask = false; bottom.mask.clear();
    }
    const int W = bottom.img.w, H = bottom.img.h;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int cx = x + bottom.x, cy = y + bottom.y;
            const int tx = cx - top.x, ty = cy - top.y;
            if (tx < 0 || ty < 0 || tx >= top.img.w || ty >= top.img.h) continue;
            const uint8_t* p = top.img.at(tx, ty);
            if (!p[3]) continue;
            uint8_t* q = bottom.img.at(x, y);
            float b[4] = {q[0] / 255.0f, q[1] / 255.0f, q[2] / 255.0f, q[3] / 255.0f * bottom.opacity};
            const float s[4] = {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f * top.maskAt(tx, ty) / 255.0f * top.opacity};
            compositePixel(top.blend, b, s, cx, cy);
            float a = bottom.opacity > 0 ? std::min(1.0f, b[3] / bottom.opacity) : b[3];
            if (top.clip) a = q[3] / 255.0f;   // clipped: it only ever showed where the base did
            for (int i = 0; i < 3; ++i) q[i] = to8(b[i]);
            q[3] = to8(a);
        }
}

void mergeDown(Doc& d) {
    if (d.active == 0 || d.layers[d.active - 1].isAdjust()) return;   // (Photoshop won't merge onto an adjustment layer either)
    Layer top = d.layers[d.active];
    mergeInto(d, d.layers[d.active - 1], top);
    d.layers.erase(d.layers.begin() + d.active);
    d.active--;
    d.editMask = false;
    d.dirty = true;
}

void mergeVisible(Doc& d) {
    Layer m = emptyLayer(d, "Merged");
    {   // what you see, composited (clipping masks, adjustments and all)
        Doc vis = d;
        vis.layers.clear();
        for (const Layer& l : d.layers) if (l.visible) vis.layers.push_back(l);
        if (!vis.layers.empty()) { vis.layers[0].clip = false; composite(vis, m.img, {0, 0, d.w, d.h}, false); }
    }
    std::vector<Layer> keep;
    for (Layer& l : d.layers) if (!l.visible) keep.push_back(std::move(l));
    keep.push_back(std::move(m));
    d.layers = std::move(keep);
    d.active = (int)d.layers.size() - 1;
    d.editMask = false;
    d.dirty = true;
}

void flatten(Doc& d) {
    Image out;
    composite(d, out, {0, 0, d.w, d.h}, false);
    Layer l;
    l.name = "Background";
    l.img = std::move(out);
    d.layers.clear();
    d.layers.push_back(std::move(l));
    d.active = 0;
    d.editMask = false;
    d.dirty = true;
}

// ---- masks
void addMask(Doc& d, int how) {
    Layer& l = d.cur();
    l.hasMask = true; l.maskOn = true;
    l.mask.assign((size_t)l.img.w * l.img.h, how == 1 ? 0 : 255);
    if (how == 2 && d.hasSel())
        for (int y = 0; y < l.img.h; ++y)
            for (int x = 0; x < l.img.w; ++x) {
                const int cx = x + l.x, cy = y + l.y;
                l.mask[(size_t)y * l.img.w + x] = (cx >= 0 && cy >= 0 && cx < d.w && cy < d.h) ? d.sel[(size_t)cy * d.w + cx] : 0;
            }
    d.editMask = true;
    d.dirty = true;
}
void deleteMask(Doc& d) { Layer& l = d.cur(); l.hasMask = false; l.mask.clear(); d.editMask = false; d.dirty = true; }
void applyMask(Doc& d) {
    Layer& l = d.cur();
    if (!l.hasMask) return;
    for (size_t i = 0; i < l.mask.size(); ++i) l.img.px[i * 4 + 3] = (uint8_t)(l.img.px[i * 4 + 3] * l.mask[i] / 255);
    deleteMask(d);
}
void invertMask(Doc& d) { Layer& l = d.cur(); if (!l.hasMask) return; for (auto& v : l.mask) v = 255 - v; d.dirty = true; }
void selectionFromMask(Doc& d) {
    const Layer& l = d.cur();
    d.sel.assign((size_t)d.w * d.h, 0);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            const int lx = x - l.x, ly = y - l.y;
            if (lx < 0 || ly < 0 || lx >= l.img.w || ly >= l.img.h) continue;
            d.sel[(size_t)y * d.w + x] = l.hasMask ? l.mask[(size_t)ly * l.img.w + lx] : l.img.at(lx, ly)[3];
        }
}

// ---- files
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool endsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && lower(s.substr(s.size() - e.size())) == e; }

bool loadImage(const std::string& path, Image& out, std::string& err) {
    int w, h, n;
    FILE* f = openFile(path, "rb");
    if (!f) { err = "Can't open " + path; return false; }
    uint8_t* data = stbi_load_from_file(f, &w, &h, &n, 4);
    fclose(f);
    if (!data) { err = std::string("Can't read the picture: ") + stbi_failure_reason(); return false; }
    out = Image(w, h);
    memcpy(out.px.data(), data, out.px.size());
    stbi_image_free(data);
    return true;
}

static void putU32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((uint8_t)(v >> (8 * i))); }
static void putF(std::vector<uint8_t>& o, float v) { uint32_t u; memcpy(&u, &v, 4); putU32(o, u); }
static void putBytes(std::vector<uint8_t>& o, const uint8_t* p, size_t n) { putU32(o, (uint32_t)n); o.insert(o.end(), p, p + n); }
struct Reader {
    const std::vector<uint8_t>& b; size_t i = 0; bool ok = true;
    uint32_t u32() { if (i + 4 > b.size()) { ok = false; return 0; } uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t)b[i++] << (8 * k); return v; }
    int32_t i32() { return (int32_t)u32(); }
    float f() { uint32_t u = u32(); float v; memcpy(&v, &u, 4); return v; }
    std::vector<uint8_t> bytes() { uint32_t n = u32(); if (!ok || i + n > b.size()) { ok = false; return {}; } std::vector<uint8_t> v(b.begin() + i, b.begin() + i + n); i += n; return v; }
};

static bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
static bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    f.write((const char*)data.data(), (std::streamsize)data.size());
    return (bool)f;
}

bool openDoc(const std::string& path, Doc& d, std::string& err) {
    if (endsWith(path, ".chimera") || endsWith(path, ".ltk")) {
        std::vector<uint8_t> b;
        if (!readFile(path, b)) { err = "Can't open " + path; return false; }
        Reader r{b};
        if (b.size() < 4 || memcmp(b.data(), "LTK1", 4)) { err = "Not a Chimera project"; return false; }
        r.i = 4;
        Doc n;
        n.w = r.i32(); n.h = r.i32();
        const int count = r.i32();
        n.active = r.i32();
        for (int k = 0; k < count && r.ok; ++k) {
            Layer l;
            auto name = r.bytes(); l.name.assign(name.begin(), name.end());
            l.x = r.i32(); l.y = r.i32();
            l.opacity = r.f(); l.blend = r.i32();
            const uint32_t flags = r.u32();
            l.visible = flags & 1; l.hasMask = flags & 2; l.maskOn = flags & 4;
            l.label = (flags >> 8) & 15;                 // the colour tag (projects from before it: 0, none)
            auto png = r.bytes();
            int w, h, c;
            uint8_t* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &c, 4);
            if (!px) { err = "A layer in the project is damaged"; return false; }
            l.img = Image(w, h); memcpy(l.img.px.data(), px, l.img.px.size()); stbi_image_free(px);
            if (l.hasMask) {
                auto mp = r.bytes();
                uint8_t* m = stbi_load_from_memory(mp.data(), (int)mp.size(), &w, &h, &c, 1);
                if (!m || w != l.img.w || h != l.img.h) { err = "A mask in the project is damaged"; return false; }
                l.mask.assign(m, m + (size_t)w * h); stbi_image_free(m);
            }
            if (flags & (1u << 12)) {   // an adjustment layer's settings
                auto ab = r.bytes();
                if (!l.adj.load(std::string(ab.begin(), ab.end()))) { err = "An adjustment layer in the project is damaged"; return false; }
            }
            l.clip = (flags & (1u << 13)) != 0;
            if (flags & (1u << 14)) {   // a fill layer's settings
                auto fb = r.bytes();
                if (!l.fill.load(std::string(fb.begin(), fb.end()))) { err = "A fill layer in the project is damaged"; return false; }
            }
            if (flags & (1u << 15)) {   // a shape layer's shape
                auto sb = r.bytes();
                if (!l.shape.load(std::string(sb.begin(), sb.end()))) { err = "A shape layer in the project is damaged"; return false; }
            }
            if (flags & (1u << 16)) {   // a text layer's words and type
                auto tb = r.bytes();
                if (!l.text.load(std::string(tb.begin(), tb.end()))) { err = "A text layer in the project is damaged"; return false; }
                l.hasText = true;
            }
            n.layers.push_back(std::move(l));
        }
        // Newer projects append their user slices after the layer data; older files end here.
        if (r.ok && r.i < b.size()) {
            if (r.i + 8 > b.size() || memcmp(b.data() + r.i, "SLI1", 4)) {
                err = "The project has an unknown extension";
                return false;
            }
            r.i += 4;
            const uint32_t slices = r.u32();
            if (slices > 100000) { err = "The project has too many slices"; return false; }
            for (uint32_t i = 0; i < slices && r.ok; ++i) {
                Slice s;
                s.id = r.i32(); s.rect.x0 = r.i32(); s.rect.y0 = r.i32(); s.rect.x1 = r.i32(); s.rect.y1 = r.i32();
                auto name = r.bytes(); s.name.assign(name.begin(), name.end());
                if (s.rect.empty()) { r.ok = false; break; }
                n.slices.push_back(std::move(s));
            }
        }
        if (!r.ok || n.layers.empty()) { err = "The project is damaged"; return false; }
        n.active = std::clamp(n.active, 0, (int)n.layers.size() - 1);
        n.path = path;
        d = std::move(n);
        return true;
    }
    Image img;
    if (!loadImage(path, img, err)) return false;
    d = newDoc(img.w, img.h, false);
    d.layers[0].img = std::move(img);
    d.path = path;
    return true;
}

static void toVec(void* ctx, void* data, int size) { auto* v = (std::vector<uint8_t>*)ctx; v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size); }

// a square of side n with the picture fitted in its middle (transparent round it), averaged down (or bilinear up)
static std::vector<uint8_t> iconSquare(const Image& src, int n) {
    std::vector<uint8_t> o((size_t)n * n * 4, 0);
    const float k = (float)n / std::max(src.w, src.h);
    const float dw = src.w * k, dh = src.h * k, ox = (n - dw) / 2, oy = (n - dh) / 2;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            // the source area under this pixel (at least one source pixel: bilinear-ish when enlarging)
            const float sx0 = (x - ox) / k, sx1 = (x + 1 - ox) / k, sy0 = (y - oy) / k, sy1 = (y + 1 - oy) / k;
            if (sx1 <= 0 || sy1 <= 0 || sx0 >= src.w || sy0 >= src.h) continue;
            const int x0 = std::max(0, (int)std::floor(sx0)), x1 = std::min(src.w, std::max(x0 + 1, (int)std::ceil(sx1)));
            const int y0 = std::max(0, (int)std::floor(sy0)), y1 = std::min(src.h, std::max(y0 + 1, (int)std::ceil(sy1)));
            double acc[4] = {0, 0, 0, 0}; double cnt = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const uint8_t* q = &src.px[((size_t)yy * src.w + xx) * 4];
                    const double a = q[3] / 255.0;
                    for (int c = 0; c < 3; ++c) acc[c] += q[c] * a;   // (colours weighted by alpha: no dark fringes)
                    acc[3] += q[3]; cnt += 1;
                }
            uint8_t* t = &o[((size_t)y * n + x) * 4];
            const double alpha = acc[3] / cnt;
            for (int c = 0; c < 3; ++c) t[c] = alpha > 0 ? (uint8_t)std::min(255.0, acc[c] / cnt / (alpha / 255.0) + 0.5) : 0;
            t[3] = (uint8_t)(alpha + 0.5);
        }
    return o;
}
// Windows' .ico: 16 to 256 px, each a PNG inside (as icons have been since Vista)
static bool iconData(const Image& img, std::vector<uint8_t>& out) {
    static const int SIZES[] = {16, 24, 32, 48, 64, 128, 256};
    const int n = (int)(sizeof SIZES / sizeof SIZES[0]);
    std::vector<std::vector<uint8_t>> pngs(n);
    for (int i = 0; i < n; ++i) {
        const std::vector<uint8_t> sq = iconSquare(img, SIZES[i]);
        if (!stbi_write_png_to_func(toVec, &pngs[i], SIZES[i], SIZES[i], 4, sq.data(), SIZES[i] * 4)) return false;
    }
    auto u16 = [&](uint32_t v) { out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16(v & 0xffff); u16(v >> 16); };
    out.clear(); u16(0); u16(1); u16((uint32_t)n);
    uint32_t offset = 6 + 16 * n;
    for (int i = 0; i < n; ++i) {
        out.push_back((uint8_t)(SIZES[i] >= 256 ? 0 : SIZES[i])); out.push_back((uint8_t)(SIZES[i] >= 256 ? 0 : SIZES[i]));
        out.push_back(0); out.push_back(0); u16(1); u16(32); u32((uint32_t)pngs[i].size()); u32(offset);
        offset += (uint32_t)pngs[i].size();
    }
    for (const auto& p : pngs) out.insert(out.end(), p.begin(), p.end());
    return true;
}

bool saveImage(const Doc& d, const std::string& path, std::string& err) {
    Image out;
    composite(d, out, {0, 0, d.w, d.h}, false);
    std::vector<uint8_t> data;
    int ok;
    if (endsWith(path, ".ico")) {
        ok = iconData(out, data);
    } else if (endsWith(path, ".jpg") || endsWith(path, ".jpeg")) {
        // JPG has no transparency: onto white
        Image rgb(d.w, d.h);
        std::vector<uint8_t> px((size_t)d.w * d.h * 3);
        for (size_t i = 0; i < (size_t)d.w * d.h; ++i) {
            const float a = out.px[i * 4 + 3] / 255.0f;
            for (int c = 0; c < 3; ++c) px[i * 3 + c] = (uint8_t)fround(out.px[i * 4 + c] * a + 255.0f * (1.0f - a));
        }
        ok = stbi_write_jpg_to_func(toVec, &data, d.w, d.h, 3, px.data(), 95);
    } else {
        ok = stbi_write_png_to_func(toVec, &data, d.w, d.h, 4, out.px.data(), d.w * 4);
    }
    if (!ok || !writeFile(path, data)) { err = "Can't write " + path; return false; }
    return true;
}

bool saveProject(const Doc& d, const std::string& path, std::string& err) {
    std::vector<uint8_t> o = {'L', 'T', 'K', '1'};
    putU32(o, d.w); putU32(o, d.h); putU32(o, (uint32_t)d.layers.size()); putU32(o, d.active);
    for (const Layer& l : d.layers) {
        putBytes(o, (const uint8_t*)l.name.data(), l.name.size());
        putU32(o, (uint32_t)l.x); putU32(o, (uint32_t)l.y);
        putF(o, l.opacity); putU32(o, l.blend);
        putU32(o, (l.visible ? 1 : 0) | (l.hasMask ? 2 : 0) | (l.maskOn ? 4 : 0) | ((uint32_t)(l.label & 15) << 8) | (l.isAdjust() ? 1u << 12 : 0) | (l.clip ? 1u << 13 : 0) | (l.isFill() ? 1u << 14 : 0) | (l.isShape() ? 1u << 15 : 0) | (l.isText() ? 1u << 16 : 0));
        std::vector<uint8_t> png;
        stbi_write_png_to_func(toVec, &png, l.img.w, l.img.h, 4, l.img.px.data(), l.img.w * 4);
        putBytes(o, png.data(), png.size());
        if (l.hasMask) {
            std::vector<uint8_t> mp;
            stbi_write_png_to_func(toVec, &mp, l.img.w, l.img.h, 1, l.mask.data(), l.img.w);
            putBytes(o, mp.data(), mp.size());
        }
        if (l.isAdjust()) { const std::string a = l.adj.save(); putBytes(o, (const uint8_t*)a.data(), a.size()); }
        if (l.isFill()) { const std::string f = l.fill.save(); putBytes(o, (const uint8_t*)f.data(), f.size()); }
        if (l.isShape()) { const std::string sh = l.shape.save(); putBytes(o, (const uint8_t*)sh.data(), sh.size()); }
        if (l.isText()) { const std::string tx = l.text.save(); putBytes(o, (const uint8_t*)tx.data(), tx.size()); }
    }
    o.insert(o.end(), {'S', 'L', 'I', '1'});
    putU32(o, (uint32_t)d.slices.size());
    for (const Slice& s : d.slices) {
        putU32(o, (uint32_t)s.id); putU32(o, (uint32_t)s.rect.x0); putU32(o, (uint32_t)s.rect.y0);
        putU32(o, (uint32_t)s.rect.x1); putU32(o, (uint32_t)s.rect.y1);
        putBytes(o, (const uint8_t*)s.name.data(), s.name.size());
    }
    if (!writeFile(path, o)) { err = "Can't write " + path; return false; }
    return true;
}

// ---- undo
static const size_t UNDO_STEPS = 20;
void History::push(const Doc& d, const std::string& what) {
    undo.push_back(d); undoNames.push_back(what);
    if (undo.size() > UNDO_STEPS) { undo.erase(undo.begin()); undoNames.erase(undoNames.begin()); }
    redo.clear(); redoNames.clear();
}
void History::doUndo(Doc& d) {
    if (undo.empty()) return;
    redo.push_back(std::move(d)); redoNames.push_back(undoNames.back());
    d = std::move(undo.back()); undo.pop_back(); undoNames.pop_back();
    d.dirty = true;
}
void History::doRedo(Doc& d) {
    if (redo.empty()) return;
    undo.push_back(std::move(d)); undoNames.push_back(redoNames.back());
    d = std::move(redo.back()); redo.pop_back(); redoNames.pop_back();
    d.dirty = true;
}
