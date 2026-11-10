// LayerTack - Select Subject through ONNX Runtime's C API (loaded at run time, so LayerTack runs without it).
#include "segment.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#else
#include <dlfcn.h>
#endif
#include "onnxruntime_c_api.h"

static const char* MODEL_URL = "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx";
static const int SIDE = 320;

std::string segmentModelPath() {
    namespace fs = std::filesystem;
    std::error_code ec;
#ifdef _WIN32
    if (fs::exists("F:/AIModels", ec)) return "F:/AIModels/segment/u2netp.onnx";
    const char* app = std::getenv("APPDATA");
    return std::string(app ? app : ".") + "/LayerTack/models/u2netp.onnx";
#else
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/LayerTack/models/u2netp.onnx";
#endif
}

static bool fetchModel(const std::string& path, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(u8path(path), ec) && fs::file_size(u8path(path), ec) > 1000000) return true;
    fs::create_directories(u8path(path).parent_path(), ec);
#ifdef _WIN32
    if (URLDownloadToFileW(nullptr, L"https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx", u8path(path).wstring().c_str(), 0, nullptr) != S_OK) {
#else
    if (std::system(("curl -sL -o '" + path + "' " + MODEL_URL).c_str()) != 0) {
#endif
        err = "Couldn't download the subject model (" + std::string(MODEL_URL) + ")";
        return false;
    }
    return true;
}

static const OrtApi* loadOrt(std::string& err) {
    static const OrtApi* api = nullptr;
    static bool tried = false;
    if (tried) { if (!api) err = "ONNX Runtime (onnxruntime.dll) isn't next to LayerTack"; return api; }
    tried = true;
    typedef const OrtApiBase*(ORT_API_CALL * GetBase)();
    GetBase getBase = nullptr;
#ifdef _WIN32
    HMODULE h = LoadLibraryW(L"onnxruntime.dll");
    if (h) getBase = (GetBase)GetProcAddress(h, "OrtGetApiBase");
#else
    void* h = dlopen("libonnxruntime.so", RTLD_NOW);
    if (h) getBase = (GetBase)dlsym(h, "OrtGetApiBase");
#endif
    if (!getBase) { err = "ONNX Runtime (onnxruntime.dll) isn't next to LayerTack"; return nullptr; }
    api = getBase()->GetApi(ORT_API_VERSION);
    if (!api) err = "This ONNX Runtime is too old for LayerTack";
    return api;
}

bool segmentSubject(const Image& img, std::vector<uint8_t>& out, std::string& err) {
    const OrtApi* ort = loadOrt(err);
    if (!ort) return false;
    const std::string model = segmentModelPath();
    if (!fetchModel(model, err)) return false;

    static OrtEnv* env = nullptr;
    static OrtSession* session = nullptr;
    auto check = [&](OrtStatus* s) {
        if (!s) return true;
        err = std::string("ONNX Runtime: ") + ort->GetErrorMessage(s);
        ort->ReleaseStatus(s);
        return false;
    };
    if (!session) {
        if (!check(ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "LayerTack", &env))) return false;
        OrtSessionOptions* opts;
        if (!check(ort->CreateSessionOptions(&opts))) return false;
#ifdef _WIN32
        const std::wstring wp = u8path(model).wstring();
        const bool ok = check(ort->CreateSession(env, wp.c_str(), opts, &session));
#else
        const bool ok = check(ort->CreateSession(env, model.c_str(), opts, &session));
#endif
        ort->ReleaseSessionOptions(opts);
        if (!ok) { session = nullptr; return false; }
    }

    // the picture, squashed to 320 x 320, scaled to its brightest value, then ImageNet-normalised (as rembg does)
    std::vector<float> input((size_t)3 * SIDE * SIDE);
    float mx = 1e-6f;
    for (size_t i = 0; i < img.px.size(); i += 4) mx = std::max({mx, (float)img.px[i], (float)img.px[i + 1], (float)img.px[i + 2]});
    const float mean[3] = {0.485f, 0.456f, 0.406f}, sd[3] = {0.229f, 0.224f, 0.225f};
    for (int y = 0; y < SIDE; ++y)
        for (int x = 0; x < SIDE; ++x) {
            const float sx = std::min(img.w - 1.0f, (x + 0.5f) * img.w / SIDE - 0.5f), sy = std::min(img.h - 1.0f, (y + 0.5f) * img.h / SIDE - 0.5f);
            const int x0 = std::max(0, (int)sx), y0 = std::max(0, (int)sy), x1 = std::min(img.w - 1, x0 + 1), y1 = std::min(img.h - 1, y0 + 1);
            const float fx = std::max(0.0f, sx - x0), fy = std::max(0.0f, sy - y0);
            for (int c = 0; c < 3; ++c) {
                const float v = (img.at(x0, y0)[c] * (1 - fx) + img.at(x1, y0)[c] * fx) * (1 - fy) + (img.at(x0, y1)[c] * (1 - fx) + img.at(x1, y1)[c] * fx) * fy;
                input[(size_t)c * SIDE * SIDE + (size_t)y * SIDE + x] = (v / mx - mean[c]) / sd[c];
            }
        }

    OrtMemoryInfo* mem;
    if (!check(ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem))) return false;
    const int64_t shape[4] = {1, 3, SIDE, SIDE};
    OrtValue* in = nullptr;
    const bool made = check(ort->CreateTensorWithDataAsOrtValue(mem, input.data(), input.size() * sizeof(float), shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in));
    ort->ReleaseMemoryInfo(mem);
    if (!made) return false;

    OrtAllocator* alloc;
    ort->GetAllocatorWithDefaultOptions(&alloc);
    char *inName, *outName;
    if (!check(ort->SessionGetInputName(session, 0, alloc, &inName)) || !check(ort->SessionGetOutputName(session, 0, alloc, &outName))) { ort->ReleaseValue(in); return false; }
    OrtValue* result = nullptr;
    const char* inNames[] = {inName};
    const char* outNames[] = {outName};
    const bool ran = check(ort->Run(session, nullptr, inNames, &in, 1, outNames, 1, &result));
    alloc->Free(alloc, inName); alloc->Free(alloc, outName);
    ort->ReleaseValue(in);
    if (!ran) return false;
    float* pred;
    if (!check(ort->GetTensorMutableData(result, (void**)&pred))) { ort->ReleaseValue(result); return false; }

    // stretch to 0..1, then back up to the picture's size
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < SIDE * SIDE; ++i) { lo = std::min(lo, pred[i]); hi = std::max(hi, pred[i]); }
    const float span = std::max(1e-6f, hi - lo);
    out.assign((size_t)img.w * img.h, 0);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const float sx = std::clamp((x + 0.5f) * SIDE / img.w - 0.5f, 0.0f, SIDE - 1.0f), sy = std::clamp((y + 0.5f) * SIDE / img.h - 0.5f, 0.0f, SIDE - 1.0f);
            const int x0 = (int)sx, y0 = (int)sy, x1 = std::min(SIDE - 1, x0 + 1), y1 = std::min(SIDE - 1, y0 + 1);
            const float fx = sx - x0, fy = sy - y0;
            const float v = (pred[y0 * SIDE + x0] * (1 - fx) + pred[y0 * SIDE + x1] * fx) * (1 - fy) + (pred[y1 * SIDE + x0] * (1 - fx) + pred[y1 * SIDE + x1] * fx) * fy;
            out[(size_t)y * img.w + x] = (uint8_t)std::lround(std::clamp((v - lo) / span, 0.0f, 1.0f) * 255.0f);
        }
    ort->ReleaseValue(result);
    return true;
}
