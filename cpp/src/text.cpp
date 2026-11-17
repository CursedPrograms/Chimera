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
    snprintf(b, sizeof b, " %g %g %g %d %d %g %g %g %g %g %g %g", size, x, y, align, vertical ? 1 : 0, tracking, leading, boxWidth, boxHeight, scaleX, scaleY, rotation);
    return "text1 " + hex(str) + " " + hex(font) + b;
}
bool Text::load(const std::string& blob) {
    char a[1], s1[65536], s2[4096]; int al = 0, v = 0;
    (void)a;
    const int fields = sscanf(blob.c_str(), "text1 %65535s %4095s %f %f %f %d %d %f %f %f %f %f %f %f", s1, s2, &size, &x, &y, &al, &v, &tracking, &leading, &boxWidth, &boxHeight, &scaleX, &scaleY, &rotation);
    if (fields != 9 && fields != 11 && fields != 14) return false;
    if (fields == 9) boxWidth = boxHeight = 0;
    if (fields != 14) scaleX = scaleY = 1, rotation = 0;
    boxWidth = std::max(0.0f, boxWidth); boxHeight = std::max(0.0f, boxHeight);
    scaleX = std::clamp(scaleX, 0.01f, 100.0f); scaleY = std::clamp(scaleY, 0.01f, 100.0f);
    if (!std::isfinite(rotation)) rotation = 0;
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

// after PhotoCraft (crates/text paragraph layout): wrap at whitespace without splitting a word.
static void wrapLines(std::vector<std::vector<int>>& lines, const stbtt_fontinfo* fi, float scale, float track, float width) {
    if (width <= 0) return;
    auto advance = [&](int cp, int next) {
        int adv = 0, lsb = 0; stbtt_GetCodepointHMetrics(fi, cp, &adv, &lsb);
        return adv * scale + (next ? stbtt_GetCodepointKernAdvance(fi, cp, next) * scale : 0) + track;
    };
    std::vector<std::vector<int>> wrapped;
    for (const auto& source : lines) {
        std::vector<int> line;
        size_t at = 0;
        while (at < source.size()) {
            while (at < source.size() && (source[at] == ' ' || source[at] == '\t')) ++at;
            if (at == source.size()) break;
            size_t end = at;
            while (end < source.size() && source[end] != ' ' && source[end] != '\t') ++end;
            std::vector<int> word(source.begin() + at, source.begin() + end);
            float used = 0;
            for (size_t i = 0; i < line.size(); ++i) used += advance(line[i], i + 1 < line.size() ? line[i + 1] : (word.empty() ? 0 : word[0]));
            float wordWidth = 0;
            for (size_t i = 0; i < word.size(); ++i) wordWidth += advance(word[i], i + 1 < word.size() ? word[i + 1] : 0);
            const float space = line.empty() ? 0 : advance(' ', word.empty() ? 0 : word[0]);
            if (!line.empty() && used + space + wordWidth > width) { wrapped.push_back(std::move(line)); line.clear(); }
            else if (!line.empty()) line.push_back(' ');
            line.insert(line.end(), word.begin(), word.end());
            at = end;
        }
        wrapped.push_back(std::move(line));
    }
    lines = std::move(wrapped);
}

bool textCoverage(const Text& t, int w, int h, std::vector<float>& cov, float box[4]) {
    cov.assign((size_t)w * h, 0.0f);
    box[0] = t.x; box[1] = t.y; box[2] = t.x; box[3] = t.y;
    Loaded* F = fontFor(t.font.empty() ? defaultFont() : t.font);
    if (!F) return false;
    const stbtt_fontinfo* fi = &F->info;
    const float scale = stbtt_ScaleForPixelHeight(fi, std::max(1.0f, t.size));
    const float transformX = std::clamp(std::isfinite(t.scaleX) ? t.scaleX : 1.0f, 0.01f, 100.0f);
    const float transformY = std::clamp(std::isfinite(t.scaleY) ? t.scaleY : 1.0f, 0.01f, 100.0f);
    const float rotation = std::isfinite(t.rotation) ? t.rotation : 0.0f;
    int asc, desc, gap; stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    const float lineH = t.size * std::max(0.5f, t.leading), track = t.tracking / 1000.0f * t.size;
    auto lines = linesOf(t.str);
    if (!t.vertical) wrapLines(lines, fi, scale, track, t.boxWidth);
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
        const float angle = rotation * 0.017453292519943295f, cs = std::cos(angle), sn = std::sin(angle);
        auto transform = [&](float x, float y) {
            const float dx = (x - t.x) * transformX, dy = (y - t.y) * transformY;
            return std::pair<float, float>(t.x + cs * dx - sn * dy, t.y + sn * dx + cs * dy);
        };
        const auto corners = {transform((float)ox, (float)oy), transform((float)(ox + bw), (float)oy),
                              transform((float)(ox + bw), (float)(oy + bh)), transform((float)ox, (float)(oy + bh))};
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        for (const auto& p : corners) { left = std::min(left, p.first); top = std::min(top, p.second); right = std::max(right, p.first); bottom = std::max(bottom, p.second); }
        minX = std::min(minX, left); minY = std::min(minY, top); maxX = std::max(maxX, right); maxY = std::max(maxY, bottom);
        const int dx0 = std::max(0, (int)std::floor(left)), dy0 = std::max(0, (int)std::floor(top));
        const int dx1 = std::min(w, (int)std::ceil(right)), dy1 = std::min(h, (int)std::ceil(bottom));
        for (int cy = dy0; cy < dy1; ++cy) {
            for (int cx = dx0; cx < dx1; ++cx) {
                const float px = cx + 0.5f - t.x, py = cy + 0.5f - t.y;
                const float sourceX = t.x + (cs * px + sn * py) / transformX;
                const float sourceY = t.y + (-sn * px + cs * py) / transformY;
                const int bx = (int)std::floor(sourceX - ox), by = (int)std::floor(sourceY - oy);
                if (bx < 0 || bx >= bw || by < 0 || by >= bh) continue;
                const unsigned char alpha = bm[(size_t)by * bw + bx];
                if (!alpha) continue;
                float& o = cov[(size_t)cy * w + cx];
                o = std::min(1.0f, o + alpha / 255.0f);
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
