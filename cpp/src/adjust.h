// Chimera - adjustments as layers: the parameters of a Photoshop adjustment, kept live on a layer, applied to what's
// below it when the picture is composited. The maths is ported from PhotoCraft (github.com/storytold/photocraft,
// crates/compose/src/adjust*.rs and crates/doc/src/adjust.rs, MIT OR Apache-2.0), fitted to Photoshop's own output.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum AdjKind {
    ADJ_NONE,
    ADJ_BRIGHTNESS_CONTRAST, ADJ_LEVELS, ADJ_CURVES, ADJ_EXPOSURE, ADJ_VIBRANCE, ADJ_HUE_SATURATION,
    ADJ_COLOR_BALANCE, ADJ_BLACK_WHITE, ADJ_PHOTO_FILTER, ADJ_CHANNEL_MIXER, ADJ_INVERT, ADJ_POSTERIZE,
    ADJ_THRESHOLD, ADJ_GRADIENT_MAP, ADJ_SELECTIVE_COLOR,
    ADJ_COUNT
};
const char* adjName(int kind);

struct LevelsCh { float inBlack = 0, inWhite = 1, gamma = 1, outBlack = 0, outWhite = 1; };
struct HueRange { float hue = 0, sat = 0, light = 0; float bounds[4] = {-45, -15, 15, 45}; };

struct Adjustment {
    int kind = ADJ_NONE;
    // Brightness/Contrast: v[0] brightness -150..150, v[1] contrast -50..100 (legacy: -100..100), flag = legacy
    // Exposure: v[0] exposure -20..20, v[1] offset -0.5..0.5, v[2] gamma 0.01..9.99
    // Vibrance: v[0] vibrance -100..100, v[1] saturation -100..100
    // Hue/Saturation: v[0] hue -180..180, v[1] saturation -100..100, v[2] lightness -100..100, flag = colorize
    // Photo Filter: v[0] density 0..1, flag = preserve luminosity, color[]
    // Posterize: v[0] levels 2..255;  Threshold: v[0] level 0..1;  Gradient Map: flag = reverse, flag2 = dither
    // Color Balance: flag = preserve luminosity;  Channel Mixer: flag = monochrome;  Selective Color: flag = relative
    // Black & White: flag = tint
    float v[4] = {0, 0, 0, 0};
    bool flag = false, flag2 = false;
    LevelsCh levels[4];                          // master, red, green, blue
    std::vector<float> curve[4];                 // master, red, green, blue: x, y pairs in 0..1 (empty = straight)
    HueRange ranges[6];                          // reds, yellows, greens, cyans, blues, magentas
    float balance[3][3] = {};                    // shadows, midtones, highlights x (cyan-red, magenta-green, yellow-blue)
    float bw[6] = {40, 60, 40, 60, 20, 80};      // reds, yellows, greens, cyans, blues, magentas (Photoshop's defaults)
    float tint[3] = {0.886f, 0.776f, 0.588f};    // sepia
    float color[3] = {0.925f, 0.541f, 0.0f};     // Photo Filter: Warming (85)
    float mixer[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};   // rows: out R, G, B = in R, G, B, constant
    std::vector<std::array<float, 4>> stops;     // Gradient Map: position, r, g, b (0..1)
    float selective[9][4] = {};                  // reds..magentas, whites, neutrals, blacks x cyan, magenta, yellow, black %

    static Adjustment make(int kind);            // with Photoshop's starting values
    std::string save() const;                    // a byte blob for .chimera projects
    bool load(const std::string& blob);
};

// the adjustment's look-up tables etc., built once per composite
struct AdjPrepared;
AdjPrepared* adjPrepare(const Adjustment& a);
void adjFree(AdjPrepared* p);
// one pixel, straight RGB 0..1, in place; x, y: canvas place (Gradient Map's dither)
void adjApply(const Adjustment& a, const AdjPrepared* p, float rgb[3], int x, int y);
