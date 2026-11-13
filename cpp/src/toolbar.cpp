// LayerTack - the tool strip, PhotoCraft's layout (see toolbar.h)
#include "toolbar.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace fs = std::filesystem;

namespace Toolbar {

// the Atelier look
static const ImU32 C_ACCENT = IM_COL32(0x00, 0x7b, 0xb7, 255), C_HOVER = IM_COL32(0x33, 0x33, 0x33, 255),
                   C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255), C_FAINT = IM_COL32(0x6a, 0x6a, 0x6a, 255),
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255);
static const float BX = 32;          // a tool button
static const float MARGIN = 7;       // the strip's side margins

struct Info { const char* label; char key; const char* icon; bool works; };
static const Info TOOLS[COUNT] = {
    {"Move Tool", 'V', "move", true}, {"Artboard Tool", 'V', "artboard", false},
    {"Rectangular Marquee Tool", 'M', "square-dashed", true}, {"Elliptical Marquee Tool", 'M', "ellipse-dashed", true},
    {"Single Row Marquee Tool", 0, "marquee-row", false}, {"Single Column Marquee Tool", 0, "marquee-column", false},
    {"Lasso Tool", 'L', "lasso", true}, {"Polygonal Lasso Tool", 'L', "pentagon", true}, {"Magnetic Lasso Tool", 'L', "lasso-magnetic", false},
    {"Object Selection Tool", 'W', "square-dashed-mouse-pointer", false}, {"Quick Selection Tool", 'W', "quick-selection", false},
    {"Magic Wand Tool", 'W', "wand-sparkles", true},
    {"Crop Tool", 'C', "crop", false}, {"Perspective Crop Tool", 'C', "perspective-crop", false}, {"Slice Tool", 'C', "slice-knife", false},
    {"Slice Select Tool", 'C', "slice-select", false},
    {"Frame Tool", 'K', "frame-placeholder", false},
    {"Eyedropper Tool", 'I', "pipette", true}, {"Color Sampler Tool", 'I', "sampler", false}, {"Ruler Tool", 'I', "ruler", false},
    {"Note Tool", 'I', "message-square", false}, {"Count Tool", 'I', "circle-dot", false},
    {"Spot Healing Brush Tool", 'J', "bandage", false}, {"Remove Tool", 'J', "bandage-sparkle", false}, {"Healing Brush Tool", 'J', "bandage", false},
    {"Patch Tool", 'J', "lasso-select", false}, {"Content-Aware Move Tool", 'J', "arrow-left-right", false}, {"Red Eye Tool", 'J', "eye", false},
    {"Brush Tool", 'B', "brush", true}, {"Pencil Tool", 'B', "pencil", false}, {"Color Replacement Tool", 'B', "brush-replace", false},
    {"Mixer Brush Tool", 'B', "palette", false},
    {"Clone Stamp Tool", 'S', "stamp", false}, {"Pattern Stamp Tool", 'S', "grid-3x3", false},
    {"History Brush Tool", 'Y', "history-brush", false}, {"Art History Brush Tool", 'Y', "sparkles", false},
    {"Eraser Tool", 'E', "eraser", true}, {"Background Eraser Tool", 'E', "eraser-background", false}, {"Magic Eraser Tool", 'E', "eraser-magic", false},
    {"Gradient Tool", 'G', "gradient", false}, {"Paint Bucket Tool", 'G', "paint-bucket", true},
    {"Blur Tool", 0, "droplet", false}, {"Sharpen Tool", 0, "triangle", false}, {"Smudge Tool", 0, "pointer", false},
    {"Dodge Tool", 'O', "lollipop", false}, {"Burn Tool", 'O', "flame", false}, {"Sponge Tool", 'O', "cloud", false},
    {"Pen Tool", 'P', "pen-tool", false}, {"Freeform Pen Tool", 'P', "pen-freeform", false}, {"Curvature Pen Tool", 'P', "pen-curvature", false},
    {"Add Anchor Point Tool", 0, "pen-add-anchor", false}, {"Delete Anchor Point Tool", 0, "pen-delete-anchor", false},
    {"Convert Point Tool", 0, "convert-point", false},
    {"Horizontal Type Tool", 'T', "type", false}, {"Vertical Type Tool", 'T', "type", false}, {"Vertical Type Mask Tool", 'T', "type-mask-vertical", false},
    {"Horizontal Type Mask Tool", 'T', "type-mask", false},
    {"Path Selection Tool", 'A', "mouse-pointer-2", false}, {"Direct Selection Tool", 'A', "direct-select", false},
    {"Rectangle Tool", 'U', "rectangle-horizontal", false}, {"Ellipse Tool", 'U', "circle", false}, {"Triangle Tool", 'U', "triangle", false},
    {"Polygon Tool", 'U', "pentagon", false}, {"Star Tool", 'U', "star", false}, {"Line Tool", 'U', "slash", false},
    {"Custom Shape Tool", 'U', "cloud", false},
    {"Hand Tool", 'H', "hand", true}, {"Rotate View Tool", 'R', "compass", false}, {"Zoom Tool", 'Z', "zoom-in", false},
};

// PhotoCraft's TOOL_SECTIONS: sections (a divider between), slots (one button, a flyout), tools
static const std::vector<std::vector<std::vector<int>>> SECTIONS = {
    {{MOVE, ARTBOARD}},
    {{RECT_MARQUEE, ELLIPSE_MARQUEE, SINGLE_ROW_MARQUEE, SINGLE_COLUMN_MARQUEE}, {LASSO, POLYGON_LASSO, MAGNETIC_LASSO},
     {OBJECT_SELECTION, QUICK_SELECTION, MAGIC_WAND}, {CROP, PERSPECTIVE_CROP, SLICE, SLICE_SELECT}, {FRAME},
     {EYEDROPPER, COLOR_SAMPLER, RULER, NOTE, COUNT_TOOL}},
    {{SPOT_HEALING, REMOVE, HEALING, PATCH, CONTENT_AWARE_MOVE, RED_EYE}, {BRUSH, PENCIL, COLOR_REPLACEMENT, MIXER_BRUSH},
     {CLONE_STAMP, PATTERN_STAMP}, {HISTORY_BRUSH, ART_HISTORY_BRUSH}, {ERASER, BACKGROUND_ERASER, MAGIC_ERASER},
     {GRADIENT, PAINT_BUCKET}, {BLUR, SHARPEN, SMUDGE}, {DODGE, BURN, SPONGE}},
    {{PEN, FREEFORM_PEN, CURVATURE_PEN, ADD_ANCHOR, DELETE_ANCHOR, CONVERT_POINT}, {TYPE_H, TYPE_V, TYPE_V_MASK, TYPE_H_MASK},
     {PATH_SELECTION, DIRECT_SELECTION}, {RECTANGLE, ELLIPSE, TRIANGLE, POLYGON, STAR, LINE, CUSTOM_SHAPE}},
    {{HAND, ROTATE_VIEW}, {ZOOM}},
};

static int cur = BRUSH;
static std::vector<int> recent;          // tools used, most recent first (what a slot shows when it isn't current)
static bool twoColumns = false;
static bool hidden[COUNT] = {};
static std::string prefFile;
static GLuint tex[COUNT] = {};
static GLuint texEllipsis = 0, texChevL = 0, texChevR = 0;

int current() { return cur; }
const char* label(int id) { return id >= 0 && id < COUNT ? TOOLS[id].label : ""; }
char key(int id) { return id >= 0 && id < COUNT ? TOOLS[id].key : 0; }
bool works(int id) { return id >= 0 && id < COUNT && TOOLS[id].works; }
static float drawnH = 900;   // how tall the strip came out last frame
float height() { return drawnH; }
float width() { return twoColumns ? BX * 2 + 2 + MARGIN * 2 : BX + MARGIN * 2; }

void select(int id) {
    if (id < 0 || id >= COUNT) return;
    cur = id;
    recent.erase(std::remove(recent.begin(), recent.end(), id), recent.end());
    recent.insert(recent.begin(), id);
}

static void save() {
    if (prefFile.empty()) return;
    std::ofstream f(fs::u8path(prefFile));
    f << "twoColumns=" << (twoColumns ? 1 : 0) << "\nhidden=";
    bool first = true;
    for (int i = 0; i < COUNT; ++i) if (hidden[i]) { f << (first ? "" : ",") << TOOLS[i].label; first = false; }
    f << "\n";
}

static GLuint loadSvg(const std::string& path, int px) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return 0;
    std::stringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    for (size_t p; (p = s.find("currentColor")) != std::string::npos;) s.replace(p, 12, "#ffffff");   // tinted when drawn
    std::vector<char> buf(s.begin(), s.end()); buf.push_back(0);
    NSVGimage* img = nsvgParse(buf.data(), "px", 96);
    if (!img) return 0;
    std::vector<unsigned char> rgba((size_t)px * px * 4, 0);
    NSVGrasterizer* r = nsvgCreateRasterizer();
    const float scale = px / std::max(img->width, img->height);
    nsvgRasterize(r, img, 0, 0, scale, rgba.data(), px, px, px * 4);
    nsvgDeleteRasterizer(r); nsvgDelete(img);
    GLuint t = 0;
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, px, px, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return t;
}

void init(const std::string& assetsDir, const std::string& prefDir) {
    const std::string dir = assetsDir + "icons/";
    const int px = 48;   // drawn at 18-20 px: sharp on high-DPI screens too
    for (int i = 0; i < COUNT; ++i) tex[i] = loadSvg(dir + TOOLS[i].icon + ".svg", px);
    texEllipsis = loadSvg(dir + "ellipsis.svg", px); texChevL = loadSvg(dir + "chevrons-left.svg", 32); texChevR = loadSvg(dir + "chevrons-right.svg", 32);
    if (prefDir.empty()) return;
    prefFile = (fs::u8path(prefDir) / "toolbar.ini").u8string();
    std::ifstream f(fs::u8path(prefFile));
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("twoColumns=", 0) == 0) twoColumns = line[11] == '1';
        else if (line.rfind("hidden=", 0) == 0) {
            std::stringstream ss(line.substr(7)); std::string name;
            while (std::getline(ss, name, ','))
                for (int i = 0; i < COUNT; ++i) if (name == TOOLS[i].label) hidden[i] = true;
        }
    }
}

void icon(ImDrawList* dl, int id, float cx, float cy, float size, unsigned col) {
    if (id < 0 || id >= COUNT || !tex[id]) return;
    dl->AddImage((ImTextureID)(intptr_t)tex[id], ImVec2(cx - size / 2, cy - size / 2), ImVec2(cx + size / 2, cy + size / 2), ImVec2(0, 0), ImVec2(1, 1), col);
}

// the tool a slot shows: the current one if it's in the slot, else the last used of it, else its first
static int slotTool(const std::vector<int>& slot) {
    if (std::find(slot.begin(), slot.end(), cur) != slot.end()) return cur;
    for (int t : recent) if (std::find(slot.begin(), slot.end(), t) != slot.end()) return t;
    return slot[0];
}
static std::vector<int> visible(const std::vector<int>& slot) {
    std::vector<int> v;
    for (int t : slot) if (!hidden[t]) v.push_back(t);
    return v;
}
static std::string tipFor(int t) {
    std::string s = TOOLS[t].label;
    if (TOOLS[t].key) { s += "  ("; s += TOOLS[t].key; s += ")"; }
    if (!TOOLS[t].works) s += "\nNot in LayerTack yet: it comes as PhotoCraft is ported.";
    return s;
}

static int openSlot = -1;          // the slot whose flyout is open
static ImVec2 openAt;              // where (beside its button)
static bool openByHold = false;    // opened by holding: releasing over a row picks it
static int openedFrame = -1;       // (the click that opened it doesn't close it)
static bool editOpen = false;

static void flyout(const std::vector<int>& slot, int shown) {
    ImGui::SetNextWindowPos(openAt);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4);
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##toolflyout", nullptr, fl);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lw = 0;
    for (int t : slot) lw = std::max(lw, ImGui::CalcTextSize(TOOLS[t].label).x);
    const float fw = std::max(180.0f, lw + 42 + 40);
    bool picked = false;
    for (int t : slot) {
        ImGui::PushID(t);
        const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + fw, a.y + 26);
        const bool clicked = ImGui::InvisibleButton("row", ImVec2(fw, 26));
        const bool over = ImGui::IsMouseHoveringRect(a, b);
        if (over) dl->AddRectFilled(a, b, C_HOVER, 3);
        if (t == shown) dl->AddRectFilled(ImVec2(a.x + 6, a.y + 11), ImVec2(a.x + 10, a.y + 15), C_TEXT);
        icon(dl, t, a.x + 26, a.y + 13, 16, TOOLS[t].works ? C_TEXT : C_DIM);
        dl->AddText(ImVec2(a.x + 42, a.y + 13 - ImGui::GetFontSize() / 2), TOOLS[t].works ? C_TEXT : C_DIM, TOOLS[t].label);
        if (TOOLS[t].key) {
            const char k[2] = {TOOLS[t].key, 0};
            dl->AddText(ImVec2(b.x - 8 - ImGui::CalcTextSize(k).x, a.y + 13 - ImGui::GetFontSize() / 2), C_DIM, k);
        }
        if (over && !TOOLS[t].works) ImGui::SetTooltip("Not in LayerTack yet: it comes as PhotoCraft is ported.");
        if (clicked || (openByHold && over && ImGui::IsMouseReleased(0))) { select(t); picked = true; }
        ImGui::PopID();
    }
    const bool inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (picked) { openSlot = -1; return; }
    if (openByHold && ImGui::IsMouseReleased(0)) openByHold = false;   // let go elsewhere: it stays open, for a click
    if (ImGui::GetFrameCount() == openedFrame) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !inside)) openSlot = -1;
}

static void editToolbar() {
    if (editOpen) { ImGui::OpenPopup("Edit Toolbar"); editOpen = false; }
    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Edit Toolbar", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::TextDisabled("Untick a tool to hide it from the toolbar. Its key still picks it.");
    ImGui::BeginChild("##list", ImVec2(0, -40), true);
    bool dirty = false;
    for (const auto& section : SECTIONS) {
        for (const auto& slot : section) {
            for (int t : slot) {
                ImGui::PushID(t);
                bool on = !hidden[t];
                const ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::Checkbox("##on", &on)) { hidden[t] = !on; dirty = true; }
                ImGui::SameLine();
                icon(ImGui::GetWindowDrawList(), t, ImGui::GetCursorScreenPos().x + 9, p.y + ImGui::GetFrameHeight() / 2, 16, C_TEXT);
                ImGui::Dummy(ImVec2(20, 1)); ImGui::SameLine();
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(TOOLS[t].label);
                ImGui::PopID();
            }
            ImGui::Spacing();
        }
        ImGui::Separator();
    }
    ImGui::EndChild();
    if (ImGui::Button("Restore Defaults", ImVec2(150, 0))) { std::fill(std::begin(hidden), std::end(hidden), false); dirty = true; }
    ImGui::SameLine(ImGui::GetWindowWidth() - 118);
    if (ImGui::Button("Done", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (dirty) save();
    ImGui::EndPopup();
}

void draw() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float startY = ImGui::GetCursorScreenPos().y;
    ImGuiIO& io = ImGui::GetIO();
    const float x0 = ImGui::GetWindowPos().x + MARGIN;
    const float rowW = twoColumns ? BX * 2 + 2 : BX;
    // the header: one column or two
    {
        const ImVec2 a(x0, ImGui::GetCursorScreenPos().y);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##cols", ImVec2(14, 14))) { twoColumns = !twoColumns; save(); }
        const bool hov = ImGui::IsItemHovered();
        const GLuint t = twoColumns ? texChevL : texChevR;
        if (t) dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(a.x + 12.5f, a.y + 12.5f), ImVec2(0, 0), ImVec2(1, 1), hov ? C_TEXT : C_FAINT);
        if (hov) ImGui::SetTooltip("Toolbar: %s", twoColumns ? "one column" : "two columns");
        ImGui::SetCursorScreenPos(ImVec2(x0, a.y + 18));
    }
    const bool tips = openSlot < 0 && !ImGui::IsMouseDown(0);
    int slotIndex = 0;
    bool firstSection = true;
    std::vector<int> openTools; int openShown = -1;
    for (const auto& section : SECTIONS) {
        // the section's visible slots
        std::vector<std::pair<int, std::vector<int>>> slots;
        for (const auto& slot : section) { auto v = visible(slot); if (!v.empty()) slots.push_back({slotIndex, v}); ++slotIndex; }
        if (slots.empty()) continue;
        if (!firstSection) {
            const float y = ImGui::GetCursorScreenPos().y + 3;
            dl->AddLine(ImVec2(x0 + 6, y), ImVec2(x0 + rowW - 6, y), C_LINE);
            ImGui::SetCursorScreenPos(ImVec2(x0, y + 4));
        }
        firstSection = false;
        for (size_t i = 0; i < slots.size(); ++i) {
            const int idx = slots[i].first; const std::vector<int>& slot = slots[i].second;
            const int col = twoColumns ? (int)(i % 2) : 0;
            if (col == 1) ImGui::SameLine(0, 2); else ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y));
            const int shown = slotTool(slot);
            const bool sel = std::find(slot.begin(), slot.end(), cur) != slot.end();
            ImGui::PushID(idx);
            const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + BX, a.y + BX);
            ImGui::InvisibleButton("slot", ImVec2(BX, BX), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool hov = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
            dl->AddRectFilled(a, b, sel ? C_ACCENT : (hov ? C_HOVER : IM_COL32(0, 0, 0, 0)), 4);
            icon(dl, shown, a.x + BX / 2, a.y + BX / 2, 18, sel ? IM_COL32(255, 255, 255, 255) : C_TEXT);
            if (slot.size() > 1) dl->AddTriangleFilled(ImVec2(b.x - 2, b.y - 2), ImVec2(b.x - 6, b.y - 2), ImVec2(b.x - 2, b.y - 6), sel ? IM_COL32(255, 255, 255, 200) : C_FAINT);
            if (hov && tips) ImGui::SetTooltip("%s", tipFor(shown).c_str());
            const bool many = slot.size() > 1;
            // a plain click picks what the slot shows; Alt-click steps to the slot's next tool
            if (ImGui::IsItemDeactivated() && hov && !openByHold && io.MouseReleased[0]) {
                if (openSlot == idx) openSlot = -1;   // clicking the button of its open flyout closes it
                else if (many && io.KeyAlt) {
                    const int from = sel ? cur : shown;
                    const size_t k = std::find(slot.begin(), slot.end(), from) - slot.begin();
                    select(slot[(k + 1) % slot.size()]);
                } else select(shown);
                if (shown == HAND && ImGui::IsMouseDoubleClicked(0)) {}   // (fit on screen: the View menu has it)
            }
            // right-click, a long press or dragging off the button opens the flyout, beside it towards the canvas
            const bool held = active && io.MouseDown[0] && io.MouseDownDuration[0] >= 0.35f;
            const bool dragged = active && io.MouseDown[0] && !hov;
            if (many && (ImGui::IsItemClicked(1) || ((held || dragged) && openSlot != idx))) {
                if (openSlot == idx && ImGui::IsItemClicked(1)) openSlot = -1;
                else { openSlot = idx; openAt = ImVec2(b.x + 6, a.y); openByHold = held || dragged; openedFrame = ImGui::GetFrameCount(); if (held) select(shown); }
            }
            if (openSlot == idx) { openTools = slot; openShown = shown; }
            ImGui::PopID();
        }
    }
    // "...": Edit Toolbar
    ImGui::SetCursorScreenPos(ImVec2(x0, ImGui::GetCursorScreenPos().y + 4));
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##more", ImVec2(BX, BX * 0.75f))) editOpen = true;
        if (ImGui::IsItemHovered()) { dl->AddRectFilled(a, ImVec2(a.x + BX, a.y + BX * 0.75f), C_HOVER, 4); if (tips) ImGui::SetTooltip("Edit Toolbar..."); }
        if (texEllipsis) dl->AddImage((ImTextureID)(intptr_t)texEllipsis, ImVec2(a.x + BX / 2 - 9, a.y + BX * 0.375f - 9), ImVec2(a.x + BX / 2 + 9, a.y + BX * 0.375f + 9), ImVec2(0, 0), ImVec2(1, 1), C_TEXT);
    }
    drawnH = ImGui::GetCursorScreenPos().y - startY + 4;
    if (openSlot >= 0 && !openTools.empty()) flyout(openTools, openShown);
    else if (openSlot >= 0) openSlot = -1;
    editToolbar();
}

bool keys(bool shift, bool ctrl, bool alt) {
    if (ctrl || alt) return false;
    static const ImGuiKey KEYS[] = {ImGuiKey_V, ImGuiKey_M, ImGuiKey_L, ImGuiKey_W, ImGuiKey_C, ImGuiKey_K, ImGuiKey_I, ImGuiKey_J, ImGuiKey_B,
                                    ImGuiKey_S, ImGuiKey_Y, ImGuiKey_E, ImGuiKey_G, ImGuiKey_O, ImGuiKey_P, ImGuiKey_T, ImGuiKey_A, ImGuiKey_U,
                                    ImGuiKey_H, ImGuiKey_R, ImGuiKey_Z};
    for (ImGuiKey k : KEYS) {
        if (!ImGui::IsKeyPressed(k, false)) continue;
        const char letter = (char)('A' + (k - ImGuiKey_A));
        // the group: every tool with this letter, in the toolbar's order
        std::vector<int> group;
        for (const auto& section : SECTIONS) for (const auto& slot : section) for (int t : slot) if (TOOLS[t].key == letter) group.push_back(t);
        if (group.empty()) return false;
        const auto at = std::find(group.begin(), group.end(), cur);
        if (shift && at != group.end()) select(group[((at - group.begin()) + 1) % group.size()]);   // the next one in the group
        else if (at == group.end()) {   // the group's last-used tool, else its first
            int pick = group[0];
            for (int t : recent) if (std::find(group.begin(), group.end(), t) != group.end()) { pick = t; break; }
            select(pick);
        }
        return true;
    }
    return false;
}

}
