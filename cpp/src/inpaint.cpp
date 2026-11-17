// Chimera - content-aware fill and the membrane (see inpaint.h), after PhotoCraft's inpaint.rs and poisson.rs.
#include "inpaint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Inpaint {

static const int R = 4;                 // patch radius: 9 x 9 patches (PhotoCraft uses 7 x 7; 9 keeps more structure)
static const int PSZ = 2 * R + 1;
static const int EM_ITERS = 10, MIN_EM_ITERS = 3, PM_ITERS = 2, REFINE_EXTENT = 640;

struct Rng {   // a fixed-seed hash generator: the same fill every time
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 11); }
};

struct Level {
    int w = 0, h = 0;
    std::vector<float> img;             // w x h x 3
    std::vector<uint8_t> hole, src;     // src: may be copied from
};

static Level half(const Level& l) {
    Level o; o.w = (l.w + 1) / 2; o.h = (l.h + 1) / 2;
    o.img.assign((size_t)o.w * o.h * 3, 0.0f); o.hole.assign((size_t)o.w * o.h, 0); o.src.assign((size_t)o.w * o.h, 0);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            float acc[3] = {0, 0, 0}; int known = 0; bool anyHole = false, allSrc = true;
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                const int sx = std::min(l.w - 1, 2 * x + dx), sy = std::min(l.h - 1, 2 * y + dy);
                const size_t i = (size_t)sy * l.w + sx;
                if (l.hole[i]) { anyHole = true; continue; }
                for (int c = 0; c < 3; ++c) acc[c] += l.img[i * 3 + c];
                ++known;
                allSrc = allSrc && l.src[i];
            }
            const size_t o_ = (size_t)y * o.w + x;
            o.hole[o_] = anyHole;   // a coarse pixel touching the hole is hole: the fill never copies a piece of it
            o.src[o_] = !anyHole && allSrc;
            if (known) for (int c = 0; c < 3; ++c) o.img[o_ * 3 + c] = acc[c] / known;
        }
    return o;
}

// the coarsest level's start: from the hole's edge inwards, ring by ring, each pixel the mean of its known neighbours
static void onionPeel(Level& l) {
    std::vector<uint8_t> known(l.hole.size());
    for (size_t i = 0; i < known.size(); ++i) known[i] = !l.hole[i];
    bool left = true;
    while (left) {
        left = false;
        std::vector<size_t> ring;
        for (int y = 0; y < l.h; ++y)
            for (int x = 0; x < l.w; ++x) {
                const size_t i = (size_t)y * l.w + x;
                if (known[i]) continue;
                left = true;
                for (int k = 0; k < 8; ++k) {
                    static const int D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
                    const int nx = x + D[k][0], ny = y + D[k][1];
                    if (nx >= 0 && ny >= 0 && nx < l.w && ny < l.h && known[(size_t)ny * l.w + nx]) { ring.push_back(i); break; }
                }
            }
        if (ring.empty()) break;
        for (size_t i : ring) {
            const int x = (int)(i % l.w), y = (int)(i / l.w);
            float acc[3] = {0, 0, 0}; float n = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= l.w || ny >= l.h) continue;
                const size_t j = (size_t)ny * l.w + nx;
                if (!known[j]) continue;
                const float wgt = (dx && dy) ? 0.7071f : 1.0f;
                for (int c = 0; c < 3; ++c) acc[c] += l.img[j * 3 + c] * wgt;
                n += wgt;
            }
            for (int c = 0; c < 3; ++c) l.img[i * 3 + c] = acc[c] / n;
        }
        for (size_t i : ring) known[i] = 1;
    }
}

struct Solver {
    Level& L;
    std::vector<uint8_t> valid;         // a patch centred here may be copied (all inside, none of it hole)
    std::vector<int> validList;
    int bx0, by0, bx1, by1;             // the targets' box: patch centres whose patch touches the hole
    std::vector<uint8_t> target;        // in the box
    std::vector<int> nnf;               // per box cell: the source centre (index into the level)
    std::vector<float> cost;
    explicit Solver(Level& l) : L(l) {}
    int bw() const { return bx1 - bx0; }

    bool prepare() {
        const int w = L.w, h = L.h;
        // valid sources: an integral image of the "may not copy" pixels
        std::vector<int> bad((size_t)(w + 1) * (h + 1), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                bad[(size_t)(y + 1) * (w + 1) + x + 1] = (!L.src[(size_t)y * w + x] ? 1 : 0) + bad[(size_t)y * (w + 1) + x + 1] + bad[(size_t)(y + 1) * (w + 1) + x] - bad[(size_t)y * (w + 1) + x];
        valid.assign((size_t)w * h, 0);
        for (int y = R; y < h - R; ++y)
            for (int x = R; x < w - R; ++x) {
                const int s = bad[(size_t)(y + R + 1) * (w + 1) + x + R + 1] - bad[(size_t)(y - R) * (w + 1) + x + R + 1] - bad[(size_t)(y + R + 1) * (w + 1) + x - R] + bad[(size_t)(y - R) * (w + 1) + x - R];
                if (!s) { valid[(size_t)y * w + x] = 1; validList.push_back(y * w + x); }
            }
        if (validList.empty()) return false;
        bx0 = w; by0 = h; bx1 = 0; by1 = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (L.hole[(size_t)y * w + x]) { bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x + 1); by1 = std::max(by1, y + 1); }
        bx0 = std::max(0, bx0 - R); by0 = std::max(0, by0 - R); bx1 = std::min(w, bx1 + R); by1 = std::min(h, by1 + R);
        target.assign((size_t)bw() * (by1 - by0), 0);
        // a target: a patch centre within R of a hole pixel (dilate the hole by R in the box)
        for (int y = by0; y < by1; ++y)
            for (int x = bx0; x < bx1; ++x) {
                bool t = false;
                for (int dy = -R; dy <= R && !t; ++dy) for (int dx = -R; dx <= R && !t; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && L.hole[(size_t)ny * w + nx]) t = true;
                }
                target[(size_t)(y - by0) * bw() + (x - bx0)] = t;
            }
        nnf.assign(target.size(), -1); cost.assign(target.size(), 1e30f);
        return true;
    }
    // the patch distance from target centre (tx, ty) to source centre s, stopping once past `best`
    float dist(int tx, int ty, int s, float best) const {
        const int w = L.w, h = L.h, sx = s % w, sy = s / w;
        float d = 0; int n = 0;
        for (int dy = -R; dy <= R; ++dy) {
            const int y = ty + dy; if (y < 0 || y >= h) continue;
            for (int dx = -R; dx <= R; ++dx) {
                const int x = tx + dx; if (x < 0 || x >= w) continue;
                const float* a = &L.img[((size_t)y * w + x) * 3];
                const float* b = &L.img[((size_t)(sy + dy) * w + (sx + dx)) * 3];
                const float e0 = a[0] - b[0], e1 = a[1] - b[1], e2 = a[2] - b[2];
                d += e0 * e0 + e1 * e1 + e2 * e2; ++n;
            }
            if (d > best * PSZ * PSZ) return 1e30f;   // already worse than the best (as a mean over a whole patch)
        }
        return n ? d / n : 1e30f;
    }
    void costs() {
        for (int y = by0; y < by1; ++y)
            for (int x = bx0; x < bx1; ++x) {
                const size_t k = (size_t)(y - by0) * bw() + (x - bx0);
                if (target[k]) cost[k] = dist(x, y, nnf[k], 1e30f);
            }
    }
    void tryMatch(int x, int y, size_t k, int s) {
        if (s < 0 || !valid[s] || s == nnf[k]) return;
        const float d = dist(x, y, s, cost[k]);
        if (d < cost[k]) { cost[k] = d; nnf[k] = s; }
    }
    void patchmatch(int pass, uint64_t seed, int radius) {
        const int w = L.w;
        Rng rng(seed);
        const bool fwd = pass % 2 == 0;
        const int dir = fwd ? 1 : -1;
        for (int yy = 0; yy < by1 - by0; ++yy) {
            const int y = fwd ? by0 + yy : by1 - 1 - yy;
            for (int xx = 0; xx < bw(); ++xx) {
                const int x = fwd ? bx0 + xx : bx1 - 1 - xx;
                const size_t k = (size_t)(y - by0) * bw() + (x - bx0);
                if (!target[k]) continue;
                // propagation: the neighbour's match, shifted along
                const int px = x - dir, py = y - dir;
                if (px >= bx0 && px < bx1) { const size_t kn = k - dir; if (target[kn] && nnf[kn] >= 0) { const int s = nnf[kn]; const int sx = s % w + dir; if (sx >= 0 && sx < w) tryMatch(x, y, k, s + dir); } }
                if (py >= by0 && py < by1) { const size_t kn = k - (size_t)dir * bw(); if (target[kn] && nnf[kn] >= 0) { const int s = nnf[kn] + dir * w; if (s >= 0 && s < (int)L.img.size() / 3) tryMatch(x, y, k, s); } }
                // random search round the current match, the window halving
                const int cx = nnf[k] % w, cy = nnf[k] / w;
                for (int rad = radius; rad >= 1; rad /= 2) {
                    const int sx = cx + (int)(rng.next() % (2 * rad + 1)) - rad, sy = cy + (int)(rng.next() % (2 * rad + 1)) - rad;
                    if (sx < 0 || sy < 0 || sx >= w || sy >= L.h) continue;
                    tryMatch(x, y, k, sy * w + sx);
                }
            }
        }
    }
    // the finish: each hole pixel copied from where its own best patch's centre is (the vote averages several
    // sources and goes soft; this keeps the grain), then one more match so the copy is consistent
    void sharpen() {
        const int w = L.w;
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<float> out = L.img;
            for (int y = by0; y < by1; ++y)
                for (int x = bx0; x < bx1; ++x) {
                    const size_t i = (size_t)y * w + x;
                    if (!L.hole[i]) continue;
                    const int s = nnf[(size_t)(y - by0) * bw() + (x - bx0)];
                    if (s < 0) continue;
                    for (int c = 0; c < 3; ++c) out[i * 3 + c] = L.img[(size_t)s * 3 + c];
                }
            L.img.swap(out);
            if (pass == 0) { costs(); patchmatch(7, 99, 2); }
        }
    }
    // each hole pixel from the patches over it, weighted by how well they match (sigma: the 75th percentile cost)
    void vote() {
        const int w = L.w;
        std::vector<float> cs;
        for (size_t k = 0; k < target.size(); ++k) if (target[k] && cost[k] < 1e29f) cs.push_back(cost[k]);
        if (cs.empty()) return;
        std::nth_element(cs.begin(), cs.begin() + cs.size() * 3 / 4, cs.end());
        const float sig2 = std::max(1e-6f, cs[cs.size() * 3 / 4]);
        std::vector<float> acc((size_t)bw() * (by1 - by0) * 4, 0.0f);
        for (int y = by0; y < by1; ++y)
            for (int x = bx0; x < bx1; ++x) {
                const size_t k = (size_t)(y - by0) * bw() + (x - bx0);
                if (!target[k] || nnf[k] < 0) continue;
                const float wt = std::exp(-cost[k] / (2 * sig2)) + 1e-12f;
                const int sx = nnf[k] % w, sy = nnf[k] / w;
                for (int dy = -R; dy <= R; ++dy) for (int dx = -R; dx <= R; ++dx) {
                    const int qx = x + dx, qy = y + dy;
                    if (qx < bx0 || qy < by0 || qx >= bx1 || qy >= by1) continue;
                    if (!L.hole[(size_t)qy * w + qx]) continue;
                    const float* s = &L.img[((size_t)(sy + dy) * w + (sx + dx)) * 3];
                    float* a = &acc[((size_t)(qy - by0) * bw() + (qx - bx0)) * 4];
                    a[0] += s[0] * wt; a[1] += s[1] * wt; a[2] += s[2] * wt; a[3] += wt;
                }
            }
        for (int y = by0; y < by1; ++y)
            for (int x = bx0; x < bx1; ++x) {
                const float* a = &acc[((size_t)(y - by0) * bw() + (x - bx0)) * 4];
                if (a[3] <= 0 || !L.hole[(size_t)y * w + x]) continue;
                float* p = &L.img[((size_t)y * w + x) * 3];
                for (int c = 0; c < 3; ++c) p[c] = a[c] / a[3];
            }
    }
};

bool complete(int w, int h, std::vector<float>& img, const std::vector<uint8_t>& hole, const std::vector<uint8_t>* source) {
    if (w <= 0 || h <= 0) return false;
    std::vector<Level> levels(1);
    levels[0].w = w; levels[0].h = h; levels[0].img = img; levels[0].hole = hole;
    levels[0].src.resize(hole.size());
    int hx0 = w, hy0 = h, hx1 = 0, hy1 = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            levels[0].src[i] = !hole[i] && (!source || (*source)[i]);
            if (hole[i]) { hx0 = std::min(hx0, x); hy0 = std::min(hy0, y); hx1 = std::max(hx1, x + 1); hy1 = std::max(hy1, y + 1); }
        }
    if (hx1 <= hx0) return true;   // nothing to fill
    // the pyramid: down to where the hole is about four patches across
    std::vector<int> exts = {std::max(hx1 - hx0, hy1 - hy0)};
    while (exts.back() > 4 * PSZ && levels.back().w / 2 >= 3 * PSZ && levels.back().h / 2 >= 3 * PSZ) {
        levels.push_back(half(levels.back()));
        exts.push_back((exts.back() + 1) / 2);
    }
    const int nlev = (int)levels.size();
    std::vector<int> prevNnf; int pbx0 = 0, pby0 = 0, pbw = 0, pbh = 0, pw = 0;
    for (int li = nlev - 1; li >= 0; --li) {
        Level& L = levels[li];
        Solver S(L);
        if (!S.prepare()) { if (li == 0) return false; continue; }
        if (li == nlev - 1) onionPeel(L);
        else {   // start from the coarser level's pixels, scaled up
            const Level& C = levels[li + 1];
            for (int y = 0; y < L.h; ++y)
                for (int x = 0; x < L.w; ++x) {
                    const size_t i = (size_t)y * L.w + x;
                    if (!L.hole[i]) continue;
                    const size_t j = (size_t)std::min(C.h - 1, y / 2) * C.w + std::min(C.w - 1, x / 2);
                    for (int c = 0; c < 3; ++c) L.img[i * 3 + c] = C.img[j * 3 + c];
                }
        }
        // the match field: the coarser level's, scaled up where it lands on a valid source; random otherwise
        Rng rng(1 ^ ((uint64_t)li << 20));
        bool upsampled = false;
        for (int y = S.by0; y < S.by1; ++y)
            for (int x = S.bx0; x < S.bx1; ++x) {
                const size_t k = (size_t)(y - S.by0) * S.bw() + (x - S.bx0);
                if (!S.target[k]) continue;
                int m = -1;
                if (!prevNnf.empty()) {
                    const int cx = x / 2 - pbx0, cy = y / 2 - pby0;
                    if (cx >= 0 && cy >= 0 && cx < pbw && cy < pbh) {
                        const int s = prevNnf[(size_t)cy * pbw + cx];
                        if (s >= 0) {
                            const int sx = x + 2 * (s % pw - x / 2), sy = y + 2 * (s / pw - y / 2);
                            if (sx >= 0 && sy >= 0 && sx < L.w && sy < L.h && S.valid[(size_t)sy * L.w + sx]) { m = sy * L.w + sx; upsampled = true; }
                        }
                    }
                }
                S.nnf[k] = m >= 0 ? m : S.validList[rng.next() % S.validList.size()];
            }
        if (upsampled) { S.costs(); S.vote(); }
        const bool refineOnly = li + 1 < nlev && exts[li] > REFINE_EXTENT;
        const int em = li == nlev - 1 ? EM_ITERS : refineOnly ? 1 : std::max(MIN_EM_ITERS, EM_ITERS >> (nlev - 1 - li));
        const int pmIters = refineOnly ? 1 : PM_ITERS;
        const int full = std::max(L.w, L.h);
        for (int it = 0; it < em; ++it) {
            S.costs();
            const int radius = refineOnly ? 2 : (li == nlev - 1 || it == 0) ? full : std::min(4 * PSZ, full);
            for (int pass = 0; pass < pmIters; ++pass) S.patchmatch(pass + it * pmIters, (uint64_t)(li * 1000 + it) + 1, radius);
            S.vote();
        }
        if (li == 0) S.sharpen();
        prevNnf.assign(S.nnf.size(), -1);
        for (size_t k = 0; k < S.nnf.size(); ++k) if (S.target[k]) prevNnf[k] = S.nnf[k];
        pbx0 = S.bx0; pby0 = S.by0; pbw = S.bw(); pbh = S.by1 - S.by0; pw = L.w;
    }
    for (size_t i = 0; i < hole.size(); ++i) if (hole[i]) for (int c = 0; c < 3; ++c) img[i * 3 + c] = levels[0].img[i * 3 + c];
    return true;
}

void membrane(int w, int h, std::vector<float>& v, const std::vector<uint8_t>& unknown) {
    // Gauss-Seidel with over-relaxation, from a coarse start (the mean of the edge), until it settles
    std::vector<int> cells;
    double edge = 0; int ne = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            if (unknown[i]) cells.push_back((int)i);
            else {
                bool nearHole = false;
                for (int k = 0; k < 4 && !nearHole; ++k) { const int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3); if (nx >= 0 && ny >= 0 && nx < w && ny < h && unknown[(size_t)ny * w + nx]) nearHole = true; }
                if (nearHole) { edge += v[i]; ++ne; }
            }
        }
    if (cells.empty()) return;
    const float start = ne ? (float)(edge / ne) : 0.0f;
    for (int i : cells) v[i] = start;
    const float omega = 1.9f;
    for (int iter = 0; iter < 2000; ++iter) {
        float change = 0;
        for (int i : cells) {
            const int x = i % w, y = i / w;
            float s = 0; int n = 0;
            if (x > 0) { s += v[i - 1]; ++n; } if (x + 1 < w) { s += v[i + 1]; ++n; }
            if (y > 0) { s += v[i - w]; ++n; } if (y + 1 < h) { s += v[i + w]; ++n; }
            if (!n) continue;
            const float nv = v[i] + omega * (s / n - v[i]);
            change = std::max(change, std::fabs(nv - v[i]));
            v[i] = nv;
        }
        if (change < 1e-4f) break;
    }
}

}
