// Chimera - what you do to pixels: adjustments, filters, fills, selections, resizing and the brush.
// Everything works on the "target": the active layer's pixels, or its mask when you're editing the mask
// (the mask is treated as a grey picture, so you can blur, fill, curve or paint it like any other).
// Inside a selection only the selected part changes (soft selections blend).
#pragma once
#include "doc.h"
#include <cstdint>
#include <vector>

// the target as floats (0..1 RGBA; a mask comes in as grey with alpha 1)
struct Work {
    int w = 0, h = 0, ox = 0, oy = 0;            // size, and where it sits on the canvas
    bool mask = false;
    std::vector<float> px;
    float* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
};
Work getWork(const Doc& d);
// write `result` back into the target, blended with `orig` by the selection
void putWork(Doc& d, const Work& orig, const Work& result);

// ---- adjustments (in place)
void adjBrightnessContrast(Work& w, float brightness, float contrast);   // -150..150, -50..100 (Photoshop's ranges)
void adjHueSat(Work& w, float hue, float sat, float light, bool colorize); // -180..180, -100..100, -100..100
void adjVibrance(Work& w, float vibrance);       // -100..100: dull colours move most, strong ones hardly at all
// Black & White: the six colour sliders in percent (reds, yellows, greens, cyans, blues, magentas)
void adjBlackWhite(Work& w, const float weights[6], bool tint, const float tintRGB[3]);
void adjCurves(Work& w, const uint8_t lut[4][256]);     // lut[0] = RGB (all), then R, G, B
void adjLevels(Work& w, float inBlack, float inWhite, float gamma, float outBlack, float outWhite);
void adjInvert(Work& w);
void adjDesaturate(Work& w);
void adjThreshold(Work& w, float level);

// curves: points (x, y in 0..1, sorted by x) -> a smooth 256-entry table (monotone cubic, like Photoshop's)
void curveLUT(const std::vector<float>& xs, const std::vector<float>& ys, uint8_t out[256]);
void histogram(const Doc& d, int channel, unsigned out[256]);            // of the target, 0 = luminosity

// ---- filters
void filterBoxBlur(Work& w, int radius);
void filterGaussianBlur(Work& w, float radius);
void filterAverage(Doc& d, Work& w);             // Photoshop's Average: everything becomes the mean colour
void filterAddNoise(Work& w, float amount, bool gaussian, bool mono, unsigned seed);   // amount 0..400 %

// ---- fills (with undo pushed by the caller)
void fillSelection(Doc& d, const uint8_t rgba[4], float opacity, int blend);
// paint bucket: from canvas point (x, y), colours within tolerance (0..255); sampleAll = look at the composite
void bucketFill(Doc& d, int x, int y, const uint8_t rgba[4], int tolerance, bool contiguous, bool sampleAll, float opacity);

// ---- selections: shapes become a canvas-sized mask, then combine with the selection
enum SelMode { SEL_NEW, SEL_ADD, SEL_SUB, SEL_AND };
std::vector<uint8_t> shapeRect(const Doc& d, int x0, int y0, int x1, int y1);
std::vector<uint8_t> shapeEllipse(const Doc& d, int x0, int y0, int x1, int y1);
std::vector<uint8_t> shapePolygon(const Doc& d, const std::vector<float>& pts);    // x, y pairs, anti-aliased
std::vector<uint8_t> shapeWand(const Doc& d, int x, int y, int tolerance, bool contiguous, bool sampleAll);
void combineSel(Doc& d, std::vector<uint8_t> shape, int mode);
void featherSel(Doc& d, float radius);
void invertSel(Doc& d);
void growSel(Doc& d, int px);                    // + grows, - shrinks
// the outline of the selection (for the marching ants): pairs of canvas points (x0, y0, x1, y1)
std::vector<int> selOutline(const Doc& d);

// ---- size
void resizeImage(Doc& d, int w, int h);          // every layer and mask resampled (bicubic)
void resizeCanvas(Doc& d, int w, int h, int anchorX, int anchorY);   // anchor 0 left/top, 1 centre, 2 right/bottom
void flipLayer(Doc& d, bool horizontal);
// Free Transform: scale (1 = as it is) and rotate (degrees) the active layer and its mask round its centre
void transformLayer(Doc& d, float scaleX, float scaleY, float degrees);

// ---- the brush: a stroke paints into a coverage buffer, so one stroke never builds past its opacity
struct Stroke {
    bool on = false;
    Work orig;
    std::vector<float> cover;                    // per target pixel, 0..1
    float lastX = 0, lastY = 0, rest = 0;
};
void strokeBegin(Doc& d, Stroke& s);
// a segment of the stroke to canvas point (x, y); returns the canvas rect it changed
Rect strokeTo(Doc& d, Stroke& s, float x, float y, float size, float hardness, float opacity, const uint8_t rgba[3], bool erase, bool first);
void strokeEnd(Stroke& s);
// the Pencil: the brush with a hard, aliased edge (every pixel is all or nothing)
Rect pencilTo(Doc& d, Stroke& s, float x, float y, float size, float opacity, const uint8_t rgb[3], bool erase, bool first);

// ---- the retouching brushes (Photoshop's, as PhotoCraft has them): each dab changes the pixels under it, in place
enum Retouch { RT_BLUR, RT_SHARPEN, RT_SMUDGE, RT_DODGE, RT_BURN, RT_SPONGE_SAT, RT_SPONGE_DESAT, RT_CLONE, RT_BG_ERASE };
struct RetouchOpts {
    int mode = RT_BLUR;
    float strength = 0.5f;                       // Blur/Sharpen/Smudge strength, Dodge/Burn exposure, Sponge flow: 0..1
    int range = 1;                               // Dodge/Burn: 0 shadows, 1 midtones, 2 highlights
    float srcDX = 0, srcDY = 0;                  // Clone Stamp: where it copies from, as an offset from the brush
    float tolerance = 0.25f;                     // Background Eraser: how far from the sampled colour still goes
};
Rect retouchTo(Doc& d, Stroke& s, float x, float y, float size, float hardness, const RetouchOpts& o, bool first);

// ---- the Gradient tool: from (x0, y0) to (x1, y1) in the foreground..background colours, into the selection (or the
// whole layer); style: GradStyle
void gradientFill(Doc& d, float x0, float y0, float x1, float y1, const uint8_t from[3], const uint8_t to[3], int style, bool reverse, float opacity);
// the Crop tool: the canvas becomes r (layers keep their pixels, moved; what's outside is cut when `deleteCropped`)
void cropCanvas(Doc& d, Rect r, bool deleteCropped);
// the Magic Eraser: what the Magic Wand would pick at (x, y), made transparent on the active layer
void magicErase(Doc& d, int x, int y, int tolerance, bool contiguous, bool sampleAll, float opacity);
