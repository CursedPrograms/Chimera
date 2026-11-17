// tests: content-aware fill on a picture with a box cut out. inpainttest in.png out.png x0 y0 x1 y1
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "inpaint.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 7) return 1;
    int w, h, n; unsigned char* px = stbi_load(argv[1], &w, &h, &n, 3);
    if (!px) return 1;
    const int x0 = atoi(argv[3]), y0 = atoi(argv[4]), x1 = atoi(argv[5]), y1 = atoi(argv[6]);
    std::vector<float> img((size_t)w * h * 3); std::vector<uint8_t> hole((size_t)w * h, 0);
    for (size_t i = 0; i < img.size(); ++i) img[i] = px[i] / 255.0f;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { hole[(size_t)y * w + x] = 1; for (int c = 0; c < 3; ++c) img[((size_t)y * w + x) * 3 + c] = 1.0f; }
    auto t0 = std::chrono::steady_clock::now();
    const bool ok = Inpaint::complete(w, h, img, hole);
    printf("ok %d, %.0f ms\n", ok, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    double var = 0, mean = 0; int cnt = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { mean += img[((size_t)y * w + x) * 3]; ++cnt; }
    mean /= cnt;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { const double d = img[((size_t)y * w + x) * 3] - mean; var += d * d; }
    printf("hole red: mean %.3f, sd %.4f\n", mean, sqrt(var / cnt));
    for (size_t i = 0; i < img.size(); ++i) px[i] = (unsigned char)std::min(255.0f, std::max(0.0f, img[i] * 255 + 0.5f));
    stbi_write_png(argv[2], w, h, 3, px, w * 3);
    return 0;
}
