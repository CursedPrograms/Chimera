// Chimera - Filter > Mimic (see mimicplug.h)
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
    // mimic-cli takes FaceFusion's option names
    static const char* enh[] = {"none", "gfpgan_1.4", "codeformer", "gpen_bfr_512"};
    const bool enhance = o.enhancer > 0 && o.enhancer <= 3;
    a.insert(a.end(), {"-t", inPng, "-o", outPng, "--output-image-quality", "100", "--processors", "face_swapper"});
    if (enhance) a.push_back("face_enhancer");
    a.insert(a.end(), {"--face-swapper-model", o.swapper == 1 ? "hyperswap_1a_256" : "inswapper_128",
                       "--face-swapper-pixel-boost", std::to_string(o.boost), "--face-swapper-weight", num(o.likeness)});
    if (enhance) { a.insert(a.end(), {"--face-enhancer-model", enh[o.enhancer], "--face-enhancer-blend", std::to_string((int)(o.blend * 100 + 0.5f))}); }
    if (o.occlusion) a.insert(a.end(), {"--face-mask-types", "box", "occlusion"});
    if (o.which == 1) a.insert(a.end(), {"--face-selector-mode", "one", "--face-selector-order", "large-small"});
    return a;
}

}
