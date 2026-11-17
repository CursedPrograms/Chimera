// Chimera - Magnetic Lasso live wire, after PhotoCraft (crates/algo/src/magnetic.rs).
#pragma once
#include "doc.h"
#include <array>
#include <unordered_map>
#include <vector>

namespace Magnetic {
struct Point { double x = 0, y = 0; };
struct Settings { float width = 10, contrast = 0.1f; };

class Tracer {
    const Image& image;
    std::unordered_map<long long, std::vector<std::array<uint8_t, 4>>> blocks;
public:
    explicit Tracer(const Image& source) : image(source) {}
    std::array<uint8_t, 4> pixel(int x, int y);
    Point snap(Point p, Settings s);
    std::vector<Point> trace(Point from, Point to, const std::vector<Point>& guide, Settings s);
};
}
