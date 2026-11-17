// Layer group checks ported from PhotoCraft (crates/doc/src/lib.rs and crates/compose/src/tests.rs).
#include "doc.h"
#include "blend.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
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
