// LayerTack - a timing check of the operations (not shipped): ltbench picture.png
#include "doc.h"
#include "ops.h"
#include <chrono>
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    Doc d; std::string err;
    if (!openDoc(argv[1], d, err)) { printf("%s\n", err.c_str()); return 1; }
    auto t0 = std::chrono::steady_clock::now();
    auto tick = [&](const char* n) { auto t1 = std::chrono::steady_clock::now(); printf("%-14s %7.1f ms\n", n, std::chrono::duration<double, std::milli>(t1 - t0).count()); t0 = t1; };
    printf("%d x %d\n", d.w, d.h);
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
