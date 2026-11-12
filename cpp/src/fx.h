// LayerTack - more filters and selection tools, ported from PhotoCraft (github.com/storytold/photocraft,
// crates/algo/src/distort.rs, stylize.rs, sharpen.rs, MIT OR Apache-2.0): Sharpen, Distort, Stylize, and
// Select > Modify. Filters work on a Work (see ops.h); the distortions are centred on `b`, the selection's box in
// the Work's own coordinates (or the whole of it).
#pragma once
#include "ops.h"

struct FxBox { int x0, y0, x1, y1; };
FxBox fxBounds(const Doc& d, const Work& w);     // the selection's box on the target, or all of it

// ---- Sharpen
void fxUnsharp(Work& w, float amount, float radius, float threshold);   // amount %, radius px, threshold levels 0..255
void fxHighPass(Work& w, float radius);

// ---- Distort
void fxTwirl(Work& w, FxBox b, float angle);                 // degrees, -999..999
void fxPinch(Work& w, FxBox b, float amount);                // -100..100
void fxSpherize(Work& w, FxBox b, float amount, int mode);   // -100..100; 0 normal, 1 horizontal only, 2 vertical only
void fxWave(Work& w, FxBox b, int generators, float wlMin, float wlMax, float ampMin, float ampMax, int type, unsigned seed);   // type 0 sine, 1 triangle, 2 square
void fxRipple(Work& w, FxBox b, float amount, int size);     // -999..999 %; size 0 small, 1 medium, 2 large
void fxPolar(Work& w, FxBox b, bool toPolar);                // rectangular to polar, or back

// ---- Stylize
void fxMosaic(Work& w, FxBox b, float cell);
void fxEmboss(Work& w, float angle, float height, float amount);    // degrees, 1..10 px, 1..500 %
void fxFindEdges(Work& w);
void fxSolarize(Work& w);

// ---- Select > Modify (canvas selection; Smooth and Border also anti-alias)
void selExpand(Doc& d, float px);
void selContract(Doc& d, float px);
void selSmooth(Doc& d, int radius);
void selBorder(Doc& d, float width);
