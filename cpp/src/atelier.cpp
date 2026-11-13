// Chimera - finding Atelier apps (see atelier.h)
#include "atelier.h"
#include "util.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace Atelier {

static bool isFile(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }

std::string findTool(const std::string& app, const std::string& exeName, const std::string& prefFile, const std::string& exeFolder) {
#ifdef _WIN32
    const std::string exe = exeName + ".exe";
#else
    const std::string exe = exeName;
#endif
    if (FILE* f = openFile(prefFile, "rb")) {
        char b[4096] = {0}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
        std::string s(b, n);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty() && isFile(u8path(s))) return s;
    }
    const fs::path here = u8path(exeFolder);
    for (const fs::path& p : {here / exe, here / "plugins" / app / exe}) if (isFile(p)) return p.u8string();
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA")) { const fs::path p = u8path(la) / "Programs" / app / exe; if (isFile(p)) return p.u8string(); }
#endif
    for (fs::path d = here; !d.empty(); d = d.parent_path()) {
        for (const char* sub : {"dist", "build/Release", "build", ""}) {
            const fs::path p = std::string(sub) == "dist" ? d / app / "dist" / app / exe : d / app / sub / exe;
            if (isFile(p)) return p.u8string();
        }
        if (d == d.parent_path()) break;
    }
    return "";
}

void remember(const std::string& prefFile, const std::string& path) {
    if (FILE* f = openFile(prefFile, "wb")) { fwrite(path.data(), 1, path.size(), f); fclose(f); }
}

}
