// Layer group checks ported from PhotoCraft (crates/doc/src/lib.rs and crates/compose/src/tests.rs).
#include "doc.h"
#include "blend.h"
#include "text.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <numeric>
#include <vector>

namespace App {
bool typeMoveDocumentEdge(ImGuiInputTextState* state, bool end, int cursor, int anchor, bool hadSelection, bool extend);
bool typeMoveVerticalCaret(ImGuiInputTextState* state, const std::string& text, int cursor, int selectionStart, int selectionEnd, int selectionAnchor, bool hadSelection, int direction, bool extend);
bool typeMoveEditorLineEdge(ImGuiInputTextState* state, const std::string& text, bool end, int cursor, int anchor, bool hadSelection, bool extend);
bool typeStepCharacterMetric(Text& text, size_t start, size_t end, int metric, float by, float dpi, float layerScaleY);
bool typeToggleCharacterStyle(Text& text, size_t start, size_t end, int styleIndex);
bool typeApplyEditorText(Text& text, const std::string& updated);
bool typeSetCharacterSize(Text& text, size_t start, size_t end, float size);
bool typeSetCharacterTracking(Text& text, size_t start, size_t end, float tracking);
bool typeSetCharacterFont(Text& text, size_t start, size_t end, const std::string& font);
bool typeSetCharacterMetricValue(Text& text, size_t start, size_t end, int metric, float value, float dpi, float layerScaleY);
int cropOptionKeyAction(bool pending, bool defaultFrame, bool dragging, bool shift, bool alt, bool ctrl, bool superKey, int key, bool& classicMode, bool& showArea);
bool cropShieldShowsOutside(bool showArea, bool classicMode);
bool cropHistoryRecord(std::vector<std::array<float,6>>& undo, std::vector<std::array<float,6>>& redo, const std::array<float,6>& before, const std::array<float,6>& after);
bool cropHistoryStep(std::vector<std::array<float,6>>& undo, std::vector<std::array<float,6>>& redo, bool isRedo, std::array<float,6>& current);
std::array<std::string,3> cropFrontImageFields(int width, int height, float dpi, bool resolutionCm);
bool cropFrontImageKeyAllowed(bool cropTool, bool dragging, bool textInput, bool shift, bool alt, bool ctrl, bool superKey);
bool cropRestoreFrameState(bool& pending, bool& dismissed, std::array<float,4>& rect, float width, float height);
bool typeMoveWordCaret(ImGuiInputTextState* state, const std::string& text, int cursor, int anchor, bool hadSelection, bool forward, bool extend);
bool typeWordNavigationShortcut(bool ctrl, bool alt, bool shift, bool mac, bool super);
bool typeDocumentNavigationShortcut(bool ctrl, bool alt, bool mac, bool super);
bool typePrepareModifiedDelete(ImGuiInputTextState* state, const std::string& text, bool backspace, bool ctrl, bool alt, bool mac, bool super);
bool typeApplyClickSelection(ImGuiInputTextState* state, const std::string& text, unsigned count);
unsigned typeNextClickCount(unsigned previous, unsigned imguiCount);
}

static Layer solidLayer(const Doc& d, const char* name, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Layer layer = emptyLayer(d, name);
    for (size_t i = 0; i < layer.img.px.size(); i += 4) {
        layer.img.px[i] = r; layer.img.px[i + 1] = g; layer.img.px[i + 2] = b; layer.img.px[i + 3] = a;
    }
    return layer;
}

// PhotoCraft's group opacity applies once to the finished children, not once to each child.
static bool groupOpacityAppliesOnce() {
    Doc d = newDoc(2, 2, false);
    d.layers.push_back(solidLayer(d, "red", 255, 0, 0));
    d.layers.push_back(solidLayer(d, "blue", 0, 0, 255));
    if (!groupLayers(d, {1, 2})) return false;
    d.groups.back().opacity = 0.5f;
    Image out; composite(d, out, {0, 0, d.w, d.h}, false);
    const uint8_t* p = out.at(0, 0);
    return p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] >= 127 && p[3] <= 128;
}

// A group mask is applied to the isolated result after its children composite.
static bool groupMaskRestrictsComposite() {
    Doc d = newDoc(2, 1, false);
    d.layers.push_back(solidLayer(d, "red", 255, 0, 0));
    d.layers.push_back(solidLayer(d, "blue", 0, 0, 255));
    if (!groupLayers(d, {1, 2})) return false;
    LayerGroup& group = d.groups.back();
    group.hasMask = true;
    group.mask = {255, 0};
    Image out; composite(d, out, {0, 0, d.w, d.h}, false);
    return out.at(0, 0)[3] == 255 && out.at(1, 0)[3] == 0;
}

// A group must contain one contiguous selection in the current flat stack representation.
static bool rejectsNonContiguousSelection() {
    Doc d = newDoc(1, 1, false);
    d.layers.push_back(solidLayer(d, "a", 255, 0, 0));
    d.layers.push_back(solidLayer(d, "b", 0, 255, 0));
    d.layers.push_back(solidLayer(d, "c", 0, 0, 255));
    return !groupLayers(d, {1, 3}) && d.groups.empty();
}

// PhotoCraft's groups_walk_and_paths: each layer path is its sibling index at every depth.
static bool nestedGroupsWalkAndLock() {
    Doc d = newDoc(1, 1, false);
    d.layers.push_back(solidLayer(d, "a", 255, 0, 0));
    d.layers.push_back(solidLayer(d, "b", 0, 0, 255));
    if (!groupLayers(d, {1, 2})) return false;
    const int outer = d.groups.back().id;
    if (!groupLayers(d, {1, 2})) return false;
    const int inner = d.groups.back().id;
    d.groups[0].locked = true;
    const auto walk = layerTreeWalk(d);
    const auto path = layerPath(d, 1);
    const bool treeOk = maxGroupDepth(d) == 2 && walk.size() == 5 && !walk[0].group && walk[1].group && walk[1].id == outer &&
                        walk[2].group && walk[2].id == inner && path == std::vector<int>({1, 0, 0}) &&
                        groupPath(d, outer) == std::vector<int>({1}) && groupPath(d, inner) == std::vector<int>({1, 0}) && groupLocked(d, inner);
    LayerGroup empty; empty.id = 99; d.groups.push_back(empty);
    return treeOk && maxGroupDepth(d) == 2;
}

// PhotoCraft's isolated_group_differs_from_pass_through: Multiply sees the document below only in pass-through mode.
static bool groupPassThroughAndIsolation() {
    auto render = [](bool passThrough) {
        Doc d = newDoc(1, 1, false);
        d.layers[0] = solidLayer(d, "gray backdrop", 128, 128, 128);
        d.layers.push_back(solidLayer(d, "gray multiply", 128, 128, 128));
        d.layers[1].blend = BM_Multiply;
        if (!groupLayers(d, {1})) return Image();
        d.groups.back().passThrough = passThrough;
        Image out; composite(d, out, {0, 0, 1, 1}, false); return out;
    };
    const Image pass = render(true), isolated = render(false);
    return pass.w == 1 && isolated.w == 1 && pass.at(0, 0)[0] < 70 && isolated.at(0, 0)[0] > 115 && isolated.at(0, 0)[0] < 140;
}

// A clipped layer above a group clips against the group's composite alpha, not its top child's alpha.
static bool clippingUsesGroupComposite() {
    Doc d = newDoc(2, 1, false);
    d.layers.push_back(solidLayer(d, "left", 255, 0, 0)); d.layers[1].img.at(1, 0)[3] = 0;
    d.layers.push_back(solidLayer(d, "right", 0, 0, 255)); d.layers[2].img.at(0, 0)[3] = 0;
    if (!groupLayers(d, {1, 2})) return false;
    d.layers.push_back(solidLayer(d, "clipped", 0, 255, 0)); d.layers.back().clip = true;
    Image out; composite(d, out, {0, 0, 2, 1}, false);
    return out.at(0, 0)[1] == 255 && out.at(1, 0)[1] == 255;
}

// Group parent links, pass-through mode, locks and paths survive a project round trip.
static bool nestedGroupRoundTrip() {
    Doc d = newDoc(1, 1, false);
    d.layers.push_back(solidLayer(d, "a", 255, 0, 0)); d.layers.push_back(solidLayer(d, "b", 0, 0, 255));
    if (!groupLayers(d, {1, 2}) || !groupLayers(d, {1, 2})) return false;
    d.groups[0].locked = true;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "chimera-group-tree-selftest.chimera";
    std::string error; Doc loaded;
    const bool ok = saveProject(d, path.u8string(), error) && openDoc(path.u8string(), loaded, error);
    std::error_code ec; std::filesystem::remove(path, ec);
    return ok && loaded.groups.size() == 2 && maxGroupDepth(loaded) == 2 && loaded.groups[1].parentId == loaded.groups[0].id &&
           loaded.groups[0].locked && loaded.layers[1].groupId == loaded.groups[1].id && layerPath(loaded, 1) == std::vector<int>({1, 0, 0});
}

// A deep knockout replaces the previous stack composite with the Background before drawing its pixels.
static bool deepKnockoutUsesBackgroundFloor() {
    Doc d = newDoc(2, 1, true);
    d.layers.push_back(solidLayer(d, "green", 0, 255, 0));
    Layer knock = solidLayer(d, "blue", 0, 0, 255); knock.img.at(1, 0)[3] = 0; knock.knockout = KNOCKOUT_DEEP;
    d.layers.push_back(std::move(knock));
    Image out; composite(d, out, {0, 0, d.w, d.h}, false);
    return out.at(0, 0)[2] == 255 && out.at(0, 0)[1] == 0 && out.at(1, 0)[1] == 255;
}

// A pass-through group below full opacity isolates its adjustments, matching Photoshop's fill rule.
static bool partialPassThroughGroupIsIsolated() {
    auto render = [](float opacity) {
        Doc d = newDoc(1, 1, true);
        Layer inverse = emptyLayer(d, "Invert"); inverse.adj = Adjustment::make(ADJ_INVERT);
        d.layers.push_back(std::move(inverse));
        if (!groupLayers(d, {1})) return Image();
        d.groups.back().opacity = opacity;
        Image out; composite(d, out, {0, 0, 1, 1}, false); return out;
    };
    const Image full = render(1.0f), partial = render(0.4f);
    return full.w == 1 && partial.w == 1 && full.at(0, 0)[0] < 5 && partial.at(0, 0)[0] > 250;
}

// Group knockout follows the alpha of the finished group, including empty pixels outside its children.
static bool groupKnockoutUsesItsCompositeShape() {
    Doc d = newDoc(2, 1, true);
    d.layers.push_back(solidLayer(d, "green", 0, 255, 0));
    d.layers.push_back(solidLayer(d, "blue", 0, 0, 255)); d.layers.back().img.at(1, 0)[3] = 0;
    if (!groupLayers(d, {2})) return false;
    LayerGroup& group = d.groups.back(); group.passThrough = false; group.knockout = KNOCKOUT_DEEP;
    Image out; composite(d, out, {0, 0, 2, 1}, false);
    return out.at(0, 0)[2] == 255 && out.at(0, 0)[1] == 0 && out.at(1, 0)[1] == 255;
}

// PhotoCraft paragraph frames reflow the same words when their width changes.
static bool paragraphTextWrapsAndPersists() {
    Text point; point.str = "A paragraph box wraps words onto later lines"; point.size = 32;
    point.font = defaultFont();
    std::vector<float> pointPixels; float pointBox[4];
    if (!textCoverage(point, 500, 300, pointPixels, pointBox)) return false;
    Text scaled = point; scaled.scaleX = 1.5f; scaled.scaleY = 1.25f;
    std::vector<float> scaledPixels; float scaledBox[4];
    if (!textCoverage(scaled, 500, 300, scaledPixels, scaledBox)) return false;
    Text paragraph = point; paragraph.boxWidth = 90; paragraph.boxHeight = 100; paragraph.x = 160; paragraph.y = 100; paragraph.rotation = 90; paragraph.scaleX = 1.25f; paragraph.scaleY = 0.8f;
    std::vector<float> paragraphPixels; float paragraphBox[4];
    if (!textCoverage(paragraph, 500, 300, paragraphPixels, paragraphBox)) return false;
    Text roundTrip;
    if (!roundTrip.load(paragraph.save())) return false;
    Text legacy;
    if (!legacy.load("text1 41 - 48 10 20 0 0 0 1.2")) return false;
    Doc project = newDoc(500, 300, false); Fill fill; fill.kind = FILL_SOLID;
    addTextLayer(project, paragraph, fill);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "chimera-paragraph-text-selftest.chimera";
    std::string error; Doc loaded;
    const bool projectOk = saveProject(project, path.u8string(), error) && openDoc(path.u8string(), loaded, error);
    std::error_code ec; std::filesystem::remove(path, ec);
    const auto savedText = std::find_if(loaded.layers.begin(), loaded.layers.end(), [](const Layer& l) { return l.isText(); });
    return paragraphBox[3] - paragraphBox[1] > pointBox[3] - pointBox[1] + 10 &&
           scaledBox[2] - scaledBox[0] > (pointBox[2] - pointBox[0]) * 1.2f && scaledBox[3] - scaledBox[1] > (pointBox[3] - pointBox[1]) * 1.1f &&
           std::fabs(roundTrip.boxWidth - paragraph.boxWidth) < 0.01f && roundTrip.boxHeight == paragraph.boxHeight && roundTrip.rotation == paragraph.rotation && roundTrip.scaleX == paragraph.scaleX && roundTrip.scaleY == paragraph.scaleY &&
           legacy.boxWidth == 0 && legacy.boxHeight == 0 && projectOk && savedText != loaded.layers.end() &&
           savedText->text.boxWidth == paragraph.boxWidth && savedText->text.boxHeight == paragraph.boxHeight && savedText->text.rotation == paragraph.rotation && savedText->text.scaleX == paragraph.scaleX && savedText->text.scaleY == paragraph.scaleY;
}

// PhotoCraft type-path placement keeps an editable Bezier path and places glyphs on its tangent.
static bool textFollowsBezierPathAndPersists() {
    Subpath curve;
    Knot a; a.ax = 24; a.ay = 140; a.ox = 120; a.oy = 20;
    Knot b; b.ax = 476; b.ay = 140; b.ix = 380; b.iy = 20;
    curve.knots = {a, b};
    Text text; text.str = "Text follows a curve"; text.size = 34; text.path = {curve}; text.pathText = true; text.pathStart = 12;
    std::vector<float> coverage; float box[4];
    if (!textCoverage(text, 500, 300, coverage, box)) return false;
    const float ink = std::accumulate(coverage.begin(), coverage.end(), 0.0f);
    Text closed = text; closed.path[0].closed = true;
    const auto line = flattenPath(closed.path);
    float length = 0;
    for (size_t i = 2; i < line[0].size(); i += 2) length += std::hypot(line[0][i] - line[0][i - 2], line[0][i + 1] - line[0][i - 1]);
    length += std::hypot(line[0][0] - line[0][line[0].size() - 2], line[0][1] - line[0][line[0].size() - 1]);
    closed.pathStart = length - 40;
    Text open = closed; open.path[0].closed = false;
    std::vector<float> closedCoverage, openCoverage; float cb[4], ob[4];
    if (!textCoverage(closed, 500, 300, closedCoverage, cb) || !textCoverage(open, 500, 300, openCoverage, ob)) return false;
    const float closedInk = std::accumulate(closedCoverage.begin(), closedCoverage.end(), 0.0f);
    const float openInk = std::accumulate(openCoverage.begin(), openCoverage.end(), 0.0f);
    Text bounded = text; bounded.pathEnd = 180;
    Text decoded, oldText2;
    const std::string encoded = bounded.save();
    if (!decoded.load(encoded) || !oldText2.load("text2 41 - - 34 0 0 0 0 0 1.2 0 0 1 1 0 0 1 0") || oldText2.pathEnd != -1) return false;
    Doc project = newDoc(500, 300, false); Fill fill; fill.kind = FILL_SOLID; addTextLayer(project, text, fill);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "chimera-text-path-selftest.chimera";
    std::string error; Doc loaded;
    const bool saved = saveProject(project, path.u8string(), error) && openDoc(path.u8string(), loaded, error);
    std::error_code ec; std::filesystem::remove(path, ec);
    const auto layer = std::find_if(loaded.layers.begin(), loaded.layers.end(), [](const Layer& l) { return l.isText(); });
    const bool result=ink > 100 && box[3] < 180 && closedInk > openInk * 1.4f && decoded.pathText && decoded.path.size() == 1 && decoded.path[0].knots.size() == 2 &&
           decoded.pathEnd == bounded.pathEnd && saved && layer != loaded.layers.end() && layer->text.pathText && layer->text.path.size() == 1 && layer->text.pathStart == text.pathStart;
    return result;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_path_placement.rs effective_range): explicit
// end markers clip open paths, while a closed path interval can pass through its start seam.
static bool textPathPlacementRangeBehaves() {
    Subpath line; Knot a; a.ax = 16; a.ay = 80; a.ox = 76; a.oy = 80; Knot b; b.ax = 184; b.ay = 80; b.ix = 124; b.iy = 80; line.knots = {a, b};
    Text text; text.str = "Path range"; text.size = 30; text.path = {line}; text.pathText = true; text.pathStart = 115; text.pathEnd = 170;
    std::vector<float> full, shortRange; float box[4], shortBox[4];
    if (!textCoverage(text, 220, 140, full, box)) return false;
    Text shortText = text; shortText.pathEnd = 130;
    if (!textCoverage(shortText, 220, 140, shortRange, shortBox)) return false;
    const float fullInk = std::accumulate(full.begin(), full.end(), 0.0f), shortInk = std::accumulate(shortRange.begin(), shortRange.end(), 0.0f);
    Subpath loop; Knot p0; p0.ax=20;p0.ay=20;p0.ox=180;p0.oy=20; Knot p1;p1.ax=180;p1.ay=20;p1.ix=20;p1.iy=20;
    Knot p2;p2.ax=180;p2.ay=180;p2.ix=180;p2.iy=20; Knot p3;p3.ax=20;p3.ay=180;p3.ix=180;p3.iy=180;
    p0.ix=20;p0.iy=180; loop.knots={p0,p1,p2,p3}; loop.closed=true;
    Text seam=text; seam.path={loop}; seam.pathStart=615; seam.pathEnd=675;
    Text beforeSeam=seam; beforeSeam.pathStart=615; beforeSeam.pathEnd=635;
    std::vector<float> seamInk, beforeInk; float seamBox[4], beforeBox[4];
    if (!textCoverage(seam,220,220,seamInk,seamBox) || !textCoverage(beforeSeam,220,220,beforeInk,beforeBox)) return false;
    const float wrapInk=std::accumulate(seamInk.begin(),seamInk.end(),0.0f), clippedInk=std::accumulate(beforeInk.begin(),beforeInk.end(),0.0f);
    return fullInk > shortInk && shortInk > 0 && wrapInk > clippedInk && clippedInk > 0;
}

// after PhotoCraft (crates/text/src/layout.rs layout_paragraph): each paragraph uses its own alignment.
static bool textParagraphAlignmentAppliesPerParagraph() {
    Text text; text.str = "wide\nsmall"; text.size = 32; text.x = 20; text.y = 20; text.boxWidth = 320;
    ParagraphStyle centered; centered.align = 1;
    ParagraphStyle right; right.align = 2;
    text.paragraphs = {{5, centered}, {5, right}};
    constexpr int width = 400, height = 140;
    std::vector<float> coverage; float box[4];
    if (!textCoverage(text, width, height, coverage, box)) return false;
    int firstMin = width, firstMax = -1, secondMin = width, secondMax = -1;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (coverage[(size_t)y * width + x] <= 0) continue;
        if (y < 58) { firstMin = std::min(firstMin, x); firstMax = std::max(firstMax, x); }
        else { secondMin = std::min(secondMin, x); secondMax = std::max(secondMax, x); }
    }
    return firstMax >= firstMin && secondMax >= secondMin &&
        std::abs((firstMin + firstMax) * 0.5f - (text.x + text.boxWidth * 0.5f)) <= 2.0f &&
        std::abs(secondMax - (text.x + text.boxWidth)) <= 2;
}

// after PhotoCraft (crates/text/src/tests.rs auto_and_explicit_leading): paragraph spacing moves following lines.
static bool textParagraphSpacingMovesFollowingParagraphs() {
    Text text; text.str = "a\nb"; text.size = 10; text.x = 20; text.y = 10;
    std::vector<float> coverage; float base[4];
    if (!textCoverage(text, 120, 80, coverage, base)) return false;
    ParagraphStyle first; first.spaceAfter = 5;
    Text after = text; after.paragraphs = {{after.str.size(), first}};
    float afterBox[4];
    if (!textCoverage(after, 120, 80, coverage, afterBox)) return false;
    ParagraphStyle second; second.spaceBefore = 5;
    Text before = text; before.paragraphs = {{2, ParagraphStyle{}}, {1, second}};
    float beforeBox[4];
    if (!textCoverage(before, 120, 80, coverage, beforeBox)) return false;
    return afterBox[3] > base[3] + 4.0f && beforeBox[3] > base[3] + 4.0f;
}

// after PhotoCraft (crates/doc/src/lib.rs char_runs and paragraph_runs): styles cover UTF-8 bytes
// without splitting a code point, and a short run list extends with the last style.
static bool richTextRunsNormalizeAndPersist() {
    Text t; t.str = "A\xc3\xa9Z"; t.size = 42;
    TextStyle first; first.size=18; first.font="Example Font"; first.bold=true;
    TextStyle second; second.size=27; second.italic=true;
    t.runs={{1,first},{1,second}};
    ParagraphStyle para; para.align=2; para.firstIndent=7.5f; para.spaceAfter=3;
    t.paragraphs={{1,para}};
    const auto runs=t.normalizedRuns(); const auto paragraphs=t.normalizedParagraphs();
    Text decoded; const bool loaded=decoded.load(t.save());
    Text old; const bool oldLoaded=old.load("text2 41 - - 48 10 20 0 0 0 1.2 0 0 1 1 0 0 0 0 -1");
    Text over=t; over.runs={{0,first},{std::numeric_limits<size_t>::max(),second},{5,first}};
    const auto clipped=over.normalizedRuns(); const auto clippedParagraphs=over.normalizedParagraphs(); Text empty=t; empty.str.clear(); empty.runs={{0,first}};
    const bool result=loaded && oldLoaded && runs.size()==2 && runs[0].bytes==1 && runs[1].bytes==3 &&
        runs[0].style.bold && runs[1].style.italic && runs[1].style.size==27 && clipped.size()==1 && clipped[0].bytes==4 && clipped[0].style==second &&
        clippedParagraphs.size()==1 && clippedParagraphs[0].bytes==4 &&
        paragraphs.size()==1 && paragraphs[0].bytes==4 && paragraphs[0].style.align==2 &&
        empty.normalizedRuns().size()==1 && empty.normalizedRuns()[0].style==first && decoded.runs.size()==2 && decoded.runs[0].bytes==1 && decoded.runs[0].style==first && decoded.runs[1].style==second && decoded.paragraphs.size()==1 &&
        decoded.paragraphs[0].style.firstIndent==7.5f && old.runs.empty();
    return result;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::char_runs): a style run remains attached
// to its Unicode characters when the UTF-8 text is split into visual lines.
static bool richTextStylesMapToUnicodeCharacters() {
    Text t; t.str = "A\xc3\xa9\nZ";
    TextStyle first; first.size = 18; first.bold = true;
    TextStyle second; second.size = 27; second.italic = true;
    t.runs = {{3, first}, {2, second}};
    const auto styles = t.characterStyles();
    return styles.size() == 4 && styles[0] == first && styles[1] == first &&
        styles[2] == second && styles[3] == second;
}

// after PhotoCraft (crates/text/src/layout.rs): character-run size and tracking participate in
// glyph advances and rasterization instead of being ignored in favor of layer defaults.
static bool richTextRunMetricsAffectLayoutAndInk() {
    Text base; base.str = "AB"; base.font = defaultFont(); base.size = 32;
    Text same = base; TextStyle ordinary; ordinary.font=base.font; ordinary.size=base.size; ordinary.leading=base.leading;
    same.runs = {{1, ordinary}, {1, ordinary}};
    std::vector<float> plainCoverage, sameCoverage; float plainBox[4], sameBox[4];
    if(!textCoverage(base,180,100,plainCoverage,plainBox)||!textCoverage(same,180,100,sameCoverage,sameBox))return false;
    if(plainCoverage!=sameCoverage)return false;
    Text varied=base;TextStyle first=ordinary,second=ordinary;second.size=54;second.tracking=500;
    varied.runs={{1,first},{1,second}};
    std::vector<float> variedCoverage;float variedBox[4];
    if(!textCoverage(varied,180,100,variedCoverage,variedBox))return false;
    const float plainInk=std::accumulate(plainCoverage.begin(),plainCoverage.end(),0.0f);
    const float variedInk=std::accumulate(variedCoverage.begin(),variedCoverage.end(),0.0f);
    return variedInk>plainInk*1.15f && variedBox[2]-variedBox[0]>plainBox[2]-plainBox[0]+4 && textAdvance(varied)>textAdvance(base)+4;
}
static bool richTextRunLeadingControlsSubsequentLineBaseline() {
    Text plain;plain.str="A\nB\nC";plain.font=defaultFont();plain.size=24;plain.x=20;plain.y=10;
    std::vector<float> plainCoverage;float plainBox[4];
    if(!textCoverage(plain,160,180,plainCoverage,plainBox))return false;
    Text spaced=plain;TextStyle first;first.font=plain.font;first.size=plain.size;first.leading=plain.leading;
    TextStyle second=first;second.leading=2.8f;
    spaced.runs={{2,first},{2,second},{1,first}};
    std::vector<float> spacedCoverage;float spacedBox[4];
    if(!textCoverage(spaced,160,180,spacedCoverage,spacedBox))return false;
    return spacedBox[3]-spacedBox[1]>plainBox[3]-plainBox[1]+20;
}
static bool richTextRunTransformsMatchPhotoCraft() {
    Text base;base.str="Hi";base.font=defaultFont();base.size=30;base.x=20;base.y=80;
    std::vector<float> plain;float plainBox[4];if(!textCoverage(base,240,180,plain,plainBox))return false;
    TextStyle style;style.font=base.font;style.size=base.size;style.leading=base.leading;
    auto runText=[&](TextStyle run){Text t=base;t.runs={{t.str.size(),run}};return t;};
    Text shifted=runText(style);shifted.runs[0].style.baselineShift=10;
    std::vector<float> shiftInk;float shiftBox[4];if(!textCoverage(shifted,240,180,shiftInk,shiftBox,72))return false;
    float highDpiBox[4];if(!textCoverage(shifted,240,180,shiftInk,highDpiBox,144))return false;
    Text wide=runText(style);wide.runs[0].style.horizontalScale=2;
    std::vector<float> wideInk;float wideBox[4];if(!textCoverage(wide,240,180,wideInk,wideBox))return false;
    Text tall=runText(style);tall.runs[0].style.verticalScale=2;
    std::vector<float> tallInk;float tallBox[4];if(!textCoverage(tall,240,180,tallInk,tallBox))return false;
    Text wrap=base;wrap.str="HO HO HO";wrap.boxWidth=80;Text plainWrap=wrap;plainWrap.runs={{8,style}};
    TextStyle compressed=style;compressed.horizontalScale=1.7f;Text scaledWrap=wrap;scaledWrap.runs={{8,compressed}};
    std::vector<float> plainWrapInk,scaledWrapInk;float plainWrapBox[4],scaledWrapBox[4];
    if(!textCoverage(plainWrap,240,180,plainWrapInk,plainWrapBox)||!textCoverage(scaledWrap,240,180,scaledWrapInk,scaledWrapBox))return false;
    return std::fabs((plainBox[1]-shiftBox[1])-10)<1.1f && std::fabs((plainBox[1]-highDpiBox[1])-20)<1.1f &&
        wideBox[2]-wideBox[0]>(plainBox[2]-plainBox[0])*1.7f && tallBox[3]-tallBox[1]>(plainBox[3]-plainBox[1])*1.7f &&
        scaledWrapBox[3]-scaledWrapBox[1]>plainWrapBox[3]-plainWrapBox[1];
}
static bool richTextFauxBoldAndItalicAffectInk() {
    Text text;text.str="Hi";text.font=defaultFont();text.size=30;text.x=20;text.y=80;
    TextStyle style;style.font=text.font;style.size=text.size;style.leading=text.leading;
    auto render=[&](TextStyle run,std::vector<float>& ink,float box[4]){Text t=text;t.runs={{2,run}};return textCoverage(t,240,180,ink,box);};
    std::vector<float> plain,bold,italic;float plainBox[4],boldBox[4],italicBox[4];
    if(!render(style,plain,plainBox))return false;
    TextStyle boldStyle=style;boldStyle.bold=true;
    TextStyle italicStyle=style;italicStyle.italic=true;
    if(!render(boldStyle,bold,boldBox)||!render(italicStyle,italic,italicBox))return false;
    const float plainInk=std::accumulate(plain.begin(),plain.end(),0.0f),boldInk=std::accumulate(bold.begin(),bold.end(),0.0f);
    return boldInk>plainInk*1.1f&&italicBox[2]>plainBox[2];
}
static bool richTextUnderlineAndStrikeUseFontMetrics() {
    Text text;text.str="Hi";text.font=defaultFont();text.size=30;text.x=20;text.y=80;
    TextStyle style;style.font=text.font;style.size=text.size;style.leading=text.leading;
    auto render=[&](TextStyle run,std::vector<float>& ink,float box[4]){Text t=text;t.runs={{2,run}};return textCoverage(t,240,180,ink,box);};
    std::vector<float> plain,under,strike;float plainBox[4],underBox[4],strikeBox[4];
    if(!render(style,plain,plainBox))return false;
    TextStyle underStyle=style;underStyle.underline=true;TextStyle strikeStyle=style;strikeStyle.strikethrough=true;
    if(!render(underStyle,under,underBox)||!render(strikeStyle,strike,strikeBox))return false;
    const float plainInk=std::accumulate(plain.begin(),plain.end(),0.0f);
    return std::accumulate(under.begin(),under.end(),0.0f)>plainInk&&underBox[3]>plainBox[3]&&
        std::accumulate(strike.begin(),strike.end(),0.0f)>plainInk;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs word_boundaries).
static bool typeWordBoundariesMatchPhotoCraft() {
    const std::string text="hello big world";
    return textWordBoundary(text,0,true)==5 && textWordBoundary(text,7,false)==6 &&
        textWordBoundary(text,15,false)==10 && textWordBoundary("hello",99,true)==5 &&
        textWordBoundary("hello",0,false)==0 && textWordBoundary("",4,true)==0 &&
        textWordBoundary("ab, cd",0,true)==2 && textWordBoundary("ab, cd",2,true)==6 &&
        textWordBoundary("Größe",0,true)==5;
}
static bool typeDoubleClickWordRangesMatchPhotoCraft() {
    return textWordSelection("ab, cd",2)==std::pair<size_t,size_t>{0,2}&&
        textWordSelection("ab, cd",3)==std::pair<size_t,size_t>{3,3}&&
        textWordSelection("ab, cd",6)==std::pair<size_t,size_t>{4,6}&&
        textWordSelection("Gr\xc3\xb6\xc3\x9f" "e ok",3)==std::pair<size_t,size_t>{0,5}&&
        textWordSelection("one",99)==std::pair<size_t,size_t>{0,3};
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs handle_move_key).
static bool typeDocumentEdgeMovementMatchesPhotoCraft() {
    ImGuiInputTextState state; state.CurLenW = 6;
    if (!App::typeMoveDocumentEdge(&state, false, 4, 1, true, true) || state.Stb.cursor != 0 || state.Stb.select_start != 1 || state.Stb.select_end != 0) return false;
    if (!App::typeMoveDocumentEdge(&state, true, 0, 0, false, false) || state.Stb.cursor != 6 || state.Stb.select_start != 6 || state.Stb.select_end != 6) return false;
    return App::typeMoveDocumentEdge(&state, false, 5, 2, true, false) && state.Stb.cursor == 0 && state.Stb.select_start == 0 && state.Stb.select_end == 0;
}
static bool typeNavigationShortcutsMatchPhotoCraft() {
    using App::typeDocumentNavigationShortcut; using App::typeWordNavigationShortcut;
    return typeWordNavigationShortcut(true,false,false,false,false) &&
        typeWordNavigationShortcut(false,true,true,false,false) &&
        typeWordNavigationShortcut(false,false,false,true,true) &&
        !typeWordNavigationShortcut(false,true,false,false,false) &&
        !typeWordNavigationShortcut(true,true,false,false,false) &&
        typeDocumentNavigationShortcut(true,false,false,false) &&
        typeDocumentNavigationShortcut(false,false,true,true) &&
        !typeDocumentNavigationShortcut(true,true,false,false);
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/canvas_tests.rs vertical_type_caret_selection_and_arrows_follow_the_columns).
static bool verticalTypeArrowsFollowTextColumns() {
    ImGuiInputTextState state; const std::string text="ABCD\nxyz";
    for(char c:text)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)text.size();
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=1;
    if(!App::typeMoveVerticalCaret(&state,text,1,1,1,1,false,2,false)||state.GetCursorPos()!=6)return false;
    if(!App::typeMoveVerticalCaret(&state,text,6,6,6,6,false,-2,false)||state.GetCursorPos()!=1)return false;
    if(!App::typeMoveVerticalCaret(&state,text,1,1,1,1,false,1,false)||state.GetCursorPos()!=2)return false;
    if(!App::typeMoveVerticalCaret(&state,text,2,2,2,2,false,-1,false)||state.GetCursorPos()!=1)return false;
    state.Stb.cursor=4;state.Stb.select_start=1;state.Stb.select_end=4;
    if(!App::typeMoveVerticalCaret(&state,text,4,1,4,1,true,-1,false)||state.GetCursorPos()!=1||state.HasSelection())return false;
    state.Stb.cursor=2;state.Stb.select_start=state.Stb.select_end=2;
    if(!App::typeMoveVerticalCaret(&state,text,2,2,2,2,false,1,true)||state.GetCursorPos()!=3||state.GetSelectionStart()!=2)return false;
    state.Stb.cursor=1;state.Stb.select_start=4;state.Stb.select_end=1;
    return App::typeMoveVerticalCaret(&state,text,1,1,4,4,true,1,true)&&state.GetCursorPos()==2&&state.Stb.select_start==4;
}
// after PhotoCraft (crates/text/src/layout/navigate.rs line_edge): Home/End use visual-row
// bounds and Shift extends from the original anchor.
static bool typeEditorHomeEndFollowVisualRows() {
    ImGuiInputTextState state; const std::string text="alpha\nbeta\n";
    for(char c:text)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)text.size();
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=8;
    if(!App::typeMoveEditorLineEdge(&state,text,false,8,8,false,false)||state.GetCursorPos()!=6)return false;
    if(!App::typeMoveEditorLineEdge(&state,text,true,8,8,false,false)||state.GetCursorPos()!=10)return false;
    state.Stb.cursor=8;state.Stb.select_start=3;state.Stb.select_end=8;
    if(!App::typeMoveEditorLineEdge(&state,text,false,8,3,true,true)||state.GetCursorPos()!=6||state.Stb.select_start!=3||state.GetSelectionStart()!=3)return false;
    state.Stb.cursor=3;state.Stb.select_start=8;state.Stb.select_end=3;
    const bool moved=App::typeMoveEditorLineEdge(&state,text,false,3,8,true,true);
    const bool correct=state.GetCursorPos()==0&&state.Stb.select_start==8&&state.Stb.select_end==0;
    return moved&&correct;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs step_char_metric): a selected
// range steps each character's existing metric, while a collapsed non-empty range does nothing.
static bool typeCharacterMetricsStepPerRun() {
    Text text;text.str="A\xc3\xa9Z";text.size=20;
    TextStyle first;first.size=20;first.leading=1.2f;TextStyle second;second.size=40;second.leading=1.5f;second.tracking=30;
    text.runs={{1,first},{3,second}};
    if(!App::typeStepCharacterMetric(text,1,3,2,20,72,1))return false;
    auto styles=text.characterStyles();
    if(styles.size()!=3||!(styles[0]==first)||styles[1].tracking!=50||styles[2].tracking!=50)return false;
    if(App::typeStepCharacterMetric(text,1,1,2,20,72,1))return false;
    if(!App::typeStepCharacterMetric(text,1,3,1,-2,144,1)||!App::typeStepCharacterMetric(text,1,3,0,2,72,1))return false;
    styles=text.characterStyles();
    if(!(styles[0]==first)||std::fabs(styles[1].baselineShift+2)>0.001f||std::fabs(styles[2].leading-1.55f)>0.001f)return false;
    if(!App::typeStepCharacterMetric(text,1,3,2,20000,72,1))return false;
    styles=text.characterStyles();if(styles[1].tracking!=10000||styles[2].tracking!=10000)return false;
    Text empty;empty.size=48;
    return App::typeStepCharacterMetric(empty,0,0,0,2,72,1)&&std::fabs(empty.normalizedRuns()[0].style.leading-(1.2f+2.0f/48))<0.001f;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/panel.rs): leading is stored as a
// per-character point value after accounting for font size, resolution, and text transforms.
static bool typeLeadingPanelEditsSelectedRun() {
    Text text;text.str="AÃ©Z";text.size=20;TextStyle a;a.size=20;TextStyle b;b.size=40;b.leading=1.5f;text.runs={{1,a},{3,b}};
    if(!App::typeSetCharacterMetricValue(text,1,3,0,44,144,1))return false;
    const auto styles=text.characterStyles();
    return std::fabs(styles[0].leading-1.2f)<.001f&&std::fabs(styles[1].leading-2.2f)<.001f&&std::fabs(styles[2].leading-2.2f)<.001f;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/panel.rs): baseline shift is a
// selection-scoped point value with the source's +/-5000 point limits.
static bool typeBaselinePanelEditsSelectedRun() {
    Text text;text.str="abc";TextStyle a,b;b.baselineShift=7;text.runs={{1,a},{2,b}};
    if(!App::typeSetCharacterMetricValue(text,1,2,1,-9000,72,1))return false;
    const auto styles=text.characterStyles();return styles[0].baselineShift==0&&styles[1].baselineShift==-5000&&styles[2].baselineShift==7;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/panel.rs): horizontal scaling uses
// percentages while the document stores a ratio on each character run.
static bool typeHorizontalScalePanelEditsSelectedRun() {
    Text text;text.str="abc";TextStyle a,b;b.horizontalScale=1.2f;text.runs={{1,a},{2,b}};
    if(!App::typeSetCharacterMetricValue(text,1,2,2,150,72,1))return false;
    const auto styles=text.characterStyles();return styles[0].horizontalScale==1&&styles[1].horizontalScale==1.5f&&styles[2].horizontalScale==1.2f;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/panel.rs): vertical scaling uses
// percentages and preserves neighboring character styles.
static bool typeVerticalScalePanelEditsSelectedRun() {
    Text text;text.str="abc";TextStyle a,b;b.verticalScale=.8f;text.runs={{1,a},{2,b}};
    if(!App::typeSetCharacterMetricValue(text,1,2,3,150,72,1))return false;
    const auto styles=text.characterStyles();return styles[0].verticalScale==1&&styles[1].verticalScale==1.5f&&styles[2].verticalScale==.8f;
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_shield.rs keys and hides_outside): Classic
// Mode keeps the stored Show Cropped Area choice but behaves as if it were on; H is consumed.
static bool cropClassicModeKeepsShowAreaAndShortcutRules() {
    bool classic=false,show=false;
    const int h=(int)ImGuiKey_H,p=(int)ImGuiKey_P;
    if(App::cropOptionKeyAction(true,false,false,false,false,false,false,h,classic,show)!=1||!show)return false;
    show=false;classic=true;
    if(!App::cropShieldShowsOutside(show,classic))return false;
    if(App::cropOptionKeyAction(true,false,false,false,false,false,false,h,classic,show)!=1||show)return false;
    if(App::cropOptionKeyAction(true,false,false,false,false,false,false,p,classic,show)!=2||classic)return false;
    if(App::cropShieldShowsOutside(show,classic))return false;
    for(const auto [shift,alt,ctrl,superKey,pending,defaultFrame,dragging]:{
            std::array<bool,7>{true,false,false,false,true,false,false},
            std::array<bool,7>{false,true,false,false,true,false,false},
            std::array<bool,7>{false,false,true,false,true,false,false},
            std::array<bool,7>{false,false,false,true,true,false,false},
            std::array<bool,7>{false,false,false,false,false,false,false},
            std::array<bool,7>{false,false,false,false,true,true,false},
            std::array<bool,7>{false,false,false,false,true,false,true}})
        if(App::cropOptionKeyAction(pending,defaultFrame,dragging,shift,alt,ctrl,superKey,h,classic,show)!=0)return false;
    return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_ui.rs push_press_step and step): undo and redo
// restore the crop frame snapshot while keeping the document history outside this state machine.
static bool cropFrameUndoRedoUsesItsOwnHistory() {
    using Step=std::array<float,6>;
    std::vector<Step> undo,redo;
    const Step before{40,30,100,70,0,1},moved{60,30,120,70,0,1};
    if(!App::cropHistoryRecord(undo,redo,before,moved)||undo.size()!=1||!redo.empty())return false;
    Step current=moved;
    if(!App::cropHistoryStep(undo,redo,false,current)||current!=before||!undo.empty()||redo.size()!=1)return false;
    if(App::cropHistoryStep(undo,redo,false,current)||current!=before)return false;
    if(!App::cropHistoryStep(undo,redo,true,current)||current!=moved||undo.size()!=1||!redo.empty())return false;
    const Step resized{60,30,130,70,0,1};
    if(!App::cropHistoryRecord(undo,redo,current,resized)||undo.size()!=2||!redo.empty())return false;
    return App::cropHistoryStep(undo,redo,false,current)&&current==moved;
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_size.rs front_image): Front Image uses the
// document dimensions and preserves the selected px/in or px/cm resolution unit.
static bool cropFrontImageUsesCurrentDocumentAndUnits() {
    const auto inch=App::cropFrontImageFields(400,300,72,false);
    const auto centimetre=App::cropFrontImageFields(400,300,72,true);
    const auto invalid=App::cropFrontImageFields(0,300,72,false);
    return inch==std::array<std::string,3>{"400 px","300 px","72"}&&
        centimetre==std::array<std::string,3>{"400 px","300 px","28.35"}&&
        invalid==std::array<std::string,3>{"","",""};
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_size.rs keys): I is Front Image only when
// Crop owns keyboard input, a gesture is idle, and no modifier or text editor takes the key.
static bool cropFrontImageShortcutUsesUnmodifiedCropInput() {
    if(!App::cropFrontImageKeyAllowed(true,false,false,false,false,false,false))return false;
    return !App::cropFrontImageKeyAllowed(false,false,false,false,false,false,false)&&
        !App::cropFrontImageKeyAllowed(true,true,false,false,false,false,false)&&
        !App::cropFrontImageKeyAllowed(true,false,true,false,false,false,false)&&
        !App::cropFrontImageKeyAllowed(true,false,false,true,false,false,false)&&
        !App::cropFrontImageKeyAllowed(true,false,false,false,true,false,false)&&
        !App::cropFrontImageKeyAllowed(true,false,false,false,false,true,false)&&
        !App::cropFrontImageKeyAllowed(true,false,false,false,false,false,true);
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_ui.rs show_frame/apply_ratio): a committed or
// cancelled hidden frame returns to the canvas bounds when a fresh press or ratio needs it.
static bool cropHiddenFrameRestoresOnlyOnDemand() {
    bool pending=false,dismissed=true;std::array<float,4> rect={8,9,18,19};
    if(!App::cropRestoreFrameState(pending,dismissed,rect,200,100)||!pending||dismissed||rect!=std::array<float,4>{0,0,200,100})return false;
    dismissed=true;pending=false;
    if(!App::cropRestoreFrameState(pending,dismissed,rect,200,100)||!pending||dismissed||rect!=std::array<float,4>{0,0,200,100})return false;
    pending=false;dismissed=true;
    return !App::cropRestoreFrameState(pending,dismissed,rect,0,100)&&!pending&&dismissed;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs toggle_char_style): mixed
// selected characters toggle on together, a fully styled selection toggles off, and caret-only text is unchanged.
static bool typeCharacterStyleShortcutsToggleSelections() {
    Text text;text.str="A\xc3\xa9Z";TextStyle plain,bold;bold.bold=true;text.runs={{1,plain},{2,bold}};
    if(!App::typeToggleCharacterStyle(text,0,2,0))return false;
    auto styles=text.characterStyles();if(styles.size()!=3||!styles[0].bold||!styles[1].bold||!styles[2].bold)return false;
    if(!App::typeToggleCharacterStyle(text,0,2,0))return false;
    styles=text.characterStyles();if(styles[0].bold||styles[1].bold||!styles[2].bold)return false;
    if(!App::typeToggleCharacterStyle(text,0,2,4)||text.characterStyles()[0].caps!=TextCaps::All)return false;
    if(!App::typeToggleCharacterStyle(text,0,2,4)||text.characterStyles()[0].caps!=TextCaps::Normal)return false;
    if(!App::typeToggleCharacterStyle(text,0,2,5)||text.characterStyles()[0].caps!=TextCaps::Small)return false;
    if(!App::typeToggleCharacterStyle(text,0,2,5)||text.characterStyles()[0].caps!=TextCaps::Normal)return false;
    if(App::typeToggleCharacterStyle(text,1,1,2)||App::typeToggleCharacterStyle(text,0,3,6))return false;
    Text empty;return App::typeToggleCharacterStyle(empty,0,0,2)&&empty.normalizedRuns()[0].style.underline;
}
// after PhotoCraft (crates/text/src/layout/shaping.rs push_cased): all caps changes drawn glyphs,
// not the editable source text, and its style persists through project serialization.
static bool richTextAllCapsRendersAndPersists() {
    Text source;source.str="Stra\xc3\x9f" "e";source.size=42;source.x=20;source.y=60;
    Text upper=source;upper.str="STRASSE";TextStyle plain;plain.size=42;upper.runs={{upper.str.size(),plain}};
    TextStyle caps=plain;caps.caps=TextCaps::All;source.runs={{source.str.size(),caps}};
    std::vector<float> sourceInk,upperInk;float sourceBox[4],upperBox[4];
    if(!textCoverage(source,240,120,sourceInk,sourceBox)||!textCoverage(upper,240,120,upperInk,upperBox))return false;
    if(sourceInk!=upperInk||textAdvance(source)!=textAdvance(upper))return false;
    Text small;small.str="a";small.size=40;small.x=20;small.y=60;TextStyle smallStyle;smallStyle.size=40;smallStyle.caps=TextCaps::Small;small.runs={{1,smallStyle}};
    Text scaledCap=small;scaledCap.str="A";TextStyle scaledStyle;scaledStyle.size=28;scaledCap.runs={{1,scaledStyle}};std::vector<float> smallInk,scaledInk;float smallBox[4],scaledBox[4];
    if(!textCoverage(small,240,120,smallInk,smallBox)||!textCoverage(scaledCap,240,120,scaledInk,scaledBox)||smallInk!=scaledInk||textAdvance(small)!=textAdvance(scaledCap))return false;
    Text decoded;if(!decoded.load(source.save()))return false;
    Text oldText4;if(!oldText4.load("text4 53747261c39f65 - - 42 20 60 0 0 0 1.2 0 0 1 1 0 0 0 0 -1 372c34322c302c312e322c302c312c312c302c302c302c302c302c302c2d3b -"))return false;
    return decoded.str==source.str&&decoded.characterStyles().size()==source.characterStyles().size()&&decoded.characterStyles()[0].caps==TextCaps::All&&
        oldText4.characterStyles()[0].caps==TextCaps::Normal;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs changed_span and replace_text): insertion
// inherits the character before the caret, replacement keeps its first character style, and edits stay UTF-8 aligned.
static bool typeEditingPreservesCharacterAndParagraphRuns() {
    Text text;text.str="ABC";TextStyle a,b,c;a.size=12;b.size=24;c.size=36;text.runs={{1,a},{1,b},{1,c}};
    if(!App::typeApplyEditorText(text,"AXBC"))return false;
    auto styles=text.characterStyles();if(styles.size()!=4||styles[0].size!=12||styles[1].size!=12||styles[2].size!=24||styles[3].size!=36)return false;
    if(!App::typeApplyEditorText(text,"AZBC"))return false;
    styles=text.characterStyles();if(styles[1].size!=12||styles[2].size!=24)return false;
    if(!App::typeApplyEditorText(text,"AZC"))return false;
    styles=text.characterStyles();if(styles.size()!=3||styles[0].size!=12||styles[1].size!=12||styles[2].size!=36)return false;
    Text unicode;unicode.str="A\xc3\xa9" "B";TextStyle u1,u2,u3;u1.size=10;u2.size=20;u3.size=30;unicode.runs={{1,u1},{2,u2},{1,u3}};
    if(!App::typeApplyEditorText(unicode,"A\xc3\xa9\xe2\x9c\xa8" "B"))return false;
    styles=unicode.characterStyles();if(styles.size()!=4||styles[0].size!=10||styles[1].size!=20||styles[2].size!=20||styles[3].size!=30)return false;
    Text paragraphs;paragraphs.str="A\nB";ParagraphStyle left,right;left.align=0;right.align=2;paragraphs.paragraphs={{2,left},{1,right}};
    if(!App::typeApplyEditorText(paragraphs,"A\nxB"))return false;
    auto paras=paragraphs.normalizedParagraphs();if(paras.size()!=2||paras[0].style.align!=0||paras[1].style.align!=2||paras[1].bytes!=2)return false;
    Text trailing;trailing.str="A\n";trailing.paragraphs={{2,left},{0,right}};
    if(!App::typeApplyEditorText(trailing,"A\nB\n"))return false;
    paras=trailing.normalizedParagraphs();return paras.size()==3&&paras.back().bytes==0&&paras.back().style.align==2;
}
// after PhotoCraft (crates/text/src/layout/paragraphs.rs next_tab_stop): tabs use 36 point stops,
// stay in the editable source string, and wrap with their expanded whitespace width.
static bool typeTabsUsePhotoCraftStops() {
    Text tab;tab.str="a\tb";tab.size=24;tab.x=4;tab.y=48;
    std::vector<float> coverage;float box72[4],box144[4];
    if(!textCoverage(tab,400,180,coverage,box72,72)||!textCoverage(tab,400,260,coverage,box144,144))return false;
    if(box144[2]-box72[2]<30||textAdvance(tab)<40)return false;
    Text stored;if(!stored.load(tab.save())||stored.str!="a\tb")return false;
    Text wrapped;wrapped.str="a\tb\tc\td\te\tf\tg";wrapped.size=24;wrapped.boxWidth=100;wrapped.boxHeight=200;
    float wrappedBox[4];if(!textCoverage(wrapped,240,260,coverage,wrappedBox,72))return false;
    return wrappedBox[2]-wrapped.x<=100.5f;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/canvas_tests.rs size_applies_at_layer_and_selection_scope):
// an active selection receives a size edit and a collapsed range formats the whole text layer.
static bool typeSizeEditsRespectSelectionScope() {
    Text text;text.str="Hello world";TextStyle small,large;small.size=30;large.size=20;
    text.runs={{6,small},{5,large}};
    if(!App::typeSetCharacterSize(text,6,11,10))return false;
    auto styles=text.characterStyles();if(styles.size()!=11||styles[0].size!=30||styles[5].size!=30||styles[6].size!=10||styles[10].size!=10)return false;
    if(!App::typeSetCharacterSize(text,0,0,40))return false;
    styles=text.characterStyles();for(const TextStyle& style:styles)if(style.size!=40)return false;
    Text unicode;unicode.str="a\xc3\xa9" "z";TextStyle a,b,c;a.size=12;b.size=24;c.size=36;unicode.runs={{1,a},{2,b},{1,c}};
    if(!App::typeSetCharacterSize(unicode,1,2,18))return false;
    styles=unicode.characterStyles();return styles.size()==3&&styles[0].size==12&&styles[1].size==18&&styles[2].size==36;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs style_range): tracking edits target the
// selected characters and preserve the other character styles.
static bool typeTrackingEditsRespectSelectionScope() {
    Text text;text.str="Hello world";TextStyle first,second;first.tracking=-20;second.tracking=80;text.runs={{6,first},{5,second}};
    if(!App::typeSetCharacterTracking(text,6,11,150))return false;
    auto styles=text.characterStyles();if(styles.size()!=11||styles[0].tracking!=-20||styles[5].tracking!=-20||styles[6].tracking!=150||styles[10].tracking!=150)return false;
    if(!App::typeSetCharacterTracking(text,0,0,-50))return false;
    styles=text.characterStyles();for(const TextStyle& style:styles)if(style.tracking!=-50)return false;
    if(!App::typeSetCharacterTracking(text,3,3,20000))return false;
    styles=text.characterStyles();for(const TextStyle& style:styles)if(style.tracking!=10000)return false;
    return true;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs style_range): font changes split and merge
// per-character runs without moving UTF-8 character boundaries.
static bool typeFontEditsRespectSelectionScope() {
    Text text;text.str="A\xc3\xa9" "BC";TextStyle first,second,last;first.font="Base Face";second.font="Old Face";last.font="Base Face";
    text.runs={{1,first},{2,second},{2,last}};
    if(!App::typeSetCharacterFont(text,1,3,"Selected Face"))return false;
    auto styles=text.characterStyles();if(styles.size()!=4||styles[0].font!="Base Face"||styles[1].font!="Selected Face"||styles[2].font!="Selected Face"||styles[3].font!="Base Face")return false;
    Text decoded;if(!decoded.load(text.save()))return false;styles=decoded.characterStyles();if(styles.size()!=4||styles[1].font!="Selected Face"||styles[2].font!="Selected Face")return false;
    if(!App::typeSetCharacterFont(text,0,0,"Whole Layer Face"))return false;
    styles=text.characterStyles();for(const TextStyle& style:styles)if(style.font!="Whole Layer Face")return false;
    Text empty;if(!App::typeSetCharacterFont(empty,0,0,"Empty Style Face"))return false;
    return empty.font=="Empty Style Face"&&empty.normalizedRuns().front().style.font=="Empty Style Face";
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs type.edit kernPair): manual kerning starts
// at the font's current pair spacing, persists per character, and rejects line edges.
static bool typeManualPairKerningStartsFromMetrics() {
    Text text;text.str="AV";text.size=40;TextStyle style;style.size=40;text.runs={{text.str.size(),style}};
    const float before=textAdvance(text);
    if(!textAdjustKerningPair(text,1,100))return false;
    auto styles=text.characterStyles();if(styles.size()!=2||!styles[0].kerningOff||styles[0].kern<=0||styles[1].kerningOff)return false;
    const float after=textAdvance(text);if(after-before<3.5f||after-before>4.5f)return false;
    Text decoded;if(!decoded.load(text.save()))return false;styles=decoded.characterStyles();
    if(styles.size()!=2||!styles[0].kerningOff||styles[0].kern!=text.characterStyles()[0].kern)return false;
    if(!textAdjustKerningPair(decoded,1,20000)||decoded.characterStyles()[0].kern!=10000)return false;
    Text lines;lines.str="A\nV";lines.size=40;
    return !textAdjustKerningPair(lines,0,20)&&!textAdjustKerningPair(lines,1,20)&&!textAdjustKerningPair(lines,2,20);
}
static bool typeModifiedDeletionMatchesPhotoCraft() {
    ImGuiInputTextState state; const std::string text="ab, cd";
    for(char c:text)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)text.size();
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=6;
    if(!App::typePrepareModifiedDelete(&state,text,true,false,true,false,false)||state.Stb.select_start!=6||state.Stb.select_end!=4)return false;
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=2;
    if(!App::typePrepareModifiedDelete(&state,text,false,false,true,false,false)||state.Stb.select_start!=2||state.Stb.select_end!=6)return false;
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=2;
    if(!App::typePrepareModifiedDelete(&state,text,false,true,false,false,false)||state.Stb.select_end!=3)return false;
    const std::string lines="ab\ncd";state.TextW.clear();for(char c:lines)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)lines.size();
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=4;
    return App::typePrepareModifiedDelete(&state,lines,true,true,false,false,false)&&state.Stb.select_end==3;
}
static bool typeMultiClickCountsMatchPhotoCraft() {
    using App::typeNextClickCount;
    return typeNextClickCount(0,0)==1&&typeNextClickCount(0,1)==1&&typeNextClickCount(0,2)==2&&
        typeNextClickCount(0,3)==3&&typeNextClickCount(3,3)==4&&typeNextClickCount(4,3)==5&&
        typeNextClickCount(5,3)==1&&typeNextClickCount(4,1)==1;
}
static bool typeMultiClickRangesMatchPhotoCraft() {
    const std::string text="ab, cd\nnext";ImGuiInputTextState state;
    for(char c:text)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)text.size();
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=1;
    if(!App::typeApplyClickSelection(&state,text,2)||state.Stb.select_start!=0||state.Stb.select_end!=2)return false;
    state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=9;
    if(!App::typeApplyClickSelection(&state,text,3)||state.Stb.select_start!=7||state.Stb.select_end!=11)return false;
    if(!App::typeApplyClickSelection(&state,text,4)||state.Stb.select_start!=7||state.Stb.select_end!=11)return false;
    return App::typeApplyClickSelection(&state,text,5)&&state.Stb.select_start==0&&state.Stb.select_end==11;
}
static bool typeCaretActionsRestartBlink() {
    ImGuiInputTextState state;const std::string text="one two";
    for(char c:text)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)text.size();
    state.CursorAnim=0;state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=1;
    if(!App::typeMoveWordCaret(&state,text,1,1,false,true,false)||state.CursorAnim>=0)return false;
    state.CursorAnim=0;state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=1;
    if(!App::typeMoveDocumentEdge(&state,false,1,1,false,false)||state.CursorAnim>=0)return false;
    state.CursorAnim=0;state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=4;
    if(!App::typeApplyClickSelection(&state,text,2)||state.CursorAnim>=0)return false;
    state.CursorAnim=0;state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=4;
    return App::typePrepareModifiedDelete(&state,text,true,false,true,false,false)&&state.CursorAnim<0;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs type_click_skips_*): a hidden
// layer or any hidden group ancestor removes its text from Type and Move hit testing.
static bool typeHitVisibilityFollowsNestedGroups() {
    auto variants=[](){
        Doc d=newDoc(256,256,false); Text t; t.str="Visible text"; t.x=40; t.y=96; t.size=48;
        Fill f; f.kind=FILL_SOLID;
        for(int i=0;i<3;++i)addTextLayer(d,t,f);
        return d;
    };
    Doc d=variants();
    if(!groupLayers(d,{2,3})||d.layers.size()!=4)return false;
    d.groups.back().visible=false;
    if(!layerShown(d,1)||layerShown(d,2)||layerShown(d,3))return false;

    d=variants();
    d.layers[2].visible=false;
    if(!groupLayers(d,{3}))return false;
    const int inner=d.groups.back().id;
    LayerGroup outer;outer.id=inner+1;outer.stackIndex=3;outer.name="Outer";outer.visible=false;outer.expanded=true;
    d.groups.back().parentId=outer.id;d.groups.push_back(outer);
    if(!layerShown(d,1)||layerShown(d,3))return false;

    d=variants(); d.layers[2].visible=false; d.layers[3].visible=false;
    if(!layerShown(d,1)||layerShown(d,2)||layerShown(d,3))return false;
    d.layers[1].visible=false; d.layers[2].visible=false; d.layers[3].visible=true;
    return !layerShown(d,1)&&!layerShown(d,2)&&layerShown(d,3)&&!layerShown(d,-1);
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs a_name_from_text_after_blank_lines_keeps_following_the_text).
static bool textLayerNameSkipsLeadingBlankLines() {
    Doc d=newDoc(20,20,false); Text t; t.str="\n\nTitle"; Fill f; f.kind=FILL_SOLID;
    addTextLayer(d,t,f);
    if(d.cur().name!="Title") return false;
    t.str="\nSubtitle"; addTextLayer(d,t,f);
    return d.cur().name=="Subtitle" && textLayerName("\nSubtitle")=="Subtitle" && textLayerName("\n\n")=="Text";
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs smart_quotes_are_typographic).
static bool smartQuotesMapOpeningAndClosingContext() {
    return textSmartQuotes("\"Hello\"")=="\xe2\x80\x9cHello\xe2\x80\x9d" &&
        textSmartQuotes("don't")=="don\xe2\x80\x99t" &&
        textSmartQuotes("(\"x\")")=="(\xe2\x80\x9cx\xe2\x80\x9d)" &&
        textSmartQuotes("--'x'")=="--\xe2\x80\x98x\xe2\x80\x99" &&
        textSmartQuotes("\"\"")=="\xe2\x80\x9c\xe2\x80\x9d" &&
        textSmartQuotes("/\"x\"")=="/\xe2\x80\x9dx\xe2\x80\x9d" &&
        textSmartQuotes("\xe2\x80\x94\"x\"")=="\xe2\x80\x94\xe2\x80\x9cx\xe2\x80\x9d" &&
        textSmartQuotesTyped("a","\"b")=="\xe2\x80\x9d" "b" &&
        textSmartQuotesTyped(" ","\"you\"")=="\xe2\x80\x9cyou\xe2\x80\x9d" &&
        textSmartQuoteCharacter(0x2014,'"')==0x201c && textSmartQuoteCharacter('/','"')==0x201d;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs smart_quotes_preference_controls_typed_quotes).
static bool typedSmartQuotesUseTheExistingCaretContext() {
    const std::string prefix="keep /";
    const std::string typed="\"new\"";
    return prefix=="keep /" && textSmartQuotesTyped(prefix,typed)=="\xe2\x80\x9dnew\xe2\x80\x9d" &&
        textSmartQuotesTyped(" ","\"new\"")=="\xe2\x80\x9cnew\xe2\x80\x9d";
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs type_unit_follows_the_units_and_rulers_preference).
static bool typeUnitsFollowResolution() {
    const TextUnit pt=textUnit(0,72), px72=textUnit(1,72), px144=textUnit(1,144), mm=textUnit(2,144);
    return pt.scale==1 && std::strcmp(pt.label,"pt")==0 && px72.scale==1 && std::strcmp(px72.label,"px")==0 &&
        px144.scale==2 && std::fabs(mm.scale-25.4f/72.0f)<1e-6f && std::strcmp(mm.label,"mm")==0;
}

// after PhotoCraft (crates/doc/src/text.rs summary_follows_first_run).
static bool textSummaryFollowsFirstStyleRun() {
    Text t; t.str="ab"; TextStyle style; style.font="Example Family"; style.size=17; style.tracking=42; style.leading=1.4f;
    t.runs={{2,style}}; t.syncSummary();
    return t.font==style.font && t.size==style.size && t.tracking==style.tracking && t.leading==style.leading;
}

// Project persistence must retain the editable character and paragraph runs, not only their summary.
static bool richTextRunsSurviveProjectRoundTrip() {
    Doc d=newDoc(40,30,false); Text t; t.str="Run persistence";
    TextStyle s; s.font="Family A"; s.size=23; s.bold=true; t.runs={{4,s},{t.str.size()-4,TextStyle{}}};
    ParagraphStyle p; p.align=2; p.spaceAfter=5; t.paragraphs={{t.str.size(),p}};
    Fill f; f.kind=FILL_SOLID; addTextLayer(d,t,f);
    const auto path=std::filesystem::temp_directory_path()/"chimera-rich-text-runs.chimera";
    std::string error; Doc loaded; const bool ok=saveProject(d,path.u8string(),error)&&openDoc(path.u8string(),loaded,error);
    std::error_code ec; std::filesystem::remove(path,ec);
    if(!ok) return false;
    const auto layer=std::find_if(loaded.layers.begin(),loaded.layers.end(),[](const Layer& l){return l.isText();});
    return layer!=loaded.layers.end() && layer->text.runs.size()==2 && layer->text.runs[0].bytes==4 &&
        layer->text.runs[0].style.font==s.font && layer->text.runs[0].style.size==23 && layer->text.runs[0].style.bold &&
        layer->text.paragraphs.size()==1 && layer->text.paragraphs[0].style.align==2 && layer->text.paragraphs[0].style.spaceAfter==5;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::paragraph_runs trailing empty paragraph).
static bool trailingEmptyParagraphKeepsItsStyle() {
    Text t; t.str="line\n"; ParagraphStyle first; first.align=1; ParagraphStyle last; last.align=2;
    t.paragraphs={{t.str.size(),first},{0,last}};
    const auto runs=t.normalizedParagraphs();
    return runs.size()==2 && runs[0].bytes==t.str.size() && runs[0].style.align==1 &&
        runs[1].bytes==0 && runs[1].style.align==2;
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/tests.rs recent font cap tests).
static bool recentFontsMoveForwardAndRespectCaps() {
    std::vector<std::string> recent;
    for(const char* f:{"A","B","C"}) noteRecentFont(recent,f,8);
    if(recent!=std::vector<std::string>({"C","B","A"})) return false;
    noteRecentFont(recent,"D",2);
    if(recent!=std::vector<std::string>({"D","C"})) return false;
    noteRecentFont(recent,"C",2);
    if(recent!=std::vector<std::string>({"C","D"})) return false;
    trimRecentFonts(recent,1);
    if(recent!=std::vector<std::string>({"C"})) return false;
    noteRecentFont(recent,"E",0);
    return recent.empty();
}

int runSelfTests() {
    struct Test { const char* name; bool (*run)(); };
    static const Test tests[] = {
        {"group opacity applies once", groupOpacityAppliesOnce},
        {"group mask restricts composite", groupMaskRestrictsComposite},
        {"non-contiguous group selection rejected", rejectsNonContiguousSelection},
        {"nested group walk, path and inherited lock", nestedGroupsWalkAndLock},
        {"pass-through and isolated group blending", groupPassThroughAndIsolation},
        {"clipping uses group composite alpha", clippingUsesGroupComposite},
        {"nested group project round trip", nestedGroupRoundTrip},
        {"deep knockout uses the Background floor", deepKnockoutUsesBackgroundFloor},
        {"partial pass-through group isolates adjustments", partialPassThroughGroupIsIsolated},
        {"group knockout uses composite shape", groupKnockoutUsesItsCompositeShape},
        {"paragraph text wraps and keeps legacy projects readable", paragraphTextWrapsAndPersists},
        {"type follows and saves a Bezier path", textFollowsBezierPathAndPersists},
        {"text path placement respects open ends and closed seams", textPathPlacementRangeBehaves},
        {"paragraph alignment applies independently", textParagraphAlignmentAppliesPerParagraph},
        {"paragraph spacing moves later paragraphs", textParagraphSpacingMovesFollowingParagraphs},
        {"rich text runs normalize UTF-8 and persist", richTextRunsNormalizeAndPersist},
        {"rich text run styles map by Unicode character", richTextStylesMapToUnicodeCharacters},
        {"rich text run size and tracking affect layout and rasterization", richTextRunMetricsAffectLayoutAndInk},
        {"rich text run leading affects line baselines", richTextRunLeadingControlsSubsequentLineBaseline},
        {"rich text run scale and baseline shift affect glyph geometry", richTextRunTransformsMatchPhotoCraft},
        {"rich text faux bold and italic affect rasterized glyphs", richTextFauxBoldAndItalicAffectInk},
        {"rich text underline and strikethrough use font metrics", richTextUnderlineAndStrikeUseFontMetrics},
        {"type tool word boundaries", typeWordBoundariesMatchPhotoCraft},
        {"type double-click word selection uses Unicode alphanumeric runs", typeDoubleClickWordRangesMatchPhotoCraft},
        {"type document-edge movement preserves Shift anchor", typeDocumentEdgeMovementMatchesPhotoCraft},
        {"type navigation shortcuts respect spacing modifiers", typeNavigationShortcutsMatchPhotoCraft},
        {"vertical Type arrow keys follow columns", verticalTypeArrowsFollowTextColumns},
        {"Type Home and End follow visual editor rows", typeEditorHomeEndFollowVisualRows},
        {"Type metric shortcuts step each selected character", typeCharacterMetricsStepPerRun},
        {"Type Character panel leading edits selected runs", typeLeadingPanelEditsSelectedRun},
        {"Type Character panel baseline shift edits selected runs", typeBaselinePanelEditsSelectedRun},
        {"Type Character panel horizontal scale edits selected runs", typeHorizontalScalePanelEditsSelectedRun},
        {"Type Character panel vertical scale edits selected runs", typeVerticalScalePanelEditsSelectedRun},
        {"Crop Classic Mode keeps Show Cropped Area on and guards H/P", cropClassicModeKeepsShowAreaAndShortcutRules},
        {"Crop frame Undo and Redo use crop history", cropFrameUndoRedoUsesItsOwnHistory},
        {"Crop Front Image preserves resolution units", cropFrontImageUsesCurrentDocumentAndUnits},
        {"Crop Front Image shortcut respects input modifiers", cropFrontImageShortcutUsesUnmodifiedCropInput},
        {"Crop hidden frame returns on demand", cropHiddenFrameRestoresOnlyOnDemand},
        {"Type style shortcuts toggle selected character runs", typeCharacterStyleShortcutsToggleSelections},
        {"rich text all caps changes rasterized glyphs and persists", richTextAllCapsRendersAndPersists},
        {"Type edits preserve character and paragraph runs", typeEditingPreservesCharacterAndParagraphRuns},
        {"Type tabs use PhotoCraft tab stops and wrap widths", typeTabsUsePhotoCraftStops},
        {"Type size edits respect selection scope", typeSizeEditsRespectSelectionScope},
        {"Type tracking edits respect selection scope", typeTrackingEditsRespectSelectionScope},
        {"Type font edits respect selection scope", typeFontEditsRespectSelectionScope},
        {"manual Type pair kerning starts from font metrics", typeManualPairKerningStartsFromMetrics},
        {"type modified deletion uses PhotoCraft boundaries", typeModifiedDeletionMatchesPhotoCraft},
        {"type multi-click count cycle matches PhotoCraft", typeMultiClickCountsMatchPhotoCraft},
        {"type multi-click selects word, line, paragraph and all", typeMultiClickRangesMatchPhotoCraft},
        {"type caret actions restart blink phase", typeCaretActionsRestartBlink},
        {"type hit testing follows nested group visibility", typeHitVisibilityFollowsNestedGroups},
        {"text layer name skips leading blank lines", textLayerNameSkipsLeadingBlankLines},
        {"smart quotes use opening and closing context", smartQuotesMapOpeningAndClosingContext},
        {"typed smart quotes use caret context", typedSmartQuotesUseTheExistingCaretContext},
        {"type unit conversion follows document resolution", typeUnitsFollowResolution},
        {"text summary follows first character run", textSummaryFollowsFirstStyleRun},
        {"rich text runs survive project round trip", richTextRunsSurviveProjectRoundTrip},
        {"trailing empty paragraph keeps its character style", trailingEmptyParagraphKeepsItsStyle},
        {"recent font history de-duplicates and trims", recentFontsMoveForwardAndRespectCaps},
    };
    int failed = 0;
    for (const Test& test : tests) {
        const bool passed = test.run();
        fprintf(stderr, "selftest: %s: %s\n", passed ? "PASS" : "FAIL", test.name);
        if (!passed) ++failed;
    }
    fprintf(stderr, "selftest: %d/%zu passed\n", (int)(sizeof(tests) / sizeof(tests[0])) - failed, sizeof(tests) / sizeof(tests[0]));
    return failed ? 1 : 0;
}
