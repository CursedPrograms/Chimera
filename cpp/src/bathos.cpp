// LayerTack - Filter > Bathos (see bathos.h)
#include "bathos.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace Bathos {

static std::string prefFile, exeFolder, found;

#ifdef _WIN32
static const char* EXE = "bathos-cli.exe";
#else
static const char* EXE = "bathos-cli";
#endif

static bool isCli(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }

static std::string find() {
    // 1. the one picked before
    if (FILE* f = openFile(prefFile, "rb")) {
        char b[4096] = {0}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
        std::string s(b, n);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty() && isCli(u8path(s))) return s;
    }
    // 2. next to LayerTack, or in its plugins folder (a packaged Atelier puts it there)
    const fs::path here = u8path(exeFolder);
    for (const fs::path& p : {here / EXE, here / "plugins" / "Bathos" / EXE}) if (isCli(p)) return p.u8string();
    // 3. installed (%LOCALAPPDATA%\Programs\Bathos)
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA")) { const fs::path p = u8path(la) / "Programs" / "Bathos" / EXE; if (isCli(p)) return p.u8string(); }
#endif
    // 4. a Bathos folder near LayerTack (D:/cc/LayerTack/cpp/dist/LayerTack -> D:/cc/Bathos), built or packaged
    for (fs::path d = here; !d.empty(); d = d.parent_path()) {
        for (const char* sub : {"dist/Bathos", "build/Release", "build", ""}) {
            const fs::path p = d / "Bathos" / sub / EXE;
            if (isCli(p)) return p.u8string();
        }
        if (d == d.parent_path()) break;
    }
    return "";
}

std::string cli() { return found; }

void setCli(const std::string& path) {
    found = path;
    if (FILE* f = openFile(prefFile, "wb")) { fwrite(path.data(), 1, path.size(), f); fclose(f); }
}

void init(const std::string& prefDir, const std::string& exeDir) {
    prefFile = (u8path(prefDir) / "bathos.txt").u8string();
    exeFolder = exeDir;
    found = find();
}

static std::string num(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }

std::vector<std::string> args(const Options& o, const std::string& inPng, const std::string& outPng) {
    std::vector<std::string> a = {found, inPng, "-o", outPng, "--size", std::to_string(o.detail)};
    if (!o.refine) a.push_back("--no-refine");
    if (!o.faces) a.push_back("--no-faces");
    else { a.push_back("--face-relief"); a.push_back(num(o.faceRelief)); }
    if (o.colour) a.push_back("--colour");
    if (o.farBright) a.push_back("--far-bright");
    if (o.contrast != 1.0f) { a.push_back("--contrast"); a.push_back(num(o.contrast)); }
    return a;
}

}
