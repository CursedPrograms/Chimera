// LayerTack - small helpers: UTF-8 paths (file names with accents work on Windows too)
#pragma once
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

inline std::filesystem::path u8path(const std::string& s) { return std::filesystem::u8path(s); }
inline FILE* openFile(const std::string& path, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + strlen(mode));
    return _wfopen(u8path(path).wstring().c_str(), m.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}
