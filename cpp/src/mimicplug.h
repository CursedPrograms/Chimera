// LayerTack - Filter > Mimic: face swap from Mimic (Atelier's face-swap app), as a plugin. LayerTack doesn't carry
// the face models: it finds mimic-cli on this PC (see atelier.h) and runs it in the background.
#pragma once
#include <string>
#include <vector>

namespace MimicPlug {

struct Options {
    std::vector<std::string> faces;   // pictures of the face to become
    int swapper = 0;                  // 0 inswapper, 1 hyperswap
    int boost = 1;
    float likeness = 0.5f;            // mimic-cli's --weight
    int enhancer = 1;                 // 0 none, 1 gfpgan, 2 codeformer, 3 gpen
    float blend = 0.8f;
    bool occlusion = false;
    int which = 0;                    // 0 every face, 1 the biggest
};

std::string cli();
void setCli(const std::string& path);
void init(const std::string& prefDir, const std::string& exeDir);
std::vector<std::string> args(const Options& o, const std::string& inPng, const std::string& outPng);

}
