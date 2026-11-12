// LayerTack - the window: menus, the tool options bar, the tools, the canvas, colours and layers, the dialogs.
// The usual image editor layout, in the Changeling greys with the Cursed pink.
#include "app.h"
#include "blend.h"
#include "refine.h"
#include "bathos.h"
#include "mimicplug.h"
#include "changeling.h"
#include "segment.h"
#include "splash.h"
#include "util.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace App {

// ---------- colours (the Changeling look: greys, the Cursed pink; solid, no gradients) ----------
static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x24, 0x24, 0x24, 255),
                   C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255), C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255),
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_PINK = IM_COL32(0xe0, 0x40, 0x8a, 255),
                   C_DEEP = IM_COL32(0xc2, 0x18, 0x6b, 255), C_WELL = IM_COL32(0x14, 0x14, 0x14, 255);

static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// ---------- layer colour tags: PhotoCraft's set and colours (Photoshop's labels), in its menu order ----------
// swatch: the menu's square; eye: the darker shade that fills the layer row's eye column
struct LabelColour { const char* name; ImU32 swatch, eye; };
static const LabelColour LABELS[] = {
    {"No Colour", 0, 0},
    {"Red", IM_COL32(0xFC, 0x5D, 0x5B, 255), IM_COL32(0x9F, 0x2F, 0x30, 255)},
    {"Orange", IM_COL32(0xF7, 0x97, 0x44, 255), IM_COL32(0x93, 0x4F, 0x0C, 255)},
    {"Yellow", IM_COL32(0xDC, 0xD6, 0x4B, 255), IM_COL32(0x99, 0x78, 0x0C, 255)},
    {"Green", IM_COL32(0x85, 0xDC, 0x6A, 255), IM_COL32(0x4E, 0x71, 0x2E, 255)},
    {"Seafoam", IM_COL32(0x1C, 0x84, 0x88, 255), IM_COL32(0x0B, 0x54, 0x4F, 255)},
    {"Blue", IM_COL32(0x78, 0xAD, 0xF6, 255), IM_COL32(0x41, 0x5B, 0x87, 255)},
    {"Indigo", IM_COL32(0x54, 0x4B, 0xE7, 255), IM_COL32(0x36, 0x34, 0x8E, 255)},
    {"Magenta", IM_COL32(0xCC, 0x1A, 0x7E, 255), IM_COL32(0x98, 0x1B, 0x51, 255)},
    {"Fuchsia", IM_COL32(0xB0, 0x14, 0xC0, 255), IM_COL32(0x71, 0x0F, 0x74, 255)},
    {"Violet", IM_COL32(0x91, 0x76, 0xD5, 255), IM_COL32(0x5D, 0x3F, 0x8E, 255)},
    {"Gray", IM_COL32(0x9C, 0x9C, 0x9C, 255), IM_COL32(0x57, 0x57, 0x57, 255)},
};
static const int LABEL_COUNT = (int)(sizeof LABELS / sizeof LABELS[0]);

static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.PopupRounding = 2; s.ScrollbarRounding = 2;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(8, 8); s.FramePadding = ImVec2(7, 4); s.ItemSpacing = ImVec2(6, 5);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT); c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL); c[ImGuiCol_ChildBg] = col4(C_PANEL); c[ImGuiCol_PopupBg] = col4(IM_COL32(0x21, 0x21, 0x21, 255));
    c[ImGuiCol_Border] = col4(C_LINE); c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_FrameBg] = col4(C_WELL); c[ImGuiCol_FrameBgHovered] = col4(IM_COL32(0x2e, 0x2e, 0x2e, 255)); c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_TitleBg] = col4(C_BG); c[ImGuiCol_TitleBgActive] = col4(C_BG); c[ImGuiCol_MenuBarBg] = col4(C_BG);
    c[ImGuiCol_ScrollbarBg] = col4(C_PANEL); c[ImGuiCol_ScrollbarGrab] = col4(C_LINE);
    c[ImGuiCol_CheckMark] = col4(C_PINK); c[ImGuiCol_SliderGrab] = col4(C_PINK); c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_ButtonHovered] = col4(C_DEEP); c[ImGuiCol_ButtonActive] = col4(C_PINK);
    c[ImGuiCol_Header] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_HeaderHovered] = col4(C_DEEP); c[ImGuiCol_HeaderActive] = col4(C_PINK);
    c[ImGuiCol_Tab] = col4(C_BG); c[ImGuiCol_TabActive] = col4(C_PANEL); c[ImGuiCol_TabHovered] = col4(C_DEEP);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
}

// ---------- state ----------
static SDL_Window* window = nullptr;
static std::string prefs;
static Doc doc;
static History hist;
// No picture yet: LayerTack starts empty and waits for one to be dropped (or File > New / Open). The first picture
// sets the canvas size. doc still holds a blank document underneath, so nothing has to cope with having none.
static bool noDoc = true;
static Image view;                               // the composite on screen
static GLuint viewTex = 0, checkerTex = 0;
static int texW = 0, texH = 0;
static Rect dirty{0, 0, 0, 0};
static int docVersion = 0, selVersion = 0;
static bool showMask = false;                    // Alt-click on a mask: see it in grey
static bool quit = false, askQuit = false;
static std::string toast; static double toastUntil = 0;

enum Tool { T_MOVE, T_RECT, T_ELLIPSE, T_LASSO, T_POLY, T_WAND, T_BRUSH, T_ERASER, T_BUCKET, T_DROPPER, T_HAND, T_COUNT };
static int tool = T_BRUSH;
static uint8_t fg[4] = {224, 64, 138, 255}, bgc[4] = {255, 255, 255, 255};
static float brushSize = 40, brushHard = 0.8f, brushOpacity = 1.0f;
static float eraserSize = 40, eraserHard = 0.8f, eraserOpacity = 1.0f;
static int selMode = SEL_NEW; static float selFeather = 0;
static int tolerance = 32; static bool contiguous = true, sampleAll = false; static float bucketOpacity = 1.0f;

// view: canvas pixel (x, y) is at screen origin + (x, y) * zoom
static float zoom = 1.0f; static ImVec2 origin(0, 0); static ImVec2 canvasPos, canvasSize; static bool fitNext = true;

// interaction
static bool dragging = false, panning = false; static ImVec2 dragStart, dragNow, panStart, originStart;
static int moveStartX = 0, moveStartY = 0;
static std::vector<float> lasso, poly;
static Stroke stroke;
static std::vector<int> ants; static int antsVersion = -1;

// thumbnails
static std::vector<GLuint> thumbs, mthumbs; static int thumbVersion = -1; static double thumbAt = 0;

static double now() { return SDL_GetTicks() / 1000.0; }
static void say(const std::string& s) { toast = s; toastUntil = now() + 5; }
static void changed(Rect r = {0, 0, 1 << 30, 1 << 30}) { dirty = dirty.unite(r.clip(doc.w, doc.h)); docVersion++; doc.dirty = true; }
static void selChanged() { selVersion++; }

// ---------- document tabs ----------
struct Tab {
    int id = 0;                                  // ImGui's name for it (indices move when tabs close)
    Doc doc; History hist; bool noDoc = true;
    float zoom = 1; ImVec2 origin; bool showMask = false; bool fit = true;
};
static int tabIds = 1;
static std::vector<Tab> tabs(1, Tab{1});                 // tabs[curTab] is empty while it's the active one
static int curTab = 0, tabPick = -1, tabClose = -1;   // tabPick: ImGui has to be told which tab shows; tabClose: asked first
static bool jobRunning();
static void setTitle();

static void stashTab() {
    Tab& t = tabs[curTab];
    t.doc = std::move(doc); t.hist = std::move(hist); t.noDoc = noDoc;
    t.zoom = zoom; t.origin = origin; t.showMask = showMask; t.fit = fitNext;
    doc = Doc(); hist = History();
}

static void loadTab(int i) {
    curTab = i;
    Tab& t = tabs[i];
    doc = std::move(t.doc); hist = std::move(t.hist); noDoc = t.noDoc;
    zoom = t.zoom; origin = t.origin; showMask = t.showMask; fitNext = t.fit;
    const int id = t.id; t = Tab(); t.id = id; t.noDoc = false;
    dirty = {0, 0, doc.w, doc.h}; texW = -1;
    docVersion++; selVersion++; thumbVersion = -1; antsVersion = -1;
    lasso.clear(); poly.clear(); stroke = Stroke(); dragging = panning = false;
    tabPick = i;
    setTitle();
}

static bool switchTab(int i) {
    if (i == curTab || i < 0 || i >= (int)tabs.size()) return false;
    if (jobRunning()) { say("Wait for the filter to finish: its result goes to this picture"); tabPick = curTab; return false; }
    stashTab();
    loadTab(i);
    return true;
}

// a new tab for a picture coming in (the empty start is used as it is)
static void newTab() {
    if (noDoc) return;
    stashTab();
    Tab t; t.id = ++tabIds;
    tabs.push_back(std::move(t));
    loadTab((int)tabs.size() - 1);
}

static void closeTab(int i) {
    if (i != curTab && !switchTab(i)) return;
    if (tabs.size() == 1) {                      // the last one: back to the empty start
        doc = newDoc(1600, 1000, true); hist = History(); noDoc = true;
        dirty = {0, 0, doc.w, doc.h}; texW = -1; docVersion++; selVersion++; thumbVersion = -1; fitNext = true;
        setTitle();
        return;
    }
    tabs.erase(tabs.begin() + i);
    curTab = -1;
    loadTab(std::min(i, (int)tabs.size() - 1));
}

static void askCloseTab(int i) {
    const bool unsaved = i == curTab ? doc.dirty && hist.canUndo() : tabs[i].doc.dirty && tabs[i].hist.canUndo();
    if (jobRunning() && i == curTab) { say("Wait for the filter to finish first"); return; }
    if (unsaved) { if (i != curTab && !switchTab(i)) return; tabClose = i; } else closeTab(i);
}

static std::string tabName(int i) {
    const Doc& d = i == curTab ? doc : tabs[i].doc;
    const bool empty = i == curTab ? noDoc : tabs[i].noDoc;
    if (empty) return "Start";
    return (d.path.empty() ? std::string("Untitled") : u8path(d.path).filename().u8string()) + (d.dirty ? " *" : "");
}
static void remember(const std::string& what) { hist.push(doc, what); }

// ---------- GL textures ----------
static GLuint makeTex(int w, int h, const void* px, bool nearest, bool repeat) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return t;
}

static void uploadView() {
    if (texW != doc.w || texH != doc.h) {
        if (viewTex) glDeleteTextures(1, &viewTex);
        view = Image(doc.w, doc.h);
        viewTex = makeTex(doc.w, doc.h, nullptr, false, false);
        texW = doc.w; texH = doc.h;
        dirty = {0, 0, doc.w, doc.h};
    }
    if (dirty.empty()) return;
    composite(doc, view, dirty, showMask && doc.cur().hasMask);
    glBindTexture(GL_TEXTURE_2D, viewTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, doc.w);
    glTexSubImage2D(GL_TEXTURE_2D, 0, dirty.x0, dirty.y0, dirty.x1 - dirty.x0, dirty.y1 - dirty.y0, GL_RGBA, GL_UNSIGNED_BYTE, view.at(dirty.x0, dirty.y0));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    dirty = {0, 0, 0, 0};
}

// a small picture of a layer (or its mask), on the checkerboard
static void makeThumb(GLuint& t, const Layer& l, bool mask) {
    const int S = 40;
    std::vector<uint8_t> px((size_t)S * S * 4);
    const float k = std::max((float)doc.w, (float)doc.h) / S;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const int cx = (int)((x + 0.5f) * k - (S * k - doc.w) / 2), cy = (int)((y + 0.5f) * k - (S * k - doc.h) / 2);
            uint8_t* o = &px[((size_t)y * S + x) * 4];
            const uint8_t chk = ((x / 5 + y / 5) & 1) ? 0x55 : 0x77;
            o[0] = o[1] = o[2] = chk; o[3] = 255;
            if (cx < 0 || cy < 0 || cx >= doc.w || cy >= doc.h) { o[0] = o[1] = o[2] = 0x24; continue; }
            const int lx = cx - l.x, ly = cy - l.y;
            const bool in = lx >= 0 && ly >= 0 && lx < l.img.w && ly < l.img.h;
            if (mask) { const uint8_t v = in ? l.mask[(size_t)ly * l.img.w + lx] : 255; o[0] = o[1] = o[2] = v; continue; }
            if (!in) continue;
            const uint8_t* p = l.img.at(lx, ly);
            for (int c = 0; c < 3; ++c) o[c] = (uint8_t)((p[c] * p[3] + chk * (255 - p[3])) / 255);
        }
    if (!t) t = makeTex(S, S, px.data(), false, false);
    else { glBindTexture(GL_TEXTURE_2D, t); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px.data()); }
}

// painting a mask: the colours go black and white (as Photoshop and PhotoCraft do), and come back for the pixels
static void maskColours() {
    static bool was = false;
    static uint8_t keepFg[4], keepBg[4];
    const bool on = doc.editMask && !doc.layers.empty() && doc.cur().hasMask;
    if (on && !was) {
        memcpy(keepFg, fg, 4); memcpy(keepBg, bgc, 4);
        const bool grey = fg[0] == fg[1] && fg[1] == fg[2] && bgc[0] == bgc[1] && bgc[1] == bgc[2];
        if (!grey) { memset(fg, 255, 3); memset(bgc, 0, 3); }   // white reveals first; X swaps to black
    } else if (!on && was) {
        memcpy(fg, keepFg, 4); memcpy(bgc, keepBg, 4);
    }
    if (on)   // a colour picked while on the mask counts as its grey
        for (uint8_t* c : {fg, bgc}) { const uint8_t g = (uint8_t)((c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000); c[0] = c[1] = c[2] = g; }
    was = on;
}

static void updateThumbs() {
    if (thumbVersion == docVersion || (thumbVersion >= 0 && thumbs.size() == doc.layers.size() && now() - thumbAt < 0.25)) return;   // new or removed layers: at once
    thumbVersion = docVersion; thumbAt = now();
    thumbs.resize(doc.layers.size(), 0); mthumbs.resize(doc.layers.size(), 0);
    for (size_t i = 0; i < doc.layers.size(); ++i) {
        makeThumb(thumbs[i], doc.layers[i], false);
        if (doc.layers[i].hasMask) makeThumb(mthumbs[i], doc.layers[i], true);
    }
}

// ---------- files ----------
#ifdef _WIN32
static std::string fromW(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
#endif
// a file dialog: save = false opens. filter: "Pictures|*.png;*.jpg" pairs
static std::string fileDialog(bool save, const char* filter, const char* defExt, const std::string& start) {
#ifdef _WIN32
    std::wstring f;
    for (const char* p = filter; *p; ++p) f += (*p == '|') ? L'\0' : (wchar_t)*p;
    f += L'\0'; f += L'\0';
    wchar_t buf[4096] = {0};
    if (!start.empty()) { std::wstring s = u8path(start).wstring(); wcsncpy(buf, s.c_str(), 4095); }
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.lpstrFilter = f.c_str(); o.lpstrFile = buf; o.nMaxFile = 4096;
    std::wstring ext = defExt ? std::wstring(defExt, defExt + strlen(defExt)) : L"";
    o.lpstrDefExt = defExt ? ext.c_str() : nullptr;
    o.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR);
    const BOOL ok = save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    return ok ? fromW(buf) : std::string();
#else
    std::string cmd = save ? "zenity --file-selection --save --confirm-overwrite 2>/dev/null" : "zenity --file-selection 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    char line[4096] = {0};
    std::string r = fgets(line, sizeof line, p) ? line : "";
    pclose(p);
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    if (save && !r.empty() && defExt && r.find('.') == std::string::npos) r += std::string(".") + defExt;
    return r;
#endif
}

static const char* OPEN_FILTER = "Pictures and projects|*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.ltk|All files|*.*|";

static void setTitle() {
    std::string name = doc.path.empty() ? "Untitled" : u8path(doc.path).filename().u8string();
    SDL_SetWindowTitle(window, ("LayerTack - " + name + (doc.dirty ? " *" : "")).c_str());
}

static void fresh() {
    noDoc = false;
    hist = History();
    showMask = false; dirty = {0, 0, doc.w, doc.h}; texW = -1;
    docVersion++; selVersion++; fitNext = true;
    doc.dirty = false;
}

bool open(const std::string& path) {
    std::string err;
    Doc d;
    if (!noDoc && jobRunning()) { say("Wait for the filter to finish, then open it"); return false; }
    if (!openDoc(path, d, err)) { say(err); return false; }
    for (int i = 0; i < (int)tabs.size(); ++i) {   // already open: just go there
        const std::string& tp = i == curTab ? doc.path : tabs[i].doc.path;
        if (!tp.empty() && tp == path && (i == curTab ? !noDoc : !tabs[i].noDoc)) { switchTab(i); return true; }
    }
    newTab();
    doc = std::move(d);
    fresh();
    setTitle();
    return true;
}

static void placeAsLayer(const std::string& path) {
    Image img; std::string err;
    if (!loadImage(path, img, err)) { say(err); return; }
    remember("Place");
    Layer l;
    l.name = u8path(path).stem().u8string();
    l.x = (doc.w - img.w) / 2; l.y = (doc.h - img.h) / 2;
    l.img = std::move(img);
    addLayer(doc, std::move(l));
    changed();
}

void dropFile(const std::string& path) {
    // nothing open yet: the picture opens, and the canvas takes its size; later pictures come in as layers
    if (noDoc || doc.layers.empty() || path.size() > 4 && path.substr(path.size() - 4) == ".ltk") open(path);
    else placeAsLayer(path);
}

static void saveAs(bool project) {
    std::string start = doc.path;
    if (!start.empty()) start = u8path(start).replace_extension(project ? ".ltk" : ".png").u8string();
    const std::string p = project ? fileDialog(true, "LayerTack project|*.ltk|", "ltk", start)
                                  : fileDialog(true, "PNG|*.png|JPG|*.jpg|", "png", start);
    if (p.empty()) return;
    std::string err;
    if (!(project ? saveProject(doc, p, err) : saveImage(doc, p, err))) { say(err); return; }
    if (project) { doc.path = p; doc.dirty = false; }
    say("Saved " + u8path(p).filename().u8string());
    setTitle();
}

static void save() {
    if (!doc.path.empty() && doc.path.size() > 4 && doc.path.substr(doc.path.size() - 4) == ".ltk") {
        std::string err;
        if (saveProject(doc, doc.path, err)) { doc.dirty = false; say("Saved"); setTitle(); } else say(err);
    } else saveAs(true);
}

// ---------- commands ----------
static void selectAll() { doc.sel.assign((size_t)doc.w * doc.h, 255); selChanged(); }
static void deselect() { doc.sel.clear(); selChanged(); }

static void selectSubject(bool asMask) {
    say("Finding the subject...");
    Image comp;
    composite(doc, comp, {0, 0, doc.w, doc.h}, false);
    std::vector<uint8_t> m; std::string err;
    if (!segmentSubject(comp, m, err)) { say(err); return; }
    if (asMask) {
        // Remove Background: a mask on the active layer from the subject
        remember("Remove Background");
        std::vector<uint8_t> saved = doc.sel;
        doc.sel = m;
        if (doc.cur().hasMask) deleteMask(doc);
        addMask(doc, 2);
        doc.sel = std::move(saved);
        changed();
        say("Background hidden with a mask: paint the mask black or white to fix the edge");
    } else {
        remember("Select Subject");
        combineSel(doc, m, selMode);
        selChanged();
        say("Subject selected");
    }
}

static void fillWith(const uint8_t* c, const char* what) {
    remember(what);
    fillSelection(doc, c, 1.0f, BM_Normal);
    changed();
}
static void clearSelection() {
    remember("Clear");
    if (doc.editMask && doc.cur().hasMask) { const uint8_t black[4] = {0, 0, 0, 255}; fillSelection(doc, black, 1.0f, BM_Normal); }
    else {
        Work o = getWork(doc), r = o;
        for (size_t i = 0; i < r.px.size(); i += 4) r.px[i + 3] = 0;
        putWork(doc, o, r);
    }
    changed();
}

// ---------- dialogs (adjustments and filters with a live preview) ----------
enum Dlg { D_NONE, D_NEW, D_IMGSIZE, D_CANVAS, D_BC, D_HS, D_BW, D_CURVES, D_LEVELS, D_GAUSS, D_BOX, D_NOISE, D_FEATHER, D_GROW, D_THRESH, D_RENAME, D_ABOUT, D_TRANSFORM, D_REFINE };
static int dlg = D_NONE; static bool dlgOpen = false;
static Doc dlgBackup; static Work dlgOrig; static bool dlgPreview = true, dlgDirty = false;
static float p[8]; static bool pb[3]; static int pi[4];
static float bwW[6] = {40, 60, 40, 60, 20, 80}; static float bwTint[3] = {0.88f, 0.69f, 0.5f};
static std::vector<float> cx[4], cy[4]; static int curveCh = 0, curveDrag = -1;
static char renameBuf[128];
// Select and Mask: what it started from, the picture, the answer so far
static std::vector<float> rfSrc, rfRgb, rfOut; static RefineCache rfCache;

static bool needsTarget(int d) { return d >= D_BC && d <= D_THRESH && d != D_FEATHER && d != D_GROW; }

static void openDlg(int d) {
    if (needsTarget(d) && !doc.cur().visible) { say("The layer is hidden"); }
    dlg = d; dlgOpen = true; dlgDirty = true;
    memset(p, 0, sizeof p); memset(pb, 0, sizeof pb); memset(pi, 0, sizeof pi);
    switch (d) {
    case D_NEW: pi[0] = 1920; pi[1] = 1080; pb[0] = true; break;
    case D_IMGSIZE: pi[0] = doc.w; pi[1] = doc.h; pb[0] = true; break;
    case D_CANVAS: pi[0] = doc.w; pi[1] = doc.h; pi[2] = 1; pi[3] = 1; break;
    case D_GAUSS: p[0] = 4; break;
    case D_BOX: p[0] = 6; break;
    case D_NOISE: p[0] = 12; pb[0] = true; pb[1] = true; break;
    case D_FEATHER: p[0] = 8; break;
    case D_GROW: pi[0] = 4; break;
    case D_THRESH: p[0] = 128; break;
    case D_LEVELS: p[0] = 0; p[1] = 255; p[2] = 1; p[3] = 0; p[4] = 255; break;
    case D_CURVES: for (int c = 0; c < 4; ++c) { cx[c] = {0, 1}; cy[c] = {0, 1}; } curveCh = 0; break;
    case D_BW: { const float def[6] = {40, 60, 40, 60, 20, 80}; memcpy(bwW, def, sizeof def); break; }
    case D_RENAME: snprintf(renameBuf, sizeof renameBuf, "%s", doc.cur().name.c_str()); break;
    case D_TRANSFORM: pi[0] = pi[1] = 100; pb[0] = true; p[0] = 0; dlgBackup = doc; break;
    case D_REFINE: {
        const Layer& l = doc.cur();
        rfSrc.assign((size_t)doc.w * doc.h, 0.0f);
        if (doc.hasSel()) { for (size_t i = 0; i < rfSrc.size(); ++i) rfSrc[i] = doc.sel[i] / 255.0f; pi[1] = 0; }
        else if (l.hasMask) {
            for (int y = 0; y < doc.h; ++y)
                for (int x = 0; x < doc.w; ++x) {
                    const int lx = x - l.x, ly = y - l.y;
                    if (lx >= 0 && ly >= 0 && lx < l.img.w && ly < l.img.h) rfSrc[(size_t)y * doc.w + x] = l.mask[(size_t)ly * l.img.w + lx] / 255.0f;
                }
            pi[1] = 1;
        } else { say("Select something first (Select > Subject is a good start), or give the layer a mask"); dlgOpen = false; dlg = D_NONE; return; }
        Image comp; composite(doc, comp, {0, 0, doc.w, doc.h}, false);
        rfRgb.resize((size_t)doc.w * doc.h * 3);
        for (size_t i = 0; i < (size_t)doc.w * doc.h; ++i) for (int c = 0; c < 3; ++c) rfRgb[i * 3 + c] = comp.px[i * 4 + c] / 255.0f;
        rfCache = RefineCache();
        p[0] = 8; pb[0] = true; p[5] = 50; pi[0] = 0;
        dlgBackup = doc;
        break;
    }
    default: break;
    }
    if (needsTarget(d)) { dlgBackup = doc; dlgOrig = getWork(doc); }
    ImGui::OpenPopup("dlg");
}

static RefineParams refineParams() { RefineParams r; r.radius = p[0]; r.smart = pb[0]; r.smooth = p[1]; r.feather = p[2]; r.contrast = p[3]; r.shift = p[4]; return r; }

// the coverage made into a layer's mask (layer coordinates)
static void maskFromCanvas(Layer& l, const std::vector<float>& a) {
    if (!l.hasMask) { l.mask.assign((size_t)l.img.w * l.img.h, 255); l.hasMask = true; l.maskOn = true; }
    for (int y = 0; y < l.img.h; ++y)
        for (int x = 0; x < l.img.w; ++x) {
            const int cx = x + l.x, cy = y + l.y;
            l.mask[(size_t)y * l.img.w + x] = cx >= 0 && cy >= 0 && cx < doc.w && cy < doc.h ? to8(a[(size_t)cy * doc.w + cx]) : 0;
        }
}

static void refinePreview() {
    rfOut = refineMask(rfRgb, rfSrc, doc.w, doc.h, refineParams(), &rfCache);
    doc = dlgBackup;
    if (dlgPreview) {
        // the view: 0 overlay (red where it's out), 1 on black, 2 on white, 3 black & white, 4 on transparency
        Layer v = emptyLayer(doc, "Select and Mask");
        const int view = pi[0];
        std::vector<uint8_t> cut;
        if (view == 4) { cut.assign((size_t)doc.w * doc.h * 4, 0); for (size_t i = 0; i < cut.size() / 4; ++i) for (int c = 0; c < 3; ++c) cut[i * 4 + c] = to8(rfRgb[i * 3 + c]); }
        if (view == 4 && pb[1]) decontaminate(cut.data(), rfOut, doc.w, doc.h, p[0], p[5]);
        for (int y = 0; y < doc.h; ++y)
            for (int x = 0; x < doc.w; ++x) {
                const size_t i = (size_t)y * doc.w + x;
                const float a = rfOut[i];
                uint8_t* o = v.img.at(x, y);
                const float* c = &rfRgb[i * 3];
                o[3] = 255;
                switch (view) {
                case 0: for (int k = 0; k < 3; ++k) { const float red = k == 0 ? 1.0f : 0.0f; o[k] = to8(c[k] * a + (c[k] * 0.5f + red * 0.5f) * (1 - a)); } break;
                case 1: for (int k = 0; k < 3; ++k) o[k] = to8(c[k] * a); break;
                case 2: for (int k = 0; k < 3; ++k) o[k] = to8(c[k] * a + (1 - a)); break;
                case 3: o[0] = o[1] = o[2] = to8(a); break;
                default: memcpy(o, &cut[i * 4], 3); o[3] = to8(a); break;
                }
            }
        if (view == 4) for (auto& l : doc.layers) l.visible = false;
        addLayer(doc, std::move(v));
        doc.editMask = false;
    }
    changed();
}

// OK: the answer goes where Output To says
static void refineApply() {
    doc = dlgBackup;
    int out = pi[1];
    if (pb[1] && out < 2) out = 3;                 // clean colours need pixels of their own (as Photoshop)
    if (out == 0) { doc.sel.resize((size_t)doc.w * doc.h); for (size_t i = 0; i < rfOut.size(); ++i) doc.sel[i] = to8(rfOut[i]); selChanged(); }
    else if (out == 1) { maskFromCanvas(doc.cur(), rfOut); doc.editMask = false; }
    else {
        Layer l = doc.cur();
        if (l.isAdjust()) { say("Pick a layer with pixels for a new layer"); return; }
        expandToCanvas(doc, l);
        if (l.hasMask) { for (size_t i = 0; i < l.mask.size(); ++i) l.img.px[i * 4 + 3] = (uint8_t)(l.img.px[i * 4 + 3] * l.mask[i] / 255); l.hasMask = false; l.mask.clear(); }
        if (pb[1]) decontaminate(l.img.px.data(), rfOut, doc.w, doc.h, p[0], p[5]);
        l.name = doc.cur().name + (out == 3 ? " (masked)" : " (cut out)");
        if (out == 2) { for (size_t i = 0; i < rfOut.size(); ++i) l.img.px[i * 4 + 3] = (uint8_t)(l.img.px[i * 4 + 3] * to8(rfOut[i]) / 255); }
        else { maskFromCanvas(l, rfOut); }
        doc.cur().visible = false;               // the original stays, hidden, as Photoshop leaves it
        addLayer(doc, std::move(l));
        if (out == 3) doc.sel.clear();
        selChanged();
    }
    hist.push(dlgBackup, "Select and Mask");
    changed();
}

static void preview() {
    if (dlg == D_REFINE) { refinePreview(); return; }
    if (dlg == D_TRANSFORM) {
        // from the layer as it was, every time
        doc.layers[doc.active] = dlgBackup.layers[doc.active];
        transformLayer(doc, pi[0] / 100.0f, (pb[0] ? pi[0] : pi[1]) / 100.0f, p[0]);
        changed();
        return;
    }
    if (!needsTarget(dlg)) return;
    Work r = dlgOrig;
    if (dlgPreview) {
        switch (dlg) {
        case D_BC: adjBrightnessContrast(r, p[0], p[1]); break;
        case D_HS: adjHueSat(r, p[0], p[1], p[2], pb[0]); if (p[3] != 0) adjVibrance(r, p[3]); break;
        case D_BW: adjBlackWhite(r, bwW, pb[0], bwTint); break;
        case D_CURVES: { uint8_t lut[4][256]; for (int c = 0; c < 4; ++c) curveLUT(cx[c], cy[c], lut[c]); adjCurves(r, lut); break; }
        case D_LEVELS: adjLevels(r, p[0] / 255, p[1] / 255, p[2], p[3] / 255, p[4] / 255); break;
        case D_GAUSS: filterGaussianBlur(r, p[0]); break;
        case D_BOX: filterBoxBlur(r, (int)p[0]); break;
        case D_NOISE: filterAddNoise(r, p[0], pb[0], pb[1], 1234); break;
        case D_THRESH: adjThreshold(r, p[0] / 255); break;
        }
    }
    putWork(doc, dlgOrig, r);
    changed();
}

static void closeDlg(bool ok) {
    if (dlg == D_REFINE) {
        if (!ok) { doc = dlgBackup; changed(); }
        dlgBackup = Doc(); rfSrc.clear(); rfRgb.clear(); rfOut.clear(); rfCache = RefineCache();
    }
    if (dlg == D_TRANSFORM) {
        if (ok) hist.push(dlgBackup, "Free Transform");
        else { doc.layers[doc.active] = dlgBackup.layers[doc.active]; changed(); }
        dlgBackup = Doc();
    }
    if (needsTarget(dlg)) {
        if (ok) { if (!dlgPreview) { dlgPreview = true; preview(); } hist.push(dlgBackup, "Adjustment"); }
        else { putWork(doc, dlgOrig, dlgOrig); changed(); }
        dlgBackup = Doc(); dlgOrig = Work();
    }
    dlg = D_NONE; dlgOpen = false;
    ImGui::CloseCurrentPopup();
}

static void curvesWidget() {
    const float S = 256;
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("curve", ImVec2(S, S));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + S, a.y + S), C_WELL);
    unsigned h[256];
    histogram(doc, curveCh, h);
    unsigned mx = 1; for (int i = 1; i < 255; ++i) mx = std::max(mx, h[i]);
    for (int i = 0; i < 256; ++i) { const float v = std::min(1.0f, (float)h[i] / mx) * S; dl->AddLine(ImVec2(a.x + i + 0.5f, a.y + S), ImVec2(a.x + i + 0.5f, a.y + S - v), IM_COL32(0x44, 0x44, 0x44, 255)); }
    for (int i = 1; i < 4; ++i) { dl->AddLine(ImVec2(a.x + S * i / 4, a.y), ImVec2(a.x + S * i / 4, a.y + S), C_LINE); dl->AddLine(ImVec2(a.x, a.y + S * i / 4), ImVec2(a.x + S, a.y + S * i / 4), C_LINE); }
    dl->AddLine(ImVec2(a.x, a.y + S), ImVec2(a.x + S, a.y), IM_COL32(0x55, 0x55, 0x55, 255));
    auto& xs = cx[curveCh]; auto& ys = cy[curveCh];
    uint8_t lut[256];
    curveLUT(xs, ys, lut);
    const ImU32 lc = curveCh == 0 ? C_TEXT : (curveCh == 1 ? IM_COL32(255, 90, 90, 255) : curveCh == 2 ? IM_COL32(90, 220, 90, 255) : IM_COL32(90, 140, 255, 255));
    for (int i = 0; i < 255; ++i) dl->AddLine(ImVec2(a.x + i, a.y + S - lut[i]), ImVec2(a.x + i + 1, a.y + S - lut[i + 1]), lc, 2);
    for (size_t i = 0; i < xs.size(); ++i) dl->AddRect(ImVec2(a.x + xs[i] * S - 4, a.y + S - ys[i] * S - 4), ImVec2(a.x + xs[i] * S + 4, a.y + S - ys[i] * S + 4), C_PINK, 0, 0, 2);
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float mx01 = std::clamp((m.x - a.x) / S, 0.0f, 1.0f), my01 = std::clamp(1 - (m.y - a.y) / S, 0.0f, 1.0f);
    if (ImGui::IsItemActivated()) {
        curveDrag = -1;
        for (size_t i = 0; i < xs.size(); ++i) if (std::fabs(xs[i] - mx01) * S < 8 && std::fabs(ys[i] - my01) * S < 8) curveDrag = (int)i;
        if (curveDrag < 0) {                     // a new point
            size_t at = 0; while (at < xs.size() && xs[at] < mx01) ++at;
            xs.insert(xs.begin() + at, mx01); ys.insert(ys.begin() + at, my01); curveDrag = (int)at;
        }
    }
    if (ImGui::IsItemActive() && curveDrag >= 0) {
        const int i = curveDrag, n = (int)xs.size();
        const float lo = i > 0 ? xs[i - 1] + 0.01f : 0.0f, hi = i < n - 1 ? xs[i + 1] - 0.01f : 1.0f;
        xs[i] = (i == 0 || i == n - 1) && n > 2 ? std::clamp(mx01, lo, hi) : std::clamp(mx01, lo, hi);
        ys[i] = my01;
        // dragged off the side: the point goes (the ends stay)
        if (i > 0 && i < n - 1 && (m.y < a.y - 20 || m.y > a.y + S + 20)) { xs.erase(xs.begin() + i); ys.erase(ys.begin() + i); curveDrag = -1; }
        dlgDirty = true;
    }
    if (ImGui::IsItemDeactivated()) curveDrag = -1;
}

static void drawDialog() {
    if (!dlgOpen) return;
    const char* titles[] = {"", "New", "Image Size", "Canvas Size", "Brightness/Contrast", "Hue/Saturation", "Black & White", "Curves",
                            "Levels", "Gaussian Blur", "Box Blur", "Add Noise", "Feather Selection", "Grow / Shrink Selection", "Threshold", "Rename Layer", "About LayerTack", "Free Transform", "Select and Mask"};
    if (dlg == D_REFINE) {   // beside the canvas, so the edge stays in view
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 340, vp->Pos.y + 90), ImGuiCond_Appearing, ImVec2(1, 0));
    } else ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    static const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_ModalWindowDimBg);
    ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] = dlg == D_REFINE ? ImVec4(0, 0, 0, 0) : dim;   // no dimming over the edge being judged
    if (!ImGui::BeginPopupModal("dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    ImGui::PushStyleColor(ImGuiCol_Text, col4(C_PINK));
    ImGui::TextUnformatted(titles[dlg]);
    ImGui::PopStyleColor();
    ImGui::Separator();
    bool ch = false;
    ImGui::PushItemWidth(260);
    switch (dlg) {
    case D_NEW:
        ImGui::InputInt("Width", &pi[0]); ImGui::InputInt("Height", &pi[1]);
        ImGui::Checkbox("White background (else transparent)", &pb[0]);
        break;
    case D_IMGSIZE: {
        const int ow = pi[0];
        if (ImGui::InputInt("Width", &pi[0]) && pb[0]) pi[1] = std::max(1, (int)std::lround((double)pi[0] * doc.h / doc.w));
        if (ImGui::InputInt("Height", &pi[1]) && pb[0]) pi[0] = std::max(1, (int)std::lround((double)pi[1] * doc.w / doc.h));
        (void)ow;
        ImGui::Checkbox("Keep proportions", &pb[0]);
        ImGui::TextDisabled("Every layer and mask is resampled (bicubic).");
        break;
    }
    case D_CANVAS:
        ImGui::InputInt("Width", &pi[0]); ImGui::InputInt("Height", &pi[1]);
        ImGui::Text("Anchor");
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                if (x) ImGui::SameLine();
                ImGui::PushID(y * 3 + x);
                const bool on = pi[2] == x && pi[3] == y;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_PINK));
                if (ImGui::Button(on ? "o" : " ", ImVec2(26, 26))) { pi[2] = x; pi[3] = y; }
                if (on) ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        break;
    case D_BC: ch |= ImGui::SliderFloat("Brightness", &p[0], -150, 150, "%.0f"); ch |= ImGui::SliderFloat("Contrast", &p[1], -50, 100, "%.0f"); break;
    case D_HS:
        ch |= ImGui::SliderFloat("Hue", &p[0], -180, 180, "%.0f"); ch |= ImGui::SliderFloat("Saturation", &p[1], -100, 100, "%.0f");
        ch |= ImGui::SliderFloat("Lightness", &p[2], -100, 100, "%.0f"); ch |= ImGui::SliderFloat("Vibrance", &p[3], -100, 100, "%.0f");
        ch |= ImGui::Checkbox("Colorize", &pb[0]);
        break;
    case D_BW: {
        const char* names[6] = {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
        for (int i = 0; i < 6; ++i) ch |= ImGui::SliderFloat(names[i], &bwW[i], -200, 300, "%.0f%%");
        ch |= ImGui::Checkbox("Tint", &pb[0]);
        if (pb[0]) { ImGui::SameLine(); ch |= ImGui::ColorEdit3("##tint", bwTint, ImGuiColorEditFlags_NoInputs); }
        break;
    }
    case D_CURVES: {
        const char* chs[] = {"RGB", "Red", "Green", "Blue"};
        ImGui::Combo("Channel", &curveCh, chs, 4);
        curvesWidget();
        ImGui::TextDisabled("Click to add a point, drag it, drag it off the box to remove it.");
        if (ImGui::Button("Reset channel")) { cx[curveCh] = {0, 1}; cy[curveCh] = {0, 1}; dlgDirty = true; }
        break;
    }
    case D_LEVELS:
        ch |= ImGui::SliderFloat("Input black", &p[0], 0, 253, "%.0f"); ch |= ImGui::SliderFloat("Gamma", &p[2], 0.1f, 9.99f, "%.2f");
        ch |= ImGui::SliderFloat("Input white", &p[1], 2, 255, "%.0f"); ch |= ImGui::SliderFloat("Output black", &p[3], 0, 255, "%.0f");
        ch |= ImGui::SliderFloat("Output white", &p[4], 0, 255, "%.0f");
        break;
    case D_GAUSS: ImGui::SliderFloat("Radius (px)", &p[0], 0.1f, 250, "%.1f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_BOX: ImGui::SliderFloat("Radius (px)", &p[0], 1, 200, "%.0f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_NOISE:
        ImGui::SliderFloat("Amount (%)", &p[0], 0.1f, 400, "%.1f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::Checkbox("Gaussian (else uniform)", &pb[0]); ch |= ImGui::Checkbox("Monochromatic", &pb[1]);
        break;
    case D_FEATHER: ImGui::SliderFloat("Radius (px)", &p[0], 0.1f, 250, "%.1f", ImGuiSliderFlags_Logarithmic); break;
    case D_GROW: ImGui::SliderInt("Pixels (- shrinks)", &pi[0], -100, 100); break;
    case D_THRESH: ch |= ImGui::SliderFloat("Level", &p[0], 1, 255, "%.0f"); break;
    case D_REFINE: {
        const char* views[] = {"Overlay", "On Black", "On White", "Black & White", "On Layers"};
        ch |= ImGui::Combo("View", &pi[0], views, 5);
        ImGui::TextDisabled("EDGE DETECTION");
        ImGui::SliderFloat("Radius (px)", &p[0], 0, 250, "%.1f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far from the edge the picture decides: wide for hair and fur");
        ch |= ImGui::Checkbox("Smart Radius", &pb[0]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The band follows the picture: narrow on hard edges, wide on soft ones");
        ImGui::TextDisabled("GLOBAL REFINEMENTS");
        ch |= ImGui::SliderFloat("Smooth", &p[1], 0, 100, "%.0f");
        ch |= ImGui::SliderFloat("Feather (px)", &p[2], 0, 250, "%.1f", ImGuiSliderFlags_Logarithmic);
        ch |= ImGui::SliderFloat("Contrast (%)", &p[3], 0, 100, "%.0f");
        ch |= ImGui::SliderFloat("Shift Edge (%)", &p[4], -100, 100, "%.0f");
        ImGui::TextDisabled("OUTPUT");
        ch |= ImGui::Checkbox("Decontaminate Colors", &pb[1]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Takes the old background's colour out of the soft edge (makes a new layer)");
        if (pb[1]) ch |= ImGui::SliderFloat("Amount (%)", &p[5], 0, 100, "%.0f");
        const char* outs[] = {"Selection", "Layer Mask", "New Layer", "New Layer with Layer Mask"};
        if (pb[1] && pi[1] < 2) pi[1] = 3;
        ImGui::Combo("Output To", &pi[1], outs, 4);
        if (ImGui::Checkbox("Preview", &dlgPreview)) ch = true;
        break;
    }
    case D_TRANSFORM:
        ImGui::SliderInt(pb[0] ? "Scale (%)" : "Width (%)", &pi[0], 1, 400); ch |= ImGui::IsItemDeactivatedAfterEdit();
        if (!pb[0]) { ImGui::SliderInt("Height (%)", &pi[1], 1, 400); ch |= ImGui::IsItemDeactivatedAfterEdit(); }
        ImGui::SliderFloat("Rotate", &p[0], -180, 180, "%.1f deg"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::Checkbox("Keep proportions", &pb[0]);
        ImGui::SameLine();
        if (ImGui::Button("90 left")) { p[0] -= 90; ch = true; }
        ImGui::SameLine();
        if (ImGui::Button("90 right")) { p[0] += 90; ch = true; }
        if (p[0] > 180) p[0] -= 360; if (p[0] < -180) p[0] += 360;
        ImGui::TextDisabled("The layer and its mask, round the layer's centre.");
        break;
    case D_RENAME: ImGui::SetKeyboardFocusHere(); if (ImGui::InputText("Name", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) { remember("Rename"); doc.cur().name = renameBuf; closeDlg(true); ImGui::PopItemWidth(); ImGui::EndPopup(); return; } break;
    case D_ABOUT: {   // the splash, as it shows while LayerTack loads
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Splash::draw(ImGui::GetWindowDrawList(), at.x, at.y, nullptr, -1);
        ImGui::Dummy(ImVec2(Splash::W, Splash::H));
        break;
    }
    }
    ImGui::PopItemWidth();
    if (needsTarget(dlg)) { if (ImGui::Checkbox("Preview", &dlgPreview)) ch = true; }
    if (ch || dlgDirty) { dlgDirty = false; preview(); }
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) && dlg != D_RENAME) {
        switch (dlg) {
        case D_NEW: if (pi[0] > 0 && pi[1] > 0 && pi[0] <= 16384 && pi[1] <= 16384) { newTab(); doc = newDoc(pi[0], pi[1], pb[0]); fresh(); setTitle(); } break;
        case D_IMGSIZE: if (pi[0] > 0 && pi[1] > 0 && pi[0] <= 16384 && pi[1] <= 16384) { remember("Image Size"); resizeImage(doc, pi[0], pi[1]); changed(); selChanged(); fitNext = true; } break;
        case D_CANVAS: if (pi[0] > 0 && pi[1] > 0 && pi[0] <= 16384 && pi[1] <= 16384) { remember("Canvas Size"); resizeCanvas(doc, pi[0], pi[1], pi[2], pi[3]); changed(); selChanged(); fitNext = true; } break;
        case D_FEATHER: remember("Feather"); featherSel(doc, p[0]); selChanged(); break;
        case D_GROW: remember(pi[0] > 0 ? "Grow" : "Shrink"); growSel(doc, pi[0]); selChanged(); break;
        case D_RENAME: remember("Rename"); doc.cur().name = renameBuf; break;
        case D_REFINE: refineApply(); break;
        }
        closeDlg(true);
    }
    if (dlg != D_ABOUT) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) closeDlg(false);
    }
    ImGui::EndPopup();
}

// ---------- Filter > Changeling: the Changelings on the picture, the result as a new layer ----------
static bool clAsk = false;                       // open the window next frame (a menu can't open it itself)
static int clStep = -1, clSource = 0;            // which Changeling; 0 the whole picture (merged), 1 this layer
static Changeling::Job clJob;
static std::string clIn, clOut, clErr;           // clOut is set while a run's result is still to come
static int clX = 0, clY = 0, clW = 0, clH = 0;   // where the picture it was given sits on the canvas, its size
static std::vector<uint8_t> clAlpha;             // and its transparency, put back on the result
static double clStarted = 0;

static void changelingMenu() {
    auto& st = Changeling::steps();
    if (Changeling::stepsLoading()) ImGui::MenuItem("Looking for the Changelings...", nullptr, false, false);
    else if (st.empty()) ImGui::MenuItem(Changeling::stepsError().c_str(), nullptr, false, false);
    for (size_t i = 0; i < st.size(); ++i) {
        if (ImGui::MenuItem((st[i].app + "...").c_str(), nullptr, false, st[i].missing.empty())) { clStep = (int)i; clAsk = true; clErr.clear(); }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", (st[i].missing.empty() ? st[i].about : st[i].missing).c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Find Changeling Studio...")) {
        const std::string f = fileDialog(false, "Changeling Studio's chain.py|chain.py|", nullptr, Changeling::studioDir());
        if (!f.empty()) { Changeling::setStudioDir(u8path(f).parent_path().u8string()); Changeling::loadSteps(); }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Changeling::studioDir().empty() ? "Not found yet" : Changeling::studioDir().c_str());
    if (ImGui::MenuItem("Look Again")) Changeling::loadSteps();
}

static void changelingStart() {
    const Changeling::Step& step = Changeling::steps()[clStep];
    Image img;
    if (clSource == 0) { img = Image(doc.w, doc.h); composite(doc, img, {0, 0, doc.w, doc.h}, false); clX = clY = 0; }
    else {
        const Layer& l = doc.cur();
        img = l.img; clX = l.x; clY = l.y;
        for (int y = 0; y < img.h; ++y) for (int x = 0; x < img.w; ++x) img.at(x, y)[3] = (uint8_t)(img.at(x, y)[3] * l.maskAt(x, y) / 255);
    }
    // the Changelings see a photo: flattened onto white; the transparency comes back afterwards
    clW = img.w; clH = img.h; clAlpha.resize((size_t)img.w * img.h);
    for (size_t i = 0; i < clAlpha.size(); ++i) {
        uint8_t* q = &img.px[i * 4]; const int a = q[3]; clAlpha[i] = (uint8_t)a;
        for (int c = 0; c < 3; ++c) q[c] = (uint8_t)((q[c] * a + 255 * (255 - a) + 127) / 255);
        q[3] = 255;
    }
    const std::string dir = Changeling::workDir();
    clIn = (u8path(dir) / "in.png").u8string();
    const std::string out = (u8path(dir) / "out.png").u8string();
    std::error_code ec; std::filesystem::remove(u8path(out), ec);
    Doc d = newDoc(img.w, img.h, false); d.layers[0].img = std::move(img);
    std::string err;
    if (!saveImage(d, clIn, err)) { clErr = "Couldn't save the picture for it: " + err; return; }
    if (!Changeling::run(clJob, step, clIn, out)) { clErr = "Couldn't start Changeling Studio's chain.py"; return; }
    clOut = out; clErr.clear(); clStarted = now();
}

static void changelingFinish() {
    const Changeling::Step& step = Changeling::steps()[clStep];
    Image img; std::string err;
    const std::string out = clOut; clOut.clear();
    if (!loadImage(out, img, err)) { clErr = step.app + " didn't make a picture: " + err; return; }
    Layer l; l.name = step.app; l.img = std::move(img);
    const bool same = l.img.w == clW && l.img.h == clH;
    if (same) {                                  // the same picture, changed: in the same place, as transparent as it was
        l.x = clX; l.y = clY;
        for (size_t i = 0; i < clAlpha.size(); ++i) l.img.px[i * 4 + 3] = (uint8_t)(l.img.px[i * 4 + 3] * clAlpha[i] / 255);
    } else { l.x = (doc.w - l.img.w) / 2; l.y = (doc.h - l.img.h) / 2; }   // a crop (Tonsor's face square): centred
    remember(step.app);
    addLayer(doc, std::move(l));
    if (same && doc.hasSel()) { addMask(doc, 2); doc.editMask = false; }   // only where it was selected
    changed();
    say(step.app + (same ? ": a new layer" : ": a new layer (" + std::to_string(doc.cur().img.w) + " x " + std::to_string(doc.cur().img.h) + ", centred)"));
    ImGui::CloseCurrentPopup();
}

static void drawChangeling() {
    if (clAsk) { clAsk = false; ImGui::OpenPopup("changeling"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("changeling", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    if (clStep < 0 || clStep >= (int)Changeling::steps().size()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    Changeling::Step& step = Changeling::steps()[clStep];
    const auto state = clJob.state();
    if (!clOut.empty() && state == Changeling::Job::DONE) changelingFinish();
    else if (!clOut.empty() && state == Changeling::Job::FAILED) {
        auto ls = clJob.lines(); clErr = ls.empty() ? "It stopped" : ls.back(); clOut.clear();
    }
    const bool running = !clOut.empty();

    ImGui::PushStyleColor(ImGuiCol_Text, col4(C_PINK));
    ImGui::Text("Changeling %s", step.app.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextDisabled("%s", step.about.c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(running);
    ImGui::PushItemWidth(260);
    const char* sources[] = {"The whole picture (merged)", "This layer"};
    ImGui::Combo("On", &clSource, sources, 2);
    for (auto& o : step.options) {
        ImGui::PushID(o.key.c_str());
        if (o.kind == "choice") {
            std::vector<const char*> c; for (auto& s : o.choices) c.push_back(s.c_str());
            ImGui::Combo(o.label.c_str(), &o.choice, c.data(), (int)c.size());
        } else if (o.kind == "number") ImGui::SliderFloat(o.label.c_str(), &o.number, o.min, o.max, "%.2f");
        else if (o.kind == "check") ImGui::Checkbox(o.label.c_str(), &o.check);
        else {                                    // a photo
            const std::string name = o.file.empty() ? (o.kind == "file" ? "(pick one)" : "(none)") : u8path(o.file).filename().u8string();
            if (ImGui::Button("Browse...")) {
                const std::string f = fileDialog(false, "Pictures|*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.jfif|All files|*.*|", nullptr, o.file);
                if (!f.empty()) o.file = f;
            }
            if (!o.file.empty() && o.kind == "file?") { ImGui::SameLine(); if (ImGui::Button("Clear")) o.file.clear(); }
            ImGui::SameLine(); ImGui::Text("%s: %s", o.label.c_str(), name.c_str());
            if (!o.file.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.file.c_str());
        }
        ImGui::PopID();
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", doc.hasSel() ? "The result comes in as a new layer, masked to the selection." : "The result comes in as a new layer.");

    if (running || !clErr.empty()) {
        ImGui::BeginChild("log", ImVec2(560, 140), true);
        auto ls = clJob.lines();
        const size_t from = ls.size() > 60 ? ls.size() - 60 : 0;
        for (size_t i = from; i < ls.size(); ++i) ImGui::TextUnformatted(ls[i].c_str());
        if (running) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    if (running) ImGui::Text("Working... %.0f s (the first run loads the model: that takes a while)", now() - clStarted);
    if (!clErr.empty()) ImGui::TextColored(col4(C_PINK), "%s", clErr.c_str());
    const std::string why = step.notReady();
    if (!running && !why.empty()) ImGui::TextDisabled("%s", why.c_str());

    ImGui::Separator();
    if (running) {
        if (ImGui::Button("Stop", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { clJob.stop(); clErr = "Stopped"; clOut.clear(); }
    } else {
        ImGui::BeginDisabled(!why.empty());
        if (ImGui::Button("Run", ImVec2(110, 0))) changelingStart();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------- Atelier plugins (Bathos, Mimic): the picture out to their command-line tool, the result back as a layer ----------
struct PluginRun {
    Changeling::Job job;                          // the same background runner as the Changelings
    std::string out, err;                         // out is set while a run's result is still to come
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<uint8_t> alpha;
    double started = 0;
};

// the whole picture (source 0) or this layer, flattened onto white, saved for the tool; false + err when it can't
static bool pluginInput(PluginRun& r, int source, const std::string& name, std::string& inPath) {
    Image img;
    if (source == 0) { img = Image(doc.w, doc.h); composite(doc, img, {0, 0, doc.w, doc.h}, false); r.x = r.y = 0; }
    else {
        const Layer& l = doc.cur();
        img = l.img; r.x = l.x; r.y = l.y;
        for (int y = 0; y < img.h; ++y) for (int x = 0; x < img.w; ++x) img.at(x, y)[3] = (uint8_t)(img.at(x, y)[3] * l.maskAt(x, y) / 255);
    }
    r.w = img.w; r.h = img.h; r.alpha.resize((size_t)img.w * img.h);
    for (size_t i = 0; i < r.alpha.size(); ++i) {
        uint8_t* q = &img.px[i * 4]; const int a = q[3]; r.alpha[i] = (uint8_t)a;
        for (int c = 0; c < 3; ++c) q[c] = (uint8_t)((q[c] * a + 255 * (255 - a) + 127) / 255);
        q[3] = 255;
    }
    const std::string dir = Changeling::workDir();
    inPath = (u8path(dir) / (name + "-in.png")).u8string();
    r.out = (u8path(dir) / (name + "-out.png")).u8string();
    std::error_code ec; std::filesystem::remove(u8path(r.out), ec);
    Doc d = newDoc(img.w, img.h, false); d.layers[0].img = std::move(img);
    std::string err;
    if (!saveImage(d, inPath, err)) { r.err = "Couldn't save the picture for it: " + err; r.out.clear(); return false; }
    return true;
}

static void pluginStart(PluginRun& r, const std::vector<std::string>& args) {
    if (!r.job.start(args, Changeling::workDir())) { r.err = "Couldn't start " + u8path(args[0]).filename().u8string(); r.out.clear(); return; }
    r.err.clear(); r.started = now();
}

// the result as a new layer (in the same place, as transparent as it was, masked to the selection)
static void pluginFinish(PluginRun& r, const std::string& layerName, const std::string& what) {
    Image img; std::string err;
    const std::string out = r.out; r.out.clear();
    if (!loadImage(out, img, err)) { r.err = what + " didn't make a picture: " + err; return; }
    Layer l; l.name = layerName; l.img = std::move(img); l.x = r.x; l.y = r.y;
    if (l.img.w == r.w && l.img.h == r.h)
        for (size_t i = 0; i < r.alpha.size(); ++i) l.img.px[i * 4 + 3] = (uint8_t)(l.img.px[i * 4 + 3] * r.alpha[i] / 255);
    remember(what);
    addLayer(doc, std::move(l));
    if (doc.hasSel()) { addMask(doc, 2); doc.editMask = false; }
    changed();
    say(what + ": a new layer");
    ImGui::CloseCurrentPopup();
}

// a running plugin's state each frame: finished, failed, or still going
static bool pluginPoll(PluginRun& r, const std::string& layerName, const std::string& what) {
    const auto state = r.job.state();
    if (!r.out.empty() && state == Changeling::Job::DONE) pluginFinish(r, layerName, what);
    else if (!r.out.empty() && state == Changeling::Job::FAILED) { auto ls = r.job.lines(); r.err = ls.empty() ? "It stopped" : ls.back(); r.out.clear(); }
    return !r.out.empty();
}

// ---------- Filter > Bathos: a depth map of the picture (Bathos, Atelier's depth app, as a plugin) ----------
static bool btAsk = false;
static int btSource = 0;                         // 0 the whole picture (merged), 1 this layer
static Bathos::Options btOpt;
static Changeling::Job btJob;                    // the same background runner as the Changelings
static std::string btOut, btErr;                 // btOut is set while a run's result is still to come
static int btX = 0, btY = 0, btW = 0, btH = 0;
static std::vector<uint8_t> btAlpha;
static double btStarted = 0;

static void bathosMenu() {
    const bool have = !Bathos::cli().empty();
    if (ImGui::MenuItem("Depth Map...", nullptr, false, have)) { btAsk = true; btErr.clear(); }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", have ? "How far away everything is, as a new layer: near is bright, far is dark. Faces get a real 3D shape."
                                     : "Bathos isn't on this PC: install it, or Find Bathos...");
    ImGui::Separator();
    if (ImGui::MenuItem("Find Bathos...")) {
        const std::string f = fileDialog(false, "bathos-cli|bathos-cli.exe;bathos-cli|", nullptr, Bathos::cli());
        if (!f.empty()) Bathos::setCli(f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", have ? Bathos::cli().c_str() : "Not found yet");
}

static void bathosStart() {
    Image img;
    if (btSource == 0) { img = Image(doc.w, doc.h); composite(doc, img, {0, 0, doc.w, doc.h}, false); btX = btY = 0; }
    else {
        const Layer& l = doc.cur();
        img = l.img; btX = l.x; btY = l.y;
        for (int y = 0; y < img.h; ++y) for (int x = 0; x < img.w; ++x) img.at(x, y)[3] = (uint8_t)(img.at(x, y)[3] * l.maskAt(x, y) / 255);
    }
    // Bathos sees a photo: flattened onto white; the transparency comes back afterwards
    btW = img.w; btH = img.h; btAlpha.resize((size_t)img.w * img.h);
    for (size_t i = 0; i < btAlpha.size(); ++i) {
        uint8_t* q = &img.px[i * 4]; const int a = q[3]; btAlpha[i] = (uint8_t)a;
        for (int c = 0; c < 3; ++c) q[c] = (uint8_t)((q[c] * a + 255 * (255 - a) + 127) / 255);
        q[3] = 255;
    }
    const std::string dir = Changeling::workDir();
    const std::string in = (u8path(dir) / "bathos-in.png").u8string(), out = (u8path(dir) / "bathos-out.png").u8string();
    std::error_code ec; std::filesystem::remove(u8path(out), ec);
    Doc d = newDoc(img.w, img.h, false); d.layers[0].img = std::move(img);
    std::string err;
    if (!saveImage(d, in, err)) { btErr = "Couldn't save the picture for it: " + err; return; }
    if (!btJob.start(Bathos::args(btOpt, in, out), dir)) { btErr = "Couldn't start bathos-cli"; return; }
    btOut = out; btErr.clear(); btStarted = now();
}

static void bathosFinish() {
    Image img; std::string err;
    const std::string out = btOut; btOut.clear();
    if (!loadImage(out, img, err)) { btErr = "Bathos didn't make a depth map: " + err; return; }
    Layer l; l.name = "Depth (Bathos)"; l.img = std::move(img); l.x = btX; l.y = btY;
    if (l.img.w == btW && l.img.h == btH)
        for (size_t i = 0; i < btAlpha.size(); ++i) l.img.px[i * 4 + 3] = (uint8_t)(l.img.px[i * 4 + 3] * btAlpha[i] / 255);
    remember("Depth Map");
    addLayer(doc, std::move(l));
    if (doc.hasSel()) { addMask(doc, 2); doc.editMask = false; }
    changed();
    say("Bathos: a depth map as a new layer");
    ImGui::CloseCurrentPopup();
}

static void drawBathos() {
    if (btAsk) { btAsk = false; ImGui::OpenPopup("bathos"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("bathos", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const auto state = btJob.state();
    if (!btOut.empty() && state == Changeling::Job::DONE) bathosFinish();
    else if (!btOut.empty() && state == Changeling::Job::FAILED) { auto ls = btJob.lines(); btErr = ls.empty() ? "It stopped" : ls.back(); btOut.clear(); }
    const bool running = !btOut.empty();

    ImGui::PushStyleColor(ImGuiCol_Text, col4(C_PINK)); ImGui::TextUnformatted("Bathos Depth Map"); ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextDisabled("near is bright, far is dark");
    ImGui::Separator();
    ImGui::BeginDisabled(running);
    ImGui::PushItemWidth(260);
    const char* sources[] = {"The whole picture (merged)", "This layer"};
    ImGui::Combo("On", &btSource, sources, 2);
    ImGui::SliderInt("Detail", &btOpt.detail, 280, 1036, "%d px");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the model sees: more picks out finer shapes but takes longer. 518 is what it was trained on.");
    ImGui::Checkbox("Refine edges", &btOpt.refine);
    ImGui::SameLine(); ImGui::Checkbox("Proper faces", &btOpt.faces);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fits a 3D face mesh to every face, so noses, eye sockets, cheeks and chins have real depth.");
    ImGui::BeginDisabled(!btOpt.faces);
    ImGui::SliderFloat("Face relief", &btOpt.faceRelief, 0.0f, 3.0f, "%.2f");
    ImGui::EndDisabled();
    ImGui::Checkbox("Colour ramp", &btOpt.colour);
    ImGui::SameLine(); ImGui::Checkbox("Far is bright", &btOpt.farBright);
    ImGui::SliderFloat("Contrast", &btOpt.contrast, 0.3f, 3.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", doc.hasSel() ? "The depth map comes in as a new layer, masked to the selection." : "The depth map comes in as a new layer.");
    if (running) ImGui::Text("Working... %.0f s (the first run loads the depth model)", now() - btStarted);
    if (!btErr.empty()) ImGui::TextColored(col4(C_PINK), "%s", btErr.c_str());
    ImGui::Separator();
    if (running) {
        if (ImGui::Button("Stop", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { btJob.stop(); btErr = "Stopped"; btOut.clear(); }
    } else {
        if (ImGui::Button("Run", ImVec2(110, 0))) bathosStart();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------- Filter > Mimic: one person's face onto the picture (Mimic, Atelier's face-swap app, as a plugin) ----------
static bool mmAsk = false;
static int mmSource = 0;
static MimicPlug::Options mmOpt;
static PluginRun mmRun;

static void mimicMenu() {
    const bool have = !MimicPlug::cli().empty();
    if (ImGui::MenuItem("Face Swap...", nullptr, false, have)) { mmAsk = true; mmRun.err.clear(); }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", have ? "Put one person's face onto the faces in the picture, as a new layer."
                                     : "Mimic isn't on this PC: install it, or Find Mimic...");
    ImGui::Separator();
    if (ImGui::MenuItem("Find Mimic...")) {
        const std::string f = fileDialog(false, "mimic-cli|mimic-cli.exe;mimic-cli|", nullptr, MimicPlug::cli());
        if (!f.empty()) MimicPlug::setCli(f);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", have ? MimicPlug::cli().c_str() : "Not found yet");
}

static void mimicStart() {
    std::string in;
    if (!pluginInput(mmRun, mmSource, "mimic", in)) return;
    pluginStart(mmRun, MimicPlug::args(mmOpt, in, mmRun.out));
}

static void drawMimic() {
    if (mmAsk) { mmAsk = false; ImGui::OpenPopup("mimic"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("mimic", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const bool running = pluginPoll(mmRun, "Mimic", "Mimic");
    ImGui::PushStyleColor(ImGuiCol_Text, col4(C_PINK)); ImGui::TextUnformatted("Mimic Face Swap"); ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextDisabled("only use faces you have the right to use");
    ImGui::Separator();
    ImGui::BeginDisabled(running);
    ImGui::PushItemWidth(260);
    const char* sources[] = {"The whole picture (merged)", "This layer"};
    ImGui::Combo("On", &mmSource, sources, 2);
    if (ImGui::Button("Pictures of the face to become...")) {
        const std::string f = fileDialog(false, "Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.tga|All files|*.*|", nullptr, mmOpt.faces.empty() ? "" : mmOpt.faces.back());
        if (!f.empty()) mmOpt.faces.push_back(f);
    }
    ImGui::SameLine();
    if (mmOpt.faces.empty()) ImGui::TextDisabled("(none yet)");
    else {
        ImGui::Text("%d picture%s", (int)mmOpt.faces.size(), mmOpt.faces.size() == 1 ? "" : "s");
        if (ImGui::IsItemHovered()) { std::string t; for (auto& f : mmOpt.faces) t += u8path(f).filename().u8string() + "\n"; ImGui::SetTooltip("%s", t.c_str()); }
        ImGui::SameLine(); if (ImGui::SmallButton("Clear")) mmOpt.faces.clear();
    }
    const char* swappers[] = {"inswapper (fast)", "HyperSwap (sharper, CPU)"};
    ImGui::Combo("Swap", &mmOpt.swapper, swappers, 2);
    ImGui::SliderInt("Pixel boost", &mmOpt.boost, 1, 4, mmOpt.boost == 1 ? "off" : "%dx");
    ImGui::SliderFloat("Likeness", &mmOpt.likeness, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0: more of the face to become. 1: more of the picture's own look.");
    const char* enh[] = {"None", "GFPGAN 1.4", "CodeFormer", "GPEN 512"};
    ImGui::Combo("Restore", &mmOpt.enhancer, enh, 4);
    ImGui::BeginDisabled(mmOpt.enhancer == 0);
    ImGui::SliderFloat("Blend", &mmOpt.blend, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();
    const char* which[] = {"Every face", "The biggest face"};
    ImGui::Combo("Faces", &mmOpt.which, which, 2);
    ImGui::Checkbox("Keep what's in front (hands, hair, glasses)", &mmOpt.occlusion);
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", doc.hasSel() ? "The result comes in as a new layer, masked to the selection." : "The result comes in as a new layer.");
    if (running) ImGui::Text("Working... %.0f s (the first run loads the face models)", now() - mmRun.started);
    if (!mmRun.err.empty()) ImGui::TextColored(col4(C_PINK), "%s", mmRun.err.c_str());
    ImGui::Separator();
    if (running) {
        if (ImGui::Button("Stop", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { mmRun.job.stop(); mmRun.err = "Stopped"; mmRun.out.clear(); }
    } else {
        ImGui::BeginDisabled(mmOpt.faces.empty());
        if (ImGui::Button("Run", ImVec2(110, 0))) mimicStart();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------- the view ----------
static ImVec2 toScreen(float x, float y) { return ImVec2(canvasPos.x + origin.x + x * zoom, canvasPos.y + origin.y + y * zoom); }
static ImVec2 toCanvas(ImVec2 s) { return ImVec2((s.x - canvasPos.x - origin.x) / zoom, (s.y - canvasPos.y - origin.y) / zoom); }
static void fit() {
    zoom = std::min(1.0f, std::min((canvasSize.x - 40) / doc.w, (canvasSize.y - 40) / doc.h));
    origin = ImVec2((canvasSize.x - doc.w * zoom) / 2, (canvasSize.y - doc.h * zoom) / 2);
}
static void zoomAt(float nz, ImVec2 screen) {
    nz = std::clamp(nz, 0.02f, 64.0f);
    const ImVec2 c = toCanvas(screen);
    zoom = nz;
    origin = ImVec2(screen.x - canvasPos.x - c.x * zoom, screen.y - canvasPos.y - c.y * zoom);
}

static int modeFromKeys() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && io.KeyAlt) return SEL_AND;
    if (io.KeyShift) return SEL_ADD;
    if (io.KeyAlt) return SEL_SUB;
    return selMode;
}

static void finishShape(std::vector<uint8_t> shape, int mode, const char* what) {
    remember(what);
    combineSel(doc, std::move(shape), mode);
    if (selFeather > 0) featherSel(doc, selFeather);
    selChanged();
}

static void pickColour(ImVec2 c, bool toBg) {
    const int x = (int)c.x, y = (int)c.y;
    if (x < 0 || y < 0 || x >= doc.w || y >= doc.h) return;
    memcpy(toBg ? bgc : fg, view.at(x, y), 3);
}

static uint8_t* brushColour() { return fg; }

static void canvasInput() {
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 m = io.MousePos;
    const ImVec2 c = toCanvas(m);
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
    const int t = space ? T_HAND : tool;

    if (hovered && io.MouseWheel != 0) zoomAt(zoom * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), m);
    // middle button always pans
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) { panning = true; panStart = m; originStart = origin; }
    if (panning) { origin = ImVec2(originStart.x + m.x - panStart.x, originStart.y + m.y - panStart.y); if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) panning = false; }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging = true; dragStart = c; dragNow = c;
        switch (t) {
        case T_HAND: panning = true; panStart = m; originStart = origin; dragging = false; break;
        case T_MOVE: remember("Move"); moveStartX = doc.cur().x; moveStartY = doc.cur().y; break;
        case T_LASSO: lasso = {c.x, c.y}; break;
        case T_POLY:
            if (poly.empty()) poly = {c.x, c.y};
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || (std::hypot(c.x - poly[0], c.y - poly[1]) * zoom < 8 && poly.size() >= 6)) {
                finishShape(shapePolygon(doc, poly), modeFromKeys(), "Polygonal Lasso"); poly.clear();
            } else { poly.push_back(c.x); poly.push_back(c.y); }
            dragging = false;
            break;
        case T_WAND: finishShape(shapeWand(doc, (int)c.x, (int)c.y, tolerance, contiguous, sampleAll), modeFromKeys(), "Magic Wand"); dragging = false; break;
        case T_BUCKET:
            if (c.x >= 0 && c.y >= 0 && c.x < doc.w && c.y < doc.h) { remember("Paint Bucket"); bucketFill(doc, (int)c.x, (int)c.y, fg, tolerance, contiguous, sampleAll, bucketOpacity); changed(); }
            dragging = false;
            break;
        case T_DROPPER: pickColour(c, io.KeyAlt); dragging = true; break;
        case T_BRUSH: case T_ERASER:
            if (io.KeyAlt && t == T_BRUSH) { pickColour(c, false); dragging = false; break; }
            if (!doc.cur().visible) { say("The layer is hidden: show it to paint"); dragging = false; break; }
            remember(t == T_BRUSH ? "Brush" : "Eraser");
            strokeBegin(doc, stroke);
            changed(strokeTo(doc, stroke, c.x, c.y, t == T_BRUSH ? brushSize : eraserSize, t == T_BRUSH ? brushHard : eraserHard, t == T_BRUSH ? brushOpacity : eraserOpacity, brushColour(), t == T_ERASER, true));
            break;
        default: break;
        }
    }
    if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragNow = c;
        switch (t) {
        case T_MOVE: {
            Layer& l = doc.cur();
            const int nx = moveStartX + (int)std::lround(c.x - dragStart.x), ny = moveStartY + (int)std::lround(c.y - dragStart.y);
            if (nx != l.x || ny != l.y) { l.x = nx; l.y = ny; changed(); }
            break;
        }
        case T_LASSO: if (std::hypot(c.x - lasso[lasso.size() - 2], c.y - lasso.back()) * zoom > 2) { lasso.push_back(c.x); lasso.push_back(c.y); } break;
        case T_DROPPER: pickColour(c, io.KeyAlt); break;
        case T_BRUSH: case T_ERASER:
            if (stroke.on) changed(strokeTo(doc, stroke, c.x, c.y, t == T_BRUSH ? brushSize : eraserSize, t == T_BRUSH ? brushHard : eraserHard, t == T_BRUSH ? brushOpacity : eraserOpacity, brushColour(), t == T_ERASER, false));
            break;
        default: break;
        }
    }
    if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging = false;
        switch (t) {
        case T_RECT: case T_ELLIPSE:
            if (std::fabs(c.x - dragStart.x) < 1 && std::fabs(c.y - dragStart.y) < 1) { if (modeFromKeys() == SEL_NEW) { remember("Deselect"); deselect(); } }
            else finishShape(t == T_RECT ? shapeRect(doc, (int)std::lround(dragStart.x), (int)std::lround(dragStart.y), (int)std::lround(c.x), (int)std::lround(c.y))
                                         : shapeEllipse(doc, (int)std::lround(dragStart.x), (int)std::lround(dragStart.y), (int)std::lround(c.x), (int)std::lround(c.y)),
                             modeFromKeys(), t == T_RECT ? "Rectangle Select" : "Ellipse Select");
            break;
        case T_LASSO: if (lasso.size() >= 6) finishShape(shapePolygon(doc, lasso), modeFromKeys(), "Lasso"); lasso.clear(); break;
        case T_BRUSH: case T_ERASER: strokeEnd(stroke); break;
        default: break;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && t != T_HAND && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning = false;
    if (t == T_HAND && panning) origin = ImVec2(originStart.x + m.x - panStart.x, originStart.y + m.y - panStart.y);
}

static void drawCanvas() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), C_BG);
    const ImVec2 a = toScreen(0, 0), b = toScreen((float)doc.w, (float)doc.h);
    dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
    dl->AddImage((ImTextureID)(intptr_t)checkerTex, a, b, ImVec2(0, 0), ImVec2((b.x - a.x) / 16, (b.y - a.y) / 16));
    glBindTexture(GL_TEXTURE_2D, viewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, zoom >= 2 ? GL_NEAREST : GL_LINEAR);
    dl->AddImage((ImTextureID)(intptr_t)viewTex, a, b);
    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), C_LINE);

    // marching ants
    if (antsVersion != selVersion) { ants = selOutline(doc); antsVersion = selVersion; }
    const int phase = (int)(now() * 8);
    for (size_t i = 0; dlg != D_REFINE && i + 3 < ants.size(); i += 4) {   // (Select and Mask shows the edge itself)
        const ImVec2 p0 = toScreen((float)ants[i], (float)ants[i + 1]), p1 = toScreen((float)ants[i + 2], (float)ants[i + 3]);
        const bool on = (((ants[i] + ants[i + 1]) / 4 + phase) & 1) != 0;
        dl->AddLine(p0, p1, on ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255), 1.0f);
    }
    // what's being drawn right now
    const ImGuiIO& io = ImGui::GetIO();
    if (dragging && (tool == T_RECT || tool == T_ELLIPSE)) {
        const ImVec2 p0 = toScreen(dragStart.x, dragStart.y), p1 = toScreen(dragNow.x, dragNow.y);
        if (tool == T_RECT) dl->AddRect(p0, p1, C_PINK, 0, 0, 1.5f);
        else dl->AddEllipse(ImVec2((p0.x + p1.x) / 2, (p0.y + p1.y) / 2), std::fabs(p1.x - p0.x) / 2, std::fabs(p1.y - p0.y) / 2, C_PINK, 0, 64, 1.5f);
    }
    auto path = [&](const std::vector<float>& v, bool toMouse) {
        for (size_t i = 0; i + 3 < v.size(); i += 2) dl->AddLine(toScreen(v[i], v[i + 1]), toScreen(v[i + 2], v[i + 3]), C_PINK, 1.5f);
        if (toMouse && v.size() >= 2) dl->AddLine(toScreen(v[v.size() - 2], v.back()), io.MousePos, C_PINK, 1.0f);
    };
    // the Lasso draws nothing while you drag: its selection appears when you let go. The Polygonal Lasso shows its
    // points, since it's built click by click.
    if (!poly.empty()) path(poly, true);
    // the brush outline
    if ((tool == T_BRUSH || tool == T_ERASER) && ImGui::IsWindowHovered()) {
        const float r = (tool == T_BRUSH ? brushSize : eraserSize) * 0.5f * zoom;
        const float h = (tool == T_BRUSH ? brushHard : eraserHard);
        dl->AddCircle(io.MousePos, r, IM_COL32(255, 255, 255, 200), 48, 1.0f);
        dl->AddCircle(io.MousePos, r + 1, IM_COL32(0, 0, 0, 160), 48, 1.0f);
        if (h < 0.98f) dl->AddCircle(io.MousePos, std::max(1.0f, r * h), IM_COL32(255, 255, 255, 90), 48, 1.0f);   // the solid core
    }
    dl->PopClipRect();
}

// ---------- tool icons (drawn, so there's no icon font to ship) ----------
static void toolIcon(ImDrawList* dl, int t, ImVec2 c, ImU32 col) {
    const float s = 9;
    switch (t) {
    case T_MOVE: dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, 2); dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), col, 2);
        for (int k = 0; k < 4; ++k) { const float a = k * 1.5708f, ca = std::cos(a), sa = std::sin(a); const ImVec2 tip(c.x + ca * s, c.y + sa * s);
            dl->AddTriangleFilled(tip, ImVec2(tip.x - ca * 4 - sa * 3, tip.y - sa * 4 + ca * 3), ImVec2(tip.x - ca * 4 + sa * 3, tip.y - sa * 4 - ca * 3), col); } break;
    case T_RECT: for (int i = 0; i < 4; ++i) { const float o = -s + i * 5; dl->AddLine(ImVec2(c.x + o, c.y - s), ImVec2(c.x + o + 3, c.y - s), col, 2); dl->AddLine(ImVec2(c.x + o, c.y + s), ImVec2(c.x + o + 3, c.y + s), col, 2);
            dl->AddLine(ImVec2(c.x - s, c.y + o), ImVec2(c.x - s, c.y + o + 3), col, 2); dl->AddLine(ImVec2(c.x + s, c.y + o), ImVec2(c.x + s, c.y + o + 3), col, 2); } break;
    case T_ELLIPSE: dl->AddEllipse(c, s, s * 0.75f, col, 0, 24, 2); break;
    case T_LASSO: dl->AddBezierCubic(ImVec2(c.x - 2, c.y + 6), ImVec2(c.x - 14, c.y - 2), ImVec2(c.x + 4, c.y - 16), ImVec2(c.x + 9, c.y - 2), col, 2);
        dl->AddBezierCubic(ImVec2(c.x + 9, c.y - 2), ImVec2(c.x + 12, c.y + 6), ImVec2(c.x + 2, c.y + 8), ImVec2(c.x - 2, c.y + 6), col, 2); dl->AddLine(ImVec2(c.x - 2, c.y + 6), ImVec2(c.x - 6, c.y + 10), col, 2); break;
    case T_POLY: { ImVec2 pts[5] = {ImVec2(c.x - s, c.y + 4), ImVec2(c.x - 4, c.y - s), ImVec2(c.x + s, c.y - 5), ImVec2(c.x + 5, c.y + s), ImVec2(c.x - s, c.y + 4)}; dl->AddPolyline(pts, 5, col, 0, 2); break; }
    case T_WAND: dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + 3, c.y - 3), col, 3);
        for (int k = 0; k < 4; ++k) { const float a = k * 1.5708f + 0.78f; dl->AddLine(ImVec2(c.x + 5 + std::cos(a) * 2, c.y - 5 + std::sin(a) * 2), ImVec2(c.x + 5 + std::cos(a) * 6, c.y - 5 + std::sin(a) * 6), col, 1.5f); } break;
    case T_BRUSH: dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - 2, c.y + 2), col, 3); dl->AddCircleFilled(ImVec2(c.x - 4, c.y + 5), 4, col); break;
    case T_ERASER: { ImVec2 q[4] = {ImVec2(c.x - s, c.y + 3), ImVec2(c.x - 1, c.y - s + 1), ImVec2(c.x + s, c.y - 2), ImVec2(c.x + 1, c.y + s - 1)}; dl->AddQuad(q[0], q[1], q[2], q[3], col, 2);
        dl->AddQuadFilled(q[0], ImVec2((q[0].x + q[1].x) / 2 + 2, (q[0].y + q[1].y) / 2 + 2), ImVec2((q[3].x + q[2].x) / 2 + 1, (q[3].y + q[2].y) / 2 + 3), q[3], col); break; }
    case T_BUCKET: { ImVec2 q[4] = {ImVec2(c.x - 7, c.y - 2), ImVec2(c.x, c.y - 9), ImVec2(c.x + 7, c.y - 2), ImVec2(c.x, c.y + 5)}; dl->AddQuad(q[0], q[1], q[2], q[3], col, 2); dl->AddCircleFilled(ImVec2(c.x + 8, c.y + 6), 3, col); break; }
    case T_DROPPER: dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + 2, c.y - 2), col, 2); dl->AddCircleFilled(ImVec2(c.x + 5, c.y - 5), 4, col); break;
    case T_HAND: for (int k = 0; k < 4; ++k) dl->AddLine(ImVec2(c.x - 6 + k * 4, c.y - 1), ImVec2(c.x - 6 + k * 4, c.y - 8 + (k == 0 || k == 3 ? 3 : 0)), col, 2.5f);
        dl->AddRectFilled(ImVec2(c.x - 7, c.y - 2), ImVec2(c.x + 7, c.y + 7), col, 3); break;
    }
}

static const char* TOOL_NAMES[T_COUNT] = {"Move (V)", "Rectangle Select (M)", "Ellipse Select (M)", "Lasso (L)", "Polygonal Lasso (L)", "Magic Wand (W)",
                                          "Brush (B)", "Eraser (E)", "Paint Bucket (G)", "Eyedropper (I)", "Hand (H, or hold Space)"};

static void toolsPanel() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int t = 0; t < T_COUNT; ++t) {
        ImGui::PushID(t);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("t", ImVec2(32, 32))) { tool = t; poly.clear(); }
        const bool on = tool == t, hov = ImGui::IsItemHovered();
        dl->AddRectFilled(a, ImVec2(a.x + 32, a.y + 32), on ? C_PINK : (hov ? IM_COL32(0x33, 0x33, 0x33, 255) : C_PANEL), 3);
        toolIcon(dl, t, ImVec2(a.x + 16, a.y + 16), on ? IM_COL32(255, 255, 255, 255) : C_TEXT);
        if (hov) ImGui::SetTooltip("%s", TOOL_NAMES[t]);
        ImGui::PopID();
        if (t == T_MOVE || t == T_WAND || t == T_BUCKET) ImGui::Dummy(ImVec2(1, 4));
    }
    // colours: foreground over background, swap, default
    ImGui::Dummy(ImVec2(1, 8));
    const ImVec2 a = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(a.x + 12, a.y + 12), ImVec2(a.x + 32, a.y + 32), IM_COL32(bgc[0], bgc[1], bgc[2], 255));
    dl->AddRect(ImVec2(a.x + 12, a.y + 12), ImVec2(a.x + 32, a.y + 32), C_LINE);
    ImGui::SetCursorScreenPos(ImVec2(a.x + 12, a.y + 12));
    if (ImGui::InvisibleButton("bgc", ImVec2(20, 20))) ImGui::OpenPopup("pickbg");
    dl->AddRectFilled(a, ImVec2(a.x + 20, a.y + 20), IM_COL32(fg[0], fg[1], fg[2], 255));
    dl->AddRect(a, ImVec2(a.x + 20, a.y + 20), IM_COL32(255, 255, 255, 200));
    ImGui::SetCursorScreenPos(a);
    if (ImGui::InvisibleButton("fgc", ImVec2(20, 20))) ImGui::OpenPopup("pickfg");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foreground colour (X swaps, D black/white)");
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + 36));
    for (int k = 0; k < 2; ++k) {
        if (ImGui::BeginPopup(k ? "pickbg" : "pickfg")) {
            uint8_t* c = k ? bgc : fg;
            float f[3] = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f};
            if (ImGui::ColorPicker3("##c", f, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex)) for (int i = 0; i < 3; ++i) c[i] = (uint8_t)std::lround(f[i] * 255);
            ImGui::EndPopup();
        }
    }
}

static bool sliderF(const char* label, float* v, float lo, float hi, const char* fmt, ImGuiSliderFlags f = 0) {
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(0, 5);
    ImGui::PushID(label); const bool r = ImGui::SliderFloat("##v", v, lo, hi, fmt, f); ImGui::PopID(); return r;
}
static bool sliderI(const char* label, int* v, int lo, int hi) {
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(0, 5);
    ImGui::PushID(label); const bool r = ImGui::SliderInt("##v", v, lo, hi); ImGui::PopID(); return r;
}

static void optionsBar() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(col4(C_PINK), "%s", TOOL_NAMES[tool]);
    ImGui::SameLine(0, 18);
    ImGui::PushItemWidth(120);
    auto selModes = [&]() {
        const char* names[] = {"New", "Add", "Subtract", "Intersect"};
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine(0, 2);
            const bool on = selMode == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_PINK));
            if (ImGui::Button(names[i])) selMode = i;
            if (on) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Shift adds, Alt subtracts");
    };
    switch (tool) {
    case T_BRUSH: case T_ERASER: {
        float& sz = tool == T_BRUSH ? brushSize : eraserSize;
        float& hd = tool == T_BRUSH ? brushHard : eraserHard;
        float& op = tool == T_BRUSH ? brushOpacity : eraserOpacity;
        sliderF("Size", &sz, 1, 1000, "%.0f px", ImGuiSliderFlags_Logarithmic); ImGui::SameLine(0, 14);
        float h = hd * 100; if (sliderF("Hardness", &h, 0, 100, "%.0f%%")) hd = h / 100; ImGui::SameLine(0, 14);
        float o = op * 100; if (sliderF("Opacity", &o, 1, 100, "%.0f%%")) op = o / 100; ImGui::SameLine(0, 14);
        ImGui::TextDisabled(doc.editMask && doc.cur().hasMask ? "Painting the MASK: black hides, white reveals" : "[ ] size, Shift+[ ] hardness, Alt picks a colour");
        break;
    }
    case T_RECT: case T_ELLIPSE: case T_LASSO: case T_POLY:
        selModes(); ImGui::SameLine(0, 14);
        sliderF("Feather", &selFeather, 0, 100, "%.0f px");
        if (tool == T_POLY) { ImGui::SameLine(0, 14); ImGui::TextDisabled("Click points, double-click or Enter closes"); }
        break;
    case T_WAND: case T_BUCKET:
        if (tool == T_WAND) { selModes(); ImGui::SameLine(0, 14); }
        sliderI("Tolerance", &tolerance, 0, 255); ImGui::SameLine(0, 10);
        ImGui::Checkbox("Contiguous", &contiguous); ImGui::SameLine(0, 10);
        ImGui::Checkbox("Sample all layers", &sampleAll);
        if (tool == T_BUCKET) { ImGui::SameLine(0, 10); float o = bucketOpacity * 100; if (sliderF("Opacity", &o, 1, 100, "%.0f%%")) bucketOpacity = o / 100; }
        break;
    case T_MOVE: ImGui::TextDisabled("Drag to move the layer (and its mask); arrow keys nudge, Shift+arrows by 10"); break;
    case T_DROPPER: ImGui::TextDisabled("Click picks the foreground colour, Alt+click the background"); break;
    case T_HAND: ImGui::TextDisabled("Drag to pan; the wheel zooms; Ctrl+0 fits, Ctrl+1 is 100%%"); break;
    }
    ImGui::PopItemWidth();
}

// The colour tags for the active layer: a swatch and its name each, the current one ticked (PhotoCraft's menu)
static void labelMenu() {
    Layer& l = doc.cur();
    for (int c = 0; c < LABEL_COUNT; ++c) {
        if (c == 1) ImGui::Separator();
        ImGui::PushID(c);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##tag", l.label == c, 0, ImVec2(170, 18)) && l.label != c) { remember("Layer Colour"); l.label = c; doc.dirty = true; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (c > 0) dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + 16, p.y + 16), LABELS[c].swatch);
        else dl->AddRect(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + 16, p.y + 16), C_DIM);
        dl->AddText(ImVec2(p.x + 24, p.y + 1), C_TEXT, LABELS[c].name);
        ImGui::PopID();
    }
}

// ---------- the Properties panel: the active adjustment layer's settings, live ----------
// one undo step per slider drag: remembered as the drag starts
static bool adjEdit(bool changedNow) {
    if (ImGui::IsItemActivated()) remember("Adjustment");
    if (changedNow) changed();
    return changedNow;
}
static bool adjSlider(const char* id, float* v, float lo, float hi, const char* fmt, const char* tip = nullptr) {
    const bool c = ImGui::SliderFloat(id, v, lo, hi, fmt);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { /* right-click: back to 0 handled by callers that want it */ }
    return adjEdit(c);
}
static bool adjCheck(const char* label, bool* v) { if (ImGui::Checkbox(label, v)) { remember("Adjustment"); changed(); return true; } return false; }
static bool adjColor(const char* id, float* rgb) { return adjEdit(ImGui::ColorEdit3(id, rgb, ImGuiColorEditFlags_NoInputs)); }

// Curves: a little editor; click adds a point, drag moves it, right-click removes it
static void curveEditor(std::vector<float>& pts, ImU32 col) {
    const float side = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + side, a.y + side);
    dl->AddRectFilled(a, b, C_WELL);
    for (int i = 1; i < 4; ++i) { dl->AddLine(ImVec2(a.x + side * i / 4, a.y), ImVec2(a.x + side * i / 4, b.y), C_LINE); dl->AddLine(ImVec2(a.x, a.y + side * i / 4), ImVec2(b.x, a.y + side * i / 4), C_LINE); }
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), C_LINE);
    if (pts.size() < 4) pts = {0, 0, 1, 1};
    Adjustment tmp = Adjustment::make(ADJ_CURVES);
    tmp.curve[0] = pts;
    AdjPrepared* prep = adjPrepare(tmp);
    ImVec2 prev;
    for (int x = 0; x <= 64; ++x) {
        float c[3] = {x / 64.0f, x / 64.0f, x / 64.0f};
        adjApply(tmp, prep, c, 0, 0);
        const ImVec2 pt(a.x + side * x / 64.0f, b.y - side * c[0]);
        if (x) dl->AddLine(prev, pt, col, 2.0f);
        prev = pt;
    }
    adjFree(prep);
    static int dragging = -1;
    ImGui::InvisibleButton("curve", ImVec2(side, side));
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float mx = std::clamp((m.x - a.x) / side, 0.0f, 1.0f), my = std::clamp((b.y - m.y) / side, 0.0f, 1.0f);
    int hit = -1;
    for (size_t i = 0; i + 1 < pts.size(); i += 2)
        if (std::fabs(pts[i] - mx) * side < 8 && std::fabs(pts[i + 1] - my) * side < 8) hit = (int)i;
    for (size_t i = 0; i + 1 < pts.size(); i += 2) dl->AddCircleFilled(ImVec2(a.x + side * pts[i], b.y - side * pts[i + 1]), (int)i == hit ? 6.0f : 4.5f, (int)i == hit ? C_PINK : C_TEXT);
    if (ImGui::IsItemActivated()) {
        remember("Curves");
        if (hit >= 0) dragging = hit;
        else { pts.push_back(mx); pts.push_back(my); dragging = (int)pts.size() - 2; changed(); }
    }
    if (ImGui::IsItemActive() && dragging >= 0 && dragging + 1 < (int)pts.size()) { pts[dragging] = mx; pts[dragging + 1] = my; changed(); }
    if (ImGui::IsItemDeactivated()) dragging = -1;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hit >= 0 && pts.size() > 4) { remember("Curves"); pts.erase(pts.begin() + hit, pts.begin() + hit + 2); changed(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to add a point, drag to move it, right-click to remove it");
}

static void propertiesPanel() {
    Layer& l = doc.cur();
    Adjustment& a = l.adj;
    ImGui::TextColored(col4(C_PINK), "PROPERTIES");
    ImGui::SameLine(); ImGui::TextDisabled("%s", adjName(a.kind));
    ImGui::PushItemWidth(-1);
    static int channel = 0, range = 0, tone = 1, outCh = 0, sel = 0;
    switch (a.kind) {
        case ADJ_BRIGHTNESS_CONTRAST:
            adjSlider("##b", &a.v[0], -150, 150, "Brightness %.0f");
            adjSlider("##c", &a.v[1], a.flag ? -100.0f : -50.0f, 100, "Contrast %.0f");
            adjCheck("Use Legacy", &a.flag);
            break;
        case ADJ_LEVELS: {
            const char* chs[] = {"RGB", "Red", "Green", "Blue"};
            ImGui::Combo("##ch", &channel, chs, 4);
            LevelsCh& c = a.levels[channel];
            float ib = c.inBlack * 255, iw = c.inWhite * 255, ob = c.outBlack * 255, ow = c.outWhite * 255;
            if (adjSlider("##ib", &ib, 0, 253, "Input black %.0f")) c.inBlack = std::min(ib, iw - 2) / 255;
            if (adjSlider("##g", &c.gamma, 0.1f, 9.99f, "Gamma %.2f")) changed();
            if (adjSlider("##iw", &iw, 2, 255, "Input white %.0f")) c.inWhite = std::max(iw, ib + 2) / 255;
            if (adjSlider("##ob", &ob, 0, 255, "Output black %.0f")) c.outBlack = ob / 255;
            if (adjSlider("##ow", &ow, 0, 255, "Output white %.0f")) c.outWhite = ow / 255;
            break;
        }
        case ADJ_CURVES: {
            const char* chs[] = {"RGB", "Red", "Green", "Blue"};
            ImGui::Combo("##ch", &channel, chs, 4);
            static const ImU32 cols[4] = {C_TEXT, IM_COL32(240, 80, 80, 255), IM_COL32(90, 220, 110, 255), IM_COL32(90, 150, 255, 255)};
            curveEditor(a.curve[channel], cols[channel]);
            if (ImGui::Button("Reset this curve", ImVec2(-1, 0))) { remember("Curves"); a.curve[channel].clear(); changed(); }
            break;
        }
        case ADJ_EXPOSURE:
            adjSlider("##e", &a.v[0], -20, 20, "Exposure %.2f");
            adjSlider("##o", &a.v[1], -0.5f, 0.5f, "Offset %.4f");
            adjSlider("##g", &a.v[2], 0.01f, 9.99f, "Gamma %.2f");
            break;
        case ADJ_VIBRANCE:
            adjSlider("##v", &a.v[0], -100, 100, "Vibrance %.0f", "Dull colours move most; skin tones are spared");
            adjSlider("##s", &a.v[1], -100, 100, "Saturation %.0f");
            break;
        case ADJ_HUE_SATURATION: {
            const char* rs[] = {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
            ImGui::BeginDisabled(a.flag);
            ImGui::Combo("##range", &range, rs, 7);
            ImGui::EndDisabled();
            if (range == 0 || a.flag) {
                adjSlider("##h", &a.v[0], a.flag ? 0.0f : -180.0f, a.flag ? 360.0f : 180.0f, "Hue %.0f");
                adjSlider("##s", &a.v[1], a.flag ? 0.0f : -100.0f, 100, "Saturation %.0f");
                adjSlider("##l", &a.v[2], -100, 100, "Lightness %.0f");
            } else {
                HueRange& r = a.ranges[range - 1];
                adjSlider("##h", &r.hue, -180, 180, "Hue %.0f");
                adjSlider("##s", &r.sat, -100, 100, "Saturation %.0f");
                adjSlider("##l", &r.light, -100, 100, "Lightness %.0f");
            }
            if (adjCheck("Colorize", &a.flag) && a.flag) { a.v[1] = std::max(a.v[1], 25.0f); }
            break;
        }
        case ADJ_COLOR_BALANCE: {
            const char* ts[] = {"Shadows", "Midtones", "Highlights"};
            ImGui::Combo("##tone", &tone, ts, 3);
            adjSlider("##cr", &a.balance[tone][0], -100, 100, "Cyan %.0f Red");
            adjSlider("##mg", &a.balance[tone][1], -100, 100, "Magenta %.0f Green");
            adjSlider("##yb", &a.balance[tone][2], -100, 100, "Yellow %.0f Blue");
            adjCheck("Preserve Luminosity", &a.flag);
            break;
        }
        case ADJ_BLACK_WHITE: {
            const char* ns[] = {"Reds %.0f%%", "Yellows %.0f%%", "Greens %.0f%%", "Cyans %.0f%%", "Blues %.0f%%", "Magentas %.0f%%"};
            for (int i = 0; i < 6; ++i) { ImGui::PushID(i); adjSlider("##bw", &a.bw[i], -200, 300, ns[i]); ImGui::PopID(); }
            adjCheck("Tint", &a.flag);
            if (a.flag) { ImGui::SameLine(); adjColor("##tint", a.tint); }
            break;
        }
        case ADJ_PHOTO_FILTER: {
            struct P { const char* n; float c[3]; };
            static const P presets[] = {{"Warming (85)", {0.925f, 0.541f, 0.0f}}, {"Warming (LBA)", {0.98f, 0.588f, 0.0f}}, {"Warming (81)", {0.922f, 0.694f, 0.075f}},
                                        {"Cooling (80)", {0.0f, 0.427f, 1.0f}}, {"Cooling (LBB)", {0.0f, 0.365f, 1.0f}}, {"Cooling (82)", {0.0f, 0.71f, 1.0f}},
                                        {"Red", {0.918f, 0.102f, 0.102f}}, {"Orange", {0.953f, 0.518f, 0.09f}}, {"Yellow", {0.976f, 0.89f, 0.11f}},
                                        {"Green", {0.098f, 0.788f, 0.098f}}, {"Cyan", {0.114f, 0.796f, 0.918f}}, {"Blue", {0.114f, 0.208f, 0.918f}},
                                        {"Violet", {0.608f, 0.114f, 0.918f}}, {"Magenta", {0.89f, 0.094f, 0.89f}}, {"Sepia", {0.675f, 0.478f, 0.2f}},
                                        {"Cursed pink", {0.878f, 0.251f, 0.541f}}};
            if (ImGui::BeginCombo("##preset", "Filter...")) {
                for (const auto& pr : presets) if (ImGui::Selectable(pr.n)) { remember("Photo Filter"); memcpy(a.color, pr.c, sizeof a.color); changed(); }
                ImGui::EndCombo();
            }
            adjColor("Colour", a.color);
            float dens = a.v[0] * 100;
            if (adjSlider("##d", &dens, 0, 100, "Density %.0f%%")) a.v[0] = dens / 100;
            adjCheck("Preserve Luminosity", &a.flag);
            break;
        }
        case ADJ_CHANNEL_MIXER: {
            const char* os[] = {"Output: Red", "Output: Green", "Output: Blue"};
            ImGui::BeginDisabled(a.flag); ImGui::Combo("##out", &outCh, os, 3); ImGui::EndDisabled();
            const int r = a.flag ? 0 : outCh;
            float v[4] = {a.mixer[r][0] * 100, a.mixer[r][1] * 100, a.mixer[r][2] * 100, a.mixer[r][3] * 100};
            const char* ns[] = {"Red %.0f%%", "Green %.0f%%", "Blue %.0f%%", "Constant %.0f%%"};
            for (int i = 0; i < 4; ++i) { ImGui::PushID(i); if (adjSlider("##m", &v[i], -200, 200, ns[i])) a.mixer[r][i] = v[i] / 100; ImGui::PopID(); }
            if (adjCheck("Monochrome", &a.flag) && a.flag) { a.mixer[0][0] = 0.4f; a.mixer[0][1] = 0.4f; a.mixer[0][2] = 0.2f; }
            break;
        }
        case ADJ_INVERT: ImGui::TextDisabled("Invert has no settings."); break;
        case ADJ_POSTERIZE: adjSlider("##p", &a.v[0], 2, 255, "Levels %.0f"); a.v[0] = std::round(a.v[0]); break;
        case ADJ_THRESHOLD: { float t = a.v[0] * 255; if (adjSlider("##t", &t, 1, 255, "Threshold level %.0f")) a.v[0] = std::round(t) / 255; break; }
        case ADJ_GRADIENT_MAP: {
            if (ImGui::BeginCombo("##gpreset", "Gradient...")) {
                struct G { const char* n; std::vector<std::array<float, 4>> s; };
                static const G gs[] = {{"Black, White", {{0, 0, 0, 0}, {1, 1, 1, 1}}},
                                       {"Cursed duotone", {{0, 0.1f, 0.1f, 0.1f}, {1, 0.878f, 0.251f, 0.541f}}},
                                       {"Sepia", {{0, 0.12f, 0.07f, 0.03f}, {0.5f, 0.6f, 0.43f, 0.26f}, {1, 1, 0.96f, 0.88f}}},
                                       {"Infrared", {{0, 0.1f, 0.0f, 0.3f}, {0.5f, 0.9f, 0.2f, 0.4f}, {1, 1, 0.95f, 0.6f}}},
                                       {"Teal and orange", {{0, 0.02f, 0.18f, 0.22f}, {0.5f, 0.5f, 0.5f, 0.48f}, {1, 1, 0.68f, 0.35f}}}};
                for (const auto& g : gs) if (ImGui::Selectable(g.n)) { remember("Gradient Map"); a.stops = g.s; changed(); }
                ImGui::EndCombo();
            }
            for (size_t i = 0; i < a.stops.size(); ++i) {
                ImGui::PushID((int)i);
                float col[3] = {a.stops[i][1], a.stops[i][2], a.stops[i][3]};
                if (adjColor("##sc", col)) { a.stops[i][1] = col[0]; a.stops[i][2] = col[1]; a.stops[i][3] = col[2]; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                float pos = a.stops[i][0] * 100;
                if (adjSlider("##sp", &pos, 0, 100, "stop at %.0f%%")) a.stops[i][0] = pos / 100;
                ImGui::SameLine();
                if (a.stops.size() > 2 && ImGui::SmallButton("x")) { remember("Gradient Map"); a.stops.erase(a.stops.begin() + i); changed(); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            std::sort(a.stops.begin(), a.stops.end(), [](const auto& x, const auto& y) { return x[0] < y[0]; });
            if (ImGui::Button("Add a stop", ImVec2(-1, 0))) { remember("Gradient Map"); a.stops.push_back({0.5f, 0.5f, 0.5f, 0.5f}); changed(); }
            adjCheck("Reverse", &a.flag); ImGui::SameLine(); adjCheck("Dither", &a.flag2);
            break;
        }
        case ADJ_SELECTIVE_COLOR: {
            const char* cs[] = {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Whites", "Neutrals", "Blacks"};
            ImGui::Combo("##sel", &sel, cs, 9);
            const char* ns[] = {"Cyan %.0f%%", "Magenta %.0f%%", "Yellow %.0f%%", "Black %.0f%%"};
            for (int i = 0; i < 4; ++i) { ImGui::PushID(i); adjSlider("##sc", &a.selective[sel][i], -100, 100, ns[i]); ImGui::PopID(); }
            bool rel = a.flag;
            if (ImGui::RadioButton("Relative", rel)) { remember("Selective Color"); a.flag = true; changed(); }
            ImGui::SameLine();
            if (ImGui::RadioButton("Absolute", !rel)) { remember("Selective Color"); a.flag = false; changed(); }
            break;
        }
        default: break;
    }
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Paint its mask to choose where it applies.");
    ImGui::Separator();
}

static void newAdjustmentMenu() {
    for (int k = ADJ_BRIGHTNESS_CONTRAST; k < ADJ_COUNT; ++k) {
        if (k == ADJ_VIBRANCE || k == ADJ_BLACK_WHITE || k == ADJ_INVERT) ImGui::Separator();
        if (ImGui::MenuItem((std::string(adjName(k)) + "...").c_str())) { remember("New " + std::string(adjName(k))); addAdjustmentLayer(doc, k); changed(); }
    }
}

static void layersPanel(float height) {
    Layer& cur = doc.cur();
    ImGui::TextColored(col4(C_PINK), "LAYERS");
    ImGui::PushItemWidth(-1);
    if (ImGui::BeginCombo("##blend", blendName(cur.blend))) {
        for (int m = 0; m < BM_Count; ++m) {
            if (blendGroupStart(m)) ImGui::Separator();
            if (ImGui::Selectable(blendName(m), m == cur.blend)) { remember("Blend Mode"); cur.blend = m; changed(); }
        }
        ImGui::EndCombo();
    }
    float op = cur.opacity * 100;
    static bool opEditing = false;
    if (ImGui::SliderFloat("##op", &op, 0, 100, "Opacity %.0f%%")) { if (!opEditing) { remember("Opacity"); opEditing = true; } cur.opacity = op / 100; changed(); }
    if (ImGui::IsItemDeactivated()) opEditing = false;
    ImGui::PopItemWidth();

    ImGui::BeginChild("layerlist", ImVec2(0, height), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = (int)doc.layers.size() - 1; i >= 0; --i) {
        Layer& l = doc.layers[i];
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = 48;
        const bool active = i == doc.active;
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), active ? IM_COL32(0x3a, 0x20, 0x2e, 255) : C_PANEL);
        // its colour tag fills the eye column (as in PhotoCraft); right-click the eye column to set it
        if (l.label > 0 && l.label < LABEL_COUNT) dl->AddRectFilled(a, ImVec2(a.x + 27, a.y + h), LABELS[l.label].eye);
        dl->AddLine(ImVec2(a.x, a.y + h), ImVec2(a.x + w, a.y + h), C_LINE);
        // the eye
        ImGui::SetCursorScreenPos(ImVec2(a.x + 4, a.y + 14));
        if (ImGui::InvisibleButton("eye", ImVec2(20, 20))) { remember("Visibility"); l.visible = !l.visible; changed(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show / hide; right-click for the layer's colour tag");
        if (ImGui::BeginPopupContextItem("tag")) { doc.active = i; labelMenu(); ImGui::EndPopup(); }
        if (l.visible) { dl->AddEllipse(ImVec2(a.x + 14, a.y + 24), 8, 5, C_TEXT, 0, 16, 1.5f); dl->AddCircleFilled(ImVec2(a.x + 14, a.y + 24), 2.5f, C_TEXT); }
        else dl->AddRect(ImVec2(a.x + 7, a.y + 17), ImVec2(a.x + 21, a.y + 31), C_LINE);
        // the layer's picture; click edits the pixels
        const ImVec2 tp(a.x + 28, a.y + 4);
        if (l.isAdjust()) {   // Photoshop's adjustment sign: a circle, half dark, half light
            const ImVec2 c(tp.x + 20, tp.y + 20);
            dl->AddRectFilled(tp, ImVec2(tp.x + 40, tp.y + 40), C_WELL);
            dl->AddCircleFilled(c, 12, C_TEXT, 32);
            dl->PathClear(); dl->PathArcTo(c, 12, 1.5708f, 4.7124f, 24); dl->PathFillConvex(IM_COL32(0x33, 0x33, 0x33, 255));
            dl->AddCircle(c, 12, C_TEXT, 32, 1.5f);
        } else if (i < (int)thumbs.size() && thumbs[i]) dl->AddImage((ImTextureID)(intptr_t)thumbs[i], tp, ImVec2(tp.x + 40, tp.y + 40));
        ImGui::SetCursorScreenPos(tp);
        if (ImGui::InvisibleButton("px", ImVec2(40, 40))) { doc.active = i; doc.editMask = l.isAdjust() && l.hasMask; if (showMask) { showMask = false; changed(); } }
        if (l.isAdjust() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s adjustment: its settings are in Properties above", adjName(l.adj.kind));
        if (active && !doc.editMask) dl->AddRect(ImVec2(tp.x - 2, tp.y - 2), ImVec2(tp.x + 42, tp.y + 42), C_PINK, 0, 0, 2);
        float nameX = tp.x + 48;
        // the mask: click edits it, Shift+click turns it off, Alt+click shows it
        if (l.hasMask) {
            const ImVec2 mp(tp.x + 46, tp.y);
            if (i < (int)mthumbs.size() && mthumbs[i]) dl->AddImage((ImTextureID)(intptr_t)mthumbs[i], mp, ImVec2(mp.x + 40, mp.y + 40));
            if (!l.maskOn) { dl->AddLine(mp, ImVec2(mp.x + 40, mp.y + 40), IM_COL32(255, 60, 60, 255), 2); dl->AddLine(ImVec2(mp.x + 40, mp.y), ImVec2(mp.x, mp.y + 40), IM_COL32(255, 60, 60, 255), 2); }
            ImGui::SetCursorScreenPos(mp);
            if (ImGui::InvisibleButton("mask", ImVec2(40, 40))) {
                const ImGuiIO& io = ImGui::GetIO();
                doc.active = i;
                if (io.KeyShift) { remember(l.maskOn ? "Disable Mask" : "Enable Mask"); l.maskOn = !l.maskOn; changed(); }
                else if (io.KeyAlt) { doc.editMask = true; showMask = !showMask; changed(); }
                else { doc.editMask = true; if (showMask) { showMask = false; changed(); } }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layer mask: click to paint it, Shift+click turns it off, Alt+click shows it");
            if (active && doc.editMask) dl->AddRect(ImVec2(mp.x - 2, mp.y - 2), ImVec2(mp.x + 42, mp.y + 42), C_PINK, 0, 0, 2);
            nameX = mp.x + 48;
        }
        // the name; double-click renames
        ImGui::SetCursorScreenPos(ImVec2(nameX, a.y));
        if (ImGui::InvisibleButton("name", ImVec2(std::max(10.0f, a.x + w - nameX), h))) { doc.active = i; doc.editMask = l.isAdjust() && l.hasMask; }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openDlg(D_RENAME);
        dl->AddText(ImVec2(nameX, a.y + 9), l.visible ? C_TEXT : C_DIM, l.name.c_str());
        char info[64];
        snprintf(info, sizeof info, "%s%s", l.blend ? blendName(l.blend) : "", l.opacity < 1 ? (std::string(l.blend ? ", " : "") + std::to_string((int)std::lround(l.opacity * 100)) + "%").c_str() : "");
        dl->AddText(ImVec2(nameX, a.y + 26), C_DIM, info);
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + 1));
        ImGui::PopID();
    }
    ImGui::EndChild();
    // the buttons under the list
    auto btn = [&](const char* label, const char* tip) { const bool r = ImGui::Button(label); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip); ImGui::SameLine(0, 3); return r; };
    if (btn("New", "New empty layer (Ctrl+Shift+N)")) { remember("New Layer"); addLayer(doc, emptyLayer(doc, "Layer " + std::to_string(doc.layers.size()))); changed(); }
    if (btn("Mask", "Add a layer mask (from the selection when there is one); Alt: hide all")) {
        if (!cur.hasMask) { remember("Add Mask"); addMask(doc, doc.hasSel() ? 2 : (ImGui::GetIO().KeyAlt ? 1 : 0)); changed(); }
        else say("The layer has a mask already");
    }
    if (btn("Adj", "New adjustment layer")) ImGui::OpenPopup("newadj");
    if (ImGui::BeginPopup("newadj")) { newAdjustmentMenu(); ImGui::EndPopup(); }
    if (btn("Dup", "Duplicate (Ctrl+J)")) { remember("Duplicate"); duplicateLayer(doc); changed(); }
    if (btn("↑", "Move the layer up")) { remember("Reorder"); moveLayer(doc, 1); changed(); }
    if (btn("↓", "Move the layer down")) { remember("Reorder"); moveLayer(doc, -1); changed(); }
    if (btn("Merge", "Merge down (Ctrl+E)")) { remember("Merge Down"); mergeDown(doc); changed(); }
    if (btn("Del", "Delete the layer")) { remember("Delete Layer"); deleteLayer(doc); changed(); }
    ImGui::NewLine();
}

// ---------- menus ----------
static bool item(const char* label, const char* key = nullptr, bool enabled = true) { return ImGui::MenuItem(label, key, false, enabled); }

static void menus() {
    if (!ImGui::BeginMainMenuBar()) return;
    const bool mask = doc.cur().hasMask;
    if (ImGui::BeginMenu("File")) {
        if (item("New...", "Ctrl+N")) openDlg(D_NEW);
        if (item("Open...", "Ctrl+O")) { const std::string p = fileDialog(false, OPEN_FILTER, nullptr, ""); if (!p.empty()) open(p); }
        if (item("Place as Layer...", "Ctrl+Shift+O", !noDoc)) { const std::string p = fileDialog(false, OPEN_FILTER, nullptr, ""); if (!p.empty()) placeAsLayer(p); }
        ImGui::Separator();
        if (item("Save Project", "Ctrl+S", !noDoc)) save();
        if (item("Save Project As...", nullptr, !noDoc)) saveAs(true);
        if (item("Export PNG / JPG...", "Ctrl+Shift+S", !noDoc)) saveAs(false);
        ImGui::Separator();
        if (item("Close", "Ctrl+W", !noDoc || tabs.size() > 1)) askCloseTab(curTab);
        ImGui::Separator();
        if (item("Quit", "Ctrl+Q")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit", !noDoc)) {
        if (item(hist.canUndo() ? ("Undo " + hist.undoNames.back()).c_str() : "Undo", "Ctrl+Z", hist.canUndo())) { hist.doUndo(doc); changed(); selChanged(); }
        if (item(hist.canRedo() ? ("Redo " + hist.redoNames.back()).c_str() : "Redo", "Ctrl+Shift+Z", hist.canRedo())) { hist.doRedo(doc); changed(); selChanged(); }
        ImGui::Separator();
        if (item("Fill with Foreground", "Alt+Backspace")) fillWith(fg, "Fill");
        if (item("Fill with Background", "Ctrl+Backspace")) fillWith(bgc, "Fill");
        if (item("Clear", "Delete")) clearSelection();
        ImGui::Separator();
        if (item("Free Transform...", "Ctrl+T")) openDlg(D_TRANSFORM);
        if (item("Flip Horizontal")) { remember("Flip"); flipLayer(doc, true); changed(); }
        if (item("Flip Vertical")) { remember("Flip"); flipLayer(doc, false); changed(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Image", !noDoc)) {
        if (ImGui::BeginMenu("Adjustments")) {
            if (item("Brightness/Contrast...")) openDlg(D_BC);
            if (item("Levels...", "Ctrl+L")) openDlg(D_LEVELS);
            if (item("Curves...", "Ctrl+M")) openDlg(D_CURVES);
            ImGui::Separator();
            if (item("Hue/Saturation...", "Ctrl+U")) openDlg(D_HS);
            if (item("Black & White...", "Ctrl+Alt+Shift+B")) openDlg(D_BW);
            ImGui::Separator();
            if (item("Invert", "Ctrl+I")) { remember("Invert"); Work o = getWork(doc), r = o; adjInvert(r); putWork(doc, o, r); changed(); }
            if (item("Desaturate", "Ctrl+Shift+U")) { remember("Desaturate"); Work o = getWork(doc), r = o; adjDesaturate(r); putWork(doc, o, r); changed(); }
            if (item("Threshold...")) openDlg(D_THRESH);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (item("Image Size...", "Ctrl+Alt+I")) openDlg(D_IMGSIZE);
        if (item("Canvas Size...", "Ctrl+Alt+C")) openDlg(D_CANVAS);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Layer", !noDoc)) {
        if (item("New Layer", "Ctrl+Shift+N")) { remember("New Layer"); addLayer(doc, emptyLayer(doc, "Layer " + std::to_string(doc.layers.size()))); changed(); }
        if (item("Duplicate Layer", "Ctrl+J")) { remember("Duplicate"); duplicateLayer(doc); changed(); }
        if (item("Delete Layer", nullptr, doc.layers.size() > 1)) { remember("Delete Layer"); deleteLayer(doc); changed(); }
        if (item("Rename Layer...")) openDlg(D_RENAME);
        if (ImGui::BeginMenu("New Adjustment Layer")) { newAdjustmentMenu(); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Layer Colour")) { labelMenu(); ImGui::EndMenu(); }
        ImGui::Separator();
        if (ImGui::BeginMenu("Layer Mask")) {
            if (item("Reveal All", nullptr, !mask)) { remember("Add Mask"); addMask(doc, 0); changed(); }
            if (item("Hide All", nullptr, !mask)) { remember("Add Mask"); addMask(doc, 1); changed(); }
            if (item("Reveal Selection", nullptr, !mask && doc.hasSel())) { remember("Add Mask"); addMask(doc, 2); changed(); }
            if (item("Hide Selection", nullptr, !mask && doc.hasSel())) { remember("Add Mask"); addMask(doc, 2); invertMask(doc); changed(); }
            ImGui::Separator();
            if (item("Edit the Mask", nullptr, mask)) { doc.editMask = true; }
            if (item(doc.cur().maskOn ? "Disable" : "Enable", "Shift+click", mask)) { remember("Toggle Mask"); doc.cur().maskOn = !doc.cur().maskOn; changed(); }
            if (item(showMask ? "Show the Picture" : "Show the Mask", "Alt+click", mask)) { showMask = !showMask; doc.editMask = true; changed(); }
            if (item("Invert", nullptr, mask)) { remember("Invert Mask"); invertMask(doc); changed(); }
            if (item("Selection from Mask", nullptr, mask)) { remember("Load Selection"); selectionFromMask(doc); selChanged(); }
            ImGui::Separator();
            if (item("Apply", nullptr, mask)) { remember("Apply Mask"); applyMask(doc); showMask = false; changed(); }
            if (item("Delete", nullptr, mask)) { remember("Delete Mask"); deleteMask(doc); showMask = false; changed(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Blend Mode")) {
            for (int m = 0; m < BM_Count; ++m) { if (blendGroupStart(m)) ImGui::Separator(); if (ImGui::MenuItem(blendName(m), nullptr, doc.cur().blend == m)) { remember("Blend Mode"); doc.cur().blend = m; changed(); } }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (item("Merge Down", "Ctrl+E", doc.active > 0)) { remember("Merge Down"); mergeDown(doc); changed(); }
        if (item("Merge Visible", "Ctrl+Shift+E")) { remember("Merge Visible"); mergeVisible(doc); changed(); }
        if (item("Flatten Image")) { remember("Flatten"); flatten(doc); changed(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Select", !noDoc)) {
        if (item("All", "Ctrl+A")) { remember("Select All"); selectAll(); }
        if (item("Deselect", "Ctrl+D", doc.hasSel())) { remember("Deselect"); deselect(); }
        if (item("Inverse", "Ctrl+Shift+I")) { remember("Inverse"); invertSel(doc); selChanged(); }
        ImGui::Separator();
        if (item("Subject")) selectSubject(false);
        if (item("Remove Background (as a mask)")) selectSubject(true);
        ImGui::Separator();
        if (item("Feather...", "Shift+F6", doc.hasSel())) openDlg(D_FEATHER);
        if (item("Grow / Shrink...", nullptr, doc.hasSel())) openDlg(D_GROW);
        if (item("Select and Mask...", "Alt+Ctrl+R", doc.hasSel() || doc.cur().hasMask)) openDlg(D_REFINE);
        if (item("Load from Mask / Transparency")) { remember("Load Selection"); selectionFromMask(doc); selChanged(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Filter", !noDoc)) {
        if (ImGui::BeginMenu("Blur")) {
            if (item("Average")) { remember("Average"); Work o = getWork(doc), r = o; filterAverage(doc, r); putWork(doc, o, r); changed(); }
            if (item("Box Blur...")) openDlg(D_BOX);
            if (item("Gaussian Blur...")) openDlg(D_GAUSS);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Noise")) { if (item("Add Noise...")) openDlg(D_NOISE); ImGui::EndMenu(); }
        ImGui::Separator();
        if (ImGui::BeginMenu("Changeling")) { changelingMenu(); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Bathos")) { bathosMenu(); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Mimic")) { mimicMenu(); ImGui::EndMenu(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (item("Fit on Screen", "Ctrl+0")) fit();
        if (item("100%", "Ctrl+1")) zoomAt(1, ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2));
        if (item("Zoom In", "Ctrl++")) zoomAt(zoom * 1.25f, ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2));
        if (item("Zoom Out", "Ctrl+-")) zoomAt(zoom / 1.25f, ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) { if (item("About LayerTack")) openDlg(D_ABOUT); ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}

// ---------- keys (Photoshop's) ----------
static void keys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || dlgOpen) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    auto k = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    auto kr = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
    if (noDoc) {   // nothing open: only New, Open and Quit
        if (ctrl && k(ImGuiKey_N) && !shift) openDlg(D_NEW);
        else if (ctrl && k(ImGuiKey_O) && !shift) { const std::string p = fileDialog(false, OPEN_FILTER, nullptr, ""); if (!p.empty()) open(p); }
        else if (ctrl && k(ImGuiKey_Q)) requestQuit();
        return;
    }
    if (ctrl) {
        if (k(ImGuiKey_Z)) { if (shift) hist.doRedo(doc); else hist.doUndo(doc); changed(); selChanged(); }
        else if (k(ImGuiKey_Y)) { hist.doRedo(doc); changed(); selChanged(); }
        else if (k(ImGuiKey_N)) { if (shift) { remember("New Layer"); addLayer(doc, emptyLayer(doc, "Layer " + std::to_string(doc.layers.size()))); changed(); } else openDlg(D_NEW); }
        else if (k(ImGuiKey_O)) { const std::string p = fileDialog(false, OPEN_FILTER, nullptr, ""); if (!p.empty()) { if (shift) placeAsLayer(p); else open(p); } }
        else if (k(ImGuiKey_S)) { if (shift) saveAs(false); else save(); }
        else if (k(ImGuiKey_Q)) requestQuit();
        else if (k(ImGuiKey_W)) askCloseTab(curTab);
        else if (k(ImGuiKey_Tab)) switchTab((curTab + (shift ? (int)tabs.size() - 1 : 1)) % (int)tabs.size());
        else if (k(ImGuiKey_A)) { remember("Select All"); selectAll(); }
        else if (k(ImGuiKey_D)) { remember("Deselect"); deselect(); }
        else if (k(ImGuiKey_I)) { if (shift) { remember("Inverse"); invertSel(doc); selChanged(); } else if (alt) openDlg(D_IMGSIZE); else { remember("Invert"); Work o = getWork(doc), r = o; adjInvert(r); putWork(doc, o, r); changed(); } }
        else if (k(ImGuiKey_C) && alt) openDlg(D_CANVAS);
        else if (k(ImGuiKey_T)) openDlg(D_TRANSFORM);
        else if (k(ImGuiKey_R) && alt) { if (doc.hasSel() || doc.cur().hasMask) openDlg(D_REFINE); }
        else if (k(ImGuiKey_J)) { remember("Duplicate"); duplicateLayer(doc); changed(); }
        else if (k(ImGuiKey_E)) { if (shift) { remember("Merge Visible"); mergeVisible(doc); } else { remember("Merge Down"); mergeDown(doc); } changed(); }
        else if (k(ImGuiKey_M)) openDlg(D_CURVES);
        else if (k(ImGuiKey_L)) openDlg(D_LEVELS);
        else if (k(ImGuiKey_U)) { if (shift) { remember("Desaturate"); Work o = getWork(doc), r = o; adjDesaturate(r); putWork(doc, o, r); changed(); } else openDlg(D_HS); }
        else if (k(ImGuiKey_B) && alt && shift) openDlg(D_BW);
        else if (k(ImGuiKey_0)) fit();
        else if (k(ImGuiKey_1)) zoomAt(1, ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2));
        else if (k(ImGuiKey_Equal) || k(ImGuiKey_KeypadAdd)) zoomAt(zoom * 1.25f, ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2));
        else if (k(ImGuiKey_Minus) || k(ImGuiKey_KeypadSubtract)) zoomAt(zoom / 1.25f, ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2));
        else if (k(ImGuiKey_Backspace)) fillWith(bgc, "Fill");
        return;
    }
    if (alt && k(ImGuiKey_Backspace)) { fillWith(fg, "Fill"); return; }
    if (shift && k(ImGuiKey_F6)) { openDlg(D_FEATHER); return; }
    if (k(ImGuiKey_Delete) || k(ImGuiKey_Backspace)) { clearSelection(); return; }
    if (k(ImGuiKey_Enter) && poly.size() >= 6) { finishShape(shapePolygon(doc, poly), selMode, "Polygonal Lasso"); poly.clear(); return; }
    if (k(ImGuiKey_Escape)) { poly.clear(); lasso.clear(); return; }
    if (k(ImGuiKey_V)) tool = T_MOVE;
    if (k(ImGuiKey_M)) tool = tool == T_RECT ? T_ELLIPSE : T_RECT;
    if (k(ImGuiKey_L)) tool = tool == T_LASSO ? T_POLY : T_LASSO;
    if (k(ImGuiKey_W)) tool = T_WAND;
    if (k(ImGuiKey_B)) tool = T_BRUSH;
    if (k(ImGuiKey_E)) tool = T_ERASER;
    if (k(ImGuiKey_G)) tool = T_BUCKET;
    if (k(ImGuiKey_I)) tool = T_DROPPER;
    if (k(ImGuiKey_H)) tool = T_HAND;
    if (k(ImGuiKey_X)) { uint8_t t[4]; memcpy(t, fg, 4); memcpy(fg, bgc, 4); memcpy(bgc, t, 4); }
    if (k(ImGuiKey_D)) { memset(fg, 0, 3); memset(bgc, 255, 3); }
    // [ and ]: size; with Shift: hardness
    float& sz = tool == T_ERASER ? eraserSize : brushSize;
    float& hd = tool == T_ERASER ? eraserHard : brushHard;
    if (kr(ImGuiKey_LeftBracket)) { if (shift) hd = std::max(0.0f, hd - 0.25f); else sz = std::max(1.0f, sz * 0.85f); }
    if (kr(ImGuiKey_RightBracket)) { if (shift) hd = std::min(1.0f, hd + 0.25f); else sz = std::min(1000.0f, sz * 1.18f + 1); }
    if (tool == T_MOVE) {
        const int step = shift ? 10 : 1;
        int dx = 0, dy = 0;
        if (kr(ImGuiKey_LeftArrow)) dx = -step; if (kr(ImGuiKey_RightArrow)) dx = step;
        if (kr(ImGuiKey_UpArrow)) dy = -step; if (kr(ImGuiKey_DownArrow)) dy = step;
        if (dx || dy) { remember("Nudge"); doc.cur().x += dx; doc.cur().y += dy; changed(); }
    }
}

// ---------- the frame ----------
static int scriptDlg = D_NONE;                  // a dialog a --capture script asked for: opened inside the next frame
static int scriptView = 0;                       // and Select and Mask's view, for it

void frame() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (scriptDlg != D_NONE) { openDlg(scriptDlg); if (dlg == D_REFINE) { pi[0] = scriptView; p[0] = 12; } scriptDlg = D_NONE; }
    menus();
    const float top = ImGui::GetFrameHeight(), optH = 38, statusH = 26, leftW = 50, rightW = 310;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(0, top)); ImGui::SetNextWindowSize(ImVec2(W, optH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(IM_COL32(0x21, 0x21, 0x21, 255)));
    ImGui::Begin("options", nullptr, fixed); optionsBar(); ImGui::End();
    ImGui::PopStyleColor();

    ImGui::SetNextWindowPos(ImVec2(0, top + optH)); ImGui::SetNextWindowSize(ImVec2(leftW, H - top - optH - statusH));
    ImGui::Begin("tools", nullptr, fixed); toolsPanel(); ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(W - rightW, top + optH)); ImGui::SetNextWindowSize(ImVec2(rightW, H - top - optH - statusH));
    ImGui::Begin("panels", nullptr, fixed);
    if (noDoc) { ImGui::SeparatorText("LAYERS"); ImGui::TextDisabled("No picture open yet."); }
    else {
        float props = 0;
        if (doc.cur().isAdjust()) {
            const float before = ImGui::GetCursorPosY();
            propertiesPanel();
            props = ImGui::GetCursorPosY() - before;
        }
        layersPanel(std::max(120.0f, H - top - optH - statusH - 130 - props));
    }
    ImGui::End();

    const float tabH = 26;
    ImGui::SetNextWindowPos(ImVec2(leftW, top + optH)); ImGui::SetNextWindowSize(ImVec2(W - leftW - rightW, tabH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::PushStyleColor(ImGuiCol_Tab, col4(C_PANEL));
    ImGui::PushStyleColor(ImGuiCol_TabActive, col4(C_LINE));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, col4(C_DEEP));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, col4(C_LINE));
    ImGui::Begin("doctabs", nullptr, fixed | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTabBar("docs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
        int pick = -1;
        for (int i = 0; i < (int)tabs.size(); ++i) {
            bool open = true;
            const std::string label = tabName(i) + "###doc" + std::to_string(tabs[i].id);
            const ImGuiTabItemFlags f = tabPick == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(label.c_str(), &open, f)) { if (i != curTab && tabPick < 0) pick = i; ImGui::EndTabItem(); }
            if (ImGui::IsItemHovered()) {
                const Doc& d = i == curTab ? doc : tabs[i].doc;
                if (!(i == curTab ? noDoc : tabs[i].noDoc)) ImGui::SetTooltip("%s\n%d x %d", d.path.empty() ? "Not saved yet" : d.path.c_str(), d.w, d.h);
            }
            if (!open) { askCloseTab(i); break; }
        }
        tabPick = -1;
        if (pick >= 0) switchTab(pick);
        ImGui::EndTabBar();
    }
    ImGui::End();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();

    canvasPos = ImVec2(leftW, top + optH + tabH); canvasSize = ImVec2(W - leftW - rightW, H - top - optH - statusH - tabH);
    if (fitNext && canvasSize.x > 50) { fit(); fitNext = false; }
    ImGui::SetNextWindowPos(canvasPos); ImGui::SetNextWindowSize(canvasSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("canvas", nullptr, fixed | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::InvisibleButton("canvasarea", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    if (noDoc) {
        // the empty workspace: a dashed drop area and how to start
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), C_WELL);
        const ImVec2 mid(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2);
        const float bw = std::min(520.0f, canvasSize.x - 60), bh = std::min(260.0f, canvasSize.y - 60);
        const ImVec2 b0(mid.x - bw / 2, mid.y - bh / 2), b1(mid.x + bw / 2, mid.y + bh / 2);
        for (float x = b0.x; x < b1.x; x += 14) { dl->AddLine(ImVec2(x, b0.y), ImVec2(std::min(x + 7, b1.x), b0.y), C_LINE, 2); dl->AddLine(ImVec2(x, b1.y), ImVec2(std::min(x + 7, b1.x), b1.y), C_LINE, 2); }
        for (float y = b0.y; y < b1.y; y += 14) { dl->AddLine(ImVec2(b0.x, y), ImVec2(b0.x, std::min(y + 7, b1.y)), C_LINE, 2); dl->AddLine(ImVec2(b1.x, y), ImVec2(b1.x, std::min(y + 7, b1.y)), C_LINE, 2); }
        auto centred = [&](float y, ImU32 col, const char* s) { const ImVec2 ts = ImGui::CalcTextSize(s); dl->AddText(ImVec2(mid.x - ts.x / 2, y), col, s); };
        centred(mid.y - 24, C_TEXT, "Drop a picture here to start");
        centred(mid.y + 2, C_DIM, "The canvas takes the picture's size.");
        centred(mid.y + 26, C_DIM, "Or File > New (Ctrl+N), File > Open (Ctrl+O)");
    } else {
        canvasInput();
        uploadView();
        drawCanvas();
    }
    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(ImVec2(0, H - statusH)); ImGui::SetNextWindowSize(ImVec2(W, statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::Begin("status", nullptr, fixed);
    const ImVec2 c = toCanvas(io.MousePos);
    if (noDoc) ImGui::TextDisabled("No picture open");
    else ImGui::Text("%.0f%%   %d x %d   %s", zoom * 100, doc.w, doc.h, (c.x >= 0 && c.y >= 0 && c.x < doc.w && c.y < doc.h) ? (std::to_string((int)c.x) + ", " + std::to_string((int)c.y)).c_str() : "");
    ImGui::SameLine(0, 30);
    if (now() < toastUntil) ImGui::TextColored(col4(C_PINK), "%s", toast.c_str());
    else ImGui::TextDisabled("%s", doc.editMask && doc.cur().hasMask ? "Editing the layer MASK" : "");
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar();

    keys();
    drawDialog();
    drawChangeling();
    drawBathos();
    drawMimic();
    updateThumbs();
    maskColours();

    // unsaved changes on the way out
    if (askQuit) { ImGui::OpenPopup("quit?"); askQuit = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("Save the changes to %s first?", tabName(curTab).c_str());
        if (ImGui::Button("Save", ImVec2(90, 0))) { save(); if (!doc.dirty) requestQuit(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(90, 0))) { doc.dirty = false; hist = History(); requestQuit(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (tabClose >= 0) { ImGui::OpenPopup("close?"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("close?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("Save the changes to %s first?", tabName(curTab).c_str());
        if (ImGui::Button("Save", ImVec2(90, 0))) { save(); if (!doc.dirty) closeTab(curTab); tabClose = -1; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", ImVec2(90, 0))) { closeTab(curTab); tabClose = -1; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) { tabClose = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    static bool wasDirty = false;
    if (doc.dirty != wasDirty) { wasDirty = doc.dirty; setTitle(); }
}

bool wantsQuit() { return quit; }
void requestQuit() {
    // the first unsaved picture is shown and asked about; when none is left, LayerTack closes
    if (doc.dirty && hist.canUndo()) { askQuit = true; return; }
    for (int i = 0; i < (int)tabs.size(); ++i)
        if (i != curTab && tabs[i].doc.dirty && tabs[i].hist.canUndo()) { if (switchTab(i)) askQuit = true; return; }
    quit = true;
}

void init(SDL_Window* win, const std::string& prefDir, void (*status)(const char* what, float progress)) {
    auto step = [status](const char* what, float progress) { if (status) status(what, progress); };
    window = win; prefs = prefDir;
    step("Applying the theme...", 0.2f);
    applyStyle();
    {   // Filter > Changeling: find Changeling Studio and ask it for the steps now, so the menu is ready
        step("Looking for Changeling Studio...", 0.35f);
        char* bp = SDL_GetBasePath();
        Changeling::init(prefDir, bp ? bp : "");
        Bathos::init(prefDir, bp ? bp : "");   // Filter > Bathos: Atelier's depth app, when it's on this PC
        MimicPlug::init(prefDir, bp ? bp : "");   // Filter > Mimic: Atelier's face-swap app
        SDL_free(bp);
        step(Changeling::studioDir().empty() ? "Changeling Studio isn't on this PC" : "Asking Changeling Studio for its filters...", 0.5f);
        Changeling::loadSteps();
    }
    step("Getting the canvas ready...", 0.6f);
    const uint8_t chk[16] = {0x66, 0x66, 0x66, 255, 0x88, 0x88, 0x88, 255, 0x88, 0x88, 0x88, 255, 0x66, 0x66, 0x66, 255};
    checkerTex = makeTex(2, 2, chk, true, true);
    doc = newDoc(1600, 1000, true);
    fresh();
    noDoc = true;                                // the blank stays hidden until a picture comes (or File > New)
    setTitle();
}

// --capture: a scripted session so the README's screenshot shows the real thing
static bool jobRunning() { return !clOut.empty() || !btOut.empty() || !mmRun.out.empty(); }
bool busy() { return jobRunning() || Changeling::stepsLoading(); }

void script(const std::string& what) {
    if (what == "empty") return;                             // the empty start, as it is
    noDoc = false;                                           // the scripts work on the blank document
    if (what == "about") { scriptDlg = D_ABOUT; return; }   // Help > About: the splash art
    if (what.rfind("adjust:", 0) == 0) {
        const int k = atoi(what.c_str() + 7);
        addAdjustmentLayer(doc, k);
        Adjustment& a = doc.cur().adj;
        if (k == ADJ_HUE_SATURATION) { a.v[0] = 120; a.v[1] = 30; }
        else if (k == ADJ_CURVES) { a.curve[0] = {0, 0, 0.25f, 0.15f, 0.75f, 0.85f, 1, 1}; }
        else if (k == ADJ_GRADIENT_MAP) { a.stops = {{0, 0.1f, 0.1f, 0.1f}, {1, 0.878f, 0.251f, 0.541f}}; }
        else if (k == ADJ_BRIGHTNESS_CONTRAST) { a.v[0] = 40; a.v[1] = 30; }
        else if (k == ADJ_VIBRANCE) { a.v[0] = 80; }
        changed();
        return;
    }
    if (what.rfind("refine", 0) == 0) {   // "refine" or "refine:<view>"
        selectSubject(false);
        scriptDlg = D_REFINE;
        scriptView = what.size() > 7 ? atoi(what.c_str() + 7) : 0;
        return;
    }
    if (what.rfind("tab:", 0) == 0) { open(what.substr(4)); return; }
    if (what.rfind("tabback:", 0) == 0) { open(what.substr(8)); switchTab(0); return; }   // and back to the first
    if (what == "bathos") { btSource = 0; btAsk = true; bathosStart(); return; }   // Filter > Bathos > Depth Map on the picture
    if (what.rfind("mimic:", 0) == 0) { mmOpt.faces = {what.substr(6)}; mmSource = 0; mmAsk = true; mimicStart(); return; }
    // "changeling:Chronos age=0.8": that Changeling on the whole picture, as Filter > Changeling runs it
    if (what.rfind("changeling:", 0) == 0) {
        while (Changeling::stepsLoading()) SDL_Delay(50);
        std::string rest = what.substr(11), app = rest.substr(0, rest.find(' '));
        auto& st = Changeling::steps();
        for (size_t i = 0; i < st.size(); ++i) if (st[i].app == app) clStep = (int)i;
        if (clStep < 0) { say("No Changeling " + app + ": " + Changeling::stepsError()); return; }
        for (size_t at = rest.find(' '); at != std::string::npos;) {
            const size_t next = rest.find(' ', at + 1);
            const std::string kv = rest.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1);
            const std::string k = kv.substr(0, kv.find('=')), v = kv.find('=') == std::string::npos ? "" : kv.substr(kv.find('=') + 1);
            for (auto& o : st[clStep].options) if (o.key == k) {
                if (o.kind == "number") o.number = (float)atof(v.c_str());
                else if (o.kind == "check") o.check = v == "on" || v == "1" || v == "true";
                else if (o.kind == "choice") { for (size_t c = 0; c < o.choices.size(); ++c) if (o.choices[c] == v) o.choice = (int)c; }
                else o.file = v;
            }
            at = next;
        }
        clSource = 0; clAsk = true;
        changelingStart();
        return;
    }
    if (what == "demo") {
        // the picture gets a mask from its subject, a pink Multiply layer goes over it, a selection is drawn
        if (doc.layers.size() == 1 && doc.path.empty()) return;
        duplicateLayer(doc);
        doc.layers[0].visible = false;
        selectSubject(true);
        doc.layers[0].visible = true;
        addLayer(doc, emptyLayer(doc, "Pink"));
        const uint8_t pink[4] = {224, 64, 138, 255};
        fillSelection(doc, pink, 1.0f, BM_Normal);
        doc.cur().blend = BM_Multiply; doc.cur().opacity = 0.55f;
        addMask(doc, 0);
        Work o = getWork(doc), r = o;
        for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) { float* q = r.at(x, y); q[0] = q[1] = q[2] = std::min(1.0f, (float)x / r.w * 1.6f); }
        putWork(doc, o, r);
        doc.editMask = true;
        combineSel(doc, shapeEllipse(doc, doc.w / 8, doc.h / 8, doc.w / 3, doc.h / 2), SEL_NEW);
        changed(); selChanged();
        tool = T_BRUSH;
    }
    if (what == "ops") {
        FILE* tl = fopen("layertack_timing.txt", "w");
        auto t0 = std::chrono::steady_clock::now();
        auto tick = [&](const char* n) { auto t1 = std::chrono::steady_clock::now(); if (tl) fprintf(tl, "%-12s %6.0f ms\n", n, std::chrono::duration<double, std::milli>(t1 - t0).count()); t0 = t1; };
        // brush with soft and hard edges, blurs, curves, black & white, noise, bucket, lasso, resize: no crashes
        addLayer(doc, emptyLayer(doc, "Strokes"));
        const uint8_t pink[3] = {224, 64, 138}, white[3] = {255, 255, 255};
        for (int k = 0; k < 2; ++k) {
            Stroke st; strokeBegin(doc, st);
            for (int i = 0; i <= 40; ++i) strokeTo(doc, st, doc.w * (0.15f + 0.7f * i / 40), doc.h * (0.3f + 0.25f * k) + std::sin(i * 0.3f) * 40, 60, k ? 1.0f : 0.0f, 1.0f, k ? white : pink, false, i == 0);
            strokeEnd(st);
        }
        tick("strokes");
        combineSel(doc, shapePolygon(doc, {doc.w * 0.1f, doc.h * 0.7f, doc.w * 0.5f, doc.h * 0.62f, doc.w * 0.42f, doc.h * 0.95f, doc.w * 0.12f, doc.h * 0.9f}), SEL_NEW);
        featherSel(doc, 6);
        tick("polygon+feather");
        doc.active = 0;
        { Work o = getWork(doc), r = o; filterGaussianBlur(r, 12); putWork(doc, o, r); }
        tick("gauss12");
        doc.sel.clear();
        { Work o = getWork(doc), r = o; const float w6[6] = {40, 60, 40, 60, 20, 80}, t[3] = {0, 0, 0}; uint8_t lut[4][256];
          std::vector<float> xs = {0, 0.25f, 0.75f, 1}, ys = {0, 0.15f, 0.9f, 1}, idx = {0, 1};
          curveLUT(xs, ys, lut[0]); for (int c = 1; c < 4; ++c) curveLUT(idx, idx, lut[c]);
          adjCurves(r, lut); filterAddNoise(r, 8, true, true, 7); (void)w6; (void)t; putWork(doc, o, r); }
        tick("curves+noise");
        addLayer(doc, emptyLayer(doc, "Bucket"));
        const uint8_t teal[4] = {40, 180, 170, 255};
        bucketFill(doc, 5, 5, teal, 10, true, false, 1.0f);
        tick("bucket");
        doc.cur().blend = BM_Color; doc.cur().opacity = 0.35f;
        addMask(doc, 0);
        doc.editMask = true;
        { Work o = getWork(doc), r = o; for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) r.at(x, y)[0] = r.at(x, y)[1] = r.at(x, y)[2] = (y > r.h / 2) ? 1.0f : 0.0f; filterGaussianBlur(r, 30); putWork(doc, o, r); }
        tick("maskblur");
        doc.editMask = false;
        { Work o = getWork(doc), r = o; const float w6[6] = {40, 60, 40, 60, 20, 80}, t[3] = {0, 0, 0}; adjBlackWhite(r, w6, false, t); filterBoxBlur(r, 3); putWork(doc, o, r); }
        tick("bw+box");
        resizeImage(doc, doc.w * 3 / 4, doc.h * 3 / 4);
        doc.active = (int)doc.layers.size() - 1; while (doc.active > 0 && doc.cur().name != "Strokes") doc.active--;
        transformLayer(doc, 0.8f, 0.8f, 25.0f);
        { Work o = getWork(doc), r = o; adjVibrance(r, 60); putWork(doc, o, r); }
        tick("resize");
        combineSel(doc, shapeWand(doc, doc.w / 2, doc.h / 2, 40, true, true), SEL_NEW);
        tick("wand");
        if (tl) fclose(tl);
        changed(); selChanged(); fitNext = true;
    }
}

} // namespace App
