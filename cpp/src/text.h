// Chimera - text layers, after PhotoCraft (crates/text: point text, horizontal or vertical): the words kept as their
// settings, so they can be changed after, drawn with stb_truetype from the fonts on this PC. The layer's Fill colours
// them (a colour or a gradient), as a shape layer's.
#pragma once
#include "shape.h"
#include <string>
#include <vector>

// after PhotoCraft (crates/doc/src/text.rs CharStyle): per-run settings stay editable even
// when the renderer has not yet applied a particular character attribute.
struct TextStyle {
    std::string font;
    float size = 48, tracking = 0, leading = 1.2f;
    float baselineShift = 0, horizontalScale = 1, verticalScale = 1;
    bool bold = false, italic = false, underline = false, strikethrough = false;
    bool operator==(const TextStyle& o) const {
        return font == o.font && size == o.size && tracking == o.tracking && leading == o.leading &&
            baselineShift == o.baselineShift && horizontalScale == o.horizontalScale && verticalScale == o.verticalScale &&
            bold == o.bold && italic == o.italic && underline == o.underline && strikethrough == o.strikethrough;
    }
};
struct TextRun { size_t bytes = 0; TextStyle style; };
struct ParagraphStyle { int align = 0; float firstIndent = 0, startIndent = 0, endIndent = 0, spaceBefore = 0, spaceAfter = 0, autoLeading = 1.2f; };
struct ParagraphRun { size_t bytes = 0; ParagraphStyle style; };

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
    BezierPath path;                  // optional path the type follows, in canvas coordinates
    float pathStart = 0, pathEnd = -1; // path placement interval; a negative end follows the text length
    bool pathText = false, pathFlip = false;
    // Runs cover UTF-8 bytes; empty lists inherit the legacy layer-wide settings above.
    std::vector<TextRun> runs;
    std::vector<ParagraphRun> paragraphs;
    float box[4] = {0, 0, 0, 0};     // where it was last drawn (canvas), for clicking on it (not saved)
    bool active() const { return !str.empty() || !font.empty(); }
    std::string save() const;
    bool load(const std::string& blob);
    std::vector<TextRun> normalizedRuns() const;
    std::vector<ParagraphRun> normalizedParagraphs() const;
    // after PhotoCraft (crates/doc/src/lib.rs TextLayer::sync_summary).
    void syncSummary();
};

// the fonts on this PC (Windows' Fonts folder, the user's own): their files and names, sorted by name
struct FontFile { std::string path, name; };
const std::vector<FontFile>& systemFonts();
std::string defaultFont();

// coverage (0..1) of the text over a w x h canvas; box: where it is (x0, y0, x1, y1), for picking and outlines
bool textCoverage(const Text& t, int w, int h, std::vector<float>& cov, float box[4]);
float textAdvance(const Text& t);     // the first line's advance in pixels, for path placement markers
// after PhotoCraft (crates/text/src/layout/navigate.rs word_boundary): returns a character index.
size_t textWordBoundary(const std::string& text, size_t index, bool forward);
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs smart_quotes).
std::string textSmartQuotes(const std::string& text);
struct TextUnit { float scale; const char* label; };
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/defaults.rs type_unit).
TextUnit textUnit(int units, float dpi);
// after PhotoCraft (crates/ui-egui/src/tools/type_tool.rs note_recent_font/sync_recent_fonts).
void noteRecentFont(std::vector<std::string>& recent, const std::string& font, size_t cap);
void trimRecentFonts(std::vector<std::string>& recent, size_t cap);
