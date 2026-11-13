// Chimera - a timing check of the operations (not shipped): ltbench picture.png
#include "doc.h"
#include "ops.h"
#include "refine.h"
#include "fx.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    Doc d; std::string err;
    if (!openDoc(argv[1], d, err)) { printf("%s\n", err.c_str()); return 1; }
    auto t0 = std::chrono::steady_clock::now();
    auto tick = [&](const char* n) { auto t1 = std::chrono::steady_clock::now(); printf("%-14s %7.1f ms\n", n, std::chrono::duration<double, std::milli>(t1 - t0).count()); t0 = t1; };
    printf("%d x %d\n", d.w, d.h);
    if (argc > 3 && std::string(argv[2]) == "fx") {
        // every Sharpen / Distort / Stylize filter once, timed, saved as <dir>/fx_<name>.jpg
        auto run = [&](const char* name, auto f) {
            Doc e = d; Work o = getWork(e), r = o;
            auto t1 = std::chrono::steady_clock::now();
            f(r, fxBounds(e, r));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
            putWork(e, o, r);
            std::string out = std::string(argv[3]) + "/fx_" + name + ".jpg";
            if (!saveImage(e, out, err)) printf("%s\n", err.c_str());
            printf("%-12s %7.1f ms\n", name, ms);
        };
        run("unsharp", [](Work& w, FxBox) { fxUnsharp(w, 200, 3, 0); });
        run("highpass", [](Work& w, FxBox) { fxHighPass(w, 8); });
        run("twirl", [](Work& w, FxBox b) { fxTwirl(w, b, 120); });
        run("pinch", [](Work& w, FxBox b) { fxPinch(w, b, 80); });
        run("spherize", [](Work& w, FxBox b) { fxSpherize(w, b, 100, 0); });
        run("wave", [](Work& w, FxBox b) { fxWave(w, b, 5, 10, 120, 5, 35, 0, 1); });
        run("ripple", [](Work& w, FxBox b) { fxRipple(w, b, 300, 1); });
        run("polar", [](Work& w, FxBox b) { fxPolar(w, b, true); });
        run("mosaic", [](Work& w, FxBox b) { fxMosaic(w, b, 24); });
        run("emboss", [](Work& w, FxBox) { fxEmboss(w, 135, 3, 200); });
        run("findedges", [](Work& w, FxBox) { fxFindEdges(w); });
        run("solarize", [](Work& w, FxBox) { fxSolarize(w); });
        run("motion", [](Work& w, FxBox) { fxMotionBlur(w, 20, 60); });
        run("radialspin", [](Work& w, FxBox b) { fxRadialBlur(w, b, 15, false, 1, 0.5f, 0.5f); });
        run("radialzoom", [](Work& w, FxBox b) { fxRadialBlur(w, b, 30, true, 1, 0.5f, 0.5f); });
        run("median", [](Work& w, FxBox) { fxMedian(w, 8); });
        run("clouds", [](Work& w, FxBox b) { const uint8_t f[3] = {224, 64, 138}, k[3] = {26, 26, 26}; fxClouds(w, b, f, k, 5, false); });
        run("crystallize", [](Work& w, FxBox b) { fxCrystallize(w, b, 18, 1); });
        run("pointillize", [](Work& w, FxBox b) { const uint8_t k[3] = {255, 255, 255}; fxPointillize(w, b, 8, 1, k); });
        // Select > Modify on an ellipse, timed
        Doc e = d; e.sel = shapeEllipse(e, e.w / 4, e.h / 4, e.w * 3 / 4, e.h * 3 / 4);
        auto t1 = std::chrono::steady_clock::now();
        selExpand(e, 100); selContract(e, 50); selSmooth(e, 20); selBorder(e, 30);
        printf("%-12s %7.1f ms\n", "sel modify", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
        int any = 0; for (uint8_t v : e.sel) any += v > 128;
        printf("border pixels %d\n", any);
        return 0;
    }
    if (argc > 2 && std::string(argv[2]) == "refine") {
        // Select and Mask on an ellipse round the middle: the time of each stage, and nothing outside 0..1
        Image comp; composite(d, comp, {0, 0, d.w, d.h}, false);
        std::vector<float> rgb((size_t)d.w * d.h * 3), m((size_t)d.w * d.h);
        for (size_t i = 0; i < m.size(); ++i) for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = comp.px[i * 4 + c] / 255.0f;
        for (int y = 0; y < d.h; ++y) for (int x = 0; x < d.w; ++x) { const float u = (x - d.w / 2.0f) / (d.w * 0.3f), v = (y - d.h / 2.0f) / (d.h * 0.4f); m[(size_t)y * d.w + x] = u * u + v * v < 1 ? 1.0f : 0.0f; }
        tick("setup");
        RefineCache cache; RefineParams rp; rp.radius = 12; rp.smart = true;
        auto a1 = refineMask(rgb, m, d.w, d.h, rp, &cache); tick("edge r12 smart");
        rp.feather = 40; rp.smooth = 30; rp.contrast = 20; rp.shift = -30;
        auto a2 = refineMask(rgb, m, d.w, d.h, rp, &cache); tick("cached + rest");
        rp.radius = 120; rp.smart = false;
        auto a3 = refineMask(rgb, m, d.w, d.h, rp, &cache); tick("edge r120");
        decontaminate(comp.px.data(), a3, d.w, d.h, 120, 80); tick("decontaminate");
        int bad = 0; size_t soft = 0;
        for (auto* a : {&a1, &a2, &a3}) for (float v : *a) { bad += !(v >= 0 && v <= 1); soft += v > 0.02f && v < 0.98f; }
        printf("soft pixels %zu, out of range %d\n", soft, bad);
        return bad ? 1 : 0;
    }
    if (argc > 3 && std::string(argv[2]) == "adj") {
        // every adjustment kind: through a .ltk project and back, the same picture; merged down, the same picture
        auto diff = [](const Doc& a, const Doc& b) {
            Image x, y; composite(a, x, {0, 0, a.w, a.h}, false); composite(b, y, {0, 0, b.w, b.h}, false);
            int m = 0; for (size_t i = 0; i < x.px.size(); ++i) m = std::max(m, std::abs((int)x.px[i] - (int)y.px[i]));
            return m;
        };
        int bad = 0;
        for (int k = 1; k < ADJ_COUNT; ++k) {
            Doc e = d;
            addAdjustmentLayer(e, k);
            Adjustment& a = e.cur().adj;
            a.v[0] = a.v[0] * 0.5f + 0.3f; a.flag = !a.flag;
            if (k == ADJ_CURVES) a.curve[1] = {0, 0, 0.5f, 0.7f, 1, 1};
            e.cur().opacity = 0.8f;
            std::string p = std::string(argv[3]) + "/adj" + std::to_string(k) + ".ltk";
            Doc f;
            if (!saveProject(e, p, err) || !openDoc(p, f, err)) { printf("%-18s %s\n", adjName(k), err.c_str()); ++bad; continue; }
            const int dl = diff(e, f);
            Doc g = e; mergeDown(g);
            const int dm = diff(e, g);
            const int dd = diff(e, d);
            const bool no = dl > 0 || dm > 2 || f.layers.size() != e.layers.size() || !f.layers.back().isAdjust();
            printf("%-18s reload %d  merge %d  (changes the picture by %d)%s\n", adjName(k), dl, dm, dd, no ? "  BAD" : "");
            bad += no;
        }
        return bad;
    }
    tick("start");
    Work o = getWork(d); tick("getWork");
    Work r = o; tick("copy");
    putWork(d, o, r); tick("putWork");
    filterBoxBlur(r, 6); tick("boxBlur 6");
    filterGaussianBlur(r, 12); tick("gauss 12");
    filterAddNoise(r, 8, true, true, 1); tick("noise");
    Image out; composite(d, out, {0, 0, d.w, d.h}, false); tick("composite");
    resizeImage(d, d.w * 3 / 4, d.h * 3 / 4); tick("resize");
}
