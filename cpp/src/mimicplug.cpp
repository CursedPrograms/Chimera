// LayerTack - Filter > Mimic (see mimicplug.h)
#include "mimicplug.h"
#include "atelier.h"
#include "util.h"
#include <cstdio>

namespace MimicPlug {

static std::string prefFile, exeFolder, found;

std::string cli() { return found; }
void setCli(const std::string& path) { found = path; Atelier::remember(prefFile, path); }

void init(const std::string& prefDir, const std::string& exeDir) {
    prefFile = (u8path(prefDir) / "mimic.txt").u8string();
    exeFolder = exeDir;
    found = Atelier::findTool("Mimic", "mimic-cli", prefFile, exeFolder);
}

static std::string num(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }

std::vector<std::string> args(const Options& o, const std::string& inPng, const std::string& outPng) {
    std::vector<std::string> a = {found};
    for (const auto& f : o.faces) { a.push_back("-s"); a.push_back(f); }
    a.insert(a.end(), {"-t", inPng, "-o", outPng, "--swapper", o.swapper == 1 ? "hyperswap" : "inswapper",
                       "--boost", std::to_string(o.boost), "--weight", num(o.likeness)});
    static const char* enh[] = {"none", "gfpgan", "codeformer", "gpen"};
    a.push_back("--enhancer"); a.push_back(enh[o.enhancer < 0 || o.enhancer > 3 ? 1 : o.enhancer]);
    a.push_back("--blend"); a.push_back(num(o.blend));
    if (o.occlusion) a.push_back("--occlusion");
    if (o.which == 1) { a.push_back("--faces"); a.push_back("largest"); }
    return a;
}

}
