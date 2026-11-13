// Chimera - Filter > Bathos (see bathos.h)
#include "bathos.h"
#include "atelier.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace Bathos {

static std::string prefFile, exeFolder, found;

std::string cli() { return found; }

void setCli(const std::string& path) { found = path; Atelier::remember(prefFile, path); }

void init(const std::string& prefDir, const std::string& exeDir) {
    prefFile = (u8path(prefDir) / "bathos.txt").u8string();
    exeFolder = exeDir;
    found = Atelier::findTool("Bathos", "bathos-cli", prefFile, exeFolder);
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
