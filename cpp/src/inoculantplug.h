// Chimera - Inoculant (Atelier's poison-and-antidote app) as a plugin: File > Save with Inoculant protects the picture
// as it's saved (a cloak against face swapping and face recognition), File > Open and Cure with Inoculant washes
// a picture before it opens (Inoculant's purifiers) and tells you first whether it looks cloaked. Chimera doesn't
// carry the face models: it finds inoculant-cli on this PC (see atelier.h) and runs it in the background.
#pragma once
#include <string>
#include <vector>

namespace InoculantPlug {

struct Protect {
    float strength = 4;        // how far each pixel may move, levels of 255 (more: stronger and more visible)
    int steps = 40;            // how long it works on it
    float robustness = 0.5f;   // how much it trains against being washed off (JPEG, blur...)
    float texture = 0.6f;      // hide the change in busy areas rather than smooth skin
};
struct Cure {                  // each purifier; the far end of its slider is "off"
    int jpeg = 75;             // quality (100: off)
    float blur = 0;            // sigma (0: off)
    int median = 1;            // radius (0: off)
    int resize = 100;          // shrink to % and back (100: off)
    float noise = 0;           // sigma, levels (0: off)
};

std::string cli();
void setCli(const std::string& path);
void init(const std::string& prefDir, const std::string& exeDir);
std::vector<std::string> protectArgs(const Protect& o, const std::string& in, const std::string& out);
std::vector<std::string> cureArgs(const Cure& o, const std::string& in, const std::string& out);
std::vector<std::string> inspectArgs(const std::string& in);

}
