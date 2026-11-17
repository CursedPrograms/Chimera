// Chimera - shape layers, after PhotoCraft (crates/vector/src/shapes.rs and doc/src/vector.rs, MIT OR Apache-2.0):
// Rectangle (with rounded corners), Ellipse, Triangle, Polygon, Star and Line, each a live shape kept as its
// settings (so it can be changed after) and drawn anti-aliased: its fill (the layer's Fill: a colour or a gradient)
// inside, and a stroke on its outline.
#pragma once
#include <string>
#include <vector>

enum ShapeKind { SHAPE_NONE, SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_TRIANGLE, SHAPE_POLYGON, SHAPE_STAR, SHAPE_LINE, SHAPE_COUNT };
const char* shapeName(int k);   // "Rectangle"...

struct Shape {
    int kind = SHAPE_NONE;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // its box on the canvas (a line: from (x0, y0) to (x1, y1))
    float radius = 0;                       // rounded corners (rectangle, triangle, polygon, star), pixels
    int sides = 5;                          // polygon and star
    float inner = 0.5f;                     // star: the inner points' distance, 0.05..0.95 of the outer
    float weight = 4;                       // line: its thickness, pixels
    bool fillOn = true;
    bool strokeOn = false;
    float stroke[3] = {0, 0, 0};
    float strokeWidth = 3;                  // pixels, centred on the outline
    std::string save() const;
    bool load(const std::string& blob);
    void move(float dx, float dy) { x0 += dx; x1 += dx; y0 += dy; y1 += dy; }
};

// its outline as a closed polygon (x, y pairs), curves and rounded corners flattened
std::vector<float> shapeOutline(const Shape& s);
// coverage (0..1) over a w x h canvas: the fill's and the stroke's
void shapeCoverage(const Shape& s, int w, int h, std::vector<float>& fill, std::vector<float>& stroke);
