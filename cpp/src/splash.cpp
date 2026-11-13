// LayerTack - the splash: the logo's three layers drawn large, the name, the version, the credits and what's loading.
// Solid colours only (Changeling greys + Cursed pink), drawn with ImGui's draw list so it's sharp at any size.
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>

#ifndef LT_VERSION
#define LT_VERSION "1.0.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;

void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   PINK = IM_COL32(0x00, 0x7b, 0xb7, 255), GREY = IM_COL32(0x9e, 0x9e, 0x9e, 255),
                   DARK = IM_COL32(0x3a, 0x3a, 0x3a, 255), GHOST = IM_COL32(0xd6, 0xd6, 0xd6, 150);

// a layer as the logo draws it: a parallelogram leaning right, centre c, w wide, h high
static void layerPoints(ImVec2 c, float w, float h, float lean, ImVec2 out[4]) {
    out[0] = ImVec2(c.x - w / 2 + lean / 2, c.y - h / 2);
    out[1] = ImVec2(c.x + w / 2 + lean / 2, c.y - h / 2);
    out[2] = ImVec2(c.x + w / 2 - lean / 2, c.y + h / 2);
    out[3] = ImVec2(c.x - w / 2 - lean / 2, c.y + h / 2);
}

// marching ants, stopped: the outline of a layer being moved
static void dashed(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy), dash = 7, gap = 5;
    for (float t = 0; t < len; t += dash + gap) {
        const float e = t + dash < len ? t + dash : len;
        dl->AddLine(ImVec2(a.x + dx * t / len, a.y + dy * t / len), ImVec2(a.x + dx * e / len, a.y + dy * e / len), col, 1.0f);
    }
}

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) {
    if (!f) f = ImGui::GetFont();
    dl->AddText(f, size, p, col, s);
}

static float textWidth(ImFont* f, float size, const char* s) {
    if (!f) f = ImGui::GetFont();
    return f->CalcTextSizeA(size, FLT_MAX, 0, s).x;
}

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);

    // the art: three layers, bottom first so the pink one sits on top, bleeding off the right edge
    const float lw = 280, lh = 90, lean = 80, cx = x + 500;
    const struct { float cy; ImU32 col; } layers[] = {{y + 255, DARK}, {y + 180, GREY}, {y + 105, PINK}};
    ImVec2 pts[4];
    for (const auto& l : layers) {
        layerPoints(ImVec2(cx, l.cy), lw, lh, lean, pts);
        dl->AddConvexPolyFilled(pts, 4, l.col);
    }
    // ...and the pink layer's outline lifted up and to the right, mid-move
    layerPoints(ImVec2(cx + 18, y + 105 - 24), lw, lh, lean, pts);
    for (int i = 0; i < 4; ++i) dashed(dl, pts[i], pts[(i + 1) % 4], GHOST);

    // the name: "Layer" in the text colour, "Tack" in pink
    const float ts = 50, tx = x + 40, ty = y + 92;
    text(dl, titleFont, ts, ImVec2(tx, ty), TEXT, "Layer");
    text(dl, titleFont, ts, ImVec2(tx + textWidth(titleFont, ts, "Layer"), ty), PINK, "Tack");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 62), DIM, "Version " LT_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 84), DIM, "An image editor by Cursed Entertainment");

    // the credits, small, like the names on a splash
    const float cs = 13, cy = y + 306;
    text(dl, smallFont, cs, ImVec2(tx, cy), DIM, "Blend maths ported from PhotoCraft (MIT / Apache-2.0). Select Subject uses U\xc2\xb2-Net.");
    text(dl, smallFont, cs, ImVec2(tx, cy + 17), DIM, "Built with Dear ImGui, SDL2, stb and ONNX Runtime.");
    text(dl, smallFont, cs, ImVec2(tx, cy + 34), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");

    // what's loading, and how far along
    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), PINK);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
}

}  // namespace Splash
