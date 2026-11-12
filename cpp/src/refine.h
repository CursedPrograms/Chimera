// LayerTack - Select and Mask: edges that follow the picture (hair, fur, soft focus), then smoothed, feathered,
// sharpened and shifted, and colours cleaned of the old background. Ported from PhotoCraft
// (github.com/storytold/photocraft, crates/algo/src/matting.rs, MIT OR Apache-2.0); the soft edges are He, Sun and
// Tang's guided filter ("Guided Image Filtering", ECCV 2010).
#pragma once
#include <cstdint>
#include <vector>

struct RefineParams {
    float radius = 0;          // the band round the edge the picture decides, pixels (0: none)
    bool smart = false;        // the band follows how soft the picture's edge is there
    float smooth = 0;          // 0..100
    float feather = 0;         // pixels
    float contrast = 0;        // 0..100
    float shift = 0;           // -100..100: soft edges pulled in or pushed out
};

// the edge detection for one radius, kept while only smooth / feather / contrast / shift move (clear it when the
// mask or the picture change)
struct RefineCache {
    float radius = -1; bool smart = false;
    std::vector<float> edge;
};

// m: canvas-sized coverage 0..1; rgb: the canvas picture, 3 floats a pixel 0..1. Returns the refined coverage.
std::vector<float> refineMask(const std::vector<float>& rgb, const std::vector<float>& m, int w, int h, const RefineParams& p, RefineCache* cache = nullptr);

// fringe pixels (soft alpha) move towards the colour of nearby fully covered ones, by amount 0..100.
// rgba: 8-bit straight, canvas-sized.
void decontaminate(uint8_t* rgba, const std::vector<float>& alpha, int w, int h, float radius, float amount);
