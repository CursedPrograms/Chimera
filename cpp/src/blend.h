// Chimera - blend modes. The maths is ported from PhotoCraft (github.com/storytold/photocraft,
// crates/color/src/blend.rs and crates/compose/src/psblend.rs, MIT OR Apache-2.0): Photoshop's formulas,
// including its exact Color Burn / Dodge, Vivid Light and Hard Mix edge cases.
#pragma once

enum BlendMode {
    BM_Normal, BM_Dissolve,
    BM_Darken, BM_Multiply, BM_ColorBurn, BM_LinearBurn, BM_DarkerColor,
    BM_Lighten, BM_Screen, BM_ColorDodge, BM_LinearDodge, BM_LighterColor,
    BM_Overlay, BM_SoftLight, BM_HardLight, BM_VividLight, BM_LinearLight, BM_PinLight, BM_HardMix,
    BM_Difference, BM_Exclusion, BM_Subtract, BM_Divide,
    BM_Hue, BM_Saturation, BM_Color, BM_Luminosity,
    BM_Count
};

const char* blendName(int mode);
bool blendGroupStart(int mode);                  // a separator goes before it in the menu, as in Photoshop

// cb: backdrop RGB, cs: source RGB (0..1) -> the blended RGB
void blendRGB(int mode, const float cb[3], const float cs[3], float out[3]);

// straight-alpha source over straight-alpha backdrop (the W3C / PDF general formula), in place on b.
// opacity already folded into s[3]. x, y: for Dissolve's pattern.
void compositePixel(int mode, float b[4], const float s[4], int x, int y);
