// Chimera - the document: layers (each with its own size, position and an optional mask), a selection and
// undo. Pixels are 8-bit RGBA, straight alpha; masks and the selection are 8-bit (255 = shown / selected).
#pragma once
#include "shape.h"
#include "text.h"
#include "adjust.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// fast float -> byte (std::lround is slow with MSVC, and every pixel goes through this)
inline float fround(float v) { return (float)(int)(v + (v >= 0 ? 0.5f : -0.5f)); }
inline uint8_t to8(float v) { v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); return (uint8_t)(int)(v * 255.0f + 0.5f); }

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;           // x1, y1 exclusive
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    Rect clip(int w, int h) const;
    Rect unite(const Rect& o) const;
};

// A user-created web slice in document pixel coordinates.
struct Slice {
    int id = 0;
    Rect rect;
    std::string name;
};

// An artboard is a named canvas region that the Artboard tool can select and resize.
struct Artboard { Rect rect; std::string name; int background = 0; };
struct LayerGroup { int id = 0; std::string name = "Group"; float opacity = 1.0f; int blend = 0; bool visible = true, expanded = false, hasMask = false, maskOn = true; std::vector<uint8_t> mask; };

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;                     // RGBA
    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

// a fill layer: Solid Colour or Gradient (PhotoCraft's fill layers, crates/doc/src/effects.rs's Gradient geometry).
// It has no pixels of its own to paint: they're drawn from these settings (renderFill) over the whole canvas, and its
// mask says where it shows
enum FillKind { FILL_NONE, FILL_SOLID, FILL_GRADIENT };
enum GradStyle { GRAD_LINEAR, GRAD_RADIAL, GRAD_ANGLE, GRAD_REFLECTED, GRAD_DIAMOND, GRAD_COUNT };
struct Fill {
    int kind = FILL_NONE;
    float colour[3] = {0.0f, 0.482f, 0.718f};    // solid; the gradient's start
    float colour2[3] = {1, 1, 1};                // the gradient's end
    int style = GRAD_LINEAR;
    float angle = 90;                            // degrees, counter-clockwise from 3 o'clock
    float scale = 1;                             // 0.1..10 (1: the canvas)
    bool reverse = false;
    std::string save() const;
    bool load(const std::string& blob);
};
const char* gradStyleName(int s);

struct Layer {
    std::string name = "Layer";
    Image img;
    int x = 0, y = 0;                            // where its top-left sits on the canvas
    std::vector<uint8_t> mask;                   // same size as img when hasMask; 255 reveals, 0 hides
    bool hasMask = false, maskOn = true;
    bool visible = true;
    float opacity = 1.0f;
    int blend = 0;                               // a BlendMode
    int label = 0;                               // its colour tag, PhotoCraft's set (0 = none; see labelName)
    Adjustment adj;                              // an adjustment layer: no pixels of its own, it changes what's below
    bool isAdjust() const { return adj.kind != ADJ_NONE; }
    bool clip = false;                           // a clipping mask: it only shows where the layer below it (its base) does
    bool frame = false;                           // a shape layer used as an image placeholder and clip base
    int groupId = 0;                              // membership in a top-level LayerGroup, zero means ungrouped
    Fill fill;                                   // a fill layer (Solid Colour, Gradient): its pixels come from these
    bool isFill() const { return fill.kind != FILL_NONE; }
    Shape shape;                                 // a shape layer (a fill layer only inside its shape, and its stroke)
    bool isShape() const { return shape.kind != SHAPE_NONE; }
    Text text; bool hasText = false;             // a text layer (a fill layer only where its letters are)
    bool isText() const { return hasText; }
    uint8_t maskAt(int lx, int ly) const { return hasMask && maskOn ? mask[(size_t)ly * img.w + lx] : 255; }
};

struct Doc {
    int w = 0, h = 0;
    std::vector<Layer> layers;                   // bottom first
    int active = 0;
    bool editMask = false;                       // painting / filters go to the active layer's mask
    std::vector<uint8_t> sel;                    // canvas-sized; empty() = nothing selected (everything is fair game)
    std::vector<Slice> slices;                   // user slices used by the Slice tools and image export
    std::vector<Artboard> artboards;             // named design regions, independent of the pixel layers
    std::vector<LayerGroup> groups;               // groups refer to flat contiguous ranges of child layers
    std::string path;                            // where it was opened from / saved to
    // the marks on it (PhotoCraft's Color Sampler, Count, Note and Ruler tools): not part of the picture
    struct Mark { float x = 0, y = 0; std::string text; };
    std::vector<Mark> samplers, counts, notes;
    float ruler[4] = {0, 0, 0, 0}; bool hasRuler = false;
    BezierPath workPath;                         // the Pen tools' path (Photoshop's Work Path)
    // the History Brush's source: the layers as they were when it opened (or Set Source); shared, so undo's copies
    // of the document don't copy it
    std::shared_ptr<const std::vector<Layer>> historyState;
    bool dirty = false;

    Layer& cur() { return layers[active]; }
    bool hasSel() const { return !sel.empty(); }
    uint8_t selAt(int x, int y) const { return sel.empty() ? 255 : sel[(size_t)y * w + x]; }
    Rect selBounds() const;
};

// ---- building and changing documents
Doc newDoc(int w, int h, bool white);
Layer emptyLayer(const Doc& d, const std::string& name);
void addLayer(Doc& d, Layer l);                  // above the active one, and makes it active
void addFillLayer(Doc& d, const Fill& f);        // above the active one, masked to the selection when there is one
void renderFill(const Doc& d, Layer& l);         // a fill layer's pixels from its settings (canvas-sized); shapes too
void addShapeLayer(Doc& d, const Shape& s, const Fill& f);   // above the active one, named "Rectangle 1"...
void addTextLayer(Doc& d, const Text& t, const Fill& f);     // above the active one, named by its words
int clipBase(const Doc& d, int i);               // the layer a clipped layer i is clipped to (i itself when it isn't)
void deleteLayer(Doc& d);
void duplicateLayer(Doc& d);
void moveLayer(Doc& d, int dir);                 // +1 up, -1 down
bool groupLayers(Doc& d, const std::vector<int>& layers);
bool ungroupActiveLayer(Doc& d);
void mergeDown(Doc& d);
void mergeVisible(Doc& d);
void flatten(Doc& d);
void expandToCanvas(Doc& d, Layer& l);           // grow a layer to cover the whole canvas (keeps its pixels)
void addAdjustmentLayer(Doc& d, int kind);       // above the active one: canvas-sized, a white mask (from the selection if any)

// ---- masks: the heart of it
void addMask(Doc& d, int how);                   // 0 reveal all, 1 hide all, 2 from the selection
void deleteMask(Doc& d);
void applyMask(Doc& d);                          // bake it into the pixels' alpha
void invertMask(Doc& d);
void selectionFromMask(Doc& d);

// ---- compositing
// Everything visible, blended bottom to top, into out (canvas-sized), only inside r. showMask: the active
// layer's mask shown in grey instead.
void composite(const Doc& d, Image& out, Rect r, bool showMask);

// ---- files
bool loadImage(const std::string& path, Image& out, std::string& err);
bool openDoc(const std::string& path, Doc& d, std::string& err);       // a picture or a .chimera project
bool saveImage(const Doc& d, const std::string& path, std::string& err); // .png or .jpg: the flattened picture
bool saveProject(const Doc& d, const std::string& path, std::string& err);

// ---- undo: whole-document snapshots
struct History {
    std::vector<Doc> undo, redo;
    std::vector<std::string> undoNames, redoNames;
    void push(const Doc& d, const std::string& what);
    bool canUndo() const { return !undo.empty(); }
    bool canRedo() const { return !redo.empty(); }
    void doUndo(Doc& d);
    void doRedo(Doc& d);
};
