// Chimera - Inoculant as a plugin (see inoculantplug.h)
#include "inoculantplug.h"
#include "atelier.h"
#include "util.h"

#include <cstdio>

namespace InoculantPlug {

static std::string prefFile, found;

std::string cli() { return found; }
void setCli(const std::string& path) { found = path; Atelier::remember(prefFile, path); }
void init(const std::string& prefDir, const std::string& exeDir) {
    prefFile = (u8path(prefDir) / "inoculant.txt").u8string();
    found = Atelier::findTool("Inoculant", "inoculant-cli", prefFile, exeDir);
}

static std::string num(float v) { char b[32]; snprintf(b, sizeof b, "%.3g", v); return b; }

std::vector<std::string> protectArgs(const Protect& o, const std::string& in, const std::string& out) {
    return {found, "protect", in, out, "--strength", num(o.strength), "--steps", std::to_string(o.steps),
            "--robustness", num(o.robustness), "--texture", num(o.texture)};
}

std::vector<std::string> cureArgs(const Cure& o, const std::string& in, const std::string& out) {
    std::vector<std::string> a = {found, "purify", in, out};
    if (o.jpeg < 100) { a.push_back("--jpeg"); a.push_back(std::to_string(o.jpeg)); }
    if (o.blur > 0) { a.push_back("--blur"); a.push_back(num(o.blur)); }
    if (o.median > 0) { a.push_back("--median"); a.push_back(std::to_string(o.median)); }
    if (o.resize < 100) { a.push_back("--resize"); a.push_back(std::to_string(o.resize)); }
    if (o.noise > 0) { a.push_back("--noise"); a.push_back(num(o.noise)); }
    if (a.size() == 4) { a.push_back("--jpeg"); a.push_back("100"); }   // everything off: just a clean copy
    return a;
}

std::vector<std::string> inspectArgs(const std::string& in) { return {found, "inspect", in}; }

}
