// LayerTack - a timing check of the operations (not shipped): ltbench picture.png
#include "doc.h"
#include "ops.h"
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
