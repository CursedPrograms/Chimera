// LayerTack - the document: layers (each with its own size, position and an optional mask), a selection and
// undo. Pixels are 8-bit RGBA, straight alpha; masks and the selection are 8-bit (255 = shown / selected).
#pragma once
#include <cstdint>
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

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;                     // RGBA
    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

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
    uint8_t maskAt(int lx, int ly) const { return hasMask && maskOn ? mask[(size_t)ly * img.w + lx] : 255; }
};

struct Doc {
    int w = 0, h = 0;
    std::vector<Layer> layers;                   // bottom first
    int active = 0;
    bool editMask = false;                       // painting / filters go to the active layer's mask
    std::vector<uint8_t> sel;                    // canvas-sized; empty() = nothing selected (everything is fair game)
    std::string path;                            // where it was opened from / saved to
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
void deleteLayer(Doc& d);
void duplicateLayer(Doc& d);
void moveLayer(Doc& d, int dir);                 // +1 up, -1 down
void mergeDown(Doc& d);
void mergeVisible(Doc& d);
void flatten(Doc& d);
void expandToCanvas(Doc& d, Layer& l);           // grow a layer to cover the whole canvas (keeps its pixels)

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
bool openDoc(const std::string& path, Doc& d, std::string& err);       // a picture or a .ltk project
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
