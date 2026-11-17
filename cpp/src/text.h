// Chimera - text layers, after PhotoCraft (crates/text: point text, horizontal or vertical): the words kept as their
// settings, so they can be changed after, drawn with stb_truetype from the fonts on this PC. The layer's Fill colours
// them (a colour or a gradient), as a shape layer's.
#pragma once
#include <string>
#include <vector>

struct Text {
    std::string str;                 // UTF-8, lines split by '\n'
    std::string font;                // a .ttf / .otf / .ttc path ("" the default: Segoe UI)
    float size = 48;                 // pixels (the em)
    float x = 0, y = 0;              // where it starts on the canvas: the first line's top left (centred / right: its anchor)
    int align = 0;                   // 0 left, 1 centre, 2 right (vertical: top, middle, bottom)
    bool vertical = false;           // letters top to bottom, lines right to left (PhotoCraft's vertical type)
    float tracking = 0;              // extra space between letters, 1/1000 em (Photoshop's units)
    float leading = 1.2f;            // line spacing, times the size
    float boxWidth = 0, boxHeight = 0; // paragraph frame in pixels; zero width means point text
    float scaleX = 1, scaleY = 1, rotation = 0; // text transform, rotation in degrees
    float box[4] = {0, 0, 0, 0};     // where it was last drawn (canvas), for clicking on it (not saved)
    bool active() const { return !str.empty() || !font.empty(); }
    std::string save() const;
    bool load(const std::string& blob);
};

// the fonts on this PC (Windows' Fonts folder, the user's own): their files and names, sorted by name
struct FontFile { std::string path, name; };
const std::vector<FontFile>& systemFonts();
std::string defaultFont();

// coverage (0..1) of the text over a w x h canvas; box: where it is (x0, y0, x1, y1), for picking and outlines
bool textCoverage(const Text& t, int w, int h, std::vector<float>& cov, float box[4]);
