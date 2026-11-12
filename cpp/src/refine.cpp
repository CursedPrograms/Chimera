// LayerTack - Select and Mask (see refine.h). Work is done only in the box round the edge, so the cost follows the
// edge's length, not the picture's size.
#include "refine.h"
#include <algorithm>
#include <cmath>

namespace {

struct Box { int x0, y0, x1, y1; int w() const { return x1 - x0; } int h() const { return y1 - y0; } };

// box mean of radius r, the window clipped to the buffer and divided by its pixel count (prefix sums)
std::vector<float> boxMean(const std::vector<float>& src, int w, int h, int r) {
    std::vector<float> tmp((size_t)w * h), out((size_t)w * h);
    std::vector<double> pre((size_t)std::max(w, h) + 1);
    for (int y = 0; y < h; ++y) {
        const float* row = &src[(size_t)y * w];
        for (int x = 0; x < w; ++x) pre[x + 1] = pre[x] + row[x];
        for (int x = 0; x < w; ++x) {
            const int lo = std::max(0, x - r), hi = std::min(w, x + r + 1);
            tmp[(size_t)y * w + x] = (float)((pre[hi] - pre[lo]) / (hi - lo));
        }
    }
    std::vector<double> col((size_t)(h + 1) * w, 0.0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) col[(size_t)(y + 1) * w + x] = col[(size_t)y * w + x] + tmp[(size_t)y * w + x];
    for (int y = 0; y < h; ++y) {
        const int lo = std::max(0, y - r), hi = std::min(h, y + r + 1);
        const double n = hi - lo;
        for (int x = 0; x < w; ++x) out[(size_t)y * w + x] = (float)((col[(size_t)hi * w + x] - col[(size_t)lo * w + x]) / n);
    }
    return out;
}

// the guided filter with a colour guide (He et al., eq. 19-21)
std::vector<float> guidedColor(const std::vector<float> ch[3], const std::vector<float>& p, int w, int h, int r, float eps) {
    const size_t n = (size_t)w * h;
    std::vector<float> m[3], mip[3], mii[6];
    const std::vector<float> mp = boxMean(p, w, h, r);
    std::vector<float> t(n);
    for (int c = 0; c < 3; ++c) {
        m[c] = boxMean(ch[c], w, h, r);
        for (size_t i = 0; i < n; ++i) t[i] = ch[c][i] * p[i];
        mip[c] = boxMean(t, w, h, r);
    }
    const int pairs[6][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}, {2, 2}};
    for (int k = 0; k < 6; ++k) {
        for (size_t i = 0; i < n; ++i) t[i] = ch[pairs[k][0]][i] * ch[pairs[k][1]][i];
        mii[k] = boxMean(t, w, h, r);
    }
    std::vector<float> av[3] = {std::vector<float>(n), std::vector<float>(n), std::vector<float>(n)}, bv(n);
    const double e = eps;
    for (size_t i = 0; i < n; ++i) {
        const double mu[3] = {m[0][i], m[1][i], m[2][i]};
        auto s = [&](int k, int a, int b) { return (double)mii[k][i] - mu[a] * mu[b]; };
        const double rr = s(0, 0, 0) + e, rg = s(1, 0, 1), rb = s(2, 0, 2), gg = s(3, 1, 1) + e, gb = s(4, 1, 2), bb = s(5, 2, 2) + e;
        const double cov[3] = {mip[0][i] - mu[0] * mp[i], mip[1][i] - mu[1] * mp[i], mip[2][i] - mu[2] * mp[i]};
        const double i00 = gg * bb - gb * gb, i01 = gb * rb - rg * bb, i02 = rg * gb - gg * rb;
        const double i11 = rr * bb - rb * rb, i12 = rb * rg - rr * gb, i22 = rr * gg - rg * rg;
        const double det = rr * i00 + rg * i01 + rb * i02;
        double a0 = 0, a1 = 0, a2 = 0;
        if (std::fabs(det) >= 1e-30) {
            a0 = (i00 * cov[0] + i01 * cov[1] + i02 * cov[2]) / det;
            a1 = (i01 * cov[0] + i11 * cov[1] + i12 * cov[2]) / det;
            a2 = (i02 * cov[0] + i12 * cov[1] + i22 * cov[2]) / det;
        }
        av[0][i] = (float)a0; av[1][i] = (float)a1; av[2][i] = (float)a2;
        bv[i] = (float)(mp[i] - a0 * mu[0] - a1 * mu[1] - a2 * mu[2]);
    }
    std::vector<float> out(n);
    const std::vector<float> ma0 = boxMean(av[0], w, h, r), ma1 = boxMean(av[1], w, h, r), ma2 = boxMean(av[2], w, h, r), mb = boxMean(bv, w, h, r);
    for (size_t i = 0; i < n; ++i) out[i] = ma0[i] * ch[0][i] + ma1[i] * ch[1][i] + ma2[i] * ch[2][i] + mb[i];
    return out;
}

// running max (or min) over 2r+1 samples, edge-clipped (van Herk / Gil-Werman)
void runningExtreme(const float* src, float* out, int n, int r, bool mx, std::vector<float>& g, std::vector<float>& hb) {
    const int k = 2 * r + 1;
    auto f = [mx](float a, float b) { return mx ? std::max(a, b) : std::min(a, b); };
    const float pad = mx ? -INFINITY : INFINITY;
    const int m = (n + 2 * r + k - 1) / k * k;
    g.assign(m, pad); hb.assign(m, pad);
    auto p = [&](int j) { return j >= r && j < r + n ? src[j - r] : pad; };
    for (int j = 0; j < m; ++j) g[j] = j % k == 0 ? p(j) : f(g[j - 1], p(j));
    for (int j = m - 1; j >= 0; --j) hb[j] = (j % k == k - 1 || j == m - 1) ? p(j) : f(hb[j + 1], p(j));
    for (int i = 0; i < n; ++i) out[i] = f(hb[i], g[i + 2 * r]);
}

std::vector<float> maxSquare(const std::vector<float>& src, int w, int h, int r, bool mx) {
    std::vector<float> g, hb, tmp((size_t)w * h), out((size_t)w * h), col(h), res(h);
    for (int y = 0; y < h; ++y) runningExtreme(&src[(size_t)y * w], &tmp[(size_t)y * w], w, std::min(r, w - 1), mx, g, hb);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) col[y] = tmp[(size_t)y * w + x];
        runningExtreme(col.data(), res.data(), h, std::min(r, h - 1), mx, g, hb);
        for (int y = 0; y < h; ++y) out[(size_t)y * w + x] = res[y];
    }
    return out;
}

// how wide the picture's edge is round each pixel: local range / local steepest step (a sharp step ~2, a ramp of L ~L)
std::vector<float> edgeWidth(const std::vector<float> ch[3], int w, int h, int r) {
    const size_t n = (size_t)w * h;
    std::vector<float> Y(n), g(n);
    for (size_t i = 0; i < n; ++i) Y[i] = 0.299f * ch[0][i] + 0.587f * ch[1][i] + 0.114f * ch[2][i];
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto at = [&](int xx, int yy) { return Y[(size_t)yy * w + xx]; };
            const float gx = (at(std::min(x + 1, w - 1), y) - at(std::max(x - 1, 0), y)) / 2;
            const float gy = (at(x, std::min(y + 1, h - 1)) - at(x, std::max(y - 1, 0))) / 2;
            g[(size_t)y * w + x] = std::sqrt(gx * gx + gy * gy);
        }
    const std::vector<float> gm = maxSquare(g, w, h, r, true), hi = maxSquare(Y, w, h, r, true), lo = maxSquare(Y, w, h, r, false);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = (hi[i] - lo[i]) / std::max(gm[i], 1e-4f);
    return out;
}

// exact Euclidean distance to the nearest seed (Felzenszwalb and Huttenlocher)
void edt1(const float* f, float* d, int n, std::vector<int>& v, std::vector<float>& z) {
    v.assign(n, 0); z.assign((size_t)n + 1, 0);
    int k = 0; v[0] = 0; z[0] = -INFINITY; z[1] = INFINITY;
    for (int q = 1; q < n; ++q) {
        auto cross = [&](int u) { return ((f[q] + (float)q * q) - (f[u] + (float)u * u)) / (2.0f * q - 2.0f * u); };
        float s = cross(v[k]);
        while (s <= z[k]) { --k; s = cross(v[k]); }   // z[0] is -inf: k never goes below 0
        ++k; v[k] = q; z[k] = s; z[k + 1] = INFINITY;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

std::vector<float> edt(const std::vector<uint8_t>& seed, int w, int h) {
    const float BIG = 1e20f;
    std::vector<float> f((size_t)w * h), col(h), dc(h), row(w), dr(w);
    std::vector<int> v; std::vector<float> z;
    for (size_t i = 0; i < f.size(); ++i) f[i] = seed[i] ? 0.0f : BIG;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) col[y] = f[(size_t)y * w + x];
        edt1(col.data(), dc.data(), h, v, z);
        for (int y = 0; y < h; ++y) f[(size_t)y * w + x] = dc[y];
    }
    for (int y = 0; y < h; ++y) {
        std::copy(&f[(size_t)y * w], &f[(size_t)y * w] + w, row.begin());
        edt1(row.data(), dr.data(), w, v, z);
        for (int x = 0; x < w; ++x) f[(size_t)y * w + x] = std::sqrt(std::min(dr[x], BIG));
    }
    return f;
}

// a Gaussian, exact for small sigma, three box blurs past that (edge-clamped either way)
void boxBlur1(const float* src, float* dst, int n, int stride, int r, std::vector<float>& tmp) {
    tmp.resize(n);
    for (int i = 0; i < n; ++i) tmp[i] = src[(size_t)i * stride];
    double acc = 0;
    for (int i = -r; i <= r; ++i) acc += tmp[std::clamp(i, 0, n - 1)];
    const double k = 1.0 / (2 * r + 1);
    for (int i = 0; i < n; ++i) {
        dst[(size_t)i * stride] = (float)(acc * k);
        acc += tmp[std::min(i + r + 1, n - 1)] - tmp[std::max(i - r, 0)];
    }
}

void gaussian(std::vector<float>& a, int w, int h, float sigma) {
    if (sigma < 0.1f) return;
    std::vector<float> tmp;
    if (sigma < 3.0f) {
        const int r = (int)std::ceil(sigma * 3);
        std::vector<float> k(2 * r + 1);
        float s = 0;
        for (int i = -r; i <= r; ++i) s += k[i + r] = std::exp(-(float)(i * i) / (2 * sigma * sigma));
        for (auto& v : k) v /= s;
        std::vector<float> t((size_t)w * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float acc = 0;
                for (int i = -r; i <= r; ++i) acc += a[(size_t)y * w + std::clamp(x + i, 0, w - 1)] * k[i + r];
                t[(size_t)y * w + x] = acc;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float acc = 0;
                for (int i = -r; i <= r; ++i) acc += t[(size_t)std::clamp(y + i, 0, h - 1) * w + x] * k[i + r];
                a[(size_t)y * w + x] = acc;
            }
        return;
    }
    // three boxes whose variances add up to sigma^2 (Kovesi)
    const double wIdeal = std::sqrt(12.0 * sigma * sigma / 3 + 1);
    int wl = (int)std::floor(wIdeal); if (wl % 2 == 0) --wl;
    const int wu = wl + 2;
    const int mIdeal = (int)std::lround((12.0 * sigma * sigma - 3.0 * wl * wl - 12.0 * wl - 9) / (-4.0 * wl - 4));
    for (int pass = 0; pass < 3; ++pass) {
        const int r = ((pass < mIdeal ? wl : wu) - 1) / 2;
        for (int y = 0; y < h; ++y) boxBlur1(&a[(size_t)y * w], &a[(size_t)y * w], w, 1, r, tmp);
        for (int x = 0; x < w; ++x) boxBlur1(&a[x], &a[x], h, w, r, tmp);
    }
}

// the box round everything that isn't plainly in or out, widened by pad, on the canvas
bool edgeBox(const std::vector<float>& m, int w, int h, int pad, Box& b) {
    b = {w, h, 0, 0};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            const bool in = m[i] >= 0.5f;
            const bool soft = m[i] > 0.0f && m[i] < 1.0f;
            const bool edge = soft || (x + 1 < w && (m[i + 1] >= 0.5f) != in) || (y + 1 < h && (m[i + w] >= 0.5f) != in);
            if (!edge) continue;
            b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y); b.x1 = std::max(b.x1, x + 2); b.y1 = std::max(b.y1, y + 2);
        }
    if (b.x1 <= b.x0) return false;
    b.x0 = std::max(0, b.x0 - pad); b.y0 = std::max(0, b.y0 - pad); b.x1 = std::min(w, b.x1 + pad); b.y1 = std::min(h, b.y1 + pad);
    return true;
}

std::vector<float> cut(const std::vector<float>& a, int w, const Box& b, int stride = 1, int c = 0) {
    std::vector<float> o((size_t)b.w() * b.h());
    for (int y = 0; y < b.h(); ++y)
        for (int x = 0; x < b.w(); ++x) o[(size_t)y * b.w() + x] = a[((size_t)(y + b.y0) * w + x + b.x0) * stride + c];
    return o;
}

void paste(std::vector<float>& a, int w, const Box& b, const std::vector<float>& o) {
    for (int y = 0; y < b.h(); ++y) std::copy(&o[(size_t)y * b.w()], &o[(size_t)y * b.w()] + b.w(), &a[(size_t)(y + b.y0) * w + b.x0]);
}

// the edge detection: the guided filter's answer, used inside a band of radius round the hard edge
std::vector<float> detectEdge(const std::vector<float>& rgb, const std::vector<float>& m, int w, int h, const RefineParams& p) {
    std::vector<float> a = m;
    const int g = (int)std::clamp(std::ceil(p.radius), 1.0f, 64.0f);
    const int halo = std::max((int)std::ceil(p.radius), 2 * g + (p.smart ? g + 1 : 0)) + 2;
    Box b;
    if (!edgeBox(m, w, h, 2 * halo, b)) return a;
    const int bw = b.w(), bh = b.h();
    const size_t n = (size_t)bw * bh;
    const std::vector<float> mm = cut(m, w, b);
    std::vector<uint8_t> boundary(n, 0);
    bool any = false;
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const size_t i = (size_t)y * bw + x;
            const bool v = mm[i] >= 0.5f;
            const bool d = (x > 0 && (mm[i - 1] >= 0.5f) != v) || (x + 1 < bw && (mm[i + 1] >= 0.5f) != v) || (y > 0 && (mm[i - bw] >= 0.5f) != v) || (y + 1 < bh && (mm[i + bw] >= 0.5f) != v);
            boundary[i] = d; any |= d;
        }
    if (!any) return a;
    const std::vector<float> dist = edt(boundary, bw, bh);
    const std::vector<float> ch[3] = {cut(rgb, w, b, 3, 0), cut(rgb, w, b, 3, 1), cut(rgb, w, b, 3, 2)};
    const std::vector<float> q = guidedColor(ch, mm, bw, bh, g, 1e-4f);
    std::vector<float> widths;
    if (p.smart) widths = edgeWidth(ch, bw, bh, g);
    std::vector<float> o = mm;
    for (size_t i = 0; i < n; ++i) {
        const float reff = p.smart ? std::clamp(widths[i], 1.5f, std::max(p.radius, 1.5f)) : p.radius;
        const float wgt = std::clamp(reff + 1.0f - dist[i], 0.0f, 1.0f);
        if (wgt > 0) o[i] = mm[i] + (std::clamp(q[i], 0.0f, 1.0f) - mm[i]) * wgt;
    }
    paste(a, w, b, o);
    return a;
}

} // namespace

std::vector<float> refineMask(const std::vector<float>& rgb, const std::vector<float>& m, int w, int h, const RefineParams& p, RefineCache* cache) {
    std::vector<float> a;
    if (p.radius > 0) {
        if (cache && cache->radius == p.radius && cache->smart == p.smart && cache->edge.size() == m.size()) a = cache->edge;
        else {
            a = detectEdge(rgb, m, w, h, p);
            if (cache) { cache->radius = p.radius; cache->smart = p.smart; cache->edge = a; }
        }
    } else a = m;
    const float ss = std::clamp(p.smooth, 0.0f, 100.0f) / 100.0f * 4.0f, fs = std::max(p.feather, 0.0f) / 2.0f;
    const int pad = (int)std::ceil(3 * ss) + (int)std::ceil(3 * fs) + 2;
    Box b;
    if (!edgeBox(a, w, h, pad, b)) return a;
    std::vector<float> o = cut(a, w, b);
    if (ss >= 0.3f) {
        gaussian(o, b.w(), b.h(), ss);
        const float gain = 1 + ss;
        for (auto& v : o) v = std::clamp((v - 0.5f) * gain + 0.5f, 0.0f, 1.0f);
    }
    gaussian(o, b.w(), b.h(), fs);
    if (p.contrast > 0) {
        const float gain = 1.0f / (1.0f - 0.99f * std::clamp(p.contrast, 0.0f, 100.0f) / 100.0f);
        for (auto& v : o) v = std::clamp((v - 0.5f) * gain + 0.5f, 0.0f, 1.0f);
    }
    // Shift Edge: each soft pixel's odds times 49^(shift/100); fully in or out stay put
    const float k = std::pow(49.0f, std::clamp(p.shift, -100.0f, 100.0f) / 100.0f);
    if (std::fabs(k - 1) >= 1e-6f)
        for (auto& v : o) {
            v = std::clamp(v, 0.0f, 1.0f);
            if (v > 0 && v < 1) v = std::clamp(k * v / (k * v + (1 - v)), 0.0f, 1.0f);
        }
    paste(a, w, b, o);
    return a;
}

void decontaminate(uint8_t* rgba, const std::vector<float>& alpha, int w, int h, float radius, float amount) {
    const int rad = (int)std::ceil(std::max(radius, 3.0f));
    amount = std::clamp(amount, 0.0f, 100.0f) / 100.0f;
    auto soft = [](float a) { return a > 0.02f && a < 0.98f; };
    Box b = {w, h, 0, 0};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (soft(alpha[(size_t)y * w + x])) { b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y); b.x1 = std::max(b.x1, x + 1); b.y1 = std::max(b.y1, y + 1); }
    if (b.x1 <= b.x0 || amount <= 0) return;
    const int halo = rad * 4;
    b.x0 = std::max(0, b.x0 - halo); b.y0 = std::max(0, b.y0 - halo); b.x1 = std::min(w, b.x1 + halo); b.y1 = std::min(h, b.y1 + halo);
    const int bw = b.w(), bh = b.h();
    const size_t n = (size_t)bw * bh;
    std::vector<float> wts(n), al(n);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const size_t i = (size_t)y * bw + x, ci = (size_t)(y + b.y0) * w + x + b.x0;
            al[i] = alpha[ci];
            wts[i] = al[i] >= 0.98f ? rgba[ci * 4 + 3] / 255.0f : 0.0f;
        }
    const std::vector<float> den = boxMean(wts, bw, bh, rad), den2 = boxMean(wts, bw, bh, rad * 4);
    std::vector<float> num[3], num2[3], v(n);
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x) {
                const size_t i = (size_t)y * bw + x;
                v[i] = wts[i] * rgba[((size_t)(y + b.y0) * w + x + b.x0) * 4 + c] / 255.0f;
            }
        num[c] = boxMean(v, bw, bh, rad); num2[c] = boxMean(v, bw, bh, rad * 4);
    }
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const size_t i = (size_t)y * bw + x;
            if (!soft(al[i])) continue;
            const float k = amount * std::clamp((1 - al[i]) * 4, 0.0f, 1.0f);
            uint8_t* q = &rgba[((size_t)(y + b.y0) * w + x + b.x0) * 4];
            for (int c = 0; c < 3; ++c) {
                const float val = q[c] / 255.0f;
                const float f = den[i] > 1e-4f ? num[c][i] / den[i] : den2[i] > 1e-4f ? num2[c][i] / den2[i] : val;
                const float o = std::clamp(val + (f - val) * k, 0.0f, 1.0f);
                q[c] = (uint8_t)(o * 255 + 0.5f);
            }
        }
}
