// Layer group checks ported from PhotoCraft (crates/doc/src/lib.rs and crates/compose/src/tests.rs).
#include "doc.h"
#include "blend.h"
#include "text.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <vector>

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
    const size_t finalSpace = encoded.find_last_of(' ');
    if (!decoded.load(encoded) || finalSpace == std::string::npos || !oldText2.load(encoded.substr(0, finalSpace)) || oldText2.pathEnd != -1) return false;
    Doc project = newDoc(500, 300, false); Fill fill; fill.kind = FILL_SOLID; addTextLayer(project, text, fill);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "chimera-text-path-selftest.chimera";
    std::string error; Doc loaded;
    const bool saved = saveProject(project, path.u8string(), error) && openDoc(path.u8string(), loaded, error);
    std::error_code ec; std::filesystem::remove(path, ec);
    const auto layer = std::find_if(loaded.layers.begin(), loaded.layers.end(), [](const Layer& l) { return l.isText(); });
    return ink > 100 && box[3] < 180 && closedInk > openInk * 1.4f && decoded.pathText && decoded.path.size() == 1 && decoded.path[0].knots.size() == 2 &&
           decoded.pathEnd == bounded.pathEnd && saved && layer != loaded.layers.end() && layer->text.pathText && layer->text.path.size() == 1 && layer->text.pathStart == text.pathStart;
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
