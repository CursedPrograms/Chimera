// LayerTack - Filter > Bathos: depth maps from Bathos (Atelier's depth app), as a plugin. LayerTack doesn't carry
// the depth model: it finds bathos-cli on this PC (next to LayerTack, in its plugins folder, where Bathos is
// installed, or a Bathos folder near LayerTack) and runs it in the background.
#pragma once
#include <string>
#include <vector>

namespace Bathos {

struct Options {
    int detail = 518;          // the long side the model sees
    bool refine = true;        // edges follow the picture's
    bool faces = true;         // proper faces (a 3D face mesh each)
    float faceRelief = 1.0f;
    bool colour = false;       // a colour ramp instead of grey
    bool farBright = false;
    float contrast = 1.0f;
};

std::string cli();             // bathos-cli's path ("" when Bathos isn't on this PC)
void setCli(const std::string& path);   // remembered in LayerTack's settings folder
void init(const std::string& prefDir, const std::string& exeDir);
// bathos-cli's command line: in.png -> out.png
std::vector<std::string> args(const Options& o, const std::string& inPng, const std::string& outPng);

}
