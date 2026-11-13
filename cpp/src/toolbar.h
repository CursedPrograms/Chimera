// Chimera - the tool strip on the left, laid out exactly as PhotoCraft's (Photoshop's): the same tools in the same
// slots and sections, each slot showing its last-used tool with a little corner mark; right-click, hold or drag away
// opens its flyout, Alt-click cycles it, the letter keys pick a group's tool and Shift+letter steps through the group.
// The header toggles one or two columns; "..." edits which tools show. Icons: PhotoCraft's (Lucide, ISC; and
// PhotoCraft's own, MIT/Apache-2.0), drawn with nanosvg.
#pragma once
#include <string>

struct ImDrawList;

namespace Toolbar {

enum Id {
    MOVE, ARTBOARD,
    RECT_MARQUEE, ELLIPSE_MARQUEE, SINGLE_ROW_MARQUEE, SINGLE_COLUMN_MARQUEE,
    LASSO, POLYGON_LASSO, MAGNETIC_LASSO,
    OBJECT_SELECTION, QUICK_SELECTION, MAGIC_WAND,
    CROP, PERSPECTIVE_CROP, SLICE, SLICE_SELECT,
    FRAME,
    EYEDROPPER, COLOR_SAMPLER, RULER, NOTE, COUNT_TOOL,
    SPOT_HEALING, REMOVE, HEALING, PATCH, CONTENT_AWARE_MOVE, RED_EYE,
    BRUSH, PENCIL, COLOR_REPLACEMENT, MIXER_BRUSH,
    CLONE_STAMP, PATTERN_STAMP,
    HISTORY_BRUSH, ART_HISTORY_BRUSH,
    ERASER, BACKGROUND_ERASER, MAGIC_ERASER,
    GRADIENT, PAINT_BUCKET,
    BLUR, SHARPEN, SMUDGE,
    DODGE, BURN, SPONGE,
    PEN, FREEFORM_PEN, CURVATURE_PEN, ADD_ANCHOR, DELETE_ANCHOR, CONVERT_POINT,
    TYPE_H, TYPE_V, TYPE_V_MASK, TYPE_H_MASK,
    PATH_SELECTION, DIRECT_SELECTION,
    RECTANGLE, ELLIPSE, TRIANGLE, POLYGON, STAR, LINE, CUSTOM_SHAPE,
    HAND, ROTATE_VIEW, ZOOM,
    COUNT
};

void init(const std::string& assetsDir, const std::string& prefDir);   // the icons, and which tools are hidden
int current();                         // the tool picked (an Id)
void select(int id);                   // pick a tool (also what the slot shows from now on)
const char* label(int id);             // "Brush Tool"
char key(int id);                      // its letter ('B'), 0 for none
bool works(int id);                    // Chimera does it already; the rest come as PhotoCraft is ported
// the strip, drawn into the current window (its width: width()); tips: false while a dialog is up
void draw();
float width();                         // one column or two, as the header chevron has it
float height();                        // how tall the tools are (the strip scrolls when the window is shorter)
// V, M, L... pick the group's tool; Shift+letter steps through the group. Returns whether a key was used.
bool keys(bool shift, bool ctrl, bool alt);
// a tool's icon at c, size px across, in col (for the options bar)
void icon(ImDrawList* dl, int id, float cx, float cy, float size, unsigned col);

}
