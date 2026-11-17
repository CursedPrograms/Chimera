// Chimera - text layers (see text.h)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace fs = std::filesystem;

std::string Text::save() const {
    // the words and the font as hex (any bytes), the rest as numbers
    auto hex = [](const std::string& s) { std::string o; char b[3]; for (unsigned char c : s) { snprintf(b, sizeof b, "%02x", c); o += b; } return o.empty() ? std::string("-") : o; };
    char b[256];
    snprintf(b, sizeof b, " %g %g %g %d %d %g %g", size, x, y, align, vertical ? 1 : 0, tracking, leading);
    return "text1 " + hex(str) + " " + hex(font) + b;
}
bool Text::load(const std::string& blob) {
    char a[1], s1[65536], s2[4096]; int al = 0, v = 0;
    (void)a;
    if (sscanf(blob.c_str(), "text1 %65535s %4095s %f %f %f %d %d %f %f", s1, s2, &size, &x, &y, &al, &v, &tracking, &leading) != 9) return false;
    auto unhex = [](const char* h) { std::string o; if (h[0] == '-') return o; for (size_t i = 0; h[i] && h[i + 1]; i += 2) { char b[3] = {h[i], h[i + 1], 0}; o += (char)strtol(b, nullptr, 16); } return o; };
    str = unhex(s1); font = unhex(s2); align = std::clamp(al, 0, 2); vertical = v != 0;
    return true;
}

const std::vector<FontFile>& systemFonts() {
    static std::vector<FontFile> list;
    static bool done = false;
    if (done) return list;
    done = true;
    std::vector<fs::path> dirs;
    if (const char* w = std::getenv("WINDIR")) dirs.push_back(fs::path(w) / "Fonts");
    if (const char* l = std::getenv("LOCALAPPDATA")) dirs.push_back(fs::path(l) / "Microsoft" / "Windows" / "Fonts");
    dirs.push_back("/usr/share/fonts"); dirs.push_back("/usr/local/share/fonts");
    std::error_code ec;
    for (const auto& d : dirs) {
        for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::string ext = it->path().extension().u8string(); for (auto& c : ext) c = (char)tolower((unsigned char)c);
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            // its family and style names from the file ("Segoe UI Bold"), else the file's name
            std::ifstream f(it->path(), std::ios::binary);
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), {});
            std::string name;
            stbtt_fontinfo fi;
            const int off = stbtt_GetFontOffsetForIndex(data.data(), 0);
            if (off >= 0 && stbtt_InitFont(&fi, data.data(), off)) {
                auto get = [&](int id) {
                    int len = 0;
                    const char* n = stbtt_GetFontNameString(&fi, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, id);
                    std::string o;
                    for (int i = 0; n && i + 1 < len; i += 2) { const int c = ((unsigned char)n[i] << 8) | (unsigned char)n[i + 1]; o += c < 128 ? (char)c : '?'; }
                    return o;
                };
                const std::string fam = get(1), sub = get(2);
                name = fam.empty() ? "" : (sub.empty() || sub == "Regular" ? fam : fam + " " + sub);
            }
            if (name.empty()) name = it->path().stem().u8string();
            list.push_back({it->path().u8string(), name});
        }
        ec.clear();
    }
    std::sort(list.begin(), list.end(), [](const FontFile& a, const FontFile& b) { return a.name < b.name; });
    list.erase(std::unique(list.begin(), list.end(), [](const FontFile& a, const FontFile& b) { return a.name == b.name; }), list.end());
    return list;
}
std::string defaultFont() {
    for (const char* f : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
        std::error_code ec;
        if (fs::exists(f, ec)) return f;
    }
    const auto& all = systemFonts();
    return all.empty() ? std::string() : all.front().path;
}

// loaded fonts, kept
struct Loaded { std::vector<unsigned char> data; stbtt_fontinfo info; bool ok = false; };
static Loaded* fontFor(const std::string& path) {
    static std::map<std::string, Loaded> cache;
    static std::mutex lock;
    std::lock_guard<std::mutex> g(lock);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.ok ? &it->second : nullptr;
    Loaded& L = cache[path];
    std::ifstream f(fs::u8path(path), std::ios::binary);
    L.data.assign(std::istreambuf_iterator<char>(f), {});
    const int off = L.data.empty() ? -1 : stbtt_GetFontOffsetForIndex(L.data.data(), 0);
    L.ok = off >= 0 && stbtt_InitFont(&L.info, L.data.data(), off);
    return L.ok ? &L : nullptr;
}

static std::vector<std::vector<int>> linesOf(const std::string& s) {   // UTF-8 to code points, by line
    std::vector<std::vector<int>> lines(1);
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        int cp = c, n = 1;
        if (c >= 0xF0) { cp = c & 7; n = 4; } else if (c >= 0xE0) { cp = c & 15; n = 3; } else if (c >= 0xC0) { cp = c & 31; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 63);
        i += n;
        if (cp == '\n') lines.emplace_back(); else if (cp != '\r') lines.back().push_back(cp);
    }
    return lines;
}

bool textCoverage(const Text& t, int w, int h, std::vector<float>& cov, float box[4]) {
    cov.assign((size_t)w * h, 0.0f);
    box[0] = t.x; box[1] = t.y; box[2] = t.x; box[3] = t.y;
    Loaded* F = fontFor(t.font.empty() ? defaultFont() : t.font);
    if (!F) return false;
    const stbtt_fontinfo* fi = &F->info;
    const float scale = stbtt_ScaleForPixelHeight(fi, std::max(1.0f, t.size));
    int asc, desc, gap; stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    const float lineH = t.size * std::max(0.5f, t.leading), track = t.tracking / 1000.0f * t.size;
    const auto lines = linesOf(t.str);
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    auto blit = [&](int cp, float gx, float gy) {   // one glyph, its origin at (gx, gy) (the baseline)
        const float sx = gx - std::floor(gx);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(fi, cp, scale, scale, sx, 0, &x0, &y0, &x1, &y1);
        const int bw = x1 - x0, bh = y1 - y0;
        if (bw <= 0 || bh <= 0) return;
        std::vector<unsigned char> bm((size_t)bw * bh);
        stbtt_MakeCodepointBitmapSubpixel(fi, bm.data(), bw, bh, bw, scale, scale, sx, 0, cp);
        const int ox = (int)std::floor(gx) + x0, oy = (int)std::round(gy) + y0;
        minX = std::min(minX, (float)ox); minY = std::min(minY, (float)oy); maxX = std::max(maxX, (float)(ox + bw)); maxY = std::max(maxY, (float)(oy + bh));
        for (int y = 0; y < bh; ++y) {
            const int cy = oy + y; if (cy < 0 || cy >= h) continue;
            for (int x = 0; x < bw; ++x) {
                const int cx = ox + x; if (cx < 0 || cx >= w) continue;
                float& o = cov[(size_t)cy * w + cx];
                o = std::min(1.0f, o + bm[(size_t)y * bw + x] / 255.0f);
            }
        }
    };
    auto advanceOf = [&](int cp, int next) {
        int adv, lsb; stbtt_GetCodepointHMetrics(fi, cp, &adv, &lsb);
        return adv * scale + (next ? stbtt_GetCodepointKernAdvance(fi, cp, next) * scale : 0) + track;
    };
    if (!t.vertical) {
        // each line's width, for centring / right
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& L = lines[li];
            float wsum = 0; for (size_t i = 0; i < L.size(); ++i) wsum += advanceOf(L[i], i + 1 < L.size() ? L[i + 1] : 0);
            float gx = t.x - (t.align == 1 ? wsum / 2 : t.align == 2 ? wsum : 0);
            const float gy = t.y + asc * scale + li * lineH;
            for (size_t i = 0; i < L.size(); ++i) { blit(L[i], gx, gy); gx += advanceOf(L[i], i + 1 < L.size() ? L[i + 1] : 0); }
            // an empty line still takes room in the box
            minX = std::min(minX, t.x); maxY = std::max(maxY, gy - desc * scale);
        }
    } else {
        // letters top to bottom, each centred on its column; lines right to left from x
        const float colW = lineH;
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& L = lines[li];
            const float step = t.size * 1.0f + track;
            const float total = L.size() * step;
            float gy = t.y - (t.align == 1 ? total / 2 : t.align == 2 ? total : 0) + asc * scale;
            const float cx = t.x - li * colW;
            for (size_t i = 0; i < L.size(); ++i) {
                int adv, lsb; stbtt_GetCodepointHMetrics(fi, L[i], &adv, &lsb);
                blit(L[i], cx - adv * scale / 2, gy);
                gy += step;
            }
        }
    }
    if (minX <= maxX) { box[0] = minX; box[1] = minY; box[2] = maxX; box[3] = maxY; }
    else { box[0] = t.x; box[1] = t.y; box[2] = t.x + t.size * 0.6f; box[3] = t.y + t.size; }
    return true;
}
