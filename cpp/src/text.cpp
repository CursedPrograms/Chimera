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
#include <cwctype>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

// after PhotoCraft (crates/text/src/layout/navigate.rs word_boundary): skip separators then
// consume the adjacent alphanumeric run, counting Unicode code points rather than UTF-8 bytes.
size_t textWordBoundary(const std::string& text, size_t index, bool forward) {
    std::vector<std::pair<size_t, uint32_t>> chars;
    for (size_t i=0; i<text.size();) {
        const size_t start=i; const unsigned char c=static_cast<unsigned char>(text[i++]); uint32_t cp=c;
        if (c>=0xF0 && i+2<text.size()) { cp=(c&7u)<<18; cp|=(static_cast<unsigned char>(text[i++])&63u)<<12; cp|=(static_cast<unsigned char>(text[i++])&63u)<<6; cp|=static_cast<unsigned char>(text[i++])&63u; }
        else if (c>=0xE0 && i+1<text.size()) { cp=(c&15u)<<12; cp|=(static_cast<unsigned char>(text[i++])&63u)<<6; cp|=static_cast<unsigned char>(text[i++])&63u; }
        else if (c>=0xC0 && i<text.size()) { cp=(c&31u)<<6; cp|=static_cast<unsigned char>(text[i++])&63u; }
        chars.push_back({start,cp});
    }
    auto alnum=[](uint32_t cp) {
        if (cp<128) return std::isalnum(static_cast<unsigned char>(cp))!=0;
        if (cp<=static_cast<uint32_t>(WCHAR_MAX)) return std::iswalnum(static_cast<wint_t>(cp))!=0;
        return false;
    };
    size_t i=std::min(index,chars.size());
    if(forward) { while(i<chars.size()&&!alnum(chars[i].second))++i; while(i<chars.size()&&alnum(chars[i].second))++i; }
    else { while(i>0&&!alnum(chars[i-1].second))--i; while(i>0&&alnum(chars[i-1].second))--i; }
    return i;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs smart_quotes): quotes open after
// whitespace, brackets and dashes; elsewhere they close. Existing non-ASCII text is preserved.
std::string textSmartQuotes(const std::string& text) {
    std::string out; out.reserve(text.size());
    auto opening = [](unsigned char c) { return std::isspace(c) || c=='(' || c=='[' || c=='{' || c=='-' || c=='/'; };
    for(size_t i=0;i<text.size();++i) {
        const char c=text[i];
        if(c=='"' || c=='\'') {
            const bool open=out.empty() || opening(static_cast<unsigned char>(out.back()));
            if(c=='"') out += open ? "\xe2\x80\x9c" : "\xe2\x80\x9d";
            else out += open ? "\xe2\x80\x98" : "\xe2\x80\x99";
        } else out.push_back(c);
    }
    return out;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/defaults.rs type_unit): points are the
// model unit, pixels follow document resolution, and millimetres remain physical units.
TextUnit textUnit(int units, float dpi) {
    const float safeDpi=std::isfinite(dpi)?std::max(1.0f,dpi):72.0f;
    if(units==1) return {safeDpi/72.0f,"px"};
    if(units==2) return {25.4f/72.0f,"mm"};
    return {1.0f,"pt"};
}

std::string Text::save() const {
    // Text settings and a path's editable Bezier knots live together in the project layer.
    auto hex = [](const std::string& s) { std::string o; char b[3]; for (unsigned char c : s) { snprintf(b, sizeof b, "%02x", c); o += b; } return o.empty() ? std::string("-") : o; };
    Shape pathShape; pathShape.kind = SHAPE_PATH; pathShape.path = path;
    const std::string pathData = pathText ? pathShape.save() : std::string();
    char b[256];
    snprintf(b, sizeof b, " %g %g %g %d %d %g %g %g %g %g %g %g %g %d %d %g", size, x, y, align, vertical ? 1 : 0, tracking, leading, boxWidth, boxHeight, scaleX, scaleY, rotation, pathStart, pathText ? 1 : 0, pathFlip ? 1 : 0, pathEnd);
    std::string runData, paraData;
    for (const TextRun& r : runs) {
        char q[256]; snprintf(q, sizeof q, "%zu,%g,%g,%g,%g,%g,%g,%d,%d,%d,%d,", r.bytes, r.style.size, r.style.tracking, r.style.leading,
            r.style.baselineShift, r.style.horizontalScale, r.style.verticalScale, r.style.bold, r.style.italic, r.style.underline, r.style.strikethrough);
        runData += q + hex(r.style.font) + ";";
    }
    for (const ParagraphRun& r : paragraphs) { char q[180]; snprintf(q, sizeof q, "%zu,%d,%g,%g,%g,%g,%g,%g;", r.bytes, r.style.align, r.style.firstIndent, r.style.startIndent, r.style.endIndent, r.style.spaceBefore, r.style.spaceAfter, r.style.autoLeading); paraData += q; }
    return "text3 " + hex(str) + " " + hex(font) + " " + hex(pathData) + b + " " + hex(runData) + " " + hex(paraData);
}
bool Text::load(const std::string& blob) {
    auto unhex = [](const std::string& h) { std::string o; if (h.empty() || h[0] == '-') return o; for (size_t i = 0; i + 1 < h.size(); i += 2) { char b[3] = {h[i], h[i + 1], 0}; o += (char)strtol(b, nullptr, 16); } return o; };
    std::string version, textHex, fontHex; int al = 0, v = 0;
    if (blob.rfind("text3 ", 0) == 0 || blob.rfind("text2 ", 0) == 0) {
        const bool v3 = blob.rfind("text3 ", 0) == 0;
        std::string pathHex; int onPath = 0, flipped = 0;
        std::istringstream in(blob);
        if (!(in >> version >> textHex >> fontHex >> pathHex >> size >> x >> y >> al >> v >> tracking >> leading >> boxWidth >> boxHeight >> scaleX >> scaleY >> rotation >> pathStart >> onPath >> flipped)) return false;
        if (!(in >> pathEnd)) pathEnd = -1;
        pathText = onPath != 0; pathFlip = flipped != 0; path.clear();
        const std::string pathData = unhex(pathHex);
        if (!pathData.empty()) { Shape shape; if (!shape.load(pathData) || shape.kind != SHAPE_PATH) return false; path = std::move(shape.path); }
        runs.clear(); paragraphs.clear();
        if (v3) {
            std::string rh, ph; if (!(in >> rh >> ph)) return false;
            std::istringstream ri(unhex(rh)); std::string item;
            while (std::getline(ri, item, ';')) { if (item.empty()) continue; std::istringstream fields(item); TextRun r; std::string fhex; int bo=0,it=0,un=0,st=0; char comma;
                if (!(fields >> r.bytes >> comma >> r.style.size >> comma >> r.style.tracking >> comma >> r.style.leading >> comma >> r.style.baselineShift >> comma >> r.style.horizontalScale >> comma >> r.style.verticalScale >> comma >> bo >> comma >> it >> comma >> un >> comma >> st >> comma)) return false;
                if (!(fields >> fhex)) return false; r.style.font = unhex(fhex); r.style.bold=bo!=0; r.style.italic=it!=0; r.style.underline=un!=0; r.style.strikethrough=st!=0; runs.push_back(std::move(r));
            }
            std::istringstream pi(unhex(ph));
            while (std::getline(pi, item, ';')) { if (item.empty()) continue; std::istringstream fields(item); ParagraphRun r; char comma;
                if (!(fields >> r.bytes >> comma >> r.style.align >> comma >> r.style.firstIndent >> comma >> r.style.startIndent >> comma >> r.style.endIndent >> comma >> r.style.spaceBefore >> comma >> r.style.spaceAfter >> comma >> r.style.autoLeading)) return false;
                paragraphs.push_back(r);
            }
        }
    } else {
        char s1[65536], s2[4096];
        const int fields = sscanf(blob.c_str(), "text1 %65535s %4095s %f %f %f %d %d %f %f %f %f %f %f %f", s1, s2, &size, &x, &y, &al, &v, &tracking, &leading, &boxWidth, &boxHeight, &scaleX, &scaleY, &rotation);
        if (fields != 9 && fields != 11 && fields != 14) return false;
        textHex = s1; fontHex = s2;
        if (fields == 9) boxWidth = boxHeight = 0;
        if (fields != 14) scaleX = scaleY = 1, rotation = 0;
        path.clear(); pathText = pathFlip = false; pathStart = 0; pathEnd = -1;
    }
    boxWidth = std::max(0.0f, boxWidth); boxHeight = std::max(0.0f, boxHeight);
    scaleX = std::clamp(scaleX, 0.01f, 100.0f); scaleY = std::clamp(scaleY, 0.01f, 100.0f);
    if (!std::isfinite(rotation)) rotation = 0;
    if (!std::isfinite(pathStart)) pathStart = 0;
    if (!std::isfinite(pathEnd)) pathEnd = -1;
    str = unhex(textHex); font = unhex(fontHex); align = std::clamp(al, 0, 2); vertical = v != 0;
    return true;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::char_runs): runs always cover the text and
// their boundaries are UTF-8 character boundaries; any missing tail inherits the layer style.
std::vector<TextRun> Text::normalizedRuns() const {
    TextStyle base; base.font=font; base.size=size; base.tracking=tracking; base.leading=leading;
    std::vector<TextRun> out;
    auto boundary = [&](size_t n) { n=std::min(n,str.size()); while (n<str.size() && n && (static_cast<unsigned char>(str[n])&0xc0)==0x80) ++n; return n; };
    size_t raw=0, at=0;
    if(str.empty()) return {{0,runs.empty()?base:runs.front().style}};
    for (const TextRun& run : runs) {
        const size_t end=boundary(run.bytes>str.size()-std::min(raw,str.size())?str.size():raw+run.bytes);
        if(end>at) out.push_back({end-at,run.style});
        at=end;
        if(raw<str.size()) raw=std::min(str.size(),raw+std::min(run.bytes,str.size()-raw));
        if(raw>=str.size()) break;
    }
    if(at<str.size()) { const TextStyle tail=out.empty()?(runs.empty()?base:runs.back().style):out.back().style; if(!out.empty()&&out.back().style==tail) out.back().bytes+=str.size()-at; else out.push_back({str.size()-at,tail}); }
    return out;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::paragraph_runs): paragraph runs cover text bytes.
std::vector<ParagraphRun> Text::normalizedParagraphs() const {
    std::vector<ParagraphRun> out; size_t raw=0,at=0;
    if(str.empty()) return {{0,paragraphs.empty()?ParagraphStyle{}:paragraphs.front().style}};
    for(const ParagraphRun& run:paragraphs) { const size_t limit=run.bytes>str.size()-std::min(raw,str.size())?str.size():raw+run.bytes; size_t end=limit; while(end<str.size()&&end&&(static_cast<unsigned char>(str[end])&0xc0)==0x80)++end; if(end>at)out.push_back({end-at,run.style}); at=end; raw=limit; if(raw>=str.size())break; }
    if(at<str.size()) { const ParagraphStyle tail=out.empty()?(paragraphs.empty()?ParagraphStyle{}:paragraphs.back().style):out.back().style; if(!out.empty()&&out.back().style.align==tail.align&&out.back().style.autoLeading==tail.autoLeading&&out.back().style.firstIndent==tail.firstIndent&&out.back().style.startIndent==tail.startIndent&&out.back().style.endIndent==tail.endIndent&&out.back().style.spaceBefore==tail.spaceBefore&&out.back().style.spaceAfter==tail.spaceAfter)out.back().bytes+=str.size()-at; else out.push_back({str.size()-at,tail}); }
    return out;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::sync_summary): simple layer fields mirror
// the first resolved character run so existing options controls show the start style.
void Text::syncSummary() {
    const auto normalized=normalizedRuns();
    if(normalized.empty()) return;
    font=normalized.front().style.font;
    size=normalized.front().style.size;
    tracking=normalized.front().style.tracking;
    leading=normalized.front().style.leading;
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

// after PhotoCraft (crates/text/src/warp.rs PathFrame): arc-length samples keep glyphs upright
// to the path tangent and make closed paths wrap their distance at the seam.
struct TextPathMetric {
    std::vector<float> points, cumulative;
    bool closed = false;
    float length = 0;
    bool sample(float at, float& x, float& y, float& tx, float& ty) const {
        if (points.size() < 4 || length <= 0) return false;
        if (closed) { at = std::fmod(at, length); if (at < 0) at += length; }
        else if (at < 0 || at > length) return false;
        const size_t n = points.size() / 2;
        size_t seg = 0;
        if (!closed && at >= length) seg = n - 2;
        else while (seg + 1 < cumulative.size() && cumulative[seg + 1] < at) ++seg;
        const size_t next = closed ? (seg + 1) % n : seg + 1;
        const float a = cumulative[seg], b = seg + 1 < cumulative.size() ? cumulative[seg + 1] : length;
        const float dx = points[next * 2] - points[seg * 2], dy = points[next * 2 + 1] - points[seg * 2 + 1];
        const float len = std::hypot(dx, dy); if (len <= 1e-6f) return false;
        const float f = std::clamp((at - a) / std::max(1e-6f, b - a), 0.0f, 1.0f);
        x = points[seg * 2] + dx * f; y = points[seg * 2 + 1] + dy * f; tx = dx / len; ty = dy / len;
        return true;
    }
};

static TextPathMetric pathMetric(const Text& t) {
    TextPathMetric metric;
    const auto flattened = flattenPath(t.path);
    size_t source = 0;
    while (source < t.path.size() && t.path[source].knots.empty()) ++source;
    if (source >= t.path.size() || flattened.empty()) return metric;
    metric.points = flattened.front(); metric.closed = t.path[source].closed;
    if (metric.points.size() < 4) return metric;
    if (t.pathFlip) {
        std::vector<float> reverse;
        for (size_t i = metric.points.size(); i >= 2; i -= 2) { reverse.push_back(metric.points[i - 2]); reverse.push_back(metric.points[i - 1]); }
        metric.points = std::move(reverse);
    }
    const size_t n = metric.points.size() / 2;
    metric.cumulative.resize(n);
    for (size_t i = 1; i < n; ++i) {
        metric.length += std::hypot(metric.points[i * 2] - metric.points[(i - 1) * 2], metric.points[i * 2 + 1] - metric.points[(i - 1) * 2 + 1]);
        metric.cumulative[i] = metric.length;
    }
    if (metric.closed) metric.length += std::hypot(metric.points[0] - metric.points[(n - 1) * 2], metric.points[1] - metric.points[(n - 1) * 2 + 1]);
    return metric;
}

float textAdvance(const Text& t) {
    Loaded* loaded = fontFor(t.font.empty() ? defaultFont() : t.font);
    if (!loaded) return 0;
    const float scale = stbtt_ScaleForPixelHeight(&loaded->info, std::max(1.0f, t.size));
    const float tracking = t.tracking / 1000.0f * t.size;
    const auto lines = linesOf(t.str);
    if (lines.empty() || t.vertical) return 0;
    const auto& line = lines.front(); float width = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&loaded->info, line[i], &advance, &bearing);
        width += advance * scale + (i + 1 < line.size() ? stbtt_GetCodepointKernAdvance(&loaded->info, line[i], line[i + 1]) * scale : 0) + tracking;
    }
    return std::max(0.0f, width);
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
    const TextPathMetric path = t.pathText ? pathMetric(t) : TextPathMetric();
    const bool pathOn = t.pathText && !t.vertical && path.length > 0;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    auto blit = [&](int cp, float gx, float gy, float pathCenter = std::numeric_limits<float>::quiet_NaN()) {   // one glyph, its origin at (gx, gy) (the baseline)
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
        if (pathOn && std::isfinite(pathCenter)) {
            const float naturalSpan = path.closed ? path.length : std::max(0.0f, path.length - t.pathStart);
            const float allowedSpan = t.pathEnd >= 0 ? std::min(naturalSpan, t.pathEnd - t.pathStart) : naturalSpan;
            if (pathCenter < 0 || pathCenter > allowedSpan) return;
            float cx = 0, cy = 0, tx = 0, ty = 0;
            if (!path.sample(t.pathStart + pathCenter, cx, cy, tx, ty)) return;
            const float nx = -ty, ny = tx, centerX = (ox + ox + bw) * 0.5f;
            float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
            for (int yy : {0, bh}) for (int xx : {0, bw}) {
                const float along = ox + xx - centerX, down = oy + yy - gy;
                const auto transformed = transform(cx + tx * along + nx * down, cy + ty * along + ny * down);
                const float px = transformed.first, py = transformed.second;
                left = std::min(left, px); top = std::min(top, py); right = std::max(right, px); bottom = std::max(bottom, py);
            }
            minX = std::min(minX, left); minY = std::min(minY, top); maxX = std::max(maxX, right); maxY = std::max(maxY, bottom);
            for (int yy = 0; yy < bh; ++yy) for (int xx = 0; xx < bw; ++xx) {
                const float along = ox + xx + 0.5f - centerX, down = oy + yy + 0.5f - gy;
                const auto transformed = transform(cx + tx * along + nx * down, cy + ty * along + ny * down);
                const int px = (int)std::floor(transformed.first), py = (int)std::floor(transformed.second);
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                const unsigned char alpha = bm[(size_t)yy * bw + xx]; if (!alpha) continue;
                float& out = cov[(size_t)py * w + px]; out = std::min(1.0f, out + alpha / 255.0f);
            }
            return;
        }
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
            for (size_t i = 0; i < L.size(); ++i) {
                const float adv = advanceOf(L[i], i + 1 < L.size() ? L[i + 1] : 0);
                blit(L[i], gx, gy, pathOn ? gx - t.x + adv * 0.5f : std::numeric_limits<float>::quiet_NaN());
                gx += adv;
            }
            // an empty line still takes room in the box
            if (!pathOn) { minX = std::min(minX, t.x); maxY = std::max(maxY, gy - desc * scale); }
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
