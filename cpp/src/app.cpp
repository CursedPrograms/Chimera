// Chimera - the window: menus, the tool options bar, the tools, the canvas, colours and layers, the dialogs.
// The usual image editor layout, in the Changeling greys with the Cursed pink.
#include "app.h"
#include "blend.h"
#include "refine.h"
#include "fx.h"
#include "bathos.h"
#include "mimicplug.h"
#include "inoculantplug.h"
#include "changeling.h"
#include "segment.h"
#include "quicksel.h"
#include "splash.h"
#include "toolbar.h"
#include "magnetic.h"
#include "util.h"
#include "inpaint.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
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
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_PINK = IM_COL32(0x00, 0x7b, 0xb7, 255),
                   C_DEEP = IM_COL32(0x00, 0x51, 0x84, 255), C_WELL = IM_COL32(0x14, 0x14, 0x14, 255);

static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// Script artifacts stay beside the executable so hooks work from any current folder.
static std::filesystem::path scriptPath(const char* name) {
    char* base = SDL_GetBasePath();
    const std::filesystem::path path = (base ? std::filesystem::u8path(base) : std::filesystem::current_path()) / name;
    if (base) SDL_free(base);
    return path;
}

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
// No picture yet: Chimera starts empty and waits for one to be dropped (or File > New / Open). The first picture
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
static void magneticClear();

enum Tool { T_MOVE, T_RECT, T_ELLIPSE, T_LASSO, T_POLY, T_MAGNETIC, T_WAND, T_BRUSH, T_ERASER, T_BUCKET, T_DROPPER, T_HAND, T_ROTATE,
            T_PENCIL, T_ZOOM, T_ROW, T_COL, T_CROP, T_PCROP, T_SLICE, T_SLICE_SELECT, T_FRAME, T_ARTBOARD, T_GRADIENT, T_MAGIC_ERASER, T_RETOUCH, T_QUICK, T_OBJECT, T_REDEYE, T_HEAL, T_SHAPE, T_MARK, T_TYPE, T_PEN, T_COUNT,
            T_NONE = T_COUNT };   // T_NONE: a tool on the strip Chimera doesn't have yet
static int tool = T_BRUSH;
static uint8_t fg[4] = {0, 0, 0, 255}, bgc[4] = {255, 255, 255, 255};   // black on white, as PhotoCraft (D)
static float brushSize = 40, brushHard = 0.8f, brushOpacity = 1.0f;
static float eraserSize = 40, eraserHard = 0.8f, eraserOpacity = 1.0f;
static float pencilSize = 4, pencilOpacity = 1.0f;
// the retouching brushes (Blur, Sharpen, Smudge, Dodge, Burn, Sponge, Clone Stamp, Background Eraser): one size and
// hardness for them all, each its own strength; Dodge/Burn's range, the Sponge's mode, the Clone Stamp's source
static float rtSize = 60, rtHard = 0.5f, rtStrength[RT_COUNT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
// Pattern Stamp's patterns (built in, and Edit > Define Pattern), the History Brush's source, and the others' settings
static std::vector<std::pair<std::string, Image>> patterns;
static int patternPick = 0;
static int customShapePick = 0;

static float replaceTol = 0.3f; static int replaceMode = 2;
static float mixWet = 0.5f, mixLoad = 0.5f, mixMix = 0.5f;
static int artStyle = 0; static float artArea = 50;
static int rtRange = 1; static bool spongeDesat = false, cloneSet = false, cloneAligned = true, cloneHaveOffset = false;
static float cloneSrcX = 0, cloneSrcY = 0, cloneDX = 0, cloneDY = 0;
static int gradToolStyle = 0; static bool gradToolReverse = false; static float gradToolOpacity = 1.0f;
static bool cropPending = false, cropDelete = true; static float cropR[4] = {0, 0, 0, 0};
// after PhotoCraft (crates/ui-egui/src/tools/crop_shield.rs CropShield)
static bool cropShowArea = true, cropShieldEnabled = true, cropShieldAuto = true;
static bool cropContentAware=false;
static bool cropShieldMatchCanvas = true;
static float cropShieldColor[3] = {0.102f,0.102f,0.102f};
static float cropShieldOpacity = 75.0f;
// after PhotoCraft (crates/ui-egui/src/tools/crop_presets.rs and crop_size.rs)
static int cropPreset = 0;
static char cropWidth[40] = "", cropHeight[40] = "", cropResolution[40] = "";
static bool cropResolutionCm = false;
// after PhotoCraft (crates/ui-egui/src/tools/crop_straighten.rs)
static bool straightenArmed=false, straightenLine=false, straightenFit=false;
static bool cropClassicMode=false, cropAutoCenter=true;
static bool cropMoveImage=false;
static int cropConsumedShortcutKey=0;
static ImVec2 cropMoveScreenStart,cropMoveOrigin;
static ImVec2 cropResizeCenterScreen;
static float cropMoveRotation=0;
static void syncCropView();
static void toggleCropMode(){cropClassicMode=!cropClassicMode;syncCropView();}
static ImVec2 straightenStart,straightenEnd;
struct CropPreset { std::string name,width,height,resolution; bool cm=false; };
static std::vector<CropPreset> cropUserPresets;
static int cropPresetChoice=0;
static int cropSavedChoice=0;
static bool cropNewDialog=false,cropDeleteDialog=false;
static char cropNewName[256]="";
static int cropDeleteChoice=-1;
static void cropSizeTarget(int& w,int& h,float& ppi);
static int cropDragPart = -1; static float cropBase[4] = {}; static ImVec2 cropDragStart;
static float cropAngle = 0, cropRotateStart = 0;
static bool cropFrameDismissed=false;
using CropSnapshot=std::array<float,6>;
static std::vector<CropSnapshot> cropUndo,cropRedo;
static CropSnapshot cropPressSnapshot{};static bool cropPressSnapshotValid=false;
// after PhotoCraft (crates/ui-egui/src/tools/crop_overlay.rs CropOverlay)
static int cropOverlay = 0, cropOverlayOrientation = 0, cropOverlayShow = 1;
static void pcropSet(const float q[8]);
static void applyPerspectiveCrop();
static bool pcropPending = false, pcropCustomSize = false; static float pcropQ[8] = {0};
static std::vector<ImVec2> pcropClicks; static int pcropDragPart = -1, pcropOutW = 0, pcropOutH = 0;
static ImVec2 pcropMoveStart; static float pcropMoveQ[8] = {0};
static int sliceActive = -1;
static int artboardActive = -1, artboardWidth = 750, artboardHeight = 1334, artboardBackground = 0;
static std::vector<int> layerSelection;
static int layerRangeAnchor = -1, activeGroupId = 0;
static bool artboardMoving = false;
static int artboardMoveDX = 0, artboardMoveDY = 0;
// Keep multi-selection contiguous when Shift-clicking because groups own a range in the flat stack.
static void selectLayer(int index) {
    layerSelection.erase(std::remove_if(layerSelection.begin(), layerSelection.end(), [](int i) { return i < 0 || i >= (int)doc.layers.size(); }), layerSelection.end());
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && layerRangeAnchor >= 0) {
        layerSelection.clear();
        for (int i = std::min(index, layerRangeAnchor); i <= std::max(index, layerRangeAnchor); ++i) layerSelection.push_back(i);
    } else if (io.KeyCtrl) {
        auto it = std::find(layerSelection.begin(), layerSelection.end(), index);
        if (it == layerSelection.end()) layerSelection.push_back(index); else if (layerSelection.size() > 1) layerSelection.erase(it);
        layerRangeAnchor = index;
    } else {
        layerSelection = {index};
        layerRangeAnchor = index;
    }
    doc.active = index;
    activeGroupId = 0;
}
static Rect sliceStartRect;
static bool sliceDidMove = false;
static bool frameEllipse = false;
static float quickSize = 30; static std::vector<float> quickPts;     // the Quick Selection brush and its stroke
static float magneticWidth = 10, magneticContrast = 10, magneticFrequency = 57;
static std::vector<Magnetic::Point> magneticPts, magneticTrail, magneticLive;
static std::vector<size_t> magneticAnchors; static std::unique_ptr<Magnetic::Tracer> magneticTracer;
static int magneticSelMode = SEL_NEW;
static float pupilSize = 50, redDarken = 50;
static int selMode = SEL_NEW; static float selFeather = 0;
static int tolerance = 32; static bool contiguous = true, sampleAll = false; static float bucketOpacity = 1.0f;

// view: canvas pixel (x, y) is at screen origin + (x, y) * zoom
static float zoom = 1.0f, viewRotation = 0.0f; static ImVec2 origin(0, 0); static ImVec2 canvasPos, canvasSize; static bool fitNext = true;
static void syncCropView(){if(tool==T_CROP&&cropPending)viewRotation=cropClassicMode?0.0f:-cropAngle*57.2957795f;}
// after PhotoCraft (crates/ui-egui/src/tools/crop_shield.rs keys): H and P belong to the
// crop only for an edited frame outside a drag and without tool/menu modifiers.
int cropOptionKeyAction(bool pending,bool defaultFrame,bool dragging,bool shift,bool alt,bool ctrl,bool superKey,int key,bool& classicMode,bool& showArea){
    if(!pending||defaultFrame||dragging||shift||alt||ctrl||superKey)return 0;
    if(key==(int)ImGuiKey_H){if(!classicMode)showArea=!showArea;return 1;}
    if(key==(int)ImGuiKey_P){classicMode=!classicMode;return 2;}
    return 0;
}
bool cropShieldShowsOutside(bool showArea,bool classicMode){return showArea||classicMode;}
std::array<std::string,3> cropFrontImageFields(int width,int height,float dpi,bool resolutionCm){
    auto number=[](float value){char text[40];snprintf(text,sizeof(text),"%.2f",value);std::string out=text;
        while(!out.empty()&&out.back()=='0')out.pop_back();if(!out.empty()&&out.back()=='.')out.pop_back();return out;};
    if(width<=0||height<=0||!std::isfinite(dpi)||dpi<=0)return {"","",""};
    return {std::to_string(width)+" px",std::to_string(height)+" px",number(resolutionCm?dpi/2.54f:dpi)};
}
bool cropFrontImageKeyAllowed(bool cropTool,bool dragging,bool textInput,bool shift,bool alt,bool ctrl,bool superKey){
    return cropTool&&!dragging&&!textInput&&!shift&&!alt&&!ctrl&&!superKey;
}
bool cropRestoreFrameState(bool& pending,bool& dismissed,std::array<float,4>& rect,float width,float height){
    if(pending||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return false;
    rect={0,0,width,height};pending=true;dismissed=false;return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_ui.rs push_press_step and step): a completed
// frame gesture owns its own Undo/Redo step and never changes the document history.
bool cropHistoryRecord(std::vector<CropSnapshot>& undo,std::vector<CropSnapshot>& redo,const CropSnapshot& before,const CropSnapshot& after){
    if(before==after)return false;undo.push_back(before);redo.clear();return true;
}
bool cropHistoryStep(std::vector<CropSnapshot>& undo,std::vector<CropSnapshot>& redo,bool isRedo,CropSnapshot& current){
    auto& from=isRedo?redo:undo;auto& to=isRedo?undo:redo;if(from.empty())return false;
    const CropSnapshot next=from.back();from.pop_back();to.push_back(current);current=next;return true;
}
static CropSnapshot cropSnapshot(){return {cropR[0],cropR[1],cropR[2],cropR[3],cropAngle,cropPending?1.0f:0.0f};}
static ImVec2 toScreen(float x,float y);
static void centerCropAtScreen(ImVec2 screen);
static void cropApplyHistorySnapshot(const CropSnapshot& s){
    const ImVec2 boxScreen=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);
    std::copy(s.begin(),s.begin()+4,cropR);cropAngle=s[4];cropPending=s[5]!=0;straightenLine=false;cropMoveImage=false;cropDragPart=-1;syncCropView();
    if(!cropClassicMode&&s[5]!=0)centerCropAtScreen(boxScreen);fitNext=true;
}
static bool cropHistoryActive(){return tool==T_CROP&&cropPending&&(!cropUndo.empty()||!cropRedo.empty());}
static bool cropHistoryMove(bool redo){
    CropSnapshot current=cropSnapshot();if(!cropHistoryStep(cropUndo,cropRedo,redo,current))return false;
    cropApplyHistorySnapshot(current);return true;
}
static void cropClearHistory(){cropUndo.clear();cropRedo.clear();cropPressSnapshotValid=false;}
static bool cropRestoreFrame(){std::array<float,4> rect={cropR[0],cropR[1],cropR[2],cropR[3]};
    if(!cropRestoreFrameState(cropPending,cropFrameDismissed,rect,(float)doc.w,(float)doc.h))return false;
    std::copy(rect.begin(),rect.end(),cropR);cropAngle=0;syncCropView();return true;
}
static void centerCropAtScreen(ImVec2 screen){
    const float cx=(cropR[0]+cropR[2])*.5f,cy=(cropR[1]+cropR[3])*.5f;
    const float dx=screen.x-canvasPos.x-canvasSize.x*.5f,dy=screen.y-canvasPos.y-canvasSize.y*.5f;
    const float a=viewRotation*.017453292519943295f,cs=std::cos(a),sn=std::sin(a);
    origin=ImVec2(cs*dx+sn*dy+canvasSize.x*.5f-cx*zoom,-sn*dx+cs*dy+canvasSize.y*.5f-cy*zoom);
}
// Keep an Auto Center handle drag's frame at its press-time screen position while Space pans the view.
static void cropCompensatePan(ImVec2 originDelta){
    if(tool!=T_CROP||cropClassicMode||!cropAutoCenter||cropDragPart<0||cropDragPart>=8||zoom<=0)return;
    const float dx=-originDelta.x/zoom,dy=-originDelta.y/zoom;
    for(int i=0;i<4;++i){cropR[i]+=(i%2==0)?dx:dy;cropBase[i]+=(i%2==0)?dx:dy;}
    cropDragStart.x+=dx;cropDragStart.y+=dy;
}

// interaction
static bool dragging = false, panning = false, cropSpacePanning = false; static ImVec2 dragStart, dragNow, panStart, originStart;
static float rotateStartAngle = 0.0f, rotateStartPointer = 0.0f;
static int moveStartX = 0, moveStartY = 0;
static int moveGroupId = 0;
static bool moveTextLayer = false;
static float moveTextStartX = 0, moveTextStartY = 0;
static std::vector<float> lasso, poly;
// the shape tools: new shapes take these (and the foreground colour); with a shape layer active, the options bar
// changes that layer's shape instead
static Shape shapeNext = [] { Shape s; s.radius = 0; s.sides = 5; s.inner = 0.5f; s.weight = 4; s.strokeWidth = 3; return s; }();
// the healing tools (Spot Healing, Remove, Healing Brush, Patch, Content-Aware Move: one Chimera tool, five modes)
enum { HM_SPOT, HM_REMOVE, HM_HEALING, HM_PATCH, HM_MOVE };
static float healSizes[3] = {40, 90, 60};
static std::vector<float> healPts;                       // the stroke being painted
static bool healSrcSet = false, healAligned = true, healHaveOffset = false, patchDrag = false;
static float healSrcX = 0, healSrcY = 0, healDX = 0, healDY = 0;
static Stroke stroke;
static std::vector<int> ants; static int antsVersion = -1;

// thumbnails
static std::vector<GLuint> thumbs, mthumbs; static int thumbVersion = -1; static double thumbAt = 0;

static double now() { return SDL_GetTicks() / 1000.0; }
static void syncTool();
static void say(const std::string& s) { toast = s; toastUntil = now() + 5; }
static void changed(Rect r = {0, 0, 1 << 30, 1 << 30}) { magneticClear(); dirty = dirty.unite(r.clip(doc.w, doc.h)); docVersion++; doc.dirty = true; }
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
    magneticClear();
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

// a Chimera project (.chimera; .ltk is the same format)
static bool isProject(const std::string& p) {
    auto ends = [&](const char* e) { const size_t n = strlen(e); return p.size() >= n && p.compare(p.size() - n, n, e) == 0; };
    return ends(".chimera") || ends(".ltk");
}
static const char* OPEN_FILTER = "Pictures and projects|*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.chimera;*.ltk|All files|*.*|";

static void setTitle() {
    std::string name = doc.path.empty() ? "Untitled" : u8path(doc.path).filename().u8string();
    SDL_SetWindowTitle(window, ("Chimera - " + name + (doc.dirty ? " *" : "")).c_str());
}

static void fresh() {
    noDoc = false;
    cropFrameDismissed=false;cropClearHistory();cropAngle=0;viewRotation=0;
    cropPending=tool==T_CROP;if(cropPending){cropR[0]=cropR[1]=0;cropR[2]=(float)doc.w;cropR[3]=(float)doc.h;}
    layerSelection.clear(); layerRangeAnchor = -1; activeGroupId = 0;
    hist = History();
    showMask = false; dirty = {0, 0, doc.w, doc.h}; texW = -1;
    docVersion++; selVersion++; fitNext = true;
    doc.dirty = false;
    if (!doc.historyState) doc.historyState = std::make_shared<const std::vector<Layer>>(doc.layers);   // the History Brush's source
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

static void addPlacedImage(Image image, const std::string& name);
// Load an image and route it through the same placement path used by Frame Tool.
static void placeAsLayer(const std::string& path) {
    Image image; std::string err;
    if (!loadImage(path, image, err)) { say(err); return; }
    addPlacedImage(std::move(image), u8path(path).stem().u8string());
}

void dropFile(const std::string& path) {
    // nothing open yet: the picture opens, and the canvas takes its size; later pictures come in as layers
    if (noDoc || doc.layers.empty() || isProject(path)) open(path);
    else placeAsLayer(path);
}

static void saveAs(bool project, bool icon = false) {
    std::string start = doc.path;
    if (!start.empty()) start = u8path(start).replace_extension(project ? ".chimera" : icon ? ".ico" : ".png").u8string();
    const std::string p = project ? fileDialog(true, "Chimera project|*.chimera|", "chimera", start)
                        : icon ? fileDialog(true, "Icon (16 to 256 px)|*.ico|", "ico", start)
                               : fileDialog(true, "PNG|*.png|JPG|*.jpg|Icon (16 to 256 px)|*.ico|", "png", start);
    if (p.empty()) return;
    std::string err;
    if (!(project ? saveProject(doc, p, err) : saveImage(doc, p, err))) { say(err); return; }
    if (project) { doc.path = p; doc.dirty = false; }
    say("Saved " + u8path(p).filename().u8string());
    setTitle();
}

static void save() {
    if (!doc.path.empty() && isProject(doc.path)) {
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
enum Dlg { D_NONE, D_NEW, D_IMGSIZE, D_CANVAS, D_BC, D_HS, D_BW, D_CURVES, D_LEVELS, D_GAUSS, D_BOX, D_NOISE, D_FEATHER, D_GROW, D_THRESH, D_RENAME, D_ABOUT, D_TRANSFORM, D_REFINE,
          D_UNSHARP, D_HIGHPASS, D_TWIRL, D_PINCH, D_SPHERIZE, D_WAVE, D_RIPPLE, D_POLAR, D_MOSAIC, D_EMBOSS,   // previewed filters
          D_MOTION, D_RADIAL, D_MEDIAN, D_CRYSTAL, D_POINTIL,
          D_EXPAND, D_CONTRACT, D_SMOOTH, D_BORDER };
static int dlg = D_NONE; static bool dlgOpen = false;
static Doc dlgBackup; static Work dlgOrig; static bool dlgPreview = true, dlgDirty = false;
static float p[8]; static bool pb[3]; static int pi[4];
static float bwW[6] = {40, 60, 40, 60, 20, 80}; static float bwTint[3] = {0.88f, 0.69f, 0.5f};
static std::vector<float> cx[4], cy[4]; static int curveCh = 0, curveDrag = -1;
static char renameBuf[128];
static bool scriptOk = false;
static bool dlgOpening = false;                    // a --capture script presses OK
// Select and Mask: what it started from, the picture, the answer so far
static std::vector<float> rfSrc, rfRgb, rfOut; static RefineCache rfCache;

static bool needsTarget(int d) { return (d >= D_BC && d <= D_THRESH && d != D_FEATHER && d != D_GROW) || (d >= D_UNSHARP && d <= D_POINTIL); }

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
    case D_UNSHARP: p[0] = 100; p[1] = 2; p[2] = 0; break;
    case D_HIGHPASS: p[0] = 10; break;
    case D_TWIRL: p[0] = 50; break;
    case D_PINCH: p[0] = 50; break;
    case D_SPHERIZE: p[0] = 100; pi[0] = 0; break;
    case D_WAVE: pi[0] = 5; p[0] = 10; p[1] = 120; p[2] = 5; p[3] = 35; pi[1] = 0; pi[2] = 1; break;
    case D_RIPPLE: p[0] = 100; pi[0] = 1; break;
    case D_POLAR: pi[0] = 0; break;
    case D_MOSAIC: p[0] = 10; break;
    case D_EMBOSS: p[0] = 135; p[1] = 3; p[2] = 100; break;
    case D_MOTION: p[0] = 0; p[1] = 10; break;
    case D_RADIAL: p[0] = 10; pi[0] = 0; pi[1] = 0; p[1] = 0.5f; p[2] = 0.5f; break;
    case D_MEDIAN: pi[0] = 2; break;
    case D_CRYSTAL: p[0] = 10; pi[0] = 1; break;
    case D_POINTIL: p[0] = 6; pi[0] = 1; break;
    case D_EXPAND: case D_CONTRACT: p[0] = 4; break;
    case D_SMOOTH: pi[0] = 4; break;
    case D_BORDER: p[0] = 8; break;
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
    dlgOpening = true;                           // opened by drawDialog: a popup opened inside a menu gets the menu's ID and never shows
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
        case D_UNSHARP: fxUnsharp(r, p[0], p[1], p[2]); break;
        case D_HIGHPASS: fxHighPass(r, p[0]); break;
        case D_TWIRL: fxTwirl(r, fxBounds(doc, r), p[0]); break;
        case D_PINCH: fxPinch(r, fxBounds(doc, r), p[0]); break;
        case D_SPHERIZE: fxSpherize(r, fxBounds(doc, r), p[0], pi[0]); break;
        case D_WAVE: fxWave(r, fxBounds(doc, r), pi[0], p[0], p[1], p[2], p[3], pi[1], (unsigned)pi[2]); break;
        case D_RIPPLE: fxRipple(r, fxBounds(doc, r), p[0], pi[0]); break;
        case D_POLAR: fxPolar(r, fxBounds(doc, r), pi[0] == 0); break;
        case D_MOSAIC: fxMosaic(r, fxBounds(doc, r), p[0]); break;
        case D_EMBOSS: fxEmboss(r, p[0], p[1], p[2]); break;
        case D_MOTION: fxMotionBlur(r, p[0], p[1]); break;
        case D_RADIAL: fxRadialBlur(r, fxBounds(doc, r), p[0], pi[0] == 1, pi[1], p[1], p[2]); break;
        case D_MEDIAN: fxMedian(r, pi[0]); break;
        case D_CRYSTAL: fxCrystallize(r, fxBounds(doc, r), p[0], (unsigned)pi[0]); break;
        case D_POINTIL: fxPointillize(r, fxBounds(doc, r), p[0], (unsigned)pi[0], bgc); break;
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
    if (dlgOpening) { ImGui::OpenPopup("dlg"); dlgOpening = false; }
    const char* titles[] = {"", "New", "Image Size", "Canvas Size", "Brightness/Contrast", "Hue/Saturation", "Black & White", "Curves",
                            "Levels", "Gaussian Blur", "Box Blur", "Add Noise", "Feather Selection", "Grow / Shrink Selection", "Threshold", "Rename Layer", "About Chimera", "Free Transform", "Select and Mask",
                            "Unsharp Mask", "High Pass", "Twirl", "Pinch", "Spherize", "Wave", "Ripple", "Polar Coordinates", "Mosaic", "Emboss",
                            "Motion Blur", "Radial Blur", "Median", "Crystallize", "Pointillize",
                            "Expand Selection", "Contract Selection", "Smooth Selection", "Border Selection"};
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
    case D_UNSHARP:
        ImGui::SliderFloat("Amount (%)", &p[0], 1, 500, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Radius (px)", &p[1], 0.1f, 250, "%.1f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Threshold (levels)", &p[2], 0, 255, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        break;
    case D_HIGHPASS: ImGui::SliderFloat("Radius (px)", &p[0], 0.1f, 250, "%.1f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_TWIRL: ImGui::SliderFloat("Angle", &p[0], -999, 999, "%.0f deg"); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_PINCH: ImGui::SliderFloat("Amount (%)", &p[0], -100, 100, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_SPHERIZE: {
        ImGui::SliderFloat("Amount (%)", &p[0], -100, 100, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        const char* modes[] = {"Normal", "Horizontal only", "Vertical only"};
        ch |= ImGui::Combo("Mode", &pi[0], modes, 3);
        break;
    }
    case D_WAVE: {
        ImGui::SliderInt("Generators", &pi[0], 1, 64); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Wavelength min", &p[0], 1, 998, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Wavelength max", &p[1], 2, 999, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Amplitude min", &p[2], 0, 998, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Amplitude max", &p[3], 1, 999, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        const char* types[] = {"Sine", "Triangle", "Square"};
        ch |= ImGui::Combo("Type", &pi[1], types, 3);
        if (ImGui::Button("Randomize")) { pi[2] = (int)(SDL_GetTicks() * 2654435761u >> 8); ch = true; }
        break;
    }
    case D_RIPPLE: {
        ImGui::SliderFloat("Amount (%)", &p[0], -999, 999, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        const char* sizes[] = {"Small", "Medium", "Large"};
        ch |= ImGui::Combo("Size", &pi[0], sizes, 3);
        break;
    }
    case D_POLAR: ch |= ImGui::RadioButton("Rectangular to Polar", &pi[0], 0); ch |= ImGui::RadioButton("Polar to Rectangular", &pi[0], 1); break;
    case D_MOSAIC: ImGui::SliderFloat("Cell size (px)", &p[0], 2, 200, "%.0f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_EMBOSS:
        ImGui::SliderFloat("Angle", &p[0], -180, 180, "%.0f deg"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Height (px)", &p[1], 1, 10, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Amount (%)", &p[2], 1, 500, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        break;
    case D_MOTION:
        ImGui::SliderFloat("Angle", &p[0], -180, 180, "%.0f deg"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Distance (px)", &p[1], 1, 2000, "%.0f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit();
        break;
    case D_RADIAL: {
        ImGui::SliderFloat("Amount", &p[0], 1, 100, "%.0f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ch |= ImGui::RadioButton("Spin", &pi[0], 0); ImGui::SameLine(); ch |= ImGui::RadioButton("Zoom", &pi[0], 1);
        const char* q[] = {"Draft", "Good", "Best"};
        ch |= ImGui::Combo("Quality", &pi[1], q, 3);
        ImGui::SliderFloat("Centre across", &p[1], 0, 1, "%.2f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Centre down", &p[2], 0, 1, "%.2f"); ch |= ImGui::IsItemDeactivatedAfterEdit();
        break;
    }
    case D_MEDIAN: ImGui::SliderInt("Radius (px)", &pi[0], 1, 100); ch |= ImGui::IsItemDeactivatedAfterEdit(); break;
    case D_CRYSTAL: case D_POINTIL:
        ImGui::SliderFloat("Cell size (px)", &p[0], dlg == D_CRYSTAL ? 3.0f : 3.0f, 300, "%.0f", ImGuiSliderFlags_Logarithmic); ch |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Button("Randomize")) { pi[0] = (int)(SDL_GetTicks() * 2654435761u >> 8); ch = true; }
        if (dlg == D_POINTIL) ImGui::TextDisabled("The dots sit on the background colour.");
        break;
    case D_EXPAND: ImGui::SliderFloat("Expand by (px)", &p[0], 1, 500, "%.0f", ImGuiSliderFlags_Logarithmic); break;
    case D_CONTRACT: ImGui::SliderFloat("Contract by (px)", &p[0], 1, 500, "%.0f", ImGuiSliderFlags_Logarithmic); break;
    case D_SMOOTH: ImGui::SliderInt("Sample radius (px)", &pi[0], 1, 500); break;
    case D_BORDER: ImGui::SliderFloat("Width (px)", &p[0], 1, 200, "%.0f", ImGuiSliderFlags_Logarithmic); break;
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
    case D_ABOUT: {   // the splash, as it shows while Chimera loads
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
    if (ImGui::Button("OK", ImVec2(110, 0)) || (ImGui::IsKeyPressed(ImGuiKey_Enter) && dlg != D_RENAME) || scriptOk) {
        scriptOk = false;
        switch (dlg) {
        case D_NEW: if (pi[0] > 0 && pi[1] > 0 && pi[0] <= 16384 && pi[1] <= 16384) { newTab(); doc = newDoc(pi[0], pi[1], pb[0]); fresh(); setTitle(); } break;
        case D_IMGSIZE: if (pi[0] > 0 && pi[1] > 0 && pi[0] <= 16384 && pi[1] <= 16384) { remember("Image Size"); resizeImage(doc, pi[0], pi[1]); changed(); selChanged(); fitNext = true; } break;
        case D_CANVAS: if (pi[0] > 0 && pi[1] > 0 && pi[0] <= 16384 && pi[1] <= 16384) { remember("Canvas Size"); resizeCanvas(doc, pi[0], pi[1], pi[2], pi[3]); changed(); selChanged(); fitNext = true; } break;
        case D_FEATHER: remember("Feather"); featherSel(doc, p[0]); selChanged(); break;
        case D_GROW: remember(pi[0] > 0 ? "Grow" : "Shrink"); if (pi[0] > 0) selExpand(doc, (float)pi[0]); else selContract(doc, (float)-pi[0]); selChanged(); break;
        case D_EXPAND: remember("Expand"); selExpand(doc, p[0]); selChanged(); break;
        case D_CONTRACT: remember("Contract"); selContract(doc, p[0]); selChanged(); break;
        case D_SMOOTH: remember("Smooth"); selSmooth(doc, pi[0]); selChanged(); break;
        case D_BORDER: remember("Border"); selBorder(doc, p[0]); selChanged(); break;
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

// ---------- File > Save with Inoculant / Open and Cure with Inoculant (Atelier's poison-and-antidote app) ----------
enum { IN_NONE, IN_SAVE, IN_CURE };
static int inMode = IN_NONE;
static bool inAsk = false;
static InoculantPlug::Protect inProtect;
static InoculantPlug::Cure inCure;
static Changeling::Job inJob, inCheck;              // the run, and the inspection of the picture to cure
static std::string inSrc, inOut, inTemp, inErr, inVerdict;
static bool inRunning = false, inChecking = false;
static double inStarted = 0;

static bool inoculantHave() { return !InoculantPlug::cli().empty(); }
static void inoculantSave() { inMode = IN_SAVE; inAsk = true; inErr.clear(); inVerdict.clear(); }
static void inoculantOpen() {
    const std::string p = fileDialog(false, "Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.webp|", nullptr, "");
    if (p.empty()) return;
    inMode = IN_CURE; inAsk = true; inSrc = p; inErr.clear(); inVerdict.clear();
    if (inCheck.start(InoculantPlug::inspectArgs(p), Changeling::workDir())) inChecking = true;
}
// the last thing it said that's worth showing (the similarity, or the verdict)
static std::string inSummary(Changeling::Job& j) {
    const auto ls = j.lines();
    std::string pick;
    for (const auto& l : ls) if (l.find("similarity to you") != std::string::npos || l.find("Looks") == 0 || l.find("Maybe") == 0 || l.find("Likely") == 0 || l.find("faces ") == 0) pick = l;
    return pick.empty() && !ls.empty() ? ls.back() : pick;
}
static void inoculantStart() {
    std::string err;
    if (inMode == IN_SAVE) {
        std::string start = doc.path;
        if (!start.empty()) start = u8path(start).replace_extension(".png").u8string();
        const std::string out = fileDialog(true, "PNG|*.png|JPG|*.jpg|", "png", start);
        if (out.empty()) return;
        Doc d = newDoc(doc.w, doc.h, false);
        composite(doc, d.layers[0].img, {0, 0, doc.w, doc.h}, false);
        Image& img = d.layers[0].img;   // onto white: the cloak is worked out on what people will see
        for (size_t i = 0; i < img.px.size(); i += 4) { const int a = img.px[i + 3]; for (int c = 0; c < 3; ++c) img.px[i + c] = (uint8_t)((img.px[i + c] * a + 255 * (255 - a) + 127) / 255); img.px[i + 3] = 255; }
        inTemp = (u8path(Changeling::workDir()) / "inoculant-in.png").u8string();
        if (!saveImage(d, inTemp, err)) { inErr = "Couldn't save the picture for it: " + err; return; }
        inOut = out;
        if (!inJob.start(InoculantPlug::protectArgs(inProtect, inTemp, out), Changeling::workDir())) { inErr = "Couldn't start inoculant-cli"; return; }
    } else {
        inOut = (u8path(Changeling::workDir()) / (u8path(inSrc).stem().u8string() + " (cured).png")).u8string();
        std::error_code ec; std::filesystem::remove(u8path(inOut), ec);
        if (!inJob.start(InoculantPlug::cureArgs(inCure, inSrc, inOut), Changeling::workDir())) { inErr = "Couldn't start inoculant-cli"; return; }
    }
    inRunning = true; inErr.clear(); inStarted = now();
}
static void inoculantDone() {
    inRunning = false;
    const std::string said = inSummary(inJob);
    std::error_code fe; if (!std::filesystem::is_regular_file(u8path(inOut), fe)) { inErr = said.empty() ? "Inoculant didn't make the picture" : said; return; }
    if (inMode == IN_SAVE) { say("Saved with Inoculant: " + u8path(inOut).filename().u8string() + (said.empty() ? "" : "  (" + said + ")")); }
    else if (open(inOut)) say("Cured with Inoculant: " + u8path(inSrc).filename().u8string());
    ImGui::CloseCurrentPopup();
    inMode = IN_NONE;
}
static void drawInoculant() {
    if (inAsk) { inAsk = false; ImGui::OpenPopup("inoculant"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("inoculant", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    if (inChecking && inCheck.state() != Changeling::Job::RUNNING) { inChecking = false; inVerdict = inSummary(inCheck); }
    if (inRunning) {
        const auto st = inJob.state();
        if (st == Changeling::Job::DONE) inoculantDone();
        else if (st == Changeling::Job::FAILED) { inRunning = false; inErr = inSummary(inJob); if (inErr.empty()) inErr = "It stopped"; }
    }
    if (inMode == IN_NONE) { ImGui::EndPopup(); return; }
    const bool save = inMode == IN_SAVE;
    ImGui::PushStyleColor(ImGuiCol_Text, col4(C_PINK)); ImGui::TextUnformatted(save ? "Save with Inoculant" : "Open and Cure with Inoculant"); ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextDisabled(save ? "a poison against face swaps and face recognition" : "washes off a cloak before it opens");
    ImGui::Separator();
    ImGui::BeginDisabled(inRunning);
    ImGui::PushItemWidth(260);
    if (save) {
        ImGui::SliderFloat("Strength", &inProtect.strength, 1, 16, "%.1f levels");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far each pixel may move. More protects better and shows more.");
        ImGui::SliderInt("Steps", &inProtect.steps, 10, 200);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long it works on the picture: more is stronger at the same strength.");
        ImGui::SliderFloat("Robustness", &inProtect.robustness, 0, 1, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much it trains against being washed off (JPEG, blur, resizing).");
        ImGui::SliderFloat("Texture", &inProtect.texture, 0, 1, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide the change in busy areas (hair, cloth) rather than smooth skin.");
        ImGui::TextDisabled("The whole picture, flattened; it protects the faces in it. PNG keeps the most of it.");
    } else {
        ImGui::TextUnformatted(u8path(inSrc).filename().u8string().c_str());
        if (inChecking) ImGui::TextDisabled("Inspecting it...");
        else if (!inVerdict.empty()) ImGui::TextWrapped("%s", inVerdict.c_str());
        ImGui::SliderInt("JPEG", &inCure.jpeg, 10, 100, inCure.jpeg >= 100 ? "off" : "quality %d");
        ImGui::SliderFloat("Blur", &inCure.blur, 0, 3, inCure.blur <= 0 ? "off" : "%.1f px");
        ImGui::SliderInt("Median", &inCure.median, 0, 3, inCure.median <= 0 ? "off" : "%d px");
        ImGui::SliderInt("Shrink and back", &inCure.resize, 25, 100, inCure.resize >= 100 ? "off" : "%d%%");
        ImGui::SliderFloat("Noise", &inCure.noise, 0, 10, inCure.noise <= 0 ? "off" : "%.1f levels");
        ImGui::TextDisabled("Each washes off more of a cloak and softens the picture a little.");
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    if (inRunning) ImGui::Text("Working... %.0f s (the first run loads the face models)", now() - inStarted);
    if (!inErr.empty()) ImGui::TextColored(col4(C_PINK), "%s", inErr.c_str());
    ImGui::Separator();
    if (inRunning) {
        if (ImGui::Button("Stop", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { inJob.stop(); inRunning = false; inErr = "Stopped"; }
    } else {
        if (ImGui::Button(save ? "Save..." : "Cure and Open", ImVec2(130, 0))) inoculantStart();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { inCheck.stop(); inChecking = false; inMode = IN_NONE; ImGui::CloseCurrentPopup(); }
    }
    ImGui::EndPopup();
}

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
static ImVec2 toScreen(float x, float y) {
    const float dx = origin.x + x * zoom - canvasSize.x * 0.5f;
    const float dy = origin.y + y * zoom - canvasSize.y * 0.5f;
    const float a = viewRotation * 0.017453292519943295f, c = std::cos(a), s = std::sin(a);
    return ImVec2(canvasPos.x + canvasSize.x * 0.5f + c * dx - s * dy,
                  canvasPos.y + canvasSize.y * 0.5f + s * dx + c * dy);
}
static ImVec2 toCanvas(ImVec2 p) {
    const float dx = p.x - canvasPos.x - canvasSize.x * 0.5f;
    const float dy = p.y - canvasPos.y - canvasSize.y * 0.5f;
    const float a = viewRotation * 0.017453292519943295f, c = std::cos(a), s = std::sin(a);
    return ImVec2((c * dx + s * dy + canvasSize.x * 0.5f - origin.x) / zoom,
                  (-s * dx + c * dy + canvasSize.y * 0.5f - origin.y) / zoom);
}
static void fit() {
    const float a = viewRotation * 0.017453292519943295f;
    const float rw = std::fabs(std::cos(a)) * doc.w + std::fabs(std::sin(a)) * doc.h;
    const float rh = std::fabs(std::sin(a)) * doc.w + std::fabs(std::cos(a)) * doc.h;
    zoom = std::min(1.0f, std::min((canvasSize.x - 40) / rw, (canvasSize.y - 40) / rh));
    origin = ImVec2((canvasSize.x - doc.w * zoom) / 2, (canvasSize.y - doc.h * zoom) / 2);
}
static void zoomAt(float nz, ImVec2 screen) {
    nz = std::clamp(nz, 0.02f, 64.0f);
    const ImVec2 c = toCanvas(screen);
    const float dx = screen.x - canvasPos.x - canvasSize.x * 0.5f;
    const float dy = screen.y - canvasPos.y - canvasSize.y * 0.5f;
    const float a = viewRotation * 0.017453292519943295f, cs = std::cos(a), sn = std::sin(a);
    const float localX = cs * dx + sn * dy + canvasSize.x * 0.5f;
    const float localY = -sn * dx + cs * dy + canvasSize.y * 0.5f;
    zoom = nz;
    origin = ImVec2(localX - c.x * zoom, localY - c.y * zoom);
}
static void beginCropImageMove(ImVec2 screen){cropMoveImage=true;cropMoveScreenStart=screen;cropMoveOrigin=origin;cropMoveRotation=viewRotation;}
static void cropImageMoveTo(ImVec2 screen){
    const float a=cropMoveRotation*.017453292519943295f,cs=std::cos(a),sn=std::sin(a),sx=screen.x-cropMoveScreenStart.x,sy=screen.y-cropMoveScreenStart.y;
    float dx=(cs*sx+sn*sy)/std::max(.001f,zoom),dy=(-sn*sx+cs*sy)/std::max(.001f,zoom);
    const float x0=std::min(cropBase[0],cropBase[2]),x1=std::max(cropBase[0],cropBase[2]),y0=std::min(cropBase[1],cropBase[3]),y1=std::max(cropBase[1],cropBase[3]);
    dx=std::clamp(dx,-x0,std::max(0.0f,(float)doc.w-x1));dy=std::clamp(dy,-y0,std::max(0.0f,(float)doc.h-y1));
    cropR[0]=cropBase[0]+dx;cropR[2]=cropBase[2]+dx;cropR[1]=cropBase[1]+dy;cropR[3]=cropBase[3]+dy;
    origin=ImVec2(cropMoveOrigin.x-dx*zoom,cropMoveOrigin.y-dy*zoom);
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_ui.rs): arrow keys nudge the frame in
// screen space, while the default mode keeps the frame fixed and moves the image beneath it.
static void nudgeCrop(float screenX,float screenY,bool large){
    const float step=large?10.0f:1.0f;
    if(!cropClassicMode){
        const ImVec2 center=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);
        std::copy(cropR,cropR+4,cropBase);
        beginCropImageMove(center);
        cropImageMoveTo(ImVec2(center.x+screenX*step,center.y+screenY*step));
        cropMoveImage=false;
    }else{
        const float a=viewRotation*.017453292519943295f,cs=std::cos(a),sn=std::sin(a);
        const float dx=(cs*screenX+sn*screenY)*step/std::max(.01f,zoom);
        const float dy=(-sn*screenX+cs*screenY)*step/std::max(.01f,zoom);
        const float w=cropR[2]-cropR[0],h=cropR[3]-cropR[1];
        const float minX=std::min(cropR[0],cropR[2]),maxX=std::max(cropR[0],cropR[2]);
        const float minY=std::min(cropR[1],cropR[3]),maxY=std::max(cropR[1],cropR[3]);
        const float sx=std::clamp(dx,-minX,(float)doc.w-maxX),sy=std::clamp(dy,-minY,(float)doc.h-maxY);
        cropR[0]+=sx;cropR[2]+=sx;cropR[1]+=sy;cropR[3]+=sy;
    }
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_size.rs): X swaps the frame and output axes.
static void swapCropOrientation(){
    const float cx=(cropR[0]+cropR[2])*.5f,cy=(cropR[1]+cropR[3])*.5f;
    const float hw=(cropR[2]-cropR[0])*.5f,hh=(cropR[3]-cropR[1])*.5f;
    cropR[0]=cx-hh;cropR[2]=cx+hh;cropR[1]=cy-hw;cropR[3]=cy+hw;
    const std::string value=cropWidth;strcpy_s(cropWidth,cropHeight);strcpy_s(cropHeight,value.c_str());
    syncCropView();
    if(!cropClassicMode&&cropAutoCenter)centerCropAtScreen(ImVec2(canvasPos.x+canvasSize.x*.5f,canvasPos.y+canvasSize.y*.5f));
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

static int retouchMode() {
    switch (Toolbar::current()) {
    case Toolbar::SHARPEN: return RT_SHARPEN; case Toolbar::SMUDGE: return RT_SMUDGE; case Toolbar::DODGE: return RT_DODGE;
    case Toolbar::BURN: return RT_BURN; case Toolbar::SPONGE: return spongeDesat ? RT_SPONGE_DESAT : RT_SPONGE_SAT;
    case Toolbar::CLONE_STAMP: return RT_CLONE; case Toolbar::BACKGROUND_ERASER: return RT_BG_ERASE;
    case Toolbar::PATTERN_STAMP: return RT_PATTERN; case Toolbar::HISTORY_BRUSH: return RT_HISTORY; case Toolbar::ART_HISTORY_BRUSH: return RT_ART_HISTORY;
    case Toolbar::COLOR_REPLACEMENT: return RT_REPLACE; case Toolbar::MIXER_BRUSH: return RT_MIXER;
    default: return RT_BLUR;
    }
}
static RetouchOpts retouchOpts() {
    RetouchOpts o; o.mode = retouchMode();
    const int slot = o.mode == RT_SPONGE_DESAT ? RT_SPONGE_SAT : o.mode;
    o.strength = rtStrength[slot]; o.range = rtRange; o.srcDX = cloneDX; o.srcDY = cloneDY; o.tolerance = rtStrength[RT_BG_ERASE];
    if (o.mode == RT_REPLACE) o.tolerance = replaceTol;
    o.fg = fg; o.replaceMode = replaceMode; o.wet = mixWet; o.load = mixLoad; o.mix = mixMix; o.artStyle = artStyle; o.artArea = artArea;
    if (o.mode == RT_PATTERN) {
        if (patterns.empty()) {   // the built-in ones: checks, stripes, dots, bricks
            auto make = [](const char* name, int w, int h, auto f) { Image im(w, h); for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { uint8_t* q = im.at(x, y); const float v = f(x, y); q[0] = q[1] = q[2] = (uint8_t)(v * 255); q[3] = 255; } patterns.push_back({name, im}); };
            make("Checks", 32, 32, [](int x, int y) { return ((x / 16 + y / 16) & 1) ? 0.85f : 0.3f; });
            make("Stripes", 24, 24, [](int x, int y) { return ((x + y) / 6) & 1 ? 0.9f : 0.2f; });
            make("Dots", 20, 20, [](int x, int y) { const float dx = x - 9.5f, dy = y - 9.5f; return dx * dx + dy * dy < 25 ? 0.15f : 0.9f; });
            make("Bricks", 40, 20, [](int x, int y) { const int xs = (y / 10) & 1 ? x + 20 : x; return (y % 10 == 0 || xs % 40 == 0) ? 0.35f : 0.75f; });
        }
        patternPick = std::clamp(patternPick, 0, (int)patterns.size() - 1);
        o.pattern = &patterns[patternPick].second;
    }
    if (o.mode == RT_HISTORY || o.mode == RT_ART_HISTORY) {
        // the source: the active layer as it was when the document opened (or Set Source); a layer made since: as it is
        if (!doc.historyState) doc.historyState = std::make_shared<const std::vector<Layer>>(doc.layers);
        const Layer& L = doc.active < (int)doc.historyState->size() ? (*doc.historyState)[doc.active] : doc.cur();
        o.history = &L.img; o.histX = L.x; o.histY = L.y;
    }
    return o;
}
// what Quick Selection and Object Selection look at: the layer, or everything (Sample All Layers)
static Image sampleImage() {
    Image comp;
    if (sampleAll) { composite(doc, comp, {0, 0, doc.w, doc.h}, false); return comp; }
    comp = Image(doc.w, doc.h);
    const Layer& l = doc.cur();
    for (int y = 0; y < l.img.h; ++y)
        for (int x = 0; x < l.img.w; ++x) {
            const int cx = x + l.x, cy = y + l.y;
            if (cx >= 0 && cy >= 0 && cx < doc.w && cy < doc.h) memcpy(comp.at(cx, cy), l.img.at(x, y), 4);
        }
    return comp;
}
// The tracer reads composite pixels in cached 128 px blocks and builds edge fields only around each live wire.
static Magnetic::Settings magneticSettings() { return {magneticWidth, magneticContrast / 100.0f}; }
static bool magneticActive() { return !magneticPts.empty(); }
static double magneticLength(const std::vector<Magnetic::Point>& p) { double d = 0; for (size_t i = 1; i < p.size(); ++i) d += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y); return d; }
static void magneticClear() { magneticPts.clear(); magneticTrail.clear(); magneticLive.clear(); magneticAnchors.clear(); magneticTracer.reset(); }
static double magneticSpacing(bool held) {
    const double f = std::clamp((double)magneticFrequency, 0.0, 100.0), stored = std::floor(40.0 - 0.35 * f);
    if (held) return stored;
    double screen = 4.0 * stored + 4.0; if (zoom < 1.0f) screen /= 2.0; return screen / std::max((double)zoom, 0.01);
}
static void magneticExtendTrail(Magnetic::Point p) {
    const double gap = std::clamp((double)magneticWidth / 4.0, 1.0, 8.0);
    if (magneticTrail.size() >= 2 && std::hypot(magneticTrail[magneticTrail.size()-2].x-p.x, magneticTrail[magneticTrail.size()-2].y-p.y) < gap) magneticTrail.back() = p;
    else magneticTrail.push_back(p);
}
static bool magneticCut(const std::vector<Magnetic::Point>& p, double len, std::vector<Magnetic::Point>& head, Magnetic::Point& point) {
    if (magneticLength(p) < len + 1.0) return false;
    double acc = 0;
    for (size_t i = 1; i < p.size(); ++i) { const auto a = p[i-1], b = p[i]; head.push_back(a); const double d = std::hypot(b.x-a.x,b.y-a.y); if (acc+d>=len&&d>0) { const double t=(len-acc)/d; point={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};head.push_back(point);return true;}acc+=d; }
    return false;
}
static void magneticUpdate(Magnetic::Point p, bool held, bool autoAnchor) {
    if (!magneticActive() || !magneticTracer) return;
    magneticExtendTrail(p); const auto target = magneticTracer->snap(p, magneticSettings()); const double every = magneticSpacing(held);
    for (int n = 0; n < 64; ++n) {
        const auto from = magneticPts.back(); const auto live = magneticTracer->trace(from, target, magneticTrail, magneticSettings());
        std::vector<Magnetic::Point> head; Magnetic::Point anchor;
        if (autoAnchor && magneticCut(live, every, head, anchor)) {
            magneticPts.insert(magneticPts.end(), head.begin() + std::min<size_t>(1, head.size()), head.end()); magneticAnchors.push_back(magneticPts.size() - 1);
            size_t nearest = 0; double best = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < magneticTrail.size(); ++i) { const double d = std::hypot(magneticTrail[i].x-anchor.x, magneticTrail[i].y-anchor.y); if (d < best) { best=d;nearest=i; } }
            std::vector<Magnetic::Point> rest; rest.push_back(anchor); for (size_t i=nearest+1;i<magneticTrail.size();++i)rest.push_back(magneticTrail[i]); magneticTrail=std::move(rest);
        } else { magneticLive = live; return; }
    }
}
static void magneticFasten() {
    const size_t before = magneticPts.size();
    for (const auto& p : magneticLive) if (magneticPts.empty() || std::hypot(magneticPts.back().x-p.x, magneticPts.back().y-p.y)>1e-9) magneticPts.push_back(p);
    if (magneticPts.size() > before) magneticAnchors.push_back(magneticPts.size()-1);
    if (!magneticPts.empty()) magneticTrail = {magneticPts.back()};
    magneticLive.clear();
}
static void magneticFinish(bool straight = false) {
    if (magneticPts.size() < 3) { magneticClear(); return; }
    if (!straight && magneticTracer) { auto closing = magneticTracer->trace(magneticPts.back(), magneticPts.front(), {}, magneticSettings()); magneticPts.insert(magneticPts.end(), closing.begin()+std::min<size_t>(1,closing.size()), closing.end()); }
    std::vector<float> poly; poly.reserve(magneticPts.size()*2); for (auto p:magneticPts) { poly.push_back((float)p.x);poly.push_back((float)p.y); }
    finishShape(shapePolygon(doc, poly), magneticSelMode, "Magnetic Lasso"); magneticClear();
}
static void magneticRemoveAnchor() {
    if (magneticAnchors.size() <= 1) { magneticClear(); return; }
    magneticAnchors.pop_back(); magneticPts.resize(magneticAnchors.back()+1); magneticLive.clear(); magneticTrail={magneticPts.back()};
}
static void magneticStart(Magnetic::Point p) {
    uploadView(); magneticTracer = std::make_unique<Magnetic::Tracer>(view);
    Magnetic::Point first = magneticTracer->snap(p, magneticSettings());
    magneticPts = {first}; magneticAnchors = {0}; magneticTrail = {p}; magneticLive.clear(); magneticSelMode = modeFromKeys();
}
static void magneticClick(Magnetic::Point p, bool twice) {
    if (!magneticActive()) { magneticStart(p); return; }
    const double closeRadius = 8.0 / std::max((double)zoom, 0.01);
    if (magneticPts.size() > 1 && magneticLength(magneticPts) > 2 * closeRadius && std::hypot(magneticPts.front().x-p.x, magneticPts.front().y-p.y) <= closeRadius) { magneticFinish(); return; }
    magneticUpdate(p, false, false);
    magneticFasten();
    if (twice) magneticFinish();
}
static void quickSelectStroke() {
    if (quickPts.size() < 2) return;
    const std::vector<uint8_t> region = QuickSel::select(sampleImage(), quickPts, quickSize);
    quickPts.clear();
    if (region.empty()) return;
    // the first stroke makes a selection, the next ones add to it (Alt takes away), as in Photoshop
    int mode = modeFromKeys();
    if (mode == SEL_NEW && doc.hasSel()) mode = SEL_ADD;
    finishShape(region, mode, "Quick Selection");
}
// Object Selection: the subject model on just the box drawn round the object
static void objectSelect(float ax, float ay, float bx, float by) {
    const int x0 = std::clamp((int)std::min(ax, bx), 0, doc.w), y0 = std::clamp((int)std::min(ay, by), 0, doc.h);
    const int x1 = std::clamp((int)std::ceil(std::max(ax, bx)), 0, doc.w), y1 = std::clamp((int)std::ceil(std::max(ay, by)), 0, doc.h);
    if (x1 - x0 < 8 || y1 - y0 < 8) return;
    say("Finding the object...");
    const Image all = sampleImage();
    Image crop(x1 - x0, y1 - y0);
    for (int y = 0; y < crop.h; ++y) memcpy(crop.at(0, y), all.at(x0, y + y0), (size_t)crop.w * 4);
    std::vector<uint8_t> m; std::string err;
    if (!segmentSubject(crop, m, err)) { say(err); return; }
    std::vector<uint8_t> region((size_t)doc.w * doc.h, 0);
    for (int y = 0; y < crop.h; ++y) memcpy(&region[(size_t)(y + y0) * doc.w + x0], &m[(size_t)y * crop.w], crop.w);
    finishShape(region, modeFromKeys(), "Object Selection");
}
static void applyCrop() {
    if (!cropPending) return;
    cropFrameDismissed=true;
    cropClearHistory();
    if(std::fabs(cropAngle)>0.001f) {
        const float cx=(cropR[0]+cropR[2])*.5f,cy=(cropR[1]+cropR[3])*.5f,cs=std::cos(cropAngle),sn=std::sin(cropAngle);
        const float x0=std::min(cropR[0],cropR[2]),x1=std::max(cropR[0],cropR[2]),y0=std::min(cropR[1],cropR[3]),y1=std::max(cropR[1],cropR[3]);
        auto turn=[&](float x,float y){const float dx=x-cx,dy=y-cy;return ImVec2(cx+cs*dx-sn*dy,cy+sn*dx+cs*dy);};
        const ImVec2 q[4]={turn(x0,y0),turn(x1,y0),turn(x1,y1),turn(x0,y1)};
        const float quad[8]={q[0].x,q[0].y,q[1].x,q[1].y,q[2].x,q[2].y,q[3].x,q[3].y};
        cropPending=false; cropAngle=0; viewRotation=0; pcropSet(quad); applyPerspectiveCrop(); return;
    }
    Rect r{(int)std::lround(std::min(cropR[0], cropR[2])), (int)std::lround(std::min(cropR[1], cropR[3])), (int)std::lround(std::max(cropR[0], cropR[2])), (int)std::lround(std::max(cropR[1], cropR[3]))};
    cropPending = false; viewRotation=0;
    r = r.clip(doc.w, doc.h);
    if (r.empty()) return;
    int outW=0,outH=0;float outPpi=doc.dpi;cropSizeTarget(outW,outH,outPpi);
    remember("Crop"); cropCanvas(doc, r, cropDelete);
    for (Slice& s : doc.slices) {
        s.rect = {std::max(s.rect.x0, r.x0) - r.x0, std::max(s.rect.y0, r.y0) - r.y0,
                  std::min(s.rect.x1, r.x1) - r.x0, std::min(s.rect.y1, r.y1) - r.y0};
    }
    doc.slices.erase(std::remove_if(doc.slices.begin(), doc.slices.end(), [](const Slice& s) { return s.rect.empty(); }), doc.slices.end());
    if(outW>0&&outH>0){resizeImage(doc,outW,outH);doc.dpi=outPpi;}
    else if(std::strtof(cropResolution,nullptr)>0)doc.dpi=outPpi;
    sliceActive = -1;
    changed(); selChanged(); fitNext = true;
}

// after PhotoCraft (crates/ui-egui/src/tools/crop_ui.rs pointer hit and resize): handles stay
// a fixed screen size, the interior moves the frame, and a press outside starts a new frame.
static void cropBegin(ImVec2 p) {
    cropSpacePanning=false;
    if(!cropPending)cropRestoreFrame();
    cropPressSnapshot=cropSnapshot();cropPressSnapshotValid=true;
    const float x0=std::min(cropR[0],cropR[2]), x1=std::max(cropR[0],cropR[2]);
    const float y0=std::min(cropR[1],cropR[3]), y1=std::max(cropR[1],cropR[3]);
    const float cx=(x0+x1)*.5f, cy=(y0+y1)*.5f, cs=std::cos(cropAngle), sn=std::sin(cropAngle);
    auto turn=[&](ImVec2 v){const float dx=v.x-cx,dy=v.y-cy;return ImVec2(cx+cs*dx-sn*dy,cy+sn*dx+cs*dy);};
    const ImVec2 h[8]={turn({x0,y0}),turn({(x0+x1)*.5f,y0}),turn({x1,y0}),turn({x1,(y0+y1)*.5f}),turn({x1,y1}),turn({(x0+x1)*.5f,y1}),turn({x0,y1}),turn({x0,(y0+y1)*.5f})};
    cropDragPart=-1; const float radius=10.0f/std::max(.01f,zoom);
    for(int i=0;i<8;++i) if(std::hypot(p.x-h[i].x,p.y-h[i].y)<=radius) { cropDragPart=i; break; }
    const float lx=cx+cs*(p.x-cx)+sn*(p.y-cy), ly=cy-sn*(p.x-cx)+cs*(p.y-cy);
    if(cropDragPart<0 && lx>=x0 && lx<=x1 && ly>=y0 && ly<=y1) cropDragPart=8;
    std::copy(cropR,cropR+4,cropBase); cropDragStart=p;
    if(cropDragPart<0 && cropPending) {
        const float edgeDist=std::min({std::fabs(lx-x0),std::fabs(lx-x1),std::fabs(ly-y0),std::fabs(ly-y1)});
        if(edgeDist>4/std::max(.01f,zoom) && edgeDist<28/std::max(.01f,zoom)) {
            cropDragPart=10; cropRotateStart=std::atan2(p.y-cy,p.x-cx);
        }
    }
    if(cropDragPart<0) { cropDragPart=9; cropR[0]=cropR[2]=p.x; cropR[1]=cropR[3]=p.y; }
    cropResizeCenterScreen=toScreen(cx,cy);
}
static float cropAspect();
static void cropDragTo(ImVec2 p, bool snapAngle=false, bool centerResize=false) {
    if(cropDragPart==10) {
        const float cx=(cropBase[0]+cropBase[2])*.5f,cy=(cropBase[1]+cropBase[3])*.5f;
        cropAngle=std::atan2(p.y-cy,p.x-cx)-cropRotateStart;
        while(cropAngle>3.14159265f)cropAngle-=6.2831853f; while(cropAngle< -3.14159265f)cropAngle+=6.2831853f;
        if(snapAngle) cropAngle=std::round(cropAngle/(15.0f*0.01745329252f))*(15.0f*0.01745329252f);
        syncCropView();
        return;
    }
    const float dx=p.x-cropDragStart.x, dy=p.y-cropDragStart.y;
    float x0=cropBase[0],y0=cropBase[1],x1=cropBase[2],y1=cropBase[3];
    if(x0>x1)std::swap(x0,x1); if(y0>y1)std::swap(y0,y1);
    const float bx0=x0,by0=y0,bx1=x1,by1=y1;
    const bool centered=centerResize||(!cropClassicMode&&cropAutoCenter);
    if(cropDragPart==9) { cropR[2]=p.x; cropR[3]=p.y; return; }
    if(cropDragPart==8) { const float w=x1-x0,h=y1-y0; x0=std::clamp(x0+dx,0.0f,std::max(0.0f,(float)doc.w-w)); y0=std::clamp(y0+dy,0.0f,std::max(0.0f,(float)doc.h-h)); x1=x0+w;y1=y0+h; }
    else {
        if(cropDragPart==0||cropDragPart==6||cropDragPart==7)x0=std::clamp(x0+dx,0.0f,x1-1);
        if(cropDragPart==1||cropDragPart==2||cropDragPart==3){}
        if(cropDragPart==2||cropDragPart==3||cropDragPart==4)x1=std::clamp(x1+dx,x0+1,(float)doc.w);
        if(cropDragPart==0||cropDragPart==1||cropDragPart==2)y0=std::clamp(y0+dy,0.0f,y1-1);
        if(cropDragPart==4||cropDragPart==5||cropDragPart==6)y1=std::clamp(y1+dy,y0+1,(float)doc.h);
        if(centered){
            if(cropDragPart==0||cropDragPart==6||cropDragPart==7)x1=std::clamp(bx1-(x0-bx0),x0+1.0f,(float)doc.w);
            else if(cropDragPart==2||cropDragPart==3||cropDragPart==4)x0=std::clamp(bx0-(x1-bx1),0.0f,x1-1.0f);
            if(cropDragPart==0||cropDragPart==1||cropDragPart==2)y1=std::clamp(by1-(y0-by0),y0+1.0f,(float)doc.h);
            else if(cropDragPart==4||cropDragPart==5||cropDragPart==6)y0=std::clamp(by0-(y1-by1),0.0f,y1-1.0f);
        }
    }
    float ratio=cropAspect();
    if(ratio<=0&&snapAngle&&bx1>bx0&&by1>by0)ratio=(bx1-bx0)/(by1-by0);
    if(cropDragPart>=0&&cropDragPart<8&&ratio>0&&std::isfinite(ratio)){
        float w=x1-x0,h=y1-y0;
        const bool horizontal=cropDragPart==3||cropDragPart==7;
        const bool vertical=cropDragPart==1||cropDragPart==5;
        const bool widthDrives=horizontal||(!vertical&&std::fabs((w-(bx1-bx0))/std::max(1.0f,bx1-bx0))>=std::fabs((h-(by1-by0))/std::max(1.0f,by1-by0)));
        if(widthDrives)h=w/ratio;else w=h*ratio;
        const float scale=std::min(1.0f,std::min((float)doc.w/std::max(1.0f,w),(float)doc.h/std::max(1.0f,h)));w*=scale;h*=scale;
        if(centered){const float cx=(bx0+bx1)*.5f,cy=(by0+by1)*.5f;x0=cx-w*.5f;x1=cx+w*.5f;y0=cy-h*.5f;y1=cy+h*.5f;}
        else {
            if(cropDragPart==0||cropDragPart==6||cropDragPart==7){x1=bx1;x0=x1-w;}else if(cropDragPart==2||cropDragPart==3||cropDragPart==4){x0=bx0;x1=x0+w;}else{x0=(bx0+bx1-w)*.5f;x1=x0+w;}
            if(cropDragPart==0||cropDragPart==1||cropDragPart==2){y1=by1;y0=y1-h;}else if(cropDragPart==4||cropDragPart==5||cropDragPart==6){y0=by0;y1=y0+h;}else{y0=(by0+by1-h)*.5f;y1=y0+h;}
        }
        if(x0<0){x1-=x0;x0=0;}if(x1>doc.w){x0-=x1-doc.w;x1=(float)doc.w;}
        if(y0<0){y1-=y0;y0=0;}if(y1>doc.h){y0-=y1-doc.h;y1=(float)doc.h;}
    }
    cropR[0]=x0;cropR[1]=y0;cropR[2]=x1;cropR[3]=y1;
    if(cropDragPart>=0&&cropDragPart<8&&!cropClassicMode&&cropAutoCenter)centerCropAtScreen(cropResizeCenterScreen);
}
static float cropLengthPx(const char* text, float dpi) {
    char* end=nullptr; const float v=std::strtof(text,&end); if(end==text||!std::isfinite(v)||v<=0)return 0;
    while(*end==' ')++end;
    if(!_stricmp(end,"in")||!_stricmp(end,"inch")||!_stricmp(end,"inches"))return v*dpi;
    if(!_stricmp(end,"cm"))return v*dpi/2.54f;
    if(!_stricmp(end,"mm"))return v*dpi/25.4f;
    return v;
}
static float cropAspect() {
    if(cropPreset==2)return doc.h>0?(float)doc.w/doc.h:0;
    if(cropPreset==1||(cropPreset>=8&&cropPreset<=15)){float ppi=doc.dpi;char* e=nullptr;const float r=std::strtof(cropResolution,&e);if(e!=cropResolution&&r>0)ppi=r*(cropResolutionCm?2.54f:1.0f);return cropLengthPx(cropWidth,ppi)/std::max(1.0f,cropLengthPx(cropHeight,ppi));}
    static const float ratios[]={0,0,0,1,4.0f/5,5.0f/7,2.0f/3,16.0f/9};
    if(cropPreset>=3&&cropPreset<8)return ratios[cropPreset];
    return 0;
}
static void fitCropAspect() {
    if(!cropPending)cropRestoreFrame();
    const float a=cropAspect(); if(a<=0||!cropPending)return;
    const float cx=(cropR[0]+cropR[2])*.5f,cy=(cropR[1]+cropR[3])*.5f;
    float w=std::fabs(cropR[2]-cropR[0]),h=std::fabs(cropR[3]-cropR[1]);
    if(w/std::max(1.0f,h)>a)w=h*a;else h=w/a;
    if(w>doc.w){w=(float)doc.w;h=w/a;}if(h>doc.h){h=(float)doc.h;w=h*a;}
    cropR[0]=std::clamp(cx-w*.5f,0.0f,std::max(0.0f,(float)doc.w-w));cropR[1]=std::clamp(cy-h*.5f,0.0f,std::max(0.0f,(float)doc.h-h));cropR[2]=cropR[0]+w;cropR[3]=cropR[1]+h;
}
// after PhotoCraft (crates/ui-egui/src/tools/crop_size.rs front_image): use the active
// document's pixel dimensions and express resolution in the unit the user already chose.
static void cropFrontImage(){
    const auto fields=cropFrontImageFields(doc.w,doc.h,doc.dpi,cropResolutionCm);
    if(fields[0].empty())return;cropPreset=1;strcpy_s(cropWidth,fields[0].c_str());strcpy_s(cropHeight,fields[1].c_str());strcpy_s(cropResolution,fields[2].c_str());fitCropAspect();
}
static void cropSizeTarget(int& w,int& h,float& ppi) {
    w=h=0; ppi=doc.dpi;
    char* end=nullptr; const float rv=std::strtof(cropResolution,&end);
    if(end!=cropResolution&&std::isfinite(rv)&&rv>0)ppi=rv*(cropResolutionCm?2.54f:1.0f);
    if(cropPreset!=1 && (cropPreset<8||cropPreset>15))return;
    const float tw=cropLengthPx(cropWidth,ppi),th=cropLengthPx(cropHeight,ppi);
    if(tw>=1&&th>=1&&tw<=300000&&th<=300000){w=(int)std::lround(tw);h=(int)std::lround(th);}
}
static std::filesystem::path cropPresetFile(){return std::filesystem::path(prefs)/"crop-presets.txt";}
static void cropLoadUserPresets(){cropUserPresets.clear();std::ifstream f(cropPresetFile());CropPreset p;while(f>>std::quoted(p.name)>>std::quoted(p.width)>>std::quoted(p.height)>>std::quoted(p.resolution)>>p.cm){if(!p.name.empty())cropUserPresets.push_back(p);p=CropPreset{};}}
static void cropSaveUserPresets(){std::ofstream f(cropPresetFile(),std::ios::trunc);for(const auto&p:cropUserPresets)f<<std::quoted(p.name)<<' '<<std::quoted(p.width)<<' '<<std::quoted(p.height)<<' '<<std::quoted(p.resolution)<<' '<<p.cm<<'\n';}
static void cropSelectUserPreset(int i){if(i<0||i>=(int)cropUserPresets.size())return;const CropPreset&p=cropUserPresets[i];strcpy_s(cropWidth,p.width.c_str());strcpy_s(cropHeight,p.height.c_str());strcpy_s(cropResolution,p.resolution.c_str());cropResolutionCm=p.cm;cropPreset=1;cropSavedChoice=i;fitCropAspect();}
static bool straightenCrop(ImVec2 a,ImVec2 b,bool fit) {
    const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
    if(!std::isfinite(len)||len*zoom<3.0f)return false;
    float degrees=std::atan2(dy,dx)*57.295779513f;
    if(degrees>45)degrees-=90;else if(degrees< -45)degrees+=90;
    cropAngle=degrees*0.01745329252f;syncCropView();
    if(fit){
        const float w=std::fabs(cropR[2]-cropR[0]),h=std::fabs(cropR[3]-cropR[1]);
        const float cs=std::fabs(std::cos(cropAngle)),sn=std::fabs(std::sin(cropAngle));
        const float scale=std::min((float)doc.w/std::max(1.0f,w*cs+h*sn),(float)doc.h/std::max(1.0f,w*sn+h*cs));
        const float nw=w*scale,nh=h*scale,cx=doc.w*.5f,cy=doc.h*.5f;
        cropR[0]=cx-nw*.5f;cropR[1]=cy-nh*.5f;cropR[2]=cx+nw*.5f;cropR[3]=cy+nh*.5f;
    }
    cropDragPart=-1;return true;
}
// Check that the four corners make a nondegenerate convex quadrilateral.
static bool pcropValid(const float q[8]) {
    double area = 0;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4, k = (i + 2) % 4;
        const double ax = q[j * 2] - q[i * 2], ay = q[j * 2 + 1] - q[i * 2 + 1];
        const double bx = q[k * 2] - q[j * 2], by = q[k * 2 + 1] - q[j * 2 + 1];
        if (ax * by - ay * bx <= 1e-3 * std::hypot(ax, ay) * std::hypot(bx, by))
            return false;
        area += q[i * 2] * q[j * 2 + 1] - q[j * 2] * q[i * 2 + 1];
    }
    return std::isfinite(area) && area >= 2.0;
}
// Solve the projective map from a unit rectangle to the selected corners.
static bool perspectiveHomography(const float q[8], double out[8]) {
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double u = i == 1 || i == 2 ? 1.0 : 0.0, v = i >= 2 ? 1.0 : 0.0;
        const double x = q[i * 2], y = q[i * 2 + 1];
        a[i * 2][0] = u;
        a[i * 2][1] = v;
        a[i * 2][2] = 1;
        a[i * 2][6] = -u * x;
        a[i * 2][7] = -v * x;
        a[i * 2][8] = x;
        a[i * 2 + 1][3] = u;
        a[i * 2 + 1][4] = v;
        a[i * 2 + 1][5] = 1;
        a[i * 2 + 1][6] = -u * y;
        a[i * 2 + 1][7] = -v * y;
        a[i * 2 + 1][8] = y;
    }
    for (int c = 0; c < 8; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 8; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[pivot][c]))
                pivot = r;
        if (std::fabs(a[pivot][c]) < 1e-10)
            return false;
        if (pivot != c)
            for (int j = c; j < 9; ++j)
                std::swap(a[c][j], a[pivot][j]);
        const double div = a[c][c];
        for (int j = c; j < 9; ++j)
            a[c][j] /= div;
        for (int r = 0; r < 8; ++r)
            if (r != c) {
                const double f = a[r][c];
                for (int j = c; j < 9; ++j)
                    a[r][j] -= f * a[c][j];
            }
    }
    for (int i = 0; i < 8; ++i)
        out[i] = a[i][8];
    return true;
}
// Store a crop quadrilateral and choose output dimensions from its edge lengths.
static void pcropSet(const float q[8]) {
    memcpy(pcropQ, q, sizeof pcropQ);
    pcropPending = true;
    pcropCustomSize = false;
    auto len = [&](int a, int b) { return std::hypot(q[a] - q[b], q[a + 1] - q[b + 1]); };
    pcropOutW = std::clamp((int)std::lround((len(0, 2) + len(6, 4)) * 0.5), 1, 16384);
    pcropOutH = std::clamp((int)std::lround((len(0, 6) + len(2, 4)) * 0.5), 1, 16384);
}
// Test whether a canvas point falls inside the current crop quadrilateral.
static bool pcropInside(ImVec2 p) {
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        if ((pcropQ[j * 2] - pcropQ[i * 2]) * (p.y - pcropQ[i * 2 + 1]) -
                (pcropQ[j * 2 + 1] - pcropQ[i * 2 + 1]) * (p.x - pcropQ[i * 2]) <
            0)
            return false;
    }
    return true;
}
// Put four clicked corners into clockwise order and start the preview.
static void pcropOrder(std::vector<ImVec2> points) {
    ImVec2 centre(0, 0);
    for (auto p : points) {
        centre.x += p.x / 4;
        centre.y += p.y / 4;
    }
    std::sort(points.begin(), points.end(), [&](ImVec2 a, ImVec2 b) {
        return std::atan2(a.y - centre.y, a.x - centre.x) < std::atan2(b.y - centre.y, b.x - centre.x);
    });
    auto first =
        std::min_element(points.begin(), points.end(), [](ImVec2 a, ImVec2 b) { return a.x + a.y < b.x + b.y; });
    std::rotate(points.begin(), first, points.end());
    const float q[8] = {points[0].x, points[0].y, points[1].x, points[1].y,
                        points[2].x, points[2].y, points[3].x, points[3].y};
    pcropSet(q);
    pcropClicks.clear();
}
// Clear the current crop preview and any incomplete corner set.
static void pcropCancel() {
    pcropPending = false;
    pcropClicks.clear();
    pcropDragPart = -1;
    pcropCustomSize = false;
}
// Invert the unit-output-to-source homography so document geometry can move forward.
static bool inverseHomography(const double h[8], double inv[9]) {
    const double a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7], i = 1;
    const double A = e * i - f * k, B = c * k - b * i, C = b * f - c * e;
    const double D = f * g - d * i, E = a * i - c * g, F = c * d - a * f;
    const double G = d * k - e * g, H = b * g - a * k, I = a * e - b * d;
    const double det = a * A + b * D + c * G;
    if (!std::isfinite(det) || std::fabs(det) < 1e-12)
        return false;
    const double cof[9] = {A, B, C, D, E, F, G, H, I};
    for (int n = 0; n < 9; ++n)
        inv[n] = cof[n] / det;
    return true;
}
// Map one source-canvas point through the inverse crop transform.
static bool mapPerspectivePoint(const double inv[9], int w, int h, double x, double y, ImVec2 &out) {
    const double z = inv[6] * x + inv[7] * y + inv[8];
    if (!std::isfinite(z) || std::fabs(z) < 1e-12)
        return false;
    const double u = (inv[0] * x + inv[1] * y + inv[2]) / z;
    const double v = (inv[3] * x + inv[4] * y + inv[5]) / z;
    if (!std::isfinite(u) || !std::isfinite(v))
        return false;
    out = ImVec2((float)(u * w), (float)(v * h));
    return true;
}
// Sample a layer through the homography with premultiplied alpha to avoid dark fringes.
static void sampleWarpPixel(const Layer &layer, float sx, float sy, uint8_t out[4]) {
    const float fx = sx - layer.x - 0.5f, fy = sy - layer.y - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float tx = fx - x0, ty = fy - y0;
    float alpha = 0, color[3] = {};
    for (int yy = 0; yy < 2; ++yy)
        for (int xx = 0; xx < 2; ++xx) {
            const int x = x0 + xx, y = y0 + yy;
            if (x < 0 || y < 0 || x >= layer.img.w || y >= layer.img.h)
                continue;
            const float weight = (xx ? tx : 1 - tx) * (yy ? ty : 1 - ty);
            const uint8_t *p = layer.img.at(x, y);
            const float a = p[3] / 255.0f;
            alpha += a * weight;
            for (int c = 0; c < 3; ++c)
                color[c] += p[c] / 255.0f * a * weight;
        }
    out[3] = to8(alpha);
    for (int c = 0; c < 3; ++c)
        out[c] = alpha > 1e-8f ? to8(color[c] / alpha) : 0;
}
// Warp a mask with the same inverse map as its layer pixels.
static std::vector<uint8_t> warpLayerMask(const Layer &layer, const double h[8], int w, int ht) {
    std::vector<uint8_t> out;
    if (!layer.hasMask)
        return out;
    out.assign((size_t)w * ht, 0);
    for (int y = 0; y < ht; ++y)
        for (int x = 0; x < w; ++x) {
            const double u = (x + 0.5) / w, v = (y + 0.5) / ht, z = h[6] * u + h[7] * v + 1;
            if (std::fabs(z) < 1e-12)
                continue;
            const float sx = (float)((h[0] * u + h[1] * v + h[2]) / z) - layer.x - 0.5f;
            const float sy = (float)((h[3] * u + h[4] * v + h[5]) / z) - layer.y - 0.5f;
            const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
            const float tx = sx - x0, ty = sy - y0;
            float value = 0;
            for (int yy = 0; yy < 2; ++yy)
                for (int xx = 0; xx < 2; ++xx) {
                    const int x1 = x0 + xx, y1 = y0 + yy;
                    if (x1 < 0 || y1 < 0 || x1 >= layer.img.w || y1 >= layer.img.h)
                        continue;
                    const size_t i = (size_t)y1 * layer.img.w + x1;
                    if (i >= layer.mask.size())
                        continue;
                    value += layer.mask[i] * (xx ? tx : 1 - tx) * (yy ? ty : 1 - ty);
                }
            out[(size_t)y * w + x] = (uint8_t)std::lround(value);
        }
    return out;
}
// Warp one raster layer or its mask while keeping its blend and visibility settings.
static Layer warpPerspectiveLayer(const Doc &sourceDoc, Layer source, const double h[8], int w, int ht,
                                  bool rasterize) {
    if (rasterize)
        renderFill(sourceDoc, source);
    Layer out = source;
    out.img = Image(w, ht);
    out.x = out.y = 0;
    if (source.isAdjust()) {
        out.mask = warpLayerMask(source, h, w, ht);
        return out;
    }
    if (source.isFill() && !rasterize) {
        out.mask = warpLayerMask(source, h, w, ht);
        return out;
    }
    for (int y = 0; y < ht; ++y)
        for (int x = 0; x < w; ++x) {
            const double u = (x + 0.5) / w, v = (y + 0.5) / ht, z = h[6] * u + h[7] * v + 1;
            if (std::fabs(z) < 1e-12)
                continue;
            const float sx = (float)((h[0] * u + h[1] * v + h[2]) / z);
            const float sy = (float)((h[3] * u + h[4] * v + h[5]) / z);
            sampleWarpPixel(source, sx, sy, out.img.at(x, y));
        }
    out.mask = warpLayerMask(source, h, w, ht);
    if (rasterize) {
        out.frame = false;
        out.fill = Fill{};
        out.shape = Shape{};
        out.hasText = false;
    }
    return out;
}
// Warp the image and preserve editable layers and document geometry where possible.
static void applyPerspectiveCrop() {
    if (!pcropPending)
        return;
    if (!pcropValid(pcropQ)) {
        say("The four corners must make a convex quadrilateral");
        return;
    }
    double h[8];
    if (!perspectiveHomography(pcropQ, h)) {
        say("Those corners do not make a usable perspective");
        return;
    }
    const int w = std::clamp(pcropOutW, 1, 16384), ht = std::clamp(pcropOutH, 1, 16384);
    if ((int64_t)w * ht * std::max<size_t>(1, doc.layers.size()) > 100'000'000) {
        say("Perspective Crop output is too large");
        return;
    }
    double inv[9];
    if (!inverseHomography(h, inv)) {
        say("Those corners do not make an invertible perspective");
        return;
    }
    remember("Perspective Crop");
    Doc sourceDoc = doc;
    Doc warpedDoc;
    warpedDoc.w = w;
    warpedDoc.h = ht;
    warpedDoc.active = std::clamp(doc.active, 0, (int)doc.layers.size() - 1);
    bool rasterized = false;
    for (const Layer &source : sourceDoc.layers) {
        const bool vectorLayer = source.isShape() || source.isText();
        rasterized = rasterized || vectorLayer;
        warpedDoc.layers.push_back(warpPerspectiveLayer(sourceDoc, source, h, w, ht, vectorLayer));
    }
    for (Layer &layer : warpedDoc.layers)
        if (layer.isFill() && !layer.isShape() && !layer.isText())
            renderFill(warpedDoc, layer);
    if(cropContentAware) {
        Image merged=sampleImage();std::vector<float> rgb((size_t)w*ht*3,0);std::vector<uint8_t> hole((size_t)w*ht,0);
        for(int y=0;y<ht;y++)for(int x=0;x<w;x++){
            const double u=(x+.5)/w,v=(y+.5)/ht,z=h[6]*u+h[7]*v+1;if(std::fabs(z)<1e-12)continue;
            const float sx=(float)((h[0]*u+h[1]*v+h[2])/z),sy=(float)((h[3]*u+h[4]*v+h[5])/z);
            const size_t i=(size_t)y*w+x;
            if(sx<0||sy<0||sx>=sourceDoc.w||sy>=sourceDoc.h){hole[i]=1;continue;}
            const uint8_t*p=merged.at(std::clamp((int)sx,0,sourceDoc.w-1),std::clamp((int)sy,0,sourceDoc.h-1));
            for(int c=0;c<3;c++)rgb[i*3+c]=p[c]/255.0f;
        }
        if(Inpaint::complete(w,ht,rgb,hole)) {
            Layer fill;fill.name="Content-Aware Crop Corners";fill.img=Image(w,ht);
            for(int y=0;y<ht;y++)for(int x=0;x<w;x++){const size_t i=(size_t)y*w+x;if(!hole[i])continue;uint8_t*p=fill.img.at(x,y);for(int c=0;c<3;c++)p[c]=to8(rgb[i*3+c]);p[3]=255;}
            warpedDoc.layers.insert(warpedDoc.layers.begin(),std::move(fill));++warpedDoc.active;
        }
        cropContentAware=false;
    }
    doc.w = w;
    doc.h = ht;
    doc.layers = std::move(warpedDoc.layers);
    doc.active = warpedDoc.active;
    sliceActive = -1;
    auto mapMarks = [&](std::vector<Doc::Mark> &marks) {
        for (auto &mark : marks) {
            ImVec2 p;
            if (mapPerspectivePoint(inv, w, ht, mark.x, mark.y, p)) {
                mark.x = p.x;
                mark.y = p.y;
            }
        }
    };
    mapMarks(doc.samplers);
    mapMarks(doc.counts);
    mapMarks(doc.notes);
    for (auto &subpath : doc.workPath)
        for (Knot &knot : subpath.knots) {
            ImVec2 p;
            if (mapPerspectivePoint(inv, w, ht, knot.ax, knot.ay, p)) {
                knot.ax = p.x;
                knot.ay = p.y;
            }
            if (mapPerspectivePoint(inv, w, ht, knot.ix, knot.iy, p)) {
                knot.ix = p.x;
                knot.iy = p.y;
            }
            if (mapPerspectivePoint(inv, w, ht, knot.ox, knot.oy, p)) {
                knot.ox = p.x;
                knot.oy = p.y;
            }
        }
    if (doc.hasRuler) {
        ImVec2 a, b;
        if (mapPerspectivePoint(inv, w, ht, doc.ruler[0], doc.ruler[1], a) &&
            mapPerspectivePoint(inv, w, ht, doc.ruler[2], doc.ruler[3], b)) {
            doc.ruler[0] = a.x;
            doc.ruler[1] = a.y;
            doc.ruler[2] = b.x;
            doc.ruler[3] = b.y;
        }
    }
    for (Slice &slice : doc.slices) {
        ImVec2 p[4];
        const double x[4] = {(double)slice.rect.x0, (double)slice.rect.x1, (double)slice.rect.x1,
                             (double)slice.rect.x0};
        const double y[4] = {(double)slice.rect.y0, (double)slice.rect.y0, (double)slice.rect.y1,
                             (double)slice.rect.y1};
        bool valid = true;
        for (int i = 0; i < 4; ++i)
            valid = mapPerspectivePoint(inv, w, ht, x[i], y[i], p[i]) && valid;
        if (valid) {
            slice.rect = {(int)std::floor(std::min({p[0].x, p[1].x, p[2].x, p[3].x})),
                          (int)std::floor(std::min({p[0].y, p[1].y, p[2].y, p[3].y})),
                          (int)std::ceil(std::max({p[0].x, p[1].x, p[2].x, p[3].x})),
                          (int)std::ceil(std::max({p[0].y, p[1].y, p[2].y, p[3].y}))};
            slice.rect = slice.rect.clip(w, ht);
        } else
            slice.rect = {};
    }
    doc.slices.erase(
        std::remove_if(doc.slices.begin(), doc.slices.end(), [](const Slice &s) { return s.rect.empty(); }),
        doc.slices.end());
    doc.sel.clear();
    if (!sourceDoc.sel.empty()) {
        doc.sel.assign((size_t)w * ht, 0);
        for (int y = 0; y < ht; ++y)
            for (int x = 0; x < w; ++x) {
                const double u = (x + 0.5) / w, v = (y + 0.5) / ht, z = h[6] * u + h[7] * v + 1;
                if (std::fabs(z) < 1e-12)
                    continue;
                const int sx = (int)std::floor((h[0] * u + h[1] * v + h[2]) / z);
                const int sy = (int)std::floor((h[3] * u + h[4] * v + h[5]) / z);
                if (sx >= 0 && sy >= 0 && sx < sourceDoc.w && sy < sourceDoc.h)
                    doc.sel[(size_t)y * w + x] = sourceDoc.sel[(size_t)sy * sourceDoc.w + sx];
            }
    }
    pcropCancel();
    changed();
    selChanged();
    fitNext = true;
    say(rasterized ? "Perspective Crop rasterized shape and text layers; other layers were preserved"
                   : "Perspective Crop preserved the layers");
}
// Return free rectangles that tile the canvas around the stored user slices.
static std::vector<Rect> autoSliceRects() {
    std::vector<int> xs = {0, doc.w}, ys = {0, doc.h};
    for (const Slice& s : doc.slices) {
        Rect r = s.rect.clip(doc.w, doc.h);
        if (r.empty()) continue;
        xs.push_back(r.x0); xs.push_back(r.x1); ys.push_back(r.y0); ys.push_back(r.y1);
    }
    std::sort(xs.begin(), xs.end()); xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end()); ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    std::vector<Rect> out;
    for (size_t yi = 1; yi < ys.size(); ++yi) {
        int run = -1;
        for (size_t xi = 0; xi < xs.size(); ++xi) {
            bool free = xi + 1 < xs.size();
            if (free) for (const Slice& s : doc.slices) {
                Rect r = s.rect.clip(doc.w, doc.h);
                if (r.x0 <= xs[xi] && xs[xi + 1] <= r.x1 && r.y0 <= ys[yi - 1] && ys[yi] <= r.y1) { free = false; break; }
            }
            if (free && run < 0) run = (int)xi;
            if ((!free || xi + 1 == xs.size()) && run >= 0) {
                Rect r{xs[run], ys[yi - 1], xs[xi], ys[yi]};
                auto prior = std::find_if(out.rbegin(), out.rend(), [&](const Rect& p) { return p.x0 == r.x0 && p.x1 == r.x1 && p.y1 == r.y0; });
                if (prior != out.rend()) prior->y1 = r.y1;
                else out.push_back(r);
                run = -1;
            }
        }
    }
    return out;
}
// Add one clipped user slice and select it for editing.
static bool createSlice(Rect r) {
    r = r.clip(doc.w, doc.h);
    if (r.empty())
        return false;
    int id = 1;
    for (const Slice &s : doc.slices)
        id = std::max(id, s.id + 1);
    remember("Create Slice");
    doc.slices.push_back({id, r, "Slice " + std::to_string(id)});
    sliceActive = (int)doc.slices.size() - 1;
    changed();
    return true;
}
// Create a vector placeholder whose alpha also clips the image placed above it.
static bool createFrame(Rect rect) {
    rect = rect.clip(doc.w, doc.h);
    if (rect.x1 - rect.x0 < 8 || rect.y1 - rect.y0 < 8)
        return false;
    Shape shape;
    shape.kind = frameEllipse ? SHAPE_ELLIPSE : SHAPE_RECT;
    shape.x0 = (float)rect.x0;
    shape.y0 = (float)rect.y0;
    shape.x1 = (float)rect.x1;
    shape.y1 = (float)rect.y1;
    shape.strokeOn = true;
    shape.strokeWidth = 3;
    shape.stroke[0] = 0.0f;
    shape.stroke[1] = 0.482f;
    shape.stroke[2] = 0.718f;
    Fill placeholder;
    placeholder.kind = FILL_SOLID;
    placeholder.colour[0] = placeholder.colour[1] = placeholder.colour[2] = 0.16f;
    int number = 1;
    for (const Layer &layer : doc.layers)
        if (layer.frame)
            ++number;
    remember("Create Frame");
    addShapeLayer(doc, shape, placeholder);
    doc.cur().frame = true;
    doc.cur().name = "Frame " + std::string(shapeName(shape.kind)) + " " + std::to_string(number);
    changed();
    return true;
}
// Create an artboard as a regular group with a rectangle fill at its base.
static bool createArtboard(Rect rect) {
    rect = rect.clip(doc.w, doc.h);
    if (rect.empty()) return false;
    int number = 1;
    for (const Artboard& a : doc.artboards) if (a.name.rfind("Artboard ", 0) == 0) ++number;
    remember("New Artboard");
    int id = 1; for (const LayerGroup& g : doc.groups) id = std::max(id, g.id + 1);
    LayerGroup group; group.id = id; group.name = "Artboard " + std::to_string(number); group.expanded = true; group.hasMask = true;
    group.mask.assign((size_t)doc.w * doc.h, 0);
    for (int y = std::max(0, rect.y0); y < std::min(doc.h, rect.y1); ++y)
        std::fill(group.mask.begin() + (size_t)y * doc.w + std::max(0, rect.x0), group.mask.begin() + (size_t)y * doc.w + std::min(doc.w, rect.x1), (uint8_t)255);
    doc.groups.push_back(group);
    Artboard board; board.rect = rect; board.name = group.name; board.background = artboardBackground; board.groupId = id;
    doc.artboards.push_back(board);
    Layer bg = emptyLayer(doc, "Background"); bg.groupId = id;
    bg.fill.kind = FILL_SOLID;
    if (artboardBackground == 0) bg.fill.colour[0] = bg.fill.colour[1] = bg.fill.colour[2] = 1;
    else if (artboardBackground == 1) bg.fill.colour[0] = bg.fill.colour[1] = bg.fill.colour[2] = 0;
    else if (artboardBackground == 2) { bg.fill.colour[0] = 0; bg.fill.colour[1] = 123.0f / 255.0f; bg.fill.colour[2] = 183.0f / 255.0f; }
    bg.shape.kind = SHAPE_RECT; bg.shape.x0 = (float)rect.x0; bg.shape.y0 = (float)rect.y0; bg.shape.x1 = (float)rect.x1; bg.shape.y1 = (float)rect.y1;
    if (!doc.layers.empty() && doc.cur().groupId) bg.groupId = doc.cur().groupId;
    renderFill(doc, bg);
    doc.layers.push_back(std::move(bg)); doc.active = (int)doc.layers.size() - 1;
    doc.layers.back().groupId = id;
    artboardActive = (int)doc.artboards.size() - 1;
    changed();
    return true;
}
// Place an image into the active frame without tying later frame edits to image resampling.
static void addPlacedImage(Image image, const std::string &name) {
    const bool intoFrame = !doc.layers.empty() && doc.cur().frame && doc.cur().isShape();
    Layer layer;
    layer.name = name;
    if (intoFrame) {
        const Shape &frame = doc.cur().shape;
        const int x0 = (int)std::floor(std::min(frame.x0, frame.x1)),
                  y0 = (int)std::floor(std::min(frame.y0, frame.y1));
        const int fw = std::max(1, (int)std::ceil(std::fabs(frame.x1 - frame.x0)));
        const int fh = std::max(1, (int)std::ceil(std::fabs(frame.y1 - frame.y0)));
        const double scale = std::max((double)fw / image.w, (double)fh / image.h);
        const int sw = std::clamp((int)std::lround(image.w * scale), 1, 16384);
        const int sh = std::clamp((int)std::lround(image.h * scale), 1, 16384);
        if ((int64_t)sw * sh > 100'000'000) {
            say("That image is too large to place in this frame");
            return;
        }
        Doc resized = newDoc(image.w, image.h, false);
        resized.layers[0].img = std::move(image);
        resizeImage(resized, sw, sh);
        layer.img = std::move(resized.layers[0].img);
        layer.x = x0 + (fw - sw) / 2;
        layer.y = y0 + (fh - sh) / 2;
        layer.clip = true;
    } else {
        layer.x = (doc.w - image.w) / 2;
        layer.y = (doc.h - image.h) / 2;
        layer.img = std::move(image);
    }
    remember("Place");
    addLayer(doc, std::move(layer));
    changed();
    say(intoFrame ? "Image placed and clipped to the frame" : "Image placed as a layer");
}
// Replace Windows-forbidden filename characters and remove trailing dots or spaces.
static std::string safeSliceFilename(const std::string& input) {
    std::string name = input;
    for (char& c : name) if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "slice";
    const std::string upper = [&]() { std::string s = name; for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }();
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
        (upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) && upper[3] >= '1' && upper[3] <= '9')) name = "_" + name;
    return name;
}
// Write each user slice and free canvas rectangle as a standalone PNG.
static bool writeSlices(const std::filesystem::path& folder, size_t& written, std::string& err) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) { err = "Could not create the slice export folder"; return false; }
    std::vector<std::pair<Rect, std::string>> slices;
    for (size_t i = 0; i < doc.slices.size(); ++i) {
        const Slice& s = doc.slices[i];
        Rect r = s.rect.clip(doc.w, doc.h);
        if (!r.empty()) slices.push_back({r, s.name.empty() ? "slice_" + std::to_string(i + 1) : s.name});
    }
    for (Rect r : autoSliceRects()) slices.push_back({r, "auto_" + std::to_string(slices.size() + 1)});
    Image all(doc.w, doc.h);
    composite(doc, all, {0, 0, doc.w, doc.h}, false);
    std::vector<std::string> usedNames;
    for (const auto& slice : slices) {
        const Rect r = slice.first;
        Image part(r.x1 - r.x0, r.y1 - r.y0);
        for (int y = 0; y < part.h; ++y) memcpy(part.at(0, y), all.at(r.x0, r.y0 + y), (size_t)part.w * 4);
        Doc output = newDoc(part.w, part.h, false);
        output.layers[0].img = std::move(part);
        std::string err;
        std::string name;
        for (unsigned int suffix = 1;; ++suffix) {
            std::string candidate = safeSliceFilename(slice.second);
            if (suffix > 1) candidate += "_" + std::to_string(suffix);
            if (std::find(usedNames.begin(), usedNames.end(), candidate) == usedNames.end()) { name = std::move(candidate); break; }
        }
        usedNames.push_back(name);
        if (!saveImage(output, (folder / std::filesystem::u8path(name + ".png")).u8string(), err)) return false;
        ++written;
    }
    return true;
}
// Ask where the sibling export folder should be created, then write all slices.
static void exportSlices() {
    if (noDoc) return;
    const std::string start = doc.path.empty() ? "slices.png" : doc.path;
    const std::string chosen = fileDialog(true, "PNG|*.png|", "png", start);
    if (chosen.empty()) return;
    const std::filesystem::path chosenPath = std::filesystem::u8path(chosen);
    const std::filesystem::path folder = chosenPath.parent_path() / (chosenPath.stem().string() + "_slices");
    size_t written = 0;
    std::string err;
    if (!writeSlices(folder, written, err)) { say(err); return; }
    say("Exported " + std::to_string(written) + " slices");
}
// Export each artboard group into a sibling folder as its own PNG.
static void exportArtboards() {
    if (noDoc || doc.artboards.empty()) return;
    const std::string start = doc.path.empty() ? "artboards.png" : doc.path;
    const std::string chosen = fileDialog(true, "PNG|*.png|", "png", start);
    if (chosen.empty()) return;
    const std::filesystem::path selected = std::filesystem::u8path(chosen);
    const std::filesystem::path folder = selected.parent_path() / (selected.stem().string() + "_artboards");
    std::error_code ec; std::filesystem::create_directories(folder, ec);
    if (ec) { say("Could not create the artboard export folder"); return; }
    std::vector<std::string> used;
    for (const Artboard& board : doc.artboards) {
        Image pixels;
        if (!renderArtboard(doc, board.groupId, pixels)) { say("An artboard has no layer group to export"); return; }
        std::string name = safeSliceFilename(board.name), base = name;
        for (unsigned suffix = 2; std::find(used.begin(), used.end(), name) != used.end(); ++suffix) name = base + "_" + std::to_string(suffix);
        used.push_back(name);
        Doc output = newDoc(pixels.w, pixels.h, false); output.layers[0].img = std::move(pixels);
        std::string err;
        if (!saveImage(output, (folder / std::filesystem::u8path(name + ".png")).u8string(), err)) { say(err); return; }
    }
    say("Exported " + std::to_string(doc.artboards.size()) + " artboards");
}
// ---------- the Pen tools (PhotoCraft's paths): Pen, Freeform Pen, Curvature Pen, Add / Delete Anchor, Convert
// Point, Path Selection and Direct Selection, on the document's work path ----------
enum { PN_PEN, PN_FREE, PN_CURVE, PN_ADD, PN_DEL, PN_CONVERT, PN_PATHSEL, PN_DIRECT };
static int penMode() {
    switch (Toolbar::current()) {
    case Toolbar::FREEFORM_PEN: return PN_FREE; case Toolbar::CURVATURE_PEN: return PN_CURVE; case Toolbar::ADD_ANCHOR: return PN_ADD;
    case Toolbar::DELETE_ANCHOR: return PN_DEL; case Toolbar::CONVERT_POINT: return PN_CONVERT; case Toolbar::PATH_SELECTION: return PN_PATHSEL;
    case Toolbar::DIRECT_SELECTION: return PN_DIRECT; default: return PN_PEN;
    }
}
static int penSub = -1;                 // the subpath being drawn (open), -1 none
static int penGrabSub = -1, penGrabKnot = -1, penGrabPart = 0;   // dragging: 0 anchor, 1 in handle, 2 out handle, 3 the whole subpath
static std::vector<float> penFree;      // the Freeform Pen's points as it's dragged
static float penStrokeWidth = 4;
// a knot's handles made smooth from its neighbours (Catmull-Rom: the Curvature Pen's way)
static void smoothKnot(Subpath& sp, int i) {
    const int n = (int)sp.knots.size();
    Knot& k = sp.knots[i];
    const bool hasPrev = sp.closed || i > 0, hasNext = sp.closed || i < n - 1;
    const Knot& a = sp.knots[hasPrev ? (i + n - 1) % n : i];
    const Knot& b = sp.knots[hasNext ? (i + 1) % n : i];
    const float tx = (b.ax - a.ax) / 6, ty = (b.ay - a.ay) / 6;
    k.ix = k.ax - tx; k.iy = k.ay - ty; k.ox = k.ax + tx; k.oy = k.ay + ty; k.smooth = true;
}
static void smoothAll(Subpath& sp) { for (int i = 0; i < (int)sp.knots.size(); ++i) smoothKnot(sp, i); }
// what's under the mouse: an anchor or a handle (part 0/1/2), within px screen pixels
static bool penHit(ImVec2 c, float px, int& sub, int& knot, int& part, bool handles) {
    for (int s2 = (int)doc.workPath.size() - 1; s2 >= 0; --s2)
        for (int k = 0; k < (int)doc.workPath[s2].knots.size(); ++k) {
            const Knot& kn = doc.workPath[s2].knots[k];
            const float pts[3][2] = {{kn.ax, kn.ay}, {kn.ix, kn.iy}, {kn.ox, kn.oy}};
            for (int q = handles ? 2 : 0; q >= 0; --q)
                if (std::hypot(pts[q][0] - c.x, pts[q][1] - c.y) * zoom <= px) { sub = s2; knot = k; part = q; return true; }
        }
    return false;
}
// the nearest point on a segment of the path: its subpath, the segment (from knot i) and t along it
static bool penNearSegment(ImVec2 c, float px, int& sub, int& seg, float& tOut) {
    float best = px / std::max(zoom, 1e-3f);
    bool found = false;
    for (int s2 = 0; s2 < (int)doc.workPath.size(); ++s2) {
        const Subpath& sp = doc.workPath[s2];
        const int n = (int)sp.knots.size(), segs = sp.closed ? n : n - 1;
        for (int i = 0; i < segs; ++i) {
            const Knot& a = sp.knots[i]; const Knot& b = sp.knots[(i + 1) % n];
            for (int k = 0; k <= 40; ++k) {
                const float t = k / 40.0f, u = 1 - t;
                const float x = u * u * u * a.ax + 3 * u * u * t * a.ox + 3 * u * t * t * b.ix + t * t * t * b.ax;
                const float y = u * u * u * a.ay + 3 * u * u * t * a.oy + 3 * u * t * t * b.iy + t * t * t * b.ay;
                const float d = std::hypot(x - c.x, y - c.y);
                if (d < best) { best = d; sub = s2; seg = i; tOut = t; found = true; }
            }
        }
    }
    return found;
}
// a knot put in at t along segment i (de Casteljau: the curve keeps its shape)
static void penSplit(Subpath& sp, int i, float t) {
    const int n = (int)sp.knots.size();
    Knot& a = sp.knots[i]; Knot& b = sp.knots[(i + 1) % n];
    auto L = [t](float p, float q) { return p + (q - p) * t; };
    const float p01x = L(a.ax, a.ox), p01y = L(a.ay, a.oy), p12x = L(a.ox, b.ix), p12y = L(a.oy, b.iy), p23x = L(b.ix, b.ax), p23y = L(b.iy, b.ay);
    const float p012x = L(p01x, p12x), p012y = L(p01y, p12y), p123x = L(p12x, p23x), p123y = L(p12y, p23y);
    Knot m; m.ax = L(p012x, p123x); m.ay = L(p012y, p123y); m.ix = p012x; m.iy = p012y; m.ox = p123x; m.oy = p123y; m.smooth = true;
    a.ox = p01x; a.oy = p01y; b.ix = p23x; b.iy = p23y;
    sp.knots.insert(sp.knots.begin() + i + 1, m);
}
static std::vector<uint8_t> pathSelection() {
    std::vector<float> cov; polygonsCoverage(flattenPath(doc.workPath), doc.w, doc.h, cov);
    std::vector<uint8_t> m(cov.size()); for (size_t i = 0; i < cov.size(); ++i) m[i] = to8(cov[i]);
    return m;
}
static void penFinishSubpath() { penSub = -1; }

// ---------- the Type tools (PhotoCraft's point text; the Type Mask tools make a selection of the letters) ----------
static Text typeNext = [] { Text t; t.size = 48; return t; }();
static std::vector<std::string> recentTypeFonts;
static int recentTypeFontCap = 10;
static int typeLayer = -1;              // the text layer being typed into (-1: none)
static size_t typeStyleSelectionStart=0,typeStyleSelectionEnd=0;
static int typeStyleSelectionLayer=-1;
static bool typeStyleSelectionValid=false;
static int typeReturnToolbar = -1;      // Move-tool double-click returns to Move when editing ends
static Doc typeEditOriginalDoc;
static bool typeEditHasSnapshot=false,typeEditCreated=false,typeEditUndoPushed=false;
static std::string typeEditUndoName="Edit Text";
static bool typeMaskEditing = false;    // a Type Mask tool: typing into typeMask, a selection when done
static Text typeMask;
static char typeBuf[8192] = "";
static bool typeFocus = false;
// after PhotoCraft (crates/engine/src/prefs.rs TypePrefs): these Type options are user preferences.
static bool typeSmartQuotesEnabled = true, typePlaceholderEnabled = true;
static bool typeSelectPlaceholder = false, typePreferencesScriptOpen = false;
static int typeSmartQuoteScriptStage=0;
static bool typeSmartQuoteScriptSelection=false;
static int typeWordNavigationScriptStage=0;
static int typeMultiClickScriptStage=0;
static int typeClickOwner=-1;
static unsigned typeClickRun=0;
static std::filesystem::path typePreferencesFile(){return (prefs.empty()?std::filesystem::current_path():std::filesystem::u8path(prefs))/"type-prefs.txt";}
static bool saveTypePreferences(){
    std::ofstream f(typePreferencesFile(),std::ios::trunc);if(!f)return false;
    f<<"TYPE2 "<<typeSmartQuotesEnabled<<' '<<typePlaceholderEnabled<<' '<<recentTypeFontCap<<' '<<recentTypeFonts.size();
    for(const std::string& path:recentTypeFonts)f<<' '<<std::quoted(path);
    f<<'\n';return (bool)f;
}
static void loadTypePreferences(){
    std::ifstream f(typePreferencesFile());std::string version;int smart=1,placeholder=1;
    if(!(f>>version>>smart>>placeholder))return;
    typeSmartQuotesEnabled=smart!=0;typePlaceholderEnabled=placeholder!=0;
    if(version=="TYPE2"){
        size_t cap=10,count=0;if(!(f>>cap>>count))return;recentTypeFontCap=(int)std::min<size_t>(cap,100);recentTypeFonts.clear();
        for(size_t i=0;i<std::min(count,(size_t)recentTypeFontCap);++i){std::string path;if(!(f>>std::quoted(path)))break;if(!path.empty()&&std::find(recentTypeFonts.begin(),recentTypeFonts.end(),path)==recentTypeFonts.end())recentTypeFonts.push_back(std::move(path));}
    }else if(version!="TYPE1")return;
}
static bool typeMoving = false;       // a Type-tool drag that moves the text without ending editing
static float typeMoveX = 0, typeMoveY = 0;
static bool typeRotating = false;
static float typeRotatePivotX = 0, typeRotatePivotY = 0, typeRotateStartAngle = 0, typeRotation0 = 0;
static int typeScaleHandle = -1;
static float typePointBox0[4] = {}, typeScaleX0 = 1, typeScaleY0 = 1;
static float typePointAnchorX0 = 0, typePointAnchorY0 = 0, typePointRotation0 = 0;
static bool typeBoxDrag = false;
static int typeResizeHandle = -1;
static float typeBoxX0 = 0, typeBoxY0 = 0, typeBoxW0 = 0, typeBoxH0 = 0;
static float typeBoxAnchorX0 = 0, typeBoxAnchorY0 = 0, typeBoxScaleX0 = 1, typeBoxScaleY0 = 1, typeBoxRotation0 = 0;
static bool typeIsMask() { return Toolbar::current() == Toolbar::TYPE_H_MASK || Toolbar::current() == Toolbar::TYPE_V_MASK; }
static bool typeIsVertical() { return Toolbar::current() == Toolbar::TYPE_V || Toolbar::current() == Toolbar::TYPE_V_MASK; }
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs commit): finish the edit and
// restore a temporary return tool only when the user has not selected another one.
static void typeCommit() {
    typeClickOwner=-1;typeClickRun=0;
    typeMoving = false;
    typeRotating = false;
    typeScaleHandle = -1;
    typeBoxDrag = false;
    typeResizeHandle = -1;
    typeSelectPlaceholder=false;
    if (typeMaskEditing) {   // the letters as a selection
        typeMaskEditing = false;
        typeMask.str = typeBuf;
        if (!typeMask.str.empty()) {
            std::vector<float> cov; float box[4];
            textCoverage(typeMask, doc.w, doc.h, cov, box, doc.dpi);
            std::vector<uint8_t> sel(cov.size());
            for (size_t i = 0; i < cov.size(); ++i) sel[i] = to8(cov[i]);
            finishShape(sel, SEL_NEW, "Type Mask");
        }
        typeEditHasSnapshot=typeEditCreated=typeEditUndoPushed=false;
        return;
    }
    if (typeLayer < 0) { typeReturnToolbar=-1;typeEditHasSnapshot=typeEditCreated=typeEditUndoPushed=false; return; }
    if(typeEditCreated&&typeLayer<(int)doc.layers.size()&&doc.layers[typeLayer].isText()&&doc.layers[typeLayer].text.str.empty()){
        doc=typeEditOriginalDoc;
    }
    else if(typeEditHasSnapshot&&(typeEditCreated||typeEditUndoPushed))hist.push(typeEditOriginalDoc,typeEditUndoName);
    typeLayer = -1;
    changed();
    typeEditHasSnapshot=typeEditCreated=typeEditUndoPushed=false;typeStyleSelectionValid=false;typeStyleSelectionLayer=-1;
    const int restore=typeReturnToolbar;typeReturnToolbar=-1;
    if(restore>=0&&Toolbar::current()==Toolbar::TYPE_H){Toolbar::select(restore);syncTool();}
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs cancel): discard text edits and restore the
// document from before this Type session without creating an undo entry.
static void typeCancel() {
    typeClickOwner=-1;typeClickRun=0;
    typeMoving=false;typeRotating=false;typeScaleHandle=-1;typeBoxDrag=false;typeResizeHandle=-1;typeSelectPlaceholder=false;
    if(typeMaskEditing){typeMaskEditing=false;typeLayer=-1;typeFocus=false;}
    else if(typeEditHasSnapshot){const bool wasDirty=typeEditOriginalDoc.dirty;doc=typeEditOriginalDoc;changed();doc.dirty=wasDirty;selChanged();typeLayer=-1;typeFocus=false;}
    else {typeLayer=-1;typeFocus=false;}
    typeEditHasSnapshot=typeEditCreated=typeEditUndoPushed=false;typeStyleSelectionValid=false;typeStyleSelectionLayer=-1;
    const int restore=typeReturnToolbar;typeReturnToolbar=-1;
    if(restore>=0&&Toolbar::current()==Toolbar::TYPE_H){Toolbar::select(restore);syncTool();}
}
static void typeSnapshotStart(bool created,const char* undoName="Edit Text"){typeEditOriginalDoc=doc;typeEditHasSnapshot=true;typeEditCreated=created;typeEditUndoPushed=false;typeEditUndoName=undoName;}
// Defer the Type undo snapshot until commit so editing does not copy the whole history or clear redo.
static void typeRemember(const char* what){
    if(typeEditHasSnapshot){
        if(!typeEditCreated&&!typeEditUndoPushed){typeEditUndoName=what?what:"Edit Text";typeEditUndoPushed=true;}
        return;
    }
    remember(what?what:"Text");
}
// PhotoCraft commits formatting and kerning keys as independent commands while a Type editor stays open.
static void typeRememberDiscrete(const char* what){
    const std::string name=what?what:"Text";
    if(!typeEditHasSnapshot){remember(name);return;}
    if(typeEditCreated){
        hist.push(typeEditOriginalDoc,typeEditUndoName);
        typeEditCreated=false;
    }else if(typeEditUndoPushed){
        hist.push(typeEditOriginalDoc,typeEditUndoName);
    }
    hist.push(doc,name);
    typeEditOriginalDoc=doc;typeEditUndoName="Edit Text";typeEditUndoPushed=false;
}
// Map a canvas point into the text frame so paragraph handles follow rotation and scale.
static ImVec2 typeToTextPoint(const Text& t, ImVec2 p) {
    const float angle = t.rotation * 0.017453292519943295f, cs = std::cos(angle), sn = std::sin(angle);
    const float dx = p.x - t.x, dy = p.y - t.y;
    return ImVec2(t.x + (cs * dx + sn * dy) / std::max(0.01f, t.scaleX),
                  t.y + (-sn * dx + cs * dy) / std::max(0.01f, t.scaleY));
}
// Use the press-time transform throughout a handle drag; moving the left/top edges also moves
// the text anchor, so inverting with the changing anchor would make a rotated box wobble.
static ImVec2 typeCanvasToBoxPoint(ImVec2 p) {
    const float angle = typeBoxRotation0 * 0.017453292519943295f, cs = std::cos(angle), sn = std::sin(angle);
    const float dx = p.x - typeBoxAnchorX0, dy = p.y - typeBoxAnchorY0;
    return ImVec2(typeBoxAnchorX0 + (cs * dx + sn * dy) / std::max(0.01f, typeBoxScaleX0),
                  typeBoxAnchorY0 + (-sn * dx + cs * dy) / std::max(0.01f, typeBoxScaleY0));
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/pointer.rs box_handle_at): the eight
// paragraph frame handles stay a fixed size on screen as zoom changes.
static int typeBoxHandleAt(const Text& t, ImVec2 c) {
    if (t.boxWidth <= 0 || t.boxHeight <= 0) return -1;
    const float mx = t.x + t.boxWidth * 0.5f, my = t.y + t.boxHeight * 0.5f;
    const float p[16] = {t.x,t.y, t.x+t.boxWidth,t.y, t.x+t.boxWidth,t.y+t.boxHeight, t.x,t.y+t.boxHeight,
                         mx,t.y, t.x+t.boxWidth,my, mx,t.y+t.boxHeight, t.x,my};
    const float reach = 6.0f / std::max(zoom, 0.01f), angle = t.rotation * 0.017453292519943295f, cs = std::cos(angle), sn = std::sin(angle);
    for (int i = 0; i < 8; ++i) {
        const float dx = (p[i*2] - t.x) * t.scaleX, dy = (p[i*2+1] - t.y) * t.scaleY;
        const float x = t.x + cs * dx - sn * dy, y = t.y + sn * dx + cs * dy;
        if (std::fabs(c.x - x) <= reach && std::fabs(c.y - y) <= reach) return i;
    }
    return -1;
}
// Point text uses its rendered bounds as the transform frame; paragraph text has its own frame.
static int typePointHandleAt(const Text& t, ImVec2 c) {
    if (t.boxWidth > 0) return -1;
    const float mx = (t.box[0] + t.box[2]) * 0.5f, my = (t.box[1] + t.box[3]) * 0.5f;
    const float p[16] = {t.box[0],t.box[1], t.box[2],t.box[1], t.box[2],t.box[3], t.box[0],t.box[3],
                         mx,t.box[1], t.box[2],my, mx,t.box[3], t.box[0],my};
    const float reach = 6.0f / std::max(zoom, 0.01f);
    for (int i = 0; i < 8; ++i) if (std::fabs(c.x - p[i*2]) <= reach && std::fabs(c.y - p[i*2+1]) <= reach) return i;
    return -1;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_path_placement.rs): start type at the nearest
// arc-length position on the active Work Path instead of snapping to its first anchor.
static float typePathNearest(const BezierPath& path, float x, float y, float* distance = nullptr, int* selected = nullptr) {
    const auto flat = flattenPath(path);
    if (flat.empty()) { if (distance) *distance = INFINITY; return 0; }
    float bestAlong = 0, best = 1e30f; int bestPath = -1;
    size_t flatIndex = 0;
    for (size_t pi = 0; pi < path.size(); ++pi) {
        if (path[pi].knots.empty()) continue;
        if (flatIndex >= flat.size()) break;
        const auto& p = flat[flatIndex++];
        if (p.size() < 4) continue;
        const size_t n = p.size() / 2; float along = 0;
        const size_t segments = path[pi].closed ? n : n - 1;
        for (size_t i = 0; i < segments; ++i) {
            const size_t j = (i + 1) % n;
            const float ax = p[i * 2], ay = p[i * 2 + 1], dx = p[j * 2] - ax, dy = p[j * 2 + 1] - ay;
            const float len2 = dx * dx + dy * dy, len = std::sqrt(len2);
            if (len < 1e-5f) continue;
            const float u = std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0f, 1.0f);
            const float ex = x - (ax + u * dx), ey = y - (ay + u * dy), dist = ex * ex + ey * ey;
            if (dist < best) { best = dist; bestAlong = along + u * len; bestPath = (int)pi; }
            along += len;
        }
    }
    if (distance) *distance = std::sqrt(best);
    if (selected) *selected = bestPath;
    return bestAlong;
}
static float typePathLength(const BezierPath& path) {
    const auto flat = flattenPath(path);
    float longest = 0; size_t fi = 0;
    for (size_t pi = 0; pi < path.size() && fi < flat.size(); ++pi) {
        if (path[pi].knots.empty()) continue;
        const auto& p = flat[fi++]; if (p.size() < 4) continue;
        float length = 0;
        for (size_t i = 2; i < p.size(); i += 2) length += std::hypot(p[i] - p[i - 2], p[i + 1] - p[i - 1]);
        if (path[pi].closed) length += std::hypot(p[0] - p[p.size() - 2], p[1] - p[p.size() - 1]);
        longest = std::max(longest, length);
    }
    return longest;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_path_placement.rs): sample an arc distance on the text's path.
static bool typePathPointAt(const Text& t, float arc, ImVec2& point, bool reverse = false) {
    const auto flat = flattenPath(t.path);
    if (flat.empty() || flat[0].size() < 4) return false;
    const auto& p = flat[0]; const size_t n = p.size() / 2;
    std::vector<float> cumulative(n + (t.path[0].closed ? 1 : 0), 0);
    for (size_t i = 1; i < n; ++i) cumulative[i] = cumulative[i - 1] + std::hypot(p[i*2] - p[(i-1)*2], p[i*2+1] - p[(i-1)*2+1]);
    if (t.path[0].closed) cumulative[n] = cumulative[n-1] + std::hypot(p[0] - p[(n-1)*2], p[1] - p[(n-1)*2+1]);
    const float length = cumulative.back(); if (length <= 0) return false;
    if (t.path[0].closed) { arc = std::fmod(arc, length); if (arc < 0) arc += length; }
    else arc = std::clamp(arc, 0.0f, length);
    if (reverse) arc = length - arc;
    size_t i = 1; while (i < cumulative.size() && cumulative[i] < arc) ++i;
    if (i >= cumulative.size()) i = cumulative.size() - 1;
    const size_t a = i - 1, b = i == n ? 0 : i; const float seg = cumulative[i] - cumulative[a];
    const float u = seg > 0 ? (arc - cumulative[a]) / seg : 0;
    float x = p[a*2] + (p[b*2] - p[a*2]) * u, y = p[a*2+1] + (p[b*2+1] - p[a*2+1]) * u;
    const float dx = (x - t.x) * t.scaleX, dy = (y - t.y) * t.scaleY, angle = t.rotation * 0.017453292519943295f;
    point = ImVec2(t.x + std::cos(angle)*dx - std::sin(angle)*dy, t.y + std::sin(angle)*dx + std::cos(angle)*dy);
    return true;
}
static int typePathDragPart = -1; // 0 start, 1 end, 2 move range, 3 coincident endpoints
static int typePathDragLayer = -1;
static int typePathDragTab = -1;
static bool typePathDragRemembered = false;
static float typePathDragStart = 0, typePathDragEnd = 0, typePathDragPointer = 0;
static float typePathDragDelta = 0;
static float typePathDragInitialSide = 0;
static bool typePathDragOriginalFlip = false, typePathDragFlipped = false;
static float typePathMovedOpenRange(float start, float end, float delta, float align, float length, float& outEnd) {
    // after PhotoCraft (crates/ui-egui/src/tools/type_path_placement.rs moved_open_range): keep the aligned anchor fixed at open endpoints.
    const float span = end - start, anchor = std::clamp(start + span * align + delta, 0.0f, length);
    if (align <= 0) { outEnd = std::min(anchor + span, length); return anchor; }
    if (align >= 1) { outEnd = anchor; return std::max(anchor - span, 0.0f); }
    const float half = std::min({span * 0.5f, anchor, length - anchor});
    outEnd = anchor + half; return anchor - half;
}
// Signed pointer distance from the unflipped path tangent, used to detect crossing to the other side.
static float typePathSignedSide(const Text& t, ImVec2 c) {
    const ImVec2 local = typeToTextPoint(t, c); const float arc = typePathNearest(t.path, local.x, local.y);
    const float step = 2.0f / std::max(t.scaleX, 0.01f); ImVec2 before, after, centre;
    if (!typePathPointAt(t, arc - step, before) || !typePathPointAt(t, arc + step, after) || !typePathPointAt(t, arc, centre)) return 0;
    const float tx = after.x - before.x, ty = after.y - before.y, norm = std::max(1e-5f, std::hypot(tx, ty));
    return (tx * (c.y - centre.y) - ty * (c.x - centre.x)) / norm;
}
// Project a pointer to the text path and update its captured interval without changing its span unexpectedly.
static void typePathDragTo(ImVec2 c) {
    if (typePathDragLayer < 0 || typePathDragLayer >= (int)doc.layers.size()) return;
    Text& t = doc.layers[typePathDragLayer].text;
    const ImVec2 local = typeToTextPoint(t, c);
    float distance = 0; float along = typePathNearest(t.path, local.x, local.y, &distance);
    const float length = typePathLength(t.path); if (length <= 0) return;
    if (t.pathFlip) along = length - along;
    float delta = along - typePathDragPointer;
    const bool closed = !t.path.empty() && t.path[0].closed;
    if (closed) delta = std::remainder(delta, length);
    const float side = typePathSignedSide(t, c);
    const bool flip = std::fabs(side) * zoom > 4.0f && typePathDragInitialSide != 0 && (side < 0) != (typePathDragInitialSide < 0);
    if ((delta != 0 || flip != typePathDragFlipped) && !typePathDragRemembered) { remember("Move Text Path"); typePathDragRemembered = true; }
    if ((delta != 0 || flip) && t.pathEnd < 0) t.pathEnd = typePathDragEnd;
    typePathDragDelta = delta;
    typePathDragFlipped = flip;
    if (typePathDragPart == 3 && typePathDragDelta != 0) typePathDragPart = typePathDragDelta < 0 ? 0 : 1;
    float proposedStart = typePathDragStart, proposedEnd = typePathDragEnd;
    if (typePathDragPart == 0) proposedStart = std::clamp(typePathDragStart + typePathDragDelta, closed ? typePathDragEnd - length : 0.0f, typePathDragEnd);
    else if (typePathDragPart == 1) proposedEnd = std::clamp(typePathDragEnd + typePathDragDelta, typePathDragStart, closed ? typePathDragStart + length : length);
    else if (typePathDragPart == 2) {
        if (closed) { proposedStart = typePathDragStart + typePathDragDelta; proposedEnd = typePathDragEnd + typePathDragDelta; }
        else {
            const float align = t.align == 0 ? 0.0f : (t.align == 1 ? 0.5f : 1.0f);
            proposedStart = typePathMovedOpenRange(typePathDragStart, typePathDragEnd, typePathDragDelta, align, length, proposedEnd);
        }
    }
    t.pathFlip = typePathDragOriginalFlip ^ flip;
    if (flip) {
        t.pathStart = length - proposedEnd;
        if (closed) { t.pathStart = std::fmod(t.pathStart, length); if (t.pathStart < 0) t.pathStart += length; }
        t.pathEnd = t.pathStart + proposedEnd - proposedStart;
    } else { t.pathStart = proposedStart; t.pathEnd = proposedEnd; }
    renderFill(doc, doc.layers[typePathDragLayer]); changed();
}
// A text layer and every group above it must be visible before it can be picked or expose path markers.
static bool typeLayerShown(const Doc& source, const Layer& layer) {
    const int index=(int)(&layer-source.layers.data());
    return layerShown(source,index);
}
static bool typePathLayerShown(const Layer& layer) { return typeLayerShown(doc, layer); }
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/layout.rs hit_layer): walk topmost first,
// skipping text hidden by any parent group and testing the rendered text bounds plus paragraph box.
static int typeHitLayer(const Doc& source, ImVec2 c, bool includeParagraph = true) {
    const std::vector<LayerTreeEntry> walk = layerTreeWalk(source);
    for (auto it = walk.rbegin(); it != walk.rend(); ++it) {
        if (it->group || it->layerIndex < 0 || it->layerIndex >= (int)source.layers.size()) continue;
        const Layer& layer = source.layers[it->layerIndex];
        if (!layer.isText() || !typeLayerShown(source, layer)) continue;
        const float* b = layer.text.box;
        const bool inGlyphs = c.x >= b[0] - 4 && c.x <= b[2] + 4 && c.y >= b[1] - 4 && c.y <= b[3] + 4;
        const ImVec2 local = typeToTextPoint(layer.text, c);
        const bool inParagraph = layer.text.boxWidth > 0 && local.x >= layer.text.x && local.x <= layer.text.x + layer.text.boxWidth && local.y >= layer.text.y && local.y <= layer.text.y + layer.text.boxHeight;
        if (inGlyphs || (includeParagraph && inParagraph)) return it->layerIndex;
    }
    return -1;
}
// Capture one of the visible path placement markers or the text range itself.
static bool typePathMarkerPress(ImVec2 c) {
    if (doc.active < 0 || doc.active >= (int)doc.layers.size() || !typePathLayerShown(doc.cur()) || !doc.cur().isText() || !doc.cur().text.pathText || groupLocked(doc, doc.cur().groupId)) return false;
    Text& tx = doc.cur().text; const float len = typePathLength(tx.path);
    const bool closed = !tx.path.empty() && tx.path[0].closed;
    const float end = tx.pathEnd >= 0 ? tx.pathEnd : (closed ? tx.pathStart + len : len);
    ImVec2 ps, pe;
    if (len <= 0 || !typePathPointAt(tx, tx.pathStart, ps, tx.pathFlip) || !typePathPointAt(tx, end, pe, tx.pathFlip)) return false;
    const bool startHit = std::hypot(c.x-ps.x,c.y-ps.y)*zoom <= 8, endHit = std::hypot(c.x-pe.x,c.y-pe.y)*zoom <= 8;
    int part = startHit && endHit ? 3 : (startHit ? 0 : (endHit ? 1 : -1));
    if (part == 3) {
        const float offset = 5.0f / std::max(zoom, 0.01f); ImVec2 before, after;
        if (typePathPointAt(tx, tx.pathStart - offset, before, tx.pathFlip) && typePathPointAt(tx, tx.pathStart + offset, after, tx.pathFlip)) {
            const float txv = after.x - before.x, tyv = after.y - before.y, norm = std::max(1e-5f, std::hypot(txv, tyv));
            const float along = ((c.x - ps.x) * txv + (c.y - ps.y) * tyv) / norm;
            part = std::fabs(along) * zoom <= 2 ? 2 : (along > 0 ? 0 : 1);
        }
    }
    const ImVec2 local = typeToTextPoint(tx, c); float dist = 0;
    float along = typePathNearest(tx.path, local.x, local.y, &dist);
    if (tx.pathFlip) along = len - along;
    if (part < 0 && dist*zoom <= 12) part = 2;
    if (part < 0) return false;
    typePathDragLayer = doc.active; typePathDragTab = curTab; typePathDragPart = part; typePathDragRemembered = false;
    typePathDragStart = tx.pathStart; typePathDragEnd = end; typePathDragPointer = along;
    typePathDragDelta = 0;
    typePathDragInitialSide = typePathSignedSide(tx, c); typePathDragOriginalFlip = tx.pathFlip; typePathDragFlipped = false;
    return true;
}
// Cancel a live preview as one gesture and discard its single history snapshot.
static void typePathDragCancel() {
    if (typePathDragRemembered) { hist.doUndo(doc); changed(); selChanged(); }
    typePathDragPart = -1; typePathDragLayer = -1; typePathDragTab = -1; typePathDragRemembered = false; dragging = false;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/pointer.rs pointer_down): edit shown type under the pointer
// unless Shift requests a new layer, then select its content in the editor.
static void typeBegin(ImVec2 c, bool forceNew = false) {
    typeStyleSelectionValid=false;typeStyleSelectionLayer=-1;
    // on a text layer's letters: edit it again; else new text where clicked (its first line's top a little above)
    const int hit = forceNew ? -1 : typeHitLayer(doc, c);
    if (hit >= 0) {
        typeSnapshotStart(false);
        const Layer& layer = doc.layers[hit];
        doc.active = hit; typeLayer = hit; snprintf(typeBuf, sizeof typeBuf, "%s", layer.text.str.c_str()); typeSelectPlaceholder=false; typeFocus = true; return;
    }
    Text t = typeNext;
    t.vertical = typeIsVertical();
    t.x = c.x; t.y = c.y - t.size * 0.75f;
    const bool placeholder=typePlaceholderEnabled&&!(typeIsMask()&&typeIsVertical());
    t.str=placeholder?"Lorem Ipsum":"";
    snprintf(typeBuf,sizeof(typeBuf),"%s",t.str.c_str());typeSelectPlaceholder=placeholder;typeFocus = true;
    if (typeIsMask()) { typeMask = t; typeMaskEditing = true; return; }
    typeSnapshotStart(true,"New Text");
    Fill f; f.kind = FILL_SOLID; for (int k = 0; k < 3; ++k) f.colour[k] = fg[k] / 255.0f;
    addTextLayer(doc, t, f);
    typeLayer = doc.active;
    changed();
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/pointer.rs double-click from Move): edit
// the shown text layer and return to Move when the text session commits.
static bool moveEditTextAt(ImVec2 c) {
    const int hit=typeHitLayer(doc,c,false);
    if(hit<0)return false;
    if(groupLocked(doc,doc.layers[hit].groupId)){say("This layer is locked by its group");return true;}
    Toolbar::select(Toolbar::TYPE_H);syncTool();typeReturnToolbar=Toolbar::MOVE;typeBegin(c);
    return typeLayer==hit;
}
static void typeBeginOnPath(ImVec2 c, float at, int selected = -1) {
    Text t = typeNext; t.vertical = false;
    if (selected >= 0 && selected < (int)doc.workPath.size()) t.path = {doc.workPath[selected]}; else t.path = doc.workPath;
    t.pathText = true; t.pathFlip = false; t.pathStart = at; t.x = c.x; t.y = c.y;
    t.str=typePlaceholderEnabled?"Lorem Ipsum":"";
    snprintf(typeBuf,sizeof(typeBuf),"%s",t.str.c_str());typeSelectPlaceholder=typePlaceholderEnabled;typeFocus = true;
    typeSnapshotStart(true,"New Text on Path");
    Fill f; f.kind = FILL_SOLID; for (int k = 0; k < 3; ++k) f.colour[k] = fg[k] / 255.0f;
    addTextLayer(doc, t, f); typeLayer = doc.active; changed();
}
// after PhotoCraft (crates/ui-egui/src/tools/move_tool.rs text movement): keep the glyphs and
// their hit box in the same document coordinates while preserving the drag's original grab offset.
static void moveTextTo(float x, float y) {
    if (doc.active < 0 || doc.active >= (int)doc.layers.size() || !doc.cur().isText()) return;
    Layer& layer = doc.cur(); layer.text.x = x; layer.text.y = y; layer.x = layer.y = 0;
    renderFill(doc, layer); changed();
}
// The Type tool's temporary drag and Move tool both update text through this geometry path.
static void typeMoveByPointer(ImVec2 start, ImVec2 now) {
    if (!typeMoving || typeLayer < 0 || typeLayer >= (int)doc.layers.size() || !doc.layers[typeLayer].isText()) return;
    moveTextTo(typeMoveX + now.x - start.x, typeMoveY + now.y - start.y);
}
// after PhotoCraft (crates/ui-egui/src/tools/type_transform.rs): rotate the text around its
// press-time anchor; Shift snaps the angle to fifteen-degree increments.
static void typeRotateByPointer(ImVec2 now, bool snap) {
    if (!typeRotating || typeLayer < 0 || typeLayer >= (int)doc.layers.size() || !doc.layers[typeLayer].isText()) return;
    const float angle = std::atan2(now.y - typeRotatePivotY, now.x - typeRotatePivotX) - typeRotateStartAngle;
    float degrees = typeRotation0 + angle * 57.29577951308232f;
    if (snap) degrees = std::round(degrees / 15.0f) * 15.0f;
    Layer& L = doc.layers[typeLayer]; L.text.rotation = std::remainder(degrees, 360.0f);
    renderFill(doc, L); changed();
}
// after PhotoCraft (crates/ui-egui/src/tools/type_transform.rs): scale point text from its
// press-time frame so each pointer update is stable and Shift keeps the original proportions.
static void typeScaleByPointer(ImVec2 now, bool constrain) {
    if (typeScaleHandle < 0 || typeLayer < 0 || typeLayer >= (int)doc.layers.size() || !doc.layers[typeLayer].isText()) return;
    const int h = typeScaleHandle; const bool left = h == 0 || h == 3 || h == 7, right = h == 1 || h == 2 || h == 5;
    const bool top = h == 0 || h == 1 || h == 4, bottom = h == 2 || h == 3 || h == 6;
    float x0 = typePointBox0[0], y0 = typePointBox0[1], x1 = typePointBox0[2], y1 = typePointBox0[3];
    if (left) x0 = std::min(now.x, x1 - 1.0f); if (right) x1 = std::max(now.x, x0 + 1.0f);
    if (top) y0 = std::min(now.y, y1 - 1.0f); if (bottom) y1 = std::max(now.y, y0 + 1.0f);
    float sx = typeScaleX0 * (x1 - x0) / std::max(1.0f, typePointBox0[2] - typePointBox0[0]);
    float sy = typeScaleY0 * (y1 - y0) / std::max(1.0f, typePointBox0[3] - typePointBox0[1]);
    if (constrain && (left || right) && (top || bottom)) { const float factor = std::max(sx / typeScaleX0, sy / typeScaleY0); sx = typeScaleX0 * factor; sy = typeScaleY0 * factor; }
    Layer& L = doc.layers[typeLayer];
    sx = std::clamp(sx, 0.01f, 100.0f); sy = std::clamp(sy, 0.01f, 100.0f);
    // Point text's origin is its transform anchor. Keep the opposite edge planted as a handle
    // moves, as Free Transform does; at a rotated angle the same scale stays about that anchor.
    if (std::fabs(typePointRotation0) < 0.001f) {
        if (left) L.text.x = typePointBox0[2] - (sx / typeScaleX0) * (typePointBox0[2] - typePointAnchorX0);
        else if (right) L.text.x = typePointBox0[0] - (sx / typeScaleX0) * (typePointBox0[0] - typePointAnchorX0);
        if (top) L.text.y = typePointBox0[3] - (sy / typeScaleY0) * (typePointBox0[3] - typePointAnchorY0);
        else if (bottom) L.text.y = typePointBox0[1] - (sy / typeScaleY0) * (typePointBox0[1] - typePointAnchorY0);
    }
    L.text.scaleX = sx; L.text.scaleY = sy;
    renderFill(doc, L); changed();
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/pointer.rs resize_box): recompute each
// frame from the press-time box so handle direction and Alt-centred resizing remain stable.
static void typeResizeByPointer(ImVec2 now, bool fromCentre) {
    if (typeResizeHandle < 0 || typeLayer < 0 || typeLayer >= (int)doc.layers.size() || !doc.layers[typeLayer].isText()) return;
    now = typeCanvasToBoxPoint(now);
    const int h = typeResizeHandle; const bool left = h == 0 || h == 3 || h == 7, right = h == 1 || h == 2 || h == 5;
    const bool top = h == 0 || h == 1 || h == 4, bottom = h == 2 || h == 3 || h == 6;
    float x0 = typeBoxX0, y0 = typeBoxY0, x1 = typeBoxX0 + typeBoxW0, y1 = typeBoxY0 + typeBoxH0;
    if (left) x0 = std::min(now.x, x1 - 4.0f); if (right) x1 = std::max(now.x, x0 + 4.0f);
    if (top) y0 = std::min(now.y, y1 - 4.0f); if (bottom) y1 = std::max(now.y, y0 + 4.0f);
    if (fromCentre) {
        const float cx = typeBoxX0 + typeBoxW0 * 0.5f, cy = typeBoxY0 + typeBoxH0 * 0.5f;
        if (left || right) { const float half = std::max(2.0f, std::fabs((left ? x0 : x1) - cx)); x0 = cx - half; x1 = cx + half; }
        if (top || bottom) { const float half = std::max(2.0f, std::fabs((top ? y0 : y1) - cy)); y0 = cy - half; y1 = cy + half; }
    }
    Layer& L = doc.layers[typeLayer]; L.text.x = x0; L.text.y = y0; L.text.boxWidth = x1 - x0; L.text.boxHeight = y1 - y0;
    renderFill(doc, L); changed();
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs insert_typed): mutate only the
// keyboard character queue so pasted text keeps its original quotation marks.
static void typeMapQueuedSmartQuotes(ImGuiInputTextState* state){
    ImGuiIO& io=ImGui::GetIO();if(!typeSmartQuotesEnabled||!state||io.InputQueueCharacters.empty())return;
    int cursor=state->GetCursorPos();
    if(state->HasSelection())cursor=std::min(state->GetSelectionStart(),state->GetSelectionEnd());
    cursor=std::clamp(cursor,0,state->CurLenW);
    ImWchar previous=cursor>0?state->TextW[cursor-1]:0;
    for(int i=0;i<io.InputQueueCharacters.Size;++i){ImWchar& c=io.InputQueueCharacters[i];
        c=(ImWchar)textSmartQuoteCharacter(previous,c);
        previous=c;
    }
}
static size_t typeCodepointIndex(const ImGuiInputTextState* state,int units){
    const int end=std::clamp(units,0,state->CurLenW);size_t count=0;
    for(int i=0;i<end;++i){const uint32_t c=state->TextW[i];if(c>=0xd800&&c<=0xdbff&&i+1<end&&state->TextW[i+1]>=0xdc00&&state->TextW[i+1]<=0xdfff)++i;++count;}
    return count;
}
static int typeWcharIndex(const ImGuiInputTextState* state,size_t codepoints){
    int i=0;size_t count=0;
    while(i<state->CurLenW&&count<codepoints){const uint32_t c=state->TextW[i++];if(c>=0xd800&&c<=0xdbff&&i<state->CurLenW&&state->TextW[i]>=0xdc00&&state->TextW[i]<=0xdfff)++i;++count;}
    return i;
}
static size_t typeCodepointsBeforeByte(const std::string& text,size_t bytes);
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs flow_key and handle_move_key):
// vertical arrows follow the text flow, and column moves keep the same character position.
bool typeMoveVerticalCaret(ImGuiInputTextState* state,const std::string& text,int cursor,int selectionStart,int selectionEnd,int selectionAnchor,bool hadSelection,int direction,bool extend){
    if(!state||direction==0)return false;
    const size_t count=typeCodepointIndex(state,state->CurLenW);
    size_t caret=typeCodepointIndex(state,std::clamp(cursor,0,state->CurLenW));
    caret=std::min(caret,count);
    size_t destination=caret;
    if(direction==-1||direction==1){
        if(hadSelection&&!extend)destination=typeCodepointIndex(state,direction<0?selectionStart:selectionEnd);
        else destination=direction<0?(caret?caret-1:0):std::min(caret+1,count);
    }else{
        size_t byte=0,cp=0;
        while(byte<text.size()&&cp<caret){const unsigned char c=(unsigned char)text[byte];byte+=c<0x80?1:c<0xe0?2:c<0xf0?3:4;++cp;}
        const size_t startByte=byte?text.rfind('\n',byte-1)==std::string::npos?0:text.rfind('\n',byte-1)+1:0;
        const size_t endByte=text.find('\n',byte)==std::string::npos?text.size():text.find('\n',byte);
        const size_t column=typeCodepointsBeforeByte(text,startByte),columnEnd=typeCodepointsBeforeByte(text,endByte);
        const size_t offset=caret>=column?std::min(caret-column,columnEnd-column):0;
        size_t targetByte=startByte;
        if(direction>0){
            if(endByte<text.size())targetByte=endByte+1;
            else {destination=count;targetByte=text.size();}
        }else if(startByte>0){const size_t priorBreak=startByte>=2?text.rfind('\n',startByte-2):std::string::npos;targetByte=priorBreak==std::string::npos?0:priorBreak+1;}
        else {destination=0;targetByte=0;}
        if((direction>0&&endByte<text.size())||(direction<0&&startByte>0)){
            const size_t targetLineEnd=text.find('\n',targetByte)==std::string::npos?text.size():text.find('\n',targetByte);
            destination=typeCodepointsBeforeByte(text,targetByte)+std::min(offset,typeCodepointsBeforeByte(text,targetLineEnd)-typeCodepointsBeforeByte(text,targetByte));
        }
    }
    const int dest=typeWcharIndex(state,destination);
    state->Stb.cursor=state->Stb.select_end=dest;
    state->Stb.select_start=extend?(hadSelection?selectionAnchor:cursor):dest;
    state->Stb.has_preferred_x=0;state->CursorFollow=true;state->CursorAnimReset();
    return true;
}
static size_t typeByteOffset(const std::string& text,size_t codepoints){
    size_t at=0,count=0;
    while(at<text.size()&&count<codepoints){const unsigned char c=(unsigned char)text[at];at+=c<0x80?1:c<0xe0?2:c<0xf0?3:4;++count;}
    return std::min(at,text.size());
}
// after PhotoCraft (crates/text/src/layout/navigate.rs line_edge): Home and End target
// the active visual row, including the editor's soft wraps, and preserve Shift's anchor.
bool typeMoveEditorLineEdge(ImGuiInputTextState* state,const std::string& text,bool end,int cursor,int anchor,bool hadSelection,bool extend){
    if(!state)return false;
    cursor=std::clamp(cursor,0,state->CurLenW);
    if(state->Ctx){
        state->Stb.cursor=cursor;state->Stb.select_start=extend&&hadSelection?anchor:cursor;state->Stb.select_end=cursor;
        state->OnKeyPressed((end?0x200005:0x200004)|(extend?0x400000:0));
        state->CursorFollow=true;state->CursorAnimReset();
        return true;
    }
    const size_t character=typeCodepointIndex(state,cursor),byte=typeByteOffset(text,character);
    const size_t lineStart=byte?([&](){const size_t p=text.rfind('\n',byte-1);return p==std::string::npos?size_t(0):p+1;})():0;
    const size_t foundEnd=text.find('\n',byte),lineEnd=foundEnd==std::string::npos?text.size():foundEnd;
    const size_t destination=typeCodepointsBeforeByte(text,end?lineEnd:lineStart);
    const int dest=typeWcharIndex(state,destination);
    state->Stb.cursor=state->Stb.select_end=dest;state->Stb.select_start=extend?(hadSelection?anchor:cursor):dest;
    state->Stb.has_preferred_x=0;state->CursorFollow=true;state->CursorAnimReset();
    return true;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs changed_span and replace_text): find the
// smallest UTF-8 character-aligned edit, then let Text preserve character and paragraph runs.
bool typeApplyEditorText(Text& text,const std::string& updated){
    const std::string& old=text.str;if(old==updated)return false;
    auto nextByte=[](const std::string& s,size_t at){const unsigned char c=(unsigned char)s[at];return std::min(s.size(),at+(size_t)(c<0x80?1:c<0xe0?2:c<0xf0?3:4));};
    auto previousByte=[](const std::string& s,size_t at){size_t p=at-1;while(p&&((unsigned char)s[p]&0xc0)==0x80)--p;return p;};
    size_t prefixOld=0,prefixNew=0;
    while(prefixOld<old.size()&&prefixNew<updated.size()){
        const size_t oldNext=nextByte(old,prefixOld),newNext=nextByte(updated,prefixNew);
        if(oldNext-prefixOld!=newNext-prefixNew||old.compare(prefixOld,oldNext-prefixOld,updated,prefixNew,newNext-prefixNew)!=0)break;
        prefixOld=oldNext;prefixNew=newNext;
    }
    size_t suffixOld=old.size(),suffixNew=updated.size();
    while(suffixOld>prefixOld&&suffixNew>prefixNew){
        const size_t oldPrev=previousByte(old,suffixOld),newPrev=previousByte(updated,suffixNew);
        if(old.compare(oldPrev,suffixOld-oldPrev,updated,newPrev,suffixNew-newPrev)!=0)break;
        suffixOld=oldPrev;suffixNew=newPrev;
    }
    return text.replaceText(prefixOld,suffixOld,updated.substr(prefixNew,suffixNew-prefixNew));
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs type.setStyle): a selected range gets
// the new size while an empty range applies it to the whole text layer.
bool typeSetCharacterSize(Text& text,size_t start,size_t end,float size){
    if(!std::isfinite(size))return false;size=std::clamp(size,4.0f,1000.0f);
    if(text.str.empty()){
        auto runs=text.normalizedRuns();if(runs.empty())return false;
        if(runs.front().style.size==size&&text.size==size)return false;
        runs.front().style.size=size;text.runs={{0,runs.front().style}};text.syncSummary();return true;
    }
    auto styles=text.characterStyles();if(styles.empty())return false;
    start=std::min(start,styles.size());end=std::min(end,styles.size());
    if(start>=end){start=0;end=styles.size();}
    bool changed=false;for(size_t i=start;i<end;++i){changed|=styles[i].size!=size;styles[i].size=size;}
    if(!changed)return false;
    text.runs.clear();size_t byte=0;
    for(const TextStyle& style:styles){const unsigned char c=(unsigned char)text.str[byte];const size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text.runs.empty()&&text.runs.back().style==style)text.runs.back().bytes+=n;else text.runs.push_back({n,style});byte=std::min(text.str.size(),byte+n);}
    text.syncSummary();return true;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs style_range): tracking remains per
// character, with an empty selection applying the value to the whole text layer.
bool typeSetCharacterTracking(Text& text,size_t start,size_t end,float tracking){
    if(!std::isfinite(tracking))return false;tracking=std::clamp(tracking,-1000.0f,10000.0f);
    if(text.str.empty()){
        auto runs=text.normalizedRuns();if(runs.empty())return false;
        if(runs.front().style.tracking==tracking&&text.tracking==tracking)return false;
        runs.front().style.tracking=tracking;text.runs={{0,runs.front().style}};text.syncSummary();return true;
    }
    auto styles=text.characterStyles();if(styles.empty())return false;
    start=std::min(start,styles.size());end=std::min(end,styles.size());if(start>=end){start=0;end=styles.size();}
    bool changed=false;for(size_t i=start;i<end;++i){changed|=styles[i].tracking!=tracking;styles[i].tracking=tracking;}
    if(!changed)return false;
    text.runs.clear();size_t byte=0;
    for(const TextStyle& style:styles){const unsigned char c=(unsigned char)text.str[byte];const size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text.runs.empty()&&text.runs.back().style==style)text.runs.back().bytes+=n;else text.runs.push_back({n,style});byte=std::min(text.str.size(),byte+n);}
    text.syncSummary();return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/panel.rs Character metrics): slider
// edits change only the selected run and keep all its other character settings intact.
bool typeSetCharacterMetricValue(Text& text,size_t start,size_t end,int metric,float value,float dpi,float layerScaleY){
    if(!std::isfinite(value)||metric<0||metric>3)return false;
    auto set=[&](TextStyle& style){
        if(metric==0){const float size=std::max(1.0f,std::isfinite(style.size)?style.size:text.size);
            const float vs=std::isfinite(style.verticalScale)&&style.verticalScale>0?style.verticalScale:1.0f;
            const float ys=std::isfinite(layerScaleY)&&layerScaleY>0?layerScaleY:1.0f;
            const float res=std::isfinite(dpi)&&dpi>0?dpi/72.0f:1.0f;
            style.leading=std::clamp(value,.01f,5000.0f)*res/(size*vs*ys);
        }else if(metric==1)style.baselineShift=std::clamp(value,-5000.0f,5000.0f);
        else if(metric==2)style.horizontalScale=std::clamp(value/100.0f,.01f,10.0f);
        else style.verticalScale=std::clamp(value/100.0f,.01f,10.0f);
    };
    auto runs=text.normalizedRuns();if(runs.empty())return false;
    if(text.str.empty()){
        if(start||end)return false;const TextStyle old=runs.front().style;set(runs.front().style);
        if(old==runs.front().style)return false;text.runs={{0,runs.front().style}};text.syncSummary();return true;
    }
    auto styles=text.characterStyles();start=std::min(start,styles.size());end=std::min(end,styles.size());
    if(start>=end){start=0;end=styles.size();}if(start>=end)return false;
    bool changed=false;for(size_t i=start;i<end;++i){TextStyle old=styles[i];set(styles[i]);changed|=!(old==styles[i]);}
    if(!changed)return false;text.runs.clear();size_t byte=0;
    for(const TextStyle& style:styles){const unsigned char c=(unsigned char)text.str[byte];const size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text.runs.empty()&&text.runs.back().style==style)text.runs.back().bytes+=n;else text.runs.push_back({n,style});byte=std::min(text.str.size(),byte+n);}
    text.syncSummary();return true;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs style_range): the font family is
// stored on the same selected character runs that layout uses to choose each glyph's face.
bool typeSetCharacterFont(Text& text,size_t start,size_t end,const std::string& font){
    if(font.empty())return false;
    if(text.str.empty()){
        auto runs=text.normalizedRuns();if(runs.empty())return false;
        if(runs.front().style.font==font&&text.font==font)return false;
        runs.front().style.font=font;text.runs={{0,runs.front().style}};text.syncSummary();return true;
    }
    auto styles=text.characterStyles();if(styles.empty())return false;
    start=std::min(start,styles.size());end=std::min(end,styles.size());if(start>=end){start=0;end=styles.size();}
    bool changed=false;for(size_t i=start;i<end;++i){changed|=styles[i].font!=font;styles[i].font=font;}
    if(!changed)return false;
    text.runs.clear();size_t byte=0;
    for(const TextStyle& style:styles){const unsigned char c=(unsigned char)text.str[byte];const size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text.runs.empty()&&text.runs.back().style==style)text.runs.back().bytes+=n;else text.runs.push_back({n,style});byte=std::min(text.str.size(),byte+n);}
    text.syncSummary();return true;
}
// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs paragraph style range): formatting
// applies to selected paragraphs, or the caret's paragraph when the range is collapsed.
static ParagraphStyle typeParagraphStyleAt(const Text& text,size_t character){
    const auto runs=text.normalizedParagraphs();
    if(runs.empty())return {};
    const size_t byte=typeByteOffset(text.str,character);
    size_t at=0;
    for(const ParagraphRun& run:runs){
        if(byte<at+run.bytes||(run.bytes==0&&byte==at))return run.style;
        at+=run.bytes;
    }
    return runs.back().style;
}
bool typeSetParagraphStyle(Text& text,size_t start,size_t end,int property,float value){
    if(property<0||property>6||!std::isfinite(value))return false;
    auto set=[&](ParagraphStyle& style){
        if(property==0)style.align=(int)std::lround(std::clamp(value,0.0f,6.0f));
        else if(property==1)style.firstIndent=std::clamp(value,-1296.0f,1296.0f);
        else if(property==2)style.startIndent=std::clamp(value,-1296.0f,1296.0f);
        else if(property==3)style.endIndent=std::clamp(value,-1296.0f,1296.0f);
        else if(property==4)style.spaceBefore=std::clamp(value,0.0f,1296.0f);
        else if(property==5)style.spaceAfter=std::clamp(value,0.0f,1296.0f);
        else style.autoLeading=std::clamp(value,.1f,10.0f);
    };
    const size_t count=text.characterStyles().size();
    start=std::min(start,count);end=std::min(end,count);
    if(start>end)std::swap(start,end);
    const size_t byteStart=typeByteOffset(text.str,start),byteEnd=typeByteOffset(text.str,end);
    const auto source=text.normalizedParagraphs();
    std::vector<std::pair<size_t,size_t>> ranges;
    size_t paragraphStart=0;
    for(size_t i=0;i<text.str.size();++i)if(text.str[i]=='\n'){ranges.push_back({paragraphStart,i+1});paragraphStart=i+1;}
    ranges.push_back({paragraphStart,text.str.size()});
    std::vector<ParagraphRun> updated;
    bool changed=false;
    size_t runStart=0;
    for(const auto& range:ranges){
        ParagraphStyle style=source.empty()?ParagraphStyle{}:source.back().style;
        for(const ParagraphRun& run:source){
            if(range.first<runStart+run.bytes||(run.bytes==0&&range.first==runStart)){style=run.style;break;}
            runStart+=run.bytes;
        }
        runStart=0;
        const bool selected=start==end
            ? (byteStart>=range.first&&(byteStart<range.second||range.first==range.second))
            : (byteStart<range.second&&byteEnd>range.first);
        if(selected){const ParagraphStyle before=style;set(style);changed|=before.align!=style.align||before.firstIndent!=style.firstIndent||
            before.startIndent!=style.startIndent||before.endIndent!=style.endIndent||before.spaceBefore!=style.spaceBefore||
            before.spaceAfter!=style.spaceAfter||before.autoLeading!=style.autoLeading;}
        const size_t bytes=range.second-range.first;
        if(!updated.empty()&&updated.back().style.align==style.align&&updated.back().style.firstIndent==style.firstIndent&&
           updated.back().style.startIndent==style.startIndent&&updated.back().style.endIndent==style.endIndent&&
           updated.back().style.spaceBefore==style.spaceBefore&&updated.back().style.spaceAfter==style.spaceAfter&&
           updated.back().style.autoLeading==style.autoLeading)updated.back().bytes+=bytes;
        else updated.push_back({bytes,style});
    }
    if(!changed)return false;
    text.paragraphs=std::move(updated);return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs step_char_metric): selected
// characters move from their own metric value, and a collapsed range in non-empty text is inert.
bool typeStepCharacterMetric(Text& text,size_t start,size_t end,int metric,float by,float dpi,float layerScaleY){
    if(!std::isfinite(by))return false;
    auto adjust=[&](TextStyle& style){
        if(metric==0){
            const float size=std::max(1.0f,std::isfinite(style.size)?style.size:text.size);
            const float vscale=std::isfinite(style.verticalScale)&&style.verticalScale>0?style.verticalScale:1.0f;
            const float yscale=std::isfinite(layerScaleY)&&layerScaleY>0?layerScaleY:1.0f;
            const float resolution=std::isfinite(dpi)&&dpi>0?dpi/72.0f:1.0f;
            const float current=std::isfinite(style.leading)&&style.leading>0?style.leading:1.2f;
            const float points=current*size*vscale*yscale/resolution;
            style.leading=std::clamp(points+by,.01f,5000.0f)*resolution/(size*vscale*yscale);
        }else if(metric==1)style.baselineShift=std::clamp((std::isfinite(style.baselineShift)?style.baselineShift:0.0f)+by,-5000.0f,5000.0f);
        else if(metric==2)style.tracking=std::clamp((std::isfinite(style.tracking)?style.tracking:0.0f)+by,-1000.0f,10000.0f);
    };
    if(text.str.empty()){
        if(start||end)return false;
        auto runs=text.normalizedRuns();if(runs.empty())return false;
        const TextStyle before=runs.front().style;adjust(runs.front().style);if(before==runs.front().style)return false;
        text.runs={{0,runs.front().style}};text.syncSummary();return true;
    }
    const auto styles=text.characterStyles();const size_t count=styles.size();start=std::min(start,count);end=std::min(end,count);
    if(start>=end)return false;
    std::vector<TextStyle> changedStyles=styles;for(size_t i=start;i<end;++i)adjust(changedStyles[i]);
    text.runs.clear();size_t byte=0;
    for(size_t i=0;i<changedStyles.size();++i){
        const unsigned char c=(unsigned char)text.str[byte];const size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text.runs.empty()&&text.runs.back().style==changedStyles[i])text.runs.back().bytes+=n;
        else text.runs.push_back({n,changedStyles[i]});
        byte=std::min(text.str.size(),byte+n);
    }
    text.syncSummary();return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs toggle_char_style): selected
// characters toggle together; mixed selections turn the style on, and a collapsed non-empty range does nothing.
bool typeToggleCharacterStyle(Text& text,size_t start,size_t end,int styleIndex){
    if(styleIndex<0||styleIndex>5)return false;
    auto value=[&](const TextStyle& s){return styleIndex==0?s.bold:styleIndex==1?s.italic:styleIndex==2?s.underline:styleIndex==3?s.strikethrough:styleIndex==4?s.caps==TextCaps::All:s.caps==TextCaps::Small;};
    auto set=[&](TextStyle& s,bool on){if(styleIndex==0)s.bold=on;else if(styleIndex==1)s.italic=on;else if(styleIndex==2)s.underline=on;else if(styleIndex==3)s.strikethrough=on;else s.caps=on?(styleIndex==4?TextCaps::All:TextCaps::Small):TextCaps::Normal;};
    auto styles=text.characterStyles();
    if(text.str.empty()){
        auto runs=text.normalizedRuns();if(start||end||runs.empty())return false;
        const bool on=!value(runs.front().style);set(runs.front().style,on);text.runs={{0,runs.front().style}};text.syncSummary();return true;
    }
    start=std::min(start,styles.size());end=std::min(end,styles.size());if(start>=end)return false;
    bool all=true;for(size_t i=start;i<end;++i)all&=value(styles[i]);
    for(size_t i=start;i<end;++i)set(styles[i],!all);
    text.runs.clear();size_t byte=0;
    for(size_t i=0;i<styles.size();++i){const unsigned char c=(unsigned char)text.str[byte];const size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
        if(!text.runs.empty()&&text.runs.back().style==styles[i])text.runs.back().bytes+=n;else text.runs.push_back({n,styles[i]});byte=std::min(text.str.size(),byte+n);}
    text.syncSummary();return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs handle_delete_key): modified
// deletion uses PhotoCraft word boundaries and Command/Ctrl+Backspace stops at the line edge.
bool typePrepareModifiedDelete(ImGuiInputTextState* state,const std::string& text,bool backspace,bool ctrl,bool alt,bool mac,bool super){
    const bool command=mac?super:ctrl;
    if(!state||(!command&&!alt))return false;
    if(state->HasSelection())return true;
    const int cursor=std::clamp(state->GetCursorPos(),0,state->CurLenW);
    const size_t character=typeCodepointIndex(state,cursor),characterCount=typeCodepointIndex(state,state->CurLenW);
    size_t target=character;
    if(backspace){
        if(command){
            // Dear ImGui 1.90.4's STB line-start key lays out the active multiline editor row,
            // including soft wraps, before selecting back to it.
            if(state->Ctx){state->OnKeyPressed(0x200004|0x400000);return true;}
            const size_t byte=typeByteOffset(text,character);
            if(byte>0){const size_t newline=text.rfind('\n',byte-1);if(newline!=std::string::npos){size_t cp=0;for(size_t i=0;i<newline+1;){const unsigned char c=(unsigned char)text[i];i+=c<0x80?1:c<0xe0?2:c<0xf0?3:4;++cp;}target=cp;}else target=0;}
        }else if(alt)target=textWordBoundary(text,character,false);
    }else if(alt)target=textWordBoundary(text,character,true);
    else if(command)target=std::min(character+1,characterCount);
    if(target==character)return true;
    const int destination=typeWcharIndex(state,target);
    state->Stb.cursor=cursor;state->Stb.select_start=cursor;state->Stb.select_end=destination;
    state->Stb.has_preferred_x=0;state->CursorFollow=true;state->CursorAnimReset();
    return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs handle_move_key): Ctrl/Alt arrows
// move by the Unicode alphanumeric run and keep the anchor when extending a selection.
bool typeMoveWordCaret(ImGuiInputTextState* state,const std::string& text,int cursor,int anchor,bool hadSelection,bool forward,bool extend){
    if(!state)return false;
    const size_t from=typeCodepointIndex(state,cursor),to=textWordBoundary(text,from,forward);
    const int dest=typeWcharIndex(state,to);
    state->Stb.cursor=state->Stb.select_end=dest;
    state->Stb.select_start=extend?(hadSelection?anchor:cursor):dest;
    state->Stb.has_preferred_x=0;state->CursorFollow=true;state->CursorAnimReset();
    return true;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs handle_move_key): Command/Ctrl+Up
// and Down move to the beginning or end of the text, preserving the selection anchor with Shift.
bool typeMoveDocumentEdge(ImGuiInputTextState* state,bool end,int cursor,int anchor,bool hadSelection,bool extend){
    if(!state)return false;
    const int dest=end?state->CurLenW:0;
    state->Stb.cursor=state->Stb.select_end=dest;
    state->Stb.select_start=extend?(hadSelection?anchor:cursor):dest;
    state->Stb.has_preferred_x=0;state->CursorFollow=true;state->CursorAnimReset();
    return true;
}
unsigned typeNextClickCount(unsigned previous,unsigned imguiCount){
    if(imguiCount<=1)return 1;
    if(previous==0)return std::min(imguiCount,5u);
    return previous%5+1;
}
static size_t typeCodepointsBeforeByte(const std::string& text,size_t bytes){
    size_t at=0,count=0;bytes=std::min(bytes,text.size());
    while(at<bytes){const unsigned char c=(unsigned char)text[at];at+=c<0x80?1:c<0xe0?2:c<0xf0?3:4;if(at<=bytes)++count;}
    return count;
}
static std::pair<size_t,size_t> typeParagraphSelection(const std::string& text,size_t caret){
    const size_t byte=typeByteOffset(text,caret);
    const size_t foundStart=byte?text.rfind('\n',byte-1):std::string::npos;
    const size_t start=foundStart==std::string::npos?0:foundStart+1;
    size_t end=text.find('\n',byte);if(end==std::string::npos)end=text.size();
    while(end>start&&text[end-1]=='\r')--end;
    return {typeCodepointsBeforeByte(text,start),typeCodepointsBeforeByte(text,end)};
}
bool typeApplyClickSelection(ImGuiInputTextState* state,const std::string& text,unsigned count){
    if(!state||count<2||count>5)return false;
    const size_t caret=typeCodepointIndex(state,state->GetCursorPos());
    if(count==3&&state->Ctx){
        state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=state->GetCursorPos();
        state->OnKeyPressed(0x200004);
        state->OnKeyPressed(0x200005|0x400000);
        return true;
    }
    std::pair<size_t,size_t> range;
    if(count==2)range=textWordSelection(text,caret);
    else if(count==3||count==4)range=typeParagraphSelection(text,caret);
    else range={0,typeCodepointIndex(state,state->CurLenW)};
    const int start=typeWcharIndex(state,range.first),end=typeWcharIndex(state,range.second);
    state->Stb.cursor=end;state->Stb.select_start=start;state->Stb.select_end=end;state->Stb.has_preferred_x=0;
    state->CursorFollow=true;state->CursorAnimReset();
    return true;
}
bool typeWordNavigationShortcut(bool ctrl,bool alt,bool shift,bool mac,bool super){
    return (!alt||shift)&&((mac?super:ctrl)||alt);
}
bool typeDocumentNavigationShortcut(bool ctrl,bool alt,bool mac,bool super){
    return !alt&&(mac?super:ctrl);
}
// the box typed into, over the picture where the text is (the layer shows the letters as they're typed)
static void typeEditor() {
    if (typeLayer < 0 && !typeMaskEditing) return;
    if (typeLayer >= (int)doc.layers.size() || (typeLayer >= 0 && !doc.layers[typeLayer].isText())) { typeLayer = -1; return; }
    const Text& t = typeMaskEditing ? typeMask : doc.layers[typeLayer].text;
    const ImVec2 at = toScreen(t.x, t.y + t.size * 1.3f + 6);
    ImGui::SetNextWindowPos(ImVec2(at.x, at.y));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##typing", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    if (typeFocus) { ImGui::SetKeyboardFocusHere(); typeFocus = false; }
    const ImGuiID typeInputId=ImGui::GetID("##tb");
    ImGuiInputTextState* movementState=ImGui::GetInputTextState(typeInputId);
    const bool wordModifier=typeWordNavigationShortcut(ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyAlt,ImGui::GetIO().KeyShift,ImGui::GetIO().ConfigMacOSXBehaviors,ImGui::GetIO().KeySuper);
    const bool documentModifier=typeDocumentNavigationShortcut(ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyAlt,ImGui::GetIO().ConfigMacOSXBehaviors,ImGui::GetIO().KeySuper);
    const bool altTypeMetricArrow=ImGui::GetIO().KeyAlt&&!ImGui::GetIO().KeyShift&&(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)||ImGui::IsKeyPressed(ImGuiKey_RightArrow));
    const bool moveWordLeft=ImGui::IsWindowFocused()&&ImGui::IsKeyPressed(ImGuiKey_LeftArrow)&&wordModifier&&!altTypeMetricArrow;
    const bool moveWordRight=ImGui::IsWindowFocused()&&ImGui::IsKeyPressed(ImGuiKey_RightArrow)&&wordModifier&&!altTypeMetricArrow;
    const bool moveDocumentStart=ImGui::IsWindowFocused()&&ImGui::IsKeyPressed(ImGuiKey_UpArrow)&&documentModifier;
    const bool moveDocumentEnd=ImGui::IsWindowFocused()&&ImGui::IsKeyPressed(ImGuiKey_DownArrow)&&documentModifier;
    const bool moveLineStart=ImGui::IsWindowFocused()&&ImGui::IsKeyPressed(ImGuiKey_Home,true);
    const bool moveLineEnd=ImGui::IsWindowFocused()&&ImGui::IsKeyPressed(ImGuiKey_End,true);
    ImGuiIO& typeIo=ImGui::GetIO();
    const int movementCursor=movementState?movementState->GetCursorPos():0,movementAnchor=movementState?movementState->GetSelectionStart():0;
    const int movementRangeStart=movementState?std::min(movementState->Stb.select_start,movementState->Stb.select_end):0;
    const int movementRangeEnd=movementState?std::max(movementState->Stb.select_start,movementState->Stb.select_end):0;
    const int movementSelectionAnchor=movementState?movementState->Stb.select_start:0;
    const int movementSelectionActive=movementState?movementState->Stb.select_end:0;
    const bool movementSelected=movementState&&movementState->HasSelection(),movementExtend=ImGui::GetIO().KeyShift;
    int verticalDirection=0;
    if(t.vertical&&ImGui::IsWindowFocused()&&!typeIo.KeyCtrl&&!typeIo.KeyAlt&&!typeIo.KeySuper){
        if(ImGui::IsKeyPressed(ImGuiKey_UpArrow,true))verticalDirection=-1;
        else if(ImGui::IsKeyPressed(ImGuiKey_DownArrow,true))verticalDirection=1;
        else if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true))verticalDirection=2;
        else if(ImGui::IsKeyPressed(ImGuiKey_RightArrow,true))verticalDirection=-2;
    }
    int typeMetric=-1;float typeMetricBy=0;int pairKerningDirection=0;float pairKerningBy=0;int typeStyleShortcut=-1;
    const bool typeCommand=typeIo.ConfigMacOSXBehaviors?typeIo.KeySuper:typeIo.KeyCtrl;
    if(ImGui::IsWindowFocused()&&typeCommand&&typeIo.KeyShift&&!typeIo.KeyAlt){
        if(ImGui::IsKeyPressed(ImGuiKey_B))typeStyleShortcut=0;
        else if(ImGui::IsKeyPressed(ImGuiKey_I))typeStyleShortcut=1;
        else if(ImGui::IsKeyPressed(ImGuiKey_U))typeStyleShortcut=2;
        else if(ImGui::IsKeyPressed(ImGuiKey_Slash))typeStyleShortcut=3;
        else if(ImGui::IsKeyPressed(ImGuiKey_K))typeStyleShortcut=4;
    }
    if(ImGui::IsWindowFocused()&&typeIo.KeyAlt&&
       (ImGui::IsKeyPressed(ImGuiKey_UpArrow,true)||ImGui::IsKeyPressed(ImGuiKey_DownArrow,true))){
        const bool up=ImGui::IsKeyPressed(ImGuiKey_UpArrow,true);const float amount=typeCommand?10.0f:2.0f;
        typeMetric=typeIo.KeyShift?1:0;typeMetricBy=typeIo.KeyShift?(up?amount:-amount):(up?-amount:amount);
    }
    if(ImGui::IsWindowFocused()&&typeIo.KeyAlt&&!typeIo.KeyShift&&
       (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)||ImGui::IsKeyPressed(ImGuiKey_RightArrow,true))){
        const bool right=ImGui::IsKeyPressed(ImGuiKey_RightArrow,true);const float amount=typeCommand?100.0f:20.0f;
        if(movementSelected){typeMetric=2;typeMetricBy=right?amount:-amount;}
        else {pairKerningDirection=right?1:-1;pairKerningBy=amount;}
    }
    const bool modifiedDelete=ImGui::IsWindowFocused()&&(ImGui::IsKeyPressed(ImGuiKey_Backspace)||ImGui::IsKeyPressed(ImGuiKey_Delete))&&
        typePrepareModifiedDelete(movementState,typeBuf,ImGui::IsKeyPressed(ImGuiKey_Backspace),typeIo.KeyCtrl,typeIo.KeyAlt,typeIo.ConfigMacOSXBehaviors,typeIo.KeySuper);
    const bool savedCtrl=typeIo.KeyCtrl,savedAlt=typeIo.KeyAlt,savedSuper=typeIo.KeySuper;
    if(modifiedDelete){typeIo.KeyCtrl=typeIo.KeyAlt=typeIo.KeySuper=false;}
    if(typeSmartQuoteScriptStage==2){ImGuiIO& io=ImGui::GetIO();io.InputQueueCharacters.clear();for(ImWchar c:{'h','i',' ','"','y','o','u','"'})io.InputQueueCharacters.push_back(c);typeSmartQuoteScriptStage=3;}
    if(ImGui::IsWindowFocused())typeMapQueuedSmartQuotes(ImGui::GetInputTextState(typeInputId));
    if (ImGui::InputTextMultiline("##tb", typeBuf, sizeof typeBuf, ImVec2(360, ImGui::GetTextLineHeight() * 4 + 8), ImGuiInputTextFlags_AllowTabInput)) {
        if (!typeMaskEditing) { Layer& L = doc.layers[typeLayer];Text updated=L.text;if(typeApplyEditorText(updated,typeBuf)){
            typeRemember("Edit Text");L.text=std::move(updated);renderFill(doc,L);changed();L.name=textLayerName(L.text.str);}}
        else typeApplyEditorText(typeMask,typeBuf);
    }
    if(modifiedDelete){typeIo.KeyCtrl=savedCtrl;typeIo.KeyAlt=savedAlt;typeIo.KeySuper=savedSuper;}
    if(verticalDirection)typeMoveVerticalCaret(ImGui::GetInputTextState(typeInputId),typeBuf,movementCursor,movementRangeStart,movementRangeEnd,movementSelectionAnchor,movementSelected,verticalDirection,movementExtend);
    if(typeMetric>=0){
        if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){
            state->Stb.cursor=movementCursor;state->Stb.select_start=movementSelectionAnchor;state->Stb.select_end=movementSelectionActive;
            const size_t start=typeCodepointIndex(state,std::min(state->Stb.select_start,state->Stb.select_end));
            const size_t end=typeCodepointIndex(state,std::max(state->Stb.select_start,state->Stb.select_end));
            Text* target=typeMaskEditing?&typeMask:(typeLayer>=0&&typeLayer<(int)doc.layers.size()&&doc.layers[typeLayer].isText()?&doc.layers[typeLayer].text:nullptr);
            if(target){Text updated=*target;if(typeStepCharacterMetric(updated,start,end,typeMetric,typeMetricBy,doc.dpi,updated.scaleY)){
                if(!typeMaskEditing)typeRememberDiscrete(typeMetric==0?"Leading":"Baseline Shift");*target=std::move(updated);
                if(!typeMaskEditing){renderFill(doc,doc.layers[typeLayer]);changed();}
            }}
        }
    }
    if(pairKerningDirection){
        if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){
            state->Stb.cursor=movementCursor;state->Stb.select_start=movementSelectionAnchor;state->Stb.select_end=movementSelectionActive;
            Text* target=typeMaskEditing?&typeMask:(typeLayer>=0&&typeLayer<(int)doc.layers.size()&&doc.layers[typeLayer].isText()?&doc.layers[typeLayer].text:nullptr);
            const size_t caret=typeCodepointIndex(state,movementCursor);
            if(target){Text updated=*target;if(textAdjustKerningPair(updated,caret,pairKerningDirection*pairKerningBy)){
                if(!typeMaskEditing)typeRememberDiscrete("Kerning");*target=std::move(updated);
                if(!typeMaskEditing){renderFill(doc,doc.layers[typeLayer]);changed();}
            }}
        }
    }
    if(typeStyleShortcut>=0){
        if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){
            state->Stb.cursor=movementCursor;state->Stb.select_start=movementSelectionAnchor;state->Stb.select_end=movementSelectionActive;
            const size_t start=typeCodepointIndex(state,std::min(state->Stb.select_start,state->Stb.select_end));
            const size_t end=typeCodepointIndex(state,std::max(state->Stb.select_start,state->Stb.select_end));
            if(!typeMaskEditing&&typeLayer>=0&&typeLayer<(int)doc.layers.size()&&doc.layers[typeLayer].isText()){
                Text updated=doc.layers[typeLayer].text;
                if(typeToggleCharacterStyle(updated,start,end,typeStyleShortcut)){
                    typeRememberDiscrete(typeStyleShortcut==0?"Faux Bold":typeStyleShortcut==1?"Faux Italic":typeStyleShortcut==2?"Underline":typeStyleShortcut==3?"Strikethrough":"All Caps");
                    doc.layers[typeLayer].text=std::move(updated);renderFill(doc,doc.layers[typeLayer]);changed();
                }
            }
        }
    }
    if(moveLineStart||moveLineEnd)typeMoveEditorLineEdge(ImGui::GetInputTextState(typeInputId),typeBuf,moveLineEnd,movementCursor,movementSelectionAnchor,movementSelected,movementExtend);
    if(ImGui::IsItemHovered()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        const int owner=typeMaskEditing?-2:typeLayer;
        const unsigned previous=typeClickOwner==owner?typeClickRun:0;
        typeClickRun=typeNextClickCount(previous,(unsigned)typeIo.MouseClickedCount[ImGuiMouseButton_Left]);typeClickOwner=owner;
        if(typeClickRun>1)typeApplyClickSelection(ImGui::GetInputTextState(typeInputId),typeBuf,typeClickRun);
    }
    if(moveWordLeft||moveWordRight)typeMoveWordCaret(ImGui::GetInputTextState(typeInputId),typeBuf,movementCursor,movementAnchor,movementSelected,moveWordRight,movementExtend);
    if(moveDocumentStart||moveDocumentEnd)typeMoveDocumentEdge(ImGui::GetInputTextState(typeInputId),moveDocumentEnd,movementCursor,movementAnchor,movementSelected,movementExtend);
    if(typeWordNavigationScriptStage==1){
        if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=2;
            typeMoveWordCaret(state,typeBuf,2,2,false,true,false);const bool forward=state->GetCursorPos()==6&&!state->HasSelection();
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=6;
            typeMoveWordCaret(state,typeBuf,6,6,false,false,false);const bool backward=state->GetCursorPos()==4&&!state->HasSelection();
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=2;
            typeMoveWordCaret(state,typeBuf,2,2,false,true,true);const bool extended=state->GetCursorPos()==6&&state->GetSelectionStart()==2;
            typeMoveDocumentEdge(state,false,4,1,true,true);const bool documentStart=state->GetCursorPos()==0&&state->GetSelectionStart()==1;
            typeMoveDocumentEdge(state,true,0,0,false,false);const bool documentEnd=state->GetCursorPos()==6&&!state->HasSelection();
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=6;const bool deleteBack=typePrepareModifiedDelete(state,typeBuf,true,false,true,false,false)&&state->Stb.select_end==4;
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=2;const bool deleteForward=typePrepareModifiedDelete(state,typeBuf,false,false,true,false,false)&&state->Stb.select_end==6;
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=4;const bool deleteLine=typePrepareModifiedDelete(state,typeBuf,true,true,false,false,false)&&state->Stb.select_start==4&&state->Stb.select_end==0;
            fprintf(stderr,"type-word-navigation: %s; word arrows/extension, document edges, Alt deletion back/forward, Ctrl line deletion=%d/%d/%d/%d/%d/%d/%d/%d\n",forward&&backward&&extended&&documentStart&&documentEnd&&deleteBack&&deleteForward&&deleteLine?"passed":"failed",forward,backward,extended,documentStart,documentEnd,deleteBack,deleteForward,deleteLine);typeWordNavigationScriptStage=2;
        }
    }
    if(typeMultiClickScriptStage==1){
        if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=1;
            const bool word=typeApplyClickSelection(state,typeBuf,2)&&state->Stb.select_start==0&&state->Stb.select_end==2;
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=8;
            const bool line=typeApplyClickSelection(state,typeBuf,3)&&state->Stb.select_start==7&&state->Stb.select_end==11;
            state->Stb.cursor=state->Stb.select_start=state->Stb.select_end=8;
            const bool paragraph=typeApplyClickSelection(state,typeBuf,4)&&state->Stb.select_start==7&&state->Stb.select_end==11;
            const bool all=typeApplyClickSelection(state,typeBuf,5)&&state->Stb.select_start==0&&state->Stb.select_end==11;
            const bool cycle=typeNextClickCount(3,3)==4&&typeNextClickCount(4,3)==5&&typeNextClickCount(5,3)==1;
            fprintf(stderr,"type-multiclick: %s; word/visual-line/paragraph/all/cycle=%d/%d/%d/%d/%d\n",word&&line&&paragraph&&all&&cycle?"passed":"failed",word,line,paragraph,all,cycle);typeMultiClickScriptStage=2;
        }
    }
    if(typeSelectPlaceholder){if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){state->SelectAll();typeSelectPlaceholder=false;}}
    if(typeSmartQuoteScriptStage==1){if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){typeSmartQuoteScriptSelection=state->GetSelectionStart()==0&&state->GetSelectionEnd()==state->CurLenW;typeSmartQuoteScriptStage=2;}}
    const bool finishTypeScript=typeSmartQuoteScriptStage==3&&typeLayer>=0&&typeLayer<(int)doc.layers.size()&&doc.layers[typeLayer].text.str=="hi \xe2\x80\x9cyou\xe2\x80\x9d";
    if(typeSmartQuoteScriptStage==3){fprintf(stderr,"type-smart-quotes: %s; keyboard quotes transform and replace selected placeholder\n",finishTypeScript&&typeSmartQuoteScriptSelection?"passed":"failed");typeSmartQuoteScriptStage=4;}
    if(ImGuiInputTextState* state=ImGui::GetInputTextState(typeInputId)){
        typeStyleSelectionStart=typeCodepointIndex(state,std::min(state->Stb.select_start,state->Stb.select_end));
        typeStyleSelectionEnd=typeCodepointIndex(state,std::max(state->Stb.select_start,state->Stb.select_end));
        typeStyleSelectionLayer=typeLayer;typeStyleSelectionValid=!typeMaskEditing;
    }
    ImGui::TextDisabled("Ctrl+Enter or click away to finish, Esc too");
    const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool done = ImGui::GetIO().KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    ImGui::End();
    if(cancel)typeCancel();else if(done||finishTypeScript)typeCommit();
}

// ---------- the marking tools: Color Sampler, Ruler, Note, Count (PhotoCraft's) ----------
enum { MK_SAMPLER, MK_RULER, MK_NOTE, MK_COUNT };
static int markMode() {
    switch (Toolbar::current()) {
    case Toolbar::RULER: return MK_RULER; case Toolbar::NOTE: return MK_NOTE; case Toolbar::COUNT_TOOL: return MK_COUNT;
    default: return MK_SAMPLER;
    }
}
static int markDrag = -1, noteSel = -1;
static float countColour[3] = {0.0f, 0.482f, 0.718f};
static std::vector<Doc::Mark>& markList(int mode) { return mode == MK_SAMPLER ? doc.samplers : mode == MK_COUNT ? doc.counts : doc.notes; }
static int markNear(const std::vector<Doc::Mark>& v, ImVec2 c, float px) {
    for (int i = (int)v.size() - 1; i >= 0; --i) if (std::hypot(v[i].x - c.x, v[i].y - c.y) * zoom <= px) return i;
    return -1;
}
// the picture's colour under a sampler (the composite, made again only when the picture changed)
static void sampledColour(float x, float y, uint8_t* rgb) {
    static Image comp; static int ver = -1;
    if (ver != docVersion || comp.w != doc.w || comp.h != doc.h) { comp = Image(doc.w, doc.h); composite(doc, comp, {0, 0, doc.w, doc.h}, false); ver = docVersion; }
    const int ix = std::clamp((int)x, 0, doc.w - 1), iy = std::clamp((int)y, 0, doc.h - 1);
    const uint8_t* q = comp.at(ix, iy);
    rgb[0] = q[0]; rgb[1] = q[1]; rgb[2] = q[2];
}
static float rulerAngle() {   // degrees, counter-clockwise from 3 o'clock (y up), as PhotoCraft and Photoshop show it
    return std::atan2(-(doc.ruler[3] - doc.ruler[1]), doc.ruler[2] - doc.ruler[0]) * 57.29578f;
}

static int shapeKind() {
    switch (Toolbar::current()) {
    case Toolbar::RECTANGLE: return SHAPE_RECT; case Toolbar::ELLIPSE: return SHAPE_ELLIPSE; case Toolbar::TRIANGLE: return SHAPE_TRIANGLE;
    case Toolbar::POLYGON: return SHAPE_POLYGON; case Toolbar::STAR: return SHAPE_STAR; case Toolbar::LINE: return SHAPE_LINE;
    case Toolbar::CUSTOM_SHAPE: return SHAPE_PATH;
    default: return SHAPE_RECT;
    }
}
// PhotoCraft's custom shape presets are paths in a 100 by 100 box; these starter paths use the same coordinates.
static const char* CUSTOM_SHAPE_NAMES[] = {"Heart", "Arrow Right", "Arrow Left", "Cloud", "Speech Bubble", "Lightning", "Diamond", "Check Mark",
                                           "Banner", "Ribbon", "Pentagon", "Hexagon", "Octagon", "Plus", "Cross", "Star", "Burst", "Leaf", "Raindrop", "Thought Bubble"};
static const std::vector<std::vector<ImVec2>> CUSTOM_SHAPE_POINTS = {
    {{50,96},{7,55},{2,37},{5,20},{15,7},{30,2},{43,8},{50,20},{57,8},{70,2},{85,7},{95,20},{98,37},{93,55}},
    {{0,32},{57,32},{57,10},{100,50},{57,90},{57,68},{0,68}},
    {{100,32},{43,32},{43,10},{0,50},{43,90},{43,68},{100,68}},
    {{18,77},{5,76},{0,62},{2,49},{12,41},{21,41},{28,20},{43,10},{58,11},{72,22},{76,34},{91,35},{100,48},{99,64},{88,77}},
    {{13,5},{87,5},{98,16},{98,60},{87,72},{52,72},{30,98},{34,72},{13,72},{2,60},{2,16}},
    {{62,0},{23,51},{46,51},{34,100},{82,39},{58,39}},
    {{50,0},{100,50},{50,100},{0,50}},
    {{4,54},{20,37},{39,57},{80,10},{97,26},{39,90}},
    {{0,8},{100,8},{100,72},{72,72},{50,100},{28,72},{0,72}},
    {{0,8},{100,8},{100,72},{72,72},{60,92},{60,72},{40,72},{40,92},{28,72},{0,72}},
    {{50,0},{100,50},{50,100},{0,50},{15,50}},
    {{25,0},{75,0},{100,50},{75,100},{25,100},{0,50}},
    {{30,0},{70,0},{100,30},{100,70},{70,100},{30,100},{0,70},{0,30}},
    {{38,0},{62,0},{62,38},{100,38},{100,62},{62,62},{62,100},{38,100},{38,62},{0,62},{0,38},{38,38}},
    {{20,0},{50,30},{80,0},{100,20},{70,50},{100,80},{80,100},{50,70},{20,100},{0,80},{30,50},{0,20}},
    {{50,0},{62,35},{98,35},{69,56},{80,94},{50,70},{20,94},{31,56},{2,35},{38,35}},
    {{50,0},{62,34},{98,38},{72,60},{80,96},{50,76},{20,96},{28,60},{2,38},{38,34}},
    {{50,2},{61,25},{88,5},{76,34},{100,48},{72,55},{86,88},{55,74},{41,100},{38,69},{7,84},{22,55},{0,38},{31,39}},
    {{50,0},{80,45},{80,68},{70,88},{50,100},{30,88},{20,68},{20,45}},
    {{50,7},{76,8},{96,27},{97,48},{85,65},{66,73},{54,70},{44,79},{40,94},{28,84},{31,70},{14,68},{3,54},{6,37},{21,29},{29,14}}
};
static const bool CUSTOM_SHAPE_SMOOTH[] = {true, false, false, true, false, false, false, false, false, false,
                                           false, false, false, false, false, false, false, true, true, true};
static Shape customShapeFromDrag(ImVec2 a, ImVec2 b) {
    // Fit the selected PhotoCraft-style preset path into the drag box without changing its outline.
    Shape sh = shapeNext;
    sh.kind = SHAPE_PATH;
    float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    if (ImGui::GetIO().KeyShift) {
        const auto& bounds = CUSTOM_SHAPE_POINTS[std::clamp(customShapePick, 0, (int)CUSTOM_SHAPE_POINTS.size() - 1)];
        float bx0 = 100, by0 = 100, bx1 = 0, by1 = 0;
        for (const ImVec2& p : bounds) { bx0 = std::min(bx0, p.x); by0 = std::min(by0, p.y); bx1 = std::max(bx1, p.x); by1 = std::max(by1, p.y); }
        const float ratio = (bx1 - bx0) / std::max(0.01f, by1 - by0);
        float w = x1 - x0, h = y1 - y0;
        if (w / std::max(h, 0.01f) > ratio) w = h * ratio; else h = w / ratio;
        const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        x0 = cx - w * 0.5f; x1 = cx + w * 0.5f;
        y0 = cy - h * 0.5f; y1 = cy + h * 0.5f;
    }
    sh.x0 = x0; sh.y0 = y0; sh.x1 = x1; sh.y1 = y1;
    Subpath sp; sp.closed = true;
    const auto& points = CUSTOM_SHAPE_POINTS[std::clamp(customShapePick, 0, (int)CUSTOM_SHAPE_POINTS.size() - 1)];
    for (size_t i = 0; i < points.size(); ++i) {
        const ImVec2& p = points[i];
        Knot k; k.ax = k.ix = k.ox = x0 + p.x / 100.0f * (x1 - x0); k.ay = k.iy = k.oy = y0 + p.y / 100.0f * (y1 - y0);
        if (CUSTOM_SHAPE_SMOOTH[customShapePick] && !(customShapePick == 0 && i == 0)) {
            const ImVec2& prev = points[(i + points.size() - 1) % points.size()];
            const ImVec2& next = points[(i + 1) % points.size()];
            const float tx = (next.x - prev.x) / 6.0f * (x1 - x0) / 100.0f;
            const float ty = (next.y - prev.y) / 6.0f * (y1 - y0) / 100.0f;
            k.ix = k.ax - tx; k.iy = k.ay - ty; k.ox = k.ax + tx; k.oy = k.ay + ty; k.smooth = true;
        }
        sp.knots.push_back(k);
    }
    // after PhotoCraft (crates/engine/src/presets/shapes.rs builtin Heart): keep its six cubic segments,
    // because a tangent estimated from corner points cannot reproduce the lobes or the bottom cusp.
    if (customShapePick == 0) {
        const float sx = (x1 - x0) / 100.0f, sy = (y1 - y0) / 100.0f;
        const float px[] = {50, 0, 28, 50, 72, 100};
        const float py[] = {92, 30, 0, 16, 0, 30};
        const float inx[] = {80, 0, 14, 47, 60, 100};
        const float iny[] = {70, 52, 0, 8, 0, 12};
        const float outx[] = {20, 0, 40, 53, 86, 100};
        const float outy[] = {70, 12, 0, 8, 0, 52};
        sp.knots.clear();
        for (int i = 0; i < 6; ++i) {
            Knot k;
            k.ax = x0 + px[i] * sx; k.ay = y0 + py[i] * sy;
            k.ix = x0 + inx[i] * sx; k.iy = y0 + iny[i] * sy;
            k.ox = x0 + outx[i] * sx; k.oy = y0 + outy[i] * sy;
            k.smooth = true;
            sp.knots.push_back(k);
        }
    }
    sh.path = {sp};
    return sh;
}
// the shape a drag from a to b makes: Shift a square / circle / regular one (a line: in 45-degree steps), Alt from
// the middle out
static Shape shapeFromDrag(ImVec2 a, ImVec2 b) {
    if (Toolbar::current() == Toolbar::CUSTOM_SHAPE) return customShapeFromDrag(a, b);
    Shape sh = shapeNext;
    sh.kind = shapeKind();
    const ImGuiIO& io = ImGui::GetIO();
    float dx = b.x - a.x, dy = b.y - a.y;
    if (sh.kind == SHAPE_LINE) {
        if (io.KeyShift) {   // the nearest 45 degrees
            const float l = std::hypot(dx, dy), ang = std::round(std::atan2(dy, dx) / 0.785398f) * 0.785398f;
            dx = std::cos(ang) * l; dy = std::sin(ang) * l;
        }
        sh.x0 = a.x; sh.y0 = a.y; sh.x1 = a.x + dx; sh.y1 = a.y + dy;
        return sh;
    }
    if (io.KeyShift) { const float m = std::max(std::fabs(dx), std::fabs(dy)); dx = dx < 0 ? -m : m; dy = dy < 0 ? -m : m; }
    if (io.KeyAlt) { sh.x0 = a.x - dx; sh.y0 = a.y - dy; sh.x1 = a.x + dx; sh.y1 = a.y + dy; }
    else { sh.x0 = a.x; sh.y0 = a.y; sh.x1 = a.x + dx; sh.y1 = a.y + dy; }
    return sh;
}

static int healMode() {
    switch (Toolbar::current()) {
    case Toolbar::REMOVE: return HM_REMOVE; case Toolbar::HEALING: return HM_HEALING; case Toolbar::PATCH: return HM_PATCH;
    case Toolbar::CONTENT_AWARE_MOVE: return HM_MOVE; default: return HM_SPOT;
    }
}
// a stroke as a canvas-sized mask: round stamps along it (soft: an edge that fades over the outer third)
static std::vector<uint8_t> strokeMask(const std::vector<float>& pts, float size, bool soft) {
    std::vector<uint8_t> m((size_t)doc.w * doc.h, 0);
    const float r = std::max(0.5f, size * 0.5f), fade = std::max(1.0f, r * 0.35f);
    auto stamp = [&](float cx, float cy) {
        const int x0 = std::max(0, (int)std::floor(cx - r - 1)), x1 = std::min(doc.w - 1, (int)std::ceil(cx + r + 1));
        const int y0 = std::max(0, (int)std::floor(cy - r - 1)), y1 = std::min(doc.h - 1, (int)std::ceil(cy + r + 1));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                const float a = soft ? std::clamp((r - d) / fade, 0.0f, 1.0f) : (d <= r ? 1.0f : 0.0f);
                uint8_t& v = m[(size_t)y * doc.w + x];
                v = std::max(v, (uint8_t)std::lround(a * 255));
            }
    };
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
        stamp(pts[i], pts[i + 1]);
        if (i + 3 < pts.size()) {
            const float dx = pts[i + 2] - pts[i], dy = pts[i + 3] - pts[i + 1], len = std::hypot(dx, dy), step = std::max(1.0f, r * 0.25f);
            for (float t = step; t < len; t += step) stamp(pts[i] + dx * t / len, pts[i + 1] + dy * t / len);
        }
    }
    return m;
}
static bool pixelLayer() { const Layer& l = doc.cur(); return !l.isAdjust() && !l.isFill() && !doc.editMask && !groupLocked(doc, l.groupId); }
static void healStrokeEnd() {
    if (healPts.empty()) return;
    const int hm = healMode();
    const std::vector<float> pts = healPts;
    healPts.clear();
    if (hm == HM_HEALING) {
        remember("Healing Brush");
        healRegion(doc, strokeMask(pts, healSizes[HM_HEALING], true), (int)std::lround(healDX), (int)std::lround(healDY));
        changed();
        return;
    }
    say(hm == HM_REMOVE ? "Removing..." : "Healing...");
    remember(hm == HM_REMOVE ? "Remove" : "Spot Healing");
    if (contentAwareFill(doc, strokeMask(pts, healSizes[hm], false))) { changed(); say(""); }
    else { hist.doUndo(doc); say("Nothing round it to copy from"); }
}
static void contentAwareFillSelection() {
    if (!doc.hasSel()) { say("Select what to fill first"); return; }
    if (!pixelLayer()) { say("Content-Aware Fill works on a layer's pixels"); return; }
    say("Filling...");
    remember("Content-Aware Fill");
    if (contentAwareFill(doc, doc.sel)) { changed(); say(""); } else { hist.doUndo(doc); say("Nothing round it to copy from"); }
}
static bool insideSel(ImVec2 c) {
    return doc.hasSel() && c.x >= 0 && c.y >= 0 && c.x < doc.w && c.y < doc.h && doc.sel[(size_t)(int)c.y * doc.w + (int)c.x] > 127;
}
static void canvasInput() {
    ImGuiIO& io = ImGui::GetIO();
    cropConsumedShortcutKey=0;
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 m = io.MousePos;
    const ImVec2 c = toCanvas(m);
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
    const int t = space ? T_HAND : tool;
    if (typePathDragPart >= 0 && (typePathDragTab != curTab || doc.active != typePathDragLayer || t != T_PEN || (penMode() != PN_PATHSEL && penMode() != PN_DIRECT))) typePathDragCancel();
    if (hovered && t == T_TYPE && typeLayer >= 0 && typeLayer < (int)doc.layers.size() && doc.layers[typeLayer].isText() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const int handle = typeBoxHandleAt(doc.layers[typeLayer].text, c);
        const int pointHandle = io.KeyCtrl ? typePointHandleAt(doc.layers[typeLayer].text, c) : -1;
        const int activeHandle = handle >= 0 ? handle : pointHandle;
        if (activeHandle == 0 || activeHandle == 2) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        else if (activeHandle == 1 || activeHandle == 3) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
        else if (activeHandle == 4 || activeHandle == 6) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        else if (activeHandle == 5 || activeHandle == 7) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (t == T_MAGNETIC && magneticActive() && (hovered || ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        magneticUpdate({c.x, c.y}, ImGui::IsMouseDown(ImGuiMouseButton_Left), true);
    if (hovered && io.MouseWheel != 0) zoomAt(zoom * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), m);
    if (tool == T_ROTATE && viewRotation != 0.0f) {
        const ImVec2 q(canvasPos.x + canvasSize.x - 24, canvasPos.y + 24);
        const float dx = m.x - q.x, dy = m.y - q.y;
        if (hovered && dx * dx + dy * dy < 14 * 14 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { viewRotation = 0.0f; dragging = false; return; }
    }
    if (tool == T_ROTATE && ImGui::IsKeyPressed(ImGuiKey_Escape)) viewRotation = 0.0f;
    // Space can temporarily take over an active Crop handle drag; returning to Crop resumes it.
    if (space && tool == T_CROP && dragging && cropDragPart >= 0 && cropDragPart < 8 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !cropSpacePanning) {
        cropSpacePanning = panning = true; panStart = m; originStart = origin;
    } else if (cropSpacePanning && !space) {
        cropSpacePanning = panning = false;
    }
    // middle button always pans
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) { panning = true; panStart = m; originStart = origin; }
    if (panning) {
        const float a = viewRotation * 0.017453292519943295f, cs = std::cos(a), sn = std::sin(a);
        const float dx = m.x - panStart.x, dy = m.y - panStart.y;
        const ImVec2 oldOrigin = origin;
        origin = ImVec2(originStart.x + cs * dx + sn * dy, originStart.y - sn * dx + cs * dy);
        if (cropSpacePanning) cropCompensatePan(ImVec2(origin.x - oldOrigin.x, origin.y - oldOrigin.y));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) panning = false;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging = true; dragStart = c; dragNow = c;
        switch (t) {
        case T_HAND: panning = true; panStart = m; originStart = origin; dragging = false; break;
        case T_ROTATE: {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { viewRotation = 0.0f; dragging = false; break; }
            const ImVec2 pivot(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
            rotateStartPointer = std::atan2(m.y - pivot.y, m.x - pivot.x);
            rotateStartAngle = viewRotation;
            break;
        }
        case T_MOVE: {
            moveTextLayer = false;
            const int textHit = typeHitLayer(doc, c, false);
            if(textHit>=0&&ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
                moveEditTextAt(c);dragging=false;break;
            }
            if (textHit >= 0) selectLayer(textHit);
            if (groupLocked(doc, doc.cur().groupId)) { say("This layer is locked by its group"); dragging = false; break; }
            remember("Move");
            if (doc.cur().isText()) {
                moveTextLayer = true; moveTextStartX = doc.cur().text.x; moveTextStartY = doc.cur().text.y;
                moveStartX = (int)std::lround(moveTextStartX); moveStartY = (int)std::lround(moveTextStartY); moveGroupId = 0;
            } else { moveStartX = doc.cur().x; moveStartY = doc.cur().y; moveGroupId = doc.cur().groupId; }
            break;
        }
        case T_LASSO: lasso = {c.x, c.y}; break;
        case T_MAGNETIC:
            magneticClick({c.x, c.y}, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
            dragging = false;
            break;
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
        case T_ZOOM: zoomAt(zoom * (io.KeyAlt ? 1 / 1.5f : 1.5f), m); dragging = false; break;
        case T_ROW: case T_COL: {
            const int y = std::clamp((int)c.y, 0, doc.h - 1), x = std::clamp((int)c.x, 0, doc.w - 1);
            finishShape(t == T_ROW ? shapeRect(doc, 0, y, doc.w, y + 1) : shapeRect(doc, x, 0, x + 1, doc.h), modeFromKeys(), t == T_ROW ? "Single Row" : "Single Column");
            dragging = false; break;
        }
        case T_CROP:
            if(straightenArmed||io.KeyCtrl){straightenLine=true;straightenFit=straightenArmed;straightenStart=straightenEnd=c;}
            else if (cropPending && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { applyCrop(); dragging = false; }
            else {cropBegin(c);if(cropDragPart==8&&!cropClassicMode)beginCropImageMove(m);}
            break;
        case T_PCROP:
            pcropDragPart = -1;
            if (pcropPending) {
                int hit = -1;
                for (int i = 0; i < 4; ++i) {
                    ImVec2 corner = toScreen(pcropQ[i * 2], pcropQ[i * 2 + 1]);
                    if (std::hypot(m.x - corner.x, m.y - corner.y) < 12) { hit = i; break; }
                }
                if (hit >= 0) pcropDragPart = hit;
                else if (pcropInside(c)) pcropDragPart = -2;
                else { pcropCancel(); pcropDragPart = -1; }
            }
            memcpy(pcropMoveQ, pcropQ, sizeof pcropQ);
            pcropMoveStart = c;
            break;
        case T_SLICE:
            sliceActive = -1;
            break;
        case T_SLICE_SELECT: {
            sliceActive = -1;
            sliceDidMove = false;
            for (int i = (int)doc.slices.size() - 1; i >= 0; --i) {
                const Rect r = doc.slices[i].rect;
                if (c.x >= r.x0 && c.x < r.x1 && c.y >= r.y0 && c.y < r.y1) { sliceActive = i; break; }
            }
            if (sliceActive >= 0) { remember("Move Slice"); sliceStartRect = doc.slices[sliceActive].rect; }
            break;
        }
        case T_FRAME:
            break;
        case T_ARTBOARD: {
            artboardMoving = false;
            for (int i = (int)doc.artboards.size() - 1; i >= 0; --i) {
                const Rect r = doc.artboards[i].rect;
                if (c.x >= r.x0 && c.x < r.x1 && c.y >= r.y0 && c.y < r.y1) {
                    artboardActive = i; artboardMoving = true; artboardMoveDX = artboardMoveDY = 0; remember("Move Artboard"); break;
                }
            }
            break;
        }
        case T_GRADIENT: case T_OBJECT: break;   // done when the drag ends
        case T_QUICK: quickPts = {c.x, c.y}; break;
        case T_REDEYE:
            if (doc.cur().isAdjust() || doc.cur().isFill()) { say("Red Eye works on a layer's pixels"); dragging = false; break; }
            remember("Red Eye");
            if (redEye(doc, (int)c.x, (int)c.y, pupilSize, redDarken)) changed(); else { hist.doUndo(doc); say("No red pupil there: click in the middle of the eye"); }
            dragging = false; break;
        case T_MAGIC_ERASER:
            if (c.x >= 0 && c.y >= 0 && c.x < doc.w && c.y < doc.h) { remember("Magic Eraser"); magicErase(doc, (int)c.x, (int)c.y, tolerance, contiguous, sampleAll, 1.0f); changed(); }
            dragging = false; break;
        case T_PENCIL:
            if (io.KeyAlt) { pickColour(c, false); dragging = false; break; }
            if (!doc.cur().visible) { say("The layer is hidden: show it to paint"); dragging = false; break; }
            remember("Pencil"); strokeBegin(doc, stroke);
            changed(pencilTo(doc, stroke, c.x, c.y, pencilSize, pencilOpacity, brushColour(), false, true));
            break;
        case T_RETOUCH: {
            const int mode = retouchMode();
            if (mode == RT_CLONE && io.KeyAlt) { cloneSet = true; cloneHaveOffset = false; cloneSrcX = c.x; cloneSrcY = c.y; say("Clone source set: paint to copy from it"); dragging = false; break; }
            if (mode == RT_CLONE && !cloneSet) { say("Alt+click where to copy from first"); dragging = false; break; }
            if (doc.cur().isAdjust() || doc.cur().isFill() || doc.editMask) { say("Retouching works on a layer's pixels"); dragging = false; break; }
            if (mode == RT_CLONE && (!cloneAligned || !cloneHaveOffset)) { cloneDX = cloneSrcX - c.x; cloneDY = cloneSrcY - c.y; cloneHaveOffset = true; }
            remember(Toolbar::label(Toolbar::current())); strokeBegin(doc, stroke);
            changed(retouchTo(doc, stroke, c.x, c.y, rtSize, rtHard, retouchOpts(), true));
            break;
        }
        case T_PEN: {
            const int pm = penMode();
            int sb, kn, part;
            penGrabSub = penGrabKnot = -1; penGrabPart = 0;
            if (pm == PN_PEN || pm == PN_CURVE) {
                if (penSub >= 0 && penSub < (int)doc.workPath.size()) {
                    Subpath& sp = doc.workPath[penSub];
                    if (sp.knots.size() >= 2 && std::hypot(sp.knots[0].ax - c.x, sp.knots[0].ay - c.y) * zoom <= 8) {   // on the first: closed
                        remember("Close Path"); sp.closed = true; if (pm == PN_CURVE) smoothAll(sp); penSub = -1; dragging = false; changed(); break;
                    }
                } else if (penHit(c, 7, sb, kn, part, false)) {   // on an existing anchor with nothing open: drag it
                    remember("Move Anchor"); penGrabSub = sb; penGrabKnot = kn; penGrabPart = 0; break;
                }
                remember(pm == PN_CURVE ? "Curvature Pen" : "Pen");
                if (penSub < 0 || penSub >= (int)doc.workPath.size()) { doc.workPath.push_back(Subpath()); penSub = (int)doc.workPath.size() - 1; }
                Subpath& sp = doc.workPath[penSub];
                Knot k; k.ax = k.ix = k.ox = c.x; k.ay = k.iy = k.oy = c.y;
                sp.knots.push_back(k);
                if (pm == PN_CURVE) smoothAll(sp);
                else { penGrabSub = penSub; penGrabKnot = (int)sp.knots.size() - 1; penGrabPart = 2; }   // a drag pulls its handles out
                changed();
                break;
            }
            if (pm == PN_FREE) { penFree = {c.x, c.y}; break; }
            if (pm == PN_ADD) {
                int seg; float t;
                if (penNearSegment(c, 8, sb, seg, t)) { remember("Add Anchor Point"); penSplit(doc.workPath[sb], seg, t); changed(); }
                dragging = false; break;
            }
            if (pm == PN_DEL) {
                if (penHit(c, 8, sb, kn, part, false)) {
                    remember("Delete Anchor Point");
                    auto& ks = doc.workPath[sb].knots; ks.erase(ks.begin() + kn);
                    if (ks.empty()) doc.workPath.erase(doc.workPath.begin() + sb);
                    else if (ks.size() < 3) doc.workPath[sb].closed = false;
                    penSub = -1; changed();
                }
                dragging = false; break;
            }
            if (pm == PN_CONVERT) {   // a click: smooth <-> corner; a drag from an anchor pulls new handles
                if (penHit(c, 8, sb, kn, part, true)) {
                    remember("Convert Point");
                    Knot& k = doc.workPath[sb].knots[kn];
                    if (part == 0) { if (k.smooth) { k.ix = k.ox = k.ax; k.iy = k.oy = k.ay; k.smooth = false; } else smoothKnot(doc.workPath[sb], kn); penGrabSub = sb; penGrabKnot = kn; penGrabPart = 4; }
                    else { k.smooth = false; penGrabSub = sb; penGrabKnot = kn; penGrabPart = part; }   // a handle on its own: a corner
                    changed();
                }
                break;
            }
            if (pm == PN_DIRECT) { if (typePathMarkerPress(c)) break; if (penHit(c, 8, sb, kn, part, true)) { remember("Move Point"); penGrabSub = sb; penGrabKnot = kn; penGrabPart = part; } break; }
            if (pm == PN_PATHSEL) {
                if (typePathMarkerPress(c)) break;
                int seg; float t;
                if (penHit(c, 8, sb, kn, part, false) || penNearSegment(c, 8, sb, seg, t)) { remember("Move Path"); penGrabSub = sb; penGrabPart = 3; }
                break;
            }
            break;
        }
        case T_TYPE:
            if (typeLayer >= 0 && typeLayer < (int)doc.layers.size() && doc.layers[typeLayer].isText()) {
                Text& activeText = doc.layers[typeLayer].text;
                const float* b = activeText.box;
                const int handle = typeBoxHandleAt(activeText, c);
                if (handle >= 0) {
                    if (groupLocked(doc, doc.cur().groupId)) { say("This layer is locked by its group"); dragging = false; break; }
                    typeRemember("Resize Text Box"); typeResizeHandle = handle;
                    typeBoxX0 = activeText.x; typeBoxY0 = activeText.y; typeBoxW0 = activeText.boxWidth; typeBoxH0 = activeText.boxHeight;
                    typeBoxAnchorX0 = activeText.x; typeBoxAnchorY0 = activeText.y;
                    typeBoxScaleX0 = activeText.scaleX; typeBoxScaleY0 = activeText.scaleY; typeBoxRotation0 = activeText.rotation;
                    typeBoxDrag = false; break;
                }
                const int pointHandle = io.KeyCtrl ? typePointHandleAt(activeText, c) : -1;
                if (pointHandle >= 0) {
                    if (groupLocked(doc, doc.cur().groupId)) { say("This layer is locked by its group"); dragging = false; break; }
                    typeRemember("Transform Text"); typeScaleHandle = pointHandle;
                    std::copy(activeText.box, activeText.box + 4, typePointBox0);
                    typeScaleX0 = activeText.scaleX; typeScaleY0 = activeText.scaleY;
                    typePointAnchorX0 = activeText.x; typePointAnchorY0 = activeText.y; typePointRotation0 = activeText.rotation;
                    typeBoxDrag = false; break;
                }
                const bool inside = c.x >= b[0] && c.x <= b[2] && c.y >= b[1] && c.y <= b[3];
                const ImVec2 local = typeToTextPoint(activeText, c);
                const bool insideParagraph = activeText.boxWidth > 0 && local.x >= activeText.x && local.x <= activeText.x + activeText.boxWidth && local.y >= activeText.y && local.y <= activeText.y + activeText.boxHeight;
                const float dx = std::max({b[0] - c.x, 0.0f, c.x - b[2]});
                const float dy = std::max({b[1] - c.y, 0.0f, c.y - b[3]});
                // PhotoCraft's Type tool moves an active text layer from just outside its
                // glyphs, or from anywhere with Ctrl held, while preserving the grab offset.
                const float outsideDistance = std::hypot(dx, dy) * zoom;
                if (io.KeyCtrl && !inside && !insideParagraph && outsideDistance <= 28.0f) {
                    if (groupLocked(doc, doc.cur().groupId)) { say("This layer is locked by its group"); dragging = false; break; }
                    typeRemember("Rotate Text"); typeRotating = true;
                    typeRotatePivotX = activeText.x; typeRotatePivotY = activeText.y;
                    typeRotateStartAngle = std::atan2(c.y - typeRotatePivotY, c.x - typeRotatePivotX);
                    typeRotation0 = activeText.rotation;
                    break;
                }
                if (io.KeyCtrl || (!inside && !insideParagraph && outsideDistance <= 38.0f)) {
                    if (groupLocked(doc, doc.cur().groupId)) { say("This layer is locked by its group"); dragging = false; break; }
                    typeRemember("Move Text"); typeMoving = true;
                    typeMoveX = doc.cur().text.x; typeMoveY = doc.cur().text.y;
                    break;
                }
                if (!inside && !insideParagraph) typeCommit();
                dragging = false;
            } else if (typeMaskEditing) {
                typeCommit(); dragging = false;
            } else {
                float distance = 0;
                int pathIndex = -1;
                const float along = typePathNearest(doc.workPath, c.x, c.y, &distance, &pathIndex);
                if (!typeIsMask() && typePathLength(doc.workPath) > 0 && distance * zoom <= 8.0f) typeBeginOnPath(c, along, pathIndex);
                else typeBegin(c, io.KeyShift && !typeIsMask());
                typeBoxDrag = typeLayer >= 0 && !doc.cur().text.pathText;
            }
            break;
        case T_MARK: {
            const int mm = markMode();
            if (mm == MK_RULER) { doc.hasRuler = true; doc.ruler[0] = doc.ruler[2] = c.x; doc.ruler[1] = doc.ruler[3] = c.y; break; }
            auto& v = markList(mm);
            const int hit = markNear(v, c, 10);
            if (io.KeyAlt) { if (hit >= 0) { remember("Delete Mark"); v.erase(v.begin() + hit); if (mm == MK_NOTE) noteSel = -1; } dragging = false; break; }
            if (hit >= 0) { markDrag = hit; if (mm == MK_NOTE) noteSel = hit; break; }
            if (c.x < 0 || c.y < 0 || c.x >= doc.w || c.y >= doc.h) { dragging = false; break; }
            if (mm == MK_SAMPLER && v.size() >= 10) { say("Ten samplers at most: Alt+click one to take it away"); dragging = false; break; }
            remember(mm == MK_SAMPLER ? "Color Sampler" : mm == MK_COUNT ? "Count" : "Note");
            Doc::Mark k; k.x = c.x; k.y = c.y; v.push_back(k);
            markDrag = (int)v.size() - 1;
            if (mm == MK_NOTE) noteSel = markDrag;
            break;
        }
        case T_HEAL: {
            const int hm = healMode();
            if (hm == HM_HEALING && io.KeyAlt) { healSrcSet = true; healHaveOffset = false; healSrcX = c.x; healSrcY = c.y; say("Healing source set: paint over what to fix"); dragging = false; break; }
            if (!pixelLayer()) { say("The healing tools work on a layer's pixels"); dragging = false; break; }
            if (hm == HM_HEALING && !healSrcSet) { say("Alt+click where to copy from first"); dragging = false; break; }
            if (hm == HM_PATCH || hm == HM_MOVE) { patchDrag = insideSel(c); if (!patchDrag) lasso = {c.x, c.y}; break; }
            if (hm == HM_HEALING && (!healAligned || !healHaveOffset)) { healDX = healSrcX - c.x; healDY = healSrcY - c.y; healHaveOffset = true; }
            healPts = {c.x, c.y};
            break;
        }
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
        const ImVec2 penLast = dragNow; (void)penLast;
        dragNow = c;
        switch (t) {
        case T_MOVE: {
            Layer& l = doc.cur();
            if (moveTextLayer && l.isText()) {
                moveTextTo(moveTextStartX + c.x - dragStart.x, moveTextStartY + c.y - dragStart.y); break;
            }
            const int nx = moveStartX + (int)std::lround(c.x - dragStart.x), ny = moveStartY + (int)std::lround(c.y - dragStart.y);
            const int dx = nx - l.x, dy = ny - l.y;
            if (moveGroupId && (dx || dy)) {
                moveGroupContents(doc, moveGroupId, dx, dy);
                changed();
            } else if (nx != l.x || ny != l.y) { l.x = nx; l.y = ny; changed(); }
            break;
        }
        case T_TYPE:
            if (typeRotating) typeRotateByPointer(c, io.KeyShift);
            else if (typeMoving) typeMoveByPointer(dragStart, c);
            else if (typeScaleHandle >= 0) typeScaleByPointer(c, io.KeyShift);
            else if (typeBoxDrag && typeLayer >= 0 && typeLayer < (int)doc.layers.size() && doc.layers[typeLayer].isText()
                     && std::hypot(c.x - dragStart.x, c.y - dragStart.y) * zoom >= 3) {
                Layer& L = doc.layers[typeLayer];
                L.text.x = std::min(dragStart.x, c.x); L.text.y = std::min(dragStart.y, c.y);
                L.text.boxWidth = std::max(4.0f, std::fabs(c.x - dragStart.x)); L.text.boxHeight = std::max(4.0f, std::fabs(c.y - dragStart.y));
                renderFill(doc, L); changed();
            }
            else if (typeResizeHandle >= 0) typeResizeByPointer(c, io.KeyAlt);
            break;
        case T_ARTBOARD:
            if (artboardMoving && artboardActive >= 0 && artboardActive < (int)doc.artboards.size()) {
                Artboard& board = doc.artboards[artboardActive];
                const int x = (int)std::lround(c.x - dragStart.x), y = (int)std::lround(c.y - dragStart.y);
                moveGroupContents(doc, board.groupId, x - artboardMoveDX, y - artboardMoveDY);
                artboardMoveDX = x; artboardMoveDY = y;
                changed();
            }
            break;
        case T_LASSO: if (std::hypot(c.x - lasso[lasso.size() - 2], c.y - lasso.back()) * zoom > 2) { lasso.push_back(c.x); lasso.push_back(c.y); } break;
        case T_DROPPER: pickColour(c, io.KeyAlt); break;
        case T_CROP: if(straightenLine)straightenEnd=c;else if(cropMoveImage)cropImageMoveTo(m);else cropDragTo(c, io.KeyShift, io.KeyAlt); break;
        case T_PCROP:
            if (pcropDragPart >= 0) { pcropQ[pcropDragPart * 2] = c.x; pcropQ[pcropDragPart * 2 + 1] = c.y; }
            else if (pcropDragPart == -2) {
                for (int i = 0; i < 4; ++i) { pcropQ[i * 2] = pcropMoveQ[i * 2] + c.x - pcropMoveStart.x; pcropQ[i * 2 + 1] = pcropMoveQ[i * 2 + 1] + c.y - pcropMoveStart.y; }
            }
            break;
        case T_SLICE_SELECT:
            if (sliceActive >= 0) {
                Slice& s = doc.slices[sliceActive];
                const int dx = std::clamp((int)std::lround(c.x - dragStart.x), -sliceStartRect.x0, doc.w - sliceStartRect.x1);
                const int dy = std::clamp((int)std::lround(c.y - dragStart.y), -sliceStartRect.y0, doc.h - sliceStartRect.y1);
                s.rect = {sliceStartRect.x0 + dx, sliceStartRect.y0 + dy, sliceStartRect.x1 + dx, sliceStartRect.y1 + dy};
                sliceDidMove = dx != 0 || dy != 0;
            }
            break;
        case T_FRAME:
            break;
        case T_QUICK: if (std::hypot(c.x - quickPts[quickPts.size() - 2], c.y - quickPts.back()) * zoom > 3) { quickPts.push_back(c.x); quickPts.push_back(c.y); } break;
        case T_PENCIL: if (stroke.on) changed(pencilTo(doc, stroke, c.x, c.y, pencilSize, pencilOpacity, brushColour(), false, false)); break;
        case T_RETOUCH: if (stroke.on) changed(retouchTo(doc, stroke, c.x, c.y, rtSize, rtHard, retouchOpts(), false)); break;
        case T_PEN: {
            if (typePathDragPart >= 0) { typePathDragTo(c); break; }
            if (penMode() == PN_FREE) { if (penFree.size() >= 2 && std::hypot(c.x - penFree[penFree.size() - 2], c.y - penFree.back()) * zoom > 3) { penFree.push_back(c.x); penFree.push_back(c.y); } break; }
            if (penGrabSub < 0 || penGrabSub >= (int)doc.workPath.size()) break;
            Subpath& sp = doc.workPath[penGrabSub];
            const float dx = c.x - penLast.x, dy = c.y - penLast.y;
            if (penGrabPart == 3) { for (auto& k : sp.knots) { k.ax += dx; k.ix += dx; k.ox += dx; k.ay += dy; k.iy += dy; k.oy += dy; } changed(); break; }
            if (penGrabKnot < 0 || penGrabKnot >= (int)sp.knots.size()) break;
            Knot& k = sp.knots[penGrabKnot];
            if (penGrabPart == 0) { k.ax += dx; k.ay += dy; k.ix += dx; k.iy += dy; k.ox += dx; k.oy += dy; }
            else if (penGrabPart == 2 || penGrabPart == 4) {   // the out handle to the mouse; a smooth knot's in handle mirrors it
                k.ox = c.x; k.oy = c.y;
                if (k.smooth || penGrabPart == 4 || penMode() == PN_PEN) { k.ix = 2 * k.ax - c.x; k.iy = 2 * k.ay - c.y; k.smooth = true; }
            } else if (penGrabPart == 1) {
                k.ix = c.x; k.iy = c.y;
                if (k.smooth) { const float l = std::hypot(k.ox - k.ax, k.oy - k.ay), m = std::max(1e-3f, std::hypot(k.ix - k.ax, k.iy - k.ay)); k.ox = k.ax - (k.ix - k.ax) / m * l; k.oy = k.ay - (k.iy - k.ay) / m * l; }
            }
            if (penMode() == PN_CURVE) smoothAll(sp);
            changed();
            break;
        }
        case T_MARK:
            if (markMode() == MK_RULER) { doc.ruler[2] = c.x; doc.ruler[3] = c.y; if (io.KeyShift) {   // in 45-degree steps
                const float dx = c.x - doc.ruler[0], dy = c.y - doc.ruler[1], l = std::hypot(dx, dy), a = std::round(std::atan2(dy, dx) / 0.785398f) * 0.785398f;
                doc.ruler[2] = doc.ruler[0] + std::cos(a) * l; doc.ruler[3] = doc.ruler[1] + std::sin(a) * l; } }
            else if (markDrag >= 0) { auto& v = markList(markMode()); if (markDrag < (int)v.size()) { v[markDrag].x = std::clamp(c.x, 0.0f, (float)doc.w - 1); v[markDrag].y = std::clamp(c.y, 0.0f, (float)doc.h - 1); } }
            break;
        case T_HEAL:
            if (healMode() >= HM_PATCH) { if (!patchDrag && lasso.size() >= 2 && std::hypot(c.x - lasso[lasso.size() - 2], c.y - lasso.back()) * zoom > 2) { lasso.push_back(c.x); lasso.push_back(c.y); } }
            else if (healPts.size() >= 2 && std::hypot(c.x - healPts[healPts.size() - 2], c.y - healPts.back()) * zoom > 2) { healPts.push_back(c.x); healPts.push_back(c.y); }
            break;
        case T_BRUSH: case T_ERASER:
            if (stroke.on) changed(strokeTo(doc, stroke, c.x, c.y, t == T_BRUSH ? brushSize : eraserSize, t == T_BRUSH ? brushHard : eraserHard, t == T_BRUSH ? brushOpacity : eraserOpacity, brushColour(), t == T_ERASER, false));
            break;
        default: break;
        }
    }
    if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging = false;
        switch (t) {
        case T_TYPE: typeMoving = false; typeRotating = false; typeBoxDrag = false; typeResizeHandle = -1; typeScaleHandle = -1; break;
        case T_ROTATE: {
            const ImVec2 pivot(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
            const float pointer = std::atan2(m.y - pivot.y, m.x - pivot.x);
            const float delta = std::remainder(pointer - rotateStartPointer, 6.283185307179586f);
            float angle = rotateStartAngle + delta * 57.29577951308232f;
            if (io.KeyShift) angle = std::round(angle / 15.0f) * 15.0f;
            viewRotation = std::remainder(angle, 360.0f);
            break;
        }
        case T_RECT: case T_ELLIPSE:
            if (std::fabs(c.x - dragStart.x) < 1 && std::fabs(c.y - dragStart.y) < 1) { if (modeFromKeys() == SEL_NEW) { remember("Deselect"); deselect(); } }
            else finishShape(t == T_RECT ? shapeRect(doc, (int)std::lround(dragStart.x), (int)std::lround(dragStart.y), (int)std::lround(c.x), (int)std::lround(c.y))
                                         : shapeEllipse(doc, (int)std::lround(dragStart.x), (int)std::lround(dragStart.y), (int)std::lround(c.x), (int)std::lround(c.y)),
                             modeFromKeys(), t == T_RECT ? "Rectangle Select" : "Ellipse Select");
            break;
        case T_LASSO: if (lasso.size() >= 6) finishShape(shapePolygon(doc, lasso), modeFromKeys(), "Lasso"); lasso.clear(); break;
        case T_BRUSH: case T_ERASER: case T_PENCIL: case T_RETOUCH: strokeEnd(stroke); break;
        case T_CROP:
            if(straightenLine){straightenCrop(straightenStart,c,straightenFit);straightenLine=false;if(straightenFit)straightenArmed=false;}
            else if (std::fabs(cropR[2] - cropR[0]) < 2 || std::fabs(cropR[3] - cropR[1]) < 2) cropPending = false;
            else if(cropDragPart==9&&!cropClassicMode&&cropAutoCenter)centerCropAtScreen(ImVec2(canvasPos.x+canvasSize.x*.5f,canvasPos.y+canvasSize.y*.5f));
            if(cropPressSnapshotValid){if(cropPending)cropHistoryRecord(cropUndo,cropRedo,cropPressSnapshot,cropSnapshot());cropPressSnapshotValid=false;}
            cropMoveImage=false;cropDragPart=-1;cropSpacePanning=false; break;
        case T_PCROP:
            if (pcropDragPart == -1) {
                if (std::hypot(c.x - dragStart.x, c.y - dragStart.y) * zoom >= 3) {
                    const float q[8] = {dragStart.x, dragStart.y, c.x, dragStart.y, c.x, c.y, dragStart.x, c.y};
                    pcropSet(q); pcropClicks.clear();
                } else {
                    pcropClicks.push_back(c);
                    if (pcropClicks.size() == 4) pcropOrder(pcropClicks);
                }
            } else if (pcropPending) {
                const float q[8] = {pcropQ[0], pcropQ[1], pcropQ[2], pcropQ[3], pcropQ[4], pcropQ[5], pcropQ[6], pcropQ[7]};
                if (pcropValid(q)) pcropSet(q);
                else pcropPending = false;
            }
            pcropDragPart = -1;
            break;
        case T_SLICE:
            if (std::fabs(c.x - dragStart.x) >= 3 && std::fabs(c.y - dragStart.y) >= 3) {
                Rect r{(int)std::floor(std::min(c.x, dragStart.x)), (int)std::floor(std::min(c.y, dragStart.y)),
                       (int)std::ceil(std::max(c.x, dragStart.x)), (int)std::ceil(std::max(c.y, dragStart.y))};
                createSlice(r);
            }
            break;
        case T_SLICE_SELECT:
            if (sliceActive >= 0 && sliceDidMove) changed();
            break;
        case T_FRAME:
            if (std::fabs(c.x - dragStart.x) >= 8 && std::fabs(c.y - dragStart.y) >= 8) {
                const Rect r{(int)std::floor(std::min(c.x, dragStart.x)), (int)std::floor(std::min(c.y, dragStart.y)),
                             (int)std::ceil(std::max(c.x, dragStart.x)), (int)std::ceil(std::max(c.y, dragStart.y))};
                createFrame(r);
            }
            break;
        case T_ARTBOARD:
            if (artboardMoving) {
                artboardMoving = false;
            } else if (std::fabs(c.x - dragStart.x) >= 3 && std::fabs(c.y - dragStart.y) >= 3) {
                createArtboard({(int)std::floor(std::min(c.x, dragStart.x)), (int)std::floor(std::min(c.y, dragStart.y)),
                                (int)std::ceil(std::max(c.x, dragStart.x)), (int)std::ceil(std::max(c.y, dragStart.y))});
            } else {
                const int x = (int)std::lround(c.x), y = (int)std::lround(c.y);
                createArtboard({x, y, x + artboardWidth, y + artboardHeight});
            }
            break;
        case T_QUICK: quickSelectStroke(); break;
        case T_PEN:
            typePathDragPart = -1; typePathDragLayer = -1; typePathDragTab = -1; typePathDragRemembered = false;
            if (penMode() == PN_FREE && penFree.size() >= 6) {   // the dragged line as a smooth path (a point every few pixels)
                remember("Freeform Pen");
                Subpath sp;
                const float step = 12 / std::max(zoom, 0.05f);
                float lx = penFree[0], ly = penFree[1];
                Knot k0; k0.ax = lx; k0.ay = ly; sp.knots.push_back(k0);
                for (size_t i = 2; i + 1 < penFree.size(); i += 2)
                    if (std::hypot(penFree[i] - lx, penFree[i + 1] - ly) >= step || i + 2 >= penFree.size()) { Knot k; k.ax = lx = penFree[i]; k.ay = ly = penFree[i + 1]; sp.knots.push_back(k); }
                sp.closed = sp.knots.size() >= 3 && std::hypot(sp.knots.back().ax - sp.knots[0].ax, sp.knots.back().ay - sp.knots[0].ay) * zoom < 12;
                if (sp.closed) sp.knots.pop_back();
                smoothAll(sp);
                doc.workPath.push_back(sp);
                changed();
            }
            penFree.clear();
            penGrabSub = penGrabKnot = -1;
            break;
        case T_MARK:
            markDrag = -1;
            if (markMode() == MK_RULER && std::hypot(doc.ruler[2] - doc.ruler[0], doc.ruler[3] - doc.ruler[1]) * zoom < 2) doc.hasRuler = false;
            break;
        case T_SHAPE: {
            const Shape sh = shapeFromDrag(dragStart, c);
            if (std::fabs(sh.x1 - sh.x0) * zoom < 2 && std::fabs(sh.y1 - sh.y0) * zoom < 2) break;
            remember(Toolbar::current() == Toolbar::CUSTOM_SHAPE ? std::string("New ") + CUSTOM_SHAPE_NAMES[customShapePick] : std::string("New ") + shapeName(sh.kind));
            Fill f; f.kind = FILL_SOLID; for (int k = 0; k < 3; ++k) f.colour[k] = fg[k] / 255.0f;
            addShapeLayer(doc, sh, f);
            changed();
            break;
        }
        case T_HEAL: {
            const int hm = healMode();
            if (hm < HM_PATCH) { healStrokeEnd(); break; }
            if (patchDrag) {
                patchDrag = false;
                const int dx = (int)std::lround(c.x - dragStart.x), dy = (int)std::lround(c.y - dragStart.y);
                if (!dx && !dy) break;
                if (hm == HM_PATCH) { remember("Patch"); healRegion(doc, doc.sel, dx, dy); changed(); }
                else { say("Moving..."); remember("Content-Aware Move"); contentAwareMove(doc, dx, dy); changed(); selChanged(); say(""); }
            } else {
                if (lasso.size() >= 6) finishShape(shapePolygon(doc, lasso), modeFromKeys(), hm == HM_PATCH ? "Patch" : "Content-Aware Move");
                lasso.clear();
            }
            break;
        }
        case T_OBJECT: objectSelect(dragStart.x, dragStart.y, c.x, c.y); break;
        case T_GRADIENT:
            if (std::hypot(c.x - dragStart.x, c.y - dragStart.y) * zoom >= 3) {
                if (doc.cur().isAdjust() || doc.cur().isFill()) { say("The Gradient tool paints a layer's pixels (or its mask)"); break; }
                remember("Gradient"); gradientFill(doc, dragStart.x, dragStart.y, c.x, c.y, fg, bgc, gradToolStyle, gradToolReverse, gradToolOpacity); changed();
            }
            break;
        default: break;
        }
    }
    if (t == T_CROP && cropPending && !dragging && dlg == D_NONE) {   // Enter crops, Escape lets it go
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) applyCrop();
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { cropPending = false;cropFrameDismissed=true;cropClearHistory();straightenArmed=false;straightenLine=false; }
        else {
            const bool defaultFrame=cropR[0]==0&&cropR[1]==0&&cropR[2]==doc.w&&cropR[3]==doc.h&&std::fabs(cropAngle)<.001f;
            const int key=ImGui::IsKeyPressed(ImGuiKey_H)?(int)ImGuiKey_H:ImGui::IsKeyPressed(ImGuiKey_P)?(int)ImGuiKey_P:0;
            const int cropOption=cropOptionKeyAction(cropPending,defaultFrame,dragging,io.KeyShift,io.KeyAlt,io.KeyCtrl,io.KeySuper,key,cropClassicMode,cropShowArea);
            if(cropOption)cropConsumedShortcutKey=key;
            if(cropOption==2)syncCropView();
            else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)) {nudgeCrop(-1,0,ImGui::GetIO().KeyShift);cropConsumedShortcutKey=(int)ImGuiKey_LeftArrow;}
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow,true)) {nudgeCrop(1,0,ImGui::GetIO().KeyShift);cropConsumedShortcutKey=(int)ImGuiKey_RightArrow;}
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,true)) {nudgeCrop(0,-1,ImGui::GetIO().KeyShift);cropConsumedShortcutKey=(int)ImGuiKey_UpArrow;}
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow,true)) {nudgeCrop(0,1,ImGui::GetIO().KeyShift);cropConsumedShortcutKey=(int)ImGuiKey_DownArrow;}
            else if (ImGui::IsKeyPressed(ImGuiKey_X)) {swapCropOrientation();cropConsumedShortcutKey=(int)ImGuiKey_X;}
            else if (ImGui::IsKeyPressed(ImGuiKey_O)) {
                cropConsumedShortcutKey=(int)ImGuiKey_O;
                if (ImGui::GetIO().KeyShift) cropOverlayOrientation = (cropOverlayOrientation + 1) % (cropOverlay == 3 ? 2 : cropOverlay == 5 ? 4 : 1);
                else { cropOverlay = (cropOverlay + 1) % 6; cropOverlayOrientation = 0; }
            }
        }
    }
    if (t == T_PCROP && !dragging && dlg == D_NONE) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) applyPerspectiveCrop();
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) pcropCancel();
    }
    if(cropFrontImageKeyAllowed(t==T_CROP,dragging,io.WantTextInput,io.KeyShift,io.KeyAlt,io.KeyCtrl,io.KeySuper)&&dlg==D_NONE&&ImGui::IsKeyPressed(ImGuiKey_I)){
        cropFrontImage();cropConsumedShortcutKey=(int)ImGuiKey_I;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && t != T_HAND && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) moveTextLayer = false;
}

static void drawCanvas() {
    syncCropView();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), C_BG);
    const ImVec2 a = toScreen(0, 0), b = toScreen((float)doc.w, 0), c = toScreen((float)doc.w, (float)doc.h), d = toScreen(0, (float)doc.h);
    dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
    const ImVec2 uv0(0, 0), uv1((float)doc.w / 16, 0), uv2((float)doc.w / 16, (float)doc.h / 16), uv3(0, (float)doc.h / 16);
    dl->AddImageQuad((ImTextureID)(intptr_t)checkerTex, a, b, c, d, uv0, uv1, uv2, uv3);
    glBindTexture(GL_TEXTURE_2D, viewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, zoom >= 2 ? GL_NEAREST : GL_LINEAR);
    dl->AddImageQuad((ImTextureID)(intptr_t)viewTex, a, b, c, d, uv0, ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1));
    dl->AddQuad(a, b, c, d, C_LINE);
    if (std::fabs(viewRotation) > 0.05f) {
        const ImVec2 q(canvasPos.x + canvasSize.x - 24, canvasPos.y + 24);
        dl->AddCircleFilled(q, 14, C_WELL, 24);
        dl->AddCircle(q, 13.5f, C_LINE, 24);
        const float a0 = -1.57079632679f + viewRotation * 0.017453292519943295f;
        const ImVec2 tip(q.x + std::cos(a0) * 8, q.y + std::sin(a0) * 8);
        dl->AddLine(q, tip, C_PINK, 2);
        dl->AddCircleFilled(tip, 2, C_PINK, 12);
        dl->AddText(ImVec2(q.x - 3, q.y + 4), C_TEXT, "N");
    }

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
        if (tool == T_RECT) {
            const ImVec2 q[4] = {p0, toScreen(dragNow.x, dragStart.y), p1, toScreen(dragStart.x, dragNow.y)};
            dl->AddPolyline(q, 4, C_PINK, ImDrawFlags_Closed, 1.5f);
        } else {
            const float cx = (dragStart.x + dragNow.x) * 0.5f, cy = (dragStart.y + dragNow.y) * 0.5f;
            std::vector<ImVec2> q; q.reserve(64);
            for (int i = 0; i < 64; ++i) { const float a = i * 6.28318530718f / 64; q.push_back(toScreen(cx + (dragNow.x - dragStart.x) * 0.5f * std::cos(a), cy + (dragNow.y - dragStart.y) * 0.5f * std::sin(a))); }
            dl->AddPolyline(q.data(), (int)q.size(), C_PINK, ImDrawFlags_Closed, 1.5f);
        }
    }
    auto path = [&](const std::vector<float>& v, bool toMouse) {
        for (size_t i = 0; i + 3 < v.size(); i += 2) dl->AddLine(toScreen(v[i], v[i + 1]), toScreen(v[i + 2], v[i + 3]), C_PINK, 1.5f);
        if (toMouse && v.size() >= 2) dl->AddLine(toScreen(v[v.size() - 2], v.back()), io.MousePos, C_PINK, 1.0f);
    };
    // the Lasso draws nothing while you drag: its selection appears when you let go. The Polygonal Lasso shows its
    // points, since it's built click by click.
    if (!poly.empty()) path(poly, true);
    if (tool == T_PEN) {   // the work path; its anchors, and the handles of curved ones
        const auto polys = flattenPath(doc.workPath);
        for (size_t i = 0; i < polys.size(); ++i) {
            std::vector<ImVec2> pts;
            for (size_t k = 0; k + 1 < polys[i].size(); k += 2) pts.push_back(toScreen(polys[i][k], polys[i][k + 1]));
            if (pts.size() >= 2) {
                const ImDrawFlags fl = doc.workPath[i].closed ? ImDrawFlags_Closed : 0;
                dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 160), fl, 3); dl->AddPolyline(pts.data(), (int)pts.size(), C_PINK, fl, 1.5f);
            }
        }
        const bool showHandles = penMode() != PN_PATHSEL;
        for (const auto& sp : doc.workPath)
            for (const auto& k : sp.knots) {
                const ImVec2 a = toScreen(k.ax, k.ay);
                if (showHandles && (k.ix != k.ax || k.iy != k.ay || k.ox != k.ax || k.oy != k.ay)) {
                    const ImVec2 hi = toScreen(k.ix, k.iy), ho = toScreen(k.ox, k.oy);
                    dl->AddLine(a, hi, IM_COL32(255, 255, 255, 160), 1); dl->AddLine(a, ho, IM_COL32(255, 255, 255, 160), 1);
                    dl->AddCircleFilled(hi, 3.5f, C_PINK); dl->AddCircleFilled(ho, 3.5f, C_PINK);
                }
                dl->AddRectFilled(ImVec2(a.x - 3.5f, a.y - 3.5f), ImVec2(a.x + 3.5f, a.y + 3.5f), IM_COL32(255, 255, 255, 255));
                dl->AddRect(ImVec2(a.x - 3.5f, a.y - 3.5f), ImVec2(a.x + 3.5f, a.y + 3.5f), C_PINK);
            }
        if ((penMode() == PN_PATHSEL || penMode() == PN_DIRECT) && doc.active >= 0 && doc.active < (int)doc.layers.size()
            && doc.cur().isText() && doc.cur().text.pathText) {
            const Text& tx = doc.cur().text; const float len = typePathLength(tx.path);
            const float end = tx.pathEnd >= 0 ? tx.pathEnd : (!tx.path.empty() && tx.path[0].closed ? tx.pathStart + len : len);
            ImVec2 a, b;
            if (typePathPointAt(tx, tx.pathStart, a, tx.pathFlip) && typePathPointAt(tx, end, b, tx.pathFlip)) {
                a = toScreen(a.x, a.y); b = toScreen(b.x, b.y);
                dl->AddLine(ImVec2(a.x-5,a.y-5),ImVec2(a.x+5,a.y+5),C_PINK,2); dl->AddLine(ImVec2(a.x-5,a.y+5),ImVec2(a.x+5,a.y-5),C_PINK,2);
                dl->AddCircleFilled(b, 5, IM_COL32(255,255,255,255)); dl->AddCircle(b, 5, C_PINK, 16, 2);
            }
        }
        if (penFree.size() >= 4) { std::vector<ImVec2> pts; for (size_t k = 0; k + 1 < penFree.size(); k += 2) pts.push_back(toScreen(penFree[k], penFree[k + 1])); dl->AddPolyline(pts.data(), (int)pts.size(), C_PINK, 0, 1.5f); }
        // the Pen's next segment, to the mouse
        if ((penMode() == PN_PEN || penMode() == PN_CURVE) && penSub >= 0 && penSub < (int)doc.workPath.size() && !doc.workPath[penSub].knots.empty() && ImGui::IsWindowHovered() && !dragging) {
            const Knot& l = doc.workPath[penSub].knots.back();
            dl->AddLine(toScreen(l.ax, l.ay), io.MousePos, IM_COL32(255, 255, 255, 120), 1);
        }
    }
    if (tool == T_TYPE) {   // the text layers' boxes, faint; the one being typed in, clear
        const auto workPath = flattenPath(doc.workPath);
        for (size_t i = 0; i < workPath.size(); ++i) {
            std::vector<ImVec2> points;
            for (size_t k = 0; k + 1 < workPath[i].size(); k += 2) points.push_back(toScreen(workPath[i][k], workPath[i][k + 1]));
            if (points.size() >= 2) dl->AddPolyline(points.data(), (int)points.size(), IM_COL32(0, 123, 183, 155), doc.workPath[i].closed ? ImDrawFlags_Closed : 0, 1.5f);
        }
        for (int i = 0; i < (int)doc.layers.size(); ++i) {
            const Layer& L = doc.layers[i];
            if (!L.isText() || !L.visible) continue;
            const float* b = L.text.box;
            const float x0 = L.text.boxWidth > 0 ? L.text.x : b[0], y0 = L.text.boxWidth > 0 ? L.text.y : b[1];
            const float x1 = L.text.boxWidth > 0 ? L.text.x + L.text.boxWidth : b[2], y1 = L.text.boxWidth > 0 ? L.text.y + L.text.boxHeight : b[3];
            const auto framePoint = [&](float x, float y) {
                const float a = L.text.rotation * 0.017453292519943295f, cs = std::cos(a), sn = std::sin(a);
                const float dx = (x - L.text.x) * L.text.scaleX, dy = (y - L.text.y) * L.text.scaleY;
                return toScreen(L.text.x + cs * dx - sn * dy, L.text.y + sn * dx + cs * dy);
            };
            const ImVec2 frame[4] = {framePoint(x0, y0), framePoint(x1, y0), framePoint(x1, y1), framePoint(x0, y1)};
            dl->AddPolyline(frame, 4, i == typeLayer ? C_PINK : IM_COL32(255, 255, 255, 70), ImDrawFlags_Closed, i == typeLayer ? 1.5f : 1.0f);
            if (i == typeLayer) {
                const float hx0 = L.text.boxWidth > 0 ? x0 : b[0], hy0 = L.text.boxWidth > 0 ? y0 : b[1];
                const float hx1 = L.text.boxWidth > 0 ? x1 : b[2], hy1 = L.text.boxWidth > 0 ? y1 : b[3];
                const float mx = (hx0 + hx1) * 0.5f, my = (hy0 + hy1) * 0.5f;
                const float handles[16] = {hx0,hy0, hx1,hy0, hx1,hy1, hx0,hy1, mx,hy0, hx1,my, mx,hy1, hx0,my};
                for (int k = 0; k < 8; ++k) {
                    const ImVec2 p = L.text.boxWidth > 0 ? framePoint(handles[k * 2], handles[k * 2 + 1]) : toScreen(handles[k * 2], handles[k * 2 + 1]);
                    dl->AddRectFilled(ImVec2(p.x - 3.5f, p.y - 3.5f), ImVec2(p.x + 3.5f, p.y + 3.5f), IM_COL32(255,255,255,255));
                    dl->AddRect(ImVec2(p.x - 3.5f, p.y - 3.5f), ImVec2(p.x + 3.5f, p.y + 3.5f), C_PINK);
                }
            }
        }
    }
    {   // the marks: samplers, counts, notes and the ruler
        for (size_t i = 0; i < doc.samplers.size(); ++i) {
            const ImVec2 q = toScreen(doc.samplers[i].x, doc.samplers[i].y);
            dl->AddCircle(q, 6, IM_COL32(0, 0, 0, 220), 16, 3); dl->AddCircle(q, 6, IM_COL32(255, 255, 255, 255), 16, 1.5f);
            dl->AddLine(ImVec2(q.x - 10, q.y), ImVec2(q.x - 6, q.y), IM_COL32(255, 255, 255, 255), 1.5f); dl->AddLine(ImVec2(q.x + 6, q.y), ImVec2(q.x + 10, q.y), IM_COL32(255, 255, 255, 255), 1.5f);
            dl->AddLine(ImVec2(q.x, q.y - 10), ImVec2(q.x, q.y - 6), IM_COL32(255, 255, 255, 255), 1.5f); dl->AddLine(ImVec2(q.x, q.y + 6), ImVec2(q.x, q.y + 10), IM_COL32(255, 255, 255, 255), 1.5f);
            const std::string n = std::to_string(i + 1);
            dl->AddText(ImVec2(q.x + 9, q.y + 5), IM_COL32(0, 0, 0, 255), n.c_str()); dl->AddText(ImVec2(q.x + 8, q.y + 4), IM_COL32(255, 255, 255, 255), n.c_str());
        }
        const ImU32 cc = IM_COL32((int)(countColour[0] * 255), (int)(countColour[1] * 255), (int)(countColour[2] * 255), 255);
        for (size_t i = 0; i < doc.counts.size(); ++i) {
            const ImVec2 q = toScreen(doc.counts[i].x, doc.counts[i].y);
            const std::string n = std::to_string(i + 1);
            const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
            dl->AddCircleFilled(q, std::max(9.0f, ts.x / 2 + 5), cc);
            dl->AddText(ImVec2(q.x - ts.x / 2, q.y - ts.y / 2), IM_COL32(255, 255, 255, 255), n.c_str());
        }
        for (size_t i = 0; i < doc.notes.size(); ++i) {
            const ImVec2 q = toScreen(doc.notes[i].x, doc.notes[i].y);
            const bool sel = (int)i == noteSel && tool == T_MARK && markMode() == MK_NOTE;
            dl->AddRectFilled(ImVec2(q.x, q.y - 16), ImVec2(q.x + 16, q.y), sel ? IM_COL32(255, 220, 90, 255) : IM_COL32(240, 200, 60, 255), 2);
            dl->AddRect(ImVec2(q.x, q.y - 16), ImVec2(q.x + 16, q.y), IM_COL32(0, 0, 0, 200), 2);
            for (int k = 0; k < 3; ++k) dl->AddLine(ImVec2(q.x + 3, q.y - 12 + k * 4), ImVec2(q.x + 13, q.y - 12 + k * 4), IM_COL32(60, 50, 10, 255), 1);
            if (ImGui::IsWindowHovered() && std::hypot(io.MousePos.x - q.x - 8, io.MousePos.y - q.y + 8) < 10 && !doc.notes[i].text.empty()) ImGui::SetTooltip("%s", doc.notes[i].text.c_str());
        }
        if (doc.hasRuler) {
            const ImVec2 a0 = toScreen(doc.ruler[0], doc.ruler[1]), a1 = toScreen(doc.ruler[2], doc.ruler[3]);
            dl->AddLine(a0, a1, IM_COL32(0, 0, 0, 200), 3); dl->AddLine(a0, a1, IM_COL32(255, 255, 255, 255), 1.5f);
            for (const ImVec2& e : {a0, a1}) { dl->AddLine(ImVec2(e.x - 6, e.y), ImVec2(e.x + 6, e.y), IM_COL32(255, 255, 255, 255), 1.5f); dl->AddLine(ImVec2(e.x, e.y - 6), ImVec2(e.x, e.y + 6), IM_COL32(255, 255, 255, 255), 1.5f); }
        }
    }
    if (tool == T_MAGNETIC) {
        std::vector<ImVec2> pts;
        for (const auto& p : magneticPts) pts.push_back(toScreen((float)p.x, (float)p.y));
        for (const auto& p : magneticLive) pts.push_back(toScreen((float)p.x, (float)p.y));
        if (pts.size() >= 2) { dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 220), 0, 3); dl->AddPolyline(pts.data(), (int)pts.size(), C_PINK, 0, 1.5f); }
        for (size_t i : magneticAnchors) if (i < magneticPts.size()) dl->AddCircleFilled(toScreen((float)magneticPts[i].x, (float)magneticPts[i].y), 3.0f, C_PINK, 12);
    }
    if (!doc.artboards.empty() && (tool == T_ARTBOARD || tool == T_MOVE)) {
        for (size_t i = 0; i < doc.artboards.size(); ++i) {
            const Artboard& board = doc.artboards[i];
            const ImVec2 a = toScreen((float)board.rect.x0, (float)board.rect.y0), b = toScreen((float)board.rect.x1, (float)board.rect.y1);
            const ImU32 fill = board.background == 1 ? IM_COL32(0, 0, 0, 28) : board.background == 2 ? IM_COL32(0, 123, 183, 22) : IM_COL32(255, 255, 255, 22);
            dl->AddRectFilled(a, b, fill);
            dl->AddRect(a, b, (int)i == artboardActive ? C_PINK : IM_COL32(235, 235, 235, 210), 0, 0, (int)i == artboardActive ? 2.0f : 1.0f);
            dl->AddText(ImVec2(a.x + 5, a.y + 4), C_PINK, board.name.c_str());
        }
    }
    if (tool == T_ARTBOARD && dragging) {
        const ImVec2 a = toScreen(std::min(dragStart.x, dragNow.x), std::min(dragStart.y, dragNow.y));
        const ImVec2 b = toScreen(std::max(dragStart.x, dragNow.x), std::max(dragStart.y, dragNow.y));
        dl->AddRect(a, b, C_PINK, 0, 0, 2.0f);
    }
    if (tool == T_TYPE && typeLayer >= 0 && typeLayer < (int)doc.layers.size() && doc.layers[typeLayer].isText() && doc.layers[typeLayer].text.pathText) {
        const Text& t = doc.layers[typeLayer].text;
        const auto paths = flattenPath(t.path);
        for (size_t i = 0; i < paths.size(); ++i) {
            std::vector<ImVec2> points;
            for (size_t k = 0; k + 1 < paths[i].size(); k += 2) points.push_back(toScreen(paths[i][k], paths[i][k + 1]));
            if (points.size() >= 2) dl->AddPolyline(points.data(), (int)points.size(), C_PINK, !t.path.empty() && t.path.front().closed ? ImDrawFlags_Closed : 0, 1.0f);
        }
    }
    if (tool == T_SHAPE && dragging) {   // the shape being drawn, outlined
        const std::vector<float> o = shapeOutline(shapeFromDrag(dragStart, dragNow));
        std::vector<ImVec2> pts;
        for (size_t i = 0; i + 1 < o.size(); i += 2) pts.push_back(toScreen(o[i], o[i + 1]));
        if (pts.size() >= 2) { dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 3); dl->AddPolyline(pts.data(), (int)pts.size(), C_PINK, ImDrawFlags_Closed, 1.5f); }
    }
    if (tool == T_HEAL) {
        const int hm = healMode();
        if (dragging && !lasso.empty() && !patchDrag) path(lasso, true);
        if (dragging && patchDrag) {   // the selection's outline where it's being dragged to
            const float ox = dragNow.x - dragStart.x, oy = dragNow.y - dragStart.y;
            for (size_t i = 0; i + 3 < ants.size(); i += 4)
                dl->AddLine(toScreen(ants[i] + ox, ants[i + 1] + oy), toScreen(ants[i + 2] + ox, ants[i + 3] + oy), C_PINK, 1.5f);
        }
        if (hm < HM_PATCH && healPts.size() >= 2) {   // the stroke, until it's healed
            const float w = std::max(1.0f, healSizes[hm] * zoom);
            for (size_t i = 0; i + 1 < healPts.size(); i += 2) {
                dl->AddCircleFilled(toScreen(healPts[i], healPts[i + 1]), w * 0.5f, IM_COL32(220, 40, 60, 90), 32);
                if (i + 3 < healPts.size()) dl->AddLine(toScreen(healPts[i], healPts[i + 1]), toScreen(healPts[i + 2], healPts[i + 3]), IM_COL32(220, 40, 60, 90), w);
            }
        }
        if (hm == HM_HEALING && healSrcSet && ImGui::IsWindowHovered()) {
            const ImVec2 sp = dragging && healHaveOffset ? toScreen(dragNow.x + healDX, dragNow.y + healDY) : toScreen(healSrcX, healSrcY);
            dl->AddLine(ImVec2(sp.x - 7, sp.y), ImVec2(sp.x + 7, sp.y), IM_COL32(255, 255, 255, 220), 1.5f);
            dl->AddLine(ImVec2(sp.x, sp.y - 7), ImVec2(sp.x, sp.y + 7), IM_COL32(255, 255, 255, 220), 1.5f);
        }
        if (hm < HM_PATCH && ImGui::IsWindowHovered()) {
            const float r = healSizes[hm] * 0.5f * zoom;
            dl->AddCircle(io.MousePos, r, IM_COL32(255, 255, 255, 200), 48, 1.0f); dl->AddCircle(io.MousePos, r + 1, IM_COL32(0, 0, 0, 160), 48, 1.0f);
        }
    }
    // the brush outline
    if (tool == T_CROP && cropPending) {   // what's kept, the rest dimmed, thirds as a guide
        const float x0 = std::clamp(std::min(cropR[0], cropR[2]), 0.0f, (float)doc.w), x1 = std::clamp(std::max(cropR[0], cropR[2]), 0.0f, (float)doc.w);
        const float y0 = std::clamp(std::min(cropR[1], cropR[3]), 0.0f, (float)doc.h), y1 = std::clamp(std::max(cropR[1], cropR[3]), 0.0f, (float)doc.h);
        const ImVec2 d0 = toScreen(0, 0), d1 = toScreen((float)doc.w, 0), d2 = toScreen((float)doc.w, (float)doc.h), d3 = toScreen(0, (float)doc.h);
        const float cx=(x0+x1)*.5f,cy=(y0+y1)*.5f,cs=std::cos(cropAngle),sn=std::sin(cropAngle);
        auto turnCrop=[&](float x,float y){const float dx=x-cx,dy=y-cy;return toScreen(cx+cs*dx-sn*dy,cy+sn*dx+cs*dy);};
        const ImVec2 q0 = turnCrop(x0,y0), q1 = turnCrop(x1,y0), q2 = turnCrop(x1,y1), q3 = turnCrop(x0,y1);
        const bool showCroppedArea=cropShieldShowsOutside(cropShowArea,cropClassicMode);
        if(!showCroppedArea||cropShieldEnabled) {
            const int alpha=showCroppedArea?(int)std::lround(cropShieldOpacity*2.55f*(cropShieldAuto&&dragging?.5f:1.0f)):255;
            const ImU32 dim=showCroppedArea?IM_COL32((int)std::lround(cropShieldColor[0]*255),(int)std::lround(cropShieldColor[1]*255),(int)std::lround(cropShieldColor[2]*255),std::clamp(alpha,0,255)):C_BG;
            const ImVec2 canvasQuad[4]={d0,d1,d2,d3};
            dl->AddConvexPolyFilled(canvasQuad,4,dim);
            struct CropShieldVertex{ImVec2 pos;float x,y;};
            std::vector<CropShieldVertex> visible={{q0,cx+cs*(x0-cx)-sn*(y0-cy),cy+sn*(x0-cx)+cs*(y0-cy)},
                {q1,cx+cs*(x1-cx)-sn*(y0-cy),cy+sn*(x1-cx)+cs*(y0-cy)},
                {q2,cx+cs*(x1-cx)-sn*(y1-cy),cy+sn*(x1-cx)+cs*(y1-cy)},
                {q3,cx+cs*(x0-cx)-sn*(y1-cy),cy+sn*(x0-cx)+cs*(y1-cy)}};
            auto cross=[](ImVec2 a,ImVec2 b,ImVec2 p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);};
            float area=0;for(int i=0;i<4;++i)area+=canvasQuad[i].x*canvasQuad[(i+1)%4].y-canvasQuad[(i+1)%4].x*canvasQuad[i].y;
            const float sign=area>=0?1.0f:-1.0f;
            for(int edge=0;edge<4&&!visible.empty();++edge){
                const ImVec2 a=canvasQuad[edge],b=canvasQuad[(edge+1)%4];std::vector<CropShieldVertex> clipped;
                CropShieldVertex prev=visible.back();float pv=sign*cross(a,b,prev.pos);bool pin=pv>=-1e-4f;
                for(const CropShieldVertex& cur:visible){const float cv=sign*cross(a,b,cur.pos);const bool cin=cv>=-1e-4f;
                    if(cin!=pin){const float t=pv/(pv-cv);clipped.push_back({ImVec2(prev.pos.x+(cur.pos.x-prev.pos.x)*t,prev.pos.y+(cur.pos.y-prev.pos.y)*t),prev.x+(cur.x-prev.x)*t,prev.y+(cur.y-prev.y)*t});}
                    if(cin)clipped.push_back(cur);prev=cur;pv=cv;pin=cin;
                }visible.swap(clipped);
            }
            auto restoreTexture=[&](ImTextureID tex,bool checker){
                if(visible.size()<3)return;
                for(size_t i=1;i+1<visible.size();++i){const CropShieldVertex v[3]={visible[0],visible[i],visible[i+1]};ImVec2 pos[4]={v[0].pos,v[1].pos,v[2].pos,v[0].pos},uv[4];
                    for(int j=0;j<3;++j)uv[j]=checker?ImVec2(v[j].x/16.0f,v[j].y/16.0f):ImVec2(v[j].x/std::max(1,doc.w),v[j].y/std::max(1,doc.h));uv[3]=uv[0];
                    dl->AddImageQuad(tex,pos[0],pos[1],pos[2],pos[3],uv[0],uv[1],uv[2],uv[3]);
                }
            };
            restoreTexture((ImTextureID)(intptr_t)checkerTex,true);
            restoreTexture((ImTextureID)(intptr_t)viewTex,false);
        }
        const ImVec2 outline[4] = {q0,q1,q2,q3}; dl->AddPolyline(outline, 4, IM_COL32(255, 255, 255, 230), ImDrawFlags_Closed, 1.5f);
        if (cropOverlayShow == 1 || (cropOverlayShow == 0 && dragging)) {
            auto guide = [&](float u, float v) { return ImVec2(q0.x + (q1.x-q0.x)*u + (q3.x-q0.x)*v, q0.y + (q1.y-q0.y)*u + (q3.y-q0.y)*v); };
            auto line = [&](float u0,float v0,float u1,float v1) { dl->AddLine(guide(u0,v0),guide(u1,v1),IM_COL32(255,255,255,100),1.0f); };
            auto cross = [&](float f) { line(f,0,f,1); line(0,f,1,f); };
            if (cropOverlay == 0) { cross(1.0f/3); cross(2.0f/3); }
            else if (cropOverlay == 1) {
                const int nu=std::clamp((int)(std::hypot(q1.x-q0.x,q1.y-q0.y)/32),2,96), nv=std::clamp((int)(std::hypot(q3.x-q0.x,q3.y-q0.y)/32),2,96);
                for(int i=1;i<nu;i++) line((float)i/nu,0,(float)i/nu,1); for(int i=1;i<nv;i++) line(0,(float)i/nv,1,(float)i/nv);
            } else if (cropOverlay == 2) {
                const float w=std::hypot(q1.x-q0.x,q1.y-q0.y),h=std::hypot(q3.x-q0.x,q3.y-q0.y),u=std::min(w,h)/w,v=std::min(w,h)/h;
                line(0,0,u,v);line(1,0,1-u,v);line(0,1,u,1-v);line(1,1,1-u,1-v);
            } else if (cropOverlay == 3) {
                if(cropOverlayOrientation%2==0){line(0,0,1,1);line(1,0,0,1);}else{line(0,1,1,0);line(1,1,0,0);}
            } else if (cropOverlay == 4) { cross(0.381966f); cross(0.618034f); }
            else {
                // The logarithmic golden spiral stays inside the unit crop frame.
                constexpr float b=.30634896f, end=12.5663706f, outer=.5f;
                float a0=cropOverlayOrientation*1.5707963f;
                ImVec2 prev=guide(.5f+outer*std::cos(a0),.5f+outer*std::sin(a0));
                for(int i=1;i<=192;i++) { const float t=end*(1.0f-i/192.0f),r=outer*std::exp(-b*t),a=a0-t;
                    ImVec2 next=guide(.5f+r*std::cos(a),.5f+r*std::sin(a)); dl->AddLine(prev,next,IM_COL32(255,255,255,100),1.0f); prev=next;
                }
            }
        }
        const ImVec2 handles[8]={q0,ImVec2((q0.x+q1.x)*.5f,(q0.y+q1.y)*.5f),q1,ImVec2((q1.x+q2.x)*.5f,(q1.y+q2.y)*.5f),q2,ImVec2((q2.x+q3.x)*.5f,(q2.y+q3.y)*.5f),q3,ImVec2((q3.x+q0.x)*.5f,(q3.y+q0.y)*.5f)};
        for(const ImVec2 h:handles) { dl->AddRectFilled(ImVec2(h.x-3,h.y-3),ImVec2(h.x+3,h.y+3),IM_COL32(255,255,255,240)); dl->AddRect(ImVec2(h.x-3,h.y-3),ImVec2(h.x+3,h.y+3),IM_COL32(30,30,30,255)); }
    }
    if(tool==T_CROP&&straightenLine){const ImVec2 a=toScreen(straightenStart.x,straightenStart.y),b=toScreen(straightenEnd.x,straightenEnd.y);dl->AddLine(a,b,IM_COL32(0,0,0,220),3);dl->AddLine(a,b,IM_COL32(255,255,255,245),1.2f);const float deg=std::atan2(straightenEnd.y-straightenStart.y,straightenEnd.x-straightenStart.x)*57.2957795f;char label[32];snprintf(label,sizeof(label),"%.1f°",deg);dl->AddText(ImVec2(b.x+10,b.y-18),IM_COL32(255,255,255,255),label);}
    if (tool == T_PCROP && pcropPending) {
        ImVec2 q[4];
        for (int i = 0; i < 4; ++i) q[i] = toScreen(pcropQ[i * 2], pcropQ[i * 2 + 1]);
        const ImU32 edge = pcropValid(pcropQ) ? IM_COL32(255, 255, 255, 235) : IM_COL32(255, 70, 70, 245);
        dl->AddPolyline(q, 4, edge, ImDrawFlags_Closed, 1.7f);
        for (int k = 1; k < 4; ++k) {
            const float t = k / 4.0f;
            dl->AddLine(ImVec2(q[0].x + (q[1].x - q[0].x) * t, q[0].y + (q[1].y - q[0].y) * t), ImVec2(q[3].x + (q[2].x - q[3].x) * t, q[3].y + (q[2].y - q[3].y) * t), IM_COL32(255, 255, 255, 100));
            dl->AddLine(ImVec2(q[0].x + (q[3].x - q[0].x) * t, q[0].y + (q[3].y - q[0].y) * t), ImVec2(q[1].x + (q[2].x - q[1].x) * t, q[1].y + (q[2].y - q[1].y) * t), IM_COL32(255, 255, 255, 100));
        }
        for (const ImVec2 p : q) { dl->AddCircleFilled(p, 5, C_PINK, 16); dl->AddCircle(p, 6, IM_COL32(255, 255, 255, 240), 16); }
    } else if (tool == T_PCROP && dragging) {
        const ImVec2 q[4] = {toScreen(dragStart.x, dragStart.y), toScreen(dragNow.x, dragStart.y), toScreen(dragNow.x, dragNow.y), toScreen(dragStart.x, dragNow.y)};
        dl->AddPolyline(q, 4, IM_COL32(255, 255, 255, 235), ImDrawFlags_Closed, 1.5f);
    } else if (tool == T_PCROP && !pcropClicks.empty()) {
        for (size_t i = 1; i < pcropClicks.size(); ++i) dl->AddLine(toScreen(pcropClicks[i - 1].x, pcropClicks[i - 1].y), toScreen(pcropClicks[i].x, pcropClicks[i].y), C_PINK, 1.5f);
        for (ImVec2 p : pcropClicks) dl->AddCircleFilled(toScreen(p.x, p.y), 4, C_PINK, 12);
    }
    if (tool == T_SLICE || tool == T_SLICE_SELECT) {
        for (Rect r : autoSliceRects()) {
            const ImVec2 q[4] = {toScreen((float)r.x0, (float)r.y0), toScreen((float)r.x1, (float)r.y0),
                                 toScreen((float)r.x1, (float)r.y1), toScreen((float)r.x0, (float)r.y1)};
            dl->AddPolyline(q, 4, IM_COL32(0, 170, 230, 90), ImDrawFlags_Closed, 1.0f);
        }
        for (size_t i = 0; i < doc.slices.size(); ++i) {
            const Rect r = doc.slices[i].rect;
            const ImVec2 q[4] = {toScreen((float)r.x0, (float)r.y0), toScreen((float)r.x1, (float)r.y0),
                                 toScreen((float)r.x1, (float)r.y1), toScreen((float)r.x0, (float)r.y1)};
            const ImU32 col = (int)i == sliceActive ? C_PINK : IM_COL32(0, 170, 230, 230);
            dl->AddPolyline(q, 4, col, ImDrawFlags_Closed, (int)i == sliceActive ? 2.0f : 1.2f);
            char label[24]; snprintf(label, sizeof label, "%d", doc.slices[i].id);
            dl->AddRectFilled(ImVec2(q[0].x, q[0].y - 18), ImVec2(q[0].x + 22, q[0].y), col);
            dl->AddText(ImVec2(q[0].x + 6, q[0].y - 17), IM_COL32(255, 255, 255, 255), label);
        }
        if (tool == T_SLICE && dragging) {
            const ImVec2 q[4] = {toScreen(dragStart.x, dragStart.y), toScreen(dragNow.x, dragStart.y),
                                 toScreen(dragNow.x, dragNow.y), toScreen(dragStart.x, dragNow.y)};
            dl->AddPolyline(q, 4, C_PINK, ImDrawFlags_Closed, 1.5f);
        }
    }
    for (size_t i = 0; i < doc.layers.size(); ++i) {
        const Layer& layer = doc.layers[i];
        if (!layer.frame || !layer.isShape()) continue;
        const std::vector<float> outline = shapeOutline(layer.shape);
        std::vector<ImVec2> points;
        for (size_t k = 0; k + 1 < outline.size(); k += 2) points.push_back(toScreen(outline[k], outline[k + 1]));
        if (points.size() >= 3) dl->AddPolyline(points.data(), (int)points.size(), C_PINK, ImDrawFlags_Closed, 2.0f);
        bool hasImage = i + 1 < doc.layers.size() && doc.layers[i + 1].clip;
        if (hasImage || points.empty()) continue;
        const ImVec2 centre = toScreen((layer.shape.x0 + layer.shape.x1) * 0.5f, (layer.shape.y0 + layer.shape.y1) * 0.5f);
        const ImVec2 a(centre.x - 18, centre.y - 14), b(centre.x + 18, centre.y + 14);
        dl->AddRectFilled(a, b, IM_COL32(28, 28, 28, 210), 2);
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 180), 2);
        dl->AddLine(ImVec2(a.x + 5, b.y - 5), ImVec2(a.x + 13, a.y + 5), C_PINK, 1.5f);
        dl->AddLine(ImVec2(a.x + 13, a.y + 5), ImVec2(a.x + 20, b.y - 5), C_PINK, 1.5f);
        dl->AddText(ImVec2(centre.x + 23, centre.y - 7), IM_COL32(255, 255, 255, 220), "Place image");
    }
    if (tool == T_FRAME && dragging) {
        const ImVec2 q[4] = {toScreen(dragStart.x, dragStart.y), toScreen(dragNow.x, dragStart.y),
                             toScreen(dragNow.x, dragNow.y), toScreen(dragStart.x, dragNow.y)};
        if (frameEllipse) {
            std::vector<ImVec2> ellipse;
            for (int i = 0; i < 64; ++i) {
                const float a = i * 6.28318530718f / 64;
                ellipse.push_back(toScreen((dragStart.x + dragNow.x) * 0.5f + (dragNow.x - dragStart.x) * 0.5f * std::cos(a),
                                           (dragStart.y + dragNow.y) * 0.5f + (dragNow.y - dragStart.y) * 0.5f * std::sin(a)));
            }
            dl->AddPolyline(ellipse.data(), (int)ellipse.size(), C_PINK, ImDrawFlags_Closed, 1.5f);
        } else dl->AddPolyline(q, 4, C_PINK, ImDrawFlags_Closed, 1.5f);
    }
    if (tool == T_OBJECT && dragging) {
        const ImVec2 q[4] = {toScreen(dragStart.x, dragStart.y), toScreen(dragNow.x, dragStart.y), toScreen(dragNow.x, dragNow.y), toScreen(dragStart.x, dragNow.y)};
        dl->AddPolyline(q, 4, C_PINK, ImDrawFlags_Closed, 1.5f);
    }
    if (tool == T_QUICK && ImGui::IsWindowHovered()) {
        const float r = quickSize * 0.5f * zoom;
        dl->AddCircle(io.MousePos, r, IM_COL32(255, 255, 255, 200), 48, 1.0f); dl->AddCircle(io.MousePos, r + 1, IM_COL32(0, 0, 0, 160), 48, 1.0f);
        for (size_t k = 0; k + 3 < quickPts.size(); k += 2) dl->AddLine(toScreen(quickPts[k], quickPts[k + 1]), toScreen(quickPts[k + 2], quickPts[k + 3]), IM_COL32(255, 255, 255, 160), std::max(1.0f, quickSize * zoom * 0.25f));
    }
    if (tool == T_GRADIENT && dragging) {
        const ImVec2 p0 = toScreen(dragStart.x, dragStart.y), p1 = toScreen(dragNow.x, dragNow.y);
        dl->AddLine(p0, p1, IM_COL32(0, 0, 0, 200), 3); dl->AddLine(p0, p1, IM_COL32(255, 255, 255, 230), 1.5f);
        dl->AddCircleFilled(p0, 3.5f, IM_COL32(255, 255, 255, 230)); dl->AddCircle(p1, 4.5f, IM_COL32(255, 255, 255, 230));
    }
    if (tool == T_RETOUCH && retouchMode() == RT_CLONE && cloneSet && ImGui::IsWindowHovered()) {   // where it copies from
        const ImVec2 sp = dragging && cloneHaveOffset ? toScreen(dragNow.x + cloneDX, dragNow.y + cloneDY) : toScreen(cloneSrcX, cloneSrcY);
        dl->AddLine(ImVec2(sp.x - 7, sp.y), ImVec2(sp.x + 7, sp.y), IM_COL32(255, 255, 255, 220), 1.5f);
        dl->AddLine(ImVec2(sp.x, sp.y - 7), ImVec2(sp.x, sp.y + 7), IM_COL32(255, 255, 255, 220), 1.5f);
    }
    if ((tool == T_BRUSH || tool == T_ERASER || tool == T_PENCIL || tool == T_RETOUCH) && ImGui::IsWindowHovered()) {
        const float r = (tool == T_BRUSH ? brushSize : tool == T_ERASER ? eraserSize : tool == T_PENCIL ? pencilSize : rtSize) * 0.5f * zoom;
        const float h = (tool == T_BRUSH ? brushHard : tool == T_ERASER ? eraserHard : tool == T_PENCIL ? 1.0f : rtHard);
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

// the strip's tool to Chimera's own (T_NONE: not in Chimera yet)
static int ltTool(int id) {
    switch (id) {
    case Toolbar::MOVE: return T_MOVE; case Toolbar::RECT_MARQUEE: return T_RECT; case Toolbar::ELLIPSE_MARQUEE: return T_ELLIPSE;
    case Toolbar::LASSO: return T_LASSO; case Toolbar::POLYGON_LASSO: return T_POLY; case Toolbar::MAGIC_WAND: return T_WAND;
    case Toolbar::BRUSH: return T_BRUSH; case Toolbar::ERASER: return T_ERASER; case Toolbar::PAINT_BUCKET: return T_BUCKET;
    case Toolbar::EYEDROPPER: return T_DROPPER; case Toolbar::HAND: return T_HAND;
    case Toolbar::ROTATE_VIEW: return T_ROTATE;
    case Toolbar::ARTBOARD: return T_ARTBOARD;
    case Toolbar::MAGNETIC_LASSO: return T_MAGNETIC;
    case Toolbar::PENCIL: return T_PENCIL; case Toolbar::ZOOM: return T_ZOOM;
    case Toolbar::SINGLE_ROW_MARQUEE: return T_ROW; case Toolbar::SINGLE_COLUMN_MARQUEE: return T_COL;
    case Toolbar::CROP: return T_CROP; case Toolbar::PERSPECTIVE_CROP: return T_PCROP;
    case Toolbar::SLICE: return T_SLICE; case Toolbar::SLICE_SELECT: return T_SLICE_SELECT; case Toolbar::FRAME: return T_FRAME;
    case Toolbar::GRADIENT: return T_GRADIENT; case Toolbar::MAGIC_ERASER: return T_MAGIC_ERASER;
    case Toolbar::BLUR: case Toolbar::SHARPEN: case Toolbar::SMUDGE: case Toolbar::DODGE: case Toolbar::BURN: case Toolbar::SPONGE:
    case Toolbar::CLONE_STAMP: case Toolbar::BACKGROUND_ERASER: return T_RETOUCH;
    case Toolbar::PATTERN_STAMP: case Toolbar::HISTORY_BRUSH: case Toolbar::ART_HISTORY_BRUSH: case Toolbar::COLOR_REPLACEMENT: case Toolbar::MIXER_BRUSH: return T_RETOUCH;
    case Toolbar::QUICK_SELECTION: return T_QUICK; case Toolbar::OBJECT_SELECTION: return T_OBJECT; case Toolbar::RED_EYE: return T_REDEYE;
    case Toolbar::SPOT_HEALING: case Toolbar::REMOVE: case Toolbar::HEALING: case Toolbar::PATCH: case Toolbar::CONTENT_AWARE_MOVE: return T_HEAL;
    case Toolbar::RECTANGLE: case Toolbar::ELLIPSE: case Toolbar::TRIANGLE: case Toolbar::POLYGON: case Toolbar::STAR: case Toolbar::LINE: case Toolbar::CUSTOM_SHAPE: return T_SHAPE;
    case Toolbar::COLOR_SAMPLER: case Toolbar::RULER: case Toolbar::NOTE: case Toolbar::COUNT_TOOL: return T_MARK;
    case Toolbar::TYPE_H: case Toolbar::TYPE_V: case Toolbar::TYPE_H_MASK: case Toolbar::TYPE_V_MASK: return T_TYPE;
    case Toolbar::PEN: case Toolbar::FREEFORM_PEN: case Toolbar::CURVATURE_PEN: case Toolbar::ADD_ANCHOR: case Toolbar::DELETE_ANCHOR:
    case Toolbar::CONVERT_POINT: case Toolbar::PATH_SELECTION: case Toolbar::DIRECT_SELECTION: return T_PEN;
    default: return T_NONE;
    }
}
static void syncTool() {
    const int t = ltTool(Toolbar::current());
    if (t != tool) {
        if (tool == T_TYPE) typeCommit();
        if (tool == T_MAGNETIC) magneticClear();
        tool = t; poly.clear();
        // after PhotoCraft (crates/ui-egui/src/tools/crop_ui.rs): an undismissed Crop starts with a frame.
        if (tool == T_CROP && !cropFrameDismissed) { cropPending=true; cropR[0]=cropR[1]=0; cropR[2]=(float)doc.w; cropR[3]=(float)doc.h; }
        else if (tool != T_CROP) {cropPending=false;cropClearHistory();}
    }
}
static bool toolbarToolWorks(int id) { return ltTool(id) != T_NONE; }

static void toolsPanel() {
    // the tools scroll when the window is too short for them; the colour chips stay underneath (PhotoCraft)
    const float chipsH = 56;
    ImGui::BeginChild("##strip", ImVec2(0, std::min(ImGui::GetContentRegionAvail().y - chipsH, Toolbar::height())), false, ImGuiWindowFlags_NoScrollbar);
    Toolbar::draw();
    ImGui::EndChild();
    syncTool();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // colours: foreground over background, swap, default (under the tools, as in PhotoCraft)
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + (Toolbar::width() - 32) / 2, ImGui::GetCursorScreenPos().y + 10));
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
    {   // the tool: its icon and name
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Toolbar::icon(ImGui::GetWindowDrawList(), Toolbar::current(), at.x + 10, at.y + ImGui::GetFrameHeight() / 2, 18, C_PINK);
        ImGui::Dummy(ImVec2(24, ImGui::GetFrameHeight())); ImGui::SameLine(0, 4);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col4(C_PINK), "%s", Toolbar::label(Toolbar::current()));
    }
    if (tool == T_NONE) {
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("Not in Chimera yet: it comes as PhotoCraft's tools are ported. Pick another tool to work on the picture.");
        return;
    }
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
    case T_MAGNETIC:
        selModes(); ImGui::SameLine(0, 10);
        sliderF("Width", &magneticWidth, 1, 256, "%.0f px", ImGuiSliderFlags_Logarithmic); ImGui::SameLine(0, 10);
        sliderF("Contrast", &magneticContrast, 1, 100, "%.0f%%"); ImGui::SameLine(0, 10);
        sliderF("Frequency", &magneticFrequency, 0, 100, "%.0f"); ImGui::SameLine(0, 10);
        sliderF("Feather", &selFeather, 0, 100, "%.0f px"); ImGui::SameLine(0, 10);
        ImGui::TextDisabled("Click anchors on an edge; double-click or Enter closes; Escape cancels");
        break;
    case T_WAND: case T_BUCKET:
        if (tool == T_WAND) { selModes(); ImGui::SameLine(0, 14); }
        sliderI("Tolerance", &tolerance, 0, 255); ImGui::SameLine(0, 10);
        ImGui::Checkbox("Contiguous", &contiguous); ImGui::SameLine(0, 10);
        ImGui::Checkbox("Sample all layers", &sampleAll);
        if (tool == T_BUCKET) { ImGui::SameLine(0, 10); float o = bucketOpacity * 100; if (sliderF("Opacity", &o, 1, 100, "%.0f%%")) bucketOpacity = o / 100; }
        break;
    case T_PENCIL: {
        sliderF("Size", &pencilSize, 1, 200, "%.0f px", ImGuiSliderFlags_Logarithmic); ImGui::SameLine(0, 14);
        float o = pencilOpacity * 100; if (sliderF("Opacity", &o, 1, 100, "%.0f%%")) pencilOpacity = o / 100; ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Hard-edged, every pixel all or nothing; Alt picks a colour");
        break;
    }
    case T_RETOUCH: {
        const int mode = retouchMode(), slot = mode == RT_SPONGE_DESAT ? RT_SPONGE_SAT : mode;
        sliderF("Size", &rtSize, 1, 1000, "%.0f px", ImGuiSliderFlags_Logarithmic); ImGui::SameLine(0, 14);
        float h = rtHard * 100; if (sliderF("Hardness", &h, 0, 100, "%.0f%%")) rtHard = h / 100; ImGui::SameLine(0, 14);
        const char* what = mode == RT_DODGE || mode == RT_BURN ? "Exposure" : mode == RT_SPONGE_SAT || mode == RT_SPONGE_DESAT || mode == RT_MIXER ? "Flow" : mode == RT_CLONE || mode == RT_PATTERN || mode == RT_HISTORY || mode == RT_ART_HISTORY || mode == RT_REPLACE ? "Opacity" : mode == RT_BG_ERASE ? "Tolerance" : "Strength";
        float s = rtStrength[slot] * 100; if (sliderF(what, &s, 1, 100, "%.0f%%")) rtStrength[slot] = s / 100;
        if (mode == RT_DODGE || mode == RT_BURN) { ImGui::SameLine(0, 14); const char* rs[] = {"Shadows", "Midtones", "Highlights"}; ImGui::SetNextItemWidth(110); ImGui::Combo("##range", &rtRange, rs, 3); }
        if (mode == RT_SPONGE_SAT || mode == RT_SPONGE_DESAT) { ImGui::SameLine(0, 14); int m2 = spongeDesat ? 1 : 0; const char* ms[] = {"Saturate", "Desaturate"}; ImGui::SetNextItemWidth(110); if (ImGui::Combo("##sp", &m2, ms, 2)) spongeDesat = m2 == 1; }
        if (mode == RT_CLONE) { ImGui::SameLine(0, 14); ImGui::Checkbox("Aligned", &cloneAligned); ImGui::SameLine(0, 14); ImGui::TextDisabled(cloneSet ? "Alt+click sets a new source" : "Alt+click where to copy from"); }
        if (mode == RT_BG_ERASE) { ImGui::SameLine(0, 14); ImGui::TextDisabled("Erases the colour under the brush's centre where you start"); }
        if (mode == RT_PATTERN) {
            ImGui::SameLine(0, 14); retouchOpts();   // (makes the built-in patterns)
            std::vector<const char*> names; for (auto& pt : patterns) names.push_back(pt.first.c_str());
            ImGui::SetNextItemWidth(140); ImGui::Combo("##pat", &patternPick, names.data(), (int)names.size());
            ImGui::SameLine(0, 14); ImGui::TextDisabled("Edit > Define Pattern makes one from the selection");
        }
        if (mode == RT_HISTORY || mode == RT_ART_HISTORY) {
            ImGui::SameLine(0, 14);
            if (ImGui::Button("Set Source to Now")) { doc.historyState = std::make_shared<const std::vector<Layer>>(doc.layers); say("The History Brush paints back to the picture as it is now"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("It paints back to the picture as it was when it opened (or when you press this)");
            if (mode == RT_ART_HISTORY) {
                ImGui::SameLine(0, 14); const char* st[] = {"Tight Short", "Loose Medium", "Dab", "Tight Curl"}; ImGui::SetNextItemWidth(120); ImGui::Combo("##art", &artStyle, st, 4);
                ImGui::SameLine(0, 14); sliderF("Area", &artArea, 5, 200, "%.0f px");
            }
        }
        if (mode == RT_REPLACE) {
            ImGui::SameLine(0, 14); const char* rm[] = {"Hue", "Saturation", "Color", "Luminosity"}; ImGui::SetNextItemWidth(110); ImGui::Combo("##rmode", &replaceMode, rm, 4);
            float t = replaceTol * 100; ImGui::SameLine(0, 14); if (sliderF("Tolerance", &t, 1, 100, "%.0f%%")) replaceTol = t / 100;
            ImGui::SameLine(0, 14); ImGui::TextDisabled("Recolours what's like the colour where you start, with the foreground");
        }
        if (mode == RT_MIXER) {
            float w = mixWet * 100, l2 = mixLoad * 100, m2 = mixMix * 100;
            ImGui::SameLine(0, 14); if (sliderF("Wet", &w, 0, 100, "%.0f%%")) mixWet = w / 100;
            ImGui::SameLine(0, 14); if (sliderF("Load", &l2, 1, 100, "%.0f%%")) mixLoad = l2 / 100;
            ImGui::SameLine(0, 14); if (sliderF("Mix", &m2, 0, 100, "%.0f%%")) mixMix = m2 / 100;
        }
        break;
    }
    case T_GRADIENT: {
        const char* names[GRAD_COUNT]; for (int k = 0; k < GRAD_COUNT; ++k) names[k] = gradStyleName(k);
        ImGui::SetNextItemWidth(120); ImGui::Combo("##gstyle", &gradToolStyle, names, GRAD_COUNT); ImGui::SameLine(0, 14);
        ImGui::Checkbox("Reverse", &gradToolReverse); ImGui::SameLine(0, 14);
        float o = gradToolOpacity * 100; if (sliderF("Opacity", &o, 1, 100, "%.0f%%")) gradToolOpacity = o / 100; ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Drag across the picture: foreground to background colour, in the selection");
        break;
    }
    case T_CROP:
        { static const char* presets[]={"Ratio","W x H x Resolution","Original Ratio","1 : 1 (Square)","4 : 5 (8 : 10)","5 : 7","2 : 3 (4 : 6)","16 : 9","4 x 5 in 300 ppi","8.5 x 11 in 300 ppi","1024 x 768 px 92 ppi","1280 x 800 px 113 ppi","1366 x 768 px 135 ppi","4 x 6 in 300 ppi","5 x 7 in 300 ppi","8 x 10 in 300 ppi","Front Image"};
        ImGui::SetNextItemWidth(150); if(ImGui::Combo("##cropPreset",&cropPreset,presets,17)) {
            static const char* ws[]={"4 in","8.5 in","1024 px","1280 px","1366 px","4 in","5 in","8 in"};
            static const char* hs[]={"5 in","11 in","768 px","800 px","768 px","6 in","7 in","10 in"};
            static const char* rs[]={"300","300","92","113","135","300","300","300"};
            if(cropPreset>=8&&cropPreset<=15){strcpy_s(cropWidth,ws[cropPreset-8]);strcpy_s(cropHeight,hs[cropPreset-8]);strcpy_s(cropResolution,rs[cropPreset-8]);cropResolutionCm=false;}
            if(cropPreset==16)cropFrontImage();
            if(cropPreset==1||cropPreset>=8)fitCropAspect();
            else {cropWidth[0]=cropHeight[0]=cropResolution[0]=0;fitCropAspect();}
        }
        if(cropPreset==1||(cropPreset>=8&&cropPreset<=15)){ImGui::SameLine(0,6);ImGui::SetNextItemWidth(72);if(ImGui::InputText("##cropW",cropWidth,sizeof(cropWidth)))fitCropAspect();ImGui::SameLine(0,3);ImGui::TextUnformatted("×");ImGui::SameLine(0,3);ImGui::SetNextItemWidth(72);if(ImGui::InputText("##cropH",cropHeight,sizeof(cropHeight)))fitCropAspect();ImGui::SameLine(0,4);ImGui::SetNextItemWidth(64);ImGui::InputText("##cropResolution",cropResolution,sizeof(cropResolution));ImGui::SameLine(0,3);ImGui::SetNextItemWidth(60);const char* ru[]={"px/in","px/cm"};int rui=cropResolutionCm?1:0;if(ImGui::Combo("##cropResolutionUnit",&rui,ru,2))cropResolutionCm=rui==1;ImGui::SameLine(0,3);if(ImGui::SmallButton("Swap")){std::string w=cropWidth;strcpy_s(cropWidth,cropHeight);strcpy_s(cropHeight,w.c_str());fitCropAspect();}ImGui::SameLine(0,3);if(ImGui::SmallButton("Clear")){cropWidth[0]=cropHeight[0]=cropResolution[0]=0;cropPreset=1;}}
        if(!cropUserPresets.empty()){ImGui::SameLine(0,5);std::vector<const char*> savedNames;for(const auto&p:cropUserPresets)savedNames.push_back(p.name.c_str());cropSavedChoice=std::clamp(cropSavedChoice,0,(int)savedNames.size()-1);ImGui::SetNextItemWidth(125);if(ImGui::Combo("##savedCropPreset",&cropSavedChoice,savedNames.data(),(int)savedNames.size()))cropSelectUserPreset(cropSavedChoice);}
        ImGui::SameLine(0,4);if(ImGui::SmallButton("New Preset")){snprintf(cropNewName,sizeof(cropNewName),"%s x %s",cropWidth,cropHeight);ImGui::OpenPopup("New Crop Preset");}
        ImGui::SameLine(0,3);ImGui::BeginDisabled(cropUserPresets.empty());if(ImGui::SmallButton("Delete Preset"))ImGui::OpenPopup("Delete Crop Preset");ImGui::EndDisabled();
        if(ImGui::BeginPopupModal("New Crop Preset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Name:");ImGui::InputText("##newCropPresetName",cropNewName,sizeof(cropNewName));if(ImGui::Button("Save",ImVec2(90,0))&&cropNewName[0]&&cropWidth[0]&&cropHeight[0]){CropPreset p{cropNewName,cropWidth,cropHeight,cropResolution,cropResolutionCm};auto it=std::find_if(cropUserPresets.begin(),cropUserPresets.end(),[&](const CropPreset&q){return q.name==p.name;});if(it==cropUserPresets.end())cropUserPresets.push_back(p);else *it=p;cropSavedChoice=(int)(std::find_if(cropUserPresets.begin(),cropUserPresets.end(),[&](const CropPreset&q){return q.name==p.name;})-cropUserPresets.begin());cropSaveUserPresets();ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel",ImVec2(90,0)))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        if(ImGui::BeginPopupModal("Delete Crop Preset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Delete the selected saved crop preset?");if(ImGui::Button("Delete",ImVec2(90,0))&&!cropUserPresets.empty()){cropUserPresets.erase(cropUserPresets.begin()+std::clamp(cropSavedChoice,0,(int)cropUserPresets.size()-1));cropSavedChoice=std::max(0,(int)cropUserPresets.size()-1);cropSaveUserPresets();ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel",ImVec2(90,0)))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        ImGui::SameLine(0,5);if(ImGui::SmallButton(straightenArmed?"Straighten: On":"Straighten"))straightenArmed=!straightenArmed;
        ImGui::SameLine(0,5);if(ImGui::Checkbox("Classic Mode",&cropClassicMode))syncCropView();ImGui::SameLine(0,5);ImGui::BeginDisabled(cropClassicMode);ImGui::Checkbox("Auto Center",&cropAutoCenter);ImGui::EndDisabled();
        static const char* overlays[]={"Thirds","Grid","Diagonal","Triangle","Golden Ratio","Golden Spiral"};
        ImGui::SetNextItemWidth(145); ImGui::Combo("##cropOverlay",&cropOverlay,overlays,6); ImGui::SameLine(0,8);
        static const char* shows[]={"Auto","Always","Never"}; ImGui::SetNextItemWidth(90); ImGui::Combo("##cropOverlayShow",&cropOverlayShow,shows,3); ImGui::SameLine(0,8);
        if(cropOverlay==3 || cropOverlay==5) { if(ImGui::SmallButton("Orientation")) cropOverlayOrientation=(cropOverlayOrientation+1)%(cropOverlay==3?2:4); ImGui::SameLine(0,8); }
        ImGui::BeginDisabled(cropClassicMode);if(cropClassicMode){bool show=true;ImGui::Checkbox("Show Cropped Area",&show);}else ImGui::Checkbox("Show Cropped Area", &cropShowArea);ImGui::EndDisabled(); ImGui::SameLine(0,8);
        ImGui::Checkbox("Shield", &cropShieldEnabled); ImGui::SameLine(0,8);
        ImGui::Checkbox("Match Canvas",&cropShieldMatchCanvas); ImGui::SameLine(0,4);
        ImGui::BeginDisabled(cropShieldMatchCanvas); ImGui::ColorEdit3("##cropShieldColor",cropShieldColor,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel|ImGuiColorEditFlags_NoSidePreview); ImGui::EndDisabled(); ImGui::SameLine(0,8);
        float shield=cropShieldOpacity; if(sliderF("Shield Opacity",&shield,0,100,"%.0f%%")) cropShieldOpacity=shield; ImGui::SameLine(0,8);
        ImGui::Checkbox("Auto Adjust",&cropShieldAuto); ImGui::SameLine(0,12);
        ImGui::Checkbox("Delete cropped pixels", &cropDelete); ImGui::SameLine(0, 14);
        ImGui::Checkbox("Content-Aware Corners",&cropContentAware); ImGui::SameLine(0,8);
        if (ImGui::Button("Crop", ImVec2(70, 0))) applyCrop(); ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(70, 0))) {cropPending = false;cropFrameDismissed=true;cropClearHistory();} ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Drag what to keep; O cycles guides, Shift+O changes orientation"); }
        break;
    case T_PCROP: {
        int width = pcropOutW, height = pcropOutH;
        if (sliderI("Width", &width, 1, 16384)) { pcropOutW = width; pcropCustomSize = true; }
        ImGui::SameLine(0, 10);
        if (sliderI("Height", &height, 1, 16384)) { pcropOutH = height; pcropCustomSize = true; }
        ImGui::SameLine(0, 10); ImGui::BeginDisabled(!pcropPending || !pcropValid(pcropQ));
        if (ImGui::Button("Apply", ImVec2(70, 0))) applyPerspectiveCrop();
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(70, 0))) pcropCancel(); ImGui::SameLine(0, 10);
        ImGui::TextDisabled("Drag a box or click four corners; drag corners to adjust, Enter applies, Escape cancels");
        break;
    }
    case T_SLICE:
        if (ImGui::Button("Export Slices...")) exportSlices(); ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Drag rectangles to mark parts of the picture for export");
        break;
    case T_SLICE_SELECT:
        if (sliceActive >= 0 && sliceActive < (int)doc.slices.size()) {
            Slice& slice = doc.slices[sliceActive];
            static int editingSliceId = -1;
            static char sliceName[128] = {};
            if (editingSliceId != slice.id) {
                editingSliceId = slice.id;
                snprintf(sliceName, sizeof sliceName, "%s", slice.name.c_str());
            }
            ImGui::SetNextItemWidth(140);
            const bool nameChanged = ImGui::InputText("Name", sliceName, sizeof sliceName);
            if (ImGui::IsItemActivated()) remember("Rename Slice");
            if (nameChanged) {
                slice.name = sliceName;
                changed();
            }
            ImGui::SameLine(0, 8);
            Rect r = slice.rect;
            int x = r.x0, y = r.y0, width = r.x1 - r.x0, height = r.y1 - r.y0;
            ImGui::PushItemWidth(80);
            auto edit = [&](const char* label, int& value, int lo, int hi) {
                const bool didChange = sliderI(label, &value, lo, std::max(lo, hi));
                if (ImGui::IsItemActivated()) remember("Slice Bounds");
                if (didChange) changed();
                return didChange;
            };
            if (edit("X", x, 0, doc.w - width)) r.x0 = x, r.x1 = x + width;
            ImGui::SameLine(0, 4);
            if (edit("Y", y, 0, doc.h - height)) r.y0 = y, r.y1 = y + height;
            ImGui::SameLine(0, 4);
            if (edit("W", width, 1, doc.w - r.x0)) r.x1 = r.x0 + width;
            ImGui::SameLine(0, 4);
            if (edit("H", height, 1, doc.h - r.y0)) r.y1 = r.y0 + height;
            ImGui::PopItemWidth();
            slice.rect = r;
            ImGui::SameLine(0, 8);
            if (ImGui::Button("Delete Slice")) {
                remember("Delete Slice"); doc.slices.erase(doc.slices.begin() + sliceActive); sliceActive = -1; changed();
            }
        } else {
            ImGui::TextDisabled("Click a slice to select it; drag to move");
            ImGui::SameLine(0, 10);
        }
        if (ImGui::Button("Export Slices...")) exportSlices();
        break;
    case T_FRAME:
        if (ImGui::Button("Rectangle")) frameEllipse = false;
        ImGui::SameLine(0, 5);
        if (ImGui::Button("Ellipse")) frameEllipse = true;
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled(frameEllipse ? "Ellipse mode" : "Rectangle mode");
        ImGui::SameLine(0, 10);
        if (ImGui::Button("Place Image...")) {
            if (doc.cur().frame) {
                const std::string p = fileDialog(false, OPEN_FILTER, nullptr, "");
                if (!p.empty()) placeAsLayer(p);
            } else say("Select or create a frame before placing an image");
        }
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("Drag a rectangle or ellipse; place an image to clip it inside the frame");
        break;
    case T_ARTBOARD: {
        sliderI("Width", &artboardWidth, 1, 10000);
        ImGui::SameLine(0, 10); sliderI("Height", &artboardHeight, 1, 10000);
        ImGui::SameLine(0, 10);
        const char* backgrounds[] = {"White", "Black", "Atelier Blue"};
        ImGui::SetNextItemWidth(120); ImGui::Combo("Background", &artboardBackground, backgrounds, IM_ARRAYSIZE(backgrounds));
        ImGui::SameLine(0, 12); ImGui::TextDisabled("Drag to draw; click to place a board at this size");
        break;
    }
    case T_ZOOM: ImGui::TextDisabled("Click zooms in, Alt+click zooms out; the wheel zooms too"); break;
    case T_ROTATE:
        ImGui::TextDisabled("Drag around the canvas centre to rotate the view; Shift snaps to 15 degrees; double-click or Escape resets");
        if (std::fabs(viewRotation) > 0.05f) { ImGui::SameLine(0, 14); if (ImGui::Button("Reset View")) viewRotation = 0.0f; ImGui::SameLine(0, 8); ImGui::TextDisabled("%.1f degrees", viewRotation); }
        break;
    case T_QUICK:
        sliderF("Size", &quickSize, 1, 500, "%.0f px", ImGuiSliderFlags_Logarithmic); ImGui::SameLine(0, 14);
        ImGui::Checkbox("Sample all layers", &sampleAll); ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Paint over what you want: it grows to the edges. Strokes add; Alt takes away");
        break;
    case T_OBJECT:
        selModes(); ImGui::SameLine(0, 14); ImGui::Checkbox("Sample all layers", &sampleAll); ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Drag a box round an object: it's found inside the box");
        break;
    case T_PEN: {
        const bool any = !doc.workPath.empty();
        ImGui::BeginDisabled(!any);
        if (ImGui::Button("Make Selection")) { combineSel(doc, pathSelection(), SEL_NEW); selChanged(); say("The path made a selection"); }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+Enter");
        ImGui::SameLine(); if (ImGui::Button("Fill Path")) {
            if (!pixelLayer()) say("Fill Path paints a layer's pixels");
            else { remember("Fill Path"); const auto saved = doc.sel; combineSel(doc, pathSelection(), SEL_NEW); fillSelection(doc, fg, 1.0f, BM_Normal); doc.sel = saved; changed(); selChanged(); }
        }
        ImGui::SameLine(); if (ImGui::Button("Stroke Path")) {
            if (!pixelLayer()) say("Stroke Path paints a layer's pixels");
            else {   // a line along the path in the foreground colour, Width wide, through the selection if there is one
                remember("Stroke Path");
                std::vector<bool> closed; for (const auto& sp : doc.workPath) closed.push_back(sp.closed);
                std::vector<float> cov; polylinesStroke(flattenPath(doc.workPath), closed, penStrokeWidth, doc.w, doc.h, cov);
                std::vector<uint8_t> m(cov.size()); for (size_t i = 0; i < cov.size(); ++i) m[i] = to8(cov[i] * (doc.hasSel() ? doc.sel[i] / 255.0f : 1.0f));
                const auto saved = doc.sel; doc.sel = m; fillSelection(doc, fg, 1.0f, BM_Normal); doc.sel = saved; changed();
            }
        }
        ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(90); ImGui::SliderFloat("##psw", &penStrokeWidth, 0.5f, 100, "%.1f px", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine(0, 10); if (ImGui::Button("Make Shape")) {   // a shape layer of the path, in the foreground colour
            remember("New Path Shape");
            Shape sh; sh.kind = SHAPE_PATH; sh.path = doc.workPath;
            Fill f; f.kind = FILL_SOLID; for (int k = 0; k < 3; ++k) f.colour[k] = fg[k] / 255.0f;
            addShapeLayer(doc, sh, f); doc.workPath.clear(); penSub = -1; changed();
        }
        ImGui::SameLine(); if (ImGui::Button("Delete Path")) { remember("Delete Path"); doc.workPath.clear(); penSub = -1; changed(); }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 14);
        const char* tips[] = {"Click for corners, drag for curves; click the first point to close it, Enter ends it open",
                              "Drag to draw a path freehand", "Click points: the curve runs smoothly through them; click the first to close",
                              "Click on the path to add a point", "Click a point to take it away", "Click a point: smooth <-> corner; drag a handle to break it",
                              "Drag a path to move it", "Drag a point or a handle to change the curve"};
        ImGui::TextDisabled("%s", tips[penMode()]);
        break;
    }
    case T_TYPE: {
        // the active text layer's type, or the next text's
        Layer& L = doc.cur();
        const bool live = L.isText() && !typeIsMask();
        Text& t = live ? L.text : (typeMaskEditing ? typeMask : typeNext);
        bool ch = false;
        const auto characterStyles=t.characterStyles();
        const bool hasCharacterSelection=live&&typeStyleSelectionValid&&typeStyleSelectionLayer==typeLayer&&typeStyleSelectionStart<typeStyleSelectionEnd&&!characterStyles.empty();
        const size_t characterStart=hasCharacterSelection?std::min(typeStyleSelectionStart,characterStyles.size()):0;
        const size_t characterEnd=hasCharacterSelection?std::min(typeStyleSelectionEnd,characterStyles.size()):0;
        const std::string selectedFont=hasCharacterSelection?(characterStyles[characterStart].font.empty()?t.font:characterStyles[characterStart].font):t.font;
        auto edited = [&](bool c) { if (live && ImGui::IsItemActivated()) typeRemember("Text"); ch |= c; };
        // the font: the fonts on this PC, by name
        const auto& fonts = systemFonts();
        const std::string cur = selectedFont.empty() ? defaultFont() : selectedFont;
        std::string curName = "Segoe UI";
        for (const auto& f : fonts) if (f.path == cur) curName = f.name;
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("##font", curName.c_str(), ImGuiComboFlags_HeightLarge)) {
            static char find[64] = "";
            ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##ff", "Find a font", find, sizeof find);
            std::string q = find; for (auto& c : q) c = (char)tolower((unsigned char)c);
            if (!q.empty()) ImGui::SeparatorText("Recent");
            else if (!recentTypeFonts.empty()) {
                ImGui::TextDisabled("Recent");
                const auto recent=recentTypeFonts;
                for (const std::string& path : recent) {
                    auto it=std::find_if(fonts.begin(),fonts.end(),[&](const FontFile& f){return f.path==path;});
                    if(it!=fonts.end() && ImGui::Selectable(it->name.c_str(),path==cur)) {
                        if(live) typeRemember("Font");Text updated=t;if(typeSetCharacterFont(updated,characterStart,characterEnd,path)){t=std::move(updated);ch=true;}
                        noteRecentFont(recentTypeFonts,path,(size_t)recentTypeFontCap);saveTypePreferences();
                    }
                }
                ImGui::Separator();
            }
            for (const auto& f : fonts) {
                std::string n = f.name; for (auto& c : n) c = (char)tolower((unsigned char)c);
                if (!q.empty() && n.find(q) == std::string::npos) continue;
                if (!q.empty() && std::find(recentTypeFonts.begin(),recentTypeFonts.end(),f.path)!=recentTypeFonts.end()) continue;
                if (ImGui::Selectable(f.name.c_str(), f.path == cur)) { if (live) typeRemember("Font");Text updated=t;if(typeSetCharacterFont(updated,characterStart,characterEnd,f.path)){t=std::move(updated);ch=true;}noteRecentFont(recentTypeFonts,f.path,(size_t)recentTypeFontCap);saveTypePreferences(); }
            }
            ImGui::EndCombo();
        }
        if (live && t.pathText) {
            ImGui::SameLine(0, 8);
            if (ImGui::Button("Path...")) ImGui::OpenPopup("##typePathSettings");
            if (ImGui::BeginPopup("##typePathSettings")) {
                if (ImGui::Button("Remove Path")) { typeRemember("Remove Text Path"); t.pathText = false; t.path.clear(); t.pathStart = 0; ch = true; ImGui::CloseCurrentPopup(); }
                edited(ImGui::Checkbox("Flip Side", &t.pathFlip));
                const float length = typePathLength(t.path);
                if (length > 0) { ImGui::SetNextItemWidth(240); edited(sliderF("Path Start", &t.pathStart, 0, length, "%.0f px")); }
                ImGui::EndPopup();
            }
        } else if (live && !t.vertical && t.boxWidth <= 0 && typePathLength(doc.workPath) > 0) {
            ImGui::SameLine(0, 8);
            if (ImGui::Button("Type on Path")) {
                int pathIndex = -1;
                typeRemember("Type on Path"); t.pathStart = typePathNearest(doc.workPath, t.x, t.y, nullptr, &pathIndex);
                t.path = pathIndex >= 0 && pathIndex < (int)doc.workPath.size() ? BezierPath{doc.workPath[pathIndex]} : doc.workPath;
                t.pathText = true; t.pathFlip = false; ch = true;
            }
        }
        ImGui::SameLine(0, 10);
        const float rawScale=std::sqrt(std::abs(t.scaleX*t.scaleY));const float sizeScale=std::isfinite(rawScale)&&rawScale>1e-3f?rawScale:1.0f;
        float shownSize=(hasCharacterSelection?characterStyles[characterStart].size:t.size)*sizeScale;
        if(sliderF("Size",&shownSize,4,1000,"%.0f px",ImGuiSliderFlags_Logarithmic)){
            Text updated=t;const bool sizeChanged=typeSetCharacterSize(updated,characterStart,characterEnd,shownSize/sizeScale);
            if(sizeChanged)t=std::move(updated);edited(sizeChanged);
        }else edited(false);
        if (live && L.fill.kind == FILL_SOLID) { ImGui::SameLine(0, 10); edited(ImGui::ColorEdit3("##tc", L.fill.colour, ImGuiColorEditFlags_NoInputs)); }
        ImGui::SameLine(0, 14);
        const char* al[] = {t.vertical ? "Top" : "Left", t.vertical ? "Middle" : "Centre", t.vertical ? "Bottom" : "Right"};
        const size_t alignStart=live&&typeStyleSelectionValid&&typeStyleSelectionLayer==typeLayer?std::min(typeStyleSelectionStart,characterStyles.size()):0;
        const size_t alignEnd=live&&typeStyleSelectionValid&&typeStyleSelectionLayer==typeLayer?std::min(typeStyleSelectionEnd,characterStyles.size()):alignStart;
        const int paragraphAlign=live?typeParagraphStyleAt(t,alignStart).align:t.align;
        const int shownAlign=paragraphAlign>=3&&paragraphAlign<=5?paragraphAlign-3:paragraphAlign;
        for (int k = 0; k < 3; ++k) {
            if (k) ImGui::SameLine(0, 2);
            const bool on = shownAlign == k;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_PINK));
            if (ImGui::Button(al[k])) {
                Text updated=t;
                const bool paragraphChanged=typeSetParagraphStyle(updated,alignStart,alignEnd,0,(float)k);
                if(paragraphChanged||t.align!=k){
                    if(live)typeRemember("Align");updated.align=k;t=std::move(updated);ch=true;
                }
            }
            if (on) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 14);
        float shownTracking=hasCharacterSelection?characterStyles[characterStart].tracking:t.tracking;
        if(sliderF("Tracking",&shownTracking,-1000,10000,"%.0f")){
            Text updated=t;const bool trackingChanged=typeSetCharacterTracking(updated,characterStart,characterEnd,shownTracking);
            if(trackingChanged)t=std::move(updated);edited(trackingChanged);
        }else edited(false);
        ImGui::SameLine(0, 14); float shownLeading=hasCharacterSelection?characterStyles[characterStart].leading:t.leading;
        if(sliderF("Leading",&shownLeading,.01f,10.0f,"%.2f")){Text updated=t;const bool metricChanged=typeSetCharacterMetricValue(updated,characterStart,characterEnd,0,shownLeading*std::max(1.0f,hasCharacterSelection?characterStyles[characterStart].size:t.size)*72.0f/std::max(1.0f,doc.dpi)*std::max(.01f,hasCharacterSelection?characterStyles[characterStart].verticalScale:1.0f)*std::max(.01f,t.scaleY),doc.dpi,t.scaleY);if(metricChanged)t=std::move(updated);edited(metricChanged);}else edited(false);
        ImGui::SameLine(0, 14); edited(sliderF("Rotate", &t.rotation, -180, 180, "%.1f°"));
        ImGui::SameLine(0, 14); edited(sliderF("Scale X", &t.scaleX, 0.1f, 10, "%.2f", ImGuiSliderFlags_Logarithmic));
        ImGui::SameLine(0, 14); edited(sliderF("Scale Y", &t.scaleY, 0.1f, 10, "%.2f", ImGuiSliderFlags_Logarithmic));
        if (live) { ImGui::SameLine(0, 14); edited(ImGui::Checkbox("Vertical", &t.vertical)); }
        if (ch && live) { renderFill(doc, L); changed(); }
        break;
    }
    case T_MARK: {
        const int mm = markMode();
        if (mm == MK_SAMPLER) {
            if (doc.samplers.empty()) ImGui::TextDisabled("Click to place a sampler (up to ten), drag one to move it, Alt+click takes it away");
            for (size_t i = 0; i < doc.samplers.size(); ++i) {
                uint8_t c[3]; sampledColour(doc.samplers[i].x, doc.samplers[i].y, c);
                if (i) ImGui::SameLine(0, 14);
                ImGui::ColorButton(("##smp" + std::to_string(i)).c_str(), ImVec4(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
                ImGui::SameLine(0, 4); ImGui::Text("#%d  %d %d %d", (int)i + 1, c[0], c[1], c[2]);
            }
            if (!doc.samplers.empty()) { ImGui::SameLine(0, 18); if (ImGui::Button("Clear All")) { remember("Clear Samplers"); doc.samplers.clear(); } }
        } else if (mm == MK_RULER) {
            if (!doc.hasRuler) ImGui::TextDisabled("Drag to measure; Shift keeps it to 45-degree steps");
            else {
                const float* r = doc.ruler;
                ImGui::Text("X %.0f  Y %.0f   W %.0f  H %.0f   A %.1f\xc2\xb0   L %.1f px", r[0], r[1], r[2] - r[0], r[3] - r[1], rulerAngle(), std::hypot(r[2] - r[0], r[3] - r[1]));
                ImGui::SameLine(0, 18);
                if (ImGui::Button("Straighten Layer")) {   // the line made level: the layer turned by the ruler's angle
                    float a = rulerAngle();
                    while (a > 45) a -= 90;
                    while (a < -45) a += 90;
                    if (!pixelLayer()) say("Straighten works on a layer's pixels");
                    else { remember("Straighten Layer"); transformLayer(doc, 1, 1, a); doc.hasRuler = false; changed(); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns the active layer so the ruler's line is level (or upright)");
                ImGui::SameLine(); if (ImGui::Button("Clear")) doc.hasRuler = false;
            }
        } else if (mm == MK_COUNT) {
            ImGui::Text("Count: %d", (int)doc.counts.size());
            ImGui::SameLine(0, 14); ImGui::ColorEdit3("##cc", countColour, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine(0, 14); if (ImGui::Button("Clear")) { remember("Clear Count"); doc.counts.clear(); }
            ImGui::SameLine(0, 14); ImGui::TextDisabled("Click adds a number, Alt+click takes one away");
        } else {
            if (noteSel >= 0 && noteSel < (int)doc.notes.size()) {
                char t[1024]; snprintf(t, sizeof t, "%s", doc.notes[noteSel].text.c_str());
                ImGui::SetNextItemWidth(420);
                if (ImGui::InputTextWithHint("##note", "Write the note here", t, sizeof t)) { doc.notes[noteSel].text = t; doc.dirty = true; }
                ImGui::SameLine(0, 8); if (ImGui::Button("Delete")) { remember("Delete Note"); doc.notes.erase(doc.notes.begin() + noteSel); noteSel = -1; }
            } else ImGui::TextDisabled("Click to add a note, click one to read and change it, Alt+click takes it away");
            if (!doc.notes.empty()) { ImGui::SameLine(0, 14); if (ImGui::Button("Clear All")) { remember("Clear Notes"); doc.notes.clear(); noteSel = -1; } }
        }
        break;
    }
    case T_SHAPE: {
        // the active shape layer's shape, or the next new shape's settings
        Layer& L = doc.cur();
        const bool live = L.isShape();
        Shape& sh = live ? L.shape : shapeNext;
        const int kind = live ? sh.kind : shapeKind();
        bool ch = false;
        auto edited = [&](bool c) { if (live && ImGui::IsItemActivated()) remember("Shape"); ch |= c; };
        if (live) { ImGui::TextDisabled("%s", L.name.c_str()); ImGui::SameLine(0, 14); }
        if (Toolbar::current() == Toolbar::CUSTOM_SHAPE) {
            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo("Shape", CUSTOM_SHAPE_NAMES[customShapePick], ImGuiComboFlags_HeightLarge)) {
                for (int i = 0; i < IM_ARRAYSIZE(CUSTOM_SHAPE_NAMES); ++i) {
                    if (i % 4) ImGui::SameLine(0, 4);
                    ImGui::PushID(i);
                    ImGui::Button("##shape", ImVec2(72, 70));
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    const int previousPick = customShapePick; customShapePick = i;
                    const std::vector<float> outline = shapeOutline(customShapeFromDrag(ImVec2(18, 4), ImVec2(54, 42)));
                    customShapePick = previousPick;
                    std::vector<ImVec2> icon;
                    for (size_t p = 0; p + 1 < outline.size(); p += 2) icon.push_back(ImVec2(a.x + 9 + outline[p], a.y + 4 + outline[p + 1]));
                    if (icon.size() > 1) ImGui::GetWindowDrawList()->AddPolyline(icon.data(), (int)icon.size(), C_PINK, ImDrawFlags_Closed, 1.5f);
                    const ImVec2 text = ImGui::CalcTextSize(CUSTOM_SHAPE_NAMES[i]);
                    ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + (72 - text.x) * 0.5f, b.y - text.y - 4), C_TEXT, CUSTOM_SHAPE_NAMES[i]);
                    if (ImGui::IsItemClicked()) customShapePick = i;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(0, 12);
        }
        edited(ImGui::Checkbox("Fill", &sh.fillOn));
        if (live && L.fill.kind == FILL_SOLID) { ImGui::SameLine(0, 4); edited(ImGui::ColorEdit3("##sfc", L.fill.colour, ImGuiColorEditFlags_NoInputs)); }
        ImGui::SameLine(0, 14);
        edited(ImGui::Checkbox("Stroke", &sh.strokeOn));
        ImGui::SameLine(0, 4); edited(ImGui::ColorEdit3("##ssc", sh.stroke, ImGuiColorEditFlags_NoInputs));
        ImGui::SameLine(0, 8); edited(sliderF("Width", &sh.strokeWidth, 0.5f, 200, "%.1f px", ImGuiSliderFlags_Logarithmic));
        ImGui::SameLine(0, 14);
        if (kind == SHAPE_LINE) edited(sliderF("Weight", &sh.weight, 0.5f, 500, "%.1f px", ImGuiSliderFlags_Logarithmic));
        else if (kind != SHAPE_ELLIPSE) edited(sliderF("Radius", &sh.radius, 0, 500, "%.0f px"));
        if (kind == SHAPE_POLYGON || kind == SHAPE_STAR) { ImGui::SameLine(0, 14); edited(sliderI("Sides", &sh.sides, 3, 64)); }
        if (kind == SHAPE_STAR) { float in = sh.inner * 100; ImGui::SameLine(0, 14); if (sliderF("Indent", &in, 5, 95, "%.0f%%")) { sh.inner = in / 100; edited(true); } else edited(false); }
        ImGui::SameLine(0, 14);
        ImGui::TextDisabled(live ? "Changes this shape layer" : Toolbar::current() == Toolbar::CUSTOM_SHAPE ? "Drag to place; Shift keeps proportions" : "Drag to draw: Shift keeps it regular, Alt draws from the middle");
        if (ch && live) { renderFill(doc, L); changed(); }
        break;
    }
    case T_HEAL: {
        const int hm = healMode();
        if (hm < HM_PATCH) { sliderF("Size", &healSizes[hm], 1, 1000, "%.0f px", ImGuiSliderFlags_Logarithmic); ImGui::SameLine(0, 14); }
        if (hm == HM_HEALING) { ImGui::Checkbox("Aligned", &healAligned); ImGui::SameLine(0, 14); }
        if (hm >= HM_PATCH) { selModes(); ImGui::SameLine(0, 14); }
        const char* tips[] = {"Paint over a spot: it's filled from what's round it",
                              "Paint over what should go (a person, a wire, a sign): it's filled from what's round it",
                              healSrcSet ? "Paint over what to fix: texture from the source, colour from round it; Alt+click sets a new source" : "Alt+click where to copy from, then paint over what to fix",
                              "Draw round what to fix, then drag the selection onto good texture",
                              "Draw round an object, then drag it: it moves and where it was is filled"};
        ImGui::TextDisabled("%s", tips[hm]);
        break;
    }
    case T_REDEYE:
        sliderF("Pupil Size", &pupilSize, 1, 100, "%.0f%%"); ImGui::SameLine(0, 14);
        sliderF("Darken", &redDarken, 1, 100, "%.0f%%"); ImGui::SameLine(0, 14);
        ImGui::TextDisabled("Click a red pupil");
        break;
    case T_ROW: case T_COL: selModes(); ImGui::SameLine(0, 14); ImGui::TextDisabled("Click: a selection one pixel %s", tool == T_ROW ? "high, across the picture" : "wide, down the picture"); break;
    case T_MAGIC_ERASER:
        sliderI("Tolerance", &tolerance, 0, 255); ImGui::SameLine(0, 10);
        ImGui::Checkbox("Contiguous", &contiguous); ImGui::SameLine(0, 10);
        ImGui::Checkbox("Sample all layers", &sampleAll); ImGui::SameLine(0, 10);
        ImGui::TextDisabled("Click: what the Magic Wand would pick goes transparent");
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

// a fill layer's settings (Properties): its colour, or its gradient's colours, style, angle and scale
static void fillPanel() {
    Layer& l = doc.cur();
    Fill& f = l.fill;
    ImGui::TextColored(col4(C_PINK), "PROPERTIES");
    ImGui::SameLine(); ImGui::TextDisabled("%s", l.isText() ? (f.kind == FILL_SOLID ? "Text (colour)" : "Text (gradient)") : l.isShape() ? (std::string(shapeName(l.shape.kind)) + (f.kind == FILL_SOLID ? " (colour)" : " (gradient)")).c_str() : f.kind == FILL_SOLID ? "Color Fill" : "Gradient Fill");
    ImGui::PushItemWidth(-1);
    bool ch = false;
    if (f.kind == FILL_SOLID) ch |= adjColor("##fc", f.colour);
    else {
        ImGui::TextUnformatted("From"); ImGui::SameLine(60); ch |= adjColor("##g1", f.colour);
        ImGui::SameLine(); ImGui::TextUnformatted("to"); ImGui::SameLine(); ch |= adjColor("##g2", f.colour2);
        int st = f.style;
        const char* names[GRAD_COUNT]; for (int k = 0; k < GRAD_COUNT; ++k) names[k] = gradStyleName(k);
        if (ImGui::Combo("##gs", &st, names, GRAD_COUNT)) { remember("Gradient Style"); f.style = st; ch = true; changed(); }
        ch |= adjSlider("##ga", &f.angle, -180, 180, "Angle %.0f\xc2\xb0");
        float sc = f.scale * 100;
        if (adjSlider("##gsc", &sc, 10, 1000, "Scale %.0f%%")) { f.scale = sc / 100; ch = true; }
        ch |= adjCheck("Reverse", &f.reverse);
    }
    if(l.isText()){
        Text& text=l.text;const auto styles=text.characterStyles();size_t start=0,end=styles.size();
        if(typeLayer==doc.active&&typeStyleSelectionValid&&typeStyleSelectionLayer==typeLayer){start=std::min(typeStyleSelectionStart,styles.size());end=std::min(typeStyleSelectionEnd,styles.size());}
        const size_t metricAt=start<end?start:(start?start-1:0);const auto runs=text.normalizedRuns();
        if(!styles.empty()||!runs.empty()){
            ImGui::SeparatorText("Character");
            const TextStyle& shownStyle=styles.empty()?runs.front().style:styles[std::min(metricAt,styles.size()-1)];
            const float metricSize=std::max(1.0f,shownStyle.size);
            const float metricVScale=std::max(.01f,shownStyle.verticalScale);
            const float metricLayerScale=std::max(.01f,text.scaleY);
            const float metricDpi=std::max(1.0f,doc.dpi);
            float leading=shownStyle.leading*metricSize*metricVScale*metricLayerScale*72.0f/metricDpi;
            if(adjSlider("Leading",&leading,.1f,5000,"%.1f pt")){Text updated=text;if(typeSetCharacterMetricValue(updated,start,end,0,leading,doc.dpi,text.scaleY)){typeRememberDiscrete("Leading");text=std::move(updated);ch=true;}}
            float baseline=shownStyle.baselineShift;
            if(adjSlider("Baseline Shift",&baseline,-1296,1296,"%.1f pt")){Text updated=text;if(typeSetCharacterMetricValue(updated,start,end,1,baseline,doc.dpi,text.scaleY)){typeRememberDiscrete("Baseline Shift");text=std::move(updated);ch=true;}}
            float hscale=shownStyle.horizontalScale*100;
            if(adjSlider("Horizontal Scale",&hscale,1,1000,"%.0f%%")){Text updated=text;if(typeSetCharacterMetricValue(updated,start,end,2,hscale,doc.dpi,text.scaleY)){typeRememberDiscrete("Horizontal Scale");text=std::move(updated);ch=true;}}
            float vscale=shownStyle.verticalScale*100;
            if(adjSlider("Vertical Scale",&vscale,1,1000,"%.0f%%")){Text updated=text;if(typeSetCharacterMetricValue(updated,start,end,3,vscale,doc.dpi,text.scaleY)){typeRememberDiscrete("Vertical Scale");text=std::move(updated);ch=true;}}
        }
        auto styleValue=[&](const TextStyle& style,int index){
            if(index==0)return style.bold;
            if(index==1)return style.italic;
            if(index==2)return style.underline;
            return style.strikethrough;
        };
        auto styleShown=[&](int index){
            if(styles.empty())return !runs.empty()&&styleValue(runs.front().style,index);
            if(start<end){for(size_t i=start;i<end;++i)if(!styleValue(styles[i],index))return false;return true;}
            return styleValue(styles[std::min(start?start-1:0,styles.size()-1)],index);
        };
        const char* styleNames[]={"Faux Bold","Faux Italic","Underline","Strikethrough"};
        for(int index=0;index<4;++index){
            bool enabled=styleShown(index);
            if(ImGui::Checkbox(styleNames[index],&enabled)){
                Text updated=text;
                if(typeToggleCharacterStyle(updated,start,end,index)){
                    typeRememberDiscrete(styleNames[index]);text=std::move(updated);ch=true;
                }
            }
        }
        auto shown=[&](TextCaps caps){
            if(styles.empty()){const auto runs=text.normalizedRuns();return !runs.empty()&&runs.front().style.caps==caps;}
            if(start<end){for(size_t i=start;i<end;++i)if(styles[i].caps!=caps)return false;return true;}
            const size_t at=start?start-1:0;return styles[std::min(at,styles.size()-1)].caps==caps;
        };
        bool allCaps=shown(TextCaps::All);if(ImGui::Checkbox("All Caps",&allCaps)){Text updated=text;if(typeToggleCharacterStyle(updated,start,end,4)){typeRememberDiscrete("All Caps");text=std::move(updated);ch=true;}}
        bool smallCaps=shown(TextCaps::Small);if(ImGui::Checkbox("Small Caps",&smallCaps)){Text updated=text;if(typeToggleCharacterStyle(updated,start,end,5)){typeRememberDiscrete("Small Caps");text=std::move(updated);ch=true;}}
        ImGui::SeparatorText("Paragraph");
        size_t paragraphStart=0,paragraphEnd=0;
        if(typeLayer==doc.active&&typeStyleSelectionValid&&typeStyleSelectionLayer==typeLayer){
            paragraphStart=std::min(typeStyleSelectionStart,styles.size());
            paragraphEnd=std::min(typeStyleSelectionEnd,styles.size());
        }
        const ParagraphStyle paragraph=typeParagraphStyleAt(text,paragraphStart);
        const char* alignments[]={"Left","Center","Right","Justify Last Left","Justify Last Center","Justify Last Right","Justify All"};
        const int alignmentCount=text.boxWidth>0?IM_ARRAYSIZE(alignments):3;
        int alignment=std::clamp(paragraph.align,0,alignmentCount-1);
        if(ImGui::Combo("Alignment",&alignment,alignments,alignmentCount)){
            Text updated=text;if(typeSetParagraphStyle(updated,paragraphStart,paragraphEnd,0,(float)alignment)){
                typeRememberDiscrete("Paragraph Alignment");text=std::move(updated);ch=true;
            }
        }
        auto paragraphSlider=[&](const char* label,int property,float value,float lo,float hi,const char* format){
            if(sliderF(label,&value,lo,hi,format)){
                Text updated=text;if(typeSetParagraphStyle(updated,paragraphStart,paragraphEnd,property,value)){
                    typeRememberDiscrete(label);text=std::move(updated);ch=true;
                }
            }
        };
        paragraphSlider("First line indent",1,paragraph.firstIndent,-1296,1296,"%.1f pt");
        paragraphSlider("Start indent",2,paragraph.startIndent,-1296,1296,"%.1f pt");
        paragraphSlider("End indent",3,paragraph.endIndent,-1296,1296,"%.1f pt");
        paragraphSlider("Space before",4,paragraph.spaceBefore,0,1296,"%.1f pt");
        paragraphSlider("Space after",5,paragraph.spaceAfter,0,1296,"%.1f pt");
        paragraphSlider("Auto leading",6,paragraph.autoLeading,.1f,10,"%.2f");
    }
    ImGui::PopItemWidth();
    if (ch) { renderFill(doc, l); changed(); }
    ImGui::Spacing(); ImGui::Separator();
}
static void typeDefaultsPanel() {
    Text& text=typeNext;
    ImGui::TextColored(col4(C_PINK),"TYPE DEFAULTS");
    ImGui::PushItemWidth(-1);
    auto runs=text.normalizedRuns();
    if(!runs.empty()){
        ImGui::SeparatorText("Character");
        TextStyle style=runs.front().style;
        auto metric=[&](const char* label,int property,float value,float lo,float hi,const char* format){
            if(sliderF(label,&value,lo,hi,format)){
                Text updated=text;
                if(typeSetCharacterMetricValue(updated,0,0,property,value,doc.dpi,text.scaleY))text=std::move(updated);
            }
        };
        float size=style.size;
        if(sliderF("Size",&size,4,1000,"%.0f px")){
            Text updated=text;if(typeSetCharacterSize(updated,0,0,size))text=std::move(updated);
        }
        float leading=style.leading*std::max(1.0f,style.size)*std::max(.01f,style.verticalScale)*
            std::max(.01f,text.scaleY)*72.0f/std::max(1.0f,doc.dpi);
        metric("Leading",0,leading,.1f,5000,"%.1f pt");
        metric("Baseline Shift",1,style.baselineShift,-1296,1296,"%.1f pt");
        float tracking=style.tracking;
        if(sliderF("Tracking",&tracking,-1000,10000,"%.0f")){
            Text updated=text;if(typeSetCharacterTracking(updated,0,0,tracking))text=std::move(updated);
        }
        float horizontal=style.horizontalScale*100;
        metric("Horizontal Scale",2,horizontal,1,1000,"%.0f%%");
        float vertical=style.verticalScale*100;
        metric("Vertical Scale",3,vertical,1,1000,"%.0f%%");
        const char* names[]={"Faux Bold","Faux Italic","Underline","Strikethrough","All Caps","Small Caps"};
        for(int i=0;i<6;++i){
            const TextStyle current=text.normalizedRuns().front().style;
            bool enabled=i==0?current.bold:i==1?current.italic:i==2?current.underline:i==3?current.strikethrough:
                i==4?current.caps==TextCaps::All:current.caps==TextCaps::Small;
            if(ImGui::Checkbox(names[i],&enabled)){
                Text updated=text;if(typeToggleCharacterStyle(updated,0,0,i))text=std::move(updated);
            }
        }
    }
    ImGui::SeparatorText("Paragraph");
    const ParagraphStyle paragraph=typeParagraphStyleAt(text,0);
    const char* alignments[]={"Left","Center","Right","Justify Last Left","Justify Last Center","Justify Last Right","Justify All"};
    int align=std::clamp(paragraph.align,0,2);
    if(ImGui::Combo("Alignment",&align,alignments,3)){
        Text updated=text;
        if(typeSetParagraphStyle(updated,0,0,0,(float)align)){updated.align=align;text=std::move(updated);}
    }
    auto paragraphSlider=[&](const char* label,int property,float value,float lo,float hi,const char* format){
        if(sliderF(label,&value,lo,hi,format)){
            Text updated=text;if(typeSetParagraphStyle(updated,0,0,property,value))text=std::move(updated);
        }
    };
    paragraphSlider("First line indent",1,paragraph.firstIndent,-1296,1296,"%.1f pt");
    paragraphSlider("Start indent",2,paragraph.startIndent,-1296,1296,"%.1f pt");
    paragraphSlider("End indent",3,paragraph.endIndent,-1296,1296,"%.1f pt");
    paragraphSlider("Space before",4,paragraph.spaceBefore,0,1296,"%.1f pt");
    paragraphSlider("Space after",5,paragraph.spaceAfter,0,1296,"%.1f pt");
    paragraphSlider("Auto leading",6,paragraph.autoLeading,.1f,10,"%.2f");
    ImGui::PopItemWidth();
    ImGui::Separator();
}
static void newFillMenu() {
    if (ImGui::MenuItem("Solid Color...")) { remember("New Fill Layer"); Fill f; f.kind = FILL_SOLID; addFillLayer(doc, f); changed(); }
    if (ImGui::MenuItem("Gradient...")) { remember("New Fill Layer"); Fill f; f.kind = FILL_GRADIENT; f.colour[0] = f.colour[1] = f.colour[2] = 0; addFillLayer(doc, f); changed(); }
}
static void toggleClip() {
    if (doc.active == 0) return;
    remember(doc.cur().clip ? "Release Clipping Mask" : "Create Clipping Mask");
    doc.cur().clip = !doc.cur().clip;
    changed();
}
static void newAdjustmentMenu() {
    for (int k = ADJ_BRIGHTNESS_CONTRAST; k < ADJ_COUNT; ++k) {
        if (k == ADJ_VIBRANCE || k == ADJ_BLACK_WHITE || k == ADJ_INVERT) ImGui::Separator();
        if (ImGui::MenuItem((std::string(adjName(k)) + "...").c_str())) { remember("New " + std::string(adjName(k))); addAdjustmentLayer(doc, k); changed(); }
    }
}

static void layersPanel(float height) {
    Layer& cur = doc.cur();
    LayerGroup* groupCur = nullptr;
    if (activeGroupId) for (LayerGroup& g : doc.groups) if (g.id == activeGroupId) { groupCur = &g; break; }
    ImGui::TextColored(col4(C_PINK), "LAYERS");
    ImGui::PushItemWidth(-1);
    int& blendMode = groupCur ? groupCur->blend : cur.blend;
    float& opacity = groupCur ? groupCur->opacity : cur.opacity;
    if (ImGui::BeginCombo("##blend", blendName(blendMode))) {
        for (int m = 0; m < BM_Count; ++m) {
            if (blendGroupStart(m)) ImGui::Separator();
            if (ImGui::Selectable(blendName(m), m == blendMode)) { remember(groupCur ? "Group Blend Mode" : "Blend Mode"); blendMode = m; changed(); }
        }
        ImGui::EndCombo();
    }
    float op = opacity * 100;
    static bool opEditing = false;
    if (ImGui::SliderFloat("##op", &op, 0, 100, "Opacity %.0f%%")) { if (!opEditing) { remember(groupCur ? "Group Opacity" : "Opacity"); opEditing = true; } opacity = op / 100; changed(); }
    if (ImGui::IsItemDeactivated()) opEditing = false;
    ImGui::PopItemWidth();

    ImGui::BeginChild("layerlist", ImVec2(0, height), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::vector<LayerTreeEntry> tree = layerTreeWalk(doc);
    std::stable_sort(tree.begin(), tree.end(), [](const LayerTreeEntry& a, const LayerTreeEntry& b) {
        const size_t n = std::min(a.path.size(), b.path.size());
        for (size_t i = 0; i < n; ++i) if (a.path[i] != b.path[i]) return a.path[i] > b.path[i];
        return a.path.size() < b.path.size();
    });
    for (const LayerTreeEntry& entry : tree) {
        if (entry.group) {
            auto g = std::find_if(doc.groups.begin(), doc.groups.end(), [&](const LayerGroup& q) { return q.id == entry.id; });
            if (g == doc.groups.end()) continue;
            bool shown = true;
            for (int parent = g->parentId; parent && shown;) {
                auto p = std::find_if(doc.groups.begin(), doc.groups.end(), [&](const LayerGroup& q) { return q.id == parent; });
                if (p == doc.groups.end()) break;
                shown = p->expanded; parent = p->parentId;
            }
            if (!shown) continue;
            const ImVec2 ga = ImGui::GetCursorScreenPos();
            const float gw = ImGui::GetContentRegionAvail().x, gh = 42;
            dl->AddRectFilled(ga, ImVec2(ga.x + gw, ga.y + gh), activeGroupId == g->id ? IM_COL32(0x16, 0x30, 0x40, 255) : C_PANEL);
            dl->AddLine(ImVec2(ga.x, ga.y + gh), ImVec2(ga.x + gw, ga.y + gh), C_LINE);
            const float indent = 12.0f * entry.depth;
            dl->AddText(ImVec2(ga.x + 12 + indent, ga.y + 13), C_TEXT, g->expanded ? "v" : ">");
            dl->AddText(ImVec2(ga.x + 30 + indent, ga.y + 12), g->visible ? C_TEXT : C_DIM, g->name.c_str());
            ImGui::SetCursorScreenPos(ImVec2(ga.x + indent, ga.y));
            ImGui::PushID(g->id + 90000);
            if (ImGui::InvisibleButton("group_row", ImVec2(gw - indent - 80, gh))) {
                activeGroupId = g->id; doc.editMask = false; layerSelection.clear();
                for (int j = 0; j < (int)doc.layers.size(); ++j) {
                    int parent = doc.layers[j].groupId;
                    while (parent && parent != g->id) { auto p = std::find_if(doc.groups.begin(), doc.groups.end(), [&](const LayerGroup& q) { return q.id == parent; }); parent = p == doc.groups.end() ? 0 : p->parentId; }
                    if (parent == g->id) { doc.active = j; layerSelection.push_back(j); }
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(ga.x + gw - 24, ga.y + 7));
            if (ImGui::SmallButton(g->expanded ? "-" : "+")) g->expanded = !g->expanded;
            ImGui::SetCursorScreenPos(ImVec2(ga.x + gw - 50, ga.y + 7));
            if (ImGui::SmallButton(g->visible ? "o" : "x")) { remember(g->visible ? "Hide Group" : "Show Group"); g->visible = !g->visible; changed(); }
            ImGui::SetCursorScreenPos(ImVec2(ga.x + gw - 76, ga.y + 7));
            if (ImGui::SmallButton(g->locked ? "L" : "U")) { remember(g->locked ? "Unlock Group" : "Lock Group"); g->locked = !g->locked; changed(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(g->locked ? "Unlock this group and its contents" : "Lock this group and its contents");
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(ga.x, ga.y + gh + 1));
            continue;
        }
        int i = entry.layerIndex;
        bool shown = true;
        for (int parent = doc.layers[i].groupId; parent && shown;) {
            auto g = std::find_if(doc.groups.begin(), doc.groups.end(), [&](const LayerGroup& q) { return q.id == parent; });
            if (g == doc.groups.end()) break;
            shown = g->expanded; parent = g->parentId;
        }
        if (!shown) continue;
        Layer& l = doc.layers[i];
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = 48;
        const bool active = i == doc.active || std::find(layerSelection.begin(), layerSelection.end(), i) != layerSelection.end();
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), active ? IM_COL32(0x16, 0x30, 0x40, 255) : C_PANEL);
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
        const float indent = (l.clip && i > 0 ? 16.0f : 0.0f) + 12.0f * entry.depth;
        const ImVec2 tp(a.x + 28 + indent, a.y + 4);
        if (indent > 0) {   // clipped to the layer below: Photoshop's bent arrow
            const float ax = a.x + 34, ay = a.y + 12;
            dl->AddLine(ImVec2(ax, ay), ImVec2(ax, ay + 18), C_TEXT, 1.5f);
            dl->AddTriangleFilled(ImVec2(ax - 4, ay + 14), ImVec2(ax + 4, ay + 14), ImVec2(ax, ay + 20), C_TEXT);
        }
        if (l.isAdjust()) {   // Photoshop's adjustment sign: a circle, half dark, half light
            const ImVec2 c(tp.x + 20, tp.y + 20);
            dl->AddRectFilled(tp, ImVec2(tp.x + 40, tp.y + 40), C_WELL);
            dl->AddCircleFilled(c, 12, C_TEXT, 32);
            dl->PathClear(); dl->PathArcTo(c, 12, 1.5708f, 4.7124f, 24); dl->PathFillConvex(IM_COL32(0x33, 0x33, 0x33, 255));
            dl->AddCircle(c, 12, C_TEXT, 32, 1.5f);
        } else if (i < (int)thumbs.size() && thumbs[i]) dl->AddImage((ImTextureID)(intptr_t)thumbs[i], tp, ImVec2(tp.x + 40, tp.y + 40));
        ImGui::SetCursorScreenPos(tp);
        if (ImGui::InvisibleButton("px", ImVec2(40, 40))) { selectLayer(i); doc.editMask = l.isAdjust() && l.hasMask; if (showMask) { showMask = false; changed(); } }
        if (l.isAdjust() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s adjustment: its settings are in Properties above", adjName(l.adj.kind));
        if (l.isFill() && ImGui::IsItemHovered()) ImGui::SetTooltip("Fill layer: its colours are in Properties above; paint its mask");
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
        if (ImGui::InvisibleButton("name", ImVec2(std::max(10.0f, a.x + w - nameX), h))) { selectLayer(i); doc.editMask = l.isAdjust() && l.hasMask; }
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
    if (btn("Mask", groupCur ? "Add a group mask" : "Add a layer mask (from the selection when there is one); Alt: hide all")) {
        if (groupCur) {
            if (!groupCur->hasMask) { remember("Add Group Mask"); groupCur->hasMask = true; groupCur->maskOn = true; groupCur->mask.assign((size_t)doc.w * doc.h, ImGui::GetIO().KeyAlt ? 0 : 255); if (doc.hasSel() && !ImGui::GetIO().KeyAlt) groupCur->mask = doc.sel; changed(); }
            else if (ImGui::GetIO().KeyShift) { remember("Remove Group Mask"); groupCur->hasMask = false; groupCur->mask.clear(); changed(); }
            else { remember(groupCur->maskOn ? "Disable Group Mask" : "Enable Group Mask"); groupCur->maskOn = !groupCur->maskOn; changed(); }
        } else if (!cur.hasMask) { remember("Add Mask"); addMask(doc, doc.hasSel() ? 2 : (ImGui::GetIO().KeyAlt ? 1 : 0)); changed(); }
        else say("The layer has a mask already");
    }
    if (btn("Adj", "New adjustment layer")) ImGui::OpenPopup("newadj");
    if (ImGui::BeginPopup("newadj")) { newAdjustmentMenu(); ImGui::EndPopup(); }
    if (btn("Dup", "Duplicate (Ctrl+J)")) { remember("Duplicate"); duplicateLayer(doc); changed(); }
    if (btn("↑", "Move the layer up")) { remember("Reorder"); moveLayer(doc, 1); changed(); }
    if (btn("↓", "Move the layer down")) { remember("Reorder"); moveLayer(doc, -1); changed(); }
    if (btn("Merge", "Merge down (Ctrl+E)")) { remember("Merge Down"); mergeDown(doc); changed(); }
    if (btn("Del", "Delete the layer")) { remember("Delete Layer"); deleteLayer(doc); changed(); }
    if (btn("Grp", "Group the selected contiguous layers (Ctrl+G)")) {
        if (layerSelection.empty()) layerSelection.push_back(doc.active);
        remember("Group Layers");
        if (groupLayers(doc, layerSelection)) { activeGroupId = doc.cur().groupId; changed(); }
        else { hist.doUndo(doc); say("Select a contiguous range of layers to group"); }
    }
    if (btn("Ungrp", "Ungroup the selected group")) { if (ungroupActiveLayer(doc)) { remember("Ungroup Layers"); activeGroupId = 0; changed(); } }
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
        if (item("Export Slices as Images...", nullptr, !noDoc)) exportSlices();
        if (item("Export Artboards to Files...", nullptr, !noDoc && !doc.artboards.empty())) exportArtboards();
        if (item("Export ICO...", nullptr, !noDoc)) saveAs(false, true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "A Windows icon: the picture at 16, 24, 32, 48, 64, 128 and 256 px in one .ico (not square: fitted, transparent round it)");
        if (item("Save with Inoculant...", nullptr, !noDoc && inoculantHave())) inoculantSave();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", inoculantHave() ? "Export the picture protected against face swaps and face recognition" : "Needs Inoculant (Atelier) on this PC");
        ImGui::Separator();
        if (item("Open and Cure with Inoculant...", nullptr, inoculantHave())) inoculantOpen();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", inoculantHave() ? "Wash a cloak off a picture as it opens (and see if it has one)" : "Needs Inoculant (Atelier) on this PC");
        ImGui::Separator();
        if (item("Close", "Ctrl+W", !noDoc || tabs.size() > 1)) askCloseTab(curTab);
        ImGui::Separator();
        if (item("Quit", "Ctrl+Q")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Preferences")) {
        if (item("Type...")) ImGui::OpenPopup("Type Preferences");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit", !noDoc)) {
        const bool boxHistory=cropHistoryActive();
        const bool canUndo=boxHistory?!cropUndo.empty():hist.canUndo(),canRedo=boxHistory?!cropRedo.empty():hist.canRedo();
        if (item(boxHistory?(canUndo?"Undo Crop":"Undo"):hist.canUndo() ? ("Undo " + hist.undoNames.back()).c_str() : "Undo", "Ctrl+Z", canUndo)) { if(boxHistory)cropHistoryMove(false);else {hist.doUndo(doc); changed(); selChanged();} }
        if (item(boxHistory?(canRedo?"Redo Crop":"Redo"):hist.canRedo() ? ("Redo " + hist.redoNames.back()).c_str() : "Redo", "Ctrl+Shift+Z", canRedo)) { if(boxHistory)cropHistoryMove(true);else {hist.doRedo(doc); changed(); selChanged();} }
        ImGui::Separator();
        if (item("Fill with Foreground", "Alt+Backspace")) fillWith(fg, "Fill");
        if (item("Fill with Background", "Ctrl+Backspace")) fillWith(bgc, "Fill");
        if (item("Clear", "Delete")) clearSelection();
        if (item("Content-Aware Fill", "Shift+F5", doc.hasSel())) contentAwareFillSelection();
        if (item("Define Pattern...")) {   // the selection's box of the picture (or all of it), as a pattern for the Pattern Stamp
            Rect r = doc.hasSel() ? doc.selBounds() : Rect{0, 0, doc.w, doc.h};
            r = r.clip(doc.w, doc.h);
            if (!r.empty() && (r.x1 - r.x0) <= 1024 && (r.y1 - r.y0) <= 1024) {
                Image all(doc.w, doc.h); composite(doc, all, {0, 0, doc.w, doc.h}, false);
                Image pt(r.x1 - r.x0, r.y1 - r.y0);
                for (int y = 0; y < pt.h; ++y) memcpy(pt.at(0, y), all.at(r.x0, y + r.y0), (size_t)pt.w * 4);
                retouchOpts();
                patterns.push_back({"Pattern " + std::to_string(patterns.size() - 3), pt}); patternPick = (int)patterns.size() - 1;
                say("Defined a pattern: pick the Pattern Stamp to paint with it");
            } else say("Select at most 1024 x 1024 pixels to define a pattern");
        }
        ImGui::Separator();
        if (item("Free Transform...", "Ctrl+T")) openDlg(D_TRANSFORM);
        if (item("Flip Horizontal")) { remember("Flip"); flipLayer(doc, true); changed(); }
        if (item("Flip Vertical")) { remember("Flip"); flipLayer(doc, false); changed(); }
        ImGui::Separator();
        if (ImGui::BeginMenu("Preferences")) {
            if (item("Type...")) ImGui::OpenPopup("Type Preferences");
            ImGui::EndMenu();
        }
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
        if (item("Group Layers", "Ctrl+G")) {
            if (layerSelection.empty()) layerSelection.push_back(doc.active);
            remember("Group Layers");
            if (groupLayers(doc, layerSelection)) { activeGroupId = doc.cur().groupId; changed(); }
            else { hist.doUndo(doc); say("Select a contiguous range of layers to group"); }
        }
        if (item("Ungroup Layers", nullptr, doc.cur().groupId != 0)) { remember("Ungroup Layers"); ungroupActiveLayer(doc); activeGroupId = 0; changed(); }
        if (item("Duplicate Layer", "Ctrl+J")) { remember("Duplicate"); duplicateLayer(doc); changed(); }
        if (item("Delete Layer", nullptr, doc.layers.size() > 1)) { remember("Delete Layer"); deleteLayer(doc); changed(); }
        if (item("Rename Layer...")) openDlg(D_RENAME);
        if (ImGui::BeginMenu("New Fill Layer")) { newFillMenu(); ImGui::EndMenu(); }
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
        if (item(doc.cur().clip ? "Release Clipping Mask" : "Create Clipping Mask", "Alt+Ctrl+G", doc.active > 0)) toggleClip();
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
        if (ImGui::BeginMenu("Modify", doc.hasSel())) {
            if (item("Border...")) openDlg(D_BORDER);
            if (item("Smooth...")) openDlg(D_SMOOTH);
            if (item("Expand...")) openDlg(D_EXPAND);
            if (item("Contract...")) openDlg(D_CONTRACT);
            if (item("Feather...", "Shift+F6")) openDlg(D_FEATHER);
            ImGui::EndMenu();
        }
        if (item("Select and Mask...", "Alt+Ctrl+R", doc.hasSel() || doc.cur().hasMask)) openDlg(D_REFINE);
        if (item("Load from Mask / Transparency")) { remember("Load Selection"); selectionFromMask(doc); selChanged(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Filter", !noDoc)) {
        if (ImGui::BeginMenu("Blur")) {
            if (item("Average")) { remember("Average"); Work o = getWork(doc), r = o; filterAverage(doc, r); putWork(doc, o, r); changed(); }
            if (item("Box Blur...")) openDlg(D_BOX);
            if (item("Gaussian Blur...")) openDlg(D_GAUSS);
            if (item("Motion Blur...")) openDlg(D_MOTION);
            if (item("Radial Blur...")) openDlg(D_RADIAL);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Distort")) {
            if (item("Pinch...")) openDlg(D_PINCH);
            if (item("Polar Coordinates...")) openDlg(D_POLAR);
            if (item("Ripple...")) openDlg(D_RIPPLE);
            if (item("Spherize...")) openDlg(D_SPHERIZE);
            if (item("Twirl...")) openDlg(D_TWIRL);
            if (item("Wave...")) openDlg(D_WAVE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Noise")) { if (item("Add Noise...")) openDlg(D_NOISE); if (item("Median...")) openDlg(D_MEDIAN); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Pixelate")) {
            if (item("Crystallize...")) openDlg(D_CRYSTAL);
            if (item("Mosaic...")) openDlg(D_MOSAIC);
            if (item("Pointillize...")) openDlg(D_POINTIL);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Render")) {
            // between the foreground and background colours, new every time
            const bool diff = item("Difference Clouds");
            if (item("Clouds") || diff) {
                remember(diff ? "Difference Clouds" : "Clouds");
                Work o = getWork(doc), r = o;
                fxClouds(r, fxBounds(doc, r), fg, bgc, SDL_GetTicks() * 2654435761u, diff);
                putWork(doc, o, r); changed();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Sharpen")) { if (item("Unsharp Mask...")) openDlg(D_UNSHARP); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Stylize")) {
            if (item("Emboss...")) openDlg(D_EMBOSS);
            if (item("Find Edges")) { remember("Find Edges"); Work o = getWork(doc), r = o; fxFindEdges(r); putWork(doc, o, r); changed(); }
            if (item("Solarize")) { remember("Solarize"); Work o = getWork(doc), r = o; fxSolarize(r); putWork(doc, o, r); changed(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Other")) { if (item("High Pass...")) openDlg(D_HIGHPASS); ImGui::EndMenu(); }
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
    if (ImGui::BeginMenu("Help")) { if (item("About Chimera")) openDlg(D_ABOUT); ImGui::EndMenu(); }
    ImGui::EndMainMenuBar();
}

static void typePreferencesDialog(){
    if(typePreferencesScriptOpen){ImGui::OpenPopup("Type Preferences");typePreferencesScriptOpen=false;}
    if(!ImGui::BeginPopupModal("Type Preferences",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextUnformatted("Type");ImGui::Separator();
    bool changedPrefs=false;
    changedPrefs|=ImGui::Checkbox("Use Smart Quotes",&typeSmartQuotesEnabled);
    changedPrefs|=ImGui::Checkbox("Fill new Type layers with placeholder text",&typePlaceholderEnabled);
    if(ImGui::SliderInt("Recent fonts",&recentTypeFontCap,0,50)){trimRecentFonts(recentTypeFonts,(size_t)recentTypeFontCap);changedPrefs=true;}
    if(changedPrefs)saveTypePreferences();
    ImGui::Separator();if(ImGui::Button("Close",ImVec2(100,0)))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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
        if (k(ImGuiKey_Z)) { if(cropHistoryActive())cropHistoryMove(shift);else {if (shift) hist.doRedo(doc); else hist.doUndo(doc); changed(); selChanged();} }
        else if (k(ImGuiKey_Y)) { if(cropHistoryActive())cropHistoryMove(true);else {hist.doRedo(doc); changed(); selChanged();} }
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
        else if (k(ImGuiKey_G) && alt) toggleClip();   // Alt+Ctrl+G: Create / Release Clipping Mask
        else if (k(ImGuiKey_G)) {
            if (layerSelection.empty()) layerSelection.push_back(doc.active);
            remember("Group Layers");
            if (groupLayers(doc, layerSelection)) { activeGroupId = doc.cur().groupId; changed(); }
            else { hist.doUndo(doc); say("Select a contiguous range of layers to group"); }
        }
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
    if (shift && k(ImGuiKey_F5)) { contentAwareFillSelection(); return; }
    if (tool == T_MAGNETIC && magneticActive()) {
        if (k(ImGuiKey_Enter) || k(ImGuiKey_KeypadEnter)) { magneticFinish(alt); return; }
        if (k(ImGuiKey_Backspace) || k(ImGuiKey_Delete)) { magneticRemoveAnchor(); return; }
    }
    if (k(ImGuiKey_Delete) || k(ImGuiKey_Backspace)) { clearSelection(); return; }
    if (tool == T_PEN && (k(ImGuiKey_Enter) || k(ImGuiKey_KeypadEnter))) {
        if (ctrl && !doc.workPath.empty()) { combineSel(doc, pathSelection(), SEL_NEW); selChanged(); } else penFinishSubpath();
        return;
    }
    if (k(ImGuiKey_Enter) && poly.size() >= 6) { finishShape(shapePolygon(doc, poly), selMode, "Polygonal Lasso"); poly.clear(); return; }
    if (k(ImGuiKey_Escape)) { if (typePathDragPart >= 0) { typePathDragCancel(); return; } poly.clear(); lasso.clear(); magneticClear(); return; }
    // the tool letters, PhotoCraft's way: a letter picks its group's tool, Shift+letter steps through the group
    if (!cropConsumedShortcutKey && Toolbar::keys(shift, ctrl, alt)) syncTool();
    if (k(ImGuiKey_X)&&cropConsumedShortcutKey!=(int)ImGuiKey_X) { uint8_t t[4]; memcpy(t, fg, 4); memcpy(fg, bgc, 4); memcpy(bgc, t, 4); }
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
static int scriptSize[2] = {0, 0};   // size: the sizes (then OK is pressed for it)
static int scriptView = 0;                       // and Select and Mask's view, for it

void frame() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (scriptDlg != D_NONE) { openDlg(scriptDlg); if (dlg == D_REFINE) { pi[0] = scriptView; p[0] = 12; } if (scriptSize[0] > 0) { pi[0] = scriptSize[0]; pi[1] = scriptSize[1]; scriptSize[0] = 0; scriptOk = true; } scriptDlg = D_NONE; }
    menus();
    typePreferencesDialog();
    const float top = ImGui::GetFrameHeight(), optH = 38, statusH = 26, leftW = Toolbar::width(), rightW = 310;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(0, top)); ImGui::SetNextWindowSize(ImVec2(W, optH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(IM_COL32(0x21, 0x21, 0x21, 255)));
    ImGui::Begin("options", nullptr, fixed); optionsBar(); ImGui::End();
    ImGui::PopStyleColor();

    ImGui::SetNextWindowPos(ImVec2(0, top + optH)); ImGui::SetNextWindowSize(ImVec2(leftW, H - top - optH - statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 8));
    ImGui::Begin("tools", nullptr, fixed); toolsPanel(); ImGui::End();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(ImVec2(W - rightW, top + optH)); ImGui::SetNextWindowSize(ImVec2(rightW, H - top - optH - statusH));
    ImGui::Begin("panels", nullptr, fixed);
    if (noDoc) { ImGui::SeparatorText("LAYERS"); ImGui::TextDisabled("No picture open yet."); }
    else {
        float props = 0;
        if (doc.cur().isAdjust() || doc.cur().isFill()) {
            const float before = ImGui::GetCursorPosY();
            if (doc.cur().isFill()) fillPanel(); else propertiesPanel();
            props = ImGui::GetCursorPosY() - before;
        } else if (tool == T_TYPE) {
            const float before = ImGui::GetCursorPosY();
            typeDefaultsPanel();
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
    drawInoculant();
    if (tool == T_TYPE) typeEditor();
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
    static int tabAsked = -1;
    if (tabClose >= 0 && tabAsked != tabClose) { ImGui::OpenPopup("close?"); }
    tabAsked = tabClose;
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
    // the first unsaved picture is shown and asked about; when none is left, Chimera closes
    if (doc.dirty && hist.canUndo()) { askQuit = true; return; }
    for (int i = 0; i < (int)tabs.size(); ++i)
        if (i != curTab && tabs[i].doc.dirty && tabs[i].hist.canUndo()) { if (switchTab(i)) askQuit = true; return; }
    quit = true;
}

void init(SDL_Window* win, const std::string& prefDir, void (*status)(const char* what, float progress)) {
    auto step = [status](const char* what, float progress) { if (status) status(what, progress); };
    window = win; prefs = prefDir; cropLoadUserPresets(); loadTypePreferences();
    step("Applying the theme...", 0.2f);
    applyStyle();
    {   // Filter > Changeling: find Changeling Studio and ask it for the steps now, so the menu is ready
        step("Looking for Changeling Studio...", 0.35f);
        char* bp = SDL_GetBasePath();
        Changeling::init(prefDir, bp ? bp : "");
        Bathos::init(prefDir, bp ? bp : "");   // Filter > Bathos: Atelier's depth app, when it's on this PC
        MimicPlug::init(prefDir, bp ? bp : "");
        InoculantPlug::init(prefDir, bp ? bp : "");   // File > Save with / Open and Cure with Inoculant   // Filter > Mimic: Atelier's face-swap app
        Toolbar::init(std::string(bp ? bp : "") + "assets/", prefDir, toolbarToolWorks);   // the strip asks Chimera which tools work
        Toolbar::select(Toolbar::BRUSH);
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
    if (what == "crop-handles") {
        Toolbar::select(Toolbar::CROP); syncTool();
        cropR[0]=doc.w*.2f; cropR[1]=doc.h*.2f; cropR[2]=doc.w*.8f; cropR[3]=doc.h*.8f;
        const ImVec2 corner(cropR[2],cropR[3]); cropBegin(corner); cropDragTo(ImVec2(corner.x-24,corner.y-18));
        const bool resized=cropDragPart==4 && std::fabs(cropR[2]-doc.w*.8f+24)<1 && std::fabs(cropR[3]-doc.h*.8f+18)<1;
        cropBegin(ImVec2((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f));
        const float x=cropR[0], y=cropR[1]; cropDragTo(ImVec2(cropDragStart.x+12,cropDragStart.y+9));
        const bool moved=cropDragPart==8 && std::fabs(cropR[0]-x-12)<1 && std::fabs(cropR[1]-y-9)<1;
        cropDragPart=-1;
        fprintf(stderr,"crop-handles: %s; corner resize %d, interior move %d\n",resized&&moved?"passed":"failed",resized?1:0,moved?1:0);
        return;
    }
    if (what == "crop-rotate") {
        Toolbar::select(Toolbar::CROP); syncTool(); cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;cropAngle=0;
        const float cx=(cropR[0]+cropR[2])*.5f,cy=(cropR[1]+cropR[3])*.5f;
        cropBegin(ImVec2(cropR[2]+14/std::max(.01f,zoom),cy));
        cropDragTo(ImVec2(cx,cy+cropR[2]-cx+17),true);
        const bool rotated=cropDragPart==10 && std::fabs(cropAngle-90.0f*0.01745329252f)<.001f;
        cropDragPart=-1;
        const float degrees=cropAngle*57.29578f;
        applyCrop();
        const bool committed=doc.w>0 && doc.h>0 && !cropPending;
        fprintf(stderr,"crop-rotate: %s; outside-frame drag %.1f degrees, warp commit %d\n",rotated&&committed?"passed":"failed",degrees,committed?1:0);
        return;
    }
    if (what == "crop-frame") {
        Toolbar::select(Toolbar::CROP); syncTool();
        const bool ok=cropPending && cropR[0]==0 && cropR[1]==0 && cropR[2]==doc.w && cropR[3]==doc.h;
        fprintf(stderr,"crop-frame: %s; full-canvas crop frame appears on tool selection\n",ok?"passed":"failed");
        return;
    }
    if (what == "crop-overlay") {
        Toolbar::select(Toolbar::CROP); syncTool();
        cropOverlay=5; cropOverlayOrientation=3; cropOverlayShow=1;
        const bool ok=cropPending && cropOverlay==5 && cropOverlayOrientation==3 && cropOverlayShow==1;
        fprintf(stderr,"crop-overlay: %s; all six guide modes, visibility choices and orientations are available\n",ok?"passed":"failed");
        return;
    }
    if (what == "crop-shield") {
        Toolbar::select(Toolbar::CROP); syncTool(); cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;
        cropShowArea=true;cropShieldEnabled=true;cropShieldOpacity=75;cropShieldAuto=true;
        cropShieldMatchCanvas=false;cropShieldColor[0]=.8f;cropShieldColor[1]=.12f;cropShieldColor[2]=.22f;
        const int idle=(int)std::lround(cropShieldOpacity*2.55f), edit=(int)std::lround(cropShieldOpacity*2.55f*.5f);
        const bool ok=idle==191 && edit==96 && std::lround(cropShieldColor[0]*255)==204 && std::lround(cropShieldColor[1]*255)==31 && std::lround(cropShieldColor[2]*255)==56;
        fprintf(stderr,"crop-shield: %s; canvas/custom colors, opacity, auto-adjust and visibility\n",ok?"passed":"failed");return;
    }
    if (what == "crop-shield-classic") {
        Toolbar::select(Toolbar::CROP);syncTool();cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;
        cropClassicMode=true;cropShowArea=false;cropShieldEnabled=true;cropShieldMatchCanvas=false;cropShieldColor[0]=.12f;cropShieldColor[1]=.38f;cropShieldColor[2]=.72f;syncCropView();fitNext=true;
        const bool ok=cropPending&&cropShieldShowsOutside(cropShowArea,cropClassicMode);
        fprintf(stderr,"crop-shield-classic: %s; Classic Mode shows the cropped area while preserving its stored preference\n",ok?"passed":"failed");return;
    }
    if (what == "crop-frame-undo") {
        Toolbar::select(Toolbar::CROP);syncTool();cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;
        const size_t documentUndo=hist.undo.size();const CropSnapshot before=cropSnapshot();cropR[0]+=doc.w*.08f;cropR[2]+=doc.w*.08f;const CropSnapshot moved=cropSnapshot();cropHistoryRecord(cropUndo,cropRedo,before,moved);
        const ImVec2 screenCenter=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);
        const bool undone=cropHistoryMove(false)&&cropR[0]==before[0]&&std::hypot(toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f).x-screenCenter.x,toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f).y-screenCenter.y)<.1f&&hist.undo.size()==documentUndo;
        const bool redone=cropHistoryMove(true)&&cropR[0]==moved[0]&&hist.undo.size()==documentUndo;
        fitNext=true;fprintf(stderr,"crop-frame-undo: %s; undo/redo restore the pending frame without touching document history\n",undone&&redone?"passed":"failed");return;
    }
    if (what == "crop-shield-rotated") {
        Toolbar::select(Toolbar::CROP);syncTool();cropR[0]=doc.w*.2f;cropR[1]=doc.h*.18f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.82f;cropAngle=.31f;cropShowArea=true;cropShieldEnabled=true;cropShieldAuto=false;cropShieldMatchCanvas=false;cropShieldOpacity=70;cropShieldColor[0]=.1f;cropShieldColor[1]=.3f;cropShieldColor[2]=.6f;cropClassicMode=false;syncCropView();
        const bool ok=std::fabs(viewRotation+cropAngle*57.2957795f)<.01f&&cropPending;
        fprintf(stderr,"crop-shield-rotated: %s; canvas tint with crop interior restored\n",ok?"passed":"failed");return;
    }
    if (what == "crop-preset") {
        Toolbar::select(Toolbar::CROP);syncTool();cropPreset=8;strcpy_s(cropWidth,"4 in");strcpy_s(cropHeight,"5 in");strcpy_s(cropResolution,"300");cropResolutionCm=false;cropAngle=0;fitCropAspect();
        int w=0,h=0;float dpi=0;cropSizeTarget(w,h,dpi);const bool target=w==1200&&h==1500&&dpi==300;
        applyCrop();Doc roundtrip;std::string err;const std::string p=(std::filesystem::temp_directory_path()/"chimera-crop-preset.chimera").string();
        const bool persisted=doc.w==1200&&doc.h==1500&&doc.dpi==300&&saveProject(doc,p,err)&&openDoc(p,roundtrip,err)&&roundtrip.dpi==300;
        fprintf(stderr,"crop-preset: %s; 4 x 5 inch at 300 ppi gives 1200 x 1500 px and saves resolution\n",target&&persisted?"passed":"failed");return;
    }
    if (what == "crop-front-image") {
        Toolbar::select(Toolbar::CROP);syncTool();cropResolutionCm=true;cropFrontImage();
        const auto fields=cropFrontImageFields(doc.w,doc.h,doc.dpi,true);
        const bool ok=cropPreset==1&&std::string(cropWidth)==fields[0]&&std::string(cropHeight)==fields[1]&&std::string(cropResolution)==fields[2]&&cropPending;
        fitNext=true;fprintf(stderr,"crop-front-image: %s; I and Front Image fill document dimensions in the selected resolution unit\n",ok?"passed":"failed");return;
    }
    if (what == "crop-hidden-frame") {
        Toolbar::select(Toolbar::CROP);syncTool();cropPending=false;cropFrameDismissed=true;
        Toolbar::select(Toolbar::MOVE);syncTool();Toolbar::select(Toolbar::CROP);syncTool();const bool staysHidden=!cropPending;
        cropPreset=3;fitCropAspect();const bool restored=cropPending&&!cropFrameDismissed&&std::fabs((cropR[2]-cropR[0])-(cropR[3]-cropR[1]))<.1f;
        fitNext=true;fprintf(stderr,"crop-hidden-frame: %s; reselect stays hidden and choosing a ratio restores/fits the frame\n",staysHidden&&restored?"passed":"failed");return;
    }
    if (what == "crop-user-presets") {
        const auto saved=cropUserPresets;cropUserPresets.push_back({"__script crop preset","640 px","480 px","96",false});cropSaveUserPresets();cropUserPresets.clear();cropLoadUserPresets();
        const bool loaded=std::any_of(cropUserPresets.begin(),cropUserPresets.end(),[](const CropPreset&p){return p.name=="__script crop preset"&&p.width=="640 px"&&p.height=="480 px"&&p.resolution=="96";});
        cropUserPresets.erase(std::remove_if(cropUserPresets.begin(),cropUserPresets.end(),[](const CropPreset&p){return p.name=="__script crop preset";}),cropUserPresets.end());cropUserPresets=saved;cropSaveUserPresets();
        Toolbar::select(Toolbar::CROP);syncTool();cropPreset=1;strcpy_s(cropWidth,"640 px");strcpy_s(cropHeight,"480 px");strcpy_s(cropResolution,"96");fitCropAspect();
        fprintf(stderr,"crop-user-presets: %s; saved size preset round-trips and restore preserves existing presets\n",loaded?"passed":"failed");return;
    }
    if (what == "crop-straighten") {
        Toolbar::select(Toolbar::CROP);syncTool();cropPreset=0;cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;cropAngle=0;
        straightenArmed=true;const bool ok=straightenCrop(ImVec2(doc.w*.1f,doc.h*.35f),ImVec2(doc.w*.9f,doc.h*.48f),true);
        straightenArmed=false;const bool fitted=cropR[0]>=0&&cropR[1]>=0&&cropR[2]<=doc.w&&cropR[3]<=doc.h&&cropAngle>0;
        fprintf(stderr,"crop-straighten: %s; one-shot level line sets the crop angle and fits frame inside canvas\n",ok&&fitted?"passed":"failed");return;
    }
    if (what == "crop-delete") {
        Doc cut=doc,keep=doc;const Rect r{10,10,std::max(11,doc.w-10),std::max(11,doc.h-10)};
        cropCanvas(cut,r,true);cropCanvas(keep,r,false);
        const bool ok=cut.w==r.x1-r.x0&&cut.h==r.y1-r.y0&&cut.cur().img.w==cut.w&&keep.cur().img.w==doc.w&&keep.cur().x==-r.x0;
        Toolbar::select(Toolbar::CROP);syncTool();cropR[0]=doc.w*.12f;cropR[1]=doc.h*.12f;cropR[2]=doc.w*.88f;cropR[3]=doc.h*.88f;cropDelete=true;
        fprintf(stderr,"crop-delete: %s; enabled cuts layer pixels, disabled keeps pixels beyond new canvas\n",ok?"passed":"failed");return;
    }
    if (what == "crop-corners") {
        Toolbar::select(Toolbar::CROP);syncTool();cropPreset=0;cropR[0]=doc.w*.16f;cropR[1]=doc.h*.16f;cropR[2]=doc.w*.84f;cropR[3]=doc.h*.84f;cropAngle=.18f;cropContentAware=true;
        applyCrop();const bool ok=std::any_of(doc.layers.begin(),doc.layers.end(),[](const Layer&l){return l.name=="Content-Aware Crop Corners";});
        fprintf(stderr,"crop-corners: %s; content-aware fill layer generated under retained crop layers\n",ok?"passed":"failed");return;
    }
    if (what == "crop-mode") {
        Toolbar::select(Toolbar::CROP);syncTool();cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;cropPending=true;cropAngle=.25f;
        cropClassicMode=false;syncCropView();const bool defaultMode=std::fabs(viewRotation+cropAngle*57.2957795f)<.01f;
        cropClassicMode=true;syncCropView();const bool classic=std::fabs(viewRotation)<.01f;
        cropClassicMode=false;syncCropView();const bool back=std::fabs(viewRotation+cropAngle*57.2957795f)<.01f;
        fprintf(stderr,"crop-mode: %s; default mode rotates view opposite crop frame, Classic Mode leaves view upright\n",defaultMode&&classic&&back?"passed":"failed");return;
    }
    if (what == "crop-mode-shortcut") {
        Toolbar::select(Toolbar::CROP);syncTool();cropPending=true;cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;cropAngle=.2f;cropClassicMode=false;syncCropView();
        toggleCropMode();const bool classic= cropClassicMode&&std::fabs(viewRotation)<.01f;toggleCropMode();const bool back=!cropClassicMode&&std::fabs(viewRotation+cropAngle*57.2957795f)<.01f;
        fprintf(stderr,"crop-mode-shortcut: %s; P toggles Classic Mode and restores default view rotation\n",classic&&back?"passed":"failed");return;
    }
    if (what == "crop-move-image") {
        Toolbar::select(Toolbar::CROP);syncTool();cropR[0]=doc.w*.18f;cropR[1]=doc.h*.18f;cropR[2]=doc.w*.82f;cropR[3]=doc.h*.82f;cropAngle=.17f;cropClassicMode=false;syncCropView();
        const ImVec2 center((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f),screen=toScreen(center.x,center.y);cropBegin(toCanvas(screen));const bool hit=cropDragPart==8;beginCropImageMove(screen);cropImageMoveTo(ImVec2(screen.x+24,screen.y+9));
        const ImVec2 after=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);const bool fixed=std::hypot(after.x-screen.x,after.y-screen.y)<.01f;
        fprintf(stderr,"crop-move-image: %s; default-mode drag shifts image under a screen-fixed frame\n",hit&&fixed&&cropMoveImage?"passed":"failed");return;
    }
    if (what == "crop-nudge") {
        Toolbar::select(Toolbar::CROP);syncTool();cropPending=true;cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;cropAngle=.13f;cropAutoCenter=true;cropPreset=0;
        cropClassicMode=false;syncCropView();const ImVec2 a=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);nudgeCrop(1,0,false);
        const ImVec2 b=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);const bool defaultFixed=std::hypot(a.x-b.x,a.y-b.y)<.01f;
        cropClassicMode=true;syncCropView();const ImVec2 c=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);nudgeCrop(1,0,false);
        const ImVec2 d=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);const bool classicMoves=std::hypot(c.x-d.x,c.y-d.y)>.9f;
        cropAngle=0;cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;cropClassicMode=false;cropAutoCenter=true;syncCropView();
        const float cy=(cropR[1]+cropR[3])*.5f,initialWidth=cropR[2]-cropR[0];const ImVec2 edge=toScreen(cropR[2],cy),midBefore=toScreen((cropR[0]+cropR[2])*.5f,cy);
        cropBegin(toCanvas(edge));const bool hitRight=cropDragPart==3;cropDragTo(toCanvas(ImVec2(edge.x+10,edge.y)));
        const ImVec2 midAfter=toScreen((cropR[0]+cropR[2])*.5f,cy);const bool autoResize=hitRight&&std::fabs((cropR[2]-cropR[0])-initialWidth-20/std::max(.01f,zoom))<.1f&&std::hypot(midAfter.x-midBefore.x,midAfter.y-midBefore.y)<.1f;
        cropR[0]=doc.w*.2f;cropR[1]=doc.h*.2f;cropR[2]=doc.w*.8f;cropR[3]=doc.h*.8f;const float panCy=(cropR[1]+cropR[3])*.5f;const ImVec2 panHandle=toScreen(cropR[2],panCy),panCenter=toScreen((cropR[0]+cropR[2])*.5f,panCy);cropBegin(toCanvas(panHandle));const ImVec2 panDelta(37,-19),panOrigin=origin;origin=ImVec2(origin.x+panDelta.x,origin.y+panDelta.y);cropCompensatePan(panDelta);const ImVec2 panCenterAfter=toScreen((cropR[0]+cropR[2])*.5f,(cropR[1]+cropR[3])*.5f);const bool panAnchored=cropDragPart==3&&std::hypot(panCenterAfter.x-panCenter.x,panCenterAfter.y-panCenter.y)<.1f;const float panWidth=cropR[2]-cropR[0];const ImVec2 resumedPointer(panHandle.x+10,panHandle.y);cropDragTo(toCanvas(resumedPointer));const float resumedWidth=cropR[2]-cropR[0];const bool panResumed=std::fabs(resumedWidth-(panWidth+20/std::max(.01f,zoom)))<.2f;origin=panOrigin;cropDragPart=-1;
        const float ratioBefore=(cropR[2]-cropR[0])/(cropR[3]-cropR[1]);const ImVec2 ratioEdge=toScreen(cropR[2],(cropR[1]+cropR[3])*.5f);
        cropBegin(toCanvas(ratioEdge));cropDragTo(toCanvas(ImVec2(ratioEdge.x+10,ratioEdge.y)),true);
        const bool shiftRatio=std::fabs((cropR[2]-cropR[0])/(cropR[3]-cropR[1])-ratioBefore)<.01f;
        const float oldW=cropR[2]-cropR[0],oldH=cropR[3]-cropR[1];swapCropOrientation();
        const bool swapped=std::fabs((cropR[2]-cropR[0])-oldH)<.01f&&std::fabs((cropR[3]-cropR[1])-oldW)<.01f;
        fprintf(stderr,"crop-nudge: %s; fixed/classic/resize/pan-anchor/pan-resume/ratio/swap %d/%d/%d/%d/%d/%d/%d\n",defaultFixed&&classicMoves&&autoResize&&panAnchored&&panResumed&&shiftRatio&&swapped?"passed":"failed",defaultFixed,classicMoves,autoResize,panAnchored,panResumed,shiftRatio,swapped);return;
    }
    if (what == "type-hidden-groups") {
        auto variants=[](){Doc d=newDoc(256,256,false);Text t;t.str="Visible text";t.x=40;t.y=96;t.size=48;Fill f;f.kind=FILL_SOLID;for(int i=0;i<3;++i)addTextLayer(d,t,f);return d;};
        Doc siblings=variants();const ImVec2 point((siblings.layers[1].text.box[0]+siblings.layers[1].text.box[2])*.5f,(siblings.layers[1].text.box[1]+siblings.layers[1].text.box[3])*.5f);
        bool hiddenSiblings=groupLayers(siblings,{2,3});if(hiddenSiblings){siblings.groups.back().visible=false;hiddenSiblings=typeHitLayer(siblings,point)==1;}
        Doc nested=variants();nested.layers[2].visible=false;bool hiddenNested=groupLayers(nested,{3});if(hiddenNested){const int inner=nested.groups.back().id;LayerGroup outer;outer.id=inner+1;outer.stackIndex=3;outer.name="Outer";outer.visible=false;nested.groups.back().parentId=outer.id;nested.groups.push_back(outer);hiddenNested=typeHitLayer(nested,point)==1;}
        Doc own=variants();own.layers[2].visible=false;own.layers[3].visible=false;const bool hiddenAbove=typeHitLayer(own,point)==1;own.layers[1].visible=false;own.layers[2].visible=false;own.layers[3].visible=true;const bool hiddenBelow=typeHitLayer(own,point)==3;
        doc=std::move(siblings);doc.active=0;typeLayer=-1;typeMaskEditing=false;Toolbar::select(Toolbar::TYPE_H);syncTool();typeBegin(point);
        const bool editShown=typeLayer==1&&doc.active==1;
        typeCancel();Toolbar::select(Toolbar::MOVE);syncTool();doc.active=0;
        const bool moveEditShown=moveEditTextAt(point)&&typeLayer==1&&Toolbar::current()==Toolbar::TYPE_H&&typeReturnToolbar==Toolbar::MOVE;
        fprintf(stderr,"type-hidden-groups: %s; hidden sibling/nested groups, hidden variants above/below, Type/Move edit shown layer=%d/%d/%d/%d/%d/%d\n",hiddenSiblings&&hiddenNested&&hiddenAbove&&hiddenBelow&&editShown&&moveEditShown?"passed":"failed",hiddenSiblings,hiddenNested,hiddenAbove,hiddenBelow,editShown,moveEditShown);return;
    }
    if (what == "type-vertical-arrows") {
        doc=newDoc(480,320,false);hist=History();Text vertical;vertical.str="HOHOHO\nHOHO";vertical.x=90;vertical.y=52;vertical.size=42;vertical.vertical=true;
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,vertical,fill);doc.active=0;
        ImGuiInputTextState state;const std::string text="ABCD\nxyz";for(char c:text)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)text.size();state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=1;
        const bool forward=typeMoveVerticalCaret(&state,text,1,1,1,1,false,2,false)&&state.GetCursorPos()==6;
        const bool back=typeMoveVerticalCaret(&state,text,6,6,6,6,false,-2,false)&&state.GetCursorPos()==1;
        Toolbar::select(Toolbar::TYPE_V);syncTool();typeLayer=0;typeMaskEditing=false;snprintf(typeBuf,sizeof(typeBuf),"%s",vertical.str.c_str());typeFocus=true;fitNext=true;
        fprintf(stderr,"type-vertical-arrows: %s; column moves preserve the inline character offset and return to the prior column\n",forward&&back?"passed":"failed");return;
    }
    if (what == "type-line-navigation") {
        doc=newDoc(480,320,false);hist=History();Text sample;sample.str="Home and End\nfollow the current row";sample.x=44;sample.y=116;sample.size=34;sample.boxWidth=340;sample.boxHeight=100;
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,sample,fill);doc.active=0;
        const std::string value="alpha\nbeta\n";ImGuiInputTextState state;for(char c:value)state.TextW.push_back((ImWchar)c);state.CurLenW=(int)value.size();state.Stb.cursor=state.Stb.select_start=state.Stb.select_end=8;
        const bool home=typeMoveEditorLineEdge(&state,value,false,8,8,false,false)&&state.GetCursorPos()==6;
        const bool end=typeMoveEditorLineEdge(&state,value,true,8,8,false,false)&&state.GetCursorPos()==10;
        Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=0;typeMaskEditing=false;snprintf(typeBuf,sizeof(typeBuf),"%s",sample.str.c_str());typeFocus=true;fitNext=true;
        fprintf(stderr,"type-line-navigation: %s; Home/End select the current row's start and end\n",home&&end?"passed":"failed");return;
    }
    if (what == "type-metric-shortcuts") {
        doc=newDoc(480,320,false);hist=History();Text sample;sample.str="Leading and baseline";sample.x=44;sample.y=116;sample.size=40;
        TextStyle selected;selected.font=sample.font;selected.size=sample.size;selected.leading=sample.leading;sample.runs={{sample.str.size(),selected}};
        Text updated=sample;const bool leading=typeStepCharacterMetric(updated,0,updated.str.size(),0,2,doc.dpi,updated.scaleY);
        const bool baseline=typeStepCharacterMetric(updated,0,updated.str.size(),1,2,doc.dpi,updated.scaleY);
        const bool tracking=typeStepCharacterMetric(updated,0,updated.str.size(),2,20,doc.dpi,updated.scaleY);
        Text styled=updated;const bool styleOn=typeToggleCharacterStyle(styled,0,styled.str.size(),0);
        const bool styleOff=styleOn&&typeToggleCharacterStyle(styled,0,styled.str.size(),0)&&!styled.characterStyles().front().bold;
        const bool capsOn=styleOff&&typeToggleCharacterStyle(styled,0,styled.str.size(),4)&&styled.characterStyles().front().caps==TextCaps::All;
        const bool capsOff=capsOn&&typeToggleCharacterStyle(styled,0,styled.str.size(),4)&&styled.characterStyles().front().caps==TextCaps::Normal;
        Text pair;pair.str="AV";pair.size=40;const float pairBefore=textAdvance(pair);const bool kern=textAdjustKerningPair(pair,1,20)&&textAdvance(pair)>pairBefore;
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,updated,fill);doc.active=0;
        Text capsPreview;capsPreview.str="All Caps: Stra\xc3\x9f" "e";capsPreview.x=44;capsPreview.y=180;capsPreview.size=30;TextStyle capsStyle;capsStyle.size=30;capsStyle.caps=TextCaps::All;capsPreview.runs={{capsPreview.str.size(),capsStyle}};addTextLayer(doc,capsPreview,fill);doc.active=1;
        Text smallCapsPreview;smallCapsPreview.str="Small Caps: Stra\xc3\x9f" "e";smallCapsPreview.x=44;smallCapsPreview.y=225;smallCapsPreview.size=30;TextStyle smallCapsStyle;smallCapsStyle.size=30;smallCapsStyle.caps=TextCaps::Small;smallCapsPreview.runs={{smallCapsPreview.str.size(),smallCapsStyle}};addTextLayer(doc,smallCapsPreview,fill);doc.active=2;
        typeLayer=0;typeSnapshotStart(false);const float tracked=doc.cur().text.runs.front().style.tracking;
        Text oneStep=doc.cur().text;const bool stepped=typeStepCharacterMetric(oneStep,0,oneStep.str.size(),2,20,doc.dpi,oneStep.scaleY);
        if(stepped){typeRememberDiscrete("Tracking");doc.cur().text=std::move(oneStep);renderFill(doc,doc.cur());changed();}
        const bool historyStep=stepped&&hist.undo.size()==1&&hist.undoNames.back()=="Tracking";
        if(historyStep)hist.doUndo(doc);
        const bool separateUndo=historyStep&&doc.cur().text.runs.front().style.tracking==tracked;
        if(separateUndo)hist.doRedo(doc);
        const bool separateRedo=separateUndo&&doc.cur().text.runs.front().style.tracking>tracked;
        typeEditHasSnapshot=typeEditCreated=typeEditUndoPushed=false;typeLayer=-1;
        Toolbar::select(Toolbar::TYPE_H);syncTool();fitNext=true;
        fprintf(stderr,"type-metric-shortcuts: %s; selected-run metrics/styles/caps, collapsed pair kerning and discrete undo\n",leading&&baseline&&tracking&&styleOn&&styleOff&&capsOn&&capsOff&&kern&&separateUndo&&separateRedo?"passed":"failed");return;
    }
    if (what == "type-shift-click") {
        doc=newDoc(480,320,false);hist=History();Text existing;existing.str="Existing words";existing.x=60;existing.y=140;existing.size=52;Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,existing,fill);
        const ImVec2 point((doc.cur().text.box[0]+doc.cur().text.box[2])*.5f,(doc.cur().text.box[1]+doc.cur().text.box[3])*.5f);Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=-1;typeBegin(point);
        const bool plainEdits=typeLayer==1&&doc.layers.size()==2&&doc.active==1;typeCommit();typeBegin(point,true);
        const bool shiftCreates=plainEdits&&typeLayer==2&&doc.layers.size()==3&&doc.active==2&&doc.cur().text.str=="Lorem Ipsum"&&typeSelectPlaceholder;fitNext=true;
        fprintf(stderr,"type-shift-click: %s; ordinary click edits existing text and Shift-click creates a selected placeholder layer\n",shiftCreates?"passed":"failed");return;
    }
    if (what == "type-move-double-click") {
        doc=newDoc(480,320,false);hist=History();Text existing;existing.str="Move edits this";existing.x=64;existing.y=140;existing.size=52;Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,existing,fill);
        const ImVec2 point((doc.cur().text.box[0]+doc.cur().text.box[2])*.5f,(doc.cur().text.box[1]+doc.cur().text.box[3])*.5f);Toolbar::select(Toolbar::MOVE);syncTool();
        const size_t historyBefore=hist.undo.size();const bool opened=moveEditTextAt(point)&&Toolbar::current()==Toolbar::TYPE_H&&typeLayer==1&&typeReturnToolbar==Toolbar::MOVE;
        doc.cur().text.str="Cancelled edit";renderFill(doc,doc.cur());typeCancel();const bool cancelled=Toolbar::current()==Toolbar::MOVE&&tool==T_MOVE&&doc.cur().text.str=="Move edits this"&&typeReturnToolbar<0&&hist.undo.size()==historyBefore;
        const bool reopened=moveEditTextAt(point)&&typeLayer==1;const size_t undoBefore=hist.undo.size();typeRemember("Edit Text");typeRemember("Edit Text");const bool coalesced=hist.undo.size()==undoBefore&&typeEditUndoPushed;doc.cur().text.str="Committed edit";renderFill(doc,doc.cur());typeCommit();const bool committed=Toolbar::current()==Toolbar::MOVE&&tool==T_MOVE&&doc.cur().text.str=="Committed edit"&&typeReturnToolbar<0&&coalesced&&hist.undo.size()==undoBefore+1&&hist.undoNames.back()=="Edit Text";
        hist.doUndo(doc);changed();const bool undoRestores=doc.cur().text.str=="Move edits this";hist.doRedo(doc);changed();const bool redoRestores=doc.cur().text.str=="Committed edit";
        const bool reopenedAgain=moveEditTextAt(point);Toolbar::select(Toolbar::ERASER);syncTool();const bool explicitTool=Toolbar::current()==Toolbar::ERASER&&tool==T_ERASER&&typeReturnToolbar<0&&typeLayer<0;
        Toolbar::select(Toolbar::MOVE);syncTool();const bool reopenedForCapture=moveEditTextAt(point);typeCommit();const bool restoredForCapture=Toolbar::current()==Toolbar::MOVE&&tool==T_MOVE;fitNext=true;
        fprintf(stderr,"type-move-double-click: %s; open/cancel/reopen/commit/undo/redo/reopen/tool-change/capture %d/%d/%d/%d/%d/%d/%d/%d/%d\n",opened&&cancelled&&reopened&&committed&&undoRestores&&redoRestores&&reopenedAgain&&explicitTool&&reopenedForCapture&&restoredForCapture?"passed":"failed",opened,cancelled,reopened,committed,undoRestores,redoRestores,reopenedAgain,explicitTool,restoredForCapture);return;
    }
    if (what == "type-word-navigation") {
        doc=newDoc(480,320,false);hist=History();Text text;text.str="ab, cd";text.x=64;text.y=140;text.size=52;Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,text,fill);
        const ImVec2 point((doc.cur().text.box[0]+doc.cur().text.box[2])*.5f,(doc.cur().text.box[1]+doc.cur().text.box[3])*.5f);Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=-1;typeBegin(point);typeWordNavigationScriptStage=1;fitNext=true;return;
    }
    if (what == "type-multiclick") {
        doc=newDoc(480,320,false);hist=History();Text text;text.str="ab, cd\nnext";text.x=64;text.y=140;text.size=52;Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,text,fill);
        const ImVec2 point((doc.cur().text.box[0]+doc.cur().text.box[2])*.5f,(doc.cur().text.box[1]+doc.cur().text.box[3])*.5f);Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=-1;typeBegin(point);typeClickOwner=-1;typeClickRun=0;typeMultiClickScriptStage=1;fitNext=true;return;
    }
    if (what == "type-smart-quotes") {
        Toolbar::select(Toolbar::TYPE_H);syncTool();typeSmartQuotesEnabled=true;typePlaceholderEnabled=true;typeSmartQuoteScriptStage=1;typeSmartQuoteScriptSelection=false;
        typeBegin(ImVec2(doc.w*.3f,doc.h*.45f));
        return;
    }
    if (what == "type-preferences") {
        const bool oldSmart=typeSmartQuotesEnabled,oldPlaceholder=typePlaceholderEnabled;const auto file=typePreferencesFile();const bool hadFile=std::filesystem::exists(file);std::string oldFile;
        if(hadFile){std::ifstream f(file,std::ios::binary);oldFile.assign(std::istreambuf_iterator<char>(f),{});}
        const auto oldRecent=recentTypeFonts;const int oldRecentCap=recentTypeFontCap;
        recentTypeFonts={"Font A","Font B","Font C"};recentTypeFontCap=2;trimRecentFonts(recentTypeFonts,(size_t)recentTypeFontCap);
        typeSmartQuotesEnabled=false;typePlaceholderEnabled=false;const bool wrote=saveTypePreferences();typeSmartQuotesEnabled=true;typePlaceholderEnabled=true;recentTypeFonts.clear();recentTypeFontCap=10;loadTypePreferences();
        const bool loadedSmart=typeSmartQuotesEnabled,loadedPlaceholder=typePlaceholderEnabled;const bool stored=wrote&&!loadedSmart&&!loadedPlaceholder;
        const bool recentStored=recentTypeFontCap==2&&recentTypeFonts==std::vector<std::string>({"Font A","Font B"});
        typeSmartQuotesEnabled=oldSmart;typePlaceholderEnabled=true;
        const Doc savedDoc=doc;const History savedHist=hist;const int savedLayer=typeLayer,savedToolbar=Toolbar::current();const bool savedMask=typeMaskEditing;const Text savedTypeMask=typeMask;const bool savedFocus=typeFocus,savedSelect=typeSelectPlaceholder;char savedBuf[sizeof(typeBuf)];memcpy(savedBuf,typeBuf,sizeof(typeBuf));
        Toolbar::select(Toolbar::TYPE_H);syncTool();typePlaceholderEnabled=true;typeBegin(ImVec2(doc.w*.5f,doc.h*.5f));
        const bool placeholder=typeLayer>=0&&doc.layers[typeLayer].text.str=="Lorem Ipsum"&&typeSelectPlaceholder;
        doc=savedDoc;hist=savedHist;typeLayer=savedLayer;typeMaskEditing=savedMask;typeMask=savedTypeMask;typeFocus=savedFocus;typeSelectPlaceholder=savedSelect;memcpy(typeBuf,savedBuf,sizeof(typeBuf));
        typePlaceholderEnabled=false;Toolbar::select(Toolbar::TYPE_H);syncTool();typeBegin(ImVec2(doc.w*.5f,doc.h*.5f));
        const bool placeholderOff=typeLayer>=0&&doc.layers[typeLayer].text.str.empty()&&!typeSelectPlaceholder;
        doc=savedDoc;hist=savedHist;typeLayer=savedLayer;typeMaskEditing=savedMask;typeMask=savedTypeMask;typeFocus=savedFocus;typeSelectPlaceholder=savedSelect;memcpy(typeBuf,savedBuf,sizeof(typeBuf));
        typePlaceholderEnabled=true;Toolbar::select(Toolbar::TYPE_H_MASK);syncTool();typeBegin(ImVec2(doc.w*.5f,doc.h*.5f));
        const bool horizontalMaskPlaceholder=typeMaskEditing&&typeMask.str=="Lorem Ipsum"&&typeSelectPlaceholder;
        doc=savedDoc;hist=savedHist;typeLayer=savedLayer;typeMaskEditing=savedMask;typeMask=savedTypeMask;typeFocus=savedFocus;typeSelectPlaceholder=savedSelect;memcpy(typeBuf,savedBuf,sizeof(typeBuf));
        typePlaceholderEnabled=true;Toolbar::select(Toolbar::TYPE_V_MASK);syncTool();typeBegin(ImVec2(doc.w*.5f,doc.h*.5f));
        const bool verticalMaskEmpty=typeMaskEditing&&typeMask.str.empty();
        doc=savedDoc;hist=savedHist;typeLayer=savedLayer;typeMaskEditing=savedMask;typeMask=savedTypeMask;typeFocus=savedFocus;typeSelectPlaceholder=savedSelect;memcpy(typeBuf,savedBuf,sizeof(typeBuf));
        Toolbar::select(savedToolbar);syncTool();
        if(hadFile){std::ofstream f(file,std::ios::binary|std::ios::trunc);f.write(oldFile.data(),(std::streamsize)oldFile.size());}else{std::error_code ec;std::filesystem::remove(file,ec);}
        typeSmartQuotesEnabled=oldSmart;typePlaceholderEnabled=oldPlaceholder;recentTypeFonts=oldRecent;recentTypeFontCap=oldRecentCap;
        const bool quotes=textSmartQuotesTyped(" ","\"you\"")=="\xe2\x80\x9cyou\xe2\x80\x9d"&&textSmartQuotesTyped("x","\"you\"")=="\xe2\x80\x9dyou\xe2\x80\x9d";
        ImGuiIO& io=ImGui::GetIO();std::vector<ImWchar> queued;for(int i=0;i<io.InputQueueCharacters.Size;++i)queued.push_back(io.InputQueueCharacters[i]);io.InputQueueCharacters.clear();
        ImGuiInputTextState fake;fake.TextW.push_back('x');fake.CurLenW=1;fake.Stb.cursor=1;io.InputQueueCharacters.push_back('"');typeSmartQuotesEnabled=true;typeMapQueuedSmartQuotes(&fake);
        const bool queueMapped=io.InputQueueCharacters.Size==1&&io.InputQueueCharacters[0]==0x201d;io.InputQueueCharacters.clear();for(ImWchar ch:queued)io.InputQueueCharacters.push_back(ch);
        io.InputQueueCharacters.clear();io.InputQueueCharacters.push_back('"');typeSmartQuotesEnabled=false;typeMapQueuedSmartQuotes(&fake);
        const bool disabledKeepsStraight=io.InputQueueCharacters.Size==1&&io.InputQueueCharacters[0]=='"';io.InputQueueCharacters.clear();for(ImWchar ch:queued)io.InputQueueCharacters.push_back(ch);
        typeSmartQuotesEnabled=oldSmart;typePlaceholderEnabled=oldPlaceholder;
        typePreferencesScriptOpen=true;
        fprintf(stderr,"type-preferences: %s (stored=%d recent=%d placeholder=%d off=%d horizontal-mask-placeholder=%d vertical-mask-empty=%d quotes-on=%d quotes-off=%d); preference and Type creation rules\n",stored&&recentStored&&placeholder&&placeholderOff&&horizontalMaskPlaceholder&&verticalMaskEmpty&&quotes&&queueMapped&&disabledKeepsStraight?"passed":"failed",stored?1:0,recentStored?1:0,placeholder?1:0,placeholderOff?1:0,horizontalMaskPlaceholder?1:0,verticalMaskEmpty?1:0,quotes&&queueMapped?1:0,disabledKeepsStraight?1:0);return;
    }
    if (what == "text-empty") {
        Toolbar::select(Toolbar::TYPE_H); syncTool();
        const size_t before = doc.layers.size();
        Text empty; empty.x = doc.w * 0.5f; empty.y = doc.h * 0.5f;
        Fill fill; fill.kind = FILL_SOLID;
        addTextLayer(doc, empty, fill); typeLayer = doc.active; typeBuf[0] = 0;
        typeCommit();
        const bool ok = doc.layers.size() == before;
        fprintf(stderr, "text-empty: %s; empty new text layer removed on commit\n", ok ? "passed" : "failed");
        return;
    }
    if (what == "empty") return;                             // the empty start, as it is
    noDoc = false;                                           // the scripts work on the blank document
    if (what == "rotate-view") {
        Toolbar::select(Toolbar::ROTATE_VIEW); syncTool();
        viewRotation = 30.0f;
        fprintf(stderr, "rotate-view: camera %.1f degrees; document pixels unchanged\n", viewRotation);
        return;
    }
    if (what == "rotate-marquee") {
        Toolbar::select(Toolbar::RECT_MARQUEE); syncTool(); viewRotation = 30.0f;
        dragging = true; dragStart = ImVec2(doc.w * 0.2f, doc.h * 0.2f); dragNow = ImVec2(doc.w * 0.8f, doc.h * 0.75f);
        fprintf(stderr, "rotate-marquee: rotated rectangle preview\n"); return;
    }
    if (what == "rotate-crop") {
        Toolbar::select(Toolbar::CROP); syncTool(); viewRotation = 30.0f; cropPending = true;
        cropR[0] = doc.w * 0.18f; cropR[1] = doc.h * 0.16f; cropR[2] = doc.w * 0.82f; cropR[3] = doc.h * 0.84f;
        fprintf(stderr, "rotate-crop: rotated crop outline and thirds\n"); return;
    }
    if (what == "magnetic-lasso") {
        Toolbar::select(Toolbar::MAGNETIC_LASSO); syncTool();
        magneticWidth = 18; magneticContrast = 6;
        Image source; composite(doc, source, {0, 0, doc.w, doc.h}, false);
        magneticTracer = std::make_unique<Magnetic::Tracer>(source);
        const Magnetic::Point firstRaw{doc.w * 0.5, doc.h * 0.12};
        magneticPts = {magneticTracer->snap(firstRaw, magneticSettings())}; magneticAnchors = {0}; magneticTrail = {firstRaw}; magneticSelMode = SEL_NEW;
        for (int i = 0; i < 16; ++i) {
            const float a = -1.57079632679f + i * 6.28318530718f / 16;
            if (i) {
                const Magnetic::Point p{doc.w * (0.5f + 0.16f * std::cos(a)), doc.h * (0.44f + 0.32f * std::sin(a))};
                magneticUpdate(p, false, true);
                if (i % 4 == 0) magneticClick(p, false);
            }
        }
        const size_t points = magneticPts.size();
        magneticFinish();
        fprintf(stderr, "magnetic-lasso: 16 live-wire anchors traced in %zu path points\n", points);
        return;
    }
    if (what == "perspective-crop-preview" || what == "perspective-crop" || what == "perspective-crop-preserve") {
        Toolbar::select(Toolbar::PERSPECTIVE_CROP);
        syncTool();
        if (what == "perspective-crop-preserve") {
            Fill gradient;
            gradient.kind = FILL_GRADIENT;
            addFillLayer(doc, gradient);
            Layer pixels = emptyLayer(doc, "Masked Pixels");
            pixels.img = Image(doc.w, doc.h);
            for (int y = doc.h / 4; y < doc.h * 3 / 4; ++y)
                for (int x = doc.w / 4; x < doc.w * 3 / 4; ++x) {
                    uint8_t *p = pixels.img.at(x, y);
                    p[0] = 230;
                    p[1] = 80;
                    p[2] = 40;
                    p[3] = 210;
                }
            pixels.hasMask = true;
            pixels.mask.assign((size_t)doc.w * doc.h, 255);
            for (int y = 0; y < doc.h; ++y)
                for (int x = 0; x < doc.w / 8; ++x)
                    pixels.mask[(size_t)y * doc.w + x] = 0;
            addLayer(doc, std::move(pixels));
            Shape shape;
            shape.kind = SHAPE_RECT;
            shape.x0 = doc.w * 0.15f;
            shape.y0 = doc.h * 0.2f;
            shape.x1 = doc.w * 0.3f;
            shape.y1 = doc.h * 0.4f;
            Fill solid;
            solid.kind = FILL_SOLID;
            addShapeLayer(doc, shape, solid);
            Text words;
            words.str = "Port";
            words.size = 64;
            words.x = doc.w * 0.65f;
            words.y = doc.h * 0.3f;
            addTextLayer(doc, words, solid);
            addAdjustmentLayer(doc, ADJ_BRIGHTNESS_CONTRAST);
            doc.samplers.push_back({doc.w * 0.25f, doc.h * 0.25f, "Sample"});
            doc.notes.push_back({doc.w * 0.7f, doc.h * 0.7f, "Keep me"});
            doc.counts.push_back({doc.w * 0.75f, doc.h * 0.4f, "1"});
            doc.hasRuler = true;
            doc.ruler[0] = doc.w * 0.2f;
            doc.ruler[1] = doc.h * 0.2f;
            doc.ruler[2] = doc.w * 0.8f;
            doc.ruler[3] = doc.h * 0.8f;
            Subpath path;
            Knot knot;
            knot.ax = knot.ix = knot.ox = doc.w * 0.4f;
            knot.ay = knot.iy = knot.oy = doc.h * 0.5f;
            path.knots.push_back(knot);
            doc.workPath.push_back(path);
            doc.historyState = std::make_shared<const std::vector<Layer>>(doc.layers);
        }
        const float q[8] = {doc.w * 0.22f, doc.h * 0.18f, doc.w * 0.78f, doc.h * 0.12f,
                            doc.w * 0.84f, doc.h * 0.82f, doc.w * 0.16f, doc.h * 0.76f};
        pcropSet(q);
        if (what == "perspective-crop") {
            applyPerspectiveCrop();
            fprintf(stderr, "perspective-crop: warped the visible image to %d x %d pixels\n", doc.w, doc.h);
        } else if (what == "perspective-crop-preserve") {
            applyPerspectiveCrop();
            const bool ok = doc.layers.size() == 6 && doc.layers[2].hasMask &&
                            doc.layers[2].mask.size() == (size_t)doc.w * doc.h && doc.layers[1].isFill() &&
                            doc.layers[5].isAdjust() && !doc.layers[3].isShape() && !doc.layers[4].isText() &&
                            doc.samplers.size() == 1 && doc.notes.size() == 1 && doc.counts.size() == 1 &&
                            doc.hasRuler && !doc.workPath.empty() && !!doc.historyState;
            fprintf(stderr,
                    "perspective-crop-preserve: %s; %zu layers, %zu samplers, %zu notes, %zu counts, ruler=%d, "
                    "work-path=%zu, history=%d\n",
                    ok ? "all preservation checks passed" : "preservation check failed", doc.layers.size(),
                    doc.samplers.size(), doc.notes.size(), doc.counts.size(), doc.hasRuler, doc.workPath.size(),
                    !!doc.historyState);
        } else {
            fprintf(stderr, "perspective-crop-preview: four-corner homography preview\n");
        }
        return;
    }
    if (what == "slices-preview" || what == "slices-export") {
        Toolbar::select(Toolbar::SLICE_SELECT); syncTool();
        doc.slices.clear(); sliceActive = -1;
        createSlice({doc.w / 10, doc.h / 8, doc.w * 48 / 100, doc.h * 7 / 8});
        createSlice({doc.w * 52 / 100, doc.h / 5, doc.w * 9 / 10, doc.h * 4 / 5});
        if (what == "slices-export") doc.slices[0].name = "bad:/unsafe. ";
        sliceActive = 0;
        if (what == "slices-export") {
            size_t written = 0; std::string err;
            const auto folder = scriptPath("slice-export-test");
            if (!writeSlices(folder, written, err)) fprintf(stderr, "slices-export: %s\n", err.c_str());
            else fprintf(stderr, "slices-export: wrote %zu PNG files; safe name=%s\n", written, safeSliceFilename(doc.slices[0].name).c_str());
            const std::string project = scriptPath("slices-roundtrip.chimera").u8string();
            Doc roundTrip;
            if (!saveProject(doc, project, err) || !openDoc(project, roundTrip, err)) fprintf(stderr, "slices-roundtrip: %s\n", err.c_str());
            else fprintf(stderr, "slices-roundtrip: restored %zu user slices\n", roundTrip.slices.size());
        } else {
            fprintf(stderr, "slices-preview: %zu user slices and generated auto slices\n", doc.slices.size());
        }
        return;
    }
    if (what == "frame") {
        Toolbar::select(Toolbar::FRAME);
        syncTool();
        frameEllipse = false;
        createFrame({doc.w / 12, doc.h / 8, doc.w * 5 / 12, doc.h * 7 / 8});
        Image sample(320, 180);
        for (int y = 0; y < sample.h; ++y)
            for (int x = 0; x < sample.w; ++x) {
                uint8_t *p = sample.at(x, y);
                p[0] = (uint8_t)(40 + 180 * x / sample.w);
                p[1] = (uint8_t)(70 + 150 * y / sample.h);
                p[2] = (uint8_t)(180 - 100 * x / sample.w);
                p[3] = 255;
            }
        addPlacedImage(std::move(sample), "Frame Photo");
        frameEllipse = true;
        createFrame({doc.w * 7 / 12, doc.h / 5, doc.w * 11 / 12, doc.h * 4 / 5});
        std::string err;
        const std::string project = scriptPath("frame-roundtrip.chimera").u8string();
        Doc roundTrip;
        const bool saved = saveProject(doc, project, err) && openDoc(project, roundTrip, err);
        const bool ok = saved && roundTrip.layers.size() == 4 && roundTrip.layers[1].frame &&
                        roundTrip.layers[2].clip && roundTrip.layers[3].frame;
        fprintf(stderr, "frame: %s; two editable frames, clipped image, project round trip=%d\n",
                ok ? "passed" : err.c_str(), saved);
        return;
    }
    if (what == "custom-shape") {
        Toolbar::select(Toolbar::CUSTOM_SHAPE); syncTool();
        customShapePick = 0;
        remember("New Heart");
        Shape sh = customShapeFromDrag(ImVec2(doc.w * 0.18f, doc.h * 0.18f), ImVec2(doc.w * 0.48f, doc.h * 0.62f));
        Fill fill; fill.kind = FILL_SOLID; fill.colour[0] = 0.85f; fill.colour[1] = 0.18f; fill.colour[2] = 0.26f;
        addShapeLayer(doc, sh, fill);
        doc.cur().name = "Heart";
        customShapePick = 1;
        sh = customShapeFromDrag(ImVec2(doc.w * 0.56f, doc.h * 0.25f), ImVec2(doc.w * 0.86f, doc.h * 0.55f));
        addShapeLayer(doc, sh, fill);
        doc.cur().name = "Arrow Right";
        changed();
        const bool ok = doc.layers.size() == 3 && doc.layers[1].shape.kind == SHAPE_PATH && doc.layers[1].shape.path[0].knots.size() == 6 && doc.layers[1].shape.path[0].knots.front().smooth && doc.layers[2].shape.path[0].knots.size() == CUSTOM_SHAPE_POINTS[1].size();
        fprintf(stderr, "custom-shape: %s; cubic PhotoCraft heart and editable arrow SHAPE_PATH created\n", ok ? "passed" : "preset path check failed");
        return;
    }
    if (what == "text-move") {
        Toolbar::select(Toolbar::TYPE_H); syncTool();
        Text t; t.str = "Grab offset stays put while editing a paragraph box"; t.size = 64; t.x = doc.w * 0.2f; t.y = doc.h * 0.4f; t.boxWidth = 280; t.boxHeight = 180;
        Fill f; f.kind = FILL_SOLID; f.colour[0] = 1; f.colour[1] = 1; f.colour[2] = 1;
        addTextLayer(doc, t, f); typeLayer = doc.active; snprintf(typeBuf, sizeof typeBuf, "%s", t.str.c_str());
        typeResizeHandle = 2; typeBoxX0 = t.x; typeBoxY0 = t.y; typeBoxW0 = t.boxWidth; typeBoxH0 = t.boxHeight;
        typeBoxAnchorX0 = t.x; typeBoxAnchorY0 = t.y; typeBoxScaleX0 = t.scaleX; typeBoxScaleY0 = t.scaleY; typeBoxRotation0 = t.rotation;
        typeResizeByPointer(ImVec2(t.x + t.boxWidth + 40, t.y + t.boxHeight + 24), false); typeResizeHandle = -1;
        typeRotating = true; typeRotatePivotX = doc.cur().text.x; typeRotatePivotY = doc.cur().text.y;
        typeRotateStartAngle = 0; typeRotation0 = doc.cur().text.rotation;
        typeRotateByPointer(ImVec2(typeRotatePivotX + 100, typeRotatePivotY + 36.397f), false); typeRotating = false;
        const float x = doc.cur().text.x, y = doc.cur().text.y;
        remember("Move Text"); typeMoving = true; typeMoveX = x; typeMoveY = y;
        typeMoveByPointer(ImVec2(0, 0), ImVec2(72, 48)); typeMoving = false;
        const bool paragraphOk = doc.cur().isText() && std::fabs(doc.cur().text.x - x - 72) < 0.01f && std::fabs(doc.cur().text.y - y - 48) < 0.01f && std::fabs(doc.cur().text.boxWidth - 320) < 0.01f && std::fabs(doc.cur().text.boxHeight - 204) < 0.01f && std::fabs(doc.cur().text.rotation - 20) < 0.1f && doc.cur().text.box[3] > doc.cur().text.box[1] + 64;
        Text point; point.str = "Point text transform handles"; point.size = 40; point.x = doc.w * 0.55f; point.y = doc.h * 0.72f;
        addTextLayer(doc, point, f); typeLayer = doc.active; snprintf(typeBuf, sizeof typeBuf, "%s", point.str.c_str());
        const float pointW = doc.cur().text.box[2] - doc.cur().text.box[0], pointH = doc.cur().text.box[3] - doc.cur().text.box[1];
        const float pointLeft = doc.cur().text.box[0], pointTop = doc.cur().text.box[1];
        typeScaleHandle = 2; std::copy(doc.cur().text.box, doc.cur().text.box + 4, typePointBox0);
        typeScaleX0 = doc.cur().text.scaleX; typeScaleY0 = doc.cur().text.scaleY;
        typePointAnchorX0 = doc.cur().text.x; typePointAnchorY0 = doc.cur().text.y; typePointRotation0 = doc.cur().text.rotation;
        typeScaleByPointer(ImVec2(typePointBox0[2] + pointW * 0.25f, typePointBox0[3] + pointH * 0.25f), false); typeScaleHandle = -1;
        const bool pointOk = doc.cur().text.scaleX > 1.2f && doc.cur().text.scaleY > 1.2f && std::fabs(doc.cur().text.box[0] - pointLeft) < 1.5f && std::fabs(doc.cur().text.box[1] - pointTop) < 1.5f;
        typeNext = Text(); typeNext.size = 34;
        Subpath curve; Knot p0; p0.ax = doc.w * 0.15f; p0.ay = doc.h * 0.82f; p0.ox = doc.w * 0.35f; p0.oy = doc.h * 0.67f;
        Knot p1; p1.ax = doc.w * 0.85f; p1.ay = doc.h * 0.82f; p1.ix = doc.w * 0.65f; p1.iy = doc.h * 0.67f;
        curve.knots = {p0, p1}; doc.workPath = {curve};
        typeBeginOnPath(ImVec2(p0.ax, p0.ay), typePathNearest(doc.workPath, p0.ax, p0.ay));
        doc.cur().text.str = "Text follows a curved path"; snprintf(typeBuf, sizeof typeBuf, "%s", doc.cur().text.str.c_str()); renderFill(doc, doc.cur()); changed();
        std::vector<float> pathCoverage; float pathBox[4]; const bool pathRendered = textCoverage(doc.cur().text, doc.w, doc.h, pathCoverage, pathBox, doc.dpi);
        const bool pathOk = pathRendered && std::count_if(pathCoverage.begin(), pathCoverage.end(), [](float a) { return a > 0.1f; }) > 100;
        const float startBefore = doc.cur().text.pathStart;
        const bool markerPicked = typePathMarkerPress(ImVec2(p0.ax, p0.ay));
        if (markerPicked) typePathDragTo(ImVec2(p0.ax + (p1.ax - p0.ax) * 0.2f, p0.ay + (p1.ay - p0.ay) * 0.2f));
        typePathDragPart = -1; typePathDragLayer = -1;
        const bool startMarkerOk = markerPicked && doc.cur().text.pathStart > startBefore + 20;
        doc.cur().text.pathEnd = doc.cur().text.pathStart + 100;
        ImVec2 endMarker; typePathPointAt(doc.cur().text, doc.cur().text.pathEnd, endMarker, doc.cur().text.pathFlip);
        const bool endPicked = typePathMarkerPress(endMarker);
        if (endPicked) { ImVec2 target; typePathPointAt(doc.cur().text, doc.cur().text.pathEnd + 35, target, doc.cur().text.pathFlip); typePathDragTo(target); }
        typePathDragPart = -1; typePathDragLayer = -1;
        const bool endMarkerOk = endPicked && doc.cur().text.pathEnd > doc.cur().text.pathStart + 120;
        ImVec2 bodyStart; typePathPointAt(doc.cur().text, doc.cur().text.pathStart + 40, bodyStart, doc.cur().text.pathFlip);
        const float bodyBefore = doc.cur().text.pathStart;
        const bool bodyPicked = typePathMarkerPress(bodyStart);
        if (bodyPicked) { ImVec2 target; typePathPointAt(doc.cur().text, doc.cur().text.pathStart + 60, target, doc.cur().text.pathFlip); typePathDragTo(target); }
        typePathDragPart = -1; typePathDragLayer = -1;
        const bool bodyMoveOk = bodyPicked && std::fabs(doc.cur().text.pathStart - bodyBefore - 20) < 12;
        const float pathLength = typePathLength(doc.cur().text.path);
        doc.cur().text.align = 0; doc.cur().text.pathStart = pathLength - 80; doc.cur().text.pathEnd = pathLength - 20;
        ImVec2 openBody; typePathPointAt(doc.cur().text, pathLength - 40, openBody, doc.cur().text.pathFlip);
        const bool openBodyPicked = typePathMarkerPress(openBody);
        if (openBodyPicked) { ImVec2 endpoint; typePathPointAt(doc.cur().text, pathLength, endpoint, doc.cur().text.pathFlip); typePathDragTo(endpoint); }
        typePathDragPart = -1; typePathDragLayer = -1;
        const bool endpointClampOk = openBodyPicked && std::fabs(doc.cur().text.pathEnd - pathLength) < 2 && std::fabs((doc.cur().text.pathEnd - doc.cur().text.pathStart) - 40) < 3;
        doc.cur().text.path[0].closed = true; const float closedLength = typePathLength(doc.cur().text.path);
        doc.cur().text.pathStart = closedLength * 0.3f; doc.cur().text.pathEnd = doc.cur().text.pathStart + closedLength;
        ImVec2 coincident; typePathPointAt(doc.cur().text, doc.cur().text.pathStart, coincident, doc.cur().text.pathFlip);
        const float coincidentStart = doc.cur().text.pathStart, coincidentEnd = doc.cur().text.pathEnd;
        const bool coincidentPicked = typePathMarkerPress(coincident);
        if (coincidentPicked) { ImVec2 forward; typePathPointAt(doc.cur().text, coincidentStart + 40, forward, doc.cur().text.pathFlip); typePathDragTo(forward); }
        typePathDragPart = -1; typePathDragLayer = -1; doc.cur().text.path[0].closed = false;
        const bool coincidentOk = coincidentPicked && std::fabs(doc.cur().text.pathStart - coincidentStart - 40) < 2 && std::fabs(doc.cur().text.pathEnd - coincidentEnd - 40) < 2;
        doc.cur().text.path[0].closed = true; doc.cur().text.pathStart = closedLength * 0.3f; doc.cur().text.pathEnd = -1;
        const float lapStart = doc.cur().text.pathStart;
        ImVec2 ahead; typePathPointAt(doc.cur().text, lapStart + 4, ahead, doc.cur().text.pathFlip);
        const bool splitPicked = typePathMarkerPress(ahead);
        if (splitPicked) { ImVec2 target; typePathPointAt(doc.cur().text, lapStart + 34, target, doc.cur().text.pathFlip); typePathDragTo(target); }
        typePathDragPart = -1; typePathDragLayer = -1;
        const bool splitLapOk = splitPicked && std::fabs(doc.cur().text.pathStart - lapStart - 30) < 3 && std::fabs((doc.cur().text.pathEnd - doc.cur().text.pathStart) - (closedLength - 30)) < 3;
        doc.cur().text.pathStart = 20; doc.cur().text.pathEnd = -1; doc.cur().text.path[0].closed = false;
        Text savedPathText = doc.cur().text;
        Subpath straight; Knot sa; sa.ax=20;sa.ay=100;sa.ox=73;sa.oy=100; Knot sb;sb.ax=180;sb.ay=100;sb.ix=127;sb.iy=100; straight.knots={sa,sb};
        doc.cur().text.path={straight}; doc.cur().text.x=20; doc.cur().text.y=100; doc.cur().text.pathStart=20; doc.cur().text.pathEnd=100; doc.cur().text.pathFlip=false; doc.cur().text.scaleX=doc.cur().text.scaleY=1; doc.cur().text.rotation=0;
        Toolbar::select(Toolbar::PATH_SELECTION); syncTool();
        const bool flipPicked=typePathMarkerPress(ImVec2(40,90));
        if (flipPicked) typePathDragTo(ImVec2(40,110));
        const bool flipOk=flipPicked && doc.cur().text.pathFlip && std::fabs(doc.cur().text.pathStart-60)<2 && std::fabs(doc.cur().text.pathEnd-140)<2;
        typePathDragPart=-1; typePathDragLayer=-1; typePathDragTab=-1; typePathDragRemembered=false;
        doc.cur().text=savedPathText; renderFill(doc,doc.cur());
        Toolbar::select(Toolbar::TYPE_H); syncTool();
        const Text beforeCancel = doc.cur().text; const size_t historyBeforeCancel = hist.undo.size();
        ImVec2 cancelStart, cancelTarget; typePathPointAt(doc.cur().text, beforeCancel.pathStart, cancelStart, beforeCancel.pathFlip);
        const bool cancelPicked = typePathMarkerPress(cancelStart);
        if (cancelPicked) { typePathPointAt(doc.cur().text, beforeCancel.pathStart + 25, cancelTarget, beforeCancel.pathFlip); typePathDragTo(cancelTarget); typePathDragCancel(); }
        const bool cancelOk = cancelPicked && doc.cur().text.pathStart == beforeCancel.pathStart && doc.cur().text.pathEnd == beforeCancel.pathEnd
            && doc.cur().text.pathFlip == beforeCancel.pathFlip && hist.undo.size() == historyBeforeCancel;
        const bool markerOk = startMarkerOk && endMarkerOk && bodyMoveOk && endpointClampOk && coincidentOk && splitLapOk && flipOk && cancelOk;
        fprintf(stderr, "text-move: %s; paragraph %d, point scale %d, path text %d, markers start/end/body/endpoint/coincident/split/flip/cancel %d/%d/%d/%d/%d/%d/%d/%d\n", paragraphOk && pointOk && pathOk && markerOk ? "passed" : "type transform check failed", paragraphOk ? 1 : 0, pointOk ? 1 : 0, pathOk ? 1 : 0, startMarkerOk ? 1 : 0, endMarkerOk ? 1 : 0, bodyMoveOk ? 1 : 0, endpointClampOk ? 1 : 0, coincidentOk ? 1 : 0, splitLapOk ? 1 : 0, flipOk ? 1 : 0, cancelOk ? 1 : 0);
        return;
    }
    if (what == "artboard") {
        Toolbar::select(Toolbar::ARTBOARD); syncTool();
        createArtboard({doc.w / 8, doc.h / 8, doc.w / 2, doc.h * 3 / 4});
        artboardBackground = 1;
        createArtboard({doc.w * 5 / 8, doc.h / 5, doc.w * 7 / 8, doc.h * 4 / 5});
        const int secondBoardGroup = doc.artboards.back().groupId;
        Fill boardFill; boardFill.kind = FILL_SOLID; boardFill.colour[0] = 0.9f; boardFill.colour[1] = 0.15f; boardFill.colour[2] = 0.2f;
        Shape boardShape; boardShape.kind = SHAPE_RECT; boardShape.x0 = doc.w * 0.68f; boardShape.y0 = doc.h * 0.32f; boardShape.x1 = doc.w * 0.82f; boardShape.y1 = doc.h * 0.62f;
        addShapeLayer(doc, boardShape, boardFill);
        const int contentLayer = doc.active;
        groupLayers(doc, {contentLayer});
        std::string err;
        const std::string project = scriptPath("artboards-roundtrip.chimera").u8string();
        Doc roundTrip;
        const bool saved = saveProject(doc, project, err) && openDoc(project, roundTrip, err);
        Image boardImage; const bool rendered = saved && renderArtboard(roundTrip, roundTrip.artboards[0].groupId, boardImage);
        const uint8_t* boardPixel = rendered ? boardImage.at(boardImage.w / 2, boardImage.h / 2) : nullptr;
        const auto nested = std::find_if(roundTrip.groups.begin(), roundTrip.groups.end(), [&](const LayerGroup& g) { return g.parentId == secondBoardGroup; });
        Image nestedBoard; const bool nestedRendered = saved && renderArtboard(roundTrip, roundTrip.artboards[1].groupId, nestedBoard);
        const uint8_t* nestedPixel = nestedRendered ? nestedBoard.at(nestedBoard.w / 2, nestedBoard.h / 2) : nullptr;
        Doc movedBoard = roundTrip; const Rect oldBoardRect = movedBoard.artboards[1].rect;
        moveGroupContents(movedBoard, movedBoard.artboards[1].groupId, 3, 4);
        const bool movedAsAUnit = movedBoard.artboards[1].rect.x0 == oldBoardRect.x0 + 3 && movedBoard.artboards[1].rect.y0 == oldBoardRect.y0 + 4 && movedBoard.layers.back().x == 3 && movedBoard.layers.back().y == 4;
        const bool ok = saved && rendered && boardPixel && boardPixel[0] > 245 && boardPixel[1] > 245 && boardPixel[2] > 245 && roundTrip.artboards.size() == 2 && roundTrip.artboards[0].name == "Artboard 1" && roundTrip.artboards[1].background == 1 &&
                        nested != roundTrip.groups.end() && nestedRendered && nestedPixel && nestedPixel[0] > 180 && nestedPixel[1] < 100 && nestedPixel[2] < 100 && movedAsAUnit;
        fprintf(stderr, "artboard: %s; nested group contents, group movement, board render and project round trip=%d\n", ok ? "passed" : err.c_str(), saved);
        changed();
        return;
    }
    if (what == "layer-groups") {
        Toolbar::select(Toolbar::MOVE); syncTool();
        Fill first; first.kind = FILL_SOLID; first.colour[0] = 0.9f; first.colour[1] = 0.18f; first.colour[2] = 0.2f;
        Shape a; a.kind = SHAPE_RECT; a.x0 = doc.w * 0.2f; a.y0 = doc.h * 0.2f; a.x1 = doc.w * 0.65f; a.y1 = doc.h * 0.72f;
        addShapeLayer(doc, a, first);
        Fill second; second.kind = FILL_SOLID; second.colour[0] = 0.1f; second.colour[1] = 0.5f; second.colour[2] = 0.9f;
        Shape b; b.kind = SHAPE_ELLIPSE; b.x0 = doc.w * 0.42f; b.y0 = doc.h * 0.28f; b.x1 = doc.w * 0.78f; b.y1 = doc.h * 0.78f;
        addShapeLayer(doc, b, second);
        layerSelection = {1, 2};
        remember("Group Layers");
        groupLayers(doc, layerSelection);
        const int outerGroup = doc.cur().groupId;
        remember("Group Layers");
        groupLayers(doc, layerSelection);
        activeGroupId = doc.cur().groupId;
        LayerGroup& group = doc.groups.back(); group.opacity = 0.72f; group.hasMask = true; group.mask.assign((size_t)doc.w * doc.h, 255); group.expanded = true;
        for (LayerGroup& g : doc.groups) if (g.id == outerGroup) { g.locked = true; g.expanded = true; }
        for (int y = 0; y < doc.h; ++y) for (int x = 0; x < doc.w / 3; ++x) group.mask[(size_t)y * doc.w + x] = 0;
        Image before; composite(doc, before, {0, 0, doc.w, doc.h}, false);
        std::string err; const std::string project = scriptPath("groups-roundtrip.chimera").u8string(); Doc roundTrip;
        const bool saved = saveProject(doc, project, err) && openDoc(project, roundTrip, err);
        Image after; if (saved) composite(roundTrip, after, {0, 0, roundTrip.w, roundTrip.h}, false);
        const bool same = saved && roundTrip.groups.size() == 2 && roundTrip.groups[0].locked && groupLocked(roundTrip, roundTrip.groups[1].id) && roundTrip.layers[1].groupId == roundTrip.layers[2].groupId && before.px == after.px;
        fprintf(stderr, "layer-groups: %s; nested groups, opacity, mask, inherited lock and project/composite round trip=%d\n", same ? "passed" : err.c_str(), saved);
        changed();
        return;
    }
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
    if (what.rfind("dlg:", 0) == 0) { scriptDlg = atoi(what.c_str() + 4); return; }   // a dialog by its number, with its starting values
    if (what.rfind("size:", 0) == 0) { int w = 0, h = 0, c = 0; sscanf(what.c_str() + 5, "%d,%d,%d", &w, &h, &c); scriptDlg = c ? D_CANVAS : D_IMGSIZE; scriptSize[0] = w; scriptSize[1] = h; return; }   // Image (or Canvas) Size to w x h, as its OK does
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
        Toolbar::select(Toolbar::BRUSH); syncTool();
    }
    if (what == "heal") {   // tests: Spot Healing on a stroke, Edit > Content-Aware Fill on a box, a Patch, a Content-Aware Move
        auto ms = [](std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
        auto t0 = std::chrono::steady_clock::now();
        Toolbar::select(Toolbar::SPOT_HEALING); syncTool();
        healPts = {doc.w * 0.2f, doc.h * 0.5f, doc.w * 0.3f, doc.h * 0.55f};
        healStrokeEnd();
        fprintf(stderr, "heal: spot %.0f ms\n", ms(t0)); t0 = std::chrono::steady_clock::now();
        combineSel(doc, shapeRect(doc, (int)(doc.w * 0.6f), (int)(doc.h * 0.1f), (int)(doc.w * 0.75f), (int)(doc.h * 0.25f)), SEL_NEW);
        contentAwareFillSelection();
        fprintf(stderr, "heal: content-aware fill %.0f ms\n", ms(t0)); t0 = std::chrono::steady_clock::now();
        combineSel(doc, shapeEllipse(doc, (int)(doc.w * 0.1f), (int)(doc.h * 0.75f), (int)(doc.w * 0.2f), (int)(doc.h * 0.9f)), SEL_NEW);
        remember("Patch"); healRegion(doc, doc.sel, (int)(doc.w * 0.15f), 0);
        fprintf(stderr, "heal: patch %.0f ms\n", ms(t0)); t0 = std::chrono::steady_clock::now();
        combineSel(doc, shapeEllipse(doc, (int)(doc.w * 0.45f), (int)(doc.h * 0.4f), (int)(doc.w * 0.55f), (int)(doc.h * 0.6f)), SEL_NEW);
        remember("Content-Aware Move"); contentAwareMove(doc, (int)(doc.w * 0.12f), 0);
        fprintf(stderr, "heal: content-aware move %.0f ms\n", ms(t0));
        changed(); selChanged();
    }
    if (what == "inoculant-save" || what == "inoculant-cure") {   // tests: the dialog, open (no run)
        if (what == "inoculant-save") inoculantSave();
        else { inMode = IN_CURE; inAsk = true; inSrc = doc.path; if (inCheck.start(InoculantPlug::inspectArgs(doc.path), Changeling::workDir())) inChecking = true; }
        fprintf(stderr, "inoculant: %s\n", InoculantPlug::cli().c_str());
    }
    if (what.rfind("ico:", 0) == 0) {   // tests: File > Export ICO to a file
        std::string err; const bool ok = saveImage(doc, what.substr(4), err);
        fprintf(stderr, "ico: %s %s\n", ok ? "saved" : "failed", err.c_str());
    }
    if (what == "pen") {   // tests: a Pen path (corners and a curve), a Curvature path, split, a shape and a selection
        Toolbar::select(Toolbar::PEN); syncTool();
        const float W = (float)doc.w, H = (float)doc.h;
        Subpath a; for (auto [x, y] : std::vector<std::pair<float, float>>{{0.1f, 0.2f}, {0.3f, 0.15f}, {0.35f, 0.45f}, {0.12f, 0.5f}}) { Knot k; k.ax = k.ix = k.ox = W * x; k.ay = k.iy = k.oy = H * y; a.knots.push_back(k); }
        a.knots[1].ox = W * 0.38f; a.knots[1].oy = H * 0.2f; a.knots[1].ix = W * 0.22f; a.knots[1].iy = H * 0.1f; a.knots[1].smooth = true;
        a.closed = true; doc.workPath.push_back(a);
        Subpath b; for (auto [x, y] : std::vector<std::pair<float, float>>{{0.6f, 0.2f}, {0.75f, 0.35f}, {0.9f, 0.2f}, {0.75f, 0.6f}}) { Knot k; k.ax = W * x; k.ay = H * y; b.knots.push_back(k); }
        b.closed = true; smoothAll(b); doc.workPath.push_back(b);
        penSplit(doc.workPath[1], 0, 0.5f);
        Shape sh; sh.kind = SHAPE_PATH; sh.path = {doc.workPath[1]}; sh.strokeOn = true; sh.strokeWidth = 4; sh.stroke[0] = sh.stroke[1] = sh.stroke[2] = 1;
        Fill f; f.kind = FILL_SOLID; f.colour[0] = 0; f.colour[1] = 0.48f; f.colour[2] = 0.72f;
        addShapeLayer(doc, sh, f);
        combineSel(doc, pathSelection(), SEL_NEW); selChanged();
        fprintf(stderr, "pen: %d subpaths, %d knots in the second; shape layer %s\n", (int)doc.workPath.size(), (int)doc.workPath[1].knots.size(), doc.cur().name.c_str());
        changed();
    }
    if (what == "type-rich-runs") {
        doc=newDoc(520,400,false);hist=History();Text text;text.str="Small Large\nNormal leading\nExpanded leading\nAfter spacing";text.font=defaultFont();text.size=42;text.x=28;text.y=40;
        TextStyle small;small.font=text.font;small.size=28;small.leading=text.leading;
        TextStyle large=small;large.size=64;large.tracking=80;
        TextStyle normal=small;normal.size=34;
        TextStyle expanded=normal;expanded.leading=2.4f;
        TextStyle transformed=normal;transformed.horizontalScale=1.35f;transformed.verticalScale=1.2f;transformed.baselineShift=8;
        text.runs={{6,small},{6,large},{15,normal},{17,expanded},{13,transformed}};Fill fill;fill.kind=FILL_SOLID;for(int k=0;k<3;++k)fill.colour[k]=fg[k]/255.0f;
        addTextLayer(doc,text,fill);
        Text effects;effects.str="Bold italic underline strike";effects.font=defaultFont();effects.size=32;effects.x=28;effects.y=270;
        TextStyle effect;effect.font=effects.font;effect.size=effects.size;effect.leading=effects.leading;
        TextStyle bold=effect;bold.bold=true;TextStyle italic=effect;italic.italic=true;TextStyle underline=effect;underline.underline=true;TextStyle strike=effect;strike.strikethrough=true;
        effects.runs={{5,bold},{7,italic},{10,underline},{6,strike}};addTextLayer(doc,effects,fill);Toolbar::select(Toolbar::TYPE_H);syncTool();fitNext=true;
        std::vector<float> coverage;float box[4];const bool rendered=textCoverage(doc.cur().text,doc.w,doc.h,coverage,box,doc.dpi);
        fprintf(stderr,"type-rich-runs: %s; per-run font/size/tracking/leading/scale/baseline/weight/italic/decorations\n",rendered?"rendered":"failed");return;
    }
    if (what == "type-defaults") {
        doc=newDoc(520,320,false);hist=History();typeNext=Text();typeNext.font=defaultFont();typeNext.size=42;
        const bool paragraph=typeSetParagraphStyle(typeNext,0,0,1,18)&&typeSetParagraphStyle(typeNext,0,0,4,12);
        Toolbar::select(Toolbar::TYPE_H);syncTool();fitNext=true;
        fprintf(stderr,"type-defaults: %s; Character and Paragraph defaults are editable without a text layer\n",paragraph?"passed":"failed");return;
    }
    if (what == "type-paragraph-alignment") {
        doc=newDoc(520,280,false);hist=History();Text text;text.str="Justified words fill a paragraph line across its text box width\nCentered paragraph\nRight aligned\nLeft aligned";text.font=defaultFont();text.size=34;text.x=30;text.y=18;text.boxWidth=460;text.boxHeight=240;
        ParagraphStyle justified;justified.align=3;justified.spaceAfter=8;ParagraphStyle centered;centered.align=1;
        ParagraphStyle right;right.align=2;ParagraphStyle left;left.startIndent=12;left.firstIndent=10;
        const size_t first=text.str.find('\n')+1,second=text.str.find('\n',first)+1,third=text.str.find('\n',second)+1;
        text.paragraphs={{first,justified},{second-first,centered},{third-second,right},{text.str.size()-third,left}};
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,text,fill);
        Toolbar::select(Toolbar::TYPE_H);syncTool();fitNext=true;
        std::vector<float> coverage;float box[4];const bool rendered=textCoverage(doc.cur().text,doc.w,doc.h,coverage,box,doc.dpi);
        fprintf(stderr,"type-paragraph-alignment: %s; justified, centered, right and left paragraph runs\n",rendered&&text.paragraphs.size()==4?"passed":"failed");return;
    }
    if (what == "type-tabs") {
        doc=newDoc(520,320,false);hist=History();Text tabs;tabs.str="Name\tValue\nA\tLonger\tB";tabs.font=defaultFont();tabs.size=36;tabs.x=36;tabs.y=46;tabs.boxWidth=260;tabs.boxHeight=200;
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,tabs,fill);Toolbar::select(Toolbar::TYPE_H);syncTool();fitNext=true;
        std::vector<float> coverage;float box[4];const bool rendered=textCoverage(doc.cur().text,doc.w,doc.h,coverage,box,doc.dpi);
        const bool preserved=doc.cur().text.str=="Name\tValue\nA\tLonger\tB"&&textAdvance(doc.cur().text)>36;
        fprintf(stderr,"type-tabs: %s; editable tab characters advance to 36 pt stops and wrapped text renders\n",rendered&&preserved?"passed":"failed");return;
    }
    if (what == "type-size-selection") {
        doc=newDoc(520,320,false);hist=History();Text sample;sample.str="Hello world";sample.font=defaultFont();sample.size=30;sample.x=36;sample.y=70;
        TextStyle first;first.font=sample.font;first.size=30;TextStyle second=first;second.size=20;sample.runs={{6,first},{5,second}};
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,sample,fill);
        Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=doc.active;typeMaskEditing=false;typeStyleSelectionStart=6;typeStyleSelectionEnd=11;typeStyleSelectionLayer=typeLayer;typeStyleSelectionValid=true;
        Text updated=doc.cur().text;const bool applied=typeSetCharacterSize(updated,6,11,42);if(applied){doc.cur().text=std::move(updated);renderFill(doc,doc.cur());changed();}
        const auto styles=doc.cur().text.characterStyles();std::vector<float> coverage;float box[4];const bool rendered=textCoverage(doc.cur().text,doc.w,doc.h,coverage,box,doc.dpi);
        const bool selected=styles.size()==11&&styles[0].size==30&&styles[5].size==30&&styles[6].size==42&&styles[10].size==42;
        fitNext=true;fprintf(stderr,"type-size-selection: %s (apply=%d selected=%d rendered=%d sizes=%g,%g,%g); changing the selected run preserves the other characters\n",applied&&selected&&rendered?"passed":"failed",applied,selected,rendered,styles.empty()?0:styles[0].size,styles.size()>6?styles[6].size:0,styles.size()>10?styles[10].size:0);return;
    }
    if (what == "type-tracking-selection") {
        doc=newDoc(520,320,false);hist=History();Text sample;sample.str="Hello world";sample.font=defaultFont();sample.size=32;sample.x=36;sample.y=70;
        TextStyle first;first.font=sample.font;first.size=32;first.tracking=-10;TextStyle second=first;second.tracking=0;sample.runs={{6,first},{5,second}};
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,sample,fill);
        Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=doc.active;typeMaskEditing=false;typeStyleSelectionStart=6;typeStyleSelectionEnd=11;typeStyleSelectionLayer=typeLayer;typeStyleSelectionValid=true;
        Text updated=doc.cur().text;const bool applied=typeSetCharacterTracking(updated,6,11,160);if(applied){doc.cur().text=std::move(updated);renderFill(doc,doc.cur());changed();}
        const auto styles=doc.cur().text.characterStyles();std::vector<float> coverage;float box[4];const bool rendered=textCoverage(doc.cur().text,doc.w,doc.h,coverage,box,doc.dpi);
        const bool selected=styles.size()==11&&styles[0].tracking==-10&&styles[5].tracking==-10&&styles[6].tracking==160&&styles[10].tracking==160;
        fitNext=true;fprintf(stderr,"type-tracking-selection: %s; selected tracking changes while the leading run stays intact\n",applied&&selected&&rendered?"passed":"failed");return;
    }
    if (what == "type-font-selection") {
        doc=newDoc(520,320,false);hist=History();const std::string base=defaultFont();std::string alternate=base;
        for(const FontFile& font:systemFonts())if(font.path!=base){alternate=font.path;break;}
        Text sample;sample.str="Hello world";sample.font=base;sample.size=32;sample.x=36;sample.y=70;
        TextStyle first;first.font=base;first.size=32;TextStyle second=first;sample.runs={{6,first},{5,second}};
        Fill fill;fill.kind=FILL_SOLID;for(int i=0;i<3;++i)fill.colour[i]=fg[i]/255.0f;addTextLayer(doc,sample,fill);
        Toolbar::select(Toolbar::TYPE_H);syncTool();typeLayer=doc.active;typeMaskEditing=false;typeStyleSelectionStart=6;typeStyleSelectionEnd=11;typeStyleSelectionLayer=typeLayer;typeStyleSelectionValid=true;
        Text updated=doc.cur().text;const bool applied=alternate!=base&&typeSetCharacterFont(updated,6,11,alternate);if(applied){doc.cur().text=std::move(updated);renderFill(doc,doc.cur());changed();}
        const auto styles=doc.cur().text.characterStyles();std::vector<float> coverage;float box[4];const bool rendered=textCoverage(doc.cur().text,doc.w,doc.h,coverage,box,doc.dpi);
        const bool selected=styles.size()==11&&styles[0].font==base&&styles[5].font==base&&styles[6].font==alternate&&styles[10].font==alternate;
        fitNext=true;fprintf(stderr,"type-font-selection: %s; selected font changes while the other run stays intact\n",applied&&selected&&rendered?"passed":"failed");return;
    }
    if (what == "text") {   // tests: horizontal and vertical text layers, centred, a Type Mask selection
        Toolbar::select(Toolbar::TYPE_H); syncTool();
        Text t; t.str = "Chimera\nText layers"; t.size = 72; t.x = doc.w * 0.5f; t.y = doc.h * 0.08f; t.align = 1;
        Fill f; f.kind = FILL_SOLID; f.colour[0] = 1; f.colour[1] = 1; f.colour[2] = 1;
        addTextLayer(doc, t, f);
        Text v; v.str = "VERTICAL"; v.size = 40; v.vertical = true; v.x = doc.w * 0.9f; v.y = doc.h * 0.1f;
        for (const auto& ff : systemFonts()) if (ff.name == "Georgia") v.font = ff.path;
        Fill g; g.kind = FILL_GRADIENT; g.colour[0] = 0; g.colour[1] = 0.48f; g.colour[2] = 0.72f;
        addTextLayer(doc, v, g);
        typeMask = Text(); typeMask.str = "MASK"; typeMask.size = 120; typeMask.x = doc.w * 0.05f; typeMask.y = doc.h * 0.7f;
        snprintf(typeBuf, sizeof typeBuf, "MASK"); typeMaskEditing = true; typeCommit();
        fprintf(stderr, "text: %d layers, %d fonts, box %.0f %.0f %.0f %.0f\n", (int)doc.layers.size(), (int)systemFonts().size(), doc.layers[1].text.box[0], doc.layers[1].text.box[1], doc.layers[1].text.box[2], doc.layers[1].text.box[3]);
        doc.active = 1; changed();
    }
    if (what == "brushes") {   // tests: each new brush along a line, as a mouse would
        const int ids[] = {Toolbar::PATTERN_STAMP, Toolbar::HISTORY_BRUSH, Toolbar::ART_HISTORY_BRUSH, Toolbar::COLOR_REPLACEMENT, Toolbar::MIXER_BRUSH};
        fg[0] = 220; fg[1] = 40; fg[2] = 120;
        for (int i = 0; i < 5; ++i) {
            Toolbar::select(ids[i]); syncTool();
            if (i == 1) { remember("x"); Work o = getWork(doc), r = o; adjInvert(r); putWork(doc, o, r); }   // something to paint back from
            const float y = doc.h * (0.15f + 0.17f * i);
            RetouchOpts op = retouchOpts();
            strokeBegin(doc, stroke);
            for (int k = 0; k <= 20; ++k) retouchTo(doc, stroke, doc.w * (0.15f + 0.035f * k), y, 70, 0.5f, op, k == 0);
            strokeEnd(stroke);
        }
        fprintf(stderr, "brushes: done\n");
        changed();
    }
    if (what == "marks") {   // tests: samplers, counts, a note and a ruler, then the Ruler tool's bar
        for (int i = 0; i < 3; ++i) { Doc::Mark k; k.x = doc.w * (0.3f + 0.2f * i); k.y = doc.h * 0.4f; doc.samplers.push_back(k); }
        for (int i = 0; i < 4; ++i) { Doc::Mark k; k.x = doc.w * (0.2f + 0.15f * i); k.y = doc.h * 0.8f; doc.counts.push_back(k); }
        Doc::Mark n; n.x = doc.w * 0.8f; n.y = doc.h * 0.2f; n.text = "Fix the sky here"; doc.notes.push_back(n);
        doc.hasRuler = true; doc.ruler[0] = doc.w * 0.1f; doc.ruler[1] = doc.h * 0.3f; doc.ruler[2] = doc.w * 0.45f; doc.ruler[3] = doc.h * 0.2f;
        Toolbar::select(what == "marks" ? Toolbar::RULER : Toolbar::COLOR_SAMPLER); syncTool();
        fprintf(stderr, "marks: ruler angle %.1f\n", rulerAngle());
    }
    if (what == "samplers") { Doc::Mark k; k.x = doc.w * 0.5f; k.y = doc.h * 0.5f; doc.samplers.push_back(k); k.x = doc.w * 0.2f; doc.samplers.push_back(k); Toolbar::select(Toolbar::COLOR_SAMPLER); syncTool(); }
    if (what == "shapes") {   // tests: every shape, drawn as a drag would, with strokes and corners
        Toolbar::select(Toolbar::STAR); syncTool();
        const float W = (float)doc.w, H = (float)doc.h;
        const int kinds[] = {SHAPE_RECT, SHAPE_ELLIPSE, SHAPE_TRIANGLE, SHAPE_POLYGON, SHAPE_STAR, SHAPE_LINE};
        for (int i = 0; i < 6; ++i) {
            Shape sh = shapeNext; sh.kind = kinds[i];
            const float cx = W * (0.12f + 0.15f * i), cy = H * 0.5f, r = std::min(W, H) * 0.12f;
            sh.x0 = cx - r; sh.y0 = cy - r; sh.x1 = cx + r; sh.y1 = cy + r;
            if (i == 0) sh.radius = r * 0.3f;
            if (i == 3) sh.sides = 6;
            if (i == 4) { sh.sides = 5; sh.inner = 0.45f; sh.radius = 3; }
            if (i == 5) { sh.y0 = cy + r; sh.y1 = cy - r; sh.weight = 8; }
            sh.strokeOn = i % 2 == 1; sh.strokeWidth = 6; sh.stroke[0] = 1; sh.stroke[1] = 1; sh.stroke[2] = 1;
            Fill f; f.kind = i == 2 ? FILL_GRADIENT : FILL_SOLID; f.colour[0] = 0; f.colour[1] = 0.482f; f.colour[2] = 0.718f;
            addShapeLayer(doc, sh, f);
        }
        fprintf(stderr, "shapes: %d layers\n", (int)doc.layers.size());
        changed();
    }
    if (what == "redeye") {   // tests: a click in the middle of the picture with the Red Eye tool
        const int n = redEye(doc, doc.w / 2, doc.h / 2, 50, 50);
        const uint8_t* p = doc.cur().img.at(doc.w / 2, doc.h / 2);
        fprintf(stderr, "redeye: %d px changed, centre now %d %d %d\n", n, p[0], p[1], p[2]);
        changed();
    }
    if (what == "quicksel") {   // tests: one click of a 30 px Quick Selection brush in the middle of the picture
        const std::vector<float> pts = {doc.w / 2.0f, doc.h / 2.0f};
        auto t0 = std::chrono::steady_clock::now();
        const std::vector<uint8_t> r = QuickSel::select(sampleImage(), pts, 30);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        size_t n = 0; for (uint8_t v : r) n += v > 127;
        fprintf(stderr, "quicksel: %zu px selected (%.0f ms)\n", n, ms);
        if (!r.empty()) { combineSel(doc, r, SEL_NEW); selChanged(); }
    }
    if (what == "tools") {   // tests: the retouching brushes, the gradient, the magic eraser and the crop, driven as a mouse would
        auto strokeLine = [&](int mode, float x0, float y0, float x1, float y1, float size, float strength) {
            RetouchOpts o; o.mode = mode; o.strength = strength; o.srcDX = -doc.w * 0.3f; o.srcDY = 0;
            strokeBegin(doc, stroke);
            for (int k = 0; k <= 40; ++k) retouchTo(doc, stroke, x0 + (x1 - x0) * k / 40, y0 + (y1 - y0) * k / 40, size, 0.4f, o, k == 0);
            strokeEnd(stroke);
        };
        const float W = (float)doc.w, H = (float)doc.h;
        strokeLine(RT_BLUR, W * 0.1f, H * 0.8f, W * 0.4f, H * 0.8f, 80, 1.0f);
        strokeLine(RT_DODGE, W * 0.55f, H * 0.2f, W * 0.9f, H * 0.2f, 90, 0.8f);
        strokeLine(RT_BURN, W * 0.55f, H * 0.35f, W * 0.9f, H * 0.35f, 90, 0.8f);
        strokeLine(RT_SPONGE_DESAT, W * 0.55f, H * 0.5f, W * 0.9f, H * 0.5f, 90, 1.0f);
        strokeLine(RT_CLONE, W * 0.7f, H * 0.7f, W * 0.8f, H * 0.7f, 90, 1.0f);
        addLayer(doc, emptyLayer(doc, "Gradient"));
        const uint8_t from[3] = {0, 123, 183}, to[3] = {255, 210, 80};
        combineSel(doc, shapeRect(doc, (int)(W * 0.05f), (int)(H * 0.05f), (int)(W * 0.35f), (int)(H * 0.35f)), SEL_NEW);
        gradientFill(doc, W * 0.05f, H * 0.05f, W * 0.35f, H * 0.35f, from, to, GRAD_DIAMOND, false, 1.0f);
        deselect();
        magicErase(doc, (int)(W * 0.2f), (int)(H * 0.2f), 20, true, false, 1.0f);   // a hole in its middle
        cropCanvas(doc, Rect{(int)(W * 0.02f), (int)(H * 0.02f), (int)(W * 0.98f), (int)(H * 0.98f)}, true);
        fprintf(stderr, "tools: %d x %d, %d layers\n", doc.w, doc.h, (int)doc.layers.size());
        changed(); fitNext = true;
    }
    if (what == "clipfill") {   // tests: a gradient fill clipped to an ellipse, saved as a project and opened again
        addLayer(doc, emptyLayer(doc, "Shape"));
        combineSel(doc, shapeEllipse(doc, doc.w / 4, doc.h / 6, doc.w / 2, doc.h * 2 / 3), SEL_NEW);
        const uint8_t white[4] = {255, 255, 255, 255};
        fillSelection(doc, white, 1.0f, BM_Normal);
        deselect();
        Fill f; f.kind = FILL_GRADIENT; f.style = GRAD_RADIAL; f.colour[0] = 0; f.colour[1] = 0.482f; f.colour[2] = 0.718f; f.colour2[0] = 0.95f; f.colour2[1] = 0.85f; f.colour2[2] = 0.3f;
        addFillLayer(doc, f);
        doc.cur().clip = true;
        std::string err;
        const std::string tmp = (std::filesystem::temp_directory_path() / "chimera_clipfill.chimera").string();
        Doc back;
        if (!saveProject(doc, tmp, err) || !openDoc(tmp, back, err)) { fprintf(stderr, "clipfill: %s\n", err.c_str()); return; }
        fprintf(stderr, "clipfill: %d layers back, top clip=%d fill=%d style=%d\n", (int)back.layers.size(), back.layers.back().clip ? 1 : 0, back.layers.back().fill.kind, back.layers.back().fill.style);
        changed();
    }
    if (what == "ops") {
        FILE* tl = fopen("chimera_timing.txt", "w");
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
