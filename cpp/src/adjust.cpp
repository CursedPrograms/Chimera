// Chimera - adjustments (see adjust.h). Ported from PhotoCraft (MIT OR Apache-2.0): crates/compose/src/adjust.rs,
// adjust/tone.rs, adjust/color.rs, adjust/channels.rs and crates/doc/src/adjust.rs; 8-bit RGB documents.
#include "adjust.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static const int LUT = 4096;

const char* adjName(int k) {
    static const char* n[] = {"None", "Brightness/Contrast", "Levels", "Curves", "Exposure", "Vibrance", "Hue/Saturation",
                              "Color Balance", "Black & White", "Photo Filter", "Channel Mixer", "Invert", "Posterize",
                              "Threshold", "Gradient Map", "Selective Color"};
    return k >= 0 && k < ADJ_COUNT ? n[k] : "?";
}

Adjustment Adjustment::make(int kind) {
    Adjustment a;
    a.kind = kind;
    for (int i = 0; i < 6; ++i) { const float c = 60.0f * i; a.ranges[i].bounds[0] = c - 45; a.ranges[i].bounds[1] = c - 15; a.ranges[i].bounds[2] = c + 15; a.ranges[i].bounds[3] = c + 45; }
    switch (kind) {
        case ADJ_EXPOSURE: a.v[2] = 1.0f; break;
        case ADJ_PHOTO_FILTER: a.v[0] = 0.25f; a.flag = true; break;
        case ADJ_POSTERIZE: a.v[0] = 4; break;
        case ADJ_THRESHOLD: a.v[0] = 128.0f / 255.0f; break;
        case ADJ_GRADIENT_MAP: a.stops = {{0, 0, 0, 0}, {1, 1, 1, 1}}; break;
        case ADJ_COLOR_BALANCE: a.flag = true; break;
        case ADJ_SELECTIVE_COLOR: a.flag = true; break;
        default: break;
    }
    return a;
}

// ---------- saving ----------
std::string Adjustment::save() const {
    std::string o;
    auto put = [&](const void* p, size_t n) { o.append((const char*)p, n); };
    const uint32_t version = 1;
    put(&version, 4); put(&kind, 4); put(v, sizeof v);
    const uint8_t f[2] = {(uint8_t)flag, (uint8_t)flag2}; put(f, 2);
    put(levels, sizeof levels);
    for (const auto& c : curve) { const uint32_t n = (uint32_t)c.size(); put(&n, 4); if (n) put(c.data(), n * 4); }
    put(ranges, sizeof ranges); put(balance, sizeof balance); put(bw, sizeof bw); put(tint, sizeof tint);
    put(color, sizeof color); put(mixer, sizeof mixer);
    const uint32_t ns = (uint32_t)stops.size(); put(&ns, 4); if (ns) put(stops.data(), ns * 16);
    put(selective, sizeof selective);
    return o;
}

bool Adjustment::load(const std::string& b) {
    size_t at = 0;
    auto get = [&](void* p, size_t n) { if (at + n > b.size()) return false; memcpy(p, b.data() + at, n); at += n; return true; };
    uint32_t version = 0;
    if (!get(&version, 4) || version != 1 || !get(&kind, 4) || !get(v, sizeof v)) return false;
    uint8_t f[2]; if (!get(f, 2)) return false; flag = f[0]; flag2 = f[1];
    if (!get(levels, sizeof levels)) return false;
    for (auto& c : curve) { uint32_t n; if (!get(&n, 4) || n > 1024) return false; c.resize(n); if (n && !get(c.data(), n * 4)) return false; }
    if (!get(ranges, sizeof ranges) || !get(balance, sizeof balance) || !get(bw, sizeof bw) || !get(tint, sizeof tint)) return false;
    if (!get(color, sizeof color) || !get(mixer, sizeof mixer)) return false;
    uint32_t ns; if (!get(&ns, 4) || ns > 256) return false; stops.resize(ns); if (ns && !get(stops.data(), ns * 16)) return false;
    return get(selective, sizeof selective);
}

// ---------- tone (tone.rs) ----------
static float lut(const std::vector<float>& t, float v) {
    const float x = std::clamp(v, 0.0f, 1.0f) * (t.size() - 1);
    const size_t i = (size_t)x, j = std::min(i + 1, t.size() - 1);
    const float f = x - i;
    return t[i] * (1 - f) + t[j] * f;
}

static const float BRIGHTNESS_KNEE = 126.0f / 255.0f;
struct Brightness {
    bool raise; float s, e, x0, alpha; bool hermite;
    explicit Brightness(float brightness) {
        const float b = std::fabs(brightness);
        s = std::pow(1.375f, b / 50.0f);
        e = 1.0f / (1.0f + 0.084f * b);
        x0 = std::min(BRIGHTNESS_KNEE / s, 1.0f);
        const float secant = (1.0f - BRIGHTNESS_KNEE) / std::max(1.0f - x0, 1e-6f);
        alpha = s / secant;
        const float beta = e / secant;
        raise = brightness > 0;
        hermite = alpha * alpha + beta * beta <= 9.0f;
    }
    float raised(float x) const {
        const float y0 = BRIGHTNESS_KNEE;
        if (x <= x0) return std::min(y0, s * x);
        const float h = std::max(1.0f - x0, 1e-6f);
        const float t = std::clamp((x - x0) / h, 0.0f, 1.0f);
        float y;
        if (hermite) {
            const float t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * s + (-2 * t3 + 3 * t2) + (t3 - t2) * h * e;
        } else y = 1.0f - (1.0f - y0) * std::pow(1.0f - t, alpha);
        return std::clamp(y, 0.0f, 1.0f);
    }
};

static float modernContrast(float v, float contrast) {
    if (contrast == 0) return v;
    const float k = contrast / 128.0f, end = 1 - k, mid = 1 + k;
    auto half = [&](float t) { return end * 0.5f * (t * t * t - 2 * t * t + t) + (-2 * t * t * t + 3 * t * t) * 0.5f + mid * 0.5f * (t * t * t - t * t); };
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.5f ? half(v / 0.5f) : 1.0f - half((1.0f - v) / 0.5f);
}

static std::vector<float> modernLut(float brightness, float contrast) {
    auto step = [](int k, int n) { return (float)k / (n - 1); };
    std::vector<float> bright(LUT);
    if (brightness == 0 || !std::isfinite(brightness)) for (int k = 0; k < LUT; ++k) bright[k] = step(k, LUT);
    else if (brightness > 0) { const Brightness c(brightness); for (int k = 0; k < LUT; ++k) bright[k] = c.raised(step(k, LUT)); }
    else {
        const Brightness c(-brightness);
        const int fine = 4 * LUT;
        std::vector<float> ys(fine);
        for (int j = 0; j < fine; ++j) ys[j] = c.raised(step(j, fine));
        int j = 0;
        for (int k = 0; k < LUT; ++k) {
            const float y = step(k, LUT);
            while (j + 2 < fine && ys[j + 1] < y) ++j;
            const float y0 = ys[j], y1 = j + 1 < fine ? ys[j + 1] : 1.0f;
            const float f = y1 > y0 ? std::clamp((y - y0) / (y1 - y0), 0.0f, 1.0f) : 0.0f;
            bright[k] = std::clamp(step(j, fine) + f / (fine - 1), 0.0f, 1.0f);
        }
    }
    for (auto& b : bright) b = std::clamp(modernContrast(b, contrast), 0.0f, 1.0f);
    return bright;
}

static float levelsGamma(float u, float g) {
    const float c = 1.0f / g;
    if (g <= 1.0f) return std::pow(u, c);
    const float a = std::pow(2.0f, g), q = 2.2f;
    const float u1 = std::pow((1.0f - c / q) / (a * (1.0f - 1.0f / q)), 1.0f / (1.0f - c));
    if (u >= u1 || u1 <= 0 || !std::isfinite(u1)) return std::pow(u, c);
    const float b = (a * u1 - std::pow(u1, c)) / std::pow(u1, q);
    if (!std::isfinite(b)) return std::pow(u, c);
    return a * u - b * std::pow(u, q);
}

// on whole 8-bit levels, as Photoshop does
static float levelsQ(const LevelsCh& ch, float v) {
    const float q = 255.0f;
    const float range = std::max(ch.inWhite - ch.inBlack, 1e-6f);
    auto whole = [&](float x) { return std::floor(x * q + 0.5f) / q; };
    v = std::round(v * q) / q;
    const float u = std::floor(std::clamp((v - ch.inBlack) / range, 0.0f, 1.0f) * q + 0.5f + 1e-3f) / q;
    const float t = whole(levelsGamma(u, std::max(ch.gamma, 0.01f)));
    return whole(ch.outBlack + t * (ch.outWhite - ch.outBlack));
}

// a natural cubic spline through the points (Photoshop's Curves)
static std::vector<float> curveLut(const std::vector<float>& xy) {
    std::vector<std::pair<double, double>> pts;
    for (size_t i = 0; i + 1 < xy.size(); i += 2) pts.push_back({xy[i], xy[i + 1]});
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end(), [](auto& a, auto& b) { return std::fabs(a.first - b.first) < 1e-6; }), pts.end());
    std::vector<float> out(LUT);
    if (pts.size() < 2) { for (int k = 0; k < LUT; ++k) out[k] = (float)k / (LUT - 1); return out; }
    const size_t n = pts.size();
    std::vector<double> m2(n, 0.0), c(n, 0.0), d(n, 0.0);
    if (n > 2) {
        for (size_t i = 1; i + 1 < n; ++i) {
            const double h0 = pts[i].first - pts[i - 1].first, h1 = pts[i + 1].first - pts[i].first;
            const double a = h0 / 6, b = (h0 + h1) / 3, cc = h1 / 6;
            const double r = (pts[i + 1].second - pts[i].second) / h1 - (pts[i].second - pts[i - 1].second) / h0;
            const double denom = b - a * c[i - 1];
            c[i] = cc / denom; d[i] = (r - a * d[i - 1]) / denom;
        }
        for (size_t i = n - 2; i >= 1; --i) { m2[i] = d[i] - c[i] * m2[i + 1]; if (i == 1) break; }
    }
    size_t seg = 0;
    for (int k = 0; k < LUT; ++k) {
        const double x = (double)k / (LUT - 1);
        if (x <= pts[0].first) { out[k] = std::clamp((float)pts[0].second, 0.0f, 1.0f); continue; }
        if (x >= pts[n - 1].first) { out[k] = std::clamp((float)pts[n - 1].second, 0.0f, 1.0f); continue; }
        while (seg + 2 < n && x > pts[seg + 1].first) ++seg;
        const double x0 = pts[seg].first, y0 = pts[seg].second, x1 = pts[seg + 1].first, y1 = pts[seg + 1].second;
        const double h = x1 - x0, a = (x1 - x) / h, b = (x - x0) / h;
        const double y = a * y0 + b * y1 + ((a * a * a - a) * m2[seg] + (b * b * b - b) * m2[seg + 1]) * h * h / 6.0;
        out[k] = std::clamp((float)y, 0.0f, 1.0f);
    }
    return out;
}

static float posterize(float v, int levels) {
    const float n = (float)std::clamp(levels, 2, 255);
    const float x = std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    const float bin = std::min(std::floor(x * n / 256.0f), n - 1);
    return std::floor(bin * 255.0f / (n - 1)) / 255.0f;
}

// ---------- colour (color.rs) ----------
static void rgbToHsl(const float c[3], float& h, float& s, float& l) {
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
    l = (mx + mn) / 2;
    if (std::fabs(mx - mn) < 1e-7f) { h = 0; s = 0; return; }
    const float d = mx - mn;
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    float hh;
    if (mx == c[0]) { hh = std::fmod((c[1] - c[2]) / d, 6.0f); if (hh < 0) hh += 6; }
    else if (mx == c[1]) hh = (c[2] - c[0]) / d + 2;
    else hh = (c[0] - c[1]) / d + 4;
    h = hh / 6;
}
static void hueRgb(float h, float p, float q, float out[3]) {
    auto f = [&](float t) {
        t = t - std::floor(t);
        if (t < 1.0f / 6) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
        return p;
    };
    out[0] = f(h + 1.0f / 3); out[1] = f(h); out[2] = f(h - 1.0f / 3);
}
static void hslToRgb(float h, float s, float l, float out[3]) {
    if (s <= 0) { out[0] = out[1] = out[2] = l; return; }
    const float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    hueRgb(h, 2 * l - q, q, out);
}

static float rangeWeight(const HueRange& r, float deg) {
    const float a = r.bounds[0];
    auto rel = [&](float x) { float v = std::fmod(x - a, 360.0f); return v < 0 ? v + 360 : v; };
    const float b = rel(r.bounds[1]), c = std::max(rel(r.bounds[2]), b), d = std::max(rel(r.bounds[3]), c), h = rel(deg);
    if (h <= b) return b <= 0 ? 1.0f : h / b;
    if (h <= c) return 1.0f;
    if (h <= d) return d <= c ? 1.0f : (d - h) / (d - c);
    return 0.0f;
}
static bool rangeNeutral(const HueRange& r) { return r.hue == 0 && r.sat == 0 && r.light == 0; }

static void hueSaturation(float c[3], float hue, float s, float l, bool colorize) {
    auto lighten = [&](float v) { return l > 0 ? v + (1 - v) * l : v * (1 + l); };
    float h, ss, ll;
    if (colorize) { rgbToHsl(c, h, ss, ll); float o[3]; float hh = std::fmod(hue, 360.0f); if (hh < 0) hh += 360; hslToRgb(hh / 360.0f, std::min(std::fabs(s), 1.0f), lighten(ll), o); memcpy(c, o, sizeof o); return; }
    float lc[3] = {lighten(c[0]), lighten(c[1]), lighten(c[2])};
    rgbToHsl(lc, h, ss, ll);
    h = h + hue / 360.0f; h -= std::floor(h);
    ss = std::clamp(s > 0 ? ss / std::max(1 - s, 1.0f / 512) : ss * (1 + s), 0.0f, 1.0f);
    hslToRgb(h, ss, ll, c);
}

static void rangeLightness(float c[3], float l) {
    l = std::clamp(l, -1.0f, 1.0f);
    const float target = l > 0 ? std::max({c[0], c[1], c[2]}) : std::min({c[0], c[1], c[2]});
    for (int i = 0; i < 3; ++i) c[i] += (target - c[i]) * std::fabs(l);
}

static float blackWhiteGray(const float c[3], const float w[6]) {
    const float r = c[0], g = c[1], b = c[2];
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), mid = r + g + b - mx - mn;
    const int primary = (r >= g && r >= b) ? 0 : (g >= b ? 2 : 4);
    const int secondary = (b <= r && b <= g) ? 1 : (r <= g ? 3 : 5);
    return mn + (mid - mn) * w[secondary] / 100 + (mx - mid) * w[primary] / 100;
}

static float lum(const float c[3]) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }
static void setLum(const float c[3], float l, float out[3]) {   // W3C SetLum + ClipColor
    const float d = l - lum(c);
    float o[3] = {c[0] + d, c[1] + d, c[2] + d};
    const float L = lum(o), n = std::min({o[0], o[1], o[2]}), x = std::max({o[0], o[1], o[2]});
    for (int i = 0; i < 3; ++i) {
        if (n < 0) o[i] = L + (o[i] - L) * L / std::max(L - n, 1e-9f);
        if (x > 1) o[i] = L + (o[i] - L) * (1 - L) / std::max(x - L, 1e-9f);
    }
    memcpy(out, o, sizeof o);
}

static float srgbToLinear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
static float linearToSrgb(float v) { return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f; }

static const float D50[3] = {0.96422f, 1.0f, 0.82521f};
static const float SRGB_TO_XYZ[3][3] = {{0.436074f, 0.385064f, 0.143080f}, {0.222504f, 0.716878f, 0.060618f}, {0.013932f, 0.097104f, 0.714173f}};
static const float XYZ_TO_SRGB[3][3] = {{3.133856f, -1.616867f, -0.490615f}, {-0.978768f, 1.916142f, 0.033454f}, {0.071945f, -0.228991f, 1.405243f}};

static void photoFilterMatrix(const float color[3], float density, float m[3][3]) {
    const float d = std::clamp(density, 0.0f, 1.0f);
    float lin[3], xyz[3] = {0, 0, 0}, s[3];
    for (int i = 0; i < 3; ++i) lin[i] = srgbToLinear(std::clamp(color[i], 0.0f, 1.0f));
    for (int r = 0; r < 3; ++r) for (int k = 0; k < 3; ++k) xyz[r] += SRGB_TO_XYZ[r][k] * lin[k];
    for (int i = 0; i < 3; ++i) s[i] = 1 - d + d * xyz[i] / D50[i];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { float v = 0; for (int k = 0; k < 3; ++k) v += XYZ_TO_SRGB[r][k] * s[k] * SRGB_TO_XYZ[k][c]; m[r][c] = v; }
}

static const float VIB_GREY[2] = {0.288f, 0.712f};
static const float VIB_BOOST[7] = {0.5001f, 0.0887f, 0.4374f, 0.7573f, -0.8113f, 0.5154f, 2.8026f};
static float hsvHue(const float c[3], float mx, float mn) {
    const float d = mx - mn;
    float h;
    if (mx == c[0]) { h = std::fmod((c[1] - c[2]) / d, 6.0f); if (h < 0) h += 6; }
    else if (mx == c[1]) h = (c[2] - c[0]) / d + 2;
    else h = (c[0] - c[1]) / d + 4;
    return h * 60;
}
static float vibranceSkin(float h) { return h <= 30 ? 1.0f : h < 45 ? (45 - h) / 15 : h < 300 ? 0.0f : (h - 300) / 60; }
static void vibranceLin(float c[3], float v) {
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
    if (mx <= 0 || mx - mn <= 1e-9f) return;
    const float t = 1 - mn / mx;
    const float shape = t * (1 - t) * (2 - t + t * t) * mx * std::max(1 - mx, 0.0f);
    float t2, v2;
    if (v < 0) { const float a = -v; t2 = t * (1 - a / 4) * (1 - a + a * t * (1 + t) / 2); v2 = mx - a * shape; }
    else {
        const float w = vibranceSkin(hsvHue(c, mx, mn));
        const float a = v * (1 - w) + 0.64f * std::pow(v, 1.6f) * w;
        const float boost = (VIB_BOOST[0] * a + VIB_BOOST[1] * a * a) * std::pow(t, VIB_BOOST[2]) * std::pow(1 - t, VIB_BOOST[3]) *
                            std::exp((VIB_BOOST[4] + VIB_BOOST[5] * a + VIB_BOOST[6] * (1 - t)) * std::max(1 - mx, 0.0f));
        t2 = std::min(t * (1 + boost), 1.0f); v2 = mx + v / 4 * shape;
    }
    const float m2 = v2 * (1 - t2), k = (v2 - m2) / (mx - mn);
    for (int i = 0; i < 3; ++i) c[i] = m2 + (c[i] - mn) * k;
}

struct BalanceCurve {
    float b = 0, w = 1, e = 1; bool knee = false; float u0 = 0, s0 = 0, p = 0, d = 0;
    BalanceCurve() = default;
    BalanceCurve(const float tones[3][3], bool preserve, int ch) {
        float t[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) t[i][j] = std::isfinite(tones[i][j]) ? std::clamp(tones[i][j], -100.0f, 100.0f) : 0;
        auto mx = [](const float* v) { return std::max({v[0], v[1], v[2]}); };
        auto mn = [](const float* v) { return std::min({v[0], v[1], v[2]}); };
        float black, white, m;
        if (preserve) { black = mx(t[0]) - t[0][ch]; white = t[2][ch] - mn(t[2]); m = t[1][ch] - (mx(t[1]) + mn(t[1])) / 2; }
        else { black = std::max(-t[0][ch], 0.0f); white = std::max(t[2][ch], 0.0f); m = t[1][ch] + (t[0][ch] + t[2][ch]) / 2; }
        const float gamma = std::pow(2.0f, m / 100);
        e = 1 / std::max(gamma, 0.01f);
        if (gamma > 1) {
            s0 = std::pow(2.0f, gamma);
            u0 = std::min(2 * std::pow(s0, 1 / (e - 1)), 1.0f);
            if (!std::isnan(u0) && u0 > 0) { knee = true; p = std::pow(u0, e); d = e * std::pow(u0, e - 1); }
        }
        b = black / 255; w = 1 - white / 255;
    }
    float eval(float v) const {
        v = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
        if (v >= w) return 1;
        if (v <= b) return 0;
        const float u = (v - b) / (w - b);
        if (knee && u < u0) {
            const float t = u / u0, t2 = t * t, t3 = t2 * t;
            return std::clamp((t3 - 2 * t2 + t) * u0 * s0 + (3 * t2 - 2 * t3) * p + (t3 - t2) * u0 * d, 0.0f, 1.0f);
        }
        return std::pow(u, e);
    }
};

static void selectiveColor(float c[3], bool relative, const float adj[9][4]) {
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]}), mid = c[0] + c[1] + c[2] - mx - mn;
    auto top = [&](int i) { return c[i] >= mx ? mx - mid : 0.0f; };
    auto bottom = [&](int i) { return c[i] <= mn ? mid - mn : 0.0f; };
    const float w[9] = {top(0), bottom(2), top(1), bottom(0), top(2), bottom(1), std::max((mn - 0.5f) * 2, 0.0f),
                        std::clamp(1 - std::fabs(mx - 0.5f) - std::fabs(mn - 0.5f), 0.0f, 1.0f), std::max((0.5f - mx) * 2, 0.0f)};
    float delta[3] = {0, 0, 0};
    for (int r = 0; r < 9; ++r) {
        if (w[r] <= 0) continue;
        for (int i = 0; i < 3; ++i) {
            const float ink = 1 - c[i];
            float d = adj[r][i] / 100 + adj[r][3] / 100;
            if (relative) d *= ink;
            else if (d > 0) d *= std::min(2 * (1 - ink), 1.0f);
            else d *= std::min(2 * ink, 1.0f);
            delta[i] += d * w[r];
        }
    }
    for (int i = 0; i < 3; ++i) c[i] = std::clamp(c[i] - delta[i], 0.0f, 1.0f);
}

static float bayer4(int x, int y) {
    static const float M[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (M[((y & 3) * 4 + (x & 3))] + 0.5f) / 16 - 0.5f;
}

// ---------- prepared tables ----------
struct AdjPrepared {
    std::vector<float> tone[3];                  // Levels / Curves / Brightness-Contrast per channel
    std::vector<float> hue[3];                   // Hue/Saturation range tables (hue, sat, light) by hue
    bool ranges = false;
    BalanceCurve balance[3];
    float pf[3][3];
};

AdjPrepared* adjPrepare(const Adjustment& a) {
    auto* p = new AdjPrepared();
    switch (a.kind) {
        case ADJ_BRIGHTNESS_CONTRAST:
            if (!a.flag) p->tone[0] = modernLut(a.v[0], a.v[1]);
            break;
        case ADJ_LEVELS:
            for (int c = 0; c < 3; ++c) {
                p->tone[c].resize(LUT);
                for (int k = 0; k < LUT; ++k) p->tone[c][k] = levelsQ(a.levels[0], levelsQ(a.levels[1 + c], (float)k / (LUT - 1)));
            }
            break;
        case ADJ_CURVES: {
            const std::vector<float> m = curveLut(a.curve[0]);
            for (int c = 0; c < 3; ++c) { p->tone[c] = curveLut(a.curve[1 + c]); for (auto& v : p->tone[c]) v = lut(m, v); }
            break;
        }
        case ADJ_HUE_SATURATION:
            for (int r = 0; r < 6; ++r) if (!rangeNeutral(a.ranges[r])) p->ranges = true;
            if (p->ranges && !a.flag) {
                for (auto& t : p->hue) t.assign(LUT, 0.0f);
                for (int k = 0; k < LUT; ++k) {
                    const float deg = (float)k / (LUT - 1) * 360;
                    for (int r = 0; r < 6; ++r) {
                        if (rangeNeutral(a.ranges[r])) continue;
                        const float w = rangeWeight(a.ranges[r], deg);
                        p->hue[0][k] += w * a.ranges[r].hue; p->hue[1][k] += w * a.ranges[r].sat / 100; p->hue[2][k] += w * a.ranges[r].light / 100;
                    }
                }
            } else p->ranges = false;
            break;
        case ADJ_COLOR_BALANCE: for (int c = 0; c < 3; ++c) p->balance[c] = BalanceCurve(a.balance, a.flag, c); break;
        case ADJ_PHOTO_FILTER: photoFilterMatrix(a.color, a.v[0], p->pf); break;
        default: break;
    }
    return p;
}

void adjFree(AdjPrepared* p) { delete p; }

void adjApply(const Adjustment& a, const AdjPrepared* p, float c[3], int x, int y) {
    switch (a.kind) {
        case ADJ_INVERT: for (int i = 0; i < 3; ++i) c[i] = 1 - c[i]; break;
        case ADJ_THRESHOLD: {
            const float t = std::round(a.v[0] * 255);
            const float sum = 30 * std::round(std::clamp(c[0], 0.0f, 1.0f) * 255) + 59 * std::round(std::clamp(c[1], 0.0f, 1.0f) * 255) + 11 * std::round(std::clamp(c[2], 0.0f, 1.0f) * 255);
            const float v = sum > 100 * t - 50 ? 1.0f : 0.0f;
            c[0] = c[1] = c[2] = v;
            break;
        }
        case ADJ_POSTERIZE: for (int i = 0; i < 3; ++i) c[i] = posterize(c[i], (int)a.v[0]); break;
        case ADJ_BRIGHTNESS_CONTRAST:
            if (a.flag) {   // legacy: contrast round 128/255, brightness before it when raising, after when lowering
                const float b = a.v[0] / 255, cc = std::clamp(a.v[1], -100.0f, 99.0f);
                const float k = cc >= 0 ? 1 / (1 - cc / 100) : 1 + cc / 100, pivot = 128.0f / 255;
                const float pre = cc >= 0 ? b : 0, post = cc >= 0 ? 0 : b;
                for (int i = 0; i < 3; ++i) c[i] = std::clamp((c[i] + pre - pivot) * k + pivot + post, 0.0f, 1.0f);
            } else for (int i = 0; i < 3; ++i) c[i] = lut(p->tone[0], c[i]);
            break;
        case ADJ_EXPOSURE: {   // in linear light with the 2.2 transfer Photoshop uses for Exposure
            const float m = std::pow(2.0f, a.v[0]), g = std::max(a.v[2], 0.01f);
            for (int i = 0; i < 3; ++i) {
                float lin = std::max(std::pow(std::max(c[i], 0.0f), 2.2f) * m + a.v[1], 0.0f);
                if (g != 1) lin = std::pow(lin, 1 / g);
                c[i] = std::clamp(std::pow(lin, 1 / 2.2f), 0.0f, 1.0f);
            }
            break;
        }
        case ADJ_LEVELS: case ADJ_CURVES: for (int i = 0; i < 3; ++i) c[i] = lut(p->tone[i], c[i]); break;
        case ADJ_HUE_SATURATION: {
            float dh = 0, ds = 0, dl = 0;
            if (p->ranges) { float h, s, l; rgbToHsl(c, h, s, l); dh = lut(p->hue[0], h); ds = lut(p->hue[1], h); dl = lut(p->hue[2], h); }
            hueSaturation(c, a.v[0] + dh, std::clamp(a.v[1] / 100 + ds, -1.0f, 1.0f), std::clamp(a.v[2] / 100, -1.0f, 1.0f), a.flag);
            if (dl != 0) rangeLightness(c, dl);
            break;
        }
        case ADJ_VIBRANCE: {
            const float v = a.v[0] / 100, s = a.v[1] / 100;
            float l[3];
            for (int i = 0; i < 3; ++i) l[i] = srgbToLinear(std::max(c[i], 0.0f));
            if (v != 0) vibranceLin(l, v);
            if (s != 0) { const float g = VIB_GREY[0] * l[0] + VIB_GREY[1] * l[1]; for (int i = 0; i < 3; ++i) l[i] = std::clamp(g + (1 + s) * (l[i] - g), 0.0f, 1.0f); }
            for (int i = 0; i < 3; ++i) c[i] = linearToSrgb(std::clamp(l[i], 0.0f, 1.0f));
            break;
        }
        case ADJ_CHANNEL_MIXER: {
            float o[3];
            for (int r = 0; r < 3; ++r) o[r] = std::clamp(a.mixer[r][0] * c[0] + a.mixer[r][1] * c[1] + a.mixer[r][2] * c[2] + a.mixer[r][3], 0.0f, 1.0f);
            if (a.flag) o[1] = o[2] = o[0];
            memcpy(c, o, sizeof o);
            break;
        }
        case ADJ_PHOTO_FILTER: {
            float lin[3], f[3];
            for (int i = 0; i < 3; ++i) lin[i] = srgbToLinear(std::max(c[i], 0.0f));
            for (int r = 0; r < 3; ++r) f[r] = linearToSrgb(std::max(p->pf[r][0] * lin[0] + p->pf[r][1] * lin[1] + p->pf[r][2] * lin[2], 0.0f));
            if (a.flag) setLum(f, lum(c), f);
            for (int i = 0; i < 3; ++i) c[i] = std::clamp(f[i], 0.0f, 1.0f);
            break;
        }
        case ADJ_BLACK_WHITE: {
            const float g = std::clamp(blackWhiteGray(c, a.bw), 0.0f, 1.0f);
            if (a.flag) { setLum(a.tint, g, c); for (int i = 0; i < 3; ++i) c[i] = std::clamp(c[i], 0.0f, 1.0f); }
            else c[0] = c[1] = c[2] = g;
            break;
        }
        case ADJ_GRADIENT_MAP: {
            float t = lum(c);
            if (a.flag) t = 1 - t;
            const auto& s = a.stops;
            float o[3] = {t, t, t};
            if (s.size() == 1) for (int i = 0; i < 3; ++i) o[i] = s[0][1 + i];
            else if (s.size() >= 2) {
                if (t <= s[0][0]) for (int i = 0; i < 3; ++i) o[i] = s[0][1 + i];
                else {
                    bool done = false;
                    for (size_t k = 0; k + 1 < s.size() && !done; ++k)
                        if (t <= s[k + 1][0]) {
                            const float f = s[k + 1][0] > s[k][0] ? (t - s[k][0]) / (s[k + 1][0] - s[k][0]) : 0;
                            for (int i = 0; i < 3; ++i) o[i] = s[k][1 + i] + (s[k + 1][1 + i] - s[k][1 + i]) * f;
                            done = true;
                        }
                    if (!done) for (int i = 0; i < 3; ++i) o[i] = s.back()[1 + i];
                }
            }
            if (a.flag2) { const float d = bayer4(x, y) / 255; for (int i = 0; i < 3; ++i) o[i] = std::clamp(o[i] + d, 0.0f, 1.0f); }
            memcpy(c, o, sizeof o);
            break;
        }
        case ADJ_COLOR_BALANCE: for (int i = 0; i < 3; ++i) c[i] = p->balance[i].eval(c[i]); break;
        case ADJ_SELECTIVE_COLOR: selectiveColor(c, a.flag, a.selective); break;
        default: break;
    }
}
